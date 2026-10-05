#define _GNU_SOURCE

#include "hibr.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifndef HIBR_SHAREDIR
#define HIBR_SHAREDIR "/usr/local/share/hibr"
#endif

#ifndef HC_UNIX
#define HC_UNIX 2440588L
#endif

typedef struct hc_cal hc_cal;
struct hc_cal {
	char *name, *title, *kind, *outside, *path;
	char *months[12];
	long y0, j0, epoch;
	long *start;
	unsigned *mask;
	int ny, nleap, leap[30];
};

static hc_cal *hc_tab;
static size_t hc_n, hc_cap;
static int hc_loaded;

int sx_out(sh *s, const char *nm, const char *v);

/* Skip blanks in a JSON text. */
const char *hc_ws(const char *p)
{
	while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')
		p++;
	return p;
}

/* Read a JSON string at p into o; the text after it, or 0. */
const char *hc_str(const char *p, str *o)
{
	o->n = 0;
	if (*p != '"')
		return 0;
	for (p++; *p && *p != '"'; p++) {
		if (*p == '\\' && p[1]) {
			p++;
			switch (*p) {
			case 'n': s_add(o, "\n", 1); break;
			case 't': s_add(o, "\t", 1); break;
			default: s_add(o, p, 1); break;
			}
			continue;
		}
		s_add(o, p, 1);
	}
	if (*p != '"')
		return 0;
	s_grow(o, 1);
	o->p[o->n] = 0;
	return p + 1;
}

/* Read a JSON number at p into *v; the text after it, or 0. */
const char *hc_num(const char *p, long *v)
{
	char *e;

	*v = strtol(p, &e, 10);
	return e == p ? 0 : e;
}

/* A string copied out of a str. */
char *hc_dup(str *o)
{
	return xs(o->n ? o->p : "");
}

/* Free one calendar's own memory. */
void hc_drop(hc_cal *c)
{
	int i;

	free(c->name);
	free(c->title);
	free(c->kind);
	free(c->outside);
	free(c->path);
	for (i = 0; i < 12; i++)
		free(c->months[i]);
	free(c->start);
	free(c->mask);
	memset(c, 0, sizeof *c);
}

/* Read an array of numbers at p, keeping each with keep; the text after it, or 0. */
const char *hc_nums(const char *p, hc_cal *c, int leap)
{
	long v;
	size_t cap = 0;

	if (*p != '[')
		return 0;
	p = hc_ws(p + 1);
	while (*p && *p != ']') {
		if (!(p = hc_num(p, &v)))
			return 0;
		if (leap) {
			if (c->nleap < 30)
				c->leap[c->nleap++] = (int)v;
		} else {
			if ((size_t)c->ny >= cap) {
				cap = cap ? cap * 2 : 64;
				c->mask = realloc(c->mask, cap * sizeof *c->mask);
				if (!c->mask)
					return 0;
			}
			c->mask[c->ny++] = (unsigned)v;
		}
		p = hc_ws(p);
		if (*p == ',')
			p = hc_ws(p + 1);
	}
	return *p == ']' ? p + 1 : 0;
}

/* Read an array of month names at p; the text after it, or 0. */
const char *hc_names(const char *p, hc_cal *c, str *t)
{
	int i = 0;

	if (*p != '[')
		return 0;
	p = hc_ws(p + 1);
	while (*p && *p != ']') {
		if (!(p = hc_str(p, t)))
			return 0;
		if (i < 12) {
			free(c->months[i]);
			c->months[i++] = hc_dup(t);
		}
		p = hc_ws(p);
		if (*p == ',')
			p = hc_ws(p + 1);
	}
	return *p == ']' ? p + 1 : 0;
}

/* How many bits are set in v: a table year's 30-day months. */
int hc_bits(unsigned v)
{
	int n = 0;

	for (; v; v &= v - 1)
		n++;
	return n;
}

/* Parse one calendar file into c; 0 when it is a whole, valid calendar. */
int hc_parse(const char *text, hc_cal *c)
{
	str k, v;
	const char *p = hc_ws(text);
	long n;
	int ok = 0, y;

	s_init(&k);
	s_init(&v);
	if (*p != '{')
		goto out;
	p = hc_ws(p + 1);
	while (*p && *p != '}') {
		if (!(p = hc_str(p, &k)))
			goto out;
		p = hc_ws(p);
		if (*p != ':')
			goto out;
		p = hc_ws(p + 1);
		if (!strcmp(k.p, "years")) {
			p = hc_nums(p, c, 0);
		} else if (!strcmp(k.p, "leap")) {
			p = hc_nums(p, c, 1);
		} else if (!strcmp(k.p, "months")) {
			p = hc_names(p, c, &v);
		} else if (*p == '"') {
			if (!(p = hc_str(p, &v)))
				goto out;
			if (!strcmp(k.p, "name"))
				c->name = hc_dup(&v);
			else if (!strcmp(k.p, "title"))
				c->title = hc_dup(&v);
			else if (!strcmp(k.p, "kind"))
				c->kind = hc_dup(&v);
			else if (!strcmp(k.p, "outside"))
				c->outside = hc_dup(&v);
		} else {
			if (!(p = hc_num(p, &n)))
				goto out;
			if (!strcmp(k.p, "first_year"))
				c->y0 = n;
			else if (!strcmp(k.p, "first_day"))
				c->j0 = n;
			else if (!strcmp(k.p, "epoch"))
				c->epoch = n;
		}
		if (!p)
			goto out;
		p = hc_ws(p);
		if (*p == ',')
			p = hc_ws(p + 1);
	}
	if (*p != '}' || !c->name || !c->kind)
		goto out;
	if (!strcmp(c->kind, "table")) {
		if (!c->ny || !c->j0 || !c->y0)
			goto out;
		c->start = xm(sizeof *c->start * (c->ny + 1));
		c->start[0] = c->j0;
		for (y = 0; y < c->ny; y++)
			c->start[y + 1] = c->start[y] + 348 + hc_bits(c->mask[y] & 0xfff);
	} else if (!strcmp(c->kind, "tabular")) {
		if (!c->epoch || !c->nleap)
			goto out;
	} else {
		goto out;
	}
	ok = 1;
out:
	s_free(&k);
	s_free(&v);
	return ok ? 0 : -1;
}

/* The calendar named nm, or 0. */
hc_cal *hc_find(const char *nm)
{
	size_t i;

	for (i = 0; i < hc_n; i++)
		if (!strcmp(hc_tab[i].name, nm))
			return &hc_tab[i];
	return 0;
}

/* Add a calendar read from a file, unless one of its name came from a folder before. */
void hc_add(hc_cal *c)
{
	if (hc_find(c->name)) {
		hc_drop(c);
		return;
	}
	if (hc_n == hc_cap) {
		hc_cap = hc_cap ? hc_cap * 2 : 8;
		hc_tab = realloc(hc_tab, hc_cap * sizeof *hc_tab);
		if (!hc_tab)
			abort();
	}
	hc_tab[hc_n++] = *c;
}

/* Read every *.json in one folder. */
void hc_dir(const char *d)
{
	DIR *dp = opendir(d);
	struct dirent *e;
	str p, t;
	FILE *f;
	size_t r, l;
	hc_cal c;

	if (!dp)
		return;
	s_init(&p);
	s_init(&t);
	while ((e = readdir(dp))) {
		l = strlen(e->d_name);
		if (l < 6 || strcmp(e->d_name + l - 5, ".json"))
			continue;
		p.n = 0;
		s_cat(&p, d);
		s_cat(&p, "/");
		s_cat(&p, e->d_name);
		if (!(f = fopen(p.p, "r")))
			continue;
		t.n = 0;
		for (;;) {
			s_grow(&t, HIBR_IOCH);
			r = fread(t.p + t.n, 1, t.cap - t.n - 1, f);
			if (!r)
				break;
			t.n += r;
		}
		fclose(f);
		s_grow(&t, 1);
		t.p[t.n] = 0;
		memset(&c, 0, sizeof c);
		if (hc_parse(t.p, &c) == 0) {
			c.path = xs(p.p);
			hc_add(&c);
		} else {
			lg(HIBR_LINF, "hcal: %s is not a calendar, left out", p.p);
			hc_drop(&c);
		}
	}
	closedir(dp);
	s_free(&p);
	s_free(&t);
}

/* The arithmetic calendar there always is, whatever the folders hold. */
void hc_builtin(void)
{
	static const int lp[] = { 2, 5, 7, 10, 13, 16, 18, 21, 24, 26, 29 };
	hc_cal c;
	int i;

	if (hc_find("civil"))
		return;
	memset(&c, 0, sizeof c);
	c.name = xs("civil");
	c.title = xs("Tabular (civil)");
	c.kind = xs("tabular");
	c.epoch = 1948440;
	c.nleap = 11;
	for (i = 0; i < 11; i++)
		c.leap[i] = lp[i];
	hc_add(&c);
}

/* Read the folders once: HIBR_CALENDARS, then the person's own, then the installed ones. */
void hc_load(void)
{
	const char *e = getenv("HIBR_CALENDARS"), *h, *x;
	char *dup, *tok, *save;
	str d;

	if (hc_loaded)
		return;
	hc_loaded = 1;
	s_init(&d);
	if (e && *e) {
		dup = xs(e);
		for (tok = strtok_r(dup, ":", &save); tok; tok = strtok_r(0, ":", &save))
			hc_dir(tok);
		free(dup);
	}
	x = getenv("XDG_CONFIG_HOME");
	h = getenv("HOME");
	if (x && *x) {
		s_cat(&d, x);
		s_cat(&d, "/hibr/calendars");
		hc_dir(d.p);
	} else if (h && *h) {
		s_cat(&d, h);
		s_cat(&d, "/.config/hibr/calendars");
		hc_dir(d.p);
	}
	hc_dir(HIBR_SHAREDIR "/calendars");
	hc_builtin();
	s_free(&d);
}

/* Forget every calendar read. */
void hc_clear(void)
{
	size_t i;

	for (i = 0; i < hc_n; i++)
		hc_drop(&hc_tab[i]);
	free(hc_tab);
	hc_tab = 0;
	hc_n = hc_cap = 0;
	hc_loaded = 0;
}

/* The Julian day number of a proleptic Gregorian date. */
long hc_gjd(long y, long m, long d)
{
	long a = (14 - m) / 12, yy = y + 4800 - a, mm = m + 12 * a - 3;

	return d + (153 * mm + 2) / 5 + 365 * yy + yy / 4 - yy / 100 + yy / 400 - 32045;
}

/* The proleptic Gregorian date of a Julian day number. */
void hc_jdg(long j, long *y, long *m, long *d)
{
	long a = j + 32044, b = (4 * a + 3) / 146097, c = a - 146097 * b / 4;
	long dd = (4 * c + 3) / 1461, e = c - 1461 * dd / 4, mm = (5 * e + 2) / 153;

	*d = e - (153 * mm + 2) / 5 + 1;
	*m = mm + 3 - 12 * (mm / 10);
	*y = 100 * b + dd - 4800 + mm / 10;
}

/* How many of a tabular cycle's leap years fall in its first n years. */
long hc_leaps(hc_cal *c, long n)
{
	long k = 0, r = n % 30;
	int i;

	if (n <= 0)
		return 0;
	for (i = 0; i < c->nleap; i++)
		if (c->leap[i] <= r)
			k++;
	return (n / 30) * c->nleap + k;
}

/* Whether year y of a tabular calendar is a leap year. */
int hc_isleap(hc_cal *c, long y)
{
	long r = ((y - 1) % 30 + 30) % 30 + 1;
	int i;

	for (i = 0; i < c->nleap; i++)
		if (c->leap[i] == r)
			return 1;
	return 0;
}

/* The Julian day of the first of month m in year y of a tabular calendar. */
long hc_tstart(hc_cal *c, long y, long m)
{
	return c->epoch + (y - 1) * 354 + hc_leaps(c, y - 1) + (59 * (m - 1) + 1) / 2;
}

/* The length of month m of year y in a tabular calendar. */
int hc_tlen(hc_cal *c, long y, long m)
{
	if (m == 12)
		return hc_isleap(c, y) ? 30 : 29;
	return m % 2 ? 30 : 29;
}

/* A day of a tabular calendar from a Julian day. */
void hc_tfrom(hc_cal *c, long j, long *y, long *m, long *d)
{
	long yy = (30 * (j - c->epoch) + 10646) / 10631, mm;

	if (j < c->epoch)
		yy = (30 * (j - c->epoch) - 10631 + 10646) / 10631;
	while (hc_tstart(c, yy + 1, 1) <= j)
		yy++;
	while (hc_tstart(c, yy, 1) > j)
		yy--;
	mm = 12;
	while (mm > 1 && hc_tstart(c, yy, mm) > j)
		mm--;
	*y = yy;
	*m = mm;
	*d = j - hc_tstart(c, yy, mm) + 1;
}

/* The calendar a table hands its out-of-range days to. */
hc_cal *hc_beyond(hc_cal *c)
{
	hc_cal *o = c->outside ? hc_find(c->outside) : 0;

	if (!o || strcmp(o->kind, "tabular"))
		o = hc_find("civil");
	return o;
}

/* A day of any calendar from a Julian day. */
void hc_from(hc_cal *c, long j, long *y, long *m, long *d)
{
	long i, lo, hi, s;

	if (!strcmp(c->kind, "tabular")) {
		hc_tfrom(c, j, y, m, d);
		return;
	}
	if (j < c->start[0] || j >= c->start[c->ny]) {
		hc_tfrom(hc_beyond(c), j, y, m, d);
		return;
	}
	lo = 0;
	hi = c->ny - 1;
	while (lo < hi) {
		i = (lo + hi + 1) / 2;
		if (c->start[i] <= j)
			lo = i;
		else
			hi = i - 1;
	}
	s = c->start[lo];
	for (i = 0; i < 12; i++) {
		long n = (c->mask[lo] >> i & 1) ? 30 : 29;
		if (j < s + n)
			break;
		s += n;
	}
	*y = c->y0 + lo;
	*m = i + 1;
	*d = j - s + 1;
}

/* The length of month m of year y in any calendar. */
int hc_len(hc_cal *c, long y, long m)
{
	if (!strcmp(c->kind, "tabular"))
		return hc_tlen(c, y, m);
	if (y < c->y0 || y >= c->y0 + c->ny)
		return hc_tlen(hc_beyond(c), y, m);
	return (c->mask[y - c->y0] >> (m - 1) & 1) ? 30 : 29;
}

/* The Julian day of day d of month m of year y in any calendar. */
long hc_to(hc_cal *c, long y, long m, long d)
{
	long s, i;

	if (!strcmp(c->kind, "tabular"))
		return hc_tstart(c, y, m) + d - 1;
	if (y < c->y0 || y >= c->y0 + c->ny)
		return hc_tstart(hc_beyond(c), y, m) + d - 1;
	s = c->start[y - c->y0];
	for (i = 1; i < m; i++)
		s += hc_len(c, y, i);
	return s + d - 1;
}

/* The Julian day of a time, as the local clock reads it. */
long hc_today(time_t t)
{
	struct tm tm;

	localtime_r(&t, &tm);
	return hc_gjd(tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
}

/* A Gregorian date or a time in seconds, as a Julian day; -1 if it is neither. */
long hc_when(const char *a)
{
	long y, m, d;
	char *e;

	if (sscanf(a, "%ld-%ld-%ld", &y, &m, &d) == 3 && strchr(a, '-') != a)
		return hc_gjd(y, m, d);
	y = strtol(a, &e, 10);
	if (*a && !*e)
		return hc_today((time_t)y);
	return -1;
}

/* Up to four numbers as words, a negative one left out, said or bound. */
int hc_words(sh *s, long a, long b, long c, long d)
{
	long v[4] = { a, b, c, d };
	str o;
	int i, rc;

	s_init(&o);
	for (i = 0; i < 4 && v[i] >= 0; i++) {
		if (i)
			s_ch(&o, ' ');
		s_num(&o, v[i]);
	}
	s_grow(&o, 1);
	o.p[o.n] = 0;
	rc = sx_out(s, 0, o.p);
	s_free(&o);
	return rc;
}

/* A Gregorian date as yyyy-mm-dd, said or bound. */
int hc_iso(sh *s, long y, long m, long d)
{
	str o;
	int rc;

	s_init(&o);
	if (y < 1000)
		s_cat(&o, y < 10 ? "000" : y < 100 ? "00" : "0");
	s_num(&o, y);
	s_ch(&o, '-');
	if (m < 10)
		s_ch(&o, '0');
	s_num(&o, m);
	s_ch(&o, '-');
	if (d < 10)
		s_ch(&o, '0');
	s_num(&o, d);
	s_grow(&o, 1);
	o.p[o.n] = 0;
	rc = sx_out(s, 0, o.p);
	s_free(&o);
	return rc;
}

/* hcal: list the calendars, or turn a day into one and back. */
int hc_bi(sh *s, int ac, char **av)
{
	const char *cn = "umalqura";
	hc_cal *c;
	long adj = 0, j, y, m, d;
	int k = 2;
	str o;
	size_t i;

	hc_load();
	if (ac < 2) {
		lg(HIBR_LERR, "usage: hcal list | date [-c cal] [-a days] [time|yyyy-mm-dd] | greg [-c cal] [-a days] y m d | month [-c cal] y m | name [-c cal] m | info [-c cal]");
		return 2;
	}
	if (!strcmp(av[1], "list")) {
		s_init(&o);
		for (i = 0; i < hc_n; i++) {
			s_cat(&o, hc_tab[i].name);
			s_cat(&o, "\t");
			s_cat(&o, hc_tab[i].title ? hc_tab[i].title : hc_tab[i].name);
			s_cat(&o, "\n");
		}
		if (o.n)
			o.p[--o.n] = 0;
		k = sx_out(s, 0, o.n ? o.p : "");
		s_free(&o);
		return k;
	}
	while (k < ac && av[k][0] == '-' && av[k][1] && !(av[k][1] >= '0' && av[k][1] <= '9')) {
		if (!strcmp(av[k], "-c") && k + 1 < ac) {
			cn = av[++k];
		} else if (!strcmp(av[k], "-a") && k + 1 < ac) {
			adj = strtol(av[++k], 0, 10);
		} else {
			lg(HIBR_LERR, "hcal: %s: unknown option", av[k]);
			return 2;
		}
		k++;
	}
	if (!(c = hc_find(cn))) {
		lg(HIBR_LERR, "hcal: %s: no such calendar (hcal list says which there are)", cn);
		return HIBR_FAIL;
	}
	if (!strcmp(av[1], "date")) {
		j = k < ac ? hc_when(av[k]) : hc_today(time(0));
		if (j < 0) {
			lg(HIBR_LERR, "hcal date: %s: not a time in seconds or a yyyy-mm-dd date", av[k]);
			return 2;
		}
		hc_from(c, j + adj, &y, &m, &d);
		return hc_words(s, y, m, d, hc_len(c, y, m));
	}
	if (!strcmp(av[1], "greg")) {
		if (ac - k != 3) {
			lg(HIBR_LERR, "usage: hcal greg [-c cal] [-a days] y m d");
			return 2;
		}
		y = strtol(av[k], 0, 10);
		m = strtol(av[k + 1], 0, 10);
		d = strtol(av[k + 2], 0, 10);
		if (m < 1 || m > 12 || d < 1 || d > 30) {
			lg(HIBR_LERR, "hcal greg: %s %s %s: not a day", av[k], av[k + 1], av[k + 2]);
			return HIBR_FAIL;
		}
		hc_jdg(hc_to(c, y, m, d) - adj, &y, &m, &d);
		return hc_iso(s, y, m, d);
	}
	if (!strcmp(av[1], "month")) {
		if (ac - k != 2) {
			lg(HIBR_LERR, "usage: hcal month [-c cal] y m");
			return 2;
		}
		y = strtol(av[k], 0, 10);
		m = strtol(av[k + 1], 0, 10);
		if (m < 1 || m > 12) {
			lg(HIBR_LERR, "hcal month: %s: not a month", av[k + 1]);
			return HIBR_FAIL;
		}
		return hc_words(s, hc_len(c, y, m), -1, -1, -1);
	}
	if (!strcmp(av[1], "name")) {
		m = k < ac ? strtol(av[k], 0, 10) : 0;
		if (m < 1 || m > 12) {
			lg(HIBR_LERR, "usage: hcal name [-c cal] m");
			return 2;
		}
		return sx_out(s, 0, c->months[m - 1] ? c->months[m - 1] : "");
	}
	if (!strcmp(av[1], "info")) {
		s_init(&o);
		s_cat(&o, c->name);
		s_ch(&o, '\t');
		s_cat(&o, c->kind);
		s_ch(&o, '\t');
		s_cat(&o, c->title ? c->title : c->name);
		s_cat(&o, "\t");
		s_cat(&o, c->path ? c->path : "(built in)");
		k = sx_out(s, 0, o.p);
		s_free(&o);
		return k;
	}
	lg(HIBR_LERR, "hcal: %s: no such command", av[1]);
	return 2;
}

/* Forget the calendars when the module goes. */
void hc_fini(sh *s)
{
	(void)s;
	hc_clear();
}

const hibr_bi hc_bis[] = {
	{ "hcal", hc_bi, "calendar systems, Hijri and others: hcal list|date|greg|month|name|info" },
	HIBR_BI_END
};

HIBR_MODULE("hcal", "1.0", "calendar systems as data: Umm al-Qura, the tabular Hijri ones, and any added",
	    hc_bis, 0, hc_fini);
