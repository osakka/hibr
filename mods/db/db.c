#define _GNU_SOURCE

#include "hibr.h"
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

ent *v_path(sh *s, const char *nm, char **ks, int nk, int make);
void v_setp(sh *s, const char *nm, char **ks, int nk, const char *val);
void v_del(sh *s, const char *k);

#ifndef DM_GROUP
#define DM_GROUP 1024
#endif
#ifndef DM_STRW
#define DM_STRW 32
#endif

enum { DM_INT = 1, DM_FLOAT = 2, DM_STR = 3 };
enum { DM_EQ, DM_NE, DM_LT, DM_LE, DM_GT, DM_GE };

struct dm_head {
	char magic[8];
	uint32_t ncols, grp;
	uint64_t nrows;
	uint32_t slen, doff;
};

typedef struct dm_col dm_col;
struct dm_col {
	char *nm;
	int ty;
	size_t w, zoff, doff;
};

typedef struct dm_db dm_db;
struct dm_db {
	int fd;
	char *path;
	unsigned char *map;
	size_t mlen, gbytes, bbytes;
	struct dm_head *h;
	int nc, ver;
	dm_col *c;
	uint64_t ndel;
};

typedef struct dm_pred dm_pred;
struct dm_pred {
	int col, op;
	int64_t i;
	double f;
	unsigned char *sv;
};

static const char dm_magic[8] = { 'H', 'I', 'B', 'R', 'D', 'B', '1', '\n' };
static const char dm_magic2[8] = { 'H', 'I', 'B', 'R', 'D', 'B', '2', '\n' };

vec dm_open_dbs;

/* The open database a handle names, or null with an error said. */
dm_db *dm_get(const char *h)
{
	char *e;
	long i = strtol(h ? h : "", &e, 10);

	if (!h || *e || i < 1 || (size_t)i > dm_open_dbs.n ||
	    !dm_open_dbs.p[i - 1]) {
		lg(HIBR_LERR, "db: no open database %s", h ? h : "");
		return 0;
	}
	return dm_open_dbs.p[i - 1];
}

/* Give an open database a handle, reusing a closed one's number. */
long dm_keep(dm_db *d)
{
	size_t i;

	for (i = 0; i < dm_open_dbs.n; i++)
		if (!dm_open_dbs.p[i]) {
			dm_open_dbs.p[i] = d;
			return (long)i + 1;
		}
	v_add(&dm_open_dbs, d);
	return (long)dm_open_dbs.n;
}

/* Where each column's zone and values sit inside a row group, and how
   long a group is. */
void dm_layout(dm_db *d)
{
	size_t z, i;

	d->bbytes = d->ver >= 2 ? (size_t)d->h->grp / 8 : 0;
	z = d->bbytes;
	for (i = 0; i < (size_t)d->nc; i++) {
		d->c[i].zoff = z;
		z += 2 * d->c[i].w;
	}
	for (i = 0; i < (size_t)d->nc; i++) {
		d->c[i].doff = z;
		z += (size_t)d->h->grp * d->c[i].w;
	}
	d->gbytes = z;
}

/* How many row groups the file has room for. */
size_t dm_groups(dm_db *d)
{
	return d->mlen > d->h->doff ? (d->mlen - d->h->doff) / d->gbytes : 0;
}

/* Map the file again at its current size. */
int dm_remap(dm_db *d)
{
	struct stat st;
	void *m;

	if (fstat(d->fd, &st) < 0)
		return HIBR_FAIL;
	if (d->map)
		munmap(d->map, d->mlen);
	m = mmap(0, (size_t)st.st_size, PROT_READ | PROT_WRITE, MAP_SHARED,
		 d->fd, 0);
	if (m == MAP_FAILED) {
		d->map = 0;
		lg(HIBR_LERR, "db: cannot map %s: %s", d->path, strerror(errno));
		return HIBR_FAIL;
	}
	d->map = m;
	d->mlen = (size_t)st.st_size;
	d->h = (struct dm_head *)d->map;
	return HIBR_OK;
}

/* Forget a database: write it back, unmap it and close it. */
void dm_free(dm_db *d)
{
	int i;

	if (!d)
		return;
	if (d->map) {
		msync(d->map, d->mlen, MS_SYNC);
		munmap(d->map, d->mlen);
	}
	if (d->fd >= 0)
		close(d->fd);
	for (i = 0; i < d->nc; i++)
		free(d->c[i].nm);
	free(d->c);
	free(d->path);
	free(d);
}

/* How many rows are deleted, counted from each group's marks. */
void dm_count(dm_db *d)
{
	size_t g, ng, i;
	unsigned char *gp;

	d->ndel = 0;
	if (!d->bbytes)
		return;
	ng = (size_t)((d->h->nrows + d->h->grp - 1) / d->h->grp);
	for (g = 0; g < ng; g++) {
		gp = d->map + d->h->doff + g * d->gbytes;
		for (i = 0; i < d->bbytes; i++) {
			unsigned b = gp[i];
			while (b) {
				d->ndel += b & 1;
				b >>= 1;
			}
		}
	}
}

/* Read the schema after the header into the column list, checking every
   length against the file, so a damaged file is refused rather than read
   past its end. */
int dm_schema(dm_db *d)
{
	unsigned char *p, *e;
	int i;

	if (d->mlen >= sizeof *d->h && !memcmp(d->h->magic, dm_magic2, 8))
		d->ver = 2;
	else
		d->ver = 1;
	if (d->mlen < sizeof *d->h ||
	    (memcmp(d->h->magic, dm_magic, 8) && memcmp(d->h->magic, dm_magic2, 8)) ||
	    !d->h->ncols || d->h->grp != DM_GROUP ||
	    sizeof *d->h + d->h->slen > d->mlen || d->h->doff > d->mlen) {
		lg(HIBR_LERR, "db: %s is not a hibr database", d->path);
		return HIBR_FAIL;
	}
	d->nc = (int)d->h->ncols;
	d->c = xm(sizeof *d->c * (size_t)d->nc);
	memset(d->c, 0, sizeof *d->c * (size_t)d->nc);
	p = d->map + sizeof *d->h;
	e = p + d->h->slen;
	for (i = 0; i < d->nc; i++) {
		if (p + 4 > e || p + 4 + p[3] > e)
			goto bad;
		d->c[i].ty = p[0];
		d->c[i].w = (size_t)p[1] | (size_t)p[2] << 8;
		if (d->c[i].ty < DM_INT || d->c[i].ty > DM_STR || !d->c[i].w)
			goto bad;
		d->c[i].nm = xm((size_t)p[3] + 1);
		memcpy(d->c[i].nm, p + 4, p[3]);
		d->c[i].nm[p[3]] = 0;
		p += 4 + p[3];
	}
	dm_layout(d);
	if (d->h->nrows > (uint64_t)dm_groups(d) * d->h->grp)
		goto bad;
	dm_count(d);
	return HIBR_OK;
bad:
	lg(HIBR_LERR, "db: %s has a damaged schema", d->path);
	return HIBR_FAIL;
}

/* Parse "name:type" or "name:str:width" into a column. */
int dm_coldef(const char *def, dm_col *c)
{
	const char *a = strchr(def, ':'), *b;
	char *e;
	long w;

	if (!a || a == def || a - def > 255) {
		lg(HIBR_LERR, "db: a column is name:int, name:float or "
			      "name:str[:width], not '%s'", def);
		return HIBR_FAIL;
	}
	c->nm = xm((size_t)(a - def) + 1);
	memcpy(c->nm, def, (size_t)(a - def));
	c->nm[a - def] = 0;
	a++;
	b = strchr(a, ':');
	if (!strcmp(a, "int")) {
		c->ty = DM_INT;
		c->w = 8;
	} else if (!strcmp(a, "float")) {
		c->ty = DM_FLOAT;
		c->w = 8;
	} else if (!strncmp(a, "str", 3) && (!a[3] || a[3] == ':')) {
		c->ty = DM_STR;
		c->w = DM_STRW;
		if (b) {
			w = strtol(b + 1, &e, 10);
			if (*e || w < 1 || w > 4096) {
				lg(HIBR_LERR, "db: a str width is 1 to 4096, "
					      "not '%s'", b + 1);
				return HIBR_FAIL;
			}
			c->w = (size_t)w;
		}
	} else {
		lg(HIBR_LERR, "db: unknown column type in '%s'", def);
		return HIBR_FAIL;
	}
	return HIBR_OK;
}

/* Create a database file with these columns, and open it. */
dm_db *dm_create(const char *path, int ac, char **defs)
{
	dm_db *d = xm(sizeof *d);
	struct dm_head h;
	str sch;
	int i, j;
	long pg = sysconf(_SC_PAGESIZE);

	memset(d, 0, sizeof *d);
	d->fd = -1;
	d->path = xs(path);
	d->nc = ac;
	d->c = xm(sizeof *d->c * (size_t)ac);
	memset(d->c, 0, sizeof *d->c * (size_t)ac);
	s_init(&sch);
	for (i = 0; i < ac; i++) {
		if (dm_coldef(defs[i], &d->c[i]) != HIBR_OK)
			goto fail;
		for (j = 0; j < i; j++)
			if (!strcmp(d->c[j].nm, d->c[i].nm)) {
				lg(HIBR_LERR, "db: column %s twice", d->c[i].nm);
				goto fail;
			}
		s_ch(&sch, d->c[i].ty);
		s_ch(&sch, (int)(d->c[i].w & 0xFF));
		s_ch(&sch, (int)(d->c[i].w >> 8));
		s_ch(&sch, (int)strlen(d->c[i].nm));
		s_cat(&sch, d->c[i].nm);
	}
	memset(&h, 0, sizeof h);
	memcpy(h.magic, dm_magic2, 8);
	d->ver = 2;
	h.ncols = (uint32_t)ac;
	h.grp = DM_GROUP;
	h.slen = (uint32_t)sch.n;
	h.doff = (uint32_t)(((sizeof h + sch.n + (size_t)pg - 1) / (size_t)pg) *
			    (size_t)pg);
	d->fd = open(path, O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC, 0644);
	if (d->fd < 0) {
		lg(HIBR_LERR, "db: cannot create %s: %s", path, strerror(errno));
		goto fail;
	}
	if (write(d->fd, &h, sizeof h) != (ssize_t)sizeof h ||
	    write(d->fd, sch.p, sch.n) != (ssize_t)sch.n ||
	    ftruncate(d->fd, h.doff) < 0) {
		lg(HIBR_LERR, "db: cannot write %s: %s", path, strerror(errno));
		goto fail;
	}
	s_free(&sch);
	if (dm_remap(d) != HIBR_OK)
		goto fail;
	dm_layout(d);
	lg(HIBR_LDBG, "db: created %s, %d columns, %zu bytes a group", path,
	   ac, d->gbytes);
	return d;
fail:
	s_free(&sch);
	dm_free(d);
	return 0;
}

/* Open an existing database file. */
dm_db *dm_open(const char *path)
{
	dm_db *d = xm(sizeof *d);

	memset(d, 0, sizeof *d);
	d->path = xs(path);
	d->fd = open(path, O_RDWR | O_CLOEXEC);
	if (d->fd < 0) {
		lg(HIBR_LERR, "db: cannot open %s: %s", path, strerror(errno));
		dm_free(d);
		return 0;
	}
	if (dm_remap(d) != HIBR_OK || dm_schema(d) != HIBR_OK) {
		dm_free(d);
		return 0;
	}
	return d;
}

/* The column a name names, or -1 with an error said. */
int dm_colof(dm_db *d, const char *nm)
{
	int i;

	for (i = 0; i < d->nc; i++)
		if (!strcmp(d->c[i].nm, nm))
			return i;
	lg(HIBR_LERR, "db: no column %s", nm);
	return -1;
}

/* Read text as a value of a column's type into raw cell bytes; out is
   that column's width. */
int dm_parse(dm_col *c, const char *t, unsigned char *out)
{
	char *e;
	int64_t i;
	double f;

	switch (c->ty) {
	case DM_INT:
		errno = 0;
		i = strtoll(t, &e, 10);
		if (!*t || *e || errno) {
			lg(HIBR_LERR, "db: %s is an int, not '%s'", c->nm, t);
			return HIBR_FAIL;
		}
		memcpy(out, &i, 8);
		return HIBR_OK;
	case DM_FLOAT:
		f = strtod(t, &e);
		if (!*t || *e) {
			lg(HIBR_LERR, "db: %s is a float, not '%s'", c->nm, t);
			return HIBR_FAIL;
		}
		memcpy(out, &f, 8);
		return HIBR_OK;
	}
	if (strlen(t) > c->w) {
		lg(HIBR_LERR, "db: %s holds at most %zu bytes, '%s' is longer",
		   c->nm, c->w, t);
		return HIBR_FAIL;
	}
	memset(out, 0, c->w);
	memcpy(out, t, strlen(t));
	return HIBR_OK;
}

/* Order two cells of a column: below zero, zero or above. */
int dm_cmp(dm_col *c, const unsigned char *a, const unsigned char *b)
{
	int64_t x, y;
	double f, g;

	switch (c->ty) {
	case DM_INT:
		memcpy(&x, a, 8);
		memcpy(&y, b, 8);
		return x < y ? -1 : x > y;
	case DM_FLOAT:
		memcpy(&f, a, 8);
		memcpy(&g, b, 8);
		return f < g ? -1 : f > g;
	}
	return memcmp(a, b, c->w);
}

/* Widen a column's zone in a group to take in a cell; the first row of a
   group sets it. */
void dm_widen(dm_db *d, int i, unsigned char *gp, unsigned char *cell, int first)
{
	unsigned char *mn = gp + d->c[i].zoff, *mx = mn + d->c[i].w;

	if (first || dm_cmp(&d->c[i], cell, mn) < 0)
		memcpy(mn, cell, d->c[i].w);
	if (first || dm_cmp(&d->c[i], cell, mx) > 0)
		memcpy(mx, cell, d->c[i].w);
}

/* Append a row of raw cells, every column's bytes one after another. */
int dm_put(dm_db *d, const unsigned char *buf)
{
	uint64_t row = d->h->nrows;
	size_t g = (size_t)(row / d->h->grp), k = (size_t)(row % d->h->grp), o = 0;
	unsigned char *gp, *cell;
	int i;

	if (g >= dm_groups(d)) {
		if (ftruncate(d->fd, (off_t)(d->h->doff + (g + 1) * d->gbytes)) < 0 ||
		    dm_remap(d) != HIBR_OK) {
			lg(HIBR_LERR, "db: cannot grow %s: %s", d->path,
			   strerror(errno));
			return HIBR_FAIL;
		}
		lg(HIBR_LDBG, "db: %s grew to %zu groups", d->path, g + 1);
	}
	gp = d->map + d->h->doff + g * d->gbytes;
	for (i = 0; i < d->nc; i++) {
		cell = gp + d->c[i].doff + k * d->c[i].w;
		memcpy(cell, buf + o, d->c[i].w);
		dm_widen(d, i, gp, cell, !k);
		o += d->c[i].w;
	}
	if (d->bbytes)
		gp[k >> 3] &= (unsigned char)~(1u << (k & 7));
	d->h->nrows = row + 1;
	return HIBR_OK;
}

/* Append one row; vals are the columns in order. */
int dm_insert(dm_db *d, int ac, char **vals)
{
	unsigned char *buf;
	size_t tot = 0, o = 0;
	int i;

	if (ac != d->nc) {
		lg(HIBR_LERR, "db: %s has %d columns, %d values given", d->path,
		   d->nc, ac);
		return HIBR_FAIL;
	}
	for (i = 0; i < d->nc; i++)
		tot += d->c[i].w;
	buf = xm(tot);
	for (i = 0; i < d->nc; i++) {
		if (dm_parse(&d->c[i], vals[i], buf + o) != HIBR_OK) {
			free(buf);
			return HIBR_FAIL;
		}
		o += d->c[i].w;
	}
	i = dm_put(d, buf);
	free(buf);
	return i;
}

/* A cell as text, appended to out: an int in decimal, a float in the
   fewest digits that read back as the same number, a str to its end. */
void dm_text(dm_col *c, const unsigned char *cell, str *out)
{
	int64_t i;
	double f, back;
	int n;
	size_t len;

	switch (c->ty) {
	case DM_INT:
		memcpy(&i, cell, 8);
		s_num(out, (long)i);
		return;
	case DM_FLOAT:
		memcpy(&f, cell, 8);
		for (n = 15; n <= 17; n++) {
			s_grow(out, 40);
			len = (size_t)snprintf(out->p + out->n, 40, "%.*g", n, f);
			back = strtod(out->p + out->n, 0);
			if (back == f || n == 17) {
				out->n += len;
				return;
			}
		}
		return;
	}
	len = strnlen((const char *)cell, c->w);
	s_add(out, (const char *)cell, len);
}

/* Read "where col op value [and col op value]... [limit n] [from n]" from
   av into predicates, a limit and a first row; where it begins is *at, and
   *at is left past it. */
long dm_from;
int dm_where(dm_db *d, int ac, char **av, int *at, vec *ps, long *limit)
{
	static const char *ops[] = { "eq", "ne", "lt", "le", "gt", "ge", 0 };
	dm_pred *p;
	int i = *at, k;
	size_t w;

	*limit = -1;
	dm_from = 0;
	while (i < ac) {
		if (!strcmp(av[i], "limit") && i + 1 < ac) {
			*limit = strtol(av[i + 1], 0, 10);
			i += 2;
			continue;
		}
		if (!strcmp(av[i], "from") && i + 1 < ac) {
			dm_from = strtol(av[i + 1], 0, 10) - 1;
			if (dm_from < 0)
				dm_from = 0;
			i += 2;
			continue;
		}
		if (strcmp(av[i], "where") && strcmp(av[i], "and"))
			break;
		if (i + 3 >= ac) {
			lg(HIBR_LERR, "db: '%s' wants a column, an operator and "
				      "a value", av[i]);
			return HIBR_FAIL;
		}
		p = xm(sizeof *p);
		memset(p, 0, sizeof *p);
		v_add(ps, p);
		p->col = dm_colof(d, av[i + 1]);
		if (p->col < 0)
			return HIBR_FAIL;
		for (k = 0; ops[k] && strcmp(ops[k], av[i + 2]); k++)
			;
		if (!ops[k]) {
			lg(HIBR_LERR, "db: no operator %s -- eq ne lt le gt ge",
			   av[i + 2]);
			return HIBR_FAIL;
		}
		p->op = k;
		w = d->c[p->col].w;
		p->sv = xm(w);
		if (dm_parse(&d->c[p->col], av[i + 3], p->sv) != HIBR_OK)
			return HIBR_FAIL;
		i += 4;
	}
	*at = i;
	return HIBR_OK;
}

/* Free a list of predicates. */
void dm_wfree(vec *ps)
{
	size_t i;

	for (i = 0; i < ps->n; i++) {
		free(((dm_pred *)ps->p[i])->sv);
		free(ps->p[i]);
	}
	v_free(ps);
}

/* Whether a comparison result satisfies an operator. */
int dm_holds(int op, int r)
{
	switch (op) {
	case DM_EQ: return r == 0;
	case DM_NE: return r != 0;
	case DM_LT: return r < 0;
	case DM_LE: return r <= 0;
	case DM_GT: return r > 0;
	}
	return r >= 0;
}

/* Whether a group can hold no row a predicate accepts, from its zone. */
int dm_skip(dm_db *d, dm_pred *p, unsigned char *gp)
{
	dm_col *c = &d->c[p->col];
	unsigned char *mn = gp + c->zoff, *mx = mn + c->w;
	int lo = dm_cmp(c, p->sv, mn), hi = dm_cmp(c, p->sv, mx);

	switch (p->op) {
	case DM_EQ: return lo < 0 || hi > 0;
	case DM_NE: return lo == 0 && hi == 0;
	case DM_LT: return lo <= 0;
	case DM_LE: return lo < 0;
	case DM_GT: return hi >= 0;
	}
	return hi > 0;
}

/* Visit every row the predicates accept, up to limit (-1 for all),
   calling fn with the group and the row within it; returns how many. */
long dm_scan(dm_db *d, vec *ps, long limit,
	     void (*fn)(dm_db *, unsigned char *, size_t, void *), void *arg)
{
	size_t g, k, k0, n, ng = (size_t)((d->h->nrows + d->h->grp - 1) / d->h->grp);
	size_t i, skipped = 0;
	unsigned char *gp;
	dm_pred *p;
	long hits = 0;
	int ok;

	for (g = (size_t)dm_from / d->h->grp; g < ng; g++) {
		gp = d->map + d->h->doff + g * d->gbytes;
		for (i = 0; i < ps->n; i++)
			if (dm_skip(d, ps->p[i], gp))
				break;
		if (i < ps->n) {
			skipped++;
			continue;
		}
		n = d->h->grp;
		if ((uint64_t)(g + 1) * d->h->grp > d->h->nrows)
			n = (size_t)(d->h->nrows - (uint64_t)g * d->h->grp);
		k0 = g == (size_t)dm_from / d->h->grp ? (size_t)dm_from % d->h->grp : 0;
		for (k = k0; k < n; k++) {
			if (d->bbytes && (gp[k >> 3] >> (k & 7)) & 1)
				continue;
			ok = 1;
			for (i = 0; ok && i < ps->n; i++) {
				p = ps->p[i];
				ok = dm_holds(p->op, dm_cmp(&d->c[p->col],
					gp + d->c[p->col].doff + k * d->c[p->col].w,
					p->sv));
			}
			if (!ok)
				continue;
			if (fn)
				fn(d, gp, k, arg);
			hits++;
			if (limit >= 0 && hits >= limit)
				goto done;
		}
	}
done:
	lg(HIBR_LDBG, "db: %ld rows matched, %zu of %zu groups skipped by "
		      "their zones", hits, skipped, ng);
	return hits;
}


/* A record number, counted from 1, as its group and its row there; one
   past the end, or a deleted one, is refused. */
int dm_rec(dm_db *d, const char *t, unsigned char **gp, size_t *k)
{
	char *e;
	long n = strtol(t ? t : "", &e, 10);
	uint64_t r;

	if (!t || !*t || *e || n < 1 || (uint64_t)n > d->h->nrows) {
		lg(HIBR_LERR, "db: no record %s", t ? t : "");
		return HIBR_FAIL;
	}
	r = (uint64_t)n - 1;
	*gp = d->map + d->h->doff + (size_t)(r / d->h->grp) * d->gbytes;
	*k = (size_t)(r % d->h->grp);
	if (d->bbytes && ((*gp)[*k >> 3] >> (*k & 7)) & 1) {
		lg(HIBR_LERR, "db: record %s is deleted", t);
		return HIBR_FAIL;
	}
	return HIBR_OK;
}

struct dm_set {
	int n;
	int *col;
	unsigned char **val;
};

/* Free the values a set holds. */
void dm_setfree(struct dm_set *st)
{
	int i;

	for (i = 0; i < st->n; i++)
		free(st->val[i]);
	free(st->val);
	free(st->col);
	memset(st, 0, sizeof *st);
}

/* Read "col val [col val ...]" from av[at] on, each value checked against
   its column, so nothing is written unless all of it can be. */
int dm_pairs(dm_db *d, int ac, char **av, int at, struct dm_set *st)
{
	int i, n = (ac - at) / 2;

	memset(st, 0, sizeof *st);
	if (n < 1 || (ac - at) % 2) {
		lg(HIBR_LERR, "db: give a column and a value, as many as are set");
		return HIBR_FAIL;
	}
	st->col = xm(sizeof *st->col * (size_t)n);
	st->val = xm(sizeof *st->val * (size_t)n);
	memset(st->val, 0, sizeof *st->val * (size_t)n);
	for (i = 0; i < n; i++) {
		st->n = i + 1;
		st->col[i] = dm_colof(d, av[at + 2 * i]);
		if (st->col[i] < 0)
			return HIBR_FAIL;
		st->val[i] = xm(d->c[st->col[i]].w);
		if (dm_parse(&d->c[st->col[i]], av[at + 2 * i + 1], st->val[i]) !=
		    HIBR_OK)
			return HIBR_FAIL;
	}
	return HIBR_OK;
}

/* Write a set's values into one row, widening the zones they land in. */
void dm_setrow(dm_db *d, unsigned char *gp, size_t k, void *arg)
{
	struct dm_set *st = arg;
	unsigned char *cell;
	int i, c;

	for (i = 0; i < st->n; i++) {
		c = st->col[i];
		cell = gp + d->c[c].doff + k * d->c[c].w;
		memcpy(cell, st->val[i], d->c[c].w);
		dm_widen(d, c, gp, cell, 0);
	}
}

/* Mark one row deleted. Its zones are left as they were: still a bound on
   what the group holds, which is all a zone has to be. */
void dm_delrow(dm_db *d, unsigned char *gp, size_t k, void *arg)
{
	(void)arg;
	gp[k >> 3] |= (unsigned char)(1u << (k & 7));
	d->ndel++;
}

/* A new file beside a database's, holding its header and schema with no
   rows: where an upgrade or a compaction is built before it is renamed
   over the original, which is untouched until then. */
int dm_newbeside(dm_db *d, str *tmp, uint64_t nrows)
{
	struct dm_head h = *d->h;
	int fd;

	s_init(tmp);
	s_cat(tmp, d->path);
	s_cat(tmp, ".tmp");
	fd = open(tmp->p, O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC, 0644);
	if (fd < 0) {
		lg(HIBR_LERR, "db: cannot create %s: %s", tmp->p, strerror(errno));
		return -1;
	}
	memcpy(h.magic, dm_magic2, 8);
	h.nrows = nrows;
	if (write(fd, &h, sizeof h) != (ssize_t)sizeof h ||
	    write(fd, d->map + sizeof h, d->h->slen) != (ssize_t)d->h->slen ||
	    ftruncate(fd, (off_t)h.doff) < 0) {
		lg(HIBR_LERR, "db: cannot write %s: %s", tmp->p, strerror(errno));
		close(fd);
		unlink(tmp->p);
		return -1;
	}
	return fd;
}

/* Put a rebuilt file in place of a database's own and open it again under
   the same handle. */
int dm_swap(dm_db *d, str *tmp)
{
	if (rename(tmp->p, d->path) < 0) {
		lg(HIBR_LERR, "db: cannot replace %s: %s", d->path, strerror(errno));
		unlink(tmp->p);
		return HIBR_FAIL;
	}
	msync(d->map, d->mlen, MS_SYNC);
	munmap(d->map, d->mlen);
	d->map = 0;
	close(d->fd);
	d->fd = open(d->path, O_RDWR | O_CLOEXEC);
	if (d->fd < 0 || dm_remap(d) != HIBR_OK) {
		lg(HIBR_LERR, "db: cannot open %s again: %s", d->path,
		   strerror(errno));
		return HIBR_FAIL;
	}
	d->ver = 2;
	dm_layout(d);
	dm_count(d);
	return HIBR_OK;
}

/* Give a database made before rows could be deleted each group's marks,
   by writing it again beside itself: what it holds and its record numbers
   are kept. */
int dm_upgrade(dm_db *d)
{
	str tmp;
	size_t ng = dm_groups(d), g, nb = (size_t)d->h->grp / 8, ng2;
	unsigned char *z;
	int fd, bad = 0;

	if (d->ver >= 2)
		return HIBR_OK;
	fd = dm_newbeside(d, &tmp, d->h->nrows);
	if (fd < 0) {
		s_free(&tmp);
		return HIBR_FAIL;
	}
	ng2 = d->gbytes + nb;
	z = xm(nb);
	memset(z, 0, nb);
	if (ftruncate(fd, (off_t)(d->h->doff + ng * ng2)) < 0)
		bad = 1;
	for (g = 0; !bad && g < ng; g++)
		if (pwrite(fd, z, nb, (off_t)(d->h->doff + g * ng2)) != (ssize_t)nb ||
		    pwrite(fd, d->map + d->h->doff + g * d->gbytes, d->gbytes,
			   (off_t)(d->h->doff + g * ng2 + nb)) != (ssize_t)d->gbytes)
			bad = 1;
	free(z);
	if (bad || fsync(fd) < 0) {
		lg(HIBR_LERR, "db: cannot write %s: %s", tmp.p, strerror(errno));
		close(fd);
		unlink(tmp.p);
		s_free(&tmp);
		return HIBR_FAIL;
	}
	close(fd);
	bad = dm_swap(d, &tmp);
	lg(HIBR_LDBG, "db: %s now keeps deletions", d->path);
	s_free(&tmp);
	return bad;
}

/* Write a database again without its deleted rows, which renumbers what
   follows them; how many rows it keeps goes in *kept. */
int dm_compact(dm_db *d, long *kept)
{
	str tmp;
	dm_db *n;
	size_t g, k, ng, rows, tot = 0, o;
	unsigned char *gp, *buf;
	int fd, i, bad = 0;

	fd = dm_newbeside(d, &tmp, 0);
	if (fd < 0) {
		s_free(&tmp);
		return HIBR_FAIL;
	}
	close(fd);
	n = dm_open(tmp.p);
	if (!n) {
		unlink(tmp.p);
		s_free(&tmp);
		return HIBR_FAIL;
	}
	for (i = 0; i < d->nc; i++)
		tot += d->c[i].w;
	buf = xm(tot);
	ng = (size_t)((d->h->nrows + d->h->grp - 1) / d->h->grp);
	for (g = 0; !bad && g < ng; g++) {
		gp = d->map + d->h->doff + g * d->gbytes;
		rows = d->h->grp;
		if ((uint64_t)(g + 1) * d->h->grp > d->h->nrows)
			rows = (size_t)(d->h->nrows - (uint64_t)g * d->h->grp);
		for (k = 0; !bad && k < rows; k++) {
			if (d->bbytes && (gp[k >> 3] >> (k & 7)) & 1)
				continue;
			for (o = 0, i = 0; i < d->nc; i++) {
				memcpy(buf + o, gp + d->c[i].doff + k * d->c[i].w,
				       d->c[i].w);
				o += d->c[i].w;
			}
			bad = dm_put(n, buf) != HIBR_OK;
		}
	}
	free(buf);
	*kept = n->h ? (long)n->h->nrows : 0;
	dm_free(n);
	if (bad) {
		unlink(tmp.p);
		s_free(&tmp);
		return HIBR_FAIL;
	}
	bad = dm_swap(d, &tmp);
	s_free(&tmp);
	return bad;
}

struct dm_out {
	sh *s;
	int bind, num;
	long n;
	str line;
};

/* One matching row: a line of tab-separated text, or an entry in $RET. */
void dm_row(dm_db *d, unsigned char *gp, size_t k, void *arg)
{
	struct dm_out *o = arg;
	str key;
	char *ks[2];
	ent *e;
	int i;

	if (!o->bind) {
		o->line.n = 0;
		if (o->num) {
			s_num(&o->line, (long)(((gp - d->map) - d->h->doff) /
					       d->gbytes * d->h->grp + k + 1));
			s_ch(&o->line, '\t');
		}
		for (i = 0; i < d->nc; i++) {
			if (i)
				s_ch(&o->line, '\t');
			dm_text(&d->c[i], gp + d->c[i].doff + k * d->c[i].w,
				&o->line);
		}
		s_ch(&o->line, '\n');
		fwrite(o->line.p, 1, o->line.n, stdout);
		o->n++;
		return;
	}
	s_init(&key);
	s_num(&key, o->n);
	ks[0] = key.p;
	if (o->num) {
		o->line.n = 0;
		s_num(&o->line, (long)(((gp - d->map) - d->h->doff) /
				       d->gbytes * d->h->grp + k + 1));
		s_ch(&o->line, 0);
		ks[1] = "#";
		v_setp(o->s, "RET", ks, 2, o->line.p);
		e = v_path(o->s, "RET", ks, 2, 0);
		if (e)
			e->ty = J_NUM;
	}
	for (i = 0; i < d->nc; i++) {
		o->line.n = 0;
		dm_text(&d->c[i], gp + d->c[i].doff + k * d->c[i].w, &o->line);
		s_ch(&o->line, 0);
		ks[1] = d->c[i].nm;
		v_setp(o->s, "RET", ks, 2, o->line.p);
		e = v_path(o->s, "RET", ks, 2, 0);
		if (e)
			e->ty = d->c[i].ty == DM_STR ? J_STR : J_NUM;
	}
	s_free(&key);
	o->n++;
}

struct dm_agg {
	int col, fn;
	long n;
	double sum;
	unsigned char *best;
};

/* One matching row into a running count, sum, minimum or maximum. */
void dm_aggrow(dm_db *d, unsigned char *gp, size_t k, void *arg)
{
	struct dm_agg *a = arg;
	dm_col *c;
	unsigned char *cell;
	int64_t i;
	double f;

	a->n++;
	if (a->col < 0)
		return;
	c = &d->c[a->col];
	cell = gp + c->doff + k * c->w;
	if (c->ty == DM_INT) {
		memcpy(&i, cell, 8);
		a->sum += (double)i;
	} else if (c->ty == DM_FLOAT) {
		memcpy(&f, cell, 8);
		a->sum += f;
	}
	if (a->n == 1 || (a->fn == 2 && dm_cmp(c, cell, a->best) < 0) ||
	    (a->fn == 3 && dm_cmp(c, cell, a->best) > 0))
		memcpy(a->best, cell, c->w);
}

/* Say a result: into $RET, and printed unless := is taking it. */
void dm_say(sh *s, const char *t)
{
	hibr_ret(s, t);
	if (!s->bind)
		printf("%s\n", t);
}

/* count, sum, min, max or avg of a column over the rows that match. A
   count or a sum of no rows is 0; a minimum, maximum or average of none
   is nothing at all, and status 1. */
int dm_aggregate(sh *s, dm_db *d, const char *fn, int ac, char **av, int at)
{
	static const char *fns[] = { "count", "sum", "min", "max", "avg", 0 };
	struct dm_agg a;
	vec ps = { 0, 0, 0 };
	long limit;
	str o;
	int k;

	memset(&a, 0, sizeof a);
	for (k = 0; fns[k] && strcmp(fns[k], fn); k++)
		;
	a.fn = k;
	a.col = -1;
	if (k > 0) {
		if (at >= ac) {
			lg(HIBR_LERR, "db %s: which column?", fn);
			return 2;
		}
		a.col = dm_colof(d, av[at++]);
		if (a.col < 0)
			return HIBR_FAIL;
		if ((k == 1 || k == 4) && d->c[a.col].ty == DM_STR) {
			lg(HIBR_LERR, "db %s: %s is a str", fn, d->c[a.col].nm);
			return HIBR_FAIL;
		}
		a.best = xm(d->c[a.col].w);
	}
	if (dm_where(d, ac, av, &at, &ps, &limit) != HIBR_OK) {
		dm_wfree(&ps);
		free(a.best);
		return 2;
	}
	if (at < ac) {
		lg(HIBR_LERR, "db: did not understand '%s'", av[at]);
		dm_wfree(&ps);
		free(a.best);
		return 2;
	}
	dm_scan(d, &ps, -1, dm_aggrow, &a);
	dm_wfree(&ps);
	if (!a.n && (k == 2 || k == 3 || k == 4)) {
		hibr_ret(s, "");
		free(a.best);
		return HIBR_FAIL;
	}
	s_init(&o);
	switch (k) {
	case 0:
		s_num(&o, a.n);
		break;
	case 1:
	case 4:
		if (d->c[a.col].ty == DM_INT && k == 1) {
			s_num(&o, (long)a.sum);
		} else {
			double v = k == 4 ? (a.n ? a.sum / (double)a.n : 0) : a.sum;
			dm_col fc = { 0, DM_FLOAT, 8, 0, 0 };

			dm_text(&fc, (unsigned char *)&v, &o);
		}
		break;
	default:
		if (a.n)
			dm_text(&d->c[a.col], a.best, &o);
		break;
	}
	s_ch(&o, 0);
	dm_say(s, o.p);
	s_free(&o);
	free(a.best);
	return HIBR_OK;
}


/* db set h N col val...: one record, by the number query -n gives it. */
int dm_cmdset(sh *s, dm_db *d, int ac, char **av)
{
	struct dm_set st;
	unsigned char *gp;
	size_t k;

	(void)s;
	if (ac < 6) {
		lg(HIBR_LERR, "db set: a record number, then a column and a value");
		return 2;
	}
	if (dm_rec(d, av[3], &gp, &k) != HIBR_OK)
		return HIBR_FAIL;
	if (dm_pairs(d, ac, av, 4, &st) != HIBR_OK) {
		dm_setfree(&st);
		return HIBR_FAIL;
	}
	dm_setrow(d, gp, k, &st);
	dm_setfree(&st);
	return HIBR_OK;
}

/* db update h [where ...] set col val...: every row that matches; says how
   many. */
int dm_cmdupdate(sh *s, dm_db *d, int ac, char **av)
{
	struct dm_set st;
	vec ps = { 0, 0, 0 };
	long limit, n;
	int at = 3;
	str o;

	if (dm_where(d, ac, av, &at, &ps, &limit) != HIBR_OK) {
		dm_wfree(&ps);
		return 2;
	}
	if (at >= ac || strcmp(av[at], "set")) {
		lg(HIBR_LERR, "db update: say what to set: set col val...");
		dm_wfree(&ps);
		return 2;
	}
	if (dm_pairs(d, ac, av, at + 1, &st) != HIBR_OK) {
		dm_setfree(&st);
		dm_wfree(&ps);
		return HIBR_FAIL;
	}
	n = dm_scan(d, &ps, limit, dm_setrow, &st);
	dm_setfree(&st);
	dm_wfree(&ps);
	s_init(&o);
	s_num(&o, n);
	dm_say(s, o.p);
	s_free(&o);
	return HIBR_OK;
}

/* db delete h N... or db delete h where ...: by record number, or every
   row that matches -- never every row for want of saying which; says how
   many. */
int dm_cmddelete(sh *s, dm_db *d, int ac, char **av)
{
	vec ps = { 0, 0, 0 };
	long limit, n = 0;
	int at = 3, i;
	unsigned char *gp;
	size_t k;
	str o;

	if (ac < 4) {
		lg(HIBR_LERR, "db delete: record numbers, or where ...");
		return 2;
	}
	if (!strcmp(av[3], "where") || !strcmp(av[3], "from")) {
		if (dm_where(d, ac, av, &at, &ps, &limit) != HIBR_OK) {
			dm_wfree(&ps);
			return 2;
		}
		if (at < ac || !ps.n) {
			lg(HIBR_LERR, at < ac ? "db: did not understand '%s'" :
			   "db delete: a where is needed; it will not delete every row",
			   at < ac ? av[at] : "");
			dm_wfree(&ps);
			return 2;
		}
		if (dm_upgrade(d) != HIBR_OK) {
			dm_wfree(&ps);
			return HIBR_FAIL;
		}
		n = dm_scan(d, &ps, limit, dm_delrow, 0);
		dm_wfree(&ps);
	} else {
		for (i = 3; i < ac; i++)
			if (dm_rec(d, av[i], &gp, &k) != HIBR_OK)
				return HIBR_FAIL;
		if (dm_upgrade(d) != HIBR_OK)
			return HIBR_FAIL;
		for (i = 3; i < ac; i++)
			if (dm_rec(d, av[i], &gp, &k) == HIBR_OK) {
				dm_delrow(d, gp, k, 0);
				n++;
			}
	}
	s_init(&o);
	s_num(&o, n);
	dm_say(s, o.p);
	s_free(&o);
	return HIBR_OK;
}

/* db create|open|close|flush|insert|import|query|count|sum|min|max|avg|
   size|cols|set|update|delete|compact. */
int m_db(sh *s, int ac, char **av)
{
	const char *sub = ac > 1 ? av[1] : "";
	dm_db *d;
	str o;
	long h, limit;
	int i, at;
	vec ps = { 0, 0, 0 };
	struct dm_out out;

	if (ac < 3) {
		lg(HIBR_LERR, "usage: db create file col:type... | open file | "
			      "insert h val... | import h file | query h [-n] [where "
			      "col op val [and ...]] [limit n] [from n] | count|sum|min|"
			      "max|avg h [col] [where ...] | size h | cols h | "
			      "set h N col val... | update h [where ...] set col "
			      "val... | delete h N... | delete h where ... | "
			      "compact h | flush h | close h");
		return 2;
	}
	if (!strcmp(sub, "create") || !strcmp(sub, "open")) {
		if (!strcmp(sub, "create") && ac < 4) {
			lg(HIBR_LERR, "db create: name at least one column");
			return 2;
		}
		d = !strcmp(sub, "create") ? dm_create(av[2], ac - 3, av + 3)
					   : dm_open(av[2]);
		if (!d)
			return HIBR_FAIL;
		h = dm_keep(d);
		s_init(&o);
		s_num(&o, h);
		dm_say(s, o.p);
		s_free(&o);
		return HIBR_OK;
	}
	d = dm_get(av[2]);
	if (!d)
		return HIBR_FAIL;
	if (!strcmp(sub, "close")) {
		h = strtol(av[2], 0, 10);
		dm_free(d);
		dm_open_dbs.p[h - 1] = 0;
		return HIBR_OK;
	}
	if (!strcmp(sub, "flush"))
		return msync(d->map, d->mlen, MS_SYNC) ? HIBR_FAIL : HIBR_OK;
	if (!strcmp(sub, "insert")) {
		if (dm_insert(d, ac - 3, av + 3) != HIBR_OK)
			return HIBR_FAIL;
		s_init(&o);
		s_num(&o, (long)d->h->nrows);
		hibr_ret(s, o.p);
		s_free(&o);
		return HIBR_OK;
	}
	if (!strcmp(sub, "size")) {
		s_init(&o);
		s_num(&o, (long)(d->h->nrows - d->ndel));
		dm_say(s, o.p);
		s_free(&o);
		return HIBR_OK;
	}
	if (!strcmp(sub, "cols")) {
		s_init(&o);
		for (i = 0; i < d->nc; i++) {
			if (i)
				s_ch(&o, ' ');
			s_cat(&o, d->c[i].nm);
			s_cat(&o, d->c[i].ty == DM_INT ? ":int" :
				  d->c[i].ty == DM_FLOAT ? ":float" : ":str:");
			if (d->c[i].ty == DM_STR)
				s_num(&o, (long)d->c[i].w);
		}
		s_ch(&o, 0);
		dm_say(s, o.p);
		s_free(&o);
		return HIBR_OK;
	}
	if (!strcmp(sub, "import")) {
		FILE *f;
		char *line = 0, *p, **vals;
		size_t cap = 0, n = 0;
		ssize_t len;
		int k, bad = 0;

		if (ac < 4) {
			lg(HIBR_LERR, "db import: which file?");
			return 2;
		}
		f = strcmp(av[3], "-") ? fopen(av[3], "r") : stdin;
		if (!f) {
			lg(HIBR_LERR, "db: cannot read %s: %s", av[3],
			   strerror(errno));
			return HIBR_FAIL;
		}
		vals = xm(sizeof *vals * (size_t)d->nc);
		while ((len = getline(&line, &cap, f)) >= 0) {
			if (len && line[len - 1] == '\n')
				line[--len] = 0;
			if (!len)
				continue;
			p = line;
			for (k = 0; k < d->nc; k++) {
				vals[k] = p;
				p = strchr(p, '\t');
				if (!p)
					break;
				*p++ = 0;
			}
			if (k != d->nc - 1 || p ||
			    dm_insert(d, d->nc, vals) != HIBR_OK) {
				lg(HIBR_LERR, "db import: line %zu skipped",
				   n + (size_t)bad + 1);
				bad++;
				continue;
			}
			n++;
		}
		free(vals);
		free(line);
		if (f != stdin)
			fclose(f);
		s_init(&o);
		s_num(&o, (long)n);
		dm_say(s, o.p);
		s_free(&o);
		return bad ? HIBR_FAIL : HIBR_OK;
	}
	if (!strcmp(sub, "query")) {
		at = 3;
		memset(&out, 0, sizeof out);
		if (at < ac && !strcmp(av[at], "-n")) {
			out.num = 1;
			at++;
		}
		if (dm_where(d, ac, av, &at, &ps, &limit) != HIBR_OK) {
			dm_wfree(&ps);
			return 2;
		}
		if (at < ac) {
			lg(HIBR_LERR, "db: did not understand '%s'", av[at]);
			dm_wfree(&ps);
			return 2;
		}
		out.s = s;
		out.bind = s->bind;
		s_init(&out.line);
		if (out.bind) {
			v_del(s, "RET");
			hibr_set(s, "RET", "", 0);
		}
		fflush(stdout);
		dm_scan(d, &ps, limit, dm_row, &out);
		fflush(stdout);
		s_free(&out.line);
		dm_wfree(&ps);
		return out.n ? HIBR_OK : HIBR_FAIL;
	}
	if (!strcmp(sub, "count") || !strcmp(sub, "sum") ||
	    !strcmp(sub, "min") || !strcmp(sub, "max") || !strcmp(sub, "avg"))
		return dm_aggregate(s, d, sub, ac, av, 3);
	if (!strcmp(sub, "set"))
		return dm_cmdset(s, d, ac, av);
	if (!strcmp(sub, "update"))
		return dm_cmdupdate(s, d, ac, av);
	if (!strcmp(sub, "delete"))
		return dm_cmddelete(s, d, ac, av);
	if (!strcmp(sub, "compact")) {
		long kept = 0;

		if (dm_compact(d, &kept) != HIBR_OK)
			return HIBR_FAIL;
		s_init(&o);
		s_num(&o, kept);
		dm_say(s, o.p);
		s_free(&o);
		return HIBR_OK;
	}
	lg(HIBR_LERR, "db: no subcommand %s", sub);
	return 2;
}

/* Close every database left open when the module is dropped. */
void dm_fini(sh *s)
{
	size_t i;

	(void)s;
	for (i = 0; i < dm_open_dbs.n; i++)
		dm_free(dm_open_dbs.p[i]);
	v_free(&dm_open_dbs);
}

const hibr_bi db_bi[] = {
	{ "db", m_db, "a small column store: create, insert, update, delete, query, aggregate" },
	HIBR_BI_END
};

HIBR_MODULE("db", "0.2", "a small column store with zone maps", db_bi, 0,
	    dm_fini);
