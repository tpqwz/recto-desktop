#include "recto.h"

#ifdef _WIN32

#include <stdio.h>

int gui_call(Shelf *shelf, const char *op, const char *a, const char *b, const char *c, Buf *out) {
    (void)shelf;
    (void)op;
    (void)a;
    (void)b;
    (void)c;
    (void)out;
    recto_seterr("the browser desk is built with Tiny C on Linux");
    return -1;
}

int gui_run(Shelf *shelf) {
    (void)shelf;
    recto_seterr("the browser desk needs WebUI, and Tiny C Compiler cannot build its Windows WebView");
    return -1;
}

int gui_http_probe(void) { return 0; }

#else

#include "webui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static Shelf *gui_shelf;

static const char GUI_HTML[] =
    "<!DOCTYPE html><html><head><meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width, initial-scale=1'>"
    "<title>Recto</title><script src='webui.js'></script><style>"
    ":root { --bg:#e6dfd2; --paper:#fbf7f0; --ink:#221e1a; --muted:#6f675e; --line:#d4cdc1; --seal:#7d2e28; --shelf:#efe8dc; }"
    "* { box-sizing:border-box; } html, body { height:100%; margin:0; }"
    "body { font-family:'Segoe UI', system-ui, sans-serif; color:var(--ink); background:var(--bg);"
    " display:grid; grid-template-rows:auto 1fr auto; min-height:100%; }"
    "header, footer { display:flex; gap:10px; align-items:center; flex-wrap:wrap; padding:10px 14px; background:var(--paper); }"
    "header { border-bottom:1px solid var(--line); } footer { border-top:1px solid var(--line); color:var(--muted); min-height:2.4rem; }"
    ".brand { font-family:Palatino, 'Palatino Linotype', serif; font-size:1.45rem; letter-spacing:0.04em; margin-right:8px; }"
    ".sub { color:var(--muted); font-size:0.85rem; margin-right:8px; }"
    ".convert { display:flex; gap:6px; flex:1 1 100%; } .convert input { flex:1; min-width:8rem; }"
    ".convert button, #open-form button { flex:0 0 auto; }"
    "header form, .tools, .page-bar { display:flex; gap:8px; align-items:center; flex-wrap:wrap; }"
    "input, select, button { font:inherit; color:inherit; background:#fff; border:1px solid var(--line); border-radius:4px; padding:6px 8px; }"
    "button { cursor:pointer; background:#f3eee6; } button.on { background:#f6e7e2; color:var(--seal); }"
    ".desk { display:grid; grid-template-columns:250px minmax(0,1fr) 240px; min-height:0; }"
    "aside, main { min-height:0; overflow:auto; } aside { background:var(--shelf); padding:12px; }"
    "aside.left { border-right:1px solid var(--line); } aside.right { border-left:1px solid var(--line); }"
    ".rail-h { font-size:0.75rem; letter-spacing:0.12em; text-transform:uppercase; color:var(--muted); margin:0 0 8px; }"
    "#shelf button, #toc button, #hits button { display:flex; width:100%; text-align:left; margin:0 0 6px;"
    " flex-direction:column; align-items:flex-start; }"
    ".meta, .toc-page, .muted, #edition { color:var(--muted); font-size:0.85rem; }"
    "#toc button { flex-direction:row; justify-content:space-between; gap:8px; } .toc-item.on { background:#f6e7e2; color:var(--seal); }"
    "main { padding:18px 22px 32px; }"
    ".page-bar, .tools, #hits, #edition, article { max-width:40rem; margin-left:auto; margin-right:auto; }"
    ".page-bar { margin-bottom:12px; } .page-bar h1 { font:1.15rem Palatino, 'Palatino Linotype', serif; margin:0; flex:1; }"
    "article { background:var(--paper); border:1px solid var(--line); padding:2.2rem 2.4rem 2.6rem;"
    " font-family:Palatino, 'Iowan Old Style', 'Palatino Linotype', serif; font-size:1.12rem; line-height:1.55;"
    " box-shadow:0 1px 0 rgba(34,30,26,0.04); }"
    "article h1 { font-size:1.45rem; font-weight:normal; color:var(--seal); margin:0 0 0.8rem; }"
    "article h2 { font-size:1.2rem; font-weight:normal; margin:1rem 0 0.4rem; } article p { margin:0 0 0.85rem; }"
    "article .note { font-size:0.95rem; border-left:2px solid var(--line); padding-left:0.8rem; }"
    "article .table { font-family:ui-monospace, monospace; font-size:0.92rem; white-space:pre-wrap; }"
    ".tools, #hits { margin-top:12px; margin-bottom:12px; }"
    "#open-form { display:flex; gap:6px; margin-top:12px; flex-wrap:wrap; } #open-form input { flex:1 1 100%; min-width:0; } #toc-filter { width:100%; }"
    "#crop { width:7.5rem; } #export-path { width:12rem; }"
    "#toc-filter { margin-bottom:8px; }"
    "#view-library table { width:100%; border-collapse:collapse; background:var(--paper); }"
    "#view-library th, #view-library td { border-bottom:1px solid var(--line); text-align:left; padding:8px; vertical-align:top; }"
    "#view-library tr { cursor:pointer; }"
    ".set-row { display:flex; justify-content:space-between; gap:12px; align-items:center; max-width:36rem;"
    " padding:8px 0; border-bottom:1px solid var(--line); }"
    ".note-set { max-width:36rem; color:var(--muted); } [hidden] { display:none !important; }"
    "@media (max-width:860px) { .desk { grid-template-columns:1fr; } aside { max-height:240px; border:0; border-bottom:1px solid var(--line); } }"
    "</style></head><body>"
    "<header><div class='brand'>Recto</div><span class='sub'>to EPUB</span>"
    "<form id='convert-form' class='convert'><button id='btn-open' type='button'>Open</button>"
    "<input id='convert-src' placeholder='Files, or a folder of pages'>"
    "<input id='convert-out' placeholder='out.epub or a folder'><button>Convert</button></form>"
    "<form id='find-form'><input id='find' placeholder='Find in the book'><button>Find</button></form>"
    "<form id='go-form'><input id='go' placeholder='Page' size='8'><button>Go</button></form>"
    "<button id='btn-library' type='button'>Library</button>"
    "<button id='btn-settings' type='button'>Settings</button>"
    "<button id='btn-samples' type='button'>Samples</button></header>"
    "<div class='desk'><aside class='left'><div class='rail-h'>Converted</div><div id='shelf'></div>"
    "<form id='open-form'><input id='open-path' placeholder='File or folder'><button>Convert</button>"
    "<button id='btn-open-side' type='button'>Open</button></form></aside>"
    "<main><div id='view-read'><div class='page-bar'><button id='prev' type='button'>Previous</button>"
    "<h1 id='doc-title'>Recto</h1><button id='next' type='button'>Next</button></div>"
    "<p id='edition'></p><div id='hits'></div><article id='page'></article>"
    "<div class='tools'><button id='reread' type='button'>Reread page</button>"
    "<input id='crop' placeholder='x y w h'>"
    "<button id='restore' type='button'>Restore</button>"
    "<input id='export-path' placeholder='book.epub'>"
    "<button id='export' type='button'>Write EPUB</button>"
    "<button id='remove' type='button'>Remove</button></div></div>"
    "<div id='view-library' hidden></div><div id='view-settings' hidden></div></main>"
    "<aside class='right'><div class='rail-h'>Contents</div>"
    "<input id='toc-filter' placeholder='Filter contents'><div id='toc'></div></aside></div>"
    "<footer id='status'>Convert a PDF, or another text file.</footer><script>"
    "var state = {shelf: [], book: null, block: 0, hits: null, armed: '', paths: []};"
    "var started = false;"
    "function el(tag, cls, text) { var node = document.createElement(tag); if (cls) node.className = cls;"
    " if (text != null) node.textContent = text; return node; }"
    "function clear(node) { while (node.firstChild) node.removeChild(node.firstChild); }"
    "function status(text) { document.getElementById('status').textContent = text || ''; }"
    "function api(op, a, b, c) {"
    " if (typeof recto_api !== 'function') return Promise.reject(new Error('The desk is not connected.'));"
    " return recto_api(op || '', a || '', b || '', c || '').then(function (raw) {"
    " var o = JSON.parse(raw); if (!o.ok) throw new Error(o.error || 'The desk could not do that.'); return o; }); }"
    "function showView(name) {"
    " document.getElementById('view-read').hidden = name !== 'read';"
    " document.getElementById('view-library').hidden = name !== 'library';"
    " document.getElementById('view-settings').hidden = name !== 'settings'; }"
    "function pagesOf(book) { var pages = [], i, block, key, cur = null;"
    " if (!book || !book.blocks) return pages;"
    " for (i = 0; i < book.blocks.length; i++) { block = book.blocks[i]; key = block.label || String(block.page);"
    " if (!cur || cur.key !== key) { cur = {key: key, page: block.page, index: i, blocks: []}; pages.push(cur); }"
    " cur.blocks.push(block); } return pages; }"
    "function currentPage(book) { var pages = pagesOf(book), i, page, last;"
    " for (i = 0; i < pages.length; i++) { page = pages[i]; last = page.index + page.blocks.length - 1;"
    " if (state.block >= page.index && state.block <= last) return {pages: pages, at: i}; }"
    " return {pages: pages, at: 0}; }"
    "function counts(book) { var parts = []; function add(n, word) { if (n) parts.push(n + ' ' + word); }"
    " add(book.chapters, 'chapters'); add(book.hyphens, 'hyphens rejoined'); add(book.headers, 'running heads');"
    " add(book.tables, 'tables'); add(book.notes, 'notes'); return parts.join(' / '); }"
    "function renderShelf() { var box = document.getElementById('shelf'); clear(box);"
    " if (!state.shelf.length) { box.appendChild(el('p', 'muted', 'No conversions yet.')); return; }"
    " state.shelf.forEach(function (book) { var btn = document.createElement('button'); btn.type = 'button';"
    " btn.className = state.book && state.book.id === book.id ? 'on' : '';"
    " btn.appendChild(el('span', 'title', book.title || '(untitled)'));"
    " btn.appendChild(el('span', 'meta', (book.creator ? book.creator + ' / ' : '') + (book.pages || 0) + ' p.'));"
    " btn.addEventListener('click', function () { openBook(book.id); }); box.appendChild(btn); }); }"
    "function renderHits() { var box = document.getElementById('hits'); clear(box); if (state.hits == null) return;"
    " if (!state.hits.length) { box.appendChild(el('p', 'muted', 'Not found.')); return; }"
    " state.hits.forEach(function (hit) { var btn = document.createElement('button'); btn.type = 'button';"
    " btn.textContent = 'p. ' + (hit.label || hit.page) + '  ' + (hit.snippet || '');"
    " btn.addEventListener('click', function () { showIndex(hit.block); showView('read'); }); box.appendChild(btn); }); }"
    "function renderToc() { var box = document.getElementById('toc'); var q = document.getElementById('toc-filter').value.trim().toLowerCase();"
    " var info, page; clear(box);"
    " if (!state.book || !state.book.toc || !state.book.toc.length) {"
    " box.appendChild(el('p', 'muted', 'This book has no contents entries.')); return; }"
    " info = currentPage(state.book); page = info.pages[info.at];"
    " state.book.toc.forEach(function (item) { var title = item.title || '';"
    " if (q && title.toLowerCase().indexOf(q) < 0 && String(item.label || '').toLowerCase().indexOf(q) < 0) return;"
    " var on = page && item.block >= page.index && item.block < page.index + page.blocks.length;"
    " var btn = document.createElement('button'); btn.type = 'button'; btn.className = 'toc-item' + (on ? ' on' : '');"
    " btn.style.paddingLeft = (8 + (item.level > 1 ? 14 : 0)) + 'px';"
    " btn.appendChild(el('span', 'toc-title', title)); btn.appendChild(el('span', 'toc-page', item.label || ''));"
    " btn.addEventListener('click', function () { showIndex(item.block); showView('read'); }); box.appendChild(btn); }); }"
    "function renderPage() { var book = state.book; var info, page, line;"
    " var article = document.getElementById('page'); clear(article);"
    " if (!book) { document.getElementById('doc-title').textContent = 'Recto';"
    " document.getElementById('edition').textContent = '';"
    " article.appendChild(el('p', 'muted', 'Convert a file to see the edition.')); return; }"
    " info = currentPage(book); page = info.pages[info.at];"
    " document.getElementById('doc-title').textContent = book.title || 'Untitled';"
    " line = page ? ('p. ' + page.key) : '';"
    " if (book.creator) line += (line ? ' / ' : '') + book.creator;"
    " var extra = counts(book); if (extra) line += (line ? ' / ' : '') + extra;"
    " document.getElementById('edition').textContent = line;"
    " if (!page || !page.blocks.length) article.appendChild(el('p', 'muted', 'This book has no text yet.'));"
    " else page.blocks.forEach(function (block) {"
    " var tag = block.k === 'h1' || block.k === 'h2' ? block.k : 'p';"
    " article.appendChild(el(tag, block.k || 'p', block.text || '')); }); }"
    "function renderLibrary() { var box = document.getElementById('view-library'); var table = document.createElement('table');"
    " var head = document.createElement('tr'); clear(box);"
    " ['Title', 'Creator', 'Work', 'Expression', 'Volume', 'Date'].forEach(function (name) { head.appendChild(el('th', '', name)); });"
    " table.appendChild(head);"
    " (state.shelf || []).forEach(function (book) { var tr = document.createElement('tr');"
    " [book.title || '(untitled)', book.creator || '', book.work || '', book.expression || '', book.volume || '', book.date || '']"
    " .forEach(function (cell) { tr.appendChild(el('td', '', cell)); });"
    " tr.addEventListener('click', function () { openBook(book.id); }); table.appendChild(tr); }); box.appendChild(table); }"
    "function parseSet(text) { var out = {}; String(text || '').split('\\n').forEach(function (line) {"
    " var i = line.indexOf('='); if (i > 0) out[line.slice(0, i)] = line.slice(i + 1); }); return out; }"
    "function selectOf(values, current) { var sel = document.createElement('select');"
    " values.forEach(function (v) { var opt = document.createElement('option'); opt.value = v; opt.textContent = v;"
    " if (v === current) opt.selected = true; sel.appendChild(opt); }); return sel; }"
    "function checkOf(on) { var input = document.createElement('input'); input.type = 'checkbox'; input.checked = !!on; return input; }"
    "function textOf(value) { var input = document.createElement('input'); input.type = 'text'; input.value = value || ''; return input; }"
    "function row(label, control) { var lab = document.createElement('label'); lab.className = 'set-row';"
    " lab.appendChild(el('span', '', label)); lab.appendChild(control); return lab; }"
    "function renderSettings() { api('settings').then(function (o) { var s = parseSet(o.text);"
    " var box = document.getElementById('view-settings'); var pics, keep, contrast, deskew, cols, eng, vision, scripts, model, save;"
    " clear(box);"
    " box.appendChild(el('p', 'note-set', 'empty reads a page only when it has no text. unread leaves a scan unread. all rereads a page whose text is thinner than the picture. best tries the built-in reader, then Tesseract, then a vision model when vision is on.'));"
    " pics = selectOf(['unread', 'empty', 'all'], s.pictures);"
    " keep = checkOf(s.keep_file === '1'); contrast = checkOf(s.contrast === '1'); deskew = checkOf(s.deskew === '1');"
    " cols = selectOf(['auto', 'one', 'two'], s.columns); eng = selectOf(['best', 'builtin', 'tesseract', 'vision'], s.engine);"
    " vision = checkOf(s.vision === '1'); scripts = textOf(s.scripts); model = textOf(s.vision_model);"
    " box.appendChild(row('Pictures', pics)); box.appendChild(row('Keep the original file', keep));"
    " box.appendChild(row('Contrast', contrast)); box.appendChild(row('Deskew', deskew));"
    " box.appendChild(row('Columns', cols)); box.appendChild(row('Reader', eng));"
    " box.appendChild(row('Vision model allowed', vision)); box.appendChild(row('Scripts', scripts));"
    " box.appendChild(row('Vision model', model));"
    " save = document.createElement('button'); save.type = 'button'; save.textContent = 'Save settings';"
    " save.addEventListener('click', function () { var pairs = ['pictures=' + pics.value, 'keep_file=' + (keep.checked ? '1' : '0'),"
    " 'contrast=' + (contrast.checked ? '1' : '0'), 'deskew=' + (deskew.checked ? '1' : '0'), 'columns=' + cols.value,"
    " 'engine=' + eng.value, 'vision=' + (vision.checked ? '1' : '0'), 'scripts=' + scripts.value, 'vision_model=' + model.value];"
    " var chain = Promise.resolve(); pairs.forEach(function (pair) { chain = chain.then(function () { return api('set', pair); }); });"
    " chain.then(function () { status('Settings saved.'); }).catch(function (err) { status(err.message); }); });"
    " box.appendChild(save); }).catch(function (err) { status(err.message); }); }"
    "function showIndex(index) { state.block = index; renderPage(); renderToc();"
    " if (!state.book) return; api('place', state.book.id, String(index)).catch(function () {}); }"
    "function showBook(book) { state.book = book; state.hits = null; state.armed = '';"
    " state.block = book.blocks && book.place_block >= 0 && book.place_block < book.blocks.length ? book.place_block : 0;"
    " document.getElementById('remove').textContent = 'Remove';"
    " document.getElementById('export-path').value = (book.id || 'book') + '.epub';"
    " document.getElementById('toc-filter').value = ''; renderShelf(); renderPage(); renderToc(); renderHits();"
    " if (book.reader_note) status(book.reader_note); }"
    "function openBook(id, note) { return api('book', id).then(function (book) { showBook(book); showView('read');"
    " if (note) status(note); }).catch(function (err) { status(err.message); }); }"
    "function refreshShelf(o, note) { state.shelf = o.books || []; renderShelf(); if (note) status(note); }"
    "function waitFor(pred) { return new Promise(function (resolve, reject) { var n = 0; (function tick() {"
    " if (pred()) resolve(); else if (n++ > 40) reject(new Error('the page did not change')); else setTimeout(tick, 50); })(); }); }"
    "function runCheck() { return api('book', 'sample-dhammapada').then(function (book) { showBook(book); showView('read');"
    " document.getElementById('go').value = 'iv';"
    " document.getElementById('go-form').dispatchEvent(new Event('submit', {bubbles: true, cancelable: true}));"
    " return waitFor(function () { return (document.getElementById('edition').textContent || '').indexOf('p. 4') >= 0; });"
    " }).then(function () { document.getElementById('btn-library').click();"
    " if (document.getElementById('view-library').hidden) throw new Error('library did not open');"
    " document.getElementById('btn-settings').click();"
    " if (document.getElementById('view-settings').hidden) throw new Error('settings did not open');"
    " document.getElementById('find').value = 'hatred';"
    " document.getElementById('find-form').dispatchEvent(new Event('submit', {bubbles: true, cancelable: true}));"
    " return waitFor(function () { return !!document.querySelector('#hits button'); }); }).then(function () {"
    " document.querySelector('#hits button').click(); status('recto-check-ok'); document.title = 'recto-check-ok';"
    " return api('check', 'ok'); }).catch(function (err) { status('recto-check-fail'); document.title = 'recto-check-fail';"
    " return api('check', 'fail ' + (err && err.message ? err.message : '')); }); }"
    "function boot() { api('shelf').then(function (o) { state.shelf = o.books || []; renderShelf(); renderPage();"
    " if (location.hash === '#check') return runCheck(); status('Convert a PDF, or another text file.'); })"
    " .catch(function (err) { status(err.message); }); }"
    "function begin() { if (started) return; started = true; boot(); }"
    "function arm() { if (!globalThis.webui) { setTimeout(arm, 20); return; }"
    " webui.setEventCallback(function (e) { if (e === webui.event.CONNECTED) begin();"
    " else if (e === webui.event.DISCONNECTED) status('The window disconnected.'); });"
    " if (webui.isConnected()) begin(); }"
    "document.getElementById('find-form').addEventListener('submit', function (ev) { ev.preventDefault();"
    " if (!state.book) { status('Open a book first.'); return; }"
    " api('find', state.book.id, document.getElementById('find').value).then(function (o) {"
    " state.hits = o.hits || []; renderHits(); status(state.hits.length ? state.hits.length + ' found.' : 'Not found.'); })"
    " .catch(function (err) { status(err.message); }); });"
    "document.getElementById('go-form').addEventListener('submit', function (ev) { ev.preventDefault();"
    " if (!state.book) { status('Open a book first.'); return; }"
    " api('goto', state.book.id, document.getElementById('go').value).then(function (o) { showIndex(o.block); showView('read'); })"
    " .catch(function (err) { status(err.message); }); });"
    "function pathList(src) { var typed = String(src || '').trim();"
    " if (state.paths.length > 1 && typed === (state.paths.length + ' files')) return state.paths;"
    " if (!typed) return [];"
    " if (typed.indexOf('\\n') >= 0) return typed.split(/\\r?\\n/).map(function (s) { return s.trim(); }).filter(Boolean);"
    " return [typed]; }"
    "function doConvert(src, dest, clearId) {"
    " var list = pathList(src);"
    " if (!list.length) { status('Convert needs a file or a folder.'); return; }"
    " status(list.length > 1 ? ('Converting ' + list.length + ' files.') : 'Converting.');"
    " api('convert', list.join('\\n'), dest || '').then(function (o) {"
    " if (clearId) { document.getElementById(clearId).value = ''; if (clearId === 'convert-src') state.paths = []; }"
    " var files = o.files || []; var ids = o.ids || [];"
    " var note = files.length === 1 ? ('Wrote ' + files[0]) : ('Wrote ' + files.length + ' epubs.');"
    " return api('shelf').then(function (shelf) { refreshShelf(shelf, note); if (ids[0]) return openBook(ids[0], note); }); })"
    " .catch(function (err) { status(err.message); }); }"
    "function openFiles() { status('Choose files.');"
    " api('pick').then(function (o) { var files = o.files || []; var box;"
    " if (!files.length) { status('No files chosen.'); return; }"
    " state.paths = files; box = document.getElementById('convert-src');"
    " box.value = files.length === 1 ? files[0] : (files.length + ' files');"
    " doConvert(box.value, document.getElementById('convert-out').value, null); })"
    " .catch(function (err) { status(err.message); }); }"
    "document.getElementById('btn-open').addEventListener('click', openFiles);"
    "document.getElementById('btn-open-side').addEventListener('click', openFiles);"
    "document.getElementById('convert-form').addEventListener('submit', function (ev) { ev.preventDefault();"
    " doConvert(document.getElementById('convert-src').value, document.getElementById('convert-out').value, 'convert-src'); });"
    "document.getElementById('open-form').addEventListener('submit', function (ev) { ev.preventDefault();"
    " doConvert(document.getElementById('open-path').value, '', 'open-path'); });"
    "document.getElementById('btn-samples').addEventListener('click', function () {"
    " api('sample').then(function (o) { refreshShelf(o, 'Samples are on the shelf.'); }).catch(function (err) { status(err.message); }); });"
    "document.getElementById('btn-library').addEventListener('click', function () { renderLibrary(); showView('library'); });"
    "document.getElementById('btn-settings').addEventListener('click', function () { showView('settings'); renderSettings(); });"
    "document.getElementById('toc-filter').addEventListener('input', renderToc);"
    "document.getElementById('prev').addEventListener('click', function () { var info, nxt; if (!state.book) return;"
    " info = currentPage(state.book); nxt = info.at - 1; if (nxt >= 0) showIndex(info.pages[nxt].index); });"
    "document.getElementById('next').addEventListener('click', function () { var info, nxt; if (!state.book) return;"
    " info = currentPage(state.book); nxt = info.at + 1; if (nxt < info.pages.length) showIndex(info.pages[nxt].index); });"
    "document.getElementById('reread').addEventListener('click', function () { var info, page, crop;"
    " if (!state.book) { status('Open a book first.'); return; } info = currentPage(state.book);"
    " page = info.pages[info.at] ? info.pages[info.at].page : 1; crop = document.getElementById('crop').value.trim();"
    " status('Reading the page.'); api('reread', state.book.id, String(page), crop).then(function (o) {"
    " status(o.note || 'Page reread.'); return api('book', state.book.id); }).then(showBook)"
    " .catch(function (err) { status(err.message); }); });"
    "document.getElementById('restore').addEventListener('click', function () {"
    " if (!state.book) return; api('restore', state.book.id).then(function () { return api('book', state.book.id); })"
    " .then(function (book) { showBook(book); status('Restored the previous revision.'); })"
    " .catch(function (err) { status(err.message); }); });"
    "document.getElementById('export').addEventListener('click', function () {"
    " if (!state.book) return; api('export', state.book.id, document.getElementById('export-path').value)"
    " .then(function (o) { status('Exported ' + (o.path || '')); }).catch(function (err) { status(err.message); }); });"
    "document.getElementById('remove').addEventListener('click', function () { var id = state.book && state.book.id; if (!id) return;"
    " if (state.armed !== id) { state.armed = id; document.getElementById('remove').textContent = 'Remove this book';"
    " status('Say remove again to confirm.'); return; }"
    " api('remove', id, 'yes').then(function (o) { state.book = null; state.armed = ''; state.hits = null;"
    " document.getElementById('remove').textContent = 'Remove'; renderPage(); renderToc(); renderHits();"
    " refreshShelf(o, 'Removed.'); }).catch(function (err) { status(err.message); }); });"
    "arm();"
    "</script></body></html>";

static const char *arg_or_empty(const char *s) { return s ? s : ""; }

static void jesc(Buf *b, const char *s) {
    const unsigned char *p = (const unsigned char *)(s ? s : "");
    buf_putc(b, '"');
    for (; *p; p++) {
        if (*p == '"' || *p == '\\') {
            buf_putc(b, '\\');
            buf_putc(b, *p);
        } else if (*p == '\n') buf_puts(b, "\\n");
        else if (*p == '\r') buf_puts(b, "\\r");
        else if (*p == '\t') buf_puts(b, "\\t");
        else if (*p < 0x20) buf_printf(b, "\\u%04x", *p);
        else buf_putc(b, *p);
    }
    buf_putc(b, '"');
}

static int fail(Buf *out, const char *msg) {
    char tmp[RECTO_ERR];
    snprintf(tmp, sizeof tmp, "%s", msg && msg[0] ? msg : "the desk could not do that");
    recto_seterr("%s", tmp);
    buf_clear(out);
    buf_puts(out, "{\"ok\":false,\"error\":");
    jesc(out, tmp);
    buf_puts(out, "}");
    return -1;
}

static void jok(Buf *out) {
    buf_clear(out);
    buf_puts(out, "{\"ok\":true}");
}

static const char *kind_name(int kind) {
    if (kind == BLK_H1) return "h1";
    if (kind == BLK_H2) return "h2";
    if (kind == BLK_NOTE) return "note";
    if (kind == BLK_TABLE) return "table";
    if (kind == BLK_LI) return "li";
    return "p";
}

static void jfield(Buf *b, const char *key, const char *val) {
    buf_putc(b, '"');
    buf_puts(b, key);
    buf_puts(b, "\":");
    jesc(b, val);
}

static void shelf_json(const Shelf *s, Buf *out) {
    int i;
    buf_clear(out);
    buf_puts(out, "{\"ok\":true,\"books\":[");
    for (i = 0; i < s->nsum; i++) {
        const Book *b = &s->sum[i];
        if (i) buf_putc(out, ',');
        buf_putc(out, '{');
        jfield(out, "id", b->id);
        buf_putc(out, ',');
        jfield(out, "title", b->title);
        buf_putc(out, ',');
        jfield(out, "creator", b->creator);
        buf_putc(out, ',');
        jfield(out, "date", b->date);
        buf_putc(out, ',');
        jfield(out, "language", b->language);
        buf_putc(out, ',');
        jfield(out, "work", b->work);
        buf_putc(out, ',');
        jfield(out, "expression", b->expression);
        buf_putc(out, ',');
        jfield(out, "volume", b->volume);
        buf_printf(out, ",\"pages\":%d,\"chapters\":%d}", b->file_pages, b->chapters);
    }
    buf_puts(out, "]}");
}

static void book_json(const Book *b, Buf *out) {
    int i;
    buf_clear(out);
    buf_puts(out, "{\"ok\":true,");
    jfield(out, "id", b->id);
    buf_putc(out, ',');
    jfield(out, "title", b->title);
    buf_putc(out, ',');
    jfield(out, "creator", b->creator);
    buf_putc(out, ',');
    jfield(out, "subject", b->subject);
    buf_putc(out, ',');
    jfield(out, "description", b->description);
    buf_putc(out, ',');
    jfield(out, "publisher", b->publisher);
    buf_putc(out, ',');
    jfield(out, "contributor", b->contributor);
    buf_putc(out, ',');
    jfield(out, "date", b->date);
    buf_putc(out, ',');
    jfield(out, "type", b->type);
    buf_putc(out, ',');
    jfield(out, "format", b->format);
    buf_putc(out, ',');
    jfield(out, "identifier", b->identifier);
    buf_putc(out, ',');
    jfield(out, "source", b->source);
    buf_putc(out, ',');
    jfield(out, "language", b->language);
    buf_putc(out, ',');
    jfield(out, "relation", b->relation);
    buf_putc(out, ',');
    jfield(out, "coverage", b->coverage);
    buf_putc(out, ',');
    jfield(out, "rights", b->rights);
    buf_putc(out, ',');
    jfield(out, "work", b->work);
    buf_putc(out, ',');
    jfield(out, "expression", b->expression);
    buf_putc(out, ',');
    jfield(out, "volume", b->volume);
    buf_putc(out, ',');
    jfield(out, "origin_name", b->origin_name);
    buf_putc(out, ',');
    jfield(out, "reader_note", b->reader_note);
    buf_printf(out,
               ",\"file_pages\":%d,\"chapters\":%d,\"place_page\":%d,\"place_block\":%d,"
               "\"hyphens\":%d,\"headers\":%d,\"columns\":%d,\"tables\":%d,\"notes\":%d,"
               "\"pictures\":%d,\"duplicates\":%d,\"blocks\":[",
               b->file_pages, b->chapters, b->place_page, b->place_block, b->hyphens, b->headers, b->columns, b->tables,
               b->notes, b->pictures, b->duplicates);
    for (i = 0; i < b->nblocks; i++) {
        const Block *blk = &b->blocks[i];
        if (i) buf_putc(out, ',');
        buf_puts(out, "{\"k\":");
        jesc(out, kind_name(blk->kind));
        buf_printf(out, ",\"page\":%d,\"label\":", blk->page);
        jesc(out, blk->label);
        buf_puts(out, ",\"text\":");
        jesc(out, blk->text);
        buf_putc(out, '}');
    }
    buf_puts(out, "],\"toc\":[");
    for (i = 0; i < b->ntoc; i++) {
        const TocItem *it = &b->toc[i];
        if (i) buf_putc(out, ',');
        buf_puts(out, "{\"title\":");
        jesc(out, it->title);
        buf_puts(out, ",\"label\":");
        jesc(out, it->label);
        buf_printf(out, ",\"page\":%d,\"block\":%d,\"level\":%d}", it->page, it->block, it->level);
    }
    buf_puts(out, "]}");
}

static void settings_json(const Settings *s, Buf *out) {
    Buf text;
    buf_init(&text);
    settings_format(s, &text);
    buf_clear(out);
    buf_puts(out, "{\"ok\":true,\"text\":");
    jesc(out, text.p ? text.p : "");
    buf_putc(out, '}');
    buf_free(&text);
}

static int load_book(Shelf *s, const char *id, Book *b) {
    memset(b, 0, sizeof *b);
    if (shelf_load(s, id, b) != 0) return -1;
    return 0;
}

static int origin_of(const Shelf *s, const Book *b, char *out, size_t cap) {
    char *books, *dir, *path;
    Buf file;
    char *text;
    books = path_join(s->dir, "books");
    dir = path_join(books, b->id);
    path = path_join(dir, "origin.txt");
    free(books);
    free(dir);
    buf_init(&file);
    if (file_read(path, &file) != 0) {
        free(path);
        buf_free(&file);
        recto_seterr("the original file is not on the shelf");
        return -1;
    }
    free(path);
    text = str_trim(file.p ? file.p : "");
    snprintf(out, cap, "%s", text);
    buf_free(&file);
    if (!out[0]) {
        recto_seterr("the original file is not on the shelf");
        return -1;
    }
    return 0;
}

static void replace_page(Book *b, int page, const char *label, const char *text) {
    int i, keep = -1, w = 0;
    for (i = 0; i < b->nblocks; i++) {
        if (b->blocks[i].page != page) continue;
        if (keep < 0) {
            keep = i;
            free(b->blocks[i].text);
            b->blocks[i].text = xstrdup(text ? text : "");
            b->blocks[i].kind = BLK_P;
            if (label && label[0]) snprintf(b->blocks[i].label, sizeof b->blocks[i].label, "%s", label);
        } else {
            free(b->blocks[i].text);
            b->blocks[i].text = NULL;
        }
    }
    for (i = 0; i < b->nblocks; i++) {
        if (b->blocks[i].text) b->blocks[w++] = b->blocks[i];
    }
    b->nblocks = w;
    if (keep < 0) book_add_block(b, BLK_P, page, label, text ? text : "", 0, 0, 0, 0, 80);
    book_rebuild_toc(b);
}

static int cmd_reread(Shelf *s, const char *id, const char *page_s, const char *rect, Buf *out) {
    Book b;
    Image im, crop;
    OcrPage op;
    char origin[RECTO_PATH];
    int page, x = 0, y = 0, w = 0, h = 0, cropped = 0, i;
    const char *label = "";
    memset(&b, 0, sizeof b);
    memset(&im, 0, sizeof im);
    memset(&crop, 0, sizeof crop);
    memset(&op, 0, sizeof op);
    if (load_book(s, id, &b) != 0) return fail(out, recto_err);
    page = atoi(page_s && page_s[0] ? page_s : "1");
    if (page < 1) {
        book_free(&b);
        return fail(out, "the page number starts at 1");
    }
    for (i = 0; i < b.nblocks; i++) {
        if (b.blocks[i].page == page) {
            label = b.blocks[i].label;
            break;
        }
    }
    if (rect && rect[0]) {
        char tmp[80];
        snprintf(tmp, sizeof tmp, "%s", rect);
        for (i = 0; tmp[i]; i++)
            if (tmp[i] == ',') tmp[i] = ' ';
        if (sscanf(tmp, "%d %d %d %d", &x, &y, &w, &h) != 4) {
            book_free(&b);
            return fail(out, "a crop is x y w h");
        }
        cropped = 1;
    }
    if (origin_of(s, &b, origin, sizeof origin) != 0) {
        book_free(&b);
        return fail(out, recto_err);
    }
    if (load_page_image(origin, page, &im) != 0) {
        book_free(&b);
        return fail(out, recto_err);
    }
    if (cropped) {
        if (image_crop(&im, x, y, w, h, &crop) != 0) {
            image_free(&im);
            book_free(&b);
            return fail(out, recto_err);
        }
        image_free(&im);
        im = crop;
        memset(&crop, 0, sizeof crop);
    }
    if (ocr_image(&im, &s->set, &op) != 0) {
        image_free(&im);
        book_free(&b);
        return fail(out, recto_err);
    }
    image_free(&im);
    if (shelf_revise(s, &b, -1, NULL) != 0) {
        ocr_page_free(&op);
        book_free(&b);
        return fail(out, recto_err);
    }
    replace_page(&b, page, label, op.text ? op.text : "");
    snprintf(b.reader_note, sizeof b.reader_note, "Page reread with %s.", op.engine[0] ? op.engine : "the reader");
    ocr_page_free(&op);
    if (shelf_save(s, &b) != 0 || shelf_refresh(s) != 0) {
        book_free(&b);
        return fail(out, recto_err);
    }
    buf_clear(out);
    buf_puts(out, "{\"ok\":true,\"note\":");
    jesc(out, b.reader_note);
    buf_putc(out, '}');
    book_free(&b);
    return 0;
}

static int cmd_open(Shelf *s, const char *path, Buf *out) {
    const char *paths[1];
    Book *books = NULL;
    int n = 0, i, rc = 0;
    if (!path || !path[0]) return fail(out, "open needs a file or a folder");
    paths[0] = path;
    if (import_paths(paths, 1, &s->set, &books, &n) != 0) return fail(out, recto_err);
    for (i = 0; i < n; i++) {
        const char *origin = NULL;
        if (books[i].source[0] && (file_exists(books[i].source) || file_is_dir(books[i].source))) origin = books[i].source;
        if (shelf_add(s, &books[i], origin) != 0) {
            rc = -1;
            break;
        }
    }
    for (i = 0; i < n; i++) book_free(&books[i]);
    free(books);
    if (rc != 0) return fail(out, recto_err);
    shelf_json(s, out);
    return 0;
}

static void free_paths(char **paths, int n) {
    int i;
    if (!paths) return;
    for (i = 0; i < n; i++) free(paths[i]);
    free(paths);
}

static int collect_paths(const char *src, char ***out, int *n) {
    const char *p;
    int cap = 0;
    *out = NULL;
    *n = 0;
    if (!src) return 0;
    p = src;
    while (*p) {
        const char *eol = strpbrk(p, "\r\n");
        size_t len = eol ? (size_t)(eol - p) : strlen(p);
        char *line = xstrndup(p, len);
        char *s = str_trim(line);
        if (s && s[0]) {
            if (*n >= 512) {
                free(line);
                free_paths(*out, *n);
                *out = NULL;
                *n = 0;
                recto_seterr("too many files");
                return -1;
            }
            if (*n >= cap) {
                cap = cap ? cap * 2 : 8;
                *out = xrealloc(*out, (size_t)cap * sizeof **out);
            }
            (*out)[(*n)++] = xstrdup(s);
        }
        free(line);
        if (!eol) break;
        p = eol + 1;
        if (*eol == '\r' && *p == '\n') p++;
    }
    return 0;
}

static int dialog_capture(const char *cmd, Buf *out) {
    FILE *f;
    char tmp[4096];
    size_t n;
    int st;
    buf_clear(out);
    f = popen(cmd, "r");
    if (!f) {
        recto_seterr("the file dialog did not open");
        return -1;
    }
    while ((n = fread(tmp, 1, sizeof tmp, f)) > 0) buf_append(out, tmp, n);
    st = pclose(f);
    if (st == 0) return 0;
    if (WIFEXITED(st) && WEXITSTATUS(st) == 1 && out->n == 0) return 1;
    if (out->n > 0) return 0;
    recto_seterr("the file dialog did not open");
    return -1;
}

static int pick_files(char ***paths, int *n) {
    Buf cmd, out;
    char py[RECTO_PATH];
    int rc;
    const char *home;
    *paths = NULL;
    *n = 0;
    py[0] = 0;
    buf_init(&cmd);
    buf_init(&out);
    if (which_cmd("zenity")) {
        buf_puts(&cmd, "zenity --file-selection --multiple --separator '\n' --title ");
        sh_quote(&cmd, "Open files to convert");
        buf_puts(&cmd, " --file-filter ");
        sh_quote(&cmd, "All files | *");
        buf_puts(&cmd, " --file-filter ");
        sh_quote(&cmd, "Documents | *.pdf *.djvu *.djv *.html *.htm *.xhtml *.txt *.md *.markdown *.png *.jpg *.jpeg *.tif *.tiff *.bmp *.pgm *.ppm *.pbm *.pnm");
        home = getenv("HOME");
        if (home && home[0]) {
            char dir[RECTO_PATH];
            size_t hn = strlen(home);
            buf_puts(&cmd, " --filename ");
            if (hn + 2 < sizeof dir && home[hn - 1] != '/') snprintf(dir, sizeof dir, "%s/", home);
            else snprintf(dir, sizeof dir, "%s", home);
            sh_quote(&cmd, dir);
        }
    } else if (which_cmd("python3")) {
        const char *script =
            "import tkinter as tk\n"
            "from tkinter import filedialog\n"
            "r = tk.Tk()\n"
            "r.withdraw()\n"
            "try:\n"
            "    r.attributes('-topmost', True)\n"
            "except Exception:\n"
            "    pass\n"
            "fs = filedialog.askopenfilenames(title='Open files to convert')\n"
            "r.destroy()\n"
            "for f in fs:\n"
            "    print(f)\n";
        temp_path(py, sizeof py, ".py");
        if (file_write(py, script, strlen(script)) != 0) {
            buf_free(&cmd);
            buf_free(&out);
            return -1;
        }
        buf_puts(&cmd, "python3 ");
        sh_quote(&cmd, py);
    } else {
        buf_free(&cmd);
        buf_free(&out);
        recto_seterr("no file dialog is installed");
        return -1;
    }
    rc = dialog_capture(cmd.p, &out);
    buf_free(&cmd);
    if (py[0]) remove(py);
    if (rc < 0) {
        buf_free(&out);
        return -1;
    }
    if (rc > 0) {
        buf_free(&out);
        return 0;
    }
    rc = collect_paths(out.p ? out.p : "", paths, n);
    buf_free(&out);
    return rc;
}

static int cmd_pick(Buf *out) {
    char **paths = NULL;
    int n = 0, i;
    if (pick_files(&paths, &n) != 0) {
        free_paths(paths, n);
        return fail(out, recto_err);
    }
    buf_clear(out);
    buf_puts(out, "{\"ok\":true,\"files\":[");
    for (i = 0; i < n; i++) {
        if (i) buf_putc(out, ',');
        jesc(out, paths[i] ? paths[i] : "");
    }
    buf_puts(out, "]}");
    free_paths(paths, n);
    return 0;
}

static int cmd_convert(Shelf *s, const char *src, const char *dest, Buf *out) {
    char **inputs = NULL;
    char **paths = NULL;
    char **ids = NULL;
    int ninputs = 0, n = 0, i;
    if (collect_paths(src, &inputs, &ninputs) != 0) return fail(out, recto_err);
    if (ninputs < 1) {
        free_paths(inputs, ninputs);
        return fail(out, "convert needs a file or a folder");
    }
    if (convert_files((const char **)inputs, ninputs, dest && dest[0] ? dest : NULL, &s->set, s, &paths, &ids, &n) !=
        0) {
        free_paths(inputs, ninputs);
        convert_free(paths, ids, n);
        return fail(out, recto_err);
    }
    free_paths(inputs, ninputs);
    buf_clear(out);
    buf_puts(out, "{\"ok\":true,\"files\":[");
    for (i = 0; i < n; i++) {
        if (i) buf_putc(out, ',');
        jesc(out, paths[i] ? paths[i] : "");
    }
    buf_puts(out, "],\"ids\":[");
    for (i = 0; i < n; i++) {
        if (i) buf_putc(out, ',');
        jesc(out, ids && ids[i] ? ids[i] : "");
    }
    buf_puts(out, "]}");
    convert_free(paths, ids, n);
    return 0;
}

int gui_call(Shelf *shelf, const char *op, const char *a, const char *b, const char *c, Buf *out) {
    Book book;
    if (!shelf || !out) return -1;
    op = arg_or_empty(op);
    a = arg_or_empty(a);
    b = arg_or_empty(b);
    c = arg_or_empty(c);
    if (strcmp(op, "shelf") == 0 || strcmp(op, "sample") == 0) {
        if (strcmp(op, "sample") == 0 && shelf_seed_samples(shelf) != 0) return fail(out, recto_err);
        shelf_json(shelf, out);
        return 0;
    }
    if (strcmp(op, "settings") == 0) {
        settings_json(&shelf->set, out);
        return 0;
    }
    if (strcmp(op, "set") == 0) {
        if (settings_apply(&shelf->set, a) != 0 || shelf_save_settings(shelf) != 0) return fail(out, recto_err);
        settings_json(&shelf->set, out);
        return 0;
    }
    if (strcmp(op, "check") == 0) {
        jok(out);
        return 0;
    }
    if (strcmp(op, "goto") == 0) {
        int at;
        if (load_book(shelf, a, &book) != 0) return fail(out, recto_err);
        at = book_label_block(&book, b);
        if (at < 0) {
            book_free(&book);
            return fail(out, "no page with that number");
        }
        buf_clear(out);
        buf_printf(out, "{\"ok\":true,\"block\":%d,\"page\":%d,\"label\":", at, book.blocks[at].page);
        jesc(out, book.blocks[at].label);
        buf_putc(out, '}');
        book_free(&book);
        return 0;
    }
    if (strcmp(op, "open") == 0) return cmd_open(shelf, a, out);
    if (strcmp(op, "convert") == 0) return cmd_convert(shelf, a, b, out);
    if (strcmp(op, "pick") == 0) return cmd_pick(out);
    if (strcmp(op, "remove") == 0) {
        char msg[96];
        if (!str_ieq(b, "yes")) {
            snprintf(msg, sizeof msg, "say remove %s --yes", a);
            return fail(out, msg);
        }
        if (shelf_delete(shelf, a) != 0) return fail(out, recto_err);
        shelf_json(shelf, out);
        return 0;
    }
    if (strcmp(op, "book") == 0) {
        if (load_book(shelf, a, &book) != 0) return fail(out, recto_err);
        book_json(&book, out);
        book_free(&book);
        return 0;
    }
    if (strcmp(op, "find") == 0) {
        Hit *hits = NULL;
        int nh = 0, i;
        if (load_book(shelf, a, &book) != 0) return fail(out, recto_err);
        if (book_find(&book, b, &hits, &nh) != 0) {
            book_free(&book);
            return fail(out, recto_err);
        }
        buf_clear(out);
        buf_puts(out, "{\"ok\":true,\"hits\":[");
        for (i = 0; i < nh; i++) {
            if (i) buf_putc(out, ',');
            buf_printf(out, "{\"block\":%d,\"page\":%d,\"label\":", hits[i].block, hits[i].page);
            jesc(out, hits[i].label);
            buf_puts(out, ",\"snippet\":");
            jesc(out, hits[i].snippet);
            buf_putc(out, '}');
        }
        buf_puts(out, "]}");
        free(hits);
        book_free(&book);
        return 0;
    }
    if (strcmp(op, "place") == 0) {
        int at = atoi(b);
        if (load_book(shelf, a, &book) != 0) return fail(out, recto_err);
        if (at < 0 || at >= book.nblocks) {
            book_free(&book);
            return fail(out, "that passage is not in the book");
        }
        book.place_block = at;
        book.place_page = book.blocks[at].page;
        if (shelf_save(shelf, &book) != 0 || shelf_refresh(shelf) != 0) {
            book_free(&book);
            return fail(out, recto_err);
        }
        book_free(&book);
        jok(out);
        return 0;
    }
    if (strcmp(op, "restore") == 0) {
        if (load_book(shelf, a, &book) != 0) return fail(out, recto_err);
        if (shelf_restore(shelf, &book) != 0) {
            book_free(&book);
            return fail(out, recto_err);
        }
        book_free(&book);
        jok(out);
        return 0;
    }
    if (strcmp(op, "export") == 0) {
        char *full = NULL;
        int rc;
        if (!b[0]) return fail(out, "export needs a file name");
        if (load_book(shelf, a, &book) != 0) return fail(out, recto_err);
        full = b[0] == '/' ? xstrdup(b) : path_join(shelf->dir, b);
        rc = epub_write(&book, full);
        book_free(&book);
        if (rc != 0) {
            free(full);
            return fail(out, recto_err);
        }
        buf_clear(out);
        buf_puts(out, "{\"ok\":true,\"path\":");
        jesc(out, full);
        buf_putc(out, '}');
        free(full);
        return 0;
    }
    if (strcmp(op, "reread") == 0) return cmd_reread(shelf, a, b, c, out);
    return fail(out, "that request is not part of the desk");
}

static void on_api(webui_event_t *e) {
    const char *op = webui_get_string_at(e, 0);
    const char *a = webui_get_string_at(e, 1);
    const char *b = webui_get_string_at(e, 2);
    const char *c = webui_get_string_at(e, 3);
    Buf out;
    buf_init(&out);
    if (getenv("RECTO_GUI_TRACE")) fprintf(stderr, "gui %s %s %s\n", arg_or_empty(op), arg_or_empty(a), arg_or_empty(b));
    if (!gui_shelf || gui_call(gui_shelf, op, a, b, c, &out) != 0) {
        if (!out.n) fail(&out, recto_err[0] ? recto_err : "the desk could not do that");
    }
    webui_return_string(e, out.p ? out.p : "{\"ok\":false,\"error\":\"empty\"}");
    buf_free(&out);
}

static int serve(Shelf *shelf, int open_browser) {
    size_t window;
    const char *url;
    gui_shelf = shelf;
    webui_set_config(ui_event_blocking, true);
    if (!open_browser) webui_set_config(show_wait_connection, false);
    window = webui_new_window();
    if (window == 0) {
        recto_seterr("the window did not open");
        return -1;
    }
    webui_bind(window, "recto_api", on_api);
    webui_set_size(window, 1180, 820);
    if (open_browser) {
        if (!webui_show(window, GUI_HTML)) {
            recto_seterr("the browser window did not open");
            webui_clean();
            gui_shelf = NULL;
            return -1;
        }
    } else {
        url = webui_start_server(window, GUI_HTML);
        if (!url || !url[0]) {
            recto_seterr("the window server did not start");
            webui_clean();
            gui_shelf = NULL;
            return -1;
        }
        printf("%s\n", url);
        fflush(stdout);
    }
    webui_wait();
    webui_clean();
    gui_shelf = NULL;
    return 0;
}

int gui_run(Shelf *shelf) {
    if (!shelf) return -1;
    return serve(shelf, getenv("RECTO_GUI_NOOPEN") ? 0 : 1);
}

static void curl_cmd(Buf *cmd, const char *jar, const char *url) {
    buf_clear(cmd);
    buf_puts(cmd, "curl -fsS --max-time 3 -c ");
    sh_quote(cmd, jar);
    buf_puts(cmd, " -b ");
    sh_quote(cmd, jar);
    buf_putc(cmd, ' ');
    sh_quote(cmd, url);
}

int gui_http_probe(void) {
    size_t window;
    const char *url;
    Buf cmd, body;
    char jar[RECTO_PATH];
    char *jsurl = NULL;
    int i, rc = -1;
    webui_set_config(show_wait_connection, false);
    window = webui_new_window();
    if (window == 0) {
        recto_seterr("the window server did not start");
        return -1;
    }
    url = webui_start_server(window, GUI_HTML);
    if (!url || !url[0]) {
        recto_seterr("the window server did not start");
        webui_exit();
        webui_clean();
        return -1;
    }
    temp_path(jar, sizeof jar, ".cookies");
    buf_init(&cmd);
    buf_init(&body);
    curl_cmd(&cmd, jar, url);
    for (i = 0; i < 20; i++) {
        if (cmd_capture(cmd.p, &body) == 0 && body.p && strstr(body.p, "id='shelf'")) {
            rc = 0;
            break;
        }
        usleep(100000);
    }
    if (rc != 0) recto_seterr("the desk page was not served");
    else {
        jsurl = path_join(url, "webui.js");
        curl_cmd(&cmd, jar, jsurl);
        free(jsurl);
        rc = -1;
        if (cmd_capture(cmd.p, &body) == 0 && body.p && strstr(body.p, "WebuiBridge")) rc = 0;
        else recto_seterr("webui.js was not served");
    }
    remove(jar);
    buf_free(&cmd);
    buf_free(&body);
    webui_exit();
    webui_clean();
    return rc;
}

#endif
