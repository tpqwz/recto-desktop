#include "recto.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void words_free(Word *w, int n) {
    int i;
    if (!w) return;
    for (i = 0; i < n; i++) free(w[i].text);
    free(w);
}

typedef struct Ln {
    int *idx;
    int n, cap;
    int page;
    char label[24];
    double x, y, w, h, size;
    int kind;
    int brk;
    int table;
} Ln;

static void ln_add(Ln *ln, int idx) {
    if (ln->n >= ln->cap) {
        ln->cap = ln->cap ? ln->cap * 2 : 8;
        ln->idx = xrealloc(ln->idx, (size_t)ln->cap * sizeof(int));
    }
    ln->idx[ln->n++] = idx;
}

static void ln_free_arr(Ln *ln, int n) {
    int i;
    for (i = 0; i < n; i++) free(ln[i].idx);
    free(ln);
}

static int word_y_cmp(const void *a, const void *b, Word *words) {
    const int *ia = a, *ib = b;
    double ya = words[*ia].y, yb = words[*ib].y;
    if (ya < yb - 0.5) return -1;
    if (ya > yb + 0.5) return 1;
    if (words[*ia].x < words[*ib].x) return -1;
    if (words[*ia].x > words[*ib].x) return 1;
    return 0;
}

static void sort_idx(int *idx, int n, Word *words) {
    int i, j;
    for (i = 1; i < n; i++) {
        int key = idx[i];
        j = i;
        while (j > 0 && word_y_cmp(&idx[j - 1], &key, words) > 0) {
            idx[j] = idx[j - 1];
            j--;
        }
        idx[j] = key;
    }
}

static void sort_idx_x(int *idx, int n, Word *words) {
    int i, j;
    for (i = 1; i < n; i++) {
        int key = idx[i];
        j = i;
        while (j > 0 && words[idx[j - 1]].x > words[key].x) {
            idx[j] = idx[j - 1];
            j--;
        }
        idx[j] = key;
    }
}

static char *join_words(Word *words, const Ln *ln, int spaced) {
    Buf b;
    int i;
    buf_init(&b);
    for (i = 0; i < ln->n; i++) {
        const char *t = words[ln->idx[i]].text;
        if (!t || !t[0]) continue;
        if (b.n && spaced) buf_putc(&b, ' ');
        buf_puts(&b, t);
    }
    return buf_steal(&b);
}

static void normalize_head(const char *s, char *out, size_t cap) {
    size_t o = 0;
    int sp = 0;
    if (!s) s = "";
    for (; *s && o + 1 < cap; s++) {
        unsigned char c = (unsigned char)*s;
        if (isalpha(c)) {
            out[o++] = (char)tolower(c);
            sp = 0;
        } else if (c == ' ' || c == '\t') {
            if (o && !sp) {
                out[o++] = ' ';
                sp = 1;
            }
        }
    }
    if (o && out[o - 1] == ' ') o--;
    out[o] = 0;
}

static int bare_page_number(const char *s, const char *label, int page) {
    char tmp[32];
    if (!s) return 0;
    while (*s && isspace((unsigned char)*s)) s++;
    if (!*s) return 0;
    {
        const char *e = s + strlen(s);
        while (e > s && isspace((unsigned char)e[-1])) e--;
        if ((size_t)(e - s) >= sizeof tmp) return 0;
        memcpy(tmp, s, (size_t)(e - s));
        tmp[e - s] = 0;
    }
    if (label && label[0] && str_ieq(tmp, label)) return 1;
    if (is_int_token(tmp) && atoi(tmp) == page) return 1;
    if (roman_parse(tmp) == page && strlen(tmp) <= 8) return 1;
    return 0;
}

static int starts_chapter(const char *s) {
    return str_istart(s, "chapter ") || str_istart(s, "chapter\t") || str_istart(s, "book ") ||
           str_istart(s, "part ");
}

static int starts_verse(const char *s) {
    int n = 0;
    if (!s) return 0;
    while (*s && isspace((unsigned char)*s)) s++;
    if (!isdigit((unsigned char)*s)) return 0;
    while (isdigit((unsigned char)*s)) {
        s++;
        n++;
    }
    return n > 0 && n <= 4 && (*s == '.' || *s == ')');
}

static int is_short_caps(const char *s) {
    int letters = 0;
    const char *p;
    size_t n;
    if (!s) return 0;
    n = strlen(s);
    if (n < 3 || n > 42) return 0;
    for (p = s; *p; p++) {
        unsigned char c = (unsigned char)*p;
        if (islower(c)) return 0;
        if (isalpha(c)) letters++;
        if (c == '.' && p[1]) return 0;
    }
    return letters >= 3;
}

static int ends_sentence(const char *s) {
    size_t n;
    if (!s || !s[0]) return 1;
    n = strlen(s);
    while (n && isspace((unsigned char)s[n - 1])) n--;
    if (!n) return 1;
    if (s[n - 1] == '"' || s[n - 1] == '\'' || s[n - 1] == ')') {
        if (n >= 2) n--;
        else return 0;
    }
    return s[n - 1] == '.' || s[n - 1] == '!' || s[n - 1] == '?';
}

static int ends_hyphen(const char *s) {
    size_t n;
    if (!s) return 0;
    n = strlen(s);
    while (n && isspace((unsigned char)s[n - 1])) n--;
    if (n < 2 || s[n - 1] != '-') return 0;
    return isalpha((unsigned char)s[n - 2]);
}

static int starts_lower(const char *s) {
    if (!s) return 0;
    while (*s && isspace((unsigned char)*s)) s++;
    if (*s == '(') s++;
    return islower((unsigned char)*s);
}

static double median_d(double *v, int n) {
    int i, j;
    double m;
    if (n <= 0) return 0;
    for (i = 1; i < n; i++) {
        double key = v[i];
        j = i;
        while (j > 0 && v[j - 1] > key) {
            v[j] = v[j - 1];
            j--;
        }
        v[j] = key;
    }
    m = v[n / 2];
    return m;
}

static int find_gutter(Word *words, int *idx, int n, double page_w) {
    int hist[32];
    int i, b, best = -1, best_v = 1 << 30;
    int left, right;
    if (page_w < 40 || n < 8) return -1;
    memset(hist, 0, sizeof hist);
    for (i = 0; i < n; i++) {
        double cx = words[idx[i]].x + words[idx[i]].w * 0.5;
        int bin = (int)(cx / page_w * 32.0);
        if (bin < 0) bin = 0;
        if (bin > 31) bin = 31;
        hist[bin]++;
    }
    for (b = 10; b <= 22; b++) {
        left = right = 0;
        for (i = 0; i < b; i++) left += hist[i];
        for (i = b + 1; i < 32; i++) right += hist[i];
        if (left < 4 || right < 4) continue;
        if (hist[b] * 4 <= left && hist[b] * 4 <= right && hist[b] < best_v) {
            best_v = hist[b];
            best = b;
        }
    }
    if (best < 0) return -1;
    return (int)((best + 0.5) / 32.0 * page_w);
}

static void measure_line(Ln *ln, Word *words) {
    int i;
    double x1 = 0, size_sum = 0;
    int sized = 0;
    if (!ln->n) return;
    sort_idx_x(ln->idx, ln->n, words);
    ln->x = words[ln->idx[0]].x;
    ln->y = words[ln->idx[0]].y;
    ln->h = words[ln->idx[0]].h;
    for (i = 0; i < ln->n; i++) {
        Word *w = &words[ln->idx[i]];
        double r = w->x + w->w;
        if (w->x < ln->x) ln->x = w->x;
        if (r > x1) x1 = r;
        if (w->y < ln->y) ln->y = w->y;
        if (w->h > ln->h) ln->h = w->h;
        if (w->size > 0) {
            size_sum += w->size;
            sized++;
        }
        if (w->label[0] && !ln->label[0]) snprintf(ln->label, sizeof ln->label, "%s", w->label);
        if (w->brk) ln->brk = 1;
    }
    ln->w = x1 - ln->x;
    ln->size = sized ? size_sum / sized : ln->h;
    ln->page = words[ln->idx[0]].page;
}

static int line_is_table(Word *words, const Ln *ln) {
    int i, wide = 0;
    double gaps = 0;
    int ng = 0;
    double med;
    double gs[64];
    if (ln->n < 3) return 0;
    if (!words[ln->idx[0]].positioned) return 0;
    for (i = 1; i < ln->n && ng < 64; i++) {
        double g = words[ln->idx[i]].x - (words[ln->idx[i - 1]].x + words[ln->idx[i - 1]].w);
        if (g < 0) g = 0;
        gs[ng++] = g;
        gaps += g;
    }
    if (!ng) return 0;
    med = gaps / ng;
    for (i = 0; i < ng; i++)
        if (gs[i] > med * 2.2 && gs[i] > 10) wide++;
    return wide >= 2;
}

static double fabs_local(double d) { return d < 0 ? -d : d; }

static int rows_align(Word *words, const Ln *a, const Ln *b) {
    int i, j, m = 0;
    for (i = 0; i < a->n; i++) {
        for (j = 0; j < b->n; j++) {
            if (fabs_local(words[a->idx[i]].x - words[b->idx[j]].x) < 18) {
                m++;
                break;
            }
        }
    }
    return m >= 2;
}

static char *table_row_text(Word *words, const Ln *ln) {
    Buf b;
    int i;
    buf_init(&b);
    for (i = 0; i < ln->n; i++) {
        if (i) buf_puts(&b, " | ");
        buf_puts(&b, words[ln->idx[i]].text ? words[ln->idx[i]].text : "");
    }
    return buf_steal(&b);
}

int compose_book(Word *words, int nwords, int file_pages, const Settings *set, Book *book) {
    int i, page, max_page = file_pages;
    Ln *lines = NULL;
    int nlines = 0, lcap = 0;
    int *page_idx = NULL;
    int columns = set ? set->columns : COL_AUTO;
    if (!book) return -1;
    book->hyphens = book->headers = book->columns = 0;
    book->tables = book->notes = book->chapters = book->duplicates = 0;
    for (i = 0; i < nwords; i++)
        if (words[i].page > max_page) max_page = words[i].page;
    if (max_page < 1) max_page = 1;
    book->file_pages = max_page;
    page_idx = xmalloc((size_t)(nwords > 0 ? nwords : 1) * sizeof(int));

    for (page = 1; page <= max_page; page++) {
        int n = 0, positioned = 0;
        int gutter = -1;
        double page_w = 0;
        Ln *page_lines = NULL;
        int pn = 0, pc = 0;
        for (i = 0; i < nwords; i++) {
            if (words[i].page != page) continue;
            if (!words[i].text || !words[i].text[0]) continue;
            page_idx[n++] = i;
            if (words[i].positioned) positioned = 1;
            if (words[i].x + words[i].w > page_w) page_w = words[i].x + words[i].w;
        }
        if (!n) continue;
        if (!positioned) {
            for (i = 0; i < n; i++) {
                Ln ln;
                memset(&ln, 0, sizeof ln);
                ln_add(&ln, page_idx[i]);
                measure_line(&ln, words);
                if (!ln.label[0]) snprintf(ln.label, sizeof ln.label, "%d", page);
                if (pn >= pc) {
                    pc = pc ? pc * 2 : 8;
                    page_lines = xrealloc(page_lines, (size_t)pc * sizeof *page_lines);
                }
                page_lines[pn++] = ln;
            }
        } else {
            double *hs = xmalloc((size_t)n * sizeof(double));
            double med_h;
            int hfill = 0;
            int cur = -1;
            sort_idx(page_idx, n, words);
            for (i = 0; i < n; i++) {
                if (words[page_idx[i]].h > 0) hs[hfill++] = words[page_idx[i]].h;
                else if (words[page_idx[i]].size > 0) hs[hfill++] = words[page_idx[i]].size;
            }
            med_h = hfill ? median_d(hs, hfill) : 12;
            free(hs);
            if (med_h < 4) med_h = 4;
            for (i = 0; i < n; i++) {
                Word *w = &words[page_idx[i]];
                int new_line = (cur < 0);
                if (!new_line) {
                    if (w->y - page_lines[cur].y > med_h * 0.65) new_line = 1;
                }
                if (new_line) {
                    Ln ln;
                    memset(&ln, 0, sizeof ln);
                    ln.y = w->y;
                    if (pn >= pc) {
                        pc = pc ? pc * 2 : 8;
                        page_lines = xrealloc(page_lines, (size_t)pc * sizeof *page_lines);
                    }
                    page_lines[pn++] = ln;
                    cur = pn - 1;
                }
                ln_add(&page_lines[cur], page_idx[i]);
            }
            for (i = 0; i < pn; i++) {
                measure_line(&page_lines[i], words);
                if (!page_lines[i].label[0]) snprintf(page_lines[i].label, sizeof page_lines[i].label, "%d", page);
            }
            if (columns != COL_ONE && pn >= 4)
                gutter = (columns == COL_TWO) ? (int)(page_w / 2) : find_gutter(words, page_idx, n, page_w);
            if (gutter > 0) {
                /* Reorder: full-width lines stay in place and split column bands.
                   A band is read down the left, then down the right. */
                Ln *ordered = xmalloc((size_t)(pn > 0 ? pn : 1) * sizeof *ordered);
                int *Lpos = xmalloc((size_t)(pn > 0 ? pn : 1) * sizeof(int));
                int *Rpos = xmalloc((size_t)(pn > 0 ? pn : 1) * sizeof(int));
                int on = 0, band_b = 0, k;
                book->columns++;
                for (i = 0; i <= pn; i++) {
                    int full = 0;
                    if (i < pn) {
                        double cx0 = page_lines[i].x;
                        double cx1 = page_lines[i].x + page_lines[i].w;
                        full = page_lines[i].w > page_w * 0.72 || (cx0 < gutter - 8 && cx1 > gutter + 8);
                    }
                    if (i == pn || full) {
                        int nl = 0, nr = 0, t;
                        for (k = band_b; k < i && k < pn; k++) {
                            double cx = page_lines[k].x + page_lines[k].w * 0.5;
                            if (cx < gutter) Lpos[nl++] = k;
                            else Rpos[nr++] = k;
                        }
                        for (t = 0; t < nl; t++) ordered[on++] = page_lines[Lpos[t]];
                        for (t = 0; t < nr; t++) ordered[on++] = page_lines[Rpos[t]];
                        if (i < pn) ordered[on++] = page_lines[i];
                        band_b = i + 1;
                    }
                }
                free(Lpos);
                free(Rpos);
                free(page_lines);
                page_lines = ordered;
                pn = on;
            }
        }
        if (nlines + pn >= lcap) {
            lcap = lcap ? lcap * 2 + pn + 8 : pn + 8;
            lines = xrealloc(lines, (size_t)lcap * sizeof *lines);
        }
        for (i = 0; i < pn; i++) lines[nlines++] = page_lines[i];
        free(page_lines);
    }
    free(page_idx);

    /* Running heads: a short line repeated at the top or bottom of several pages. */
    {
        typedef struct {
            char key[80];
            int count;
        } Key;
        Key keys[64];
        int nk = 0, a, b;
        int *drop = calloc((size_t)(nlines > 0 ? nlines : 1), sizeof(int));
        memset(keys, 0, sizeof keys);
        for (a = 0; a < nlines;) {
            b = a;
            while (b < nlines && lines[b].page == lines[a].page) b++;
            if (b > a) {
                int slots[2];
                int ns = 0, s;
                char *t;
                slots[ns++] = a;
                if (b - 1 != a) slots[ns++] = b - 1;
                for (s = 0; s < ns; s++) {
                    char norm[80];
                    int found = 0, k;
                    t = join_words(words, &lines[slots[s]], 1);
                    normalize_head(t, norm, sizeof norm);
                    free(t);
                    if (strlen(norm) < 2 || strlen(norm) > 60) continue;
                    for (k = 0; k < nk; k++)
                        if (strcmp(keys[k].key, norm) == 0) {
                            keys[k].count++;
                            found = 1;
                            break;
                        }
                    if (!found && nk < 64) {
                        snprintf(keys[nk].key, sizeof keys[nk].key, "%s", norm);
                        keys[nk].count = 1;
                        nk++;
                    }
                }
            }
            a = b;
        }
        for (a = 0; a < nlines;) {
            b = a;
            while (b < nlines && lines[b].page == lines[a].page) b++;
            if (b > a) {
                int slots[2];
                int ns = 0, s;
                slots[ns++] = a;
                if (b - 1 != a) slots[ns++] = b - 1;
                for (s = 0; s < ns; s++) {
                    char norm[80];
                    char *t = join_words(words, &lines[slots[s]], 1);
                    int k;
                    normalize_head(t, norm, sizeof norm);
                    if (bare_page_number(t, lines[slots[s]].label, lines[slots[s]].page) && (b - a) > 1) {
                        drop[slots[s]] = 1;
                        book->headers++;
                    } else {
                        for (k = 0; k < nk; k++) {
                            int need = max_page <= 3 ? 2 : (max_page + 1) / 2;
                            if (need < 2) need = 2;
                            if (keys[k].count >= need && strcmp(keys[k].key, norm) == 0) {
                                drop[slots[s]] = 1;
                                book->headers++;
                                break;
                            }
                        }
                    }
                    free(t);
                }
            }
            a = b;
        }
        {
            int wri = 0;
            for (i = 0; i < nlines; i++) {
                if (drop[i]) free(lines[i].idx);
                else lines[wri++] = lines[i];
            }
            nlines = wri;
        }
        free(drop);
    }

    /* A page printed twice: the second half of the lines repeats the first. */
    {
        int a;
        for (a = 0; a < nlines;) {
            int b = a, mid, k, same, half;
            Buf L, R;
            while (b < nlines && lines[b].page == lines[a].page) b++;
            half = b - a;
            if (half >= 4 && (half % 2) == 0) {
                mid = a + half / 2;
                buf_init(&L);
                buf_init(&R);
                for (k = a; k < mid; k++) {
                    char *t = join_words(words, &lines[k], 1);
                    buf_puts(&L, t);
                    buf_putc(&L, '\n');
                    free(t);
                }
                for (k = mid; k < b; k++) {
                    char *t = join_words(words, &lines[k], 1);
                    buf_puts(&R, t);
                    buf_putc(&R, '\n');
                    free(t);
                }
                same = L.n > 40 && L.n == R.n && memcmp(L.p, R.p, L.n) == 0;
                buf_free(&L);
                buf_free(&R);
                if (same) {
                    int wri = mid;
                    for (k = mid; k < b; k++) free(lines[k].idx);
                    for (k = b; k < nlines; k++) lines[wri++] = lines[k];
                    nlines = wri;
                    book->duplicates++;
                    b = mid;
                }
            }
            a = b;
        }
    }

    /* Headings, notes, tables. */
    {
        int a;
        for (a = 0; a < nlines;) {
            int b = a, k, ns = 0;
            double sizes[512];
            double med = 0;
            while (b < nlines && lines[b].page == lines[a].page) b++;
            for (k = a; k < b && ns < 512; k++)
                if (lines[k].size > 0) sizes[ns++] = lines[k].size;
            if (ns) med = median_d(sizes, ns);
            for (k = a; k < b; k++) {
                char *t = join_words(words, &lines[k], 1);
                if (med > 0 && lines[k].size > med * 1.28) lines[k].kind = BLK_H1;
                else if (med > 0 && lines[k].size > 0 && lines[k].size < med * 0.78) lines[k].kind = BLK_NOTE;
                else if (starts_chapter(t)) lines[k].kind = BLK_H1;
                else if (is_short_caps(t)) lines[k].kind = BLK_H2;
                else lines[k].kind = BLK_P;
                if (lines[k].kind == BLK_P && line_is_table(words, &lines[k])) lines[k].table = 1;
                free(t);
            }
            for (k = a; k < b; k++) {
                if (!lines[k].table) continue;
                if (k + 1 < b && lines[k + 1].table && rows_align(words, &lines[k], &lines[k + 1])) {
                    lines[k].kind = BLK_TABLE;
                    lines[k + 1].kind = BLK_TABLE;
                }
            }
            /* A lone wide-gap line is not a table unless a neighbour agrees. */
            for (k = a; k < b; k++)
                if (lines[k].kind != BLK_TABLE) lines[k].table = 0;
            a = b;
        }
    }

    /* Merge wrapped lines. Rejoin a letter broken by a line-end hyphen. */
    {
        Buf para;
        int open = 0;
        int kind = BLK_P, page_no = 1, conf = 100;
        int x = 0, y = 0, w = 0, h = 0;
        char label[24];
        label[0] = 0;
        buf_init(&para);
        for (i = 0; i < nlines; i++) {
            char *t = NULL;
            int knd = lines[i].kind;
            if (knd == BLK_TABLE) {
                int j = i;
                Buf tab;
                if (open) {
                    book_add_block(book, kind, page_no, label, para.p, x, y, w, h, conf);
                    buf_clear(&para);
                    open = 0;
                }
                buf_init(&tab);
                while (j < nlines && lines[j].kind == BLK_TABLE && lines[j].page == lines[i].page) {
                    char *row = table_row_text(words, &lines[j]);
                    if (tab.n) buf_putc(&tab, '\n');
                    buf_puts(&tab, row);
                    free(row);
                    j++;
                }
                book_add_block(book, BLK_TABLE, lines[i].page, lines[i].label, tab.p, (int)lines[i].x,
                               (int)lines[i].y, (int)lines[i].w, (int)lines[i].h, 100);
                book->tables++;
                buf_free(&tab);
                i = j - 1;
                continue;
            }
            t = join_words(words, &lines[i], 1);
            if (knd == BLK_H1 || knd == BLK_H2) {
                if (open) {
                    book_add_block(book, kind, page_no, label, para.p, x, y, w, h, conf);
                    buf_clear(&para);
                    open = 0;
                }
                book_add_block(book, knd, lines[i].page, lines[i].label, t, (int)lines[i].x, (int)lines[i].y,
                               (int)lines[i].w, (int)lines[i].h, 100);
                if (knd == BLK_H1) book->chapters++;
                free(t);
                continue;
            }
            if (starts_verse(t) && open) {
                book_add_block(book, kind, page_no, label, para.p, x, y, w, h, conf);
                buf_clear(&para);
                open = 0;
            }
            if (!open) {
                buf_puts(&para, t);
                kind = knd;
                page_no = lines[i].page;
                snprintf(label, sizeof label, "%s", lines[i].label);
                x = (int)lines[i].x;
                y = (int)lines[i].y;
                w = (int)lines[i].w;
                h = (int)lines[i].h;
                conf = 100;
                open = 1;
            } else {
                int hyphen = ends_hyphen(para.p) && starts_lower(t);
                int wrap = lines[i].page == page_no && !lines[i - 1].brk && !ends_sentence(para.p) && starts_lower(t);
                if (hyphen) {
                    size_t n = para.n;
                    while (n && isspace((unsigned char)para.p[n - 1])) n--;
                    if (n && para.p[n - 1] == '-') n--;
                    para.n = n;
                    para.p[n] = 0;
                    buf_puts(&para, t);
                    book->hyphens++;
                } else if (wrap && kind == knd) {
                    buf_putc(&para, ' ');
                    buf_puts(&para, t);
                } else {
                    book_add_block(book, kind, page_no, label, para.p, x, y, w, h, conf);
                    buf_clear(&para);
                    buf_puts(&para, t);
                    kind = knd;
                    page_no = lines[i].page;
                    snprintf(label, sizeof label, "%s", lines[i].label);
                    x = (int)lines[i].x;
                    y = (int)lines[i].y;
                    w = (int)lines[i].w;
                    h = (int)lines[i].h;
                }
            }
            if (lines[i].brk && open) {
                book_add_block(book, kind, page_no, label, para.p, x, y, w, h, conf);
                buf_clear(&para);
                open = 0;
            }
            free(t);
        }
        if (open && para.n) book_add_block(book, kind, page_no, label, para.p, x, y, w, h, conf);
        buf_free(&para);
    }

    ln_free_arr(lines, nlines);
    for (i = 0; i < book->nblocks; i++)
        if (book->blocks[i].kind == BLK_NOTE) book->notes++;
    book_rebuild_toc(book);
    return 0;
}
