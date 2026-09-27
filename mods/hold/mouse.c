#define _GNU_SOURCE

#include "hd.h"
#include <stdlib.h>

/* A legacy (pre-SGR) X10 report: \e[M then three raw bytes, button+32,
   column+32, row+32. There is no release-with-a-button in this encoding --
   only ever "released", button 3, regardless of which one -- and a
   position past 223 cannot be represented at all, so it saturates there
   instead of wrapping into some other byte's meaning. This is the
   protocol's own limit, not a shortcut: a program that never asked for SGR
   could not have told the difference between a real column 223 and a
   wrapped one either. */
void hd_mlegacy(str *out, long b, long x, long y)
{
	if (b > 223)
		b = 223;
	if (x < 1)
		x = 1;
	if (x > 223)
		x = 223;
	if (y < 1)
		y = 1;
	if (y > 223)
		y = 223;
	s_cat(out, "\033[M");
	s_ch(out, (int)b + 32);
	s_ch(out, (int)x + 32);
	s_ch(out, (int)y + 32);
}

/* Rewrite one complete SGR mouse report -- \e[<Pb;Px;PyM or m, buffered
   whole in cn->mbuf including its own terminator -- by this client's own
   offset, and append the result. Pb (the button/modifier code) passes
   through unchanged; only the position is this client's own to translate.
   The client's own terminal is always told to use SGR (render.c's own
   hd_modes sees to that) regardless of what the program on the pty asked
   for, so mtrans only ever has this one form to recognise -- but the
   program still has to receive whatever form it did ask for, so this
   re-encodes to legacy here if that is what cn->msgr says it wants.
   Malformed input cannot reach here: mtrans only calls this once it has
   seen digits, ';' and a terminator and nothing else since "\e[<". */
void hd_mrewrite(struct hd_cli *cn, str *out)
{
	const char *p = cn->mbuf.p + 3;
	char term = cn->mbuf.p[cn->mbuf.n - 1];
	long b, x, y;

	b = strtol(p, (char **)&p, 10);
	if (*p == ';')
		p++;
	x = strtol(p, (char **)&p, 10);
	if (*p == ';')
		p++;
	y = strtol(p, (char **)&p, 10);
	x += cn->col;
	y += cn->row;
	if (!cn->msgr) {
		hd_mlegacy(out, term == 'm' ? 3 : b, x, y);
		return;
	}
	s_cat(out, "\033[<");
	s_num(out, b);
	s_ch(out, ';');
	s_num(out, x);
	s_ch(out, ';');
	s_num(out, y);
	s_ch(out, term);
}

/* A client's own bytes toward the pty, with any SGR mouse report in them
   rewritten by this client's own offset first -- a click at its own local
   column 4 has to land at the union's own column 4 + cn->col, or it hits
   whatever happens to be there instead of what the client actually clicked
   on. Everything else -- keystrokes, any other escape sequence -- passes
   through untouched, one byte at a time if that is what it takes to tell
   the two apart; a plain byte or an already-known non-mouse sequence never
   waits on a later byte to be forwarded.

   A report can arrive split across more than one read (a slow link, or the
   client's own tty driver breaking up a burst), so cn->mst/cn->mbuf carry
   an in-progress sequence from one call to the next: 0 nothing pending, 1
   just saw ESC, 2 saw ESC[, 3 saw ESC[< and is accumulating digits and ';'
   toward a terminator. Anything that turns out not to be heading for a
   complete, well-formed report -- a different escape, or one that runs
   past a sane length -- flushes what was buffered as-is and starts over;
   nothing is ever dropped, only possibly forwarded a few bytes later than
   a byte-for-byte relay would have. */
void hd_mtrans(struct hd_cli *cn, const char *p, size_t n, str *out)
{
	size_t i;
	unsigned char c;
	int done;

	for (i = 0; i < n; i++) {
		c = (unsigned char)p[i];
		done = 0;
		while (!done) {
			done = 1;
			switch (cn->mst) {
			case 0:
				if (c == 0x1b) {
					s_ch(&cn->mbuf, (int)c);
					cn->mst = 1;
				} else {
					s_ch(out, (int)c);
				}
				break;
			case 1:
				if (c == '[') {
					s_ch(&cn->mbuf, (int)c);
					cn->mst = 2;
				} else {
					s_add(out, cn->mbuf.p, cn->mbuf.n);
					cn->mbuf.n = 0;
					cn->mst = 0;
					done = 0;
				}
				break;
			case 2:
				if (c == '<') {
					s_ch(&cn->mbuf, (int)c);
					cn->mst = 3;
				} else {
					s_add(out, cn->mbuf.p, cn->mbuf.n);
					cn->mbuf.n = 0;
					cn->mst = 0;
					done = 0;
				}
				break;
			case 3:
				if (c == 'M' || c == 'm') {
					s_ch(&cn->mbuf, (int)c);
					hd_mrewrite(cn, out);
					cn->mbuf.n = 0;
					cn->mst = 0;
				} else if ((c >= '0' && c <= '9') ||
					   c == ';') {
					/* 32 is a real margin, not a guess:
					   the longest a genuine report gets
					   is "\e[<223;9999;9999M", 18 bytes. */
					if (cn->mbuf.n < 32) {
						s_ch(&cn->mbuf, (int)c);
					} else {
						s_add(out, cn->mbuf.p,
						      cn->mbuf.n);
						s_ch(out, (int)c);
						cn->mbuf.n = 0;
						cn->mst = 0;
					}
				} else {
					s_add(out, cn->mbuf.p, cn->mbuf.n);
					cn->mbuf.n = 0;
					cn->mst = 0;
					done = 0;
				}
				break;
			}
		}
	}
}
