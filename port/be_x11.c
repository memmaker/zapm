/* X11 frontend for ZAPM's curses shim (from ~/Games/xrogue/port/be_x11.c):
 * one window per pane plus an undecorated pop-up over the map. Text only
 * (the user rejected NetHack tiles), curses colours; the map uses a bigger font.
 * Env: ZAPM_MAPPX (map font px, default 26), ZAPM_XFT (font family, default Menlo), ZAPM_TEXT (text px, 14),
 * ZAPM_MAP / _STATUS / _MSG / _INV = "x,y" window positions. */
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <X11/Xft/Xft.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "curses.h"

static Display *dpy;
static int scr;
static Visual *vis;
static Colormap cmap;
static GC gc;
static XftFont *mapfont, *txtfont;
static XftColor xfg, xbg, xpal[3][8];
static unsigned long ppal[3][8];
static unsigned long fg, bg;
static int mw, mh, tw, th, curP = -1, curY, curX;

static struct pane {
    Window win;
    Pixmap pix;
    XftDraw *xd;
    int cols, rows, cw, ch, pad;
} P[NPANES];

static const char *pname[NPANES] = { "MAP", "STATUS", "MSG", "INV", "POP" };
static const char *ptitle[NPANES] = { "ZAPM", "ZAPM Status", "ZAPM Messages", "ZAPM Inventory", "" };

static unsigned long rgb(int r, int g, int b)
{
    XColor c;
    c.red = r * 257; c.green = g * 257; c.blue = b * 257;
    XAllocColor(dpy, cmap, &c);
    return c.pixel;
}

static XftFont *font(double px, double stretch, int weight)
{
    const char *fam = getenv("ZAPM_XFT");
    FcMatrix m;
    FcMatrixInit(&m);
    m.xx = stretch;
    return XftFontOpen(dpy, scr, XFT_FAMILY, XftTypeString, fam ? fam : "Menlo",
                       XFT_WEIGHT, XftTypeInteger, weight,
                       XFT_PIXEL_SIZE, XftTypeDouble, px, XFT_MATRIX, XftTypeMatrix, &m, NULL);
}

static void open_display(void)
{
    XGlyphInfo gi;
    XRenderColor c1 = { 0xdcdc, 0xdcdc, 0xdcdc, 0xffff }, c0 = { 0, 0, 0, 0xffff };
    const char *e;
    double px;

    if (!(dpy = XOpenDisplay(NULL))) { fprintf(stderr, "zapm: no X display\n"); exit(1); }
    scr = DefaultScreen(dpy);
    vis = DefaultVisual(dpy, scr);
    cmap = DefaultColormap(dpy, scr);
    fg = rgb(215, 215, 215); bg = rgb(0, 0, 0);
    gc = XCreateGC(dpy, DefaultRootWindow(dpy), 0, NULL);
    mapfont = font((e = getenv("ZAPM_MAPPX")) ? atof(e) : 26, 1, XFT_WEIGHT_BOLD);
    XftTextExtents8(dpy, mapfont, (FcChar8 *)"M", 1, &gi);
    mw = gi.xOff; mh = mapfont->ascent + mapfont->descent;
    px = (e = getenv("ZAPM_TEXT")) ? atof(e) : 14;
    txtfont = font(px, 1, XFT_WEIGHT_MEDIUM);
    XftTextExtents8(dpy, txtfont, (FcChar8 *)"M", 1, &gi);
    tw = gi.xOff; th = txtfont->ascent + txtfont->descent;
    XftColorAllocValue(dpy, vis, cmap, &c1, &xfg);
    XftColorAllocValue(dpy, vis, cmap, &c0, &xbg);
    {   /* curses colours: normal, bold (bright), dim */
        static const int base[8][3] = { {0,0,0}, {200,40,40}, {40,180,40}, {200,160,40},
            {60,90,230}, {190,60,190}, {40,180,190}, {200,200,200} };
        static const int hi[8][3] = { {110,110,110}, {255,90,90}, {100,255,100}, {255,255,90},
            {120,150,255}, {255,110,255}, {110,255,255}, {255,255,255} };
        int i, k;
        for (i = 0; i < 8; i++)
            for (k = 0; k < 3; k++) {
                const int *c = k == 1 ? hi[i] : base[i];
                int d = k == 2 ? 2 : 1;
                XRenderColor rc;
                if (k == 2 && i == 7) d = 1, c = hi[0];     /* dark gray */
                rc.red = c[0] / d * 257; rc.green = c[1] / d * 257; rc.blue = c[2] / d * 257; rc.alpha = 0xffff;
                XftColorAllocValue(dpy, vis, cmap, &rc, &xpal[k][i]);
                ppal[k][i] = rgb(c[0] / d, c[1] / d, c[2] / d);
            }
    }
}

/* Default layout for a 1440x932 screen: map top left, Status right of it,
 * Messages and Inventory below. XQuartz adds ~28 px title bars. */
static void place(int p, int *x, int *y)
{
    char var[32];
    const char *e;
    int right = P[P_MAP].cols * mw + 6;
    *x = p == P_STATUS || p == P_INV ? right : 0;
    *y = p == P_MSG ? P[P_MAP].rows * mh + 36 : p == P_INV ? P[P_STATUS].rows * th + 36 : 0;
    snprintf(var, sizeof var, "ZAPM_%s", pname[p]);
    if ((e = getenv(var))) sscanf(e, "%d,%d", x, y);
}

static void make_pixmap(struct pane *q)
{
    int w = q->cols * q->cw + 2 * q->pad, h = q->rows * q->ch + 2 * q->pad;
    if (q->xd) XftDrawDestroy(q->xd);
    if (q->pix) XFreePixmap(dpy, q->pix);
    q->pix = XCreatePixmap(dpy, q->win, w, h, DefaultDepth(dpy, scr));
    q->xd = XftDrawCreate(dpy, q->pix, vis, cmap);
    XSetForeground(dpy, gc, bg);
    XFillRectangle(dpy, q->pix, gc, 0, 0, w, h);
}

void be_init(int p, int cols, int rows)
{
    struct pane *q = &P[p];
    XSizeHints h;
    int x, y;

    if (!dpy) open_display();
    q->cols = cols; q->rows = rows;
    q->cw = p == P_MAP ? mw : tw;
    q->ch = p == P_MAP ? mh : th;
    place(p, &x, &y);
    q->win = XCreateSimpleWindow(dpy, DefaultRootWindow(dpy), x, y, cols * q->cw, rows * q->ch, 0, fg, bg);
    h.flags = PPosition | USPosition | PMinSize | PMaxSize;
    h.x = x; h.y = y;
    h.min_width = h.max_width = cols * q->cw;
    h.min_height = h.max_height = rows * q->ch;
    XSetWMNormalHints(dpy, q->win, &h);
    XStoreName(dpy, q->win, ptitle[p]);
    XSelectInput(dpy, q->win, KeyPressMask | ExposureMask);
    make_pixmap(q);
    XMapWindow(dpy, q->win);
    XFlush(dpy);
}

/* The pop-up: no title bar, over the top left of the map, sized to its
 * content plus one character of padding. */
void be_popup(int rows, int cols)
{
    struct pane *q = &P[P_POP];
    int x, y, w, h;
    Window child;

    if (!rows) { if (q->win) XUnmapWindow(dpy, q->win); return; }
    q->cw = tw; q->ch = th; q->pad = tw;
    q->cols = cols; q->rows = rows;
    w = cols * tw + 2 * q->pad; h = rows * th + 2 * q->pad;
    XTranslateCoordinates(dpy, P[P_MAP].win, DefaultRootWindow(dpy), mw, 0, &x, &y, &child);
    if (!q->win) {
        XSetWindowAttributes a;
        a.override_redirect = True;
        a.background_pixel = bg;
        a.border_pixel = fg;
        q->win = XCreateWindow(dpy, DefaultRootWindow(dpy), x, y, w, h, 1, CopyFromParent,
                               InputOutput, CopyFromParent, CWOverrideRedirect | CWBackPixel | CWBorderPixel, &a);
        XSelectInput(dpy, q->win, KeyPressMask | ExposureMask);
    } else XMoveResizeWindow(dpy, q->win, x, y, w, h);
    make_pixmap(q);
    XMapRaised(dpy, q->win);
}

static void draw_text(struct pane *q, int y, int x, chtype ch)
{
    FcChar8 c = ch & A_CHARTEXT;
    XftFont *f = q == &P[P_MAP] ? mapfont : txtfont;
    int inv = !!(ch & A_REVERSE), px = q->pad + x * q->cw, py = q->pad + y * q->ch;
    int k = ch & A_BOLD ? 1 : ch & A_DIM ? 2 : 0, col = PAIR_NUMBER(ch) % 8;
    XGlyphInfo gi;
    if (!PAIR_NUMBER(ch)) col = 7;
    XSetForeground(dpy, gc, inv ? ppal[k][col] : bg);
    XFillRectangle(dpy, q->pix, gc, px, py, q->cw, q->ch);
    if (c == ' ') return;
    XftTextExtents8(dpy, f, &c, 1, &gi);
    XftDrawString8(q->xd, inv ? &xbg : &xpal[k][col], f, px + (q->cw - gi.xOff) / 2,
                   py + (q->ch - f->ascent - f->descent) / 2 + f->ascent, &c, 1);
}

void be_put(int p, int y, int x, chtype ch, int tile, int under)
{
    struct pane *q = &P[p];
    if (!q->pix || y < 0 || x < 0 || y >= q->rows || x >= q->cols) return;
    draw_text(q, y, x, ch);
}

void be_cursor(int p, int y, int x) { curP = p; curY = y; curX = x; }

void be_flush(void)
{
    int p;
    for (p = 0; p < NPANES; p++) {
        struct pane *q = &P[p];
        if (q->win && q->pix)
            XCopyArea(dpy, q->pix, q->win, gc, 0, 0, q->cols * q->cw + 2 * q->pad,
                      q->rows * q->ch + 2 * q->pad, 0, 0);
    }
    /* cursor: a bar under the cell */
    if (curP >= 0 && P[curP].win && curY < P[curP].rows && curX < P[curP].cols) {
        struct pane *q = &P[curP];
        int px = q->pad + curX * q->cw, py = q->pad + curY * q->ch;
        XSetForeground(dpy, gc, fg);
        XFillRectangle(dpy, q->win, gc, px, py + q->ch - 2, q->cw, 2);
    }
    XFlush(dpy);
}

static int keycode(XKeyEvent *ev)
{
    char buf[8];
    KeySym ks;
    int n = XLookupString(ev, buf, sizeof buf, &ks, NULL);
    switch (ks) {       /* ZAPM moves with the digits */
    case XK_Left: case XK_KP_Left: case XK_KP_4: return '4';
    case XK_Right: case XK_KP_Right: case XK_KP_6: return '6';
    case XK_Up: case XK_KP_Up: case XK_KP_8: return '8';
    case XK_Down: case XK_KP_Down: case XK_KP_2: return '2';
    case XK_Home: case XK_KP_Home: case XK_KP_7: return '7';
    case XK_Prior: case XK_KP_Prior: case XK_KP_9: return '9';
    case XK_End: case XK_KP_End: case XK_KP_1: return '1';
    case XK_Next: case XK_KP_Next: case XK_KP_3: return '3';
    case XK_KP_Begin: case XK_KP_5: return '5';
    case XK_KP_Enter: case XK_Return: return '\r';
    case XK_BackSpace: case XK_Delete: return 127;
    case XK_KP_Add: return '+';
    case XK_KP_Subtract: return '-';
    case XK_KP_Multiply: return '*';
    case XK_KP_Divide: return '/';
    case XK_KP_Decimal: case XK_KP_Delete: return '.';
    case XK_KP_0: case XK_KP_Insert: return '0';
    }
    return n == 1 ? (unsigned char)buf[0] : -1;
}

int be_getkey(int wait)
{
    XEvent ev;
    for (;;) {
        if (!wait && !XPending(dpy)) return -1;
        XNextEvent(dpy, &ev);
        if (ev.type == Expose) be_flush();
        else if (ev.type == KeyPress) {
            int k = keycode(&ev.xkey);
            if (getenv("ZAPM_KEYLOG")) fprintf(stderr, "key %d\n", k);
            if (k >= 0) return k;
        }
    }
}

void be_sound(const char *s) { }

void be_end(void) { if (dpy) XCloseDisplay(dpy); dpy = NULL; }
