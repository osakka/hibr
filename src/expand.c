#include "pri.h"
#include <ctype.h>
#include <pwd.h>
#include <dirent.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

void xvar2(sh *s, part *p, str *b, str *m, const char *v);

/* Render a signed number into the expansion arena. */
char *xnum(sh *s, long v)
{
	unsigned long u = v < 0 ? -(unsigned long)v : (unsigned long)v;
	unsigned long t = u;
	size_t d = 1, off = v < 0 ? 1 : 0;
	char *p;

	while (t >= 10) {
		t /= 10;
		d++;
	}
	p = ar_alloc(s->xa, d + off + 1);
	p[d + off] = 0;
	while (d--) {
		p[off + d] = (char)('0' + (u % 10));
		u /= 10;
	}
	if (v < 0)
		p[0] = '-';
	return p;
}

/* Join the positional parameters with a separator. */
char *xjoin(sh *s, const char *sep)
{
	str b;
	int i;
	char *r;

	s_init(&b);
	for (i = 0; i < s->ac; i++) {
		if (i)
			s_cat(&b, sep);
		s_cat(&b, s->av[i]);
	}
	r = ar_dup(s->xa, b.p ? b.p : "", b.n);
	s_free(&b);
	return r;
}

/* Resolve a parameter name to its value or NULL. */
const char *xval(sh *s, const char *k)
{
	if (k[0] && !k[1]) {
		switch (k[0]) {
		case '?':
			return xnum(s, s->st);
		case '#':
			return xnum(s, s->ac);
		case '$':
			return xnum(s, s->pid);
		case '0':
			return s->arg0 ? s->arg0 : "hibr";
		case '*':
		case '@':
			return xjoin(s, " ");
		case '-':
			return "";
		}
		if (isdigit((unsigned char)k[0])) {
			int i = k[0] - '0';
			return i >= 1 && i <= s->ac ? s->av[i - 1] : 0;
		}
	}
	{
		const char *r = hibr_get(s, k);
		if (r)
			return r;
	}
	if (!strcmp(k, "RANDOM"))
		return xnum(s, (long)(rand() % 32768));
	if (!strcmp(k, "SECONDS"))
		return xnum(s, (long)time(0) - s->t0);
	return 0;
}

/* Append text to the buffer with a uniform quoting mask. */
void xput(str *b, str *m, const char *t, size_t n, int q)
{
	if (!n)
		return;
	s_add(b, t, n);
	s_grow(m, n);
	memset(m->p + m->n, q ? 1 : 0, n);
	m->n += n;
	m->p[m->n] = 0;
}

/* Expand a word into a single joined string. */
char *xone(sh *s, word *w)
{
	vec *o;
	char *r;

	if (!w)
		return ar_dup(s->xa, "", 0);
	o = vb_get(s);
	xw(s, w, o, HIBR_XONE);
	r = o->n ? (char *)o->p[0] : ar_dup(s->xa, "", 0);
	vb_put(s, o);
	return r;
}

/* Expand a word into a match pattern, keeping quoted characters literal. */
char *xpat(sh *s, word *w)
{
	vec *o;
	char *r;

	if (!w)
		return ar_dup(s->xa, "", 0);
	o = vb_get(s);
	xw(s, w, o, HIBR_XPAT);
	r = o->n ? (char *)o->p[0] : ar_dup(s->xa, "", 0);
	vb_put(s, o);
	return r;
}

/* Strip a matching prefix or suffix from a value. */
char *xtrim(sh *s, const char *v, const char *pat, int op)
{
	size_t n = strlen(v), i;
	str t;
	char *r = 0;

	s_init(&t);
	if (op == V_RS || op == V_RL) {
		size_t best = 0;
		int got = 0;
		for (i = 0; i <= n; i++) {
			t.n = 0;
			s_add(&t, v, i);
			if (gmatch(pat, t.p ? t.p : "")) {
				if (!got || (op == V_RL ? i > best : i < best)) {
					best = i;
					got = 1;
				}
				if (op == V_RS)
					break;
			}
		}
		r = ar_dup(s->xa, v + (got ? best : 0), n - (got ? best : 0));
	} else {
		size_t best = 0;
		int got = 0;
		for (i = 0; i <= n; i++) {
			t.n = 0;
			s_add(&t, v + n - i, i);
			if (gmatch(pat, t.p ? t.p : "")) {
				if (!got || (op == V_SL ? i > best : i < best)) {
					best = i;
					got = 1;
				}
				if (op == V_SS)
					break;
			}
		}
		r = ar_dup(s->xa, v, n - (got ? best : 0));
	}
	s_free(&t);
	return r;
}

/* Replace the first or every match of a pattern inside a value. */
char *xrepl(sh *s, const char *v, const char *pat, const char *rep, int all)
{
	size_t n = strlen(v), i, j;
	str out, t;
	char *r;

	s_init(&out);
	s_init(&t);
	i = 0;
	while (i <= n) {
		size_t best = 0;
		int got = 0;
		for (j = n; j >= i; j--) {
			t.n = 0;
			s_add(&t, v + i, j - i);
			if (gmatch(pat, t.p ? t.p : "")) {
				best = j;
				got = 1;
				break;
			}
			if (j == i)
				break;
		}
		if (got && best > i) {
			s_cat(&out, rep);
			i = best;
			if (!all)
				break;
			continue;
		}
		if (got && !*pat) {
			s_cat(&out, rep);
			if (!all)
				break;
		}
		if (i < n)
			s_ch(&out, v[i]);
		i++;
	}
	if (i < n)
		s_add(&out, v + i, n - i);
	r = ar_dup(s->xa, out.p ? out.p : "", out.n);
	s_free(&out);
	s_free(&t);
	return r;
}

/* Replace a match anchored at one end of the value, longest first. */
char *xrepl_a(sh *s, const char *v, const char *pat, const char *rep, int end)
{
	size_t n = strlen(v), i, j;
	str t, out;
	char *r;

	s_init(&t);
	s_init(&out);
	if (!end) {
		for (j = n + 1; j-- > 0;) {
			t.n = 0;
			s_add(&t, v, j);
			if (gmatch(pat, t.p ? t.p : "")) {
				s_cat(&out, rep);
				s_add(&out, v + j, n - j);
				break;
			}
		}
		if (!out.n && !out.p)
			s_add(&out, v, n);
	} else {
		for (i = 0; i <= n; i++) {
			t.n = 0;
			s_add(&t, v + i, n - i);
			if (gmatch(pat, t.p ? t.p : "")) {
				s_add(&out, v, i);
				s_cat(&out, rep);
				break;
			}
		}
		if (i > n)
			s_add(&out, v, n);
	}
	r = ar_dup(s->xa, out.p ? out.p : "", out.n);
	s_free(&out);
	s_free(&t);
	return r;
}

/* True when a subscript selects every element. */
int xall(const char *i)
{
	return i && (!strcmp(i, "@") || !strcmp(i, "*"));
}

/* Resolve a subscript: numeric keys and expressions evaluate, names stay. */
char *xkey(sh *s, char *t)
{
	const char *v;
	size_t i;
	int digits = 1;

	if (!t || !*t)
		return t;
	for (i = 0; t[i]; i++)
		if (!isdigit((unsigned char)t[i]))
			digits = 0;
	if (digits)
		return t;
	if (strpbrk(t, "+-*/%()<>=!&|^ "))
		return xnum(s, ax_run(s, t));
	if (isname(t)) {
		v = hibr_get(s, t);
		if (v && *v) {
			for (i = 0; v[i]; i++)
				if (!isdigit((unsigned char)v[i]))
					return t;
			return ar_dup(s->xa, v, strlen(v));
		}
	}
	return t;
}

/* Resolve a negative subscript against the highest key at that level. */
char *xneg(sh *s, const char *nm, char **ks, int lvl)
{
	vec *l = vb_get(s);
	long top = -1, k, want = atol(ks[lvl]);
	size_t j;

	v_list(s, nm, ks, lvl, l, 1);
	for (j = 0; j < l->n; j++) {
		k = atol((char *)l->p[j]);
		if (k > top)
			top = k;
	}
	vb_put(s, l);
	k = top + 1 + want;
	lg(HIBR_LTRC, "subscript %ld counts back to %ld", want, k);
	return k < 0 ? ks[lvl] : xnum(s, k);
}

/* Expand a part's subscript chain, dropping a trailing @ or *. */
int xkeys(sh *s, part *p, char ***out, int *all)
{
	word *w;
	int n = 0, i = 0;
	char **ks;

	*all = 0;
	for (w = p->idx; w; w = w->nx)
		n++;
	ks = n ? ar_alloc(s->xa, (size_t)n * sizeof *ks) : 0;
	for (w = p->idx; w; w = w->nx) {
		ks[i] = xone(s, w);
		if (!xall(ks[i]) && !w_hasq(w)) {
			ks[i] = xkey(s, ks[i]);
			if (ks[i][0] == '-' && isdigit((unsigned char)ks[i][1]))
				ks[i] = xneg(s, p->t, ks, i);
		}
		i++;
	}
	if (n && xall(ks[n - 1])) {
		*all = 1;
		n--;
	}
	*out = ks;
	return n;
}

/* Join everything below a subscript path with a separator. */
char *xajoin(sh *s, const char *k, char **ks, int nk, const char *sep, int keys)
{
	vec *o = vb_get(s);
	str b;
	size_t i;
	char *r;

	v_list(s, k, ks, nk, o, keys);
	s_init(&b);
	for (i = 0; i < o->n; i++) {
		if (i)
			s_cat(&b, sep);
		s_cat(&b, (char *)o->p[i]);
	}
	r = ar_dup(s->xa, b.p ? b.p : "", b.n);
	s_free(&b);
	vb_put(s, o);
	return r;
}

/* Expand one variable part into the output buffer. */
void xvar(sh *s, part *p, str *b, str *m)
{
	const char *v;
	char *a;

	if (p->arr) {
		char **ks;
		int all, nk = xkeys(s, p, &ks, &all);
		if (p->op == V_LEN) {
			char *c = xnum(s, (long)(all ? v_count(s, p->t, ks, nk) :
						 (long)strlen(v_getp(s, p->t, ks,
								     nk) ?
							      v_getp(s, p->t, ks,
								     nk) : "")));
			xput(b, m, c, strlen(c), 1);
			return;
		}
		if (p->op == V_KEYS) {
			a = xajoin(s, p->t, ks, nk, " ", 1);
			xput(b, m, a, strlen(a), p->q || s->strict);
			return;
		}
		if (all && p->op == V_SUBSTR) {
			vec *lst = vb_get(s);
			char *spec = xone(s, p->arg), *colon = strchr(spec, ':');
			long off, len, k2;
			str j;
			v_list(s, p->t, ks, nk, lst, 0);
			if (colon)
				*colon = 0;
			off = ax_run(s, spec);
			if (off < 0)
				off += (long)lst->n;
			if (off < 0)
				off = 0;
			len = colon ? ax_run(s, colon + 1) : (long)lst->n - off;
			s_init(&j);
			for (k2 = off; k2 < off + len && k2 < (long)lst->n; k2++) {
				if (j.n)
					s_ch(&j, ' ');
				s_cat(&j, (char *)lst->p[k2]);
			}
			vb_put(s, lst);
			xput(b, m, j.p ? j.p : "", j.n, p->q || s->strict);
			s_free(&j);
			return;
		}
		if (all)
			v = v_find(s, p->t) ? xajoin(s, p->t, ks, nk, " ", 0) : 0;
		else
			v = v_getp(s, p->t, ks, nk);
	} else if (p->op == V_KEYS) {
		a = xajoin(s, p->t, 0, 0, " ", 1);
		xput(b, m, a, strlen(a), p->q || s->strict);
		return;
	} else if (p->op == V_NAMES) {
		vec *nm = vb_get(s);
		str j;
		size_t i;
		v_names(s, p->t, nm);
		if (nm->n > 1)
			qsort(nm->p, nm->n, sizeof *nm->p, gcmp);
		s_init(&j);
		for (i = 0; i < nm->n; i++) {
			if (i)
				s_ch(&j, ' ');
			s_cat(&j, (char *)nm->p[i]);
			free(nm->p[i]);
		}
		xput(b, m, j.p ? j.p : "", j.n, p->q || s->strict);
		s_free(&j);
		vb_put(s, nm);
		return;
	} else if (p->op & V_INDF) {
		const char *r = xval(s, p->t);
		part q2 = *p;
		q2.op = p->op & ~V_INDF;
		lg(HIBR_LTRC, "indirect with modifier via %s", p->t);
		xvar2(s, &q2, b, m, r && *r ? xbyname(s, r) : 0);
		return;
	} else if (p->op == V_IND) {
		const char *r = xval(s, p->t);
		if (!r || !*r)
			return;
		lg(HIBR_LTRC, "indirect: %s names %s", p->t, r);
		v = xbyname(s, r);
		xvar2(s, p, b, m, v);
		return;
	} else {
		v = xval(s, p->t);
	}
	xvar2(s, p, b, m, v);
}

/* True for a byte that needs no escape in a backslash quoted word. */
int xqsafe(int c, int first)
{
	if (isalnum(c) || strchr("_./-:=@+%", c))
		return 1;
	return !first && (c == '~' || c == '#');
}

/* Quote a value so that reading it back gives the same string. */
char *xquote(sh *s, const char *v, int bs)
{
	str o;
	const char *p;
	unsigned c;
	int ctl = 0;
	char *r;

	for (p = v; *p; p++)
		if ((unsigned char)*p < 32 || (unsigned char)*p == 127)
			ctl = 1;
	s_init(&o);
	if (!ctl && bs && *v) {
		for (p = v; *p; p++) {
			if (!xqsafe((unsigned char)*p, p == v))
				s_ch(&o, '\\');
			s_ch(&o, *p);
		}
	} else if (!ctl) {
		s_ch(&o, '\'');
		for (p = v; *p; p++) {
			if (*p == '\'')
				s_cat(&o, "'\\''");
			else
				s_ch(&o, *p);
		}
		s_ch(&o, '\'');
	} else {
		s_cat(&o, "$'");
		for (p = v; *p; p++) {
			c = (unsigned char)*p;
			if (*p == '\n')
				s_cat(&o, "\\n");
			else if (*p == '\t')
				s_cat(&o, "\\t");
			else if (*p == '\r')
				s_cat(&o, "\\r");
			else if (*p == '\\' || *p == '\'') {
				s_ch(&o, '\\');
				s_ch(&o, *p);
			} else if (c < 32 || c == 127) {
				s_ch(&o, '\\');
				s_ch(&o, (char)('0' + ((c >> 6) & 7)));
				s_ch(&o, (char)('0' + ((c >> 3) & 7)));
				s_ch(&o, (char)('0' + (c & 7)));
			} else
				s_ch(&o, *p);
		}
		s_ch(&o, '\'');
	}
	r = ar_dup(s->xa, o.p ? o.p : "", o.n);
	s_free(&o);
	return r;
}

/* Read the variable a piece of text names, subscripts and all. */
const char *xbyname(sh *s, const char *r)
{
	const char *br = strchr(r, '[');
	const char *v;
	vec *ks;
	str nm;
	char *q, *end;

	if (!br)
		return xval(s, r);
	s_init(&nm);
	s_add(&nm, r, (size_t)(br - r));
	ks = vb_get(s);
	q = (char *)br + 1;
	while (q && *q) {
		end = strchr(q, ']');
		if (!end)
			break;
		*end = 0;
		v_add(ks, xkey(s, ar_dup(s->xa, q, strlen(q))));
		*end = ']';
		q = end + 1;
		if (*q == '[')
			q++;
		else
			break;
	}
	v = hibr_getp(s, nm.p ? nm.p : "", (char **)ks->p, (int)ks->n);
	lg(HIBR_LTRC, "indirect %s reached %s", r, v ? v : "(unset)");
	vb_put(s, ks);
	s_free(&nm);
	return v;
}

/* Apply the parameter modifier to a resolved value. */
void xvar2(sh *s, part *p, str *b, str *m, const char *v)
{
	char *a;
	int set = v != 0;
	int ok = set && (!p->col || *v);

	switch (p->op) {
	case V_XQ:
		a = xquote(s, v ? v : "", 0);
		xput(b, m, a, strlen(a), 1);
		return;
	case V_XE: {
		str t;
		s_init(&t);
		pf_esc(&t, v ? v : "", 0);
		xput(b, m, t.p ? t.p : "", t.n, p->q || s->strict);
		s_free(&t);
		return;
	}
	case V_LEN:
		a = xnum(s, v ? (long)strlen(v) : 0);
		xput(b, m, a, strlen(a), 1);
		return;
	case V_DEF:
		if (!ok) {
			a = xone(s, p->arg);
			xput(b, m, a, strlen(a), p->q || s->strict);
			return;
		}
		break;
	case V_ASG:
		if (!ok) {
			a = xone(s, p->arg);
			hibr_set(s, p->t, a, 0);
			xput(b, m, a, strlen(a), p->q || s->strict);
			return;
		}
		break;
	case V_ERR:
		if (!ok) {
			a = xone(s, p->arg);
			lg(HIBR_LERR, "%s: %s", p->t, *a ? a : "parameter not set");
			s->st = 1;
			s->stop = 1;
			if (!s->it)
				s->quit = 1;
			return;
		}
		break;
	case V_ALT:
		if (ok) {
			a = xone(s, p->arg);
			xput(b, m, a, strlen(a), p->q || s->strict);
		}
		return;
	case V_RS:
	case V_RL:
	case V_SS:
	case V_SL:
		if (!v)
			return;
		a = xtrim(s, v, xpat(s, p->arg), p->op);
		xput(b, m, a, strlen(a), p->q || s->strict);
		return;
	case V_UP:
	case V_UPALL:
	case V_LOW:
	case V_LOWALL: {
		size_t i;
		if (!v)
			return;
		a = ar_dup(s->xa, v, strlen(v));
		for (i = 0; a[i]; i++) {
			if (p->op == V_UP || p->op == V_UPALL)
				a[i] = (char)toupper((unsigned char)a[i]);
			else
				a[i] = (char)tolower((unsigned char)a[i]);
			if (p->op == V_UP || p->op == V_LOW)
				break;
		}
		xput(b, m, a, strlen(a), p->q || s->strict);
		return;
	}
	case V_SUBSTR: {
		char *spec = xone(s, p->arg), *colon = strchr(spec, ':');
		long off, len, n;
		if (!v)
			return;
		if (colon)
			*colon = 0;
		off = ax_run(s, spec);
		n = (long)strlen(v);
		if (off < 0)
			off += n;
		if (off < 0)
			off = 0;
		if (off > n)
			off = n;
		len = colon ? ax_run(s, colon + 1) : n - off;
		if (len < 0)
			len = n - off + len;
		if (len < 0)
			len = 0;
		if (off + len > n)
			len = n - off;
		xput(b, m, v + off, (size_t)len, p->q || s->strict);
		return;
	}
	case V_SUB:
	case V_SUBA:
		if (!v)
			return;
		a = xrepl(s, v, xpat(s, p->arg),
			  p->arg ? xone(s, p->arg->nx) : "", p->op == V_SUBA);
		xput(b, m, a, strlen(a), p->q || s->strict);
		return;
	case V_SUBP:
	case V_SUBF:
		if (!v)
			return;
		a = xrepl_a(s, v, xpat(s, p->arg),
			    p->arg ? xone(s, p->arg->nx) : "",
			    p->op == V_SUBF);
		xput(b, m, a, strlen(a), p->q || s->strict);
		return;
	}
	if (v) {
		xput(b, m, v, strlen(v), p->q || s->strict);
		return;
	}
	if (s->uset && p->op == V_NONE) {
		lg(HIBR_LERR, "%s: unbound variable", p->t);
		s->st = 1;
		s->stop = 1;
		if (!s->it)
			s->quit = 1;
	}
}

/* Expand one word part into the output buffer. */
void xpart(sh *s, part *p, str *b, str *m)
{
	char *a;

	switch (p->k) {
	case P_TXT:
		xput(b, m, p->t, p->n, p->q);
		return;
	case P_VAR:
		xvar(s, p, b, m);
		return;
	case P_CMD:
		a = xcap(s, p->t);
		xput(b, m, a, strlen(a), p->q || s->strict);
		return;
	case P_PSUB:
		a = xpsub(s, p);
		xput(b, m, a, strlen(a), 1);
		return;
	case P_ARI:
		a = xnum(s, ax_text(s, p->t));
		xput(b, m, a, strlen(a), p->q || s->strict);
		return;
	}
}

/* The alternative list of an extended group, as a run of patterns. */
const char *gnext(const char *a, const char *close)
{
	int d = 0;

	for (; a < close; a++) {
		if (*a == '(')
			d++;
		else if (*a == ')')
			d--;
		else if (*a == '|' && !d)
			return a;
	}
	return close;
}

/* Match !(list) by finding a split the list does not cover. */
int gneg(const char *body, const char *close, const char *rest, const char *t)
{
	const char *a, *b;
	size_t i, n = strlen(t);
	str x;
	int bad;

	for (i = 0; i <= n; i++) {
		if (!gmatch(rest, t + i))
			continue;
		bad = 0;
		for (a = body; a <= close && !bad; a = b + 1) {
			b = gnext(a, close);
			s_init(&x);
			s_add(&x, a, (size_t)(b - a));
			{
				str y;
				s_init(&y);
				s_add(&y, t, i);
				if (gmatch(x.p ? x.p : "", y.p ? y.p : ""))
					bad = 1;
				s_free(&y);
			}
			s_free(&x);
			if (b >= close)
				break;
		}
		if (!bad)
			return 1;
	}
	return 0;
}

/* Match one extended group -- ?( *( +( @( !( -- and what follows it. */
int gext(const char *p, const char *t)
{
	int op = *p, d = 1, ok = 0;
	const char *body = p + 2, *close, *rest, *a, *b;
	str x;

	for (close = body; *close; close++) {
		if (*close == '(')
			d++;
		else if (*close == ')' && !--d)
			break;
	}
	if (!*close)
		return 0;
	rest = close + 1;
	if (op == '!')
		return gneg(body, close, rest, t);
	if ((op == '?' || op == '*') && gmatch(rest, t))
		return 1;
	for (a = body; a <= close && !ok; a = b + 1) {
		b = gnext(a, close);
		if (b > a || op == '?' || op == '@') {
			s_init(&x);
			s_add(&x, a, (size_t)(b - a));
			if ((op == '*' || op == '+') && b > a) {
				s_ch(&x, '*');
				s_ch(&x, '(');
				s_add(&x, body, (size_t)(close - body));
				s_ch(&x, ')');
			}
			s_cat(&x, rest);
			ok = gmatch(x.p ? x.p : "", t);
			s_free(&x);
		}
		if (b >= close)
			break;
	}
	return ok;
}

/* Match a pattern against text with escapes, star and class support. */
int gmatch(const char *p, const char *t)
{
	while (*p) {
		if (p[1] == '(' && (*p == '?' || *p == '*' || *p == '+' ||
				    *p == '@' || *p == '!'))
			return gext(p, t);
		if (*p == '*') {
			p++;
			if (!*p)
				return 1;
			while (*t) {
				if (gmatch(p, t))
					return 1;
				t++;
			}
			return gmatch(p, t);
		}
		if (!*t)
			return 0;
		if (*p == '?') {
			p++;
			t++;
			continue;
		}
		if (*p == '[') {
			int neg = 0, hit = 0;
			const char *q = p + 1;
			if (*q == '!' || *q == '^') {
				neg = 1;
				q++;
			}
			for (; *q && *q != ']'; q++) {
				if (q[1] == '-' && q[2] && q[2] != ']') {
					if ((unsigned char)*t >= (unsigned char)q[0] &&
					    (unsigned char)*t <= (unsigned char)q[2])
						hit = 1;
					q += 2;
					continue;
				}
				if (*q == *t)
					hit = 1;
			}
			if (!*q)
				return 0;
			if (hit == neg)
				return 0;
			p = q + 1;
			t++;
			continue;
		}
		if (*p == '\\' && p[1])
			p++;
		if (*p++ != *t++)
			return 0;
	}
	return !*t;
}

/* Remove pattern escapes from a path segment. */
char *gplain(sh *s, const char *p, size_t n)
{
	str b;
	size_t i;
	char *r;

	s_init(&b);
	for (i = 0; i < n; i++) {
		if (p[i] == '\\' && i + 1 < n)
			i++;
		s_ch(&b, p[i]);
	}
	r = ar_dup(s->xa, b.p ? b.p : "", b.n);
	s_free(&b);
	return r;
}

/* True if a segment carries unescaped pattern characters. */
int gmeta(const char *p, size_t n)
{
	size_t i, j;

	for (i = 0; i < n; i++) {
		if (p[i] == '\\') {
			i++;
			continue;
		}
		if (p[i] == '*' || p[i] == '?')
			return 1;
		if (p[i] != '[')
			continue;
		for (j = i + 2; j < n; j++) {
			if (p[j] == '\\') {
				j++;
				continue;
			}
			if (p[j] == ']')
				return 1;
		}
	}
	return 0;
}

/* True if a field holds an unquoted pattern worth matching. */
int xmeta(const char *t, const char *mk, size_t n)
{
	size_t i, j;

	for (i = 0; i < n; i++) {
		if (mk[i])
			continue;
		if ((t[i] == '?' || t[i] == '*' || t[i] == '+' ||
		     t[i] == '@' || t[i] == '!') && i + 1 < n &&
		    t[i + 1] == '(' && !mk[i + 1])
			return 1;
		if (t[i] == '*' || t[i] == '?')
			return 1;
		if (t[i] != '[')
			continue;
		for (j = i + 2; j < n; j++)
			if (!mk[j] && t[j] == ']')
				return 1;
	}
	return 0;
}

/* Order directory entries for stable glob output. */
int gcmp(const void *a, const void *b)
{
	return strcmp(*(char *const *)a, *(char *const *)b);
}

/* Walk a path pattern against the filesystem. */
void gwalk(sh *s, str *dir, const char *pat, vec *out, int *cnt)
{
	const char *sl = strchr(pat, '/');
	size_t sn = sl ? (size_t)(sl - pat) : strlen(pat);
	struct stat st;
	DIR *d;
	struct dirent *de;
	vec names = { 0, 0, 0 };
	size_t i, keep = dir->n;
	char *seg;

	if (sn == 2 && pat[0] == '*' && pat[1] == '*') {
		vec subs = { 0, 0, 0 };
		size_t q;
		if (sl) {
			gwalk(s, dir, sl + 1, out, cnt);
		} else {
			d = opendir(dir->n ? dir->p : ".");
			if (d) {
				while ((de = readdir(d))) {
					if (de->d_name[0] == '.')
						continue;
					s_cat(dir, de->d_name);
					v_add(out, ar_dup(s->xa, dir->p, dir->n));
					(*cnt)++;
					dir->n = keep;
					dir->p[keep] = 0;
				}
				closedir(d);
			}
		}
		d = opendir(dir->n ? dir->p : ".");
		if (!d)
			return;
		while ((de = readdir(d))) {
			if (de->d_name[0] == '.')
				continue;
			s_cat(dir, de->d_name);
			if (!lstat(dir->p, &st) && S_ISDIR(st.st_mode))
				v_add(&subs, xs(de->d_name));
			dir->n = keep;
			dir->p[keep] = 0;
		}
		closedir(d);
		if (subs.n > 1)
			qsort(subs.p, subs.n, sizeof *subs.p, gcmp);
		for (q = 0; q < subs.n; q++) {
			s_cat(dir, (char *)subs.p[q]);
			s_ch(dir, '/');
			gwalk(s, dir, pat, out, cnt);
			dir->n = keep;
			dir->p[keep] = 0;
			free(subs.p[q]);
		}
		v_free(&subs);
		return;
	}
	if (!gmeta(pat, sn)) {
		seg = gplain(s, pat, sn);
		s_cat(dir, seg);
		if (sl) {
			s_ch(dir, '/');
			gwalk(s, dir, sl + 1, out, cnt);
		} else if (stat(dir->p, &st) == 0) {
			v_add(out, ar_dup(s->xa, dir->p, dir->n));
			(*cnt)++;
		}
		dir->n = keep;
		if (dir->p)
			dir->p[keep] = 0;
		return;
	}
	seg = ar_dup(s->xa, pat, sn);
	d = opendir(dir->n ? dir->p : ".");
	if (!d) {
		lg(HIBR_LTRC, "glob: cannot open %s", dir->n ? dir->p : ".");
		return;
	}
	while ((de = readdir(d))) {
		if (de->d_name[0] == '.' && seg[0] != '.')
			continue;
		if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, ".."))
			continue;
		if (!gmatch(seg, de->d_name))
			continue;
		v_add(&names, xs(de->d_name));
	}
	closedir(d);
	if (names.n > 1)
		qsort(names.p, names.n, sizeof *names.p, gcmp);
	for (i = 0; i < names.n; i++) {
		s_cat(dir, (char *)names.p[i]);
		if (sl) {
			s_ch(dir, '/');
			gwalk(s, dir, sl + 1, out, cnt);
		} else {
			v_add(out, ar_dup(s->xa, dir->p, dir->n));
			(*cnt)++;
		}
		dir->n = keep;
		if (dir->p)
			dir->p[keep] = 0;
		free(names.p[i]);
	}
	v_free(&names);
}

/* Add one expanded field whose every byte came from a quoted part. */
void xoutq(sh *s, vec *out, vec *outm, const char *t, size_t n)
{
	char *mk;

	if (!outm) {
		v_add(out, ar_dup(s->xa, t ? t : "", n));
		return;
	}
	mk = ar_alloc(s->xa, n + 1);
	memset(mk, 1, n);
	xout(s, out, outm, t, mk, n);
}

/* Pad a mask vector with unquoted entries until it matches its field vector. */
void xpad(vec *out, vec *outm)
{
	if (!outm)
		return;
	while (outm->n < out->n)
		v_add(outm, 0);
}

/* Resolve a subscript, treating one holding a quoted byte as a literal key. */
char *xkey_q(sh *s, char *t, const char *mk)
{
	size_t i;

	if (mk)
		for (i = 0; t[i]; i++)
			if (mk[i]) {
				lg(HIBR_LTRC, "subscript '%s' quoted, literal", t);
				return t;
			}
	return xkey(s, t);
}

/* Expand one field, globbing it when it holds unquoted patterns. */
void xfield(sh *s, const char *t, const char *mk, size_t n, vec *out,
	    vec *outm)
{
	str pat, dir;
	size_t i;
	int meta = 0, cnt = 0;

	if (!xmeta(t, mk, n)) {
		xout(s, out, outm, t, mk, n);
		return;
	}
	meta = 1;
	s_init(&pat);
	for (i = 0; i < n; i++) {
		if (mk[i] && (t[i] == '*' || t[i] == '?' || t[i] == '[' || t[i] == '\\'))
			s_ch(&pat, '\\');
		s_ch(&pat, t[i]);
	}
	if (!meta) {
		xout(s, out, outm, t, mk, n);
		s_free(&pat);
		return;
	}
	s_init(&dir);
	if (pat.p[0] == '/') {
		s_ch(&dir, '/');
		gwalk(s, &dir, pat.p + 1, out, &cnt);
	} else {
		gwalk(s, &dir, pat.p, out, &cnt);
	}
	if (!cnt && (s->sopt & O_FAILGLOB)) {
		lg(HIBR_LERR, "no match: %.*s", (int)n, t);
		s->xerr = 1;
	} else if (!cnt && (s->sopt & O_NULLGLOB))
		lg(HIBR_LTRC, "no match for %.*s, dropping the word", (int)n,
		   t);
	else if (!cnt)
		xout(s, out, outm, t, mk, n);
	else {
		xpad(out, outm);
		lg(HIBR_LTRC, "glob matched %d paths", cnt);
	}
	s_free(&dir);
	s_free(&pat);
}

/* Split an expanded buffer into fields on unquoted IFS characters. */
void xsplit(sh *s, str *b, str *m, vec *out, vec *outm)
{
	const char *ifs = sh_ifs(s);
	size_t i = 0, st;

	if (!ifs)
		ifs = " \t\n";
	while (i < b->n) {
		while (i < b->n && !m->p[i] && *ifs && strchr(ifs, b->p[i]))
			i++;
		if (i >= b->n)
			break;
		st = i;
		while (i < b->n && !(!m->p[i] && *ifs && strchr(ifs, b->p[i])))
			i++;
		xfield(s, b->p + st, m->p + st, i - st, out, outm);
	}
}

/* True if a word carries any quoted part. */
int w_hasq(word *w)
{
	part *p;

	for (p = w->p; p; p = p->nx)
		if (p->q)
			return 1;
	return 0;
}

/* Expand a leading tilde in place. */
/* What a ~ prefix names: home, a user, the working directory, the stack. */
const char *xtilde1(sh *s, const char *t, size_t n)
{
	struct passwd *pw;
	str who;
	const char *r = 0;
	long k;

	if (!n)
		return hibr_get(s, "HOME");
	if (n == 1 && *t == '+')
		return hibr_get(s, "PWD");
	if (n == 1 && *t == '-')
		return hibr_get(s, "OLDPWD");
	if (isdigit((unsigned char)*t) || ((*t == '+' || *t == '-') && n > 1)) {
		int back = *t == '-';
		k = atol(t + (*t == '+' || *t == '-' ? 1 : 0));
		return dir_at(s, back ? -(k + 1) : k);
	}
	s_init(&who);
	s_add(&who, t, n);
	pw = getpwnam(who.p ? who.p : "");
	if (pw)
		r = ar_dup(s->xa, pw->pw_dir, strlen(pw->pw_dir));
	s_free(&who);
	return r;
}

/* Expand a leading ~ in a word, if it has one. */
void xtilde(sh *s, word *w, str *b, str *m)
{
	part *p = w->p;
	const char *h;
	size_t n = 0;

	if (!p || p->k != P_TXT || p->q || !p->n || p->t[0] != '~')
		return;
	while (1 + n < p->n && p->t[1 + n] != '/')
		n++;
	h = xtilde1(s, p->t + 1, n);
	if (!h)
		return;
	lg(HIBR_LTRC, "~%.*s is %s", (int)n, p->t + 1, h);
	xput(b, m, h, strlen(h), 1);
	p->t += 1 + n;
	p->n -= 1 + n;
}

/* Expand a word into zero or more fields. */
void xwm(sh *s, word *w, vec *out, int fl, vec *outm)
{
	str *b, *m;
	part *p;
	char *sv;
	size_t svn;

	if (!w)
		return;
	if (w->p && w->p->nx == 0 && w->p->k == P_VAR &&
	    (w->p->q || s->strict) && !(fl & (HIBR_XONE | HIBR_XPAT))) {
		part *p0 = w->p;
		int i;
		if (!p0->arr && p0->t[0] == '@' && !p0->t[1]) {
			for (i = 0; i < s->ac; i++)
				xoutq(s, out, outm, s->av[i],
				      strlen(s->av[i]));
			return;
		}
		if (p0->arr && p0->op == V_SUBSTR) {
			char **ks;
			int all, nk = xkeys(s, p0, &ks, &all);
			vec *lst;
			char *spec, *colon;
			long off, len, k2;
			if (!all)
				goto normal;
			lst = vb_get(s);
			v_list(s, p0->t, ks, nk, lst, 0);
			spec = xone(s, p0->arg);
			colon = strchr(spec, ':');
			if (colon)
				*colon = 0;
			off = ax_run(s, spec);
			if (off < 0)
				off += (long)lst->n;
			if (off < 0)
				off = 0;
			len = colon ? ax_run(s, colon + 1) : (long)lst->n - off;
			for (k2 = off; k2 < off + len && k2 < (long)lst->n; k2++)
				xoutq(s, out, outm, (char *)lst->p[k2],
				      strlen((char *)lst->p[k2]));
			vb_put(s, lst);
			return;
		}
		if (p0->arr && (p0->op == V_NONE || p0->op == V_KEYS)) {
			char **ks;
			int all, nk = xkeys(s, p0, &ks, &all);
			vec *lst;
			size_t k;
			word *lw = p0->idx;
			while (lw && lw->nx)
				lw = lw->nx;
			if (!all || strcmp(xone(s, lw), "@"))
				goto normal;
			lst = vb_get(s);
			v_list(s, p0->t, ks, nk, lst, p0->op == V_KEYS);
			for (k = 0; k < lst->n; k++)
				xoutq(s, out, outm, (char *)lst->p[k],
				      strlen((char *)lst->p[k]));
			vb_put(s, lst);
			return;
		}
	}
	if (0) {
normal:
		;
	}
	if ((fl & HIBR_XPAT) && w->p && !w->p->nx && w->p->k == P_TXT &&
	    !w->p->q && w->p->n && w->p->t[0] != '~') {
		xout(s, out, outm, w->p->t, 0, w->p->n);
		return;
	}
	if (w->p && !w->p->nx && w->p->k == P_VAR && !w->p->q && !w->p->arr &&
	    w->p->op == V_NONE && !s->uset && !(fl & HIBR_XPAT)) {
		const char *v = xval(s, w->p->t);
		size_t k, n;
		if (fl & HIBR_XONE) {
			xout(s, out, outm, v ? v : "", 0,
			     v ? strlen(v) : 0);
			return;
		}
		if (!v)
			return;
		n = strlen(v);
		if (!s->strict) {
			for (k = 0; k < n; k++)
				if (w_meta[(unsigned char)v[k]])
					break;
			if (k == n && !sh_ifs(s)) {
				if (n)
					xout(s, out, outm, v, 0, n);
				return;
			}
		}
	}
	if (w->p && !w->p->nx && w->p->k == P_TXT && !(fl & HIBR_XPAT)) {
		part *p0 = w->p;
		size_t k;
		int plain = 1;
		if (p0->q || (fl & HIBR_XONE)) {
			if (p0->q)
				xoutq(s, out, outm, p0->t, p0->n);
			else
				xout(s, out, outm, p0->t, 0, p0->n);
			return;
		}
		for (k = 0; k < p0->n && plain; k++)
			if (strchr("*?[~ \t\n", p0->t[k]))
				plain = 0;
		if (plain && p0->n && !sh_ifs(s)) {
			xout(s, out, outm, p0->t, 0, p0->n);
			return;
		}
	}
	b = sb_get(s);
	m = sb_get(s);
	sv = w->p ? w->p->t : 0;
	svn = w->p ? w->p->n : 0;
	xtilde(s, w, b, m);
	for (p = w->p; p; p = p->nx)
		xpart(s, p, b, m);
	if (w->p) {
		w->p->t = sv;
		w->p->n = svn;
	}
	if (fl & HIBR_XPAT) {
		str *q = sb_get(s);
		size_t i;
		for (i = 0; i < b->n; i++) {
			if (m->p[i] && (b->p[i] == '*' || b->p[i] == '?' ||
					b->p[i] == '[' || b->p[i] == '\\'))
				s_ch(q, '\\');
			s_ch(q, b->p[i]);
		}
		xout(s, out, outm, q->p ? q->p : "", 0, q->n);
		sb_put(s, q);
	} else if (fl & HIBR_XONE) {
		xout(s, out, outm, b->p ? b->p : "", m->p, b->n);
	} else if (!b->n) {
		if (w_hasq(w))
			xout(s, out, outm, "", 0, 0);
	} else {
		xsplit(s, b, m, out, outm);
	}
	sb_put(s, m);
	sb_put(s, b);
}

/* Expand a word into one field, reporting which of its bytes were quoted. */
char *xone_q(sh *s, word *w, char **mask)
{
	vec *o, *om;
	char *r;

	*mask = 0;
	if (!w)
		return ar_dup(s->xa, "", 0);
	o = vb_get(s);
	om = vb_get(s);
	xwm(s, w, o, HIBR_XONE, om);
	r = o->n ? (char *)o->p[0] : ar_dup(s->xa, "", 0);
	if (om->n == o->n && om->n)
		*mask = (char *)om->p[0];
	vb_put(s, om);
	vb_put(s, o);
	return r;
}

/* Copy a word, replacing one text part with new text. */
word *br_make(sh *s, word *w, part *tgt, const char *txt, size_t tn)
{
	word *nw = ar_alloc(s->xa, sizeof *nw);
	part *p, *c, **t = &nw->p;

	for (p = w->p; p && p != tgt; p = p->nx) {
		c = ar_alloc(s->xa, sizeof *c);
		*c = *p;
		c->nx = 0;
		*t = c;
		t = &c->nx;
	}
	c = ar_alloc(s->xa, sizeof *c);
	c->k = P_TXT;
	c->q = 0;
	c->t = ar_dup(s->xa, txt, tn);
	c->n = tn;
	c->nx = tgt ? tgt->nx : 0;
	*t = c;
	return nw;
}

/* Generate the items of a {a..b} or {1..9..2} range. */
int br_range(sh *s, const char *b, size_t n, vec *out)
{
	str t;
	const char *d1, *d2;
	long lo, hi, step = 1, v;
	int pad = 0, alpha = 0;
	char *txt;

	s_init(&t);
	s_add(&t, b, n);
	txt = t.p ? t.p : (char *)"";
	d1 = strstr(txt, "..");
	if (!d1) {
		s_free(&t);
		return 0;
	}
	d2 = strstr(d1 + 2, "..");
	if (d2)
		step = atol(d2 + 2);
	if (!step)
		step = 1;
	if (isalpha((unsigned char)txt[0]) && d1 == txt + 1 &&
	    isalpha((unsigned char)d1[2])) {
		alpha = 1;
		lo = txt[0];
		hi = d1[2];
	} else {
		if (txt[0] == '0' && d1 - txt > 1)
			pad = (int)(d1 - txt);
		lo = atol(txt);
		hi = atol(d1 + 2);
	}
	if (step < 0)
		step = -step;
	for (v = lo; lo <= hi ? v <= hi : v >= hi; v += lo <= hi ? step : -step) {
		str o;
		s_init(&o);
		if (alpha) {
			s_ch(&o, (int)v);
		} else {
			str num;
			int k;
			s_init(&num);
			s_num(&num, v);
			for (k = pad - (int)num.n; k > 0; k--)
				s_ch(&o, '0');
			s_add(&o, num.p, num.n);
			s_free(&num);
		}
		v_add(out, ar_dup(s->xa, o.p ? o.p : "", o.n));
		s_free(&o);
	}
	s_free(&t);
	return 1;
}

/* Expand the first brace group in a word, returning how many words resulted. */
int br_split(sh *s, word *w, vec *out)
{
	part *p;
	size_t i, j, k, d, last;
	vec *alts;
	str nt;
	int made = 0;

	for (p = w->p; p; p = p->nx) {
		if (p->k != P_TXT || p->q || p->n < 3)
			continue;
		for (i = 0; i + 2 < p->n; i++) {
			if (p->t[i] != '{')
				continue;
			d = 1;
			for (j = i + 1; j < p->n; j++) {
				if (p->t[j] == '{')
					d++;
				else if (p->t[j] == '}' && --d == 0)
					break;
			}
			if (j >= p->n)
				continue;
			alts = vb_get(s);
			d = 0;
			last = i + 1;
			for (k = i + 1; k < j; k++) {
				if (p->t[k] == '{')
					d++;
				else if (p->t[k] == '}')
					d--;
				else if (p->t[k] == ',' && !d) {
					v_add(alts, ar_dup(s->xa, p->t + last,
							   k - last));
					last = k + 1;
				}
			}
			if (alts->n)
				v_add(alts, ar_dup(s->xa, p->t + last, j - last));
			else if (!br_range(s, p->t + i + 1, j - i - 1, alts)) {
				vb_put(s, alts);
				continue;
			}
			for (k = 0; k < alts->n; k++) {
				s_init(&nt);
				s_add(&nt, p->t, i);
				s_cat(&nt, (char *)alts->p[k]);
				s_add(&nt, p->t + j + 1, p->n - j - 1);
				v_add(out, br_make(s, w, p, nt.p ? nt.p : "",
						   nt.n));
				s_free(&nt);
				made++;
			}
			vb_put(s, alts);
			return made;
		}
	}
	return 0;
}

/* Expand every brace group in a word. */
void br_expand(sh *s, word *w, vec *out)
{
	vec *todo, *next;
	part *p;
	size_t i;
	int again = 1, seen = 0;

	for (p = w->p; p && !seen; p = p->nx)
		if (p->k == P_TXT && !p->q && memchr(p->t, '{', p->n))
			seen = 1;
	if (!seen) {
		v_add(out, w);
		return;
	}
	todo = vb_get(s);
	v_add(todo, w);
	while (again) {
		again = 0;
		next = vb_get(s);
		for (i = 0; i < todo->n; i++) {
			if (br_split(s, (word *)todo->p[i], next))
				again = 1;
			else
				v_add(next, todo->p[i]);
		}
		vb_put(s, todo);
		todo = next;
	}
	for (i = 0; i < todo->n; i++)
		v_add(out, todo->p[i]);
	vb_put(s, todo);
}

/* Bytes that stop a word being its own expansion: patterns, braces, IFS. */
const char w_meta[256] = {
	['*'] = 1, ['?'] = 1, ['['] = 1, ['~'] = 1, ['{'] = 1,
	[' '] = 1, ['\t'] = 1, ['\n'] = 1
};

/* True for a word that expands to exactly its own text, unchanged. */
int w_simple(word *w)
{
	part *p = w->p;
	size_t i;

	if (!p || p->nx || p->k != P_TXT || p->q || !p->n)
		return 0;
	for (i = 0; i < p->n; i++)
		if (w_meta[(unsigned char)p->t[i]])
			return 0;
	return 1;
}

/* Build a NULL terminated argv from a word list. */
char **xargv(sh *s, word *w, int *ac, char ***am)
{
	vec *o = vb_get(s);
	vec *om = 0;
	vec *bw;
	char **r, **q = 0;
	size_t i;
	int first = 1, plain = !sh_ifs(s);

	for (; w; w = w->nx) {
		if (plain && w_simple(w)) {
			xout(s, o, om, w->p->t, 0, w->p->n);
		} else {
			bw = vb_get(s);
			br_expand(s, w, bw);
			for (i = 0; i < bw->n; i++)
				xwm(s, (word *)bw->p[i], o, 0, om);
			vb_put(s, bw);
		}
		if (om)
			xpad(o, om);
		else if (first && o->n &&
			 (bi_mask((char *)o->p[0]) ||
			  al_get(s, (char *)o->p[0]))) {
			lg(HIBR_LDBG, "%s reads argument quoting, masking",
			   (char *)o->p[0]);
			om = vb_get(s);
			xpad(o, om);
		}
		first = 0;
	}
	if (om && om->n != o->n) {
		lg(HIBR_LDBG, "argv mask desync (%lu of %lu), dropping",
		   (unsigned long)om->n, (unsigned long)o->n);
		vb_put(s, om);
		om = 0;
	}
	r = ar_alloc(s->xa, (o->n + 1) * sizeof *r);
	if (om)
		q = ar_alloc(s->xa, (o->n + 1) * sizeof *q);
	for (i = 0; i < o->n; i++) {
		r[i] = (char *)o->p[i];
		if (q)
			q[i] = (char *)om->p[i];
	}
	r[o->n] = 0;
	*ac = (int)o->n;
	if (am)
		*am = q;
	if (om)
		vb_put(s, om);
	vb_put(s, o);
	return r;
}

struct ax { sh *s; const char *p; int depth, bad, skip, hasq; };

long ax_comma(struct ax *a);
long ax_assign(struct ax *a);
long ax_bin(struct ax *a, int mp);

/* Skip arithmetic whitespace. */
void ax_ws(struct ax *a)
{
	while (*a->p == ' ' || *a->p == '\t' || *a->p == '\n')
		a->p++;
}

/* Report an arithmetic error once. */
void ax_err(struct ax *a, const char *m)
{
	if (!a->bad && !a->skip)
		lg(HIBR_LERR, "arithmetic: %s near '%.20s'", m, a->p);
	a->bad = 1;
}

/* Read a[...] inside arithmetic, giving the element's value. */
long ax_elem(struct ax *a, const char *nm)
{
	const char *b, *e;
	const char *t;
	char *ks[1];
	str k;
	long v = 0;
	int d = 1;

	a->p++;
	b = a->p;
	for (e = b; *e && d; e++) {
		if (*e == '[')
			d++;
		else if (*e == ']' && !--d)
			break;
	}
	if (!*e) {
		ax_err(a, "expected ] after subscript");
		return 0;
	}
	s_init(&k);
	s_add(&k, b, (size_t)(e - b));
	a->p = e + 1;
	ks[0] = xkey(a->s, k.p ? k.p : "");
	t = hibr_getp(a->s, nm, ks, 1);
	if (t && *t)
		v = strtol(t, 0, 0);
	lg(HIBR_LTRC, "arithmetic read %s[%s] as %ld", nm, k.p ? k.p : "", v);
	s_free(&k);
	return v;
}

/* Read a variable's numeric value. */
long ax_get(struct ax *a, const char *nm)
{
	const char *t = nm[1] ? hibr_get(a->s, nm) : xval(a->s, nm);

	if (!t && nm[1])
		t = xval(a->s, nm);
	return t && *t ? strtol(t, 0, 0) : 0;
}

/* Store a numeric value, unless this branch is not being evaluated. */
void ax_set(struct ax *a, const char *nm, long v)
{
	if (a->skip || a->bad)
		return;
	hibr_set(a->s, nm, xnum(a->s, v), 0);
}

/* Read an identifier at the cursor. */
int ax_name(struct ax *a, str *nm)
{
	const char *b = a->p;

	if (!(isalpha((unsigned char)*a->p) || *a->p == '_'))
		return 0;
	while (isalnum((unsigned char)*a->p) || *a->p == '_')
		a->p++;
	s_add(nm, b, (size_t)(a->p - b));
	return 1;
}

/* Value of one digit in a base, using bash's alphabet above ten. */
int ax_digit(int c, long base, long *out)
{
	long v;

	if (isdigit(c))
		v = c - '0';
	else if (islower(c))
		v = c - 'a' + 10;
	else if (isupper(c))
		v = base > 36 ? c - 'A' + 36 : c - 'A' + 10;
	else if (c == '@')
		v = 62;
	else if (c == '_')
		v = 63;
	else
		return 0;
	if (v >= base)
		return 0;
	*out = v;
	return 1;
}

/* Parse a primary: number, variable with optional postfix step, or group. */
long ax_prim(struct ax *a)
{
	long v = 0;
	str nm;

	ax_ws(a);
	if (*a->p == '(') {
		if (++a->depth > HIBR_AXDEPTH) {
			ax_err(a, "nested too deeply");
			while (*a->p)
				a->p++;
			return 0;
		}
		a->p++;
		v = ax_comma(a);
		a->depth--;
		ax_ws(a);
		if (*a->p == ')')
			a->p++;
		else
			ax_err(a, "expected )");
		return v;
	}
	if (isdigit((unsigned char)*a->p)) {
		char *e;
		long base = strtol(a->p, &e, 10), d;
		if (*e == '#') {
			if (base < 2 || base > 64) {
				ax_err(a, "base must be between 2 and 64");
				return 0;
			}
			a->p = e + 1;
			if (!ax_digit((unsigned char)*a->p, base, &d)) {
				ax_err(a, "no digits after the base");
				return 0;
			}
			for (v = 0; ax_digit((unsigned char)*a->p, base, &d);
			     a->p++)
				v = v * base + d;
			return v;
		}
		v = strtol(a->p, &e, 0);
		a->p = e;
		return v;
	}
	s_init(&nm);
	if (ax_name(a, &nm)) {
		if (*a->p == '[') {
			v = ax_elem(a, nm.p);
			s_free(&nm);
			return v;
		}
		v = ax_get(a, nm.p);
		ax_ws(a);
		if ((a->p[0] == '+' && a->p[1] == '+') ||
		    (a->p[0] == '-' && a->p[1] == '-')) {
			ax_set(a, nm.p, a->p[0] == '+' ? v + 1 : v - 1);
			a->p += 2;
		}
		s_free(&nm);
		return v;
	}
	s_free(&nm);
	if (*a->p == '$') {
		a->p++;
		return ax_prim(a);
	}
	ax_err(a, "syntax error");
	return 0;
}

/* Parse a unary expression, including prefix increment and decrement. */
long ax_un(struct ax *a)
{
	str nm;
	long v;

	ax_ws(a);
	if ((a->p[0] == '+' && a->p[1] == '+') || (a->p[0] == '-' && a->p[1] == '-')) {
		int up = a->p[0] == '+';
		a->p += 2;
		ax_ws(a);
		s_init(&nm);
		if (!ax_name(a, &nm)) {
			ax_err(a, "++ and -- need a variable");
			s_free(&nm);
			return 0;
		}
		v = ax_get(a, nm.p) + (up ? 1 : -1);
		ax_set(a, nm.p, v);
		s_free(&nm);
		return v;
	}
	if (*a->p == '-') {
		a->p++;
		return (long)(0UL - (unsigned long)ax_un(a));
	}
	if (*a->p == '+') {
		a->p++;
		return ax_un(a);
	}
	if (*a->p == '!') {
		a->p++;
		return !ax_un(a);
	}
	if (*a->p == '~') {
		a->p++;
		return ~ax_un(a);
	}
	return ax_prim(a);
}

const char *ax_tk[] = { "||", "&&", "|", "^", "&", "==", "!=", "<<", ">>",
			"<=", ">=", "<", ">", "+", "-", "*", "/", "%", "**", 0 };
const int ax_pr[] = { 1, 2, 3, 4, 5, 6, 6, 9, 9, 7, 7, 7, 7, 10, 10, 11, 11, 11, 12 };
const int ax_ln[] = { 2, 2, 1, 1, 1, 2, 2, 2, 2, 2, 2, 1, 1, 1, 1, 1, 1, 1, 2 };

/* Identify the longest binary operator at the cursor. */
int ax_op(struct ax *a, int *pr, int *ln)
{
	int best = -1;
	size_t bl;
	char c = *a->p, d;

	if (!c)
		return -1;
	d = a->p[1];
	switch (c) {
	case '|': best = d == '|' ? 0 : 2; break;
	case '&': best = d == '&' ? 1 : 4; break;
	case '^': best = 3; break;
	case '=': best = d == '=' ? 5 : -1; break;
	case '!': best = d == '=' ? 6 : -1; break;
	case '<': best = d == '<' ? 7 : d == '=' ? 9 : 11; break;
	case '>': best = d == '>' ? 8 : d == '=' ? 10 : 12; break;
	case '+': best = 13; break;
	case '-': best = 14; break;
	case '*': best = d == '*' ? 18 : 15; break;
	case '/': best = 16; break;
	case '%': best = 17; break;
	}
	if (best < 0)
		return -1;
	bl = (size_t)ax_ln[best];
	if (a->p[bl] == '=' && best != 5 && best != 6 && best != 9 && best != 10)
		return -1;
	*pr = ax_pr[best];
	*ln = (int)bl;
	return best;
}

/* Apply a binary arithmetic operator. */
long ax_do(struct ax *a, int op, long l, long r)
{
	long v = 1;

	if (a->skip)
		return 0;
	switch (op) {
	case 0: return l || r;
	case 1: return l && r;
	case 2: return l | r;
	case 3: return l ^ r;
	case 4: return l & r;
	case 5: return l == r;
	case 6: return l != r;
	case 7: return (long)((unsigned long)l << (r & 63));
	case 8: return l >> (r & 63);
	case 9: return l <= r;
	case 10: return l >= r;
	case 11: return l < r;
	case 12: return l > r;
	case 13: return (long)((unsigned long)l + (unsigned long)r);
	case 14: return (long)((unsigned long)l - (unsigned long)r);
	case 15: return (long)((unsigned long)l * (unsigned long)r);
	case 16:
	case 17:
		if (!r) {
			ax_err(a, "division by zero");
			return 0;
		}
		if (r == -1)
			return op == 16 ? (long)(0UL - (unsigned long)l) : 0;
		return op == 16 ? l / r : l % r;
	case 18: {
		unsigned long acc = 1, base = (unsigned long)l;
		if (r < 0) {
			ax_err(a, "negative exponent");
			return 0;
		}
		while (r > 0) {
			if (r & 1)
				acc *= base;
			base *= base;
			r >>= 1;
		}
		(void)v;
		return (long)acc;
	}
	}
	return 0;
}

/* Parse a binary expression by precedence climbing, short-circuiting && and ||. */
long ax_bin(struct ax *a, int mp)
{
	long l = ax_un(a), r;
	int op, pr, ln;

	for (;;) {
		ax_ws(a);
		op = ax_op(a, &pr, &ln);
		if (op < 0 || pr < mp)
			break;
		a->p += ln;
		if ((op == 0 && l) || (op == 1 && !l)) {
			a->skip++;
			ax_bin(a, pr + 1);
			a->skip--;
			l = op == 0;
			continue;
		}
		r = ax_bin(a, op == 18 ? pr : pr + 1);
		l = ax_do(a, op, l, r);
	}
	return l;
}

/* Parse a conditional expression. */
long ax_tern(struct ax *a)
{
	long c = ax_bin(a, 1), t, f;

	ax_ws(a);
	if (*a->p != '?')
		return c;
	a->p++;
	if (!c)
		a->skip++;
	t = ax_assign(a);
	if (!c)
		a->skip--;
	ax_ws(a);
	if (*a->p != ':') {
		ax_err(a, "expected : in ?:");
		return 0;
	}
	a->p++;
	if (c)
		a->skip++;
	f = ax_assign(a);
	if (c)
		a->skip--;
	return c ? t : f;
}

const char *ax_as[] = { "<<=", ">>=", "**=", "+=", "-=", "*=", "/=", "%=",
			"&=", "|=", "^=", "=", 0 };
const int ax_asop[] = { 7, 8, 18, 13, 14, 15, 16, 17, 4, 2, 3, -1 };

/* Parse an assignment, falling back to a conditional expression. */
long ax_assign(struct ax *a)
{
	const char *save;
	str nm;
	long v, r;
	int i;
	size_t n;

	ax_ws(a);
	save = a->p;
	if (!a->hasq)
		return ax_tern(a);
	s_init(&nm);
	if (ax_name(a, &nm)) {
		ax_ws(a);
		for (i = 0; ax_as[i]; i++) {
			n = strlen(ax_as[i]);
			if (strncmp(a->p, ax_as[i], n))
				continue;
			if (ax_asop[i] < 0 && a->p[1] == '=')
				break;
			a->p += n;
			r = ax_assign(a);
			v = ax_asop[i] < 0 ? r : ax_do(a, ax_asop[i], ax_get(a, nm.p), r);
			ax_set(a, nm.p, v);
			s_free(&nm);
			return v;
		}
	}
	s_free(&nm);
	a->p = save;
	return ax_tern(a);
}

/* Parse a comma separated sequence, yielding the last value. */
long ax_comma(struct ax *a)
{
	long v = ax_assign(a);

	for (;;) {
		ax_ws(a);
		if (*a->p != ',')
			return v;
		a->p++;
		v = ax_assign(a);
	}
}

/* Evaluate an arithmetic expression string. */
long ax_run(sh *s, const char *src)
{
	struct ax a;
	long v;

	a.s = s;
	a.p = src;
	a.depth = 0;
	a.bad = 0;
	a.skip = 0;
	a.hasq = strchr(src, '=') != 0;
	v = ax_comma(&a);
	ax_ws(&a);
	if (*a.p && !a.bad)
		ax_err(&a, "unexpected text");
	if (a.bad)
		s->xerr = 1;
	return a.bad ? 0 : v;
}

/* Expand then evaluate arithmetic source text. */
long ax_text(sh *s, const char *t)
{
	amark m = ar_mark(s->xa);
	lex l;
	word *w;
	long v;

	lx_init(&l, s, t);
	l.a = s->xa;
	l.nb = 1;
	w = lx_word(&l);
	v = ax_run(s, w ? xone(s, w) : "");
	ar_rel(s->xa, m);
	return v;
}
