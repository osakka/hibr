#include "pr.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

struct colr { const char *nm; int code; };

const struct colr pr_cols[] = {
	{ "black", 0 }, { "red", 1 }, { "green", 2 }, { "yellow", 3 },
	{ "blue", 4 }, { "purple", 5 }, { "magenta", 5 }, { "cyan", 6 },
	{ "white", 7 }
};

/* Read one hexadecimal digit, or -1. */
int pr_hex(int c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}

/* Append the SGR parameters naming one colour, true when understood. */
int pr_col(str *o, const char *v, size_t n, int bg)
{
	size_t i;
	int br = 0, all = 1;

	if (n > 7 && !strncmp(v, "bright-", 7)) {
		br = 1;
		v += 7;
		n -= 7;
	}
	if (n == 7 && *v == '#') {
		int r = pr_hex(v[1]) * 16 + pr_hex(v[2]);
		int g = pr_hex(v[3]) * 16 + pr_hex(v[4]);
		int b = pr_hex(v[5]) * 16 + pr_hex(v[6]);
		if (r < 0 || g < 0 || b < 0)
			return 0;
		s_cat(o, bg ? ";48;2;" : ";38;2;");
		s_num(o, r);
		s_ch(o, ';');
		s_num(o, g);
		s_ch(o, ';');
		s_num(o, b);
		return 1;
	}
	for (i = 0; i < n; i++)
		if (!isdigit((unsigned char)v[i]))
			all = 0;
	if (all && n) {
		s_cat(o, bg ? ";48;5;" : ";38;5;");
		s_num(o, atol(v));
		return 1;
	}
	for (i = 0; i < sizeof pr_cols / sizeof pr_cols[0]; i++)
		if (strlen(pr_cols[i].nm) == n &&
		    !strncmp(pr_cols[i].nm, v, n)) {
			s_ch(o, ';');
			s_num(o, (bg ? 40 : 30) + pr_cols[i].code + (br ? 60 : 0));
			return 1;
		}
	return 0;
}

/* Build the escape sequence described by a style string. */
void pr_sgr(const char *style, str *o)
{
	str b;
	const char *p = style;

	s_init(&b);
	while (p && *p) {
		const char *q = p;
		size_t n;
		while (*q && *q != ' ')
			q++;
		n = (size_t)(q - p);
		if (n == 4 && !strncmp(p, "bold", 4))
			s_cat(&b, ";1");
		else if (n == 6 && !strncmp(p, "dimmed", 6))
			s_cat(&b, ";2");
		else if (n == 6 && !strncmp(p, "italic", 6))
			s_cat(&b, ";3");
		else if (n == 9 && !strncmp(p, "underline", 9))
			s_cat(&b, ";4");
		else if (n == 5 && !strncmp(p, "blink", 5))
			s_cat(&b, ";5");
		else if (n == 8 && !strncmp(p, "inverted", 8))
			s_cat(&b, ";7");
		else if (n == 13 && !strncmp(p, "strikethrough", 13))
			s_cat(&b, ";9");
		else if (n == 4 && !strncmp(p, "none", 4)) {
			s_free(&b);
			s_init(&b);
		} else if (n > 3 && !strncmp(p, "fg:", 3))
			pr_col(&b, p + 3, n - 3, 0);
		else if (n > 3 && !strncmp(p, "bg:", 3))
			pr_col(&b, p + 3, n - 3, 1);
		else if (n)
			pr_col(&b, p, n, 0);
		p = *q ? q + 1 : q;
	}
	if (b.n) {
		s_cat(o, "\033[0");
		s_add(o, b.p, b.n);
		s_ch(o, 'm');
	}
	s_free(&b);
}

/* Wrap text in a style, restoring it after any nested reset. */
char *pr_wrap(const char *style, const char *body)
{
	str sg, o;
	const char *p = body;

	s_init(&sg);
	pr_sgr(style, &sg);
	if (!sg.n || !*body) {
		s_free(&sg);
		return xs(body);
	}
	s_init(&o);
	s_add(&o, sg.p, sg.n);
	while (*p) {
		if (!strncmp(p, "\033[0m", 4)) {
			s_cat(&o, "\033[0m");
			s_add(&o, sg.p, sg.n);
			p += 4;
			continue;
		}
		s_ch(&o, *p++);
	}
	s_cat(&o, "\033[0m");
	s_free(&sg);
	return o.p;
}

/* Copy a span of bytes into fresh memory. */
char *pr_span(const char *b, const char *e)
{
	char *r = xm((size_t)(e - b) + 1);

	memcpy(r, b, (size_t)(e - b));
	r[e - b] = 0;
	return r;
}

/* Render a format string up to a stop character, reporting filled variables. */
char *pr_fmt(pctx *c, const char **pp, int stop, int depth, int *any,
	     plook lk, void *ud)
{
	const char *p = *pp;
	str o;

	s_init(&o);
	s_grow(&o, 1);
	o.p[0] = 0;
	if (depth > PR_DEPTH) {
		lg(HIBR_LERR, "prompt: format nested deeper than %d", PR_DEPTH);
		*pp = p + strlen(p);
		return o.p;
	}
	while (*p && *p != stop) {
		if (*p == '\\' && p[1]) {
			p++;
			if (*p == 'n')
				s_ch(&o, '\n');
			else if (*p == 't')
				s_ch(&o, '\t');
			else if (*p == 'e')
				s_ch(&o, 27);
			else
				s_ch(&o, *p);
			p++;
			continue;
		}
		if (*p == '$' && (isalnum((unsigned char)p[1]) || p[1] == '_')) {
			const char *b = ++p;
			char *nm, *v;
			while (*p && (isalnum((unsigned char)*p) || *p == '_'))
				p++;
			nm = pr_span(b, p);
			v = lk(c, ud, nm);
			free(nm);
			if (v) {
				if (*v)
					*any = 1;
				s_cat(&o, v);
				free(v);
			}
			continue;
		}
		if (*p == '[') {
			int in = 0;
			char *t, *w;
			p++;
			t = pr_fmt(c, &p, ']', depth + 1, &in, lk, ud);
			if (*p == ']')
				p++;
			if (*p == '(') {
				int sin = 0;
				char *sty;
				p++;
				sty = pr_fmt(c, &p, ')', depth + 1, &sin, lk, ud);
				if (*p == ')')
					p++;
				w = pr_wrap(sty, t);
				free(sty);
				free(t);
			} else {
				w = t;
			}
			s_cat(&o, w);
			free(w);
			if (in)
				*any = 1;
			continue;
		}
		if (*p == '(') {
			int in = 0;
			char *t;
			p++;
			t = pr_fmt(c, &p, ')', depth + 1, &in, lk, ud);
			if (*p == ')')
				p++;
			if (in) {
				s_cat(&o, t);
				*any = 1;
			}
			free(t);
			continue;
		}
		s_ch(&o, *p++);
	}
	*pp = p;
	return o.p;
}
