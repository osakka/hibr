#define _GNU_SOURCE

#include "mk.h"
#include <stdlib.h>
#include <string.h>

typedef struct mk_dl mk_dl;
typedef struct mk_br mk_br;
typedef struct mk_s mk_s;

struct mk_dl {
	mk_dl *pv, *nx;
	mk_n *n;
	int c, len, olen, open, close;
};

struct mk_br {
	mk_br *pv;
	mk_n *n;
	size_t pos;
	int img, active, after;
	mk_dl *dl;
};

struct mk_s {
	mk_n *leaf;
	const char *s;
	size_t n, i;
	mk_dl *dl;
	mk_br *br;
	vec *refs;
	int gfm, maxd;
};

/* A text node of t, len long, for the content from a to b. */
static mk_n *mk_txt(mk_s *S, const char *t, size_t len, long a, long b)
{
	mk_n *n = mk_new(MK_TEXT);

	n->open = 0;
	s_add(&n->s, t, len);
	n->a = n->ca = a;
	n->b = n->cb = b;
	mk_append(S->leaf, n);
	return n;
}

/* A node of type t with no text, for the content from a to b. */
static mk_n *mk_mark(mk_s *S, int t, long a, long b)
{
	mk_n *n = mk_new(t);

	n->open = 0;
	n->a = n->ca = a;
	n->b = n->cb = b;
	mk_append(S->leaf, n);
	return n;
}

static int mk_isspc(char c)
{
	return c == ' ' || c == '\t';
}

/* Skip blanks at the start of the next line. */
static void mk_skipsp(mk_s *S)
{
	while (S->i < S->n && mk_isspc(S->s[S->i]))
		S->i++;
}

/* A line ending: a hard break after two spaces, else a soft one. */
static void mk_nl(mk_s *S)
{
	size_t nl = S->i;
	int hard = nl >= 2 && S->s[nl - 1] == ' ' && S->s[nl - 2] == ' ';

	mk_mark(S, hard ? MK_HARD : MK_SOFT, (long)nl, (long)nl + 1);
	S->i++;
	mk_skipsp(S);
}

/* A backslash: an escaped punctuation character, a hard break, or itself. */
static void mk_bslash(mk_s *S)
{
	size_t i = S->i + 1;
	unsigned char c;

	if (i < S->n) {
		c = (unsigned char)S->s[i];
		if (c < 0x80 && mk_ispunct(c)) {
			mk_txt(S, S->s + i, 1, (long)i, (long)i + 1);
			S->i = i + 1;
			return;
		}
		if (c == '\n') {
			mk_mark(S, MK_HARD, (long)S->i, (long)i + 1);
			S->i = i + 1;
			mk_skipsp(S);
			return;
		}
	}
	mk_txt(S, "\\", 1, (long)S->i, (long)S->i + 1);
	S->i++;
}

/* An entity or numeric reference, or a plain ampersand. */
static void mk_amp(mk_s *S)
{
	str t;
	size_t k;

	s_init(&t);
	k = mk_entref(S->s + S->i, S->n - S->i, &t);
	if (k) {
		mk_txt(S, t.p, t.n, (long)S->i, (long)(S->i + k));
		S->i += k;
	} else {
		mk_txt(S, "&", 1, (long)S->i, (long)S->i + 1);
		S->i++;
	}
	s_free(&t);
}

/* A code span, or the backticks as text when nothing closes them. */
static void mk_ticks(mk_s *S)
{
	size_t i = S->i, L = 0, j, k, a, b;
	const char *s = S->s;
	mk_n *n;

	while (i + L < S->n && s[i + L] == '`')
		L++;
	j = i + L;
	while (j < S->n) {
		if (s[j] != '`') {
			j++;
			continue;
		}
		k = 0;
		while (j + k < S->n && s[j + k] == '`')
			k++;
		if (k == L)
			break;
		j += k;
	}
	if (j >= S->n) {
		mk_txt(S, s + i, L, (long)i, (long)(i + L));
		S->i = i + L;
		return;
	}
	a = i + L;
	b = j;
	n = mk_mark(S, MK_CSPAN, (long)i, (long)(j + L));
	n->ca = (long)a;
	n->cb = (long)b;
	for (k = a; k < b; k++)
		s_ch(&n->s, s[k] == '\n' ? ' ' : s[k]);
	if (n->s.n >= 2 && n->s.p[0] == ' ' && n->s.p[n->s.n - 1] == ' ' &&
	    strspn(n->s.p, " ") < n->s.n) {
		memmove(n->s.p, n->s.p + 1, n->s.n - 2);
		n->s.n -= 2;
		n->s.p[n->s.n] = 0;
		n->ca++;
		n->cb--;
	}
	S->i = j + L;
}

static int mk_isal(char c)
{
	return (c | 32) >= 'a' && (c | 32) <= 'z';
}

static int mk_isan(char c)
{
	return mk_isal(c) || (c >= '0' && c <= '9');
}

/* An absolute URI's length after <, up to its >, or 0. */
static size_t mk_uri(const char *s, size_t n)
{
	size_t i = 0;

	if (!n || !mk_isal(s[0]))
		return 0;
	i = 1;
	while (i < n && (mk_isan(s[i]) || s[i] == '+' || s[i] == '.' || s[i] == '-'))
		i++;
	if (i < 2 || i > 32 || i >= n || s[i] != ':')
		return 0;
	i++;
	while (i < n && s[i] != '>') {
		if ((unsigned char)s[i] <= 0x20 || s[i] == '<' || s[i] == 0x7F)
			return 0;
		i++;
	}
	return i < n ? i : 0;
}

/* An email address's length after <, up to its >, or 0. */
static size_t mk_mail(const char *s, size_t n)
{
	size_t i = 0, seg;

	while (i < n && (mk_isan(s[i]) || strchr(".!#$%&'*+/=?^_`{|}~-", s[i])))
		i++;
	if (!i || i >= n || s[i] != '@')
		return 0;
	i++;
	for (;;) {
		seg = 0;
		while (i < n && (mk_isan(s[i]) || s[i] == '-') && seg < 63) {
			i++;
			seg++;
		}
		if (!seg || s[i - seg] == '-' || s[i - 1] == '-')
			return 0;
		if (i < n && s[i] == '.') {
			i++;
			continue;
		}
		break;
	}
	return i < n && s[i] == '>' ? i : 0;
}

/* <: an autolink, raw HTML, or the character. */
static void mk_angle(mk_s *S)
{
	const char *s = S->s + S->i + 1;
	size_t n = S->n - S->i - 1, k;
	long at = (long)S->i;
	mk_n *l, *t;
	str u;
	int mail = 0;

	if ((k = mk_uri(s, n)) || (k = mk_mail(s, n), mail = k > 0)) {
		l = mk_mark(S, MK_LINK, at, at + (long)k + 2);
		l->ca = at + 1;
		l->cb = at + 1 + (long)k;
		s_init(&u);
		if (mail)
			s_cat(&l->url, "mailto:");
		{
			size_t i = 0, e;
			while (i < k) {
				if (s[i] == '&' && (e = mk_entref(s + i, k - i, &u))) {
					i += e;
					continue;
				}
				s_ch(&u, s[i]);
				i++;
			}
		}
		s_add(&l->url, u.p, u.n);
		t = mk_new(MK_TEXT);
		t->open = 0;
		s_add(&t->s, u.p, u.n);
		t->a = t->ca = at + 1;
		t->b = t->cb = at + 1 + (long)k;
		mk_append(l, t);
		s_free(&u);
		S->i += k + 2;
		return;
	}
	k = mk_htmltag(S->s + S->i, S->n - S->i);
	if (k) {
		t = mk_mark(S, MK_RAW, at, at + (long)k);
		s_add(&t->s, S->s + S->i, k);
		S->i += k;
		return;
	}
	mk_txt(S, "<", 1, at, at + 1);
	S->i++;
}

/* A run of * _ or ~: text, and a delimiter when it can open or close. */
static void mk_delim(mk_s *S)
{
	size_t i = S->i, L = 0;
	char c = S->s[i];
	unsigned before, after;
	int l, lf, rf, op, cl;
	mk_n *t;
	mk_dl *d;

	while (i + L < S->n && S->s[i + L] == c)
		L++;
	before = mk_before(S->s, i);
	after = i + L < S->n ? mk_utf8(S->s + i + L, S->n - i - L, &l) : '\n';
	lf = !mk_isspace(after) &&
	     (!mk_ispunct(after) || mk_isspace(before) || mk_ispunct(before));
	rf = !mk_isspace(before) &&
	     (!mk_ispunct(before) || mk_isspace(after) || mk_ispunct(after));
	if (c == '_') {
		op = lf && (!rf || mk_ispunct(before));
		cl = rf && (!lf || mk_ispunct(after));
	} else {
		op = lf;
		cl = rf;
	}
	t = mk_txt(S, S->s + i, L, (long)i, (long)(i + L));
	S->i = i + L;
	if (c == '~' && L > 2)
		return;
	if (!op && !cl)
		return;
	d = xm(sizeof *d);
	memset(d, 0, sizeof *d);
	d->n = t;
	d->c = c;
	d->len = d->olen = (int)L;
	d->open = op;
	d->close = cl;
	d->pv = S->dl;
	if (S->dl)
		S->dl->nx = d;
	S->dl = d;
}

/* How deep the nodes from a up to, not including, b nest, plus one. */
static int mk_span(mk_n *a, mk_n *b)
{
	int d = 0;

	for (; a && a != b; a = a->nx)
		if (a->dep > d)
			d = a->dep;
	return d + 1;
}

/* Take a delimiter off the stack. */
static void mk_rmdl(mk_s *S, mk_dl *d)
{
	if (d->pv)
		d->pv->nx = d->nx;
	if (d->nx)
		d->nx->pv = d->pv;
	else
		S->dl = d->pv;
	free(d);
}

/* Wrap what lies between an opener and a closer in emphasis, strong or
   strikethrough; the delimiter to carry on from. */
static mk_dl *mk_wrap(mk_s *S, mk_dl *op, mk_dl *cl)
{
	int use;
	mk_n *on = op->n, *cn = cl->n, *e, *k, *nx;
	mk_dl *next;

	if (op->c == '~')
		use = cl->len;
	else
		use = cl->len >= 2 && op->len >= 2 ? 2 : 1;
	op->len -= use;
	cl->len -= use;
	on->s.n -= use;
	on->s.p[on->s.n] = 0;
	on->b -= use;
	on->cb -= use;
	memmove(cn->s.p, cn->s.p + use, cn->s.n - use);
	cn->s.n -= use;
	cn->s.p[cn->s.n] = 0;
	cn->a += use;
	cn->ca += use;
	while (cl->pv != op)
		mk_rmdl(S, cl->pv);
	e = mk_new(op->c == '~' ? MK_DEL : use == 2 ? MK_STRONG : MK_EMPH);
	e->open = 0;
	e->dep = mk_span(on->nx, cn);
	if (e->dep > S->maxd)
		S->maxd = e->dep;
	e->a = on->b;
	e->b = cn->a;
	e->ca = e->a + use;
	e->cb = e->b - use;
	for (k = on->nx; k && k != cn; k = nx) {
		nx = k->nx;
		mk_unlink(k);
		mk_append(e, k);
	}
	mk_insafter(on, e);
	if (!op->len) {
		mk_unlink(on);
		mk_free(on);
		mk_rmdl(S, op);
	}
	if (!cl->len) {
		next = cl->nx;
		mk_unlink(cn);
		mk_free(cn);
		mk_rmdl(S, cl);
		return next;
	}
	return cl;
}

/* The index of a delimiter character in the openers' floor table. */
static int mk_dci(int c)
{
	return c == '*' ? 0 : c == '_' ? 1 : 2;
}

/* Match the delimiters above bot into emphasis, as the spec's algorithm
   does, then drop them. */
static void mk_emph(mk_s *S, mk_dl *bot)
{
	mk_dl *floor[3][6], *cl, *op, *old;
	int i, j, ci, idx, found, odd;

	for (i = 0; i < 3; i++)
		for (j = 0; j < 6; j++)
			floor[i][j] = bot;
	cl = S->dl;
	while (cl && cl->pv != bot)
		cl = cl->pv;
	if (cl == bot)
		cl = 0;
	while (cl) {
		if (!cl->close) {
			cl = cl->nx;
			continue;
		}
		ci = mk_dci(cl->c);
		idx = cl->c == '~' ? cl->olen % 3 : (cl->open ? 3 : 0) + cl->olen % 3;
		op = cl->pv;
		found = 0;
		while (op && op != bot && op != floor[ci][idx]) {
			if (op->open && op->c == cl->c) {
				if (cl->c == '~') {
					found = op->len == cl->len;
				} else {
					odd = (cl->open || op->close) &&
					      (op->olen + cl->olen) % 3 == 0 &&
					      !(op->olen % 3 == 0 && cl->olen % 3 == 0);
					found = !odd;
				}
				if (found && S->maxd >= MK_DEPTH) {
					found = 0;
					break;
				}
				if (found)
					break;
			}
			op = op->pv;
		}
		old = cl;
		if (found) {
			cl = mk_wrap(S, op, cl);
		} else {
			cl = cl->nx;
			floor[ci][idx] = old->pv;
			if (!old->open)
				mk_rmdl(S, old);
		}
	}
	while (S->dl && S->dl != bot)
		mk_rmdl(S, S->dl);
}

/* [ or ![: text, and a bracket that a later ] may close. */
static void mk_open(mk_s *S, int img)
{
	mk_br *b = xm(sizeof *b);
	size_t k = img ? 2 : 1;

	memset(b, 0, sizeof *b);
	b->n = mk_txt(S, S->s + S->i, k, (long)S->i, (long)(S->i + k));
	b->pos = S->i + k;
	b->img = img;
	b->active = 1;
	b->dl = S->dl;
	b->pv = S->br;
	if (S->br)
		S->br->after = 1;
	S->br = b;
	S->i += k;
}

/* Take the innermost bracket off its stack. */
static void mk_popbr(mk_s *S)
{
	mk_br *b = S->br;

	S->br = b->pv;
	free(b);
}

/* Blanks, including line endings, from i. */
static size_t mk_ws(const char *s, size_t n, size_t i)
{
	while (i < n && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n'))
		i++;
	return i;
}

/* An inline link's (destination "title") from the ( at i: where it ends,
   or 0. */
static size_t mk_inline(mk_s *S, size_t i, str *url, str *title)
{
	const char *s = S->s;
	size_t n = S->n, j, k, e, t, te;

	j = mk_ws(s, n, i + 1);
	if (j < n && s[j] == ')')
		return j + 1;
	if (j < n && s[j] == '<' && j + 1 < n && s[j + 1] == '>')
		k = 2;
	else
		k = mk_linkdest(s + j, n - j, url);
	if (!k)
		return 0;
	e = j + k;
	t = mk_ws(s, n, e);
	te = t;
	if (t > e && t < n && (k = mk_linktitle(s + t, n - t, title)))
		te = t + k;
	else
		title->n = 0;
	te = mk_ws(s, n, te);
	if (te < n && s[te] == ')')
		return te + 1;
	url->n = 0;
	title->n = 0;
	return 0;
}

/* A link label from the [ at i, for a reference: its end, with where its
   text is in *a and *b; 0 when there is none. */
static size_t mk_reflab(mk_s *S, size_t i, size_t *a, size_t *b)
{
	const char *s = S->s;
	size_t j = i + 1;

	if (i >= S->n || s[i] != '[')
		return 0;
	while (j < S->n && s[j] != ']') {
		if (s[j] == '[')
			return 0;
		if (s[j] == '\\' && j + 1 < S->n)
			j++;
		j++;
		if (j - i - 1 > 999)
			return 0;
	}
	if (j >= S->n)
		return 0;
	*a = i + 1;
	*b = j;
	return j + 1;
}

/* ]: close the innermost bracket into a link or an image if it can be one,
   else the character. */
static void mk_close(mk_s *S)
{
	size_t start = S->i, i = S->i + 1, e, la = 0, lb = 0;
	mk_br *ob = S->br, *b;
	mk_n *l, *k, *nx;
	mk_ref *r;
	str url, title;
	int img, found = 0;

	if (!ob) {
		mk_txt(S, "]", 1, (long)start, (long)start + 1);
		S->i = i;
		return;
	}
	if (!ob->active) {
		mk_popbr(S);
		mk_txt(S, "]", 1, (long)start, (long)start + 1);
		S->i = i;
		return;
	}
	img = ob->img;
	s_init(&url);
	s_init(&title);
	if (i < S->n && S->s[i] == '(' && (e = mk_inline(S, i, &url, &title))) {
		found = 2;
		i = e;
	} else {
		e = mk_reflab(S, i, &la, &lb);
		if ((!e || la == lb) && !ob->after) {
			la = ob->pos;
			lb = start;
			if (!e)
				e = i;
		}
		if (e && lb > la && (r = mk_findref(S->refs, S->s + la, lb - la))) {
			s_cat(&url, r->url);
			if (r->title)
				s_cat(&title, r->title);
			found = r->title ? 3 : 1;
			i = e;
		}
	}
	if (found && S->maxd >= MK_DEPTH) {
		lg(HIBR_LDBG, "md: links past %d deep, the rest is text", MK_DEPTH);
		found = 0;
	}
	if (!found) {
		mk_popbr(S);
		s_free(&url);
		s_free(&title);
		mk_txt(S, "]", 1, (long)start, (long)start + 1);
		S->i = start + 1;
		return;
	}
	l = mk_new(img ? MK_IMG : MK_LINK);
	l->open = 0;
	l->dep = mk_span(ob->n->nx, 0);
	if (l->dep > S->maxd)
		S->maxd = l->dep;
	l->a = ob->n->a;
	l->b = (long)i;
	l->ca = (long)ob->pos;
	l->cb = (long)start;
	l->url = url;
	l->title = title;
	l->lt = found == 2 ? (title.n > 0) : found == 3;
	for (k = ob->n->nx; k; k = nx) {
		nx = k->nx;
		mk_unlink(k);
		mk_append(l, k);
	}
	mk_insafter(ob->n, l);
	mk_unlink(ob->n);
	mk_free(ob->n);
	{
		mk_n *save = S->leaf;
		mk_dl *bot = ob->dl;
		(void)save;
		mk_emph(S, bot);
	}
	mk_popbr(S);
	if (!img)
		for (b = S->br; b; b = b->pv)
			if (!b->img)
				b->active = 0;
	S->i = i;
}

/* Whether a byte begins something the inline parser looks at. */
static int mk_special(mk_s *S, char c)
{
	switch (c) {
	case '\n':
	case '`':
	case '\\':
	case '&':
	case '<':
	case '*':
	case '_':
	case '[':
	case '!':
	case ']':
		return 1;
	case '~':
		return S->gfm;
	}
	return 0;
}

/* One inline element at the current position. */
static void mk_inl1(mk_s *S)
{
	char c = S->s[S->i];
	size_t j, e;

	switch (c) {
	case '\n':
		mk_nl(S);
		return;
	case '`':
		mk_ticks(S);
		return;
	case '\\':
		mk_bslash(S);
		return;
	case '&':
		mk_amp(S);
		return;
	case '<':
		mk_angle(S);
		return;
	case '*':
	case '_':
		mk_delim(S);
		return;
	case '~':
		if (S->gfm) {
			mk_delim(S);
			return;
		}
		break;
	case '[':
		mk_open(S, 0);
		return;
	case '!':
		if (S->i + 1 < S->n && S->s[S->i + 1] == '[') {
			mk_open(S, 1);
			return;
		}
		mk_txt(S, "!", 1, (long)S->i, (long)S->i + 1);
		S->i++;
		return;
	case ']':
		mk_close(S);
		return;
	}
	j = S->i + 1;
	while (j < S->n && !mk_special(S, S->s[j]))
		j++;
	e = j;
	if (j < S->n && S->s[j] == '\n')
		while (e > S->i && mk_isspc(S->s[e - 1]))
			e--;
	if (e > S->i)
		mk_txt(S, S->s + S->i, e - S->i, (long)S->i, (long)e);
	S->i = j;
}

/* Whether a text node holds its content byte for byte. */
static int mk_literal(mk_n *n)
{
	return n->t == MK_TEXT && (long)n->s.n == n->cb - n->ca;
}

/* Join neighbouring text nodes that are one run of content. */
static void mk_merge(mk_n *up)
{
	mk_n *k, *nx;

	for (k = up->kid; k; k = k->nx) {
		while (mk_literal(k) && k->nx && mk_literal(k->nx) &&
		       k->cb == k->nx->ca) {
			nx = k->nx;
			s_add(&k->s, nx->s.p, nx->s.n);
			k->b = k->cb = nx->cb;
			mk_unlink(nx);
			mk_free(nx);
		}
		if (k->kid && k->t != MK_LINK && k->t != MK_IMG)
			mk_merge(k);
	}
}

/* Where an autolink's trailing punctuation starts: GFM's path validation. */
static size_t mk_trail(const char *d, size_t e)
{
	size_t i, o, c, ne;

	for (i = 0; i < e; i++)
		if (d[i] == '<') {
			e = i;
			break;
		}
	while (e > 0) {
		char ch = d[e - 1];
		if (strchr("?!.,:*_~'\"", ch)) {
			e--;
		} else if (ch == ';') {
			ne = e - 1;
			while (ne > 0 && mk_isan(d[ne - 1]))
				ne--;
			if (ne > 0 && ne < e - 1 && d[ne - 1] == '&')
				e = ne - 1;
			else
				e--;
		} else if (ch == ')') {
			o = c = 0;
			for (i = 0; i < e; i++) {
				if (d[i] == '(')
					o++;
				else if (d[i] == ')')
					c++;
			}
			if (c <= o)
				break;
			e--;
		} else {
			break;
		}
	}
	return e;
}

/* A valid domain at d: its length, or 0; how far it was read, in *seen,
   either way. A short one needs no period. */
static size_t mk_domain(const char *d, size_t n, int sh, size_t *seen)
{
	size_t i;
	int u1 = 0, u2 = 0, np = 0, l;
	unsigned c;

	for (i = 0; i < n;) {
		if (d[i] == '_') {
			u2++;
		} else if (d[i] == '.') {
			u1 = u2;
			u2 = 0;
			np++;
		} else if (d[i] != '-') {
			c = mk_utf8(d + i, n - i, &l);
			if (mk_isspace(c) || mk_ispunct(c))
				break;
			i += l;
			continue;
		}
		i++;
	}
	*seen = i;
	if (u1 || u2)
		return 0;
	if (!i || (!sh && !np))
		return 0;
	return i;
}

/* An extended www or url autolink at d: its length, or 0 with how much
   cannot start one either in *skip. */
static size_t mk_wwwurl(const char *d, size_t n, int *www, size_t *skip)
{
	static const char *sch[] = { "http://", "https://", "ftp://", 0 };
	size_t k = 0, dl, e, seen;
	int i;

	*www = 0;
	*skip = 1;
	if (n >= 4 && !strncmp(d, "www.", 4)) {
		*www = 1;
	} else {
		for (i = 0; sch[i]; i++)
			if (n > strlen(sch[i]) && !strncmp(d, sch[i], strlen(sch[i]))) {
				k = strlen(sch[i]);
				break;
			}
		if (!sch[i])
			return 0;
	}
	dl = mk_domain(d + k, n - k, !*www, &seen);
	if (!dl) {
		*skip = k + seen > 1 ? k + seen : 1;
		return 0;
	}
	e = k + dl;
	while (e < n && !mk_isspace((unsigned char)d[e]) && d[e] != '<')
		e++;
	e = mk_trail(d, e);
	return e > k ? e : 0;
}

/* An extended email autolink whose @ is at d[at], its local part no further
   back than d[lo]: where it starts in *a, its end, or 0. */
static size_t mk_email(const char *d, size_t n, size_t lo, size_t at, size_t *a)
{
	size_t s = at, e;
	int np = 0;

	while (s > lo && (mk_isan(d[s - 1]) || strchr(".+-_", d[s - 1])))
		s--;
	if (s == at || (s > 0 && d[s - 1] == '/'))
		return 0;
	for (e = at + 1; e < n; e++) {
		if (mk_isan(d[e]))
			continue;
		if (d[e] == '.' && e + 1 < n && mk_isan(d[e + 1])) {
			np++;
			continue;
		}
		if (d[e] != '-' && d[e] != '_')
			break;
	}
	if (e - at < 2 || !np || !mk_isal(d[e - 1]))
		return 0;
	*a = s;
	return e;
}

/* A text node for t's text from x to y, its content positions following. */
static mk_n *mk_piece(mk_n *t, size_t x, size_t y)
{
	mk_n *n = mk_new(MK_TEXT);

	n->open = 0;
	s_add(&n->s, t->s.p + x, y - x);
	n->a = n->ca = t->ca + (long)x;
	n->b = n->cb = t->ca + (long)y;
	return n;
}

/* Replace text node t with its pieces: plain text between the spans in sp
   (start, end, prefix, three to a span), a link for each span. */
static mk_n *mk_autosplit(mk_n *t, size_t *sp, int nsp)
{
	mk_n *at = t, *l, *nx = t->nx;
	size_t from = 0;
	int i;

	for (i = 0; i < nsp; i++) {
		size_t x = sp[3 * i], y = sp[3 * i + 1];
		if (x > from) {
			l = mk_piece(t, from, x);
			mk_insafter(at, l);
			at = l;
		}
		l = mk_new(MK_LINK);
		l->open = 0;
		s_cat(&l->url, sp[3 * i + 2] == 1 ? "http://" :
			       sp[3 * i + 2] == 2 ? "mailto:" : "");
		s_add(&l->url, t->s.p + x, y - x);
		l->a = l->ca = t->ca + (long)x;
		l->b = l->cb = t->ca + (long)y;
		mk_append(l, mk_piece(t, x, y));
		mk_insafter(at, l);
		at = l;
		from = y;
	}
	if (from < t->s.n)
		mk_insafter(at, mk_piece(t, from, t->s.n));
	mk_unlink(t);
	mk_free(t);
	return nx;
}

/* Find GFM's extended autolinks in the text under up, outside links. */
static void mk_autol(mk_s *S, mk_n *up)
{
	mk_n *k, *nx;
	size_t i, e, a, skip, lo, *sp = 0, cap = 0;
	int www, nsp;
	char pc, c;

	for (k = up->kid; k; k = nx) {
		nx = k->nx;
		if (k->t == MK_LINK || k->t == MK_IMG)
			continue;
		if (k->kid) {
			mk_autol(S, k);
			continue;
		}
		if (!mk_literal(k))
			continue;
		nsp = 0;
		lo = 0;
		for (i = 0; i < k->s.n;) {
			long ci = k->ca + (long)i;
			size_t x = 0, y = 0, pre = 0;
			c = k->s.p[i];
			if (c == 'w' || c == 'h' || c == 'f') {
				pc = ci > 0 ? S->s[ci - 1] : ' ';
				if (!(pc == ' ' || pc == '\t' || pc == '\n' ||
				      strchr("*_~(", pc))) {
					i++;
					continue;
				}
				e = mk_wwwurl(k->s.p + i, k->s.n - i, &www, &skip);
				if (!e) {
					i += skip;
					continue;
				}
				x = i;
				y = i + e;
				pre = www ? 1 : 0;
			} else if (c == '@') {
				e = mk_email(k->s.p, k->s.n, lo, i, &a);
				if (!e) {
					i++;
					continue;
				}
				x = a;
				y = e;
				pre = 2;
			} else {
				i++;
				continue;
			}
			if ((size_t)(nsp + 1) * 3 > cap) {
				cap = cap ? cap * 2 : 48;
				sp = xr(sp, cap * sizeof *sp);
			}
			sp[3 * nsp] = x;
			sp[3 * nsp + 1] = y;
			sp[3 * nsp + 2] = pre;
			nsp++;
			i = lo = y;
		}
		if (nsp)
			nx = mk_autosplit(k, sp, nsp);
	}
	free(sp);
}

/* Parse a leaf's content into inline nodes under it. */
void mk_inlines(mk_n *leaf, vec *refs, int gfm)
{
	mk_s S;

	memset(&S, 0, sizeof S);
	S.leaf = leaf;
	S.s = leaf->s.p ? leaf->s.p : "";
	S.n = leaf->s.n;
	S.refs = refs;
	S.gfm = gfm;
	while (S.n && strchr(" \t\n\r", S.s[S.n - 1]))
		S.n--;
	while (S.i < S.n)
		mk_inl1(&S);
	mk_emph(&S, 0);
	while (S.br)
		mk_popbr(&S);
	if (gfm) {
		mk_merge(leaf);
		mk_autol(&S, leaf);
	}
}
