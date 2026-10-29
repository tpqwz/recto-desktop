#include "recto.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef RECTO_NO_ZLIB
#include <zlib.h>
#endif

enum { VK_NULL, VK_BOOL, VK_NUM, VK_NAME, VK_STR, VK_REF, VK_ARR, VK_DICT };

typedef struct Val Val;
typedef struct {
    char *k;
    Val *v;
} KV;

struct Val {
    int kind;
    int b;
    double num;
    int ref_n, ref_g;
    unsigned char *s;
    int slen;
    Val **items;
    int nitems, icap;
    KV *kv;
    int nkv, kcap;
};

typedef struct {
    int type; /* 0 free, 1 plain, 2 objstm */
    int gen;
    int a;
    int b;
} XEnt;

typedef struct {
    int parsed;
    int loading;
    Val *v;
    unsigned char *stream;
    size_t slen;
} Slot;

typedef struct {
    unsigned char *buf;
    size_t len;
    XEnt *xref;
    Slot *slot;
    int size;
    int root;
    int info;
} Pdf;

typedef struct {
    const unsigned char *s;
    size_t n, i;
} Pc;

static void val_free(Val *v);

static Val *val_new(int kind) {
    Val *v = xmalloc(sizeof *v);
    memset(v, 0, sizeof *v);
    v->kind = kind;
    return v;
}

static void val_free(Val *v) {
    int i;
    if (!v) return;
    free(v->s);
    for (i = 0; i < v->nitems; i++) val_free(v->items[i]);
    free(v->items);
    for (i = 0; i < v->nkv; i++) {
        free(v->kv[i].k);
        val_free(v->kv[i].v);
    }
    free(v->kv);
    free(v);
}

static void arr_add(Val *a, Val *item) {
    if (a->nitems >= a->icap) {
        a->icap = a->icap ? a->icap * 2 : 4;
        a->items = xrealloc(a->items, (size_t)a->icap * sizeof *a->items);
    }
    a->items[a->nitems++] = item;
}

static void dict_put(Val *d, const char *k, Val *item) {
    int i;
    for (i = 0; i < d->nkv; i++) {
        if (strcmp(d->kv[i].k, k) == 0) {
            val_free(d->kv[i].v);
            d->kv[i].v = item;
            return;
        }
    }
    if (d->nkv >= d->kcap) {
        d->kcap = d->kcap ? d->kcap * 2 : 4;
        d->kv = xrealloc(d->kv, (size_t)d->kcap * sizeof *d->kv);
    }
    d->kv[d->nkv].k = xstrdup(k);
    d->kv[d->nkv].v = item;
    d->nkv++;
}

static Val *dict_get(Val *d, const char *k) {
    int i;
    if (!d || d->kind != VK_DICT) return NULL;
    for (i = 0; i < d->nkv; i++)
        if (strcmp(d->kv[i].k, k) == 0) return d->kv[i].v;
    return NULL;
}

static void skip_ws(Pc *p) {
    for (;;) {
        while (p->i < p->n && isspace(p->s[p->i])) p->i++;
        if (p->i < p->n && p->s[p->i] == '%') {
            while (p->i < p->n && p->s[p->i] != '\n') p->i++;
            continue;
        }
        break;
    }
}

static int is_delim(int c) {
    return isspace(c) || c == '(' || c == ')' || c == '<' || c == '>' || c == '[' || c == ']' || c == '{' || c == '}' ||
           c == '/' || c == '%';
}

static int match_kw(Pc *p, const char *kw) {
    size_t n = strlen(kw);
    size_t i = p->i;
    skip_ws(p);
    if (p->i + n <= p->n && memcmp(p->s + p->i, kw, n) == 0) {
        int end = (p->i + n >= p->n) || is_delim(p->s[p->i + n]);
        if (end) {
            p->i += n;
            return 1;
        }
    }
    p->i = i;
    return 0;
}

static char *parse_name(Pc *p) {
    Buf b;
    buf_init(&b);
    if (p->i < p->n && p->s[p->i] == '/') p->i++;
    while (p->i < p->n && !is_delim(p->s[p->i])) {
        unsigned char c = p->s[p->i++];
        if (c == '#' && p->i + 1 < p->n && isxdigit(p->s[p->i]) && isxdigit(p->s[p->i + 1])) {
            char hex[3] = {(char)p->s[p->i], (char)p->s[p->i + 1], 0};
            c = (unsigned char)strtol(hex, NULL, 16);
            p->i += 2;
        }
        buf_putc(&b, c);
    }
    return buf_steal(&b);
}

static int hex_nibble(int c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static unsigned char *parse_lit_string(Pc *p, int *outn) {
    Buf b;
    int depth = 1;
    buf_init(&b);
    if (p->i < p->n && p->s[p->i] == '(') p->i++;
    while (p->i < p->n && depth) {
        unsigned char c = p->s[p->i++];
        if (c == '\\') {
            if (p->i >= p->n) break;
            c = p->s[p->i++];
            if (c == 'n') buf_putc(&b, '\n');
            else if (c == 'r') buf_putc(&b, '\r');
            else if (c == 't') buf_putc(&b, '\t');
            else if (c == 'b') buf_putc(&b, '\b');
            else if (c == 'f') buf_putc(&b, '\f');
            else if (c >= '0' && c <= '7') {
                int v = c - '0', k;
                for (k = 0; k < 2 && p->i < p->n && p->s[p->i] >= '0' && p->s[p->i] <= '7'; k++)
                    v = (v << 3) + (p->s[p->i++] - '0');
                buf_putc(&b, v);
            } else if (c == '\n') {
            } else if (c == '\r') {
                if (p->i < p->n && p->s[p->i] == '\n') p->i++;
            } else buf_putc(&b, c);
        } else if (c == '(') {
            depth++;
            buf_putc(&b, c);
        } else if (c == ')') {
            depth--;
            if (depth) buf_putc(&b, c);
        } else buf_putc(&b, c);
    }
    *outn = (int)b.n;
    return (unsigned char *)buf_steal(&b);
}

static unsigned char *parse_hex_string(Pc *p, int *outn) {
    Buf b;
    int hi = -1;
    buf_init(&b);
    if (p->i < p->n && p->s[p->i] == '<') p->i++;
    while (p->i < p->n && p->s[p->i] != '>') {
        int n = hex_nibble(p->s[p->i]);
        if (n >= 0) {
            if (hi < 0) hi = n;
            else {
                buf_putc(&b, (hi << 4) | n);
                hi = -1;
            }
        }
        p->i++;
    }
    if (hi >= 0) buf_putc(&b, hi << 4);
    if (p->i < p->n && p->s[p->i] == '>') p->i++;
    *outn = (int)b.n;
    return (unsigned char *)buf_steal(&b);
}

static int parse_number(Pc *p, double *out) {
    size_t start;
    char tmp[64];
    char *end = NULL;
    skip_ws(p);
    start = p->i;
    if (p->i < p->n && (p->s[p->i] == '+' || p->s[p->i] == '-')) p->i++;
    while (p->i < p->n && isdigit(p->s[p->i])) p->i++;
    if (p->i < p->n && p->s[p->i] == '.') {
        p->i++;
        while (p->i < p->n && isdigit(p->s[p->i])) p->i++;
    }
    if (p->i == start || p->i - start >= sizeof tmp) return 0;
    memcpy(tmp, p->s + start, p->i - start);
    tmp[p->i - start] = 0;
    *out = strtod(tmp, &end);
    return end != tmp;
}

static Val *parse_value(Pc *p, int depth);

static Val *parse_array(Pc *p, int depth) {
    Val *a = val_new(VK_ARR);
    if (p->i < p->n && p->s[p->i] == '[') p->i++;
    for (;;) {
        Val *item;
        skip_ws(p);
        if (p->i >= p->n) break;
        if (p->s[p->i] == ']') {
            p->i++;
            break;
        }
        item = parse_value(p, depth + 1);
        if (!item) break;
        arr_add(a, item);
    }
    return a;
}

static Val *parse_dict(Pc *p, int depth) {
    Val *d = val_new(VK_DICT);
    /* The trailer keyword and the dictionary are often on different lines. */
    skip_ws(p);
    if (p->i + 1 < p->n && p->s[p->i] == '<' && p->s[p->i + 1] == '<') p->i += 2;
    for (;;) {
        char *name;
        Val *item;
        skip_ws(p);
        if (p->i + 1 < p->n && p->s[p->i] == '>' && p->s[p->i + 1] == '>') {
            p->i += 2;
            break;
        }
        if (p->i >= p->n || p->s[p->i] != '/') break;
        name = parse_name(p);
        item = parse_value(p, depth + 1);
        if (item) dict_put(d, name, item);
        free(name);
    }
    return d;
}

static Val *parse_value(Pc *p, int depth) {
    double num;
    size_t mark;
    if (depth > 40) return val_new(VK_NULL);
    skip_ws(p);
    if (p->i >= p->n) return val_new(VK_NULL);
    if (p->s[p->i] == '/') {
        Val *v = val_new(VK_NAME);
        char *n = parse_name(p);
        v->s = (unsigned char *)n;
        v->slen = (int)strlen(n);
        return v;
    }
    if (p->s[p->i] == '(') {
        Val *v = val_new(VK_STR);
        v->s = parse_lit_string(p, &v->slen);
        return v;
    }
    if (p->s[p->i] == '<' && p->i + 1 < p->n && p->s[p->i + 1] == '<') return parse_dict(p, depth);
    if (p->s[p->i] == '<') {
        Val *v = val_new(VK_STR);
        v->s = parse_hex_string(p, &v->slen);
        return v;
    }
    if (p->s[p->i] == '[') return parse_array(p, depth);
    if (match_kw(p, "true")) {
        Val *v = val_new(VK_BOOL);
        v->b = 1;
        return v;
    }
    if (match_kw(p, "false")) {
        Val *v = val_new(VK_BOOL);
        v->b = 0;
        return v;
    }
    if (match_kw(p, "null")) return val_new(VK_NULL);
    mark = p->i;
    if (parse_number(p, &num)) {
        size_t after = p->i;
        double gen;
        size_t mid;
        skip_ws(p);
        mid = p->i;
        if (parse_number(p, &gen)) {
            skip_ws(p);
            if (p->i < p->n && p->s[p->i] == 'R' && (p->i + 1 >= p->n || is_delim(p->s[p->i + 1]))) {
                Val *v = val_new(VK_REF);
                p->i++;
                v->ref_n = (int)num;
                v->ref_g = (int)gen;
                return v;
            }
        }
        p->i = after;
        (void)mid;
        (void)mark;
        {
            Val *v = val_new(VK_NUM);
            v->num = num;
            return v;
        }
    }
    p->i = mark;
    return val_new(VK_NULL);
}

static void utf8_put(Buf *b, unsigned cp) {
    if (cp < 0x80) buf_putc(b, (int)cp);
    else if (cp < 0x800) {
        buf_putc(b, 0xC0 | (cp >> 6));
        buf_putc(b, 0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        buf_putc(b, 0xE0 | (cp >> 12));
        buf_putc(b, 0x80 | ((cp >> 6) & 0x3F));
        buf_putc(b, 0x80 | (cp & 0x3F));
    } else {
        buf_putc(b, '?');
    }
}

static char *pdf_text_string(const unsigned char *s, int n) {
    Buf b;
    int i;
    buf_init(&b);
    if (!s || n <= 0) return buf_steal(&b);
    if (n >= 2 && s[0] == 0xFE && s[1] == 0xFF) {
        for (i = 2; i + 1 < n; i += 2) utf8_put(&b, ((unsigned)s[i] << 8) | s[i + 1]);
        return buf_steal(&b);
    }
    for (i = 0; i < n; i++) {
        unsigned char c = s[i];
        if (c == '\r') continue;
        utf8_put(&b, c);
    }
    return buf_steal(&b);
}

static void xref_ensure(Pdf *pdf, int size) {
    int i;
    if (size <= pdf->size) return;
    pdf->xref = xrealloc(pdf->xref, (size_t)size * sizeof *pdf->xref);
    pdf->slot = xrealloc(pdf->slot, (size_t)size * sizeof *pdf->slot);
    for (i = pdf->size; i < size; i++) {
        memset(&pdf->xref[i], 0, sizeof pdf->xref[i]);
        memset(&pdf->slot[i], 0, sizeof pdf->slot[i]);
    }
    pdf->size = size;
}

#ifdef RECTO_NO_ZLIB
static int flate_decode(const unsigned char *in, size_t n, unsigned char **out, size_t *outn) {
    (void)in;
    (void)n;
    (void)out;
    (void)outn;
    recto_seterr("this build has no zlib, so it cannot read a compressed PDF stream");
    return -1;
}
#else
static int flate_decode(const unsigned char *in, size_t n, unsigned char **out, size_t *outn) {
    z_stream z;
    size_t cap = n * 4 + 64;
    unsigned char *buf;
    int rc;
    if (cap < 256) cap = 256;
    buf = xmalloc(cap);
    memset(&z, 0, sizeof z);
    z.next_in = (Bytef *)in;
    z.avail_in = (uInt)n;
    if (inflateInit(&z) != Z_OK) {
        free(buf);
        recto_seterr("cannot start flate decoder");
        return -1;
    }
    for (;;) {
        z.next_out = buf + z.total_out;
        z.avail_out = (uInt)(cap - z.total_out);
        rc = inflate(&z, Z_NO_FLUSH);
        if (rc == Z_STREAM_END) break;
        if (rc != Z_OK) {
            inflateEnd(&z);
            free(buf);
            recto_seterr("flate stream is damaged");
            return -1;
        }
        if (z.avail_out == 0) {
            if (cap > 64ul * 1024ul * 1024ul) {
                inflateEnd(&z);
                free(buf);
                recto_seterr("flate stream is too large");
                return -1;
            }
            cap *= 2;
            buf = xrealloc(buf, cap);
        }
    }
    *outn = z.total_out;
    *out = buf;
    inflateEnd(&z);
    return 0;
}
#endif

static int a85_decode(const unsigned char *in, size_t n, unsigned char **out, size_t *outn) {
    Buf b;
    unsigned val = 0;
    int k = 0, i;
    buf_init(&b);
    for (i = 0; i < (int)n; i++) {
        unsigned char c = in[i];
        if (isspace(c)) continue;
        if (c == '~') break;
        if (c == 'z' && k == 0) {
            buf_append(&b, "\0\0\0\0", 4);
            continue;
        }
        if (c < 33 || c > 117) continue;
        val = val * 85 + (c - 33);
        k++;
        if (k == 5) {
            unsigned char chunk[4];
            chunk[0] = (unsigned char)(val >> 24);
            chunk[1] = (unsigned char)(val >> 16);
            chunk[2] = (unsigned char)(val >> 8);
            chunk[3] = (unsigned char)val;
            buf_append(&b, chunk, 4);
            val = 0;
            k = 0;
        }
    }
    if (k) {
        int j;
        unsigned char chunk[4];
        for (j = k; j < 5; j++) val = val * 85 + 84;
        chunk[0] = (unsigned char)(val >> 24);
        chunk[1] = (unsigned char)(val >> 16);
        chunk[2] = (unsigned char)(val >> 8);
        chunk[3] = (unsigned char)val;
        buf_append(&b, chunk, (size_t)(k - 1));
    }
    *outn = b.n;
    *out = (unsigned char *)buf_steal(&b);
    return 0;
}

static int hex_decode(const unsigned char *in, size_t n, unsigned char **out, size_t *outn) {
    Pc p;
    int len = 0;
    p.s = in;
    p.n = n;
    p.i = 0;
    *out = parse_hex_string(&p, &len);
    *outn = (size_t)len;
    return 0;
}

static int apply_predictor(Val *dict, unsigned char **data, size_t *n) {
    Val *parms, *pred, *cols, *colors, *bpc;
    int predictor = 1, columns = 1, ncol = 1, bits = 8;
    int row_bytes, stride, rows, y, x;
    unsigned char *raw, *out;
    if (!dict) return 0;
    parms = dict_get(dict, "DecodeParms");
    if (!parms && dict_get(dict, "DP")) parms = dict_get(dict, "DP");
    if (!parms || parms->kind != VK_DICT) return 0;
    pred = dict_get(parms, "Predictor");
    if (pred && pred->kind == VK_NUM) predictor = (int)pred->num;
    if (predictor <= 1) return 0;
    cols = dict_get(parms, "Columns");
    colors = dict_get(parms, "Colors");
    bpc = dict_get(parms, "BitsPerComponent");
    if (cols && cols->kind == VK_NUM) columns = (int)cols->num;
    if (colors && colors->kind == VK_NUM) ncol = (int)colors->num;
    if (bpc && bpc->kind == VK_NUM) bits = (int)bpc->num;
    if (columns < 1) columns = 1;
    if (ncol < 1) ncol = 1;
    if (bits < 1) bits = 8;
    row_bytes = (columns * ncol * bits + 7) / 8;
    stride = row_bytes + 1;
    if (stride <= 1 || *n < (size_t)stride) return 0;
    rows = (int)(*n / (size_t)stride);
    raw = *data;
    out = xmalloc((size_t)rows * (size_t)row_bytes);
    for (y = 0; y < rows; y++) {
        unsigned char *src = raw + (size_t)y * (size_t)stride;
        unsigned char *dst = out + (size_t)y * (size_t)row_bytes;
        unsigned char *prev = y ? out + (size_t)(y - 1) * (size_t)row_bytes : NULL;
        int filter = src[0];
        if (predictor >= 10 && predictor <= 14) filter = predictor - 10;
        for (x = 0; x < row_bytes; x++) {
            int left = x ? dst[x - 1] : 0;
            int up = prev ? prev[x] : 0;
            int ul = (prev && x) ? prev[x - 1] : 0;
            int v = src[1 + x];
            if (filter == 1) v += left;
            else if (filter == 2) v += up;
            else if (filter == 3) v += (left + up) / 2;
            else if (filter == 4) {
                int p = left + up - ul;
                int pa = abs(p - left), pb = abs(p - up), pc = abs(p - ul);
                int pr = (pa <= pb && pa <= pc) ? left : (pb <= pc ? up : ul);
                v += pr;
            }
            dst[x] = (unsigned char)v;
        }
    }
    free(raw);
    *data = out;
    *n = (size_t)rows * (size_t)row_bytes;
    return 0;
}

static int decode_filters(Val *dict, unsigned char *raw, size_t n, unsigned char **out, size_t *outn) {
    Val *filt = dict ? dict_get(dict, "Filter") : NULL;
    Val *names[8];
    int nf = 0, i;
    unsigned char *cur = NULL;
    size_t cn = 0;
    if (!filt) {
        cur = xmalloc(n ? n : 1);
        if (n) memcpy(cur, raw, n);
        *out = cur;
        *outn = n;
        return apply_predictor(dict, out, outn);
    }
    if (filt->kind == VK_NAME) names[nf++] = filt;
    else if (filt->kind == VK_ARR) {
        for (i = 0; i < filt->nitems && nf < 8; i++) names[nf++] = filt->items[i];
    }
    cur = xmalloc(n ? n : 1);
    if (n) memcpy(cur, raw, n);
    cn = n;
    for (i = 0; i < nf; i++) {
        const char *nm = (names[i] && names[i]->kind == VK_NAME) ? (char *)names[i]->s : "";
        unsigned char *next = NULL;
        size_t nn = 0;
        int rc = 0;
        if (strcmp(nm, "FlateDecode") == 0 || strcmp(nm, "Fl") == 0) rc = flate_decode(cur, cn, &next, &nn);
        else if (strcmp(nm, "ASCII85Decode") == 0 || strcmp(nm, "A85") == 0) rc = a85_decode(cur, cn, &next, &nn);
        else if (strcmp(nm, "ASCIIHexDecode") == 0 || strcmp(nm, "AHx") == 0) rc = hex_decode(cur, cn, &next, &nn);
        else {
            free(cur);
            recto_seterr("PDF filter %s is not supported", nm[0] ? nm : "(unknown)");
            return -1;
        }
        free(cur);
        if (rc != 0) return -1;
        cur = next;
        cn = nn;
    }
    *out = cur;
    *outn = cn;
    return apply_predictor(dict, out, outn);
}

static int load_object_at(Pdf *pdf, int num, size_t off);

static Val *resolve(Pdf *pdf, Val *v) {
    if (!v) return NULL;
    if (v->kind != VK_REF) return v;
    if (v->ref_n <= 0 || v->ref_n >= pdf->size) return NULL;
    if (!pdf->slot[v->ref_n].parsed) {
        if (pdf->xref[v->ref_n].type == 1) {
            if (load_object_at(pdf, v->ref_n, (size_t)pdf->xref[v->ref_n].a) != 0) return NULL;
        } else if (pdf->xref[v->ref_n].type == 2) {
            if (load_object_at(pdf, v->ref_n, 0) != 0) return NULL;
        } else return NULL;
    }
    return pdf->slot[v->ref_n].v;
}

static int parse_objstm(Pdf *pdf, int stm_num) {
    Slot *slot;
    Val *v, *nval, *first;
    int count, i, data_at;
    Pc p;
    int *nums, *offs;
    if (stm_num <= 0 || stm_num >= pdf->size) return -1;
    if (!pdf->slot[stm_num].parsed) {
        if (pdf->xref[stm_num].type != 1) return -1;
        if (load_object_at(pdf, stm_num, (size_t)pdf->xref[stm_num].a) != 0) return -1;
    }
    slot = &pdf->slot[stm_num];
    v = slot->v;
    if (!v || !slot->stream) return -1;
    nval = dict_get(v, "N");
    first = dict_get(v, "First");
    if (!nval || !first) return -1;
    count = (int)nval->num;
    data_at = (int)first->num;
    if (count < 0 || count > 100000 || data_at < 0 || (size_t)data_at > slot->slen) return -1;
    nums = xmalloc((size_t)(count > 0 ? count : 1) * sizeof(int));
    offs = xmalloc((size_t)(count > 0 ? count : 1) * sizeof(int));
    p.s = slot->stream;
    p.n = slot->slen;
    p.i = 0;
    for (i = 0; i < count; i++) {
        double a, b;
        if (!parse_number(&p, &a) || !parse_number(&p, &b)) {
            free(nums);
            free(offs);
            return -1;
        }
        nums[i] = (int)a;
        offs[i] = (int)b;
    }
    for (i = 0; i < count; i++) {
        int on = nums[i];
        size_t start, end;
        Pc op;
        if (on <= 0 || on >= pdf->size) continue;
        if (pdf->xref[on].type != 2 || pdf->xref[on].a != stm_num) continue;
        if (pdf->slot[on].parsed) continue;
        start = (size_t)data_at + (size_t)offs[i];
        end = (i + 1 < count) ? (size_t)data_at + (size_t)offs[i + 1] : slot->slen;
        if (start > slot->slen || end > slot->slen || end < start) continue;
        op.s = slot->stream + start;
        op.n = end - start;
        op.i = 0;
        pdf->slot[on].v = parse_value(&op, 0);
        pdf->slot[on].parsed = 1;
    }
    free(nums);
    free(offs);
    return 0;
}

static int load_object_at(Pdf *pdf, int num, size_t off) {
    Pc p;
    double on, gen;
    Val *v;
    Slot *slot;
    if (num <= 0 || num >= pdf->size) return -1;
    slot = &pdf->slot[num];
    if (slot->parsed || slot->loading) return slot->parsed ? 0 : -1;
    if (pdf->xref[num].type == 2) {
        slot->loading = 1;
        if (parse_objstm(pdf, pdf->xref[num].a) != 0) {
            slot->loading = 0;
            return -1;
        }
        slot->loading = 0;
        return slot->parsed ? 0 : -1;
    }
    if (off >= pdf->len) return -1;
    slot->loading = 1;
    p.s = pdf->buf;
    p.n = pdf->len;
    p.i = off;
    if (!parse_number(&p, &on) || !parse_number(&p, &gen) || !match_kw(&p, "obj")) {
        slot->loading = 0;
        recto_seterr("PDF object %d is not at the cross-reference offset", num);
        return -1;
    }
    v = parse_value(&p, 0);
    skip_ws(&p);
    if (match_kw(&p, "stream")) {
        Val *lenv;
        int length = -1;
        size_t start;
        if (p.i < p.n && p.s[p.i] == '\r') p.i++;
        if (p.i < p.n && p.s[p.i] == '\n') p.i++;
        start = p.i;
        lenv = v ? dict_get(v, "Length") : NULL;
        if (lenv && lenv->kind == VK_REF) lenv = resolve(pdf, lenv);
        if (lenv && lenv->kind == VK_NUM) length = (int)lenv->num;
        if (length < 0 || start + (size_t)length > pdf->len) {
            /* Hunt for endstream if the length is missing. */
            const unsigned char *hit = NULL;
            if (start < pdf->len) {
                const unsigned char *s = pdf->buf + start;
                size_t remain = pdf->len - start;
                size_t k;
                for (k = 0; k + 9 < remain; k++) {
                    if (memcmp(s + k, "endstream", 9) == 0) {
                        hit = s + k;
                        break;
                    }
                }
            }
            if (!hit) {
                val_free(v);
                slot->loading = 0;
                recto_seterr("PDF stream %d has no end", num);
                return -1;
            }
            length = (int)(hit - (pdf->buf + start));
            while (length > 0 && (pdf->buf[start + (size_t)length - 1] == '\n' || pdf->buf[start + (size_t)length - 1] == '\r'))
                length--;
        }
        if (decode_filters(v, pdf->buf + start, (size_t)length, &slot->stream, &slot->slen) != 0) {
            val_free(v);
            slot->loading = 0;
            return -1;
        }
    }
    slot->v = v;
    slot->parsed = 1;
    slot->loading = 0;
    return 0;
}

static int apply_xref_stream(Pdf *pdf, Val *dict, const unsigned char *data, size_t n) {
    Val *W, *Index, *Size, *Root, *Info;
    int w0 = 1, w1 = 2, w2 = 1;
    int size, pos = 0, pair;
    int start = 0, count = 0;
    if (!dict) return -1;
    W = dict_get(dict, "W");
    Size = dict_get(dict, "Size");
    Index = dict_get(dict, "Index");
    Root = dict_get(dict, "Root");
    Info = dict_get(dict, "Info");
    if (Size && Size->kind == VK_NUM) size = (int)Size->num;
    else size = pdf->size;
    if (size < 1) return -1;
    xref_ensure(pdf, size);
    if (W && W->kind == VK_ARR && W->nitems >= 3) {
        w0 = W->items[0]->kind == VK_NUM ? (int)W->items[0]->num : 0;
        w1 = W->items[1]->kind == VK_NUM ? (int)W->items[1]->num : 0;
        w2 = W->items[2]->kind == VK_NUM ? (int)W->items[2]->num : 0;
    }
    if (Root && Root->kind == VK_REF) pdf->root = Root->ref_n;
    if (Info && Info->kind == VK_REF) pdf->info = Info->ref_n;
    if (!Index || Index->kind != VK_ARR) {
        start = 0;
        count = size;
        Index = NULL;
    }
    pair = 0;
    if (Index) {
        if (Index->nitems < 2) return -1;
        start = (int)Index->items[0]->num;
        count = (int)Index->items[1]->num;
        pair = 2;
    }
    while (count > 0 || (Index && pair < Index->nitems)) {
        int e;
        if (count == 0 && Index && pair + 1 < Index->nitems) {
            start = (int)Index->items[pair]->num;
            count = (int)Index->items[pair + 1]->num;
            pair += 2;
        }
        for (e = 0; e < count; e++) {
            int f, type, a = 0, b = 0;
            unsigned val;
            if (pos + w0 + w1 + w2 > (int)n) return -1;
            type = 1;
            if (w0) {
                val = 0;
                for (f = 0; f < w0; f++) val = (val << 8) | data[pos++];
                type = (int)val;
            }
            val = 0;
            for (f = 0; f < w1; f++) val = (val << 8) | data[pos++];
            a = (int)val;
            val = 0;
            for (f = 0; f < w2; f++) val = (val << 8) | data[pos++];
            b = (int)val;
            if (start + e > 0 && start + e < pdf->size && type != 0) {
                pdf->xref[start + e].type = type;
                pdf->xref[start + e].a = a;
                pdf->xref[start + e].b = b;
                pdf->xref[start + e].gen = (type == 1) ? b : 0;
            }
        }
        count = 0;
        if (!Index || pair >= Index->nitems) break;
    }
    return 0;
}

static int load_xref_at(Pdf *pdf, size_t off, int depth);

static int load_classic_xref(Pdf *pdf, Pc *p) {
    Val *trailer, *root, *info, *size, *prev, *enc;
    int *nums = NULL, *offs = NULL, *gens = NULL, nent = 0, ecap = 0;
    int i;
    if (!match_kw(p, "xref")) {
        recto_seterr("xref keyword missing");
        return -1;
    }
    for (;;) {
        double start, count;
        size_t mark;
        skip_ws(p);
        mark = p->i;
        if (!parse_number(p, &start)) {
            p->i = mark;
            break;
        }
        if (!parse_number(p, &count)) {
            free(nums);
            free(offs);
            free(gens);
            recto_seterr("xref subsection is truncated");
            return -1;
        }
        for (i = 0; i < (int)count; i++) {
            double ofs, gen;
            int num = (int)start + i;
            skip_ws(p);
            if (!parse_number(p, &ofs) || !parse_number(p, &gen)) {
                free(nums);
                free(offs);
                free(gens);
                recto_seterr("xref entry %d is truncated", num);
                return -1;
            }
            skip_ws(p);
            if (p->i < p->n && p->s[p->i] == 'n' && num > 0) {
                if (nent >= ecap) {
                    ecap = ecap ? ecap * 2 : 16;
                    nums = xrealloc(nums, (size_t)ecap * sizeof(int));
                    offs = xrealloc(offs, (size_t)ecap * sizeof(int));
                    gens = xrealloc(gens, (size_t)ecap * sizeof(int));
                }
                nums[nent] = num;
                offs[nent] = (int)ofs;
                gens[nent] = (int)gen;
                nent++;
            }
            while (p->i < p->n && p->s[p->i] != '\n') p->i++;
        }
    }
    if (!match_kw(p, "trailer")) {
        free(nums);
        free(offs);
        free(gens);
        recto_seterr("PDF trailer is missing");
        return -1;
    }
    trailer = parse_dict(p, 0);
    if (!trailer) {
        free(nums);
        free(offs);
        free(gens);
        return -1;
    }
    enc = dict_get(trailer, "Encrypt");
    if (enc) {
        val_free(trailer);
        free(nums);
        free(offs);
        free(gens);
        recto_seterr("this PDF is encrypted");
        return -1;
    }
    prev = dict_get(trailer, "Prev");
    if (prev && prev->kind == VK_NUM) load_xref_at(pdf, (size_t)prev->num, 1);
    size = dict_get(trailer, "Size");
    if (size && size->kind == VK_NUM) xref_ensure(pdf, (int)size->num);
    root = dict_get(trailer, "Root");
    info = dict_get(trailer, "Info");
    if (root && root->kind == VK_REF) pdf->root = root->ref_n;
    if (info && info->kind == VK_REF) pdf->info = info->ref_n;
    if (pdf->root <= 0) recto_seterr("PDF trailer has no root");
    for (i = 0; i < nent; i++) {
        if (nums[i] >= pdf->size) xref_ensure(pdf, nums[i] + 1);
        pdf->xref[nums[i]].type = 1;
        pdf->xref[nums[i]].a = offs[i];
        pdf->xref[nums[i]].gen = gens[i];
    }
    free(nums);
    free(offs);
    free(gens);
    val_free(trailer);
    return 0;
}

static int load_xref_at(Pdf *pdf, size_t off, int depth) {
    Pc p;
    if (depth > 16 || off >= pdf->len) {
        recto_seterr("cross-reference offset %lu is outside the file", (unsigned long)off);
        return -1;
    }
    p.s = pdf->buf;
    p.n = pdf->len;
    p.i = off;
    skip_ws(&p);
    if (p.i + 4 <= p.n && memcmp(p.s + p.i, "xref", 4) == 0) return load_classic_xref(pdf, &p);
    {
        double on, gen;
        Val *v;
        size_t stream_at;
        int length = -1;
        unsigned char *decoded = NULL;
        size_t dn = 0;
        Val *lenv, *typ;
        if (!parse_number(&p, &on) || !parse_number(&p, &gen) || !match_kw(&p, "obj")) return -1;
        v = parse_value(&p, 0);
        skip_ws(&p);
        if (!match_kw(&p, "stream") || !v) {
            val_free(v);
            return -1;
        }
        if (p.i < p.n && p.s[p.i] == '\r') p.i++;
        if (p.i < p.n && p.s[p.i] == '\n') p.i++;
        stream_at = p.i;
        lenv = dict_get(v, "Length");
        if (lenv && lenv->kind == VK_NUM) length = (int)lenv->num;
        if (length < 0) {
            val_free(v);
            return -1;
        }
        typ = dict_get(v, "Type");
        if (!typ || typ->kind != VK_NAME || strcmp((char *)typ->s, "XRef") != 0) {
            val_free(v);
            recto_seterr("startxref does not point at a cross-reference");
            return -1;
        }
        if (decode_filters(v, pdf->buf + stream_at, (size_t)length, &decoded, &dn) != 0) {
            val_free(v);
            return -1;
        }
        {
            Val *prev = dict_get(v, "Prev");
            if (prev && prev->kind == VK_NUM) load_xref_at(pdf, (size_t)prev->num, depth + 1);
        }
        if (apply_xref_stream(pdf, v, decoded, dn) != 0) {
            free(decoded);
            val_free(v);
            return -1;
        }
        free(decoded);
        val_free(v);
        return 0;
    }
}

static size_t find_startxref(const unsigned char *s, size_t n) {
    size_t i, from = n > 8192 ? n - 8192 : 0;
    for (i = n; i > from + 9; i--) {
        if (memcmp(s + i - 9, "startxref", 9) == 0) {
            Pc p;
            double off;
            p.s = s;
            p.n = n;
            p.i = i;
            if (parse_number(&p, &off)) return (size_t)off;
        }
    }
    return 0;
}

typedef struct {
    char name[40];
    int two;
    unsigned short map[256];
    int has_map;
    unsigned int *code;
    unsigned int *uni;
    int nmap, mcap;
} FontMap;

static void font_add(FontMap *f, unsigned code, unsigned uni, int nbytes) {
    if (nbytes <= 1 && code < 256) {
        f->map[code] = (unsigned short)(uni ? uni : 1);
        f->has_map = 1;
        return;
    }
    f->two = 1;
    if (f->nmap >= f->mcap) {
        f->mcap = f->mcap ? f->mcap * 2 : 16;
        f->code = xrealloc(f->code, (size_t)f->mcap * sizeof *f->code);
        f->uni = xrealloc(f->uni, (size_t)f->mcap * sizeof *f->uni);
    }
    f->code[f->nmap] = code;
    f->uni[f->nmap] = uni;
    f->nmap++;
}

static unsigned font_lookup(const FontMap *f, unsigned code, int *found) {
    int i;
    *found = 0;
    if (!f) return code;
    if (!f->two && code < 256 && f->has_map && f->map[code]) {
        *found = 1;
        return f->map[code];
    }
    for (i = 0; i < f->nmap; i++)
        if (f->code[i] == code) {
            *found = 1;
            return f->uni[i];
        }
    if (code < 128) {
        *found = 1;
        return code;
    }
    return code;
}

static unsigned hex_cp(const char *s, int n) {
    unsigned v = 0;
    int i;
    for (i = 0; i < n; i++) {
        int d = hex_nibble((unsigned char)s[i]);
        if (d < 0) break;
        v = (v << 4) | (unsigned)d;
    }
    return v;
}

static void parse_tounicode(const unsigned char *s, size_t n, FontMap *f) {
    const char *t = (const char *)s;
    const char *p = t;
    const char *end = t + n;
    while (p < end) {
        if ((size_t)(end - p) >= 11 && strncmp(p, "beginbfchar", 11) == 0) {
            p += 11;
            while (p < end && strncmp(p, "endbfchar", 9) != 0) {
                const char *a, *b;
                while (p < end && *p != '<' && strncmp(p, "endbfchar", 9) != 0) p++;
                if (p >= end || *p != '<') break;
                a = ++p;
                while (p < end && *p != '>') p++;
                if (p >= end) break;
                while (p < end && *p != '<') p++;
                if (p >= end) break;
                b = ++p;
                while (p < end && *p != '>') p++;
                font_add(f, hex_cp(a, (int)(strchr(a, '>') - a)), hex_cp(b, (int)(p - b)), (int)(strchr(a, '>') - a) > 2 ? 2 : 1);
                if (p < end) p++;
            }
        } else if ((size_t)(end - p) >= 12 && strncmp(p, "beginbfrange", 12) == 0) {
            p += 12;
            while (p < end && strncmp(p, "endbfrange", 10) != 0) {
                const char *a, *b, *c;
                unsigned start, stop, dest, k;
                while (p < end && *p != '<' && strncmp(p, "endbfrange", 10) != 0) p++;
                if (p >= end || *p != '<') break;
                a = ++p;
                while (p < end && *p != '>') p++;
                start = hex_cp(a, (int)(p - a));
                while (p < end && *p != '<') p++;
                if (p >= end) break;
                b = ++p;
                while (p < end && *p != '>') p++;
                stop = hex_cp(b, (int)(p - b));
                while (p < end && *p != '<' && *p != '[') p++;
                if (p < end && *p == '<') {
                    c = ++p;
                    while (p < end && *p != '>') p++;
                    dest = hex_cp(c, (int)(p - c));
                    for (k = start; k <= stop && k - start < 256; k++)
                        font_add(f, k, dest + (k - start), (int)(strchr(a, '>') - a) > 2 ? 2 : 1);
                }
                while (p < end && *p != '\n') p++;
            }
        } else p++;
    }
}

static void load_font(Pdf *pdf, Val *font_ref, FontMap *f) {
    Val *font, *tu, *enc;
    memset(f, 0, sizeof *f);
    font = resolve(pdf, font_ref);
    if (!font || font->kind != VK_DICT) return;
    enc = dict_get(font, "Encoding");
    tu = dict_get(font, "ToUnicode");
    if (tu && tu->kind == VK_REF) {
        int num = tu->ref_n;
        if (num > 0 && num < pdf->size) {
            if (!pdf->slot[num].parsed) load_object_at(pdf, num, pdf->xref[num].type == 1 ? (size_t)pdf->xref[num].a : 0);
            if (pdf->slot[num].stream) parse_tounicode(pdf->slot[num].stream, pdf->slot[num].slen, f);
        }
    }
    (void)enc;
}

static char *show_string(const unsigned char *s, int n, const FontMap *font) {
    Buf b;
    int i;
    buf_init(&b);
    if (n >= 2 && s && s[0] == 0xFE && s[1] == 0xFF) {
        free(b.p);
        return pdf_text_string(s, n);
    }
    if (font && font->two) {
        for (i = 0; i + 1 < n; i += 2) {
            unsigned code = ((unsigned)s[i] << 8) | s[i + 1];
            int found = 0;
            unsigned u = font_lookup(font, code, &found);
            utf8_put(&b, found ? u : code);
        }
    } else {
        for (i = 0; i < n; i++) {
            int found = 0;
            unsigned u = font ? font_lookup(font, s[i], &found) : s[i];
            if (!found) u = s[i];
            utf8_put(&b, u < 32 && u != 9 ? ' ' : u);
        }
    }
    return buf_steal(&b);
}

typedef struct {
    double a, b, c, d, e, f;
} Mtx;

typedef struct {
    Mtx ctm;
    Mtx tm, tlm;
    double leading, font_size, char_sp, word_sp, hscale;
    char font[40];
} GState;

static Mtx mtx_mul(Mtx m, Mtx c) {
    Mtx r;
    r.a = m.a * c.a + m.b * c.c;
    r.b = m.a * c.b + m.b * c.d;
    r.c = m.c * c.a + m.d * c.c;
    r.d = m.c * c.b + m.d * c.d;
    r.e = m.e * c.a + m.f * c.c + c.e;
    r.f = m.e * c.b + m.f * c.d + c.f;
    return r;
}

static void mtx_id(Mtx *m) {
    m->a = 1;
    m->b = 0;
    m->c = 0;
    m->d = 1;
    m->e = 0;
    m->f = 0;
}

static void text_move(GState *g, double tx, double ty) {
    g->tm.e += tx * g->tm.a + ty * g->tm.c;
    g->tm.f += tx * g->tm.b + ty * g->tm.d;
}

static void add_word(Word **words, int *n, int *cap, const char *text, int page, double x, double y, double size) {
    Word *w;
    char *copy;
    if (!text || !text[0]) return;
    copy = xstrdup(text);
    if (*n >= *cap) {
        *cap = *cap ? *cap * 2 : 32;
        *words = xrealloc(*words, (size_t)(*cap) * sizeof **words);
    }
    w = &(*words)[(*n)++];
    memset(w, 0, sizeof *w);
    w->text = copy;
    w->page = page;
    w->x = x;
    w->y = y;
    w->size = size;
    w->h = size > 0 ? size : 12;
    w->w = (double)strlen(text) * (w->h * 0.5);
    w->conf = 100;
    w->positioned = 1;
}

static void emit_shown(Word **words, int *n, int *cap, const char *text, int page, GState *g, double page_h) {
    double ux = g->tm.e * g->ctm.a + g->tm.f * g->ctm.c + g->ctm.e;
    double uy = g->tm.e * g->ctm.b + g->tm.f * g->ctm.d + g->ctm.f;
    double y = page_h - uy;
    double size = g->font_size;
    const char *p = text ? text : "";
    if (size < 1) size = 12;
    while (*p) {
        char tok[256];
        int k = 0;
        while (*p == ' ') p++;
        if (!*p) break;
        while (*p && *p != ' ' && k + 1 < (int)sizeof tok) tok[k++] = *p++;
        tok[k] = 0;
        add_word(words, n, cap, tok, page, ux, y, size);
        ux += (double)k * size * 0.5;
    }
}

static int extract_content(Pdf *pdf, const unsigned char *s, size_t n, Val *resources, int page, double page_h,
                           Word **words, int *nw, int *cap) {
    Pc p;
    GState g, stack[32];
    int sp = 0, depth_q = 0;
    FontMap fonts[16];
    char font_names[16][40];
    int nfonts = 0;
    typedef struct {
        int kind;
        double num;
        unsigned char *s;
        int slen;
        char name[48];
    } Op;
    Op ops[48];
    int nop = 0;
    int i;
    memset(&g, 0, sizeof g);
    mtx_id(&g.ctm);
    mtx_id(&g.tm);
    mtx_id(&g.tlm);
    g.hscale = 100;
    g.font_size = 12;
    p.s = s;
    p.n = n;
    p.i = 0;
    while (p.i < p.n) {
        skip_ws(&p);
        if (p.i >= p.n) break;
        if (p.s[p.i] == '(') {
            Op *o;
            if (nop >= 48) nop = 0;
            o = &ops[nop++];
            memset(o, 0, sizeof *o);
            o->kind = 1;
            o->s = parse_lit_string(&p, &o->slen);
            continue;
        }
        if (p.s[p.i] == '<' && !(p.i + 1 < p.n && p.s[p.i + 1] == '<')) {
            Op *o;
            if (nop >= 48) nop = 0;
            o = &ops[nop++];
            memset(o, 0, sizeof *o);
            o->kind = 1;
            o->s = parse_hex_string(&p, &o->slen);
            continue;
        }
        if (p.s[p.i] == '[') {
            p.i++;
            while (p.i < p.n) {
                Op *o;
                skip_ws(&p);
                if (p.i < p.n && p.s[p.i] == ']') {
                    p.i++;
                    break;
                }
                if (nop >= 48) {
                    for (i = 0; i < nop; i++) free(ops[i].s);
                    nop = 0;
                }
                o = &ops[nop];
                memset(o, 0, sizeof *o);
                if (p.s[p.i] == '(') {
                    o->kind = 1;
                    o->s = parse_lit_string(&p, &o->slen);
                    nop++;
                } else if (p.s[p.i] == '<') {
                    o->kind = 1;
                    o->s = parse_hex_string(&p, &o->slen);
                    nop++;
                } else if (parse_number(&p, &o->num)) {
                    o->kind = 2;
                    nop++;
                } else p.i++;
            }
            continue;
        }
        if (p.s[p.i] == '/') {
            Op *o = &ops[nop < 48 ? nop : 0];
            char *nm;
            if (nop >= 48) {
                for (i = 0; i < nop; i++) free(ops[i].s);
                nop = 0;
                o = &ops[0];
            }
            memset(o, 0, sizeof *o);
            nm = parse_name(&p);
            o->kind = 3;
            snprintf(o->name, sizeof o->name, "%s", nm);
            free(nm);
            nop++;
            continue;
        }
        if (isdigit(p.s[p.i]) || p.s[p.i] == '-' || p.s[p.i] == '+' || p.s[p.i] == '.') {
            Op *o;
            if (nop >= 48) {
                for (i = 0; i < nop; i++) free(ops[i].s);
                nop = 0;
            }
            o = &ops[nop++];
            memset(o, 0, sizeof *o);
            o->kind = 0;
            parse_number(&p, &o->num);
            continue;
        }
        {
            char op[16];
            int k = 0;
            FontMap *fm = NULL;
            while (p.i < p.n && !isspace(p.s[p.i]) && !is_delim(p.s[p.i]) && k + 1 < (int)sizeof op) op[k++] = (char)p.s[p.i++];
            op[k] = 0;
            for (i = 0; i < nfonts; i++)
                if (strcmp(font_names[i], g.font) == 0) fm = &fonts[i];
            if (strcmp(op, "BT") == 0) {
                mtx_id(&g.tm);
                mtx_id(&g.tlm);
            } else if (strcmp(op, "ET") == 0) {
            } else if (strcmp(op, "Tm") == 0 && nop >= 6) {
                g.tm.a = ops[nop - 6].num;
                g.tm.b = ops[nop - 5].num;
                g.tm.c = ops[nop - 4].num;
                g.tm.d = ops[nop - 3].num;
                g.tm.e = ops[nop - 2].num;
                g.tm.f = ops[nop - 1].num;
                g.tlm = g.tm;
            } else if ((strcmp(op, "Td") == 0 || strcmp(op, "TD") == 0) && nop >= 2) {
                if (strcmp(op, "TD") == 0) g.leading = -ops[nop - 1].num;
                text_move(&g, ops[nop - 2].num, ops[nop - 1].num);
                /* Td translates the text line matrix, then sets Tm = Tlm.
                   text_move above moved Tm only. Rebuild from Tlm. */
                g.tlm.e += ops[nop - 2].num * g.tlm.a + ops[nop - 1].num * g.tlm.c;
                g.tlm.f += ops[nop - 2].num * g.tlm.b + ops[nop - 1].num * g.tlm.d;
                g.tm = g.tlm;
            } else if (strcmp(op, "T*") == 0) {
                g.tlm.e += -g.leading * g.tlm.c;
                g.tlm.f += -g.leading * g.tlm.d;
                g.tm = g.tlm;
            } else if (strcmp(op, "Tf") == 0 && nop >= 2) {
                snprintf(g.font, sizeof g.font, "%s", ops[nop - 2].name);
                g.font_size = ops[nop - 1].num;
                if (resources) {
                    Val *fd = dict_get(resources, "Font");
                    fd = resolve(pdf, fd);
                    if (fd && fd->kind == VK_DICT) {
                        Val *fr = dict_get(fd, g.font);
                        int have = 0;
                        for (i = 0; i < nfonts; i++)
                            if (strcmp(font_names[i], g.font) == 0) have = 1;
                        if (!have && fr && nfonts < 16) {
                            snprintf(font_names[nfonts], sizeof font_names[nfonts], "%s", g.font);
                            load_font(pdf, fr, &fonts[nfonts]);
                            nfonts++;
                        }
                    }
                }
            } else if (strcmp(op, "TL") == 0 && nop >= 1) g.leading = ops[nop - 1].num;
            else if (strcmp(op, "Tc") == 0 && nop >= 1) g.char_sp = ops[nop - 1].num;
            else if (strcmp(op, "Tw") == 0 && nop >= 1) g.word_sp = ops[nop - 1].num;
            else if (strcmp(op, "Tz") == 0 && nop >= 1) g.hscale = ops[nop - 1].num;
            else if (strcmp(op, "Tj") == 0 && nop >= 1 && ops[nop - 1].kind == 1) {
                char *txt = show_string(ops[nop - 1].s, ops[nop - 1].slen, fm);
                emit_shown(words, nw, cap, txt, page, &g, page_h);
                text_move(&g, (double)(txt ? strlen(txt) : 0) * g.font_size * 0.45 * (g.hscale / 100.0), 0);
                free(txt);
            } else if (strcmp(op, "'") == 0 && nop >= 1 && ops[nop - 1].kind == 1) {
                char *txt;
                g.tlm.e += -g.leading * g.tlm.c;
                g.tlm.f += -g.leading * g.tlm.d;
                g.tm = g.tlm;
                txt = show_string(ops[nop - 1].s, ops[nop - 1].slen, fm);
                emit_shown(words, nw, cap, txt, page, &g, page_h);
                free(txt);
            } else if (strcmp(op, "\"") == 0 && nop >= 3 && ops[nop - 1].kind == 1) {
                char *txt;
                g.word_sp = ops[nop - 3].num;
                g.char_sp = ops[nop - 2].num;
                g.tlm.e += -g.leading * g.tlm.c;
                g.tlm.f += -g.leading * g.tlm.d;
                g.tm = g.tlm;
                txt = show_string(ops[nop - 1].s, ops[nop - 1].slen, fm);
                emit_shown(words, nw, cap, txt, page, &g, page_h);
                free(txt);
            } else if (strcmp(op, "TJ") == 0) {
                Buf joined;
                buf_init(&joined);
                for (i = 0; i < nop; i++) {
                    if (ops[i].kind == 1) {
                        char *txt = show_string(ops[i].s, ops[i].slen, fm);
                        buf_puts(&joined, txt);
                        text_move(&g, (double)(txt ? strlen(txt) : 0) * g.font_size * 0.45 * (g.hscale / 100.0), 0);
                        free(txt);
                    } else if (ops[i].kind == 0 || ops[i].kind == 2) {
                        if (ops[i].num < -120) buf_putc(&joined, ' ');
                        text_move(&g, -ops[i].num / 1000.0 * g.font_size * (g.hscale / 100.0), 0);
                    }
                }
                /* Re-show from the start position is hard after moving. Emit the joined
                   string at the position before TJ by rewinding is not stored.
                   Emit the joined text as one word at the current point minus its width. */
                if (joined.n) {
                    double width = (double)joined.n * g.font_size * 0.45;
                    double save_e = g.tm.e, save_f = g.tm.f;
                    text_move(&g, -width, 0);
                    emit_shown(words, nw, cap, joined.p, page, &g, page_h);
                    g.tm.e = save_e;
                    g.tm.f = save_f;
                }
                buf_free(&joined);
            } else if (strcmp(op, "cm") == 0 && nop >= 6) {
                Mtx m;
                m.a = ops[0].num;
                m.b = ops[1].num;
                m.c = ops[2].num;
                m.d = ops[3].num;
                m.e = ops[4].num;
                m.f = ops[5].num;
                g.ctm = mtx_mul(m, g.ctm);
            } else if (strcmp(op, "q") == 0) {
                if (depth_q < 32) stack[depth_q++] = g;
            } else if (strcmp(op, "Q") == 0) {
                if (depth_q > 0) g = stack[--depth_q];
            }
            for (i = 0; i < nop; i++) free(ops[i].s);
            nop = 0;
            (void)sp;
        }
    }
    for (i = 0; i < nop; i++) free(ops[i].s);
    for (i = 0; i < nfonts; i++) {
        free(fonts[i].code);
        free(fonts[i].uni);
    }
    return 0;
}

static int walk_pages(Pdf *pdf, Val *node, Val *inherited, int *page_out, Word **words, int *nw, int *cap, char ***labels,
                      int *label_cap);

static Val *inherit_get(Val *node, Val *inherited, const char *key) {
    Val *v = node ? dict_get(node, key) : NULL;
    if (v) return v;
    return inherited ? dict_get(inherited, key) : NULL;
}

static double box_height(Val *box) {
    if (!box || box->kind != VK_ARR || box->nitems < 4) return 792;
    if (box->items[1]->kind != VK_NUM || box->items[3]->kind != VK_NUM) return 792;
    return box->items[3]->num - box->items[1]->num;
}

static int page_content(Pdf *pdf, Val *page, Val *inherited, int page_no, Word **words, int *nw, int *cap) {
    Val *contents, *res, *box;
    double ph;
    contents = dict_get(page, "Contents");
    res = inherit_get(page, inherited, "Resources");
    res = resolve(pdf, res);
    box = inherit_get(page, inherited, "MediaBox");
    box = resolve(pdf, box);
    ph = box_height(box);
    if (!contents) return 0;
    if (contents->kind == VK_REF || contents->kind == VK_DICT) {
        Val *one = resolve(pdf, contents);
        int num = contents->kind == VK_REF ? contents->ref_n : 0;
        unsigned char *stream = NULL;
        size_t slen = 0;
        int owned = 0;
        if (num > 0 && num < pdf->size && pdf->slot[num].stream) {
            stream = pdf->slot[num].stream;
            slen = pdf->slot[num].slen;
        } else if (one && one->kind == VK_DICT) {
            return 0;
        }
        if (stream) return extract_content(pdf, stream, slen, res, page_no, ph, words, nw, cap);
        (void)owned;
    } else if (contents->kind == VK_ARR) {
        int i;
        for (i = 0; i < contents->nitems; i++) {
            Val *ref = contents->items[i];
            int num = ref && ref->kind == VK_REF ? ref->ref_n : 0;
            resolve(pdf, ref);
            if (num > 0 && num < pdf->size && pdf->slot[num].stream)
                extract_content(pdf, pdf->slot[num].stream, pdf->slot[num].slen, res, page_no, ph, words, nw, cap);
        }
    }
    return 0;
}

static void label_format(Val *style, int index_in_range, char *out, size_t cap) {
    Val *s, *st, *p;
    int start = 1, num;
    const char *prefix = "";
    char body[32];
    if (!style || style->kind != VK_DICT) {
        snprintf(out, cap, "%d", index_in_range + 1);
        return;
    }
    s = dict_get(style, "S");
    st = dict_get(style, "St");
    p = dict_get(style, "P");
    if (st && st->kind == VK_NUM) start = (int)st->num;
    if (p && p->kind == VK_STR) {
        char *t = pdf_text_string(p->s, p->slen);
        prefix = t;
        num = start + index_in_range;
        body[0] = 0;
        if (s && s->kind == VK_NAME && (strcmp((char *)s->s, "r") == 0 || strcmp((char *)s->s, "R") == 0))
            roman_format(num, strcmp((char *)s->s, "R") == 0, body, sizeof body);
        else snprintf(body, sizeof body, "%d", num);
        snprintf(out, cap, "%s%s", prefix, body);
        free(t);
        return;
    }
    num = start + index_in_range;
    if (s && s->kind == VK_NAME && (strcmp((char *)s->s, "r") == 0 || strcmp((char *)s->s, "R") == 0))
        roman_format(num, strcmp((char *)s->s, "R") == 0, out, cap);
    else snprintf(out, cap, "%d", num);
}

static int walk_pages(Pdf *pdf, Val *node, Val *inherited, int *page_out, Word **words, int *nw, int *cap, char ***labels,
                      int *label_cap) {
    Val *type, *kids, *resolved;
    resolved = resolve(pdf, node);
    if (!resolved || resolved->kind != VK_DICT) return 0;
    type = dict_get(resolved, "Type");
    if (type && type->kind == VK_NAME && strcmp((char *)type->s, "Page") == 0) {
        int page = ++(*page_out);
        if (page >= *label_cap) {
            int ncap = (*label_cap) ? (*label_cap) * 2 : 16;
            *labels = xrealloc(*labels, (size_t)ncap * sizeof **labels);
            memset(*labels + *label_cap, 0, (size_t)(ncap - *label_cap) * sizeof **labels);
            *label_cap = ncap;
        }
        page_content(pdf, resolved, inherited, page, words, nw, cap);
        return 1;
    }
    kids = dict_get(resolved, "Kids");
    kids = resolve(pdf, kids);
    if (kids && kids->kind == VK_ARR) {
        int i;
        for (i = 0; i < kids->nitems; i++) walk_pages(pdf, kids->items[i], resolved, page_out, words, nw, cap, labels, label_cap);
    }
    return 0;
}

static void apply_labels(Pdf *pdf, char **labels, int npages) {
    Val *root, *pl, *nums;
    int i;
    root = NULL;
    if (pdf->root > 0 && pdf->root < pdf->size) {
        load_object_at(pdf, pdf->root, pdf->xref[pdf->root].type == 1 ? (size_t)pdf->xref[pdf->root].a : 0);
        root = pdf->slot[pdf->root].v;
    }
    for (i = 1; i <= npages; i++) {
        if (!labels[i]) labels[i] = xstrdup("");
    }
    if (!root) return;
    pl = dict_get(root, "PageLabels");
    pl = resolve(pdf, pl);
    if (!pl || pl->kind != VK_DICT) {
        for (i = 1; i <= npages; i++) {
            free(labels[i]);
            labels[i] = xmalloc(16);
            snprintf(labels[i], 16, "%d", i);
        }
        return;
    }
    nums = dict_get(pl, "Nums");
    nums = resolve(pdf, nums);
    if (!nums || nums->kind != VK_ARR) return;
    for (i = 1; i <= npages; i++) {
        free(labels[i]);
        labels[i] = xmalloc(32);
        snprintf(labels[i], 32, "%d", i);
    }
    for (i = 0; i + 1 < nums->nitems; i += 2) {
        int start, next, p;
        Val *style;
        if (nums->items[i]->kind != VK_NUM) continue;
        start = (int)nums->items[i]->num;
        style = nums->items[i + 1];
        next = npages;
        if (i + 2 < nums->nitems && nums->items[i + 2]->kind == VK_NUM) next = (int)nums->items[i + 2]->num;
        for (p = start; p < next && p < npages; p++) {
            char body[32];
            label_format(style, p - start, body, sizeof body);
            snprintf(labels[p + 1], 32, "%s", body);
        }
    }
}

static void read_info(Pdf *pdf, Book *meta) {
    Val *info;
    Val *title, *author;
    char *t;
    if (!meta || pdf->info <= 0 || pdf->info >= pdf->size) return;
    load_object_at(pdf, pdf->info, pdf->xref[pdf->info].type == 1 ? (size_t)pdf->xref[pdf->info].a : 0);
    info = pdf->slot[pdf->info].v;
    if (!info || info->kind != VK_DICT) return;
    title = dict_get(info, "Title");
    author = dict_get(info, "Author");
    if (title && title->kind == VK_STR) {
        t = pdf_text_string(title->s, title->slen);
        snprintf(meta->title, sizeof meta->title, "%s", t);
        free(t);
    }
    if (author && author->kind == VK_STR) {
        t = pdf_text_string(author->s, author->slen);
        snprintf(meta->creator, sizeof meta->creator, "%s", t);
        free(t);
    }
}

static void pdf_close(Pdf *pdf) {
    int i;
    for (i = 0; i < pdf->size; i++) {
        val_free(pdf->slot[i].v);
        free(pdf->slot[i].stream);
    }
    free(pdf->slot);
    free(pdf->xref);
    free(pdf->buf);
    memset(pdf, 0, sizeof *pdf);
}

int pdf_extract_native(const char *path, Word **words, int *nwords, int *npages, char ***labels, Book *meta) {
    Pdf pdf;
    Buf file;
    size_t xref_at;
    int page_count = 0, cap = 0, lcap = 0;
    Val *root, *pages;
    memset(&pdf, 0, sizeof pdf);
    *words = NULL;
    *nwords = 0;
    *npages = 0;
    *labels = NULL;
    buf_init(&file);
    if (file_read(path, &file) != 0) return -1;
    if (file.n < 5 || memcmp(file.p, "%PDF", 4) != 0) {
        buf_free(&file);
        recto_seterr("not a PDF: %s", path);
        return -1;
    }
    pdf.buf = (unsigned char *)file.p;
    pdf.len = file.n;
    file.p = NULL;
    xref_at = find_startxref(pdf.buf, pdf.len);
    if (!xref_at || load_xref_at(&pdf, xref_at, 0) != 0 || pdf.root <= 0) {
        int root_id = pdf.root;
        size_t at = xref_at;
        pdf_close(&pdf);
        if (!recto_err[0]) recto_seterr("cannot read the PDF cross-reference (root %d, at %lu)", root_id, (unsigned long)at);
        return -1;
    }
    if (load_object_at(&pdf, pdf.root, pdf.xref[pdf.root].type == 1 ? (size_t)pdf.xref[pdf.root].a : 0) != 0) {
        pdf_close(&pdf);
        return -1;
    }
    root = pdf.slot[pdf.root].v;
    pages = root ? dict_get(root, "Pages") : NULL;
    if (!pages) {
        pdf_close(&pdf);
        recto_seterr("PDF has no pages");
        return -1;
    }
    lcap = 8;
    *labels = xmalloc((size_t)lcap * sizeof **labels);
    memset(*labels, 0, (size_t)lcap * sizeof **labels);
    walk_pages(&pdf, pages, NULL, &page_count, words, nwords, &cap, labels, &lcap);
    apply_labels(&pdf, *labels, page_count);
    {
        int i;
        for (i = 0; i < *nwords; i++) {
            int pg = (*words)[i].page;
            if (pg >= 1 && pg <= page_count && (*labels)[pg])
                snprintf((*words)[i].label, sizeof(*words)[i].label, "%s", (*labels)[pg]);
        }
    }
    if (meta) read_info(&pdf, meta);
    *npages = page_count;
    pdf_close(&pdf);
    return 0;
}

int pdf_page_raster(const char *path, int page, Image *im) {
    char base[RECTO_PATH], pgm[RECTO_PATH];
    Buf cmd, sink;
    int rc;
    if (!which_cmd("pdftoppm")) {
        recto_seterr("pdftoppm is not installed, so this picture page cannot be rasterized");
        return -1;
    }
    temp_path(base, sizeof base, "");
    snprintf(pgm, sizeof pgm, "%s.pgm", base);
    buf_init(&cmd);
    buf_init(&sink);
    buf_puts(&cmd, "pdftoppm -gray -r 150 -f ");
    buf_printf(&cmd, "%d -l %d -singlefile ", page, page);
    sh_quote(&cmd, path);
    buf_putc(&cmd, ' ');
    sh_quote(&cmd, base);
    buf_puts(&cmd, " >/dev/null 2>/dev/null");
    cmd_capture(cmd.p, &sink);
    buf_free(&cmd);
    buf_free(&sink);
    if (!file_exists(pgm)) {
        recto_seterr("could not rasterize PDF page %d", page);
        return -1;
    }
    rc = image_load(pgm, im);
    remove(pgm);
    return rc;
}
