/* DEFLATE decoder (RFC 1951) with gzip (RFC 1952) and zlib (RFC 1950)
 * wrappers, after the canonical-Huffman approach of zlib's "puff". */

#include "http.h"

#define MAXBITS  15
#define MAXLCODES 286
#define MAXDCODES 30

typedef struct {
    const uint8_t *in;
    int inlen, incnt;
    uint32_t bitbuf;
    int bitcnt;
    uint8_t *out;
    int outlen, outcap;
    bool bad;
} state_t;

typedef struct {
    short count[MAXBITS + 1];
    short symbol[MAXLCODES];
} huff_t;

static int bits(state_t *s, int need)
{
    uint32_t val = s->bitbuf;
    while (s->bitcnt < need) {
        if (s->incnt >= s->inlen) {
            s->bad = true;
            return 0;
        }
        val |= (uint32_t)s->in[s->incnt++] << s->bitcnt;
        s->bitcnt += 8;
    }
    s->bitbuf = val >> need;
    s->bitcnt -= need;
    return (int)(val & ((1u << need) - 1));
}

static bool put(state_t *s, uint8_t c)
{
    if (s->outlen >= s->outcap) {
        int cap = s->outcap ? s->outcap * 2 : 65536;
        if (cap > 64 * 1024 * 1024)
            return false;
        uint8_t *n = kmalloc(cap + 1);
        if (s->out)
            memcpy(n, s->out, s->outlen);
        kfree(s->out);
        s->out = n;
        s->outcap = cap;
    }
    s->out[s->outlen++] = c;
    return true;
}

static int stored(state_t *s)
{
    s->bitbuf = 0;
    s->bitcnt = 0;
    if (s->incnt + 4 > s->inlen)
        return -1;
    unsigned len = s->in[s->incnt] | s->in[s->incnt + 1] << 8;
    unsigned nlen = s->in[s->incnt + 2] | s->in[s->incnt + 3] << 8;
    s->incnt += 4;
    if (len != (~nlen & 0xFFFF) || s->incnt + (int)len > s->inlen)
        return -1;
    while (len--)
        if (!put(s, s->in[s->incnt++]))
            return -1;
    return 0;
}

static int decode(state_t *s, const huff_t *h)
{
    int code = 0, first = 0, index = 0;
    for (int len = 1; len <= MAXBITS; len++) {
        code |= bits(s, 1);
        if (s->bad)
            return -1;
        int count = h->count[len];
        if (code - count < first)
            return h->symbol[index + (code - first)];
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
    }
    return -1;
}

static int construct(huff_t *h, const short *length, int n)
{
    short offs[MAXBITS + 1];
    for (int len = 0; len <= MAXBITS; len++)
        h->count[len] = 0;
    for (int sym = 0; sym < n; sym++)
        h->count[length[sym]]++;
    if (h->count[0] == n)
        return 0;
    int left = 1;
    for (int len = 1; len <= MAXBITS; len++) {
        left <<= 1;
        left -= h->count[len];
        if (left < 0)
            return left;
    }
    offs[1] = 0;
    for (int len = 1; len < MAXBITS; len++)
        offs[len + 1] = offs[len] + h->count[len];
    for (int sym = 0; sym < n; sym++)
        if (length[sym])
            h->symbol[offs[length[sym]]++] = sym;
    return left;
}

static int codes(state_t *s, const huff_t *lencode, const huff_t *distcode)
{
    static const short lbase[29] = { 3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19,  23, 27,
                                     31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258 };
    static const short lext[29] = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0 };
    static const short dbase[30] = { 1,   2,   3,   4,   5,   7,    9,    13,   17,   25,   33,   49,   65,    97,    129,
                                     193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577 };
    static const short dext[30] = { 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13 };
    for (;;) {
        int sym = decode(s, lencode);
        if (sym < 0)
            return -1;
        if (sym < 256) {
            if (!put(s, (uint8_t)sym))
                return -1;
        } else if (sym == 256) {
            return 0;
        } else {
            sym -= 257;
            if (sym >= 29)
                return -1;
            int len = lbase[sym] + bits(s, lext[sym]);
            int dsym = decode(s, distcode);
            if (dsym < 0 || dsym >= 30)
                return -1;
            int dist = dbase[dsym] + bits(s, dext[dsym]);
            if (s->bad || dist > s->outlen)
                return -1;
            while (len--)
                if (!put(s, s->out[s->outlen - dist]))
                    return -1;
        }
    }
}

static int fixed(state_t *s)
{
    static huff_t lencode, distcode;
    static bool built;
    if (!built) {
        short lengths[MAXLCODES];
        int sym = 0;
        for (; sym < 144; sym++)
            lengths[sym] = 8;
        for (; sym < 256; sym++)
            lengths[sym] = 9;
        for (; sym < 280; sym++)
            lengths[sym] = 7;
        for (; sym < 288 && sym < MAXLCODES; sym++)
            lengths[sym] = 8;
        construct(&lencode, lengths, MAXLCODES);
        for (sym = 0; sym < MAXDCODES; sym++)
            lengths[sym] = 5;
        construct(&distcode, lengths, MAXDCODES);
        built = true;
    }
    return codes(s, &lencode, &distcode);
}

static int dynamic(state_t *s)
{
    static const short order[19] = { 16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15 };
    short lengths[MAXLCODES + MAXDCODES];
    huff_t lencode, distcode;
    int nlen = bits(s, 5) + 257, ndist = bits(s, 5) + 1, ncode = bits(s, 4) + 4;
    if (s->bad || nlen > MAXLCODES || ndist > MAXDCODES)
        return -1;
    int index;
    for (index = 0; index < ncode; index++)
        lengths[order[index]] = bits(s, 3);
    for (; index < 19; index++)
        lengths[order[index]] = 0;
    if (construct(&lencode, lengths, 19) != 0)
        return -1;
    index = 0;
    while (index < nlen + ndist) {
        int sym = decode(s, &lencode);
        if (sym < 0)
            return -1;
        if (sym < 16) {
            lengths[index++] = sym;
            continue;
        }
        int len = 0, rep;
        if (sym == 16) {
            if (!index)
                return -1;
            len = lengths[index - 1];
            rep = 3 + bits(s, 2);
        } else if (sym == 17) {
            rep = 3 + bits(s, 3);
        } else {
            rep = 11 + bits(s, 7);
        }
        if (index + rep > nlen + ndist)
            return -1;
        while (rep--)
            lengths[index++] = len;
    }
    if (lengths[256] == 0)
        return -1;
    int err = construct(&lencode, lengths, nlen);
    if (err < 0 || (err > 0 && nlen - lencode.count[0] != 1))
        return -1;
    err = construct(&distcode, lengths + nlen, ndist);
    if (err < 0 || (err > 0 && ndist - distcode.count[0] != 1))
        return -1;
    return codes(s, &lencode, &distcode);
}

static int inflate_raw(const uint8_t *src, int len, uint8_t **out)
{
    state_t s = { 0 };
    s.in = src;
    s.inlen = len;
    int last, err = 0;
    do {
        last = bits(&s, 1);
        int type = bits(&s, 2);
        if (s.bad)
            break;
        err = type == 0 ? stored(&s) : type == 1 ? fixed(&s) : type == 2 ? dynamic(&s) : -1;
        if (err)
            break;
    } while (!last);
    /* a truncated stream still gives back what was decoded */
    if ((err || s.bad) && !s.outlen) {
        kfree(s.out);
        return -1;
    }
    if (!s.out)
        s.out = kmalloc(1);
    s.out[s.outlen] = 0;
    *out = s.out;
    return s.outlen;
}

int inflate_any(const uint8_t *src, int len, uint8_t **out)
{
    if (len >= 18 && src[0] == 0x1F && src[1] == 0x8B && src[2] == 8) {
        int flags = src[3], o = 10;
        if (flags & 4)
            o += 2 + (src[o] | src[o + 1] << 8);
        if (flags & 8)
            while (o < len && src[o++])
                ;
        if (flags & 16)
            while (o < len && src[o++])
                ;
        if (flags & 2)
            o += 2;
        if (o >= len)
            return -1;
        return inflate_raw(src + o, len - o, out);
    }
    if (len >= 2 && (src[0] & 0x0F) == 8 && ((src[0] << 8) | src[1]) % 31 == 0)
        return inflate_raw(src + 2, len - 2, out);
    return inflate_raw(src, len, out);
}
