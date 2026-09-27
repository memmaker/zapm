/* Browser frontend for the curses shim (RVIP step 7): web/zapm.js draws the
 * panes (Module.zp); input waits with Asyncify. user/player.sav is an
 * autosave while playing (ZAPM deletes it on load), removed at exit unless
 * the player saved with S. No sound: ZAPM has no sound effects. */
#include <emscripten.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "Global.h"
#include "Hero.h"
#include "Map.h"
#include "Object.h"

extern int RvipSaved, RvipQuietSave, RvipAtPrompt;
int saveGame ();

EM_JS(void, js_init, (int p, int c, int r), { Module.zp.init(p, c, r); });
EM_JS(void, js_put, (int p, int y, int x, int ch), { Module.zp.put(p, y, x, ch); });
EM_JS(void, js_cursor, (int p, int y, int x), { Module.zp.cursor(p, y, x); });
EM_JS(void, js_popup, (int r, int c), { Module.zp.popup(r, c); });
EM_JS(void, js_flush, (int hy, int hx), { Module.zp.flush(hy, hx); });
EM_JS(int, js_key, (int at_cmd), { return Module.zp.key(at_cmd); });
EM_JS(void, js_prompt, (const char *s), { Module.zp.prompt(UTF8ToString(s)); });
void be_prompt(const char *s) { js_prompt(s); }
EM_JS(int, js_want_save, (void), { return Module.zp.wantSave(); });
EM_JS(void, js_end, (int saved), { Module.zp.end(saved); });

void webEnd()
{
    char f[ZAPM_PATH_LENGTH];
    snprintf(f, sizeof f, "%s/%s.sav", DataDir, Hero.mName);
    if (!RvipSaved) unlink(f);      /* died or quit: the game is over */
    js_end(RvipSaved);
}

extern "C" {
void be_init(int p, int cols, int rows) { js_init(p, cols, rows); }
void be_put(int p, int y, int x, chtype ch, int, int) { js_put(p, y, x, ch); }
void be_cursor(int p, int y, int x) { js_cursor(p, y, x); }
void be_popup(int rows, int cols) { js_popup(rows, cols); }

/* Visible window (RVIP 5b): creatures the hero sees and objects on seen
 * squares, in the game's own colours */
EM_JS(void, js_vis, (const char *s), { if (Module.zp.vis) Module.zp.vis(UTF8ToString(s)); });
static const char *vcolor (int c)
{
    static const char *pal[] = { "#000", "#c33", "#3b3", "#cc3", "#35d", "#c3c", "#3cc", "#bbb", "#f55", "#5f5", "#ff5", "#58f", "#f5f", "#5ff", "#fff", "#900", "#070", "#a60", "#009" };
    return c >= 0 && c < (int) (sizeof pal / sizeof *pal) ? pal[c] : "";
}

static void sendVisible ()
{
    static char buf[4096];
    int n = 0;
    if (!Level) { js_vis (""); return; }
    for (int i = 0; i < Level->mCrList.count () && n < 3900; i++) {
        shCreature *c = Level->mCrList.get (i);
        if (!c || c == (&Hero) || !(&Hero)->canSee (c)) continue;
        n += snprintf (buf + n, sizeof buf - n, "M%c%s\t%s\n", c->mGlyph.mChar, c->getDescription (), vcolor (c->mGlyph.mForeground));
    }
    for (int x = 0; x < MAPMAXCOLUMNS; x++)
        for (int y = 0; y < MAPMAXROWS && n < 3900; y++) {
            shObjectVector *v = Level->mObjects[x][y];
            if (!v || !(&Hero)->canSee (x, y)) continue;
            for (int i = 0; i < v->count () && n < 3900; i++) {
                shObject *o = v->get (i);
                shGlyph g = o->mIlk->mGlyph;
                n += snprintf (buf + n, sizeof buf - n, "I%c%s\t%s\n", g.mChar, o->getDescription (), vcolor (g.mForeground));
            }
        }
    buf[n] = 0;
    js_vis (buf);
}

void be_flush(void) { sendVisible (); js_flush(Hero.mY, Hero.mX); }
void be_sound(const char *) { }
void be_end(void) { }

/* Save and keep playing: write into save/tmp, then rename over the save. */
static void autosave(void)
{
    char dir[ZAPM_PATH_LENGTH], tmp[ZAPM_PATH_LENGTH], sav[ZAPM_PATH_LENGTH];
    if (!Hero.mName[0] || Hero.mHP <= 0) return;
    strcpy(dir, DataDir);
    snprintf(tmp, sizeof tmp, "%s/tmp/%s.sav", dir, Hero.mName);
    snprintf(sav, sizeof sav, "%s/%s.sav", dir, Hero.mName);
    snprintf(DataDir, ZAPM_PATH_LENGTH, "%s/tmp", dir);
    unlink(tmp);
    RvipQuietSave = 1;
    int ok = 0 == saveGame();
    RvipQuietSave = 0;
    strcpy(DataDir, dir);
    if (ok) rename(tmp, sav);
}

int be_getkey(int wait)
{
    int k;
    for (;;) {
        if (RvipAtPrompt && js_want_save()) autosave();
        if ((k = js_key(RvipAtPrompt)) >= 0) return k;
        if (!wait) {                /* polling (explore, each step): paint it, 40 ms */
            emscripten_sleep(40);
            return (k = js_key(RvipAtPrompt)) >= 0 ? k : -1;
        }
        emscripten_sleep(10);
    }
}
}

/* animations (explosions, rays) sleep through the page (-Dusleep=wc_usleep) */
extern "C" int wc_usleep(useconds_t us)
{
    be_flush();
    emscripten_sleep(us / 1000);
    return 0;
}

/* Run report (roguelikes-index/server/CONTRACT.md): fire-and-forget GET,
   never throws, offline just fails silently. Negative ints are omitted. */
EM_JS(void, js_beacon, (const char *g, const char *ev, const char *name, const char *killer, int depth, int score, int turns, int lvl), {
    try {
        var p = [['g', UTF8ToString(g)], ['ev', UTF8ToString(ev)], ['name', name ? UTF8ToString(name) : ''],
                 ['killer', killer ? UTF8ToString(killer) : ''], ['depth', depth], ['score', score], ['turns', turns], ['lvl', lvl]];
        var q = p.filter(function (a) { return a[1] !== '' && !(a[1] < 0); })
                 .map(function (a) { return a[0] + '=' + encodeURIComponent(a[1]); }).join('&');
        if (window.RvipWM && RvipWM.report) RvipWM.report(q); else fetch('/roguelikes/beacon?' + q, { keepalive: true, mode: 'no-cors' }).catch(function () {});
    } catch (e) {}
});
/* Called from shHero::die after logGame (score final). Clock is in ms of game
   time; one normal-speed turn is 1000. */
void be_run_end (int how, const char *k, int score)
{
    const char *ev = "death";
    if (kWonGame == how) ev = "win", k = NULL;
    else if (kQuitGame == how) ev = "quit", k = NULL;
    else if (k && !strncmp (k, "a ", 2)) k += 2;
    else if (k && !strncmp (k, "an ", 3)) k += 3;
    else if (k && !strncmp (k, "the ", 4)) k += 4;
    js_beacon ("zapm", ev, getenv ("ZAPM_NAME"), k, Level ? Level->mDLevel : -1, score, Clock / 1000, Hero.mCLevel);
}
