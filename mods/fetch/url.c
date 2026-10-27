#define _GNU_SOURCE

#include "ft.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

/* Split an http or https address into its parts; the path and query stay
   as they are written, already encoded, and the fragment is dropped. */
int ft_urlsplit(const char *u, ft_url *o, str *err)
{
	const char *p, *e, *hp, *at, *c;

	s_init(&o->host);
	s_init(&o->port);
	s_init(&o->path);
	if (!strncasecmp(u, "https://", 8)) {
		o->tls = 1;
		p = u + 8;
	} else if (!strncasecmp(u, "http://", 7)) {
		o->tls = 0;
		p = u + 7;
	} else {
		s_fmt(err, "%s is not an http:// or https:// address", u);
		return HIBR_FAIL;
	}
	e = p + strcspn(p, "/?#");
	hp = p;
	for (at = p; at < e; at++)
		if (*at == '@')
			hp = at + 1;
	if (hp < e && *hp == '[') {
		c = memchr(hp, ']', (size_t)(e - hp));
		if (!c) {
			s_fmt(err, "%s: an unclosed [ in the host", u);
			return HIBR_FAIL;
		}
		s_add(&o->host, hp + 1, (size_t)(c - hp - 1));
		c++;
		if (c < e && *c == ':')
			s_add(&o->port, c + 1, (size_t)(e - c - 1));
	} else {
		c = hp;
		while (c < e && *c != ':')
			c++;
		s_add(&o->host, hp, (size_t)(c - hp));
		if (c < e)
			s_add(&o->port, c + 1, (size_t)(e - c - 1));
	}
	if (!o->host.n) {
		s_fmt(err, "%s names no host", u);
		return HIBR_FAIL;
	}
	for (c = o->host.p; *c; c++)
		if (*c >= 'A' && *c <= 'Z')
			*(char *)c = (char)(*c + 32);
	if (!o->port.n)
		s_cat(&o->port, o->tls ? "443" : "80");
	if (*e == '/' || *e == '?')
		s_add(&o->path, e, strcspn(e, "#"));
	if (!o->path.n || o->path.p[0] != '/') {
		str t;

		s_init(&t);
		s_ch(&t, '/');
		if (o->path.n)
			s_cat(&t, o->path.p);
		s_free(&o->path);
		o->path = t;
	}
	return HIBR_OK;
}

/* Free a split address. */
void ft_urlfree(ft_url *u)
{
	s_free(&u->host);
	s_free(&u->port);
	s_free(&u->path);
}

/* A split address written out again, the port only when it is not the
   scheme's own. */
void ft_urlstr(const ft_url *u, str *o)
{
	s_cat(o, u->tls ? "https://" : "http://");
	if (strchr(u->host.p, ':')) {
		s_ch(o, '[');
		s_cat(o, u->host.p);
		s_ch(o, ']');
	} else {
		s_cat(o, u->host.p);
	}
	if (strcmp(u->port.p, u->tls ? "443" : "80")) {
		s_ch(o, ':');
		s_cat(o, u->port.p);
	}
	s_cat(o, u->path.p);
}

/* Remove "." and ".." segments from a path, as RFC 3986 section 5.2.4
   says, into o. */
void ft_dots(const char *in, size_t n, str *o)
{
	const char *p = in, *e = in + n, *seg;
	size_t k;

	while (p < e) {
		if (e - p >= 3 && !strncmp(p, "../", 3)) {
			p += 3;
		} else if (e - p >= 2 && !strncmp(p, "./", 2)) {
			p += 2;
		} else if (e - p >= 3 && !strncmp(p, "/./", 3)) {
			p += 2;
		} else if (e - p == 2 && !strncmp(p, "/.", 2)) {
			p += 1;
			s_ch(o, '/');
			p = e;
		} else if (e - p >= 4 && !strncmp(p, "/../", 4)) {
			p += 3;
			while (o->n && o->p[o->n - 1] != '/')
				o->n--;
			if (o->n)
				o->n--;
		} else if (e - p == 3 && !strncmp(p, "/..", 3)) {
			while (o->n && o->p[o->n - 1] != '/')
				o->n--;
			if (o->n)
				o->n--;
			s_ch(o, '/');
			p = e;
		} else if ((e - p == 1 && *p == '.') ||
			   (e - p == 2 && !strncmp(p, "..", 2))) {
			p = e;
		} else {
			seg = p;
			if (*seg == '/')
				seg++;
			k = (size_t)(seg - p);
			while (seg < e && *seg != '/')
				seg++;
			s_add(o, p, (size_t)(seg - p) > k ? (size_t)(seg - p) :
					 k);
			p = seg;
		}
	}
	if (o->p)
		o->p[o->n] = 0;
	else
		s_cat(o, "");
}

/* A reference -- a redirect's Location -- resolved against the address it
   came from, as RFC 3986 section 5.2 says: absolute, scheme-relative,
   absolute-path, query-only or relative. */
void ft_resolve(const char *base, const char *ref, str *o)
{
	ft_url b;
	str err, path, t;
	const char *q, *sl;

	o->n = 0;
	if (!strncasecmp(ref, "http://", 7) || !strncasecmp(ref, "https://", 8)) {
		s_cat(o, ref);
		return;
	}
	s_init(&err);
	memset(&b, 0, sizeof b);
	if (ft_urlsplit(base, &b, &err) != HIBR_OK) {
		s_cat(o, ref);
		s_free(&err);
		return;
	}
	s_free(&err);
	if (!strncmp(ref, "//", 2)) {
		s_cat(o, b.tls ? "https:" : "http:");
		s_cat(o, ref);
		ft_urlfree(&b);
		return;
	}
	s_init(&path);
	s_init(&t);
	q = strchr(b.path.p, '?');
	if (!*ref || *ref == '#') {
		s_cat(&path, b.path.p);
	} else if (*ref == '?') {
		s_add(&path, b.path.p, q ? (size_t)(q - b.path.p) : b.path.n);
		s_cat(&path, ref);
	} else {
		if (*ref == '/') {
			s_cat(&t, ref);
		} else {
			sl = b.path.p;
			{
				const char *x, *end = q ? q : b.path.p + b.path.n;

				for (x = b.path.p; x < end; x++)
					if (*x == '/')
						sl = x;
			}
			s_add(&t, b.path.p, (size_t)(sl - b.path.p) + 1);
			s_cat(&t, ref);
		}
		q = strpbrk(t.p, "?#");
		ft_dots(t.p, q ? (size_t)(q - t.p) : t.n, &path);
		if (q && *q == '?')
			s_add(&path, q, strcspn(q, "#"));
	}
	{
		char *h = strchr(path.p ? path.p : "", '#');

		if (h) {
			path.n = (size_t)(h - path.p);
			*h = 0;
		}
	}
	s_free(&b.path);
	s_init(&b.path);
	s_cat(&b.path, path.p && path.n ? path.p : "/");
	ft_urlstr(&b, o);
	ft_urlfree(&b);
	s_free(&path);
	s_free(&t);
}

/* Whether two addresses are the same origin: scheme, host and port. */
int ft_sameorigin(const char *a, const char *b)
{
	ft_url x, y;
	str err;
	int same;

	s_init(&err);
	memset(&x, 0, sizeof x);
	memset(&y, 0, sizeof y);
	same = ft_urlsplit(a, &x, &err) == HIBR_OK &&
	       ft_urlsplit(b, &y, &err) == HIBR_OK && x.tls == y.tls &&
	       !strcmp(x.host.p, y.host.p) && !strcmp(x.port.p, y.port.p);
	ft_urlfree(&x);
	ft_urlfree(&y);
	s_free(&err);
	return same;
}

/* An HTTP date, in any of the three forms HTTP allows, as seconds since
   the epoch; -1 when it cannot be read -- 0 is a date, the epoch, which is
   what a server sends to take a cookie away. */
long ft_httpdate(const char *p)
{
	static const char *mon[] = { "jan", "feb", "mar", "apr", "may", "jun",
				     "jul", "aug", "sep", "oct", "nov", "dec" };
	struct tm tm;
	int day = -1, mo = -1, yr = -1, h = -1, mi = 0, se = 0, i;
	const char *q;
	char *end;
	long v;

	memset(&tm, 0, sizeof tm);
	while (*p) {
		while (*p && strchr(" ,-\t", *p))
			p++;
		if (!*p)
			break;
		q = p;
		while (*q && !strchr(" ,-\t", *q))
			q++;
		if (*p >= '0' && *p <= '9') {
			if (memchr(p, ':', (size_t)(q - p))) {
				h = (int)strtol(p, &end, 10);
				if (*end == ':')
					mi = (int)strtol(end + 1, &end, 10);
				if (*end == ':')
					se = (int)strtol(end + 1, &end, 10);
			} else {
				v = strtol(p, &end, 10);
				if (day < 0 && q - p <= 2)
					day = (int)v;
				else
					yr = (int)v;
			}
		} else if (q - p >= 3) {
			for (i = 0; i < 12; i++)
				if (q - p == 3 && mo < 0 &&
				    !strncasecmp(p, mon[i], 3))
					mo = i;
		}
		p = q;
	}
	if (day < 1 || mo < 0 || yr < 0 || h < 0)
		return -1;
	if (yr < 100)
		yr += yr < 70 ? 2000 : 1900;
	tm.tm_year = yr - 1900;
	tm.tm_mon = mo;
	tm.tm_mday = day;
	tm.tm_hour = h;
	tm.tm_min = mi;
	tm.tm_sec = se;
	return (long)timegm(&tm);
}

/* Base64, for a Basic login. */
void ft_b64(str *o, const char *p, size_t n)
{
	static const char t[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz"
				"0123456789+/";
	size_t i;
	unsigned v;

	for (i = 0; i + 2 < n; i += 3) {
		v = (unsigned char)p[i] << 16 | (unsigned char)p[i + 1] << 8 |
		    (unsigned char)p[i + 2];
		s_ch(o, t[v >> 18]);
		s_ch(o, t[(v >> 12) & 63]);
		s_ch(o, t[(v >> 6) & 63]);
		s_ch(o, t[v & 63]);
	}
	if (n - i == 1) {
		v = (unsigned char)p[i] << 16;
		s_ch(o, t[v >> 18]);
		s_ch(o, t[(v >> 12) & 63]);
		s_cat(o, "==");
	} else if (n - i == 2) {
		v = (unsigned char)p[i] << 16 | (unsigned char)p[i + 1] << 8;
		s_ch(o, t[v >> 18]);
		s_ch(o, t[(v >> 12) & 63]);
		s_ch(o, t[(v >> 6) & 63]);
		s_ch(o, '=');
	}
}
