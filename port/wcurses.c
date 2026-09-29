/* In-memory curses + panels for ZAPM. Windows are routed to frontend panes:
 * the map window, the side window (Status), the log window
 * (Messages: history + the live rows) and any other panel on top (help,
 * menus, history...) as a pop-up sized to the cells it really uses.
 * Cut down from ~/Games/xrogue/port/wcurses.c. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "curses.h"

WINDOW *stdscr, *wc_mapwin, *wc_sidewin, *wc_logwin;
int LINES = 25, COLS = 80, COLORS = 8, COLOR_PAIRS = 64;
#define HIST 200           /* message history rows */
#define LIVE 5              /* live log rows */
#define MAXINV 28
static WINDOW *pn[NPANES];
static PANEL *ptop;         /* panel stack, top */
static int pop_h, pop_w;
static WINDOW *pop_win;     /* window shown as the pop-up */

WINDOW *newwin(int rows, int cols, int by, int bx)
{
    WINDOW *w = calloc(1, sizeof *w);
    if (!rows) rows = LINES - by;
    if (!cols) cols = COLS - bx;
    w->maxy = rows; w->maxx = cols; w->begy = by; w->begx = bx;
    w->c = malloc(sizeof(chtype) * rows * cols);
    w->first = malloc(sizeof(short) * rows);
    w->last = malloc(sizeof(short) * rows);
    werase(w);
    return w;
}

int delwin(WINDOW *w)
{
    if (!w) return ERR;
    if (w == pop_win) pop_win = NULL;
    free(w->c); free(w->first); free(w->last); free(w);
    return OK;
}

WINDOW *initscr(void)
{
    if (!stdscr) {
        stdscr = newwin(LINES, COLS, 0, 0);
        pn[P_STATUS] = newwin(20, 16, 0, 0);
        pn[P_MSG] = newwin(HIST + LIVE, COLS, 0, 0);
        pn[P_INV] = newwin(MAXINV, 50, 0, 0);
    }
    return stdscr;
}

/* the game's own windows (called from shInterface's constructor) */
void wc_windows(WINDOW *map, WINDOW *side, WINDOW *log)
{
    wc_mapwin = map; wc_sidewin = side; wc_logwin = log;
    be_init(P_MAP, map->maxx, map->maxy);
    be_init(P_STATUS, pn[P_STATUS]->maxx, pn[P_STATUS]->maxy);
    be_init(P_MSG, pn[P_MSG]->maxx, pn[P_MSG]->maxy);
    be_init(P_INV, pn[P_INV]->maxx, pn[P_INV]->maxy);
}

int endwin(void) { return OK; }

static void touch(WINDOW *w, int y, int x)
{
    if (w->first[y] < 0 || x < w->first[y]) w->first[y] = x;
    if (x > w->last[y]) w->last[y] = x;
}

static void untouch(WINDOW *w)
{
    int y;
    for (y = 0; y < w->maxy; y++) w->first[y] = w->last[y] = -1;
}

static void pset(WINDOW *p, int y, int x, chtype ch)
{
    if (y < 0 || x < 0 || y >= p->maxy || x >= p->maxx || p->c[y * p->maxx + x] == ch) return;
    p->c[y * p->maxx + x] = ch;
    touch(p, y, x);
}

/* Messages pane, filled from the top: the nhist history rows in use, then
 * the live log rows, then blank rows; a full history scrolls. */
static chtype hb[HIST][256];
static int nhist;

static void msg_compose(void)
{
    WINDOW *p = pn[P_MSG], *w = wc_logwin;
    int y, x, row = 0, live = 0;
    if (!w) return;
    for (y = 0; y < LIVE && y < w->maxy; y++)
        for (x = 0; x < w->maxx; x++)
            if ((w->c[y * w->maxx + x] & A_CHARTEXT) != ' ') live = y + 1;
    if (w->cury < LIVE && w->cury + 1 > live && w->curx > 0) live = w->cury + 1;
    for (y = 0; y < nhist; y++, row++)
        for (x = 0; x < p->maxx; x++) pset(p, row, x, hb[y][x]);
    for (y = 0; y < live; y++, row++)
        for (x = 0; x < p->maxx; x++) pset(p, row, x, x < w->maxx ? w->c[y * w->maxx + x] : ' ');
    for (; row < p->maxy; row++)
        for (x = 0; x < p->maxx; x++) pset(p, row, x, ' ');
}

static void hist(WINDOW *w, int row)
{
    WINDOW *p = pn[P_MSG];
    char r[256], *m;
    int x, n = 0;
    for (x = 0; x < w->maxx && x < 255; x++)
        if ((r[x] = w->c[row * w->maxx + x] & A_CHARTEXT) != ' ') n = x + 1;
    r[n] = 0;
    if ((m = strstr(r, "  --More--"))) *m = 0;
    if (!*r) return;
    /* a repeat of the newest line: "line (xN)" in its row */
    static char prev[256];
    static int reps;
    if (nhist && !strcmp(r, prev)) {
        char sfx[16];
        snprintf(sfx, sizeof sfx, " (x%d)", ++reps);
        for (n = strlen(r), x = 0; sfx[x] && n + x < p->maxx; x++) hb[nhist - 1][n + x] = (unsigned char)sfx[x];
        return;
    }
    reps = 1;
    strcpy(prev, r);
    if (nhist == HIST) memmove(hb, hb + 1, sizeof hb[0] * (HIST - 1));
    else nhist++;
    for (x = 0; x < p->maxx; x++)
        hb[nhist - 1][x] = x < w->maxx && x < (int)strlen(r) ? w->c[row * w->maxx + x] : ' ';
}

static void scroll1(WINDOW *w)
{
    int x;
    if (w == wc_logwin) hist(w, 0);
    memmove(w->c, w->c + w->maxx, sizeof(chtype) * w->maxx * (w->maxy - 1));
    for (x = 0; x < w->maxx; x++) w->c[(w->maxy - 1) * w->maxx + x] = ' ';
    touchwin(w);
}

int wmove(WINDOW *w, int y, int x)
{
    if (!w || y < 0 || x < 0 || y >= w->maxy || x >= w->maxx) return ERR;
    w->cury = y; w->curx = x;
    return OK;
}

static void newline(WINDOW *w)
{
    w->curx = 0;
    if (w->cury + 1 < w->maxy) w->cury++;
    else if (w->scroll) scroll1(w);
}

int waddch(WINDOW *w, chtype ch)
{
    int c = ch & A_CHARTEXT, i;
    if (c == '\n') {
        for (i = w->curx; i < w->maxx; i++) pset(w, w->cury, i, ' ');
        newline(w);
        return OK;
    }
    if (c == '\r') { w->curx = 0; return OK; }
    if (c == '\b') { if (w->curx) w->curx--; return OK; }
    if (!(ch & A_COLOR) && (w->attr & A_COLOR)) ch |= w->attr & A_COLOR;
    ch |= w->attr & ~(A_COLOR | A_CHARTEXT);
    pset(w, w->cury, w->curx, ch);
    if (++w->curx >= w->maxx) {
        if (w->cury + 1 < w->maxy || w->scroll) newline(w);
        else { w->curx = w->maxx - 1; return ERR; }
    }
    return OK;
}

int waddnstr(WINDOW *w, const char *s, int n)
{
    while (*s && n--) waddch(w, (unsigned char)*s++);
    return OK;
}

int wattrset(WINDOW *w, int a) { w->attr = a; return OK; }
int scrollok(WINDOW *w, int b) { w->scroll = b; return OK; }
int wtimeout(WINDOW *w, int t) { return OK; }

int werase(WINDOW *w)
{
    int i;
    if (w == wc_logwin)
        for (i = 0; i < w->maxy; i++) hist(w, i);
    for (i = 0; i < w->maxy * w->maxx; i++) w->c[i] = ' ';
    w->cury = w->curx = 0;
    return touchwin(w);
}

int touchwin(WINDOW *w)
{
    int y;
    for (y = 0; y < w->maxy; y++) { w->first[y] = 0; w->last[y] = w->maxx - 1; }
    return OK;
}

/* panels: a stack; the top visible one that isn't a game window is the pop-up */
PANEL *new_panel(WINDOW *w)
{
    PANEL *p = calloc(1, sizeof *p);
    p->w = w; p->below = ptop;
    if (ptop) ptop->above = p;
    ptop = p;
    return p;
}

int del_panel(PANEL *p)
{
    if (p->above) p->above->below = p->below; else ptop = p->below;
    if (p->below) p->below->above = p->above;
    free(p);
    return OK;
}

int hide_panel(PANEL *p) { p->hidden = 1; return OK; }
void update_panels(void) { }

static WINDOW *top_popup(void)
{
    PANEL *p;
    for (p = ptop; p; p = p->below)
        if (!p->hidden && p->w != wc_mapwin && p->w != wc_sidewin) return p->w;
    return NULL;
}

/* a cell that shows nothing (unprintable chars are sent as blanks) */
static int blank(chtype ch) { int c = ch & A_CHARTEXT; return (c <= ' ' || c > 126) && !(ch & A_REVERSE); }

/* curses colours -> CSS (the page gets finished colours, W0): normal,
 * bold (bright), dim; pair 0 = default gray */
static const char *const PAL[3][8] = {
    { "#000000", "#c82828", "#28b428", "#c8a028", "#3c5ae6", "#be3cbe", "#28b4be", "#c8c8c8" },
    { "#6e6e6e", "#ff5a5a", "#64ff64", "#ffff5a", "#7896ff", "#ff6eff", "#6effff", "#ffffff" },
    { "#000000", "#641414", "#145a14", "#645014", "#1e2d73", "#5f1e5f", "#145a5f", "#6e6e6e" } };

const char *wc_color(chtype ch)
{
    int pair = PAIR_NUMBER(ch);
    if (!pair && (ch & A_DIM)) return PAL[1][0];
    return PAL[ch & A_BOLD ? 1 : ch & A_DIM ? 2 : 0][pair ? pair % 8 : 7];
}

WINDOW *wc_pane(int p) { return p >= 0 && p < NPANES ? pn[p] : NULL; }

/* Text panes go out as whole rows (RVIP W0 rules 5, 6): each changed row
 * once, trimmed, colour runs "\x05#rrggbb" .. "\x06" (reverse video in a
 * colour: "\x05#000000/#rrggbb"), plain reverse between \x01 and \x02;
 * and the rows in use (to the last non-blank one or the cursor). */
static int rows_sent[NPANES], cur_p = -1, cur_y;

static void run_key(chtype ch, char *k)
{
    const char *c = wc_color(ch);
    int def = !strcmp(c, PAL[0][7]);
    if (ch & A_REVERSE) {
        if (def) strcpy(k, "\x01");
        else sprintf(k, "\x05#000000/%s", c);
    } else if (def) *k = 0;
    else sprintf(k, "\x05%s", c);
}


static void send_rows(int i, int used)
{
    if (used != rows_sent[i]) be_rows(i, rows_sent[i] = used);
}

static void pflush(int i)
{
    WINDOW *p = pn[i];
    int y, x, used = 0;
    if (!p) return;
    for (y = 0; y < p->maxy; y++)
        for (x = 0; x < p->maxx; x++)
            if (!blank(p->c[y * p->maxx + x])) used = y + 1;
    if (cur_p == i && cur_y >= used) used = cur_y + 1;
    send_rows(i, used);
    for (y = 0; y < p->maxy; y++) {
        char buf[256 * 24], k[24], open[24] = "";
        int n = 0, end = p->maxx;
        if (p->first[y] < 0) continue;
        p->first[y] = p->last[y] = -1;
        while (end > 0 && blank(p->c[y * p->maxx + end - 1])) end--;
        for (x = 0; x < end && x < 256; x++) {
            chtype ch = p->c[y * p->maxx + x];
            int c = ch & A_CHARTEXT;
            run_key(ch, k);
            if (strcmp(k, open)) {
                if (*open) buf[n++] = *open == 1 ? 2 : 6;
                n += sprintf(buf + n, "%s", k);
                strcpy(open, k);
            }
            buf[n++] = c < 32 || c > 126 ? ' ' : c;
        }
        if (*open) buf[n++] = *open == 1 ? 2 : 6;
        buf[n] = 0;
        be_line(i, y, buf, "", -1);
    }
}

static void map_refresh(WINDOW *w)
{
    int y, x;
    for (y = 0; y < w->maxy; y++)
        for (x = 0; x < w->maxx && w->first[y] >= 0; x++)
            if (x >= w->first[y] && x <= w->last[y]) be_put(P_MAP, y, x, w->c[y * w->maxx + x], -1, -1);
    untouch(w);
    wc_inv(pn[P_INV]);
}

static void pop_refresh(WINDOW *w)
{
    int y, x, y0 = 1 << 30, y1 = -1, x0 = 1 << 30, x1 = -1;
    if (w != pop_win) pop_h = 0;
    pop_win = w;
    for (y = 0; y < w->maxy; y++)
        for (x = 0; x < w->maxx; x++) {
            if (blank(w->c[y * w->maxx + x])) continue;
            if (y < y0) y0 = y;
            if (y > y1) y1 = y;
            if (x < x0) x0 = x;
            if (x > x1) x1 = x;
        }
    untouch(w);
    if (y1 < 0) { y0 = x0 = 0; y1 = x1 = 0; }
    if (y1 - y0 + 1 != pop_h || x1 - x0 + 1 != pop_w) {
        pop_h = y1 - y0 + 1; pop_w = x1 - x0 + 1;
        be_popup(pop_h, pop_w);
        delwin(pn[P_POP]);
        pn[P_POP] = newwin(pop_h, pop_w, 0, 0);
        rows_sent[P_POP] = 0;
    }
    for (y = y0; y <= y1; y++)
        for (x = x0; x <= x1; x++) pset(pn[P_POP], y - y0, x - x0, w->c[y * w->maxx + x]);
    pop_win->begy = y0; pop_win->begx = x0;   /* cursor offset (begy/begx unused otherwise) */
}

static void dump(const char *name, WINDOW *p)
{
    FILE *f = fopen(getenv("ZAPM_DUMP"), "a");
    int y, x;
    if (!f) return;
    fprintf(f, "== %s\n", name);
    for (y = 0; p && y < p->maxy; y++) {
        for (x = 0; x < p->maxx; x++) fputc(p->c[y * p->maxx + x] & A_CHARTEXT, f);
        fputc('\n', f);
    }
    fclose(f);
}

int wnoutrefresh(WINDOW *w)
{
    int y, x;
    if (w == wc_mapwin) map_refresh(w);
    else if (w == wc_sidewin) {
        for (y = 0; y < w->maxy && y < pn[P_STATUS]->maxy; y++)
            for (x = 0; x < w->maxx && x < pn[P_STATUS]->maxx; x++)
                pset(pn[P_STATUS], y, x, w->c[y * w->maxx + x]);
        untouch(w);
    } else if (w == wc_logwin) {
        msg_compose();
        {   /* the cursor row of the log: the prompt line over the map */
            char r[256];
            for (x = 0; x < w->maxx && x < 255; x++) r[x] = w->c[w->cury * w->maxx + x] & A_CHARTEXT;
            r[x] = 0;
            be_prompt(r);
        }
        untouch(w);
    }
    return OK;
}

/* cursor goes to the window that waits for a key */
static void set_cursor(int p, int y, int x)
{
    int o = cur_p;
    cur_p = p; cur_y = y;
    be_cursor(p, y, x);
    if (p > P_MAP && y >= rows_sent[p]) send_rows(p, y + 1);   /* a text pane shows the cursor row */
    if (o > P_MAP && o != p && pn[o] && (o != P_POP || pop_h)) pflush(o);   /* its rows in use without it */
}

static void cursor(WINDOW *w)
{
    if (w == wc_mapwin) set_cursor(P_MAP, w->cury, w->curx);
    else if (w == wc_logwin) set_cursor(P_MSG, nhist + w->cury, w->curx);
    else if (w && w == pop_win && pop_h) set_cursor(P_POP, w->cury - w->begy, w->curx - w->begx);
    else set_cursor(-1, 0, 0);
}

int doupdate(void)
{
    WINDOW *top = top_popup();
    int i;
    if (top) pop_refresh(top);
    else if (pop_h || pop_win) { be_popup(0, 0); pop_h = 0; pop_win = NULL; }
    for (i = P_STATUS; i < NPANES; i++) if (i != P_POP || pop_h) pflush(i);
    be_flush();
    if (getenv("ZAPM_DUMP")) {        /* testing: panes as text */
        fclose(fopen(getenv("ZAPM_DUMP"), "w"));
        dump("MAP", wc_mapwin);
        dump("STATUS", pn[P_STATUS]);
        dump("MSG", pn[P_MSG]);
        dump("INV", pn[P_INV]);
        if (pop_h) dump("POP", pn[P_POP]);
    }
    return OK;
}

int wrefresh(WINDOW *w)
{
    wnoutrefresh(w);
    return doupdate();
}

#define QMAX 64
static int q[QMAX], qn;

int wc_pending(void) { return qn; }
void wc_push(int k) { if (qn < QMAX) q[qn++] = k; }
void wc_flushkeys(void) { qn = 0; while (be_getkey(0) >= 0) ; }

int wgetch(WINDOW *w)
{
    int k;
    wnoutrefresh(w);
    doupdate();
    cursor(w);
    be_flush();
    if (qn) { k = q[0]; memmove(q, q + 1, sizeof(int) * --qn); return k; }
    return be_getkey(1);
}

int wc_kbhit(void)
{
    int k;
    if (qn) return 1;
    if ((k = be_getkey(0)) < 0) return 0;
    wc_push(k);
    return 1;
}
