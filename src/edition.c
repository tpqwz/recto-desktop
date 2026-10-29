#include "recto.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void book_init(Book *b) {
    memset(b, 0, sizeof *b);
    b->place_page = 1;
}

void book_free(Book *b) {
    int i;
    if (!b) return;
    if (b->blocks) {
        for (i = 0; i < b->nblocks; i++) free(b->blocks[i].text);
    }
    free(b->blocks);
    if (b->toc) {
        for (i = 0; i < b->ntoc; i++) free(b->toc[i].title);
    }
    free(b->toc);
    memset(b, 0, sizeof *b);
}

const char *block_kind_name(int kind) {
    switch (kind) {
    case BLK_H1: return "h1";
    case BLK_H2: return "h2";
    case BLK_NOTE: return "note";
    case BLK_TABLE: return "table";
    case BLK_LI: return "li";
    default: return "p";
    }
}

static int kind_from_name(const char *s) {
    if (str_ieq(s, "h1")) return BLK_H1;
    if (str_ieq(s, "h2")) return BLK_H2;
    if (str_ieq(s, "note")) return BLK_NOTE;
    if (str_ieq(s, "table")) return BLK_TABLE;
    if (str_ieq(s, "li")) return BLK_LI;
    return BLK_P;
}

void book_add_block(Book *b, int kind, int page, const char *label, const char *text, int x, int y, int w, int h,
                    int conf) {
    Block *blk;
    if (!text) text = "";
    if (b->nblocks >= b->bcap) {
        b->bcap = b->bcap ? b->bcap * 2 : 16;
        b->blocks = xrealloc(b->blocks, (size_t)b->bcap * sizeof *b->blocks);
    }
    blk = &b->blocks[b->nblocks++];
    memset(blk, 0, sizeof *blk);
    blk->kind = kind;
    blk->page = page > 0 ? page : 1;
    snprintf(blk->label, sizeof blk->label, "%s", label && label[0] ? label : "");
    if (!blk->label[0]) snprintf(blk->label, sizeof blk->label, "%d", blk->page);
    blk->text = xstrdup(text);
    blk->x = x;
    blk->y = y;
    blk->w = w;
    blk->h = h;
    blk->conf = conf;
}

void book_rebuild_toc(Book *b) {
    int i;
    for (i = 0; i < b->ntoc; i++) free(b->toc[i].title);
    free(b->toc);
    b->toc = NULL;
    b->ntoc = 0;
    b->tcap = 0;
    b->chapters = 0;
    for (i = 0; i < b->nblocks; i++) {
        TocItem *it;
        int level = 0;
        if (b->blocks[i].kind == BLK_H1) level = 1;
        else if (b->blocks[i].kind == BLK_H2) level = 2;
        else continue;
        if (level == 1) b->chapters++;
        if (b->ntoc >= b->tcap) {
            b->tcap = b->tcap ? b->tcap * 2 : 16;
            b->toc = xrealloc(b->toc, (size_t)b->tcap * sizeof *b->toc);
        }
        it = &b->toc[b->ntoc++];
        memset(it, 0, sizeof *it);
        it->title = xstrdup(b->blocks[i].text ? b->blocks[i].text : "");
        snprintf(it->label, sizeof it->label, "%s", b->blocks[i].label);
        it->page = b->blocks[i].page;
        it->level = level;
        it->block = i;
    }
}

static void put_meta(Buf *b, const char *key, const char *val) {
    if (!val || !val[0]) return;
    buf_puts(b, key);
    buf_puts(b, ": ");
    buf_puts(b, val);
    buf_putc(b, '\n');
}

int book_write_file(const Book *b, const char *path) {
    Buf out;
    int i;
    buf_init(&out);
    buf_puts(&out, "recto 1\n");
    put_meta(&out, "id", b->id);
    put_meta(&out, "title", b->title);
    put_meta(&out, "creator", b->creator);
    put_meta(&out, "subject", b->subject);
    put_meta(&out, "description", b->description);
    put_meta(&out, "publisher", b->publisher);
    put_meta(&out, "contributor", b->contributor);
    put_meta(&out, "date", b->date);
    put_meta(&out, "type", b->type);
    put_meta(&out, "format", b->format);
    put_meta(&out, "identifier", b->identifier);
    put_meta(&out, "source", b->source);
    put_meta(&out, "language", b->language);
    put_meta(&out, "relation", b->relation);
    put_meta(&out, "coverage", b->coverage);
    put_meta(&out, "rights", b->rights);
    put_meta(&out, "work", b->work);
    put_meta(&out, "expression", b->expression);
    put_meta(&out, "volume", b->volume);
    put_meta(&out, "origin", b->origin_name);
    put_meta(&out, "reader", b->reader_note);
    buf_printf(&out, "pages: %d\n", b->file_pages);
    buf_printf(&out, "hyphens: %d\n", b->hyphens);
    buf_printf(&out, "headers: %d\n", b->headers);
    buf_printf(&out, "columns: %d\n", b->columns);
    buf_printf(&out, "tables: %d\n", b->tables);
    buf_printf(&out, "notes: %d\n", b->notes);
    buf_printf(&out, "pictures: %d\n", b->pictures);
    buf_printf(&out, "chapters: %d\n", b->chapters);
    buf_printf(&out, "duplicates: %d\n", b->duplicates);
    buf_printf(&out, "place_page: %d\n", b->place_page);
    buf_printf(&out, "place_block: %d\n", b->place_block);
    buf_puts(&out, "---\n");
    for (i = 0; i < b->nblocks; i++) {
        const Block *blk = &b->blocks[i];
        const char *p;
        buf_printf(&out, "@%s %d %s %d %d %d %d %d\n", block_kind_name(blk->kind), blk->page,
                   blk->label[0] ? blk->label : "-", blk->x, blk->y, blk->w, blk->h, blk->conf);
        p = blk->text ? blk->text : "";
        while (*p) {
            const char *nl = strchr(p, '\n');
            size_t n = nl ? (size_t)(nl - p) : strlen(p);
            if (n == 4 && memcmp(p, "@end", 4) == 0) buf_putc(&out, '\\');
            else if (n && p[0] == '\\') buf_putc(&out, '\\');
            buf_append(&out, p, n);
            buf_putc(&out, '\n');
            if (!nl) break;
            p = nl + 1;
        }
        buf_puts(&out, "@end\n");
    }
    {
        int rc = file_write(path, out.p, out.n);
        buf_free(&out);
        return rc;
    }
}

static void set_field(Book *b, const char *key, const char *val) {
    char *dest = NULL;
    size_t cap = 0;
    if (str_ieq(key, "id")) {
        dest = b->id;
        cap = sizeof b->id;
    } else if (str_ieq(key, "title")) {
        dest = b->title;
        cap = sizeof b->title;
    } else if (str_ieq(key, "creator")) {
        dest = b->creator;
        cap = sizeof b->creator;
    } else if (str_ieq(key, "subject")) {
        dest = b->subject;
        cap = sizeof b->subject;
    } else if (str_ieq(key, "description")) {
        dest = b->description;
        cap = sizeof b->description;
    } else if (str_ieq(key, "publisher")) {
        dest = b->publisher;
        cap = sizeof b->publisher;
    } else if (str_ieq(key, "contributor")) {
        dest = b->contributor;
        cap = sizeof b->contributor;
    } else if (str_ieq(key, "date")) {
        dest = b->date;
        cap = sizeof b->date;
    } else if (str_ieq(key, "type")) {
        dest = b->type;
        cap = sizeof b->type;
    } else if (str_ieq(key, "format")) {
        dest = b->format;
        cap = sizeof b->format;
    } else if (str_ieq(key, "identifier")) {
        dest = b->identifier;
        cap = sizeof b->identifier;
    } else if (str_ieq(key, "source")) {
        dest = b->source;
        cap = sizeof b->source;
    } else if (str_ieq(key, "language")) {
        dest = b->language;
        cap = sizeof b->language;
    } else if (str_ieq(key, "relation")) {
        dest = b->relation;
        cap = sizeof b->relation;
    } else if (str_ieq(key, "coverage")) {
        dest = b->coverage;
        cap = sizeof b->coverage;
    } else if (str_ieq(key, "rights")) {
        dest = b->rights;
        cap = sizeof b->rights;
    } else if (str_ieq(key, "work")) {
        dest = b->work;
        cap = sizeof b->work;
    } else if (str_ieq(key, "expression")) {
        dest = b->expression;
        cap = sizeof b->expression;
    } else if (str_ieq(key, "volume")) {
        dest = b->volume;
        cap = sizeof b->volume;
    } else if (str_ieq(key, "origin")) {
        dest = b->origin_name;
        cap = sizeof b->origin_name;
    } else if (str_ieq(key, "reader")) {
        dest = b->reader_note;
        cap = sizeof b->reader_note;
    }
    if (dest) snprintf(dest, cap, "%s", val);
    else if (str_ieq(key, "pages")) b->file_pages = atoi(val);
    else if (str_ieq(key, "hyphens")) b->hyphens = atoi(val);
    else if (str_ieq(key, "headers")) b->headers = atoi(val);
    else if (str_ieq(key, "columns")) b->columns = atoi(val);
    else if (str_ieq(key, "tables")) b->tables = atoi(val);
    else if (str_ieq(key, "notes")) b->notes = atoi(val);
    else if (str_ieq(key, "pictures")) b->pictures = atoi(val);
    else if (str_ieq(key, "chapters")) b->chapters = atoi(val);
    else if (str_ieq(key, "duplicates")) b->duplicates = atoi(val);
    else if (str_ieq(key, "place_page")) b->place_page = atoi(val);
    else if (str_ieq(key, "place_block")) b->place_block = atoi(val);
}

int book_read_mem(const char *text, Book *b) {
    const char *p;
    int header = 1;
    if (!text || strncmp(text, "recto 1", 7) != 0) {
        recto_seterr("not a recto edition");
        return -1;
    }
    book_free(b);
    book_init(b);
    p = text;
    while (*p) {
        const char *eol = strchr(p, '\n');
        size_t n = eol ? (size_t)(eol - p) : strlen(p);
        char *line = xstrndup(p, n);
        char *s = str_trim(line);
        if (header) {
            char *colon;
            if (strcmp(s, "---") == 0) header = 0;
            else if ((colon = strchr(s, ':')) != NULL) {
                *colon = 0;
                set_field(b, str_trim(s), str_trim(colon + 1));
            }
        } else if (s[0] == '@' && strncmp(s, "@end", 4) != 0) {
            char kind[32], label[32];
            int page = 1, x = 0, y = 0, w = 0, h = 0, conf = 100;
            Buf body;
            int got = sscanf(s, "@%31s %d %31s %d %d %d %d %d", kind, &page, label, &x, &y, &w, &h, &conf);
            buf_init(&body);
            if (got >= 3 && strcmp(label, "-") == 0) label[0] = 0;
            p = eol ? eol + 1 : p + n;
            while (*p) {
                const char *e2 = strchr(p, '\n');
                size_t n2 = e2 ? (size_t)(e2 - p) : strlen(p);
                if (n2 == 4 && memcmp(p, "@end", 4) == 0) {
                    p = e2 ? e2 + 1 : p + n2;
                    break;
                }
                if (n2 && p[0] == '\\') {
                    buf_append(&body, p + 1, n2 - 1);
                } else buf_append(&body, p, n2);
                buf_putc(&body, '\n');
                if (!e2) {
                    p = p + n2;
                    break;
                }
                p = e2 + 1;
            }
            if (body.n && body.p[body.n - 1] == '\n') {
                body.n--;
                body.p[body.n] = 0;
            }
            book_add_block(b, kind_from_name(got >= 1 ? kind : "p"), page, got >= 3 ? label : "", body.p, x, y, w, h,
                           conf);
            buf_free(&body);
            free(line);
            continue;
        }
        free(line);
        if (!eol) break;
        p = eol + 1;
    }
    if (!b->file_pages) {
        int i, m = 1;
        for (i = 0; i < b->nblocks; i++)
            if (b->blocks[i].page > m) m = b->blocks[i].page;
        b->file_pages = m;
    }
    book_rebuild_toc(b);
    return 0;
}

int book_read_file(const char *path, Book *b) {
    Buf buf;
    int rc;
    buf_init(&buf);
    if (file_read(path, &buf) != 0) return -1;
    rc = book_read_mem(buf.p, b);
    buf_free(&buf);
    return rc;
}

static void snippet_at(const char *text, const char *hit, char *out, size_t cap) {
    const char *start;
    size_t n;
    int i;
    if (!text) text = "";
    start = hit ? hit : text;
    if (start - text > 24) start = hit - 24;
    else start = text;
    while (start > text && start[-1] != ' ' && start[-1] != '\n') start--;
    n = strlen(start);
    if (n > cap - 1) n = cap - 1;
    for (i = 0; i < (int)n; i++) out[i] = (start[i] == '\n') ? ' ' : start[i];
    out[i] = 0;
}

int book_find(const Book *b, const char *query, Hit **hits, int *nhits) {
    int i, n = 0, cap = 0;
    Hit *list = NULL;
    if (!query || !query[0]) {
        recto_seterr("find needs a word");
        return -1;
    }
    for (i = 0; i < b->nblocks; i++) {
        const char *p = b->blocks[i].text ? b->blocks[i].text : "";
        const char *s = p;
        while (*s) {
            size_t k;
            for (k = 0; query[k]; k++) {
                if (!s[k]) break;
                if (tolower((unsigned char)s[k]) != tolower((unsigned char)query[k])) break;
            }
            if (!query[k]) {
                Hit *h;
                if (n >= cap) {
                    cap = cap ? cap * 2 : 8;
                    list = xrealloc(list, (size_t)cap * sizeof *list);
                }
                h = &list[n++];
                memset(h, 0, sizeof *h);
                h->block = i;
                h->page = b->blocks[i].page;
                snprintf(h->label, sizeof h->label, "%s", b->blocks[i].label);
                snippet_at(p, s, h->snippet, sizeof h->snippet);
                break;
            }
            s++;
        }
    }
    *hits = list;
    *nhits = n;
    return 0;
}

int book_label_block(const Book *b, const char *label) {
    int i;
    char tmp[32];
    if (!label || !label[0]) return -1;
    for (i = 0; i < b->nblocks; i++)
        if (str_ieq(b->blocks[i].label, label)) return i;
    snprintf(tmp, sizeof tmp, "%s", label);
    if (is_int_token(tmp)) {
        int n = atoi(tmp);
        for (i = 0; i < b->nblocks; i++)
            if (b->blocks[i].page == n) return i;
    } else {
        int r = roman_parse(tmp);
        if (r > 0) {
            char dec[16];
            snprintf(dec, sizeof dec, "%d", r);
            for (i = 0; i < b->nblocks; i++)
                if (str_ieq(b->blocks[i].label, dec) || b->blocks[i].page == r) return i;
        }
    }
    return -1;
}
