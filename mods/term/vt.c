#define _GNU_SOURCE

#include "tm.h"
#include <stdlib.h>
#include <string.h>

extern const py_api *tm_pty;

/* Read parameter n out of a CSI sequence, or the default when it is absent.

   "absent" and "zero" are the same thing in most sequences and different in
   a few, so the caller passes the default it wants and an explicit 0 gets
   it too: CSI 0 A moves one line, like CSI A. */
long tm_par(tm_t *t, int n, long def)
{
	const char *p = t->pb.p ? t->pb.p : "";
	int i = 0;
	long v;
	char *e;

	while (*p == '?' || *p == '>' || *p == '!')
		p++;
	for (;;) {
		if (i == n) {
			if (*p < '0' || *p > '9')
				return def;
			v = strtol(p, &e, 10);
			return v ? v : def;
		}
		while (*p && *p != ';')
			p++;
		if (!*p)
			return def;
		p++;
		i++;
	}
}

/* How many parameters a CSI sequence carries. */
int tm_npar(tm_t *t)
{
	const char *p = t->pb.p ? t->pb.p : "";
	int n = 1;

	if (!*p)
		return 0;
	while (*p)
		if (*p++ == ';')
			n++;
	return n;
}

/* True when the sequence began with the private marker. */
int tm_priv(tm_t *t)
{
	return t->pb.p && t->pb.p[0] == '?';
}

/* Send a reply back to the program, for the sequences that ask a question. */
void tm_reply(tm_t *t, const char *s)
{
	if (tm_pty && t->pty)
		tm_pty->write(t->pty, s, strlen(s));
}

/* Put the cursor somewhere, clamped to the screen. */
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

/* One colour out of an SGR 38/48 run, which is three or five parameters
   long depending on whether it names a palette entry or three channels. */
unsigned tm_colour(tm_t *t, int *i)
{
	long kind = tm_par(t, *i + 1, 0);

	if (kind == 5) {
		*i += 2;
		return DP_PAL | (unsigned)(tm_par(t, *i, 0) & 0xFF);
	}
	if (kind == 2) {
		unsigned r = (unsigned)(tm_par(t, *i + 2, 0) & 0xFF);
		unsigned g = (unsigned)(tm_par(t, *i + 3, 0) & 0xFF);
		unsigned b = (unsigned)(tm_par(t, *i + 4, 0) & 0xFF);
		*i += 4;
		return DP_RGB | (r << 16) | (g << 8) | b;
	}
	*i += 1;
	return DP_DEFAULT;
}

/* Select graphic rendition: the pen. */
void tm_sgr(tm_t *t)
{
	int n = tm_npar(t), i;
	long v;

	if (!n) {
		t->fg = t->dfg;
		t->bg = t->dbg;
		t->attr = 0;
		return;
	}
	for (i = 0; i < n; i++) {
		v = tm_par(t, i, 0);
		switch (v) {
		case 0:
			t->fg = t->dfg;
			t->bg = t->dbg;
			t->attr = 0;
			break;
		case 1: t->attr |= DP_BOLD; break;
		case 2: t->attr |= DP_DIM; break;
		case 3: t->attr |= DP_ITAL; break;
		case 4: t->attr |= DP_UNDER; break;
		case 5:
		case 6: t->attr |= DP_BLINK; break;
		case 7: t->attr |= DP_REV; break;
		case 9: t->attr |= DP_STRIKE; break;
		case 21:
		case 22: t->attr &= ~(DP_BOLD | DP_DIM); break;
		case 23: t->attr &= ~DP_ITAL; break;
		case 24: t->attr &= ~DP_UNDER; break;
		case 25: t->attr &= ~DP_BLINK; break;
		case 27: t->attr &= ~DP_REV; break;
		case 29: t->attr &= ~DP_STRIKE; break;
		case 38: t->fg = tm_colour(t, &i); break;
		case 39: t->fg = t->dfg; break;
		case 48: t->bg = tm_colour(t, &i); break;
		case 49: t->bg = t->dbg; break;
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

/* Swap to the alternate screen, or back.

   A pager or an editor uses this so that what was on the screen before it
   started comes back when it stops. Keeping a second grid is the whole of
   it; the saved cursor goes with it, because that is what the sequence that
   asks for the swap also asks for. */
void tm_altscreen(tm_t *t, int on)
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
	if (on) {
		t->sr = t->cr;
		t->sc = t->cc;
		tm_erase(t, 0, 0, t->rows - 1, t->cols - 1);
		tm_goto(t, 0, 0);
	} else {
		tm_goto(t, t->sr, t->sc);
	}
	lg(HIBR_LDBG, "terminal %d %s the alternate screen", t->id,
	   on ? "entered" : "left");
}

/* Set or reset a private mode: the ?-prefixed ones a program turns on. */
void tm_mode(tm_t *t, int on)
{
	int n = tm_npar(t), i;
	long v;

	for (i = 0; i < n; i++) {
		v = tm_par(t, i, 0);
		switch (v) {
		case 7: t->autowrap = on; break;
		case 25: t->vis = on; break;
		case 47:
		case 1047:
		case 1049: tm_altscreen(t, on); break;
		default: break;
		}
	}
}

/* Act on one complete CSI sequence. */
void tm_csi(tm_t *t, int f)
{
	long a = tm_par(t, 0, 1), b = tm_par(t, 1, 1);
	int n;
	str r;

	switch (f) {
	case 'A': tm_goto(t, t->cr - (int)a, t->cc); break;
	case 'B': tm_goto(t, t->cr + (int)a, t->cc); break;
	case 'C': tm_goto(t, t->cr, t->cc + (int)a); break;
	case 'D': tm_goto(t, t->cr, t->cc - (int)a); break;
	case 'E': tm_goto(t, t->cr + (int)a, 0); break;
	case 'F': tm_goto(t, t->cr - (int)a, 0); break;
	case 'G':
	case '`': tm_goto(t, t->cr, (int)a - 1); break;
	case 'd': tm_goto(t, (int)a - 1, t->cc); break;
	case 'H':
	case 'f': tm_goto(t, (int)a - 1, (int)b - 1); break;
	case 'J':
		n = (int)tm_par(t, 0, 0);
		if (n == 0)
			tm_erase(t, t->cr, t->cc, t->rows - 1, t->cols - 1);
		else if (n == 1)
			tm_erase(t, 0, 0, t->cr, t->cc);
		else
			tm_erase(t, 0, 0, t->rows - 1, t->cols - 1);
		break;
	case 'K':
		n = (int)tm_par(t, 0, 0);
		if (n == 0)
			tm_erase(t, t->cr, t->cc, t->cr, t->cols - 1);
		else if (n == 1)
			tm_erase(t, t->cr, 0, t->cr, t->cc);
		else
			tm_erase(t, t->cr, 0, t->cr, t->cols - 1);
		break;
	case 'L': tm_ilines(t, (int)a); break;
	case 'M': tm_dlines(t, (int)a); break;
	case 'P': tm_dchars(t, (int)a); break;
	case '@': tm_ichars(t, (int)a); break;
	case 'S': tm_scroll(t, (int)a); break;
	case 'T': tm_scroll(t, -(int)a); break;
	case 'X':
		tm_erase(t, t->cr, t->cc, t->cr,
			 t->cc + (int)a - 1 < t->cols - 1 ?
			 t->cc + (int)a - 1 : t->cols - 1);
		break;
	case 'h': if (tm_priv(t)) tm_mode(t, 1); break;
	case 'l': if (tm_priv(t)) tm_mode(t, 0); break;
	case 'm': tm_sgr(t); break;
	case 'r':
		t->top = (int)a - 1;
		t->bot = (int)tm_par(t, 1, (long)t->rows) - 1;
		if (t->top < 0)
			t->top = 0;
		if (t->bot >= t->rows)
			t->bot = t->rows - 1;
		if (t->top >= t->bot) {
			t->top = 0;
			t->bot = t->rows - 1;
		}
		tm_goto(t, t->top, 0);
		break;
	case 's': t->sr = t->cr; t->sc = t->cc; break;
	case 'u': tm_goto(t, t->sr, t->sc); break;
	case 'n':
		if (tm_par(t, 0, 0) == 6) {
			s_init(&r);
			s_cat(&r, "\033[");
			s_num(&r, (long)t->cr + 1);
			s_ch(&r, ';');
			s_num(&r, (long)t->cc + 1);
			s_ch(&r, 'R');
			tm_reply(t, r.p);
			s_free(&r);
		}
		break;
	case 'c': tm_reply(t, "\033[?1;2c"); break;
	default:
		lg(HIBR_LTRC, "terminal %d ignored CSI %s%c", t->id,
		   t->pb.p ? t->pb.p : "", f);
		break;
	}
}

/* Act on one ESC sequence that is not a CSI. */
void tm_esc(tm_t *t, int c)
{
	switch (c) {
	case '7': t->sr = t->cr; t->sc = t->cc; break;
	case '8': tm_goto(t, t->sr, t->sc); break;
	case 'D':
		if (t->cr == t->bot)
			tm_scroll(t, 1);
		else
			tm_goto(t, t->cr + 1, t->cc);
		break;
	case 'E':
		if (t->cr == t->bot)
			tm_scroll(t, 1);
		else
			tm_goto(t, t->cr + 1, 0);
		t->cc = 0;
		break;
	case 'M':
		if (t->cr == t->top)
			tm_scroll(t, -1);
		else
			tm_goto(t, t->cr - 1, t->cc);
		break;
	case 'c':
		t->fg = t->dfg;
		t->bg = t->dbg;
		t->attr = 0;
		t->top = 0;
		t->bot = t->rows - 1;
		t->autowrap = 1;
		t->vis = 1;
		tm_altscreen(t, 0);
		tm_erase(t, 0, 0, t->rows - 1, t->cols - 1);
		tm_goto(t, 0, 0);
		break;
	default: break;
	}
}

/* One control character in the ground state. */
void tm_ctrl(tm_t *t, int c)
{
	switch (c) {
	case '\r': t->cc = 0; t->wrapnext = 0; break;
	case '\n':
	case 0x0B:
	case 0x0C:
		if (t->cr == t->bot)
			tm_scroll(t, 1);
		else if (t->cr < t->rows - 1)
			t->cr++;
		t->wrapnext = 0;
		break;
	case '\b':
		if (t->wrapnext)
			t->wrapnext = 0;
		else if (t->cc > 0)
			t->cc--;
		break;
	case '\t':
		t->wrapnext = 0;
		do
			t->cc++;
		while (t->cc < t->cols - 1 && t->cc % 8);
		if (t->cc >= t->cols)
			t->cc = t->cols - 1;
		break;
	default: break;
	}
}

/* Feed bytes from the program through the state machine.

   Bytes arrive in whatever sizes the pty hands over, so a sequence or a
   UTF-8 character can be split across two calls. The state and the partial
   character both live on the terminal, which is why this can be called with
   one byte at a time and get the same answer. */
void tm_feed(tm_t *t, const char *b, size_t n)
{
	size_t i = 0;
	unsigned char c;
	unsigned cp;
	int l;

	while (i < n) {
		c = (unsigned char)b[i];
		switch (t->st) {
		case T_ESC:
			i++;
			if (c == '[') {
				t->pb.n = 0;
				if (t->pb.p)
					t->pb.p[0] = 0;
				t->st = T_CSI;
			} else if (c == ']') {
				t->pb.n = 0;
				if (t->pb.p)
					t->pb.p[0] = 0;
				t->st = T_OSC;
			} else if (c == '(' || c == ')' || c == '*' ||
				   c == '+' || c == '#' || c == '%') {
				t->st = T_ESCQ;
			} else {
				tm_esc(t, c);
				t->st = T_GND;
			}
			continue;
		case T_ESCQ:
			i++;
			t->st = T_GND;
			continue;
		case T_CSI:
			i++;
			if (c >= 0x40 && c <= 0x7E) {
				tm_csi(t, c);
				t->st = T_GND;
			} else if (t->pb.n < 64) {
				s_ch(&t->pb, (int)c);
			}
			continue;
		case T_OSC:
			i++;
			if (c == 0x07) {
				t->st = T_GND;
				tm_osc(t);
			} else if (c == 0x1B) {
				t->st = T_GND;
				tm_osc(t);
				if (i < n && b[i] == '\\')
					i++;
			} else if (t->pb.n < 256) {
				s_ch(&t->pb, (int)c);
			}
			continue;
		}
		if (c == 0x1B) {
			t->st = T_ESC;
			t->ub.n = 0;
			i++;
			continue;
		}
		if (c < 0x20 || c == 0x7F) {
			tm_ctrl(t, c);
			i++;
			continue;
		}
		if (c < 0x80 && !t->ub.n) {
			tm_glyph(t, c);
			i++;
			continue;
		}
		/* u8dec cannot say "not yet": handed one byte of a three
		   byte character it returns that byte as if it were the
		   whole of it, so a decoder fed a stream has to count the
		   bytes itself before asking. */
		s_ch(&t->ub, (int)c);
		i++;
		l = u8len((unsigned char)t->ub.p[0]);
		if (l < 1 || l > 4)
			l = 1;
		if (t->ub.n >= (size_t)l) {
			if (u8dec(t->ub.p, t->ub.n, &cp) > 0)
				tm_glyph(t, cp);
			t->ub.n = 0;
			t->ub.p[0] = 0;
		}
	}
}

/* An operating system command: the only one that matters is the title. */
void tm_osc(tm_t *t)
{
	const char *p = t->pb.p ? t->pb.p : "";
	long which = strtol(p, 0, 10);

	if (which != 0 && which != 1 && which != 2)
		return;
	while (*p && *p != ';')
		p++;
	if (*p == ';')
		p++;
	t->title.n = 0;
	if (t->title.p)
		t->title.p[0] = 0;
	s_cat(&t->title, p);
	lg(HIBR_LDBG, "terminal %d is called '%s'", t->id, p);
}
