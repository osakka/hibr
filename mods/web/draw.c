#define _GNU_SOURCE

#include "wb.h"
#include <string.h>

/* Append a code point as UTF-8. */
void wb_putu(str *b, unsigned cp)
{
	if (cp < 0x80) {
		s_ch(b, (int)cp);
	} else if (cp < 0x800) {
		s_ch(b, (int)(0xC0 | (cp >> 6)));
		s_ch(b, (int)(0x80 | (cp & 0x3F)));
	} else if (cp < 0x10000) {
		s_ch(b, (int)(0xE0 | (cp >> 12)));
		s_ch(b, (int)(0x80 | ((cp >> 6) & 0x3F)));
		s_ch(b, (int)(0x80 | (cp & 0x3F)));
	} else {
		s_ch(b, (int)(0xF0 | (cp >> 18)));
		s_ch(b, (int)(0x80 | ((cp >> 12) & 0x3F)));
		s_ch(b, (int)(0x80 | ((cp >> 6) & 0x3F)));
		s_ch(b, (int)(0x80 | (cp & 0x3F)));
	}
}

/* Draw a tab's last frame, rows by w at row, col of the screen, clipped to
   the rectangle prow, pcol, ph, pw; runs of one pen go out together, and a
   link's cells carry its address. Fails when there is no frame yet. */
int wb_draw(sh *s, const dp_api *dp, wb_tab *t, int row, int col, int h, int w,
	    int prow, int pcol, int ph, int pw)
{
	int r, c, c0, sr, sc;
	wb_cell *k, *a;
	str run;
	const char *uri;

	/* As pixels the page is the page -- its own fonts, its pictures, its
	   layout -- and there is no text layer at all: a text cell paints its
	   own background, so anything drawn over the bitmap would box itself
	   out of it (ADR 0037). Clicks and keys still reach the page, since
	   those go by coordinate rather than through the cells. */
	if (t->pixels && t->shot && dp->image) {
		int ih = h < t->rows ? h : t->rows;

		if (ih > 0 && w > 0 && row >= prow && col >= pcol &&
		    row + ih <= prow + ph && col + w <= pcol + pw &&
		    dp->image(s, 0, row, col, ih, w, t->shot, t->shotw, t->shoth,
			      DP_IMG_CHOSEN))
			return HIBR_OK;
	}
	if (!t->grid)
		return HIBR_FAIL;
	s_init(&run);
	for (r = 0; r < h && r < t->grows; r++) {
		sr = row + r;
		if (sr < prow || sr >= prow + ph)
			continue;
		c = 0;
		while (c < w && c < t->gcols) {
			c0 = c;
			a = t->grid + (size_t)r * t->gcols + c;
			run.n = 0;
			while (c < w && c < t->gcols) {
				k = t->grid + (size_t)r * t->gcols + c;
				if (k->fg != a->fg || k->bg != a->bg ||
				    k->attr != a->attr || k->link != a->link)
					break;
				sc = col + c;
				if (sc >= pcol && sc < pcol + pw && k->cp != WB_CONT)
					wb_putu(&run, k->cp ? k->cp : ' ');
				c++;
			}
			if (!run.n)
				continue;
			sc = col + c0;
			if (sc < pcol)
				sc = pcol;
			uri = a->link >= 0 && (size_t)a->link < t->links.n ?
			      t->links.p[a->link] : 0;
			if (dp->link)
				dp->link(uri);
			dp->pen(a->fg, a->bg, a->attr);
			dp->put(sr, sc, run.p);
		}
	}
	if (dp->link)
		dp->link(0);
	s_free(&run);
	return HIBR_OK;
}
