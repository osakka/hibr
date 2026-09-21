#include "pr.h"
#include <stdlib.h>
#include <string.h>

/* Match a glob against one path, where a star never crosses a slash. */
int ig_wild(const char *p, const char *t, int depth)
{
	if (depth > 64)
		return 0;
	while (*p) {
		if (p[0] == '*' && p[1] == '*') {
			const char *q = p + 2;
			if (*q == '/') {
				q++;
				for (;;) {
					if (ig_wild(q, t, depth + 1))
						return 1;
					while (*t && *t != '/')
						t++;
					if (!*t)
						return 0;
					t++;
				}
			}
			if (!*q)
				return 1;
			p++;
			continue;
		}
		if (*p == '*') {
			p++;
			for (;;) {
				if (ig_wild(p, t, depth + 1))
					return 1;
				if (!*t || *t == '/')
					return 0;
				t++;
			}
		}
		if (*p == '?') {
			if (!*t || *t == '/')
				return 0;
			p++;
			t++;
			continue;
		}
		if (*p == '[') {
			const char *q = p + 1;
			int neg = 0, hit = 0;
			if (*q == '!' || *q == '^') {
				neg = 1;
				q++;
			}
			if (!*t || *t == '/')
				return 0;
			for (; *q && *q != ']'; q++) {
				if (q[1] == '-' && q[2] && q[2] != ']') {
					if ((unsigned char)*t >=
						    (unsigned char)*q &&
					    (unsigned char)*t <=
						    (unsigned char)q[2])
						hit = 1;
					q += 2;
				} else if (*q == *t) {
					hit = 1;
				}
			}
			if (*q != ']')
				return 0;
			if (hit == neg)
				return 0;
			p = q + 1;
			t++;
			continue;
		}
		if (*p == '\\' && p[1])
			p++;
		if (*p != *t)
			return 0;
		p++;
		t++;
	}
	return !*t;
}

/* Parse one ignore file into a list of patterns. */
void ig_load(const char *text, vec *out)
{
	const char *p = text;

	while (p && *p) {
		const char *b = p, *e;
		struct ig *g;
		size_t n;
		while (*p && *p != '\n')
			p++;
		e = p;
		if (*p)
			p++;
		while (e > b && (e[-1] == '\r'))
			e--;
		while (e > b && e[-1] == ' ' && (e - 1 == b || e[-2] != '\\'))
			e--;
		if (e == b || *b == '#')
			continue;
		g = xm(sizeof *g);
		memset(g, 0, sizeof *g);
		if (*b == '!') {
			g->neg = 1;
			b++;
		}
		if (e > b && e[-1] == '/') {
			g->dironly = 1;
			e--;
		}
		if (e == b) {
			free(g);
			continue;
		}
		if (*b == '/') {
			g->anchored = 1;
			b++;
		}
		for (n = 0; b + n < e; n++)
			if (b[n] == '/' && b + n + 1 < e)
				g->anchored = 1;
		g->pat = pr_span(b, e);
		v_add(out, g);
	}
}

/* Release a list of patterns. */
void ig_free(vec *v)
{
	size_t i;

	for (i = 0; i < v->n; i++) {
		struct ig *g = (struct ig *)v->p[i];
		free(g->pat);
		free(g);
	}
	v_free(v);
}

/* Decide whether a path is ignored, later rules winning over earlier. */
int ig_test(vec *stack, const char *rel, const char *name, int isdir)
{
	size_t i, j;
	int verdict = 0;

	for (i = 0; i < stack->n; i++) {
		struct iglev *l = (struct iglev *)stack->p[i];
		const char *sub = rel + l->base;
		for (j = 0; j < l->pats.n; j++) {
			struct ig *g = (struct ig *)l->pats.p[j];
			int hit;
			if (g->dironly && !isdir)
				continue;
			hit = g->anchored ? ig_wild(g->pat, sub, 0)
					  : ig_wild(g->pat, name, 0);
			if (hit)
				verdict = !g->neg;
		}
	}
	return verdict;
}
