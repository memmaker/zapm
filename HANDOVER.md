# ZAPM 0.8.3 (winny- fork) — RVIP import (2026-09-25)

Case O (C++, curses + panel). Source: https://github.com/winny-/ZAPM-winny
(shallow clone, upstream commit 0fd7af8); `git log`: upstream, then the port.

- Build: `make -f port/Makefile` → `./zapm-x11` (objects in `port/obj`).
  Upstream `make` (ncurses) still works with `BASE_LDFLAGS="-lpanel -lncurses"`.
- Run: `./play.sh` or `~/Desktop/Games/Roguelikes/ZAPM.app`. Saves in `user/<name>.sav`
  (ZAPM asks for the name; `-u name` skips the question). Docs: `Docs/zapm.html`.
- Port: `port/curses.h` + `wcurses.c` (in-memory curses + panels, routed to panes:
  map window → Map, side window → Status, log window → Messages with history,
  any other panel on top → pop-up sized to its content), `port/be_x11.c`
  (from XRogue, text only, curses colours, arrows/keypad → digits).
  `Rvip.cpp`: explore `X`, `<`/`>` stair walk, Enter menu, inventory (3c),
  hooked via `shInterface::rvipCommand()` in `shHero::takeTurn()`.
  Small hooks: Interface.cpp/h (keys, help texts, msg counter, shMenu cursor),
  Hero.cpp (rvipCommand, interrupt stops walks), Inventory.cpp (item prompts open
  the cursor list at once).
- Text only: the user rejected NetHack tiles (no ZAPM tile set exists).
- ASan: upstream overflow in `ObjectSymbols[]` (12 entries for 13 object types),
  fixed. Clean afterwards (new game, explore, menus, inventory, save, restore).
- Tested: char creation (cursor pick), explore + doors + stop on monsters,
  secret-door dead ends, `>` walk + descend, inventory quaff + reopen, Enter
  menu → command, save/restore.
- Web (step 7): `sh web/build.sh` → `web/dist`, `web/deploy.sh` →
  https://ruzzoli.de/roguelikes/zapm/. `port/be_web.cpp` + `web/zapm.js` (from
  XRogue's, text only, curses colours; layout map | Status/Inventory column,
  Messages under the map). Save `user/player.sav` in IDBFS (`-u player`),
  autosave at the command prompt via a temp DataDir + rename, one right after
  load (ZAPM deletes the save it loads); `exitZapm()` calls `webEnd()` (drop
  the save unless saved with S, tell the page). GitHub: memmaker/zapm.
- No sound: upstream has no sound effects (user: sound only if upstream has sfx).
- Not done: switching lists
  (inventory/equipment/floor) in item prompts; mouse. Shift/Ctrl+letter item
  shortcuts don't exist: ZAPM uses a-zA-Z as item letters (use numpad - and *).
