#define _GNU_SOURCE
#include <errno.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "gfxask.h"

/* gfxask -- what a terminal says it draws. See gfxask.h. */

const char gx_ask[] = "\033_Gi=4201,s=1,v=1,a=q,t=d,f=24;AAAA\033\\\033[?u\033[c";

/* An APC at the front of p, and whether it is kitty's own `OK`. */
int gx_apc(const char *p, size_t n, size_t max, size_t *used, int *kit)
{
	size_t i, end = 0;

	*kit = 0;
	for (i = 2; i < n; i++) {
		if ((unsigned char)p[i] != 27)
			continue;
		if (i + 1 >= n)
			return 0;
		if (p[i + 1] != '\\')
			return -1;
		end = i;
		break;
	}
	if (!end)
		return n < max ? 0 : -1;
	*used = end + 2;
	if (end > 2 && p[2] == 'G' && memmem(p + 2, end - 2, ";OK", 3))
		*kit = 1;
	return 2;
}

/* A private report at the front of p, and whether it is the device
   attributes, and whether those say sixel and the rectangle copy (feature
   28, DECCRA: Gitea #337), or the keyboard protocol's own answer. */
int gx_report(const char *p, size_t n, size_t max, size_t *used, int *da,
	      int *sixel, int *kbd, int *copy)
{
	size_t i;
	int six = 0, cp = 0, v = -1;

	*da = 0;
	*sixel = 0;
	*kbd = 0;
	*copy = 0;
	for (i = 3; i < n; i++) {
		if (p[i] >= '0' && p[i] <= '9') {
			v = (v < 0 ? 0 : v * 10) + (p[i] - '0');
			continue;
		}
		if (p[i] == ';') {
			if (v == 4)
				six = 1;
			if (v == 28)
				cp = 1;
			v = -1;
			continue;
		}
		if (p[i] >= '@' && p[i] <= '~')
			break;
		if (p[i] == ':' || p[i] == '$' || p[i] == ' ')
			continue;
		return -1;
	}
	if (i >= n)
		return n < max ? 0 : -1;
	*used = i + 1;
	if (p[i] == 'c') {
		*da = 1;
		*sixel = six || v == 4;
		*copy = cp || v == 28;
	}
	if (p[i] == 'u')
		*kbd = 1;
	return 2;
}

/* Milliseconds on a clock no one sets. */
long gx_now(void)
{
	struct timespec t;

	clock_gettime(CLOCK_MONOTONIC, &t);
	return (long)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

/* Ask, read until the device attributes answer or the time is up, and keep
   whatever was not an answer. The wait is against a deadline rather than a
   count of reads: every read here can be interrupted, and restarting the
   wait per signal would make the bound meaningless. */
int gx_probe(int in, int out, long ms, str *rest, int *kbd, int *copy)
{
	const char *f = getenv("HIBR_GFX");
	str b;
	size_t i, used, off = 0;
	long end, left;
	int kit = 0, da = 0, six = 0, k, s, d, r, kb, cy;
	ssize_t w;
	struct pollfd q;

	*kbd = 0;
	*copy = 0;
	if (f && *f) {
		if (!strcmp(f, "kitty"))
			return GX_KITTY;
		if (!strcmp(f, "sixel"))
			return GX_SIXEL;
		return GX_NONE;
	}
	for (i = 0; i < sizeof gx_ask - 1;) {
		w = write(out, gx_ask + i, sizeof gx_ask - 1 - i);
		if (w < 0 && errno == EINTR)
			continue;
		if (w <= 0)
			return GX_UNKNOWN;
		i += (size_t)w;
	}
	s_init(&b);
	s_grow(&b, 256);
	end = gx_now() + ms;
	while (!da && (left = end - gx_now()) > 0) {
		q.fd = in;
		q.events = POLLIN;
		r = poll(&q, 1, (int)left);
		if (r < 0 && errno == EINTR)
			continue;
		if (r <= 0)
			break;
		if (b.cap - b.n < 256)
			s_grow(&b, b.n + 256);
		w = read(in, b.p + b.n, b.cap - b.n - 1);
		if (w < 0 && errno == EINTR)
			continue;
		if (w <= 0)
			break;
		b.n += (size_t)w;
		while (off < b.n && !da) {
			if ((unsigned char)b.p[off] != 27) {
				s_ch(rest, b.p[off++]);
				continue;
			}
			if (off + 2 >= b.n)
				break;
			r = -1;
			if (b.p[off + 1] == '_') {
				r = gx_apc(b.p + off, b.n - off, 64, &used, &k);
				if (r == 2 && k)
					kit = 1;
			} else if (b.p[off + 1] == '[' && b.p[off + 2] == '?') {
				r = gx_report(b.p + off, b.n - off, 64, &used, &d, &s,
					      &kb, &cy);
				if (r == 2 && d) {
					da = 1;
					six = s;
					*copy = cy;
				}
				if (r == 2 && kb)
					*kbd = 1;
			}
			if (r == 0)
				break;
			if (r == 2) {
				off += used;
				continue;
			}
			s_ch(rest, b.p[off++]);
		}
	}
	if (off < b.n)
		s_add(rest, b.p + off, b.n - off);
	s_free(&b);
	if (kit)
		return GX_KITTY;
	if (da)
		return six ? GX_SIXEL : GX_NONE;
	return GX_UNKNOWN;
}

/* A capability as the word `hold clients` prints. */
const char *gx_name(int g)
{
	switch (g) {
	case GX_KITTY: return "kitty";
	case GX_SIXEL: return "sixel";
	case GX_NONE: return "none";
	case GX_UNASKED: return "unasked";
	}
	return "unknown";
}

/* That word back as a capability; anything else is unknown. */
int gx_byname(const char *s)
{
	if (!s)
		return GX_UNKNOWN;
	if (!strcmp(s, "kitty"))
		return GX_KITTY;
	if (!strcmp(s, "sixel"))
		return GX_SIXEL;
	if (!strcmp(s, "none"))
		return GX_NONE;
	if (!strcmp(s, "unasked"))
		return GX_UNASKED;
	return GX_UNKNOWN;
}
