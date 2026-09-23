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
