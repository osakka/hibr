#define _GNU_SOURCE

#include "tm.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>

extern const py_api *tm_pty;

void tm_ctl(tm_t *t, unsigned c);
void tm_csi(tm_t *t, int f);
void tm_escd(tm_t *t, int f);
void tm_oscend(tm_t *t);
void tm_dcsend(tm_t *t);

/* xterm keeps no more than 30 parameters and caps each at 65535; anything
   beyond is dropped rather than grown into. */
#ifndef TM_PMAX
#define TM_PMAX 32
#endif
#ifndef TM_SMAX
#define TM_SMAX (1 << 20)
#endif

/* Milliseconds on a clock that never jumps, for synchronized output's own
   timeout. */
long tm_ms(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (long)ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

/* Forget the sequence being collected: its parameters, its prefix and its
   intermediates. */
void tm_pclear(tm_t *t)
{
	t->pn = 0;
	t->pcur = 0;
	t->pdig = 0;
	t->pfx = 0;
	t->in.n = 0;
	if (t->in.p)
		t->in.p[0] = 0;
}

/* Close the parameter being read: its value, or -1 when it had no digits,
   and whether a ':' introduced it as a sub-parameter of the one before. */
void tm_ppush(tm_t *t)
{
	if (t->pn >= TM_PMAX)
		return;
	if (t->pn == t->pcap) {
		t->pcap = t->pcap ? t->pcap * 2 : 8;
		t->pv = xr(t->pv, (size_t)t->pcap * sizeof *t->pv);
		t->ps = xr(t->ps, (size_t)t->pcap);
	}
	t->pv[t->pn] = (t->pdig & 1) ? t->pcur : -1;
	t->ps[t->pn] = (t->pdig & 2) ? 1 : 0;
	t->pn++;
	t->pcur = 0;
	t->pdig = 0;
}

/* One parameter byte: a digit, or the ';' and ':' between them. */
void tm_pbyte(tm_t *t, unsigned c)
{
	if (c >= '0' && c <= '9') {
		if (t->pcur < 65535)
			t->pcur = t->pcur * 10 + (int)(c - '0');
		if (t->pcur > 65535)
			t->pcur = 65535;
		t->pdig |= 1;
		return;
	}
	tm_ppush(t);
	if (c == ':')
		t->pdig = 2;
}

/* The last parameter has no separator after it; close it at dispatch, if
   the sequence had any at all. */
void tm_pend(tm_t *t)
{
	if (t->pn || t->pdig)
		tm_ppush(t);
}

/* Where the nth top-level parameter sits among them all, sub-parameters
   included, or -1. */
int tm_ptop(tm_t *t, int n)
{
	int i, k = -1;

	for (i = 0; i < t->pn; i++) {
		if (!t->ps[i])
			k++;
		if (k == n)
			return i;
	}
	return -1;
}

/* How many top-level parameters there are. */
int tm_npar(tm_t *t)
{
	int i, k = 0;

	for (i = 0; i < t->pn; i++)
		if (!t->ps[i])
			k++;
	return k;
}

/* Read parameter n, or the default when it is absent.

   "absent" and "zero" are the same thing in most sequences and different in
   a few, so the caller passes the default it wants and an explicit 0 gets
   it too: CSI 0 A moves one line, like CSI A. */
long tm_par(tm_t *t, int n, long def)
{
	int i = tm_ptop(t, n);

	if (i < 0 || t->pv[i] <= 0)
		return def;
	return t->pv[i];
}

/* Send a reply back to the program, for the sequences that ask a question. */
void tm_reply(tm_t *t, const char *s)
{
	if (tm_pty && t->pty)
		tm_pty->write(t->pty, s, strlen(s));
}

/* A reply of the shape lead a;b tail, or lead a tail when b is negative. */
void tm_reply2(tm_t *t, const char *lead, long a, long b, const char *tail)
{
	str r;

	s_init(&r);
	s_cat(&r, lead);
	s_num(&r, a);
	if (b >= 0) {
		s_ch(&r, ';');
		s_num(&r, b);
	}
	s_cat(&r, tail);
	tm_reply(t, r.p);
	s_free(&r);
}

/* Put the cursor somewhere, clamped to the screen. Every move ends a
   pending wrap. */
void tm_goto(tm_t *t, int r, int c)
{
	if (r < 0)
		r = 0;
	if (c < 0)
		c = 0;
	if (r >= t->rows)
		r = t->rows - 1;
	if (c >= t->cols)
		c = t->cols - 1;
	t->cr = r;
	t->cc = c;
	t->wrapnext = 0;
}

/* A move to an absolute row, which counts from the top margin, and stays
   inside the region, while origin mode is on. */
void tm_cup(tm_t *t, int r, int c)
{
	if (t->om) {
		r += t->top;
		if (r > t->bot)
			r = t->bot;
		if (r < t->top)
			r = t->top;
	}
	tm_goto(t, r, c);
}

/* Up or down by n: a cursor inside the scroll region stops at its margin,
   one outside it stops at the edge of the screen. */
void tm_vmove(tm_t *t, int n)
{
	int r = t->cr + n, lim;

	if (n < 0) {
		lim = t->cr >= t->top ? t->top : 0;
		if (r < lim)
			r = lim;
	} else {
		lim = t->cr <= t->bot ? t->bot : t->rows - 1;
		if (r > lim)
			r = lim;
	}
	tm_goto(t, r, t->cc);
}

/* The next tab stop to the right, or the last column. */
void tm_tab(tm_t *t, int n)
{
	int c = t->cc;

	while (n-- > 0) {
		c++;
		while (c < t->cols - 1 && !t->tabs[c])
			c++;
		if (c >= t->cols - 1) {
			c = t->cols - 1;
			break;
		}
	}
	t->cc = c;
	t->wrapnext = 0;
}

/* The previous tab stop to the left, or the first column. */
void tm_btab(tm_t *t, int n)
{
	int c = t->cc;

	while (n-- > 0 && c > 0) {
		c--;
		while (c > 0 && !t->tabs[c])
			c--;
	}
	t->cc = c;
	t->wrapnext = 0;
}

/* DECSC: the cursor and everything DEC STD 070 keeps with it -- the pen,
   the character sets, origin mode and autowrap mode (its "wrap flag" is
   the mode, not a pending wrap, which a restore always clears) -- one slot
   for each screen, so an editor's own save on the alternate screen never
   overwrites the one the shell is waiting on. */
void tm_decsc(tm_t *t)
{
	tm_save *s = &t->sv[t->inalt];

	s->set = 1;
	s->r = t->cr;
	s->c = t->cc;
	s->wrap = t->autowrap;
	s->om = t->om;
	s->fg = t->fg;
	s->bg = t->bg;
	s->attr = t->attr;
	s->g[0] = t->gset[0];
	s->g[1] = t->gset[1];
	s->gl = t->gl;
}

/* DECRC: what DECSC kept, or home and a plain pen when nothing was. */
void tm_decrc(tm_t *t)
{
	tm_save *s = &t->sv[t->inalt];

	if (!s->set) {
		t->fg = t->dfg;
		t->bg = t->dbg;
		t->attr = 0;
		t->om = 0;
		t->gset[0] = t->gset[1] = 'B';
		t->gl = 0;
		tm_goto(t, 0, 0);
		return;
	}
	t->om = s->om;
	t->fg = s->fg;
	t->bg = s->bg;
	t->attr = s->attr;
	t->gset[0] = s->g[0];
	t->gset[1] = s->g[1];
	t->gl = s->gl;
	t->autowrap = s->wrap;
	tm_goto(t, s->r, s->c);
}

/* One colour out of an SGR 38/48/58 run, in either spelling: the older
   ';'-separated one (38;5;n and 38;2;r;g;b), where the colour's own values
   are the parameters that follow and i moves past them, or ITU T.416's
   ':'-separated one (38:5:n, 38:2::r:g:b or 38:2:r:g:b), where they are
   sub-parameters of this one and i stays put. */
unsigned tm_colour(tm_t *t, int *i)
{
	int j = *i, k = 0, sub;
	long kind, v[4];

	while (j + 1 + k < t->pn && t->ps[j + 1 + k])
		k++;
	sub = k > 0;
	if (sub) {
		kind = t->pv[j + 1];
		if (kind == 5 && k >= 2)
			return DP_PAL | (unsigned)(t->pv[j + 2] & 0xFF);
		if (kind == 2 && k >= 4) {
			v[0] = t->pv[j + k - 2];
			v[1] = t->pv[j + k - 1];
			v[2] = t->pv[j + k];
			goto rgb;
		}
		return DP_DEFAULT;
	}
	kind = j + 1 < t->pn ? t->pv[j + 1] : -1;
	if (kind == 5 && j + 2 < t->pn) {
		*i = j + 2;
		return DP_PAL | (unsigned)(t->pv[j + 2] & 0xFF);
	}
	if (kind == 2 && j + 4 < t->pn) {
		v[0] = t->pv[j + 2];
		v[1] = t->pv[j + 3];
		v[2] = t->pv[j + 4];
		*i = j + 4;
		goto rgb;
	}
	*i = j + 1 < t->pn ? j + 1 : j;
	return DP_DEFAULT;
rgb:
	for (k = 0; k < 3; k++) {
		if (v[k] < 0)
			v[k] = 0;
		if (v[k] > 255)
			v[k] = 255;
	}
	return DP_RGB | ((unsigned)v[0] << 16) | ((unsigned)v[1] << 8) |
	       (unsigned)v[2];
}

/* Select graphic rendition: the pen. Sub-parameters belong to the code
   before them, so the loop steps over them; underline is the one plain code
   that reads its own (4:0 off, 4:1 and upward a style, all shown as one). */
void tm_sgr(tm_t *t)
{
	int i;
	long v, sub;

	if (!t->pn) {
		t->fg = t->dfg;
		t->bg = t->dbg;
		t->attr = 0;
		return;
	}
	for (i = 0; i < t->pn; i++) {
		if (t->ps[i])
			continue;
		v = t->pv[i] < 0 ? 0 : t->pv[i];
		switch (v) {
		case 0:
			t->fg = t->dfg;
			t->bg = t->dbg;
			t->attr = 0;
			break;
		case 1: t->attr |= DP_BOLD; break;
		case 2: t->attr |= DP_DIM; break;
		case 3: t->attr |= DP_ITAL; break;
		case 4:
			sub = i + 1 < t->pn && t->ps[i + 1] ? t->pv[i + 1] : -1;
			if (sub == 0)
				t->attr &= ~DP_UNDER;
			else
				t->attr |= DP_UNDER;
			break;
		case 5:
		case 6: t->attr |= DP_BLINK; break;
		case 7: t->attr |= DP_REV; break;
		case 8: t->attr |= TM_HIDE; break;
		case 9: t->attr |= DP_STRIKE; break;
		case 21: t->attr |= DP_UNDER; break;
		case 22: t->attr &= ~(DP_BOLD | DP_DIM); break;
		case 23: t->attr &= ~DP_ITAL; break;
		case 24: t->attr &= ~DP_UNDER; break;
		case 25: t->attr &= ~DP_BLINK; break;
		case 27: t->attr &= ~DP_REV; break;
		case 28: t->attr &= ~TM_HIDE; break;
		case 29: t->attr &= ~DP_STRIKE; break;
		case 38: t->fg = tm_colour(t, &i); break;
		case 39: t->fg = t->dfg; break;
		case 48: t->bg = tm_colour(t, &i); break;
		case 49: t->bg = t->dbg; break;
		case 58: (void)tm_colour(t, &i); break;
		default:
			if (v >= 30 && v <= 37)
				t->fg = DP_PAL | (unsigned)(v - 30);
			else if (v >= 40 && v <= 47)
				t->bg = DP_PAL | (unsigned)(v - 40);
			else if (v >= 90 && v <= 97)
				t->fg = DP_PAL | (unsigned)(v - 90 + 8);
			else if (v >= 100 && v <= 107)
				t->bg = DP_PAL | (unsigned)(v - 100 + 8);
			break;
		}
	}
}

/* Swap to the alternate screen, or back, and nothing else: each of the
   private modes that asks for it wants a different mix of clearing and
   saving on top. */
void tm_altswap(tm_t *t, int on)
{
	tm_cell *tmp;
	size_t i;

	if (!!on == t->inalt)
		return;
	if (!t->alt) {
		t->alt = xm((size_t)t->rows * t->cols * sizeof *t->alt);
		for (i = 0; i < (size_t)t->rows * t->cols; i++)
			tm_blank(t, t->alt + i);
	}
	tmp = t->g;
	t->g = t->alt;
	t->alt = tmp;
	t->inalt = !!on;
	t->sel = 0;
	lg(HIBR_LDBG, "terminal %d %s the alternate screen", t->id,
	   on ? "entered" : "left");
}

/* Synchronized output (mode 2026): a program brackets a frame with it so
   nothing half-drawn is ever shown. The screen as it was when the frame
   began is kept, and tm_draw shows that until the frame ends -- or for a
   second at most, so a program that dies mid-frame cannot freeze the
   window. */
void tm_setsync(tm_t *t, int on)
{
	if (on) {
		if (!t->sync) {
			free(t->frz);
			t->frz = xm((size_t)t->rows * t->cols * sizeof *t->frz);
			memcpy(t->frz, t->g,
			       (size_t)t->rows * t->cols * sizeof *t->frz);
			t->fcr = t->cr;
			t->fcc = t->cc;
		}
		t->sync = 1;
		t->synct = tm_ms();
		return;
	}
	t->sync = 0;
	free(t->frz);
	t->frz = 0;
}

/* Whether drawing should show the frozen frame rather than the live one. */
int tm_syncing(tm_t *t)
{
	return t->sync && t->frz && tm_ms() - t->synct < 1000;
}

/* A private mode's current value, for DECRQM: 1 set, 2 reset, 0 for one
   this terminal does not know. */
int tm_decget(tm_t *t, long v)
{
	int on;

	switch (v) {
	case 1: on = t->ckm; break;
	case 6: on = t->om; break;
	case 7: on = t->autowrap; break;
	case 25: on = t->vis; break;
	case 47:
	case 1047:
	case 1049: on = t->inalt; break;
	case 66: on = t->kpam; break;
	case 9:
	case 1000:
	case 1002:
	case 1003: on = t->mmode == (int)v; break;
	case 1004: on = t->focus; break;
	case 1006: on = t->msgr; break;
	case 2004: on = t->bpaste; break;
	case 2026: on = t->sync; break;
	default: return 0;
	}
	return on ? 1 : 2;
}

/* Set or reset the ?-prefixed modes a program turns on. */
void tm_decmode(tm_t *t, int on)
{
	int n = tm_npar(t), i;
	long v;

	for (i = 0; i < n; i++) {
		v = tm_par(t, i, 0);
		switch (v) {
		case 1: t->ckm = on; break;
		case 6:
			t->om = on;
			tm_cup(t, 0, 0);
			break;
		case 7:
			t->autowrap = on;
			if (!on)
				t->wrapnext = 0;
			break;
		case 25: t->vis = on; break;
		case 47: tm_altswap(t, on); break;
		case 1047:
			if (!on && t->inalt)
				tm_erase(t, 0, 0, t->rows - 1, t->cols - 1);
			tm_altswap(t, on);
			break;
		case 1048:
			if (on)
				tm_decsc(t);
			else
				tm_decrc(t);
			break;
		case 1049:
			if (on && !t->inalt) {
				tm_decsc(t);
				tm_altswap(t, 1);
				tm_erase(t, 0, 0, t->rows - 1, t->cols - 1);
			} else if (!on && t->inalt) {
				tm_altswap(t, 0);
				tm_decrc(t);
			}
			break;
		case 66: t->kpam = on; break;
		case 9:
		case 1000:
		case 1002:
		case 1003:
			if (on)
				t->mmode = (int)v;
			else if (t->mmode == (int)v)
				t->mmode = 0;
			lg(HIBR_LDBG, "terminal %d mouse %s", t->id,
			   tm_mname(t));
			break;
		case 1004: t->focus = on; break;
		case 1006: t->msgr = on; break;
		case 2004: t->bpaste = on; break;
		case 2026: tm_setsync(t, on); break;
		default:
			lg(HIBR_LTRC, "terminal %d ignored private mode %ld",
			   t->id, v);
			break;
		}
	}
}

/* Set or reset the plain ANSI modes: insert, and newline. */
void tm_ansimode(tm_t *t, int on)
{
	int n = tm_npar(t), i;
	long v;

	for (i = 0; i < n; i++) {
		v = tm_par(t, i, 0);
		if (v == 4)
			t->irm = on;
		else if (v == 20)
			t->lnm = on;
	}
}

/* A soft reset (DECSTR) puts the modes and the pen back and leaves the
   screen alone; a hard one (RIS) clears it and forgets everything else as
   well. */
void tm_reset(tm_t *t, int hard)
{
	t->fg = t->dfg;
	t->bg = t->dbg;
	t->attr = 0;
	t->top = 0;
	t->bot = t->rows - 1;
	t->autowrap = 1;
	t->vis = 1;
	t->ckm = t->kpam = t->om = t->irm = 0;
	t->gset[0] = t->gset[1] = 'B';
	t->gl = 0;
	memset(t->sv, 0, sizeof t->sv);
	t->wrapnext = 0;
	if (!hard)
		return;
	t->lnm = t->focus = 0;
	t->mmode = t->msgr = t->bpaste = 0;
	tm_setsync(t, 0);
	tm_tabreset(t);
	tm_altswap(t, 0);
	tm_erase(t, 0, 0, t->rows - 1, t->cols - 1);
	tm_goto(t, 0, 0);
	t->last = 0;
}

/* A request for this terminal's state (DECRQSS, DCS $ q): the scroll
   region, the cursor shape and the pen are what programs ask about; any
   other gets the "not understood" answer, which is still an answer. */
void tm_decrqss(tm_t *t)
{
	const char *q = t->os.p ? t->os.p : "";
	unsigned v;
	str r;

	s_init(&r);
	if (!strcmp(q, "r")) {
		s_cat(&r, "\033P1$r");
		s_num(&r, (long)t->top + 1);
		s_ch(&r, ';');
		s_num(&r, (long)t->bot + 1);
		s_cat(&r, "r\033\\");
	} else if (!strcmp(q, " q")) {
		s_cat(&r, "\033P1$r");
		s_num(&r, t->cshape == TM_BAR ? 6 : t->cshape == TM_UNDER ? 4 : 2);
		s_cat(&r, " q\033\\");
	} else if (!strcmp(q, "m")) {
		s_cat(&r, "\033P1$r0");
		if (t->attr & DP_BOLD) s_cat(&r, ";1");
		if (t->attr & DP_DIM) s_cat(&r, ";2");
		if (t->attr & DP_ITAL) s_cat(&r, ";3");
		if (t->attr & DP_UNDER) s_cat(&r, ";4");
		if (t->attr & DP_BLINK) s_cat(&r, ";5");
		if (t->attr & DP_REV) s_cat(&r, ";7");
		if (t->attr & TM_HIDE) s_cat(&r, ";8");
		if (t->attr & DP_STRIKE) s_cat(&r, ";9");
		for (v = 0; v < 2; v++) {
			unsigned c = v ? t->bg : t->fg;
			if (c & DP_RGB) {
				s_cat(&r, v ? ";48;2;" : ";38;2;");
				s_num(&r, (long)((c >> 16) & 0xFF));
				s_ch(&r, ';');
				s_num(&r, (long)((c >> 8) & 0xFF));
				s_ch(&r, ';');
				s_num(&r, (long)(c & 0xFF));
			} else if (c & DP_PAL) {
				s_cat(&r, v ? ";48;5;" : ";38;5;");
				s_num(&r, (long)(c & 0xFF));
			}
		}
		s_cat(&r, "m\033\\");
	} else {
		s_cat(&r, "\033P0$r\033\\");
	}
	tm_reply(t, r.p);
	s_free(&r);
}

/* A DCS string is complete. Only the queries are answered; everything
   else -- sixel, tmux passthrough, the rest -- is consumed and dropped,
   which is the point: none of it is ever printed. */
void tm_dcsend(tm_t *t)
{
	const char *in = t->in.p ? t->in.p : "";
	str r;

	if (t->ofin == 'q' && !strcmp(in, "$")) {
		tm_decrqss(t);
	} else if (t->ofin == 'q' && !strcmp(in, "+")) {
		s_init(&r);
		s_cat(&r, "\033P0+r");
		s_cat(&r, t->os.p ? t->os.p : "");
		s_cat(&r, "\033\\");
		tm_reply(t, r.p);
		s_free(&r);
	}
}

/* An operating system command is complete: the title, a clipboard set
   (52, kept whole as "selection;base64" for whoever runs this terminal to
   take -- a request to read the clipboard is never answered), and the colour
   queries a program uses to tell a dark background from a light one --
   answered only when whoever runs this terminal has said what its colours
   are, since a guessed answer is worse than none. */
void tm_oscend(tm_t *t)
{
	const char *p = t->os.p ? t->os.p : "";
	long which = strtol(p, 0, 10);
	unsigned c;
	str r;

	while (*p && *p != ';')
		p++;
	if (*p == ';')
		p++;
	if (which == 0 || which == 1 || which == 2) {
		t->title.n = 0;
		if (t->title.p)
			t->title.p[0] = 0;
		s_cat(&t->title, p);
		lg(HIBR_LDBG, "terminal %d is called '%s'", t->id, p);
		return;
	}
	if (which == 52) {
		if (strchr(p, ';') && strcmp(strchr(p, ';') + 1, "?")) {
			t->clip.n = 0;
			if (t->clip.p)
				t->clip.p[0] = 0;
			s_cat(&t->clip, p);
			lg(HIBR_LDBG, "terminal %d set the clipboard", t->id);
		} else {
			lg(HIBR_LDBG, "terminal %d asked to read the clipboard; "
				      "not answered", t->id);
		}
		return;
	}
	if ((which == 10 || which == 11) && !strcmp(p, "?") && t->hasrgb) {
		c = which == 10 ? t->rgbfg : t->rgbbg;
		s_init(&r);
		s_cat(&r, "\033]");
		s_num(&r, which);
		s_cat(&r, ";rgb:");
		for (which = 16; which >= 0; which -= 8) {
			static const char hx[] = "0123456789abcdef";
			unsigned b = (c >> which) & 0xFF;
			s_ch(&r, hx[b >> 4]);
			s_ch(&r, hx[b & 15]);
			s_ch(&r, hx[b >> 4]);
			s_ch(&r, hx[b & 15]);
			if (which)
				s_ch(&r, '/');
		}
		s_cat(&r, t->obel ? "\a" : "\033\\");
		tm_reply(t, r.p);
		s_free(&r);
		return;
	}
	lg(HIBR_LTRC, "terminal %d ignored OSC %ld", t->id, which);
}

/* One complete sequence with an intermediate byte and no prefix. */
void tm_csiin(tm_t *t, int f, const char *in)
{
	long a = tm_par(t, 0, 0);

	if (!strcmp(in, " ") && f == 'q') {
		if (a <= 2)
			t->cshape = TM_BLOCK;
		else if (a <= 4)
			t->cshape = TM_UNDER;
		else
			t->cshape = TM_BAR;
	} else if (!strcmp(in, "!") && f == 'p') {
		tm_reset(t, 0);
	} else if (!strcmp(in, "$") && f == 'p') {
		tm_reply2(t, "\033[", a,
			  a == 4 ? (t->irm ? 1 : 2) : a == 20 ? (t->lnm ? 1 : 2) : 0,
			  "$y");
	} else {
		lg(HIBR_LTRC, "terminal %d ignored CSI %s%c", t->id, in, f);
	}
}

/* One complete CSI sequence with the private '?' prefix. */
void tm_csiq(tm_t *t, int f, const char *in)
{
	long a = tm_par(t, 0, 0);

	if (!*in && f == 'h') {
		tm_decmode(t, 1);
	} else if (!*in && f == 'l') {
		tm_decmode(t, 0);
	} else if (!strcmp(in, "$") && f == 'p') {
		tm_reply2(t, "\033[?", a, tm_decget(t, a), "$y");
	} else if (!*in && f == 'n' && a == 6) {
		tm_reply2(t, "\033[?", (long)(t->cr - (t->om ? t->top : 0)) + 1,
			  (long)t->cc + 1, "R");
	} else if (!*in && f == 'J') {
		t->pfx = 0;
		tm_csi(t, 'J');
	} else if (!*in && f == 'K') {
		t->pfx = 0;
		tm_csi(t, 'K');
	} else {
		lg(HIBR_LTRC, "terminal %d ignored CSI ?%s%c", t->id, in, f);
	}
}

/* One complete CSI sequence with the '>' prefix: the secondary device
   attributes and the version are answered; the keyboard and modifier
   settings, which this terminal does not have, are consumed -- and never
   read as SGR or a cursor restore, which is what they looked like before
   the prefix was looked at. */
void tm_csigt(tm_t *t, int f, const char *in)
{
	if (!*in && f == 'c' && tm_par(t, 0, 0) == 0)
		tm_reply(t, "\033[>1;10;0c");
	else if (!*in && f == 'q')
		tm_reply(t, "\033P>|hibr-term 0.24\033\\");
	else
		lg(HIBR_LTRC, "terminal %d ignored CSI >%s%c", t->id, in, f);
}

/* Act on one complete CSI sequence. */
void tm_csi(tm_t *t, int f)
{
	const char *in = t->in.p ? t->in.p : "";
	long a = tm_par(t, 0, 1), b = tm_par(t, 1, 1);
	int n, i;

	if (t->pfx == '?') {
		tm_csiq(t, f, in);
		return;
	}
	if (t->pfx == '>') {
		tm_csigt(t, f, in);
		return;
	}
	if (t->pfx) {
		lg(HIBR_LTRC, "terminal %d ignored CSI %c%s%c", t->id, t->pfx,
		   in, f);
		return;
	}
	if (*in) {
		tm_csiin(t, f, in);
		return;
	}
	switch (f) {
	case '@': tm_ichars(t, (int)a); break;
	case 'A': tm_vmove(t, -(int)a); break;
	case 'B':
	case 'e': tm_vmove(t, (int)a); break;
	case 'C':
	case 'a': tm_goto(t, t->cr, t->cc + (int)a); break;
	case 'D': tm_goto(t, t->cr, t->cc - (int)a); break;
	case 'E': tm_vmove(t, (int)a); t->cc = 0; break;
	case 'F': tm_vmove(t, -(int)a); t->cc = 0; break;
	case 'G':
	case '`': tm_goto(t, t->cr, (int)a - 1); break;
	case 'd': tm_cup(t, (int)a - 1, t->cc); break;
	case 'H':
	case 'f': tm_cup(t, (int)a - 1, (int)b - 1); break;
	case 'I': tm_tab(t, (int)a); break;
	case 'Z': tm_btab(t, (int)a); break;
	case 'J':
		n = (int)tm_par(t, 0, 0);
		if (n == 0)
			tm_erase(t, t->cr, t->cc, t->rows - 1, t->cols - 1);
		else if (n == 1)
			tm_erase(t, 0, 0, t->cr, t->cc);
		else if (n == 2)
			tm_erase(t, 0, 0, t->rows - 1, t->cols - 1);
		else if (n == 3)
			tm_sbclear(t);
		t->wrapnext = 0;
		break;
	case 'K':
		n = (int)tm_par(t, 0, 0);
		if (n == 0)
			tm_erase(t, t->cr, t->cc, t->cr, t->cols - 1);
		else if (n == 1)
			tm_erase(t, t->cr, 0, t->cr, t->cc);
		else if (n == 2)
			tm_erase(t, t->cr, 0, t->cr, t->cols - 1);
		t->wrapnext = 0;
		break;
	case 'L': tm_ilines(t, (int)a); break;
	case 'M': tm_dlines(t, (int)a); break;
	case 'P': tm_dchars(t, (int)a); break;
	case 'S': tm_scroll(t, (int)a); break;
	case 'T':
		if (tm_npar(t) <= 1)
			tm_scroll(t, -(int)a);
		break;
	case 'X':
		tm_erase(t, t->cr, t->cc, t->cr,
			 t->cc + (int)a - 1 < t->cols - 1 ?
			 t->cc + (int)a - 1 : t->cols - 1);
		t->wrapnext = 0;
		break;
	case 'b':
		if (a > (long)t->rows * t->cols)
			a = (long)t->rows * t->cols;
		for (i = 0; t->last && i < (int)a; i++)
			tm_glyph(t, t->last);
		break;
	case 'c':
		if (tm_par(t, 0, 0) == 0)
			tm_reply(t, "\033[?62;22c");
		break;
	case 'g':
		n = (int)tm_par(t, 0, 0);
		if (n == 0 && t->cc < t->cols)
			t->tabs[t->cc] = 0;
		else if (n == 3)
			memset(t->tabs, 0, (size_t)t->cols);
		break;
	case 'h': tm_ansimode(t, 1); break;
	case 'l': tm_ansimode(t, 0); break;
	case 'm': tm_sgr(t); break;
	case 'n':
		n = (int)tm_par(t, 0, 0);
		if (n == 5)
			tm_reply(t, "\033[0n");
		else if (n == 6)
			tm_reply2(t, "\033[",
				  (long)(t->cr - (t->om ? t->top : 0)) + 1,
				  (long)t->cc + 1, "R");
		break;
	case 'r':
		n = (int)tm_par(t, 1, (long)t->rows) - 1;
		if (n >= t->rows)
			n = t->rows - 1;
		if ((int)a - 1 < n) {
			t->top = (int)a - 1;
			t->bot = n;
		}
		tm_cup(t, 0, 0);
		break;
	case 's':
		if (!t->pn)
			tm_decsc(t);
		break;
	case 't':
		if (tm_par(t, 0, 0) == 18)
			tm_reply2(t, "\033[8;", (long)t->rows, (long)t->cols, "t");
		break;
	case 'u':
		if (!t->pn)
			tm_decrc(t);
		break;
	default:
		lg(HIBR_LTRC, "terminal %d ignored CSI %c", t->id, f);
		break;
	}
}

/* Act on one ESC sequence: the ones with no intermediate, the character
   set designations, and the screen alignment test. */
void tm_escd(tm_t *t, int f)
{
	const char *in = t->in.p ? t->in.p : "";
	int r, c;

	if (*in) {
		if ((in[0] == '(' || in[0] == ')') && !in[1])
			t->gset[in[0] == ')'] = f == '0' ? '0' : 'B';
		else if (!strcmp(in, "#") && f == '8') {
			for (r = 0; r < t->rows; r++)
				for (c = 0; c < t->cols; c++) {
					tm_blank(t, tm_at(t, r, c));
					tm_at(t, r, c)->cp = 'E';
				}
			t->top = 0;
			t->bot = t->rows - 1;
			tm_goto(t, 0, 0);
		}
		return;
	}
	switch (f) {
	case '7': tm_decsc(t); break;
	case '8': tm_decrc(t); break;
	case 'D': tm_lf(t); t->wrapnext = 0; break;
	case 'E': tm_lf(t); t->cc = 0; t->wrapnext = 0; break;
	case 'H':
		if (t->cc < t->cols)
			t->tabs[t->cc] = 1;
		break;
	case 'M':
		if (t->cr == t->top)
			tm_scroll(t, -1);
		else if (t->cr > 0)
			t->cr--;
		t->wrapnext = 0;
		break;
	case 'c': tm_reset(t, 1); break;
	case '=': t->kpam = 1; break;
	case '>': t->kpam = 0; break;
	case 'Z': tm_reply(t, "\033[?62;22c"); break;
	default: break;
	}
}

/* One C0 control, wherever it arrives -- inside a CSI sequence it still
   acts, which is what a VT does and what programs occasionally rely on. */
void tm_ctl(tm_t *t, unsigned c)
{
	switch (c) {
	case '\r': t->cc = 0; t->wrapnext = 0; break;
	case '\n':
	case 0x0B:
	case 0x0C:
		tm_lf(t);
		if (t->lnm)
			t->cc = 0;
		t->wrapnext = 0;
		break;
	case '\b':
		if (t->wrapnext)
			t->wrapnext = 0;
		else if (t->cc > 0)
			t->cc--;
		break;
	case '\t': tm_tab(t, 1); break;
	case 0x0E: t->gl = 1; break;
	case 0x0F: t->gl = 0; break;
	default: break;
	}
}

/* DEC Special Graphics, 0x5F to 0x7E: the line drawing set ncurses and
   screen select with ESC ( 0 and SO. */
static const unsigned short tm_gfx[32] = {
	0x00A0, 0x25C6, 0x2592, 0x2409, 0x240C, 0x240D, 0x240A, 0x00B0,
	0x00B1, 0x2424, 0x240B, 0x2518, 0x2510, 0x250C, 0x2514, 0x253C,
	0x23BA, 0x23BB, 0x2500, 0x23BC, 0x23BD, 0x251C, 0x2524, 0x2534,
	0x252C, 0x2502, 0x2264, 0x2265, 0x03C0, 0x2260, 0x00A3, 0x00B7
};

/* A printable character, through whichever character set is in use. */
void tm_print(tm_t *t, unsigned c)
{
	if (t->gset[t->gl] == '0' && c >= 0x5F && c <= 0x7E)
		c = tm_gfx[c - 0x5F];
	tm_glyph(t, c);
}

/* A string byte, kept only up to a bound. */
void tm_sput(tm_t *t, unsigned c)
{
	if (t->os.n < TM_SMAX)
		tm_utf8(&t->os, c);
}

/* One character through the parser: Paul Williams' DEC state machine,
   which is what every serious emulator implements. CAN and SUB abandon any
   sequence; ESC begins a new one from anywhere, finishing an OSC or DCS
   string on the way; everything else depends on the state. */
void tm_step(tm_t *t, unsigned c)
{
	if (c == 0x18 || c == 0x1A) {
		t->st = T_GND;
		return;
	}
	if (c == 0x1B) {
		if (t->st == T_OSC)
			tm_oscend(t);
		else if (t->st == T_DCSS)
			tm_dcsend(t);
		tm_pclear(t);
		t->st = T_ESC;
		return;
	}
	switch (t->st) {
	case T_GND:
		if (c < 0x20)
			tm_ctl(t, c);
		else if (c != 0x7F && (c < 0x80 || c >= 0xA0))
			tm_print(t, c);
		return;
	case T_ESC:
	case T_ESCI:
		if (c < 0x20) {
			tm_ctl(t, c);
			return;
		}
		if (c == 0x7F)
			return;
		if (c <= 0x2F) {
			s_ch(&t->in, (int)c);
			t->st = T_ESCI;
			return;
		}
		if (t->st == T_ESC) {
			switch (c) {
			case '[':
				t->st = T_CSIE;
				return;
			case ']':
				t->os.n = 0;
				if (t->os.p)
					t->os.p[0] = 0;
				t->obel = 0;
				t->st = T_OSC;
				return;
			case 'P':
				t->os.n = 0;
				if (t->os.p)
					t->os.p[0] = 0;
				t->st = T_DCSE;
				return;
			case 'X':
			case '^':
			case '_':
				t->st = T_SOS;
				return;
			}
		}
		tm_escd(t, (int)c);
		t->st = T_GND;
		return;
	case T_CSIE:
	case T_CSIP:
		if (c < 0x20) {
			tm_ctl(t, c);
			return;
		}
		if (c == 0x7F)
			return;
		if ((c >= '0' && c <= '9') || c == ';' || c == ':') {
			tm_pbyte(t, c);
			t->st = T_CSIP;
			return;
		}
		if (c >= 0x3C && c <= 0x3F) {
			if (t->st == T_CSIE) {
				t->pfx = (int)c;
				t->st = T_CSIP;
			} else {
				t->st = T_CSIX;
			}
			return;
		}
		/* fall through */
	case T_CSII:
		if (c < 0x20) {
			tm_ctl(t, c);
			return;
		}
		if (c == 0x7F)
			return;
		if (c >= 0x20 && c <= 0x2F) {
			if (t->in.n < 4)
				s_ch(&t->in, (int)c);
			t->st = T_CSII;
			return;
		}
		if (c >= 0x40 && c <= 0x7E) {
			tm_pend(t);
			t->st = T_GND;
			tm_csi(t, (int)c);
			return;
		}
		t->st = T_CSIX;
		return;
	case T_CSIX:
		if (c < 0x20)
			tm_ctl(t, c);
		else if (c >= 0x40 && c <= 0x7E)
			t->st = T_GND;
		return;
	case T_DCSE:
	case T_DCSP:
		if (c < 0x20 || c == 0x7F)
			return;
		if ((c >= '0' && c <= '9') || c == ';' || c == ':') {
			tm_pbyte(t, c);
			t->st = T_DCSP;
			return;
		}
		if (c >= 0x3C && c <= 0x3F) {
			if (t->st == T_DCSE) {
				t->pfx = (int)c;
				t->st = T_DCSP;
			} else {
				t->st = T_DCSX;
			}
			return;
		}
		/* fall through */
	case T_DCSI:
		if (c < 0x20 || c == 0x7F)
			return;
		if (c >= 0x20 && c <= 0x2F) {
			if (t->in.n < 4)
				s_ch(&t->in, (int)c);
			t->st = T_DCSI;
			return;
		}
		if (c >= 0x40 && c <= 0x7E) {
			tm_pend(t);
			t->ofin = (int)c;
			t->st = T_DCSS;
			return;
		}
		t->st = T_DCSX;
		return;
	case T_DCSS:
		if (c != 0x7F)
			tm_sput(t, c);
		return;
	case T_OSC:
		if (c == 0x07) {
			t->obel = 1;
			tm_oscend(t);
			t->st = T_GND;
			return;
		}
		if (c >= 0x20)
			tm_sput(t, c);
		return;
	case T_DCSX:
	case T_SOS:
	default:
		return;
	}
}

/* Feed bytes from the program through UTF-8 decoding and the parser.

   Bytes arrive in whatever sizes the pty hands over, so a sequence or a
   UTF-8 character can be split across two calls. The state and the partial
   character both live on the terminal, which is why this can be called with
   one byte at a time and get the same answer. A malformed byte becomes
   U+FFFD, and the byte that broke a sequence is read again on its own --
   an ESC arriving in the middle of a character still starts a sequence. */
void tm_feed(tm_t *t, const char *b, size_t n)
{
	size_t i;
	unsigned c, cp;

	for (i = 0; i < n; i++) {
		c = (unsigned char)b[i];
		if (t->uneed) {
			if ((c & 0xC0) == 0x80) {
				t->uacc = (t->uacc << 6) | (c & 0x3F);
				if (--t->uneed)
					continue;
				cp = t->uacc;
				if ((t->ulen == 3 && cp < 0x800) ||
				    (t->ulen == 4 && (cp < 0x10000 || cp > 0x10FFFF)) ||
				    (cp >= 0xD800 && cp <= 0xDFFF))
					cp = 0xFFFD;
				tm_step(t, cp);
				continue;
			}
			t->uneed = 0;
			tm_step(t, 0xFFFD);
		}
		if (c < 0x80) {
			tm_step(t, c);
		} else if (c >= 0xC2 && c <= 0xDF) {
			t->uacc = c & 0x1F;
			t->uneed = 1;
			t->ulen = 2;
		} else if ((c & 0xF0) == 0xE0) {
			t->uacc = c & 0x0F;
			t->uneed = 2;
			t->ulen = 3;
		} else if (c >= 0xF0 && c <= 0xF4) {
			t->uacc = c & 0x07;
			t->uneed = 3;
			t->ulen = 4;
		} else {
			tm_step(t, 0xFFFD);
		}
	}
}
