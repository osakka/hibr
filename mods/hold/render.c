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

/* Append the escape sequence that sets a whole pen (gfxask's gx_sgr, shared
   with the console), at the client's colour depth. */
void hd_sgr(str *b, unsigned fg, unsigned bg, unsigned at, unsigned ul)
{
	gx_sgr(b, fg, bg, at, ul, hd_cdep);
}

/* What a display that is not Blit gets for Blit's glyphs: the desktop's table,
   sorted by code point, and a hollow square for one it does not name. */
static unsigned *hd_subk, *hd_subv;
static size_t hd_subn;

/* What to send for code point cp of Blit's private plane. */
unsigned hd_subst(unsigned cp)
{
	size_t a = 0, b = hd_subn;

	while (a < b) {
		size_t m = a + (b - a) / 2;

		if (hd_subk[m] == cp)
			return hd_subv[m];
		if (hd_subk[m] < cp)
			a = m + 1;
		else
			b = m;
	}
	return 0x25A1;
}

/* Take the desktop's table, `CODEPOINT REPLACEMENT` a line, in hex and in
   order of code point. */
void hd_substset(const char *p, size_t n)
{
	size_t i = 0, cap = 0, k = 0;
	unsigned *ks = 0, *vs = 0;

	while (i < n) {
		char *e;
		unsigned long c, v;
		str l;

		s_init(&l);
		while (i < n && p[i] != '\n')
			s_ch(&l, p[i++]);
		i++;
		if (!l.n) {
			s_free(&l);
			continue;
		}
		s_ch(&l, 0);
		c = strtoul(l.p, &e, 16);
		v = strtoul(e, &e, 16);
		s_free(&l);
		if (!c || !v || (k && c <= ks[k - 1]))
			continue;
		if (k == cap) {
			cap = cap ? cap * 2 : 256;
			ks = xr(ks, cap * sizeof *ks);
			vs = xr(vs, cap * sizeof *vs);
		}
		ks[k] = (unsigned)c;
		vs[k++] = (unsigned)v;
	}
	free(hd_subk);
	free(hd_subv);
	hd_subk = ks;
	hd_subv = vs;
	hd_subn = k;
	lg(HIBR_LDBG, "hold: %zu of Blit's glyphs have a plain form", k);
}

/* Forget what this client was sent, so every cell goes again. */
void hd_cstale(struct hd_cli *cn)
{
	size_t i, n = (size_t)cn->rows * (size_t)cn->cols;

	if (!cn->front)
		return;
	for (i = 0; i < n; i++)
		cn->front[i].cp = ~0u;
}

/* From the pen the client's terminal has to this one, through gfxask's
   gx_sgrto; have says whether the client's pen is known at all. */
void hd_sgrto(str *b, unsigned fg, unsigned bg, unsigned at, unsigned ul,
	      int have, unsigned lfg, unsigned lbg, unsigned lat, unsigned lul)
{
	gx_sgrto(b, fg, bg, at, ul, have, lfg, lbg, lat, lul, hd_cdep);
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
	unsigned long long g = 2166136261u, x;

	/* Eight bytes a step, the way the console hashes a picture: every
	   picture is hashed on every render that follows a change, and a
	   film's frame is 6.4 MB -- byte at a time it was 38% of a held
	   video's whole cost. */
	while (n >= 8) {
		memcpy(&x, p, 8);
		g ^= x;
		g *= 1099511628211ull;
		p += 8;
		n -= 8;
	}
	while (n--) {
		g ^= (unsigned char)*p++;
		g *= 1099511628211ull;
	}
	return (unsigned)(g ^ (g >> 32));
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

/* A picture this display holds and does not show now -- it is on another
   display, or this one cannot draw it -- loses only its placement there
   (d=i) and keeps its image, so it can come back: an image deleted outright
   (d=I) is one Blit remembers and refuses frames for, which is how a game
   moved off a display and back stayed black there (Gitea #492). It stays on
   the display's list unplaced (a hash of 0), so the session dropping it
   later still deletes it there, and a return sends its placement again --
   with its newest frame first, since an unplaced image is one Blit may
   free when memory is short, so what this display was last sent of it is
   forgotten here too. */
void hd_unshow(struct hd_cli *cn, unsigned id, str *out, unsigned *nid,
	       unsigned *nhash, size_t *nlen, unsigned *nbase, size_t *nn,
	       int n)
{
	size_t j, m;

	if (!id || *nn >= (size_t)n)
		return;
	for (j = 0; j < cn->pn && cn->pid[j] != id; j++)
		;
	if (j == cn->pn)
		return;
	for (m = 0; m < *nn; m++)
		if (nid[m] == id)
			return;
	if (cn->phash[j]) {
		s_cat(out, "\033_Ga=d,d=i,q=2,i=");
		s_num(out, (long)id);
		s_cat(out, "\033\\");
	}
	for (m = 0; m < cn->fsn; m++)
		if (cn->fsid[m] == id) {
			cn->fsid[m] = cn->fsid[cn->fsn - 1];
			cn->fsseq[m] = cn->fsseq[cn->fsn - 1];
			cn->fsn--;
			break;
		}
	nid[*nn] = id;
	nhash[*nn] = 0;
	nlen[*nn] = 0;
	nbase[*nn] = 0;
	(*nn)++;
}

/* A one-letter key of a kitty picture's first control string, into v: 1
   when it is there. */
int hd_imgkey(const char *p, size_t n, char key, long *v)
{
	size_t i;

	if (n < 4 || p[0] != '\033' || p[1] != '_' || p[2] != 'G')
		return 0;
	for (i = 3; i + 1 < n && p[i] != ';' && p[i] != '\033'; i++) {
		if (p[i] != key || p[i + 1] != '=')
			continue;
		if (i > 3 && p[i - 1] != ',')
			continue;
		*v = strtol(p + i + 2, 0, 10);
		return 1;
	}
	return 0;
}

/* A kitty picture's size in its own pixels: what its transmission says (s=,
   v=), or for one placed by descriptor what its newest frame says. 0 when
   neither is known -- a PNG names no size -- and then it cannot be cut. */
int hd_imgpx(const char *p, size_t n, unsigned id, int *pw, int *ph)
{
	long s = 0, v = 0;

	if (hd_imgkey(p, n, 's', &s) && hd_imgkey(p, n, 'v', &v) && s > 0 &&
	    v > 0) {
		*pw = (int)s;
		*ph = (int)v;
		return 1;
	}
	return id && hd_frmpx(id, pw, ph);
}

/* A picture drawn at its own size across this display's edge -- Firefox's
   page in a window over the seam of two stacked displays -- cut to the
   part of it on this display, by its pixels: its rectangle on the screen
   (its cell, its X/Y offset into it, its source size) met with the
   display's, the source rectangle and the offset worked out again for the
   cell the part starts in, which goes back in *r, *c. 0 when none of it is
   here, or the display's cell size is not known. */
int hd_imgnatcut(struct hd_cli *cn, str *pic, str *cut, int *r, int *c)
{
	long x = 0, y = 0, w = 0, h = 0, ox = 0, oy = 0;
	long cw, ch, top, left, t, l, b, rt;

	if (!cn->cols || !cn->rows || cn->xpix < cn->cols || cn->ypix < cn->rows)
		return 0;
	cw = cn->xpix / cn->cols;
	ch = cn->ypix / cn->rows;
	if (!hd_imgkey(pic->p, pic->n, 'w', &w) || !hd_imgkey(pic->p, pic->n, 'h', &h) ||
	    w <= 0 || h <= 0)
		return 0;
	hd_imgkey(pic->p, pic->n, 'x', &x);
	hd_imgkey(pic->p, pic->n, 'y', &y);
	hd_imgkey(pic->p, pic->n, 'X', &ox);
	hd_imgkey(pic->p, pic->n, 'Y', &oy);
	top = *r * ch + oy;
	left = *c * cw + ox;
	t = top > cn->row * ch ? top : cn->row * ch;
	l = left > cn->col * cw ? left : cn->col * cw;
	b = top + h < (cn->row + cn->rows) * ch ? top + h : (cn->row + cn->rows) * ch;
	rt = left + w < (cn->col + cn->cols) * cw ? left + w : (cn->col + cn->cols) * cw;
	if (b <= t || rt <= l)
		return 0;
	cut->n = 0;
	hd_imgcutnat(cut, pic->p, pic->n, (int)(x + l - left), (int)(y + t - top),
		     (int)(rt - l), (int)(b - t), (int)(l % cw), (int)(t % ch));
	pic->n = 0;
	s_add(pic, cut->p, cut->n);
	*r = (int)(t / ch);
	*c = (int)(l / cw);
	return 1;
}

/* Whether a placement is drawn at its own size: an a=p with neither c= nor
   r=, as a program window's subsurface is (Gitea #496). */
int hd_imgnat(const char *p, size_t n)
{
	long v;

	return n > 6 && !memcmp(p, "\033_Ga=p", 6) &&
	       !hd_imgkey(p, n, 'c', &v) && !hd_imgkey(p, n, 'r', &v);
}

/* A picture drawn at its own size (no c= or r=: a program window's
   subsurface, Gitea #496) showing only part of itself: the source
   rectangle x, y, w by h of it, offset ox, oy into the cell it is placed
   at -- every key but those passed on as they were. */
void hd_imgcutnat(str *o, const char *p, size_t n, int x, int y, int w,
		  int h, int ox, int oy)
{
	size_t i = 3, s, e;
	int first = 1;

	for (e = 3; e < n && p[e] != ';' && p[e] != '\033'; e++)
		;
	s_add(o, p, 3);
	while (i < e) {
		for (s = i; s < e && p[s] != ','; s++)
			;
		if (!(s - i >= 2 && p[i + 1] == '=' &&
		      strchr("xywhXY", p[i]))) {
			if (!first)
				s_ch(o, ',');
			s_add(o, p + i, s - i);
			first = 0;
		}
		i = s + 1;
	}
	if (!first)
		s_ch(o, ',');
	s_cat(o, "x=");
	s_num(o, x);
	s_cat(o, ",y=");
	s_num(o, y);
	s_cat(o, ",w=");
	s_num(o, w);
	s_cat(o, ",h=");
	s_num(o, h);
	if (ox) {
		s_cat(o, ",X=");
		s_num(o, ox);
	}
	if (oy) {
		s_cat(o, ",Y=");
		s_num(o, oy);
	}
	s_add(o, p + e, n - e);
}

/* The same picture, showing only part of itself: its first control string
   has the source rectangle (x, y, w, h, in its own pixels) and the cells it
   fills (c, r) put in place of any it had, and every byte after that is
   passed on as it was. Blit honours the rectangle on a placement (its ADR
   0060), and so does kitty (Gitea #492). */
void hd_imgcrop(str *o, const char *p, size_t n, int x, int y, int w, int h,
		int c, int r)
{
	size_t i = 3, s, e;
	int first = 1;

	for (e = 3; e < n && p[e] != ';' && p[e] != '\033'; e++)
		;
	s_add(o, p, 3);
	while (i < e) {
		for (s = i; s < e && p[s] != ','; s++)
			;
		if (!(s - i >= 2 && p[i + 1] == '=' &&
		      strchr("xywhcr", p[i]))) {
			if (!first)
				s_ch(o, ',');
			s_add(o, p + i, s - i);
			first = 0;
		}
		i = s + 1;
	}
	if (!first)
		s_ch(o, ',');
	s_cat(o, "x=");
	s_num(o, x);
	s_cat(o, ",y=");
	s_num(o, y);
	s_cat(o, ",w=");
	s_num(o, w);
	s_cat(o, ",h=");
	s_num(o, h);
	s_cat(o, ",c=");
	s_num(o, c);
	s_cat(o, ",r=");
	s_num(o, r);
	s_add(o, p + e, n - e);
}

/* How many of a kept kitty picture's bytes are its transmission: its
   escapes up to and including the first that is not followed by more (no
   m=1). What comes after is edits. n when there is no end to be found. */
size_t hd_imgbase(const char *p, size_t n)
{
	size_t i = 0, e, k;
	int more;

	while (i + 3 < n && p[i] == '\033' && p[i + 1] == '_') {
		for (e = i + 2; e + 1 < n && !(p[e] == '\033' && p[e + 1] == '\\');
		     e++)
			;
		if (e + 1 >= n)
			return n;
		more = 0;
		for (k = i + 2; k + 3 < e && p[k] != ';'; k++)
			if ((k == i + 3 || p[k - 1] == ',') && p[k] == 'm' &&
			    p[k + 1] == '=' && p[k + 2] == '1')
				more = 1;
		i = e + 2;
		if (!more)
			return i;
	}
	return n;
}

/* Append an absolute cursor move. */
void hd_goto(str *b, int row, int col)
{
	gx_mvto(b, -1, -1, row, col, 0);
}

/* One control sequence with a count, the count left out when it is 1. */
void hd_csin(str *b, long n, char f)
{
	gx_csin(b, n, f);
}

/* The cheapest way from where the client's cursor is to row, col: gfxask's
   gx_mvto (Gitea #353). */
void hd_mvto(str *b, int lr, int lc, int row, int col, int cols)
{
	gx_mvto(b, lr, lc, row, col, cols);
}

/* Whether a cell is a blank an erase can stand for: a space, alone in its
   cell, no link, and no attribute a blank shows -- reverse, underline and
   strike are all visible on a space. */
int hd_blankok(unsigned cp, unsigned at, unsigned w, unsigned lk)
{
	return (cp == ' ' || cp == 0) && w == 1 && !lk &&
	       !(at & (DP_REV | DP_UNDER | DP_STRIKE | DP_FRAME | DP_SHKIND));
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
	kf = hd_tm->focus(tid);
	if (kf != cn->fset) {
		s_cat(out, kf ? "\033[?1004h" : "\033[?1004l");
		cn->fset = kf;
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
	/* The pointer lock the program asked for (Gitea #379), to the one
	   display that can hold it and was used last, once per ask: a display
	   that gave it back is not asked again until the program asks again,
	   and a lock the program gives up goes from whichever display has it. */
	{
		int g = hd_tm->plock ? hd_tm->plock(tid) : 0;

		if (g && g != cn->lockgen && cn->lockok &&
		    cn->fd == hd_locktgt(hd_rencls, tid)) {
			s_cat(out, "\033[?7701h");
			cn->lockgen = g;
			cn->plock = 1;
			lg(HIBR_LDBG, "hold: a display asked to hold the pointer");
		} else if (!g && cn->plock) {
			s_cat(out, "\033[?7701l");
			cn->plock = 0;
		}
		if (!g)
			cn->lockgen = 0;
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
vec *hd_rencls;

/* The display the pointer lock goes to: one that can hold it and shows the
   cell the program asked from -- the desktop puts the cursor on the window
   that wants the pointer -- the last used of them where two show it, and the
   last used of all where none does or nothing said where. */
int hd_locktgt(vec *cls, int tid)
{
	int r, c, got = -1;
	size_t i;

	if (!cls || !hd_tm->plockat || !hd_tm->plockat(tid, &r, &c))
		return hd_lockfd;
	for (i = 0; i < cls->n; i++) {
		struct hd_cli *k = cls->p[i];

		if (!k->lockok || r < k->row || r >= k->row + k->rows ||
		    c < k->col || c >= k->col + k->cols)
			continue;
		if (k->fd == hd_lockfd)
			return k->fd;
		if (got < 0)
			got = k->fd;
	}
	if (got >= 0 && got != hd_lockfd)
		lg(HIBR_LDBG, "hold: the pointer lock goes to the display showing %d,%d", r, c);
	return got >= 0 ? got : hd_lockfd;
}

void hd_render(int tid, struct hd_cli *cn, str *out)
{
	int r, c, lr, lc, have, cr, cc, cvis, clear, wipe = 0;
	unsigned cp, fg, bg, at, pa, w, lfg = 0, lbg = 0, lat = 0, lk, llk = 0;
	unsigned ul, lul = DP_DEFAULT;
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
		clear = 1;
	}
	/* A clear is a whole new screen, so nothing this client was sent is
	   on it any more and every cell goes again -- and it is erased in no
	   colour of its own. The terminal's pen is whatever the last frame
	   ended on, and an erase paints the pen's background on a terminal
	   that does that (bce): an alternate screen left and entered again,
	   which is what Restart Desktop does, cleared a display to the last
	   window's face and kept only the cells that had changed, so its
	   whole desktop stayed that colour until something drew over it
	   (Gitea #387). */
	if (clear) {
		n = (size_t)cn->rows * (size_t)cn->cols;
		for (i = 0; i < n; i++) {
			cn->front[i].cp = ~0u;
			cn->front[i].w = 1;
		}
		s_cat(out, "\033[0m\033[H\033[2J");
	}
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
			ul = hd_tm->ulat(tid, cn->row + r, cn->col + c);
			f = &cn->front[(size_t)r * cn->cols + c];
			if (f->cp == cp && f->fg == fg && f->bg == bg &&
			    f->attr == at && f->link == lk && f->ul == ul)
				continue;
			pa = cn->blit == 2 ? at & ~(DP_FRAME | DP_SHADOW | DP_ICON) : at;
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
				       hd_tm->ulat(tid, cn->row + r,
						   cn->col + c + k) == ul &&
				       !hd_tm->linkat(tid, cn->row + r,
						      cn->col + c + k))
					k++;
				end = c + k == cn->cols;
				if ((end && k >= 2) || k >= HD_ECHMIN) {
					int i2;

					hd_mvto(out, lr, lc, r, c, cn->cols);
					if (!have || fg != lfg || bg != lbg ||
					    pa != lat || ul != lul) {
						hd_sgrto(out, fg, bg, pa, ul, have,
							 lfg, lbg, lat, lul);
						lfg = fg;
						lbg = bg;
						lat = pa;
						lul = ul;
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
						g2->ul = ul;
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
					     hd_tm->ulat(tid, cn->row + r,
							 cn->col + i2) == lul &&
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
			if (!have || fg != lfg || bg != lbg || pa != lat ||
			    ul != lul) {
				hd_sgrto(out, fg, bg, pa, ul, have, lfg, lbg, lat,
					 lul);
				lfg = fg;
				lbg = bg;
				lat = pa;
				lul = ul;
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
			u8put(out, cn->blit == 2 && cp >= 0x100000 ? hd_subst(cp)
							      : cp ? cp : ' ');
			f->cp = cp;
			f->fg = fg;
			f->bg = bg;
			f->attr = at;
			f->ul = ul;
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
				       hd_tm->ulat(tid, cn->row + r,
						   cn->col + c + 1 + k) == ul &&
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
	   cells: it cannot be diffed, so each goes out at its own corner,
	   after the text -- whole, or as this client's part of it when it
	   crosses the client's edge (Gitea #492).

	   Only what changed, though. A kitty picture is an object the
	   terminal keeps, so this client's own list of (id, hash of the bytes
	   sent) says which it already has: one whose bytes are unchanged is
	   not sent again, and an id the session no longer has is deleted,
	   since nothing else would take it off the screen. Re-placing every
	   picture whenever any one of them changed meant a viewer's own
	   picture moving re-sent an 860 kB wallpaper with it, which is what
	   made pixels unusable over a slow link. A sixel carries no id and is
	   paint: it is simply sent again. */
	/* Only the first wipe a display is ever sent deletes its images
	   (d=A): Blit remembers an image deleted outright, refuses its frames
	   for good and ends its input route, so a display carried across
	   `hold upgrade`, or asked again what it draws, loses only its
	   placements (d=a) and has them sent again below (Gitea #492). */
	if (!cn->kwiped && (cn->gfx == GX_KITTY || cn->gfx == GX_UNASKED)) {
		s_cat(out, cn->kwonce ? "\033_Ga=d,d=a,q=2\033\\" :
					"\033_Ga=d,d=A,q=2\033\\");
		cn->kwonce = 1;
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
		int g[4];
		unsigned *nid, *nhash, *nbase;
		size_t *nlen, nn = 0, j, from, base;
		str pic, cut;

		nid = n > 0 ? xm((size_t)n * sizeof *nid) : 0;
		nhash = n > 0 ? xm((size_t)n * sizeof *nhash) : 0;
		nlen = n > 0 ? xm((size_t)n * sizeof *nlen) : 0;
		nbase = n > 0 ? xm((size_t)n * sizeof *nbase) : 0;
		hd_frmsweep(tid);
		hd_f3sweep(tid);
		s_init(&pic);
		s_init(&cut);
		for (k = 0; k < n; k++) {
			unsigned id, h;
			int had = 0;

			pic.n = 0;
			if (!hd_tm->image(tid, k, &ir, &ic, &irows, &icols,
					  &pic))
				continue;
			id = hd_imgid(pic.p, pic.n);
			g[0] = ir;
			g[1] = ic;
			g[2] = irows;
			g[3] = icols;
			/* A picture out of view -- a program window minimised,
			   on another workspace (Gitea #499): kept by the
			   emulator covering no cells, so it loses its placement
			   on every display and keeps its image there. */
			if (irows < 1 || icols < 1 || !hd_onclient(cn, g)) {
				hd_unshow(cn, id, out, nid, nhash, nlen, nbase,
					  &nn, n);
				continue;
			}
			/* And only a picture this client's own terminal
			   said it draws: a kitty transmission to a kitty
			   terminal, a sixel to a sixel one. A bitmap sent
			   anywhere else is a black rectangle nothing
			   repaints, and a client that has not said --
			   one from before 0.99.235 -- is sent none (Gitea
			   #258). */
			if (!hd_canshow(cn, pic.p, pic.n))
				continue;
			/* A picture across the edge of this display -- one
			   window over two Blit sessions, as one tall screen
			   split in two is -- is sent as the part of itself on
			   this display, with a source rectangle in its own
			   pixels (Gitea #492). A sixel is paint and cannot be
			   cut, and a picture whose size is not known cannot
			   be either: both are left off, as every straddling
			   picture was before. */
			/* One drawn at its own size (a program window's
			   subsurface, Gitea #496) has no cells to cut by: it
			   goes whole to the display holding its top-left cell,
			   which clips it at its own edge, and to no other. */
			if (pic.p[1] == '_' && hd_imgnat(pic.p, pic.n) &&
			    (ir < cn->row || ic < cn->col ||
			     ir + irows > cn->row + cn->rows ||
			     ic + icols > cn->col + cn->cols)) {
				if (!hd_imgnatcut(cn, &pic, &cut, &ir, &ic)) {
					hd_unshow(cn, id, out, nid, nhash, nlen,
						  nbase, &nn, n);
					continue;
				}
			} else if (ir < cn->row || ic < cn->col ||
			    ir + irows > cn->row + cn->rows ||
			    ic + icols > cn->col + cn->cols) {
				int r0 = ir > cn->row ? ir : cn->row;
				int c0 = ic > cn->col ? ic : cn->col;
				int r1 = ir + irows < cn->row + cn->rows ?
						 ir + irows : cn->row + cn->rows;
				int c1 = ic + icols < cn->col + cn->cols ?
						 ic + icols : cn->col + cn->cols;
				int pw, ph, py0, py1, px0, px1;

				if (pic.p[1] != '_' ||
				    !hd_imgpx(pic.p, pic.n, id, &pw, &ph)) {
					hd_unshow(cn, id, out, nid, nhash, nlen,
						  nbase, &nn, n);
					continue;
				}
				/* A placement already showing part of its
				   picture -- a program window cut to its
				   geometry (Gitea #496) -- is cut within that
				   part. */
				{
					long bx = 0, by = 0, bw = 0, bh = 0;

					if (hd_imgkey(pic.p, pic.n, 'w', &bw) &&
					    hd_imgkey(pic.p, pic.n, 'h', &bh) &&
					    bw > 0 && bh > 0) {
						hd_imgkey(pic.p, pic.n, 'x', &bx);
						hd_imgkey(pic.p, pic.n, 'y', &by);
						pw = (int)bw;
						ph = (int)bh;
					}
					py0 = (int)by + (int)((long)(r0 - ir) * ph / irows);
					py1 = (int)by + (int)((long)(r1 - ir) * ph / irows);
					px0 = (int)bx + (int)((long)(c0 - ic) * pw / icols);
					px1 = (int)bx + (int)((long)(c1 - ic) * pw / icols);
				}
				if (py1 <= py0 || px1 <= px0) {
					hd_unshow(cn, id, out, nid, nhash, nlen,
						  nbase, &nn, n);
					continue;
				}
				cut.n = 0;
				hd_imgcrop(&cut, pic.p, pic.n, px0, py0,
					   px1 - px0, py1 - py0, c1 - c0, r1 - r0);
				pic.n = 0;
				s_add(&pic, cut.p, cut.n);
				ir = r0;
				ic = c0;
			}
			h = hd_hash(pic.p, pic.n);
			/* A placement by descriptor that moves sends the same
			   bytes at another cell (Gitea #486, a stream keeps its
			   image and placement for life), so where it is is part
			   of what a display already has. Never edited, so the
			   prefix checks below lose nothing by it. */
			if (id && pic.n > 5 && !memcmp(pic.p, "\033_Ga=p", 6))
				h ^= (unsigned)ir * 2654435761u ^
				     (unsigned)ic * 40503u;
			base = id ? hd_imgbase(pic.p, pic.n) : pic.n;
			from = 0;
			if (id && !clear)
				for (j = 0; j < cn->pn; j++) {
					if (cn->pid[j] != id)
						continue;
					if (cn->phash[j] == h) {
						had = 1;
						break;
					}
					/* Edited since: what it has, then more. */
					if (cn->plen && cn->plen[j] < pic.n &&
					    hd_hash(pic.p, cn->plen[j]) == cn->phash[j])
						from = cn->plen[j];
					/* Or edited whole since: what follows
					   the transmission it already has. */
					else if (cn->pbase && base < pic.n &&
						 cn->plen[j] >= base &&
						 hd_hash(pic.p, base) == cn->pbase[j])
						from = base;
					break;
				}
			/* A picture placed by descriptor: its pixels go to the
			   display's client first, on hold's own socket, so Blit
			   has them when the placement arrives. */
			if (id && pic.n > 5 && !memcmp(pic.p, "\033_Ga=p", 6)) {
				hd_frmsend(cn, id);
				hd_f3send(cn, id);
			}
			if (id) {
				nid[nn] = id;
				nhash[nn] = h;
				nlen[nn] = pic.n;
				nbase[nn] = hd_hash(pic.p, base);
				nn++;
			}
			if (had)
				continue;
			if (from) {
				s_add(out, pic.p + from, pic.n - from);
			} else {
				hd_goto(out, ir - cn->row, ic - cn->col);
				s_add(out, pic.p, pic.n);
			}
			sent++;
		}
		s_free(&pic);
		s_free(&cut);
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
		/* And every picture this display was sent frames for by
		   descriptor that it no longer shows: deleted on its terminal
		   if it never had the placement too -- or a frame alone made
		   an image there nothing would ever take away -- and forgotten
		   here either way, so the picture coming back is sent its frame
		   again with its placement. Kept, the record said the display
		   already had the newest frame, the placement went out alone,
		   and Blit refused it: the video never came back (0.99.400). */
		for (j = 0; j < cn->fsn;) {
			size_t m;
			int live = 0, placed = 0;

			for (m = 0; m < nn; m++)
				if (nid[m] == cn->fsid[j]) {
					live = 1;
					break;
				}
			if (live) {
				j++;
				continue;
			}
			for (m = 0; m < cn->pn; m++)
				if (cn->pid[m] == cn->fsid[j])
					placed = 1;
			if (!placed) {
				s_cat(out, "\033_Ga=d,d=I,q=2,i=");
				s_num(out, (long)cn->fsid[j]);
				s_cat(out, "\033\\");
			}
			cn->fsid[j] = cn->fsid[cn->fsn - 1];
			cn->fsseq[j] = cn->fsseq[cn->fsn - 1];
			cn->fsn--;
		}
		free(cn->pid);
		free(cn->phash);
		free(cn->plen);
		free(cn->pbase);
		cn->pid = nid;
		cn->phash = nhash;
		cn->plen = nlen;
		cn->pbase = nbase;
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

	hd_rencls = cls;
	/* Nothing until it has said what it draws, which comes at once from
	   any client that will: its first frame must already be the right
	   one for its terminal (Gitea #476). */
	if (cn->gfxwait)
		return;
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
