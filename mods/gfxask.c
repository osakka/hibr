#define _GNU_SOURCE
#include <errno.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>

#include "gfxask.h"
#include "display.h"

/* gfxask -- what a terminal says it draws. See gfxask.h. */

const char gx_ask[] = "\033_Gi=4201,s=1,v=1,a=q,t=d,f=24;AAAA\033\\\033[?u"
		       "\033[?1016$p\033]22;?none\033\\\033P+q626365\033\\"
		       "\033P+q726570\033\\\033P+q524742\033\\"
		       "\033P+q636f6c6f7273\033\\\033[c";

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
	      int *sixel, int *kbd, int *copy, int *mpix)
{
	size_t i;
	int six = 0, cp = 0, v = -1, first = -1, dollar = 0;

	*da = 0;
	*sixel = 0;
	*kbd = 0;
	*copy = 0;
	*mpix = -1;
	for (i = 3; i < n; i++) {
		if (p[i] >= '0' && p[i] <= '9') {
			v = (v < 0 ? 0 : v * 10) + (p[i] - '0');
			continue;
		}
		if (p[i] == ';') {
			if (first < 0)
				first = v;
			if (v == 4)
				six = 1;
			if (v == 28)
				cp = 1;
			v = -1;
			continue;
		}
		if (p[i] == '$')
			dollar = 1;
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
	/* DECRPM, CSI ? mode ; state $ y: 1 set, 2 reset, 3 and 4 the same
	   permanently, 0 a mode the terminal does not know. */
	if (p[i] == 'y' && dollar && first == 1016)
		*mpix = v >= 1 && v <= 4;
	return 2;
}

/* An OSC at the front of p, and when it is the pointer-shape query's answer
   (OSC 22, `1` or `0` for the one name asked), that answer in *ptr. Every
   other OSC is taken and ignored: an answer is never a key. */
int gx_osc(const char *p, size_t n, size_t max, size_t *used, int *ptr)
{
	size_t i, end = 0, skip = 0;

	for (i = 2; i < n; i++) {
		if ((unsigned char)p[i] == 7) {
			end = i;
			skip = 1;
			break;
		}
		if ((unsigned char)p[i] == 27) {
			if (i + 1 >= n)
				return 0;
			if (p[i + 1] != '\\')
				return -1;
			end = i;
			skip = 2;
			break;
		}
	}
	if (!skip)
		return n < max ? 0 : -1;
	*used = end + skip;
	if (end >= 6 && !memcmp(p + 2, "22;", 3))
		*ptr = p[5] == '1';
	return 2;
}

/* A hex digit's value, -1 for none. */
int gx_hexv(int c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}

/* A DCS at the front of p, and when it is XTGETTCAP's answer, what it says
   about the capability asked: `1+r<name>[=<value>]` yes, `0+r<name>` no,
   each in hex. bce, rep, RGB and Tc are flags; colors is a count, its value
   a number written in hex digits. Every other DCS is taken and ignored. */
int gx_dcs(const char *p, size_t n, size_t max, size_t *used, gx_tcap *tc)
{
	size_t i, e, k;
	long v = 0;
	int yes;

	for (i = 2; i + 1 < n; i++) {
		if ((unsigned char)p[i] != 27 || p[i + 1] != '\\')
			continue;
		*used = i + 2;
		if (i < 6 || (p[2] != '0' && p[2] != '1') || p[3] != '+' ||
		    p[4] != 'r')
			return 2;
		yes = p[2] == '1';
		for (e = 5; e < i && p[e] != '=' && p[e] != ';'; e++)
			;
		if (yes && e < i && p[e] == '=')
			for (k = e + 1; k + 1 < i; k += 2) {
				int hi = gx_hexv(p[k]), lo = gx_hexv(p[k + 1]);

				if (hi < 0 || lo < 0 || hi * 16 + lo < '0' ||
				    hi * 16 + lo > '9')
					break;
				v = v * 10 + (hi * 16 + lo - '0');
			}
		k = e - 5;
		if (k == 6 && !strncasecmp(p + 5, "626365", 6))
			tc->bce = yes;
		else if (k == 6 && !strncasecmp(p + 5, "726570", 6))
			tc->rep = yes;
		else if ((k == 6 && !strncasecmp(p + 5, "524742", 6)) ||
			 (k == 4 && !strncasecmp(p + 5, "5463", 4)))
			tc->rgb = yes;
		else if (k == 12 && !strncasecmp(p + 5, "636f6c6f7273", 12))
			tc->colors = yes ? v : 0;
		return 2;
	}
	return n < max ? 0 : -1;
}

/* How many colours to send a terminal (Gitea #353). The name is the last
   resort and reads only what it says plainly: `256color` is 256, the Linux
   console and screen's own names without it are 16, and anything else is
   24-bit -- a terminal that answers nothing and names itself `xterm` may
   well be a truecolour one reached over ssh, where COLORTERM is dropped, and
   stepping it down would be a loss nobody asked for. */
int gx_depth(const gx_tcap *tc)
{
	const char *f = getenv("HIBR_COLOURS"), *ct = getenv("COLORTERM"),
		   *t = getenv("TERM");
	int n;

	if (f && *f) {
		n = atoi(f);
		if (n == 24 || n == 256 || n == 16 || n == 8)
			return n;
	}
	if (tc && tc->rgb == 1)
		return 24;
	if (ct && (!strcmp(ct, "truecolor") || !strcmp(ct, "24bit")))
		return 24;
	if (tc && tc->colors > 0)
		return tc->colors >= 256 ? 256 : tc->colors >= 16 ? 16 : 8;
	if (t && strstr(t, "256color"))
		return 256;
	if (t && (!strcmp(t, "linux") || !strncmp(t, "screen", 6) ||
		  !strncmp(t, "vt", 2)))
		return 16;
	return 24;
}

/* xterm's own sixteen, which is what an index below 16 means to a terminal
   that has not been told otherwise, and what the step-down aims at. */
static const unsigned gx_pal16[16] = {
	0x000000, 0xcd0000, 0x00cd00, 0xcdcd00, 0x0000ee, 0xcd00cd, 0x00cdcd,
	0xe5e5e5, 0x7f7f7f, 0xff0000, 0x00ff00, 0xffff00, 0x5c5cff, 0xff00ff,
	0x00ffff, 0xffffff
};

/* How far apart two colours look, near enough: squared distance with green
   weighted most and blue least, as the eye does. */
long gx_cdist(unsigned a, unsigned b)
{
	long dr = (long)((a >> 16) & 255) - (long)((b >> 16) & 255);
	long dg = (long)((a >> 8) & 255) - (long)((b >> 8) & 255);
	long db = (long)(a & 255) - (long)(b & 255);

	return 3 * dr * dr + 4 * dg * dg + 2 * db * db;
}

/* What an index of the 256 looks like, for stepping one down to 16. */
unsigned gx_idxrgb(int i)
{
	static const int lv[6] = { 0, 95, 135, 175, 215, 255 };

	if (i < 16)
		return gx_pal16[i];
	if (i < 232) {
		i -= 16;
		return ((unsigned)lv[i / 36] << 16) | ((unsigned)lv[i / 6 % 6] << 8) |
		       (unsigned)lv[i % 6];
	}
	i = 8 + (i - 232) * 10;
	return ((unsigned)i << 16) | ((unsigned)i << 8) | (unsigned)i;
}

/* The nearest of the 256 to a colour: the nearer of its cube point and its
   grey. */
int gx_near256(unsigned c)
{
	int r = (int)((c >> 16) & 255), g = (int)((c >> 8) & 255),
	    b = (int)(c & 255), ri, gi, bi, k, gy;

	ri = r < 48 ? 0 : r < 115 ? 1 : (r - 35) / 40;
	gi = g < 48 ? 0 : g < 115 ? 1 : (g - 35) / 40;
	bi = b < 48 ? 0 : b < 115 ? 1 : (b - 35) / 40;
	k = 16 + 36 * ri + 6 * gi + bi;
	gy = (r + g + b) / 3;
	gy = gy < 8 ? 0 : gy > 238 ? 23 : (gy - 8 + 5) / 10;
	if (gy > 23)
		gy = 23;
	return gx_cdist(c, gx_idxrgb(232 + gy)) < gx_cdist(c, gx_idxrgb(k)) ?
		       232 + gy : k;
}

/* The nearest of the first n of the standard sixteen. */
int gx_nearpal(unsigned c, int n)
{
	int i, best = 0;
	long d, bd = -1;

	for (i = 0; i < n; i++) {
		d = gx_cdist(c, gx_pal16[i]);
		if (bd < 0 || d < bd) {
			bd = d;
			best = i;
		}
	}
	return best;
}

void gx_sgrcol(str *b, unsigned v, int bgp, int depth)
{
	int i;

	if (v == DP_DEFAULT) {
		s_cat(b, bgp ? ";49" : ";39");
		return;
	}
	if ((v & DP_RGB) && (depth == 24 || depth == 0)) {
		s_cat(b, bgp ? ";48;2;" : ";38;2;");
		s_num(b, (long)((v >> 16) & 0xFF));
		s_ch(b, ';');
		s_num(b, (long)((v >> 8) & 0xFF));
		s_ch(b, ';');
		s_num(b, (long)(v & 0xFF));
		return;
	}
	i = (v & DP_RGB) ? -1 : (int)(v & 0xFF);
	if (depth == 256 || depth == 24 || depth == 0) {
		if (i < 0)
			i = gx_near256(v & 0xFFFFFF);
		s_cat(b, bgp ? ";48;5;" : ";38;5;");
		s_num(b, (long)i);
		return;
	}
	if (i < 0 || i >= depth)
		i = gx_nearpal(i < 0 ? v & 0xFFFFFF : gx_idxrgb(i), depth);
	s_ch(b, ';');
	s_num(b, (long)((i < 8 ? (bgp ? 40 : 30) : (bgp ? 100 : 90)) + i % 8));
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
int gx_probe(int in, int out, long ms, str *rest, int *kbd, int *copy,
	     int *mpix, int *ptr, gx_tcap *tc)
{
	const char *f = getenv("HIBR_GFX");
	str b;
	size_t i, used, off = 0;
	long end, left;
	int kit = 0, da = 0, six = 0, k, s, d, r, kb, cy, mp, force = -1;
	ssize_t w;
	struct pollfd q;

	*kbd = 0;
	*copy = 0;
	*mpix = 0;
	*ptr = -1;
	tc->bce = tc->rep = tc->rgb = tc->colors = -1;
	if (f && *f)
		force = !strcmp(f, "kitty") ? GX_KITTY
			: !strcmp(f, "sixel") ? GX_SIXEL : GX_NONE;
	for (i = 0; i < sizeof gx_ask - 1;) {
		w = write(out, gx_ask + i, sizeof gx_ask - 1 - i);
		if (w < 0 && errno == EINTR)
			continue;
		if (w <= 0)
			return force >= 0 ? force : GX_UNKNOWN;
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
			} else if (b.p[off + 1] == ']') {
				r = gx_osc(b.p + off, b.n - off, 64, &used, ptr);
			} else if (b.p[off + 1] == 'P') {
				r = gx_dcs(b.p + off, b.n - off, 64, &used, tc);
			} else if (b.p[off + 1] == '[' && b.p[off + 2] == '?') {
				r = gx_report(b.p + off, b.n - off, 64, &used, &d, &s,
					      &kb, &cy, &mp);
				if (r == 2 && mp > 0)
					*mpix = 1;
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
	if (force >= 0) {
		lg(HIBR_LDBG, "gfx: %s, as HIBR_GFX asks; asked the rest anyway",
		   gx_name(force));
		return force;
	}
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
