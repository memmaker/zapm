/* Minimal curses + panel for ZAPM (RVIP port): in-memory windows handed to
 * a frontend (be_x11.c) by pane. Only what ZAPM uses. Cut down from
 * ~/Games/xrogue/port/curses.h. */
#ifndef WCURSES_H
#define WCURSES_H
#include <stdarg.h>
#include <stdio.h>
#include <ctype.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned int chtype;
typedef unsigned int attr_t;
#ifndef TRUE
#define TRUE 1
#define FALSE 0
#endif
#define ERR (-1)
#define OK 0

/* cell: char 0-7, colour pair 8-13, attributes 16+ */
#define A_CHARTEXT 0xff
#define A_COLOR    0x3f00
#define COLOR_PAIR(n) (((n) & 0x3f) << 8)
#define PAIR_NUMBER(a) (((a) & A_COLOR) >> 8)
#define A_NORMAL   0
#define A_BOLD     0x10000
#define A_DIM      0x20000
#define A_REVERSE  0x40000
#define A_STANDOUT A_REVERSE
#define COLOR_BLACK 0
#define KEY_MAX 0777

typedef struct _win {
    int maxy, maxx, begy, begx, cury, curx, attr, scroll;
    short *first, *last;    /* changed range per line, -1 = none */
    chtype *c;
} WINDOW;
typedef struct _panel { WINDOW *w; int hidden; struct _panel *below, *above; } PANEL;

extern WINDOW *stdscr;
extern int LINES, COLS, COLORS, COLOR_PAIRS;

WINDOW *initscr(void);
int endwin(void);
WINDOW *newwin(int, int, int, int);
int delwin(WINDOW *);
int wmove(WINDOW *, int, int);
int waddch(WINDOW *, chtype);
int waddnstr(WINDOW *, const char *, int);
int wattrset(WINDOW *, int);
int werase(WINDOW *);
int touchwin(WINDOW *);
int wnoutrefresh(WINDOW *);
int doupdate(void);
int wrefresh(WINDOW *);
int wgetch(WINDOW *);
int wtimeout(WINDOW *, int);
int scrollok(WINDOW *, int);
PANEL *new_panel(WINDOW *);
int del_panel(PANEL *);
int hide_panel(PANEL *);
void update_panels(void);

#define waddstr(w, s) waddnstr(w, s, -1)
#define mvwaddch(w, y, x, ch) (wmove(w, y, x) == ERR ? ERR : waddch(w, ch))
#define mvwaddstr(w, y, x, s) (wmove(w, y, x) == ERR ? ERR : waddstr(w, s))
#define mvwaddnstr(w, y, x, s, n) (wmove(w, y, x) == ERR ? ERR : waddnstr(w, s, n))
#define getyx(w, y, x) ((y) = (w)->cury, (x) = (w)->curx)
#define getmaxyx(w, y, x) ((y) = (w)->maxy, (x) = (w)->maxx)
#define has_colors() TRUE
#define start_color() OK
#define init_pair(p, f, b) OK
#define can_change_color() FALSE
#define curs_set(n) OK
#define raw() OK
#define noecho() OK
#define nl() OK
#define nonl() OK
#define notimeout(w, b) OK
#define intrflush(w, b) OK
#define keypad(w, b) OK

/* frontend: panes (map, status, messages, inventory) and a pop-up box
 * sized to its content. Text only. */
enum { P_MAP, P_STATUS, P_MSG, P_INV, P_POP, NPANES };
void be_init(int pane, int cols, int rows);
void be_put(int pane, int y, int x, chtype ch, int tile, int under);
void be_cursor(int pane, int y, int x);
void be_prompt(const char *s);           /* live message row (rvip-wm.js prompt line) */   /* pane -1: no cursor */
void be_popup(int rows, int cols);        /* 0: close */
void be_flush(void);
int  be_getkey(int wait);   /* -1 when !wait and nothing queued */
void be_end(void);
void be_sound(const char *);
/* wcurses.c */
extern WINDOW *wc_mapwin, *wc_sidewin, *wc_logwin;
int  wc_pending(void);      /* keys queued */
int  wc_kbhit(void);        /* a key is waiting (explore stops on any key) */
void wc_push(int key);      /* queue a key for wgetch (item actions) */
void wc_flushkeys(void);
void wc_windows(WINDOW *map, WINDOW *side, WINDOW *log);
void wc_inv(WINDOW *);      /* Rvip.cpp: inventory pane */
#ifdef __cplusplus
}
#endif
#endif
