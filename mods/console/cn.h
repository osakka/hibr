#ifndef DP_H
#define DP_H

#include "../display.h"
#include <termios.h>



typedef struct cn_cell cn_cell;
typedef struct cn_grid cn_grid;
typedef struct cn_pane cn_pane;

struct cn_cell {
	unsigned cp;
	char *ext;
	unsigned fg, bg, attr;
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
void cn_mouseon(int mode);
void cn_signals(int on);
void cn_pen(unsigned fg, unsigned bg, unsigned attr);
void cn_getpen(unsigned *fg, unsigned *bg, unsigned *attr);
void cn_clear(void);
int cn_put(int row, int col, const char *t);
void cn_fill(int row, int col, int h, int w, const char *t);
void cn_cursor(int row, int col, int vis);
long cn_flush(void);
int cn_key(int ms, str *out);
int cn_colour(const char *t, unsigned *out);
unsigned cn_attr(const char *t);

void cn_inval(void);
void cn_gfree(cn_grid *g);
int cn_gsize(cn_grid *g, int rows, int cols);
void cn_cellset(cn_cell *c, unsigned cp, const char *ext, size_t en);

cn_pane *cn_pfind(const char *nm);
cn_pane *cn_pset(const char *nm, int row, int col, int h, int w);
void cn_pclear(void);
int cn_pput(const cn_pane *p, int row, int col, const char *t);
void cn_praise(cn_pane *p);
void cn_plower(cn_pane *p);
int cn_pdrop(const char *nm);
cn_pane *cn_phit(int row, int col);

#endif
