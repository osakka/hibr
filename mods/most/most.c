#define _GNU_SOURCE

#include "hibr.h"
#include "../display.h"
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef MS_CH
#define MS_CH 65536
#endif

typedef struct ms_line ms_line;
struct ms_line {
	char *p;
	size_t n;
};

typedef struct ms_buf ms_buf;
struct ms_buf {
	char *name;
	vec lines;
	str part;
	int fd;
	int eof;
	int binary;
	long bytes;
};

typedef struct ms_win ms_win;
struct ms_win {
	ms_buf *b;
	long top;
	int hoff;
	int follow;
};

static const dp_api *dp;

/* Add one line to a buffer, taking ownership of a copy. */
void ms_add(ms_buf *b, const char *p, size_t n)
{
	ms_line *l = xm(sizeof *l);

	l->p = xm(n + 1);
	memcpy(l->p, p, n);
	l->p[n] = 0;
	l->n = n;
	v_add(&b->lines, l);
}

/* Read whatever has arrived, without waiting for the end. */
int ms_pump(ms_buf *b)
{
	char *t = xm(MS_CH);
	ssize_t k;
	size_t i, st;
	int got = 0;

	if (b->fd < 0 || b->eof) {
		free(t);
		return 0;
	}
	for (;;) {
		k = read(b->fd, t, MS_CH);
		if (k < 0 && errno == EINTR)
			continue;
		if (k < 0)
			break;
		if (k == 0) {
			b->eof = 1;
			break;
		}
		b->bytes += k;
		if (!b->lines.n && !b->part.n) {
			for (i = 0; i < (size_t)k; i++)
				if (!t[i]) {
					b->binary = 1;
					break;
				}
		}
		s_add(&b->part, t, (size_t)k);
		st = 0;
		for (i = 0; i < b->part.n; i++) {
			if (b->part.p[i] != '\n')
				continue;
			ms_add(b, b->part.p + st, i - st);
			st = i + 1;
			got = 1;
		}
		if (st) {
			memmove(b->part.p, b->part.p + st, b->part.n - st);
			b->part.n -= st;
		}
		if ((size_t)k < MS_CH)
			break;
	}
	free(t);
	return got;
}

/* Finish the buffer off, turning any trailing text into a last line. */
void ms_settle(ms_buf *b)
{
	if (b->eof && b->part.n) {
		ms_add(b, b->part.p, b->part.n);
		b->part.n = 0;
	}
}

/* Open a file, or take a descriptor that is already open. */
ms_buf *ms_open(const char *nm, int fd)
{
	ms_buf *b = xm(sizeof *b);
	const char *sl = strrchr(nm, '/');

	memset(b, 0, sizeof *b);
	s_init(&b->part);
	b->name = xs(sl && sl[1] ? sl + 1 : nm);
	b->fd = fd;
	if (fd < 0) {
		b->fd = open(nm, O_RDONLY);
		if (b->fd < 0) {
			lg(HIBR_LERR, "most: %s: %s", nm, strerror(errno));
			free(b->name);
			free(b);
			return 0;
		}
	}
	return b;
}

/* Release a buffer and every line in it. */
void ms_free(ms_buf *b)
{
	size_t i;

	for (i = 0; i < b->lines.n; i++) {
		free(((ms_line *)b->lines.p[i])->p);
		free(b->lines.p[i]);
	}
	v_free(&b->lines);
	s_free(&b->part);
	if (b->fd > 0)
		close(b->fd);
	free(b->name);
	free(b);
}

/* Turn an SGR sequence into a pen, so colour in the input survives paging. */
const char *ms_sgr(const char *p, const char *e, unsigned *fg, unsigned *bg,
		   unsigned *at)
{
	long v[8];
	int n = 0, i;

	p += 2;
	v[0] = 0;
	while (p < e && *p != 'm') {
		if (*p >= '0' && *p <= '9') {
			v[n] = v[n] * 10 + (*p - '0');
		} else if (*p == ';') {
			if (++n >= 8)
				n = 7;
			v[n] = 0;
		}
		p++;
	}
	n++;
	for (i = 0; i < n; i++) {
		long c = v[i];
		if (c == 0) {
			*fg = DP_DEFAULT;
			*bg = DP_DEFAULT;
			*at = 0;
		} else if (c == 1) {
			*at |= DP_BOLD;
		} else if (c == 2) {
			*at |= DP_DIM;
		} else if (c == 3) {
			*at |= DP_ITAL;
		} else if (c == 4) {
			*at |= DP_UNDER;
		} else if (c == 7) {
			*at |= DP_REV;
		} else if (c == 9) {
			*at |= DP_STRIKE;
		} else if (c >= 30 && c <= 37) {
			*fg = DP_PAL | (unsigned)(c - 30);
		} else if (c == 39) {
			*fg = DP_DEFAULT;
		} else if (c >= 40 && c <= 47) {
			*bg = DP_PAL | (unsigned)(c - 40);
		} else if (c == 49) {
			*bg = DP_DEFAULT;
		} else if (c >= 90 && c <= 97) {
			*fg = DP_PAL | (unsigned)(c - 90 + 8);
		} else if (c >= 100 && c <= 107) {
			*bg = DP_PAL | (unsigned)(c - 100 + 8);
		} else if ((c == 38 || c == 48) && i + 1 < n) {
			unsigned *t = c == 38 ? fg : bg;
			if (v[i + 1] == 5 && i + 2 < n) {
				*t = DP_PAL | (unsigned)v[i + 2];
				i += 2;
			} else if (v[i + 1] == 2 && i + 4 < n) {
				*t = DP_RGB | ((unsigned)v[i + 2] << 16) |
				     ((unsigned)v[i + 3] << 8) |
				     (unsigned)v[i + 4];
				i += 4;
			}
		}
	}
	return p < e ? p + 1 : e;
}

/* Where a match starts, or null; a case-folded search when asked. */
const char *ms_find(const char *h, size_t hn, const char *nd, int fold)
{
	size_t n = strlen(nd), i, j;

	if (!n || n > hn)
		return 0;
	for (i = 0; i + n <= hn; i++) {
		for (j = 0; j < n; j++) {
			char a = h[i + j], b = nd[j];
			if (fold) {
				a = (char)tolower((unsigned char)a);
				b = (char)tolower((unsigned char)b);
			}
			if (a != b)
				break;
		}
		if (j == n)
			return h + i;
	}
	return 0;
}

/* Draw one line into a row, honouring the horizontal offset and any colour. */
void ms_draw(ms_win *w, int row, int cols, const char *find, int fold,
	     unsigned mfg, unsigned mbg)
{
	ms_line *l;
	const char *p, *e, *hit;
	unsigned fg = DP_DEFAULT, bg = DP_DEFAULT, at = 0;
	int col = 0, skip = w->hoff, lcol = 0;
	size_t off;
	char one[8];
	int len;

	if ((size_t)w->top + (size_t)row >= w->b->lines.n)
		return;
	l = (ms_line *)w->b->lines.p[w->top + row];
	p = l->p;
	e = l->p + l->n;
	while (p < e && col < cols) {
		if (*p == 27 && p + 1 < e && p[1] == '[') {
			const char *q = p + 2;
			while (q < e && *q != 'm' && !(*q >= '@' && *q <= '~'))
				q++;
			if (q < e && *q == 'm') {
				p = ms_sgr(p, e, &fg, &bg, &at);
				continue;
			}
			p = q < e ? q + 1 : e;
			continue;
		}
		off = (size_t)(p - l->p);
		hit = 0;
		if (find && *find) {
			hit = ms_find(l->p + off, l->n - off, find, fold);
			if (hit != l->p + off)
				hit = 0;
		}
		len = 1;
		if ((unsigned char)*p >= 0xC0) {
			unsigned cp;
			len = u8dec(p, (size_t)(e - p), &cp);
		}
		if (*p == '\t') {
			int k = 8 - (lcol % 8);
			while (k--) {
				if (skip > 0) {
					skip--;
				} else if (col < cols) {
					dp->pen(fg, bg, at);
					col += dp->put(row, col, " ");
				}
				lcol++;
			}
			p++;
			continue;
		}
		if (skip > 0) {
			skip--;
			lcol++;
			p += len;
			continue;
		}
		lcol++;
		if (hit) {
			size_t fn = strlen(find);
			dp->pen(mfg, mbg, DP_BOLD);
			while (fn && p < e && col < cols) {
				len = 1;
				if ((unsigned char)*p >= 0xC0) {
					unsigned cp;
					len = u8dec(p, (size_t)(e - p), &cp);
				}
				memcpy(one, p, (size_t)len);
				one[len] = 0;
				col += dp->put(row, col, one);
				p += len;
				fn -= fn < (size_t)len ? fn : (size_t)len;
			}
			continue;
		}
		dp->pen(fg, bg, at);
		if ((unsigned char)*p < 32) {
			dp->pen(DP_PAL | 244u, bg, at);
			one[0] = '^';
			one[1] = (char)(*p + 64);
			one[2] = 0;
		} else {
			memcpy(one, p, (size_t)len);
			one[len] = 0;
		}
		col += dp->put(row, col, one);
		p += len;
	}
}

/* Page through files, or through whatever is being piped in. */
int m_most(sh *s, int ac, char **av)
{
	vec bufs;
	ms_win win[2];
	int nwin = 1, cur = 0, i, rows, cols, vh, quit = 0, fold = 1;
	int status = 1, redraw = 1;
	str find, key, msg;
	ms_buf *b;

	dp = (const dp_api *)hibr_require(s, "display", DP_API_VER);
	if (!dp) {
		lg(HIBR_LERR, "most: needs a display; mod load console");
		return HIBR_FAIL;
	}
	bufs.p = 0;
	bufs.n = 0;
	bufs.cap = 0;
	s_init(&find);
	s_init(&key);
	s_init(&msg);
	for (i = 1; i < ac; i++) {
		if (av[i][0] == '-' && av[i][1]) {
			lg(HIBR_LERR, "usage: most [file...]");
			return 2;
		}
		b = ms_open(av[i], -1);
		if (b)
			v_add(&bufs, b);
	}
	if (!bufs.n) {
		if (isatty(0)) {
			lg(HIBR_LERR, "most: give a file, or pipe something in");
			return 2;
		}
		b = ms_open("standard input", 0);
		if (b)
			v_add(&bufs, b);
	}
	if (!bufs.n)
		return HIBR_FAIL;
	if (dp->open(s) != HIBR_OK)
		return HIBR_FAIL;
	memset(win, 0, sizeof win);
	win[0].b = (ms_buf *)bufs.p[0];
	win[1].b = (ms_buf *)bufs.p[bufs.n > 1 ? 1 : 0];
	for (i = 0; i < (int)bufs.n; i++)
		ms_pump((ms_buf *)bufs.p[i]);

	while (!quit) {
		int wi, r;
		dp->size(&rows, &cols);
		if (dp->resized())
			redraw = 1;
		for (i = 0; i < (int)bufs.n; i++) {
			ms_buf *bb = (ms_buf *)bufs.p[i];
			if (ms_pump(bb))
				redraw = 1;
			ms_settle(bb);
		}
		for (wi = 0; wi < nwin; wi++)
			if (win[wi].follow) {
				long h = nwin == 1 ? rows - status
						   : (rows - status) / nwin - 1;
				long want = (long)win[wi].b->lines.n - h;
				if (want < 0)
					want = 0;
				if (win[wi].top != want) {
					win[wi].top = want;
					redraw = 1;
				}
			}
		if (redraw) {
			dp->pen(DP_DEFAULT, DP_DEFAULT, 0);
			dp->clear();
			vh = nwin == 1 ? rows - status
				       : (rows - status) / nwin - 1;
			for (wi = 0; wi < nwin; wi++) {
				int base = wi * (vh + 1);
				for (r = 0; r < vh; r++) {
					ms_win sv = win[wi];
					sv.top = win[wi].top;
					ms_draw(&sv, base + r, cols, find.p,
						fold, DP_PAL | 16u,
						DP_PAL | 227u);
					if (0)
						break;
				}
				if (nwin > 1) {
					str t;
					s_init(&t);
					s_cat(&t, wi == cur ? " * " : "   ");
					s_cat(&t, win[wi].b->name);
					dp->pen(DP_PAL | 252u, DP_PAL | 238u,
						0);
					dp->fill(base + vh, 0, 1, cols, " ");
					dp->put(base + vh, 0, t.p);
					s_free(&t);
				}
			}
			{
				str t;
				long ln = (long)win[cur].b->lines.n;
				s_init(&t);
				s_ch(&t, ' ');
				s_cat(&t, win[cur].b->name);
				s_cat(&t, "  ");
				s_num(&t, win[cur].top + 1);
				s_ch(&t, '-');
				s_num(&t, win[cur].top + vh < ln
						  ? win[cur].top + vh
						  : ln);
				s_ch(&t, '/');
				s_num(&t, ln);
				if (!win[cur].b->eof)
					s_cat(&t, "+");
				if (win[cur].hoff) {
					s_cat(&t, "  col ");
					s_num(&t, (long)win[cur].hoff + 1);
				}
				if (win[cur].follow)
					s_cat(&t, "  FOLLOWING");
				if (win[cur].b->binary)
					s_cat(&t, "  BINARY");
				if (find.n) {
					s_cat(&t, "  /");
					s_cat(&t, find.p);
				}
				if (msg.n) {
					s_cat(&t, "  ");
					s_cat(&t, msg.p);
				}
				dp->pen(DP_PAL | 16u, DP_PAL | 110u, 0);
				dp->fill(rows - 1, 0, 1, cols, " ");
				dp->put(rows - 1, 0, t.p);
				dp->pen(DP_DEFAULT, DP_DEFAULT, 0);
				s_free(&t);
			}
			dp->flush();
			redraw = 0;
		}
		key.n = 0;
		if (key.p)
			key.p[0] = 0;
		r = dp->key(win[cur].follow || !win[cur].b->eof ? 400 : -1,
			    &key);
		if (r < 0)
			break;
		if (r == 0)
			continue;
		msg.n = 0;
		if (msg.p)
			msg.p[0] = 0;
		vh = nwin == 1 ? rows - status : (rows - status) / nwin - 1;
		redraw = 1;
		{
			const char *k = key.p ? key.p : "";
			ms_win *w = &win[cur];
			long last = (long)w->b->lines.n - vh;
			if (last < 0)
				last = 0;
			if (!strcmp(k, "q") || !strcmp(k, "ctrl-c")) {
				quit = 1;
			} else if (!strcmp(k, "down") || !strcmp(k, "j") ||
				   !strcmp(k, "enter")) {
				if (w->top < last)
					w->top++;
			} else if (!strcmp(k, "up") || !strcmp(k, "k")) {
				if (w->top > 0)
					w->top--;
			} else if (!strcmp(k, "pagedown") || !strcmp(k, " ") ||
				   !strcmp(k, "ctrl-f")) {
				w->top += vh;
				if (w->top > last)
					w->top = last;
			} else if (!strcmp(k, "pageup") || !strcmp(k, "ctrl-b")) {
				w->top -= vh;
				if (w->top < 0)
					w->top = 0;
			} else if (!strcmp(k, "home") || !strcmp(k, "g")) {
				w->top = 0;
			} else if (!strcmp(k, "end") || !strcmp(k, "G")) {
				w->top = last;
			} else if (!strcmp(k, "right") || !strcmp(k, "l")) {
				w->hoff += 8;
			} else if (!strcmp(k, "left") || !strcmp(k, "h")) {
				w->hoff -= 8;
				if (w->hoff < 0)
					w->hoff = 0;
			} else if (!strcmp(k, "F")) {
				w->follow = !w->follow;
			} else if (!strcmp(k, "i")) {
				fold = !fold;
				s_cat(&msg, fold ? "ignoring case"
						 : "matching case");
			} else if (!strcmp(k, "s") || !strcmp(k, "2")) {
				nwin = nwin == 1 ? 2 : 1;
				if (nwin == 1)
					cur = 0;
			} else if (!strcmp(k, "tab")) {
				if (nwin > 1)
					cur = cur ? 0 : 1;
			} else if (!strcmp(k, "n")) {
				if (bufs.n > 1) {
					int x;
					for (x = 0; x < (int)bufs.n; x++)
						if (bufs.p[x] == w->b)
							break;
					w->b = (ms_buf *)
						bufs.p[(x + 1) % (int)bufs.n];
					w->top = 0;
					w->hoff = 0;
				}
			} else if (!strcmp(k, "/")) {
				str q;
				int done = 0;
				s_init(&q);
				while (!done) {
					str t;
					s_init(&t);
					s_cat(&t, " /");
					s_cat(&t, q.p ? q.p : "");
					dp->pen(DP_PAL | 16u, DP_PAL | 227u,
						0);
					dp->fill(rows - 1, 0, 1, cols, " ");
					dp->put(rows - 1, 0, t.p);
					dp->flush();
					s_free(&t);
					key.n = 0;
					if (key.p)
						key.p[0] = 0;
					if (dp->key(-1, &key) != 1)
						break;
					k = key.p ? key.p : "";
					if (!strcmp(k, "enter")) {
						done = 1;
					} else if (!strcmp(k, "escape")) {
						q.n = 0;
						done = 1;
					} else if (!strcmp(k, "backspace")) {
						if (q.n)
							q.n--;
						if (q.p)
							q.p[q.n] = 0;
					} else if (!strncmp(k, "paste ", 6)) {
						s_cat(&q, k + 6);
					} else if (strlen(k) <= 4 &&
						   (unsigned char)k[0] >= 32) {
						s_cat(&q, k);
					}
				}
				find.n = 0;
				if (find.p)
					find.p[0] = 0;
				if (q.n)
					s_cat(&find, q.p);
				s_free(&q);
				if (find.n) {
					long x;
					for (x = w->top + 1;
					     x < (long)w->b->lines.n; x++) {
						ms_line *ln =
							(ms_line *)w->b->lines.p[x];
						if (ms_find(ln->p, ln->n,
							    find.p, fold)) {
							w->top = x;
							break;
						}
					}
				}
			} else if (!strcmp(k, "N")) {
				long x;
				for (x = w->top + 1; find.n &&
						     x < (long)w->b->lines.n;
				     x++) {
					ms_line *ln =
						(ms_line *)w->b->lines.p[x];
					if (ms_find(ln->p, ln->n, find.p, fold)) {
						w->top = x;
						break;
					}
				}
			} else if (!strcmp(k, "?")) {
				s_cat(&msg,
				      "j k space b g G  h l  / N i  F  s tab n  q");
			}
			if (w->top > last)
				w->top = last;
			if (w->top < 0)
				w->top = 0;
		}
	}
	dp->close(s);
	for (i = 0; i < (int)bufs.n; i++)
		ms_free((ms_buf *)bufs.p[i]);
	v_free(&bufs);
	s_free(&find);
	s_free(&key);
	s_free(&msg);
	return HIBR_OK;
}

const hibr_bi most_bi[] = {
	{ "most", m_most, "page through files or a pipe, with colour and search" },
	HIBR_BI_END
};

HIBR_MODULE("most", "0.21", "a pager built on the screen module", most_bi, 0, 0);
