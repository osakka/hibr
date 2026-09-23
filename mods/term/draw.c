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

/* Whether (r, c) is where the cursor sits and ought to be drawn there: on,
   visible, not off in the scrollback looking at something that is not
   where the program left it, and actually inside the region asked for. */
int tm_atcursor(tm_t *t, int curon, int r, int c)
{
	return curon && t->vis && !t->view && r == t->cr && c == t->cc;
}

/* The attribute a cell draws with, cursor and selection both folded in.
   Both can want DP_REV; ORing the two conditions rather than XORing them
   in separately is what keeps a cursor sitting inside a selection reverse
   rather than cancelling back to plain. Underline is added the same way,
   never removed -- an underlined cursor over already-underlined text is
   still underlined either way. */
unsigned tm_cellattr(tm_t *t, const tm_cell *k, int curon, long ar, int r,
		     int c)
{
	unsigned at = k->attr;
	int cur = tm_atcursor(t, curon, r, c);

	if (tm_insel(t, ar, c) || (cur && t->cshape == TM_BLOCK))
		at ^= DP_REV;
	if (cur && t->cshape == TM_UNDER)
		at |= DP_UNDER;
	return at;
}

/* Put the terminal's screen on the display, at a position and within a size.

   Cells are written in runs that share a pen, because a run costs one pen
   change and one put, and a cell at a time costs one of each per cell. The
   display underneath is already a damage model, so what actually reaches
   the terminal is only what changed -- this decides how much work it takes
   to find that out, not how much is sent.

   curon says whether this window may show its cursor at all -- the desktop
   decides that, by focus, which this module knows nothing of. A bar cursor
   is drawn last, over whatever character was there: a full row of cells is
   not glyphs and a thin mark beside one, so replacing it is what a
   character-cell terminal has to do instead. */
void tm_draw(tm_t *t, const dp_api *dp, int row, int col, int h, int w,
	     int curon)
{
	int r, c, n;
	long ar;
	unsigned fg, bg, at;
	tm_cell *k;
	str run, bar;

	if (!dp)
		return;
	if (h <= 0 || h > t->rows)
		h = t->rows;
	if (w <= 0 || w > t->cols)
		w = t->cols;
	s_init(&run);
	for (r = 0; r < h; r++) {
		ar = tm_absrow(t, r);
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
			at = tm_cellattr(t, k, curon, ar, r, c);
			run.n = 0;
			if (run.p)
				run.p[0] = 0;
			n = c;
			while (c < w) {
				k = tm_vat(t, r, c);
				if (!k || !k->w)
					break;
				if (k->fg != fg || k->bg != bg ||
				    tm_cellattr(t, k, curon, ar, r, c) != at)
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
	if (tm_atcursor(t, curon, t->cr, t->cc) && t->cshape == TM_BAR &&
	    t->cc < w && t->cr < h) {
		k = tm_vat(t, t->cr, t->cc);
		s_init(&bar);
		tm_utf8(&bar, 0x258F);
		dp->pen(k ? k->fg : t->dfg, k ? k->bg : t->dbg, 0);
		dp->put(row + t->cr, col + t->cc, bar.p);
		s_free(&bar);
	}
}
