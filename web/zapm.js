/*
 * ZAPM in the browser: draws the panes of the curses shim (port/be_web.cpp
 * calls Module.zp), keyboard input, tiling windows, saves in IndexedDB.
 * Text only, curses colours; no sound (ZAPM has none). Copied from
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
	var FONT_MIN = 8, FONT_MAX = 28;
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
	var running = false, saveReq = false;
	var cur = { p: -1, y: 0, x: 0 };
	var dpr = Math.max(1, Math.min(3, window.devicePixelRatio || 1));
	var L = null, rects = {};

	function $(id) { return document.getElementById(id); }
	function clamp(v, lo, hi) { return Math.max(lo, Math.min(hi, v)); }
	function status(msg, isError) {
		var s = $('status');
		s.textContent = msg;
		s.className = isError ? 'error' : '';
		s.hidden = !msg;
	}

	/* ---------- panes ---------- */

	function measure(px) {
		var c = document.createElement('canvas').getContext('2d');
		c.font = px + 'px ' + FONT;
		return Math.ceil(c.measureText('M').width);
	}

	/* Cell size from the zoom settings; rebuilds the canvas and redraws */
	function shape(p) {
		var T = panes[p];
		var f = p === P_MAP ? L.tile : p === P_POP ? L.font.pop : L.font[WIN[p]];
		T.cw = measure(f); T.ch = Math.round(f * 1.3); T.pad = p === P_POP ? T.cw : 0;
		T.font = (p === P_MAP ? 'bold ' : '') + f + 'px ' + FONT;
		var w = T.cols * T.cw + 2 * T.pad, h = T.rows * T.ch + 2 * T.pad;
		T.cv.width = Math.round(w * dpr); T.cv.height = Math.round(h * dpr);
		T.w = w; T.h = h;
		T.ctx = T.cv.getContext('2d');
		T.ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
		T.ctx.fillStyle = BG; T.ctx.fillRect(0, 0, w, h);
		for (var i = 0; i < T.cols * T.rows; i++) draw(p, (i / T.cols) | 0, i % T.cols);
		fit(p);
	}

	function makePane(p, cols, rows) {
		var cv = p === P_POP ? document.querySelector('#pop canvas') : document.querySelector('#t-' + WIN[p] + ' canvas');
		var n = cols * rows;
		panes[p] = { cv: cv, cols: cols, rows: rows, ch_: new Int32Array(n).fill(32) };
		shape(p);
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
		var font = W >= 1600 ? 14 : 13, cw = measure(font), ch = Math.round(font * 1.3);
		var sideW = 50 * cw + BORDER + GUT, tile = TILE_STEPS[0];
		TILE_STEPS.forEach(function (t) {
			if (64 * measure(t) + BORDER <= W - sideW && 20 * Math.round(t * 1.3) + BORDER <= H - 13 * ch - TITLE_H) tile = t;
		});
		var mapH = 20 * Math.round(tile * 1.3) + BORDER, statH = 20 * ch + TITLE_H + BORDER;
		return { v: 1, tile: tile, auto: true, font: { msg: font, stat: font, inv: font, pop: font },
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
				Object.keys(d.font).forEach(function (k) {
					if (s.font && s.font[k] >= FONT_MIN && s.font[k] <= FONT_MAX) d.font[k] = s.font[k];
				});
				if (s.wm) d.wm = s.wm;
			}
		} catch (err) { /* nothing saved yet */ }
		L = d;
	}

	var saveTimer = 0;
	function saveLayout() {
		clearTimeout(saveTimer);
		saveTimer = setTimeout(function () {
			try { Module.FS.writeFile(LAYOUT_FILE, JSON.stringify(L)); syncFiles(); }
			catch (err) { console.warn('layout not saved', err); }
		}, 400);
	}

	function place(el, r) {
		el.style.left = r[0] + 'px'; el.style.top = r[1] + 'px';
		el.style.width = Math.max(0, r[2]) + 'px'; el.style.height = Math.max(0, r[3]) + 'px';
	}

	/* Show a canvas at its size, or scaled down to fit its window (never clipped) */
	function fit(p) {
		var T = panes[p];
		if (!T) return;
		var box;
		if (p === P_POP) {
			if (!rects.map) return;
			var m = rects.map, A = areaSize();
			box = { w: A.w - m[0] - 16, h: A.h - 8 };
		} else {
			var r = rects[WIN[p]];
			if (!r) return;
			box = { w: r[2] - BORDER, h: r[3] - BORDER - ($('game').classList.contains('wm-single') ? 0 : TITLE_H) };
		}
		/* the map never shrinks: bigger than its window, it scrolls with the hero */
		if (p === P_MAP) { T.box = box; scrollMap(true); return; }
		var sc = Math.min(1, box.w / T.w, box.h / T.h);
		T.cv.style.width = T.w * sc + 'px';
		T.cv.style.height = T.h * sc + 'px';
		if (p === P_POP) { var pop = $('pop'); pop.style.left = rects.map[0] + 8 + 'px'; pop.style.top = rects.map[1] + 4 + 'px'; }
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
	function zoomList(d) {
		L.font.vis = clamp((L.font.vis || 13) + d, FONT_MIN, FONT_MAX);
		document.querySelector('#t-vis .body').style.fontSize = L.font.vis + 'px';
		saveLayout();
	}
	function applyDom() { if (wm) wm.apply(); }
	function makeWM() {
		var s = defaultLayout().split, A = areaSize();
		var line = Math.round(L.font.msg * 1.3) + 4, stat = Math.round(L.font.stat * 1.3) + 4;
		wm = RvipWM({
			area: $('game'), menu: $('btn-layout'),
			wins: [{ id: 'map', title: 'Map' }, { id: 'msg', title: 'Messages' }, { id: 'stat', title: 'Status' }, { id: 'inv', title: 'Inventory' }, { id: 'vis', title: 'Visible' }],
			multi: { d: 'h', r: s.side, a: { d: 'v', r: s.bottom, a: 'map', b: 'msg' }, b: { d: 'v', r: s.stat, a: 'stat', b: { d: 'v', r: 0.6, a: 'inv', b: 'vis' } } },
			single: { d: 'v', r: line / A.h, a: 'msg', b: { d: 'v', r: 1 - stat / (A.h - line), a: 'map', b: 'stat' } },
			state: L.wm, noFont: 'map',
			save: function (st) { L.wm = st; saveLayout(); },
			layout: function (r) { rects = r; WIN.forEach(function (id, p) { fit(p); }); fit(P_POP); },
			font: function (id, d) { if (id === 'vis') zoomList(d); else zoomText(id, d); },
			onReset: resetLayout
		});
		wm.apply();
	}

	function zoomMap(d) {
		var i = clamp(TILE_STEPS.indexOf(L.tile) + d, 0, TILE_STEPS.length - 1);
		L.tile = TILE_STEPS[i]; L.auto = false;
		shape(P_MAP); applyDom(); saveLayout();
		status('Map font: ' + L.tile + ' px');
		setTimeout(function () { status(''); }, 1200);
	}

	function zoomText(id, d) {
		var ids = [id];
		ids.forEach(function (k) { L.font[k] = clamp(L.font[k] + d, FONT_MIN, FONT_MAX); });
		L.font.pop = L.font[ids[0]];            /* pop-ups follow the last zoomed window */
		WIN.forEach(function (w, p) { if (p && ids.indexOf(w) >= 0) shape(p); });
		if (panes[P_POP]) shape(P_POP);
		applyDom(); saveLayout();
	}

	function resetLayout() {
		var a = L.audio;
		L = defaultLayout(); L.audio = a; L.wm = wm.state();
		for (var p = 0; p < panes.length; p++) if (panes[p]) shape(p);
		applyDom(); saveLayout();
	}

	/* ---------- called by the game (port/be_web.c) ---------- */
	var zp = {
		init: function (p, cols, rows) {
			if (!L) loadLayout();
			makePane(p, cols, rows);
			if (p === P_INV) { $('game').hidden = false; if (L.font.vis) document.querySelector('#t-vis .body').style.fontSize = L.font.vis + 'px'; makeWM(); }
		},
		put: function (p, y, x, ch) {
			var T = panes[p];
			if (!T || y < 0 || x < 0 || y >= T.rows || x >= T.cols) return;
			T.ch_[y * T.cols + x] = ch;
			draw(p, y, x);
		},
		cursor: function (p, y, x) { cur.p = p; cur.y = y; cur.x = x; },
		popup: function (rows, cols) {
			if (!rows) { $('pop').hidden = true; panes[P_POP] = null; if (cur.p === P_POP) cur.p = -1; return; }
			makePane(P_POP, cols, rows);
			$('pop').hidden = false;
			fit(P_POP);
		},
		flush: function (hy, hx) {
			if (hy !== hero.y || hx !== hero.x) { hero.y = hy; hero.x = hx; scrollMap(false); }
			/* the cursor is drawn over the cell; redraw that cell next time */
			if (zp.lastCur && panes[zp.lastCur.p]) draw(zp.lastCur.p, zp.lastCur.y, zp.lastCur.x);
			drawCursor();
			zp.lastCur = cur.p >= 0 ? { p: cur.p, y: cur.y, x: cur.x } : null;
		},
		vis: function (s) { RvipWM.visible(document.querySelector('#t-vis .body'), s); },
		key: function (atCmd) { RvipWM.prompt.wait(atCmd); return events.length ? events.shift() : -1; },
		prompt: function (s) { RvipWM.prompt.text(s); },
		requestSave: function () { saveReq = true; },   /* also for testing */
		wantSave: function () {
			if (!saveReq || !running) return 0;
			saveReq = false;
			setTimeout(syncFiles, 0);           /* after the game wrote the file */
			return 1;
		},
		end: function (saved) {
			running = false;
			syncFiles(function () {
				$('overlay-msg').textContent = saved ? 'Your game has been saved. Play again to continue it.'
					: 'The game is over.';
				$('overlay').hidden = false;
			});
		}
	};

	/* ---------- input ---------- */
	function onKey(e) {
		if (!$('help').hidden) {
			if (e.key === 'Escape') { $('help').hidden = true; e.preventDefault(); }
			return;
		}
		if (!running || e.isComposing || e.metaKey) return;
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
	var syncing = false, syncAgain = false, pendingCbs = [];
	function syncFiles(cb) {
		if (!Module.FS) { if (cb) cb(); return; }
		if (typeof cb === 'function') pendingCbs.push(cb);
		if (syncing) { syncAgain = true; return; }
		syncing = true;
		var cbs = pendingCbs; pendingCbs = [];
		Module.FS.syncfs(false, function (err) {
			syncing = false;
			if (err) status('Saving to browser storage (IndexedDB) failed: ' + err + '. Use "Export save" to keep a copy.', true);
			cbs.forEach(function (f) { f(err); });
			if (syncAgain) { syncAgain = false; syncFiles(); }
		});
	}
	function hasSave() { try { Module.FS.stat(SAVE); return true; } catch (e) { return false; } }
	function exportSave() {
		if (running) saveReq = true;
		setTimeout(function () {
			if (!hasSave()) { status('There is no saved game yet.', true); return; }
			var a = document.createElement('a');
			a.href = URL.createObjectURL(new Blob([Module.FS.readFile(SAVE)], { type: 'application/octet-stream' }));
			a.download = 'player.sav';
			document.body.appendChild(a); a.click();
			setTimeout(function () { URL.revokeObjectURL(a.href); a.remove(); }, 1000);
		}, running ? 1500 : 0);
	}
	function importSave(file) {
		var r = new FileReader();
		r.onload = function () {
			if (!confirm('Replace the current game with "' + file.name + '"?')) return;
			running = false;
			Module.FS.writeFile(SAVE, new Uint8Array(r.result));
			syncFiles(function (err) { if (!err) location.reload(); });
		};
		r.readAsArrayBuffer(file);
	}
	function newGame() {
		if (!confirm('Delete the saved game in this browser and start a new one?')) return;
		running = false;
		if (hasSave()) Module.FS.unlink(SAVE);
		syncFiles(function (err) { if (!err) location.reload(); });
	}

	/* ---------- help ---------- */
	var helpLoaded = false;
	function toggleHelp() {
		var h = $('help');
		h.hidden = !h.hidden;
		if (!h.hidden && !helpLoaded) {
			helpLoaded = true;
			fetch('help.html').then(function (r) { if (!r.ok) throw new Error(r.status); return r.text(); })
				.then(function (t) { $('help-body').innerHTML = t; })
				.catch(function (err) { helpLoaded = false; $('help-body').textContent = 'Could not load the guide (' + err + '). Press ? in the game for its own help.'; });
		}
		if (!h.hidden) $('help-body').focus();
	}

	/* ---------- startup ---------- */
	window.Module = {
		zp: zp,
		arguments: ['-u', 'player'],
		preRun: [function () {
			var FS = Module.FS;
			FS.mkdirTree(DIR);
			FS.mount(Module.IDBFS, {}, DIR);
			FS.chdir('/zapm');                   /* DATADIR "user" is relative */
			Module.ENV.USER = 'player';
			var who = '';                        /* the game plays as "player" (save file name), so ask once for the run report */
			try { who = localStorage.getItem('zapm-name') || ''; } catch (err) { /* no storage */ }
			if (!who) { who = (prompt('What is your name, adventurer?', '') || '').replace(/[,\n]/g, '').trim().slice(0, 30); try { if (who) localStorage.setItem('zapm-name', who); } catch (err) { /* no storage */ } }
			if (who) Module.ENV.ZAPM_NAME = who;
			Module.addRunDependency('idbfs');
			FS.syncfs(true, function (err) {
				try { FS.mkdir(DIR + '/tmp'); } catch (e) { }   /* autosave writes here first */
				if (err) status('Could not read saved games from IndexedDB (' + err + '). Saving may not work in this browser mode.', true);
				Module.removeRunDependency('idbfs');
			});
		}],
		onRuntimeInitialized: function () {
			running = true;
			saveReq = true;          /* ZAPM deletes the save it loads: write it back at once */
			status('');
		},
		print: function (s) { console.log(s); },
		printErr: function (s) { console.warn(s); },
		setStatus: function (s) { if (s && !running) status(s.replace(/\(\d+\/\d+\)/, '').trim() || 'Loading…'); },
		onAbort: function (what) { crashed(what); }
	};

	function crashed(err) {
		if (!running) return;
		running = false;
		var msg = (err && (err.message || err.reason && err.reason.message)) || String(err);
		console.error('[zapm] crash:', err);
		status('The game crashed (' + msg + '). Reload the page to continue from the last autosave.', true);
	}
	window.addEventListener('unhandledrejection', function (e) {
		/* exit() unwinds with an ExitStatus; that is the normal end */
		if (e.reason && e.reason.name === 'ExitStatus') return;
		crashed(e.reason);
	});
	window.addEventListener('error', function (e) {
		if (e.error && e.error.name === 'ExitStatus') return;
		if (e.error instanceof WebAssembly.RuntimeError || /zapm-core/.test(e.filename || '')) crashed(e.error || e.message);
	});

	/* autosave: every 2 minutes and when the page is hidden */
	setInterval(function () { saveReq = true; }, 120000);
	document.addEventListener('visibilitychange', function () { if (document.hidden) saveReq = true; });
	window.addEventListener('beforeunload', function (e) { if (running) { e.preventDefault(); e.returnValue = ''; } });

	document.addEventListener('keydown', onKey);
	document.addEventListener('DOMContentLoaded', function () {
		$('btn-export').onclick = exportSave;
		$('btn-import').onclick = function () { $('import-file').click(); };
		$('import-file').onchange = function () { if (this.files[0]) importSave(this.files[0]); this.value = ''; };
		$('btn-new').onclick = newGame;
		$('btn-help').onclick = toggleHelp;
		$('help-close').onclick = toggleHelp;
		$('btn-zoom-in').onclick = function () { zoomMap(1); };
		$('btn-zoom-out').onclick = function () { zoomMap(-1); };
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
