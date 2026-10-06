#define _GNU_SOURCE

#include "hibr.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct csv_rd csv_rd;
struct csv_rd {
	FILE *f;
	const char *t;
	size_t tn, ti;
	int sep, bom;
};

vec csv_open;

var *v_find(sh *s, const char *k);
void mp_ixbuild(ent *h, size_t n);

/* The next byte of a reader, or EOF. */
int csv_get(csv_rd *r)
{
	if (r->f)
		return getc(r->f);
	if (r->ti >= r->tn)
		return EOF;
	return (unsigned char)r->t[r->ti++];
}

/* Put one byte back. */
void csv_unget(csv_rd *r, int c)
{
	if (c == EOF)
		return;
	if (r->f)
		ungetc(c, r->f);
	else if (r->ti)
		r->ti--;
}

/* Free the fields of a record. */
void csv_clear(vec *fs)
{
	size_t i;

	for (i = 0; i < fs->n; i++)
		free(fs->p[i]);
	fs->n = 0;
}

/* Read one record into fs, RFC 4180: fields between separators, a field
   in double quotes may hold separators, line breaks and "" for a quote;
   a record ends at LF or CRLF outside quotes. 0 when there was none. */
int csv_rec(csv_rd *r, vec *fs)
{
	str f;
	int c, q = 0, any = 0, was = 0;

	csv_clear(fs);
	if (r->bom) {
		r->bom = 0;
		c = csv_get(r);
		if (c == 0xef) {
			int c2 = csv_get(r), c3 = csv_get(r);

			if (!(c2 == 0xbb && c3 == 0xbf)) {
				csv_unget(r, c3);
				csv_unget(r, c2);
				csv_unget(r, c);
			}
		} else {
			csv_unget(r, c);
		}
	}
	s_init(&f);
	for (;;) {
		c = csv_get(r);
		if (c == EOF)
			break;
		any = 1;
		if (q) {
			if (c == '"') {
				int n = csv_get(r);

				if (n == '"') {
					s_ch(&f, '"');
				} else {
					q = 0;
					csv_unget(r, n);
				}
			} else {
				s_ch(&f, c);
			}
			continue;
		}
		if (c == '"' && !f.n && !was) {
			q = 1;
			was = 1;
			continue;
		}
		if (c == r->sep) {
			v_add(fs, xs(f.p ? f.p : ""));
			f.n = 0;
			if (f.p)
				f.p[0] = 0;
			was = 0;
			continue;
		}
		if (c == '\r') {
			int n = csv_get(r);

			if (n == '\n')
				break;
			csv_unget(r, n);
			s_ch(&f, c);
			continue;
		}
		if (c == '\n')
			break;
		s_ch(&f, c);
	}
	if (!any) {
		s_free(&f);
		return 0;
	}
	v_add(fs, xs(f.p ? f.p : ""));
	s_free(&f);
	return 1;
}

/* A separator named on the command line: one character, or tab. */
int csv_sep(const char *v)
{
	if (!strcmp(v, "tab") || !strcmp(v, "\\t"))
		return '\t';
	return (unsigned char)v[0];
}

/* One field as it is written: quoted when it holds the separator, a quote
   or a line break, its quotes doubled. */
void csv_put(str *o, const char *v, int sep)
{
	const char *p;

	if (!strchr(v, sep) && !strpbrk(v, "\"\r\n")) {
		s_cat(o, v);
		return;
	}
	s_ch(o, '"');
	for (p = v; *p; p++) {
		if (*p == '"')
			s_ch(o, '"');
		s_ch(o, *p);
	}
	s_ch(o, '"');
}

/* A whole text or file into memory. */
int csv_slurp(const char *path, str *o)
{
	FILE *f = strcmp(path, "-") ? fopen(path, "rb") : stdin;
	char *buf;
	size_t n;

	if (!f) {
		lg(HIBR_LERR, "csv: %s: cannot read", path);
		return HIBR_FAIL;
	}
	buf = xm(HIBR_IOCH);
	while ((n = fread(buf, 1, HIBR_IOCH, f)) > 0)
		s_add(o, buf, n);
	free(buf);
	if (f != stdin)
		fclose(f);
	return HIBR_OK;
}

/* csv read [-s SEP] [-H] FILE | -t TEXT: every record. Bound, RET[i][j],
   or with -H the first record names the columns and RET[i][name]; printed,
   a record to a line, its fields between tabs. */
int csv_read(sh *s, int ac, char **av)
{
	csv_rd r;
	str in, o, k1, k2;
	vec fs = { 0, 0, 0 }, hd = { 0, 0, 0 };
	const char *path = 0, *text = 0;
	int i, head = 0, rc = HIBR_OK;
	size_t row = 0, j;
	var *v = 0;
	ent **tail = 0, *re, *fe, **ft;

	memset(&r, 0, sizeof r);
	r.sep = ',';
	r.bom = 1;
	for (i = 2; i < ac; i++) {
		if (!strcmp(av[i], "-s") && i + 1 < ac)
			r.sep = csv_sep(av[++i]);
		else if (!strcmp(av[i], "-H"))
			head = 1;
		else if (!strcmp(av[i], "-t") && i + 1 < ac)
			text = av[++i];
		else if (!path)
			path = av[i];
		else {
			lg(HIBR_LERR, "usage: csv read [-s sep] [-H] file|-t text");
			return 2;
		}
	}
	if (!path && !text) {
		lg(HIBR_LERR, "usage: csv read [-s sep] [-H] file|-t text");
		return 2;
	}
	s_init(&in);
	if (text) {
		r.t = text;
		r.tn = strlen(text);
	} else {
		if (csv_slurp(path, &in) != HIBR_OK) {
			s_free(&in);
			return HIBR_FAIL;
		}
		r.t = in.p ? in.p : "";
		r.tn = in.n;
	}
	s_init(&o);
	s_init(&k1);
	s_init(&k2);
	if (s->bind) {
		hibr_retn(s, 0, 0);
		v = v_find(s, "RET");
		if (v) {
			v->am = 1;
			for (tail = &v->map; *tail; tail = &(*tail)->nx)
				;
		}
	}
	if (head && csv_rec(&r, &hd) == 0) {
		s_free(&in);
		return HIBR_OK;
	}
	while (csv_rec(&r, &fs)) {
		if (!s->bind) {
			o.n = 0;
			for (j = 0; j < fs.n; j++) {
				if (j)
					s_ch(&o, '\t');
				s_cat(&o, fs.p[j]);
			}
			printf("%s\n", o.p ? o.p : "");
			row++;
			continue;
		}
		if (!v)
			break;
		k1.n = 0;
		s_num(&k1, (long)row);
		re = xm(sizeof *re);
		memset(re, 0, sizeof *re);
		re->k = xs(k1.p);
		ft = &re->map;
		for (j = 0; j < fs.n; j++) {
			k2.n = 0;
			if (head && j < hd.n && *(char *)hd.p[j])
				s_cat(&k2, hd.p[j]);
			else
				s_num(&k2, (long)j);
			fe = xm(sizeof *fe);
			memset(fe, 0, sizeof *fe);
			fe->k = xs(k2.p);
			fe->s = fs.p[j];
			fs.p[j] = 0;
			*ft = fe;
			ft = &fe->nx;
			re->n++;
		}
		fs.n = 0;
		*tail = re;
		tail = &re->nx;
		v->n++;
		row++;
	}
	/* The rows were linked here rather than through hibr_setp, which is
	   what makes 50,000 of them 0.24 s instead of three and a half minutes
	   -- but a chain built by hand has no index, so every later lookup in
	   the script would walk it. One pass now gives the whole map one
	   (Gitea #73); the shell is linked -rdynamic, so this is declared
	   above rather than reached through an interface. */
	if (v && v->map && row >= 16)
		mp_ixbuild(v->map, row);
	csv_clear(&fs);
	csv_clear(&hd);
	v_free(&fs);
	v_free(&hd);
	s_free(&o);
	s_free(&k1);
	s_free(&k2);
	s_free(&in);
	return rc;
}

/* csv split [-s SEP] TEXT: the fields of one record, as RET's list. */
int csv_split(sh *s, int ac, char **av)
{
	csv_rd r;
	vec fs = { 0, 0, 0 };
	int i;
	size_t j;
	const char *text = 0;

	memset(&r, 0, sizeof r);
	r.sep = ',';
	for (i = 2; i < ac; i++) {
		if (!strcmp(av[i], "-s") && i + 1 < ac)
			r.sep = csv_sep(av[++i]);
		else
			text = av[i];
	}
	if (!text) {
		lg(HIBR_LERR, "usage: csv split [-s sep] text");
		return 2;
	}
	r.t = text;
	r.tn = strlen(text);
	if (!csv_rec(&r, &fs))
		v_add(&fs, xs(""));
	hibr_retn(s, (char **)fs.p, fs.n);
	if (!s->bind)
		for (j = 0; j < fs.n; j++)
			printf("%s\n", (char *)fs.p[j]);
	csv_clear(&fs);
	v_free(&fs);
	return HIBR_OK;
}

/* csv line [-s SEP] FIELD...: one record, written. */
int csv_line(sh *s, int ac, char **av)
{
	str o;
	int i = 2, sep = ',', first = 1;

	if (ac > 3 && !strcmp(av[2], "-s")) {
		sep = csv_sep(av[3]);
		i = 4;
	}
	s_init(&o);
	for (; i < ac; i++) {
		if (!first)
			s_ch(&o, sep);
		first = 0;
		csv_put(&o, av[i], sep);
	}
	hibr_ret(s, o.p ? o.p : "");
	if (!s->bind)
		printf("%s\n", o.p ? o.p : "");
	s_free(&o);
	return HIBR_OK;
}

/* csv open [-s SEP] FILE: a reader for csv row, its number the result. */
int csv_opn(sh *s, int ac, char **av)
{
	csv_rd *r;
	FILE *f;
	const char *path = 0;
	int i, sep = ',';
	size_t k;
	str o;

	for (i = 2; i < ac; i++) {
		if (!strcmp(av[i], "-s") && i + 1 < ac)
			sep = csv_sep(av[++i]);
		else
			path = av[i];
	}
	if (!path) {
		lg(HIBR_LERR, "usage: csv open [-s sep] file");
		return 2;
	}
	f = strcmp(path, "-") ? fopen(path, "rb") : stdin;
	if (!f) {
		lg(HIBR_LERR, "csv: %s: cannot read", path);
		return HIBR_FAIL;
	}
	r = xm(sizeof *r);
	memset(r, 0, sizeof *r);
	r->f = f;
	r->sep = sep;
	r->bom = 1;
	for (k = 0; k < csv_open.n && csv_open.p[k]; k++)
		;
	if (k == csv_open.n)
		v_add(&csv_open, r);
	else
		csv_open.p[k] = r;
	s_init(&o);
	s_num(&o, (long)k + 1);
	hibr_ret(s, o.p);
	if (!s->bind)
		printf("%s\n", o.p);
	s_free(&o);
	return HIBR_OK;
}

/* The reader a handle names, or none. */
csv_rd *csv_h(const char *h)
{
	long k = strtol(h, 0, 10);

	if (k < 1 || (size_t)k > csv_open.n)
		return 0;
	return csv_open.p[k - 1];
}

/* csv row H: the next record, as RET's list; fails at the end. */
int csv_row(sh *s, int ac, char **av)
{
	csv_rd *r;
	vec fs = { 0, 0, 0 };
	size_t j;
	int got;

	if (ac < 3 || !(r = csv_h(av[2]))) {
		lg(HIBR_LERR, "usage: csv row handle");
		return 2;
	}
	got = csv_rec(r, &fs);
	if (!got) {
		hibr_retn(s, 0, 0);
		v_free(&fs);
		return HIBR_FAIL;
	}
	hibr_retn(s, (char **)fs.p, fs.n);
	if (!s->bind)
		for (j = 0; j < fs.n; j++)
			printf("%s%s", j ? "\t" : "", (char *)fs.p[j]);
	if (!s->bind)
		printf("\n");
	csv_clear(&fs);
	v_free(&fs);
	return HIBR_OK;
}

/* csv close H. */
int csv_cls(sh *s, int ac, char **av)
{
	csv_rd *r;
	long k;

	(void)s;
	if (ac < 3 || !(r = csv_h(av[2]))) {
		lg(HIBR_LERR, "usage: csv close handle");
		return 2;
	}
	k = strtol(av[2], 0, 10);
	if (r->f && r->f != stdin)
		fclose(r->f);
	free(r);
	csv_open.p[k - 1] = 0;
	return HIBR_OK;
}

/* csv read|split|line|open|row|close ... */
int csv_bi(sh *s, int ac, char **av)
{
	const char *sub = ac > 1 ? av[1] : "";

	if (!strcmp(sub, "read"))
		return csv_read(s, ac, av);
	if (!strcmp(sub, "split"))
		return csv_split(s, ac, av);
	if (!strcmp(sub, "line"))
		return csv_line(s, ac, av);
	if (!strcmp(sub, "open"))
		return csv_opn(s, ac, av);
	if (!strcmp(sub, "row"))
		return csv_row(s, ac, av);
	if (!strcmp(sub, "close"))
		return csv_cls(s, ac, av);
	lg(HIBR_LERR, "usage: csv read|split|line|open|row|close ...");
	return 2;
}

/* Close every reader left open. */
void csv_fini(sh *s)
{
	size_t k;
	csv_rd *r;

	(void)s;
	for (k = 0; k < csv_open.n; k++) {
		r = csv_open.p[k];
		if (!r)
			continue;
		if (r->f && r->f != stdin)
			fclose(r->f);
		free(r);
	}
	v_free(&csv_open);
}

const hibr_bi csv_bis[] = {
	{ "csv", csv_bi, "CSV, RFC 4180: csv read|split|line|open|row|close" },
	HIBR_BI_END
};

HIBR_MODULE("csv", "1.0", "CSV read and written as RFC 4180 says, at C speed",
	    csv_bis, 0, csv_fini);
