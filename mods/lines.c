#define _GNU_SOURCE

#include "hibr.h"
#include "re.h"
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <fnmatch.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef LI_BIN
#define LI_BIN 8000
#endif

typedef struct li_grep li_grep;
struct li_grep {
	re_t re;
	char *pat, *lpat, *glob;
	size_t pn;
	int fixed, icase, names, before, after, hits, sep;
};

int pl_prog(sh *s, char **av);

/* Read a whole descriptor into o; 0 on success. */
int li_slurp(int fd, str *o)
{
	ssize_t n;

	for (;;) {
		s_grow(o, HIBR_IOCH);
		n = read(fd, o->p + o->n, o->cap - o->n - 1);
		if (n < 0 && errno == EINTR)
			continue;
		if (n < 0)
			return -1;
		if (n == 0)
			break;
		o->n += (size_t)n;
	}
	s_grow(o, 1);
	o->p[o->n] = 0;
	return 0;
}

/* Read a whole file into o; 0 on success, the reason logged otherwise. */
int li_file(const char *who, const char *path, str *o)
{
	int fd = open(path, O_RDONLY), r;

	if (fd < 0) {
		lg(HIBR_LERR, "lines %s: %s: %s", who, path, strerror(errno));
		return -1;
	}
	r = li_slurp(fd, o);
	if (r < 0)
		lg(HIBR_LERR, "lines %s: %s: %s", who, path, strerror(errno));
	close(fd);
	return r;
}

/* A whole number argument, or -1. */
long li_num(const char *t)
{
	char *e;
	long v;

	if (!t || !*t)
		return -1;
	v = strtol(t, &e, 10);
	return *e || v < 0 ? -1 : v;
}

/* lines show [-n] FILE FROM [TO]: lines FROM to TO, byte for byte; TO may be $. */
int li_show(sh *s, int ac, char **av)
{
	str f;
	long a, b, ln = 1;
	size_t i = 0, e;
	int num = 0, k = 2;

	(void)s;
	if (k < ac && !strcmp(av[k], "-n")) {
		num = 1;
		k++;
	}
	if (ac - k < 2 || ac - k > 3) {
		lg(HIBR_LERR, "usage: lines show [-n] file from [to]");
		return 2;
	}
	a = li_num(av[k + 1]);
	b = ac - k == 3 ? (!strcmp(av[k + 2], "$") ? 0x7fffffffL : li_num(av[k + 2])) : a;
	if (a < 1 || b < a) {
		lg(HIBR_LERR, "lines show: %s %s: not a range of lines",
		   av[k + 1], ac - k == 3 ? av[k + 2] : "");
		return 2;
	}
	s_init(&f);
	if (li_file("show", av[k], &f) < 0) {
		s_free(&f);
		return HIBR_FAIL;
	}
	while (i < f.n && ln <= b) {
		e = i;
		while (e < f.n && f.p[e] != '\n')
			e++;
		if (ln >= a) {
			if (num)
				printf("%ld\t", ln);
			fwrite(f.p + i, 1, e - i + (e < f.n), stdout);
		}
		i = e + 1;
		ln++;
	}
	s_free(&f);
	return HIBR_OK;
}

/* Count the times t occurs in h, not overlapping. */
size_t li_count(const char *h, size_t hn, const char *t, size_t tn)
{
	size_t n = 0;
	const char *p = h, *q;

	if (!tn)
		return 0;
	while ((q = memmem(p, hn - (size_t)(p - h), t, tn))) {
		n++;
		p = q + tn;
	}
	return n;
}

/* lines count FILE TEXT: how many times a literal text occurs. */
int li_cnt(sh *s, int ac, char **av)
{
	str f;

	(void)s;
	if (ac != 4) {
		lg(HIBR_LERR, "usage: lines count file text");
		return 2;
	}
	s_init(&f);
	if (li_file("count", av[2], &f) < 0) {
		s_free(&f);
		return HIBR_FAIL;
	}
	printf("%zu\n", li_count(f.p, f.n, av[3], strlen(av[3])));
	s_free(&f);
	return HIBR_OK;
}

/* Whether one line matches the search; the line is NUL-ended in place. */
int li_hit(li_grep *g, char *l, size_t n, str *low)
{
	size_t i;

	if (!g->fixed)
		return regexec(&g->re, l, 0, 0, 0) == 0;
	if (!g->icase)
		return memmem(l, n, g->pat, g->pn) != 0;
	low->n = 0;
	s_add(low, l, n);
	for (i = 0; i < n; i++)
		low->p[i] = (char)tolower((unsigned char)low->p[i]);
	return memmem(low->p, n, g->lpat, g->pn) != 0;
}

/* Print one line of a file, as a match (:) or as context (-). */
void li_out(li_grep *g, const char *path, long ln, const char *l, size_t n, int ctx)
{
	if (g->sep)
		fputs("--\n", stdout);
	g->sep = 0;
	printf("%s%c%ld%c", path, ctx ? '-' : ':', ln, ctx ? '-' : ':');
	fwrite(l, 1, n, stdout);
	fputc('\n', stdout);
}

/* Search one file, printing grep's file:line:text with context. */
void li_one(li_grep *g, const char *path)
{
	str f, low;
	vec st = { 0, 0, 0 };
	size_t i, e, k, n;
	long ln, last = 0, after = 0, first;

	s_init(&f);
	if (li_file("grep", path, &f) < 0) {
		s_free(&f);
		return;
	}
	if (memchr(f.p, 0, f.n < LI_BIN ? f.n : LI_BIN) ||
	    (g->fixed && !g->icase && !memmem(f.p, f.n, g->pat, g->pn))) {
		s_free(&f);
		return;
	}
	s_init(&low);
	for (i = 0; i < f.n;) {
		e = i;
		while (e < f.n && f.p[e] != '\n')
			e++;
		v_add(&st, (void *)i);
		i = e + 1;
	}
	n = st.n;
	for (k = 0; k < n; k++) {
		i = (size_t)st.p[k];
		e = k + 1 < n ? (size_t)st.p[k + 1] - 1 : f.n;
		if (e > i && e == f.n && f.p[e - 1] == '\n')
			e--;
		ln = (long)k + 1;
		f.p[e] = 0;
		if (li_hit(g, f.p + i, e - i, &low)) {
			if (g->names) {
				printf("%s\n", path);
				g->hits++;
				break;
			}
			first = ln - g->before;
			if (first <= last)
				first = last + 1;
			if (first < 1)
				first = 1;
			if (g->hits && first > last + 1 && (g->before || g->after))
				g->sep = 1;
			for (; first < ln; first++) {
				size_t a = (size_t)st.p[first - 1];
				size_t b = (size_t)st.p[first] - 1;
				li_out(g, path, first, f.p + a, b - a, 1);
			}
			li_out(g, path, ln, f.p + i, e - i, 0);
			g->hits++;
			last = ln;
			after = g->after;
		} else if (after > 0) {
			li_out(g, path, ln, f.p + i, e - i, 1);
			last = ln;
			after--;
		}
		if (e < f.n)
			f.p[e] = '\n';
	}
	v_free(&st);
	s_free(&low);
	s_free(&f);
}

/* Compare two names for qsort. */
int li_cmp(const void *a, const void *b)
{
	return strcmp(*(char *const *)a, *(char *const *)b);
}

/* Search a path: a file, or a directory walked in name order, hidden ones skipped. */
void li_walk(li_grep *g, const char *path, int top)
{
	struct stat st;
	DIR *d;
	struct dirent *e;
	vec nm = { 0, 0, 0 };
	str p;
	size_t k;
	const char *b;

	if ((top ? stat(path, &st) : lstat(path, &st)) < 0) {
		if (top)
			lg(HIBR_LERR, "lines grep: %s: %s", path, strerror(errno));
		return;
	}
	if (S_ISREG(st.st_mode)) {
		b = strrchr(path, '/');
		b = b ? b + 1 : path;
		if (!g->glob || top || fnmatch(g->glob, b, 0) == 0)
			li_one(g, path);
		return;
	}
	if (!S_ISDIR(st.st_mode) || !(d = opendir(path)))
		return;
	while ((e = readdir(d)))
		if (e->d_name[0] != '.')
			v_add(&nm, xs(e->d_name));
	closedir(d);
	if (nm.n)
		qsort(nm.p, nm.n, sizeof *nm.p, li_cmp);
	s_init(&p);
	for (k = 0; k < nm.n; k++) {
		p.n = 0;
		s_cat(&p, path);
		if (p.n && p.p[p.n - 1] != '/')
			s_ch(&p, '/');
		s_cat(&p, nm.p[k]);
		li_walk(g, p.p, 0);
		free(nm.p[k]);
	}
	s_free(&p);
	v_free(&nm);
}

/* lines grep [-F] [-i] [-l] [-A n] [-B n] [-C n] [-g glob] PATTERN [PATH...] */
int li_grepbi(sh *s, int ac, char **av)
{
	li_grep g;
	int k = 2, rc;
	long v;
	size_t i;

	(void)s;
	memset(&g, 0, sizeof g);
	for (; k < ac && av[k][0] == '-' && av[k][1]; k++) {
		if (!strcmp(av[k], "--")) {
			k++;
			break;
		}
		if (!strcmp(av[k], "-F"))
			g.fixed = 1;
		else if (!strcmp(av[k], "-i"))
			g.icase = 1;
		else if (!strcmp(av[k], "-l"))
			g.names = 1;
		else if (!strcmp(av[k], "-g") && k + 1 < ac)
			g.glob = av[++k];
		else if ((!strcmp(av[k], "-A") || !strcmp(av[k], "-B") || !strcmp(av[k], "-C")) &&
			 k + 1 < ac && (v = li_num(av[k + 1])) >= 0) {
			if (av[k][1] != 'B')
				g.after = (int)v;
			if (av[k][1] != 'A')
				g.before = (int)v;
			k++;
		} else {
			lg(HIBR_LERR, "lines grep: %s: unknown option", av[k]);
			return 2;
		}
	}
	if (k >= ac) {
		lg(HIBR_LERR, "usage: lines grep [-F] [-i] [-l] [-A n] [-B n] [-C n] [-g glob] pattern [path...]");
		return 2;
	}
	g.pat = av[k++];
	g.pn = strlen(g.pat);
	if (!g.fixed && !strpbrk(g.pat, ".[]()*+?{}|^$\\"))
		g.fixed = 1;
	if (g.fixed && g.icase) {
		g.lpat = xs(g.pat);
		for (i = 0; i < g.pn; i++)
			g.lpat[i] = (char)tolower((unsigned char)g.lpat[i]);
	}
	if (!g.fixed && (rc = regcomp(&g.re, g.pat, HIBR_REG_EXTENDED |
				       (g.icase ? HIBR_REG_ICASE : 0)))) {
		lg(HIBR_LERR, "lines grep: %s: not a regular expression", g.pat);
		return 2;
	}
	if (k >= ac)
		li_walk(&g, ".", 1);
	for (; k < ac; k++)
		li_walk(&g, av[k], 1);
	if (!g.fixed)
		regfree(&g.re);
	free(g.lpat);
	fflush(stdout);
	return g.hits ? HIBR_OK : HIBR_FAIL;
}

/* Take one block's text: the lines from i up to the marker line m, joined. */
int li_part(const char *t, size_t n, size_t *i, const char *m, str *o)
{
	size_t e, ml = strlen(m);
	int any = 0;

	o->n = 0;
	while (*i < n) {
		e = *i;
		while (e < n && t[e] != '\n')
			e++;
		if (e - *i == ml && !memcmp(t + *i, m, ml)) {
			*i = e + 1;
			s_grow(o, 1);
			o->p[o->n] = 0;
			return 0;
		}
		if (any)
			s_ch(o, '\n');
		s_add(o, t + *i, e - *i);
		any = 1;
		*i = e + 1;
	}
	return -1;
}

/* lines edit [-n] FILE < blocks: each old text found once, all checked, then the file replaced whole. */
int li_edit(sh *s, int ac, char **av)
{
	str in, f, o, a, b, tmp;
	struct stat st;
	size_t i = 0, e, c, blk = 0, n;
	char *q;
	int dry = 0, k = 2, fd, rc = HIBR_FAIL;
	const char *path;

	if (k < ac && !strcmp(av[k], "-n")) {
		dry = 1;
		k++;
	}
	if (ac - k != 1) {
		lg(HIBR_LERR, "usage: lines edit [-n] file < blocks");
		return 2;
	}
	path = av[k];
	if (!dry && (s->sopt & O_PLAN) && !pl_prog(s, av))
		return HIBR_FAIL;
	s_init(&in);
	s_init(&f);
	s_init(&o);
	s_init(&a);
	s_init(&b);
	s_init(&tmp);
	if (li_slurp(0, &in) < 0 || li_file("edit", path, &f) < 0 || stat(path, &st) < 0)
		goto out;
	while (i < in.n) {
		e = i;
		while (e < in.n && in.p[e] != '\n')
			e++;
		if (e - i == 4 && !memcmp(in.p + i, "<<<<", 4)) {
			i = e + 1;
			blk++;
			if (li_part(in.p, in.n, &i, "====", &a) < 0 ||
			    li_part(in.p, in.n, &i, ">>>>", &b) < 0) {
				lg(HIBR_LERR, "lines edit: block %zu: no ==== and >>>> to end it", blk);
				goto out;
			}
			if (!a.n) {
				lg(HIBR_LERR, "lines edit: block %zu: nothing to find", blk);
				goto out;
			}
			c = li_count(f.p, f.n, a.p, a.n);
			if (c != 1) {
				q = memchr(a.p, '\n', a.n);
				lg(HIBR_LERR, "lines edit: %s: block %zu found %zu times: %.*s", path,
				   blk, c, (int)(q ? (size_t)(q - a.p) : a.n), a.p);
				goto out;
			}
			q = memmem(f.p, f.n, a.p, a.n);
			n = (size_t)(q - f.p);
			o.n = 0;
			s_add(&o, f.p, n);
			s_add(&o, b.p ? b.p : "", b.n);
			s_add(&o, q + a.n, f.n - n - a.n);
			f.n = 0;
			s_add(&f, o.p, o.n);
			continue;
		}
		if (e > i && strspn(in.p + i, " \t") < e - i) {
			lg(HIBR_LERR, "lines edit: text outside a block: %.*s", (int)(e - i), in.p + i);
			goto out;
		}
		i = e + 1;
	}
	if (!blk) {
		lg(HIBR_LERR, "lines edit: no <<<< block on standard input");
		goto out;
	}
	if (dry) {
		printf("%s: %zu change%s would apply\n", path, blk, blk == 1 ? "" : "s");
		rc = HIBR_OK;
		goto out;
	}
	s_cat(&tmp, path);
	s_cat(&tmp, ".lines-XXXXXX");
	fd = mkstemp(tmp.p);
	if (fd < 0) {
		lg(HIBR_LERR, "lines edit: %s: %s", tmp.p, strerror(errno));
		goto out;
	}
	if (fchmod(fd, st.st_mode & 07777) < 0 ||
	    write(fd, f.p, f.n) != (ssize_t)f.n || close(fd) < 0 ||
	    rename(tmp.p, path) < 0) {
		lg(HIBR_LERR, "lines edit: %s: %s", path, strerror(errno));
		unlink(tmp.p);
		goto out;
	}
	printf("%s: %zu change%s\n", path, blk, blk == 1 ? "" : "s");
	rc = HIBR_OK;
out:
	s_free(&in);
	s_free(&f);
	s_free(&o);
	s_free(&a);
	s_free(&b);
	s_free(&tmp);
	return rc;
}

/* lines show|grep|edit|count ... */
int li_bi(sh *s, int ac, char **av)
{
	const char *sub = ac > 1 ? av[1] : "";

	if (!strcmp(sub, "show"))
		return li_show(s, ac, av);
	if (!strcmp(sub, "grep"))
		return li_grepbi(s, ac, av);
	if (!strcmp(sub, "edit"))
		return li_edit(s, ac, av);
	if (!strcmp(sub, "count"))
		return li_cnt(s, ac, av);
	lg(HIBR_LERR, "usage: lines show|grep|edit|count ...");
	return 2;
}

const hibr_bi li_bis[] = {
	{ "lines", li_bi, "files by line: lines show|grep|edit|count" },
	HIBR_BI_END
};

HIBR_MODULE("lines", "1.0", "show, search and edit files by line, exactly",
	    li_bis, 0, 0);
