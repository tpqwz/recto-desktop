#include "recto.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#else
#include <unistd.h>
#endif

static const char *DHAMMAPADA =
    "recto 1\n"
    "id: sample-dhammapada\n"
    "title: The Dhammapada\n"
    "creator: translated by Max Muller\n"
    "subject: Buddhism\n"
    "description: Verses from the Dhammapada, with the printed page numbers kept.\n"
    "publisher: Clarendon Press\n"
    "date: 1881\n"
    "type: Text\n"
    "format: text\n"
    "identifier: urn:recto:sample-dhammapada\n"
    "source: Sacred Books of the East, volume 10\n"
    "language: eng\n"
    "relation: https://rect.grok.me\n"
    "rights: Public domain\n"
    "work: Dhammapada\n"
    "expression: English translation by Max Muller, 1881\n"
    "volume: 1\n"
    "reader: Sample edition on the shelf.\n"
    "pages: 4\n"
    "place_page: 1\n"
    "place_block: 0\n"
    "---\n"
    "@h1 1 1 0 0 0 0 100\n"
    "Chapter I. The Twin-Verses\n"
    "@end\n"
    "@p 1 1 0 0 0 0 100\n"
    "1. All that we are is the result of what we have thought: it is founded on our thoughts, it is made up of our thoughts. If a man speaks or acts with an evil thought, pain follows him, as the wheel follows the foot of the ox that draws the carriage.\n"
    "@end\n"
    "@p 1 1 0 0 0 0 100\n"
    "2. All that we are is the result of what we have thought: it is founded on our thoughts, it is made up of our thoughts. If a man speaks or acts with a pure thought, happiness follows him, like a shadow that never leaves him.\n"
    "@end\n"
    "@p 1 1 0 0 0 0 100\n"
    "3. He abused me, he beat me, he defeated me, he robbed me, -- in those who harbour such thoughts hatred will never cease.\n"
    "@end\n"
    "@p 1 1 0 0 0 0 100\n"
    "4. He abused me, he beat me, he defeated me, he robbed me, -- in those who do not harbour such thoughts hatred will cease.\n"
    "@end\n"
    "@p 1 1 0 0 0 0 100\n"
    "5. For hatred does not cease by hatred at any time: hatred ceases by love, this is an old rule.\n"
    "@end\n"
    "@p 1 1 0 0 0 0 100\n"
    "6. The world does not know that we must all come to an end here; -- but those who know it, their quarrels cease at once.\n"
    "@end\n"
    "@p 1 1 0 0 0 0 100\n"
    "7. He who lives looking for pleasures only, his senses uncontrolled, immoderate in his food, idle, and weak, Mara (the tempter) will certainly overthrow him, as the wind throws down a weak tree.\n"
    "@end\n"
    "@p 1 1 0 0 0 0 100\n"
    "8. He who lives without looking for pleasures, his senses well controlled, moderate in his food, faithful and strong, him Mara will certainly not overthrow, any more than the wind throws down a rocky mountain.\n"
    "@end\n"
    "@h1 4 4 0 0 0 0 100\n"
    "Chapter II. On Earnestness\n"
    "@end\n"
    "@p 4 4 0 0 0 0 100\n"
    "21. Earnestness is the path of immortality (Nirvana), thoughtlessness the path of death. Those who are in earnest do not die, those who are thoughtless are as if dead already.\n"
    "@end\n"
    "@p 4 4 0 0 0 0 100\n"
    "22. Those who are advanced in earnestness, having understood this clearly, delight in earnestness, and rejoice in the knowledge of the Ariyas (the elect).\n"
    "@end\n"
    "@p 4 4 0 0 0 0 100\n"
    "23. These wise people, meditative, steady, always possessed of strong powers, attain to Nirvana, the highest happiness.\n"
    "@end\n"
    "@p 4 4 0 0 0 0 100\n"
    "24. If an earnest person has roused himself, if he is not forgetful, if his deeds are pure, if he acts with consideration, if he restrains himself, and lives according to law, -- then his glory will increase.\n"
    "@end\n";

static const char *REPUBLIC =
    "recto 1\n"
    "id: sample-republic\n"
    "title: The Republic\n"
    "creator: Plato\n"
    "subject: Philosophy\n"
    "description: Title page of the Jowett translation.\n"
    "date: 1892\n"
    "type: Text\n"
    "format: text\n"
    "identifier: urn:recto:sample-republic\n"
    "language: eng\n"
    "relation: https://rect.grok.me\n"
    "rights: Public domain\n"
    "work: Politeia\n"
    "expression: English translation by Benjamin Jowett\n"
    "volume: 1\n"
    "reader: Bibliographic sample. The dialogue itself is not copied here.\n"
    "pages: 1\n"
    "---\n"
    "@h1 1 1 0 0 0 0 100\n"
    "The Republic\n"
    "@end\n"
    "@p 1 1 0 0 0 0 100\n"
    "Plato. Translated by Benjamin Jowett.\n"
    "@end\n";

static const char *VARIETIES =
    "recto 1\n"
    "id: sample-varieties\n"
    "title: The Varieties of Religious Experience\n"
    "creator: William James\n"
    "subject: Religion\n"
    "description: Title page of the 1902 lectures.\n"
    "publisher: Longmans, Green\n"
    "date: 1902\n"
    "type: Text\n"
    "format: text\n"
    "identifier: urn:recto:sample-varieties\n"
    "language: eng\n"
    "relation: https://rect.grok.me\n"
    "rights: Public domain\n"
    "work: The Varieties of Religious Experience\n"
    "expression: The Gifford Lectures, 1901-1902\n"
    "volume: 1\n"
    "reader: Bibliographic sample.\n"
    "pages: 1\n"
    "---\n"
    "@h1 1 1 0 0 0 0 100\n"
    "The Varieties of Religious Experience\n"
    "@end\n"
    "@p 1 1 0 0 0 0 100\n"
    "William James. 1902.\n"
    "@end\n";

static const char *CANNON =
    "recto 1\n"
    "id: sample-cannon\n"
    "title: Bodily Changes in Pain, Hunger, Fear and Rage\n"
    "creator: Walter B. Cannon\n"
    "subject: Physiology\n"
    "description: Title page of the Appleton edition.\n"
    "publisher: D. Appleton and Company\n"
    "date: 1915\n"
    "type: Text\n"
    "format: text\n"
    "identifier: urn:recto:sample-cannon\n"
    "language: eng\n"
    "relation: https://rect.grok.me\n"
    "rights: Public domain\n"
    "work: Bodily Changes in Pain, Hunger, Fear and Rage\n"
    "expression: First edition, 1915\n"
    "volume: 1\n"
    "reader: Bibliographic sample.\n"
    "pages: 1\n"
    "---\n"
    "@h1 1 1 0 0 0 0 100\n"
    "Bodily Changes in Pain, Hunger, Fear and Rage\n"
    "@end\n"
    "@p 1 1 0 0 0 0 100\n"
    "Walter B. Cannon. D. Appleton and Company, 1915.\n"
    "@end\n";

static const char *CARMEL =
    "recto 1\n"
    "id: sample-carmel\n"
    "title: The Ascent of Mount Carmel\n"
    "creator: Saint John of the Cross\n"
    "contributor: David Lewis\n"
    "subject: Theology\n"
    "description: Title page of the Lewis translation.\n"
    "date: 1906\n"
    "type: Text\n"
    "format: text\n"
    "identifier: urn:recto:sample-carmel\n"
    "language: eng\n"
    "relation: https://rect.grok.me\n"
    "rights: Public domain\n"
    "work: Subida del Monte Carmelo\n"
    "expression: English translation by David Lewis, 1906\n"
    "volume: 1\n"
    "reader: Bibliographic sample.\n"
    "pages: 1\n"
    "---\n"
    "@h1 1 i 0 0 0 0 100\n"
    "The Ascent of Mount Carmel\n"
    "@end\n"
    "@p 1 i 0 0 0 0 100\n"
    "Saint John of the Cross. Translated by David Lewis, 1906. Subida del Monte Carmelo.\n"
    "@end\n";

const char *shelf_sample_edition(const char *id) {
    if (!id) return NULL;
    if (str_ieq(id, "sample-dhammapada")) return DHAMMAPADA;
    if (str_ieq(id, "sample-republic")) return REPUBLIC;
    if (str_ieq(id, "sample-varieties")) return VARIETIES;
    if (str_ieq(id, "sample-cannon")) return CANNON;
    if (str_ieq(id, "sample-carmel")) return CARMEL;
    return NULL;
}

static int id_ok(const char *id) {
    const char *p;
    if (!id || !id[0] || strlen(id) > 40) return 0;
    if (strcmp(id, ".") == 0 || strcmp(id, "..") == 0) return 0;
    for (p = id; *p; p++) {
        unsigned char c = (unsigned char)*p;
        if (!(isalnum(c) || c == '-' || c == '_')) return 0;
    }
    return 1;
}

static char *book_dir(const Shelf *s, const char *id) {
    char *books = path_join(s->dir, "books");
    char *one = path_join(books, id);
    free(books);
    return one;
}

static char *book_path(const Shelf *s, const char *id) {
    char *dir = book_dir(s, id);
    char *p = path_join(dir, "book.txt");
    free(dir);
    return p;
}

static void summary_take(Book *dst, const Book *src) {
    memset(dst, 0, sizeof *dst);
    memcpy(dst->id, src->id, sizeof dst->id);
    memcpy(dst->title, src->title, sizeof dst->title);
    memcpy(dst->creator, src->creator, sizeof dst->creator);
    memcpy(dst->date, src->date, sizeof dst->date);
    memcpy(dst->language, src->language, sizeof dst->language);
    memcpy(dst->work, src->work, sizeof dst->work);
    memcpy(dst->expression, src->expression, sizeof dst->expression);
    memcpy(dst->volume, src->volume, sizeof dst->volume);
    memcpy(dst->origin_name, src->origin_name, sizeof dst->origin_name);
    dst->file_pages = src->file_pages;
    dst->chapters = src->chapters;
    dst->place_page = src->place_page;
    dst->place_block = src->place_block;
}

static const char *pic_name(int p) {
    if (p == PIC_UNREAD) return "unread";
    if (p == PIC_ALL) return "all";
    return "empty";
}

static const char *eng_name(int e) {
    if (e == ENG_BUILTIN) return "builtin";
    if (e == ENG_TESS) return "tesseract";
    if (e == ENG_VISION) return "vision";
    return "best";
}

static const char *col_name(int c) {
    if (c == COL_ONE) return "one";
    if (c == COL_TWO) return "two";
    return "auto";
}

static int on_off(const char *v, int *out) {
    if (str_ieq(v, "1") || str_ieq(v, "on") || str_ieq(v, "yes") || str_ieq(v, "true")) {
        *out = 1;
        return 0;
    }
    if (str_ieq(v, "0") || str_ieq(v, "off") || str_ieq(v, "no") || str_ieq(v, "false")) {
        *out = 0;
        return 0;
    }
    return -1;
}

static int scripts_ok(const char *v) {
    const char *p;
    if (!v || !v[0] || strlen(v) > 70) return 0;
    for (p = v; *p; p++) {
        unsigned char c = (unsigned char)*p;
        if (!(isalnum(c) || c == '+' || c == '_' || c == '-')) return 0;
    }
    return 1;
}

void settings_format(const Settings *s, Buf *out) {
    buf_printf(out, "pictures=%s\n", pic_name(s->pictures));
    buf_printf(out, "keep_file=%d\n", s->keep_file ? 1 : 0);
    buf_printf(out, "contrast=%d\n", s->contrast ? 1 : 0);
    buf_printf(out, "deskew=%d\n", s->deskew ? 1 : 0);
    buf_printf(out, "columns=%s\n", col_name(s->columns));
    buf_printf(out, "engine=%s\n", eng_name(s->engine));
    buf_printf(out, "vision=%d\n", s->vision ? 1 : 0);
    buf_printf(out, "scripts=%s\n", s->scripts);
    buf_printf(out, "vision_model=%s\n", s->vision_model);
}

int settings_apply(Settings *s, const char *kv) {
    char buf[160];
    char *eq, *key, *val;
    int bit;
    if (!s || !kv) return -1;
    snprintf(buf, sizeof buf, "%s", kv);
    eq = strchr(buf, '=');
    if (!eq) {
        recto_seterr("a setting is written as key=value");
        return -1;
    }
    *eq = 0;
    key = str_trim(buf);
    val = str_trim(eq + 1);
    if (str_ieq(key, "pictures")) {
        if (str_ieq(val, "unread")) s->pictures = PIC_UNREAD;
        else if (str_ieq(val, "empty")) s->pictures = PIC_EMPTY;
        else if (str_ieq(val, "all")) s->pictures = PIC_ALL;
        else {
            recto_seterr("pictures is unread, empty, or all");
            return -1;
        }
    } else if (str_ieq(key, "keep_file")) {
        if (on_off(val, &bit) != 0) {
            recto_seterr("keep_file is on or off");
            return -1;
        }
        s->keep_file = bit;
    } else if (str_ieq(key, "contrast")) {
        if (on_off(val, &bit) != 0) {
            recto_seterr("contrast is on or off");
            return -1;
        }
        s->contrast = bit;
    } else if (str_ieq(key, "deskew")) {
        if (on_off(val, &bit) != 0) {
            recto_seterr("deskew is on or off");
            return -1;
        }
        s->deskew = bit;
    } else if (str_ieq(key, "vision")) {
        if (on_off(val, &bit) != 0) {
            recto_seterr("vision is on or off");
            return -1;
        }
        s->vision = bit;
    } else if (str_ieq(key, "columns")) {
        if (str_ieq(val, "auto")) s->columns = COL_AUTO;
        else if (str_ieq(val, "one")) s->columns = COL_ONE;
        else if (str_ieq(val, "two")) s->columns = COL_TWO;
        else {
            recto_seterr("columns is auto, one, or two");
            return -1;
        }
    } else if (str_ieq(key, "engine")) {
        if (str_ieq(val, "best")) s->engine = ENG_BEST;
        else if (str_ieq(val, "builtin")) s->engine = ENG_BUILTIN;
        else if (str_ieq(val, "tesseract") || str_ieq(val, "tess")) s->engine = ENG_TESS;
        else if (str_ieq(val, "vision")) s->engine = ENG_VISION;
        else {
            recto_seterr("engine is best, builtin, tesseract, or vision");
            return -1;
        }
    } else if (str_ieq(key, "scripts")) {
        if (!scripts_ok(val)) {
            recto_seterr("scripts is a tesseract language list such as eng");
            return -1;
        }
        snprintf(s->scripts, sizeof s->scripts, "%s", val);
    } else if (str_ieq(key, "vision_model")) {
        if (!val[0] || strlen(val) > 40 || strchr(val, ' ')) {
            recto_seterr("vision_model is a model name such as grok-4.7");
            return -1;
        }
        snprintf(s->vision_model, sizeof s->vision_model, "%s", val);
    } else {
        recto_seterr("unknown setting %s", key);
        return -1;
    }
    return 0;
}

static int settings_read(Settings *s, const char *path) {
    Buf b;
    char *p;
    buf_init(&b);
    if (!file_exists(path)) return 0;
    if (file_read(path, &b) != 0) return -1;
    p = b.p ? b.p : "";
    while (*p) {
        char *eol = strchr(p, '\n');
        char line[160];
        size_t n = eol ? (size_t)(eol - p) : strlen(p);
        if (n >= sizeof line) n = sizeof line - 1;
        memcpy(line, p, n);
        line[n] = 0;
        str_trim(line);
        if (line[0] && line[0] != '#') {
            if (settings_apply(s, line) != 0) {
                buf_free(&b);
                return -1;
            }
        }
        if (!eol) break;
        p = eol + 1;
    }
    buf_free(&b);
    return 0;
}

static int copy_file(const char *from, const char *to) {
    Buf b;
    int rc;
    buf_init(&b);
    if (file_read(from, &b) != 0) return -1;
    rc = file_write(to, b.p, b.n);
    buf_free(&b);
    return rc;
}

static int rm_tree(const char *path) {
    if (!path || !file_exists(path)) return 0;
    if (file_is_dir(path)) {
        char **names = NULL;
        int n = 0, i;
        if (dir_list(path, &names, &n) == 0) {
            for (i = 0; i < n; i++) {
                char *child;
                if (strcmp(names[i], ".") == 0 || strcmp(names[i], "..") == 0) continue;
                child = path_join(path, names[i]);
                rm_tree(child);
                free(child);
            }
            dir_list_free(names, n);
        }
#ifdef _WIN32
        _rmdir(path);
#else
        rmdir(path);
#endif
    } else remove(path);
    return 0;
}

static void slug_id(const char *title, char *out, size_t cap) {
    size_t o = 0;
    const char *p = title ? title : "";
    for (; *p && o + 1 < cap && o < 28; p++) {
        unsigned char c = (unsigned char)tolower((unsigned char)*p);
        if (isalnum(c)) out[o++] = (char)c;
        else if ((c == ' ' || c == '-' || c == '_') && o && out[o - 1] != '-') out[o++] = '-';
    }
    while (o && out[o - 1] == '-') o--;
    out[o] = 0;
    if (!out[0]) snprintf(out, cap, "book");
}

static int have_id(const Shelf *s, const char *id) {
    char *p = book_path(s, id);
    int yes = file_exists(p);
    free(p);
    return yes;
}

int shelf_refresh(Shelf *s) {
    char *books;
    char **names = NULL;
    int n = 0, i;
    if (!s) return -1;
    for (i = 0; i < s->nsum; i++) book_free(&s->sum[i]);
    free(s->sum);
    s->sum = NULL;
    s->nsum = 0;
    s->scap = 0;
    books = path_join(s->dir, "books");
    if (dir_list(books, &names, &n) != 0) {
        free(books);
        return -1;
    }
    for (i = 0; i < n; i++) {
        char *dir, *path;
        Book full;
        if (!id_ok(names[i])) continue;
        dir = path_join(books, names[i]);
        path = path_join(dir, "book.txt");
        free(dir);
        if (!file_exists(path)) {
            free(path);
            continue;
        }
        memset(&full, 0, sizeof full);
        if (book_read_file(path, &full) != 0) {
            free(path);
            book_free(&full);
            continue;
        }
        free(path);
        if (s->nsum >= s->scap) {
            s->scap = s->scap ? s->scap * 2 : 8;
            s->sum = xrealloc(s->sum, (size_t)s->scap * sizeof *s->sum);
        }
        summary_take(&s->sum[s->nsum], &full);
        s->nsum++;
        book_free(&full);
    }
    dir_list_free(names, n);
    free(books);
    for (i = 1; i < s->nsum; i++) {
        Book key = s->sum[i];
        int j = i;
        while (j > 0 && natcmp(s->sum[j - 1].title, key.title) > 0) {
            s->sum[j] = s->sum[j - 1];
            j--;
        }
        s->sum[j] = key;
    }
    return 0;
}

int shelf_save_settings(const Shelf *s) {
    Buf b;
    char *path;
    int rc;
    if (!s) return -1;
    buf_init(&b);
    settings_format(&s->set, &b);
    path = path_join(s->dir, "settings.txt");
    rc = file_write(path, b.p, b.n);
    free(path);
    buf_free(&b);
    return rc;
}

static int write_init(const Shelf *s) {
    char *p = path_join(s->dir, "init");
    int rc = file_write(p, "1\n", 2);
    free(p);
    return rc;
}

static int default_dir(char *out, size_t cap) {
    const char *env = getenv("RECTO_SHELF");
    const char *home;
    if (env && env[0]) {
        snprintf(out, cap, "%s", env);
        return 0;
    }
#ifdef _WIN32
    home = getenv("USERPROFILE");
#else
    home = getenv("HOME");
#endif
    if (!home || !home[0]) home = ".";
    snprintf(out, cap, "%s/.recto", home);
    return 0;
}

int shelf_open(Shelf *s, const char *dir_or_null) {
    char chosen[RECTO_PATH];
    char *books, *settings, *initp;
    if (!s) return -1;
    memset(s, 0, sizeof *s);
    settings_default(&s->set);
    if (dir_or_null && dir_or_null[0]) snprintf(chosen, sizeof chosen, "%s", dir_or_null);
    else default_dir(chosen, sizeof chosen);
    if (dir_make_p(chosen) != 0) return -1;
    s->dir = xstrdup(chosen);
    books = path_join(s->dir, "books");
    if (dir_make_p(books) != 0) {
        free(books);
        return -1;
    }
    free(books);
    settings = path_join(s->dir, "settings.txt");
    if (settings_read(&s->set, settings) != 0) {
        free(settings);
        return -1;
    }
    free(settings);
    if (shelf_refresh(s) != 0) return -1;
    initp = path_join(s->dir, "init");
    if (!file_exists(initp)) {
        if (s->nsum == 0) {
            if (shelf_seed_samples(s) != 0) {
                free(initp);
                return -1;
            }
        }
        write_init(s);
        s->seeded = 1;
    }
    free(initp);
    return 0;
}

void shelf_close(Shelf *s) {
    int i;
    if (!s) return;
    for (i = 0; i < s->nsum; i++) book_free(&s->sum[i]);
    free(s->sum);
    free(s->dir);
    memset(s, 0, sizeof *s);
}

int shelf_load(const Shelf *s, const char *id, Book *b) {
    char *path;
    if (!s || !b || !id_ok(id)) {
        recto_seterr("that book is not on the shelf");
        return -1;
    }
    path = book_path(s, id);
    if (book_read_file(path, b) != 0) {
        free(path);
        return -1;
    }
    free(path);
    if (!b->id[0]) snprintf(b->id, sizeof b->id, "%s", id);
    return 0;
}

int shelf_save(const Shelf *s, const Book *b) {
    char *dir, *path;
    int rc;
    if (!s || !b || !id_ok(b->id)) {
        recto_seterr("the book has no shelf id");
        return -1;
    }
    dir = book_dir(s, b->id);
    if (dir_make_p(dir) != 0) {
        free(dir);
        return -1;
    }
    free(dir);
    path = book_path(s, b->id);
    rc = book_write_file(b, path);
    free(path);
    return rc;
}

int shelf_add(Shelf *s, Book *b, const char *origin_path) {
    char slug[40];
    int n = 2;
    char *dir, *origin;
    if (!s || !b) return -1;
    if (!id_ok(b->id)) {
        slug_id(b->title[0] ? b->title : "book", slug, sizeof slug);
        snprintf(b->id, sizeof b->id, "%s", slug);
        while (have_id(s, b->id)) {
            snprintf(b->id, sizeof b->id, "%s-%d", slug, n);
            n++;
            if (n > 10000) {
                recto_seterr("could not choose a shelf id");
                return -1;
            }
        }
    } else if (have_id(s, b->id)) {
        recto_seterr("a book named %s is already on the shelf", b->id);
        return -1;
    }
    if (origin_path && origin_path[0] && !b->origin_name[0])
        snprintf(b->origin_name, sizeof b->origin_name, "%s", path_base(origin_path));
    if (shelf_save(s, b) != 0) return -1;
    dir = book_dir(s, b->id);
    if (origin_path && origin_path[0] && !file_is_dir(origin_path)) {
        origin = path_join(dir, "origin.txt");
        file_write(origin, origin_path, strlen(origin_path));
        free(origin);
        if (s->set.keep_file) {
            char *filedir = path_join(dir, "file");
            char *dest;
            dir_make(filedir);
            dest = path_join(filedir, path_base(origin_path));
            if (copy_file(origin_path, dest) != 0) {
                free(dest);
                free(filedir);
                free(dir);
                return -1;
            }
            free(dest);
            free(filedir);
        }
    } else if (origin_path && origin_path[0]) {
        origin = path_join(dir, "origin.txt");
        file_write(origin, origin_path, strlen(origin_path));
        free(origin);
    }
    free(dir);
    return shelf_refresh(s);
}

int shelf_delete(Shelf *s, const char *id) {
    char *dir;
    if (!s || !id_ok(id)) {
        recto_seterr("that book is not on the shelf");
        return -1;
    }
    dir = book_dir(s, id);
    if (!file_exists(dir)) {
        free(dir);
        recto_seterr("that book is not on the shelf");
        return -1;
    }
    rm_tree(dir);
    free(dir);
    return shelf_refresh(s);
}

static int snapshot(const Shelf *s, const Book *b) {
    char *dir = book_dir(s, b->id);
    char *rev = path_join(dir, "revisions");
    int i, rc;
    char name[32];
    char *path = NULL;
    dir_make_p(rev);
    for (i = 1; i < 100000; i++) {
        snprintf(name, sizeof name, "%05d.txt", i);
        free(path);
        path = path_join(rev, name);
        if (!file_exists(path)) break;
    }
    rc = book_write_file(b, path);
    free(path);
    free(rev);
    free(dir);
    return rc;
}

int shelf_revise(Shelf *s, Book *b, int block, const char *text) {
    if (!s || !b) return -1;
    if (block >= b->nblocks) {
        recto_seterr("that passage is not in the book");
        return -1;
    }
    if (snapshot(s, b) != 0) return -1;
    if (block >= 0) {
        free(b->blocks[block].text);
        b->blocks[block].text = xstrdup(text ? text : "");
        book_rebuild_toc(b);
    }
    if (shelf_save(s, b) != 0) return -1;
    return shelf_refresh(s);
}

int shelf_restore(Shelf *s, Book *b) {
    char *dir, *rev;
    char **names = NULL;
    int n = 0, i, best = -1;
    char *path;
    Book old;
    if (!s || !b || !id_ok(b->id)) return -1;
    dir = book_dir(s, b->id);
    rev = path_join(dir, "revisions");
    free(dir);
    if (dir_list(rev, &names, &n) != 0 || n == 0) {
        free(rev);
        dir_list_free(names, n);
        recto_seterr("this book has no earlier revision");
        return -1;
    }
    for (i = 0; i < n; i++) {
        if (best < 0 || natcmp(names[i], names[best]) > 0) best = i;
    }
    path = path_join(rev, names[best]);
    memset(&old, 0, sizeof old);
    if (book_read_file(path, &old) != 0) {
        free(path);
        free(rev);
        dir_list_free(names, n);
        book_free(&old);
        return -1;
    }
    snprintf(old.id, sizeof old.id, "%s", b->id);
    if (shelf_save(s, &old) != 0) {
        free(path);
        free(rev);
        dir_list_free(names, n);
        book_free(&old);
        return -1;
    }
    remove(path);
    book_free(b);
    *b = old;
    memset(&old, 0, sizeof old);
    free(path);
    free(rev);
    dir_list_free(names, n);
    return shelf_refresh(s);
}

int shelf_seed_samples(Shelf *s) {
    static const char *ids[] = {"sample-dhammapada", "sample-republic", "sample-varieties", "sample-cannon",
                                "sample-carmel"};
    int i;
    if (!s) return -1;
    for (i = 0; i < 5; i++) {
        Book b;
        const char *text;
        if (have_id(s, ids[i])) continue;
        text = shelf_sample_edition(ids[i]);
        memset(&b, 0, sizeof b);
        if (!text || book_read_mem(text, &b) != 0) {
            book_free(&b);
            return -1;
        }
        if (shelf_add(s, &b, NULL) != 0) {
            book_free(&b);
            return -1;
        }
        book_free(&b);
    }
    s->seeded = 1;
    return 0;
}
