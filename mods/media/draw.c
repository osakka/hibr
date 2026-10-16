#define _GNU_SOURCE

#include "mv.h"
#include <string.h>

/* One cell as it will be drawn: its pen and its text. */
typedef struct mv_cell mv_cell;
struct mv_cell {
	unsigned fg, bg;
	const char *t;
};

/* The size the picture is drawn at now, which a frame made before a
   resize or a change of mode does not have yet. */
int mv_tw, mv_th;

/* The pixel of the frame at x, y of the picture as drawn now, sampled
   from whatever size the frame was made at; 0 outside the picture. */
uint8_t *mv_at(mv_vf *f, int x, int y)
{
	if (x < 0 || y < 0 || x >= mv_tw || y >= mv_th)
		return 0;
	if (mv_tw != f->w)
		x = (int)((long)x * f->w / mv_tw);
	if (mv_th != f->h)
		y = (int)((long)y * f->h / mv_th);
	return f->rgb + ((size_t)y * f->w + x) * 3;
}

/* The colour of a pixel, or black outside the picture. */
unsigned mv_px(mv_vf *f, int x, int y)
{
	uint8_t *q = mv_at(f, x, y);

	if (!q)
		return DP_RGB;
	return DP_RGB | (unsigned)q[0] << 16 | (unsigned)q[1] << 8 | q[2];
}

/* What one cell of the picture is: two pixels as a half block, or one as
   a character chosen by its brightness. */
void mv_cellof(mv_pl *p, int mode, mv_vf *f, int x, int y, mv_cell *c)
{
	static const char *const ch[] = { " ", ".", ":", "-", "=", "+", "*", "#",
					  "%", "@" };
	unsigned a, b;
	int lum;
	uint8_t *q;

	if (mode == 0) {
		a = mv_px(f, x, y * 2);
		b = mv_px(f, x, y * 2 + 1);
		c->bg = b;
		if (a == b) {
			c->fg = a;
			c->t = " ";
		} else {
			c->fg = a;
			c->t = "\xe2\x96\x80";
		}
		return;
	}
	q = mv_at(f, x, y);
	if (!q) {
		c->fg = mode == 1 ? DP_RGB : DP_DEFAULT;
		c->bg = c->fg;
		c->t = " ";
		return;
	}
	lum = (q[0] * 299 + q[1] * 587 + q[2] * 114) / 1000;
	c->t = ch[lum * 9 / 255];
	if (mode == 1) {
		c->fg = DP_RGB | (unsigned)q[0] << 16 | (unsigned)q[1] << 8 | q[2];
		c->bg = DP_RGB;
	} else {
		c->fg = DP_DEFAULT;
		c->bg = DP_DEFAULT;
	}
}

/* Draw the frame shown into the grid it was made for, at row, col of the
   screen, clipped to the rectangle prow, pcol, ph, pw: the picture in the
   middle, black around it. Cells of one pen go out as one run. Fails when
   there is no frame yet. */
int mv_draw(sh *s, const dp_api *dp, mv_pl *p, int row, int col, int prow, int pcol,
	    int ph, int pw)
{
	mv_vf *f = &p->cur;
	int cols = p->tcols > 0 ? p->tcols : 80, rows = p->trows > 0 ? p->trows : 24;
	int fh, ox, oy, r, c, c0, sr, sc, cm;
	mv_cell a, k;
	str run;

	if (!f->rgb)
		return HIBR_FAIL;
	/* The cells to draw when a bitmap is not what happens. For the
	   pixel mode that is **half blocks**, the best cells there are --
	   it used to fall through to this function's own brightness ramp,
	   one character per cell, so asking for pixels and not getting
	   them gave a worse picture than asking for half blocks, which is
	   the wrong way round for a setting that promises the best
	   (Gitea #199). */
	cm = p->mode == 3 ? 0 : p->mode;
	pthread_mutex_lock(&p->mu);
	mv_fit(p, p->mode, &mv_tw, &mv_th);
	pthread_mutex_unlock(&p->mu);
	fh = p->mode == 0 ? (mv_th + 1) / 2 : mv_th;
	ox = (cols - mv_tw) / 2;
	oy = (rows - fh) / 2;
	/* Pixels where the display can place them: the frame as it was decoded,
	   which the backend scales to the rectangle itself. The fixed palette,
	   not one chosen per frame -- it is the same every frame, so a terminal
	   keeps its colour registers and a film does not flicker through 256 new
	   ones twenty-four times a second. */
	if (p->mode == 3 && dp->image) {
		int ir = row + (oy > 0 ? oy : 0), ic = col + (ox > 0 ? ox : 0);
		int ih = fh < rows ? fh : rows, iwc = mv_tw < cols ? mv_tw : cols;
		int fits = ih > 0 && iwc > 0 && ir >= prow && ic >= pcol &&
			   ir + ih <= prow + ph && ic + iwc <= pcol + pw;

		lg(HIBR_LDBG, "media: pixels grid %dx%d fit %dx%d at %d,%d "
			      "rect %d,%d %dx%d clip %d,%d %dx%d fits %d",
		   cols, rows, mv_tw, mv_th, row, col, ir, ic, ih, iwc,
		   prow, pcol, ph, pw, fits);
		if (fits && dp->image(s, 0, ir, ic, ih, iwc, f->rgb, f->w,
				      f->h, 0)) {
			p->drew = 1;
			return HIBR_OK;
		}
		if (fits)
			lg(HIBR_LDBG, "media: the display refused the picture");
		/* Refused: the grid was fitted for a bitmap, and half
		   blocks want their own -- twice the rows, since a cell
		   holds two of them. Without this the lower half of the
		   picture reads off the end of the grid and comes back
		   black. */
		pthread_mutex_lock(&p->mu);
		mv_fit(p, cm, &mv_tw, &mv_th);
		pthread_mutex_unlock(&p->mu);
		fh = cm == 0 ? (mv_th + 1) / 2 : mv_th;
		ox = (cols - mv_tw) / 2;
		oy = (rows - fh) / 2;
	}
	p->drew = 0;
	s_init(&run);
	for (r = 0; r < rows; r++) {
		sr = row + r;
		if (sr < prow || sr >= prow + ph)
			continue;
		c = 0;
		while (c < cols) {
			c0 = c;
			mv_cellof(p, cm, f, c - ox, r - oy, &a);
			run.n = 0;
			while (c < cols) {
				mv_cellof(p, cm, f, c - ox, r - oy, &k);
				if (k.fg != a.fg || k.bg != a.bg || strcmp(k.t, a.t))
					break;
				sc = col + c;
				if (sc >= pcol && sc < pcol + pw)
					s_cat(&run, k.t);
				c++;
			}
			if (!run.n)
				continue;
			sc = col + c0;
			if (sc < pcol)
				sc = pcol;
			dp->pen(a.fg, a.bg, 0);
			dp->put(sr, sc, run.p);
		}
	}
	s_free(&run);
	return HIBR_OK;
}
