/*
 * Baseline JPEG decoder: Huffman-coded sequential DCT, 8-bit samples,
 * greyscale or YCbCr with any sampling factors, restart markers.
 * Progressive and arithmetic-coded files are rejected.
 */

#include "jpeg.h"

typedef struct {
    uint8_t bits[17];
    uint8_t vals[256];
    int maxcode[18], valptr[17], mincode[17];
    bool present;
} huff_t;

typedef struct {
    int id, h, v, tq, td, ta;
    int bw, bh; /* plane size in blocks */
    uint8_t *plane;
    int stride;
    int pred;
} comp_t;

typedef struct {
    const uint8_t *p, *end;
    uint32_t bitbuf;
    int bitcnt;
    bool marker_hit;

    uint16_t qt[4][64];
    huff_t huff[2][4];
    comp_t comp[3];
    int ncomp;
    int width, height;
    int hmax, vmax;
    int restart;
} jdec_t;

static const uint8_t zigzag[64] = {
    0, 1, 8, 16, 9, 2, 3, 10, 17, 24, 32, 25, 18, 11, 4, 5, 12, 19, 26, 33, 40, 48,
    41, 34, 27, 20, 13, 6, 7, 14, 21, 28, 35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23,
    30, 37, 44, 51, 58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63,
};

/* T[x][u] = 0.5 * C(u) * cos((2x + 1) u pi / 16), scaled by 4096 */
static int idct_tab[8][8];

static void idct_init(void)
{
    static const int c[9] = { 4096, 4017, 3784, 3406, 2896, 2276, 1567, 799, 0 };
    for (int x = 0; x < 8; x++)
        for (int u = 0; u < 8; u++) {
            int m = ((2 * x + 1) * u) % 32;
            int sign = 1;
            if (m > 16)
                m = 32 - m;
            if (m > 8) {
                m = 16 - m;
                sign = -1;
            }
            int v = sign * c[m];
            if (u == 0)
                v = v * 2896 / 4096; /* 1/sqrt(2) */
            idct_tab[x][u] = v / 2;
        }
}

static void idct_block(const int *in, uint8_t *out, int stride)
{
    int tmp[64];
    for (int v = 0; v < 8; v++) {
        const int *row = &in[v * 8];
        bool ac_zero = true;
        for (int u = 1; u < 8; u++)
            if (row[u]) {
                ac_zero = false;
                break;
            }
        for (int x = 0; x < 8; x++) {
            int s;
            if (ac_zero) {
                s = row[0] * idct_tab[x][0];
            } else {
                s = 0;
                for (int u = 0; u < 8; u++)
                    s += row[u] * idct_tab[x][u];
            }
            tmp[v * 8 + x] = s >> 8; /* keep 4 fractional bits */
        }
    }
    for (int x = 0; x < 8; x++)
        for (int y = 0; y < 8; y++) {
            int s = 0;
            for (int v = 0; v < 8; v++)
                s += tmp[v * 8 + x] * idct_tab[y][v];
            s = ((s + (1 << 15)) >> 16) + 128;
            out[y * stride + x] = (uint8_t)CLAMP(s, 0, 255);
        }
}

/* ---- bit reader ------------------------------------------------------------- */

static void fill_bits(jdec_t *d)
{
    while (d->bitcnt <= 24) {
        uint32_t b = 0;
        if (!d->marker_hit && d->p < d->end) {
            b = *d->p;
            if (b == 0xFF) {
                uint8_t next = d->p + 1 < d->end ? d->p[1] : 0;
                if (next == 0x00) {
                    d->p += 2;
                } else {
                    d->marker_hit = true; /* leave the marker for the caller */
                    b = 0;
                }
            } else {
                d->p++;
            }
        }
        d->bitbuf |= b << (24 - d->bitcnt);
        d->bitcnt += 8;
    }
}

static int get_bits(jdec_t *d, int n)
{
    if (n == 0)
        return 0;
    fill_bits(d);
    int v = d->bitbuf >> (32 - n);
    d->bitbuf <<= n;
    d->bitcnt -= n;
    return v;
}

static int extend(int v, int n) { return v < (1 << (n - 1)) ? v - (1 << n) + 1 : v; }

static int decode_huff(jdec_t *d, const huff_t *h)
{
    int code = 0;
    for (int len = 1; len <= 16; len++) {
        code = (code << 1) | get_bits(d, 1);
        if (h->maxcode[len] >= 0 && code <= h->maxcode[len])
            return h->vals[h->valptr[len] + code - h->mincode[len]];
    }
    return 0; /* corrupt data: treat as EOB */
}

static void build_huff(huff_t *h)
{
    int code = 0, k = 0;
    for (int len = 1; len <= 16; len++) {
        h->valptr[len] = k;
        h->mincode[len] = code;
        code += h->bits[len];
        k += h->bits[len];
        h->maxcode[len] = h->bits[len] ? code - 1 : -1;
        code <<= 1;
    }
    h->maxcode[17] = 0x7FFFFFFF;
    h->present = true;
}

/* ---- segments ------------------------------------------------------------------ */

static int u16(const uint8_t *p) { return (p[0] << 8) | p[1]; }

static bool decode_block(jdec_t *d, comp_t *c, uint8_t *out)
{
    int coef[64];
    memset(coef, 0, sizeof coef);
    const uint16_t *q = d->qt[c->tq];
    huff_t *dc = &d->huff[0][c->td], *ac = &d->huff[1][c->ta];
    int s = decode_huff(d, dc);
    int diff = s ? extend(get_bits(d, s), s) : 0;
    c->pred += diff;
    coef[0] = c->pred * q[0];
    for (int k = 1; k < 64;) {
        int rs = decode_huff(d, ac);
        int r = rs >> 4;
        s = rs & 15;
        if (s == 0) {
            if (r != 15)
                break;
            k += 16;
            continue;
        }
        k += r;
        if (k > 63)
            break;
        coef[zigzag[k]] = extend(get_bits(d, s), s) * q[k];
        k++;
    }
    idct_block(coef, out, c->stride);
    return true;
}

static void reset_stream(jdec_t *d)
{
    d->bitbuf = 0;
    d->bitcnt = 0;
    d->marker_hit = false;
    for (int i = 0; i < d->ncomp; i++)
        d->comp[i].pred = 0;
}

static bool decode_scan(jdec_t *d)
{
    int mcuw = 8 * d->hmax, mcuh = 8 * d->vmax;
    int mx = (d->width + mcuw - 1) / mcuw, my = (d->height + mcuh - 1) / mcuh;
    int todo = d->restart;
    reset_stream(d);
    for (int y = 0; y < my; y++)
        for (int x = 0; x < mx; x++) {
            if (d->restart && todo == 0) {
                /* expect RSTn */
                while (d->p + 1 < d->end && !(d->p[0] == 0xFF && d->p[1] >= 0xD0 && d->p[1] <= 0xD7))
                    d->p++;
                if (d->p + 1 < d->end)
                    d->p += 2;
                reset_stream(d);
                todo = d->restart;
            }
            for (int ci = 0; ci < d->ncomp; ci++) {
                comp_t *c = &d->comp[ci];
                for (int by = 0; by < c->v; by++)
                    for (int bx = 0; bx < c->h; bx++) {
                        int px = (x * c->h + bx) * 8, py = (y * c->v + by) * 8;
                        decode_block(d, c, &c->plane[py * c->stride + px]);
                    }
            }
            if (d->restart)
                todo--;
        }
    return true;
}

/* bilinear sample of a subsampled chroma plane at full-resolution pixel (x, y) */
static int sample(const comp_t *c, int x, int y, int hmax, int vmax)
{
    if (c->h == hmax && c->v == vmax)
        return c->plane[y * c->stride + x];
    /* position in plane space, 8 fractional bits */
    int fx = ((2 * x + 1) * c->h * 128) / hmax - 128;
    int fy = ((2 * y + 1) * c->v * 128) / vmax - 128;
    int maxx = c->bw * 8 - 1, maxy = c->bh * 8 - 1;
    if (fx < 0)
        fx = 0;
    if (fy < 0)
        fy = 0;
    int x0 = fx >> 8, y0 = fy >> 8, tx = fx & 255, ty = fy & 255;
    int x1 = MIN(x0 + 1, maxx), y1 = MIN(y0 + 1, maxy);
    const uint8_t *p = c->plane;
    int a = p[y0 * c->stride + x0], b = p[y0 * c->stride + x1];
    int e = p[y1 * c->stride + x0], f = p[y1 * c->stride + x1];
    int top = a * 256 + (b - a) * tx, bot = e * 256 + (f - e) * tx;
    return (top * 256 + (bot - top) * ty + 32768) >> 16;
}

bool jpeg_decode(const uint8_t *data, size_t len, image_t **out)
{
    static bool tab_ready;
    if (!tab_ready) {
        idct_init();
        tab_ready = true;
    }
    if (len < 4 || data[0] != 0xFF || data[1] != 0xD8)
        return false;

    jdec_t *d = kzalloc(sizeof *d);
    d->p = data + 2;
    d->end = data + len;
    bool ok = false, frame = false;

    while (d->p + 4 <= d->end) {
        if (d->p[0] != 0xFF) {
            d->p++;
            continue;
        }
        uint8_t m = d->p[1];
        d->p += 2;
        if (m == 0xD8 || (m >= 0xD0 && m <= 0xD7) || m == 0x01 || m == 0xFF) {
            if (m == 0xFF)
                d->p--;
            continue;
        }
        if (m == 0xD9)
            break;
        int seglen = u16(d->p);
        const uint8_t *seg = d->p + 2, *segend = d->p + seglen;
        if (segend > d->end)
            goto done;

        switch (m) {
        case 0xDB: /* DQT */
            while (seg < segend) {
                int pq = seg[0] >> 4, tq = seg[0] & 3;
                seg++;
                for (int k = 0; k < 64; k++) {
                    d->qt[tq][k] = pq ? u16(seg + 2 * k) : seg[k];
                }
                seg += pq ? 128 : 64;
            }
            break;
        case 0xC4: /* DHT */
            while (seg < segend) {
                int tc = seg[0] >> 4, th = seg[0] & 3;
                huff_t *h = &d->huff[tc & 1][th];
                int total = 0;
                h->bits[0] = 0;
                for (int i = 1; i <= 16; i++) {
                    h->bits[i] = seg[i];
                    total += seg[i];
                }
                if (total > 256)
                    goto done;
                memcpy(h->vals, seg + 17, total);
                build_huff(h);
                seg += 17 + total;
            }
            break;
        case 0xC0: /* SOF0 baseline */
        case 0xC1: /* extended sequential, Huffman */
            if (seg[0] != 8)
                goto done;
            d->height = u16(seg + 1);
            d->width = u16(seg + 3);
            d->ncomp = seg[5];
            if (d->ncomp != 1 && d->ncomp != 3)
                goto done;
            for (int i = 0; i < d->ncomp; i++) {
                comp_t *c = &d->comp[i];
                c->id = seg[6 + i * 3];
                c->h = seg[7 + i * 3] >> 4;
                c->v = seg[7 + i * 3] & 15;
                c->tq = seg[8 + i * 3] & 3;
                if (c->h < 1 || c->h > 4 || c->v < 1 || c->v > 4)
                    goto done;
                d->hmax = MAX(d->hmax, c->h);
                d->vmax = MAX(d->vmax, c->v);
            }
            frame = true;
            break;
        case 0xC2:
        case 0xC3:
        case 0xC5:
        case 0xC6:
        case 0xC7:
        case 0xC9:
        case 0xCA:
        case 0xCB:
        case 0xCD:
        case 0xCE:
        case 0xCF:
            kprintf("jpeg: unsupported frame type %02x\n", m);
            goto done;
        case 0xDD: /* DRI */
            d->restart = u16(seg);
            break;
        case 0xDA: { /* SOS */
            if (!frame)
                goto done;
            int ns = seg[0];
            for (int i = 0; i < ns; i++) {
                int id = seg[1 + i * 2], t = seg[2 + i * 2];
                for (int k = 0; k < d->ncomp; k++)
                    if (d->comp[k].id == id) {
                        d->comp[k].td = t >> 4;
                        d->comp[k].ta = t & 15;
                    }
            }
            int mcuw = 8 * d->hmax, mcuh = 8 * d->vmax;
            int mx = (d->width + mcuw - 1) / mcuw, my = (d->height + mcuh - 1) / mcuh;
            for (int i = 0; i < d->ncomp; i++) {
                comp_t *c = &d->comp[i];
                c->bw = mx * c->h;
                c->bh = my * c->v;
                c->stride = c->bw * 8;
                c->plane = kmalloc((size_t)c->stride * c->bh * 8);
            }
            d->p = segend;
            decode_scan(d);

            image_t *img = image_new(d->width, d->height);
            for (int y = 0; y < d->height; y++)
                for (int x = 0; x < d->width; x++) {
                    int Y = d->comp[0].plane[y * d->comp[0].stride + x];
                    if (d->ncomp == 1) {
                        img->px[y * d->width + x] = RGB(Y, Y, Y);
                        continue;
                    }
                    int cb = sample(&d->comp[1], x, y, d->hmax, d->vmax) - 128;
                    int cr = sample(&d->comp[2], x, y, d->hmax, d->vmax) - 128;
                    int r = Y + ((91881 * cr + 32768) >> 16);
                    int g = Y - ((22554 * cb + 46802 * cr + 32768) >> 16);
                    int b = Y + ((116130 * cb + 32768) >> 16);
                    img->px[y * d->width + x] = RGB(CLAMP(r, 0, 255), CLAMP(g, 0, 255), CLAMP(b, 0, 255));
                }
            *out = img;
            ok = true;
            goto done;
        }
        default:
            break;
        }
        d->p = segend;
    }

done:
    for (int i = 0; i < 3; i++)
        if (d->comp[i].plane)
            kfree(d->comp[i].plane);
    kfree(d);
    return ok;
}
