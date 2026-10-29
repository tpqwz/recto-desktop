#include "recto.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void help(void) {
    printf("Recto %s converts PDF, DjVu, HTML, plain text, Markdown, and page images to EPUB.\n"
           "Printed page numbers and the contents are kept.\n"
           "\n"
           "  recto convert INPUT... OUTPUT.epub|FOLDER\n"
           "  recto INPUT [OUTPUT.epub]\n"
           "  recto gui                     check the edition in a browser window\n"
           "  recto desk                    check the edition in the terminal\n"
           "  recto open FILE|FOLDER...     add books to the shelf\n"
           "  recto books                   list the shelf\n"
           "  recto show ID                 print a book\n"
           "  recto page ID LABEL           print one printed page\n"
           "  recto find ID WORDS           find in a book\n"
           "  recto export ID FILE.epub     write the edition again\n"
           "  recto ocr FILE [--page N] [--rect x,y,w,h] [--engine NAME]\n"
           "  recto remove ID --yes         remove a book\n"
           "  recto sample                  put the sample editions back\n"
           "  recto settings [key=value]    show or change settings\n"
           "  recto library                 show the catalogue\n"
           "  recto version                 print the version\n"
           "  recto test                    run the self-test\n"
           "\n"
           "One .epub file is one book. A folder receives one EPUB per book.\n"
           "With no output path, the EPUB is written beside the source.\n"
           "A folder of pictures is one book. Several PDF or DjVu files are several books.\n"
           "The shelf is $RECTO_SHELF, or ~/.recto.\n",
           RECTO_VERSION);
}

static int known_command(const char *s) {
    return strcmp(s, "help") == 0 || strcmp(s, "--help") == 0 || strcmp(s, "-h") == 0 || strcmp(s, "version") == 0 ||
           strcmp(s, "--version") == 0 || strcmp(s, "-V") == 0 || strcmp(s, "test") == 0 ||
           strcmp(s, "gui") == 0 || strcmp(s, "desk") == 0 || strcmp(s, "convert") == 0 || strcmp(s, "open") == 0 ||
           strcmp(s, "books") == 0 || strcmp(s, "show") == 0 || strcmp(s, "page") == 0 || strcmp(s, "find") == 0 ||
           strcmp(s, "export") == 0 || strcmp(s, "ocr") == 0 || strcmp(s, "remove") == 0 || strcmp(s, "sample") == 0 ||
           strcmp(s, "settings") == 0 || strcmp(s, "library") == 0;
}

static int ends_with_epub_name(const char *s) {
    size_t n;
    if (!s) return 0;
    n = strlen(s);
    return n >= 5 && str_ieq(s + n - 5, ".epub");
}

static int ends_with_sep(const char *s) {
    size_t n;
    if (!s || !s[0]) return 0;
    n = strlen(s);
    return s[n - 1] == '/' || s[n - 1] == '\\';
}

static int run_convert(Shelf *s, const char **inputs, int ninputs, const char *dest) {
    char **outs = NULL;
    char **ids = NULL;
    int n = 0, i;
    if (ninputs < 1) {
        fprintf(stderr, "recto: convert needs an .epub file or a folder\n");
        return 1;
    }
    if (convert_files(inputs, ninputs, dest, &s->set, NULL, &outs, &ids, &n) != 0) {
        fprintf(stderr, "recto: %s\n", recto_err);
        convert_free(outs, ids, n);
        return 1;
    }
    for (i = 0; i < n; i++) printf("%s\n", outs[i] ? outs[i] : "");
    convert_free(outs, ids, n);
    return 0;
}

static int need_shelf(Shelf *s) {
    if (shelf_open(s, NULL) != 0) {
        fprintf(stderr, "recto: %s\n", recto_err);
        return -1;
    }
    return 0;
}

static int cmd_open(Shelf *s, int argc, char **argv) {
    Book *books = NULL;
    int n = 0, i, rc = 0;
    if (argc < 3) {
        fprintf(stderr, "recto: open needs a file or a folder\n");
        return 1;
    }
    if (import_paths((const char **)(argv + 2), argc - 2, &s->set, &books, &n) != 0) {
        fprintf(stderr, "recto: %s\n", recto_err);
        return 1;
    }
    for (i = 0; i < n; i++) {
        const char *origin = NULL;
        if (books[i].source[0] && file_exists(books[i].source)) origin = books[i].source;
        if (shelf_add(s, &books[i], origin) != 0) {
            fprintf(stderr, "recto: %s\n", recto_err);
            rc = 1;
            break;
        }
        printf("%s  %s\n", books[i].id, books[i].title);
    }
    for (i = 0; i < n; i++) book_free(&books[i]);
    free(books);
    return rc;
}

static int cmd_show(Shelf *s, const char *id, const char *label) {
    Book b;
    int i, any = 0;
    memset(&b, 0, sizeof b);
    if (shelf_load(s, id, &b) != 0) {
        fprintf(stderr, "recto: %s\n", recto_err);
        return 1;
    }
    printf("%s\n", b.title);
    if (b.creator[0]) printf("%s\n", b.creator);
    for (i = 0; i < b.nblocks; i++) {
        if (label && !str_ieq(b.blocks[i].label, label) && !(is_int_token(label) && b.blocks[i].page == atoi(label)))
            continue;
        any = 1;
        printf("\n[%s]\n%s\n", b.blocks[i].label, b.blocks[i].text ? b.blocks[i].text : "");
    }
    if (label && !any) {
        int at = book_label_block(&b, label);
        if (at >= 0) printf("\n[%s]\n%s\n", b.blocks[at].label, b.blocks[at].text ? b.blocks[at].text : "");
        else {
            fprintf(stderr, "recto: no page with that number\n");
            book_free(&b);
            return 1;
        }
    }
    book_free(&b);
    return 0;
}

static int cmd_find(Shelf *s, const char *id, int argc, char **argv) {
    Book b;
    Hit *hits = NULL;
    int nh = 0, i;
    Buf q;
    memset(&b, 0, sizeof b);
    if (shelf_load(s, id, &b) != 0) {
        fprintf(stderr, "recto: %s\n", recto_err);
        return 1;
    }
    buf_init(&q);
    for (i = 3; i < argc; i++) {
        if (q.n) buf_putc(&q, ' ');
        buf_puts(&q, argv[i]);
    }
    if (book_find(&b, q.p, &hits, &nh) != 0) {
        fprintf(stderr, "recto: %s\n", recto_err);
        buf_free(&q);
        book_free(&b);
        return 1;
    }
    for (i = 0; i < nh; i++) printf("p. %s  %s\n", hits[i].label, hits[i].snippet);
    if (nh == 0) printf("not found\n");
    free(hits);
    buf_free(&q);
    book_free(&b);
    return 0;
}

static int cmd_ocr(Shelf *s, int argc, char **argv) {
    const char *path = NULL;
    int page = 1, x = 0, y = 0, w = 0, h = 0, cropped = 0, i;
    Image im, crop;
    OcrPage op;
    for (i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--page") == 0 && i + 1 < argc) {
            page = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--engine") == 0 && i + 1 < argc) {
            char kv[80];
            snprintf(kv, sizeof kv, "engine=%s", argv[++i]);
            if (settings_apply(&s->set, kv) != 0) {
                fprintf(stderr, "recto: %s\n", recto_err);
                return 1;
            }
        } else if (strcmp(argv[i], "--rect") == 0 && i + 1 < argc) {
            char buf[80];
            int k;
            snprintf(buf, sizeof buf, "%s", argv[++i]);
            for (k = 0; buf[k]; k++)
                if (buf[k] == ',') buf[k] = ' ';
            if (sscanf(buf, "%d %d %d %d", &x, &y, &w, &h) != 4) {
                fprintf(stderr, "recto: crop is x,y,w,h\n");
                return 1;
            }
            cropped = 1;
        } else if (argv[i][0] != '-') path = argv[i];
        else {
            fprintf(stderr, "recto: unknown option %s\n", argv[i]);
            return 1;
        }
    }
    if (!path) {
        fprintf(stderr, "recto: ocr needs a file\n");
        return 1;
    }
    memset(&im, 0, sizeof im);
    memset(&crop, 0, sizeof crop);
    memset(&op, 0, sizeof op);
    if (load_page_image(path, page, &im) != 0) {
        fprintf(stderr, "recto: %s\n", recto_err);
        return 1;
    }
    if (cropped) {
        if (image_crop(&im, x, y, w, h, &crop) != 0) {
            fprintf(stderr, "recto: %s\n", recto_err);
            image_free(&im);
            return 1;
        }
        image_free(&im);
        im = crop;
    }
    if (ocr_image(&im, &s->set, &op) != 0) {
        fprintf(stderr, "recto: %s\n", recto_err);
        image_free(&im);
        return 1;
    }
    printf("%s\n", op.text ? op.text : "");
    image_free(&im);
    ocr_page_free(&op);
    return 0;
}

static int cmd_convert(Shelf *s, int argc, char **argv) {
    if (argc < 4) {
        fprintf(stderr, "recto: convert needs an .epub file or a folder\n");
        return 1;
    }
    return run_convert(s, (const char **)(argv + 2), argc - 3, argv[argc - 1]);
}

static int cmd_shorthand(Shelf *s, int argc, char **argv) {
    const char *last;
    if (argc < 2 || !file_exists(argv[1])) return -1;
    if (argc == 2) return run_convert(s, (const char **)(argv + 1), 1, NULL);
    last = argv[argc - 1];
    if (file_is_dir(last) || ends_with_sep(last) || ends_with_epub_name(last))
        return run_convert(s, (const char **)(argv + 1), argc - 2, last);
    return run_convert(s, (const char **)(argv + 1), argc - 1, NULL);
}

int main(int argc, char **argv) {
    Shelf shelf;
    int rc = 0;
    if (argc < 2 || strcmp(argv[1], "help") == 0 || strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0) {
        help();
        return 0;
    }
    if (strcmp(argv[1], "version") == 0 || strcmp(argv[1], "--version") == 0 || strcmp(argv[1], "-V") == 0) {
        printf("%s\n", RECTO_VERSION);
        return 0;
    }
    if (strcmp(argv[1], "test") == 0) return recto_selftest() == 0 ? 0 : 1;
    if (!known_command(argv[1])) {
        if (!file_exists(argv[1])) {
            help();
            return 1;
        }
        if (need_shelf(&shelf) != 0) return 1;
        rc = cmd_shorthand(&shelf, argc, argv);
        shelf_close(&shelf);
        if (rc < 0) {
            help();
            return 1;
        }
        return rc;
    }
    if (need_shelf(&shelf) != 0) return 1;
    if (strcmp(argv[1], "gui") == 0) {
        rc = gui_run(&shelf);
        if (rc != 0) fprintf(stderr, "recto: %s\n", recto_err[0] ? recto_err : "the window did not open");
        shelf_close(&shelf);
        return rc == 0 ? 0 : 1;
    }
    if (strcmp(argv[1], "desk") == 0) {
        rc = desk_run(&shelf);
        if (rc != 0) fprintf(stderr, "recto: %s\n", recto_err[0] ? recto_err : "the desk did not open");
        shelf_close(&shelf);
        return rc == 0 ? 0 : 1;
    }
    if (strcmp(argv[1], "convert") == 0) rc = cmd_convert(&shelf, argc, argv);
    else if (strcmp(argv[1], "open") == 0) rc = cmd_open(&shelf, argc, argv);
    else if (strcmp(argv[1], "books") == 0) {
        int i;
        for (i = 0; i < shelf.nsum; i++)
            printf("%s  %s  %d\n", shelf.sum[i].id, shelf.sum[i].title[0] ? shelf.sum[i].title : "(untitled)",
                   shelf.sum[i].file_pages);
    } else if (strcmp(argv[1], "show") == 0 && argc >= 3) rc = cmd_show(&shelf, argv[2], NULL);
    else if (strcmp(argv[1], "page") == 0 && argc >= 4) rc = cmd_show(&shelf, argv[2], argv[3]);
    else if (strcmp(argv[1], "find") == 0 && argc >= 4) rc = cmd_find(&shelf, argv[2], argc, argv);
    else if (strcmp(argv[1], "export") == 0 && argc >= 4) {
        Book b;
        memset(&b, 0, sizeof b);
        if (shelf_load(&shelf, argv[2], &b) != 0 || epub_write(&b, argv[3]) != 0) {
            fprintf(stderr, "recto: %s\n", recto_err);
            rc = 1;
        }
        book_free(&b);
    } else if (strcmp(argv[1], "ocr") == 0) rc = cmd_ocr(&shelf, argc, argv);
    else if (strcmp(argv[1], "remove") == 0 && argc >= 3) {
        int yes = argc >= 4 && strcmp(argv[3], "--yes") == 0;
        if (!yes) {
            fprintf(stderr, "recto: say remove %s --yes\n", argv[2]);
            rc = 1;
        } else if (shelf_delete(&shelf, argv[2]) != 0) {
            fprintf(stderr, "recto: %s\n", recto_err);
            rc = 1;
        }
    } else if (strcmp(argv[1], "sample") == 0) {
        if (shelf_seed_samples(&shelf) != 0) {
            fprintf(stderr, "recto: %s\n", recto_err);
            rc = 1;
        }
    } else if (strcmp(argv[1], "settings") == 0) {
        int i;
        for (i = 2; i < argc; i++) {
            if (settings_apply(&shelf.set, argv[i]) != 0) {
                fprintf(stderr, "recto: %s\n", recto_err);
                rc = 1;
                break;
            }
        }
        if (rc == 0 && argc > 2 && shelf_save_settings(&shelf) != 0) {
            fprintf(stderr, "recto: %s\n", recto_err);
            rc = 1;
        }
        if (rc == 0) {
            Buf b;
            buf_init(&b);
            settings_format(&shelf.set, &b);
            fwrite(b.p, 1, b.n, stdout);
            buf_free(&b);
        }
    } else if (strcmp(argv[1], "library") == 0) {
        int i;
        for (i = 0; i < shelf.nsum; i++) {
            Book b;
            memset(&b, 0, sizeof b);
            if (shelf_load(&shelf, shelf.sum[i].id, &b) != 0) continue;
            printf("%s\n  title: %s\n  creator: %s\n  date: %s\n  work: %s\n  expression: %s\n  volume: %s\n", b.id,
                   b.title, b.creator, b.date, b.work, b.expression, b.volume);
            book_free(&b);
        }
    } else {
        help();
        rc = 1;
    }
    shelf_close(&shelf);
    return rc;
}
