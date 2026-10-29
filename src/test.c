#include "recto.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef _WIN32
#include <unistd.h>
#endif

#ifndef RECTO_NO_ZLIB
#include <zlib.h>
#endif

static int checks;
static int fails;

static void ok(int cond, const char *msg) {
    checks++;
    if (!cond) {
        fprintf(stderr, "fail: %s\n", msg);
        if (recto_err[0]) fprintf(stderr, "  %s\n", recto_err);
        fails++;
    }
}

static void ok_has(const char *hay, const char *needle, const char *msg) {
    checks++;
    if (!hay || !needle || !strstr(hay, needle)) {
        fprintf(stderr, "fail: %s\n  want [%s]\n  got [%s]\n", msg, needle ? needle : "", hay ? hay : "(null)");
        if (recto_err[0]) fprintf(stderr, "  %s\n", recto_err);
        fails++;
    }
}

static void add_word(Word **w, int *n, int *cap, const char *text, int page, double x, double y, double ww, double h,
                     int positioned) {
    Word *word;
    if (*n >= *cap) {
        *cap = *cap ? *cap * 2 : 8;
        *w = xrealloc(*w, (size_t)(*cap) * sizeof **w);
    }
    word = &(*w)[(*n)++];
    memset(word, 0, sizeof *word);
    word->text = xstrdup(text);
    word->page = page;
    word->x = x;
    word->y = y;
    word->w = ww;
    word->h = h;
    word->size = h;
    word->conf = 100;
    word->positioned = positioned;
}

static int mem_has(const char *hay, size_t n, const char *needle) {
    size_t m, i;
    if (!hay || !needle) return 0;
    m = strlen(needle);
    if (!m || n < m) return 0;
    for (i = 0; i + m <= n; i++)
        if (memcmp(hay + i, needle, m) == 0) return 1;
    return 0;
}

static int block_has(const Book *b, const char *s) {
    int i;
    for (i = 0; i < b->nblocks; i++)
        if (b->blocks[i].text && strstr(b->blocks[i].text, s)) return 1;
    return 0;
}

static void sweep(const char *path) {
    if (!path || !file_exists(path)) return;
    if (file_is_dir(path)) {
        char **names = NULL;
        int n = 0, i;
        if (dir_list(path, &names, &n) == 0) {
            for (i = 0; i < n; i++) {
                char *child = path_join(path, names[i]);
                sweep(child);
                free(child);
            }
            dir_list_free(names, n);
        }
#ifndef _WIN32
        rmdir(path);
#else
        _rmdir(path);
#endif
    } else remove(path);
}

static int write_sample_pdf(const char *path) {
    Buf b;
    int off[12];
    int xref_at, i;
    const char *plain = "BT /F1 12 Tf 72 720 Td (Hello from Recto) Tj ET\n";
    const char *third = "BT /F1 12 Tf 72 680 Td (Label check) Tj ET\n";
    const char *flate_src = "BT /F1 12 Tf 72 700 Td (Flate page text) Tj ET\n";
    unsigned char zbuf[256];
    size_t zlen = 0;
    buf_init(&b);
    memset(off, 0, sizeof off);
#ifndef RECTO_NO_ZLIB
    {
        uLongf dest = sizeof zbuf;
        if (compress(zbuf, &dest, (const Bytef *)flate_src, (uLong)strlen(flate_src)) != Z_OK) {
            buf_free(&b);
            recto_seterr("could not compress the test page");
            return -1;
        }
        zlen = (size_t)dest;
    }
#else
    (void)flate_src;
    (void)zbuf;
#endif
    buf_puts(&b, "%PDF-1.4\n");
    off[1] = (int)b.n;
    buf_puts(&b, "1 0 obj\n<< /Type /Catalog /Pages 2 0 R /PageLabels << /Nums [ 0 << /S /r >> 2 << /S /D >> ] >> >>\nendobj\n");
    off[2] = (int)b.n;
    buf_puts(&b, "2 0 obj\n<< /Type /Pages /Count 3 /Kids [3 0 R 6 0 R 8 0 R] /MediaBox [0 0 612 792] "
                 "/Resources << /Font << /F1 5 0 R >> >> >>\nendobj\n");
    off[3] = (int)b.n;
    buf_puts(&b, "3 0 obj\n<< /Type /Page /Parent 2 0 R /Contents 4 0 R >>\nendobj\n");
    off[4] = (int)b.n;
    buf_printf(&b, "4 0 obj\n<< /Length %lu >>\nstream\n", (unsigned long)strlen(plain));
    buf_puts(&b, plain);
    buf_puts(&b, "endstream\nendobj\n");
    off[5] = (int)b.n;
    buf_puts(&b, "5 0 obj\n<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>\nendobj\n");
    off[6] = (int)b.n;
    buf_puts(&b, "6 0 obj\n<< /Type /Page /Parent 2 0 R /Contents 7 0 R >>\nendobj\n");
    off[7] = (int)b.n;
#ifndef RECTO_NO_ZLIB
    buf_printf(&b, "7 0 obj\n<< /Length %lu /Filter /FlateDecode >>\nstream\n", (unsigned long)zlen);
    buf_append(&b, zbuf, zlen);
    buf_puts(&b, "\nendstream\nendobj\n");
#else
    buf_printf(&b, "7 0 obj\n<< /Length %lu >>\nstream\n", (unsigned long)strlen(flate_src));
    buf_puts(&b, flate_src);
    buf_puts(&b, "endstream\nendobj\n");
#endif
    off[8] = (int)b.n;
    buf_puts(&b, "8 0 obj\n<< /Type /Page /Parent 2 0 R /Contents 9 0 R >>\nendobj\n");
    off[9] = (int)b.n;
    buf_printf(&b, "9 0 obj\n<< /Length %lu >>\nstream\n", (unsigned long)strlen(third));
    buf_puts(&b, third);
    buf_puts(&b, "endstream\nendobj\n");
    off[10] = (int)b.n;
    buf_puts(&b, "10 0 obj\n<< /Title (The Republic) /Author (Plato) >>\nendobj\n");
    xref_at = (int)b.n;
    buf_puts(&b, "xref\n0 11\n");
    buf_puts(&b, "0000000000 65535 f \n");
    for (i = 1; i <= 10; i++) buf_printf(&b, "%010d 00000 n \n", off[i]);
    buf_printf(&b, "trailer\n<< /Size 11 /Root 1 0 R /Info 10 0 R >>\nstartxref\n%d\n", xref_at);
    buf_puts(&b, "%%EOF\n");
    {
        int rc = file_write(path, b.p, b.n);
        buf_free(&b);
        return rc;
    }
}

static int word_is(const Word *w, int n, int page, const char *text, const char *label) {
    int i;
    for (i = 0; i < n; i++) {
        if (w[i].page == page && w[i].text && strcmp(w[i].text, text) == 0) {
            if (!label || strcmp(w[i].label, label) == 0) return 1;
        }
    }
    return 0;
}

int recto_selftest(void) {
    char roman[16];
    char shelf_dir[RECTO_PATH];
    char pdf_path[RECTO_PATH];
    char text_path[RECTO_PATH];
    char epub_path[RECTO_PATH];
    char png_path[RECTO_PATH];
    Settings set;
    settings_default(&set);
    checks = 0;
    fails = 0;
    recto_err[0] = 0;
    ok(RECTO_VERSION[0] == '0' && strstr(RECTO_VERSION, "-alpha.") != NULL, "early alpha version");

    ok(roman_parse("xiv") == 14, "xiv is 14");
    ok(roman_parse("IV") == 4, "IV is 4");
    ok(roman_parse("hello") == 0, "hello is not roman");
    roman_format(14, 0, roman, sizeof roman);
    ok(strcmp(roman, "xiv") == 0, "14 formats as xiv");
    roman_format(4, 1, roman, sizeof roman);
    ok(strcmp(roman, "IV") == 0, "4 formats as IV");
    ok(natcmp("p2", "p10") < 0, "natural order puts p2 before p10");

    {
        Image im;
        OcrPage page;
        memset(&im, 0, sizeof im);
        memset(&page, 0, sizeof page);
        ok(font_render("HELLO WORLD", 4, &im) == 0, "render a line");
        ok(ocr_builtin(&im, &page) == 0, "read the rendered line");
        ok_has(page.text, "HELLO WORLD", "built-in reader returns its own letters");
        ocr_page_free(&page);
        image_free(&im);
    }

    {
        Word *w = NULL;
        int n = 0, cap = 0;
        Book b;
        book_init(&b);
        add_word(&w, &n, &cap, "A broken sen-", 1, 0, 0, 0, 12, 0);
        add_word(&w, &n, &cap, "tence continues here.", 1, 0, 20, 0, 12, 0);
        ok(compose_book(w, n, 1, &set, &b) == 0, "compose a hyphenated line");
        ok(b.hyphens == 1, "one hyphen was rejoined");
        ok_has(b.nblocks ? b.blocks[0].text : "", "A broken sentence continues here.", "hyphen join");
        words_free(w, n);
        book_free(&b);
    }

    {
        Word *w = NULL;
        int n = 0, cap = 0, i;
        Book b;
        book_init(&b);
        for (i = 1; i <= 3; i++) {
            const char *body = i == 1 ? "Alpha remains after the head is cut." : i == 2 ? "Beta remains after the head is cut." : "Gamma remains after the head is cut.";
            add_word(&w, &n, &cap, "Running Head", i, 0, 0, 0, 12, 0);
            add_word(&w, &n, &cap, body, i, 0, 20, 0, 12, 0);
        }
        ok(compose_book(w, n, 3, &set, &b) == 0, "compose running heads");
        ok(b.headers >= 2, "running heads were removed");
        ok(!block_has(&b, "Running Head"), "the running head is gone");
        ok(block_has(&b, "Beta remains"), "the body line stays");
        words_free(w, n);
        book_free(&b);
    }

    {
        Word *w = NULL;
        int n = 0, cap = 0, i;
        Book b;
        const char *L[4] = {"L1", "L2", "L3", "L4"};
        const char *R[4] = {"R1", "R2", "R3", "R4"};
        book_init(&b);
        for (i = 0; i < 4; i++) {
            add_word(&w, &n, &cap, L[i], 1, 30, 10 + i * 40, 40, 12, 1);
            add_word(&w, &n, &cap, R[i], 1, 500, 30 + i * 40, 40, 12, 1);
        }
        ok(compose_book(w, n, 1, &set, &b) == 0, "compose two columns");
        ok(b.columns >= 1, "a gutter was found");
        ok(b.nblocks >= 8, "column lines stayed apart");
        ok(b.nblocks >= 1 && strcmp(b.blocks[0].text, "L1") == 0, "the left column is read first");
        ok(b.nblocks >= 5 && strcmp(b.blocks[4].text, "R1") == 0, "the right column follows the left");
        words_free(w, n);
        book_free(&b);
    }

    {
        Word *w = NULL;
        int n = 0, cap = 0;
        Book b;
        const char *a = "This line is the first copy of a duplicated page.";
        const char *c = "This line is the second copy of a duplicated page.";
        book_init(&b);
        add_word(&w, &n, &cap, a, 1, 0, 0, 0, 12, 0);
        add_word(&w, &n, &cap, c, 1, 0, 20, 0, 12, 0);
        add_word(&w, &n, &cap, a, 1, 0, 40, 0, 12, 0);
        add_word(&w, &n, &cap, c, 1, 0, 60, 0, 12, 0);
        ok(compose_book(w, n, 1, &set, &b) == 0, "compose a duplicated page");
        ok(b.duplicates == 1, "the repeated half was dropped");
        ok(b.nblocks == 2, "two lines remain from the duplicated page");
        words_free(w, n);
        book_free(&b);
    }

    {
        Word *w = NULL;
        int n = 0, cap = 0, row;
        Book b;
        const char *top[6] = {"Name", "A", "B", "C", "City", "Year"};
        const char *bot[6] = {"Ada", "One", "Two", "Three", "Rome", "Year"};
        double xs[6] = {10, 20, 30, 40, 160, 280};
        book_init(&b);
        for (row = 0; row < 6; row++) add_word(&w, &n, &cap, top[row], 1, xs[row], 10, 8, 12, 1);
        for (row = 0; row < 6; row++) add_word(&w, &n, &cap, bot[row], 1, xs[row], 50, 8, 12, 1);
        ok(compose_book(w, n, 1, &set, &b) == 0, "compose a table");
        ok(b.tables >= 1, "aligned rows became a table");
        ok(block_has(&b, " | "), "table cells are divided");
        words_free(w, n);
        book_free(&b);
    }

    {
        Book b, back;
        char path[RECTO_PATH];
        book_init(&b);
        snprintf(b.title, sizeof b.title, "Round Trip");
        snprintf(b.creator, sizeof b.creator, "Recto");
        book_add_block(&b, BLK_H1, 1, "i", "Chapter", 0, 0, 0, 0, 100);
        book_add_block(&b, BLK_P, 1, "i", "Hello\nthere", 0, 0, 0, 0, 100);
        book_rebuild_toc(&b);
        temp_path(path, sizeof path, ".recto");
        ok(book_write_file(&b, path) == 0, "write an edition");
        memset(&back, 0, sizeof back);
        ok(book_read_file(path, &back) == 0, "read an edition");
        ok(strcmp(back.title, "Round Trip") == 0, "title survived");
        ok(back.nblocks == 2 && back.blocks[1].text && strcmp(back.blocks[1].text, "Hello\nthere") == 0, "body survived");
        ok(back.chapters == 1, "the chapter was counted");
        remove(path);
        book_free(&b);
        book_free(&back);
    }

    {
        Image im, back;
        Buf png;
        memset(&im, 0, sizeof im);
        memset(&back, 0, sizeof back);
        buf_init(&png);
        ok(image_alloc(&im, 12, 10, 255) == 0, "allocate an image");
        im.px[1 * 12 + 2] = 0;
        ok(image_write_png(&im, &png) == 0, "write a png");
        temp_path(png_path, sizeof png_path, ".png");
        ok(file_write(png_path, png.p, png.n) == 0, "store the png");
        ok(image_load(png_path, &back) == 0, "read the png");
        ok(back.w == 12 && back.h == 10 && back.px && back.px[1 * 12 + 2] == 0, "png pixel survived");
        remove(png_path);
        buf_free(&png);
        image_free(&im);
        image_free(&back);
    }

    {
        Image im;
        Buf json;
        memset(&im, 0, sizeof im);
        buf_init(&json);
        image_alloc(&im, 8, 8, 255);
        ok(vision_build_request(&im, "transcribe", "grok-4.7", &json) == 0, "build a vision request");
        ok_has(json.p, "input_image", "vision request carries an image");
        ok_has(json.p, "grok-4.7", "vision request names the model");
        ok_has(json.p, "\"store\":false", "vision request is not stored");
        buf_free(&json);
        image_free(&im);
    }

    temp_path(pdf_path, sizeof pdf_path, ".pdf");
    ok(write_sample_pdf(pdf_path) == 0, "write a sample pdf");
    {
        Word *words = NULL;
        int nwords = 0, npages = 0;
        char **labels = NULL;
        Book meta;
        int i;
        memset(&meta, 0, sizeof meta);
        ok(pdf_extract_native(pdf_path, &words, &nwords, &npages, &labels, &meta) == 0, "read the sample pdf");
        ok(npages == 3, "the sample pdf has three pages");
        ok(word_is(words, nwords, 1, "Hello", "i"), "page one is labelled i");
        ok(word_is(words, nwords, 2, "Flate", "ii"), "the flate page was read");
        ok(word_is(words, nwords, 3, "Label", "1"), "page three is labelled 1");
        ok(strcmp(meta.title, "The Republic") == 0, "pdf title");
        ok(strcmp(meta.creator, "Plato") == 0, "pdf author");
        words_free(words, nwords);
        if (labels) {
            for (i = 0; i <= npages; i++) free(labels[i]);
            free(labels);
        }
        book_free(&meta);
    }

    temp_path(shelf_dir, sizeof shelf_dir, "-shelf");
    sweep(shelf_dir);
    {
        Shelf shelf;
        Book b, loaded;
        Hit *hits = NULL;
        int nh = 0;
        const char *paths[1];
        Book *opened = NULL;
        int nopened = 0;
        ok(shelf_open(&shelf, shelf_dir) == 0, "open a fresh shelf");
        ok(shelf.nsum == 5, "a new shelf receives the five samples");
        memset(&b, 0, sizeof b);
        ok(shelf_load(&shelf, "sample-dhammapada", &b) == 0, "load the Dhammapada sample");
        ok(b.chapters == 2, "the sample has two chapters");
        ok(book_find(&b, "Hatred", &hits, &nh) == 0 && nh > 0, "find is case-insensitive");
        ok(book_label_block(&b, "iv") >= 0, "a roman page number reaches printed page 4");
        free(hits);
        book_free(&b);

        memset(&b, 0, sizeof b);
        book_init(&b);
        snprintf(b.title, sizeof b.title, "Notes");
        book_add_block(&b, BLK_P, 1, "1", "original passage", 0, 0, 0, 0, 100);
        ok(shelf_add(&shelf, &b, NULL) == 0, "add a book");
        memset(&loaded, 0, sizeof loaded);
        ok(shelf_load(&shelf, b.id, &loaded) == 0, "load the added book");
        ok_has(loaded.nblocks ? loaded.blocks[0].text : "", "original passage", "saved text");
        ok(shelf_revise(&shelf, &loaded, 0, "revised passage") == 0, "revise a passage");
        ok_has(loaded.blocks[0].text, "revised passage", "revision is in memory");
        ok(shelf_restore(&shelf, &loaded) == 0, "restore the previous revision");
        ok_has(loaded.nblocks ? loaded.blocks[0].text : "", "original passage", "restore brings the old text back");
        book_free(&loaded);
        book_free(&b);

        ok(settings_apply(&shelf.set, "pictures=all") == 0, "apply a setting");
        ok(shelf_save_settings(&shelf) == 0, "save settings");
        shelf_close(&shelf);
        ok(shelf_open(&shelf, shelf_dir) == 0, "reopen the shelf");
        ok(shelf.set.pictures == PIC_ALL, "pictures=all was kept");
        ok(shelf.nsum >= 5, "reopening does not forget the books");

        temp_path(text_path, sizeof text_path, ".txt");
        file_write(text_path, "An imported paragraph about the desk.\n", 38);
#ifndef _WIN32
        {
            Buf g;
            int before = shelf.nsum;
            buf_init(&g);
            ok(gui_call(&shelf, "shelf", "", "", "", &g) == 0, "window shelf");
            ok_has(g.p, "sample-dhammapada", "window shelf lists the Dhammapada");
            ok(gui_call(&shelf, "book", "sample-dhammapada", "", "", &g) == 0, "window book");
            ok_has(g.p, "hatred", "window book has the verse");
            ok_has(g.p, "\"k\":\"h1\"", "window book keeps a chapter heading");
            ok(gui_call(&shelf, "goto", "sample-dhammapada", "iv", "", &g) == 0, "window goes to a roman page");
            ok_has(g.p, "\"page\":4", "roman iv is printed page 4");
            ok(gui_call(&shelf, "find", "sample-dhammapada", "hatred", "", &g) == 0, "window find");
            ok_has(g.p, "hatred", "a hit comes back to the window");
            ok(gui_call(&shelf, "find", "sample-cannon", "hatred", "", &g) == 0, "window find in another book");
            ok_has(g.p, "\"hits\":[]", "window find can miss");
            ok(gui_call(&shelf, "settings", "", "", "", &g) == 0, "window settings");
            ok_has(g.p, "pictures=all", "window settings show pictures=all");
            ok(gui_call(&shelf, "set", "columns=one", "", "", &g) == 0, "window applies a setting");
            ok(shelf.set.columns == COL_ONE, "columns=one reached the shelf");
            ok(gui_call(&shelf, "set", "columns=auto", "", "", &g) == 0, "window restores columns");
            ok(shelf.set.columns == COL_AUTO, "columns=auto was restored");
            ok(gui_call(&shelf, "remove", "sample-dhammapada", "", "", &g) != 0, "window remove asks for confirmation");
            ok_has(g.p, "--yes", "window remove names the confirmation");
            ok(shelf.nsum == before, "a refused remove leaves the shelf");
            ok(gui_call(&shelf, "open", text_path, "", "", &g) == 0, "window open");
            ok(shelf.nsum == before + 1, "window open adds a book");
            temp_path(epub_path, sizeof epub_path, "-gui.epub");
            ok(gui_call(&shelf, "convert", text_path, epub_path, "", &g) == 0, "window convert");
            ok_has(g.p, epub_path, "window convert names the epub");
            ok(shelf.nsum == before + 2, "window convert keeps the edition");
            remove(epub_path);
            {
                char ba[RECTO_PATH], bb[RECTO_PATH], batch[RECTO_PATH], joined[RECTO_PATH * 2 + 4];
                char **names = NULL;
                int nn = 0, bi, saw_a = 0, saw_b = 0;
                temp_path(ba, sizeof ba, "-a.txt");
                temp_path(bb, sizeof bb, "-b.txt");
                temp_path(batch, sizeof batch, "-batch");
                file_write(ba, "Alpha batch page.\n", 18);
                file_write(bb, "Beta batch page.\n", 17);
                snprintf(joined, sizeof joined, "%s\n%s", ba, bb);
                ok(gui_call(&shelf, "convert", joined, epub_path, "", &g) != 0, "a batch needs a folder for one epub name");
                ok(shelf.nsum == before + 2, "a refused batch does not add books");
                ok(gui_call(&shelf, "convert", joined, batch, "", &g) == 0, "window converts a batch of files");
                ok(shelf.nsum == before + 4, "a batch keeps both editions");
                ok_has(g.p, batch, "the batch names the folder");
                if (dir_list(batch, &names, &nn) == 0) {
                    for (bi = 0; bi < nn; bi++) {
                        char *child = path_join(batch, names[bi]);
                        Buf z;
                        buf_init(&z);
                        if (file_read(child, &z) == 0) {
                            if (mem_has(z.p, z.n, "Alpha batch page")) saw_a = 1;
                            if (mem_has(z.p, z.n, "Beta batch page")) saw_b = 1;
                        }
                        remove(child);
                        buf_free(&z);
                        free(child);
                    }
                    dir_list_free(names, nn);
                }
                ok(saw_a && saw_b, "both files in the batch were converted");
#ifndef _WIN32
                rmdir(batch);
#else
                _rmdir(batch);
#endif
                remove(ba);
                remove(bb);
            }
            buf_free(&g);
        }
#endif
        paths[0] = text_path;
        ok(import_paths(paths, 1, &shelf.set, &opened, &nopened) == 0, "import a text file");
        ok(nopened == 1 && block_has(&opened[0], "imported paragraph"), "the paragraph was kept");
        if (nopened == 1) {
            book_free(&opened[0]);
            free(opened);
        }
        paths[0] = pdf_path;
        opened = NULL;
        nopened = 0;
        ok(import_paths(paths, 1, &set, &opened, &nopened) == 0, "import the sample pdf");
        ok(nopened == 1 && strcmp(opened[0].title, "The Republic") == 0, "import keeps the pdf title");
        ok(nopened == 1 && block_has(&opened[0], "Hello from Recto"), "import keeps the pdf text");
        ok(nopened == 1 && block_has(&opened[0], "Flate page text"), "import keeps the flate page");
        if (nopened == 1) {
            temp_path(epub_path, sizeof epub_path, ".epub");
            ok(epub_write(&opened[0], epub_path) == 0, "write an epub");
            {
                Buf zip;
                buf_init(&zip);
                ok(file_read(epub_path, &zip) == 0, "read the epub");
                ok(zip.n > 58 && memcmp(zip.p + 30, "mimetype", 8) == 0, "mimetype is the first zip entry");
                ok(zip.n > 58 && memcmp(zip.p + 38, "application/epub+zip", 20) == 0, "mimetype is stored uncompressed");
                buf_free(&zip);
            }
            remove(epub_path);
            book_free(&opened[0]);
            free(opened);
        }
        {
            char **outs = NULL;
            char **ids = NULL;
            int nout = 0, kept;
            const char *in[2];
            char html_path[RECTO_PATH];
            char md_path[RECTO_PATH];
            char folder[RECTO_PATH];
            char one_epub[RECTO_PATH];
            char sib[RECTO_PATH];
            Buf zip;
            const char *dot;
            size_t sn;
            const char *html =
                "<html><h1>Chapter I. The Gate</h1><p>The gate was open.</p><script>nope()</script></html>";
            const char *md = "# Title\n\nThe note remains.\n";
            kept = shelf.nsum;
            temp_path(epub_path, sizeof epub_path, ".epub");
            in[0] = pdf_path;
            ok(convert_files(in, 1, epub_path, &set, NULL, &outs, &ids, &nout) == 0, "convert a pdf to epub");
            ok(nout == 1 && outs && outs[0] && strcmp(outs[0], epub_path) == 0, "the pdf epub uses the given path");
            ok(shelf.nsum == kept, "a convert without the shelf does not add a book");
            buf_init(&zip);
            ok(nout == 1 && file_read(epub_path, &zip) == 0, "read the converted pdf epub");
            ok(zip.p && zip.n > 58 && memcmp(zip.p + 30, "mimetype", 8) == 0, "converted epub stores mimetype first");
            ok(mem_has(zip.p, zip.n, "Hello from Recto"), "converted epub keeps the pdf text");
            buf_free(&zip);
            remove(epub_path);
            convert_free(outs, ids, nout);
            outs = NULL;
            ids = NULL;
            nout = 0;

            temp_path(html_path, sizeof html_path, ".html");
            temp_path(epub_path, sizeof epub_path, "-html.epub");
            file_write(html_path, html, strlen(html));
            in[0] = html_path;
            ok(convert_files(in, 1, epub_path, &set, NULL, &outs, &ids, &nout) == 0, "convert html to epub");
            buf_init(&zip);
            ok(file_read(epub_path, &zip) == 0, "read the html epub");
            ok(mem_has(zip.p, zip.n, "gate was open"), "html prose is in the epub");
            ok(!mem_has(zip.p, zip.n, "nope"), "html script is left out of the epub");
            ok(!mem_has(zip.p, zip.n, "&lt;h1&gt;") && !mem_has(zip.p, zip.n, "<h1>Chapter"),
               "html tags are not copied into the epub");
            buf_free(&zip);
            remove(epub_path);
            remove(html_path);
            convert_free(outs, ids, nout);
            outs = NULL;
            ids = NULL;
            nout = 0;

            temp_path(md_path, sizeof md_path, ".md");
            temp_path(epub_path, sizeof epub_path, "-md.epub");
            file_write(md_path, md, strlen(md));
            in[0] = md_path;
            ok(convert_files(in, 1, epub_path, &set, NULL, &outs, &ids, &nout) == 0, "convert markdown to epub");
            buf_init(&zip);
            ok(file_read(epub_path, &zip) == 0, "read the markdown epub");
            ok(mem_has(zip.p, zip.n, "The note remains"), "markdown prose is in the epub");
            ok(!mem_has(zip.p, zip.n, "# Title"), "a markdown heading mark is not copied");
            buf_free(&zip);
            remove(epub_path);
            remove(md_path);
            convert_free(outs, ids, nout);
            outs = NULL;
            ids = NULL;
            nout = 0;

            in[0] = text_path;
            ok(convert_files(in, 1, NULL, &set, NULL, &outs, &ids, &nout) == 0, "convert a text file beside itself");
            dot = strrchr(text_path, '.');
            sn = dot ? (size_t)(dot - text_path) : strlen(text_path);
            if (sn + 6 < sizeof sib) {
                memcpy(sib, text_path, sn);
                memcpy(sib + sn, ".epub", 6);
            } else sib[0] = 0;
            ok(nout == 1 && outs && outs[0] && strcmp(outs[0], sib) == 0, "the sibling epub replaces the extension");
            if (nout == 1 && outs && outs[0]) remove(outs[0]);
            convert_free(outs, ids, nout);
            outs = NULL;
            ids = NULL;
            nout = 0;

            temp_path(one_epub, sizeof one_epub, "-one.epub");
            in[0] = pdf_path;
            in[1] = text_path;
            ok(convert_files(in, 2, one_epub, &set, NULL, &outs, &ids, &nout) != 0, "one epub file is one book");
            ok_has(recto_err, "one epub file is one book", "several books need a folder");
            convert_free(outs, ids, nout);
            outs = NULL;
            ids = NULL;
            nout = 0;

            temp_path(folder, sizeof folder, "-epubs");
            ok(convert_files(in, 2, folder, &set, NULL, &outs, &ids, &nout) == 0, "a folder receives one epub per book");
            ok(nout == 2 && outs && outs[0] && outs[1] && file_exists(outs[0]) && file_exists(outs[1]),
               "two books become two epub files");
            if (outs) {
                if (nout > 0) remove(outs[0]);
                if (nout > 1) remove(outs[1]);
            }
            convert_free(outs, ids, nout);
#ifndef _WIN32
            rmdir(folder);
#else
            _rmdir(folder);
#endif
        }
        remove(text_path);
#ifndef _WIN32
        {
            int saved = dup(0);
            int sp[2];
            if (saved >= 0 && pipe(sp) == 0) {
                dup2(sp[0], 0);
                ok(desk_run(&shelf) != 0, "the desk stays closed when stdin is not a terminal");
                dup2(saved, 0);
                close(saved);
                close(sp[0]);
                close(sp[1]);
            }
        }
#endif
        shelf_close(&shelf);
    }

    remove(pdf_path);
    sweep(shelf_dir);
#ifndef _WIN32
    ok(gui_http_probe() == 0, "the browser desk page is served");
#endif
    if (fails) {
        fprintf(stderr, "recto: %d of %d checks failed\n", fails, checks);
        return -1;
    }
    printf("recto: %d checks passed\n", checks);
    return 0;
}
