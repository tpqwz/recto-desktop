#ifndef RECTO_H
#define RECTO_H

/* Recto converts text files, PDF first, to EPUB. C99, for Tiny C Compiler.
   One translation unit set: no VLAs, no POSIX-only calls in headers. */

#include <stddef.h>
#include <stdint.h>

#define RECTO_PATH 768
#define RECTO_ERR 2048

enum {
    PIC_UNREAD = 0,
    PIC_EMPTY = 1,
    PIC_ALL = 2
};

enum {
    ENG_BEST = 0,
    ENG_BUILTIN = 1,
    ENG_TESS = 2,
    ENG_VISION = 3
};

enum {
    COL_AUTO = 0,
    COL_ONE = 1,
    COL_TWO = 2
};

enum {
    BLK_P = 0,
    BLK_H1 = 1,
    BLK_H2 = 2,
    BLK_NOTE = 3,
    BLK_TABLE = 4,
    BLK_LI = 5
};

typedef struct Settings {
    int pictures;     /* PIC_* */
    int keep_file;
    int contrast;
    int deskew;
    int columns;      /* COL_* */
    int engine;       /* ENG_* */
    int vision;       /* vision may run when engine is best or vision */
    char scripts[80]; /* tesseract -l string; eng starts on */
    char vision_model[48];
} Settings;

typedef struct Word {
    char *text;
    int page;         /* 1-based file page */
    char label[24];   /* printed page; empty if unknown */
    double x, y, w, h;
    double size;
    int conf;         /* 0..100; 100 means a text layer */
    int brk;          /* do not merge this line with the next */
    int positioned;   /* geometry is real */
} Word;

typedef struct Block {
    int kind;
    int page;
    char label[24];
    char *text;
    int x, y, w, h;
    int conf;
} Block;

typedef struct TocItem {
    char *title;
    char label[24];
    int page;
    int level;
    int block;
} TocItem;

typedef struct Hit {
    int block;
    int page;
    char label[24];
    char snippet[180];
} Hit;

/* Fifteen Dublin Core elements, plus the work / expression / volume
   the desk uses to say which text, which translation, and which copy. */
typedef struct Book {
    char id[48];
    char title[256];
    char creator[256];
    char subject[256];
    char description[512];
    char publisher[256];
    char contributor[256];
    char date[80];
    char type[80];
    char format[80];
    char identifier[160];
    char source[256];
    char language[80];
    char relation[256];
    char coverage[160];
    char rights[256];
    char work[256];
    char expression[256];
    char volume[80];
    char origin_name[256];
    char reader_note[160];
    int file_pages;
    int hyphens;
    int headers;
    int columns;
    int tables;
    int notes;
    int pictures;
    int chapters;
    int duplicates;
    int place_page;
    int place_block;
    Block *blocks;
    int nblocks;
    int bcap;
    TocItem *toc;
    int ntoc;
    int tcap;
} Book;

typedef struct Buf {
    char *p;
    size_t n;
    size_t cap;
} Buf;

typedef struct Image {
    int w, h;
    uint8_t *px; /* 0 black, 255 white */
} Image;

typedef struct Blob {
    int x0, y0, x1, y1;
    int area;
} Blob;

typedef struct OcrWord {
    char *text;
    int x, y, w, h;
    int conf;
} OcrWord;

typedef struct OcrPage {
    OcrWord *words;
    int n;
    int cap;
    char *text;
    int conf;
    int pictures;
    char engine[16];
} OcrPage;

typedef struct Shelf Shelf;

extern char recto_err[RECTO_ERR];

void recto_seterr(const char *fmt, ...);
void settings_default(Settings *s);

void *xmalloc(size_t n);
void *xrealloc(void *p, size_t n);
char *xstrdup(const char *s);
char *xstrndup(const char *s, size_t n);

void buf_init(Buf *b);
void buf_free(Buf *b);
void buf_clear(Buf *b);
void buf_reserve(Buf *b, size_t need);
void buf_append(Buf *b, const void *p, size_t n);
void buf_puts(Buf *b, const char *s);
void buf_putc(Buf *b, int c);
void buf_printf(Buf *b, const char *fmt, ...);
char *buf_steal(Buf *b);

int file_read(const char *path, Buf *b);
int file_write(const char *path, const void *p, size_t n);
int file_exists(const char *path);
int file_is_dir(const char *path);
int dir_make(const char *path);
int dir_make_p(const char *path);
int dir_list(const char *path, char ***names, int *n);
void dir_list_free(char **names, int n);

char *path_join(const char *a, const char *b);
const char *path_base(const char *p);

char *str_trim(char *s);
int str_ieq(const char *a, const char *b);
int str_istart(const char *s, const char *pfx);
int str_icontains(const char *s, const char *q);
void str_tolower(char *s);

int roman_parse(const char *s);
void roman_format(int n, int upper, char *out, size_t cap);
int is_int_token(const char *s);
int natcmp(const char *a, const char *b);

int which_cmd(const char *name);
void sh_quote(Buf *b, const char *s);
int cmd_capture(const char *cmd, Buf *out);
int temp_path(char *out, size_t cap, const char *suffix);

void book_init(Book *b);
void book_free(Book *b);
void book_add_block(Book *b, int kind, int page, const char *label,
                    const char *text, int x, int y, int w, int h, int conf);
void book_rebuild_toc(Book *b);
int book_write_file(const Book *b, const char *path);
int book_read_file(const char *path, Book *b);
int book_read_mem(const char *text, Book *b);
int book_find(const Book *b, const char *query, Hit **hits, int *nhits);
int book_label_block(const Book *b, const char *label);
const char *block_kind_name(int kind);

void words_free(Word *w, int n);
int compose_book(Word *words, int nwords, int file_pages, const Settings *set, Book *book);

void image_free(Image *im);
int image_alloc(Image *im, int w, int h, int fill);
int image_load(const char *path, Image *im);
int image_save_pgm(const Image *im, const char *path);
int image_write_png(const Image *im, Buf *out);
int image_crop(const Image *src, int x, int y, int w, int h, Image *dst);
void image_invert(Image *im);
void image_contrast(Image *im);
void image_otsu(Image *im);
int image_deskew(Image *im);
void image_fit(Image *im, int max_edge);
int image_blobs(const Image *im, Blob **out, int *n);

int font_render(const char *text, int scale, Image *im);
void ocr_page_free(OcrPage *p);
int ocr_builtin(const Image *im, OcrPage *out);
int ocr_tesseract_file(const char *path, const char *lang, OcrPage *out);
int ocr_image(const Image *im, const Settings *set, OcrPage *out);

int vision_build_request(const Image *im, const char *prompt, const char *model, Buf *json);
int vision_ocr(const Image *im, const Settings *set, OcrPage *out);

int pdf_extract_native(const char *path, Word **words, int *nwords, int *npages,
                       char ***labels, Book *meta);
int pdf_page_raster(const char *path, int page, Image *im);

int epub_write(const Book *b, const char *path);

struct Shelf {
    char *dir;
    Settings set;
    Book *sum;
    int nsum;
    int scap;
    int seeded;
};

int shelf_open(Shelf *s, const char *dir_or_null);
void shelf_close(Shelf *s);
int shelf_refresh(Shelf *s);
int shelf_save_settings(const Shelf *s);
int shelf_add(Shelf *s, Book *b, const char *origin_path);
int shelf_load(const Shelf *s, const char *id, Book *b);
int shelf_save(const Shelf *s, const Book *b);
int shelf_delete(Shelf *s, const char *id);
int shelf_revise(Shelf *s, Book *b, int block, const char *text);
int shelf_restore(Shelf *s, Book *b);
int shelf_seed_samples(Shelf *s);
const char *shelf_sample_edition(const char *id);
int settings_apply(Settings *s, const char *kv);
void settings_format(const Settings *s, Buf *out);

int import_paths(const char **paths, int npaths, const Settings *set, Book **books, int *nbooks);
int convert_files(const char **inputs, int ninputs, const char *dest, const Settings *set, Shelf *shelf,
                  char ***paths, char ***ids, int *nouts);
void convert_free(char **paths, char **ids, int n);
int load_page_image(const char *path, int page, Image *im);

int desk_run(Shelf *shelf);
int gui_call(Shelf *shelf, const char *op, const char *a, const char *b, const char *c, Buf *out);
int gui_run(Shelf *shelf);
int gui_http_probe(void);
int recto_selftest(void);

#endif
