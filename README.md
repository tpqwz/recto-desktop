# Recto

Recto converts PDF, DjVu, HTML, plain text, Markdown, and page images to EPUB. Printed page numbers and the contents are kept. A reading edition can be checked afterwards (contents, page Go, find) in the terminal or in a browser window. It is in the spirit of [rect.grok.me](https://rect.grok.me).

The program is C99 and builds with Tiny C Compiler. With no arguments it prints help. `recto FILE` writes an EPUB beside the file. `recto gui` opens the shelf in a browser window, using [WebUI](https://github.com/webui-dev/webui) 2.5.0-beta.4 (MIT, sources in `third_party/webui`). Tiny C Compiler builds that C core on Linux and links it with pthread, libm, and libdl. The Windows WebView in WebUI is C++, so `build.bat` stays a line-mode build and `recto gui` explains that.

## Build

```sh
./build.sh
```

That runs `tcc` over `src/*.c` and the WebUI C sources, then links zlib, pthread, libm, and libdl. zlib is used for PNG, FlateDecode, and EPUB checksums. If zlib headers are missing, add `-DRECTO_NO_ZLIB`; compressed PDF streams, PNG, and the vision request body will not work.

On Windows, `build.bat` produces `recto.exe`. The Windows build converts on the command line and has a line-mode desk. Directory import is implemented for POSIX.

## Convert

```
recto convert INPUT... OUTPUT.epub
recto convert INPUT... FOLDER
recto INPUT
recto INPUT OUTPUT.epub
recto gui
recto desk
```

The last argument of `recto convert` is the `.epub` file or a folder. One `.epub` file is one book. A folder receives one EPUB per book, named from the title. With no output path, the EPUB is written beside the source (`notes.pdf` becomes `notes.epub`). A path that already exists as a directory is a folder of EPUBs, and so is a path that ends in `/` or `\`. A folder of page images is one book, in natural filename order. Each PDF or DjVu is its own book. An `.epub` input is refused. A command-line convert does not add the book to the shelf. A convert in the window does, so the edition can be checked.

`recto desk` opens the terminal shelf. `recto gui` opens the same shelf in the browser. The shelf lives in `$RECTO_SHELF` or `~/.recto`. The first empty shelf receives five short sample editions. Removing them does not bring them back; `recto sample` does.

```
recto open FILE|FOLDER...
recto books
recto show ID
recto page ID LABEL
recto find ID WORDS
recto export ID FILE.epub
recto ocr FILE [--page N] [--rect x,y,w,h] [--engine best|builtin|tesseract|vision]
recto remove ID --yes
recto settings [key=value]
recto library
recto test
```

The window has a convert form, Open for one or more files, the shelf of converted editions, the library, contents with a filter, page Go, find, settings, a two-step remove, the reading place, restore, and reread of a page or a crop. Several chosen files are one batch. With the output box empty, each EPUB is written beside its source. A folder in the output box receives the whole batch. Write EPUB saves the edition again after a reread. An EPUB path that is not absolute is written in the shelf directory. Page numbers accept arabic and roman labels (`14`, `xiv`).

In the desk: `j`/`k` move, Enter opens, `o` adds a file, `l` is the library, `,` is settings, `s` restores samples, `d` twice removes a book. Inside a book: `n`/`p` turn the page, `g` goes to a printed page, `/` finds, `c` is the contents, `e` exports, `r` rereads the page, `x` reads a crop (`x y w h`), `b` returns to the shelf.

## Reading pictures

`pictures` decides when a page is read:

- `empty` (the default) reads a page only when it has no text
- `unread` leaves a scan unread
- `all` rereads a page whose text layer is thinner than the picture read

`engine=best` tries the built-in reader, then Tesseract if it is installed (`scripts`, default `eng`), then a vision model when `vision` is on and the earlier read is thin. The built-in reader matches a 5x7 Latin face. It is there so a scan can be tried with no extra install. It is not a substitute for Tesseract on a real book face; a low score is how Recto hands the page on.

`recto ocr` can read a file, one PDF page, or a rectangle. `--engine builtin` forces the built-in reader. Tesseract, when installed, is one pass over whatever languages you name. `djvutxt` reads DjVu text. `pdftotext` and `pdftoppm` are used when the native PDF reader cannot, or when a page has to be rasterized. `ddjvu` rasterizes a DjVu page.

Vision calls `https://api.x.ai/v1/responses` with model `grok-4.7` (or `vision_model`) and `store: false`. It needs `XAI_API_KEY` and `curl`. The picture is sent as a PNG data URL. The original file is copied onto the shelf only when `keep_file` is on. Rereading a page uses the path recorded at import, so the file still has to be there.

Other switches: `contrast`, `deskew`, `columns` (`auto`, `one`, `two`).

`recto test` checks roman page numbers, the built-in reader, edition cleanup, the shelf, a hand-built PDF (plain and FlateDecode, with page labels), conversion of PDF, HTML, and Markdown, and EPUB packaging. It uses a temporary shelf and does not touch `~/.recto`.
