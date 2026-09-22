#ifndef TM_H
#define TM_H

#include "hibr.h"
#include "../display.h"
#include "../pty.h"

typedef struct tm_cell tm_cell;
typedef struct tm_t tm_t;

struct tm_cell {
	unsigned cp;
	unsigned fg, bg, attr;
	unsigned char w;
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
};

tm_t *tm_find(int id);
tm_t *tm_new(int rows, int cols);
void tm_free(tm_t *t);
int tm_size(tm_t *t, int rows, int cols);

tm_cell *tm_at(tm_t *t, int r, int c);
void tm_blank(tm_t *t, tm_cell *c);
void tm_erase(tm_t *t, int r0, int c0, int r1, int c1);
void tm_scroll(tm_t *t, int n);
void tm_ilines(tm_t *t, int n);
void tm_dlines(tm_t *t, int n);
void tm_ichars(tm_t *t, int n);
void tm_dchars(tm_t *t, int n);
void tm_glyph(tm_t *t, unsigned cp);

void tm_feed(tm_t *t, const char *b, size_t n);
void tm_osc(tm_t *t);
void tm_utf8(str *b, unsigned cp);
void tm_draw(tm_t *t, const dp_api *dp, int row, int col, int h, int w);
int tm_keybytes(const char *name, str *out);

#endif
