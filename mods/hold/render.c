#define _GNU_SOURCE

#include "hd.h"
#include <stdlib.h>
#include <string.h>

/* How many colours the client being rendered takes: what Control Panel
   forces for every display, else what this one said, else 24-bit (Gitea
   #353). Set by hd_render for the frame it builds. */
static int hd_cdep = 24;

/* Append the escape sequence for one colour, stepped down to what the
   client takes. The encoding is gfxask's, shared with the console: hold
   must not depend on the display module, and gfxask is neither. */
void hd_sgrcol(str *b, unsigned v, int bgp)
{
	gx_sgrcol(b, v, bgp, hd_cdep);
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

/* From the pen the client's terminal has to this one: only the colour that
   changed when the attributes are the same -- what console's cn_sgrto does
   too, and for the same reason: a cell over a translucent window or a
   picture differs from the one before it in its background alone, and the
   full reset form sent the foreground and the attributes again for each.
   have says whether the client's pen is known at all. */
void hd_sgrto(str *b, unsigned fg, unsigned bg, unsigned at, int have,
	      unsigned lfg, unsigned lbg, unsigned lat)
{
	size_t p;

	if (!have || at != lat) {
		hd_sgr(b, fg, bg, at);
		return;
	}
	s_cat(b, "\033[");
	p = b->n;
	if (fg != lfg)
		hd_sgrcol(b, fg, 0);
	if (bg != lbg)
		hd_sgrcol(b, bg, 1);
	if (b->n > p && b->p[p] == ';') {
		memmove(b->p + p, b->p + p + 1, b->n - p - 1);
		b->n--;
		b->p[b->n] = 0;
	}
	s_ch(b, 'm');
}

/* One of the program's rectangle copies, replayed on a client whose
   terminal copies too (Gitea #337): clipped so that both ends lie inside
   this client's own view -- what it cannot see it cannot copy from -- sent
   as DECCRA, and made to this client's front exactly as sent, so the diff
   that follows sends only what the copy did not already put right. The
   destination's edge columns and the one each side are marked unknown,
   since how a terminal splits a wide glyph at an edge is its own business
   and the diff is what settles it: a copy can only ever save bytes. */
void hd_copyto(struct hd_cli *cn, str *out, int r, int c, int h, int w,
	       int dr, int dc)
{
	int sr = r - cn->row, sc = c - cn->col, tr = dr - cn->row, tc = dc - cn->col;
	int lo, hi, i, k;
	struct hd_cell *tmp;

	lo = 0;
	if (-sr > lo) lo = -sr;
	if (-tr > lo) lo = -tr;
	hi = h;
	if (cn->rows - sr < hi) hi = cn->rows - sr;
	if (cn->rows - tr < hi) hi = cn->rows - tr;
	if (hi <= lo)
		return;
	sr += lo;
	tr += lo;
	h = hi - lo;
	lo = 0;
	if (-sc > lo) lo = -sc;
	if (-tc > lo) lo = -tc;
	hi = w;
	if (cn->cols - sc < hi) hi = cn->cols - sc;
	if (cn->cols - tc < hi) hi = cn->cols - tc;
	if (hi <= lo)
		return;
	sc += lo;
	tc += lo;
	w = hi - lo;
	s_cat(out, "\033[");
	s_num(out, (long)sr + 1);
	s_ch(out, ';');
	s_num(out, (long)sc + 1);
	s_ch(out, ';');
	s_num(out, (long)(sr + h));
	s_ch(out, ';');
	s_num(out, (long)(sc + w));
	s_cat(out, ";1;");
	s_num(out, (long)tr + 1);
	s_ch(out, ';');
	s_num(out, (long)tc + 1);
	s_cat(out, ";1$v");
	tmp = xm(sizeof *tmp * (size_t)h * (size_t)w);
	for (i = 0; i < h; i++)
		memcpy(tmp + (size_t)i * w,
		       cn->front + (size_t)(sr + i) * cn->cols + sc,
		       sizeof *tmp * (size_t)w);
	for (i = 0; i < h; i++)
		memcpy(cn->front + (size_t)(tr + i) * cn->cols + tc,
		       tmp + (size_t)i * w, sizeof *tmp * (size_t)w);
	free(tmp);
	for (i = 0; i < h; i++) {
		struct hd_cell *row = cn->front + (size_t)(tr + i) * cn->cols;

		k = tc + w - 1;
		if (row[tc].w == 0)
			row[tc].cp = ~0u;
		if (tc > 0 && row[tc - 1].w == 2)
			row[tc - 1].cp = ~0u;
		if (row[k].w == 2)
			row[k].cp = ~0u;
		if (k + 1 < cn->cols && row[k + 1].w == 0)
			row[k + 1].cp = ~0u;
	}
}

/* FNV-1a, to tell one picture's bytes from another's without keeping them. */
unsigned hd_hash(const char *p, size_t n)
{
	unsigned h = 2166136261u;

	while (n--) {
		h ^= (unsigned char)*p++;
		h *= 16777619u;
	}
	return h;
}

/* Whether a client's own terminal draws this picture: a kitty transmission
   (ESC _ G) for a terminal that said kitty, a sixel (ESC P) for one that
   said sixel, and nothing for one that said neither or answered nothing. A
   client that was never asked -- carried across an upgrade from a server
   older than the asking -- is sent what it was sent before, every picture
   it contains (Gitea #274); only the resume path makes one, never an
   attach, so the gate stays shut for every client that could have said. */
int hd_canshow(const struct hd_cli *cn, const char *p, size_t n)
{
	if (cn->gfx == GX_UNASKED)
		return 1;
	if (n >= 3 && p[0] == '\033' && p[1] == '_' && p[2] == 'G')
		return cn->gfx == GX_KITTY;
	if (n >= 2 && p[0] == '\033' && p[1] == 'P')
		return cn->gfx == GX_SIXEL;
	return 0;
}

/* The kitty image id inside a picture's own escape, 0 for a sixel (which has
   none) or for anything without one: the bytes begin ESC _ G <control>, and
   the control data carries i=<id>. Read here rather than asked of the
   emulator, because it is in the escape hold is already holding. */
unsigned hd_imgid(const char *p, size_t n)
{
	size_t i;

	if (n < 4 || p[0] != '\033' || p[1] != '_')
		return 0;
	for (i = 2; i + 1 < n && p[i] != ';'; i++) {
		if (p[i] != 'i' || p[i + 1] != '=')
			continue;
		if (i > 2 && p[i - 1] != ',')
			continue;
		return (unsigned)strtoul(p + i + 2, 0, 10);
	}
	return 0;
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

/* One control sequence with a count, the count left out when it is 1:
   `CSI n f`. */
void hd_csin(str *b, long n, char f)
{
	s_cat(b, "\033[");
	if (n != 1)
		s_num(b, n);
	s_ch(b, f);
}

/* The cheapest way from where the client's cursor is (lr, lc; lr < 0 or lc
   past the last column when it is not known) to row, col (Gitea #353): an
   absolute move, or a vertical one and a horizontal one -- a carriage
   return, a column, or a count forward or back. A scrolled screen of text
   has its changed cells scattered over every row, and one absolute move
   before each was 37% of what a scrolling terminal window cost. A cursor
   past the last column is in the pending-wrap state, from which a relative
   move is not the same on every terminal, so only an absolute one goes. */
void hd_mvto(str *b, int lr, int lc, int row, int col, int cols)
{
	str v, h;
	size_t best;

	if (lr == row && lc == col)
		return;
	if (lr < 0 || lc < 0 || lc >= cols) {
		hd_goto(b, row, col);
		return;
	}
	s_init(&v);
	s_init(&h);
	if (row > lr)
		hd_csin(&v, row - lr, 'B');
	else if (row < lr)
		hd_csin(&v, lr - row, 'A');
	if (col == 0 && lc != 0)
		s_ch(&h, '\r');
	else if (col > lc)
		hd_csin(&h, col - lc, 'C');
	else if (col < lc)
		hd_csin(&h, lc - col, 'D');
	best = v.n + h.n;
	if (col != lc && col != 0) {
		str a;

		s_init(&a);
		hd_csin(&a, col + 1, 'G');
		if (a.n < h.n) {
			h.n = 0;
			s_add(&h, a.p, a.n);
			best = v.n + h.n;
		}
		s_free(&a);
	}
	{
		str a;

		s_init(&a);
		hd_goto(&a, row, col);
		if (a.n <= best) {
			s_add(b, a.p, a.n);
		} else {
			if (v.n)
				s_add(b, v.p, v.n);
			if (h.n)
				s_add(b, h.p, h.n);
		}
		s_free(&a);
	}
	s_free(&v);
	s_free(&h);
}

/* Whether a cell is a blank an erase can stand for: a space, alone in its
   cell, no link, and no attribute a blank shows -- reverse, underline and
   strike are all visible on a space. */
int hd_blankok(unsigned cp, unsigned at, unsigned w, unsigned lk)
{
	return (cp == ' ' || cp == 0) && w == 1 && !lk &&
	       !(at & (DP_REV | DP_UNDER | DP_STRIKE));
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

/* Bring a client's own terminal to the mode state the session is actually
   in right now, transition by transition: the alternate screen, mouse
   reporting and its encoding, bracketed paste, the cursor's shape, the
   window title the program set, and the kitty keyboard protocol's flags on
   a terminal that speaks it -- set after the screen switch, since such a
   terminal keeps one set of flags per screen (Gitea #326). Each is
   sent only the first time (cn->primed still 0) or on a genuine change,
   never every frame -- unlike the cell content below, still repainted in
   full every settled frame, this is cheap to get right from the start and
   expensive to get wrong: skipping the alternate-screen switch, say, means
   a session's own full-screen redraw lands in the client's *main* buffer
   instead, burning over whatever scrollback was there before it attached. */
int hd_modes(int tid, struct hd_cli *cn, str *out)
{
	int alt = 0, bpaste = 0, cshape = 0, mmode, sgr = 0, clear, kf, mch, pw;
	str tt;

	hd_tm->modes(tid, &alt, &bpaste, &cshape);
	mmode = hd_tm->mouse(tid, &sgr);
	clear = !cn->primed || alt != cn->alt;

	if (!cn->primed || alt != cn->alt)
		s_cat(out, alt ? "\033[?1049h" : "\033[?1049l");
	kf = cn->kbd ? hd_tm->kbd(tid) : 0;
	if (cn->kbd && (!cn->primed || alt != cn->alt || kf != cn->kset)) {
		s_cat(out, "\033[=");
		s_num(out, kf);
		s_cat(out, ";1u");
		cn->kset = kf;
		lg(HIBR_LDBG, "hold: keyboard flags %d put on a display", kf);
	}
	mch = !cn->primed || mmode != cn->mmode;
	if (mch) {
		s_cat(out, "\033[?1003l\033[?1002l\033[?1000l\033[?1006l");
		if (mmode == 1000)
			s_cat(out, "\033[?1000h");
		else if (mmode == 1002)
			s_cat(out, "\033[?1002h");
		else if (mmode == 1003)
			s_cat(out, "\033[?1003h");
		/* Always SGR on the client's own terminal, regardless of
		   whether the program asked for it: that is what lets
		   mtrans's own parser recognise every report the same way.
		   A program that never asked for SGR still gets one in that
		   form -- mrewrite re-encodes it back to what it did ask
		   for before it reaches the pty. */
		if (mmode)
			s_cat(out, "\033[?1006h");
	}
	/* Pixels only where the program wants them, the terminal said it can,
	   and its cell size is known -- without that a report cannot be put
	   in the session's own pixels. After 1006, since a terminal that keeps
	   one encoding takes the last one set, and turning it off puts SGR
	   back rather than leave the old form (Gitea #370). */
	pw = mmode && cn->mpix && hd_tm->mpix(tid) && cn->cols &&
	     cn->rows && cn->xpix / cn->cols > 1 && cn->ypix / cn->rows > 1;
	if (pw != cn->pixon || (mch && pw)) {
		if (pw)
			s_cat(out, "\033[?1016h");
		else if (cn->pixon)
			s_cat(out, mmode ? "\033[?1016l\033[?1006h" :
					   "\033[?1016l");
		cn->pixon = pw;
		lg(HIBR_LDBG, "hold: a display reports the mouse in %s",
		   pw ? "pixels" : "cells");
	}
	s_init(&tt);
	hd_tm->title(tid, &tt);
	if (tt.n != cn->title.n || (tt.n && memcmp(tt.p, cn->title.p, tt.n))) {
		s_cat(out, "\033]2;");
		if (tt.n)
			s_add(out, tt.p, tt.n);
		s_ch(out, 7);
		cn->title.n = 0;
		s_add(&cn->title, tt.p ? tt.p : "", tt.n);
	}
	s_free(&tt);
	/* The pointer shape the program asked for (Gitea #373), to a client
	   whose terminal knows shapes at all, and `none` only to one that
	   said it can hide: any other is sent its default, so a display that
	   cannot hide keeps its pointer rather than losing it to a name it
	   does not know. */
	if (cn->ptrask) {
		s_init(&tt);
		if (hd_tm->pointer)
			hd_tm->pointer(tid, &tt);
		if (cn->ptrask < 2 && tt.n == 4 && !memcmp(tt.p, "none", 4))
			tt.n = 0;
		if (tt.n != cn->ptrsh.n ||
		    (tt.n && memcmp(tt.p, cn->ptrsh.p, tt.n))) {
			s_cat(out, "\033]22;");
			if (tt.n)
				s_add(out, tt.p, tt.n);
			s_cat(out, "\033\\");
			cn->ptrsh.n = 0;
			s_add(&cn->ptrsh, tt.p ? tt.p : "", tt.n);
		}
		s_free(&tt);
	}
	if (!cn->primed || bpaste != cn->bpaste)
		s_cat(out, bpaste ? "\033[?2004h" : "\033[?2004l");
	if (!cn->primed || cshape != cn->cshape) {
		if (cshape == TM_UNDER)
			s_cat(out, "\033[4 q");
		else if (cshape == TM_BAR)
			s_cat(out, "\033[6 q");
		else
			s_cat(out, "\033[2 q");
	}
	cn->primed = 1;
	cn->alt = alt;
	cn->mmode = mmode;
	cn->msgr = sgr;
	cn->bpaste = bpaste;
	cn->cshape = cshape;
	return clear;
}

/* One client's own rectangle of the union grid, sent as a diff against
   what this client was last sent: only a cell that actually changed, a
   cursor move only when the next changed cell is not already where the
   last glyph left the terminal's own cursor, and the pen only when it
   differs from the last one actually emitted -- the same shape as
   console's own cn_flush, arrived at independently, since hold must not
   depend on the display module to share it with. front is allocated (and
   every cell in it marked impossible, cp ~0u, which nothing real is ever
   sent as) the first time this client is rendered, or again after srv.c
   frees it on a resize -- both cases fall straight out of front being
   NULL here, so this frame is a full repaint by construction, not a diff
   against stale or wrongly-sized content. */
void hd_render(int tid, struct hd_cli *cn, str *out)
{
	int r, c, lr, lc, have, cr, cc, cvis, clear, wipe = 0;
	unsigned cp, fg, bg, at, w, lfg = 0, lbg = 0, lat = 0, lk, llk = 0;
	const char *uri;
	struct hd_cell *f;
	size_t n, i;

	out->n = 0;
	hd_cdep = hd_fdepth ? hd_fdepth : cn->depth ? cn->depth : 24;
	/* The whole frame, the modes, the copies, the cells, the pictures
	   and the cursor, as one synchronized update: a terminal that knows
	   mode 2026 presents it at once and never half of it, however the
	   bytes are split on the way; one that does not ignores the pair
	   (Gitea #338). */
	s_cat(out, HD_SYNCH);
	clear = hd_modes(tid, cn, out);
	if (!cn->front) {
		n = (size_t)cn->rows * (size_t)cn->cols;
		cn->front = xm(sizeof *cn->front * n);
		for (i = 0; i < n; i++) {
			cn->front[i].cp = ~0u;
			cn->front[i].w = 1;
		}
		clear = 1;
	}
	if (clear)
		s_cat(out, "\033[H\033[2J");
	/* The program's copies since this client was last sent one, for a
	   terminal that copies: replayed before the diff, which then sends
	   what they did not put right. A full repaint needs none of them. */
	if (clear || !cn->copy) {
		cn->cseq = hd_tm->copyseq(tid);
	} else {
		unsigned cur = hd_tm->copyseq(tid), q;
		int cr0, cc0, ch0, cw0, cdr, cdc;

		for (q = cn->cseq + 1; q <= cur; q++)
			if (hd_tm->copyop(tid, q, &cr0, &cc0, &ch0, &cw0, &cdr, &cdc))
				hd_copyto(cn, out, cr0, cc0, ch0, cw0, cdr, cdc);
		cn->cseq = cur;
	}
	lr = -1;
	lc = -1;
	have = 0;
	for (r = 0; r < cn->rows; r++) {
		for (c = 0; c < cn->cols; c++) {
			if (!hd_tm->at(tid, cn->row + r, cn->col + c, &cp,
					&fg, &bg, &at, &w) || !w)
				continue;
			lk = hd_tm->linkat(tid, cn->row + r, cn->col + c);
			f = &cn->front[(size_t)r * cn->cols + c];
			if (f->cp == cp && f->fg == fg && f->bg == bg &&
			    f->attr == at && f->link == lk)
				continue;
			/* A run of blank cells in one pen as one erase (Gitea
			   #353): to the end of the row, or eight or more inside
			   it -- only where the erase gives the same colour, which
			   is a blank with none of its own on any terminal, and
			   any blank on one that said its erase paints the pen's
			   background. The cursor stays where the erase began. */
			if (hd_blankok(cp, at, w, lk) &&
			    (bg == DP_DEFAULT || cn->bce)) {
				int k = 1, end;
				unsigned c2, f2, b2, a2, w2;

				while (c + k < cn->cols &&
				       hd_tm->at(tid, cn->row + r, cn->col + c + k,
						 &c2, &f2, &b2, &a2, &w2) &&
				       w2 == 1 && (c2 == ' ' || c2 == 0) &&
				       f2 == fg && b2 == bg && a2 == at &&
				       !hd_tm->linkat(tid, cn->row + r,
						      cn->col + c + k))
					k++;
				end = c + k == cn->cols;
				if ((end && k >= 2) || k >= HD_ECHMIN) {
					int i2;

					hd_mvto(out, lr, lc, r, c, cn->cols);
					if (!have || fg != lfg || bg != lbg ||
					    at != lat) {
						hd_sgrto(out, fg, bg, at, have, lfg,
							 lbg, lat);
						lfg = fg;
						lbg = bg;
						lat = at;
						have = 1;
					}
					if (llk) {
						s_cat(out, "\033]8;;\a");
						llk = 0;
					}
					if (end)
						s_cat(out, "\033[K");
					else
						hd_csin(out, k, 'X');
					for (i2 = 0; i2 < k; i2++) {
						struct hd_cell *g2 = f + i2;

						g2->cp = ' ';
						g2->fg = fg;
						g2->bg = bg;
						g2->attr = at;
						g2->link = 0;
						g2->w = 1;
					}
					lr = r;
					lc = c;
					c += k - 1;
					continue;
				}
			}
			/* A short gap of unchanged cells in the pen already in
			   force is drawn over when that is shorter than moving
			   past it, as the console's cn_near does. */
			if (r != lr || c != lc) {
				str mv;
				int g = c - lc, ok = have && r == lr && lc >= 0 &&
						     g > 0 && g <= HD_GAPMAX;
				int i2;
				unsigned c2, f2, b2, a2, w2;

				s_init(&mv);
				hd_mvto(&mv, lr, lc, r, c, cn->cols);
				for (i2 = lc; ok && i2 < c; i2++)
					ok = hd_tm->at(tid, cn->row + r, cn->col + i2,
						       &c2, &f2, &b2, &a2, &w2) &&
					     w2 == 1 && (c2 == 0 || (c2 >= 32 && c2 < 127)) &&
					     f2 == lfg && b2 == lbg && a2 == lat &&
					     hd_tm->linkat(tid, cn->row + r,
							   cn->col + i2) == llk;
				if (ok && (size_t)g <= mv.n) {
					for (i2 = lc; i2 < c; i2++) {
						hd_tm->at(tid, cn->row + r,
							  cn->col + i2, &c2, &f2, &b2,
							  &a2, &w2);
						s_ch(out, c2 ? (int)c2 : ' ');
					}
				} else {
					s_add(out, mv.p, mv.n);
				}
				s_free(&mv);
			}
			if (!have || fg != lfg || bg != lbg || at != lat) {
				hd_sgrto(out, fg, bg, at, have, lfg, lbg, lat);
				lfg = fg;
				lbg = bg;
				lat = at;
				have = 1;
			}
			if (lk != llk) {
				uri = lk ? hd_tm->linkuri(tid, lk) : 0;
				s_cat(out, "\033]8;;");
				if (uri)
					s_cat(out, uri);
				s_ch(out, 7);
				llk = lk;
			}
			hd_utf8(out, cp ? cp : ' ');
			f->cp = cp;
			f->fg = fg;
			f->bg = bg;
			f->attr = at;
			f->link = lk;
			f->w = (unsigned char)w;
			/* The continuation half is never itself read from
			   tm_api (w is 0 there, skipped above) so it is never
			   otherwise kept in step -- left stale, a wide glyph
			   later replaced by two narrow ones would find its
			   second half's old value here matching by
			   coincidence and never get sent. */
			if (w == 2 && c + 1 < cn->cols) {
				cn->front[(size_t)r * cn->cols + c + 1] = *f;
				cn->front[(size_t)r * cn->cols + c + 1].w = 0;
			}
			lr = r;
			lc = c + (int)w;
			/* The same glyph in the same pen right after it, on a
			   terminal that repeats one (REP, Gitea #353): the count
			   instead of the glyphs, when that is shorter -- a box's
			   rule, a wallpaper's fill. */
			if (cn->rep && w == 1) {
				int k = 0, gl = cp < 0x80 ? 1 : cp < 0x800 ? 2 :
						cp < 0x10000 ? 3 : 4, i2;
				unsigned c2, f2, b2, a2, w2;
				str rp;

				while (c + 1 + k < cn->cols &&
				       hd_tm->at(tid, cn->row + r, cn->col + c + 1 + k,
						 &c2, &f2, &b2, &a2, &w2) &&
				       c2 == cp && w2 == 1 && f2 == fg && b2 == bg &&
				       a2 == at &&
				       hd_tm->linkat(tid, cn->row + r,
						     cn->col + c + 1 + k) == lk)
					k++;
				s_init(&rp);
				if (k > 0)
					hd_csin(&rp, k, 'b');
				if (k > 0 && rp.n < (size_t)(k * gl)) {
					s_add(out, rp.p, rp.n);
					for (i2 = 1; i2 <= k; i2++)
						f[i2] = *f;
					lc = c + 1 + k;
					c += k;
				}
				s_free(&rp);
			}
		}
	}
	if (llk)
		s_cat(out, "\033]8;;\a");
	/* The pictures the program put on the session's grid. A bitmap is not
	   cells: it cannot be diffed and it cannot be clipped, so each goes
	   out whole at its own corner, after the text. One that does not fit
	   inside this client's own rectangle is skipped rather than cut in
	   half.

	   Only what changed, though. A kitty picture is an object the
	   terminal keeps, so this client's own list of (id, hash of the bytes
	   sent) says which it already has: one whose bytes are unchanged is
	   not sent again, and an id the session no longer has is deleted,
	   since nothing else would take it off the screen. Re-placing every
	   picture whenever any one of them changed meant a viewer's own
	   picture moving re-sent an 860 kB wallpaper with it, which is what
	   made pixels unusable over a slow link. A sixel carries no id and is
	   paint: it is simply sent again. */
	if (!cn->kwiped && (cn->gfx == GX_KITTY || cn->gfx == GX_UNASKED)) {
		s_cat(out, "\033_Ga=d,d=A,q=2\033\\");
		cn->pn = 0;
		cn->kwiped = 1;
		wipe = 1;
		lg(HIBR_LDBG, "hold: a display's earlier pictures cleared");
	} else if (!cn->kwiped && (cn->gfx == GX_SIXEL || cn->gfx == GX_NONE)) {
		/* Only a display that has said it draws something else is
		   done without one: one that has not said yet is sent no
		   picture, and may yet turn out to be kitty -- deciding on
		   its first render, before its answer could have arrived,
		   sent the first picture with no wipe before it. */
		cn->kwiped = 1;
	}
	if (hd_tm->images && (clear || wipe || cn->imgen != hd_tm->imgen(tid))) {
		int ir, ic, irows, icols, k, n = hd_tm->images(tid), sent = 0;
		unsigned *nid, *nhash;
		size_t nn = 0, j;
		str pic;

		nid = n > 0 ? xm((size_t)n * sizeof *nid) : 0;
		nhash = n > 0 ? xm((size_t)n * sizeof *nhash) : 0;
		s_init(&pic);
		for (k = 0; k < n; k++) {
			unsigned id, h;
			int had = 0;

			pic.n = 0;
			if (!hd_tm->image(tid, k, &ir, &ic, &irows, &icols,
					  &pic))
				continue;
			if (ir < cn->row || ic < cn->col ||
			    ir + irows > cn->row + cn->rows ||
			    ic + icols > cn->col + cn->cols)
				continue;
			/* And only a picture this client's own terminal
			   said it draws: a kitty transmission to a kitty
			   terminal, a sixel to a sixel one. A bitmap sent
			   anywhere else is a black rectangle nothing
			   repaints, and a client that has not said --
			   one from before 0.99.235 -- is sent none (Gitea
			   #258). */
			if (!hd_canshow(cn, pic.p, pic.n))
				continue;
			id = hd_imgid(pic.p, pic.n);
			h = hd_hash(pic.p, pic.n);
			if (id && !clear)
				for (j = 0; j < cn->pn; j++)
					if (cn->pid[j] == id &&
					    cn->phash[j] == h) {
						had = 1;
						break;
					}
			if (id) {
				nid[nn] = id;
				nhash[nn] = h;
				nn++;
			}
			if (had)
				continue;
			hd_goto(out, ir - cn->row, ic - cn->col);
			s_add(out, pic.p, pic.n);
			sent++;
		}
		s_free(&pic);
		/* Whatever this client was holding that the session no longer
		   has: a kitty picture stays on a terminal until it is told to
		   go, and the program's own delete was consumed by the
		   emulator. */
		for (j = 0; j < cn->pn; j++) {
			size_t m;
			int live = 0;

			for (m = 0; m < nn; m++)
				if (nid[m] == cn->pid[j]) {
					live = 1;
					break;
				}
			if (live)
				continue;
			s_cat(out, "\033_Ga=d,d=I,q=2,i=");
			s_num(out, (long)cn->pid[j]);
			s_cat(out, "\033\\");
		}
		free(cn->pid);
		free(cn->phash);
		cn->pid = nid;
		cn->phash = nhash;
		cn->pn = nn;
		cn->imgen = hd_tm->imgen(tid);
		if (sent)
			lg(HIBR_LDBG, "hold: %d picture(s) passed on, %zu held",
			   sent, nn);
	}
	if (hd_tm->cursor(tid, &cr, &cc, &cvis) && cvis &&
	    cr >= cn->row && cr < cn->row + cn->rows && cc >= cn->col &&
	    cc < cn->col + cn->cols) {
		hd_goto(out, cr - cn->row, cc - cn->col);
		s_cat(out, "\033[?25h");
	} else {
		s_cat(out, "\033[?25l");
	}
	s_cat(out, HD_SYNCL);
}

/* Render and send one client its own rectangle; drop it from the list on a
   failed send, the same as a failed mirror used to be. This is also what a
   just-attached client gets immediately, on its own, before anything new
   has come from the pty to render -- a program that does not react to its
   own SIGWINCH (cat, say, unlike a shell redrawing its prompt) would
   otherwise leave a newly attached client seeing nothing at all until the
   program next writes something on its own, which for a quiet session may
   be never. */
void hd_rensend1(vec *cls, struct hd_cli *cn, int tid)
{
	str out;

	s_init(&out);
	hd_render(tid, cn, &out);
	cn->sent += (long long)out.n;
	cn->frames++;
	if (!hd_send(cn->fd, HD_DATA, out.p, out.n))
		hd_cdrop(cls, cn->fd);
	s_free(&out);
}

/* Pass on to every attached terminal what the program sent for the
   terminal rather than the screen -- a clipboard set, a bell, a
   notification -- as it came. The emulator draws nothing for any of them,
   so without this none reached any machine; sent to every client, each
   reaches all of them. */
void hd_passsend(vec *cls, int tid)
{
	str c, o;
	size_t i;
	struct hd_cli *cn;

	s_init(&c);
	s_init(&o);
	if (hd_tm->clip(tid, &c)) {
		s_cat(&o, "\033]52;");
		s_add(&o, c.p, c.n);
		s_ch(&o, 7);
	}
	hd_tm->pass(tid, &o);
	if (!o.n) {
		s_free(&o);
		s_free(&c);
		return;
	}
	for (i = cls->n; i-- > 0;) {
		cn = cls->p[i];
		if (!hd_send(cn->fd, HD_DATA, o.p, o.n))
			hd_cdrop(cls, cn->fd);
	}
	lg(HIBR_LDBG, "hold: %zu bytes passed on to %zu terminals", o.n,
	   cls->n);
	s_free(&o);
	s_free(&c);
}

/* Render and send every attached client its own rectangle. */
void hd_rensend(vec *cls, int tid)
{
	size_t i;

	for (i = 0; i < cls->n; ) {
		struct hd_cli *cn = cls->p[i];
		size_t before = cls->n;

		hd_rensend1(cls, cn, tid);
		if (cls->n == before)
			i++;
	}
}
