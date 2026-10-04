#define _GNU_SOURCE

#include "tr.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

enum { QC_ID, QC_CLASS, QC_ATTR, QC_FIRST, QC_LAST, QC_ONLY, QC_NTH, QC_NTHLAST,
       QC_FIRSTT, QC_LASTT, QC_EMPTY, QC_ROOT, QC_NOT };

/* One condition of a compound selector: an id, a class, an attribute
   test, or a pseudo-class. */
typedef struct qc qc;
struct qc {
	int t, op, ci;
	long a, b;
	char *nm, *val;
	vec sub;
};

/* A compound selector and the combinator that joins it to the one before:
   ' ' descendant, '>' child, '+' next sibling, '~' later sibling. */
typedef struct qs qs;
struct qs {
	char *tag;
	vec conds;
	int comb;
};

/* A parse in progress. */
typedef struct qp qp;
struct qp {
	const char *s;
	size_t i;
	int bad;
};

int q_compound(qp *p, qs *c);

/* Skip whitespace. */
void q_ws(qp *p)
{
	while (p->s[p->i] && isspace((unsigned char)p->s[p->i]))
		p->i++;
}

/* An identifier: letters, digits, - and _, any non-ASCII byte, and an
   escaped character; lowered when asked. */
char *q_ident(qp *p, int low)
{
	str o;
	unsigned char c;

	s_init(&o);
	while ((c = (unsigned char)p->s[p->i])) {
		if (c == '\\' && p->s[p->i + 1]) {
			s_ch(&o, p->s[p->i + 1]);
			p->i += 2;
			continue;
		}
		if (!(isalnum(c) || c == '-' || c == '_' || c >= 0x80))
			break;
		s_ch(&o, low ? tolower(c) : c);
		p->i++;
	}
	if (!o.n) {
		s_free(&o);
		return 0;
	}
	return o.p;
}

/* A quoted or bare value in an attribute selector. */
char *q_value(qp *p)
{
	str o;
	char q = p->s[p->i];

	if (q != '"' && q != '\'')
		return q_ident(p, 0);
	p->i++;
	s_init(&o);
	while (p->s[p->i] && p->s[p->i] != q) {
		if (p->s[p->i] == '\\' && p->s[p->i + 1])
			p->i++;
		s_ch(&o, p->s[p->i]);
		p->i++;
	}
	if (p->s[p->i] == q)
		p->i++;
	else
		p->bad = 1;
	if (!o.p)
		s_cat(&o, "");
	return o.p;
}

/* An+B from :nth-child(): odd, even, a number, or an expression. */
void q_anb(const char *s, long *a, long *b)
{
	char *e;

	while (isspace((unsigned char)*s))
		s++;
	if (!strncasecmp(s, "odd", 3)) {
		*a = 2;
		*b = 1;
		return;
	}
	if (!strncasecmp(s, "even", 4)) {
		*a = 2;
		*b = 0;
		return;
	}
	if (strchr(s, 'n') || strchr(s, 'N')) {
		if (*s == 'n' || *s == 'N')
			*a = 1;
		else if (*s == '-' && (s[1] == 'n' || s[1] == 'N'))
			*a = -1;
		else if (*s == '+' && (s[1] == 'n' || s[1] == 'N'))
			*a = 1;
		else
			*a = strtol(s, &e, 10);
		s = strpbrk(s, "nN") + 1;
		while (isspace((unsigned char)*s))
			s++;
		*b = *s ? strtol(s, 0, 10) : 0;
		if (*s == '-' || *s == '+') {
			long sign = *s == '-' ? -1 : 1;

			s++;
			*b = sign * strtol(s, 0, 10);
		}
		return;
	}
	*a = 0;
	*b = strtol(s, 0, 10);
}

/* Free a parsed selector list. */
void q_free(vec *list)
{
	size_t i, j, k;

	for (i = 0; i < list->n; i++) {
		vec *cx = list->p[i];

		for (j = 0; j < cx->n; j++) {
			qs *c = cx->p[j];

			for (k = 0; k < c->conds.n; k++) {
				qc *d = c->conds.p[k];

				free(d->nm);
				free(d->val);
				q_free(&d->sub);
				free(d);
			}
			v_free(&c->conds);
			free(c->tag);
			free(c);
		}
		v_free(cx);
		free(cx);
	}
	v_free(list);
}

/* A selector list: complex selectors separated by commas. */
int q_list(qp *p, vec *out, char close)
{
	vec *cx;
	qs *c;
	int comb;

	for (;;) {
		q_ws(p);
		cx = xm(sizeof *cx);
		memset(cx, 0, sizeof *cx);
		v_add(out, cx);
		comb = 0;
		for (;;) {
			c = xm(sizeof *c);
			memset(c, 0, sizeof *c);
			c->comb = comb;
			v_add(cx, c);
			if (!q_compound(p, c))
				p->bad = 1;
			if (p->bad)
				return 0;
			comb = 0;
			if (isspace((unsigned char)p->s[p->i])) {
				q_ws(p);
				comb = ' ';
			}
			if (p->s[p->i] == '>' || p->s[p->i] == '+' || p->s[p->i] == '~') {
				comb = p->s[p->i++];
				q_ws(p);
			}
			if (!p->s[p->i] || p->s[p->i] == ',' || p->s[p->i] == close)
				break;
			if (!comb) {
				p->bad = 1;
				return 0;
			}
		}
		if (p->s[p->i] == ',') {
			p->i++;
			continue;
		}
		return !p->bad;
	}
}

/* A compound selector: a type or *, then ids, classes, attribute tests and
   pseudo-classes, with nothing between. */
int q_compound(qp *p, qs *c)
{
	qc *d;
	char ch;
	int any = 0;

	if (p->s[p->i] == '*') {
		p->i++;
		any = 1;
	} else if ((c->tag = q_ident(p, 1))) {
		any = 1;
	}
	for (;;) {
		ch = p->s[p->i];
		if (ch != '#' && ch != '.' && ch != '[' && ch != ':')
			break;
		p->i++;
		d = xm(sizeof *d);
		memset(d, 0, sizeof *d);
		v_add(&c->conds, d);
		any = 1;
		if (ch == '#') {
			d->t = QC_ID;
			if (!(d->nm = q_ident(p, 0)))
				return 0;
		} else if (ch == '.') {
			d->t = QC_CLASS;
			if (!(d->nm = q_ident(p, 0)))
				return 0;
		} else if (ch == '[') {
			d->t = QC_ATTR;
			q_ws(p);
			if (!(d->nm = q_ident(p, 1)))
				return 0;
			q_ws(p);
			if (strchr("~|^$*", p->s[p->i]) && p->s[p->i + 1] == '=') {
				d->op = p->s[p->i];
				p->i += 2;
			} else if (p->s[p->i] == '=') {
				d->op = '=';
				p->i++;
			}
			if (d->op) {
				q_ws(p);
				if (!(d->val = q_value(p)))
					return 0;
				q_ws(p);
				if (p->s[p->i] == 'i' || p->s[p->i] == 'I') {
					d->ci = 1;
					p->i++;
					q_ws(p);
				}
			}
			if (p->s[p->i] != ']')
				return 0;
			p->i++;
		} else {
			char *nm = q_ident(p, 1);

			if (!nm)
				return 0;
			d->nm = nm;
			if (!strcmp(nm, "first-child"))
				d->t = QC_FIRST;
			else if (!strcmp(nm, "last-child"))
				d->t = QC_LAST;
			else if (!strcmp(nm, "only-child"))
				d->t = QC_ONLY;
			else if (!strcmp(nm, "first-of-type"))
				d->t = QC_FIRSTT;
			else if (!strcmp(nm, "last-of-type"))
				d->t = QC_LASTT;
			else if (!strcmp(nm, "empty"))
				d->t = QC_EMPTY;
			else if (!strcmp(nm, "root"))
				d->t = QC_ROOT;
			else if ((!strcmp(nm, "nth-child") || !strcmp(nm, "nth-last-child")) &&
				 p->s[p->i] == '(') {
				size_t st = ++p->i;
				str arg;

				d->t = nm[4] == 'l' ? QC_NTHLAST : QC_NTH;
				while (p->s[p->i] && p->s[p->i] != ')')
					p->i++;
				if (p->s[p->i] != ')')
					return 0;
				s_init(&arg);
				s_add(&arg, p->s + st, p->i - st);
				q_anb(arg.p ? arg.p : "", &d->a, &d->b);
				s_free(&arg);
				p->i++;
			} else if (!strcmp(nm, "not") && p->s[p->i] == '(') {
				d->t = QC_NOT;
				p->i++;
				if (!q_list(p, &d->sub, ')') || p->s[p->i] != ')')
					return 0;
				p->i++;
			} else {
				return 0;
			}
		}
	}
	return any;
}

/* Whether a word is in a space-separated list, as ~= and class test. */
int q_word(const char *list, const char *w, int ci)
{
	size_t n = strlen(w);
	const char *s = list;

	if (!n)
		return 0;
	while (*s) {
		while (*s && isspace((unsigned char)*s))
			s++;
		if (!*s)
			break;
		if ((ci ? !strncasecmp(s, w, n) : !strncmp(s, w, n)) &&
		    (!s[n] || isspace((unsigned char)s[n])))
			return 1;
		while (*s && !isspace((unsigned char)*s))
			s++;
	}
	return 0;
}

/* An element's previous and next element siblings. */
hl_n *q_prev(hl_n *n)
{
	for (n = n->pv; n && n->t != HL_ELEM; n = n->pv)
		;
	return n;
}

/* The next element sibling. */
hl_n *q_next(hl_n *n)
{
	for (n = n->nx; n && n->t != HL_ELEM; n = n->nx)
		;
	return n;
}

/* An element's position among its element siblings, from 1, counting
   only those of its own type when asked, from the end when asked. */
long q_pos(hl_n *n, int fromend, int sametype)
{
	long k = 1;
	hl_n *m = n;

	for (;;) {
		m = fromend ? q_next(m) : q_prev(m);
		if (!m)
			break;
		if (!sametype || (m->ns == n->ns && !strcmp(m->tag, n->tag)))
			k++;
	}
	return k;
}

int q_matchlist(vec *list, hl_n *n);

/* Whether one condition holds for an element. */
int q_cond(qc *d, hl_n *n)
{
	const char *v;
	size_t vl, dl;
	long k;

	switch (d->t) {
	case QC_ID:
		v = hl_attr_get(n, "id");
		return v && !strcmp(v, d->nm);
	case QC_CLASS:
		v = hl_attr_get(n, "class");
		return v && q_word(v, d->nm, 0);
	case QC_ATTR:
		v = hl_attr_get(n, d->nm);
		if (!v)
			return 0;
		if (!d->op)
			return 1;
		vl = strlen(v);
		dl = strlen(d->val);
		switch (d->op) {
		case '=':
			return d->ci ? !strcasecmp(v, d->val) : !strcmp(v, d->val);
		case '~':
			return q_word(v, d->val, d->ci);
		case '|':
			return (d->ci ? !strncasecmp(v, d->val, dl) : !strncmp(v, d->val, dl)) &&
			       (v[dl] == 0 || v[dl] == '-');
		case '^':
			return dl && (d->ci ? !strncasecmp(v, d->val, dl) : !strncmp(v, d->val, dl));
		case '$':
			return dl && vl >= dl &&
			       (d->ci ? !strcasecmp(v + vl - dl, d->val) : !strcmp(v + vl - dl, d->val));
		case '*':
			return dl && (d->ci ? strcasestr(v, d->val) != 0 : strstr(v, d->val) != 0);
		}
		return 0;
	case QC_FIRST:
		return !q_prev(n);
	case QC_LAST:
		return !q_next(n);
	case QC_ONLY:
		return !q_prev(n) && !q_next(n);
	case QC_FIRSTT:
		return q_pos(n, 0, 1) == 1;
	case QC_LASTT:
		return q_pos(n, 1, 1) == 1;
	case QC_EMPTY:
		return !n->kid;
	case QC_ROOT:
		return n->up && n->up->t == HL_DOC;
	case QC_NTH:
	case QC_NTHLAST:
		k = q_pos(n, d->t == QC_NTHLAST, 0);
		if (!d->a)
			return k == d->b;
		return (k - d->b) % d->a == 0 && (k - d->b) / d->a >= 0;
	case QC_NOT:
		return !q_matchlist(&d->sub, n);
	}
	return 0;
}

/* Whether a compound selector matches an element. */
int q_compmatch(qs *c, hl_n *n)
{
	size_t i;

	if (n->t != HL_ELEM)
		return 0;
	if (c->tag && strcasecmp(c->tag, n->tag))
		return 0;
	for (i = 0; i < c->conds.n; i++)
		if (!q_cond(c->conds.p[i], n))
			return 0;
	return 1;
}

/* Whether a complex selector matches an element, from its last compound
   back through the combinators. */
int q_cxmatch(vec *cx, long k, hl_n *n)
{
	qs *c = cx->p[k];
	hl_n *m;

	if (!q_compmatch(c, n))
		return 0;
	if (k == 0)
		return 1;
	switch (c->comb) {
	case '>':
		return n->up && n->up->t == HL_ELEM && q_cxmatch(cx, k - 1, n->up);
	case '+':
		return (m = q_prev(n)) && q_cxmatch(cx, k - 1, m);
	case '~':
		for (m = q_prev(n); m; m = q_prev(m))
			if (q_cxmatch(cx, k - 1, m))
				return 1;
		return 0;
	default:
		for (m = n->up; m && m->t == HL_ELEM; m = m->up)
			if (q_cxmatch(cx, k - 1, m))
				return 1;
		return 0;
	}
}

/* Whether any selector in a list matches an element. */
int q_matchlist(vec *list, hl_n *n)
{
	size_t i;

	for (i = 0; i < list->n; i++) {
		vec *cx = list->p[i];

		if (cx->n && q_cxmatch(cx, (long)cx->n - 1, n))
			return 1;
	}
	return 0;
}

/* Every element under a node that a selector list matches, in document
   order, without recursion so depth cannot run the stack out. */
void q_walk(vec *list, hl_n *root, vec *out)
{
	hl_n *n = root->kid;

	while (n) {
		if (n->t == HL_ELEM && q_matchlist(list, n))
			v_add(out, n);
		if (n->kid) {
			n = n->kid;
			continue;
		}
		while (n && n != root && !n->nx)
			n = n->up;
		if (!n || n == root)
			break;
		n = n->nx;
	}
}

/* Find the elements under a node that a selector matches; -1 when the
   selector does not parse. */
int hl_query(hl_n *root, const char *sel, vec *out)
{
	vec list = { 0, 0, 0 };
	qp p;

	p.s = sel;
	p.i = 0;
	p.bad = 0;
	if (!q_list(&p, &list, 0) || p.bad || p.s[p.i]) {
		q_free(&list);
		return -1;
	}
	q_walk(&list, root, out);
	q_free(&list);
	return 0;
}
