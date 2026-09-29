# ZAPM 0.8.3 (winny- fork) — RVIP port

Case O (C++, curses + panel). Source: https://github.com/winny-/ZAPM-winny
(shallow clone, upstream commit 0fd7af8); `git log`: upstream, then the port.
GitHub: memmaker/zapm.

- Web: `sh web/build.sh` → `web/dist`, `web/deploy.sh` →
  https://ruzzoli.de/roguelikes/zapm/. `port/be_web.cpp` + `web/zapm.js`
  (RvipWM windows Map, Messages, Status, Inventory, Visible; text windows are
  HTML). Save `user/player.sav` in IDBFS (`-u player`), autosave at the command
  prompt via a temp DataDir + rename, one right after load (ZAPM deletes the
  save it loads); `exitZapm()` calls `webEnd()` (drop the save unless saved
  with S, tell the page).
- Native (X11, for testing): `make -f port/Makefile` → `./zapm-x11` (objects in
  `port/obj`), `./play.sh`. Saves in `user/<name>.sav`. Upstream `make`
  (ncurses) still works with `BASE_LDFLAGS="-lpanel -lncurses"`.
- Port: `port/curses.h` + `wcurses.c` (in-memory curses + panels, routed to
  panes: map window → Map, side window → Status, log window → Messages with
  history, any other panel on top → pop-up sized to its content),
  `port/be_x11.c` (text only, curses colours, arrows/keypad → digits).
  `Rvip.cpp`: explore `X`, `<`/`>` stair walk, Enter menu, inventory with
  cursor + item menus, hooked via `shInterface::rvipCommand()` in
  `shHero::takeTurn()`. Small hooks: Interface.cpp/h (keys, help texts, msg
  counter, shMenu cursor), Hero.cpp (rvipCommand, interrupt stops walks),
  Inventory.cpp (item prompts open the cursor list at once).
- Prompt line: `be_prompt(r)` from `wnoutrefresh()` of the log window in
  `port/wcurses.c`, `js_key(RvipAtPrompt)` in `port/be_web.cpp`
  (`be_x11.c` has an empty stub).
- Fixed upstream bug (ASan): overflow in `ObjectSymbols[]` (12 entries for 13
  object types).
- Text only (no ZAPM tile set exists). No sound: upstream has none.
- Shift/Ctrl+letter item shortcuts don't exist: ZAPM uses a-zA-Z as item
  letters (use numpad - and *).

## Open
- Switching lists (inventory/equipment/floor) in item prompts; mouse.
