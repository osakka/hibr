#define _GNU_SOURCE

#include "tm.h"
#include <stdlib.h>
#include <string.h>

/* Is a cell one that a stored line can leave off its end? */
int tm_plain(const tm_cell *k)
{
	return k->cp == ' ' && k->bg == DP_DEFAULT && !k->attr && k->w == 1;
}

/* Keep a line that has scrolled off the top.

   The store is a ring that grows by doubling until it reaches sbmax lines
   and then overwrites its oldest; it only ever grows while it has not yet
   wrapped, so the head is still at zero whenever it is reallocated.  A line
   is kept without its trailing blanks, so a screen of short lines costs
   what its text costs rather than its width. */
void tm_push(tm_t *t, const tm_cell *row, int w)
{
	tm_line *l;
	int n = w, cap;

	t->tot++;
	if (t->sbmax <= 0)
		return;
	while (n > 0 && tm_plain(row + n - 1))
		n--;
	l = xm(sizeof *l + (size_t)n * sizeof *row);
	l->w = n;
	if (n)
		memcpy(l->c, row, (size_t)n * sizeof *row);
	if (t->sbn == t->sbcap && t->sbcap < t->sbmax) {
		cap = t->sbcap ? t->sbcap * 2 : 64;
		if (cap > t->sbmax)
			cap = t->sbmax;
		t->sb = xr(t->sb, (size_t)cap * sizeof *t->sb);
		t->sbcap = cap;
	}
	if (t->sbn < t->sbcap) {
		t->sb[(t->sbh + t->sbn) % t->sbcap] = l;
		t->sbn++;
		if (t->view)
			t->view++;
	} else {
		free(t->sb[t->sbh]);
		t->sb[t->sbh] = l;
		t->sbh = (t->sbh + 1) % t->sbcap;
	}
	if (t->view > t->sbn)
		t->view = t->sbn;
}

/* Line i of the scrollback, oldest first. */
tm_line *tm_sbline(tm_t *t, int i)
{
	if (i < 0 || i >= t->sbn)
		return 0;
	return t->sb[(t->sbh + i) % t->sbcap];
}

/* Take the newest line back off, for a screen that has grown taller. */
tm_line *tm_pop(tm_t *t)
{
	tm_line *l;

	if (!t->sbn)
		return 0;
	l = t->sb[(t->sbh + t->sbn - 1) % t->sbcap];
	t->sbn--;
	t->tot--;
	if (t->view > t->sbn)
		t->view = t->sbn;
	return l;
}

/* Forget the scrollback, which is what ESC [ 3 J asks for. */
void tm_sbclear(tm_t *t)
{
	int i;

	for (i = 0; i < t->sbn; i++)
		free(tm_sbline(t, i));
	free(t->sb);
	t->sb = 0;
	t->sbcap = t->sbn = t->sbh = t->view = 0;
}

/* The cell shown at a row of the screen, with the view scrolled back into
   the store.  Past the end of a stored line it is a blank in the default
   pen, which is what the trailing blanks it was stored without were. */
tm_cell *tm_vat(tm_t *t, int r, int c)
{
	tm_line *l;
	int i;

	if (!t->view || t->inalt)
		return tm_at(t, r, c);
	if (r < 0 || r >= t->rows || c < 0 || c >= t->cols)
		return 0;
	i = t->sbn - t->view + r;
	if (i >= t->sbn)
		return tm_at(t, i - t->sbn, c);
	l = tm_sbline(t, i);
	return l && c < l->w ? l->c + c : &t->nil;
}

/* Move the view n lines back into the store, or forward if n is negative,
   and say where it ended up. */
int tm_view(tm_t *t, int n)
{
	if (t->inalt)
		return t->view = 0;
	t->view += n;
	if (t->view > t->sbn)
		t->view = t->sbn;
	if (t->view < 0)
		t->view = 0;
	return t->view;
}

/* The number a shown row has among every line the terminal has held.

   Lines are numbered from the first ever pushed into the scrollback, so a
   selection made on them stays on the same text while more output scrolls
   underneath it; a row's position on the screen would not. */
long tm_absrow(tm_t *t, int r)
{
	return t->tot - (t->inalt ? 0 : t->view) + r;
}

/* Start a selection at a shown cell, or carry it on to one. */
void tm_selset(tm_t *t, int r, int c, int start)
{
	long a = tm_absrow(t, r);

	if (c < 0)
		c = 0;
	if (c >= t->cols)
		c = t->cols - 1;
	if (start) {
		t->sa = a;
		t->sca = c;
	}
	t->sz = a;
	t->scz = c;
	t->sel = !start;
}

/* Is a cell, by line number and column, inside the selection? */
int tm_insel(tm_t *t, long a, int c)
{
	long a0 = t->sa, a1 = t->sz;
	int c0 = t->sca, c1 = t->scz;

	if (!t->sel)
		return 0;
	if (a0 > a1 || (a0 == a1 && c0 > c1)) {
		a0 = t->sz;
		a1 = t->sa;
		c0 = t->scz;
		c1 = t->sca;
	}
	if (a < a0 || a > a1)
		return 0;
	if (a == a0 && c < c0)
		return 0;
	if (a == a1 && c > c1)
		return 0;
	return 1;
}

/* The line with a number, stored or on screen, as cells and a width. */
const tm_cell *tm_absline(tm_t *t, long a, int *w)
{
	long first = t->tot - t->sbn;
	tm_line *l;

	*w = 0;
	if (a < first)
		return 0;
	if (a < t->tot) {
		l = tm_sbline(t, (int)(a - first));
		if (!l)
			return 0;
		*w = l->w;
		return l->c;
	}
	if (a - t->tot >= t->rows)
		return 0;
	*w = t->cols;
	return t->g + (size_t)(a - t->tot) * t->cols;
}

/* The selected text: each line without its trailing blanks, joined by
   newlines, as a terminal's copy gives it. */
void tm_seltext(tm_t *t, str *out)
{
	long a, a0 = t->sa, a1 = t->sz;
	int c, c0 = t->sca, c1 = t->scz, w, from, to;
	const tm_cell *row;
	size_t keep;

	if (!t->sel)
		return;
	if (a0 > a1 || (a0 == a1 && c0 > c1)) {
		a0 = t->sz;
		a1 = t->sa;
		c0 = t->scz;
		c1 = t->sca;
	}
	for (a = a0; a <= a1; a++) {
		row = tm_absline(t, a, &w);
		from = a == a0 ? c0 : 0;
		to = a == a1 ? c1 : t->cols - 1;
		keep = out->n;
		for (c = from; row && c <= to && c < w; c++)
			if (row[c].w)
				tm_utf8(out, row[c].cp ? row[c].cp : ' ');
		while (out->n > keep && out->p[out->n - 1] == ' ')
			out->p[--out->n] = 0;
		if (a < a1)
			s_ch(out, '\n');
	}
}
