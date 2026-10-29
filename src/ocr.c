#include "recto.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "font5x7.inc"

static int glyph_ink(int ch, int row) {
    if (ch < 32 || ch > 126) return 0;
    return font5x7[ch - 32][row];
}

int font_render(const char *text, int scale, Image *im) {
    int x, y, rows = 1, cols = 0, col = 0, gap, gw, gh, i;
    if (scale < 1) scale = 1;
    if (!text) text = "";
    gap = scale;
    gw = 5 * scale;
    gh = 7 * scale;
    for (i = 0; text[i]; i++) {
        if (text[i] == '\n') {
            rows++;
            if (col > cols) cols = col;
            col = 0;
        } else col++;
    }
    if (col > cols) cols = col;
    if (cols < 1) cols = 1;
    if (image_alloc(im, cols * (gw + gap) + scale * 2, rows * (gh + scale * 2) + scale * 2, 255) != 0)
        return -1;
    x = scale;
    y = scale;
    for (i = 0; text[i]; i++) {
        int ch = (unsigned char)text[i];
        int r, c;
        if (ch == '\n') {
            x = scale;
            y += gh + scale * 2;
            continue;
        }
        if (ch < 32 || ch > 126) ch = '?';
        for (r = 0; r < 7; r++) {
            int bits = glyph_ink(ch, r);
            for (c = 0; c < 5; c++) {
                if (bits & (0x10 >> c)) {
                    int yy, xx;
                    for (yy = 0; yy < scale; yy++) {
                        for (xx = 0; xx < scale; xx++) {
                            int px = x + c * scale + xx;
                            int py = y + r * scale + yy;
                            if (px >= 0 && py >= 0 && px < im->w && py < im->h)
                                im->px[(size_t)py * (size_t)im->w + px] = 0;
                        }
                    }
                }
            }
        }
        x += gw + gap;
    }
    return 0;
}

void ocr_page_free(OcrPage *p) {
    int i;
    if (!p) return;
    for (i = 0; i < p->n; i++) free(p->words[i].text);
    free(p->words);
    free(p->text);
    memset(p, 0, sizeof *p);
}

static void ocr_add_word(OcrPage *p, const char *text, int x, int y, int w, int h, int conf) {
    OcrWord *word;
    if (!text || !text[0]) return;
    if (p->n >= p->cap) {
        p->cap = p->cap ? p->cap * 2 : 16;
        p->words = xrealloc(p->words, (size_t)p->cap * sizeof *p->words);
    }
    word = &p->words[p->n++];
    word->text = xstrdup(text);
    word->x = x;
    word->y = y;
    word->w = w;
    word->h = h;
    word->conf = conf;
}

static int hamming_at(const Image *im, int x0, int y0, int bw, int bh, int ch) {
    int dist = 0, gy, gx;
    int pad_x0 = x0, pad_y0 = y0, pad_w = bw, pad_h = bh;
    if (bw < 1) bw = 1;
    if (bh < 1) bh = 1;
    /* Pad the tight blob out to a 5:7 cell so narrow letters keep their side margins. */
    if (bw * 7 > bh * 5) {
        int nh = bw * 7 / 5;
        if (nh < 1) nh = 1;
        pad_y0 = y0 - (nh - bh) / 2;
        pad_h = nh;
    } else {
        int nw = bh * 5 / 7;
        if (nw < 1) nw = 1;
        pad_x0 = x0 - (nw - bw) / 2;
        pad_w = nw;
    }
    if (pad_w < 1) pad_w = 1;
    if (pad_h < 1) pad_h = 1;
    for (gy = 0; gy < 7; gy++) {
        int bits = glyph_ink(ch, gy);
        int sy0 = pad_y0 + gy * pad_h / 7;
        int sy1 = pad_y0 + (gy + 1) * pad_h / 7;
        if (sy1 <= sy0) sy1 = sy0 + 1;
        for (gx = 0; gx < 5; gx++) {
            int sx0 = pad_x0 + gx * pad_w / 5;
            int sx1 = pad_x0 + (gx + 1) * pad_w / 5;
            int ink = 0, tot = 0, yy, xx, on;
            if (sx1 <= sx0) sx1 = sx0 + 1;
            for (yy = sy0; yy < sy1; yy++) {
                for (xx = sx0; xx < sx1; xx++) {
                    tot++;
                    if (xx >= 0 && yy >= 0 && xx < im->w && yy < im->h && im->px[(size_t)yy * (size_t)im->w + xx] < 128)
                        ink++;
                }
            }
            on = tot && ink * 2 >= tot;
            if (on != ((bits & (0x10 >> gx)) != 0)) dist++;
        }
    }
    return dist;
}

static int classify_blob(const Image *im, const Blob *b, int *conf) {
    int bw = b->x1 - b->x0 + 1;
    int bh = b->y1 - b->y0 + 1;
    int best_ch = '?', best = 99, ch;
    for (ch = 33; ch <= 126; ch++) {
        int d;
        if (ch == ' ') continue;
        d = hamming_at(im, b->x0, b->y0, bw, bh, ch);
        if (d < best) {
            best = d;
            best_ch = ch;
        }
    }
    if (best > 12) {
        *conf = 15;
        return '?';
    }
    *conf = 100 - best * 6;
    if (*conf < 0) *conf = 0;
    return best_ch;
}

static int blob_cmp(const void *a, const void *b) {
    const Blob *ba = a, *bb = b;
    int ca = (ba->y0 + ba->y1) / 2;
    int cb = (bb->y0 + bb->y1) / 2;
    if (ca != cb) return ca < cb ? -1 : 1;
    if (ba->x0 != bb->x0) return ba->x0 < bb->x0 ? -1 : 1;
    return 0;
}

static int median_int(int *v, int n) {
    int *c, i, m;
    if (n <= 0) return 0;
    c = xmalloc((size_t)n * sizeof(int));
    memcpy(c, v, (size_t)n * sizeof(int));
    for (i = 1; i < n; i++) {
        int k = c[i], j = i;
        while (j > 0 && c[j - 1] > k) {
            c[j] = c[j - 1];
            j--;
        }
        c[j] = k;
    }
    m = c[n / 2];
    free(c);
    return m;
}

int ocr_builtin(const Image *src, OcrPage *out) {
    Image im;
    Blob *blobs = NULL;
    int nblobs = 0, i, med_h, med_w;
    int *heights, *order;
    int nlines = 0;
    int *line_start = NULL;
    memset(out, 0, sizeof *out);
    snprintf(out->engine, sizeof out->engine, "builtin");
    if (!src || !src->px) return -1;
    if (image_alloc(&im, src->w, src->h, 255) != 0) return -1;
    memcpy(im.px, src->px, (size_t)src->w * (size_t)src->h);
    image_otsu(&im);
    if (image_blobs(&im, &blobs, &nblobs) != 0) {
        image_free(&im);
        return -1;
    }
    if (nblobs == 0) {
        out->text = xstrdup("");
        image_free(&im);
        free(blobs);
        return 0;
    }
    heights = xmalloc((size_t)nblobs * sizeof(int));
    for (i = 0; i < nblobs; i++) heights[i] = blobs[i].y1 - blobs[i].y0 + 1;
    med_h = median_int(heights, nblobs);
    {
        int *widths = xmalloc((size_t)nblobs * sizeof(int));
        for (i = 0; i < nblobs; i++) widths[i] = blobs[i].x1 - blobs[i].x0 + 1;
        med_w = median_int(widths, nblobs);
        free(widths);
    }
    if (med_h < 1) med_h = 1;
    if (med_w < 1) med_w = 1;
    {
        int wri = 0;
        for (i = 0; i < nblobs; i++) {
            int bh = blobs[i].y1 - blobs[i].y0 + 1;
            int bw = blobs[i].x1 - blobs[i].x0 + 1;
            if ((bh > med_h * 3 && bw > med_w * 2) || blobs[i].area > (im.w * im.h) / 8) {
                out->pictures++;
                continue;
            }
            blobs[wri++] = blobs[i];
        }
        nblobs = wri;
    }
    qsort(blobs, (size_t)nblobs, sizeof *blobs, blob_cmp);
    order = xmalloc((size_t)(nblobs > 0 ? nblobs : 1) * sizeof(int));
    line_start = xmalloc((size_t)(nblobs + 1) * sizeof(int));
    for (i = 0; i < nblobs; i++) order[i] = i;
    line_start[0] = 0;
    for (i = 1; i < nblobs; i++) {
        int cy = (blobs[i].y0 + blobs[i].y1) / 2;
        int py = (blobs[i - 1].y0 + blobs[i - 1].y1) / 2;
        if (abs(cy - py) > med_h * 6 / 10 && blobs[i].y0 > blobs[i - 1].y1 - med_h / 5)
            line_start[++nlines] = i;
    }
    line_start[++nlines] = nblobs;
    {
        int line, conf_sum = 0, conf_n = 0;
        for (line = 0; line < nlines; line++) {
            int begin = line_start[line];
            int end = line_start[line + 1];
            int k, j;
            int word_x = 0, word_y = 0, word_r = 0, word_b = 0, word_conf = 100;
            char glyph[160];
            int gn = 0, have = 0;
            int line_top = 0, line_bot = 0;
            for (k = begin + 1; k < end; k++) {
                int key = order[k];
                j = k;
                while (j > begin && blobs[order[j - 1]].x0 > blobs[key].x0) {
                    order[j] = order[j - 1];
                    j--;
                }
                order[j] = key;
            }
            if (begin >= end) continue;
            line_top = blobs[order[begin]].y0;
            line_bot = blobs[order[begin]].y1;
            for (k = begin; k < end; k++) {
                if (blobs[order[k]].y0 < line_top) line_top = blobs[order[k]].y0;
                if (blobs[order[k]].y1 > line_bot) line_bot = blobs[order[k]].y1;
            }
            for (k = begin; k < end; k++) {
                Blob *b = &blobs[order[k]];
                int gap = 0, ch = 0, conf = 0;
                int bw = b->x1 - b->x0 + 1;
                int bh = b->y1 - b->y0 + 1;
                if (k > begin) gap = b->x0 - blobs[order[k - 1]].x1 - 1;
                if (have && gap > med_w * 6 / 10) {
                    glyph[gn] = 0;
                    ocr_add_word(out, glyph, word_x, word_y, word_r - word_x + 1, word_b - word_y + 1, word_conf);
                    conf_sum += word_conf;
                    conf_n++;
                    gn = 0;
                    have = 0;
                    word_conf = 100;
                }
                if (bh * 2 < med_h && b->y0 > line_top + (line_bot - line_top) / 2) {
                    ch = (bh > bw) ? ',' : '.';
                    conf = 70;
                } else {
                    ch = classify_blob(&im, b, &conf);
                }
                if (ch && gn + 1 < (int)sizeof glyph) glyph[gn++] = (char)ch;
                if (!have) {
                    word_x = b->x0;
                    word_y = b->y0;
                    word_r = b->x1;
                    word_b = b->y1;
                    have = 1;
                }
                if (b->x1 > word_r) word_r = b->x1;
                if (b->y1 > word_b) word_b = b->y1;
                if (b->y0 < word_y) word_y = b->y0;
                if (conf < word_conf) word_conf = conf;
            }
            if (have && gn) {
                glyph[gn] = 0;
                ocr_add_word(out, glyph, word_x, word_y, word_r - word_x + 1, word_b - word_y + 1, word_conf);
                conf_sum += word_conf;
                conf_n++;
            }
        }
        out->conf = conf_n ? conf_sum / conf_n : 0;
    }
    {
        Buf text;
        int w, line_y = -100000;
        buf_init(&text);
        for (w = 0; w < out->n; w++) {
            if (w && abs(out->words[w].y - line_y) > med_h * 6 / 10) buf_putc(&text, '\n');
            else if (w) buf_putc(&text, ' ');
            buf_puts(&text, out->words[w].text);
            line_y = out->words[w].y;
        }
        out->text = buf_steal(&text);
    }
    free(line_start);
    free(order);
    free(heights);
    free(blobs);
    image_free(&im);
    return 0;
}

static int lang_ok(const char *lang) {
    const char *p;
    if (!lang || !lang[0]) return 0;
    for (p = lang; *p; p++) {
        unsigned char c = (unsigned char)*p;
        if (!(isalnum(c) || c == '+' || c == '_' || c == '-')) return 0;
    }
    return 1;
}

int ocr_tesseract_file(const char *path, const char *lang, OcrPage *out) {
    Buf cmd, raw;
    char *save, *line;
    int conf_sum = 0, conf_n = 0, cur_line = -1;
    if (!which_cmd("tesseract")) {
        recto_seterr("tesseract is not installed");
        return -1;
    }
    if (!lang_ok(lang)) lang = "eng";
    memset(out, 0, sizeof *out);
    snprintf(out->engine, sizeof out->engine, "tesseract");
    buf_init(&cmd);
    buf_init(&raw);
    buf_puts(&cmd, "tesseract ");
    sh_quote(&cmd, path);
    buf_puts(&cmd, " stdout tsv -l ");
    buf_puts(&cmd, lang);
    buf_puts(&cmd, " --psm 6 2>/dev/null");
    if (cmd_capture(cmd.p, &raw) != 0) {
        buf_free(&cmd);
        buf_free(&raw);
        recto_seterr("tesseract failed on %s", path);
        return -1;
    }
    buf_free(&cmd);
    save = raw.p;
    while (save && *save) {
        char *tab[16];
        int nt = 0;
        char *eol = strchr(save, '\n');
        if (eol) *eol = 0;
        line = save;
        if (line[0] && line[0] != 'l') { /* skip header starting with "level" */
            char *p = line;
            while (nt < 12) {
                tab[nt++] = p;
                p = strchr(p, '\t');
                if (!p) break;
                *p = 0;
                p++;
            }
            if (nt >= 12 && atoi(tab[0]) == 5 && tab[11][0]) {
                int left = atoi(tab[6]);
                int top = atoi(tab[7]);
                int wid = atoi(tab[8]);
                int hei = atoi(tab[9]);
                int conf = atoi(tab[10]);
                int line_no = atoi(tab[4]);
                if (conf < 0) conf = 0;
                ocr_add_word(out, tab[11], left, top, wid, hei, conf);
                conf_sum += conf;
                conf_n++;
                (void)line_no;
                (void)cur_line;
            }
        }
        if (!eol) break;
        save = eol + 1;
    }
    {
        Buf text;
        int w, line_y = -100000, med_h = 12;
        buf_init(&text);
        for (w = 0; w < out->n; w++) {
            if (w && abs(out->words[w].y - line_y) > med_h) buf_putc(&text, '\n');
            else if (w) buf_putc(&text, ' ');
            buf_puts(&text, out->words[w].text);
            line_y = out->words[w].y;
        }
        out->text = buf_steal(&text);
    }
    out->conf = conf_n ? conf_sum / conf_n : 0;
    buf_free(&raw);
    return 0;
}

static int count_letters(const char *s) {
    int n = 0;
    if (!s) return 0;
    for (; *s; s++)
        if (isalpha((unsigned char)*s)) n++;
    return n;
}

static int prepare_image(const Image *src, const Settings *set, Image *dst) {
    size_t i, n;
    unsigned long sum = 0;
    if (image_alloc(dst, src->w, src->h, 255) != 0) return -1;
    memcpy(dst->px, src->px, (size_t)src->w * (size_t)src->h);
    image_fit(dst, 2000);
    n = (size_t)dst->w * (size_t)dst->h;
    for (i = 0; i < n; i++) sum += dst->px[i];
    if (n && sum / n < 110) image_invert(dst);
    if (!set || set->contrast) image_contrast(dst);
    if (!set || set->deskew) image_deskew(dst);
    return 0;
}

int ocr_image(const Image *src, const Settings *set, OcrPage *out) {
    Settings local;
    Image im;
    int engine;
    char pgm[RECTO_PATH];
    if (!set) {
        settings_default(&local);
        set = &local;
    }
    memset(out, 0, sizeof *out);
    if (prepare_image(src, set, &im) != 0) return -1;
    engine = set->engine;
    if (engine == ENG_BUILTIN || engine == ENG_BEST) {
        if (ocr_builtin(&im, out) != 0) {
            image_free(&im);
            return -1;
        }
        if (engine == ENG_BUILTIN || (out->conf >= 60 && count_letters(out->text) >= 4)) {
            image_free(&im);
            return 0;
        }
    }
    if (engine == ENG_TESS || engine == ENG_BEST) {
        OcrPage tess;
        if (which_cmd("tesseract")) {
            temp_path(pgm, sizeof pgm, ".pgm");
            if (image_save_pgm(&im, pgm) == 0 && ocr_tesseract_file(pgm, set->scripts[0] ? set->scripts : "eng", &tess) == 0) {
                if (engine == ENG_TESS || tess.conf >= out->conf || count_letters(tess.text) > count_letters(out->text)) {
                    ocr_page_free(out);
                    *out = tess;
                } else ocr_page_free(&tess);
            }
            remove(pgm);
            if (engine == ENG_TESS) {
                image_free(&im);
                return out->text ? 0 : -1;
            }
            if (out->conf >= 55 && count_letters(out->text) >= 4) {
                image_free(&im);
                return 0;
            }
        } else if (engine == ENG_TESS) {
            image_free(&im);
            recto_seterr("tesseract is not installed");
            return -1;
        }
    }
    if ((engine == ENG_VISION || engine == ENG_BEST) && set->vision) {
        OcrPage vis;
        if (vision_ocr(&im, set, &vis) == 0) {
            if (engine == ENG_VISION || count_letters(vis.text) > count_letters(out->text)) {
                ocr_page_free(out);
                *out = vis;
            } else ocr_page_free(&vis);
        } else if (engine == ENG_VISION) {
            image_free(&im);
            return -1;
        }
    }
    image_free(&im);
    if (!out->text) out->text = xstrdup("");
    return 0;
}
