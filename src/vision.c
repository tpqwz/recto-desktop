#include "recto.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *B64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static void b64(const unsigned char *in, size_t n, Buf *out) {
    size_t i;
    for (i = 0; i < n; i += 3) {
        unsigned v = (unsigned)in[i] << 16;
        int rem = 1;
        if (i + 1 < n) {
            v |= (unsigned)in[i + 1] << 8;
            rem = 2;
        }
        if (i + 2 < n) {
            v |= in[i + 2];
            rem = 3;
        }
        buf_putc(out, B64[(v >> 18) & 63]);
        buf_putc(out, B64[(v >> 12) & 63]);
        buf_putc(out, rem >= 2 ? B64[(v >> 6) & 63] : '=');
        buf_putc(out, rem >= 3 ? B64[v & 63] : '=');
    }
}

static void json_escape(Buf *b, const char *s) {
    for (; s && *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\') {
            buf_putc(b, '\\');
            buf_putc(b, c);
        } else if (c == '\n') buf_puts(b, "\\n");
        else if (c < 32) buf_printf(b, "\\u%04x", c);
        else buf_putc(b, c);
    }
}

int vision_build_request(const Image *im, const char *prompt, const char *model, Buf *json) {
    Buf png, enc;
    buf_init(&png);
    buf_init(&enc);
    buf_clear(json);
    if (image_write_png(im, &png) != 0) {
        buf_free(&png);
        buf_free(&enc);
        return -1;
    }
    b64((unsigned char *)png.p, png.n, &enc);
    buf_puts(json, "{\"model\":\"");
    json_escape(json, model && model[0] ? model : "grok-4.7");
    buf_puts(json,
             "\",\"store\":false,\"input\":[{\"role\":\"user\",\"content\":["
             "{\"type\":\"input_image\",\"image_url\":\"data:image/png;base64,");
    buf_append(json, enc.p, enc.n);
    buf_puts(json, "\",\"detail\":\"high\"},{\"type\":\"input_text\",\"text\":\"");
    json_escape(json, prompt ? prompt : "");
    buf_puts(json, "\"}]}]}\n");
    buf_free(&png);
    buf_free(&enc);
    return 0;
}

static char *json_unescape(const char *s, size_t n) {
    Buf b;
    size_t i;
    buf_init(&b);
    for (i = 0; i < n; i++) {
        if (s[i] == '\\' && i + 1 < n) {
            char c = s[++i];
            if (c == 'n') buf_putc(&b, '\n');
            else if (c == 't') buf_putc(&b, '\t');
            else if (c == 'r') buf_putc(&b, '\r');
            else if (c == 'u' && i + 4 < n) {
                int k, cp = 0;
                for (k = 0; k < 4; k++) {
                    char h = s[++i];
                    cp <<= 4;
                    if (h >= '0' && h <= '9') cp += h - '0';
                    else if (h >= 'a' && h <= 'f') cp += h - 'a' + 10;
                    else if (h >= 'A' && h <= 'F') cp += h - 'A' + 10;
                }
                if (cp < 128) buf_putc(&b, cp);
                else buf_putc(&b, '?');
            } else buf_putc(&b, c);
        } else buf_putc(&b, s[i]);
    }
    return buf_steal(&b);
}

static char *json_string_after(const char *json, const char *marker) {
    const char *p = strstr(json, marker);
    const char *q, *e;
    if (!p) return NULL;
    q = strchr(p + strlen(marker), '"');
    /* Prefer the next "text" string after the marker. */
    {
        const char *t = strstr(p, "\"text\"");
        if (t) {
            t = strchr(t + 6, '"');
            if (t) q = t + 1;
        }
    }
    if (!q || !*q) return NULL;
    if (*q == '"') q++;
    e = q;
    while (*e && *e != '"') {
        if (*e == '\\' && e[1]) e += 2;
        else e++;
    }
    return json_unescape(q, (size_t)(e - q));
}

int vision_ocr(const Image *im, const Settings *set, OcrPage *out) {
    static const char *prompt =
        "Transcribe every word visible in this image. Keep the reading order. "
        "Rejoin a word split only by a hyphen at the end of a line. "
        "Leave out running headers and bare page numbers. Keep headings. "
        "Put footnote text on its own lines. Render a table as rows of cells separated by \" | \". "
        "Do not add words that are not visible. If the image has no text, return an empty transcript.";
    const char *key = getenv("XAI_API_KEY");
    char req[RECTO_PATH], hdr[RECTO_PATH], resp[RECTO_PATH];
    Buf json, body, cmd;
    char *text;
    if (!key || !key[0]) {
        recto_seterr("vision reading needs XAI_API_KEY");
        return -1;
    }
    if (!which_cmd("curl")) {
        recto_seterr("vision reading needs curl");
        return -1;
    }
    memset(out, 0, sizeof *out);
    snprintf(out->engine, sizeof out->engine, "vision");
    buf_init(&json);
    buf_init(&body);
    buf_init(&cmd);
    if (vision_build_request(im, prompt, set && set->vision_model[0] ? set->vision_model : "grok-4.7", &json) != 0) {
        buf_free(&json);
        buf_free(&body);
        buf_free(&cmd);
        return -1;
    }
    temp_path(req, sizeof req, ".json");
    temp_path(hdr, sizeof hdr, ".hdr");
    temp_path(resp, sizeof resp, ".out");
    if (file_write(req, json.p, json.n) != 0) {
        buf_free(&json);
        buf_free(&body);
        buf_free(&cmd);
        return -1;
    }
    {
        Buf h;
        buf_init(&h);
        buf_puts(&h, "Authorization: Bearer ");
        buf_puts(&h, key);
        buf_putc(&h, '\n');
        buf_puts(&h, "Content-Type: application/json\n");
        file_write(hdr, h.p, h.n);
        buf_free(&h);
    }
    buf_puts(&cmd, "curl -sS --max-time 120 https://api.x.ai/v1/responses -H @");
    sh_quote(&cmd, hdr);
    buf_puts(&cmd, " --data-binary @");
    sh_quote(&cmd, req);
    buf_puts(&cmd, " -o ");
    sh_quote(&cmd, resp);
    if (cmd_capture(cmd.p, &body) != 0 && !file_exists(resp)) {
        remove(req);
        remove(hdr);
        recto_seterr("vision request failed");
        buf_free(&json);
        buf_free(&body);
        buf_free(&cmd);
        return -1;
    }
    buf_free(&body);
    buf_init(&body);
    if (file_read(resp, &body) != 0) {
        remove(req);
        remove(hdr);
        remove(resp);
        buf_free(&json);
        buf_free(&cmd);
        return -1;
    }
    text = json_string_after(body.p ? body.p : "", "output_text");
    if (!text) {
        recto_seterr("vision response had no transcript");
        remove(req);
        remove(hdr);
        remove(resp);
        buf_free(&json);
        buf_free(&body);
        buf_free(&cmd);
        return -1;
    }
    out->text = text;
    out->conf = text[0] ? 80 : 0;
    out->words = xmalloc(sizeof *out->words);
    out->n = 1;
    out->cap = 1;
    out->words[0].text = xstrdup(text);
    out->words[0].x = 0;
    out->words[0].y = 0;
    out->words[0].w = im->w;
    out->words[0].h = im->h;
    out->words[0].conf = out->conf;
    snprintf(out->engine, sizeof out->engine, "vision");
    remove(req);
    remove(hdr);
    remove(resp);
    buf_free(&json);
    buf_free(&body);
    buf_free(&cmd);
    return 0;
}
