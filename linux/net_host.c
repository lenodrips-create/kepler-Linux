/* The network API the shell's HTTPS client and Terminal use (net/net.h),
 * on Linux sockets instead of the kernel's own e1000 driver and TCP/IP. */

#define _GNU_SOURCE
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/random.h>
#include <sys/socket.h>
#include <unistd.h>

#include "host.h"
#include "net.h"

/* ---- the adapter --------------------------------------------------------------------------- */

static char if_name[IF_NAMESIZE];
static char if_label[64];

/* the first interface that is up, is not loopback and has an IPv4 address */
static uint32_t find_ip(void)
{
    struct ifaddrs *list, *a;
    uint32_t ip = 0;
    if (getifaddrs(&list))
        return 0;
    for (a = list; a; a = a->ifa_next) {
        if (!a->ifa_addr || a->ifa_addr->sa_family != AF_INET || (a->ifa_flags & IFF_LOOPBACK) || !(a->ifa_flags & IFF_UP))
            continue;
        ip = ntohl(((struct sockaddr_in *)a->ifa_addr)->sin_addr.s_addr);
        strlcpy(if_name, a->ifa_name, sizeof if_name);
        break;
    }
    freeifaddrs(list);
    return ip;
}

void net_init(void) {}
void net_poll(void) {}

bool net_has_adapter(void)
{
    struct if_nameindex *ifs = if_nameindex();
    bool any = false;
    for (struct if_nameindex *i = ifs; i && i->if_index; i++)
        if (strcmp(i->if_name, "lo"))
            any = true;
    if (ifs)
        if_freenameindex(ifs);
    return any;
}

uint32_t net_ip(void) { return find_ip(); }
bool net_ready(void) { return find_ip() != 0; }

const char *net_status(void)
{
    static char s[64];
    uint32_t ip = find_ip();
    if (ip) {
        char a[20];
        net_ip_str(ip, a, sizeof a);
        ksnprintf(s, sizeof s, "Connected (%s)", a);
    } else {
        strlcpy(s, net_has_adapter() ? "Waiting for an address..." : "No network adapter", sizeof s);
    }
    return s;
}

void net_ip_str(uint32_t ip, char *out, int n) { ksnprintf(out, n, "%u.%u.%u.%u", ip >> 24, (ip >> 16) & 255, (ip >> 8) & 255, ip & 255); }

const char *e1000_name(void)
{
    find_ip();
    ksnprintf(if_label, sizeof if_label, "%s", if_name[0] ? if_name : "network adapter");
    return if_label;
}

/* ---- DNS: getaddrinfo on a helper thread, polled like the kernel's resolver ------------------- */

typedef struct {
    char name[256];
    volatile int state; /* DNS_PENDING, DNS_DONE, DNS_FAILED */
    uint32_t ip;
    uint32_t used;
} lookup_t;

static lookup_t lookups[8];
static pthread_mutex_t dns_lock = PTHREAD_MUTEX_INITIALIZER;

static void *resolve_thread(void *arg)
{
    lookup_t *l = arg;
    struct addrinfo hints = { 0 }, *res;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    char name[256];
    pthread_mutex_lock(&dns_lock);
    strlcpy(name, l->name, sizeof name);
    pthread_mutex_unlock(&dns_lock);
    int r = getaddrinfo(name, NULL, &hints, &res);
    pthread_mutex_lock(&dns_lock);
    if (r == 0 && res) {
        l->ip = ntohl(((struct sockaddr_in *)res->ai_addr)->sin_addr.s_addr);
        l->state = DNS_DONE;
        freeaddrinfo(res);
    } else {
        l->state = DNS_FAILED;
    }
    pthread_mutex_unlock(&dns_lock);
    return NULL;
}

int dns_resolve(const char *name, uint32_t *ip)
{
    struct in_addr a;
    if (inet_aton(name, &a)) {
        *ip = ntohl(a.s_addr);
        return DNS_DONE;
    }
    pthread_mutex_lock(&dns_lock);
    lookup_t *found = NULL, *oldest = &lookups[0];
    for (int i = 0; i < ARRAY_LEN(lookups); i++) {
        if (!strcasecmp(lookups[i].name, name))
            found = &lookups[i];
        if (lookups[i].used < oldest->used && lookups[i].state != DNS_PENDING)
            oldest = &lookups[i];
    }
    int state;
    if (found) {
        state = found->state;
        *ip = found->ip;
        found->used = timer_ms();
        /* a failure is only remembered until it is reported */
        if (state == DNS_FAILED)
            found->name[0] = 0;
        pthread_mutex_unlock(&dns_lock);
        return state;
    }
    strlcpy(oldest->name, name, sizeof oldest->name);
    oldest->state = DNS_PENDING;
    oldest->used = timer_ms();
    pthread_mutex_unlock(&dns_lock);
    pthread_t t;
    if (pthread_create(&t, NULL, resolve_thread, oldest) == 0)
        pthread_detach(t);
    else
        oldest->state = DNS_FAILED;
    return DNS_PENDING;
}

/* ---- TCP ---------------------------------------------------------------------------------------- */

struct tcp {
    int fd;
    int state;
    char error[96];
};

tcp_t *tcp_open(uint32_t ip, uint16_t port)
{
    tcp_t *t = kzalloc(sizeof *t);
    t->fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    struct sockaddr_in sa = { 0 };
    sa.sin_family = AF_INET;
    sa.sin_port = htons(port);
    sa.sin_addr.s_addr = htonl(ip);
    t->state = TCP_CONNECTING;
    if (t->fd < 0 || (connect(t->fd, (struct sockaddr *)&sa, sizeof sa) && errno != EINPROGRESS)) {
        t->state = TCP_FAILED;
        strlcpy(t->error, "Kepler can't connect to the server.", sizeof t->error);
    }
    return t;
}

int tcp_status(tcp_t *t)
{
    if (t->state == TCP_CONNECTING) {
        int err = 0;
        socklen_t len = sizeof err;
        struct sockaddr_in peer;
        socklen_t plen = sizeof peer;
        if (getpeername(t->fd, (struct sockaddr *)&peer, &plen) == 0) {
            t->state = TCP_OPEN;
        } else if (getsockopt(t->fd, SOL_SOCKET, SO_ERROR, &err, &len) == 0 && err && err != EINPROGRESS) {
            t->state = TCP_FAILED;
            strlcpy(t->error, err == ECONNREFUSED ? "The server refused the connection." : "Kepler can't connect to the server.",
                    sizeof t->error);
        }
    }
    return t->state;
}

const char *tcp_error(tcp_t *t) { return t->error[0] ? t->error : "The connection failed."; }

int tcp_write(tcp_t *t, const void *data, int n)
{
    if (tcp_status(t) != TCP_OPEN)
        return 0;
    ssize_t r = send(t->fd, data, n, MSG_NOSIGNAL);
    if (r < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            t->state = TCP_FAILED;
            strlcpy(t->error, "The connection was reset.", sizeof t->error);
        }
        return 0;
    }
    return (int)r;
}

int tcp_read(tcp_t *t, void *buf, int n)
{
    int st = tcp_status(t);
    if (st != TCP_OPEN)
        return 0;
    ssize_t r = recv(t->fd, buf, n, 0);
    if (r > 0)
        return (int)r;
    if (r == 0)
        t->state = TCP_PEER_CLOSED;
    else if (errno != EAGAIN && errno != EWOULDBLOCK) {
        t->state = TCP_FAILED;
        strlcpy(t->error, "The connection was reset.", sizeof t->error);
    }
    return 0;
}

void tcp_close(tcp_t *t)
{
    if (!t)
        return;
    if (t->fd >= 0)
        close(t->fd);
    kfree(t);
}

void tcp_input(uint32_t src, const uint8_t *seg, int len) { (void)src, (void)seg, (void)len; }
void tcp_poll(void) {}

/* ---- randomness: the kernel's pool, topped up from the OS ---------------------------------------- */

void host_entropy_init(void)
{
    uint8_t seed[48];
    if (getrandom(seed, sizeof seed, 0) == (ssize_t)sizeof seed)
        entropy_add(seed, sizeof seed);
}
