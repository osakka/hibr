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
	c->x = 0;
	c->link = 0;
	c->w = 1;
}

/* The cell at a position, or null if it is off the screen. */
tm_cell *tm_at(tm_t *t, int r, int c)
{
	if (r < 0 || r >= t->rows || c < 0 || c >= t->cols)
		return 0;
	return t->g + (size_t)r * t->cols + c;
}

/* A blank in place of half a wide glyph a copy split, so neither half is
   left on its own. */
void tm_cpblank(tm_cell *k)
{
	k->cp = ' ';
	k->x = 0;
	k->w = 1;
}

/* DECCRA, CSI Pts;Pls;Pbs;Prs;Pps;Ptd;Pld;Ppd $ v: the rectangle of cells
   from top-left (Pts, Pls) to bottom-right (Pbs, Prs), 1-based and
   inclusive, copied whole to (Ptd, Pld), relative to the margins in
   origin mode, the page numbers ignored. Through a copy of its own, so an
   overlap reads the source as it was. A wide glyph cut by either edge of
   the destination is blanked on both halves, as Blit does. The cursor and
   the pen do not move. Each copy is remembered, for a relay to replay on
   a real terminal that copies too (Gitea #337). */
void tm_deccra(tm_t *t)
{
	int off = t->om ? t->top : 0;
	int r0 = (int)tm_par(t, 0, 1) - 1 + off, c0 = (int)tm_par(t, 1, 1) - 1;
	int r1 = (int)tm_par(t, 2, t->rows) - 1 + off;
	int c1 = (int)tm_par(t, 3, t->cols) - 1;
	int dr = (int)tm_par(t, 5, 1) - 1 + off, dc = (int)tm_par(t, 6, 1) - 1;
	int h, w, i, j;
	tm_cell *tmp, *k;
	tm_cop *op;

	if (r0 < 0)
		r0 = 0;
	if (c0 < 0)
		c0 = 0;
	if (r1 >= t->rows)
		r1 = t->rows - 1;
	if (c1 >= t->cols)
		c1 = t->cols - 1;
	if (dr < 0)
		dr = 0;
	if (dc < 0)
		dc = 0;
	h = r1 - r0 + 1;
	w = c1 - c0 + 1;
	if (h <= 0 || w <= 0 || dr >= t->rows || dc >= t->cols)
		return;
	if (dr + h > t->rows)
		h = t->rows - dr;
	if (dc + w > t->cols)
		w = t->cols - dc;
	tmp = xm((size_t)h * (size_t)w * sizeof *tmp);
	for (i = 0; i < h; i++)
		memcpy(tmp + (size_t)i * w, tm_at(t, r0 + i, c0),
		       (size_t)w * sizeof *tmp);
	for (i = 0; i < h; i++)
		memcpy(tm_at(t, dr + i, dc), tmp + (size_t)i * w,
		       (size_t)w * sizeof *tmp);
	free(tmp);
	for (i = 0; i < h; i++) {
		k = tm_at(t, dr + i, dc);
		if (k->w == 0)
			tm_cpblank(k);
		if (dc > 0 && (k = tm_at(t, dr + i, dc - 1)) && k->w == 2)
			tm_cpblank(k);
		j = dc + w - 1;
		k = tm_at(t, dr + i, j);
		if (k->w == 2)
			tm_cpblank(k);
		if (j + 1 < t->cols && (k = tm_at(t, dr + i, j + 1)) && k->w == 0)
			tm_cpblank(k);
	}
	if (!t->cops)
		t->cops = xm(TM_COPMAX * sizeof *t->cops);
	op = &t->cops[t->copseq % TM_COPMAX];
	op->r = r0;
	op->c = c0;
	op->h = h;
	op->w = w;
	op->dr = dr;
	op->dc = dc;
	t->copseq++;
	lg(HIBR_LDBG, "terminal %d copied %dx%d from %d,%d to %d,%d", t->id,
	   h, w, r0, c0, dr, dc);
}

/* One remembered copy, seq counting from 1: 0 for one made before the
   last resize or too long ago to be kept. */
int tm_copyop(tm_t *t, unsigned seq, tm_cop *out)
{
	if (!t->cops || seq == 0 || seq > t->copseq || seq <= t->copbase ||
	    t->copseq - seq >= TM_COPMAX)
		return 0;
	*out = t->cops[(seq - 1) % TM_COPMAX];
	return 1;
}

/* Tab stops back to every eighth column, for the width the screen has. */
void tm_tabreset(tm_t *t)
{
	int c;

	t->tabs = xr(t->tabs, (size_t)t->cols);
	for (c = 0; c < t->cols; c++)
		t->tabs[c] = c && c % 8 == 0;
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
	t->gset[0] = t->gset[1] = 'B';
	tm_blank(t, &t->nil);
	t->g = xm((size_t)rows * cols * sizeof *t->g);
	s_init(&t->in);
	s_init(&t->os);
	s_init(&t->title);
	s_init(&t->ptrs);
	s_init(&t->clip);
	s_init(&t->out);
	s_init(&t->nq);
	s_init(&t->kst);
	s_init(&t->rq);
	for (i = 0; i < (size_t)rows * cols; i++)
		tm_blank(t, t->g + i);
	tm_tabreset(t);
	v_add(&tm_list, t);
	lg(HIBR_LDBG, "terminal %d is %dx%d", t->id, rows, cols);
	return t;
}

void tm_free(tm_t *t)
{
	size_t i;
	int k;

	for (i = 0; i < tm_list.n; i++)
		if (tm_list.p[i] == (void *)t) {
			memmove(tm_list.p + i, tm_list.p + i + 1,
				(tm_list.n - i - 1) * sizeof *tm_list.p);
			tm_list.n--;
			break;
		}
	free(t->g);
	free(t->cops);
	free(t->alt);
	free(t->frz);
	free(t->pv);
	free(t->ps);
	free(t->tabs);
	for (k = 0; k < t->xn; k++)
		free(t->xp[k]);
	free(t->xp);
	tm_sbclear(t);
	tm_imgclear(t);
	free(t->img);
	s_free(&t->imgb);
	s_free(&t->in);
	s_free(&t->os);
	s_free(&t->title);
	s_free(&t->ptrs);
	s_free(&t->clip);
	s_free(&t->out);
	s_free(&t->nq);
	s_free(&t->kst);
	s_free(&t->rq);
	for (k = 0; k < t->lkn; k++)
		free(t->lk[k]);
	free(t->lk);
	free(t);
}

/* A grid of a new size holding an old one's cells, old row `from` landing
   on new row `to`, and blank wherever the old one had nothing. A wide
   character cut in half by a narrower width loses both halves. */
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
		if (cols < ocols && n[(size_t)r * cols + cols - 1].w == 2)
			tm_blank(t, n + (size_t)r * cols + cols - 1);
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
	unsigned char *ot;
	int r, c, shift = 0, pull = 0, oc = t->cols, i;

	if (rows < 1 || cols < 1)
		return 0;
	if (rows == t->rows && cols == t->cols)
		return 1;
	/* Only a real change: a resize to the size it already is happens on
	   every attach (hold sizes the union each time), and clearing the
	   pictures there would take them away from the client that is about
	   to be sent them. */
	/* A copy remembered from before is in coordinates this grid no
	   longer has. */
	t->copbase = t->copseq;
	if (t->imgn)
		tm_imgclear(t);
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
		if (l && l->w > cols && n[(size_t)r * cols + cols - 1].w == 2)
			tm_blank(t, n + (size_t)r * cols + cols - 1);
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
	free(t->frz);
	t->frz = 0;
	t->sync = 0;
	t->cr += pull - shift;
	t->rows = rows;
	t->cols = cols;
	ot = xm((size_t)oc);
	memcpy(ot, t->tabs, (size_t)oc);
	tm_tabreset(t);
	for (c = 0; c < cols && c < oc; c++)
		t->tabs[c] = ot[c];
	free(ot);
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
	for (i = 0; i < 2; i++) {
		if (t->sv[i].r >= rows)
			t->sv[i].r = rows - 1;
		if (t->sv[i].c >= cols)
			t->sv[i].c = cols - 1;
	}
	t->wrapnext = 0;
	lg(HIBR_LDBG, "terminal %d resized to %dx%d, %d up, %d back", t->id,
	   rows, cols, shift, pull);
	return 1;
}

/* A wide character is two cells that only mean something together, so
   touching either half blanks both: what is left of one never shows as a
   stray half-glyph, or stays on screen because a zero-width right half is
   never drawn over. */
void tm_unwide(tm_t *t, int r, int c)
{
	tm_cell *k = tm_at(t, r, c);

	if (!k)
		return;
	if (k->w == 0 && c > 0) {
		tm_blank(t, k - 1);
		tm_blank(t, k);
	} else if (k->w == 2) {
		tm_blank(t, k);
		if (c + 1 < t->cols)
			tm_blank(t, k + 1);
	}
}

/* Blank everything from one position to another, inclusive. */
void tm_erase(tm_t *t, int r0, int c0, int r1, int c1)
{
	int r, c, a, b;

	/* The whole screen cleared forgets every picture. Part of it is cells
	   erased, which is what a write is too: a picture that owns its cells
	   goes when one inside it is erased, and one drawn under the text does
	   not, since erasing a line over a wallpaper or a window's picture is
	   the display drawing its own cells (Gitea #353) -- every line a lean
	   renderer ends with an erase would otherwise take the pictures with
	   it. */
	if (t->imgn) {
		if (r0 <= 0 && c0 <= 0 && r1 >= t->rows - 1 && c1 >= t->cols - 1)
			tm_imgclear(t);
		else
			tm_imgarea(t, r0, r0 == r1 ? c0 : 0, r1,
				   r0 == r1 ? c1 : t->cols - 1);
	}
	tm_unwide(t, r0, c0);
	tm_unwide(t, r1, c1);
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

	if (t->imgn)
		tm_imgclear(t);
	if (!n || h < 1)
		return;
	if (n > h || -n > h)
		n = n > 0 ? h : -h;
	if (!t->slbad) {
		if (!t->sln) {
			t->sltop = t->top;
			t->slbot = t->bot;
			t->sln = n;
		} else if (t->sltop == t->top && t->slbot == t->bot &&
			   (t->sln > 0) == (n > 0)) {
			t->sln += n;
		} else {
			t->slbad = 1;
		}
	}
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

/* Down a line: scroll the region at its bottom margin, stop at the screen's
   own bottom when the cursor is below the region. */
void tm_lf(tm_t *t)
{
	if (t->cr == t->bot)
		tm_scroll(t, 1);
	else if (t->cr < t->rows - 1)
		t->cr++;
}

/* Open n blank lines at the cursor, pushing the rest of the region down.
   Like every VT, it leaves the cursor at the left margin. */
void tm_ilines(tm_t *t, int n)
{
	int save = t->top;

	if (t->cr < t->top || t->cr > t->bot)
		return;
	t->top = t->cr;
	tm_scroll(t, -n);
	t->top = save;
	t->cc = 0;
	t->wrapnext = 0;
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
	t->cc = 0;
	t->wrapnext = 0;
}

/* Open n blank cells at the cursor, pushing the rest of the line right and
   off the edge. */
void tm_ichars(tm_t *t, int n)
{
	int c;

	if (n < 1)
		return;
	if (n > t->cols - t->cc)
		n = t->cols - t->cc;
	tm_unwide(t, t->cr, t->cc);
	for (c = t->cols - 1; c >= t->cc + n; c--)
		*tm_at(t, t->cr, c) = *tm_at(t, t->cr, c - n);
	for (c = t->cc; c < t->cc + n && c < t->cols; c++)
		tm_blank(t, tm_at(t, t->cr, c));
	if (tm_at(t, t->cr, t->cols - 1)->w == 2)
		tm_blank(t, tm_at(t, t->cr, t->cols - 1));
	t->wrapnext = 0;
}

/* Take n cells out at the cursor, pulling the rest of the line left. */
void tm_dchars(tm_t *t, int n)
{
	int c;

	if (n < 1)
		return;
	if (n > t->cols - t->cc)
		n = t->cols - t->cc;
	tm_unwide(t, t->cr, t->cc);
	if (t->cc + n < t->cols)
		tm_unwide(t, t->cr, t->cc + n);
	for (c = t->cc; c < t->cols - n; c++)
		*tm_at(t, t->cr, c) = *tm_at(t, t->cr, c + n);
	for (c = t->cols - n > t->cc ? t->cols - n : t->cc; c < t->cols; c++)
		tm_blank(t, tm_at(t, t->cr, c));
	t->wrapnext = 0;
}

/* A cell's combining sequence as UTF-8, or empty. */
const char *tm_ext(tm_t *t, const tm_cell *k)
{
	if (!k->x || (int)k->x > t->xn)
		return "";
	return t->xp[k->x - 1];
}

/* A cell as the text it shows: its character, and whatever combined with
   it. The right half of a wide character is nothing at all. */
void tm_cellstr(tm_t *t, const tm_cell *k, str *out)
{
	if (!k->w)
		return;
	tm_utf8(out, k->cp ? k->cp : ' ');
	if (k->x)
		s_cat(out, tm_ext(t, k));
}

/* A combining sequence's id in the pool, adding it the first time it is
   seen. Distinct sequences are few even in text full of them, so a lookup
   by comparison is enough; past a sanity bound the mark is dropped rather
   than the pool growing without end. */
unsigned tm_intern(tm_t *t, const char *s)
{
	int i;

	for (i = 0; i < t->xn; i++)
		if (!strcmp(t->xp[i], s))
			return (unsigned)i + 1;
	if (t->xn >= 65536)
		return 0;
	if (t->xn == t->xcap) {
		t->xcap = t->xcap ? t->xcap * 2 : 16;
		t->xp = xr(t->xp, (size_t)t->xcap * sizeof *t->xp);
	}
	t->xp[t->xn] = strdup(s);
	return (unsigned)++t->xn;
}

/* A zero-width character joins the cell before the cursor -- or the cell
   under it, when a wrap is pending, since that is the one just written.
   With nothing written before it on the line there is nothing to join, and
   it goes, as it does in xterm. */
void tm_combine(tm_t *t, unsigned cp)
{
	tm_cell *k;
	int c = t->wrapnext ? t->cc : t->cc - 1;
	str s;

	k = tm_at(t, t->cr, c);
	if (k && k->w == 0 && c > 0)
		k = tm_at(t, t->cr, c - 1);
	if (!k || !k->w)
		return;
	s_init(&s);
	s_cat(&s, tm_ext(t, k));
	tm_utf8(&s, cp);
	k->x = tm_intern(t, s.p);
	s_free(&s);
}

/* Write one character at the cursor and move on.

   The wrap is deferred: a glyph that lands in the last column leaves the
   cursor there with wrapnext set, and only the *next* glyph moves to the
   line below. A terminal that wraps eagerly puts the cursor on the next line
   as soon as the last column is filled, and then a carriage return arriving
   before any more text lands one line too far down -- which is how a prompt
   exactly as wide as the screen ends up scrolling for ever. Insert mode
   opens room first; a wide character that no longer fits on the line wraps
   whole, leaving the last column blank, as xterm does. */
void tm_glyph(tm_t *t, unsigned cp)
{
	int w = u8w(cp);
	tm_cell *c;

	if (w == 0) {
		tm_combine(t, cp);
		return;
	}
	t->last = cp;
	if (t->wrapnext && t->autowrap) {
		t->cc = 0;
		tm_lf(t);
	}
	t->wrapnext = 0;
	if (t->cc + w > t->cols) {
		if (!t->autowrap) {
			t->cc = t->cols - w;
			if (t->cc < 0)
				t->cc = 0;
		} else {
			tm_unwide(t, t->cr, t->cc);
			t->cc = 0;
			tm_lf(t);
		}
	}
	if (t->irm)
		tm_ichars(t, w);
	tm_unwide(t, t->cr, t->cc);
	if (w == 2)
		tm_unwide(t, t->cr, t->cc + 1);
	c = tm_at(t, t->cr, t->cc);
	if (!c)
		return;
	/* A character drawn inside a picture is the end of that picture: the
	   cell is the text's again (img.c). */
	if (t->imgn)
		tm_imghit(t, t->cr, t->cc);
	c->cp = cp;
	c->fg = t->fg;
	c->bg = t->bg;
	c->attr = t->attr;
	c->x = 0;
	c->link = t->link;
	c->w = (unsigned char)w;
	if (w == 2 && t->cc + 1 < t->cols) {
		c = tm_at(t, t->cr, t->cc + 1);
		c->cp = 0;
		c->fg = t->fg;
		c->bg = t->bg;
		c->attr = t->attr;
		c->x = 0;
		c->link = t->link;
		c->w = 0;
	}
	if (t->cc + w >= t->cols) {
		t->cc = t->cols - 1;
		t->wrapnext = t->autowrap;
	} else {
		t->cc += w;
	}
}
