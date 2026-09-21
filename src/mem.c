#include "pri.h"
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <string.h>
#include <stdarg.h>

int hibr_lv = HIBR_LWRN;

/* Emit a diagnostic line when its level is enabled. */
void lg(int lv, const char *f, ...)
{
	va_list ap;

	if (lv > hibr_lv)
		return;
	fputs("hibr: ", stderr);
	va_start(ap, f);
	vfprintf(stderr, f, ap);
	va_end(ap);
	fputc('\n', stderr);
}

/* Allocate heap memory or terminate. */
void *xm(size_t n)
{
	void *p = malloc(n);

	if (!p) {
		lg(HIBR_LERR, "out of memory (%lu bytes)", (unsigned long)n);
		exit(70);
	}
	return p;
}

/* Resize heap memory or terminate. */
void *xr(void *p, size_t n)
{
	void *q = realloc(p, n);

	if (!q) {
		lg(HIBR_LERR, "out of memory (%lu bytes)", (unsigned long)n);
		exit(70);
	}
	return q;
}

/* Duplicate a string on the heap. */
char *xs(const char *s)
{
	size_t n = strlen(s) + 1;
	char *p = xm(n);

	memcpy(p, s, n);
	return p;
}

/* Create an arena with the given default chunk size. */
arena *ar_new(size_t ch)
{
	arena *a = xm(sizeof *a);

	a->b = 0;
	a->ch = ch ? ch : HIBR_ARCH;
	return a;
}

/* Push a fresh block able to hold n bytes. */
blk *ar_blk(arena *a, size_t n)
{
	size_t c = a->ch;
	blk *b;

	while (c < n)
		c <<= 1;
	b = xm(sizeof *b + c);
	b->cap = c;
	b->use = 0;
	b->nx = a->b;
	a->b = b;
	lg(HIBR_LTRC, "arena block %lu", (unsigned long)c);
	return b;
}

/* Bump allocate zeroed memory from an arena. */
void *ar_alloc(arena *a, size_t n)
{
	blk *b = a->b;
	char *p;

	n = (n + 15) & ~(size_t)15;
	if (!b || b->cap - b->use < n)
		b = ar_blk(a, n);
	p = b->d + b->use;
	b->use += n;
	memset(p, 0, n);
	return p;
}

/* Copy n bytes into an arena as a NUL terminated string. */
char *ar_dup(arena *a, const char *s, size_t n)
{
	char *p = ar_alloc(a, n + 1);

	if (n)
		memcpy(p, s, n);
	return p;
}

/* Record the current arena high water position. */
amark ar_mark(arena *a)
{
	amark m;

	m.b = a->b;
	m.use = a->b ? a->b->use : 0;
	return m;
}

/* Release every allocation made after a mark. */
void ar_rel(arena *a, amark m)
{
	blk *n;

	while (a->b && a->b != m.b) {
		n = a->b->nx;
		free(a->b);
		a->b = n;
	}
	if (a->b)
		a->b->use = m.use;
}

/* Drop all but the newest block and rewind it. */
void ar_reset(arena *a)
{
	blk *n;

	while (a->b && a->b->nx) {
		n = a->b->nx;
		free(a->b);
		a->b = n;
	}
	if (a->b)
		a->b->use = 0;
}

/* Destroy an arena and every block it owns. */
void ar_free(arena *a)
{
	blk *n;

	while (a->b) {
		n = a->b->nx;
		free(a->b);
		a->b = n;
	}
	free(a);
}

/* Reset a dynamic string to empty. */
void s_init(str *s)
{
	s->p = 0;
	s->n = 0;
	s->cap = 0;
}

/* Ensure a dynamic string can take n more bytes. */
void s_grow(str *s, size_t n)
{
	size_t c;

	if (s->p && s->n + n + 1 <= s->cap)
		return;
	c = s->cap ? s->cap : 32;
	while (c < s->n + n + 1)
		c <<= 1;
	s->p = xr(s->p, c);
	s->cap = c;
}

/* Append n bytes to a dynamic string. */
void s_add(str *s, const char *p, size_t n)
{
	if (!s->p || s->n + n + 1 > s->cap)
		s_grow(s, n);
	if (n)
		memcpy(s->p + s->n, p, n);
	s->n += n;
	s->p[s->n] = 0;
}

/* Append a NUL terminated string. */
void s_cat(str *s, const char *p)
{
	s_add(s, p, strlen(p));
}

/* Append one character. */
void s_ch(str *s, int c)
{
	if (!s->p || s->n + 2 > s->cap)
		s_grow(s, 1);
	s->p[s->n++] = (char)c;
	s->p[s->n] = 0;
}

/* Release a dynamic string. */
void s_free(str *s)
{
	free(s->p);
	s->p = 0;
	s->n = 0;
	s->cap = 0;
}

/* Append a pointer to a dynamic vector. */
void v_add(vec *v, void *x)
{
	if (v->n == v->cap) {
		v->cap = v->cap ? v->cap * 2 : 8;
		v->p = xr(v->p, v->cap * sizeof *v->p);
	}
	v->p[v->n++] = x;
}

/* Release a dynamic vector. */
void v_free(vec *v)
{
	free(v->p);
	v->p = 0;
	v->n = 0;
	v->cap = 0;
}

/* Borrow a scratch string from the shell pool. */
str *sb_get(sh *s)
{
	str *b;

	if (s->sbf.n) {
		b = (str *)s->sbf.p[--s->sbf.n];
		b->n = 0;
		if (b->p)
			b->p[0] = 0;
		return b;
	}
	b = xm(sizeof *b);
	s_init(b);
	return b;
}

/* Return a scratch string to the shell pool. */
void sb_put(sh *s, str *b)
{
	v_add(&s->sbf, b);
}

/* Borrow a scratch vector from the shell pool. */
vec *vb_get(sh *s)
{
	vec *v;

	if (s->vbf.n) {
		v = (vec *)s->vbf.p[--s->vbf.n];
		v->n = 0;
		return v;
	}
	v = xm(sizeof *v);
	v->p = 0;
	v->n = 0;
	v->cap = 0;
	return v;
}

/* Return a scratch vector to the shell pool. */
void vb_put(sh *s, vec *v)
{
	v_add(&s->vbf, v);
}

/* Append a signed number in decimal. */
void s_num(str *s, long v)
{
	unsigned long u = v < 0 ? -(unsigned long)v : (unsigned long)v;
	unsigned long t = u;
	size_t d = 1, i;

	while (t >= 10) {
		t /= 10;
		d++;
	}
	if (v < 0)
		s_ch(s, '-');
	s_grow(s, d);
	i = s->n + d;
	s->n = i;
	s->p[i] = 0;
	t = u;
	do {
		s->p[--i] = (char)('0' + (t % 10));
		t /= 10;
	} while (t);
}
