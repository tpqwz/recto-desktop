#include "recto.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int ext_is(const char *path, const char *ext) {
    const char *base = path_base(path ? path : "");
    const char *dot = strrchr(base, '.');
    return dot && str_ieq(dot, ext);
}

static int starts_with(const char *path, const char *magic) {
    FILE *f;
    char buf[16];
    size_t n, need;
    if (!path || !magic) return 0;
    need = strlen(magic);
    if (need >= sizeof buf) return 0;
    f = fopen(path, "rb");
    if (!f) return 0;
    n = fread(buf, 1, need, f);
    fclose(f);
    return n == need && memcmp(buf, magic, need) == 0;
}

static int is_image_name(const char *name) {
    return ext_is(name, ".png") || ext_is(name, ".pgm") || ext_is(name, ".ppm") || ext_is(name, ".pbm") ||
           ext_is(name, ".pnm") || ext_is(name, ".bmp") || ext_is(name, ".jpg") || ext_is(name, ".jpeg") ||
           ext_is(name, ".tif") || ext_is(name, ".tiff");
}

static int is_pdf_name(const char *path) { return ext_is(path, ".pdf") || starts_with(path, "%PDF"); }

static int is_djvu_name(const char *path) { return ext_is(path, ".djvu") || ext_is(path, ".djv"); }

static int name_cmp(const void *a, const void *b) {
    return natcmp(*(char *const *)a, *(char *const *)b);
}

static void add_word(Word **words, int *n, int *cap, const char *text, int page, const char *label, double x, double y,
                     double w, double h, int conf, int positioned, int brk) {
    Word *word;
    if (!text || !text[0]) return;
    if (*n >= *cap) {
        *cap = *cap ? *cap * 2 : 32;
        *words = xrealloc(*words, (size_t)(*cap) * sizeof **words);
    }
    word = &(*words)[(*n)++];
    memset(word, 0, sizeof *word);
    word->text = xstrdup(text);
    word->page = page > 0 ? page : 1;
    if (label && label[0]) snprintf(word->label, sizeof word->label, "%s", label);
    word->x = x;
    word->y = y;
    word->w = w;
    word->h = h > 0 ? h : 12;
    word->size = word->h;
    word->conf = conf;
    word->positioned = positioned;
    word->brk = brk;
}

static void drop_page(Word **words, int *n, int page) {
    int i, w = 0;
    for (i = 0; i < *n; i++) {
        if ((*words)[i].page == page) free((*words)[i].text);
        else (*words)[w++] = (*words)[i];
    }
    *n = w;
}

static int page_letters(const Word *words, int n, int page) {
    int i, c = 0;
    for (i = 0; i < n; i++) {
        const char *p;
        if (words[i].page != page) continue;
        for (p = words[i].text; p && *p; p++)
            if (isalpha((unsigned char)*p)) c++;
    }
    return c;
}

static int ocr_letters(const OcrPage *op) {
    int i, c = 0;
    const char *p;
    if (!op) return 0;
    if (op->text) {
        for (p = op->text; *p; p++)
            if (isalpha((unsigned char)*p)) c++;
        return c;
    }
    for (i = 0; i < op->n; i++)
        for (p = op->words[i].text; p && *p; p++)
            if (isalpha((unsigned char)*p)) c++;
    return c;
}

static void free_labels(char **labels, int npages) {
    int i;
    if (!labels) return;
    for (i = 0; i <= npages; i++) free(labels[i]);
    free(labels);
}

static void title_from_path(Book *b, const char *path) {
    char tmp[256];
    char *dot;
    if (b->title[0]) return;
    snprintf(tmp, sizeof tmp, "%s", path_base(path));
    dot = strrchr(tmp, '.');
    if (dot && dot != tmp) *dot = 0;
    if (!tmp[0]) snprintf(tmp, sizeof tmp, "Untitled");
    snprintf(b->title, sizeof b->title, "%s", tmp);
}

static int push_book(Book **books, int *n, int *cap, Book *one) {
    if (*n >= *cap) {
        *cap = *cap ? *cap * 2 : 4;
        *books = xrealloc(*books, (size_t)(*cap) * sizeof **books);
    }
    (*books)[*n] = *one;
    memset(one, 0, sizeof *one);
    (*n)++;
    return 0;
}

static int load_raster(const char *path, Image *im) {
    char pgm[RECTO_PATH];
    Buf cmd, sink;
    recto_err[0] = 0;
    if (image_load(path, im) == 0) return 0;
    if (!which_cmd("jpegtopnm") && !which_cmd("convert")) return -1;
    temp_path(pgm, sizeof pgm, ".pgm");
    buf_init(&cmd);
    buf_init(&sink);
    if (which_cmd("jpegtopnm")) {
        buf_puts(&cmd, "jpegtopnm ");
        sh_quote(&cmd, path);
        buf_puts(&cmd, " > ");
        sh_quote(&cmd, pgm);
        buf_puts(&cmd, " 2>/dev/null");
    } else {
        buf_puts(&cmd, "convert ");
        sh_quote(&cmd, path);
        buf_putc(&cmd, ' ');
        sh_quote(&cmd, pgm);
        buf_puts(&cmd, " >/dev/null 2>/dev/null");
    }
    cmd_capture(cmd.p, &sink);
    buf_free(&cmd);
    buf_free(&sink);
    if (image_load(pgm, im) == 0) {
        remove(pgm);
        return 0;
    }
    remove(pgm);
    return -1;
}

static int read_ocr_image(const char *path, const Settings *set, OcrPage *out) {
    Image im;
    int rc;
    memset(&im, 0, sizeof im);
    memset(out, 0, sizeof *out);
    if (load_raster(path, &im) == 0) {
        rc = ocr_image(&im, set, out);
        image_free(&im);
        return rc;
    }
    if (!set || set->engine == ENG_BUILTIN || set->engine == ENG_VISION) {
        recto_seterr("cannot read the picture %s", path_base(path));
        return -1;
    }
    return ocr_tesseract_file(path, set && set->scripts[0] ? set->scripts : "eng", out);
}

int load_page_image(const char *path, int page, Image *im) {
    if (!path || !im) return -1;
    if (page < 1) page = 1;
    memset(im, 0, sizeof *im);
    if (file_is_dir(path)) {
        char **names = NULL;
        int n = 0, i, seen = 0;
        if (dir_list(path, &names, &n) != 0) return -1;
        qsort(names, (size_t)n, sizeof *names, name_cmp);
        for (i = 0; i < n; i++) {
            char *child;
            int rc;
            if (!is_image_name(names[i])) continue;
            seen++;
            if (seen != page) continue;
            child = path_join(path, names[i]);
            rc = load_raster(child, im);
            free(child);
            dir_list_free(names, n);
            return rc;
        }
        dir_list_free(names, n);
        recto_seterr("that folder has no picture page %d", page);
        return -1;
    }
    if (is_pdf_name(path)) return pdf_page_raster(path, page, im);
    if (is_djvu_name(path)) {
        char pgm[RECTO_PATH];
        Buf cmd, sink;
        int rc;
        if (!which_cmd("ddjvu")) {
            recto_seterr("ddjvu is not installed, so this DjVu page cannot be rasterized");
            return -1;
        }
        temp_path(pgm, sizeof pgm, ".pgm");
        buf_init(&cmd);
        buf_init(&sink);
        buf_printf(&cmd, "ddjvu -format=pgm -page=%d ", page);
        sh_quote(&cmd, path);
        buf_putc(&cmd, ' ');
        sh_quote(&cmd, pgm);
        cmd_capture(cmd.p, &sink);
        buf_free(&cmd);
        buf_free(&sink);
        rc = image_load(pgm, im);
        remove(pgm);
        return rc;
    }
    if (page != 1) {
        recto_seterr("that file has one page");
        return -1;
    }
    return load_raster(path, im);
}

static void append_ocr(Word **words, int *n, int *cap, const OcrPage *op, int page, const char *label) {
    int i;
    if (op->n > 0) {
        for (i = 0; i < op->n; i++)
            add_word(words, n, cap, op->words[i].text, page, label, op->words[i].x, op->words[i].y, op->words[i].w,
                     op->words[i].h, op->words[i].conf, 1, 0);
        return;
    }
    add_word(words, n, cap, op->text, page, label, 0, 0, 0, 12, op->conf, 0, 1);
}

static int ocr_into(Word **words, int *n, int *cap, const Image *im, const Settings *set, int page, const char *label,
                    int *pictures) {
    OcrPage op;
    int letters, have;
    memset(&op, 0, sizeof op);
    if (ocr_image(im, set, &op) != 0) {
        ocr_page_free(&op);
        return -1;
    }
    letters = ocr_letters(&op);
    have = page_letters(*words, *n, page);
    if (set && set->pictures == PIC_ALL && have > 0 && letters <= have) {
        ocr_page_free(&op);
        return 0;
    }
    if (have > 0) drop_page(words, n, page);
    append_ocr(words, n, cap, &op, page, label);
    if (pictures) *pictures += op.pictures;
    ocr_page_free(&op);
    return 0;
}

static void lines_from_text(const char *text, Word **words, int *n, int *cap, int *pages) {
    int page = 1;
    const char *p = text ? text : "";
    while (*p) {
        const char *eol = strchr(p, '\n');
        size_t len = eol ? (size_t)(eol - p) : strlen(p);
        char *line = xstrndup(p, len);
        char *s;
        if (len && line[len - 1] == '\r') line[len - 1] = 0;
        s = str_trim(line);
        if (s[0] == '\f') {
            page++;
            s++;
            s = str_trim(s);
        }
        if (s[0]) add_word(words, n, cap, s, page, "", 0, 0, 0, 12, 100, 0, 0);
        else if (*n > 0) (*words)[*n - 1].brk = 1;
        free(line);
        if (!eol) break;
        p = eol + 1;
    }
    if (pages && page > *pages) *pages = page;
}

static int capture_tool(const char *cmd, Word **words, int *n, int *cap, int *pages) {
    Buf out;
    int rc;
    buf_init(&out);
    rc = cmd_capture(cmd, &out);
    if (rc != 0) {
        buf_free(&out);
        return -1;
    }
    lines_from_text(out.p, words, n, cap, pages);
    buf_free(&out);
    return 0;
}

static int read_pdftotext(const char *path, Word **words, int *n, int *cap, int *pages) {
    Buf cmd;
    int rc;
    if (!which_cmd("pdftotext")) {
        recto_seterr("this PDF has no readable text layer, and pdftotext is not installed");
        return -1;
    }
    buf_init(&cmd);
    buf_puts(&cmd, "pdftotext -layout -enc UTF-8 ");
    sh_quote(&cmd, path);
    buf_puts(&cmd, " -");
    rc = capture_tool(cmd.p, words, n, cap, pages);
    buf_free(&cmd);
    return rc;
}

static int read_djvu(const char *path, Word **words, int *n, int *cap, int *pages) {
    Buf cmd;
    int rc;
    if (!which_cmd("djvutxt")) {
        recto_seterr("djvutxt is not installed, so this DjVu file cannot be read");
        return -1;
    }
    buf_init(&cmd);
    buf_puts(&cmd, "djvutxt ");
    sh_quote(&cmd, path);
    rc = capture_tool(cmd.p, words, n, cap, pages);
    buf_free(&cmd);
    return rc;
}

static void finish_book(Book *b, Word *words, int nwords, int pages, const Settings *set, const char *path,
                        const char *format, int pictures) {
    book_init(b);
    compose_book(words, nwords, pages, set, b);
    title_from_path(b, path);
    if (!b->language[0]) snprintf(b->language, sizeof b->language, "%s", set && set->scripts[0] ? set->scripts : "eng");
    snprintf(b->format, sizeof b->format, "%s", format);
    snprintf(b->type, sizeof b->type, "Text");
    snprintf(b->origin_name, sizeof b->origin_name, "%s", path_base(path));
    if (!b->source[0]) snprintf(b->source, sizeof b->source, "%s", path);
    b->pictures += pictures;
    if (b->place_page < 1) b->place_page = 1;
}

static const char *label_of(char **labels, int npages, int page) {
    if (!labels || page < 1 || page > npages || !labels[page] || !labels[page][0]) return "";
    return labels[page];
}

static int read_pdf(const char *path, const Settings *set, Book *b) {
    Word *words = NULL;
    int nwords = 0, cap = 0, npages = 0, pictures = 0, page;
    char **labels = NULL;
    Book meta;
    memset(&meta, 0, sizeof meta);
    if (pdf_extract_native(path, &words, &nwords, &npages, &labels, &meta) != 0) {
        words_free(words, nwords);
        words = NULL;
        nwords = 0;
        npages = 0;
        if (read_pdftotext(path, &words, &nwords, &cap, &npages) != 0) {
            book_free(&meta);
            return -1;
        }
    }
    if (cap < nwords) cap = nwords;
    if (npages < 1) npages = 1;
    if (set && set->pictures != PIC_UNREAD) {
        for (page = 1; page <= npages; page++) {
            int letters = page_letters(words, nwords, page);
            int need = (set->pictures == PIC_EMPTY) ? (letters == 0) : (letters < 40);
            Image im;
            if (!need) continue;
            memset(&im, 0, sizeof im);
            if (pdf_page_raster(path, page, &im) != 0) continue;
            ocr_into(&words, &nwords, &cap, &im, set, page, label_of(labels, npages, page), &pictures);
            image_free(&im);
        }
    }
    finish_book(b, words, nwords, npages, set, path, "PDF", pictures);
    if (meta.title[0]) snprintf(b->title, sizeof b->title, "%s", meta.title);
    if (meta.creator[0]) snprintf(b->creator, sizeof b->creator, "%s", meta.creator);
    words_free(words, nwords);
    free_labels(labels, npages);
    book_free(&meta);
    return 0;
}

static int tag_name_is(const char *p, const char *name) {
    size_t i;
    for (i = 0; name[i]; i++) {
        unsigned char a = (unsigned char)p[i];
        unsigned char b = (unsigned char)name[i];
        if (a >= 'A' && a <= 'Z') a = (unsigned char)(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = (unsigned char)(b - 'A' + 'a');
        if (a != b) return 0;
    }
    return p[i] == '>' || p[i] == '/' || isspace((unsigned char)p[i]);
}

static const char *skip_tag(const char *p) {
    const char *q = strchr(p, '>');
    return q ? q + 1 : p + strlen(p);
}

static const char *skip_until(const char *p, const char *endtag) {
    size_t n = strlen(endtag);
    while (*p) {
        if (*p == '<' && p[1] == '/') {
            const char *q = p + 2;
            size_t i;
            for (i = 0; i < n; i++) {
                unsigned char a = (unsigned char)q[i];
                unsigned char b = (unsigned char)endtag[i];
                if (a >= 'A' && a <= 'Z') a = (unsigned char)(a - 'A' + 'a');
                if (a != b) break;
            }
            if (i == n) return skip_tag(q + n - 1 < q ? q : p);
        }
        p++;
    }
    return p;
}

static int html_entity(const char *p, char *out, const char **next) {
    int n = 0;
    if (p[0] != '&') return 0;
    if (str_istart(p, "&amp;")) {
        *out = '&';
        *next = p + 5;
        return 1;
    }
    if (str_istart(p, "&lt;")) {
        *out = '<';
        *next = p + 4;
        return 1;
    }
    if (str_istart(p, "&gt;")) {
        *out = '>';
        *next = p + 4;
        return 1;
    }
    if (str_istart(p, "&quot;")) {
        *out = '"';
        *next = p + 6;
        return 1;
    }
    if (str_istart(p, "&apos;")) {
        *out = '\'';
        *next = p + 6;
        return 1;
    }
    if (str_istart(p, "&nbsp;")) {
        *out = ' ';
        *next = p + 6;
        return 1;
    }
    if (p[1] == '#' ) {
        const char *s = p + 2;
        unsigned long v = 0;
        if (*s == 'x' || *s == 'X') {
            s++;
            while (isxdigit((unsigned char)*s)) {
                unsigned char c = (unsigned char)*s++;
                v *= 16;
                if (c >= '0' && c <= '9') v += (unsigned)(c - '0');
                else if (c >= 'a' && c <= 'f') v += (unsigned)(c - 'a' + 10);
                else v += (unsigned)(c - 'A' + 10);
            }
        } else {
            while (isdigit((unsigned char)*s)) v = v * 10 + (unsigned)(*s++ - '0');
        }
        if (*s == ';') {
            *out = v < 128 ? (char)v : '?';
            *next = s + 1;
            return 1;
        }
    }
    while (p[n] && p[n] != ';' && n < 12) n++;
    if (p[n] == ';') {
        *out = '?';
        *next = p + n + 1;
        return 1;
    }
    return 0;
}

static char *html_to_text(const char *s) {
    Buf b;
    const char *p = s ? s : "";
    int line = 0;
    buf_init(&b);
    while (*p) {
        if (*p == '<') {
            int block = tag_name_is(p + 1, "p") || tag_name_is(p + 1, "div") || tag_name_is(p + 1, "h1") ||
                        tag_name_is(p + 1, "h2") || tag_name_is(p + 1, "h3") || tag_name_is(p + 1, "li") ||
                        tag_name_is(p + 1, "br") || tag_name_is(p + 1, "tr") || tag_name_is(p + 1, "section") ||
                        tag_name_is(p + 1, "blockquote") || tag_name_is(p + 1, "/p") || tag_name_is(p + 1, "/h1") ||
                        tag_name_is(p + 1, "/h2") || tag_name_is(p + 1, "/div") || tag_name_is(p + 1, "/li");
            if (tag_name_is(p + 1, "script")) {
                p = skip_until(skip_tag(p + 1), "script");
                continue;
            }
            if (tag_name_is(p + 1, "style")) {
                p = skip_until(skip_tag(p + 1), "style");
                continue;
            }
            if (block && line) buf_putc(&b, '\n');
            line = 0;
            p = skip_tag(p + 1);
            continue;
        }
        if (*p == '&') {
            char ch;
            const char *next = NULL;
            if (html_entity(p, &ch, &next)) {
                buf_putc(&b, ch);
                line = 1;
                p = next;
                continue;
            }
        }
        if (*p == '\r') {
            p++;
            continue;
        }
        buf_putc(&b, *p);
        line = (*p != '\n');
        p++;
    }
    return buf_steal(&b);
}

static char *markdown_to_text(const char *s) {
    Buf b;
    const char *p = s ? s : "";
    buf_init(&b);
    while (*p) {
        const char *eol = strchr(p, '\n');
        size_t len = eol ? (size_t)(eol - p) : strlen(p);
        const char *q = p;
        size_t n = len;
        if (n && q[n - 1] == '\r') n--;
        while (n && (*q == '#' || *q == ' ')) {
            if (*q == '#') {
                q++;
                n--;
                if (n && *q == ' ') {
                    q++;
                    n--;
                }
                break;
            }
            break;
        }
        buf_append(&b, q, n);
        buf_putc(&b, '\n');
        if (!eol) break;
        p = eol + 1;
    }
    return buf_steal(&b);
}

static int looks_binary(const char *p, size_t n) {
    size_t i, lim = n < 4096 ? n : 4096;
    for (i = 0; i < lim; i++)
        if (p[i] == 0) return 1;
    return 0;
}

static int read_text_file(const char *path, const Settings *set, Book *b) {
    Buf raw;
    Word *words = NULL;
    int n = 0, cap = 0, pages = 1;
    const char *format = "text";
    char *cooked = NULL;
    const char *body;
    buf_init(&raw);
    if (file_read(path, &raw) != 0) return -1;
    if (raw.p && strncmp(raw.p, "recto 1", 7) == 0) {
        int rc = book_read_mem(raw.p, b);
        buf_free(&raw);
        if (rc == 0) {
            title_from_path(b, path);
            if (!b->origin_name[0]) snprintf(b->origin_name, sizeof b->origin_name, "%s", path_base(path));
        }
        return rc;
    }
    if (looks_binary(raw.p ? raw.p : "", raw.n)) {
        buf_free(&raw);
        recto_seterr("%s is not a text file Recto can convert", path_base(path));
        return -1;
    }
    body = raw.p ? raw.p : "";
    if (ext_is(path, ".html") || ext_is(path, ".htm") || ext_is(path, ".xhtml")) {
        cooked = html_to_text(body);
        body = cooked ? cooked : "";
        format = "HTML";
    } else if (ext_is(path, ".md") || ext_is(path, ".markdown")) {
        cooked = markdown_to_text(body);
        body = cooked ? cooked : "";
        format = "Markdown";
    }
    lines_from_text(body, &words, &n, &cap, &pages);
    free(cooked);
    buf_free(&raw);
    finish_book(b, words, n, pages, set, path, format, 0);
    words_free(words, n);
    return 0;
}

static int read_images(const char **paths, int npaths, const Settings *set, Book *b, const char *title_path) {
    Word *words = NULL;
    int nwords = 0, cap = 0, i, pictures = 0, pages = 0;
    if (set && set->pictures == PIC_UNREAD) {
        recto_seterr("picture reading is off (pictures=unread)");
        return -1;
    }
    for (i = 0; i < npaths; i++) {
        OcrPage op;
        char label[24];
        pages++;
        snprintf(label, sizeof label, "%d", pages);
        memset(&op, 0, sizeof op);
        if (read_ocr_image(paths[i], set, &op) != 0) {
            ocr_page_free(&op);
            words_free(words, nwords);
            return -1;
        }
        append_ocr(&words, &nwords, &cap, &op, pages, label);
        pictures += op.pictures;
        ocr_page_free(&op);
    }
    finish_book(b, words, nwords, pages > 0 ? pages : 1, set, title_path, "image", pictures);
    words_free(words, nwords);
    if (set && set->engine == ENG_BUILTIN)
        snprintf(b->reader_note, sizeof b->reader_note, "Read with the built-in letter reader.");
    else snprintf(b->reader_note, sizeof b->reader_note, "Picture pages were read for this conversion.");
    return 0;
}

static int import_file(const char *path, const Settings *set, Book **books, int *nbooks, int *cap) {
    Book one;
    memset(&one, 0, sizeof one);
    if (ext_is(path, ".epub")) {
        recto_seterr("%s is already an epub", path_base(path));
        return -1;
    }
    if (is_pdf_name(path)) {
        if (read_pdf(path, set, &one) != 0) return -1;
    } else if (is_djvu_name(path)) {
        Word *words = NULL;
        int n = 0, wcap = 0, pages = 1;
        if (read_djvu(path, &words, &n, &wcap, &pages) != 0) return -1;
        finish_book(&one, words, n, pages, set, path, "DjVu", 0);
        words_free(words, n);
    } else if (is_image_name(path)) {
        const char *onepath = path;
        if (read_images(&onepath, 1, set, &one, path) != 0) return -1;
    } else if (read_text_file(path, set, &one) != 0) return -1;
    return push_book(books, nbooks, cap, &one);
}

static int import_dir(const char *path, const Settings *set, Book **books, int *nbooks, int *cap) {
    char **names = NULL;
    int n = 0, i, imgs = 0, made = 0;
    char **img = NULL;
    int icap = 0;
    if (dir_list(path, &names, &n) != 0) return -1;
    qsort(names, (size_t)n, sizeof *names, name_cmp);
    for (i = 0; i < n; i++) {
        char *child = path_join(path, names[i]);
        if (file_is_dir(child)) {
            free(child);
            continue;
        }
        if (is_pdf_name(child) || is_djvu_name(child)) {
            if (import_file(child, set, books, nbooks, cap) != 0) {
                free(child);
                dir_list_free(names, n);
                dir_list_free(img, imgs);
                return -1;
            }
            made++;
        } else if (is_image_name(child)) {
            if (imgs >= icap) {
                icap = icap ? icap * 2 : 8;
                img = xrealloc(img, (size_t)icap * sizeof *img);
            }
            img[imgs++] = xstrdup(child);
        }
        free(child);
    }
    dir_list_free(names, n);
    if (imgs) {
        Book one;
        memset(&one, 0, sizeof one);
        if (read_images((const char **)img, imgs, set, &one, path) != 0) {
            dir_list_free(img, imgs);
            return -1;
        }
        push_book(books, nbooks, cap, &one);
        made++;
    }
    dir_list_free(img, imgs);
    if (!made) {
        recto_seterr("no books in %s", path);
        return -1;
    }
    return 0;
}

int import_paths(const char **paths, int npaths, const Settings *set, Book **books, int *nbooks) {
    Settings fallback;
    int cap = 0, i;
    const Settings *use = set;
    if (!books || !nbooks) return -1;
    *books = NULL;
    *nbooks = 0;
    if (!set) {
        settings_default(&fallback);
        use = &fallback;
    }
    if (npaths <= 0) {
        recto_seterr("open needs a file or a folder");
        return -1;
    }
    for (i = 0; i < npaths; i++) {
        int rc;
        if (!paths[i] || !paths[i][0]) {
            recto_seterr("open needs a file or a folder");
            rc = -1;
        } else if (file_is_dir(paths[i])) rc = import_dir(paths[i], use, books, nbooks, &cap);
        else if (!file_exists(paths[i])) {
            recto_seterr("cannot open %s", paths[i]);
            rc = -1;
        } else rc = import_file(paths[i], use, books, nbooks, &cap);
        if (rc != 0) {
            int k;
            for (k = 0; k < *nbooks; k++) book_free(&(*books)[k]);
            free(*books);
            *books = NULL;
            *nbooks = 0;
            return -1;
        }
    }
    if (*nbooks == 0) {
        recto_seterr("nothing in that path could be opened");
        return -1;
    }
    return 0;
}

static int ends_with_epub(const char *path) {
    return ext_is(path, ".epub");
}

static void path_with_epub_ext(const char *src, char *out, size_t cap) {
    const char *base;
    const char *dot;
    size_t keep;
    if (!out || !cap) return;
    out[0] = 0;
    if (!src) src = "";
    base = path_base(src);
    dot = strrchr(base, '.');
    if (!dot || dot == base) {
        snprintf(out, cap, "%s.epub", src);
        return;
    }
    keep = (size_t)(dot - src);
    if (keep + 6 > cap) keep = cap > 6 ? cap - 6 : 0;
    if (keep) memcpy(out, src, keep);
    memcpy(out + keep, ".epub", 6);
}

static int ensure_parent(const char *path) {
    char tmp[RECTO_PATH];
    char *slash = NULL;
    char *p;
    size_t n;
    if (!path || !path[0]) return 0;
    n = strlen(path);
    if (n >= sizeof tmp) {
        recto_seterr("path too long");
        return -1;
    }
    memcpy(tmp, path, n + 1);
    for (p = tmp; *p; p++)
        if (*p == '/' || *p == '\\') slash = p;
    if (!slash || slash == tmp) return 0;
    *slash = 0;
    if (!tmp[0]) return 0;
    return dir_make_p(tmp);
}

static void leaf_name(const char *title, char *out, size_t cap) {
    size_t i, o = 0;
    int dash = 0;
    if (!out || cap < 2) return;
    if (!title) title = "";
    for (i = 0; title[i] && o + 1 < cap && o < 80; i++) {
        unsigned char c = (unsigned char)title[i];
        if (c >= 'A' && c <= 'Z') c = (unsigned char)(c - 'A' + 'a');
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
            out[o++] = (char)c;
            dash = 0;
        } else if (o && !dash) {
            out[o++] = '-';
            dash = 1;
        }
    }
    while (o && out[o - 1] == '-') o--;
    out[o] = 0;
    if (!out[0]) snprintf(out, cap, "book");
}

static int leaf_taken(char **paths, int n, const char *leaf) {
    int i;
    for (i = 0; i < n; i++) {
        if (paths && paths[i] && str_ieq(path_base(paths[i]), leaf)) return 1;
    }
    return 0;
}

static void choose_leaf(char **paths, int n, const char *title, char *out, size_t cap) {
    char base[88];
    int k;
    leaf_name(title, base, sizeof base);
    snprintf(out, cap, "%s.epub", base);
    if (!leaf_taken(paths, n, out)) return;
    for (k = 2; k < 10000; k++) {
        snprintf(out, cap, "%s-%d.epub", base, k);
        if (!leaf_taken(paths, n, out)) return;
    }
}

static void free_books(Book *books, int n) {
    int i;
    if (!books) return;
    for (i = 0; i < n; i++) book_free(&books[i]);
    free(books);
}

static int push_out(char ***paths, char ***ids, int *n, int *cap, const char *path, const char *id) {
    if (*n >= *cap) {
        *cap = *cap ? *cap * 2 : 4;
        *paths = xrealloc(*paths, (size_t)(*cap) * sizeof **paths);
        *ids = xrealloc(*ids, (size_t)(*cap) * sizeof **ids);
    }
    (*paths)[*n] = xstrdup(path ? path : "");
    (*ids)[*n] = xstrdup(id ? id : "");
    (*n)++;
    return 0;
}

void convert_free(char **paths, char **ids, int n) {
    int i;
    if (n > 0 && paths) {
        for (i = 0; i < n; i++) free(paths[i]);
    }
    free(paths);
    if (n > 0 && ids) {
        for (i = 0; i < n; i++) free(ids[i]);
    }
    free(ids);
}

int convert_files(const char **inputs, int ninputs, const char *dest, const Settings *set, Shelf *shelf,
                  char ***paths, char ***ids, int *nouts) {
    Book *books = NULL;
    char **op = NULL;
    char **oi = NULL;
    int nbooks = 0, n = 0, cap = 0, i, dir_mode = 0;
    if (!paths || !ids || !nouts) return -1;
    *paths = NULL;
    *ids = NULL;
    *nouts = 0;
    if (dest && !dest[0]) dest = NULL;
    if (import_paths(inputs, ninputs, set, &books, &nbooks) != 0) return -1;
    if (dest && ends_with_epub(dest)) {
        if (nbooks != 1) {
            recto_seterr("one epub file is one book; give a folder for several");
            free_books(books, nbooks);
            return -1;
        }
    } else if (dest) {
        if (file_exists(dest) && !file_is_dir(dest)) {
            recto_seterr("%s is not a folder", dest);
            free_books(books, nbooks);
            return -1;
        }
        if (dir_make_p(dest) != 0) {
            free_books(books, nbooks);
            return -1;
        }
        dir_mode = 1;
    }
    for (i = 0; i < nbooks; i++) {
        char outp[RECTO_PATH];
        const char *origin;
        outp[0] = 0;
        if (dest && !dir_mode) {
            if (strlen(dest) >= sizeof outp) {
                recto_seterr("path too long");
                free_books(books, nbooks);
                convert_free(op, oi, n);
                return -1;
            }
            snprintf(outp, sizeof outp, "%s", dest);
        } else if (dir_mode) {
            char leaf[112];
            char *full;
            choose_leaf(op, n, books[i].title, leaf, sizeof leaf);
            full = path_join(dest, leaf);
            if (!full || strlen(full) >= sizeof outp) {
                free(full);
                recto_seterr("path too long");
                free_books(books, nbooks);
                convert_free(op, oi, n);
                return -1;
            }
            snprintf(outp, sizeof outp, "%s", full);
            free(full);
        } else path_with_epub_ext(books[i].source, outp, sizeof outp);
        if (!outp[0]) {
            recto_seterr("the epub path is empty");
            free_books(books, nbooks);
            convert_free(op, oi, n);
            return -1;
        }
        if (ensure_parent(outp) != 0 || epub_write(&books[i], outp) != 0) {
            free_books(books, nbooks);
            convert_free(op, oi, n);
            return -1;
        }
        origin = books[i].source;
        if (shelf && shelf_add(shelf, &books[i], origin[0] ? origin : NULL) != 0) {
            free_books(books, nbooks);
            convert_free(op, oi, n);
            return -1;
        }
        push_out(&op, &oi, &n, &cap, outp, shelf ? books[i].id : "");
    }
    free_books(books, nbooks);
    *paths = op;
    *ids = oi;
    *nouts = n;
    return 0;
}
