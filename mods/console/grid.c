#define _GNU_SOURCE

#include "cn.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern int cn_fd;
extern int cn_on;
void cn_wr(int fd, const char *p, size_t n);

cn_grid cn_back, cn_front;
unsigned cn_fg, cn_bg, cn_penat;
int cn_crow, cn_ccol, cn_cvis;
unsigned cn_lfg, cn_lbg, cn_lat;
int cn_lset, cn_lvis = -1;

/* Release every cell and the grid itself. */
void cn_gfree(cn_grid *g)
{
	int i, n = g->rows * g->cols;

	for (i = 0; i < n; i++)
		free(g->c[i].ext);
	free(g->c);
	g->c = 0;
	g->rows = 0;
	g->cols = 0;
}

/* Resize a grid, discarding what it held. */
int cn_gsize(cn_grid *g, int rows, int cols)
{
	int i, n = rows * cols;

	if (rows <= 0 || cols <= 0)
		return HIBR_FAIL;
	if (g->rows == rows && g->cols == cols)
		return HIBR_OK;
	cn_gfree(g);
	g->c = xm((size_t)n * sizeof *g->c);
	memset(g->c, 0, (size_t)n * sizeof *g->c);
	for (i = 0; i < n; i++) {
		g->c[i].cp = ' ';
		g->c[i].w = 1;
	}
	g->rows = rows;
	g->cols = cols;
	return HIBR_OK;
}

/* Set a cell's character, keeping any trailing combining marks. */
void cn_cellset(cn_cell *c, unsigned cp, const char *ext, size_t en)
{
	free(c->ext);
	c->ext = 0;
	c->cp = cp;
	if (ext && en) {
		c->ext = xm(en + 1);
		memcpy(c->ext, ext, en);
		c->ext[en] = 0;
	}
}

/* Make the front buffer describe nothing, so the next flush paints it all. */
void cn_inval(void)
{
	int i, n = cn_front.rows * cn_front.cols;

	for (i = 0; i < n; i++)
		cn_front.c[i].cp = 0xFFFFFFFFu;
	cn_lset = 0;
	cn_lvis = -1;
}

/* Make both grids match the terminal, after a resize or at open. */
int cn_fit(void)
{
	int rows, cols;

	cn_size(&rows, &cols);
	if (cn_back.rows == rows && cn_back.cols == cols)
		return HIBR_OK;
	if (cn_gsize(&cn_back, rows, cols) != HIBR_OK)
		return HIBR_FAIL;
	if (cn_gsize(&cn_front, rows, cols) != HIBR_OK)
		return HIBR_FAIL;
	cn_inval();
	lg(HIBR_LDBG, "screen now %d by %d", rows, cols);
	return HIBR_OK;
}

/* Set the pen used by later writes. */
void cn_pen(unsigned fg, unsigned bg, unsigned attr)
{
	cn_fg = fg;
	cn_bg = bg;
	cn_penat = attr;
}

/* Read the pen back. */
void cn_getpen(unsigned *fg, unsigned *bg, unsigned *attr)
{
	*fg = cn_fg;
	*bg = cn_bg;
	*attr = cn_penat;
}

/* Blank the back buffer with the current pen. */
void cn_clear(void)
{
	int i, n;

	if (cn_fit() != HIBR_OK)
		return;
	n = cn_back.rows * cn_back.cols;
	for (i = 0; i < n; i++) {
		cn_cellset(&cn_back.c[i], ' ', 0, 0);
		cn_back.c[i].w = 1;
		cn_back.c[i].cont = 0;
		cn_back.c[i].fg = cn_fg;
		cn_back.c[i].bg = cn_bg;
		cn_back.c[i].attr = cn_penat;
	}
}

/* Darken one colour toward black by pct percent of its own value. RGB scales
   exactly; a palette index or the terminal's own default cannot be scaled
   without a colour table the display does not have, so DP_DIM is left for
   the caller to add as the fallback that reaches every terminal. */
unsigned cn_dim1(unsigned v, int pct)
{
	unsigned r, g, b;

	if (!(v & DP_RGB))
		return v;
	r = (v >> 16) & 0xFF;
	g = (v >> 8) & 0xFF;
	b = v & 0xFF;
	r = r * (unsigned)pct / 100;
	g = g * (unsigned)pct / 100;
	b = b * (unsigned)pct / 100;
	return DP_RGB | (r << 16) | (g << 8) | b;
}

/* Darken a rectangle of the back buffer in place, in front of whatever is
   drawn under it -- a window's shadow, cast on the desktop and on windows
   below it, is this over their already-drawn cells before the window
   drawing on top of it goes in and paints over its own footprint.  Nothing
   is undone: a fixed shape darkened once a frame, not state carried between
   frames, so there is nothing to restore when the window moves. */
void cn_darken(int row, int col, int h, int w, int pct)
{
	int r, c;
	cn_cell *k;

	if (cn_fit() != HIBR_OK)
		return;
	if (pct < 0)
		pct = 0;
	if (pct > 100)
		pct = 100;
	for (r = row; r < row + h; r++) {
		if (r < 0 || r >= cn_back.rows)
			continue;
		for (c = col; c < col + w; c++) {
			if (c < 0 || c >= cn_back.cols)
				continue;
			k = &cn_back.c[r * cn_back.cols + c];
			k->fg = cn_dim1(k->fg, pct);
			k->bg = cn_dim1(k->bg, pct);
			k->attr |= DP_DIM;
		}
	}
}

/* Overwrite a cell that is half of a wide glyph, and its other half. */
void cn_split(int row, int col)
{
	cn_cell *c;

	if (row < 0 || row >= cn_back.rows || col < 0 || col >= cn_back.cols)
		return;
	c = &cn_back.c[row * cn_back.cols + col];
	if (c->cont && col > 0) {
		cn_cellset(c - 1, ' ', 0, 0);
		(c - 1)->w = 1;
	}
	if (c->w == 2 && col + 1 < cn_back.cols) {
		cn_cellset(c + 1, ' ', 0, 0);
		(c + 1)->w = 1;
		(c + 1)->cont = 0;
	}
}

/* Write text into the back buffer, returning the columns it advanced. */
int cn_put(int row, int col, const char *t)
{
	size_t n, i = 0;
	int start = col, w, l;
	unsigned cp;
	cn_cell *c;

	if (cn_fit() != HIBR_OK || !t)
		return 0;
	n = strlen(t);
	while (i < n) {
		l = u8dec(t + i, n - i, &cp);
		w = u8w(cp);
		if (w == 0) {
			if (col > start && row >= 0 && row < cn_back.rows) {
				c = &cn_back.c[row * cn_back.cols + col - 1];
				if (!c->ext)
					cn_cellset(c, c->cp, t + i, (size_t)l);
				else {
					size_t o = strlen(c->ext);
					char *e = xm(o + (size_t)l + 1);
					memcpy(e, c->ext, o);
					memcpy(e + o, t + i, (size_t)l);
					e[o + (size_t)l] = 0;
					free(c->ext);
					c->ext = e;
				}
			}
			i += (size_t)l;
			continue;
		}
		if (row < 0 || row >= cn_back.rows || col < 0 ||
		    col + w > cn_back.cols) {
			col += w;
			i += (size_t)l;
			continue;
		}
		cn_split(row, col);
		if (w == 2)
			cn_split(row, col + 1);
		c = &cn_back.c[row * cn_back.cols + col];
		cn_cellset(c, cp, 0, 0);
		c->w = (unsigned char)w;
		c->cont = 0;
		c->fg = cn_fg;
		c->bg = cn_bg;
		c->attr = cn_penat;
		if (w == 2) {
			cn_cellset(c + 1, 0, 0, 0);
			(c + 1)->w = 0;
			(c + 1)->cont = 1;
			(c + 1)->fg = cn_fg;
			(c + 1)->bg = cn_bg;
			(c + 1)->attr = cn_penat;
		}
		col += w;
		i += (size_t)l;
	}
	return col - start;
}

/* Repeat one character over a rectangle. */
void cn_fill(int row, int col, int h, int w, const char *t)
{
	int r, c, adv;

	if (!t || !*t)
		t = " ";
	for (r = row; r < row + h; r++)
		for (c = col; c < col + w;) {
			adv = cn_put(r, c, t);
			c += adv > 0 ? adv : 1;
		}
}

/* Place the cursor, and say whether it should be seen. */
void cn_cursor(int row, int col, int vis)
{
	cn_crow = row;
	cn_ccol = col;
	cn_cvis = vis;
}

/* Append the escape sequence for one colour. */
void cn_sgrcol(str *b, unsigned v, int bgp)
{
	if (v == DP_DEFAULT) {
		s_cat(b, bgp ? ";49" : ";39");
		return;
	}
	if (v & DP_RGB) {
		s_cat(b, bgp ? ";48;2;" : ";38;2;");
		s_num(b, (long)((v >> 16) & 0xFF));
		s_ch(b, ';');
		s_num(b, (long)((v >> 8) & 0xFF));
		s_ch(b, ';');
		s_num(b, (long)(v & 0xFF));
		return;
	}
	s_cat(b, bgp ? ";48;5;" : ";38;5;");
	s_num(b, (long)(v & 0xFF));
}

/* Append the escape sequence that moves from one pen to another. */
void cn_sgr(str *b, unsigned fg, unsigned bg, unsigned at)
{
	s_cat(b, "\033[0");
	if (at & DP_BOLD)
		s_cat(b, ";1");
	if (at & DP_DIM)
		s_cat(b, ";2");
	if (at & DP_ITAL)
		s_cat(b, ";3");
	if (at & DP_UNDER)
		s_cat(b, ";4");
	if (at & DP_BLINK)
		s_cat(b, ";5");
	if (at & DP_REV)
		s_cat(b, ";7");
	if (at & DP_STRIKE)
		s_cat(b, ";9");
	cn_sgrcol(b, fg, 0);
	cn_sgrcol(b, bg, 1);
	s_ch(b, 'm');
}

/* Append a cursor move, preferring the shorter form when on the same row. */
void cn_goto(str *b, int row, int col, int cr, int cc)
{
	if (row == cr && col == cc)
		return;
	if (row == cr && col == cc + 1) {
		s_cat(b, "\033[C");
		return;
	}
	s_cat(b, "\033[");
	s_num(b, (long)(row + 1));
	s_ch(b, ';');
	s_num(b, (long)(col + 1));
	s_ch(b, 'H');
}

/* True when two cells would draw identically. */
int cn_same(const cn_cell *a, const cn_cell *b)
{
	if (a->cp != b->cp || a->fg != b->fg || a->bg != b->bg ||
	    a->attr != b->attr || a->w != b->w || a->cont != b->cont)
		return 0;
	if (!a->ext && !b->ext)
		return 1;
	if (!a->ext || !b->ext)
		return 0;
	return !strcmp(a->ext, b->ext);
}

/* Encode one codepoint as UTF-8 into the buffer. */
void cn_enc(str *b, unsigned cp)
{
	if (cp < 0x80) {
		s_ch(b, (char)cp);
	} else if (cp < 0x800) {
		s_ch(b, (char)(0xC0 | (cp >> 6)));
		s_ch(b, (char)(0x80 | (cp & 0x3F)));
	} else if (cp < 0x10000) {
		s_ch(b, (char)(0xE0 | (cp >> 12)));
		s_ch(b, (char)(0x80 | ((cp >> 6) & 0x3F)));
		s_ch(b, (char)(0x80 | (cp & 0x3F)));
	} else {
		s_ch(b, (char)(0xF0 | (cp >> 18)));
		s_ch(b, (char)(0x80 | ((cp >> 12) & 0x3F)));
		s_ch(b, (char)(0x80 | ((cp >> 6) & 0x3F)));
		s_ch(b, (char)(0x80 | (cp & 0x3F)));
	}
}

/* True when a changed cell is close enough that moving costs more than drawing. */
int cn_near(int r, int c)
{
	int k;

	for (k = c; k < c + 3 && k < cn_back.cols; k++)
		if (!cn_same(&cn_back.c[r * cn_back.cols + k],
			      &cn_front.c[r * cn_front.cols + k]))
			return 1;
	return 0;
}

/* Send only the cells that changed, and return how many bytes that took. */
long cn_flush(void)
{
	int r, c, cr = -9, cc = -9;
	long n;
	str b;
	cn_cell *bk, *ft;

	if (!cn_on)
		return 0;
	if (cn_fit() != HIBR_OK)
		return 0;
	s_init(&b);
	for (r = 0; r < cn_back.rows; r++) {
		for (c = 0; c < cn_back.cols; c++) {
			bk = &cn_back.c[r * cn_back.cols + c];
			ft = &cn_front.c[r * cn_front.cols + c];
			if (bk->cont)
				continue;
			if (cn_same(bk, ft) &&
			    !(cr == r && cc == c && cn_near(r, c)))
				continue;
			cn_goto(&b, r, c, cr, cc);
			if (!cn_lset || bk->fg != cn_lfg ||
			    bk->bg != cn_lbg || bk->attr != cn_lat) {
				cn_sgr(&b, bk->fg, bk->bg, bk->attr);
				cn_lfg = bk->fg;
				cn_lbg = bk->bg;
				cn_lat = bk->attr;
				cn_lset = 1;
			}
			cn_enc(&b, bk->cp ? bk->cp : ' ');
			if (bk->ext)
				s_cat(&b, bk->ext);
			cr = r;
			cc = c + (bk->w ? bk->w : 1);
			cn_cellset(ft, bk->cp, bk->ext,
				   bk->ext ? strlen(bk->ext) : 0);
			ft->fg = bk->fg;
			ft->bg = bk->bg;
			ft->attr = bk->attr;
			ft->w = bk->w;
			ft->cont = bk->cont;
			if (bk->w == 2 && c + 1 < cn_back.cols) {
				cn_cell *b2 = bk + 1, *f2 = ft + 1;
				cn_cellset(f2, b2->cp, 0, 0);
				f2->w = b2->w;
				f2->cont = b2->cont;
				f2->fg = b2->fg;
				f2->bg = b2->bg;
				f2->attr = b2->attr;
			}
		}
	}
	if (cn_cvis) {
		cn_goto(&b, cn_crow, cn_ccol, cr, cc);
		if (cn_lvis != 1)
			s_cat(&b, "\033[?25h");
		cn_lvis = 1;
	} else if (cn_lvis != 0) {
		s_cat(&b, "\033[?25l");
		cn_lvis = 0;
	}
	n = (long)b.n;
	if (b.n)
		cn_wr(cn_fd, b.p, b.n);
	s_free(&b);
	lg(HIBR_LDBG, "screen flush wrote %ld bytes", n);
	return n;
}
