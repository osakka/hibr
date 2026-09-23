#ifndef TM_H
#define TM_H

#include "hibr.h"
#include "../display.h"
#include "../pty.h"

typedef struct tm_cell tm_cell;
typedef struct tm_line tm_line;
typedef struct tm_t tm_t;

struct tm_cell {
	unsigned cp;
	unsigned fg, bg, attr;
	unsigned char w;
};

struct tm_line {
	int w;
	tm_cell c[];
};

enum { T_GND, T_ESC, T_CSI, T_OSC, T_ESCQ };

struct tm_t {
	int id, pty, rows, cols;
	tm_cell *g, *alt;
	int cr, cc, sr, sc;
	int top, bot;
	unsigned fg, bg, attr;
	unsigned dfg, dbg, dattr;
	int vis, wrapnext, autowrap, inalt, done;
	int st;
	str pb, ub, title;
	tm_line **sb;
	int sbcap, sbn, sbh, sbmax, view;
	int mmode, msgr, bpaste;
	tm_cell nil;
	long tot, sa, sz;
	int sel, sca, scz;
};

tm_t *tm_find(int id);
tm_t *tm_new(int rows, int cols);
void tm_free(tm_t *t);
int tm_size(tm_t *t, int rows, int cols);
tm_cell *tm_regrid(tm_t *t, tm_cell *old, int orows, int ocols, int rows,
		   int cols, int from, int to);

tm_cell *tm_at(tm_t *t, int r, int c);
void tm_blank(tm_t *t, tm_cell *c);
void tm_erase(tm_t *t, int r0, int c0, int r1, int c1);
void tm_scroll(tm_t *t, int n);
void tm_ilines(tm_t *t, int n);
void tm_dlines(tm_t *t, int n);
void tm_ichars(tm_t *t, int n);
void tm_dchars(tm_t *t, int n);
void tm_glyph(tm_t *t, unsigned cp);
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
void tm_osc(tm_t *t);
void tm_utf8(str *b, unsigned cp);
void tm_draw(tm_t *t, const dp_api *dp, int row, int col, int h, int w);
int tm_keybytes(const char *name, str *out);
int tm_mouse(tm_t *t, const char *act, const char *btn, int r, int c,
	     str *out);
const char *tm_mname(tm_t *t);

#endif
