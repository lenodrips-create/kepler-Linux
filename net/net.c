/* Ethernet, ARP, IPv4, ICMP echo, UDP, DHCP and DNS. Everything is polled
 * from the main loop through net_poll(). */

#include "net.h"

#define ETH_ARP  0x0806
#define ETH_IPV4 0x0800
#define PROTO_ICMP 1
#define PROTO_TCP  6
#define PROTO_UDP  17

#define DHCP_CLIENT 68
#define DHCP_SERVER 67
#define DNS_PORT    53
#define DNS_LOCAL   51053

static bool adapter;
static uint8_t mac[6];
static uint32_t my_ip, netmask, gateway, dns_server;
static uint16_t ip_id;
static uint8_t frame[1600];
static char status[64] = "Not started";

static const uint8_t broadcast_mac[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

bool net_has_adapter(void) { return adapter; }
bool net_ready(void) { return my_ip != 0; }
uint32_t net_ip(void) { return my_ip; }
const char *net_status(void) { return status; }

void net_ip_str(uint32_t ip, char *out, int n)
{
    ksnprintf(out, n, "%u.%u.%u.%u", ip >> 24, (ip >> 16) & 0xFF, (ip >> 8) & 0xFF, ip & 0xFF);
}

/* ---- checksums ----------------------------------------------------------------- */

uint32_t net_sum(const void *data, int len, uint32_t sum)
{
    const uint8_t *p = data;
    while (len > 1) {
        sum += (uint32_t)p[0] << 8 | p[1];
        p += 2;
        len -= 2;
    }
    if (len)
        sum += (uint32_t)p[0] << 8;
    return sum;
}

uint16_t net_fold(uint32_t sum)
{
    while (sum >> 16)
        sum = (sum & 0xFFFF) + (sum >> 16);
    return (uint16_t)~sum;
}

uint32_t net_pseudo(uint32_t src, uint32_t dst, uint8_t proto, int len)
{
    return (src >> 16) + (src & 0xFFFF) + (dst >> 16) + (dst & 0xFFFF) + proto + (uint32_t)len;
}

/* ---- Ethernet and ARP ------------------------------------------------------------- */

static void eth_send(const uint8_t *dst, uint16_t type, const void *payload, int len)
{
    if (len > 1500)
        return;
    memcpy(frame, dst, 6);
    memcpy(frame + 6, mac, 6);
    put16(frame + 12, type);
    memcpy(frame + 14, payload, len);
    int total = 14 + len;
    if (total < 60) {
        memset(frame + total, 0, 60 - total);
        total = 60;
    }
    e1000_send(frame, total);
}

#define ARP_SLOTS 16

static struct {
    uint32_t ip;
    uint8_t mac[6];
    bool known;
    uint32_t asked;
    uint8_t *pending; /* one IP packet waiting for this address */
    int pending_len;
} arp[ARP_SLOTS];

static void arp_send(uint16_t op, const uint8_t *tmac, uint32_t tip)
{
    uint8_t p[28];
    put16(p, 1);
    put16(p + 2, ETH_IPV4);
    p[4] = 6;
    p[5] = 4;
    put16(p + 6, op);
    memcpy(p + 8, mac, 6);
    put32(p + 14, my_ip);
    memcpy(p + 18, tmac, 6);
    put32(p + 24, tip);
    eth_send(op == 1 ? broadcast_mac : tmac, ETH_ARP, p, 28);
}

static int arp_slot(uint32_t ip, bool create)
{
    for (int i = 0; i < ARP_SLOTS; i++)
        if (arp[i].ip == ip)
            return i;
    if (!create)
        return -1;
    for (int i = 0; i < ARP_SLOTS; i++)
        if (!arp[i].ip)
            return i;
    int victim = krand() % ARP_SLOTS;
    kfree(arp[victim].pending);
    memset(&arp[victim], 0, sizeof arp[victim]);
    return victim;
}

static void arp_input(const uint8_t *p, int len)
{
    if (len < 28 || be16(p) != 1 || be16(p + 2) != ETH_IPV4)
        return;
    uint16_t op = be16(p + 6);
    uint32_t sip = be32(p + 14), tip = be32(p + 24);
    if (sip) {
        int i = arp_slot(sip, tip == my_ip && my_ip);
        if (i >= 0) {
            arp[i].ip = sip;
            memcpy(arp[i].mac, p + 8, 6);
            arp[i].known = true;
            if (arp[i].pending) {
                eth_send(arp[i].mac, ETH_IPV4, arp[i].pending, arp[i].pending_len);
                kfree(arp[i].pending);
                arp[i].pending = NULL;
            }
        }
    }
    if (op == 1 && my_ip && tip == my_ip)
        arp_send(2, p + 8, sip);
}

/* ---- IPv4 ------------------------------------------------------------------------------ */

bool net_send_ip(uint32_t dst, uint8_t proto, const void *payload, int len)
{
    static uint8_t pkt[1500];
    if (!adapter || len > 1480)
        return false;
    uint8_t *h = pkt;
    h[0] = 0x45;
    h[1] = 0;
    put16(h + 2, 20 + len);
    put16(h + 4, ip_id++);
    put16(h + 6, 0x4000); /* don't fragment */
    h[8] = 64;
    h[9] = proto;
    put16(h + 10, 0);
    put32(h + 12, my_ip);
    put32(h + 16, dst);
    put16(h + 10, net_fold(net_sum(h, 20, 0)));
    memcpy(pkt + 20, payload, len);
    int total = 20 + len;

    if (dst == 0xFFFFFFFF) {
        eth_send(broadcast_mac, ETH_IPV4, pkt, total);
        return true;
    }
    uint32_t hop = ((dst & netmask) == (my_ip & netmask)) ? dst : gateway;
    int i = arp_slot(hop, true);
    arp[i].ip = hop;
    if (arp[i].known) {
        eth_send(arp[i].mac, ETH_IPV4, pkt, total);
        return true;
    }
    /* keep the newest packet until the neighbour answers */
    kfree(arp[i].pending);
    arp[i].pending = kmalloc(total);
    memcpy(arp[i].pending, pkt, total);
    arp[i].pending_len = total;
    if (timer_ms() - arp[i].asked > 300 || !arp[i].asked) {
        arp[i].asked = timer_ms() | 1;
        arp_send(1, (const uint8_t[6]){ 0 }, hop);
    }
    return true;
}

static bool udp_send(uint32_t dst, uint16_t sport, uint16_t dport, const void *data, int len)
{
    uint8_t buf[1480];
    if (len > (int)sizeof buf - 8)
        return false;
    put16(buf, sport);
    put16(buf + 2, dport);
    put16(buf + 4, 8 + len);
    put16(buf + 6, 0);
    memcpy(buf + 8, data, len);
    uint16_t c = net_fold(net_sum(buf, 8 + len, net_pseudo(my_ip, dst, PROTO_UDP, 8 + len)));
    put16(buf + 6, c ? c : 0xFFFF);
    return net_send_ip(dst, PROTO_UDP, buf, 8 + len);
}

/* ---- DHCP ------------------------------------------------------------------------------------ */

enum { DHCP_OFF, DHCP_DISCOVER, DHCP_REQUEST, DHCP_BOUND };
static int dhcp_state;
static uint32_t dhcp_xid, dhcp_offer, dhcp_server_id, dhcp_sent;
static int dhcp_tries;

static void dhcp_send(uint8_t type)
{
    uint8_t p[300];
    memset(p, 0, sizeof p);
    p[0] = 1; /* request */
    p[1] = 1; /* Ethernet */
    p[2] = 6;
    put32(p + 4, dhcp_xid);
    put16(p + 10, 0x8000); /* answer by broadcast: we have no address yet */
    memcpy(p + 28, mac, 6);
    put32(p + 236, 0x63825363);
    uint8_t *o = p + 240;
    *o++ = 53, *o++ = 1, *o++ = type;
    if (type == 3) {
        *o++ = 50, *o++ = 4;
        put32(o, dhcp_offer);
        o += 4;
        *o++ = 54, *o++ = 4;
        put32(o, dhcp_server_id);
        o += 4;
    }
    *o++ = 12, *o++ = 8;
    memcpy(o, "keplerpc", 8);
    o += 8;
    *o++ = 55, *o++ = 3, *o++ = 1, *o++ = 3, *o++ = 6;
    *o++ = 255;
    uint32_t saved = my_ip;
    my_ip = 0;
    udp_send(0xFFFFFFFF, DHCP_CLIENT, DHCP_SERVER, p, (int)(o - p));
    my_ip = saved;
    dhcp_sent = timer_ms();
}

static void dhcp_start(void)
{
    dhcp_xid = krand();
    dhcp_state = DHCP_DISCOVER;
    dhcp_tries = 0;
    strlcpy(status, "Looking for a network...", sizeof status);
    dhcp_send(1);
}

static void dhcp_input(const uint8_t *p, int len)
{
    if (len < 240 || p[0] != 2 || be32(p + 4) != dhcp_xid || be32(p + 236) != 0x63825363)
        return;
    uint32_t yiaddr = be32(p + 16);
    int type = 0;
    uint32_t mask = 0xFFFFFF00, router = 0, dns = 0, server = 0;
    for (int i = 240; i < len;) {
        uint8_t opt = p[i];
        if (opt == 255)
            break;
        if (opt == 0) {
            i++;
            continue;
        }
        if (i + 1 >= len)
            break;
        int ol = p[i + 1];
        const uint8_t *v = p + i + 2;
        if (i + 2 + ol > len)
            break;
        if (opt == 53 && ol >= 1)
            type = v[0];
        else if (opt == 1 && ol >= 4)
            mask = be32(v);
        else if (opt == 3 && ol >= 4)
            router = be32(v);
        else if (opt == 6 && ol >= 4)
            dns = be32(v);
        else if (opt == 54 && ol >= 4)
            server = be32(v);
        i += 2 + ol;
    }
    if (type == 2 && dhcp_state == DHCP_DISCOVER) {
        dhcp_offer = yiaddr;
        dhcp_server_id = server;
        dhcp_state = DHCP_REQUEST;
        dhcp_tries = 0;
        dhcp_send(3);
    } else if (type == 5 && dhcp_state == DHCP_REQUEST) {
        my_ip = yiaddr;
        netmask = mask;
        gateway = router;
        dns_server = dns ? dns : router;
        dhcp_state = DHCP_BOUND;
        char a[16], g[16], d[16];
        net_ip_str(my_ip, a, sizeof a);
        net_ip_str(gateway, g, sizeof g);
        net_ip_str(dns_server, d, sizeof d);
        ksnprintf(status, sizeof status, "Connected (%s)", a);
        kprintf("net: address %s, gateway %s, dns %s\n", a, g, d);
    } else if (type == 6) {
        dhcp_start();
    }
}

/* ---- DNS --------------------------------------------------------------------------------------- */

#define DNS_SLOTS 32

static struct {
    char name[128];
    uint32_t ip;
    int state;
    uint16_t id;
    int tries;
    uint32_t sent, when;
} dns[DNS_SLOTS];

static bool parse_ipv4(const char *s, uint32_t *ip)
{
    uint32_t v = 0;
    for (int part = 0; part < 4; part++) {
        if (!kisdigit(*s))
            return false;
        int n = 0;
        while (kisdigit(*s))
            n = n * 10 + (*s++ - '0');
        if (n > 255)
            return false;
        v = v << 8 | n;
        if (part < 3 && *s++ != '.')
            return false;
    }
    if (*s)
        return false;
    *ip = v;
    return true;
}

static void dns_query(int i)
{
    uint8_t q[300];
    memset(q, 0, 12);
    put16(q, dns[i].id);
    put16(q + 2, 0x0100); /* recursion desired */
    put16(q + 4, 1);
    int o = 12;
    const char *s = dns[i].name;
    while (*s && o < 280) {
        const char *dot = strchr(s, '.');
        int n = dot ? (int)(dot - s) : (int)strlen(s);
        if (n > 63)
            n = 63;
        q[o++] = n;
        memcpy(q + o, s, n);
        o += n;
        s += n;
        if (*s == '.')
            s++;
    }
    q[o++] = 0;
    put16(q + o, 1); /* A */
    put16(q + o + 2, 1);
    o += 4;
    udp_send(dns_server, DNS_LOCAL, DNS_PORT, q, o);
    dns[i].sent = timer_ms();
    dns[i].tries++;
}

int dns_resolve(const char *name, uint32_t *ip)
{
    if (parse_ipv4(name, ip))
        return DNS_DONE;
    char lower[128];
    int n = 0;
    while (name[n] && n < 127) {
        lower[n] = ktolower(name[n]);
        n++;
    }
    lower[n] = 0;
    int free_slot = -1, oldest = 0;
    for (int i = 0; i < DNS_SLOTS; i++) {
        if (dns[i].name[0] && !strcmp(dns[i].name, lower)) {
            /* forget failures after a while so a retry can work */
            if (dns[i].state == DNS_FAILED && timer_ms() - dns[i].when > 15000) {
                dns[i].name[0] = 0;
                free_slot = i;
                break;
            }
            if (dns[i].state == DNS_DONE)
                *ip = dns[i].ip;
            return dns[i].state;
        }
        if (!dns[i].name[0] && free_slot < 0)
            free_slot = i;
        if (dns[i].when < dns[oldest].when)
            oldest = i;
    }
    if (!net_ready())
        return adapter ? DNS_PENDING : DNS_FAILED;
    int i = free_slot >= 0 ? free_slot : oldest;
    strlcpy(dns[i].name, lower, sizeof dns[i].name);
    dns[i].state = DNS_PENDING;
    dns[i].id = (uint16_t)krand();
    dns[i].tries = 0;
    dns[i].when = timer_ms();
    dns_query(i);
    return DNS_PENDING;
}

static int skip_name(const uint8_t *p, int len, int o)
{
    while (o < len) {
        uint8_t l = p[o];
        if (l == 0)
            return o + 1;
        if ((l & 0xC0) == 0xC0)
            return o + 2;
        o += l + 1;
    }
    return len;
}

static void dns_input(const uint8_t *p, int len)
{
    if (len < 12)
        return;
    uint16_t id = be16(p);
    int i;
    for (i = 0; i < DNS_SLOTS; i++)
        if (dns[i].name[0] && dns[i].state == DNS_PENDING && dns[i].id == id)
            break;
    if (i == DNS_SLOTS)
        return;
    int rcode = p[3] & 0xF, qd = be16(p + 4), an = be16(p + 6);
    int o = 12;
    for (int k = 0; k < qd; k++)
        o = skip_name(p, len, o) + 4;
    for (int k = 0; k < an && o + 10 <= len; k++) {
        o = skip_name(p, len, o);
        if (o + 10 > len)
            break;
        uint16_t type = be16(p + o), rdlen = be16(p + o + 8);
        o += 10;
        if (type == 1 && rdlen == 4 && o + 4 <= len) {
            dns[i].ip = be32(p + o);
            dns[i].state = DNS_DONE;
            dns[i].when = timer_ms();
            return;
        }
        o += rdlen;
    }
    (void)rcode;
    dns[i].state = DNS_FAILED;
    dns[i].when = timer_ms();
}

/* ---- receive path ---------------------------------------------------------------------------- */

static void icmp_input(uint32_t src, const uint8_t *p, int len)
{
    if (len < 8 || p[0] != 8)
        return;
    uint8_t reply[1480];
    if (len > (int)sizeof reply)
        return;
    memcpy(reply, p, len);
    reply[0] = 0;
    put16(reply + 2, 0);
    put16(reply + 2, net_fold(net_sum(reply, len, 0)));
    net_send_ip(src, PROTO_ICMP, reply, len);
}

static void udp_input(uint32_t src, const uint8_t *p, int len)
{
    if (len < 8)
        return;
    uint16_t sport = be16(p), dport = be16(p + 2), ulen = be16(p + 4);
    if (ulen < 8 || ulen > len)
        return;
    if (dport == DHCP_CLIENT && sport == DHCP_SERVER)
        dhcp_input(p + 8, ulen - 8);
    else if (dport == DNS_LOCAL && sport == DNS_PORT)
        dns_input(p + 8, ulen - 8);
    (void)src;
}

static void ip_input(const uint8_t *p, int len)
{
    if (len < 20 || (p[0] >> 4) != 4)
        return;
    int hl = (p[0] & 0xF) * 4, total = be16(p + 2);
    if (hl < 20 || total < hl || total > len)
        return;
    if (be16(p + 6) & 0x3FFF)
        return; /* fragments: not supported */
    uint32_t src = be32(p + 12), dst = be32(p + 16);
    if (my_ip && dst != my_ip && dst != 0xFFFFFFFF)
        return;
    const uint8_t *body = p + hl;
    int blen = total - hl;
    switch (p[9]) {
    case PROTO_ICMP: icmp_input(src, body, blen); break;
    case PROTO_UDP: udp_input(src, body, blen); break;
    case PROTO_TCP:
        if (my_ip)
            tcp_input(src, body, blen);
        break;
    }
}

/* ---- setup and polling ------------------------------------------------------------------------ */

void net_init(void)
{
    adapter = e1000_init(mac);
    entropy_add(mac, 6);
    if (!adapter) {
        strlcpy(status, "No network adapter", sizeof status);
        kprintf("net: no supported network card\n");
        return;
    }
    dhcp_start();
}

void net_poll(void)
{
    if (!adapter)
        return;
    static uint8_t buf[2048];
    int n, budget = 64;
    while (budget-- > 0 && (n = e1000_receive(buf, sizeof buf)) > 0) {
        uint32_t t = (uint32_t)rdtsc();
        entropy_add(&t, sizeof t);
        if (n < 14 || (memcmp(buf, mac, 6) && memcmp(buf, broadcast_mac, 6)))
            continue;
        uint16_t type = be16(buf + 12);
        if (type == ETH_ARP)
            arp_input(buf + 14, n - 14);
        else if (type == ETH_IPV4)
            ip_input(buf + 14, n - 14);
    }
    uint32_t now = timer_ms();
    if ((dhcp_state == DHCP_DISCOVER || dhcp_state == DHCP_REQUEST) && now - dhcp_sent > 2000) {
        if (++dhcp_tries > 4 && dhcp_state == DHCP_REQUEST)
            dhcp_start();
        else
            dhcp_send(dhcp_state == DHCP_DISCOVER ? 1 : 3);
        if (dhcp_tries > 3)
            strlcpy(status, e1000_link_up() ? "No DHCP server answered" : "Network cable unplugged", sizeof status);
    }
    for (int i = 0; i < DNS_SLOTS; i++)
        if (dns[i].name[0] && dns[i].state == DNS_PENDING && now - dns[i].sent > 1500) {
            if (dns[i].tries >= 4) {
                dns[i].state = DNS_FAILED;
                dns[i].when = now;
            } else {
                dns_query(i);
            }
        }
    tcp_poll();
}
