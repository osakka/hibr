#define _GNU_SOURCE

#include "hd.h"
#include <stdlib.h>
#include <string.h>

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
   reporting and its encoding, bracketed paste, the cursor's shape, and the
   window title the program set. Each is
   sent only the first time (cn->primed still 0) or on a genuine change,
   never every frame -- unlike the cell content below, still repainted in
   full every settled frame, this is cheap to get right from the start and
   expensive to get wrong: skipping the alternate-screen switch, say, means
   a session's own full-screen redraw lands in the client's *main* buffer
   instead, burning over whatever scrollback was there before it attached. */
int hd_modes(int tid, struct hd_cli *cn, str *out)
{
	int alt = 0, bpaste = 0, cshape = 0, mmode, sgr = 0, clear;
	str tt;

	hd_tm->modes(tid, &alt, &bpaste, &cshape);
	mmode = hd_tm->mouse(tid, &sgr);
	clear = !cn->primed || alt != cn->alt;

	if (!cn->primed || alt != cn->alt)
		s_cat(out, alt ? "\033[?1049h" : "\033[?1049l");
	if (!cn->primed || mmode != cn->mmode) {
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
	int r, c, lr, lc, have, cr, cc, cvis, clear;
	unsigned cp, fg, bg, at, w, lfg = 0, lbg = 0, lat = 0, lk, llk = 0;
	const char *uri;
	struct hd_cell *f;
	size_t n, i;

	out->n = 0;
	clear = hd_modes(tid, cn, out);
	if (!cn->front) {
		n = (size_t)cn->rows * (size_t)cn->cols;
		cn->front = xm(sizeof *cn->front * n);
		for (i = 0; i < n; i++)
			cn->front[i].cp = ~0u;
		clear = 1;
	}
	if (clear)
		s_cat(out, "\033[H\033[2J");
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
			if (r != lr || c != lc)
				hd_goto(out, r, c);
			if (!have || fg != lfg || bg != lbg || at != lat) {
				hd_sgr(out, fg, bg, at);
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
			/* The continuation half is never itself read from
			   tm_api (w is 0 there, skipped above) so it is never
			   otherwise kept in step -- left stale, a wide glyph
			   later replaced by two narrow ones would find its
			   second half's old value here matching by
			   coincidence and never get sent. */
			if (w == 2 && c + 1 < cn->cols)
				cn->front[(size_t)r * cn->cols + c + 1] = *f;
			lr = r;
			lc = c + (int)w;
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
	if (hd_tm->images && (clear || cn->imgen != hd_tm->imgen(tid))) {
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
