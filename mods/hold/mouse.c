#define _GNU_SOURCE

#include "hd.h"
#include <stdlib.h>

/* Rewrite one complete SGR mouse report -- \e[<Pb;Px;PyM or m, buffered
   whole in cn->mbuf including its own terminator -- by this client's own
   offset, and append the result. Pb (the button/modifier code) passes
   through unchanged; only the position is this client's own to translate.
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
	s_cat(out, "\033[<");
	s_num(out, b);
	s_ch(out, ';');
	s_num(out, x + cn->col);
	s_ch(out, ';');
	s_num(out, y + cn->row);
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
