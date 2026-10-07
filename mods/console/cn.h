#ifndef DP_H
#define DP_H

#include "../display.h"
#include <termios.h>
#include <signal.h>



/* Where the console's own copy of the terminal's descriptor starts: clear of
   0-9, of a {var} redirection's own 10 upward, and of a process
   substitution's 60. */
#ifndef CN_FDBASE
#define CN_FDBASE 120
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
};

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
	       unsigned id, int under, str *o);
void kt_del(str *o, unsigned id);
void kt_delall(str *o);
/* image.c: a picture the console keeps as a region of the grid. */
int cn_image(sh *s, const char *pane, int row, int col, int h, int w,
	     const unsigned char *rgb, int iw, int ih, unsigned flags);
void cn_imgscale(const unsigned char *in, int iw, int ih,
		 unsigned char *out, int w, int h);
void cn_imgcheck(void);
size_t cn_imgsend(str *b, int over);
/* The deletes owed to the terminal for pictures that have gone, emitted
   before the diff so the text underneath is painted in the same frame. */
size_t cn_imgdels(str *b);
void cn_imgclear(void);
int cn_imgat(int row, int col);
size_t cn_imgn(void);
/* Whether pictures can be drawn as pixels, and how: CN_GFX_NONE, CN_GFX_SIXEL
   or CN_GFX_KITTY. Decided once, from HIBR_GFX and then the terminal's name.
   The name of the one in force, for `console gfx`. */
int cn_gfx(sh *s);
const char *cn_gfxname(int k);
extern volatile sig_atomic_t cn_wgen;
unsigned cn_dim1(unsigned v, int pct, unsigned deflt);
void cn_darken(int row, int col, int h, int w, int pct);
void cn_setdim(unsigned fg, unsigned bg);
void cn_cursor(int row, int col, int vis);
long cn_flush(void);
int cn_key(int ms, str *out);
extern size_t cn_eaten;
void cn_watchadd(int fd);
void cn_watchdel(int fd);
int cn_colour(const char *t, unsigned *out);
unsigned cn_attr(const char *t);

void cn_inval(void);
void cn_gfree(cn_grid *g);
int cn_gsize(cn_grid *g, int rows, int cols);
void cn_cellset(cn_cell *c, unsigned cp, const char *ext, size_t en);

int cn_shot(const char *path, const char *fmt, int r0, int c0, int h, int w);

extern vec cn_panes;
int cn_behind(int on);
void cn_owninval(void);
cn_pane *cn_pfind(const char *nm);
cn_pane *cn_pset(const char *nm, int row, int col, int h, int w);
int cn_prect(const char *nm, int *row, int *col, int *h, int *w);
void cn_pclear(void);
int cn_pput(sh *s, const cn_pane *p, int row, int col, const char *t);
void cn_praise(cn_pane *p);
void cn_plower(cn_pane *p);
int cn_pdrop(const char *nm);
cn_pane *cn_phit(int row, int col);

#endif
