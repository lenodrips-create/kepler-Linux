#pragma once

/* HTTP/1.1 GET over TCP, or over TLS (BearSSL) for https:// URLs, with
 * redirects, chunked bodies and gzip. Non-blocking: start a request with
 * http_get() and keep calling http_poll() until it stops returning HTTP_BUSY. */

#include "kernel.h"

typedef struct http http_t;
enum { HTTP_BUSY, HTTP_DONE, HTTP_ERROR };

http_t *http_get(const char *url);
int http_poll(http_t *h);
void http_free(http_t *h);

const char *http_stage(http_t *h);  /* what it is doing now, for a status line */
const char *http_error(http_t *h);  /* why it failed */
const char *http_url(http_t *h);    /* where it ended up after redirects */
bool http_secure(http_t *h);        /* fetched over a verified TLS connection */
int http_code(http_t *h);           /* 200, 404... */
const char *http_type(http_t *h);   /* the Content-Type header, or "" */
const uint8_t *http_body(http_t *h, int *len);
int http_received(http_t *h);       /* body bytes so far */

/* URLs */
typedef struct {
    char scheme[8]; /* "http" or "https" */
    char host[128];
    int port;
    char path[1024]; /* with the query, always starting with '/' */
} url_t;

bool url_parse(const char *s, url_t *u);
/* rel against base (absolute, scheme-relative, rooted, relative, query or fragment) */
void url_resolve(const char *base, const char *rel, char *out, int n);
void url_encode(const char *s, char *out, int n);

/* gzip / zlib / raw deflate (inflate.c); returns the length or -1 */
int inflate_any(const uint8_t *src, int len, uint8_t **out);
