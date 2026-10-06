#ifndef TM_H
#define TM_H

#include "hibr.h"
#include "../display.h"
#include "../pty.h"
#include "../term.h"

typedef struct tm_cell tm_cell;
typedef struct tm_line tm_line;
typedef struct tm_save tm_save;
typedef struct tm_t tm_t;

/* One cell. x is 0, or 1 + an index into the terminal's own pool of
   combining sequences (tm_t.xp): the marks, joiners and variation
   selectors that followed this cell's character. A cell stays plain data,
   so a scroll or a resize can still copy it with memcpy. */
struct tm_cell {
	unsigned cp;
	unsigned fg, bg, attr;
	unsigned x;
	unsigned link;
	unsigned char w;
};

struct tm_line {
	int w;
	tm_cell c[];
};

/* What DECSC saves and DECRC restores: position, pen, pending wrap, origin
   mode and the character sets -- not just where the cursor was. */
struct tm_save {
	int set, r, c, wrap, om, gl;
	unsigned fg, bg, attr;
	unsigned char g[2];
};

/* Paul Williams' DEC parser states: ground, escape and its intermediates,
   the four CSI states, the five DCS states, OSC, SOS/PM, whose strings are
   consumed rather than shown, and APC, which is kept because the kitty
   graphics protocol is one. */
enum {
	T_GND, T_ESC, T_ESCI, T_CSIE, T_CSIP, T_CSII, T_CSIX,
	T_DCSE, T_DCSP, T_DCSI, T_DCSS, T_DCSX, T_OSC, T_SOS, T_APC
};

/* One picture a program put on this terminal: where it starts, the cells it
   covers, the kitty protocol's own id (0 for a sixel, which has none), and
   the escapes exactly as they came. See img.c. */
typedef struct tm_img tm_img;
struct tm_img {
	int r, c, rows, cols;
	unsigned id;
	str data;
};

#ifndef TM_IMGMAX
#define TM_IMGMAX (4u << 20)
#endif

/* Conceal (SGR 8) has no display attribute of its own: the cell is drawn
   as a blank instead, so it lives above DP_ATTRS and never reaches pen(). */
#ifndef TM_HIDE
#define TM_HIDE 0x10000u
#endif

struct tm_t {
	int id, pty, rows, cols;
	tm_cell *g, *alt;
	int cr, cc;
	int top, bot;
	unsigned fg, bg, attr;
	unsigned dfg, dbg, dattr;
	int vis, wrapnext, autowrap, inalt, done;
	int st;
	long *pv;
	unsigned char *ps;
	int pn, pcap, pcur, pdig;
	int pfx;
	str in, os, title, clip;
	unsigned link;
	char **lk;
	int lkn, lkcap;
	int bells;
	str out, nq;
	int ofin, obel;
	unsigned uacc;
	int uneed, ulen;
	unsigned last;
	unsigned char gset[2];
	int gl;
	tm_save sv[2];
	int ckm, kpam, om, irm, lnm, focus;
	unsigned char *tabs;
	char **xp;
	int xn, xcap;
	int sync;
	long synct;
	tm_cell *frz;
	int fcr, fcc;
	int hasrgb;
	unsigned rgbfg, rgbbg;
	tm_line **sb;
	int sbcap, sbn, sbh, sbmax, view;
	int mmode, msgr, bpaste;
	tm_cell nil;
	long tot, sa, sz;
	int sel, sca, scz;
	int cshape;
	/* The pictures this terminal is holding (img.c), a generation that
	   changes whenever the set does, the cell size whoever owns the real
	   terminal has told us about -- without which a sixel cannot be
	   placed at all -- and the one being gathered chunk by chunk. */
	tm_img *img;
	int imgn, imgcap;
	unsigned imgen;
	int cellw, cellh;
	str imgb;
	int imgr, imgc, imgrows, imgcols, imgon;
	unsigned imgid;
};

tm_t *tm_find(int id);
int tm_savefile(tm_t *t, const char *path);
int tm_loadfile(tm_t *t, const char *path);
tm_t *tm_new(int rows, int cols);
void tm_free(tm_t *t);
int tm_size(tm_t *t, int rows, int cols);
tm_cell *tm_regrid(tm_t *t, tm_cell *old, int orows, int ocols, int rows,
		   int cols, int from, int to);

tm_cell *tm_at(tm_t *t, int r, int c);
void tm_blank(tm_t *t, tm_cell *c);
void tm_unwide(tm_t *t, int r, int c);
void tm_erase(tm_t *t, int r0, int c0, int r1, int c1);
void tm_scroll(tm_t *t, int n);
void tm_ilines(tm_t *t, int n);
void tm_dlines(tm_t *t, int n);
void tm_ichars(tm_t *t, int n);
void tm_dchars(tm_t *t, int n);
void tm_lf(tm_t *t);
void tm_glyph(tm_t *t, unsigned cp);
const char *tm_ext(tm_t *t, const tm_cell *k);
void tm_cellstr(tm_t *t, const tm_cell *k, str *out);
void tm_tabreset(tm_t *t);
int tm_plain(const tm_cell *k);
void tm_push(tm_t *t, const tm_cell *row, int w);
tm_line *tm_sbline(tm_t *t, int i);
tm_line *tm_pop(tm_t *t);
void tm_sbclear(tm_t *t);
tm_cell *tm_vat(tm_t *t, int r, int c);
int tm_view(tm_t *t, int n);
long tm_absrow(tm_t *t, int r);
void tm_selset(tm_t *t, int r, int c, int start);
int tm_insel(tm_t *t, long a, int c);
void tm_seltext(tm_t *t, str *out);
const tm_cell *tm_absline(tm_t *t, long a, int *w);

void tm_feed(tm_t *t, const char *b, size_t n);
void tm_reset(tm_t *t, int hard);
long tm_ms(void);
int tm_syncing(tm_t *t);
void tm_utf8(str *b, unsigned cp);
void tm_draw(tm_t *t, const dp_api *dp, int row, int col, int h, int w,
	     int curon);
int tm_keybytes(tm_t *t, const char *name, str *out);
int tm_mouse(tm_t *t, const char *act, const char *btn, int r, int c,
	     str *out);
const char *tm_mname(tm_t *t);

/* The table offered under "terminal" -- defined in api.c, alongside the
   static instance it hands out, so tm_ini (in term.c) has something to pass
   to hibr_provide without that instance needing to be extern. */
void tm_pass(tm_t *t, const char *raw, size_t n);
void tm_bell(tm_t *t);
void tm_imgdrop(tm_t *t, int i);
void tm_imgclear(tm_t *t);
void tm_imghit(tm_t *t, int r, int c);
void tm_imgkeep(tm_t *t, int r, int c, int rows, int cols, unsigned id,
		const char *p, size_t n);
long tm_imgkey(const char *s, int k);
int tm_imgkeyc(const char *s, int k);
void tm_apcraw(tm_t *t, str *o);
void tm_apcend(tm_t *t);
void tm_imgparams(tm_t *t, str *o);
void tm_sixelend(tm_t *t);
void tm_note(tm_t *t, const char *title, size_t tn, const char *body);
unsigned tm_linkid(tm_t *t, const char *uri);
void tm_unb64(const char *p, str *out);
int tm_api_clip(int id, str *out);
int tm_api_pass(int id, str *out);
int tm_api_title(int id, str *out);
unsigned tm_api_linkat(int id, int r, int c);
const char *tm_api_linkuri(int id, unsigned link);
const tm_api *tm_apiget(void);

#endif
