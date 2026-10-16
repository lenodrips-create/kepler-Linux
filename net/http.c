/* HTTP/1.1 client over plain TCP or BearSSL TLS. */

#include "bearssl.h"
#include "http.h"
#include "net.h"

#define MAX_REDIRECTS 8
#define MAX_BODY      (8 * 1024 * 1024)
#define MAX_HEADER    (32 * 1024)
#define IDLE_TIMEOUT  30000

extern const br_x509_trust_anchor ca_roots[];
extern const size_t ca_roots_count;

enum { ST_DNS, ST_CONNECT, ST_TALK, ST_DONE, ST_ERROR };
enum { CH_SIZE, CH_DATA, CH_DATA_END, CH_TRAILER, CH_DONE };

struct http {
    int state;
    char url[1100];
    url_t u;
    int redirects;
    uint32_t ip;
    tcp_t *tcp;
    bool tls;
    br_ssl_client_context *sc;
    br_x509_minimal_context *xc;
    uint8_t *iobuf;
    bool handshake_done;

    char request[1400];
    int req_len, req_sent;

    char *head; /* response header bytes */
    int head_len;
    bool have_head;
    int code;
    int content_length; /* -1 if not given */
    bool chunked;
    char type[96];
    char encoding[24];
    char location[1100];

    uint8_t *body;
    int body_len, body_cap;
    int chunk_state, chunk_left;
    char chunk_line[24];
    int chunk_line_len;

    bool peer_closed;
    uint32_t last_progress;
    char stage[160];
    char error[200];
};

/* ---- URLs ---------------------------------------------------------------------------- */

bool url_parse(const char *s, url_t *u)
{
    memset(u, 0, sizeof *u);
    const char *p = strchr(s, ':');
    if (!p || p[1] != '/' || p[2] != '/' || p - s >= (int)sizeof u->scheme)
        return false;
    for (int i = 0; i < p - s; i++)
        u->scheme[i] = ktolower(s[i]);
    if (strcmp(u->scheme, "http") && strcmp(u->scheme, "https"))
        return false;
    u->port = strcmp(u->scheme, "https") ? 80 : 443;
    p += 3;
    int n = 0;
    while (*p && *p != '/' && *p != '?' && *p != '#' && *p != ':' && n < (int)sizeof u->host - 1)
        u->host[n++] = ktolower(*p++);
    u->host[n] = 0;
    if (*p == '@')
        return false; /* user info: not supported */
    if (*p == ':') {
        p++;
        u->port = 0;
        while (kisdigit(*p))
            u->port = u->port * 10 + (*p++ - '0');
    }
    if (!n || u->port <= 0 || u->port > 65535)
        return false;
    if (*p != '/')
        strlcpy(u->path, "/", sizeof u->path);
    int o = strlen(u->path);
    while (*p && *p != '#' && o < (int)sizeof u->path - 4) {
        /* spaces and other unsafe bytes must be escaped on the request line */
        unsigned char c = *p++;
        if (c <= ' ' || c >= 127) {
            ksnprintf(u->path + o, sizeof u->path - o, "%%%02X", c);
            o += 3;
        } else {
            u->path[o++] = c;
        }
    }
    u->path[o] = 0;
    return true;
}

void url_resolve(const char *base, const char *rel, char *out, int n)
{
    while (*rel == ' ' || *rel == '\t' || *rel == '\n' || *rel == '\r')
        rel++;
    const char *colon = strchr(rel, ':');
    const char *slash = strchr(rel, '/');
    if (colon && (!slash || colon < slash)) {
        strlcpy(out, rel, n); /* absolute (or another scheme) */
        return;
    }
    url_t b;
    if (!url_parse(base, &b)) {
        strlcpy(out, rel, n);
        return;
    }
    char origin[200];
    if ((b.port == 80 && !strcmp(b.scheme, "http")) || (b.port == 443 && !strcmp(b.scheme, "https")))
        ksnprintf(origin, sizeof origin, "%s://%s", b.scheme, b.host);
    else
        ksnprintf(origin, sizeof origin, "%s://%s:%d", b.scheme, b.host, b.port);
    if (rel[0] == '/' && rel[1] == '/') {
        ksnprintf(out, n, "%s:%s", b.scheme, rel);
        return;
    }
    if (rel[0] == '#') {
        ksnprintf(out, n, "%s%s", origin, b.path);
        return;
    }
    if (rel[0] == '/') {
        ksnprintf(out, n, "%s%s", origin, rel);
        return;
    }
    if (rel[0] == '?') {
        char dir[1024];
        strlcpy(dir, b.path, sizeof dir);
        char *q = strchr(dir, '?');
        if (q)
            *q = 0;
        ksnprintf(out, n, "%s%s%s", origin, dir, rel);
        return;
    }
    /* relative to the directory of the base path, then fold "." and ".." */
    char path[1100];
    strlcpy(path, b.path, sizeof path);
    char *q = strchr(path, '?');
    if (q)
        *q = 0;
    char *last = strrchr(path, '/');
    if (last)
        last[1] = 0;
    strlcat(path, rel, sizeof path);
    char clean[1100];
    int o = 0;
    for (const char *p = path; *p;) {
        if (p[0] == '/' && p[1] == '.' && (p[2] == '/' || !p[2])) {
            p += 2;
            continue;
        }
        if (p[0] == '/' && p[1] == '.' && p[2] == '.' && (p[3] == '/' || !p[3])) {
            while (o > 0 && clean[--o] != '/')
                ;
            p += 3;
            continue;
        }
        if (*p == '?') {
            while (*p && o < (int)sizeof clean - 1)
                clean[o++] = *p++;
            break;
        }
        if (o < (int)sizeof clean - 1)
            clean[o++] = *p;
        p++;
    }
    clean[o] = 0;
    if (!clean[0])
        strlcpy(clean, "/", sizeof clean);
    ksnprintf(out, n, "%s%s", origin, clean);
}

void url_encode(const char *s, char *out, int n)
{
    static const char hex[] = "0123456789ABCDEF";
    int o = 0;
    for (; *s && o < n - 4; s++) {
        unsigned char c = *s;
        if (kisalpha(c) || kisdigit(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            out[o++] = c;
        } else if (c == ' ') {
            out[o++] = '+';
        } else {
            out[o++] = '%';
            out[o++] = hex[c >> 4];
            out[o++] = hex[c & 15];
        }
    }
    out[o] = 0;
}

/* ---- request lifecycle ----------------------------------------------------------------- */

static void set_stage(http_t *h, const char *fmt, const char *arg)
{
    ksnprintf(h->stage, sizeof h->stage, fmt, arg);
}

static void fail(http_t *h, const char *msg)
{
    strlcpy(h->error, msg, sizeof h->error);
    h->state = ST_ERROR;
    kprintf("http: %s: %s\n", h->url, msg);
}

static void drop_connection(http_t *h)
{
    if (h->tcp)
        tcp_close(h->tcp);
    h->tcp = NULL;
    kfree(h->sc);
    kfree(h->xc);
    kfree(h->iobuf);
    h->sc = NULL;
    h->xc = NULL;
    h->iobuf = NULL;
}

static void reset_response(http_t *h)
{
    kfree(h->head);
    kfree(h->body);
    h->head = NULL;
    h->body = NULL;
    h->head_len = h->body_len = h->body_cap = 0;
    h->have_head = false;
    h->code = 0;
    h->content_length = -1;
    h->chunked = false;
    h->type[0] = h->encoding[0] = h->location[0] = 0;
    h->chunk_state = CH_SIZE;
    h->chunk_line_len = 0;
    h->peer_closed = false;
    h->handshake_done = false;
    h->req_sent = 0;
}

static bool start(http_t *h, const char *url)
{
    strlcpy(h->url, url, sizeof h->url);
    reset_response(h);
    if (!url_parse(url, &h->u)) {
        fail(h, "That address isn't a web address Kepler understands.");
        return false;
    }
    h->tls = !strcmp(h->u.scheme, "https");
    h->state = ST_DNS;
    h->last_progress = timer_ms();
    set_stage(h, "Looking up %s...", h->u.host);
    char host[160];
    if ((h->u.port == 80 && !h->tls) || (h->u.port == 443 && h->tls))
        strlcpy(host, h->u.host, sizeof host);
    else
        ksnprintf(host, sizeof host, "%s:%d", h->u.host, h->u.port);
    h->req_len = ksnprintf(h->request, sizeof h->request,
                           "GET %s HTTP/1.1\r\n"
                           "Host: %s\r\n"
                           "User-Agent: Mozilla/5.0 (KeplerOS i686) Kepler/1.0 Lynx/2.9\r\n"
                           "Accept: text/html,application/xhtml+xml,text/plain;q=0.9,*/*;q=0.5\r\n"
                           "Accept-Language: en-US,en;q=0.8\r\n"
                           "Accept-Encoding: gzip, identity\r\n"
                           "Connection: close\r\n\r\n",
                           h->u.path, host);
    return true;
}

http_t *http_get(const char *url)
{
    http_t *h = kzalloc(sizeof *h);
    start(h, url);
    return h;
}

void http_free(http_t *h)
{
    if (!h)
        return;
    drop_connection(h);
    kfree(h->head);
    kfree(h->body);
    kfree(h);
}

const char *http_stage(http_t *h) { return h->stage; }
const char *http_error(http_t *h) { return h->error; }
const char *http_url(http_t *h) { return h->url; }
bool http_secure(http_t *h) { return h->tls && h->state == ST_DONE; }
int http_code(http_t *h) { return h->code; }
const char *http_type(http_t *h) { return h->type; }
int http_received(http_t *h) { return h->body_len; }

const uint8_t *http_body(http_t *h, int *len)
{
    *len = h->body_len;
    return h->body;
}

/* ---- TLS -------------------------------------------------------------------------------------- */

/* days since 1 January 0000 (proleptic Gregorian), as BearSSL wants them */
static uint32_t days_since_year0(int y, int m, int d)
{
    y -= m <= 2;
    int era = (y >= 0 ? y : y - 399) / 400;
    int yoe = y - era * 400;
    int doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    int unix_days = era * 146097 + doe - 719468;
    return (uint32_t)(unix_days + 719528);
}

static void tls_begin(http_t *h)
{
    h->sc = kzalloc(sizeof *h->sc);
    h->xc = kzalloc(sizeof *h->xc);
    h->iobuf = kmalloc(BR_SSL_BUFSIZE_BIDI);
    br_ssl_client_init_full(h->sc, h->xc, ca_roots, ca_roots_count);
    datetime_t dt;
    rtc_read(&dt);
    br_x509_minimal_set_time(h->xc, days_since_year0(dt.year, dt.month, dt.day),
                             (uint32_t)(dt.hour * 3600 + dt.minute * 60 + dt.second));
    br_ssl_engine_set_buffer(&h->sc->eng, h->iobuf, BR_SSL_BUFSIZE_BIDI, 1);
    uint8_t seed[32];
    entropy_get(seed);
    br_ssl_engine_inject_entropy(&h->sc->eng, seed, sizeof seed);
    br_ssl_client_reset(h->sc, h->u.host, 0);
}

static const char *tls_error_text(int err)
{
    switch (err) {
    case BR_ERR_X509_EXPIRED:
        return "This site's certificate has expired or isn't valid yet (check the computer's clock).";
    case BR_ERR_X509_BAD_SERVER_NAME:
        return "This site's certificate is for a different website.";
    case BR_ERR_X509_NOT_TRUSTED:
        return "This site's certificate isn't from an authority Kepler trusts.";
    case BR_ERR_BAD_VERSION:
    case BR_ERR_BAD_CIPHER_SUITE:
    case BR_ERR_UNSUPPORTED_VERSION:
        return "This site needs a newer kind of secure connection than Kepler supports.";
    }
    if (err >= BR_ERR_SEND_FATAL_ALERT)
        return "The site refused the secure connection.";
    if (err >= BR_ERR_X509_OK)
        return "This site's certificate couldn't be verified.";
    return "The secure connection failed.";
}

/* ---- response parsing -------------------------------------------------------------------------- */

static bool body_append(http_t *h, const uint8_t *data, int n)
{
    if (h->body_len + n > MAX_BODY) {
        fail(h, "The page is too big for Kepler.");
        return false;
    }
    if (h->body_len + n + 1 > h->body_cap) {
        int cap = MAX(h->body_cap * 2, 65536);
        while (cap < h->body_len + n + 1)
            cap *= 2;
        uint8_t *nb = kmalloc(cap);
        if (h->body)
            memcpy(nb, h->body, h->body_len);
        kfree(h->body);
        h->body = nb;
        h->body_cap = cap;
    }
    memcpy(h->body + h->body_len, data, n);
    h->body_len += n;
    h->body[h->body_len] = 0;
    return true;
}

static bool header_value(const char *head, const char *name, char *out, int n)
{
    int nl = strlen(name);
    for (const char *p = head; *p;) {
        const char *eol = strchr(p, '\n');
        if (!eol)
            break;
        bool match = true;
        for (int i = 0; i < nl; i++)
            if (ktolower(p[i]) != name[i]) {
                match = false;
                break;
            }
        if (match && p[nl] == ':') {
            const char *v = p + nl + 1;
            while (*v == ' ' || *v == '\t')
                v++;
            int len = (int)(eol - v);
            while (len > 0 && (v[len - 1] == '\r' || v[len - 1] == ' '))
                len--;
            len = MIN(len, n - 1);
            memcpy(out, v, len);
            out[len] = 0;
            return true;
        }
        p = eol + 1;
    }
    return false;
}

static void parse_head(http_t *h)
{
    h->have_head = true;
    const char *s = h->head;
    if (strncmp(s, "HTTP/", 5)) {
        fail(h, "The server sent something that isn't HTTP.");
        return;
    }
    const char *sp = strchr(s, ' ');
    h->code = sp ? katoi(sp + 1) : 0;
    char v[64];
    if (header_value(s, "content-length", v, sizeof v))
        h->content_length = katoi(v);
    if (header_value(s, "transfer-encoding", v, sizeof v))
        for (char *c = v; *c; c++)
            if (!strncmp(c, "chunked", 7) || !strncmp(c, "Chunked", 7))
                h->chunked = true;
    header_value(s, "content-type", h->type, sizeof h->type);
    header_value(s, "content-encoding", h->encoding, sizeof h->encoding);
    header_value(s, "location", h->location, sizeof h->location);
    if (h->code == 204 || h->code == 304 || (h->code >= 100 && h->code < 200))
        h->content_length = 0;
}

static bool body_complete(http_t *h)
{
    if (h->chunked)
        return h->chunk_state == CH_DONE;
    return h->content_length >= 0 && h->body_len >= h->content_length;
}

static void feed_body(http_t *h, const uint8_t *p, int n)
{
    if (!h->chunked) {
        if (h->content_length >= 0)
            n = MIN(n, h->content_length - h->body_len);
        if (n > 0)
            body_append(h, p, n);
        return;
    }
    while (n > 0 && h->state != ST_ERROR && h->chunk_state != CH_DONE) {
        if (h->chunk_state == CH_DATA) {
            int take = MIN(n, h->chunk_left);
            if (!body_append(h, p, take))
                return;
            p += take;
            n -= take;
            h->chunk_left -= take;
            if (!h->chunk_left)
                h->chunk_state = CH_DATA_END;
            continue;
        }
        /* the size line, the CRLF after the data, or the trailer: line by line */
        char c = *p++;
        n--;
        if (c != '\n') {
            if (h->chunk_line_len < (int)sizeof h->chunk_line - 1)
                h->chunk_line[h->chunk_line_len++] = c;
            continue;
        }
        h->chunk_line[h->chunk_line_len] = 0;
        bool empty = h->chunk_line_len == 0 || (h->chunk_line_len == 1 && h->chunk_line[0] == '\r');
        h->chunk_line_len = 0;
        if (h->chunk_state == CH_DATA_END) {
            h->chunk_state = CH_SIZE;
        } else if (h->chunk_state == CH_SIZE) {
            int size = 0;
            for (char *q = h->chunk_line; *q; q++) {
                int d = kisdigit(*q) ? *q - '0' : (*q >= 'a' && *q <= 'f') ? *q - 'a' + 10 : (*q >= 'A' && *q <= 'F') ? *q - 'A' + 10 : -1;
                if (d < 0)
                    break;
                size = size * 16 + d;
            }
            h->chunk_left = size;
            h->chunk_state = size ? CH_DATA : CH_TRAILER;
        } else if (h->chunk_state == CH_TRAILER && empty) {
            h->chunk_state = CH_DONE;
        }
    }
}

static void feed(http_t *h, const uint8_t *p, int n)
{
    h->last_progress = timer_ms();
    while (!h->have_head && n > 0) {
        if (!h->head)
            h->head = kmalloc(MAX_HEADER + 1);
        h->head[h->head_len++] = *p++;
        n--;
        h->head[h->head_len] = 0;
        if (h->head_len >= 4 && !memcmp(h->head + h->head_len - 4, "\r\n\r\n", 4)) {
            parse_head(h);
            if (h->state == ST_ERROR)
                return;
            /* a 100 Continue is followed by the real response */
            if (h->code >= 100 && h->code < 200) {
                h->head_len = 0;
                h->have_head = false;
            }
        } else if (h->head_len >= MAX_HEADER) {
            fail(h, "The server's response headers are too long.");
            return;
        }
    }
    if (n > 0)
        feed_body(h, p, n);
    if (h->have_head)
        ksnprintf(h->stage, sizeof h->stage, "Loading %s... %d KB", h->u.host, h->body_len / 1024);
}

/* the response is complete: follow a redirect, or unpack and finish */
static void finish(http_t *h)
{
    drop_connection(h);
    if (h->code >= 300 && h->code < 400 && h->location[0]) {
        if (++h->redirects > MAX_REDIRECTS) {
            fail(h, "The page redirected too many times.");
            return;
        }
        char next[1100];
        url_resolve(h->url, h->location, next, sizeof next);
        start(h, next);
        return;
    }
    if (h->body_len && (strstr(h->encoding, "gzip") || strstr(h->encoding, "deflate"))) {
        uint8_t *plain;
        int n = inflate_any(h->body, h->body_len, &plain);
        if (n < 0) {
            fail(h, "The page arrived compressed in a way Kepler couldn't unpack.");
            return;
        }
        kfree(h->body);
        h->body = plain;
        h->body_len = n;
        h->body_cap = n + 1;
    }
    h->state = ST_DONE;
    kprintf("http: %s -> %d, %d bytes%s\n", h->url, h->code, h->body_len, h->tls ? " (tls)" : "");
}

/* ---- the pump ----------------------------------------------------------------------------------- */

static void talk_plain(http_t *h)
{
    if (h->req_sent < h->req_len) {
        int n = tcp_write(h->tcp, h->request + h->req_sent, h->req_len - h->req_sent);
        if (n > 0)
            h->req_sent += n;
    }
    uint8_t buf[4096];
    int n;
    while (h->state == ST_TALK && (n = tcp_read(h->tcp, buf, sizeof buf)) > 0)
        feed(h, buf, n);
    if (h->state != ST_TALK)
        return;
    int st = tcp_status(h->tcp);
    if (st == TCP_FAILED) {
        fail(h, tcp_error(h->tcp));
        return;
    }
    if (h->have_head && body_complete(h)) {
        finish(h);
        return;
    }
    if (st == TCP_PEER_CLOSED) {
        if (!h->have_head)
            fail(h, "The server closed the connection without answering.");
        else
            finish(h);
    }
}

static void talk_tls(http_t *h)
{
    br_ssl_engine_context *eng = &h->sc->eng;
    for (int round = 0; round < 64 && h->state == ST_TALK; round++) {
        unsigned st = br_ssl_engine_current_state(eng);
        if (st & BR_SSL_CLOSED) {
            int err = br_ssl_engine_last_error(eng);
            if (err != BR_ERR_OK && !(h->have_head && (body_complete(h) || (h->content_length < 0 && !h->chunked))))
                fail(h, tls_error_text(err));
            else if (!h->have_head)
                fail(h, "The server closed the connection without answering.");
            else
                finish(h);
            return;
        }
        bool moved = false;
        if (st & BR_SSL_SENDREC) {
            size_t len;
            uint8_t *buf = br_ssl_engine_sendrec_buf(eng, &len);
            int n = tcp_write(h->tcp, buf, (int)len);
            if (n < 0) {
                fail(h, "The connection was lost.");
                return;
            }
            if (n > 0) {
                br_ssl_engine_sendrec_ack(eng, n);
                moved = true;
            }
        }
        if (st & BR_SSL_RECVREC) {
            size_t len;
            uint8_t *buf = br_ssl_engine_recvrec_buf(eng, &len);
            int n = tcp_read(h->tcp, buf, (int)len);
            if (n > 0) {
                br_ssl_engine_recvrec_ack(eng, n);
                h->last_progress = timer_ms();
                moved = true;
            } else if (tcp_status(h->tcp) == TCP_PEER_CLOSED || tcp_status(h->tcp) == TCP_FAILED) {
                /* many servers just hang up instead of sending close_notify */
                if (h->have_head && (body_complete(h) || (h->content_length < 0 && !h->chunked)))
                    finish(h);
                else if (!h->handshake_done)
                    fail(h, tcp_status(h->tcp) == TCP_FAILED ? tcp_error(h->tcp) : "The secure connection was closed by the server.");
                else
                    fail(h, "The connection closed before the page finished loading.");
                return;
            }
        }
        if (st & BR_SSL_SENDAPP) {
            if (!h->handshake_done) {
                h->handshake_done = true;
                set_stage(h, "Waiting for %s...", h->u.host);
            }
            if (h->req_sent < h->req_len) {
                size_t len;
                uint8_t *buf = br_ssl_engine_sendapp_buf(eng, &len);
                int n = MIN((int)len, h->req_len - h->req_sent);
                memcpy(buf, h->request + h->req_sent, n);
                br_ssl_engine_sendapp_ack(eng, n);
                h->req_sent += n;
                if (h->req_sent == h->req_len)
                    br_ssl_engine_flush(eng, 0);
                moved = true;
            }
        }
        if (st & BR_SSL_RECVAPP) {
            size_t len;
            uint8_t *buf = br_ssl_engine_recvapp_buf(eng, &len);
            feed(h, buf, (int)len);
            br_ssl_engine_recvapp_ack(eng, len);
            moved = true;
            if (h->state == ST_TALK && h->have_head && body_complete(h)) {
                finish(h);
                return;
            }
        }
        if (!moved)
            break;
    }
}

int http_poll(http_t *h)
{
    for (int pass = 0; pass < 4; pass++) {
        int before = h->state;
        switch (h->state) {
        case ST_DNS: {
            int r = dns_resolve(h->u.host, &h->ip);
            if (r == DNS_FAILED) {
                fail(h, net_ready() ? "Kepler can't find the server. Check the address." : net_has_adapter()
                                                                                             ? "Not connected to a network yet."
                                                                                             : "This computer has no network adapter.");
            } else if (r == DNS_DONE) {
                h->tcp = tcp_open(h->ip, h->u.port);
                if (!h->tcp) {
                    fail(h, "Kepler couldn't open a connection.");
                    break;
                }
                h->state = ST_CONNECT;
                h->last_progress = timer_ms();
                set_stage(h, "Connecting to %s...", h->u.host);
            }
            break;
        }
        case ST_CONNECT: {
            int st = tcp_status(h->tcp);
            if (st == TCP_FAILED) {
                fail(h, tcp_error(h->tcp));
            } else if (st == TCP_OPEN) {
                h->state = ST_TALK;
                h->last_progress = timer_ms();
                if (h->tls) {
                    set_stage(h, "Securing the connection to %s...", h->u.host);
                    tls_begin(h);
                } else {
                    set_stage(h, "Waiting for %s...", h->u.host);
                }
            }
            break;
        }
        case ST_TALK:
            if (h->tls)
                talk_tls(h);
            else
                talk_plain(h);
            if (h->state == ST_TALK && timer_ms() - h->last_progress > IDLE_TIMEOUT)
                fail(h, "The server stopped responding.");
            break;
        }
        if (h->state == before)
            break;
    }
    if (h->state == ST_DONE)
        return HTTP_DONE;
    if (h->state == ST_ERROR) {
        drop_connection(h);
        return HTTP_ERROR;
    }
    return HTTP_BUSY;
}
