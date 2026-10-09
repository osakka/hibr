#ifndef HIBR_MM_H
#define HIBR_MM_H

#include "hibr.h"

#define MM_NONE 0
#define MM_FLOW 1
#define MM_SEQ 2
#define MM_PIE 3

#define MM_TD 0
#define MM_LR 1
#define MM_BT 2
#define MM_RL 3

#define MM_RECT 0
#define MM_ROUND 1
#define MM_DIAMOND 2
#define MM_CIRCLE 3

#define MM_SOLID 0
#define MM_DOTTED 1
#define MM_THICK 2

#define MM_PLAIN 0
#define MM_OPEN 1
#define MM_CROSS 2
#define MM_RING 3

#define MM_NOTE_OVER 1
#define MM_NOTE_LEFT 2
#define MM_NOTE_RIGHT 3

#define MM_GHLINE 0
#define MM_GVLINE 1
#define MM_GTL 2
#define MM_GTR 3
#define MM_GBL 4
#define MM_GBR 5
#define MM_GTEEU 6
#define MM_GTEED 7
#define MM_GTEEL 8
#define MM_GTEER 9
#define MM_GPLUS 10
#define MM_GDOWN 11
#define MM_GUP 12
#define MM_GLEFT 13
#define MM_GRIGHT 14
#define MM_GDHLINE 15
#define MM_GDVLINE 16
#define MM_GTHLINE 17
#define MM_GTVLINE 18
#define MM_GDIAL 19
#define MM_GDIAR 20
#define MM_GDIBL 21
#define MM_GDIBR 22
#define MM_GBLOCK 23
#define MM_GRING 24
#define MM_GXMARK 25
#define MM_GN 26

typedef struct mm_node {
	str id;
	str label;
	int shape;
	int rank, order, dummy;
	int pos, lvl;
	int x, y, w, h;
} mm_node;

typedef struct mm_edge {
	int from, to, style, arrow, rev;
	int track, c0, c1, first, last;
	str label;
} mm_edge;

typedef struct mm_msg {
	int from, to, style, arrow, note;
	str text;
} mm_msg;

typedef struct mm_slice {
	str label;
	double v;
} mm_slice;

typedef struct mm_dia {
	int kind, dir, data;
	str why;
	str note;
	str title;
	vec nodes;
	vec edges;
	vec msgs;
	vec slices;
	int w, h;
} mm_dia;

typedef struct mm_grid {
	int w, h;
	unsigned *cp;
	char *sty;
} mm_grid;

mm_dia *mm_parse(const char *t);
void mm_free(mm_dia *d);
int mm_nodeof(mm_dia *d, const char *id, int make);
void mm_err(mm_dia *d, int ln, const char *msg, const char *what);
void mm_note(mm_dia *d, const char *msg, const char *what);

void mm_lay(mm_dia *d);
int mm_ranks(mm_dia *d);
int mm_across(mm_dia *d, mm_node *n);
int mm_along(mm_dia *d, mm_node *n);
int mm_vert(mm_dia *d);

mm_grid *mm_render(mm_dia *d, int ascii, int wide);
void mm_gfree(mm_grid *g);
void mm_u8(str *o, unsigned cp);
size_t mm_width(const char *p);
unsigned mm_glyph(int g, int ascii);

#endif
