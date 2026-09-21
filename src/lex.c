#include "pri.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

/* True for characters that terminate an unquoted word. */
int ismeta(int c)
{
	return c == '|' || c == '&' || c == ';' || c == '<' || c == '>' ||
	       c == '(' || c == ')';
}

/* True if the text is a valid shell name. */
int isname(const char *t)
{
	size_t i;

	if (!t || !*t || isdigit((unsigned char)t[0]))
		return 0;
	for (i = 0; t[i]; i++)
		if (!(isalnum((unsigned char)t[i]) || t[i] == '_'))
			return 0;
	return 1;
}

/* Prepare a lexer over a source string. */
void lx_init(lex *l, sh *s, const char *src)
{
	memset(l, 0, sizeof *l);
	l->s = s;
	l->a = s->ar;
	l->p = src;
	l->e = src + strlen(src);
	l->fd = -1;
}

/* Emit the pending literal run as a word part. */
void lx_flush(lex *l, str *b, part ***t, int q)
{
	part *p;

	if (!b->n)
		return;
	p = ar_alloc(l->a, sizeof *p);
	p->k = P_TXT;
	p->q = (unsigned)q;
	p->n = b->n;
	p->t = ar_dup(l->a, b->p, b->n);
	**t = p;
	*t = &p->nx;
	b->n = 0;
	if (b->p)
		b->p[0] = 0;
}

/* Append a character, flushing first if its quoting differs. */
void lx_ch(lex *l, str *b, part ***t, int *cq, int q, int c)
{
	if (b->n && *cq != q)
		lx_flush(l, b, t, *cq);
	*cq = q;
	s_ch(b, c);
}

/* Find the matching close delimiter, honouring quotes and nesting. */
const char *sk_bal(lex *l, const char *p, char o, char c)
{
	int d = 1;

	while (p < l->e) {
		if (*p == '\\') {
			p += 2;
			continue;
		}
		if (*p == '\'') {
			for (p++; p < l->e && *p != '\''; p++)
				;
			if (p >= l->e)
				break;
			p++;
			continue;
		}
		if (*p == '"') {
			for (p++; p < l->e && *p != '"'; p++)
				if (*p == '\\')
					p++;
			if (p >= l->e)
				break;
			p++;
			continue;
		}
		if (*p == o)
			d++;
		else if (*p == c && --d == 0)
			return p;
		p++;
	}
	l->more = 1;
	return 0;
}

/* Take the raw text up to the matching close paren, consuming it. */
char *lx_span(lex *l)
{
	const char *b = l->p;
	const char *e = sk_bal(l, b, '(', ')');
	char *r;

	if (!e)
		return 0;
	r = ar_dup(l->a, b, (size_t)(e - b));
	l->p = e + 1;
	return r;
}

/* Consume an optional -> type annotation, returning the type name. */
char *lx_arrow(lex *l)
{
	const char *p = l->p, *b;

	while (p < l->e && (*p == ' ' || *p == '\t'))
		p++;
	if (p + 1 >= l->e || p[0] != '-' || p[1] != '>')
		return 0;
	p += 2;
	while (p < l->e && (*p == ' ' || *p == '\t'))
		p++;
	b = p;
	while (p < l->e && (isalnum((unsigned char)*p) || *p == '_'))
		p++;
	l->p = p;
	return ar_dup(l->a, b, (size_t)(p - b));
}

/* Lex a bounded substring as one word without blank splitting. */
word *lx_sub(lex *l, const char *b, const char *e)
{
	lex t = *l;
	word *w;

	t.p = b;
	t.e = e;
	t.nb = 1;
	w = lx_word(&t);
	if (t.more)
		l->more = 1;
	return w;
}

/* Parse ${...}, keeping any modifier that follows an indirect name. */
void lx_brace(lex *l, part *p, const char *b, const char *e)
{
	int ind = b + 1 < e && *b == '!';

	lx_brace1(l, p, b, e);
	if (ind && p->op != V_KEYS && p->op != V_NAMES && p->op != V_IND)
		p->op |= V_INDF;
}

/* Parse the interior of a brace expansion into a variable part. */
void lx_brace1(lex *l, part *p, const char *b, const char *e)
{
	const char *q = b;

	int lenop = 0, keyop = 0;
	word **it = &p->idx;

	p->k = P_VAR;
	if (b < e && *b == '#' && e - b > 1) {
		lenop = 1;
		b++;
		q = b;
	} else if (b < e && *b == '!' && e - b > 1) {
		keyop = 1;
		b++;
		q = b;
	}
	while (q < e && (isalnum((unsigned char)*q) || *q == '_'))
		q++;
	if (q == b && q < e)
		q++;
	p->t = ar_dup(l->a, b, (size_t)(q - b));
	p->n = (size_t)(q - b);
	while (q < e && *q == '[') {
		const char *r = q + 1;
		int d = 1;
		word *ix;
		while (r < e && d) {
			if (*r == '[')
				d++;
			else if (*r == ']')
				d--;
			if (d)
				r++;
		}
		if (r >= e) {
			lg(HIBR_LERR, "bad subscript in ${%.*s}", (int)(e - b), b);
			l->err = 1;
			return;
		}
		p->arr = 1;
		ix = lx_sub(l, q + 1, r);
		if (!ix)
			ix = ar_alloc(l->a, sizeof *ix);
		*it = ix;
		it = &ix->nx;
		q = r + 1;
	}
	if (lenop) {
		p->op = V_LEN;
		return;
	}
	if (keyop) {
		if (p->arr) {
			p->op = V_KEYS;
			return;
		}
		if (q < e && (*q == '*' || *q == '@')) {
			p->op = V_NAMES;
			return;
		}
		if (q >= e) {
			p->op = V_IND;
			return;
		}
	}
	if (q >= e)
		return;
	if (*q == ':') {
		if (q + 1 >= e || !strchr("-=?+", q[1])) {
			p->op = V_SUBSTR;
			p->arg = lx_sub(l, q + 1, e);
			return;
		}
		p->col = 1;
		q++;
	}
	if (q >= e)
		return;
	if (*q == '@' && q + 1 < e) {
		switch (q[1]) {
		case 'Q':
			p->op = V_XQ;
			return;
		case 'E':
			p->op = V_XE;
			return;
		case 'U':
			p->op = V_UPALL;
			return;
		case 'L':
			p->op = V_LOWALL;
			return;
		case 'u':
			p->op = V_UP;
			return;
		}
	}
	if (*q == '^' || *q == ',') {
		int dbl = q + 1 < e && q[1] == *q;
		p->op = *q == '^' ? (dbl ? V_UPALL : V_UP) : (dbl ? V_LOWALL : V_LOW);
		return;
	}
	switch (*q) {
	case '-':
		p->op = V_DEF;
		q++;
		break;
	case '=':
		p->op = V_ASG;
		q++;
		break;
	case '?':
		p->op = V_ERR;
		q++;
		break;
	case '+':
		p->op = V_ALT;
		q++;
		break;
	case '#':
		if (q + 1 < e && q[1] == '#') {
			p->op = V_RL;
			q += 2;
		} else {
			p->op = V_RS;
			q++;
		}
		break;
	case '%':
		if (q + 1 < e && q[1] == '%') {
			p->op = V_SL;
			q += 2;
		} else {
			p->op = V_SS;
			q++;
		}
		break;
	case '/': {
		const char *r;
		p->op = V_SUB;
		q++;
		if (q < e && *q == '/') {
			p->op = V_SUBA;
			q++;
		} else if (q < e && *q == '#') {
			p->op = V_SUBP;
			q++;
		} else if (q < e && *q == '%') {
			p->op = V_SUBF;
			q++;
		}
		for (r = q; r < e; r++) {
			if (*r == '\\') {
				r++;
				continue;
			}
			if (*r == '/')
				break;
		}
		p->arg = lx_sub(l, q, r < e ? r : e);
		if (!p->arg)
			p->arg = ar_alloc(l->a, sizeof *p->arg);
		p->arg->nx = r < e ? lx_sub(l, r + 1, e) : 0;
		return;
	}
	default:
		lg(HIBR_LERR, "bad substitution: ${%.*s}", (int)(e - b), b);
		l->err = 1;
		return;
	}
	p->arg = lx_sub(l, q, e);
}

/* Parse a dollar or backquote expansion into a part. */
part *lx_dol(lex *l, int q)
{
	part *p = ar_alloc(l->a, sizeof *p);
	const char *b, *e;
	int c;

	p->q = (unsigned)q;
	if (*l->p == '`') {
		b = l->p + 1;
		e = b;
		while (e < l->e && *e != '`') {
			if (*e == '\\')
				e++;
			e++;
		}
		if (e >= l->e) {
			l->more = 1;
			return 0;
		}
		p->k = P_CMD;
		p->t = ar_dup(l->a, b, (size_t)(e - b));
		p->n = (size_t)(e - b);
		l->p = e + 1;
		return p;
	}
	l->p++;
	if (l->p >= l->e) {
		p->k = P_TXT;
		p->t = ar_dup(l->a, "$", 1);
		p->n = 1;
		return p;
	}
	c = *l->p;
	if (c == '(') {
		b = l->p + 1;
		e = sk_bal(l, b, '(', ')');
		if (!e)
			return 0;
		l->p = e + 1;
		if (e > b && *b == '(' && e[-1] == ')') {
			p->k = P_ARI;
			p->t = ar_dup(l->a, b + 1, (size_t)(e - b - 2));
			p->n = (size_t)(e - b - 2);
		} else {
			p->k = P_CMD;
			p->t = ar_dup(l->a, b, (size_t)(e - b));
			p->n = (size_t)(e - b);
		}
		return p;
	}
	if (c == '{') {
		b = l->p + 1;
		e = sk_bal(l, b, '{', '}');
		if (!e)
			return 0;
		l->p = e + 1;
		lx_brace(l, p, b, e);
		return p;
	}
	if (isalpha(c) || c == '_') {
		b = l->p;
		while (l->p < l->e && (isalnum((unsigned char)*l->p) || *l->p == '_'))
			l->p++;
		p->k = P_VAR;
		p->t = ar_dup(l->a, b, (size_t)(l->p - b));
		p->n = (size_t)(l->p - b);
		return p;
	}
	if (isdigit(c) || strchr("?#$!*@-", c)) {
		p->k = P_VAR;
		p->t = ar_dup(l->a, l->p, 1);
		p->n = 1;
		l->p++;
		return p;
	}
	p->k = P_TXT;
	p->t = ar_dup(l->a, "$", 1);
	p->n = 1;
	return p;
}

/* Read one shell word, resolving quotes into parts. */
word *lx_word(lex *l)
{
	str b;
	part *h = 0, **t = &h, *x;
	word *w;
	int q = 0, cq = 0, seen = 0, c;

	s_init(&b);
	while (l->p < l->e) {
		c = *l->p;
		if ((c == '<' || c == '>') && l->p + 1 < l->e &&
		    l->p[1] == '(') {
			const char *bb, *ee;
			lx_flush(l, &b, &t, cq);
			l->p += 2;
			bb = l->p;
			ee = sk_bal(l, bb, '(', ')');
			if (!ee)
				break;
			x = ar_alloc(l->a, sizeof *x);
			x->k = P_PSUB;
			x->op = c == '<';
			x->q = 1;
			x->t = ar_dup(l->a, bb, (size_t)(ee - bb));
			x->n = (size_t)(ee - bb);
			l->p = ee + 1;
			*t = x;
			t = &x->nx;
			seen = 1;
			continue;
		}
		if (!q && !l->nb && c == '\n')
			break;
		if (!q && !l->nb && l->p + 1 < l->e && l->p[1] == '(' &&
		    (c == '?' || c == '*' || c == '+' || c == '@' ||
		     c == '!')) {
			const char *ee = sk_bal(l, l->p + 2, '(', ')');
			if (ee) {
				lg(HIBR_LTRC, "extended pattern group %.*s",
				   (int)(ee - l->p + 1), l->p);
				for (; l->p <= ee; l->p++)
					lx_ch(l, &b, &t, &cq, 0, *l->p);
				seen = 1;
				continue;
			}
		}
		if (!q && !l->nb && (c == ' ' || c == '\t' || ismeta(c)))
			break;
		if (c == '\'' && !q) {
			l->p++;
			seen = 1;
			while (l->p < l->e && *l->p != '\'')
				lx_ch(l, &b, &t, &cq, 1, *l->p++);
			if (l->p >= l->e) {
				l->more = 1;
				break;
			}
			l->p++;
			continue;
		}
		if (c == '"') {
			l->p++;
			q = !q;
			seen = 1;
			continue;
		}
		if (c == '\\') {
			l->p++;
			if (l->p >= l->e) {
				l->more = 1;
				break;
			}
			c = *l->p++;
			if (c == '\n')
				continue;
			if (q && !strchr("\"\\$`", c))
				lx_ch(l, &b, &t, &cq, 1, '\\');
			lx_ch(l, &b, &t, &cq, 1, c);
			seen = 1;
			continue;
		}
		if (c == '$' && l->p + 1 < l->e && l->p[1] == '\'' && !q) {
			l->p += 2;
			seen = 1;
			while (l->p < l->e && *l->p != '\'') {
				int v, k;
				if (*l->p != '\\') {
					lx_ch(l, &b, &t, &cq, 1, *l->p++);
					continue;
				}
				l->p++;
				if (l->p >= l->e)
					break;
				switch (*l->p) {
				case 'n': lx_ch(l, &b, &t, &cq, 1, '\n'); l->p++; break;
				case 't': lx_ch(l, &b, &t, &cq, 1, '\t'); l->p++; break;
				case 'r': lx_ch(l, &b, &t, &cq, 1, '\r'); l->p++; break;
				case 'a': lx_ch(l, &b, &t, &cq, 1, 7); l->p++; break;
				case 'b': lx_ch(l, &b, &t, &cq, 1, '\b'); l->p++; break;
				case 'f': lx_ch(l, &b, &t, &cq, 1, '\f'); l->p++; break;
				case 'v': lx_ch(l, &b, &t, &cq, 1, '\v'); l->p++; break;
				case 'e': lx_ch(l, &b, &t, &cq, 1, 27); l->p++; break;
				case 'x':
					l->p++;
					v = 0;
					for (k = 0; k < 2 && l->p < l->e &&
						    isxdigit((unsigned char)*l->p);
					     k++) {
						v = v * 16 +
						    (isdigit((unsigned char)*l->p) ?
							     *l->p - '0' :
							     (tolower(*l->p) - 'a' + 10));
						l->p++;
					}
					lx_ch(l, &b, &t, &cq, 1, v);
					break;
				case '0': case '1': case '2': case '3':
				case '4': case '5': case '6': case '7':
					v = 0;
					for (k = 0; k < 3 && l->p < l->e &&
						    *l->p >= '0' && *l->p <= '7';
					     k++)
						v = v * 8 + (*l->p++ - '0');
					lx_ch(l, &b, &t, &cq, 1, v);
					break;
				default:
					lx_ch(l, &b, &t, &cq, 1, *l->p++);
					break;
				}
			}
			if (l->p < l->e)
				l->p++;
			else
				l->more = 1;
			continue;
		}
		if (c == '$' || c == '`') {
			lx_flush(l, &b, &t, cq);
			x = lx_dol(l, q);
			if (!x)
				break;
			*t = x;
			t = &x->nx;
			continue;
		}
		lx_ch(l, &b, &t, &cq, q, c);
		l->p++;
	}
	if (q)
		l->more = 1;
	lx_flush(l, &b, &t, cq);
	s_free(&b);
	if (!h && !seen)
		return 0;
	if (!h) {
		x = ar_alloc(l->a, sizeof *x);
		x->k = P_TXT;
		x->q = 1;
		x->t = ar_dup(l->a, "", 0);
		x->n = 0;
		h = x;
	}
	w = ar_alloc(l->a, sizeof *w);
	w->p = h;
	return w;
}

struct hd { redir *r; char *d; int dash, q; };

/* Parse a here-document body, expanding parameters unless quoted. */
word *lx_body(lex *l, const char *b, const char *e, int q)
{
	lex t = *l;
	str buf;
	part *h = 0, **tp = &h, *x;
	word *w;
	int cq = 1;

	t.p = b;
	t.e = e;
	t.nb = 1;
	s_init(&buf);
	while (t.p < t.e) {
		int c = *t.p;
		if (!q && c == '\\' && t.p + 1 < t.e &&
		    (t.p[1] == '$' || t.p[1] == '`' || t.p[1] == '\\' ||
		     t.p[1] == '\n')) {
			t.p++;
			if (*t.p == '\n') {
				t.p++;
				continue;
			}
			lx_ch(&t, &buf, &tp, &cq, 1, *t.p++);
			continue;
		}
		if (!q && (c == '$' || c == '`')) {
			lx_flush(&t, &buf, &tp, cq);
			x = lx_dol(&t, 1);
			if (!x)
				break;
			*tp = x;
			tp = &x->nx;
			continue;
		}
		lx_ch(&t, &buf, &tp, &cq, 1, c);
		t.p++;
	}
	lx_flush(&t, &buf, &tp, cq);
	s_free(&buf);
	if (!h) {
		h = ar_alloc(l->a, sizeof *h);
		h->k = P_TXT;
		h->q = 1;
		h->t = ar_dup(l->a, "", 0);
	}
	w = ar_alloc(l->a, sizeof *w);
	w->p = h;
	return w;
}

/* Register a pending here-document for the delimiter word just read. */
void lx_here(lex *l, redir *r, word *d)
{
	struct hd *h = ar_alloc(l->a, sizeof *h);
	str b;
	part *p;

	s_init(&b);
	for (p = d ? d->p : 0; p; p = p->nx) {
		if (p->k != P_TXT) {
			lg(HIBR_LERR, "here-document delimiter must be a plain word");
			l->err = 1;
			s_free(&b);
			return;
		}
		if (p->q)
			h->q = 1;
		s_add(&b, p->t, p->n);
	}
	h->r = r;
	h->dash = l->dash;
	h->d = ar_dup(l->a, b.p ? b.p : "", b.n);
	s_free(&b);
	v_add(&l->hq, h);
	lg(HIBR_LTRC, "here-document pending for %s", h->d);
}

/* Read the bodies of every here-document queued on this line. */
void lx_hdoc(lex *l)
{
	size_t i;
	struct hd *h;
	str b;
	const char *ln, *nl;

	for (i = 0; i < l->hq.n && !l->more; i++) {
		h = (struct hd *)l->hq.p[i];
		s_init(&b);
		for (;;) {
			if (l->p >= l->e) {
				l->more = 1;
				break;
			}
			nl = memchr(l->p, '\n', (size_t)(l->e - l->p));
			if (!nl) {
				l->more = 1;
				break;
			}
			ln = l->p;
			if (h->dash)
				while (ln < nl && *ln == '\t')
					ln++;
			if ((size_t)(nl - ln) == strlen(h->d) &&
			    !memcmp(ln, h->d, (size_t)(nl - ln))) {
				l->p = nl + 1;
				break;
			}
			s_add(&b, ln, (size_t)(nl - ln));
			s_ch(&b, '\n');
			l->p = nl + 1;
		}
		if (!l->more)
			h->r->w = lx_body(l, b.p ? b.p : "",
					  (b.p ? b.p : "") + b.n, h->q);
		s_free(&b);
	}
	l->hq.n = 0;
}

/* Scan a redirection operator token. */
int lx_redir(lex *l)
{
	int c = *l->p++;

	if (c == '<') {
		if (l->p < l->e && *l->p == '<') {
			l->p++;
			l->dash = 0;
			if (l->p < l->e && *l->p == '<') {
				l->p++;
				return l->tk = T_HERES;
			}
			if (l->p < l->e && *l->p == '-') {
				l->p++;
				l->dash = 1;
			}
			return l->tk = T_HERE;
		}
		if (l->p < l->e && *l->p == '&') {
			l->p++;
			return l->tk = T_DIN;
		}
		if (l->p < l->e && *l->p == '>') {
			l->p++;
			return l->tk = T_RW;
		}
		return l->tk = T_LT;
	}
	if (l->p < l->e && *l->p == '>') {
		l->p++;
		return l->tk = T_APP;
	}
	if (l->p < l->e && *l->p == '&') {
		l->p++;
		return l->tk = T_DOUT;
	}
	if (l->p < l->e && *l->p == '|') {
		l->p++;
		l->clob = 1;
	}
	return l->tk = T_GT;
}

/* Advance to the next token. */
int lx_next(lex *l)
{
	const char *q;
	int c, n;

	if (l->more)
		return l->tk = T_EOF;
	l->w = 0;
	l->fd = -1;
	l->both = 0;
	l->clob = 0;
	l->fdvar = 0;
	for (;;) {
		while (l->p < l->e && (*l->p == ' ' || *l->p == '\t'))
			l->p++;
		if (l->p + 1 < l->e && *l->p == '\\' && l->p[1] == '\n') {
			l->p += 2;
			continue;
		}
		if (l->p < l->e && *l->p == '#') {
			while (l->p < l->e && *l->p != '\n')
				l->p++;
			continue;
		}
		break;
	}
	l->tkb = l->p;
	if (l->p >= l->e)
		return l->tk = T_EOF;
	c = *l->p;
	if (c == '\n') {
		l->p++;
		if (l->hq.n)
			lx_hdoc(l);
		return l->tk = T_NL;
	}
	if (c == ';') {
		l->p++;
		if (l->p < l->e && *l->p == ';') {
			l->p++;
			if (l->p < l->e && *l->p == '&') {
				l->p++;
				return l->tk = T_DSEMIAMP;
			}
			return l->tk = T_DSEMI;
		}
		if (l->p < l->e && *l->p == '&') {
			l->p++;
			return l->tk = T_SEMIAMP;
		}
		return l->tk = T_SEMI;
	}
	if (c == '(' && l->p + 1 < l->e && l->p[1] == '(') {
		const char *outer = sk_bal(l, l->p + 1, '(', ')');
		const char *inner = outer ? sk_bal(l, l->p + 2, '(', ')') : 0;
		if (outer && inner && inner + 1 == outer) {
			word *w = ar_alloc(l->a, sizeof *w);
			part *pp = ar_alloc(l->a, sizeof *pp);
			pp->k = P_TXT;
			pp->q = 1;
			pp->t = ar_dup(l->a, l->p + 2, (size_t)(inner - l->p - 2));
			pp->n = (size_t)(inner - l->p - 2);
			w->p = pp;
			l->w = w;
			l->p = outer + 1;
			return l->tk = T_ARITH;
		}
		if (!outer)
			return l->tk = T_EOF;
	}
	if (c == '(') {
		l->p++;
		return l->tk = T_LP;
	}
	if (c == ')') {
		l->p++;
		return l->tk = T_RP;
	}
	if (c == '&') {
		l->p++;
		if (l->p < l->e && *l->p == '&') {
			l->p++;
			return l->tk = T_AND;
		}
		if (l->p < l->e && *l->p == '>') {
			l->both = 1;
			return lx_redir(l);
		}
		return l->tk = T_AMP;
	}
	if (c == '|') {
		l->p++;
		if (l->p < l->e && *l->p == '|') {
			l->p++;
			return l->tk = T_OR;
		}
		return l->tk = T_PIPE;
	}
	if (isdigit(c)) {
		int big = 0;
		q = l->p;
		n = 0;
		while (q < l->e && isdigit((unsigned char)*q)) {
			if (n > 100000000)
				big = 1;
			else
				n = n * 10 + (*q - '0');
			q++;
		}
		if (!big && q < l->e && (*q == '<' || *q == '>')) {
			l->fd = n;
			l->p = q;
			return lx_redir(l);
		}
	}
	if (c == '{') {
		const char *q = l->p + 1;
		while (q < l->e && (isalnum((unsigned char)*q) || *q == '_'))
			q++;
		if (q > l->p + 1 && q + 1 < l->e && *q == '}' &&
		    (q[1] == '<' || q[1] == '>')) {
			l->fdvar = ar_dup(l->a, l->p + 1,
					  (size_t)(q - l->p - 1));
			l->p = q + 1;
			return lx_redir(l);
		}
	}
	if ((c == '<' || c == '>') && l->p + 1 < l->e && l->p[1] == '(') {
		l->w = lx_word(l);
		if (l->w)
			return l->tk = T_WORD;
	}
	if (c == '<' || c == '>')
		return lx_redir(l);
	l->w = lx_word(l);
	if (!l->w) {
		lg(HIBR_LERR, "unexpected character '%c'", c);
		l->err = 1;
		l->p++;
		return l->tk = T_EOF;
	}
	return l->tk = T_WORD;
}
