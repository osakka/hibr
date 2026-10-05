#define _GNU_SOURCE

#include "hibr.h"
#include <dirent.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifndef HIBR_SHAREDIR
#define HIBR_SHAREDIR "/usr/local/share/hibr"
#endif

#ifndef SA_PI
#define SA_PI 3.14159265358979323846
#endif

typedef struct sa_meth sa_meth;
struct sa_meth {
	char *name, *title, *round, *path;
	double fajr, isha, maghrib;
	long ishamin, seasonal;
	double adj[6];
};

typedef struct sa_sun sa_sun;
struct sa_sun {
	double decl, ra, sid;
};

static sa_meth *sa_tab;
static size_t sa_n, sa_cap;
static int sa_loaded;

int sx_out(sh *s, const char *nm, const char *v);

/* Degrees to radians. */
double sa_rad(double d)
{
	return d * SA_PI / 180.0;
}

/* Radians to degrees. */
double sa_deg(double r)
{
	return r * 180.0 / SA_PI;
}

/* An angle brought into 0 to 360. */
double sa_unwind(double a)
{
	return a - 360.0 * floor(a / 360.0);
}

/* An angle brought into -180 to 180. */
double sa_shift(double a)
{
	if (a >= -180 && a <= 180)
		return a;
	return a - 360.0 * round(a / 360.0);
}

/* The Julian day of a Gregorian date at midnight UT, Meeus p.60. */
double sa_jd(long y, long m, long d)
{
	long yy = m > 2 ? y : y - 1, mm = m > 2 ? m : m + 12, a = yy / 100;
	long b = 2 - a + a / 4;

	return floor(365.25 * (yy + 4716)) + floor(30.6001 * (mm + 1)) + d + b - 1524.5;
}

/* Where the sun is on a Julian day: declination, right ascension, apparent sidereal time (Meeus ch. 25). */
void sa_sunat(double jd, sa_sun *o)
{
	double t = (jd - 2451545.0) / 36525.0, l0, lp, om, m, c, lam, e0, ea, th, dpsi, deps, o2;

	l0 = sa_unwind(280.4664567 + 36000.76983 * t + 0.0003032 * t * t);
	lp = sa_unwind(218.3165 + 481267.8813 * t);
	om = sa_unwind(125.04452 - 1934.136261 * t + 0.0020708 * t * t + t * t * t / 450000.0);
	m = sa_unwind(357.52911 + 35999.05029 * t - 0.0001537 * t * t);
	c = (1.914602 - 0.004817 * t - 0.000014 * t * t) * sin(sa_rad(m)) +
	    (0.019993 - 0.000101 * t) * sin(2 * sa_rad(m)) + 0.000289 * sin(3 * sa_rad(m));
	o2 = 125.04 - 1934.136 * t;
	lam = sa_rad(sa_unwind(l0 + c - 0.00569 - 0.00478 * sin(sa_rad(o2))));
	th = sa_unwind(280.46061837 + 360.98564736629 * (jd - 2451545.0) + 0.000387933 * t * t - t * t * t / 38710000.0);
	dpsi = -17.2 / 3600 * sin(sa_rad(om)) - 1.32 / 3600 * sin(2 * sa_rad(l0)) -
	       0.23 / 3600 * sin(2 * sa_rad(lp)) + 0.21 / 3600 * sin(2 * sa_rad(om));
	deps = 9.2 / 3600 * cos(sa_rad(om)) + 0.57 / 3600 * cos(2 * sa_rad(l0)) +
	       0.1 / 3600 * cos(2 * sa_rad(lp)) - 0.09 / 3600 * cos(2 * sa_rad(om));
	e0 = 23.439291 - 0.013004167 * t - 0.0000001639 * t * t + 0.0000005036 * t * t * t;
	ea = sa_rad(e0 + 0.00256 * cos(sa_rad(o2)));
	o->decl = sa_deg(asin(sin(ea) * sin(lam)));
	o->ra = sa_unwind(sa_deg(atan2(cos(ea) * sin(lam), cos(lam))));
	o->sid = th + dpsi * cos(sa_rad(e0 + deps));
}

/* Interpolate between three equidistant values, Meeus p.24. */
double sa_interp(double y2, double y1, double y3, double n)
{
	double a = y2 - y1, b = y3 - y2;

	return y2 + n / 2 * (a + b + n * (b - a));
}

/* The same for angles, unwinding the steps between them. */
double sa_interpa(double y2, double y1, double y3, double n)
{
	double a = sa_unwind(y2 - y1), b = sa_unwind(y3 - y2);

	return y2 + n / 2 * (a + b + n * (b - a));
}

/* The day's sun at a place: today, yesterday and tomorrow, and the approximate transit. */
typedef struct sa_day sa_day;
struct sa_day {
	sa_sun s, p, n;
	double lat, lon, m0;
};

/* Set up a day: the sun's three positions and the approximate transit (Meeus p.102). */
void sa_dayat(sa_day *d, long y, long m, long dd, double lat, double lon)
{
	double jd = sa_jd(y, m, dd), m0, ex;

	sa_sunat(jd, &d->s);
	sa_sunat(jd - 1, &d->p);
	sa_sunat(jd + 1, &d->n);
	d->lat = lat;
	d->lon = lon;
	m0 = (d->s.ra - lon - d->s.sid) / 360.0;
	m0 -= floor(m0);
	ex = (12.0 - lon / 15.0) / 24.0;
	ex -= floor(ex);
	if (m0 - ex > 0.5)
		m0 -= 1.0;
	else if (ex - m0 > 0.5)
		m0 += 1.0;
	d->m0 = m0;
}

/* Noon: the corrected transit, in hours UT. */
double sa_transit(sa_day *d)
{
	double th = sa_unwind(d->s.sid + 360.985647 * d->m0);
	double a = sa_unwind(sa_interpa(d->s.ra, d->p.ra, d->n.ra, d->m0));
	double h = sa_shift(th + d->lon - a);

	return (d->m0 - h / 360.0) * 24.0;
}

/* When the sun is at an altitude before or after noon, in hours UT; NaN if it never is. */
double sa_hour(sa_day *d, double h0, int after)
{
	double t1 = sin(sa_rad(h0)) - sin(sa_rad(d->lat)) * sin(sa_rad(d->s.decl));
	double t2 = cos(sa_rad(d->lat)) * cos(sa_rad(d->s.decl));
	double hh, m, th, a, del, h, alt, dm;

	hh = sa_deg(acos(t1 / t2));
	m = after ? d->m0 + hh / 360.0 : d->m0 - hh / 360.0;
	th = sa_unwind(d->s.sid + 360.985647 * m);
	a = sa_unwind(sa_interpa(d->s.ra, d->p.ra, d->n.ra, m));
	del = sa_interp(d->s.decl, d->p.decl, d->n.decl, m);
	h = th + d->lon - a;
	alt = sa_deg(asin(sin(sa_rad(d->lat)) * sin(sa_rad(del)) + cos(sa_rad(d->lat)) * cos(sa_rad(del)) * cos(sa_rad(h))));
	dm = (alt - h0) / (360.0 * cos(sa_rad(del)) * cos(sa_rad(d->lat)) * sin(sa_rad(h)));
	return (m + dm) * 24.0;
}

/* Asr: when a shadow is its length plus shadow times the object's. */
double sa_asr(sa_day *d, double shadow)
{
	double t = fabs(d->lat - d->s.decl);

	return sa_hour(d, sa_deg(atan(1.0 / (shadow + tan(sa_rad(t))))), 1);
}

/* Hours UT on a day as epoch seconds, whole seconds as adhan takes them; -1 for none. */
long sa_epoch(long y, long m, long d, double hrs)
{
	long jdn, h, mi, s;

	if (isnan(hrs))
		return -1;
	h = (long)floor(hrs);
	mi = (long)floor((hrs - h) * 60.0);
	s = (long)floor((hrs - (h + mi / 60.0)) * 3600.0);
	jdn = (long)floor(sa_jd(y, m, d) + 0.5);
	return (jdn - 2440588L) * 86400L + h * 3600L + mi * 60L + s;
}

/* Whether a Gregorian year is a leap year. */
int sa_leap(long y)
{
	return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
}

/* Days since the solstice nearest the start of the year, as the Moonsighting Committee counts. */
long sa_solst(long doy, long y, double lat)
{
	long n = sa_leap(y) ? 366 : 365, d;

	if (lat >= 0) {
		d = doy + 10;
		if (d >= n)
			d -= n;
	} else {
		d = doy - (sa_leap(y) ? 173 : 172);
		if (d < 0)
			d += n;
	}
	return d;
}

/* The Moonsighting Committee's seasonal minutes, from four values across the year. */
double sa_season(long dyy, double a, double b, double c, double d)
{
	if (dyy < 91)
		return a + (b - a) / 91.0 * dyy;
	if (dyy < 137)
		return b + (c - b) / 46.0 * (dyy - 91);
	if (dyy < 183)
		return c + (d - c) / 46.0 * (dyy - 137);
	if (dyy < 229)
		return d + (c - d) / 46.0 * (dyy - 183);
	if (dyy < 275)
		return c + (b - c) / 46.0 * (dyy - 229);
	return b + (a - b) / 91.0 * (dyy - 275);
}

/* A time moved by whole minutes and rounded as the method says. */
long sa_round(long t, double adj, const char *how)
{
	long s;

	if (t < 0)
		return t;
	t += (long)(adj * 60.0);
	s = ((t % 60) + 60) % 60;
	if (how && !strcmp(how, "none"))
		return t;
	if (how && !strcmp(how, "up"))
		return t + 60 - s;
	return s >= 30 ? t + 60 - s : t - s;
}

/* Skip blanks in a JSON text. */
const char *sa_ws(const char *p)
{
	while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')
		p++;
	return p;
}

/* Read a JSON string at p into o; the text after it, or 0. */
const char *sa_str(const char *p, str *o)
{
	o->n = 0;
	if (*p != '"')
		return 0;
	for (p++; *p && *p != '"'; p++) {
		if (*p == '\\' && p[1])
			p++;
		s_add(o, p, 1);
	}
	if (*p != '"')
		return 0;
	s_grow(o, 1);
	o->p[o->n] = 0;
	return p + 1;
}

/* Parse one method file into m; 0 when it is a whole, usable method. */
int sa_parse(const char *text, sa_meth *m)
{
	str k, v;
	const char *p = sa_ws(text);
	char *e;
	double n;
	int ok = 0, i;

	s_init(&k);
	s_init(&v);
	if (*p != '{')
		goto out;
	p = sa_ws(p + 1);
	while (*p && *p != '}') {
		if (!(p = sa_str(p, &k)))
			goto out;
		p = sa_ws(p);
		if (*p != ':')
			goto out;
		p = sa_ws(p + 1);
		if (*p == '"') {
			if (!(p = sa_str(p, &v)))
				goto out;
			if (!strcmp(k.p, "name"))
				m->name = xs(v.p);
			else if (!strcmp(k.p, "title"))
				m->title = xs(v.p);
			else if (!strcmp(k.p, "rounding"))
				m->round = xs(v.p);
		} else if (*p == '[') {
			p = sa_ws(p + 1);
			for (i = 0; *p && *p != ']'; i++) {
				n = strtod(p, &e);
				if (e == p)
					goto out;
				if (!strcmp(k.p, "adjust") && i < 6)
					m->adj[i] = n;
				p = sa_ws(e);
				if (*p == ',')
					p = sa_ws(p + 1);
			}
			if (*p != ']')
				goto out;
			p++;
		} else {
			n = strtod(p, &e);
			if (e == p)
				goto out;
			p = e;
			if (!strcmp(k.p, "fajr"))
				m->fajr = n;
			else if (!strcmp(k.p, "isha"))
				m->isha = n;
			else if (!strcmp(k.p, "isha_minutes"))
				m->ishamin = (long)n;
			else if (!strcmp(k.p, "maghrib"))
				m->maghrib = n;
			else if (!strcmp(k.p, "seasonal"))
				m->seasonal = (long)n;
		}
		p = sa_ws(p);
		if (*p == ',')
			p = sa_ws(p + 1);
	}
	ok = *p == '}' && m->name && m->fajr > 0 && (m->isha > 0 || m->ishamin > 0);
out:
	s_free(&k);
	s_free(&v);
	return ok ? 0 : -1;
}

/* Free one method's own memory. */
void sa_drop(sa_meth *m)
{
	free(m->name);
	free(m->title);
	free(m->round);
	free(m->path);
	memset(m, 0, sizeof *m);
}

/* The method named nm, or 0. */
sa_meth *sa_find(const char *nm)
{
	size_t i;

	for (i = 0; i < sa_n; i++)
		if (!strcmp(sa_tab[i].name, nm))
			return &sa_tab[i];
	return 0;
}

/* Keep a method, unless one of its name came from a folder before. */
void sa_add(sa_meth *m)
{
	if (sa_find(m->name)) {
		sa_drop(m);
		return;
	}
	if (sa_n == sa_cap) {
		sa_cap = sa_cap ? sa_cap * 2 : 16;
		sa_tab = realloc(sa_tab, sa_cap * sizeof *sa_tab);
		if (!sa_tab)
			abort();
	}
	sa_tab[sa_n++] = *m;
}

/* Read every *.json in one folder. */
void sa_dir(const char *d)
{
	DIR *dp = opendir(d);
	struct dirent *e;
	str p, t;
	FILE *f;
	size_t r, l;
	sa_meth m;

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
		memset(&m, 0, sizeof m);
		if (sa_parse(t.p, &m) == 0) {
			m.path = xs(p.p);
			sa_add(&m);
		} else {
			lg(HIBR_LINF, "salat: %s is not a method, left out", p.p);
			sa_drop(&m);
		}
	}
	closedir(dp);
	s_free(&p);
	s_free(&t);
}

/* The method there always is, the Muslim World League's, whatever the folders hold. */
void sa_builtin(void)
{
	sa_meth m;

	if (sa_find("mwl"))
		return;
	memset(&m, 0, sizeof m);
	m.name = xs("mwl");
	m.title = xs("Muslim World League");
	m.fajr = 18;
	m.isha = 17;
	m.adj[2] = 1;
	sa_add(&m);
}

/* The methods in order of their titles, so a list reads the same wherever the files are. */
void sa_sort(void)
{
	size_t i, j;
	sa_meth t;

	for (i = 1; i < sa_n; i++) {
		t = sa_tab[i];
		for (j = i; j > 0 && strcmp(sa_tab[j - 1].title ? sa_tab[j - 1].title : sa_tab[j - 1].name,
					  t.title ? t.title : t.name) > 0; j--)
			sa_tab[j] = sa_tab[j - 1];
		sa_tab[j] = t;
	}
}

/* Read the folders once: HIBR_SALAT, then the person's own, then the installed ones. */
void sa_load(void)
{
	const char *e = getenv("HIBR_SALAT"), *h, *x;
	char *dup, *tok, *save;
	str d;

	if (sa_loaded)
		return;
	sa_loaded = 1;
	s_init(&d);
	if (e && *e) {
		dup = xs(e);
		for (tok = strtok_r(dup, ":", &save); tok; tok = strtok_r(0, ":", &save))
			sa_dir(tok);
		free(dup);
	}
	x = getenv("XDG_CONFIG_HOME");
	h = getenv("HOME");
	if (x && *x) {
		s_cat(&d, x);
		s_cat(&d, "/hibr/salat");
		sa_dir(d.p);
	} else if (h && *h) {
		s_cat(&d, h);
		s_cat(&d, "/.config/hibr/salat");
		sa_dir(d.p);
	}
	sa_dir(HIBR_SHAREDIR "/salat");
	sa_builtin();
	sa_sort();
	s_free(&d);
}

/* Forget every method read. */
void sa_clear(void)
{
	size_t i;

	for (i = 0; i < sa_n; i++)
		sa_drop(&sa_tab[i]);
	free(sa_tab);
	sa_tab = 0;
	sa_n = sa_cap = 0;
	sa_loaded = 0;
}

/* The day of the year, 1 to 366. */
long sa_doy(long y, long m, long d)
{
	return (long)(sa_jd(y, m, d) - sa_jd(y, 1, 1)) + 1;
}

/* A day's times, as epoch seconds: fajr, sunrise, dhuhr, asr, maghrib, isha, and the night's middle. */
void sa_times(sa_meth *me, long y, long mo, long dd, double lat, double lon, int hanafi, const char *rule,
	      const char *shafaq, double *uadj, long *out)
{
	sa_day d, tm;
	long sr, ss, tsr, fa, is, mg, night, safe, ty, tmo, tdd, jdn, doy;
	double port_f, port_i, ma, mb, mc, md, al = fabs(lat);
	int i;

	sa_dayat(&d, y, mo, dd, lat, lon);
	jdn = (long)floor(sa_jd(y, mo, dd) + 0.5) + 1;
	{
		long a = jdn + 32044, b = (4 * a + 3) / 146097, c = a - 146097 * b / 4;
		long q = (4 * c + 3) / 1461, e = c - 1461 * q / 4, mm = (5 * e + 2) / 153;
		tdd = e - (153 * mm + 2) / 5 + 1;
		tmo = mm + 3 - 12 * (mm / 10);
		ty = 100 * b + q - 4800 + mm / 10;
	}
	sa_dayat(&tm, ty, tmo, tdd, lat, lon);
	out[2] = sa_epoch(y, mo, dd, sa_transit(&d));
	sr = sa_epoch(y, mo, dd, sa_hour(&d, -50.0 / 60.0, 0));
	ss = sa_epoch(y, mo, dd, sa_hour(&d, -50.0 / 60.0, 1));
	tsr = sa_epoch(ty, tmo, tdd, sa_hour(&tm, -50.0 / 60.0, 0));
	out[3] = sa_epoch(y, mo, dd, sa_asr(&d, hanafi ? 2.0 : 1.0));
	night = (tsr >= 0 && ss >= 0) ? tsr - ss : -1;
	port_f = port_i = 0.5;
	if (rule && !strcmp(rule, "seventh")) {
		port_f = port_i = 1.0 / 7.0;
	} else if (rule && !strcmp(rule, "angle")) {
		port_f = me->fajr / 60.0;
		port_i = me->isha / 60.0;
	}
	doy = sa_doy(y, mo, dd);
	fa = sa_epoch(y, mo, dd, sa_hour(&d, -me->fajr, 0));
	if (me->seasonal && lat >= 55)
		fa = night >= 0 ? (long)floor((double)sr - (double)night / 7.0) : -1;
	if (me->seasonal && sr >= 0) {
		ma = 75 + 28.65 / 55.0 * al;
		mb = 75 + 19.44 / 55.0 * al;
		mc = 75 + 32.74 / 55.0 * al;
		md = 75 + 48.1 / 55.0 * al;
		safe = sr + (long)round(sa_season(sa_solst(doy, y, lat), ma, mb, mc, md) * -60.0);
	} else {
		safe = (night >= 0 && sr >= 0) ? (long)floor((double)sr - port_f * night) : -1;
	}
	if (safe >= 0 && (fa < 0 || safe > fa))
		fa = safe;
	if (me->ishamin > 0) {
		is = ss >= 0 ? ss + me->ishamin * 60 : -1;
	} else {
		is = sa_epoch(y, mo, dd, sa_hour(&d, -me->isha, 1));
		if (me->seasonal && lat >= 55)
			is = night >= 0 ? (long)floor((double)ss + (double)night / 7.0) : -1;
		if (me->seasonal && ss >= 0) {
			if (shafaq && !strcmp(shafaq, "ahmer")) {
				ma = 62 + 17.4 / 55.0 * al;
				mb = 62 - 7.16 / 55.0 * al;
				mc = 62 + 5.12 / 55.0 * al;
				md = 62 + 19.44 / 55.0 * al;
			} else if (shafaq && !strcmp(shafaq, "abyad")) {
				ma = 75 + 25.6 / 55.0 * al;
				mb = 75 + 7.16 / 55.0 * al;
				mc = 75 + 36.84 / 55.0 * al;
				md = 75 + 81.84 / 55.0 * al;
			} else {
				ma = 75 + 25.6 / 55.0 * al;
				mb = 75 + 2.05 / 55.0 * al;
				mc = 75 - 9.21 / 55.0 * al;
				md = 75 + 6.14 / 55.0 * al;
			}
			safe = ss + (long)round(sa_season(sa_solst(doy, y, lat), ma, mb, mc, md) * 60.0);
		} else {
			safe = (night >= 0 && ss >= 0) ? (long)floor((double)ss + port_i * night) : -1;
		}
		if (safe >= 0 && (is < 0 || safe < is))
			is = safe;
	}
	mg = ss;
	if (me->maghrib > 0) {
		long am = sa_epoch(y, mo, dd, sa_hour(&d, -me->maghrib, 1));
		if (am >= 0 && ss >= 0 && ss < am && is > am)
			mg = am;
	}
	out[0] = fa;
	out[1] = sr;
	out[4] = mg;
	out[5] = is;
	for (i = 0; i < 6; i++)
		out[i] = sa_round(out[i], me->adj[i] + (uadj ? uadj[i] : 0), me->round);
	out[6] = (ss >= 0 && tsr >= 0) ? ss + (tsr - ss) / 2 : -1;
}

/* A date given as yyyy-mm-dd or a time in seconds, as the local calendar day; 0 if it is neither. */
int sa_day1(const char *a, long *y, long *m, long *d)
{
	struct tm tm;
	time_t t;
	char *e;
	long v;

	int n = 0;

	if (*a != '-' && sscanf(a, "%ld-%ld-%ld%n", y, m, d, &n) == 3 && n > 0 && !a[n])
		return *m >= 1 && *m <= 12 && *d >= 1 && *d <= 31;
	v = strtol(a, &e, 10);
	if (!*a || *e)
		return 0;
	t = (time_t)v;
	localtime_r(&t, &tm);
	*y = tm.tm_year + 1900;
	*m = tm.tm_mon + 1;
	*d = tm.tm_mday;
	return 1;
}

/* salat: the day's prayer times for a place, by a method read from a folder. */
int sa_bi(sh *s, int ac, char **av)
{
	const char *mn = "mwl", *rule = "middle", *shafaq = "general";
	sa_meth *me;
	double uadj[6] = { 0 }, lat, lon;
	long y, mo, d, out[7];
	int k = 2, hanafi = 0, i, rc;
	char *e;
	str o;
	time_t now;
	struct tm tm;
	size_t j;

	sa_load();
	if (ac < 2) {
		lg(HIBR_LERR, "usage: salat list | times [-m method] [-a hanafi|shafi] [-r middle|seventh|angle] [-q general|ahmer|abyad] [-j f,s,d,a,m,i] lat lon [yyyy-mm-dd|time] | info [-m method]");
		return 2;
	}
	if (!strcmp(av[1], "list")) {
		s_init(&o);
		for (j = 0; j < sa_n; j++) {
			s_cat(&o, sa_tab[j].name);
			s_ch(&o, '\t');
			s_cat(&o, sa_tab[j].title ? sa_tab[j].title : sa_tab[j].name);
			s_ch(&o, '\n');
		}
		if (o.n)
			o.p[--o.n] = 0;
		rc = sx_out(s, 0, o.n ? o.p : "");
		s_free(&o);
		return rc;
	}
	while (k < ac && av[k][0] == '-' && av[k][1] && !(av[k][1] >= '0' && av[k][1] <= '9') && av[k][1] != '.') {
		if (k + 1 >= ac) {
			lg(HIBR_LERR, "salat: %s needs a value", av[k]);
			return 2;
		}
		if (!strcmp(av[k], "-m")) {
			mn = av[++k];
		} else if (!strcmp(av[k], "-a")) {
			hanafi = !strcmp(av[++k], "hanafi");
		} else if (!strcmp(av[k], "-r")) {
			rule = av[++k];
		} else if (!strcmp(av[k], "-q")) {
			shafaq = av[++k];
		} else if (!strcmp(av[k], "-j")) {
			e = av[++k];
			for (i = 0; i < 6 && *e; i++) {
				uadj[i] = strtod(e, &e);
				if (*e == ',')
					e++;
			}
		} else {
			lg(HIBR_LERR, "salat: %s: unknown option", av[k]);
			return 2;
		}
		k++;
	}
	if (!(me = sa_find(mn))) {
		lg(HIBR_LERR, "salat: %s: no such method (salat list says which there are)", mn);
		return HIBR_FAIL;
	}
	if (!strcmp(av[1], "info")) {
		s_init(&o);
		s_cat(&o, me->name);
		s_ch(&o, '\t');
		s_cat(&o, me->title ? me->title : me->name);
		s_ch(&o, '\t');
		s_cat(&o, me->path ? me->path : "(built in)");
		rc = sx_out(s, 0, o.p);
		s_free(&o);
		return rc;
	}
	if (strcmp(av[1], "times")) {
		lg(HIBR_LERR, "salat: %s: no such command", av[1]);
		return 2;
	}
	if (ac - k < 2 || ac - k > 3) {
		lg(HIBR_LERR, "usage: salat times [options] lat lon [yyyy-mm-dd|time]");
		return 2;
	}
	lat = strtod(av[k], &e);
	if (*e || lat < -90 || lat > 90) {
		lg(HIBR_LERR, "salat: %s: not a latitude", av[k]);
		return 2;
	}
	lon = strtod(av[k + 1], &e);
	if (*e || lon < -180 || lon > 180) {
		lg(HIBR_LERR, "salat: %s: not a longitude", av[k + 1]);
		return 2;
	}
	if (ac - k == 3) {
		if (!sa_day1(av[k + 2], &y, &mo, &d)) {
			lg(HIBR_LERR, "salat: %s: not a yyyy-mm-dd date or a time in seconds", av[k + 2]);
			return 2;
		}
	} else {
		now = time(0);
		localtime_r(&now, &tm);
		y = tm.tm_year + 1900;
		mo = tm.tm_mon + 1;
		d = tm.tm_mday;
	}
	sa_times(me, y, mo, d, lat, lon, hanafi, rule, shafaq, uadj, out);
	s_init(&o);
	for (i = 0; i < 7; i++) {
		if (i)
			s_ch(&o, ' ');
		s_num(&o, out[i]);
	}
	s_grow(&o, 1);
	o.p[o.n] = 0;
	rc = sx_out(s, 0, o.p);
	s_free(&o);
	return rc;
}

/* Forget the methods when the module goes. */
void sa_fini(sh *s)
{
	(void)s;
	sa_clear();
}

const hibr_bi sa_bis[] = {
	{ "salat", sa_bi, "prayer times by a method read from a folder: salat list|times|info" },
	HIBR_BI_END
};

HIBR_MODULE("salat", "1.0", "prayer times: methods as data, the sun's place computed as Meeus gives it",
	    sa_bis, 0, sa_fini);
