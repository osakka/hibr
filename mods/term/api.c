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

int tm_api_modes(int id, int *alt, int *bpaste, int *cshape)
{
	tm_t *t = tm_find(id);

	if (!t)
		return 0;
	*alt = t->inalt;
	*bpaste = t->bpaste;
	*cshape = t->cshape;
	return 1;
}

/* The clipboard the program last set, as OSC 52's own "selection;base64",
   handed over once: 1 and appended to out, or 0 when nothing is waiting. */
int tm_api_clip(int id, str *out)
{
	tm_t *t = tm_find(id);

	if (!t || !t->clip.n)
		return 0;
	s_add(out, t->clip.p, t->clip.n);
	t->clip.n = 0;
	t->clip.p[0] = 0;
	return 1;
}

static const tm_api term_api = {
	tm_api_new, tm_api_free, tm_api_resize, tm_api_rows, tm_api_cols,
	tm_api_feed, tm_api_at, tm_api_mouse, tm_api_cursor, tm_api_modes,
	tm_api_clip
};

const tm_api *tm_apiget(void)
{
	return &term_api;
}
