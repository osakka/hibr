#ifndef DP_H
#define DP_H

#include "../display.h"
#include "../gfxask.h"
#include <termios.h>
#include <signal.h>



/* Synchronized output's pair, which brackets every frame cn_flush sends. */
#ifndef CN_SYNCH
#define CN_SYNCH "\033[?2026h"
#endif
#ifndef CN_SYNCL
#define CN_SYNCL "\033[?2026l"
#endif

/* Where the console's own copy of the terminal's descriptor starts: clear of
   0-9, of a {var} redirection's own 10 upward, and of a process
   substitution's 60. */
#ifndef CN_FDBASE
#define CN_FDBASE 120
#endif

/* The kitty z-index a picture drawn under the text is placed at. The
   protocol has two layers below the glyphs: a negative z is under the text
   but *over* each cell's own background colour, and only a z below
   INT32_MIN/2 (-1073741824) is "drawn under cells with non-default
   background colors". A wallpaper wants the second -- the desktop paints a
   window's face in a real colour and blanks only the cells the picture is
   meant to show through -- so z=-1 composited every window's fill away and
   left the glyphs floating on the picture (Gitea #175). One below the
   threshold rather than INT32_MIN, so anything that ever wants to go under
   this still can. */
#ifndef CN_ZUNDER
#define CN_ZUNDER (-1073741825)
#endif

#ifndef CN_GFX_NONE
#define CN_GFX_NONE 0
#define CN_GFX_SIXEL 1
#define CN_GFX_KITTY 2
#endif

typedef struct cn_cell cn_cell;
typedef struct cn_grid cn_grid;
typedef struct cn_pane cn_pane;

struct cn_cell {
	unsigned cp;
	char *ext;
	unsigned fg, bg, attr;
	unsigned link;
	unsigned char w, cont;
};

struct cn_grid {
	int rows, cols;
	cn_cell *c;
};

struct cn_pane {
	char *nm;
	int row, col, h, w;
	int top;
	/* Whether its last move was sent as a rectangle copy (Gitea #337),
	   which `console copied` answers, so the desktop can leave a window
	   the copy already drew undrawn. */
	int copied;
	/* Whether anything was written through it since the last flush, so
	   a picture of its it did not place again is known to have gone
	   (Gitea #346). */
	int drawn;
};

/* A pane marked `top` is above everything, whatever the stack says: a cell
   inside it is written only by drawing through that pane, so no window, no
   shadow, no menu and no picture can cover it (the desktop's menu bar).
   cn_ttop and cn_tbot are the rows any top pane spans, a band a write
   outside it is refused by in two comparisons; cn_wpane is the pane a write
   is being made through, set by cn_pput and cn_pfill for its length. */
extern int cn_ttop, cn_tbot, cn_tok;
extern const cn_pane *cn_wpane;
void cn_tband(void);
int cn_ptopat(int row, int col);
cn_pane *cn_ptopin(int row, int col, int h, int w);

int cn_open(sh *s);
void cn_close(sh *s);
int cn_isopen(void);
void cn_size(int *rows, int *cols);
int cn_resized(void);
int cn_pending(void);
void cn_reassert(void);
void cn_mouseon(int mode);
void cn_signals(int on);
void cn_clip(const char *t);
void cn_b64(str *o, const unsigned char *p, size_t n);
void cn_pen(unsigned fg, unsigned bg, unsigned attr);
void cn_getpen(unsigned *fg, unsigned *bg, unsigned *attr);
void cn_clear(void);
int cn_put(int row, int col, const char *t);
void cn_fill(int row, int col, int h, int w, const char *t);
void cn_bell(void);
void cn_notify(const char *t);
void cn_link(const char *uri);
int cn_fitq(void);
/* The pixel size of a cell, from the terminal's own ws_xpixel/ws_ypixel: 0
   when it says nothing, which is what a plain pty does and what a backend
   that cannot place pixels must take as "no pictures here". */
void cn_cellpx(int *w, int *h);
int cn_prect(const char *nm, int *row, int *col, int *h, int *w);
extern cn_grid cn_back, cn_front;
/* sixel.c: a picture as the bytes a terminal paints, in the fixed 6x6x6
   palette or one chosen from the picture itself. */
void six_encode(const unsigned char *rgb, int w, int h, int chosen, str *o);
/* kitty.c: a picture as the kitty graphics protocol's own escape, which is an
   object with an id rather than paint, so every one placed is deleted later. */
unsigned kt_id(void);
void kt_encode(const unsigned char *rgb, int iw, int ih, int cols, int rows,
	       unsigned id, int under, int comp, str *o);
void kt_del(str *o, unsigned id);
void kt_delall(str *o);
/* How many times anything has written to the grid or placed a picture. A
   flush compares it with its own last reading to tell a new frame from the
   same one flushed twice (Gitea #118). */
extern size_t cn_wrote;
/* image.c: a picture the console keeps as a region of the grid. */
int cn_image(sh *s, const char *pane, int row, int col, int h, int w,
	     const unsigned char *rgb, int iw, int ih, unsigned flags);
void cn_imgscalesrc(const unsigned char *in, int iw, int ih,
		    int rx, int ry, int rw, int rh,
		    unsigned char *out, int w, int h);
void cn_imgscale(const unsigned char *in, int iw, int ih,
		 unsigned char *out, int w, int h);
void cn_imgcheck(int again);
int cn_imgkeep(sh *s, int row, int col, int h, int w);
size_t cn_imgsend(str *b, int over);
/* The deletes owed to the terminal for pictures that have gone, emitted
   before the diff so the text underneath is painted in the same frame. */
size_t cn_imgdels(str *b);
size_t cn_imgdlate(str *b);
void cn_imgowelate(unsigned id);
void cn_imgclear(void);
int cn_imgat(int row, int col);
int cn_imgcellat(int row, int col, unsigned *out);
/* Whether a picture drawn under the text covers this cell at all, asked
   without wanting its colour there -- what decides whether a cell's
   background has to be kept distinguishable from the terminal's own. */
int cn_imgunderat(int row, int col);
unsigned cn_bgtrans(int row, int col);
int cn_translucent(int pct, unsigned fb);
size_t cn_imgn(void);
/* Whether pictures can be drawn as pixels, and how: CN_GFX_NONE, CN_GFX_SIXEL
   or CN_GFX_KITTY. From HIBR_GFX, then from what the terminal itself
   answered (cn_gotcap), and only then from its name -- a name says what a
   family usually does, and inside `hold` it is the session's own name,
   frozen when it was started, so a client reattaching from another machine
   could never change it (Gitea #216).
   The name of the one in force, for `console gfx`. */
int cn_gfx(sh *s);
int cn_gfxsure(sh *s);
const char *cn_gfxname(int k);
extern volatile sig_atomic_t cn_wgen;
unsigned cn_dim1(unsigned v, int pct, unsigned deflt);
/* What the terminal says its own colours are, and what a colour resolves
   to on it -- see the comment on cn_tbgv in term.c for why a display that
   places a picture under the text has to know. The longest reply worth
   waiting for the end of. */
#ifndef CN_OSCMAX
#define CN_OSCMAX 64
#endif
void cn_askcolours(void);
void cn_askmaybe(void);
/* What the terminal answered about drawing pictures: kind is CN_GFX_KITTY
   or CN_GFX_SIXEL, yes 1 or 0. */
void cn_gotcap(int kind, int yes);
void cn_gotcolour(int which, int idx, unsigned rgb);
unsigned cn_termbg(void);
int cn_termbgok(void);
unsigned cn_rgbof(unsigned v);
/* The background a cell is actually written with: its own, unless that is
   exactly the terminal's and a picture drawn under the text would be let
   through it. */
unsigned cn_opaque(int row, int col, unsigned bg);
void cn_darken(int row, int col, int h, int w, int pct);
void cn_darken1(int row, int col, int h, int w, int pct, int once, int half);
unsigned cn_halfof(const cn_cell *k, int top, int *isbg);
void cn_darkhalf(cn_cell *k, int r, int c, int pct, int half);
void cn_split(int row, int col);
int cn_inpane(int row, int col);
void cn_setdim(unsigned fg, unsigned bg);
void cn_cursor(int row, int col, int vis);
long cn_flush(void);
int cn_key(int ms, str *out);
extern size_t cn_eaten;
void cn_watchadd(int fd);
void cn_watchdel(int fd);
size_t cn_wlist(int **fds);
void cn_gotkbd(void);
/* SGR-Pixels, 1016 (Gitea #370): whether a program asked for the pointer
   in pixels, whether the terminal said it can report them (-1 not said, 0
   no, 1 yes), and whether it has been asked to. A report decoded while
   pixels are in force carries `x,y` after its cell, and `-` when they were
   asked for and are not. */
extern int cn_pixwant, cn_pixok, cn_pixon;
void cn_gotmpix(int yes);
void cn_pixapply(void);
extern int cn_kbdon;
int cn_wmuted(int fd);
int cn_colour(const char *t, unsigned *out);
unsigned cn_attr(const char *t);

void cn_inval(void);
void cn_gfree(cn_grid *g);
int cn_gsize(cn_grid *g, int rows, int cols);
void cn_cellset(cn_cell *c, unsigned cp, const char *ext, size_t en);

int cn_shot(const char *path, const char *fmt, int r0, int c0, int h, int w);

/* Marks a cell a shadow has already darkened, so casting the same shadow
   again does nothing. Darkening is multiplicative: without this, a shadow
   cast on two frames running comes out twice as dark, and every scheme for
   casting it exactly once per repaint has to be kept in step with every
   scheme for deciding what to repaint -- which went wrong three times
   running. A private bit above every DP_ attribute, never emitted: the SGR
   builder tests each flag it knows by name. Any ordinary write to the cell
   clears it, because cn_put and cn_fill assign the pen's own attributes. */
#ifndef CN_SHADOWED
#define CN_SHADOWED (1u << 16)
#endif
/* A cell a fill laid down, glyph and all: a pattern, such as a wallpaper's,
   rather than text, which a half-block shadow may replace (Gitea #340).
   Private like CN_SHADOWED, and never emitted. */
#ifndef CN_FILLED
#define CN_FILLED (1u << 17)
#endif

/* What a still picture's payload is compressed as before it goes out
   (Gitea #129). Zlib by default: it is the protocol's own o=z, which a
   terminal that draws kitty pictures at all understands, where f=100 is a
   PNG that one taking f=24 may simply draw nothing for -- and q=2 means it
   cannot say so. `console imgcomp` chooses. */
#ifndef CN_COMP_NONE
#define CN_COMP_NONE 0
#define CN_COMP_ZLIB 1
#define CN_COMP_PNG 2
#endif

extern vec cn_panes;
extern int cn_quiet;
extern int cn_mousemode;
#ifndef CN_DETAIL_AUTO
#define CN_DETAIL_AUTO 0
#define CN_DETAIL_FULL 1
#define CN_DETAIL_HALF 2
#endif
extern int cn_imghalf;
extern int cn_imgcomp;

typedef struct cn_ol cn_ol;
struct cn_ol {
	int on, r, c, h, w;
	unsigned fg, tl, hz, tr, vt, bl, br;
	unsigned *tt;
	int tn;
};
extern cn_ol cn_olay;

/* Rectangle copies (DECCRA, Gitea #337): cn_copym is `console copy`, 0
   auto, 1 on, 2 off. In auto a copy is sent when the thing that will apply
   it has said it can: hibr's own emulator by answering XTVERSION with
   `hibr-term ... deccra` (cn_vouch, cn_hterm), a hold server by
   HIBR_HOLDCOPY in the program's environment, and any other terminal by
   feature 28 in its device attributes (cn_da28) -- see cn_copyok. */
extern int cn_copym, cn_da28, cn_vouch, cn_hterm;
int cn_copyok(void);
int cn_copyrect(int r, int c, int h, int w, int dr, int dc);
void cn_copyclear(void);
unsigned cn_olat(int r, int c);
unsigned cn_olglyph(const char *t);
void cn_oltitle(const char *t);
int cn_behind(int on);
void cn_owninval(void);
cn_pane *cn_pfind(const char *nm);
cn_pane *cn_pset(const char *nm, int row, int col, int h, int w);
int cn_prect(const char *nm, int *row, int *col, int *h, int *w);
void cn_pclear(void);
int cn_pput(sh *s, const cn_pane *p, int row, int col, const char *t);
int cn_pfill(const cn_pane *p, int row, int col, int h, int w, const char *t);
void cn_praise(cn_pane *p);
void cn_plower(cn_pane *p);
int cn_pdrop(const char *nm);
cn_pane *cn_phit(int row, int col);

#endif
