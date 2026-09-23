#define _GNU_SOURCE

#include "tm.h"
#include <stdlib.h>
#include <string.h>

vec tm_list;
int tm_next = 1;

/* Find a terminal by the id the script was given, or null. */
tm_t *tm_find(int id)
{
	size_t i;
	tm_t *t;

	for (i = 0; i < tm_list.n; i++) {
		t = (tm_t *)tm_list.p[i];
		if (t->id == id)
			return t;
	}
	return 0;
}

/* A cell as it is when nothing has been written to it: a space in whatever
   the pen's background currently is, which is what makes an erase under a
   coloured pen come out coloured. */
void tm_blank(tm_t *t, tm_cell *c)
{
	c->cp = ' ';
	c->fg = t->fg;
	c->bg = t->bg;
	c->attr = 0;
	c->w = 1;
}

/* The cell at a position, or null if it is off the screen. */
tm_cell *tm_at(tm_t *t, int r, int c)
{
	if (r < 0 || r >= t->rows || c < 0 || c >= t->cols)
		return 0;
	return t->g + (size_t)r * t->cols + c;
}

/* A terminal with nothing on it yet. */
tm_t *tm_new(int rows, int cols)
{
	tm_t *t = xm(sizeof *t);
	size_t i;

	memset(t, 0, sizeof *t);
	t->id = tm_next++;
	t->rows = rows;
	t->cols = cols;
	t->fg = DP_DEFAULT;
	t->bg = DP_DEFAULT;
	t->dfg = DP_DEFAULT;
	t->dbg = DP_DEFAULT;
	t->vis = 1;
	t->autowrap = 1;
	t->top = 0;
	t->bot = rows - 1;
	t->sbmax = 1000;
	tm_blank(t, &t->nil);
	t->g = xm((size_t)rows * cols * sizeof *t->g);
	s_init(&t->pb);
	s_init(&t->ub);
	s_init(&t->title);
	for (i = 0; i < (size_t)rows * cols; i++)
		tm_blank(t, t->g + i);
	v_add(&tm_list, t);
	lg(HIBR_LDBG, "terminal %d is %dx%d", t->id, rows, cols);
	return t;
}

void tm_free(tm_t *t)
{
	size_t i;

	for (i = 0; i < tm_list.n; i++)
		if (tm_list.p[i] == (void *)t) {
			memmove(tm_list.p + i, tm_list.p + i + 1,
				(tm_list.n - i - 1) * sizeof *tm_list.p);
			tm_list.n--;
			break;
		}
	free(t->g);
	free(t->alt);
	tm_sbclear(t);
	s_free(&t->pb);
	s_free(&t->ub);
	s_free(&t->title);
	free(t);
}

/* A grid of a new size holding an old one's cells, old row `from` landing
   on new row `to`, and blank wherever the old one had nothing. */
tm_cell *tm_regrid(tm_t *t, tm_cell *old, int orows, int ocols, int rows,
		   int cols, int from, int to)
{
	tm_cell *n = xm((size_t)rows * cols * sizeof *n);
	int r, c, src;
	size_t i;

	for (i = 0; i < (size_t)rows * cols; i++)
		tm_blank(t, n + i);
	for (r = 0; r < rows; r++) {
		src = r - to + from;
		if (src < 0 || src >= orows)
			continue;
		for (c = 0; c < cols && c < ocols; c++)
			n[(size_t)r * cols + c] = old[(size_t)src * ocols + c];
	}
	return n;
}

/* Change the size.

   Nothing reflows. A real terminal rewraps long lines when it widens, and
   getting that right needs the original line breaks, which a cell grid has
   already thrown away -- so this keeps what fits and says so rather than
   guessing.  What it does do is what xterm does with height: a screen that
   shrinks under the cursor pushes its top lines into the scrollback rather
   than losing the line being typed on, and one that grows pulls them back.
   Both screens are resized, so a program on the alternate screen stays
   there and simply redraws. */
int tm_size(tm_t *t, int rows, int cols)
{
	tm_cell *mg, *n;
	tm_line *l;
	int r, c, shift = 0, pull = 0;

	if (rows < 1 || cols < 1)
		return 0;
	if (rows == t->rows && cols == t->cols)
		return 1;
	mg = t->inalt ? t->alt : t->g;
	if (!t->inalt) {
		if (rows < t->rows && t->cr >= rows)
			shift = t->cr - rows + 1;
		else if (rows > t->rows)
			pull = rows - t->rows < t->sbn ? rows - t->rows : t->sbn;
	}
	for (r = 0; r < shift; r++)
		tm_push(t, mg + (size_t)r * t->cols, t->cols);
	n = tm_regrid(t, mg, t->rows, t->cols, rows, cols, shift, pull);
	for (r = pull - 1; r >= 0; r--) {
		l = tm_pop(t);
		for (c = 0; l && c < l->w && c < cols; c++)
			n[(size_t)r * cols + c] = l->c[c];
		free(l);
	}
	free(mg);
	if (t->inalt) {
		t->alt = n;
		n = tm_regrid(t, t->g, t->rows, t->cols, rows, cols, 0, 0);
		free(t->g);
		t->g = n;
	} else {
		t->g = n;
		if (t->alt) {
			free(t->alt);
			t->alt = 0;
		}
	}
	t->cr += pull - shift;
	t->rows = rows;
	t->cols = cols;
	t->top = 0;
	t->bot = rows - 1;
	t->view = 0;
	t->sel = 0;
	if (t->cr >= rows)
		t->cr = rows - 1;
	if (t->cr < 0)
		t->cr = 0;
	if (t->cc >= cols)
		t->cc = cols - 1;
	t->wrapnext = 0;
	lg(HIBR_LDBG, "terminal %d resized to %dx%d, %d up, %d back", t->id,
	   rows, cols, shift, pull);
	return 1;
}

/* Blank everything from one position to another, inclusive. */
void tm_erase(tm_t *t, int r0, int c0, int r1, int c1)
{
	int r, c, a, b;

	for (r = r0; r <= r1; r++) {
		if (r < 0 || r >= t->rows)
			continue;
		a = r == r0 ? c0 : 0;
		b = r == r1 ? c1 : t->cols - 1;
		for (c = a; c <= b; c++)
			if (c >= 0 && c < t->cols)
				tm_blank(t, tm_at(t, r, c));
	}
}

/* Move the scroll region up by n rows, or down if n is negative. */
void tm_scroll(tm_t *t, int n)
{
	int r, h = t->bot - t->top + 1;
	size_t w = (size_t)t->cols * sizeof *t->g;

	if (!n || h < 1)
		return;
	if (n > h || -n > h)
		n = n > 0 ? h : -h;
	if (n > 0) {
		if (t->top == 0 && !t->inalt)
			for (r = 0; r < n; r++)
				tm_push(t, t->g + (size_t)r * t->cols, t->cols);
		for (r = t->top; r <= t->bot - n; r++)
			memcpy(t->g + (size_t)r * t->cols,
			       t->g + (size_t)(r + n) * t->cols, w);
		tm_erase(t, t->bot - n + 1, 0, t->bot, t->cols - 1);
	} else {
		for (r = t->bot; r >= t->top - n; r--)
			memcpy(t->g + (size_t)r * t->cols,
			       t->g + (size_t)(r + n) * t->cols, w);
		tm_erase(t, t->top, 0, t->top - n - 1, t->cols - 1);
	}
}

/* Open n blank lines at the cursor, pushing the rest of the region down. */
void tm_ilines(tm_t *t, int n)
{
	int save = t->top;

	if (t->cr < t->top || t->cr > t->bot)
		return;
	t->top = t->cr;
	tm_scroll(t, -n);
	t->top = save;
}

/* Take n lines out at the cursor, pulling the rest of the region up.  A
   deleted line is gone, not history, so it stays out of the scrollback even
   when it was the top line. */
void tm_dlines(tm_t *t, int n)
{
	int save = t->top, keep = t->sbmax;

	if (t->cr < t->top || t->cr > t->bot)
		return;
	t->top = t->cr;
	t->sbmax = 0;
	tm_scroll(t, n);
	t->sbmax = keep;
	t->top = save;
}

/* Open n blank cells at the cursor, pushing the rest of the line right. */
void tm_ichars(tm_t *t, int n)
{
	int c;

	if (n < 1)
		return;
	for (c = t->cols - 1; c >= t->cc + n; c--)
		*tm_at(t, t->cr, c) = *tm_at(t, t->cr, c - n);
	for (c = t->cc; c < t->cc + n && c < t->cols; c++)
		tm_blank(t, tm_at(t, t->cr, c));
}

/* Take n cells out at the cursor, pulling the rest of the line left. */
void tm_dchars(tm_t *t, int n)
{
	int c;

	if (n < 1)
		return;
	for (c = t->cc; c < t->cols - n; c++)
		*tm_at(t, t->cr, c) = *tm_at(t, t->cr, c + n);
	for (c = t->cols - n > t->cc ? t->cols - n : t->cc; c < t->cols; c++)
		tm_blank(t, tm_at(t, t->cr, c));
}

/* Write one character at the cursor and move on.

   The wrap is deferred: a glyph that lands in the last column leaves the
   cursor there with wrapnext set, and only the *next* glyph moves to the
   line below. A terminal that wraps eagerly puts the cursor on the next line
   as soon as the last column is filled, and then a carriage return arriving
   before any more text lands one line too far down -- which is how a prompt
   exactly as wide as the screen ends up scrolling for ever. */
void tm_glyph(tm_t *t, unsigned cp)
{
	int w = u8w(cp);
	tm_cell *c;

	if (w < 1)
		w = 1;
	if (t->wrapnext) {
		t->cc = 0;
		if (t->cr == t->bot)
			tm_scroll(t, 1);
		else if (t->cr < t->rows - 1)
			t->cr++;
		t->wrapnext = 0;
	}
	if (t->cc + w > t->cols) {
		if (!t->autowrap) {
			t->cc = t->cols - w;
			if (t->cc < 0)
				t->cc = 0;
		} else {
			t->cc = 0;
			if (t->cr == t->bot)
				tm_scroll(t, 1);
			else if (t->cr < t->rows - 1)
				t->cr++;
		}
	}
	c = tm_at(t, t->cr, t->cc);
	if (!c)
		return;
	c->cp = cp;
	c->fg = t->fg;
	c->bg = t->bg;
	c->attr = t->attr;
	c->w = (unsigned char)w;
	if (w == 2 && t->cc + 1 < t->cols) {
		c = tm_at(t, t->cr, t->cc + 1);
		c->cp = 0;
		c->fg = t->fg;
		c->bg = t->bg;
		c->attr = t->attr;
		c->w = 0;
	}
	if (t->cc + w >= t->cols && t->autowrap) {
		t->cc = t->cols - 1;
		t->wrapnext = 1;
	} else {
		t->cc += w;
	}
}
