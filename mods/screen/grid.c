#define _GNU_SOURCE

#include "scr.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern int scr_fd;
extern int scr_on;
void scr_wr(int fd, const char *p, size_t n);

scr_grid scr_back, scr_front;
unsigned scr_fg, scr_bg, scr_penat;
int scr_crow, scr_ccol, scr_cvis;
unsigned scr_lfg, scr_lbg, scr_lat;
int scr_lset, scr_lvis = -1;

/* Release every cell and the grid itself. */
void scr_gfree(scr_grid *g)
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
int scr_gsize(scr_grid *g, int rows, int cols)
{
	int i, n = rows * cols;

	if (rows <= 0 || cols <= 0)
		return HIBR_FAIL;
	if (g->rows == rows && g->cols == cols)
		return HIBR_OK;
	scr_gfree(g);
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
void scr_cellset(scr_cell *c, unsigned cp, const char *ext, size_t en)
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
void scr_inval(void)
{
	int i, n = scr_front.rows * scr_front.cols;

	for (i = 0; i < n; i++)
		scr_front.c[i].cp = 0xFFFFFFFFu;
	scr_lset = 0;
	scr_lvis = -1;
}

/* Make both grids match the terminal, after a resize or at open. */
int scr_fit(void)
{
	int rows, cols;

	scr_size(&rows, &cols);
	if (scr_back.rows == rows && scr_back.cols == cols)
		return HIBR_OK;
	if (scr_gsize(&scr_back, rows, cols) != HIBR_OK)
		return HIBR_FAIL;
	if (scr_gsize(&scr_front, rows, cols) != HIBR_OK)
		return HIBR_FAIL;
	scr_inval();
	lg(HIBR_LDBG, "screen now %d by %d", rows, cols);
	return HIBR_OK;
}

/* Set the pen used by later writes. */
void scr_pen(unsigned fg, unsigned bg, unsigned attr)
{
	scr_fg = fg;
	scr_bg = bg;
	scr_penat = attr;
}

/* Read the pen back. */
void scr_getpen(unsigned *fg, unsigned *bg, unsigned *attr)
{
	*fg = scr_fg;
	*bg = scr_bg;
	*attr = scr_penat;
}

/* Blank the back buffer with the current pen. */
void scr_clear(void)
{
	int i, n;

	if (scr_fit() != HIBR_OK)
		return;
	n = scr_back.rows * scr_back.cols;
	for (i = 0; i < n; i++) {
		scr_cellset(&scr_back.c[i], ' ', 0, 0);
		scr_back.c[i].w = 1;
		scr_back.c[i].cont = 0;
		scr_back.c[i].fg = scr_fg;
		scr_back.c[i].bg = scr_bg;
		scr_back.c[i].attr = scr_penat;
	}
}

/* Overwrite a cell that is half of a wide glyph, and its other half. */
void scr_split(int row, int col)
{
	scr_cell *c;

	if (row < 0 || row >= scr_back.rows || col < 0 || col >= scr_back.cols)
		return;
	c = &scr_back.c[row * scr_back.cols + col];
	if (c->cont && col > 0) {
		scr_cellset(c - 1, ' ', 0, 0);
		(c - 1)->w = 1;
	}
	if (c->w == 2 && col + 1 < scr_back.cols) {
		scr_cellset(c + 1, ' ', 0, 0);
		(c + 1)->w = 1;
		(c + 1)->cont = 0;
	}
}

/* Write text into the back buffer, returning the columns it advanced. */
int scr_put(int row, int col, const char *t)
{
	size_t n, i = 0;
	int start = col, w, l;
	unsigned cp;
	scr_cell *c;

	if (scr_fit() != HIBR_OK || !t)
		return 0;
	n = strlen(t);
	while (i < n) {
		l = u8dec(t + i, n - i, &cp);
		w = u8w(cp);
		if (w == 0) {
			if (col > start && row >= 0 && row < scr_back.rows) {
				c = &scr_back.c[row * scr_back.cols + col - 1];
				if (!c->ext)
					scr_cellset(c, c->cp, t + i, (size_t)l);
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
		if (row < 0 || row >= scr_back.rows || col < 0 ||
		    col + w > scr_back.cols) {
			col += w;
			i += (size_t)l;
			continue;
		}
		scr_split(row, col);
		if (w == 2)
			scr_split(row, col + 1);
		c = &scr_back.c[row * scr_back.cols + col];
		scr_cellset(c, cp, 0, 0);
		c->w = (unsigned char)w;
		c->cont = 0;
		c->fg = scr_fg;
		c->bg = scr_bg;
		c->attr = scr_penat;
		if (w == 2) {
			scr_cellset(c + 1, 0, 0, 0);
			(c + 1)->w = 0;
			(c + 1)->cont = 1;
			(c + 1)->fg = scr_fg;
			(c + 1)->bg = scr_bg;
			(c + 1)->attr = scr_penat;
		}
		col += w;
		i += (size_t)l;
	}
	return col - start;
}

/* Repeat one character over a rectangle. */
void scr_fill(int row, int col, int h, int w, const char *t)
{
	int r, c, adv;

	if (!t || !*t)
		t = " ";
	for (r = row; r < row + h; r++)
		for (c = col; c < col + w;) {
			adv = scr_put(r, c, t);
			c += adv > 0 ? adv : 1;
		}
}

/* Place the cursor, and say whether it should be seen. */
void scr_cursor(int row, int col, int vis)
{
	scr_crow = row;
	scr_ccol = col;
	scr_cvis = vis;
}

/* Append the escape sequence for one colour. */
void scr_sgrcol(str *b, unsigned v, int bgp)
{
	if (v == SCR_DEFAULT) {
		s_cat(b, bgp ? ";49" : ";39");
		return;
	}
	if (v & SCR_RGB) {
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
void scr_sgr(str *b, unsigned fg, unsigned bg, unsigned at)
{
	s_cat(b, "\033[0");
	if (at & SCR_BOLD)
		s_cat(b, ";1");
	if (at & SCR_DIM)
		s_cat(b, ";2");
	if (at & SCR_ITAL)
		s_cat(b, ";3");
	if (at & SCR_UNDER)
		s_cat(b, ";4");
	if (at & SCR_BLINK)
		s_cat(b, ";5");
	if (at & SCR_REV)
		s_cat(b, ";7");
	if (at & SCR_STRIKE)
		s_cat(b, ";9");
	scr_sgrcol(b, fg, 0);
	scr_sgrcol(b, bg, 1);
	s_ch(b, 'm');
}

/* Append a cursor move, preferring the shorter form when on the same row. */
void scr_goto(str *b, int row, int col, int cr, int cc)
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
int scr_same(const scr_cell *a, const scr_cell *b)
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
void scr_enc(str *b, unsigned cp)
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
int scr_near(int r, int c)
{
	int k;

	for (k = c; k < c + 3 && k < scr_back.cols; k++)
		if (!scr_same(&scr_back.c[r * scr_back.cols + k],
			      &scr_front.c[r * scr_front.cols + k]))
			return 1;
	return 0;
}

/* Send only the cells that changed, and return how many bytes that took. */
long scr_flush(void)
{
	int r, c, cr = -9, cc = -9;
	long n;
	str b;
	scr_cell *bk, *ft;

	if (!scr_on)
		return 0;
	if (scr_fit() != HIBR_OK)
		return 0;
	s_init(&b);
	for (r = 0; r < scr_back.rows; r++) {
		for (c = 0; c < scr_back.cols; c++) {
			bk = &scr_back.c[r * scr_back.cols + c];
			ft = &scr_front.c[r * scr_front.cols + c];
			if (bk->cont)
				continue;
			if (scr_same(bk, ft) &&
			    !(cr == r && cc == c && scr_near(r, c)))
				continue;
			scr_goto(&b, r, c, cr, cc);
			if (!scr_lset || bk->fg != scr_lfg ||
			    bk->bg != scr_lbg || bk->attr != scr_lat) {
				scr_sgr(&b, bk->fg, bk->bg, bk->attr);
				scr_lfg = bk->fg;
				scr_lbg = bk->bg;
				scr_lat = bk->attr;
				scr_lset = 1;
			}
			scr_enc(&b, bk->cp ? bk->cp : ' ');
			if (bk->ext)
				s_cat(&b, bk->ext);
			cr = r;
			cc = c + (bk->w ? bk->w : 1);
			scr_cellset(ft, bk->cp, bk->ext,
				   bk->ext ? strlen(bk->ext) : 0);
			ft->fg = bk->fg;
			ft->bg = bk->bg;
			ft->attr = bk->attr;
			ft->w = bk->w;
			ft->cont = bk->cont;
			if (bk->w == 2 && c + 1 < scr_back.cols) {
				scr_cell *b2 = bk + 1, *f2 = ft + 1;
				scr_cellset(f2, b2->cp, 0, 0);
				f2->w = b2->w;
				f2->cont = b2->cont;
				f2->fg = b2->fg;
				f2->bg = b2->bg;
				f2->attr = b2->attr;
			}
		}
	}
	if (scr_cvis) {
		scr_goto(&b, scr_crow, scr_ccol, cr, cc);
		if (scr_lvis != 1)
			s_cat(&b, "\033[?25h");
		scr_lvis = 1;
	} else if (scr_lvis != 0) {
		s_cat(&b, "\033[?25l");
		scr_lvis = 0;
	}
	n = (long)b.n;
	if (b.n)
		scr_wr(scr_fd, b.p, b.n);
	s_free(&b);
	lg(HIBR_LDBG, "screen flush wrote %ld bytes", n);
	return n;
}
