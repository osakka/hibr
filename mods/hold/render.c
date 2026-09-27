#define _GNU_SOURCE

#include "hd.h"

/* Append the escape sequence for one colour -- the same default/256/24-bit
   encoding console's own cn_sgrcol (mods/console/grid.c) emits, duplicated
   rather than shared: hold must not depend on the display module, and this
   is a small, stable, self-contained piece of string building with nothing
   else of console's to entangle it. */
void hd_sgrcol(str *b, unsigned v, int bgp)
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

/* Append the escape sequence that moves from one pen to another -- console's
   own cn_sgr, duplicated for the same reason as hd_sgrcol above. */
void hd_sgr(str *b, unsigned fg, unsigned bg, unsigned at)
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
	hd_sgrcol(b, fg, 0);
	hd_sgrcol(b, bg, 1);
	s_ch(b, 'm');
}

/* Append an absolute cursor move. */
void hd_goto(str *b, int row, int col)
{
	s_cat(b, "\033[");
	s_num(b, (long)(row + 1));
	s_ch(b, ';');
	s_num(b, (long)(col + 1));
	s_ch(b, 'H');
}

/* Append a code point as UTF-8 -- term's own tm_utf8 (mods/term/draw.c),
   duplicated for the same reason as hd_sgr above. */
void hd_utf8(str *b, unsigned cp)
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

/* One client's own rectangle of the union grid, as a full screen: cleared,
   every visible cell in it, and the real cursor if it falls inside --
   translated from the union's own coordinates into the client's. No
   diffing yet: every frame is a full repaint of just this client's own
   rows and columns. That is the whole point of this slice -- correctness
   of the clipping, not the cost of repainting it. */
void hd_render(int tid, struct hd_cli *cn, str *out)
{
	int r, c, have, cr, cc, cvis;
	unsigned cp, fg, bg, at, w, lfg = 0, lbg = 0, lat = 0;

	out->n = 0;
	s_cat(out, "\033[H\033[2J");
	for (r = 0; r < cn->rows; r++) {
		hd_goto(out, r, 0);
		have = 0;
		for (c = 0; c < cn->cols; c++) {
			if (!hd_tm->at(tid, cn->row + r, cn->col + c, &cp,
					&fg, &bg, &at, &w) || !w)
				continue;
			if (!have || fg != lfg || bg != lbg || at != lat) {
				hd_sgr(out, fg, bg, at);
				lfg = fg;
				lbg = bg;
				lat = at;
				have = 1;
			}
			hd_utf8(out, cp ? cp : ' ');
		}
	}
	if (hd_tm->cursor(tid, &cr, &cc, &cvis) && cvis &&
	    cr >= cn->row && cr < cn->row + cn->rows && cc >= cn->col &&
	    cc < cn->col + cn->cols) {
		hd_goto(out, cr - cn->row, cc - cn->col);
		s_cat(out, "\033[?25h");
	} else {
		s_cat(out, "\033[?25l");
	}
}

/* Render and send every attached client its own rectangle. A client whose
   send fails is dropped, the same as a failed mirror used to be. */
void hd_rensend(vec *cls, int tid)
{
	size_t i;
	str out;

	s_init(&out);
	for (i = 0; i < cls->n; ) {
		struct hd_cli *cn = cls->p[i];

		hd_render(tid, cn, &out);
		if (!hd_send(cn->fd, HD_DATA, out.p, out.n))
			hd_cdrop(cls, cn->fd);
		else
			i++;
	}
	s_free(&out);
}
