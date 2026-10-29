#include "recto.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef RECTO_NO_ZLIB
#include <zlib.h>
#endif

void image_free(Image *im) {
    if (!im) return;
    free(im->px);
    im->px = NULL;
    im->w = 0;
    im->h = 0;
}

int image_alloc(Image *im, int w, int h, int fill) {
    size_t n;
    if (w <= 0 || h <= 0 || w > 20000 || h > 20000) {
        recto_seterr("image size %d by %d is not usable", w, h);
        return -1;
    }
    n = (size_t)w * (size_t)h;
    if (n > 80ul * 1000ul * 1000ul) {
        recto_seterr("image has too many pixels");
        return -1;
    }
    im->w = w;
    im->h = h;
    im->px = xmalloc(n);
    memset(im->px, fill, n);
    return 0;
}

static int pnm_num(const uint8_t *s, size_t n, size_t *i) {
    int v = 0;
    while (*i < n) {
        unsigned char c = s[*i];
        if (c == '#') {
            while (*i < n && s[*i] != '\n') (*i)++;
            continue;
        }
        if (!isspace(c)) break;
        (*i)++;
    }
    if (*i >= n || !isdigit(s[*i])) return -1;
    while (*i < n && isdigit(s[*i])) {
        v = v * 10 + (s[*i] - '0');
        (*i)++;
    }
    return v;
}

static int load_pnm(const uint8_t *s, size_t n, Image *im) {
    size_t i = 2;
    int w, h, maxv = 1, magic, x, y, raw;
    if (n < 3 || s[0] != 'P' || s[1] < '1' || s[1] > '6') return -1;
    magic = s[1] - '0';
    w = pnm_num(s, n, &i);
    h = pnm_num(s, n, &i);
    if (magic != 1 && magic != 4) maxv = pnm_num(s, n, &i);
    if (w <= 0 || h <= 0 || maxv <= 0) {
        recto_seterr("broken PNM header");
        return -1;
    }
    if (image_alloc(im, w, h, 255) != 0) return -1;
    while (i < n && isspace(s[i])) i++;
    raw = (magic >= 4);
    if (!raw) {
        for (y = 0; y < h; y++) {
            for (x = 0; x < w; x++) {
                int r = pnm_num(s, n, &i), g, b, gray;
                if (r < 0) {
                    image_free(im);
                    recto_seterr("short PNM");
                    return -1;
                }
                if (magic == 1) gray = r ? 0 : 255;
                else if (magic == 2) gray = (r * 255) / maxv;
                else {
                    g = pnm_num(s, n, &i);
                    b = pnm_num(s, n, &i);
                    if (g < 0 || b < 0) {
                        image_free(im);
                        return -1;
                    }
                    gray = (r * 30 + g * 59 + b * 11) / 100;
                    gray = gray * 255 / maxv;
                }
                im->px[y * w + x] = (uint8_t)(gray < 0 ? 0 : gray > 255 ? 255 : gray);
            }
        }
        return 0;
    }
    if (magic == 4) {
        int bit = 0;
        for (y = 0; y < h; y++) {
            bit = 0;
            for (x = 0; x < w; x++) {
                int byte;
                if (i >= n) {
                    image_free(im);
                    recto_seterr("short PBM");
                    return -1;
                }
                byte = s[i];
                im->px[y * w + x] = (byte & (128 >> bit)) ? 0 : 255;
                if (++bit == 8) {
                    bit = 0;
                    i++;
                }
            }
            if (bit) i++;
        }
        return 0;
    }
    if (magic == 5) {
        size_t need = (size_t)w * (size_t)h;
        if (maxv != 255) {
            size_t k;
            if (i + need > n) {
                image_free(im);
                recto_seterr("short PGM");
                return -1;
            }
            for (k = 0; k < need; k++) im->px[k] = (uint8_t)((s[i + k] * 255) / maxv);
        } else {
            if (i + need > n) {
                image_free(im);
                recto_seterr("short PGM");
                return -1;
            }
            memcpy(im->px, s + i, need);
        }
        return 0;
    }
    {
        size_t k, need = (size_t)w * (size_t)h;
        if (i + need * 3 > n) {
            image_free(im);
            recto_seterr("short PPM");
            return -1;
        }
        for (k = 0; k < need; k++) {
            int r = s[i + k * 3], g = s[i + k * 3 + 1], b = s[i + k * 3 + 2];
            im->px[k] = (uint8_t)((r * 30 + g * 59 + b * 11) / 100);
        }
    }
    return 0;
}

static uint16_t ru16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t ru32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int load_bmp(const uint8_t *s, size_t n, Image *im) {
    uint32_t off, width;
    int32_t height;
    uint16_t bits;
    uint32_t comp;
    int topdown = 0, w, h, y, x, rowbytes;
    const uint8_t *pal = NULL;
    if (n < 54 || s[0] != 'B' || s[1] != 'M') return -1;
    off = ru32(s + 10);
    width = ru32(s + 18);
    height = (int32_t)ru32(s + 22);
    bits = ru16(s + 28);
    comp = ru32(s + 30);
    if (comp != 0 || (bits != 24 && bits != 8 && bits != 1)) {
        recto_seterr("this BMP is not an uncompressed 1, 8, or 24-bit file");
        return -1;
    }
    if ((int32_t)height < 0) {
        topdown = 1;
        height = -height;
    }
    w = (int)width;
    h = (int)height;
    if (image_alloc(im, w, h, 255) != 0) return -1;
    if (bits == 8 || bits == 1) pal = s + 14 + ru32(s + 14);
    rowbytes = ((bits * w + 31) / 32) * 4;
    for (y = 0; y < h; y++) {
        int sy = topdown ? y : (h - 1 - y);
        const uint8_t *row;
        if ((size_t)off + (size_t)sy * (size_t)rowbytes + (size_t)rowbytes > n) {
            image_free(im);
            recto_seterr("short BMP");
            return -1;
        }
        row = s + off + (size_t)sy * (size_t)rowbytes;
        if (bits == 24) {
            for (x = 0; x < w; x++) {
                int b = row[x * 3], g = row[x * 3 + 1], r = row[x * 3 + 2];
                im->px[y * w + x] = (uint8_t)((r * 30 + g * 59 + b * 11) / 100);
            }
        } else if (bits == 8) {
            for (x = 0; x < w; x++) {
                const uint8_t *e = pal + row[x] * 4;
                int b = e[0], g = e[1], r = e[2];
                im->px[y * w + x] = (uint8_t)((r * 30 + g * 59 + b * 11) / 100);
            }
        } else {
            for (x = 0; x < w; x++) {
                int on = row[x >> 3] & (128 >> (x & 7));
                im->px[y * w + x] = on ? 0 : 255;
            }
        }
    }
    return 0;
}

#ifdef RECTO_NO_ZLIB
int image_write_png(const Image *im, Buf *out) {
    (void)im;
    (void)out;
    recto_seterr("this build has no zlib, so it cannot write a PNG");
    return -1;
}
static int load_png(const uint8_t *s, size_t n, Image *im) {
    (void)s;
    (void)n;
    (void)im;
    recto_seterr("this build has no zlib, so it cannot read a PNG");
    return -1;
}
#else
static uint32_t be32(uint32_t v) {
    return ((v & 0xff) << 24) | ((v & 0xff00) << 8) | ((v & 0xff0000) >> 8) | ((v >> 24) & 0xff);
}

static void png_chunk(Buf *out, const char *type, const uint8_t *data, size_t n) {
    uint32_t lenb = be32((uint32_t)n);
    uint32_t c;
    uint8_t tb[4];
    buf_append(out, &lenb, 4);
    memcpy(tb, type, 4);
    buf_append(out, tb, 4);
    if (n) buf_append(out, data, n);
    c = crc32(0L, tb, 4);
    if (n) c = crc32(c, data, (uInt)n);
    c = be32(c);
    buf_append(out, &c, 4);
}

int image_write_png(const Image *im, Buf *out) {
    uint8_t ihdr[13];
    uint8_t *raw;
    size_t stride, y, raw_n;
    uLongf clen;
    uint8_t *comp;
    uint32_t w, h;
    static const uint8_t sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    if (!im || !im->px) return -1;
    buf_clear(out);
    buf_append(out, sig, 8);
    w = be32((uint32_t)im->w);
    h = be32((uint32_t)im->h);
    memcpy(ihdr, &w, 4);
    memcpy(ihdr + 4, &h, 4);
    ihdr[8] = 8;
    ihdr[9] = 0;
    ihdr[10] = 0;
    ihdr[11] = 0;
    ihdr[12] = 0;
    png_chunk(out, "IHDR", ihdr, 13);
    stride = (size_t)im->w + 1;
    raw_n = stride * (size_t)im->h;
    raw = xmalloc(raw_n);
    for (y = 0; y < (size_t)im->h; y++) {
        raw[y * stride] = 0;
        memcpy(raw + y * stride + 1, im->px + y * (size_t)im->w, (size_t)im->w);
    }
    clen = compressBound((uLong)raw_n);
    comp = xmalloc(clen);
    if (compress(comp, &clen, raw, (uLong)raw_n) != Z_OK) {
        free(raw);
        free(comp);
        recto_seterr("PNG compress failed");
        return -1;
    }
    png_chunk(out, "IDAT", comp, clen);
    png_chunk(out, "IEND", NULL, 0);
    free(raw);
    free(comp);
    return 0;
}

static int paeth(int a, int b, int c) {
    int p = a + b - c;
    int pa = abs(p - a), pb = abs(p - b), pc = abs(p - c);
    if (pa <= pb && pa <= pc) return a;
    if (pb <= pc) return b;
    return c;
}

static int load_png(const uint8_t *s, size_t n, Image *im) {
    size_t i = 8;
    uint32_t w = 0, h = 0;
    int ctype = -1, depth = 0, interlace = 0;
    Buf idat;
    uint8_t *raw = NULL;
    uLongf raw_n = 0;
    size_t stride, y, x;
    int bpp;
    static const uint8_t sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    buf_init(&idat);
    if (n < 8 || memcmp(s, sig, 8) != 0) return -1;
    while (i + 12 <= n) {
        uint32_t len = (s[i] << 24) | (s[i + 1] << 16) | (s[i + 2] << 8) | s[i + 3];
        const uint8_t *type = s + i + 4;
        const uint8_t *data = s + i + 8;
        if (i + 12 + len > n) {
            buf_free(&idat);
            recto_seterr("broken PNG");
            return -1;
        }
        if (memcmp(type, "IHDR", 4) == 0 && len >= 13) {
            w = (data[0] << 24) | (data[1] << 16) | (data[2] << 8) | data[3];
            h = (data[4] << 24) | (data[5] << 16) | (data[6] << 8) | data[7];
            depth = data[8];
            ctype = data[9];
            interlace = data[12];
        } else if (memcmp(type, "IDAT", 4) == 0) {
            buf_append(&idat, data, len);
        } else if (memcmp(type, "IEND", 4) == 0) {
            break;
        }
        i += 12 + len;
    }
    if (depth != 8 || interlace != 0 || (ctype != 0 && ctype != 2) || w == 0 || h == 0) {
        buf_free(&idat);
        recto_seterr("PNG must be 8-bit gray or RGB, without interlacing");
        return -1;
    }
    bpp = ctype == 2 ? 3 : 1;
    stride = (size_t)w * (size_t)bpp + 1;
    raw_n = (uLongf)(stride * h);
    raw = xmalloc(raw_n);
    if (uncompress(raw, &raw_n, (const Bytef *)idat.p, (uLong)idat.n) != Z_OK || raw_n < stride * h) {
        free(raw);
        buf_free(&idat);
        recto_seterr("PNG inflate failed");
        return -1;
    }
    buf_free(&idat);
    if (image_alloc(im, (int)w, (int)h, 255) != 0) {
        free(raw);
        return -1;
    }
    for (y = 0; y < h; y++) {
        uint8_t *row = raw + y * stride;
        uint8_t *prev = y ? raw + (y - 1) * stride : NULL;
        int filter = row[0];
        uint8_t *d = row + 1;
        for (x = 0; x < (size_t)w * (size_t)bpp; x++) {
            int left = x >= (size_t)bpp ? d[x - (size_t)bpp] : 0;
            int up = prev ? prev[1 + x] : 0;
            int ul = (prev && x >= (size_t)bpp) ? prev[1 + x - (size_t)bpp] : 0;
            int v = d[x];
            if (filter == 1) v += left;
            else if (filter == 2) v += up;
            else if (filter == 3) v += (left + up) / 2;
            else if (filter == 4) v += paeth(left, up, ul);
            d[x] = (uint8_t)v;
        }
        if (bpp == 1) memcpy(im->px + y * w, d, w);
        else {
            for (x = 0; x < w; x++) {
                int r = d[x * 3], g = d[x * 3 + 1], b = d[x * 3 + 2];
                im->px[y * w + x] = (uint8_t)((r * 30 + g * 59 + b * 11) / 100);
            }
        }
    }
    free(raw);
    return 0;
}
#endif

int image_load(const char *path, Image *im) {
    Buf b;
    int rc = -1;
    memset(im, 0, sizeof *im);
    buf_init(&b);
    if (file_read(path, &b) != 0) return -1;
    if (b.n >= 2 && b.p[0] == 'P' && b.p[1] >= '1' && b.p[1] <= '6') rc = load_pnm((uint8_t *)b.p, b.n, im);
    else if (b.n >= 8 && (unsigned char)b.p[0] == 137 && memcmp(b.p + 1, "PNG", 3) == 0)
        rc = load_png((uint8_t *)b.p, b.n, im);
    else if (b.n >= 2 && b.p[0] == 'B' && b.p[1] == 'M') rc = load_bmp((uint8_t *)b.p, b.n, im);
    else recto_seterr("not a PNM, PNG, or BMP: %s", path);
    buf_free(&b);
    return rc;
}

int image_save_pgm(const Image *im, const char *path) {
    FILE *f;
    char head[64];
    int n;
    if (!im || !im->px) return -1;
    f = fopen(path, "wb");
    if (!f) {
        recto_seterr("cannot write %s", path);
        return -1;
    }
    n = snprintf(head, sizeof head, "P5\n%d %d\n255\n", im->w, im->h);
    if (fwrite(head, 1, (size_t)n, f) != (size_t)n ||
        fwrite(im->px, 1, (size_t)im->w * (size_t)im->h, f) != (size_t)im->w * (size_t)im->h) {
        fclose(f);
        recto_seterr("short write on %s", path);
        return -1;
    }
    fclose(f);
    return 0;
}

int image_crop(const Image *src, int x, int y, int w, int h, Image *dst) {
    int yy;
    if (!src || !src->px) return -1;
    if (x < 0) {
        w += x;
        x = 0;
    }
    if (y < 0) {
        h += y;
        y = 0;
    }
    if (x + w > src->w) w = src->w - x;
    if (y + h > src->h) h = src->h - y;
    if (w <= 0 || h <= 0) {
        recto_seterr("rectangle is outside the page");
        return -1;
    }
    if (image_alloc(dst, w, h, 255) != 0) return -1;
    for (yy = 0; yy < h; yy++)
        memcpy(dst->px + (size_t)yy * (size_t)w, src->px + (size_t)(y + yy) * (size_t)src->w + x, (size_t)w);
    return 0;
}

void image_invert(Image *im) {
    size_t i, n = (size_t)im->w * (size_t)im->h;
    for (i = 0; i < n; i++) im->px[i] = (uint8_t)(255 - im->px[i]);
}

void image_contrast(Image *im) {
    size_t i, n = (size_t)im->w * (size_t)im->h;
    unsigned hist[256];
    size_t seen = 0, lo_n, hi_n;
    int lo = 0, hi = 255, v;
    if (!im || n == 0) return;
    memset(hist, 0, sizeof hist);
    for (i = 0; i < n; i++) hist[im->px[i]]++;
    lo_n = n / 50;
    hi_n = n - lo_n;
    if (lo_n < 1) lo_n = 1;
    for (v = 0; v < 256; v++) {
        seen += hist[v];
        if (seen >= lo_n) {
            lo = v;
            break;
        }
    }
    seen = 0;
    for (v = 0; v < 256; v++) {
        seen += hist[v];
        if (seen >= hi_n) {
            hi = v;
            break;
        }
    }
    if (hi <= lo + 8) return;
    for (i = 0; i < n; i++) {
        int g = (im->px[i] - lo) * 255 / (hi - lo);
        if (g < 0) g = 0;
        if (g > 255) g = 255;
        im->px[i] = (uint8_t)g;
    }
}

void image_otsu(Image *im) {
    size_t i, n = (size_t)im->w * (size_t)im->h;
    unsigned hist[256];
    double sum = 0, sumb = 0, wb = 0, wf, maxv = -1;
    int t, best = 128, thr;
    memset(hist, 0, sizeof hist);
    for (i = 0; i < n; i++) hist[im->px[i]]++;
    for (t = 0; t < 256; t++) sum += (double)t * hist[t];
    for (t = 0; t < 256; t++) {
        double m, between;
        wb += hist[t];
        if (wb == 0) continue;
        wf = (double)n - wb;
        if (wf == 0) break;
        sumb += (double)t * hist[t];
        m = sumb / wb - (sum - sumb) / wf;
        between = wb * wf * m * m;
        if (between > maxv) {
            maxv = between;
            best = t;
        }
    }
    thr = best;
    for (i = 0; i < n; i++) im->px[i] = im->px[i] <= thr ? 0 : 255;
}

int image_deskew(Image *im) {
    int h, w, tenths, best = 0, best_var = -1;
    size_t ink = 0, i, n;
    uint8_t *src;
    if (!im || im->w < 80 || im->h < 80) return 0;
    n = (size_t)im->w * (size_t)im->h;
    for (i = 0; i < n; i++)
        if (im->px[i] < 128) ink++;
    if (ink < 50 || ink * 2 > n) return 0;
    w = im->w;
    h = im->h;
    src = im->px;
    for (tenths = -50; tenths <= 50; tenths += 5) {
        int *rows = calloc((size_t)h, sizeof(int));
        int y, x;
        double mean, var = 0;
        int used = 0;
        if (!rows) return 0;
        for (y = 0; y < h; y += 2) {
            const uint8_t *row = src + (size_t)y * (size_t)w;
            for (x = 0; x < w; x += 2) {
                int yy;
                if (row[x] >= 128) continue;
                yy = y + ((((x - w / 2) * tenths) * 114) >> 16);
                if (yy >= 0 && yy < h) rows[yy]++;
            }
        }
        for (y = 0; y < h; y++)
            if (rows[y]) used++;
        mean = 0;
        for (y = 0; y < h; y++) mean += rows[y];
        mean /= h;
        for (y = 0; y < h; y++) {
            double d = rows[y] - mean;
            var += d * d;
        }
        (void)used;
        if (var > best_var) {
            best_var = (int)var;
            best = tenths;
        }
        free(rows);
    }
    if (best == 0) return 0;
    {
        uint8_t *dst = xmalloc(n);
        int y, x;
        memset(dst, 255, n);
        for (y = 0; y < h; y++) {
            for (x = 0; x < w; x++) {
                int yy = y + ((((x - w / 2) * best) * 114) >> 16);
                if (yy >= 0 && yy < h) dst[(size_t)yy * (size_t)w + x] = src[(size_t)y * (size_t)w + x];
            }
        }
        free(im->px);
        im->px = dst;
    }
    return best;
}

void image_fit(Image *im, int max_edge) {
    int w, h, step, nw, nh, y, x;
    uint8_t *dst;
    if (!im || max_edge < 16) return;
    w = im->w;
    h = im->h;
    if (w <= max_edge && h <= max_edge) return;
    step = 1;
    while (w / step > max_edge || h / step > max_edge) step++;
    nw = w / step;
    nh = h / step;
    if (nw < 1) nw = 1;
    if (nh < 1) nh = 1;
    dst = xmalloc((size_t)nw * (size_t)nh);
    for (y = 0; y < nh; y++) {
        for (x = 0; x < nw; x++) dst[(size_t)y * (size_t)nw + x] = im->px[(size_t)(y * step) * (size_t)w + (size_t)(x * step)];
    }
    free(im->px);
    im->px = dst;
    im->w = nw;
    im->h = nh;
}

int image_blobs(const Image *im, Blob **out, int *nout) {
    int w, h, x, y, n = 0, cap = 0;
    size_t npx;
    uint8_t *seen;
    Blob *blobs = NULL;
    int *stack;
    if (!im || !im->px) return -1;
    w = im->w;
    h = im->h;
    npx = (size_t)w * (size_t)h;
    seen = xmalloc(npx);
    memset(seen, 0, npx);
    stack = xmalloc(npx * sizeof(int));
    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) {
            size_t start = (size_t)y * (size_t)w + (size_t)x;
            int sp = 0, area = 0;
            int x0, y0, x1, y1;
            if (seen[start] || im->px[start] >= 128) continue;
            x0 = x1 = x;
            y0 = y1 = y;
            stack[sp++] = (int)start;
            seen[start] = 1;
            while (sp) {
                int p = stack[--sp];
                int px = p % w;
                int py = p / w;
                int dy, dx;
                area++;
                if (px < x0) x0 = px;
                if (py < y0) y0 = py;
                if (px > x1) x1 = px;
                if (py > y1) y1 = py;
                for (dy = -1; dy <= 1; dy++) {
                    for (dx = -1; dx <= 1; dx++) {
                        int nx, ny;
                        size_t ni;
                        if (!dx && !dy) continue;
                        nx = px + dx;
                        ny = py + dy;
                        if (nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
                        ni = (size_t)ny * (size_t)w + (size_t)nx;
                        if (seen[ni] || im->px[ni] >= 128) continue;
                        seen[ni] = 1;
                        stack[sp++] = (int)ni;
                    }
                }
            }
            if (area < 4) continue;
            if (n >= cap) {
                cap = cap ? cap * 2 : 64;
                blobs = xrealloc(blobs, (size_t)cap * sizeof *blobs);
            }
            blobs[n].x0 = x0;
            blobs[n].y0 = y0;
            blobs[n].x1 = x1;
            blobs[n].y1 = y1;
            blobs[n].area = area;
            n++;
            if (n >= 20000) goto done;
        }
    }
done:
    free(seen);
    free(stack);
    *out = blobs;
    *nout = n;
    return 0;
}
