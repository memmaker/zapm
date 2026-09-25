/* RVIP additions (~/Games/rvip-tools/RVIP.md): auto-explore (X), '<'/'>' walk to the
 * nearest known stairs, command menu on Enter, inventory list with a cursor
 * and item menus, cursor list for item prompts. */
#include <string.h>
#include <stdlib.h>
#include <curses.h>
#include "Global.h"
#include "Util.h"
#include "Map.h"
#include "Interface.h"
#include "Hero.h"
#include "Monster.h"

int RvipAutoMore = 1;           /* --More-- after 5 log lines does not wait */
int RvipMsgs;                   /* bumped by shInterface::vp() */
int RvipSaved, RvipQuietSave;   /* web autosave (port/be_web.cpp) */
int RvipAtPrompt;               /* waiting for a command key */
static int mode;                /* 0 off, 1 explore, 2 to '>', 3 to '<' */
static int msgs0, reopen, opened;
static shMapLevel *lev;
static unsigned char seen[MAPMAXCOLUMNS][MAPMAXROWS], visited[MAPMAXCOLUMNS][MAPMAXROWS],
    tried[MAPMAXCOLUMNS][MAPMAXROWS];

void rvipStop () { mode = 0; }

/* A floating list with a cursor. keys[i] < 0: header row. Returns the
   index chosen, -1 on Escape. Letters choose their entry. */
static int
rvipList (const char *title, int n, const char **text, const int *keys, int cur = 0)
{
    int i, w = strlen (title), top = 0, rows;
    for (i = 0; i < n; i++) w = maxi (w, (int) strlen (text[i]) + 4);
    rows = mini (n, 22);
    while (cur < n && keys[cur] < 0) cur++;
    WINDOW *win = newwin (rows + 2, w + 2, 0, 0);
    PANEL *pan = new_panel (win);
    int res = -1;
    while (1) {
        char buf[128];
        if (cur < top) top = cur;
        if (cur >= top + rows) top = cur - rows + 1;
        werase (win);
        wattrset (win, A_BOLD);
        mvwaddstr (win, 0, 1, title);
        for (i = top; i < n && i < top + rows; i++) {
            if (keys[i] < 0)
                snprintf (buf, sizeof buf, "%-*s", w, text[i]);
            else
                snprintf (buf, sizeof buf, " %c  %-*s", keys[i] < ' ' ? '^' : keys[i],
                          w - 4, text[i]);
            if (keys[i] >= 0 && keys[i] < ' ') buf[2] = keys[i] + '@';
            wattrset (win, i == cur ? A_REVERSE : keys[i] < 0 ? ColorMap[kYellow] : A_NORMAL);
            mvwaddnstr (win, 1 + i - top, 1, buf, w);
        }
        wattrset (win, A_NORMAL);
        if (n > rows) mvwaddstr (win, rows + 1, 1, top + rows < n ? "--More--" : "--End--");
        wmove (win, 1 + cur - top, 1);
        int k = I->getChar (win);
        int step = 0;
        if ('8' == k || 'k' == k) step = -1;
        else if ('2' == k || 'j' == k) step = 1;
        else if ('5' == k || '\r' == k || '\n' == k || ' ' == k) { res = cur; break; }
        else if (27 == k || '0' == k) break;
        else {
            for (i = 0; i < n; i++) if (keys[i] == k) break;
            if (i < n) { res = i; break; }
        }
        if (step) {
            i = cur;
            do i += step; while (i >= 0 && i < n && keys[i] < 0);
            if (i >= 0 && i < n) cur = i;
        }
    }
    hide_panel (pan);
    del_panel (pan);
    delwin (win);
    I->drawScreen ();
    return res;
}

/* key bound to a command (reverse of mKey2Cmd) */
int
shInterface::keyFor (Command c)
{
    for (int k = ' ' + 1; k < 127; k++) if (mKey2Cmd[k] == c) return k;   /* printable first */
    for (int k = 1; k < 256; k++) if (mKey2Cmd[k] == c) return k;
    return -1;
}

static int
hostileInView ()
{
    for (int x = 0; x < MAPMAXCOLUMNS; x++)
        for (int y = 0; y < MAPMAXROWS; y++) {
            shCreature *c = Level->getCreature (x, y);
            /* sessile ones (fuel barrels, fungi) only when close */
            if (c && c != &Hero && Hero.canSee (c) && c->isHostile () &&
                (!c->isSessile () || maxi (abs (x - Hero.mX), abs (y - Hero.mY)) <= 2))
                return 1;
        }
    return 0;
}

static int
passable (int x, int y)
{
    shFeature *f = Level->getKnownFeature (x, y);
    if (!seen[x][y]) return 0;
    if (f && f->isTrap () && !f->mTrapUnknown) return 0;
    if (f && shFeature::kDoorClosed == f->mType) return !tried[x][y];
    if (Level->isObstacle (x, y) || Level->isWatery (x, y)) return 0;
    shCreature *c = Level->getCreature (x, y);
    return !c || c == &Hero || !Hero.canSee (c);
}

static int
target (int x, int y)
{
    if (2 == mode || 3 == mode) {
        shFeature *f = Level->getKnownFeature (x, y);
        return f && f->mType == (2 == mode ? shFeature::kStairsDown : shFeature::kStairsUp);
    }
    if (visited[x][y]) return 0;
    if (Level->countObjects (x, y)) return 1;
    for (int dx = -1; dx <= 1; dx++)
        for (int dy = -1; dy <= 1; dy++)
            if (Level->isInBounds (x + dx, y + dy) && !seen[x + dx][y + dy]) return 1;
    return 0;
}

/* next step of the current walk, or kNoCommand when done/stopped */
shInterface::Command
shInterface::rvipStep ()
{
    static short px[MAPMAXCOLUMNS][MAPMAXROWS], py[MAPMAXCOLUMNS][MAPMAXROWS];
    static int qx[MAPMAXCOLUMNS * MAPMAXROWS], qy[MAPMAXCOLUMNS * MAPMAXROWS];
    int x, y, h = 0, t = 0, found = 0;

    if (opened) msgs0 = RvipMsgs;   /* the door's own messages don't stop us */
    opened = 0;
    if (RvipMsgs != msgs0 || hostileInView () || wc_kbhit ()) return kNoCommand;
    visited[Hero.mX][Hero.mY] = 1;
    if (target (Hero.mX, Hero.mY) && mode > 1)
        return 2 == mode ? kMoveDown : kMoveUp;
    memset (px, -1, sizeof px);
    px[Hero.mX][Hero.mY] = Hero.mX; py[Hero.mX][Hero.mY] = Hero.mY;
    qx[t] = Hero.mX; qy[t++] = Hero.mY;
    while (h < t) {
        x = qx[h]; y = qy[h++];
        if ((x != Hero.mX || y != Hero.mY) && target (x, y)) { found = 1; break; }
        shFeature *door = Level->getKnownFeature (x, y);
        for (int d = 0; d < 8; d++) {
            int nx = x, ny = y;
            if (!Level->moveForward ((shDirection) d, &nx, &ny) || px[nx][ny] >= 0 ||
                !passable (nx, ny))
                continue;
            /* no diagonal steps through doorways */
            shFeature *nd = Level->getKnownFeature (nx, ny);
            if ((d & 1) && ((door && door->isDoor ()) || (nd && nd->isDoor ()))) continue;
            px[nx][ny] = x; py[nx][ny] = y;
            qx[t] = nx; qy[t++] = ny;
        }
    }
    if (!found) {
        p (1 == mode ? "Nothing left to explore (search for secret doors with s)." : "You don't know a way to those stairs.");
        return kNoCommand;
    }
    while (px[x][y] != Hero.mX || py[x][y] != Hero.mY) {
        int ox = x; x = px[ox][y]; y = py[ox][y];
    }
    shFeature *f = Level->getFeature (x, y);
    shDirection d = vectorDirection (x - Hero.mX, y - Hero.mY);
    if (f && shFeature::kDoorClosed == f->mType) {
        tried[x][y] = 1;        /* locked: skipped on the next press */
        wc_push (keyFor ((Command) (kMoveN + d)));
        opened = 1;
        return kOpen;
    }
    return (Command) (kMoveN + d);
}

void
wc_inv (WINDOW *w)
{
    int i, n = Hero.mInventory ? Hero.mInventory->count () : 0;
    werase (w);
    for (i = 0; i < n && i < w->maxy; i++) {
        shObject *o = Hero.mInventory->get (i);
        wmove (w, i, 0);
        wattrset (w, o->isWorn () || o->isWielded () ? ColorMap[kWhite] : A_NORMAL);
        char buf[128];
        snprintf (buf, sizeof buf, "%c - %s", o->mLetter, o->inv ());
        waddnstr (w, buf, w->maxx);
    }
    wattrset (w, A_NORMAL);
}

/* the actions that fit an item: command + label; first = main action */
static int
itemActions (shObject *o, shInterface::Command *c, const char **l)
{
    int n = 0;
#define ADD(_c, _l) (c[n] = shInterface::_c, l[n++] = _l)
    if (o->isA (kCanister)) ADD (kQuaff, "Quaff");
    if (o->isA (kFloppyDisk)) ADD (kExecute, "Execute");
    if (o->isA (kImplant)) { if (o->isWorn ()) ADD (kUninstall, "Uninstall"); else ADD (kInstall, "Install"); }
    if (o->isA (kArmor)) { if (o->isWorn ()) ADD (kTakeOff, "Take off"); else ADD (kWear, "Wear"); }
    if (o->isA (kRayGun)) ADD (kZapRayGun, "Zap");
    if (o->isUseable ()) ADD (kUse, "Use / apply");
    if ((o->isA (kWeapon) || o->isA (kRayGun)) && !o->isWielded ()) ADD (kWield, "Wield");
    if (o->isWielded () && o->isA (kWeapon)) ADD (kFireWeapon, "Fire");
    if (o->isThrownWeapon () || o->isA (kCanister)) ADD (kThrow, "Throw");
    ADD (kNoCommand, "Examine");
    ADD (kDrop, "Drop");
    ADD (kName, "Name");
    ADD (kAdjust, "Adjust letter");
#undef ADD
    return n;
}

/* 'i': list with a cursor. Letter = main action, Enter = action menu,
   numpad + main action, - drop, * examine, 0 / . / Esc close. */
shInterface::Command
shInterface::rvipInventory ()
{
    static int cur;
    while (1) {
        int n = Hero.mInventory->count (), i;
        if (!n) { p ("You aren't carrying anything!"); return kNoCommand; }
        const char *text[64];
        int keys[64];
        static char lines[64][100];
        for (i = 0; i < n && i < 64; i++) {
            shObject *o = Hero.mInventory->get (i);
            snprintf (lines[i], 100, "%s", o->inv ());
            text[i] = lines[i];
            keys[i] = o->mLetter;
        }
        if (cur >= n) cur = n - 1;
        /* read the key ourselves to tell letters from Enter */
        int k, pick = -1, act = 0;   /* act: 0 main, 1 menu, 2 drop, 3 examine */
        {
            const char *title = "Inventory (Enter: menu, -: drop, *: examine)";
            int top = 0, rows = mini (n, 22), w = strlen (title);
            for (i = 0; i < n; i++) w = maxi (w, (int) strlen (text[i]) + 4);
            WINDOW *win = newwin (rows + 2, w + 2, 0, 0);
            PANEL *pan = new_panel (win);
            while (pick < 0) {
                char buf[128];
                if (cur < top) top = cur;
                if (cur >= top + rows) top = cur - rows + 1;
                werase (win);
                wattrset (win, A_BOLD);
                mvwaddstr (win, 0, 1, title);
                for (i = top; i < n && i < top + rows; i++) {
                    snprintf (buf, sizeof buf, " %c  %-*s", keys[i], w - 4, text[i]);
                    wattrset (win, i == cur ? A_REVERSE : A_NORMAL);
                    mvwaddnstr (win, 1 + i - top, 1, buf, w);
                }
                wattrset (win, A_NORMAL);
                wmove (win, 1 + cur - top, 1);
                k = getChar (win);
                if ('8' == k) { if (cur > 0) cur--; }
                else if ('2' == k) { if (cur < n - 1) cur++; }
                else if ('5' == k || '\r' == k || '\n' == k || ' ' == k) { pick = cur; act = 1; }
                else if ('+' == k) { pick = cur; act = 0; }
                else if ('-' == k) { pick = cur; act = 2; }
                else if ('*' == k) { pick = cur; act = 3; }
                else if (27 == k || '0' == k || '.' == k) break;
                else {
                    for (i = 0; i < n; i++) if (keys[i] == k) { pick = cur = i; act = 0; }
                    if (pick < 0) { wc_push (k); break; }  /* any other key: a command */
                }
            }
            hide_panel (pan);
            del_panel (pan);
            delwin (win);
            drawScreen ();
        }
        if (pick < 0) return kNoCommand;
        shObject *o = Hero.mInventory->get (pick);
        Command c[16];
        const char *l[16];
        int na = itemActions (o, c, l);
        if (1 == act) {
            int ak[16];
            char title[100];
            for (i = 0; i < na; i++) ak[i] = kNoCommand == c[i] ? '*' : keyFor (c[i]);
            snprintf (title, sizeof title, "%c - %s", o->mLetter, o->inv ());
            i = rvipList (title, na, l, ak);
            if (i < 0) continue;
            act = 9 + i;
        }
        Command cmd = 0 == act ? c[0] : 2 == act ? kDrop : 3 == act ? kNoCommand : c[act - 9];
        if (kNoCommand == cmd) {
            pageLog ();
            p ("%c - %s", o->mLetter, o->inv ());
            continue;
        }
        wc_push (o->mLetter);
        reopen = 1;
        return cmd;
    }
}

/* Enter: every command, grouped like the help (movement, then commands) */
int
shInterface::rvipMenu ()
{
    const char *text[KEY_MAX / 2];
    int keys[KEY_MAX / 2], n = 0;
    static const struct { int cmd; const char *t; } mv[] = {
        { kExplore, "Explore automatically" }, { kMoveDown, "Go down (walks to known stairs)" },
        { kMoveUp, "Go up (walks to known stairs)" }, { kGlide, "Glide: move until something is found" },
        { kRest, "Rest for one second" }, { kSearch, "Search for traps and secret doors" }, { 0, 0 } };
    text[n] = "Movement"; keys[n++] = -1;
    for (int i = 0; mv[i].t; i++) { text[n] = mv[i].t; keys[n++] = keyFor ((Command) mv[i].cmd); }
    text[n] = "Commands"; keys[n++] = -1;
    for (int k = 1; k < 256; k++) {
        Command c = mKey2Cmd[k];
        if (c < kAdjust || c >= kGodMode || kExplore == c || kSearch == c || kEnter == c ||
            kRest == c || kGlide == c || kMoveUp == c || kMoveDown == c)
            continue;
        if (keyFor (c) != k) continue;   /* list each command once */
        text[n] = mCommandHelp[c]; keys[n++] = k;
    }
    int i = rvipList ("Commands", n, text, keys);
    return i < 0 ? 0 : keys[i];
}

/* replaces getCommand () in shHero::takeTurn () */
shInterface::Command
shInterface::rvipCommand ()
{
    if (lev != Level) {
        lev = Level; mode = 0;
        memset (seen, 0, sizeof seen); memset (visited, 0, sizeof visited);
        memset (tried, 0, sizeof tried);
    }
    for (int x = 0; x < MAPMAXCOLUMNS; x++)
        for (int y = 0; y < MAPMAXROWS; y++)
            if (Hero.canSee (x, y) || ' ' != (Level->getMemory (x, y) & A_CHARTEXT)) seen[x][y] = 1;
    if (mode) {
        Command c = rvipStep ();
        if (kNoCommand != c) { msgs0 = RvipMsgs; return c; }
        mode = 0;
    }
    if (reopen) {
        reopen = 0;
        if (!hostileInView ()) { Command c = rvipInventory (); if (kNoCommand != c) return c; }
    }
    while (1) {
        RvipAtPrompt = 1;
        Command c = getCommand ();
        RvipAtPrompt = 0;
        int onstairs = 0;
        shFeature *f = Level->getFeature (Hero.mX, Hero.mY);
        if (kEnter == c) {
            int k = rvipMenu ();
            if (k) wc_push (k);
            continue;
        }
        if (kListInventory == c) {
            Hero.reorganizeInventory ();
            c = rvipInventory ();
            if (kNoCommand == c) continue;
            return c;
        }
        if (kMoveDown == c)
            onstairs = Hero.isTrapped () || (f && (shFeature::kStairsDown == f->mType ||
                shFeature::kHole == f->mType || shFeature::kTrapDoor == f->mType ||
                shFeature::kPit == f->mType || shFeature::kAcidPit == f->mType ||
                shFeature::kSewagePit == f->mType));
        if (kMoveUp == c) onstairs = (f && shFeature::kStairsUp == f->mType) || Hero.isTrapped ();
        if (kExplore == c || ((kMoveDown == c || kMoveUp == c) && !onstairs)) {
            mode = kExplore == c ? 1 : kMoveDown == c ? 2 : 3;
            if (hostileInView ()) { p ("Not with a monster in view."); mode = 0; continue; }
            msgs0 = RvipMsgs;
            c = rvipStep ();
            if (kNoCommand == c) { mode = 0; continue; }
            return c;
        }
        return c;
    }
}
