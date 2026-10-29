@echo off
rem The browser desk is the Linux build. WebUI's Windows file includes a C++ WebView.
tcc -o recto.exe src\util.c src\image.c src\ocr.c src\clean.c src\edition.c src\pdf.c src\shelf.c src\epub.c src\vision.c src\open.c src\ui.c src\gui.c src\test.c src\main.c -lz %*
