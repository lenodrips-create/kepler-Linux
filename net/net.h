#pragma once

/* Networking: an Intel PRO/1000 (e1000) driver, Ethernet, ARP, IPv4, ICMP,
 * UDP, DHCP, DNS and a TCP client, all polled from the main loop. Addresses
 * are host-order uint32_t (a.b.c.d is a << 24 | ... | d). */

#include "kernel.h"

/* ---- the card (drivers/e1000.c) ------------------------------------------- */

bool e1000_init(uint8_t mac[6]);
bool e1000_send(const void *frame, int len);
int e1000_receive(void *buf, int max); /* a frame's length, or 0 */
bool e1000_link_up(void);
const char *e1000_name(void);

/* ---- core (net.c) ----------------------------------------------------------- */

void net_init(void);
void net_poll(void);
bool net_has_adapter(void);
bool net_ready(void); /* DHCP has given us an address */
const char *net_status(void);
uint32_t net_ip(void);
void net_ip_str(uint32_t ip, char *out, int n);

/* used by tcp.c */
bool net_send_ip(uint32_t dst, uint8_t proto, const void *payload, int len);
uint32_t net_sum(const void *data, int len, uint32_t sum);
uint16_t net_fold(uint32_t sum);
uint32_t net_pseudo(uint32_t src, uint32_t dst, uint8_t proto, int len);

static inline uint16_t be16(const uint8_t *p) { return (uint16_t)(p[0] << 8 | p[1]); }
static inline uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
static inline void put16(uint8_t *p, uint16_t v) { p[0] = v >> 8; p[1] = v; }
static inline void put32(uint8_t *p, uint32_t v) { p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v; }

/* ---- DNS (net.c) -------------------------------------------------------------- */

enum { DNS_FAILED = -1, DNS_PENDING = 0, DNS_DONE = 1 };
/* non-blocking: call again until it stops returning DNS_PENDING */
int dns_resolve(const char *name, uint32_t *ip);

/* ---- TCP (tcp.c) ---------------------------------------------------------------- */

typedef struct tcp tcp_t;
enum { TCP_CONNECTING, TCP_OPEN, TCP_PEER_CLOSED, TCP_FAILED };

tcp_t *tcp_open(uint32_t ip, uint16_t port);
int tcp_status(tcp_t *t);
const char *tcp_error(tcp_t *t);
int tcp_write(tcp_t *t, const void *data, int n); /* bytes accepted */
int tcp_read(tcp_t *t, void *buf, int n);         /* bytes read, 0 if none yet */
void tcp_close(tcp_t *t);                         /* the handle is gone afterwards */
void tcp_input(uint32_t src, const uint8_t *seg, int len);
void tcp_poll(void);

/* ---- randomness (entropy.c) --------------------------------------------------- */

/* stir anything unpredictable in: input timings, packet arrival times... */
void entropy_add(const void *data, int n);
void entropy_get(uint8_t out[32]);
