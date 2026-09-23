#define _GNU_SOURCE

#include "tm.h"
#include <stdlib.h>
#include <string.h>

/* Append a code point to a string as UTF-8. */
void tm_utf8(str *b, unsigned cp)
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

/* Put the terminal's screen on the display, at a position and within a size.

   Cells are written in runs that share a pen, because a run costs one pen
   change and one put, and a cell at a time costs one of each per cell. The
   display underneath is already a damage model, so what actually reaches
   the terminal is only what changed -- this decides how much work it takes
   to find that out, not how much is sent. */
void tm_draw(tm_t *t, const dp_api *dp, int row, int col, int h, int w)
{
	int r, c, n;
	unsigned fg, bg, at;
	tm_cell *k;
	str run;

	if (!dp)
		return;
	if (h <= 0 || h > t->rows)
		h = t->rows;
	if (w <= 0 || w > t->cols)
		w = t->cols;
	s_init(&run);
	for (r = 0; r < h; r++) {
		c = 0;
		while (c < w) {
			k = tm_vat(t, r, c);
			if (!k) {
				c++;
				continue;
			}
			if (!k->w) {
				c++;
				continue;
			}
			fg = k->fg;
			bg = k->bg;
			at = k->attr;
			run.n = 0;
			if (run.p)
				run.p[0] = 0;
			n = c;
			while (c < w) {
				k = tm_vat(t, r, c);
				if (!k || !k->w)
					break;
				if (k->fg != fg || k->bg != bg ||
				    k->attr != at)
					break;
				tm_utf8(&run, k->cp ? k->cp : ' ');
				c += k->w;
			}
			if (run.n) {
				dp->pen(fg, bg, at);
				dp->put(row + r, col + n, run.p);
			}
			if (c == n)
				c++;
		}
	}
	s_free(&run);
}
