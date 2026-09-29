/*
 * ZAPM in the browser: draws the panes of the curses shim (port/be_web.cpp
 * calls Module.zp), keyboard input, tiling windows, saves in IndexedDB.
 * Text only, curses colours (resolved in port/wcurses.c); the text windows
 * are HTML lines from the game (RVIP W0 rule 6), the map is the only canvas.
 * No sound (ZAPM has none). Copied from
 * ~/Games/xrogue/web/xrogue.js. Loaded before zapm-core.js.
 */
(function () {
	'use strict';

	var P_MAP = 0, P_STATUS = 1, P_MSG = 2, P_INV = 3, P_POP = 4;
	var WIN = ['map', 'stat', 'msg', 'inv'];          /* pane -> window id */
	var DIR = '/zapm/user';                           /* IDBFS mount: saves, scores, layout */
	var SAVE = DIR + '/player.sav', LAYOUT_FILE = DIR + '/web-layout.json';
	var FONT = '"DejaVu Sans Mono", Menlo, Consolas, "Liberation Mono", monospace';
	var FG = '#dcdcdc', BG = '#000';
	var GUT = 6, TITLE_H = 20, BORDER = 2;
	var TILE_STEPS = [10, 11, 12, 13, 14, 16, 18, 20, 22, 24, 28, 32, 36, 40, 48];   /* map font px */
	/* ZAPM moves with the digits */
	var KEY = { ArrowDown: 50, ArrowUp: 56, ArrowLeft: 52, ArrowRight: 54,
		Home: 55, PageUp: 57, End: 49, PageDown: 51 };
	/* curses colours (port/curses.h: pair << 8, bold 0x10000, dim 0x20000, reverse 0x40000) */
	var PAL = [
		['#000', '#c82828', '#28b428', '#c8a028', '#3c5ae6', '#be3cbe', '#28b4be', '#c8c8c8'],
		['#6e6e6e', '#ff5a5a', '#64ff64', '#ffff5a', '#7896ff', '#ff6eff', '#6effff', '#fff'],
		['#000', '#641414', '#145a14', '#645014', '#1e2d73', '#5f1e5f', '#145a5f', '#6e6e6e']];

	var panes = [];            /* {cv, ctx, cols, rows, cw, ch, pad, buf} */
	var events = [];
	var app, saveReq = false;
	var cur = { p: -1, y: 0, x: 0 };
	var dpr = Math.max(1, Math.min(3, window.devicePixelRatio || 1));
	var L = null, rects = {};

	function $(id) { return document.getElementById(id); }
	function clamp(v, lo, hi) { return Math.max(lo, Math.min(hi, v)); }

	/* ---------- panes ---------- */

	/* fonts from the index page's fonts/: L.face for every pane but the map,
	 * L.mapFace for the map (chooser on its title bar) */
	function face(p) { var n = L && (p === P_MAP ? L.mapFace : L.face); return n ? '"' + n + '", ' + FONT : FONT; }
	function measure(px) {   /* map cell width: on the map's own canvas */
		var c = document.querySelector('#t-map canvas').getContext('2d');
		c.font = px + 'px ' + face(P_MAP);
		return Math.ceil(c.measureText('M').width);
	}

	/* Map cell size from the zoom; rebuilds the canvas and redraws */
	function shape(p) {
		var T = panes[p], f = L.tile;
		T.cw = measure(f); T.ch = Math.round(f * 1.3); T.pad = 0;
		T.font = (!L.mapFace ? 'bold ' : '') + f + 'px ' + face(p);
		var w = T.cols * T.cw, h = T.rows * T.ch;
		T.cv.width = Math.round(w * dpr); T.cv.height = Math.round(h * dpr);
		T.w = w; T.h = h;
		T.ctx = T.cv.getContext('2d');
		T.ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
		T.ctx.fillStyle = BG; T.ctx.fillRect(0, 0, w, h);
		for (var i = 0; i < T.cols * T.rows; i++) draw(p, (i / T.cols) | 0, i % T.cols);
		fit(p);
	}

	function makePane(p, cols, rows) {
		panes[p] = { cv: document.querySelector('#t-map canvas'), cols: cols, rows: rows, ch_: new Int32Array(cols * rows).fill(32) };
		shape(p);
	}

	/* ---------- text windows (RVIP W0 rule 6): HTML lines from the game ----------
	 * Each changed row comes trimmed, with colour runs "\x05#fg[/#bg]" .. "\x06"
	 * and reverse video between \x01 and \x02 (port/wcurses.c), plus the rows
	 * in use; the WM sets the text size (A−/A+ per window). */
	var txt = [];              /* pane -> {el, lines, n} */
	function textPane(p) {
		var el = p === P_POP ? $('pop').firstElementChild : document.querySelector('#t-' + WIN[p] + ' .body' + (p === P_INV ? '' : ' pre'));
		el.textContent = '';
		txt[p] = { el: el, lines: [], css: [], n: 0 };
	}
	function esc(s) { return s.replace(/&/g, '&amp;').replace(/</g, '&lt;'); }
	var TOK = /\x05\*?#[0-9a-f]{6}(?:\/#[0-9a-f]{6})?|[\x01\x02\x06]|[^]/g;
	function rowHtml(p, y) {
		var s = txt[p].lines[y] || '', cx = cur.p === p && cur.y === y ? cur.x : -1, k = 0, out = '';
		var t = s.match(TOK) || [];
		if (cx >= 0) {                              /* the cursor: one cell, past the end if need be */
			var n = t.filter(function (a) { return a.length === 1 && a > '\x06'; }).length;
			for (; n <= cx; n++) t.push(' ');
		}
		t.forEach(function (a) {
			if (a === '\x01') out += '<span class="so">';
			else if (a === '\x02' || a === '\x06') out += '</span>';
			else if (a[0] === '\x05') { var b = a[1] === '*', c = a.slice(b ? 2 : 1).split('/'); out += '<span style="color:' + c[0] + (b ? ';font-weight:bold' : '') + (c[1] ? ';background:' + c[1] : '') + '">'; }
			else { var e = esc(a); out += k++ === cx ? '<span class="cur">' + e + '</span>' : e; }
		});
		return out;
	}
	function drawRow(p, y) {
		var T = txt[p], d = T && T.el.children[y];
		if (d) { d.innerHTML = rowHtml(p, y); d.style.color = T.css[y] || ''; }
	}
	function setRows(p, n) {
		var T = txt[p];
		while (T.el.children.length < n) { T.el.appendChild(document.createElement('div')); drawRow(p, T.el.children.length - 1); }
		while (T.el.children.length > n) T.el.removeChild(T.el.lastChild);
		T.n = n;
	}
	function msgMark() {                          /* before a Messages change: was it at the end? */
		var b = txt[P_MSG] && txt[P_MSG].el.parentNode;
		if (b && zp.follow == null) zp.follow = b.scrollTop + b.clientHeight >= b.scrollHeight - 4;
	}
	function popFont() { $('pop').style.fontSize = RvipWM.fontSize('msg') + 'px'; placePop(); }   /* pop-ups use the Messages size */
	function placePop() { if (!$('pop').hidden && rects.map) RvipWM.popup($('pop'), { x: 8 }); }
	/* the top-bar font on every text window and the pop-up */
	function applyFace() {
		['#t-stat .body', '#t-msg .body', '#t-inv .body', '#t-vis .body', '#pop'].forEach(function (q) {
			var e = document.querySelector(q); if (e) e.style.fontFamily = L.face ? '"' + L.face + '", ' + FONT : FONT; });
	}

	function draw(p, y, x) {
		var T = panes[p], c = T.ctx, i = y * T.cols + x, ch = T.ch_[i];
		var px = T.pad + x * T.cw, py = T.pad + y * T.ch;
		var inv = !!(ch & 0x40000), pair = (ch >> 8) & 0x3f;
		var fg = PAL[ch & 0x10000 ? 1 : ch & 0x20000 ? 2 : 0][pair ? pair % 8 : 7];
		if (!pair && ch & 0x20000) fg = PAL[1][0];
		c.fillStyle = inv ? fg : BG;
		c.fillRect(px, py, T.cw, T.ch);
		var k = ch & 0xff;
		if (k > 32) {
			c.font = T.font;
			c.textAlign = 'center'; c.textBaseline = 'middle';
			c.fillStyle = inv ? BG : fg;
			c.fillText(String.fromCharCode(k), px + T.cw / 2, py + T.ch / 2 + 1);
		}
	}

	function drawCursor() {
		var T = panes[cur.p];
		if (!T || cur.y >= T.rows || cur.x >= T.cols) return;
		if (cur.p !== P_MAP || !T) return;
		if (cur.y === hero.y && cur.x === hero.x) return;   /* no cursor on the hero */
		var c = T.ctx, px = T.pad + cur.x * T.cw, py = T.pad + cur.y * T.ch;
		c.fillStyle = c.strokeStyle = FG;
		c.fillRect(px, py + T.ch - 2, T.cw, 2);
	}

	/* ---------- tiling layout ---------- */
	/*
	 *   +--------------------+--------+   side:   x of left part | right column
	 *   |                    | status |   bottom: y of map | messages (left part)
	 *   |        map         +--------+   stat:   y of status | inventory (right)
	 *   |                    |        |
	 *   +--------------------+  inv   |
	 *   |      messages      |        |
	 *   +--------------------+--------+
	 */
	var SPLITS = ['bottom', 'side', 'stat'];

	function areaSize() {
		var g = $('game');
		return { w: g.clientWidth, h: g.clientHeight };
	}

	function defaultLayout() {
		var A = areaSize(), W = A.w, H = A.h;
		if (W < 400 || H < 300) { W = 1280; H = 720; }
		var font = 13, cw = 0.62 * font, ch = Math.round(font * 1.3);   /* 13 px text, roughly */
		var sideW = 50 * cw + BORDER + GUT, tile = TILE_STEPS[0];
		TILE_STEPS.forEach(function (t) {
			if (64 * measure(t) + BORDER <= W - sideW && 20 * Math.round(t * 1.3) + BORDER <= H - 13 * ch - TITLE_H) tile = t;
		});
		var mapH = 20 * Math.round(tile * 1.3) + BORDER, statH = 20 * ch + TITLE_H + BORDER;
		return { v: 1, tile: tile, auto: true,
			split: { side: clamp((64 * measure(tile) + BORDER + GUT / 2) / W, 0.3, 0.9), bottom: clamp((mapH + GUT / 2) / H, 0.2, 0.9),
				stat: clamp((statH + GUT / 2) / H, 0.2, 0.8) } };
	}

	function loadLayout() {
		var d = defaultLayout();
		try {
			var s = JSON.parse(Module.FS.readFile(LAYOUT_FILE, { encoding: 'utf8' }));
			if (s && s.v === 1) {
				if (!s.auto) {
					d.auto = false;
					SPLITS.forEach(function (k) { if (s.split[k] > 0 && s.split[k] < 1) d.split[k] = s.split[k]; });
					if (TILE_STEPS.indexOf(s.tile) >= 0) d.tile = s.tile;
				}
				if (s.wm) d.wm = s.wm;
				if (s.font && d.wm && !d.wm.fs) d.wm.fs = { msg: s.font.msg, stat: s.font.stat, inv: s.font.inv, vis: s.font.vis };   /* old layout: sizes were ours */
				if (typeof s.face === 'string') d.face = s.face;
				if (typeof s.mapFace === 'string') d.mapFace = s.mapFace;
			}
		} catch (err) { /* nothing saved yet */ }
		L = d;
		$('sel-font').value = L.face || '';   /* if the font list came first */
		loadFace(L.face); loadFace(L.mapFace);
	}

	var saveTimer = 0;
	function saveLayout() {
		clearTimeout(saveTimer);
		saveTimer = setTimeout(function () {
			try { Module.FS.writeFile(LAYOUT_FILE, JSON.stringify(L)); app.sync(); }
			catch (err) { console.warn('layout not saved', err); }
		}, 400);
	}

	function place(el, r) {
		el.style.left = r[0] + 'px'; el.style.top = r[1] + 'px';
		el.style.width = Math.max(0, r[2]) + 'px'; el.style.height = Math.max(0, r[3]) + 'px';
	}

	/* the map never shrinks: bigger than its window, it scrolls with the hero */
	function fit(p) {
		var T = panes[p], r = rects.map;
		if (!T || !r) return;
		T.box = { w: r[2] - BORDER, h: r[3] - BORDER - ($('game').classList.contains('wm-single') ? 0 : TITLE_H) };
		scrollMap(true);
	}

	var hero = { y: 0, x: 0 }, off = { x: 0, y: 0 };
	/* Keep the hero in the middle half of the map window; recentre when it
	 * leaves it (or always, after a zoom, resize or new level) */
	function scrollMap() {
		var T = panes[P_MAP];
		if (!T || !T.box) return;
		T.cv.style.width = T.w + 'px'; T.cv.style.height = T.h + 'px';
		off = RvipWM.center(T.cv, (hero.x + 0.5) * T.cw, (hero.y + 0.5) * T.ch, T.w, T.h, T.box.w, T.box.h);
	}

	var wm = null;
	function applyDom() { if (wm) wm.apply(); }
	function makeWM() {
		var s = defaultLayout().split, A = areaSize();
		var line = Math.round(13 * 1.3) + 4, stat = Math.round(13 * 1.3) + 4;
		wm = RvipWM({
			area: $('game'), menu: $('btn-layout'),
			wins: [{ id: 'map', title: 'Map' }, { id: 'msg', title: 'Messages' }, { id: 'stat', title: 'Status' }, { id: 'inv', title: 'Inventory' }, { id: 'vis', title: 'Visible' }],
			multi: { d: 'h', r: s.side, a: { d: 'v', r: s.bottom, a: 'map', b: 'msg' }, b: { d: 'v', r: s.stat, a: 'stat', b: { d: 'v', r: 0.6, a: 'inv', b: 'vis' } } },
			single: { d: 'v', r: line / A.h, a: 'msg', b: { d: 'v', r: 1 - stat / (A.h - line), a: 'map', b: 'stat' } },
			state: L.wm,
			save: function (st) { L.wm = st; saveLayout(); },
			layout: function (r) { rects = r; fit(P_MAP); placePop(); var mb = txt[P_MSG] && txt[P_MSG].el.parentNode; if (mb) mb.scrollTop = mb.scrollHeight; },
			/* A- / A+: the map steps its font; the text windows are the WM's; the pop-up follows Messages */
			zoom: { map: function (px, d) { zoomMap(d); }, msg: popFont },
			onReset: resetLayout
		});
		wm.apply();
		renderMapSel();
	}

	function zoomMap(d) {
		var i = clamp(TILE_STEPS.indexOf(L.tile) + d, 0, TILE_STEPS.length - 1);
		L.tile = TILE_STEPS[i]; L.auto = false;
		shape(P_MAP); applyDom(); saveLayout();
		app.status('Map font: ' + L.tile + ' px');
		setTimeout(function () { app.status(''); }, 1200);
	}

	function resetLayout() {
		var a = L.audio, fc = L.face, mf = L.mapFace;
		L = defaultLayout(); L.audio = a; L.face = fc; L.mapFace = mf; L.wm = wm.state();
		shape(P_MAP); popFont();
		applyDom(); saveLayout();
	}

	/* ---------- called by the game (port/be_web.c) ---------- */
	var zp = {
		init: function (p, cols, rows) {
			if (!L) loadLayout();
			if (p === P_MAP) makePane(p, cols, rows); else textPane(p);
			if (p === P_INV) { $('game').hidden = false; makeWM(); applyFace(); popFont(); }
		},
		put: function (p, y, x, ch) {
			var T = panes[p];
			if (!T || y < 0 || x < 0 || y >= T.rows || x >= T.cols) return;
			T.ch_[y * T.cols + x] = ch;
			draw(p, y, x);
		},
		cursor: function (p, y, x) {
			var o = cur.p, oy = cur.y;
			cur.p = p; cur.y = y; cur.x = x;
			if (txt[o]) drawRow(o, oy);
			if (txt[p]) drawRow(p, y);
		},
		line: function (p, y, s, c, t) {
			var T = txt[p];
			if (!T) return;
			if (p === P_MSG) msgMark();
			T.lines[y] = s; T.css[y] = c;
			if (y < T.n) drawRow(p, y);
		},
		rows: function (p, n) { if (!txt[p]) return; if (p === P_MSG) msgMark(); setRows(p, n); },
		popup: function (rows, cols) {
			if (!rows) { $('pop').hidden = true; if (cur.p === P_POP) cur.p = -1; return; }
			textPane(P_POP);
			$('pop').hidden = false;
		},
		flush: function (hy, hx) {
			if (hy !== hero.y || hx !== hero.x) { hero.y = hy; hero.x = hx; scrollMap(false); }
			/* the cursor is drawn over the cell; redraw that cell next time */
			if (zp.lastCur) draw(P_MAP, zp.lastCur.y, zp.lastCur.x);
			drawCursor();
			var mb = txt[P_MSG] && txt[P_MSG].el.parentNode;   /* follow the newest message unless scrolled up */
			if (mb && zp.follow) mb.scrollTop = mb.scrollHeight;
			zp.follow = null;
			placePop();
			zp.lastCur = cur.p === P_MAP && panes[P_MAP] ? { y: cur.y, x: cur.x } : null;
		},
		vis: function (s) { RvipWM.visible(document.querySelector('#t-vis .body'), s); },
		key: function (atCmd) { RvipWM.prompt.wait(atCmd); return events.length ? events.shift() : -1; },
		prompt: function (s) { RvipWM.prompt.text(s); },
		requestSave: function () { saveReq = true; },   /* also for testing */
		wantSave: function () {
			if (!saveReq || !app.running) return 0;
			saveReq = false;
			setTimeout(app.sync, 0);           /* after the game wrote the file */
			return 1;
		},
		end: function (saved) {
			app.running = false;
			app.sync(function () {
				$('overlay-msg').textContent = saved ? 'Your game has been saved. Play again to continue it.'
					: 'The game is over.';
				$('overlay').hidden = false;
			});
		}
	};

	/* ---------- input ---------- */
	function onKey(e) {
		if (!app.running || e.isComposing || e.metaKey) return;
		var k = e.key, code = e.code || '', m = /^Numpad(\d)$/.exec(code), c;
		if (m) c = 48 + +m[1];
		else if (code === 'NumpadEnter' || k === 'Enter') c = 13;
		else if (code === 'NumpadDecimal') c = 46;
		else if (k === 'Escape') c = 27;
		else if (k === 'Backspace' || k === 'Delete') c = 127;
		else if (k === 'Tab') c = 9;
		else if (KEY[k]) c = KEY[k];
		else if (k.length === 1) {
			c = k.charCodeAt(0);
			if (e.ctrlKey && !e.altKey) {
				var u = k.toUpperCase().charCodeAt(0);
				if (u >= 64 && u <= 95) c = u & 0x1F;
			}
			if (c > 255) return;
		}
		else return;
		events.push(c);
		e.preventDefault();
	}

	/* ---------- saves: IndexedDB (IDBFS) ---------- */
	function hasSave() { try { Module.FS.stat(SAVE); return true; } catch (e) { return false; } }
	function putSave(file, data) { Module.FS.writeFile(SAVE, data); }
	function clearSave() { if (hasSave()) Module.FS.unlink(SAVE); }

	/* ---------- fonts ---------- */
	/* map font chooser: on the Map title bar (shown on hover) */
	var mapSel = document.createElement('select');
	mapSel.title = 'Map font';
	mapSel.innerHTML = '<option value="">Default font</option>';
	mapSel.addEventListener('pointerdown', function (e) { e.stopPropagation(); });   /* not a window drag */
	function renderMapSel() {
		var bs = document.querySelector('#t-map .wm-btns');
		if (bs && mapSel.parentNode !== bs) bs.insertBefore(mapSel, bs.firstChild);
		mapSel.value = (L && L.mapFace) || '';
	}
	function loadFace(n, now) {
		var redraw = function () {
			if (panes[P_MAP]) shape(P_MAP);
			applyFace(); applyDom();
		};
		if (!n) { if (now) redraw(); return; }
		var ff = new FontFace(n, 'url(../fonts/' + n + '.woff)');
		ff.load().then(function () { document.fonts.add(ff); redraw(); }).catch(function () { app.status('Could not load the font ' + n + '.', true); });
	}

	/* ---------- startup ---------- */
	app = RvipApp({ name: 'zapm', save: function () { return hasSave() ? SAVE : null; }, clear: clearSave, put: putSave,
		flush: function (done) { saveReq = true; setTimeout(done, 1500); } });   /* the game saves at its next wantSave() poll */
	/* the player's name: asked once, kept in this game's IndexedDB folder (never localStorage) */
	function askName(max, bad) {
		var FS = Module.FS, f = DIR + '/web-name', n = '';
		try { n = FS.readFile(f, { encoding: 'utf8' }); } catch (e) { }
		if (!n) { n = (prompt('What is your name, adventurer?', '') || '').replace(bad, '').trim().slice(0, max); if (n) { FS.writeFile(f, n); app.sync(); } }
		return n;
	}
	window.Module = {
		zp: zp,
		arguments: ['-u', 'player'],
		preRun: [function () {
			var FS = Module.FS;
			FS.mkdirTree(DIR);
			FS.mount(Module.IDBFS, {}, DIR);
			FS.chdir('/zapm');                   /* DATADIR "user" is relative */
			Module.ENV.USER = 'player';
			Module.addRunDependency('idbfs');
			FS.syncfs(true, function (err) {
				try { FS.mkdir(DIR + '/tmp'); } catch (e) { }   /* autosave writes here first */
				if (err) app.status('Could not read saved games from IndexedDB (' + err + '). Saving may not work in this browser mode.', true);
				var who = askName(30, /[,\n]/g);   /* the game plays as "player" (save file name); the name is for the run report */
				if (who) Module.ENV.ZAPM_NAME = who;
				Module.removeRunDependency('idbfs');
			});
		}],
		onRuntimeInitialized: function () {
			app.running = true;
			saveReq = true;          /* ZAPM deletes the save it loads: write it back at once */
			app.status('');
		},
		print: function (s) { console.log(s); },
		printErr: function (s) { console.warn(s); },
		setStatus: function (s) { if (s && !app.running) app.status(s.replace(/\(\d+\/\d+\)/, '').trim() || 'Loading…'); },
		onAbort: function (what) { app.crashed(what); }
	};

	/* autosave: every 2 minutes and when the page is hidden */
	setInterval(function () { saveReq = true; }, 120000);
	document.addEventListener('visibilitychange', function () { if (document.hidden) { saveReq = true; app.sync(); } });
	window.addEventListener('pagehide', function () { app.sync(); });
	window.addEventListener('beforeunload', function (e) { if (app.running) { e.preventDefault(); e.returnValue = ''; } });

	document.addEventListener('keydown', onKey);
	document.addEventListener('DOMContentLoaded', function () {
		RvipWM.dropdown($('btn-file'), $('menu-file'));
		RvipWM.fonts.then(function (list) {
			[[$('sel-font'), 'face'], [mapSel, 'mapFace']].forEach(function (a) {
				RvipWM.fontOptions(a[0]);
				a[0].value = (L && L[a[1]]) || '';
			});
		}).catch(function () { });
		[[$('sel-font'), 'face'], [mapSel, 'mapFace']].forEach(function (a) {
			a[0].onchange = function () { if (!L) return; L[a[1]] = this.value; saveLayout(); loadFace(this.value, true); this.blur(); };
		});
		$('btn-restart').onclick = function () { location.reload(); };
		document.querySelectorAll('button').forEach(function (b) {
			b.addEventListener('mousedown', function (e) { e.preventDefault(); });
		});
	});
	var resizeTimer = 0;
	window.addEventListener('resize', function () {
		if (!L) return;
		clearTimeout(resizeTimer);
		resizeTimer = setTimeout(function () {
			if (L.auto) {                        /* not customised: follow the window */
				var d = defaultLayout();
				if (d.tile !== L.tile) { L.tile = d.tile; shape(P_MAP); }
			}
			applyDom();
		}, 150);
	});
})();
