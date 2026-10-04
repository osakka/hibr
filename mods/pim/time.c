#define _GNU_SOURCE
#include "pm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>

/* Days from 1970-01-01 to a civil date, for any year (Howard Hinnant's
   days_from_civil). */
long long pm_days(int y, int m, int d)
{
	long long era, yoe, doy, doe;

	y -= m <= 2;
	era = (y >= 0 ? y : y - 399) / 400;
	yoe = (long long)y - era * 400;
	doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
	doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
	return era * 146097 + doe - 719468;
}

/* The civil date of a day number. */
void pm_civil(long long z, int *y, int *m, int *d)
{
	long long era, doe, yoe, doy, mp;

	z += 719468;
	era = (z >= 0 ? z : z - 146096) / 146097;
	doe = z - era * 146097;
	yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
	doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
	mp = (5 * doy + 2) / 153;
	*d = (int)(doy - (153 * mp + 2) / 5 + 1);
	*m = (int)(mp < 10 ? mp + 3 : mp - 9);
	*y = (int)(yoe + era * 400 + (*m <= 2));
}

/* The weekday of a day number, Monday 0 to Sunday 6. */
int pm_wday(long long z)
{
	long long w = (z + 3) % 7;

	return (int)(w < 0 ? w + 7 : w);
}

/* Days in a month. */
int pm_mdays(int y, int m)
{
	static const int n[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };

	if (m == 2 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0))
		return 29;
	return n[(m - 1) % 12];
}

/* Read n digits as a number, or -1. */
int pm_digits(const char *p, int n)
{
	int v = 0, i;

	for (i = 0; i < n; i++) {
		if (p[i] < '0' || p[i] > '9')
			return -1;
		v = v * 10 + (p[i] - '0');
	}
	return v;
}

/* Parse a DATE (YYYYMMDD) or DATE-TIME (YYYYMMDDTHHMMSS[Z]) value, in the
   zone a TZID names, or floating; 0 on success. */
int pm_dtparse(const char *v, const char *tzid, int isdate, pm_dt *o)
{
	size_t n = strlen(v);

	memset(o, 0, sizeof *o);
	if (n < 8 || (o->y = pm_digits(v, 4)) < 0 || (o->mo = pm_digits(v + 4, 2)) < 1 ||
	    o->mo > 12 || (o->d = pm_digits(v + 6, 2)) < 1 || o->d > 31)
		return -1;
	if (n == 8 || isdate) {
		o->date = 1;
		return 0;
	}
	if (n < 15 || (v[8] != 'T' && v[8] != 't') || (o->h = pm_digits(v + 9, 2)) < 0 ||
	    (o->mi = pm_digits(v + 11, 2)) < 0 || (o->s = pm_digits(v + 13, 2)) < 0)
		return -1;
	if (v[15] == 'Z' || v[15] == 'z')
		o->utc = 1;
	else if (tzid && *tzid)
		o->tz = xs(tzid);
	return 0;
}

/* Free what a parsed moment holds. */
void pm_dtfree(pm_dt *t)
{
	free(t->tz);
	t->tz = 0;
}

/* Whether the system knows a zone by name: a file under zoneinfo, and a
   name with nothing in it that could climb out of that folder. */
int pm_tzok(const char *tz)
{
	str p;
	struct stat st;
	int ok;

	if (!tz || !*tz || *tz == '/' || strstr(tz, ".."))
		return 0;
	s_init(&p);
	s_cat(&p, "/usr/share/zoneinfo/");
	s_cat(&p, tz);
	ok = !stat(p.p, &st) && S_ISREG(st.st_mode);
	s_free(&p);
	return ok;
}

/* Run mktime or localtime in a zone, with TZ put back afterwards -- the
   shell and everything it starts read TZ too. A zone of null is the
   process's own. */
long long pm_inzone(const char *tz, struct tm *tm, long long t, int tolocal)
{
	char *old = 0;
	const char *cur = getenv("TZ");
	time_t tt = (time_t)t;
	long long r = 0;

	if (tz) {
		old = cur ? xs(cur) : 0;
		setenv("TZ", tz, 1);
		tzset();
	}
	if (tolocal)
		localtime_r(&tt, tm);
	else
		r = (long long)mktime(tm);
	if (tz) {
		if (old) {
			setenv("TZ", old, 1);
			free(old);
		} else {
			unsetenv("TZ");
		}
		tzset();
	}
	return r;
}

/* An offset written in a VTIMEZONE (+0100, -0530), in seconds. */
long pm_offset(const char *v)
{
	int h, m, sgn = 1;

	if (!v || (*v != '+' && *v != '-') || strlen(v) < 5)
		return 0;
	if (*v == '-')
		sgn = -1;
	h = pm_digits(v + 1, 2);
	m = pm_digits(v + 3, 2);
	if (h < 0 || m < 0)
		return 0;
	return sgn * (h * 3600L + m * 60L);
}

/* The standard offset a calendar's own VTIMEZONE gives a TZID, for a zone
   the system does not know (Outlook's "W. Europe Standard Time"): its
   STANDARD part's TZOFFSETTO, without daylight saving. 1 when found. */
int pm_vtzoff(pm_comp *cal, const char *tz, long *off)
{
	size_t i, j;
	pm_comp *z, *k;
	pm_prop *p;

	for (i = 0; cal && i < cal->kids.n; i++) {
		z = cal->kids.p[i];
		if (strcmp(z->name, "VTIMEZONE") || !(p = pm_get(z, "TZID")) || strcmp(p->val, tz))
			continue;
		for (j = 0; j < z->kids.n; j++) {
			k = z->kids.p[j];
			if (!strcmp(k->name, "STANDARD") && (p = pm_get(k, "TZOFFSETTO"))) {
				*off = pm_offset(p->val);
				return 1;
			}
		}
		for (j = 0; j < z->kids.n; j++) {
			k = z->kids.p[j];
			if ((p = pm_get(k, "TZOFFSETTO"))) {
				*off = pm_offset(p->val);
				return 1;
			}
		}
	}
	return 0;
}

/* Seconds since the epoch for a local time in a zone: a system zone through
   zoneinfo, one only the calendar describes at its standard offset, and
   anything else -- a floating time, an unknown zone -- in this process's
   own zone. */
long long pm_local(int y, int mo, int d, int h, int mi, int s, const char *tz, pm_comp *cal)
{
	struct tm tm;
	long off;

	if (tz && !strcmp(tz, "UTC"))
		return pm_days(y, mo, d) * 86400 + h * 3600LL + mi * 60LL + s;
	if (tz && !pm_tzok(tz)) {
		if (pm_vtzoff(cal, tz, &off))
			return pm_days(y, mo, d) * 86400 + h * 3600LL + mi * 60LL + s - off;
		lg(HIBR_LDBG, "pim: zone %s is unknown here; read as local time", tz);
		tz = 0;
	}
	memset(&tm, 0, sizeof tm);
	tm.tm_year = y - 1900;
	tm.tm_mon = mo - 1;
	tm.tm_mday = d;
	tm.tm_hour = h;
	tm.tm_min = mi;
	tm.tm_sec = s;
	tm.tm_isdst = -1;
	return pm_inzone(tz, &tm, 0, 0);
}

/* A moment as seconds since the epoch: a whole day from local midnight. */
long long pm_epoch(const pm_dt *t, pm_comp *cal)
{
	if (t->date)
		return pm_local(t->y, t->mo, t->d, 0, 0, 0, 0, cal);
	if (t->utc)
		return pm_days(t->y, t->mo, t->d) * 86400 + t->h * 3600LL + t->mi * 60LL + t->s;
	return pm_local(t->y, t->mo, t->d, t->h, t->mi, t->s, t->tz, cal);
}

/* The civil time of a moment in a zone (or this process's own). */
void pm_tolocal(long long t, const char *tz, int *y, int *mo, int *d, int *h, int *mi, int *s)
{
	struct tm tm;

	if (tz && !strcmp(tz, "UTC")) {
		long long day = t >= 0 ? t / 86400 : (t - 86399) / 86400, r = t - day * 86400;

		pm_civil(day, y, mo, d);
		*h = (int)(r / 3600);
		*mi = (int)(r / 60 % 60);
		*s = (int)(r % 60);
		return;
	}
	memset(&tm, 0, sizeof tm);
	pm_inzone(tz && pm_tzok(tz) ? tz : 0, &tm, t, 1);
	*y = tm.tm_year + 1900;
	*mo = tm.tm_mon + 1;
	*d = tm.tm_mday;
	*h = tm.tm_hour;
	*mi = tm.tm_min;
	*s = tm.tm_sec;
}

/* A DURATION (RFC 5545 3.3.6) in seconds: [+-]P[nW][nD][T[nH][nM][nS]].
   0 on success. */
int pm_dur(const char *v, long long *secs)
{
	long long n = 0, t = 0;
	int sgn = 1, intime = 0, any = 0;

	if (*v == '+' || *v == '-')
		sgn = *v++ == '-' ? -1 : 1;
	if (*v != 'P' && *v != 'p')
		return -1;
	for (v++; *v; v++) {
		if (*v >= '0' && *v <= '9') {
			n = n * 10 + (*v - '0');
			any = 1;
			continue;
		}
		switch (*v) {
		case 'T': case 't': intime = 1; continue;
		case 'W': case 'w': t += n * 604800; break;
		case 'D': case 'd': t += n * 86400; break;
		case 'H': case 'h': if (!intime) return -1; t += n * 3600; break;
		case 'M': case 'm': if (!intime) return -1; t += n * 60; break;
		case 'S': case 's': if (!intime) return -1; t += n; break;
		default: return -1;
		}
		n = 0;
	}
	if (!any)
		return -1;
	*secs = sgn * t;
	return 0;
}

/* A date or date-time property's moment: its TZID and VALUE=DATE read,
   isdate set for a whole day. Returns LLONG_MIN-ish -1LL<<62 when it does
   not parse. */
long long pm_propt(pm_prop *p, pm_comp *cal, int *isdate)
{
	pm_dt t;
	const char *vt;
	long long r;

	if (isdate)
		*isdate = 0;
	if (!p)
		return -(1LL << 62);
	vt = pm_pget(p, "VALUE");
	if (pm_dtparse(p->val, pm_pget(p, "TZID"), vt && !strcasecmp(vt, "DATE"), &t))
		return -(1LL << 62);
	if (isdate)
		*isdate = t.date;
	r = pm_epoch(&t, cal);
	pm_dtfree(&t);
	return r;
}

/* Make a zone the process's own for a while, keeping what TZ was; the old
   value is given back to pm_tzpop. */
char *pm_tzpush(const char *tz)
{
	const char *cur = getenv("TZ");
	char *old = xs(cur ? cur : "");

	if (!cur)
		old[0] = 1;
	setenv("TZ", tz, 1);
	tzset();
	return old;
}

/* Put TZ back as pm_tzpush found it. */
void pm_tzpop(char *old)
{
	if (old[0] == 1)
		unsetenv("TZ");
	else
		setenv("TZ", old, 1);
	free(old);
	tzset();
}

/* The UTC offset of the current zone at a moment, and whether it is
   daylight saving time then. */
long pm_zoffnow(long long t, int *dst)
{
	struct tm tm;
	time_t tt = (time_t)t;

	localtime_r(&tt, &tm);
	if (dst)
		*dst = tm.tm_isdst > 0;
	return tm.tm_gmtoff;
}

/* An offset written as +HHMM. */
void pm_offout(str *o, long off)
{
	long a = off < 0 ? -off : off;

	s_ch(o, off < 0 ? '-' : '+');
	s_ch(o, (char)('0' + a / 36000));
	s_ch(o, (char)('0' + a / 3600 % 10));
	s_ch(o, (char)('0' + a / 600 % 6));
	s_ch(o, (char)('0' + a / 60 % 10));
}

/* A local moment written as YYYYMMDDTHHMMSS. */
void pm_dtout(str *o, int y, int mo, int d, int h, int mi, int s)
{
	str b;
	int v[] = { y, mo, d, h, mi, s }, w[] = { 4, 2, 2, 2, 2, 2 }, i, k;

	s_init(&b);
	for (i = 0; i < 6; i++) {
		if (i == 3)
			s_ch(o, 'T');
		b.n = 0;
		s_num(&b, v[i]);
		for (k = (int)b.n; k < w[i]; k++)
			s_ch(o, '0');
		s_cat(o, b.p);
	}
	s_free(&b);
}

/* A VTIMEZONE for a system zone, made from what zoneinfo says between two
   years: each change of offset found by a daily walk and then pinned to
   the second, written as a STANDARD or DAYLIGHT part with its own DTSTART.
   Exact for every moment in those years, which is what a server, or the
   other end of an invitation, needs. The zone is set once for the walk. */
void pm_vtimezone(str *o, const char *tz, int y0, int y1)
{
	long long t = pm_days(y0, 1, 1) * 86400, end = pm_days(y1 + 1, 1, 1) * 86400, lo, hi, mid;
	long prev, cur, std;
	int y, mo, d, h, mi, s, any = 0, dst;
	str l;
	char *old = pm_tzpush(tz);
	vec parts = { 0, 0, 0 };
	size_t i;

	s_init(&l);
	prev = std = pm_zoffnow(t, 0);
	for (; t < end; t += 86400) {
		cur = pm_zoffnow(t + 86400, 0);
		if (cur == prev)
			continue;
		lo = t;
		hi = t + 86400;
		while (hi - lo > 1) {
			mid = lo + (hi - lo) / 2;
			if (pm_zoffnow(mid, 0) == prev)
				lo = mid;
			else
				hi = mid;
		}
		pm_zoffnow(hi, &dst);
		{
			struct tm tm;
			time_t tt = (time_t)hi;
			str *pt = xm(sizeof *pt);

			s_init(pt);
			localtime_r(&tt, &tm);
			pm_line(pt, dst ? "BEGIN:DAYLIGHT" : "BEGIN:STANDARD");
			pm_tolocal(hi + prev, "UTC", &y, &mo, &d, &h, &mi, &s);
			l.n = 0;
			s_cat(&l, "DTSTART:");
			pm_dtout(&l, y, mo, d, h, mi, s);
			pm_lineb(pt, &l);
			l.n = 0;
			s_cat(&l, "TZOFFSETFROM:");
			pm_offout(&l, prev);
			pm_lineb(pt, &l);
			l.n = 0;
			s_cat(&l, "TZOFFSETTO:");
			pm_offout(&l, cur);
			pm_lineb(pt, &l);
			if (tm.tm_zone && *tm.tm_zone) {
				l.n = 0;
				s_cat(&l, "TZNAME:");
				s_cat(&l, tm.tm_zone);
				pm_lineb(pt, &l);
			}
			pm_line(pt, dst ? "END:DAYLIGHT" : "END:STANDARD");
			v_add(&parts, pt);
		}
		prev = cur;
		any = 1;
	}
	pm_tzpop(old);
	pm_line(o, "BEGIN:VTIMEZONE");
	l.n = 0;
	s_cat(&l, "TZID:");
	s_cat(&l, tz);
	pm_lineb(o, &l);
	for (i = 0; i < parts.n; i++) {
		str *pt = parts.p[i];

		s_add(o, pt->p, pt->n);
		s_free(pt);
		free(pt);
	}
	v_free(&parts);
	if (!any) {
		pm_line(o, "BEGIN:STANDARD");
		pm_line(o, "DTSTART:19700101T000000");
		l.n = 0;
		s_cat(&l, "TZOFFSETFROM:");
		pm_offout(&l, std);
		pm_lineb(o, &l);
		l.n = 0;
		s_cat(&l, "TZOFFSETTO:");
		pm_offout(&l, std);
		pm_lineb(o, &l);
		pm_line(o, "END:STANDARD");
	}
	pm_line(o, "END:VTIMEZONE");
	s_free(&l);
}
