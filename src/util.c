#include "recto.h"

#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <direct.h>
#include <io.h>
#define recto_access _access
#define recto_unlink _unlink
#define recto_popen _popen
#define recto_pclose _pclose
#define RECTO_F_OK 0
#ifndef PATH_SEP
#define PATH_SEP '\\'
#endif
#else
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#define recto_access access
#define recto_unlink unlink
#define recto_popen popen
#define recto_pclose pclose
#define RECTO_F_OK F_OK
#ifndef PATH_SEP
#define PATH_SEP '/'
#endif
#endif

char recto_err[RECTO_ERR];

void recto_seterr(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(recto_err, sizeof recto_err, fmt, ap);
    va_end(ap);
}

void settings_default(Settings *s) {
    memset(s, 0, sizeof *s);
    s->pictures = PIC_EMPTY;
    s->contrast = 1;
    s->deskew = 1;
    s->columns = COL_AUTO;
    s->engine = ENG_BEST;
    s->vision = 0;
    snprintf(s->scripts, sizeof s->scripts, "eng");
    snprintf(s->vision_model, sizeof s->vision_model, "grok-4.7");
}

void *xmalloc(size_t n) {
    void *p;
    if (n == 0) n = 1;
    p = malloc(n);
    if (!p) {
        fprintf(stderr, "recto: out of memory (%lu bytes)\n", (unsigned long)n);
        exit(1);
    }
    return p;
}

void *xrealloc(void *p, size_t n) {
    void *q;
    if (n == 0) n = 1;
    q = realloc(p, n);
    if (!q) {
        fprintf(stderr, "recto: out of memory (%lu bytes)\n", (unsigned long)n);
        exit(1);
    }
    return q;
}

char *xstrdup(const char *s) {
    size_t n;
    char *d;
    if (!s) s = "";
    n = strlen(s);
    d = xmalloc(n + 1);
    memcpy(d, s, n + 1);
    return d;
}

char *xstrndup(const char *s, size_t n) {
    char *d;
    size_t i;
    if (!s) s = "";
    for (i = 0; i < n && s[i]; i++) {
    }
    d = xmalloc(i + 1);
    memcpy(d, s, i);
    d[i] = 0;
    return d;
}

void buf_init(Buf *b) {
    b->p = NULL;
    b->n = 0;
    b->cap = 0;
}

void buf_free(Buf *b) {
    free(b->p);
    b->p = NULL;
    b->n = 0;
    b->cap = 0;
}

void buf_clear(Buf *b) {
    b->n = 0;
    if (b->p && b->cap) b->p[0] = 0;
}

void buf_reserve(Buf *b, size_t need) {
    size_t cap;
    if (need <= b->cap) return;
    cap = b->cap ? b->cap : 64;
    while (cap < need) {
        if (cap > (size_t)-1 / 2) {
            cap = need;
            break;
        }
        cap *= 2;
    }
    b->p = xrealloc(b->p, cap);
    b->cap = cap;
}

void buf_append(Buf *b, const void *p, size_t n) {
    buf_reserve(b, b->n + n + 1);
    if (n && p) memcpy(b->p + b->n, p, n);
    b->n += n;
    b->p[b->n] = 0;
}

void buf_puts(Buf *b, const char *s) {
    if (!s) return;
    buf_append(b, s, strlen(s));
}

void buf_putc(Buf *b, int c) {
    char ch = (char)c;
    buf_append(b, &ch, 1);
}

void buf_printf(Buf *b, const char *fmt, ...) {
    va_list ap;
    int need;
    va_start(ap, fmt);
    need = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (need < 0) return;
    buf_reserve(b, b->n + (size_t)need + 1);
    va_start(ap, fmt);
    vsnprintf(b->p + b->n, b->cap - b->n, fmt, ap);
    va_end(ap);
    b->n += (size_t)need;
}

char *buf_steal(Buf *b) {
    char *p;
    buf_reserve(b, b->n + 1);
    b->p[b->n] = 0;
    p = b->p;
    b->p = NULL;
    b->n = 0;
    b->cap = 0;
    if (!p) p = xstrdup("");
    return p;
}

int file_read(const char *path, Buf *b) {
    FILE *f;
    long sz;
    buf_clear(b);
    f = fopen(path, "rb");
    if (!f) {
        recto_seterr("cannot open %s", path);
        return -1;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        recto_seterr("cannot measure %s", path);
        return -1;
    }
    sz = ftell(f);
    if (sz < 0) {
        fclose(f);
        recto_seterr("cannot measure %s", path);
        return -1;
    }
    if ((unsigned long)sz > 256ul * 1024ul * 1024ul) {
        fclose(f);
        recto_seterr("%s is larger than 256 MB", path);
        return -1;
    }
    rewind(f);
    buf_reserve(b, (size_t)sz + 1);
    if (sz && fread(b->p, 1, (size_t)sz, f) != (size_t)sz) {
        fclose(f);
        recto_seterr("short read on %s", path);
        return -1;
    }
    b->n = (size_t)sz;
    b->p[b->n] = 0;
    fclose(f);
    return 0;
}

int file_write(const char *path, const void *p, size_t n) {
    FILE *f = fopen(path, "wb");
    if (!f) {
        recto_seterr("cannot write %s", path);
        return -1;
    }
    if (n && fwrite(p, 1, n, f) != n) {
        fclose(f);
        recto_seterr("short write on %s", path);
        return -1;
    }
    if (fclose(f) != 0) {
        recto_seterr("cannot finish %s", path);
        return -1;
    }
    return 0;
}

int file_exists(const char *path) {
    return path && recto_access(path, RECTO_F_OK) == 0;
}

int file_is_dir(const char *path) {
#ifdef _WIN32
    struct _stat st;
    if (_stat(path, &st) != 0) return 0;
    return (st.st_mode & _S_IFDIR) != 0;
#else
    struct stat st;
    if (stat(path, &st) != 0) return 0;
    return S_ISDIR(st.st_mode);
#endif
}

int dir_make(const char *path) {
#ifdef _WIN32
    if (_mkdir(path) == 0 || errno == EEXIST) return 0;
#else
    if (mkdir(path, 0755) == 0 || errno == EEXIST) return 0;
#endif
    if (file_is_dir(path)) return 0;
    recto_seterr("cannot create %s", path);
    return -1;
}

int dir_make_p(const char *path) {
    char tmp[RECTO_PATH];
    size_t i, n;
    if (!path || !path[0]) return -1;
    n = strlen(path);
    if (n >= sizeof tmp) {
        recto_seterr("path too long");
        return -1;
    }
    memcpy(tmp, path, n + 1);
    for (i = 1; i < n; i++) {
        if (tmp[i] == '/' || tmp[i] == '\\') {
            tmp[i] = 0;
            if (tmp[0] && dir_make(tmp) != 0) return -1;
            tmp[i] = PATH_SEP;
        }
    }
    return dir_make(tmp);
}

int dir_list(const char *path, char ***names, int *nout) {
    char **list = NULL;
    int n = 0, cap = 0;
#ifdef _WIN32
    /* Windows directory listing is filled in by the Win32 build.
       This file still compiles there once FindFirstFile is wired;
       the Linux desk is the tested path. */
    (void)path;
    recto_seterr("directory listing needs a POSIX host in this build");
    *names = NULL;
    *nout = 0;
    return -1;
#else
    DIR *d = opendir(path);
    struct dirent *de;
    if (!d) {
        recto_seterr("cannot list %s", path);
        return -1;
    }
    while ((de = readdir(d)) != NULL) {
        if (de->d_name[0] == '.') continue;
        if (n >= cap) {
            cap = cap ? cap * 2 : 16;
            list = xrealloc(list, (size_t)cap * sizeof *list);
        }
        list[n++] = xstrdup(de->d_name);
    }
    closedir(d);
    *names = list;
    *nout = n;
    return 0;
#endif
}

void dir_list_free(char **names, int n) {
    int i;
    for (i = 0; i < n; i++) free(names[i]);
    free(names);
}

char *path_join(const char *a, const char *b) {
    Buf buf;
    size_t na;
    buf_init(&buf);
    buf_puts(&buf, a ? a : "");
    na = buf.n;
    if (na && a[na - 1] != '/' && a[na - 1] != '\\') buf_putc(&buf, '/');
    buf_puts(&buf, b ? b : "");
    return buf_steal(&buf);
}

const char *path_base(const char *p) {
    const char *s = p ? p : "";
    const char *slash = s;
    for (; *s; s++) {
        if (*s == '/' || *s == '\\') slash = s + 1;
    }
    return slash;
}

char *str_trim(char *s) {
    char *e;
    if (!s) return s;
    while (*s && isspace((unsigned char)*s)) s++;
    if (!*s) return s;
    e = s + strlen(s);
    while (e > s && isspace((unsigned char)e[-1])) e--;
    *e = 0;
    return s;
}

int str_ieq(const char *a, const char *b) {
    if (!a || !b) return 0;
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return 0;
        a++;
        b++;
    }
    return *a == 0 && *b == 0;
}

int str_istart(const char *s, const char *pfx) {
    if (!s || !pfx) return 0;
    while (*pfx) {
        if (tolower((unsigned char)*s) != tolower((unsigned char)*pfx)) return 0;
        s++;
        pfx++;
    }
    return 1;
}

int str_icontains(const char *s, const char *q) {
    size_t nq;
    if (!s || !q || !q[0]) return 0;
    nq = strlen(q);
    for (; *s; s++) {
        size_t i;
        for (i = 0; i < nq; i++) {
            if (!s[i]) return 0;
            if (tolower((unsigned char)s[i]) != tolower((unsigned char)q[i])) break;
        }
        if (i == nq) return 1;
    }
    return 0;
}

void str_tolower(char *s) {
    for (; s && *s; s++) *s = (char)tolower((unsigned char)*s);
}

static int roman_val(int c) {
    switch (tolower(c)) {
    case 'i': return 1;
    case 'v': return 5;
    case 'x': return 10;
    case 'l': return 50;
    case 'c': return 100;
    case 'd': return 500;
    case 'm': return 1000;
    default: return 0;
    }
}

int roman_parse(const char *s) {
    int total = 0, prev = 0, cur, i, any = 0;
    if (!s || !s[0]) return 0;
    for (i = (int)strlen(s) - 1; i >= 0; i--) {
        cur = roman_val((unsigned char)s[i]);
        if (!cur) return 0;
        any = 1;
        if (cur < prev) total -= cur;
        else {
            total += cur;
            prev = cur;
        }
    }
    if (!any || total <= 0 || total > 4000) return 0;
    return total;
}

void roman_format(int n, int upper, char *out, size_t cap) {
    static const int v[] = {1000, 900, 500, 400, 100, 90, 50, 40, 10, 9, 5, 4, 1};
    static const char *r[] = {"m", "cm", "d", "cd", "c", "xc", "l", "xl", "x", "ix", "v", "iv", "i"};
    size_t o = 0;
    int i;
    if (cap == 0) return;
    out[0] = 0;
    if (n <= 0 || n > 4000) return;
    for (i = 0; i < 13; i++) {
        while (n >= v[i]) {
            const char *p = r[i];
            while (*p && o + 1 < cap) {
                out[o++] = upper ? (char)toupper((unsigned char)*p) : *p;
                p++;
            }
            n -= v[i];
        }
    }
    out[o] = 0;
}

int is_int_token(const char *s) {
    if (!s || !s[0]) return 0;
    if (*s == '+' || *s == '-') s++;
    if (!*s) return 0;
    while (*s) {
        if (!isdigit((unsigned char)*s)) return 0;
        s++;
    }
    return 1;
}

int natcmp(const char *a, const char *b) {
    if (!a) a = "";
    if (!b) b = "";
    while (*a && *b) {
        if (isdigit((unsigned char)*a) && isdigit((unsigned char)*b)) {
            unsigned long na, nb;
            char *ea, *eb;
            na = strtoul(a, &ea, 10);
            nb = strtoul(b, &eb, 10);
            if (na != nb) return na < nb ? -1 : 1;
            a = ea;
            b = eb;
        } else {
            unsigned char ca = (unsigned char)tolower((unsigned char)*a);
            unsigned char cb = (unsigned char)tolower((unsigned char)*b);
            if (ca != cb) return ca < cb ? -1 : 1;
            a++;
            b++;
        }
    }
    if (*a) return 1;
    if (*b) return -1;
    return 0;
}

int which_cmd(const char *name) {
    const char *path, *p;
    char buf[RECTO_PATH];
    size_t nl;
    if (!name || !name[0]) return 0;
    if (strchr(name, '/') || strchr(name, '\\')) return file_exists(name);
    path = getenv("PATH");
    if (!path) return 0;
    nl = strlen(name);
    p = path;
    while (*p) {
        const char *colon = p;
        size_t n;
        while (*colon && *colon != ':'
#ifdef _WIN32
               && *colon != ';'
#endif
        ) colon++;
        n = (size_t)(colon - p);
        if (n + 1 + nl + 1 < sizeof buf) {
            memcpy(buf, p, n);
            buf[n] = '/';
            memcpy(buf + n + 1, name, nl + 1);
#ifdef _WIN32
            {
                char exe[RECTO_PATH];
                snprintf(exe, sizeof exe, "%s.exe", buf);
                if (file_exists(buf) || file_exists(exe)) return 1;
            }
#else
            if (file_exists(buf) && access(buf, X_OK) == 0) return 1;
#endif
        }
        if (!*colon) break;
        p = colon + 1;
    }
    return 0;
}

void sh_quote(Buf *b, const char *s) {
    buf_putc(b, '\'');
    if (!s) s = "";
    for (; *s; s++) {
        if (*s == '\'') buf_puts(b, "'\\''");
        else buf_putc(b, *s);
    }
    buf_putc(b, '\'');
}

int cmd_capture(const char *cmd, Buf *out) {
    FILE *f;
    char tmp[4096];
    size_t n;
    buf_clear(out);
    f = recto_popen(cmd, "r");
    if (!f) {
        recto_seterr("cannot run command");
        return -1;
    }
    while ((n = fread(tmp, 1, sizeof tmp, f)) > 0) buf_append(out, tmp, n);
    if (recto_pclose(f) != 0 && out->n == 0) {
        recto_seterr("command failed");
        return -1;
    }
    return 0;
}

int temp_path(char *out, size_t cap, const char *suffix) {
    static unsigned seq;
    unsigned r;
    seq++;
    r = (unsigned)time(NULL) ^ (seq * 2654435761u);
#ifdef _WIN32
    snprintf(out, cap, "%s\\recto-%u%s", getenv("TEMP") ? getenv("TEMP") : ".", r, suffix ? suffix : "");
#else
    snprintf(out, cap, "/tmp/recto-%u%s", r, suffix ? suffix : "");
#endif
    return 0;
}
