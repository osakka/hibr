#include "pri.h"
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <errno.h>

int hibr_lv = HIBR_LWRN;
sh *lg_sh;
unsigned lg_ln, lg_col;

/* Append text to a JSON string's contents, escaped. */
void lg_jstr(str *o, const char *t)
{
	for (; *t; t++) {
		unsigned char c = (unsigned char)*t;
		if (c == '"' || c == '\\') {
			s_ch(o, '\\');
			s_ch(o, (char)c);
		} else if (c == '\n') {
			s_cat(o, "\\n");
		} else if (c == '\t') {
			s_cat(o, "\\t");
		} else if (c < 0x20) {
			s_cat(o, "\\u00");
			s_ch(o, "0123456789abcdef"[c >> 4]);
			s_ch(o, "0123456789abcdef"[c & 15]);
		} else {
			s_ch(o, (char)c);
		}
	}
}

/* Line n of a file, without its newline, or null when it cannot be read. */
char *lg_line(const char *path, unsigned n)
{
	FILE *f = fopen(path, "r");
	str l;
	int c;
	unsigned at = 1;

	if (!f)
		return 0;
	s_init(&l);
	while ((c = fgetc(f)) != EOF && at <= n) {
		if (c == '\n') {
			at++;
			continue;
		}
		if (at == n)
			s_ch(&l, (char)c);
	}
	fclose(f);
	if (at < n) {
		s_free(&l);
		return 0;
	}
	return l.p ? l.p : xs("");
}

/* One line of JSON keyed by key: the message, and where it happened when that is known. */
void lg_jline(str *o, const char *key, const char *m)
{
	char *src;
	const char *file = lg_sh->src ? lg_sh->src : "command line";
	unsigned ln = lg_ln ? lg_ln : lg_sh->ln;

	s_cat(o, "{\"");
	s_cat(o, key);
	s_cat(o, "\":\"");
	lg_jstr(o, m);
	s_cat(o, "\",\"file\":\"");
	lg_jstr(o, file);
	s_ch(o, '"');
	if (ln) {
		s_cat(o, ",\"line\":");
		s_num(o, (long)ln);
	}
	if (lg_col) {
		s_cat(o, ",\"col\":");
		s_num(o, (long)lg_col);
	}
	src = ln && lg_sh->src ? lg_line(lg_sh->src, ln) : 0;
	if (src) {
		s_cat(o, ",\"source\":\"");
		lg_jstr(o, src);
		s_ch(o, '"');
		free(src);
	}
	s_cat(o, "}\n");
}

/* One diagnostic as a line of JSON, for agent mode, keyed by its level's name. */
void lg_json(int lv, const char *f, va_list ap)
{
	static const char *lvn[] = { "error", "warning", "info", "debug",
				     "trace" };
	va_list ap2;
	str o;
	char *m;
	int n;

	va_copy(ap2, ap);
	n = vsnprintf(0, 0, f, ap2);
	va_end(ap2);
	m = xm((size_t)(n < 0 ? 0 : n) + 1);
	vsnprintf(m, (size_t)(n < 0 ? 0 : n) + 1, f, ap);
	s_init(&o);
	lg_jline(&o, lvn[lv < 0 ? 0 : lv > 4 ? 4 : lv], m);
	fputs(o.p, stderr);
	s_free(&o);
	free(m);
}

/* Emit a diagnostic line when its level is enabled. */
void lg(int lv, const char *f, ...)
{
	va_list ap;

	if (lv > hibr_lv)
		return;
	fflush(stdout);
	va_start(ap, f);
	if (lg_sh && (lg_sh->sopt & O_AGENT))
		lg_json(lv, f, ap);
	else {
		fputs("hibr: ", stderr);
		vfprintf(stderr, f, ap);
		fputc('\n', stderr);
	}
	va_end(ap);
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
	a->fr = 0;
	a->nfr = 0;
	a->ch = ch ? ch : HIBR_ARCH;
	return a;
}

/* Push a fresh block able to hold n bytes. */
blk *ar_blk(arena *a, size_t n)
{
	size_t c = a->ch;
	blk *b;

	if (a->fr && a->fr->cap >= n) {
		b = a->fr;
		a->fr = b->nx;
		a->nfr--;
	} else {
		while (c < n)
			c <<= 1;
		b = xm(sizeof *b + c);
		b->cap = c;
		lg(HIBR_LTRC, "arena block %lu", (unsigned long)c);
	}
	b->use = 0;
	b->nx = a->b;
	a->b = b;
	return b;
}

/* Keep a released block for the next command, or give it back. */
void ar_drop(arena *a, blk *b)
{
	if (a->nfr < HIBR_ARKEEP && b->cap <= a->ch) {
		b->nx = a->fr;
		a->fr = b;
		a->nfr++;
		return;
	}
	free(b);
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
		ar_drop(a, a->b);
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
		ar_drop(a, a->b);
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
	while (a->fr) {
		n = a->fr->nx;
		free(a->fr);
		a->fr = n;
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

/* read, retried when a signal interrupts it. An interrupted read is not an
   end of input, and every loop that stops at one loses whatever was still
   coming: in a command substitution the parent closed the pipe and the
   child died of SIGPIPE, so `$(cmd)` came back **empty, with status 141**,
   whenever a signal landed in the read. A script's own trap is installed
   without SA_RESTART on purpose -- bash interrupts a blocking `read`
   builtin so the trap can run -- so a read that must not lose data cannot
   rely on the flag and retries here instead. Gitea #144. */
ssize_t io_rdall(int fd, void *buf, size_t n)
{
	ssize_t r;

	for (;;) {
		r = read(fd, buf, n);
		if (r >= 0 || errno != EINTR)
			return r;
	}
}
