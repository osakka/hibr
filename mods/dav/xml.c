#define _GNU_SOURCE

#include "dv.h"
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

/* A namespace prefix in scope, and the depth that declared it. */
typedef struct dv_nsb dv_nsb;
struct dv_nsb {
	char *pre, *uri;
	int depth;
};

/* Append a code point as UTF-8. */
void dv_putu(str *o, unsigned long cp)
{
	if (cp < 0x80) {
		s_ch(o, (int)cp);
	} else if (cp < 0x800) {
		s_ch(o, (int)(0xC0 | (cp >> 6)));
		s_ch(o, (int)(0x80 | (cp & 0x3F)));
	} else if (cp < 0x10000) {
		s_ch(o, (int)(0xE0 | (cp >> 12)));
		s_ch(o, (int)(0x80 | ((cp >> 6) & 0x3F)));
		s_ch(o, (int)(0x80 | (cp & 0x3F)));
	} else if (cp < 0x110000) {
		s_ch(o, (int)(0xF0 | (cp >> 18)));
		s_ch(o, (int)(0x80 | ((cp >> 12) & 0x3F)));
		s_ch(o, (int)(0x80 | ((cp >> 6) & 0x3F)));
		s_ch(o, (int)(0x80 | (cp & 0x3F)));
	}
}

/* Append text with its character and entity references replaced. */
void dv_xtext(str *o, const char *p, const char *e)
{
	const char *semi;
	unsigned long cp;
	char *end;

	while (p < e) {
		if (*p != '&') {
			s_ch(o, *p++);
			continue;
		}
		semi = memchr(p, ';', (size_t)(e - p));
		if (!semi || semi - p > 12) {
			s_ch(o, *p++);
			continue;
		}
		if (p[1] == '#') {
			cp = p[2] == 'x' || p[2] == 'X' ? strtoul(p + 3, &end, 16) :
							  strtoul(p + 2, &end, 10);
			if (end == semi) {
				dv_putu(o, cp);
				p = semi + 1;
				continue;
			}
		} else if (!strncmp(p, "&lt;", 4)) {
			s_ch(o, '<');
			p += 4;
			continue;
		} else if (!strncmp(p, "&gt;", 4)) {
			s_ch(o, '>');
			p += 4;
			continue;
		} else if (!strncmp(p, "&amp;", 5)) {
			s_ch(o, '&');
			p += 5;
			continue;
		} else if (!strncmp(p, "&quot;", 6)) {
			s_ch(o, '"');
			p += 6;
			continue;
		} else if (!strncmp(p, "&apos;", 6)) {
			s_ch(o, '\'');
			p += 6;
			continue;
		}
		s_ch(o, *p++);
	}
}

/* Free an element and everything under it. */
void dv_xfree(dv_x *x)
{
	size_t i;

	if (!x)
		return;
	for (i = 0; i < x->kids.n; i++)
		dv_xfree(x->kids.p[i]);
	v_free(&x->kids);
	s_free(&x->text);
	free(x->ns);
	free(x->name);
	free(x->aname);
	free(x);
}

/* A new element. */
dv_x *dv_xnew(const char *ns, const char *name)
{
	dv_x *x = xm(sizeof *x);

	memset(x, 0, sizeof *x);
	x->ns = xs(ns);
	x->name = xs(name);
	s_init(&x->text);
	return x;
}

/* The namespace a prefix means at this point, "" when none. */
const char *dv_nsfind(vec *nsb, const char *pre)
{
	size_t i = nsb->n;
	dv_nsb *b;

	while (i--) {
		b = nsb->p[i];
		if (!strcmp(b->pre, pre))
			return b->uri;
	}
	return "";
}

/* Drop the prefixes declared deeper than a depth. */
void dv_nspop(vec *nsb, int depth)
{
	dv_nsb *b;

	while (nsb->n) {
		b = nsb->p[nsb->n - 1];
		if (b->depth <= depth)
			break;
		free(b->pre);
		free(b->uri);
		free(b);
		nsb->n--;
	}
}

/* Find a terminator after p, or the end. */
const char *dv_xskip(const char *p, const char *e, const char *t)
{
	size_t n = strlen(t);

	while (p + n <= e) {
		if (!memcmp(p, t, n))
			return p + n;
		p++;
	}
	return e;
}

/* Read a start tag at p (just past its '<'): its namespace declarations,
   then its name resolved against them. Returns where it ends, and sets
   empty for one that closes itself. */
const char *dv_xtag(const char *p, const char *e, vec *nsb, int depth,
		    dv_x **out, int *empty)
{
	const char *n0, *n1, *a0, *a1, *v0, *v1, *colon;
	str nm, pre, val, an;
	char q;
	dv_nsb *b;

	s_init(&nm);
	s_init(&pre);
	s_init(&val);
	s_init(&an);
	*empty = 0;
	n0 = p;
	while (p < e && !strchr(" \t\r\n/>", *p))
		p++;
	n1 = p;
	for (;;) {
		while (p < e && strchr(" \t\r\n", *p))
			p++;
		if (p >= e)
			break;
		if (*p == '/') {
			*empty = 1;
			p++;
			continue;
		}
		if (*p == '>') {
			p++;
			break;
		}
		a0 = p;
		while (p < e && !strchr(" \t\r\n=/>", *p))
			p++;
		a1 = p;
		while (p < e && strchr(" \t\r\n", *p))
			p++;
		if (p >= e || *p != '=')
			continue;
		p++;
		while (p < e && strchr(" \t\r\n", *p))
			p++;
		if (p >= e || (*p != '"' && *p != '\''))
			continue;
		q = *p++;
		v0 = p;
		while (p < e && *p != q)
			p++;
		v1 = p;
		if (p < e)
			p++;
		if (a1 - a0 >= 5 && !memcmp(a0, "xmlns", 5) &&
		    (a1 - a0 == 5 || a0[5] == ':')) {
			b = xm(sizeof *b);
			memset(b, 0, sizeof *b);
			pre.n = 0;
			if (a1 - a0 > 6)
				s_add(&pre, a0 + 6, (size_t)(a1 - a0 - 6));
			val.n = 0;
			dv_xtext(&val, v0, v1);
			b->pre = xs(pre.p ? pre.p : "");
			b->uri = xs(val.p ? val.p : "");
			b->depth = depth;
			v_add(nsb, b);
		} else if (a1 - a0 == 4 && !memcmp(a0, "name", 4)) {
			an.n = 0;
			dv_xtext(&an, v0, v1);
		}
	}
	colon = memchr(n0, ':', (size_t)(n1 - n0));
	pre.n = 0;
	if (colon) {
		s_add(&pre, n0, (size_t)(colon - n0));
		s_add(&nm, colon + 1, (size_t)(n1 - colon - 1));
	} else {
		s_add(&nm, n0, (size_t)(n1 - n0));
	}
	*out = dv_xnew(dv_nsfind(nsb, pre.p ? pre.p : ""), nm.p ? nm.p : "");
	if (an.p)
		(*out)->aname = xs(an.p);
	s_free(&an);
	s_free(&nm);
	s_free(&pre);
	s_free(&val);
	return p;
}

/* Parse an XML document into elements, with namespaces resolved; enough
   of XML for a WebDAV reply, lenient about what it does not need, and
   bounded in depth. The root returned is a holder; its child is the
   document's element. */
dv_x *dv_xparse(const char *p, size_t n)
{
	const char *e = p + n, *t;
	dv_x *root = dv_xnew("", "#doc"), *x, *top;
	vec stack = { 0, 0, 0 }, nsb = { 0, 0, 0 };
	int empty;

	v_add(&stack, root);
	while (p < e) {
		top = stack.p[stack.n - 1];
		if (*p != '<') {
			t = memchr(p, '<', (size_t)(e - p));
			if (!t)
				t = e;
			if (top != root)
				dv_xtext(&top->text, p, t);
			p = t;
			continue;
		}
		if (e - p >= 4 && !memcmp(p, "<!--", 4)) {
			p = dv_xskip(p + 4, e, "-->");
		} else if (e - p >= 9 && !memcmp(p, "<![CDATA[", 9)) {
			t = dv_xskip(p + 9, e, "]]>");
			if (top != root)
				s_add(&top->text, p + 9,
				      (size_t)(t - p - 9) -
					      (t - 3 >= p + 9 && !memcmp(t - 3, "]]>", 3) ?
						       3 : 0));
			p = t;
		} else if (p[1] == '?') {
			p = dv_xskip(p + 2, e, "?>");
		} else if (p[1] == '!') {
			t = memchr(p, '[', (size_t)(e - p));
			if (t && t < (const char *)memchr(p, '>', (size_t)(e - p)))
				p = dv_xskip(t, e, "]>");
			else
				p = dv_xskip(p, e, ">");
		} else if (p[1] == '/') {
			p = dv_xskip(p, e, ">");
			if (stack.n > 1) {
				stack.n--;
				dv_nspop(&nsb, (int)stack.n - 1);
			}
		} else {
			if (stack.n > DV_XDEPTH) {
				lg(HIBR_LERR, "dav: a reply nested deeper than %d",
				   DV_XDEPTH);
				break;
			}
			p = dv_xtag(p + 1, e, &nsb, (int)stack.n, &x, &empty);
			v_add(&top->kids, x);
			if (empty)
				dv_nspop(&nsb, (int)stack.n - 1);
			else
				v_add(&stack, x);
		}
	}
	dv_nspop(&nsb, -1);
	v_free(&nsb);
	v_free(&stack);
	return root;
}

/* The first child with a namespace and a name. */
dv_x *dv_xkid(dv_x *x, const char *ns, const char *name)
{
	size_t i;
	dv_x *k;

	if (!x)
		return 0;
	for (i = 0; i < x->kids.n; i++) {
		k = x->kids.p[i];
		if (!strcmp(k->name, name) && !strcmp(k->ns, ns))
			return k;
	}
	return 0;
}

/* An HTTP date, in any of the three forms HTTP allows, as seconds since
   the epoch; 0 when it cannot be read. */
long dv_httpdate(const char *p)
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
		return 0;
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
