#ifndef SCR_H
#define SCR_H

#include "hibr.h"
#include <termios.h>

#ifndef SCR_ATTRS
#define SCR_BOLD 1u
#define SCR_DIM 2u
#define SCR_ITAL 4u
#define SCR_UNDER 8u
#define SCR_BLINK 16u
#define SCR_REV 32u
#define SCR_STRIKE 64u
#define SCR_ATTRS 127u
#endif

#ifndef SCR_COLOUR
#define SCR_DEFAULT 0u
#define SCR_PAL 0x1000000u
#define SCR_RGB 0x2000000u
#define SCR_COLOUR 1
#endif

typedef struct scr_cell scr_cell;
typedef struct scr_grid scr_grid;
typedef struct scr_pane scr_pane;

struct scr_cell {
	unsigned cp;
	char *ext;
	unsigned fg, bg, attr;
	unsigned char w, cont;
};

struct scr_grid {
	int rows, cols;
	scr_cell *c;
};

struct scr_pane {
	char *nm;
	int row, col, h, w;
};

int scr_open(sh *s);
void scr_close(sh *s);
int scr_isopen(void);
void scr_size(int *rows, int *cols);
int scr_resized(void);
void scr_pen(unsigned fg, unsigned bg, unsigned attr);
void scr_getpen(unsigned *fg, unsigned *bg, unsigned *attr);
void scr_clear(void);
int scr_put(int row, int col, const char *t);
void scr_fill(int row, int col, int h, int w, const char *t);
void scr_cursor(int row, int col, int vis);
long scr_flush(void);
int scr_key(int ms, str *out);
int scr_colour(const char *t, unsigned *out);
unsigned scr_attr(const char *t);

void scr_inval(void);
void scr_gfree(scr_grid *g);
int scr_gsize(scr_grid *g, int rows, int cols);
void scr_cellset(scr_cell *c, unsigned cp, const char *ext, size_t en);

scr_pane *scr_pfind(const char *nm);
scr_pane *scr_pset(const char *nm, int row, int col, int h, int w);
void scr_pclear(void);
int scr_pput(const scr_pane *p, int row, int col, const char *t);

#endif
