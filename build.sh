#!/bin/sh
cd "$(dirname "$0")" || exit 1
# WebUI 2.5.0-beta.4 (browser window, no WebView). Linux C core only.
tcc -o recto \
    -Ithird_party/webui/include -Ithird_party/webui/src \
    -DNDEBUG -DNO_CACHING -DNO_CGI -DUSE_WEBSOCKET -DNO_SSL \
    src/util.c src/image.c src/ocr.c src/clean.c src/edition.c \
    src/pdf.c src/shelf.c src/epub.c src/vision.c src/open.c \
    src/ui.c src/gui.c src/test.c src/main.c \
    third_party/webui/src/civetweb/civetweb.c \
    third_party/webui/src/webui.c \
    -lz -lpthread -lm -ldl "$@"
