#include "recto.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifndef RECTO_NO_ZLIB
#include <zlib.h>
#endif

static void xml_escape(Buf *b, const char *s) {
    if (!s) return;
    for (; *s; s++) {
        if (*s == '&') buf_puts(b, "&amp;");
        else if (*s == '<') buf_puts(b, "&lt;");
        else if (*s == '>') buf_puts(b, "&gt;");
        else if (*s == '"') buf_puts(b, "&quot;");
        else buf_putc(b, *s);
    }
}

static void put_u16(Buf *b, unsigned v) {
    unsigned char d[2];
    d[0] = (unsigned char)(v & 255);
    d[1] = (unsigned char)((v >> 8) & 255);
    buf_append(b, d, 2);
}

static void put_u32(Buf *b, unsigned v) {
    unsigned char d[4];
    d[0] = (unsigned char)(v & 255);
    d[1] = (unsigned char)((v >> 8) & 255);
    d[2] = (unsigned char)((v >> 16) & 255);
    d[3] = (unsigned char)((v >> 24) & 255);
    buf_append(b, d, 4);
}

static unsigned dos_time(void) {
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    unsigned timev, datev;
    if (!t) return 0;
    timev = (unsigned)((t->tm_hour << 11) | (t->tm_min << 5) | (t->tm_sec / 2));
    datev = (unsigned)(((t->tm_year - 80) << 9) | ((t->tm_mon + 1) << 5) | t->tm_mday);
    return timev | (datev << 16);
}

static void zip_store(Buf *out, const char *name, const char *data, size_t n, unsigned stamp, unsigned *offset, Buf *central,
                      int *count) {
    unsigned crc;
    size_t namelen = strlen(name);
#ifdef RECTO_NO_ZLIB
    /* A stored ZIP still needs a CRC. Compute it here when zlib is absent. */
    unsigned c = 0xFFFFFFFFu;
    size_t i;
    for (i = 0; i < n; i++) {
        int k;
        c ^= (unsigned char)data[i];
        for (k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
    }
    crc = c ^ 0xFFFFFFFFu;
#else
    crc = (unsigned)crc32(0L, (const Bytef *)data, (uInt)n);
#endif
    *offset = (unsigned)out->n;
    put_u32(out, 0x04034b50u);
    put_u16(out, 20);
    put_u16(out, 0);
    put_u16(out, 0); /* store */
    put_u16(out, stamp & 0xFFFF);
    put_u16(out, (stamp >> 16) & 0xFFFF);
    put_u32(out, crc);
    put_u32(out, (unsigned)n);
    put_u32(out, (unsigned)n);
    put_u16(out, (unsigned)namelen);
    put_u16(out, 0);
    buf_append(out, name, namelen);
    if (n) buf_append(out, data, n);

    put_u32(central, 0x02014b50u);
    put_u16(central, 20);
    put_u16(central, 20);
    put_u16(central, 0);
    put_u16(central, 0);
    put_u16(central, stamp & 0xFFFF);
    put_u16(central, (stamp >> 16) & 0xFFFF);
    put_u32(central, crc);
    put_u32(central, (unsigned)n);
    put_u32(central, (unsigned)n);
    put_u16(central, (unsigned)namelen);
    put_u16(central, 0);
    put_u16(central, 0);
    put_u16(central, 0);
    put_u16(central, 0);
    put_u32(central, 0);
    put_u32(central, *offset);
    buf_append(central, name, namelen);
    (*count)++;
}

static const char *STYLE =
    "body{font-family:Georgia,Palatino,serif;line-height:1.45;margin:0;padding:0 8%;color:#1c1915}"
    "h1,h2{font-weight:normal}h1{text-align:center}p{margin:0 0 .35em;text-indent:1.15em}"
    "p.note{font-size:.9em;text-indent:0;margin-top:1em}p.noindent{text-indent:0}"
    "table{border-collapse:collapse;margin:1em 0}td{border:1px solid #c8c0b4;padding:.3em .5em;vertical-align:top}"
    "span.pagebreak{display:block;text-align:center;color:#8a8175;margin:1.2em 0 .4em;font-size:.8em}";

int epub_write(const Book *b, const char *path) {
    Buf xhtml, opf, ncx, nav, out, central;
    int i, count = 0;
    unsigned stamp = dos_time();
    unsigned off = 0;
    const char *title = b->title[0] ? b->title : "Untitled";
    const char *lang = b->language[0] ? b->language : "en";
    const char *last_label = "";
    buf_init(&xhtml);
    buf_init(&opf);
    buf_init(&ncx);
    buf_init(&nav);
    buf_init(&out);
    buf_init(&central);

    buf_puts(&xhtml,
             "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
             "<html xmlns=\"http://www.w3.org/1999/xhtml\" xmlns:epub=\"http://www.idpf.org/2007/ops\" xml:lang=\"");
    xml_escape(&xhtml, lang);
    buf_puts(&xhtml, "\"><head><title>");
    xml_escape(&xhtml, title);
    buf_puts(&xhtml, "</title><link rel=\"stylesheet\" href=\"style.css\"/></head><body>\n");
    for (i = 0; i < b->nblocks; i++) {
        const Block *blk = &b->blocks[i];
        if (strcmp(last_label, blk->label) != 0) {
            buf_puts(&xhtml, "<span epub:type=\"pagebreak\" id=\"page-");
            xml_escape(&xhtml, blk->label);
            buf_puts(&xhtml, "\" class=\"pagebreak\">");
            xml_escape(&xhtml, blk->label);
            buf_puts(&xhtml, "</span>\n");
            last_label = blk->label;
        }
        if (blk->kind == BLK_H1 || blk->kind == BLK_H2) {
            buf_printf(&xhtml, "<%s id=\"b%d\">", blk->kind == BLK_H1 ? "h1" : "h2", i);
            xml_escape(&xhtml, blk->text);
            buf_printf(&xhtml, "</%s>\n", blk->kind == BLK_H1 ? "h1" : "h2");
        } else if (blk->kind == BLK_TABLE) {
            const char *p = blk->text ? blk->text : "";
            buf_puts(&xhtml, "<table>\n");
            while (*p) {
                const char *nl = strchr(p, '\n');
                size_t n = nl ? (size_t)(nl - p) : strlen(p);
                const char *cell = p;
                buf_puts(&xhtml, "<tr>");
                while (cell < p + n) {
                    const char *bar = cell;
                    size_t cn;
                    while (bar + 2 < p + n && !(bar[0] == ' ' && bar[1] == '|' && bar[2] == ' ')) bar++;
                    if (bar + 2 < p + n && bar[0] == ' ' && bar[1] == '|' && bar[2] == ' ') cn = (size_t)(bar - cell);
                    else {
                        cn = (size_t)(p + n - cell);
                        bar = p + n;
                    }
                    buf_puts(&xhtml, "<td>");
                    {
                        char *tmp = xstrndup(cell, cn);
                        xml_escape(&xhtml, tmp);
                        free(tmp);
                    }
                    buf_puts(&xhtml, "</td>");
                    if (bar >= p + n) break;
                    cell = bar + 3;
                }
                buf_puts(&xhtml, "</tr>\n");
                if (!nl) break;
                p = nl + 1;
            }
            buf_puts(&xhtml, "</table>\n");
        } else {
            buf_printf(&xhtml, "<p class=\"%s\">", blk->kind == BLK_NOTE ? "note" : "noindent");
            xml_escape(&xhtml, blk->text);
            buf_puts(&xhtml, "</p>\n");
        }
    }
    buf_puts(&xhtml, "</body></html>\n");

    buf_puts(&nav,
             "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
             "<html xmlns=\"http://www.w3.org/1999/xhtml\" xmlns:epub=\"http://www.idpf.org/2007/ops\"><head><title>Contents</title></head><body>\n"
             "<nav epub:type=\"toc\"><h1>Contents</h1><ol>\n");
    for (i = 0; i < b->ntoc; i++) {
        buf_printf(&nav, "<li><a href=\"text/book.xhtml#b%d\">", b->toc[i].block);
        xml_escape(&nav, b->toc[i].title);
        buf_puts(&nav, "</a></li>\n");
    }
    buf_puts(&nav, "</ol></nav>\n<nav epub:type=\"page-list\" hidden=\"hidden\"><ol>\n");
    last_label = "";
    for (i = 0; i < b->nblocks; i++) {
        if (strcmp(last_label, b->blocks[i].label) == 0) continue;
        last_label = b->blocks[i].label;
        buf_printf(&nav, "<li><a href=\"text/book.xhtml#page-%s\">", last_label);
        xml_escape(&nav, last_label);
        buf_puts(&nav, "</a></li>\n");
    }
    buf_puts(&nav, "</ol></nav></body></html>\n");

    buf_puts(&ncx,
             "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
             "<ncx xmlns=\"http://www.daisy.org/z3986/2005/ncx/\" version=\"2005-1\"><head>"
             "<meta name=\"dtb:uid\" content=\"urn:uuid:recto\"/></head><docTitle><text>");
    xml_escape(&ncx, title);
    buf_puts(&ncx, "</text></docTitle><navMap>\n");
    for (i = 0; i < b->ntoc; i++) {
        buf_printf(&ncx, "<navPoint id=\"n%d\" playOrder=\"%d\"><navLabel><text>", i + 1, i + 1);
        xml_escape(&ncx, b->toc[i].title);
        buf_printf(&ncx, "</text></navLabel><content src=\"text/book.xhtml#b%d\"/></navPoint>\n", b->toc[i].block);
    }
    buf_puts(&ncx, "</navMap></ncx>\n");

    buf_puts(&opf,
             "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
             "<package xmlns=\"http://www.idpf.org/2007/opf\" unique-identifier=\"bookid\" version=\"3.0\">\n"
             "<metadata xmlns:dc=\"http://purl.org/dc/elements/1.1/\">\n"
             "<dc:identifier id=\"bookid\">urn:uuid:recto</dc:identifier>\n<dc:title>");
    xml_escape(&opf, title);
    buf_puts(&opf, "</dc:title>\n<dc:language>");
    xml_escape(&opf, lang);
    buf_puts(&opf, "</dc:language>\n");
    if (b->creator[0]) {
        buf_puts(&opf, "<dc:creator>");
        xml_escape(&opf, b->creator);
        buf_puts(&opf, "</dc:creator>\n");
    }
    if (b->source[0]) {
        buf_puts(&opf, "<dc:source>");
        xml_escape(&opf, b->source);
        buf_puts(&opf, "</dc:source>\n");
    }
    buf_puts(&opf, "<dc:rights>");
    xml_escape(&opf, b->rights[0] ? b->rights : "Personal reading copy. Page numbers follow the file.");
    buf_puts(&opf,
             "</dc:rights>\n<meta property=\"dcterms:modified\">2026-01-01T00:00:00Z</meta>\n</metadata>\n<manifest>\n"
             "<item id=\"nav\" href=\"nav.xhtml\" media-type=\"application/xhtml+xml\" properties=\"nav\"/>\n"
             "<item id=\"ncx\" href=\"toc.ncx\" media-type=\"application/x-dtbncx+xml\"/>\n"
             "<item id=\"css\" href=\"style.css\" media-type=\"text/css\"/>\n"
             "<item id=\"book\" href=\"text/book.xhtml\" media-type=\"application/xhtml+xml\"/>\n"
             "</manifest>\n<spine toc=\"ncx\"><itemref idref=\"book\"/></spine>\n</package>\n");

    zip_store(&out, "mimetype", "application/epub+zip", 20, stamp, &off, &central, &count);
    zip_store(&out, "META-INF/container.xml",
              "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
              "<container version=\"1.0\" xmlns=\"urn:oasis:names:tc:opendocument:xmlns:container\">"
              "<rootfiles><rootfile full-path=\"OEBPS/content.opf\" media-type=\"application/oebps-package+xml\"/>"
              "</rootfiles></container>\n",
              strlen("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                     "<container version=\"1.0\" xmlns=\"urn:oasis:names:tc:opendocument:xmlns:container\">"
                     "<rootfiles><rootfile full-path=\"OEBPS/content.opf\" media-type=\"application/oebps-package+xml\"/>"
                     "</rootfiles></container>\n"),
              stamp, &off, &central, &count);
    zip_store(&out, "OEBPS/content.opf", opf.p, opf.n, stamp, &off, &central, &count);
    zip_store(&out, "OEBPS/toc.ncx", ncx.p, ncx.n, stamp, &off, &central, &count);
    zip_store(&out, "OEBPS/nav.xhtml", nav.p, nav.n, stamp, &off, &central, &count);
    zip_store(&out, "OEBPS/style.css", STYLE, strlen(STYLE), stamp, &off, &central, &count);
    zip_store(&out, "OEBPS/text/book.xhtml", xhtml.p, xhtml.n, stamp, &off, &central, &count);
    {
        unsigned cd = (unsigned)out.n;
        buf_append(&out, central.p, central.n);
        put_u32(&out, 0x06054b50u);
        put_u16(&out, 0);
        put_u16(&out, 0);
        put_u16(&out, (unsigned)count);
        put_u16(&out, (unsigned)count);
        put_u32(&out, (unsigned)central.n);
        put_u32(&out, cd);
        put_u16(&out, 0);
    }
    {
        int rc = file_write(path, out.p, out.n);
        buf_free(&xhtml);
        buf_free(&opf);
        buf_free(&ncx);
        buf_free(&nav);
        buf_free(&out);
        buf_free(&central);
        return rc;
    }
}
