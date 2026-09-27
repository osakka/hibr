#define _GNU_SOURCE

#include "tm.h"

/* A terminal with nothing on it yet, addressed by id from here on -- the
   same handle style as pty's own spawn, and never this module's own tm_t
   pointer, which stays private to it. */
int tm_api_new(int rows, int cols)
{
	return tm_new(rows, cols)->id;
}

void tm_api_free(int id)
{
	tm_t *t = tm_find(id);

	if (t)
		tm_free(t);
}

int tm_api_resize(int id, int rows, int cols)
{
	tm_t *t = tm_find(id);

	return t ? tm_size(t, rows, cols) : 0;
}

int tm_api_rows(int id)
{
	tm_t *t = tm_find(id);

	return t ? t->rows : 0;
}

int tm_api_cols(int id)
{
	tm_t *t = tm_find(id);

	return t ? t->cols : 0;
}

void tm_api_feed(int id, const char *b, size_t n)
{
	tm_t *t = tm_find(id);

	if (t)
		tm_feed(t, b, n);
}

int tm_api_at(int id, int r, int c, unsigned *cp, unsigned *fg, unsigned *bg,
	      unsigned *attr, unsigned *w)
{
	tm_t *t = tm_find(id);
	tm_cell *k = t ? tm_at(t, r, c) : 0;

	if (!k)
		return 0;
	*cp = k->cp;
	*fg = k->fg;
	*bg = k->bg;
	*attr = k->attr;
	*w = k->w;
	return 1;
}

int tm_api_mouse(int id, int *sgr)
{
	tm_t *t = tm_find(id);

	if (!t)
		return 0;
	if (sgr)
		*sgr = t->msgr;
	return t->mmode;
}

int tm_api_cursor(int id, int *r, int *c, int *vis)
{
	tm_t *t = tm_find(id);

	if (!t)
		return 0;
	*r = t->cr;
	*c = t->cc;
	*vis = t->vis && !t->view;
	return 1;
}

static const tm_api term_api = {
	tm_api_new, tm_api_free, tm_api_resize, tm_api_rows, tm_api_cols,
	tm_api_feed, tm_api_at, tm_api_mouse, tm_api_cursor
};

const tm_api *tm_apiget(void)
{
	return &term_api;
}
