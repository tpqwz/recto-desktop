#include "recto.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <io.h>
#else
#include <signal.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>
#endif

enum { M_SHELF = 0, M_BOOK, M_TOC, M_LIB, M_SET, M_HITS, M_ASK };

enum { ASK_OPEN = 1, ASK_FIND, ASK_GO, ASK_RECT, ASK_EPUB, ASK_SCRIPTS, ASK_MODEL };

typedef struct Desk {
    Shelf *shelf;
    int mode;
    int sel;
    int armed;
    int have;
    Book book;
    int cur;
    int vtop;
    char **lines;
    int nlines;
    int lcap;
    char status[240];
    char ask[RECTO_PATH];
    int ask_n;
    int ask_kind;
    Hit *hits;
    int nhits;
    int hit;
    int set_cur;
    int rows, cols;
    int quit;
} Desk;

#ifndef _WIN32
static struct termios term_saved;
static int term_live;

static void term_end(void) {
    if (!term_live) return;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &term_saved);
    {
        const char *s = "\033[?25h\033[?1049l";
        if (write(STDOUT_FILENO, s, strlen(s)) < 0) {
            /* The terminal is already going away. */
        }
    }
    term_live = 0;
}

static void on_signal(int sig) {
    (void)sig;
    term_end();
    _exit(1);
}

static int term_begin(void) {
    struct termios raw;
    if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO)) {
        recto_seterr("the desk needs a terminal; use recto help");
        return -1;
    }
    if (tcgetattr(STDIN_FILENO, &term_saved) != 0) return -1;
    raw = term_saved;
    raw.c_lflag &= (unsigned)~(ICANON | ECHO);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) != 0) return -1;
    term_live = 1;
    {
        const char *s = "\033[?1049h\033[?25l\033[2J\033[H";
        if (write(STDOUT_FILENO, s, strlen(s)) < 0) {
            term_end();
            return -1;
        }
    }
    atexit(term_end);
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    return 0;
}

static int read_byte(unsigned char *c, int wait) {
    struct termios t;
    ssize_t n;
    if (wait) {
        if (tcgetattr(STDIN_FILENO, &t) != 0) return 0;
        t.c_cc[VMIN] = 0;
        t.c_cc[VTIME] = 1;
        tcsetattr(STDIN_FILENO, TCSANOW, &t);
    }
    do {
        n = read(STDIN_FILENO, c, 1);
    } while (n < 0 && errno == EINTR);
    if (wait) {
        t.c_cc[VMIN] = 1;
        t.c_cc[VTIME] = 0;
        tcsetattr(STDIN_FILENO, TCSANOW, &t);
    }
    return n == 1;
}

static int read_key(void) {
    unsigned char c, a, b;
    if (!read_byte(&c, 0)) return -1;
    if (c == 3) return 'q';
    if (c == '\r' || c == '\n') return '\n';
    if (c == 127 || c == 8) return 8;
    if (c != 27) return (int)c;
    if (!read_byte(&a, 1)) return 27;
    if (a != '[' && a != 'O') return 27;
    if (!read_byte(&b, 1)) return 27;
    if (b == 'A') return 'k';
    if (b == 'B') return 'j';
    if (b == 'C' || b == '5') return 'n';
    if (b == 'D' || b == '6') return 'p';
    return 27;
}
#endif

static void set_status(Desk *d, const char *s) {
    snprintf(d->status, sizeof d->status, "%s", s ? s : "");
}

static void measure(Desk *d) {
    d->rows = 24;
    d->cols = 80;
#ifndef _WIN32
    {
        struct winsize ws;
        if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0) {
            if (ws.ws_row > 6) d->rows = ws.ws_row;
            if (ws.ws_col > 20) d->cols = ws.ws_col;
        }
    }
#endif
    if (d->cols > 200) d->cols = 200;
}

static void free_lines(Desk *d) {
    int i;
    for (i = 0; i < d->nlines; i++) free(d->lines[i]);
    free(d->lines);
    d->lines = NULL;
    d->nlines = 0;
    d->lcap = 0;
}

static void push_line(Desk *d, Buf *line) {
    if (d->nlines >= d->lcap) {
        d->lcap = d->lcap ? d->lcap * 2 : 16;
        d->lines = xrealloc(d->lines, (size_t)d->lcap * sizeof *d->lines);
    }
    d->lines[d->nlines++] = buf_steal(line);
}

static void rebuild_view(Desk *d) {
    const char *text, *p;
    int width;
    Buf line;
    free_lines(d);
    if (!d->have || d->cur < 0 || d->cur >= d->book.nblocks) return;
    text = d->book.blocks[d->cur].text ? d->book.blocks[d->cur].text : "";
    width = d->cols - 2;
    if (width < 16) width = 16;
    buf_init(&line);
    p = text;
    while (*p) {
        int at_break = (*p == '\n');
        if (at_break || (int)line.n >= width) {
            if (!at_break && line.n) {
                size_t k = line.n;
                while (k > 0 && line.p[k - 1] != ' ') k--;
                if (k > 8) {
                    char *rest = xstrdup(line.p + k);
                    line.n = k;
                    if (k && line.p[k - 1] == ' ') line.n = k - 1;
                    line.p[line.n] = 0;
                    push_line(d, &line);
                    buf_init(&line);
                    buf_puts(&line, rest);
                    free(rest);
                    continue;
                }
            }
            push_line(d, &line);
            buf_init(&line);
            if (at_break) {
                p++;
                continue;
            }
        }
        if (*p != '\n') buf_putc(&line, *p == '\t' ? ' ' : *p);
        p++;
    }
    if (line.n || d->nlines == 0) push_line(d, &line);
    else buf_free(&line);
}

static void save_place(Desk *d) {
    if (!d->have || !d->book.id[0]) return;
    if (d->book.nblocks > 0 && d->cur >= 0 && d->cur < d->book.nblocks) {
        d->book.place_block = d->cur;
        d->book.place_page = d->book.blocks[d->cur].page;
    }
    shelf_save(d->shelf, &d->book);
    shelf_refresh(d->shelf);
}

static void open_index(Desk *d, int index) {
    if (index < 0 || index >= d->shelf->nsum) return;
    save_place(d);
    book_free(&d->book);
    memset(&d->book, 0, sizeof d->book);
    if (shelf_load(d->shelf, d->shelf->sum[index].id, &d->book) != 0) {
        set_status(d, recto_err);
        d->have = 0;
        return;
    }
    d->have = 1;
    d->cur = d->book.place_block;
    if (d->cur < 0 || d->cur >= d->book.nblocks) d->cur = 0;
    d->vtop = 0;
    d->mode = M_BOOK;
    d->sel = index;
    rebuild_view(d);
    set_status(d, d->book.title);
}

static int parse_rect(const char *s, int *x, int *y, int *w, int *h) {
    char buf[80];
    int i;
    snprintf(buf, sizeof buf, "%s", s ? s : "");
    for (i = 0; buf[i]; i++)
        if (buf[i] == ',') buf[i] = ' ';
    return sscanf(buf, "%d %d %d %d", x, y, w, h) == 4;
}

static int book_origin(Desk *d, char *out, size_t cap) {
    char *books, *dir, *path;
    Buf b;
    char *s;
    if (!d->have) return -1;
    books = path_join(d->shelf->dir, "books");
    dir = path_join(books, d->book.id);
    path = path_join(dir, "origin.txt");
    free(books);
    free(dir);
    buf_init(&b);
    if (file_read(path, &b) != 0) {
        free(path);
        buf_free(&b);
        recto_seterr("the original file is not on the shelf");
        return -1;
    }
    free(path);
    s = str_trim(b.p ? b.p : "");
    snprintf(out, cap, "%s", s);
    buf_free(&b);
    return out[0] ? 0 : -1;
}

static void replace_page(Book *b, int page, const char *label, const char *text) {
    int i, keep = -1, w = 0;
    for (i = 0; i < b->nblocks; i++) {
        if (b->blocks[i].page != page) continue;
        if (keep < 0) {
            keep = i;
            free(b->blocks[i].text);
            b->blocks[i].text = xstrdup(text ? text : "");
            b->blocks[i].kind = BLK_P;
            if (label && label[0]) snprintf(b->blocks[i].label, sizeof b->blocks[i].label, "%s", label);
        } else {
            free(b->blocks[i].text);
            b->blocks[i].text = NULL;
        }
    }
    for (i = 0; i < b->nblocks; i++) {
        if (b->blocks[i].text) b->blocks[w++] = b->blocks[i];
    }
    b->nblocks = w;
    if (keep < 0) book_add_block(b, BLK_P, page, label, text ? text : "", 0, 0, 0, 0, 80);
    book_rebuild_toc(b);
}

static void reread_page(Desk *d, int x, int y, int w, int h, int cropped) {
    char origin[RECTO_PATH];
    Image im, crop;
    OcrPage op;
    int page;
    const char *label = "";
    if (!d->have) return;
    if (book_origin(d, origin, sizeof origin) != 0) {
        set_status(d, recto_err);
        return;
    }
    page = (d->book.nblocks > 0) ? d->book.blocks[d->cur].page : 1;
    if (d->book.nblocks > 0) label = d->book.blocks[d->cur].label;
    memset(&im, 0, sizeof im);
    memset(&crop, 0, sizeof crop);
    memset(&op, 0, sizeof op);
    if (load_page_image(origin, page, &im) != 0) {
        set_status(d, recto_err);
        return;
    }
    if (cropped) {
        if (image_crop(&im, x, y, w, h, &crop) != 0) {
            image_free(&im);
            set_status(d, recto_err);
            return;
        }
        image_free(&im);
        im = crop;
        memset(&crop, 0, sizeof crop);
    }
    if (ocr_image(&im, &d->shelf->set, &op) != 0) {
        image_free(&im);
        ocr_page_free(&op);
        set_status(d, recto_err);
        return;
    }
    image_free(&im);
    if (shelf_revise(d->shelf, &d->book, -1, NULL) != 0) {
        ocr_page_free(&op);
        set_status(d, recto_err);
        return;
    }
    replace_page(&d->book, page, label, op.text ? op.text : "");
    snprintf(d->book.reader_note, sizeof d->book.reader_note, "Page reread with %s.", op.engine);
    shelf_save(d->shelf, &d->book);
    shelf_refresh(d->shelf);
    ocr_page_free(&op);
    if (d->cur >= d->book.nblocks) d->cur = d->book.nblocks ? d->book.nblocks - 1 : 0;
    d->vtop = 0;
    rebuild_view(d);
    set_status(d, d->book.reader_note);
}

static void add_paths(Desk *d, const char *path) {
    const char *paths[1];
    Book *books = NULL;
    int n = 0, i;
    paths[0] = path;
    if (import_paths(paths, 1, &d->shelf->set, &books, &n) != 0) {
        set_status(d, recto_err);
        return;
    }
    for (i = 0; i < n; i++) {
        const char *origin = NULL;
        if (books[i].source[0] && (file_exists(books[i].source) || file_is_dir(books[i].source))) origin = books[i].source;
        if (shelf_add(d->shelf, &books[i], origin) != 0) {
            set_status(d, recto_err);
            break;
        }
        set_status(d, books[i].title);
    }
    for (i = 0; i < n; i++) book_free(&books[i]);
    free(books);
    if (d->shelf->nsum > 0) d->sel = d->shelf->nsum - 1;
}

static void start_ask(Desk *d, int kind, const char *prompt) {
    d->mode = M_ASK;
    d->ask_kind = kind;
    d->ask_n = 0;
    d->ask[0] = 0;
    set_status(d, prompt);
}

static void finish_ask(Desk *d) {
    int kind = d->ask_kind;
    char text[RECTO_PATH];
    snprintf(text, sizeof text, "%s", d->ask);
    d->mode = d->have && kind != ASK_OPEN ? M_BOOK : M_SHELF;
    if (kind == ASK_SCRIPTS || kind == ASK_MODEL) d->mode = M_SET;
    if (kind == ASK_FIND && d->have) {
        free(d->hits);
        d->hits = NULL;
        d->nhits = 0;
        if (book_find(&d->book, text, &d->hits, &d->nhits) != 0) {
            set_status(d, recto_err);
            return;
        }
        d->hit = 0;
        d->mode = M_HITS;
        if (d->nhits == 0) set_status(d, "not found");
        else set_status(d, "enter opens a hit");
        return;
    }
    if (kind == ASK_GO && d->have) {
        int b = book_label_block(&d->book, text);
        if (b < 0) {
            set_status(d, "no page with that number");
            d->mode = M_BOOK;
            return;
        }
        d->cur = b;
        d->vtop = 0;
        d->mode = M_BOOK;
        rebuild_view(d);
        return;
    }
    if (kind == ASK_OPEN) {
        d->mode = M_SHELF;
        add_paths(d, text);
        return;
    }
    if (kind == ASK_EPUB && d->have) {
        if (epub_write(&d->book, text) != 0) set_status(d, recto_err);
        else set_status(d, "exported");
        d->mode = M_BOOK;
        return;
    }
    if (kind == ASK_RECT && d->have) {
        int x, y, w, h;
        d->mode = M_BOOK;
        if (!parse_rect(text, &x, &y, &w, &h)) {
            set_status(d, "crop is x y w h");
            return;
        }
        reread_page(d, x, y, w, h, 1);
        return;
    }
    if (kind == ASK_SCRIPTS) {
        char kv[120];
        snprintf(kv, sizeof kv, "scripts=%s", text);
        if (settings_apply(&d->shelf->set, kv) != 0) set_status(d, recto_err);
        else {
            shelf_save_settings(d->shelf);
            set_status(d, "scripts saved");
        }
        return;
    }
    if (kind == ASK_MODEL) {
        char kv[80];
        snprintf(kv, sizeof kv, "vision_model=%s", text);
        if (settings_apply(&d->shelf->set, kv) != 0) set_status(d, recto_err);
        else {
            shelf_save_settings(d->shelf);
            set_status(d, "model saved");
        }
    }
}

static void cycle_setting(Desk *d) {
    Settings *s = &d->shelf->set;
    const char *kv = NULL;
    switch (d->set_cur) {
    case 0:
        if (s->pictures == PIC_EMPTY) kv = "pictures=all";
        else if (s->pictures == PIC_ALL) kv = "pictures=unread";
        else kv = "pictures=empty";
        break;
    case 1: kv = s->keep_file ? "keep_file=0" : "keep_file=1"; break;
    case 2: kv = s->contrast ? "contrast=0" : "contrast=1"; break;
    case 3: kv = s->deskew ? "deskew=0" : "deskew=1"; break;
    case 4:
        if (s->columns == COL_AUTO) kv = "columns=one";
        else if (s->columns == COL_ONE) kv = "columns=two";
        else kv = "columns=auto";
        break;
    case 5:
        if (s->engine == ENG_BEST) kv = "engine=builtin";
        else if (s->engine == ENG_BUILTIN) kv = "engine=tesseract";
        else if (s->engine == ENG_TESS) kv = "engine=vision";
        else kv = "engine=best";
        break;
    case 6: kv = s->vision ? "vision=0" : "vision=1"; break;
    case 7: start_ask(d, ASK_SCRIPTS, "tesseract languages, such as eng"); return;
    case 8: start_ask(d, ASK_MODEL, "vision model, such as grok-4.7"); return;
    default: return;
    }
    if (kv && settings_apply(s, kv) == 0) shelf_save_settings(d->shelf);
}

static void draw_line(int row, int col, const char *text) {
    printf("\033[%d;%dH\033[K%s", row, col, text ? text : "");
}

static void clip_copy(char *dst, size_t cap, const char *src) {
    size_t i, n = 0;
    if (!src) src = "";
    for (i = 0; src[i] && n + 1 < cap; i++) {
        unsigned char c = (unsigned char)src[i];
        if (c == '\n' || c == '\t') c = ' ';
        if (c < 32) continue;
        dst[n++] = (char)c;
    }
    dst[n] = 0;
}

static void draw(Desk *d) {
    int i, body;
    char fit[256];
    measure(d);
    body = d->rows - 2;
    if (body < 1) body = 1;
    fputs("\033[H\033[2J", stdout);
    if (d->mode == M_SHELF) {
        draw_line(1, 1, "Recto: convert to EPUB");
        if (d->shelf->nsum == 0) draw_line(3, 3, "No conversions yet. Press o to add a file.");
        if (d->sel >= d->shelf->nsum) d->sel = d->shelf->nsum ? d->shelf->nsum - 1 : 0;
        if (d->sel < 0) d->sel = 0;
        for (i = 0; i < body - 1 && i < d->shelf->nsum; i++) {
            int index = i;
            const Book *b;
            char row[240];
            if (d->shelf->nsum > body - 1 && d->sel >= body - 1) index = d->sel - (body - 2) + i;
            if (index < 0 || index >= d->shelf->nsum) break;
            b = &d->shelf->sum[index];
            snprintf(row, sizeof row, "%c %-48.48s  %d", index == d->sel ? '>' : ' ', b->title[0] ? b->title : b->id,
                     b->file_pages);
            clip_copy(fit, (size_t)(d->cols > 250 ? 250 : d->cols), row);
            draw_line(i + 2, 1, fit);
        }
        clip_copy(fit, (size_t)d->cols, "j/k move   enter open   o open file   l library   , settings   s samples   d remove   q quit");
        draw_line(d->rows - 1, 1, fit);
    } else if (d->mode == M_BOOK) {
        char head[240];
        const Block *blk = NULL;
        if (d->have && d->book.nblocks > 0 && d->cur >= 0 && d->cur < d->book.nblocks) blk = &d->book.blocks[d->cur];
        snprintf(head, sizeof head, "%s", d->have ? d->book.title : "Recto");
        if (blk) {
            size_t n = strlen(head);
            snprintf(head + n, sizeof head - n, "    p. %s", blk->label[0] ? blk->label : "?");
        }
        clip_copy(fit, (size_t)d->cols, head);
        draw_line(1, 1, fit);
        if (!blk) draw_line(3, 1, "This book has no text yet.");
        else {
            int win = body - 1;
            if (win < 1) win = 1;
            if (d->vtop < 0) d->vtop = 0;
            for (i = 0; i < win && d->vtop + i < d->nlines; i++) draw_line(i + 2, 1, d->lines[d->vtop + i]);
        }
        clip_copy(fit, (size_t)d->cols,
                  "j/k scroll   n/p page   g number   / find   c contents   e epub   r reread   x crop   b shelf   q quit");
        draw_line(d->rows - 1, 1, fit);
    } else if (d->mode == M_TOC) {
        draw_line(1, 1, "Contents");
        if (d->book.ntoc == 0) draw_line(3, 1, "This book has no contents entries.");
        for (i = 0; i < body - 1 && i < d->book.ntoc; i++) {
            char row[240];
            const TocItem *it = &d->book.toc[i];
            snprintf(row, sizeof row, "%c %*s%s    %s", i == d->sel ? '>' : ' ', it->level > 1 ? 2 : 0, "", it->title,
                     it->label);
            clip_copy(fit, (size_t)d->cols, row);
            draw_line(i + 2, 1, fit);
        }
        draw_line(d->rows - 1, 1, "enter goes there    b back");
    } else if (d->mode == M_LIB) {
        const Book *b = d->have ? &d->book : (d->shelf->nsum ? &d->shelf->sum[d->sel] : NULL);
        draw_line(1, 1, "Library");
        if (!b) draw_line(3, 1, "The shelf is empty.");
        else {
            const char *rows[16];
            char store[16][300];
            int n = 0;
            snprintf(store[n], sizeof store[n], "title: %s", b->title); rows[n] = store[n]; n++;
            snprintf(store[n], sizeof store[n], "creator: %s", b->creator); rows[n] = store[n]; n++;
            snprintf(store[n], sizeof store[n], "subject: %s", b->subject); rows[n] = store[n]; n++;
            snprintf(store[n], sizeof store[n], "date: %s", b->date); rows[n] = store[n]; n++;
            snprintf(store[n], sizeof store[n], "publisher: %s", b->publisher); rows[n] = store[n]; n++;
            snprintf(store[n], sizeof store[n], "language: %s", b->language); rows[n] = store[n]; n++;
            snprintf(store[n], sizeof store[n], "work: %s", b->work); rows[n] = store[n]; n++;
            snprintf(store[n], sizeof store[n], "expression: %s", b->expression); rows[n] = store[n]; n++;
            snprintf(store[n], sizeof store[n], "volume: %s", b->volume); rows[n] = store[n]; n++;
            snprintf(store[n], sizeof store[n], "identifier: %s", b->identifier); rows[n] = store[n]; n++;
            snprintf(store[n], sizeof store[n], "source: %s", b->source); rows[n] = store[n]; n++;
            snprintf(store[n], sizeof store[n], "rights: %s", b->rights); rows[n] = store[n]; n++;
            snprintf(store[n], sizeof store[n], "pages: %d    chapters: %d", b->file_pages, b->chapters); rows[n] = store[n]; n++;
            for (i = 0; i < n && i < body - 1; i++) {
                clip_copy(fit, (size_t)d->cols, rows[i]);
                draw_line(i + 2, 1, fit);
            }
        }
        draw_line(d->rows - 1, 1, "b back");
    } else if (d->mode == M_SET) {
        Settings *s = &d->shelf->set;
        char row[8][160];
        const char *pics = s->pictures == PIC_UNREAD ? "unread" : s->pictures == PIC_ALL ? "all" : "empty";
        const char *eng = s->engine == ENG_BUILTIN ? "builtin" : s->engine == ENG_TESS ? "tesseract" : s->engine == ENG_VISION ? "vision" : "best";
        const char *col = s->columns == COL_ONE ? "one" : s->columns == COL_TWO ? "two" : "auto";
        draw_line(1, 1, "Settings");
        snprintf(row[0], sizeof row[0], "pictures %s", pics);
        snprintf(row[1], sizeof row[1], "keep original file %s", s->keep_file ? "on" : "off");
        snprintf(row[2], sizeof row[2], "contrast %s", s->contrast ? "on" : "off");
        snprintf(row[3], sizeof row[3], "deskew %s", s->deskew ? "on" : "off");
        snprintf(row[4], sizeof row[4], "columns %s", col);
        snprintf(row[5], sizeof row[5], "reader %s", eng);
        snprintf(row[6], sizeof row[6], "vision model %s", s->vision ? "allowed" : "off");
        snprintf(row[7], sizeof row[7], "scripts %s", s->scripts);
        for (i = 0; i < 8; i++) {
            char show[180];
            snprintf(show, sizeof show, "%c %s", i == d->set_cur ? '>' : ' ', row[i]);
            draw_line(i + 2, 1, show);
        }
        {
            char show[180];
            snprintf(show, sizeof show, "%c model %s", d->set_cur == 8 ? '>' : ' ', s->vision_model);
            draw_line(10, 1, show);
        }
        draw_line(d->rows - 1, 1, "enter changes    b back");
    } else if (d->mode == M_HITS) {
        draw_line(1, 1, "Find");
        if (d->nhits == 0) draw_line(3, 1, "Not found.");
        for (i = 0; i < body - 1 && i < d->nhits; i++) {
            char row[240];
            snprintf(row, sizeof row, "%c p. %s  %s", i == d->hit ? '>' : ' ', d->hits[i].label, d->hits[i].snippet);
            clip_copy(fit, (size_t)d->cols, row);
            draw_line(i + 2, 1, fit);
        }
        draw_line(d->rows - 1, 1, "enter opens    b back");
    } else if (d->mode == M_ASK) {
        char row[RECTO_PATH + 8];
        draw_line(1, 1, d->status);
        snprintf(row, sizeof row, "> %s", d->ask);
        clip_copy(fit, (size_t)d->cols, row);
        draw_line(3, 1, fit);
        draw_line(d->rows - 1, 1, "enter accepts    esc cancels");
    }
    if (d->mode != M_ASK && d->status[0]) {
        clip_copy(fit, (size_t)d->cols, d->status);
        draw_line(d->rows, 1, fit);
    }
    fflush(stdout);
}

static void scroll_book(Desk *d, int dir) {
    int win = d->rows - 4;
    if (win < 1) win = 1;
    if (!d->have || d->book.nblocks <= 0) return;
    if (dir > 0) {
        if (d->vtop + win < d->nlines) d->vtop++;
        else if (d->cur + 1 < d->book.nblocks) {
            d->cur++;
            d->vtop = 0;
            rebuild_view(d);
        }
    } else if (d->vtop > 0) d->vtop--;
    else if (d->cur > 0) {
        d->cur--;
        rebuild_view(d);
        d->vtop = d->nlines > win ? d->nlines - win : 0;
    }
}

static void page_book(Desk *d, int dir) {
    int page, i;
    if (!d->have || d->book.nblocks <= 0) return;
    page = d->book.blocks[d->cur].page;
    if (dir > 0) {
        for (i = d->cur + 1; i < d->book.nblocks; i++) {
            if (d->book.blocks[i].page != page) {
                d->cur = i;
                break;
            }
        }
    } else {
        int prev = page;
        for (i = d->cur - 1; i >= 0; i--) {
            if (d->book.blocks[i].page != page) {
                prev = d->book.blocks[i].page;
                break;
            }
        }
        if (prev != page) {
            for (i = 0; i < d->book.nblocks; i++) {
                if (d->book.blocks[i].page == prev) {
                    d->cur = i;
                    break;
                }
            }
        }
    }
    d->vtop = 0;
    rebuild_view(d);
}

static void remove_selected(Desk *d) {
    const char *id;
    if (d->sel < 0 || d->sel >= d->shelf->nsum) return;
    id = d->shelf->sum[d->sel].id;
    if (!d->armed) {
        d->armed = 1;
        set_status(d, "press d again to remove this book");
        return;
    }
    d->armed = 0;
    if (d->have && strcmp(d->book.id, id) == 0) {
        book_free(&d->book);
        memset(&d->book, 0, sizeof d->book);
        d->have = 0;
        d->mode = M_SHELF;
    }
    if (shelf_delete(d->shelf, id) != 0) set_status(d, recto_err);
    else set_status(d, "removed");
    if (d->sel >= d->shelf->nsum) d->sel = d->shelf->nsum ? d->shelf->nsum - 1 : 0;
}

#ifndef _WIN32
static void handle(Desk *d, int key) {
    if (d->mode == M_ASK) {
        if (key == 27) {
            d->mode = (d->ask_kind == ASK_SCRIPTS || d->ask_kind == ASK_MODEL) ? M_SET : d->have ? M_BOOK : M_SHELF;
            d->status[0] = 0;
            return;
        }
        if (key == '\n') {
            finish_ask(d);
            return;
        }
        if (key == 8) {
            if (d->ask_n > 0) d->ask[--d->ask_n] = 0;
            return;
        }
        if (key >= 32 && key < 127 && d->ask_n + 1 < (int)sizeof d->ask) {
            d->ask[d->ask_n++] = (char)key;
            d->ask[d->ask_n] = 0;
        }
        return;
    }
    if (key != 'd') d->armed = 0;
    if (key == 'q') {
        save_place(d);
        d->quit = 1;
        return;
    }
    if (d->mode == M_SHELF) {
        if (key == 'j' && d->sel + 1 < d->shelf->nsum) d->sel++;
        else if (key == 'k' && d->sel > 0) d->sel--;
        else if (key == '\n') open_index(d, d->sel);
        else if (key == 'o') start_ask(d, ASK_OPEN, "path to open");
        else if (key == 'l') d->mode = M_LIB;
        else if (key == ',') d->mode = M_SET;
        else if (key == 's') {
            if (shelf_seed_samples(d->shelf) != 0) set_status(d, recto_err);
            else set_status(d, "samples are on the shelf");
        } else if (key == 'd') remove_selected(d);
        return;
    }
    if (d->mode == M_BOOK) {
        if (key == 'j') scroll_book(d, 1);
        else if (key == 'k') scroll_book(d, -1);
        else if (key == 'n' || key == ' ') page_book(d, 1);
        else if (key == 'p') page_book(d, -1);
        else if (key == 'g') start_ask(d, ASK_GO, "page number, arabic or roman");
        else if (key == '/') start_ask(d, ASK_FIND, "find");
        else if (key == 'c' || key == '\t') {
            d->mode = M_TOC;
            d->sel = 0;
        } else if (key == 'e') start_ask(d, ASK_EPUB, "epub path");
        else if (key == 'r') reread_page(d, 0, 0, 0, 0, 0);
        else if (key == 'x') start_ask(d, ASK_RECT, "crop x y w h");
        else if (key == 'b') {
            save_place(d);
            d->mode = M_SHELF;
        } else if (key == 'l') d->mode = M_LIB;
        else if (key == ',') d->mode = M_SET;
        return;
    }
    if (d->mode == M_TOC) {
        if (key == 'j' && d->sel + 1 < d->book.ntoc) d->sel++;
        else if (key == 'k' && d->sel > 0) d->sel--;
        else if (key == '\n' && d->book.ntoc > 0) {
            d->cur = d->book.toc[d->sel].block;
            d->vtop = 0;
            d->mode = M_BOOK;
            rebuild_view(d);
        } else if (key == 'b' || key == 27) d->mode = M_BOOK;
        return;
    }
    if (d->mode == M_HITS) {
        if (key == 'j' && d->hit + 1 < d->nhits) d->hit++;
        else if (key == 'k' && d->hit > 0) d->hit--;
        else if (key == '\n' && d->nhits > 0) {
            d->cur = d->hits[d->hit].block;
            d->vtop = 0;
            d->mode = M_BOOK;
            rebuild_view(d);
        } else if (key == 'b' || key == 27) d->mode = M_BOOK;
        return;
    }
    if (d->mode == M_SET) {
        if (key == 'j' && d->set_cur < 8) d->set_cur++;
        else if (key == 'k' && d->set_cur > 0) d->set_cur--;
        else if (key == '\n') cycle_setting(d);
        else if (key == 'b' || key == 27) d->mode = d->have ? M_BOOK : M_SHELF;
        return;
    }
    if (d->mode == M_LIB) {
        if (key == 'b' || key == 27 || key == '\n') d->mode = d->have ? M_BOOK : M_SHELF;
    }
}

int desk_run(Shelf *shelf) {
    Desk d;
    if (!shelf) return -1;
    memset(&d, 0, sizeof d);
    d.shelf = shelf;
    measure(&d);
    if (term_begin() != 0) return -1;
    while (!d.quit) {
        int key;
        draw(&d);
        key = read_key();
        if (key < 0) break;
        handle(&d, key);
    }
    save_place(&d);
    free_lines(&d);
    free(d.hits);
    book_free(&d.book);
    term_end();
    return 0;
}
#else
int desk_run(Shelf *shelf) {
    char line[RECTO_PATH];
    if (!shelf) return -1;
    if (!_isatty(_fileno(stdout))) {
        recto_seterr("the desk needs a terminal; use recto help");
        return -1;
    }
    for (;;) {
        int i;
        printf("\nRecto: convert to EPUB (%d)\n", shelf->nsum);
        for (i = 0; i < shelf->nsum; i++)
            printf("  %d  %s\n", i + 1, shelf->sum[i].title[0] ? shelf->sum[i].title : shelf->sum[i].id);
        printf("open N, o PATH, del ID, sample, set key=value, q\n> ");
        if (!fgets(line, sizeof line, stdin)) return 0;
        str_trim(line);
        if (line[0] == 'q') return 0;
        if (str_istart(line, "open ")) {
            int n = atoi(line + 5);
            Book b;
            if (n < 1 || n > shelf->nsum) {
                printf("%s\n", "that number is not on the shelf");
                continue;
            }
            memset(&b, 0, sizeof b);
            if (shelf_load(shelf, shelf->sum[n - 1].id, &b) != 0) {
                printf("%s\n", recto_err);
                continue;
            }
            printf("%s\n", b.title);
            for (i = 0; i < b.nblocks && i < 40; i++) printf("[%s] %s\n", b.blocks[i].label, b.blocks[i].text);
            book_free(&b);
        } else if (str_istart(line, "o ")) {
            const char *paths[1];
            Book *books = NULL;
            int nb = 0;
            paths[0] = str_trim(line + 2);
            if (import_paths(paths, 1, &shelf->set, &books, &nb) != 0) printf("%s\n", recto_err);
            else {
                for (i = 0; i < nb; i++) {
                    shelf_add(shelf, &books[i], books[i].source);
                    book_free(&books[i]);
                }
                free(books);
            }
        } else if (str_istart(line, "del ")) {
            if (shelf_delete(shelf, str_trim(line + 4)) != 0) printf("%s\n", recto_err);
        } else if (str_ieq(line, "sample")) {
            shelf_seed_samples(shelf);
        } else if (str_istart(line, "set ")) {
            if (settings_apply(&shelf->set, str_trim(line + 4)) != 0) printf("%s\n", recto_err);
            else shelf_save_settings(shelf);
        }
    }
}
#endif
