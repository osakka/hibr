#define _GNU_SOURCE
#include "pm.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

char *pm_tzpush(const char *tz);
void pm_tzpop(char *old);
int pm_tzok(const char *tz);
int pm_vtzoff(pm_comp *cal, const char *tz, long *off);
long long pm_inzone(const char *tz, struct tm *tm, long long t, int tolocal);

/* Add an int to a vec of ints. */
void pm_vin(vec *v, int x)
{
	v_add(v, (void *)(intptr_t)x);
}

/* An int of a vec of ints. */
int pm_vat(vec *v, size_t i)
{
	return (int)(intptr_t)v->p[i];
}

/* Whether a vec of ints holds a value. */
int pm_vhas(vec *v, int x)
{
	size_t i;

	for (i = 0; i < v->n; i++)
		if (pm_vat(v, i) == x)
			return 1;
	return 0;
}

/* A weekday's two letters, Monday 0; -1 when it is not one. */
int pm_wdof(const char *p)
{
	static const char *n[] = { "MO", "TU", "WE", "TH", "FR", "SA", "SU" };
	int i;

	for (i = 0; i < 7; i++)
		if (!strncasecmp(p, n[i], 2))
			return i;
	return -1;
}

/* Read a comma-separated list of numbers into a vec, each within bounds and
   never zero unless zero is allowed. 0 on success. */
int pm_ints(const char *v, vec *o, int lo, int hi, int zero)
{
	char *e;
	long n;

	while (*v) {
		n = strtol(v, &e, 10);
		if (e == v || n < lo || n > hi || (!zero && n == 0))
			return -1;
		pm_vin(o, (int)n);
		v = e;
		if (*v == ',')
			v++;
		else if (*v)
			return -1;
	}
	return 0;
}

/* Free a rule's lists. */
void pm_rulefree(pm_rule *r)
{
	v_free(&r->bymonth);
	v_free(&r->byweekno);
	v_free(&r->byyearday);
	v_free(&r->bymonthday);
	v_free(&r->byday);
	v_free(&r->byhour);
	v_free(&r->byminute);
	v_free(&r->bysecond);
	v_free(&r->bysetpos);
}

/* Parse an RRULE value. UNTIL is read in the zone of DTSTART, a date as the
   end of that day. 0 on success, with a reason logged otherwise. */
int pm_ruleparse(const char *v, pm_rule *r, pm_comp *cal, const char *tz)
{
	char *dup = xs(v), *part, *save = 0, *eq;
	int bad = 0;

	memset(r, 0, sizeof *r);
	r->freq = -1;
	r->interval = 1;
	for (part = strtok_r(dup, ";", &save); part && !bad; part = strtok_r(0, ";", &save)) {
		eq = strchr(part, '=');
		if (!eq) {
			bad = 1;
			break;
		}
		*eq++ = 0;
		pm_upper(part);
		if (!strcmp(part, "FREQ")) {
			const char *f[] = { "SECONDLY", "MINUTELY", "HOURLY", "DAILY", "WEEKLY",
					    "MONTHLY", "YEARLY", 0 };
			int i;

			for (i = 0; f[i] && strcasecmp(eq, f[i]); i++)
				;
			if (!f[i])
				bad = 1;
			r->freq = i;
		} else if (!strcmp(part, "INTERVAL")) {
			r->interval = atoi(eq);
			bad = r->interval < 1;
		} else if (!strcmp(part, "COUNT")) {
			r->count = atoi(eq);
			bad = r->count < 1;
		} else if (!strcmp(part, "UNTIL")) {
			pm_dt t;

			if (pm_dtparse(eq, tz, 0, &t)) {
				bad = 1;
			} else {
				r->hasuntil = 1;
				if (t.date)
					r->until = pm_local(t.y, t.mo, t.d, 23, 59, 59, tz, cal);
				else
					r->until = pm_epoch(&t, cal);
				pm_dtfree(&t);
			}
		} else if (!strcmp(part, "WKST")) {
			r->wkst = pm_wdof(eq);
			bad = r->wkst < 0;
		} else if (!strcmp(part, "BYMONTH")) {
			bad = pm_ints(eq, &r->bymonth, 1, 12, 0);
		} else if (!strcmp(part, "BYWEEKNO")) {
			bad = pm_ints(eq, &r->byweekno, -53, 53, 0);
		} else if (!strcmp(part, "BYYEARDAY")) {
			bad = pm_ints(eq, &r->byyearday, -366, 366, 0);
		} else if (!strcmp(part, "BYMONTHDAY")) {
			bad = pm_ints(eq, &r->bymonthday, -31, 31, 0);
		} else if (!strcmp(part, "BYHOUR")) {
			bad = pm_ints(eq, &r->byhour, 0, 23, 1);
		} else if (!strcmp(part, "BYMINUTE")) {
			bad = pm_ints(eq, &r->byminute, 0, 59, 1);
		} else if (!strcmp(part, "BYSECOND")) {
			bad = pm_ints(eq, &r->bysecond, 0, 60, 1);
		} else if (!strcmp(part, "BYSETPOS")) {
			bad = pm_ints(eq, &r->bysetpos, -366, 366, 0);
		} else if (!strcmp(part, "BYDAY")) {
			char *p = eq, *e;
			long n;
			int wd;

			while (*p && !bad) {
				n = strtol(p, &e, 10);
				if (e == p)
					n = 0;
				wd = pm_wdof(e);
				if (wd < 0 || n < -53 || n > 53) {
					bad = 1;
					break;
				}
				pm_vin(&r->byday, (int)n * 8 + wd);
				p = e + 2;
				if (*p == ',')
					p++;
			}
		} else {
			lg(HIBR_LDBG, "pim: RRULE part %s not understood, left out", part);
		}
	}
	if (r->freq < 0)
		bad = 1;
	if (bad)
		lg(HIBR_LERR, "pim: RRULE %s does not parse", v);
	free(dup);
	return bad ? -1 : 0;
}

/* The ordinal and weekday packed in a BYDAY entry. */
int pm_bdord(int x)
{
	return x >= 0 ? x / 8 : -((-x + 7) / 8);
}

/* Its weekday. */
int pm_bdwd(int x)
{
	return x - pm_bdord(x) * 8;
}

/* The week number of a day in its year, weeks starting on wkst and week 1
   the first with four days of the year in it (RFC 5545's BYWEEKNO); the
   number of weeks that year in nweeks. A day before week 1 is in the last
   week of the year before, and returns 0 here. */
int pm_weekno(long long day, int y, int wkst, int *nweeks)
{
	long long jan1 = pm_days(y, 1, 1), w1;
	int off = (pm_wday(jan1) - wkst + 7) % 7, ylen = (int)(pm_days(y + 1, 1, 1) - jan1);

	w1 = off <= 3 ? jan1 - off : jan1 + (7 - off);
	*nweeks = (int)((jan1 + ylen - w1 + (7 - 4)) / 7);
	{
		long long nj = pm_days(y + 1, 1, 1);
		int noff = (pm_wday(nj) - wkst + 7) % 7;
		long long nw1 = noff <= 3 ? nj - noff : nj + (7 - noff);

		*nweeks = (int)((nw1 - w1) / 7);
	}
	if (day < w1)
		return 0;
	return (int)((day - w1) / 7) + 1;
}

/* Whether a day passes the rule's day-level parts for the period it is in:
   BYMONTH, BYWEEKNO, BYYEARDAY, BYMONTHDAY, BYDAY (an ordinal counting in
   the month for a monthly rule or a yearly one with BYMONTH, else in the
   year), and the defaults RFC 5545 takes from DTSTART when none is given. */
int pm_dayok(pm_rule *r, long long day, int y0, int mo0, int d0, int wd0)
{
	int y, m, d, wd = pm_wday(day), md, yd, ylen, i, ok, n, nw, wn, inmonth;
	long long jan1;

	pm_civil(day, &y, &m, &d);
	md = pm_mdays(y, m);
	jan1 = pm_days(y, 1, 1);
	yd = (int)(day - jan1) + 1;
	ylen = (int)(pm_days(y + 1, 1, 1) - jan1);
	if (r->bymonth.n && !pm_vhas(&r->bymonth, m))
		return 0;
	if (r->byweekno.n) {
		wn = pm_weekno(day, y, r->wkst, &nw);
		ok = 0;
		for (i = 0; i < (int)r->byweekno.n && !ok; i++) {
			n = pm_vat(&r->byweekno, (size_t)i);
			ok = wn && (n > 0 ? n == wn : nw + n + 1 == wn);
		}
		if (!ok)
			return 0;
	}
	if (r->byyearday.n) {
		ok = 0;
		for (i = 0; i < (int)r->byyearday.n && !ok; i++) {
			n = pm_vat(&r->byyearday, (size_t)i);
			ok = n > 0 ? n == yd : ylen + n + 1 == yd;
		}
		if (!ok)
			return 0;
	}
	if (r->bymonthday.n) {
		ok = 0;
		for (i = 0; i < (int)r->bymonthday.n && !ok; i++) {
			n = pm_vat(&r->bymonthday, (size_t)i);
			ok = n > 0 ? n == d : md + n + 1 == d;
		}
		if (!ok)
			return 0;
	}
	if (r->byday.n) {
		inmonth = r->freq == PM_MONTHLY || (r->freq == PM_YEARLY && r->bymonth.n);
		ok = 0;
		for (i = 0; i < (int)r->byday.n && !ok; i++) {
			int x = pm_vat(&r->byday, (size_t)i), o = pm_bdord(x);

			if (pm_bdwd(x) != wd)
				continue;
			if (!o || r->freq < PM_MONTHLY || (r->freq == PM_WEEKLY))
				ok = 1;
			else if (inmonth)
				ok = o > 0 ? (d - 1) / 7 + 1 == o : (md - d) / 7 + 1 == -o;
			else
				ok = o > 0 ? (yd - 1) / 7 + 1 == o : (ylen - yd) / 7 + 1 == -o;
		}
		if (!ok)
			return 0;
	}
	if (!r->byweekno.n && !r->byyearday.n && !r->bymonthday.n && !r->byday.n) {
		if (r->freq == PM_WEEKLY && wd != wd0)
			return 0;
		if (r->freq == PM_MONTHLY && d != d0)
			return 0;
		if (r->freq == PM_YEARLY) {
			if (d != d0)
				return 0;
			if (!r->bymonth.n && m != mo0)
				return 0;
		}
	}
	(void)y0;
	return 1;
}

/* Whether a period of a rule finer than a day falls where the rule's BY
   parts allow: its month, day of the month, weekday, and -- for a rule
   finer than the part -- its hour and minute. */
int pm_subok(pm_rule *r, long long day, int h, int mi)
{
	int y, m, d, md, i, ok, n;

	pm_civil(day, &y, &m, &d);
	md = pm_mdays(y, m);
	if (r->bymonth.n && !pm_vhas(&r->bymonth, m))
		return 0;
	if (r->bymonthday.n) {
		ok = 0;
		for (i = 0; i < (int)r->bymonthday.n && !ok; i++) {
			n = pm_vat(&r->bymonthday, (size_t)i);
			ok = n > 0 ? n == d : md + n + 1 == d;
		}
		if (!ok)
			return 0;
	}
	if (r->byday.n) {
		ok = 0;
		for (i = 0; i < (int)r->byday.n && !ok; i++)
			ok = pm_bdwd(pm_vat(&r->byday, (size_t)i)) == pm_wday(day);
		if (!ok)
			return 0;
	}
	if (r->byhour.n && !pm_vhas(&r->byhour, h))
		return 0;
	if (r->freq <= PM_MINUTELY && r->byminute.n && !pm_vhas(&r->byminute, mi))
		return 0;
	return 1;
}

/* How a candidate's local time becomes a moment: in UTC, in a system zone
   set for the whole walk, at a calendar zone's fixed offset, or in the
   process's own zone (floating, and whole days). */
typedef struct pm_zc pm_zc;
struct pm_zc {
	int mode;
	long off;
};

/* A local time as seconds since the epoch, by a zone context. */
long long pm_zmk(pm_zc *z, long long day, int h, int mi, int s)
{
	struct tm tm;
	int y, mo, d;

	if (z->mode == 0)
		return day * 86400 + h * 3600LL + mi * 60LL + s;
	if (z->mode == 2)
		return day * 86400 + h * 3600LL + mi * 60LL + s - z->off;
	pm_civil(day, &y, &mo, &d);
	memset(&tm, 0, sizeof tm);
	tm.tm_year = y - 1900;
	tm.tm_mon = mo - 1;
	tm.tm_mday = d;
	tm.tm_hour = h;
	tm.tm_min = mi;
	tm.tm_sec = s;
	tm.tm_isdst = -1;
	return (long long)mktime(&tm);
}

/* Sort helper for candidate moments. */
int pm_llcmp(const void *a, const void *b)
{
	long long x = *(const long long *)a, y = *(const long long *)b;

	return x < y ? -1 : x > y;
}

/* Expand a recurrence from DTSTART into the instances that overlap
   [from, to): each a start, as seconds since the epoch, added to out as a
   heap-allocated long long. DTSTART is always the first instance. The walk
   goes period by period -- a day, a week, a month or a year, INTERVAL of
   them at a time -- making each period's candidate days and times,
   choosing BYSETPOS among them, and stops at COUNT, UNTIL, the end of the
   window, PM_MAXINST instances or PM_MAXSTEPS periods. */
int pm_rrule(pm_rule *r, const pm_dt *st, pm_comp *cal, long long from, long long to,
	     long long dur, vec *out)
{
	pm_zc z;
	char *old = 0;
	long long d0 = pm_days(st->y, st->mo, st->d), t0, day, pstart, t, *keep;
	int wd0 = pm_wday(d0), count = 0, steps = 0, done = 0, y, m, dd, i, ndays;
	vec hrs = { 0, 0, 0 }, mins = { 0, 0, 0 }, secs = { 0, 0, 0 };
	long long *cand = 0;
	size_t nc = 0, cap = 0, k, a, b, c;

	memset(&z, 0, sizeof z);
	if (st->date) {
		z.mode = 3;
	} else if (st->utc) {
		z.mode = 0;
	} else if (st->tz && !strcmp(st->tz, "UTC")) {
		z.mode = 0;
	} else if (st->tz && pm_tzok(st->tz)) {
		z.mode = 1;
		old = pm_tzpush(st->tz);
	} else if (st->tz && pm_vtzoff(cal, st->tz, &z.off)) {
		z.mode = 2;
	} else {
		z.mode = 3;
	}
	t0 = pm_zmk(&z, d0, st->date ? 0 : st->h, st->date ? 0 : st->mi, st->date ? 0 : st->s);
	if (r->byhour.n && r->freq >= PM_DAILY)
		for (k = 0; k < r->byhour.n; k++)
			pm_vin(&hrs, pm_vat(&r->byhour, k));
	else
		pm_vin(&hrs, st->h);
	if (r->byminute.n && r->freq >= PM_HOURLY)
		for (k = 0; k < r->byminute.n; k++)
			pm_vin(&mins, pm_vat(&r->byminute, k));
	else
		pm_vin(&mins, st->mi);
	if (r->bysecond.n && r->freq >= PM_MINUTELY)
		for (k = 0; k < r->bysecond.n; k++)
			pm_vin(&secs, pm_vat(&r->bysecond, k));
	else
		pm_vin(&secs, st->s);
	if (t0 < to && t0 + (dur > 0 ? dur : 1) > from) {
		keep = xm(sizeof *keep);
		*keep = t0;
		v_add(out, keep);
	}
	count = 1;
	if (r->count && count >= r->count)
		done = 1;
	for (k = 0; !done; k++) {
		if (++steps > PM_MAXSTEPS) {
			lg(HIBR_LDBG, "pim: a recurrence walked %d periods and stopped", PM_MAXSTEPS);
			break;
		}
		nc = 0;
		if (r->freq <= PM_HOURLY) {
			long long step = r->freq == PM_HOURLY ? 3600 : r->freq == PM_MINUTELY ? 60 : 1;
			long long tot = (long long)(st->h * 3600 + st->mi * 60 + st->s) +
					(long long)k * r->interval * step;
			long long pd = d0 + (tot >= 0 ? tot / 86400 : (tot - 86399) / 86400);
			long long sod = tot - (pd - d0) * 86400;
			int ph = (int)(sod / 3600), pmi = (int)(sod / 60 % 60), ps = (int)(sod % 60);

			pstart = pm_zmk(&z, pd, ph, pmi, ps);
			if (pm_subok(r, pd, ph, pmi)) {
				for (b = 0; b < (r->freq == PM_HOURLY ? mins.n : 1); b++)
					for (c = 0; c < (r->freq >= PM_MINUTELY ? secs.n : 1); c++) {
						int mm = r->freq == PM_HOURLY ? pm_vat(&mins, b) : pmi;
						int ss = r->freq >= PM_MINUTELY ? pm_vat(&secs, c) : ps;

						if (nc == cap) {
							cap = cap ? cap * 2 : 64;
							cand = realloc(cand, cap * sizeof *cand);
						}
						cand[nc++] = pm_zmk(&z, pd, ph, mm, ss);
					}
				if (nc > 1)
					qsort(cand, nc, sizeof *cand, pm_llcmp);
			}
		} else {
			long long first, last;

			if (r->freq == PM_DAILY) {
				first = last = d0 + (long long)k * r->interval;
			} else if (r->freq == PM_WEEKLY) {
				long long ws = d0 - (pm_wday(d0) - r->wkst + 7) % 7;

				first = ws + (long long)k * r->interval * 7;
				last = first + 6;
			} else if (r->freq == PM_MONTHLY) {
				long long mi = (long long)(st->mo - 1) + (long long)k * r->interval;

				y = st->y + (int)(mi / 12);
				m = (int)(mi % 12) + 1;
				first = pm_days(y, m, 1);
				last = first + pm_mdays(y, m) - 1;
			} else {
				y = st->y + (int)k * r->interval;
				first = pm_days(y, 1, 1);
				last = pm_days(y + 1, 1, 1) - 1;
			}
			pstart = pm_zmk(&z, first, 0, 0, 0);
			ndays = (int)(last - first + 1);
			for (i = 0; i < ndays; i++) {
				day = first + i;
				if (!pm_dayok(r, day, st->y, st->mo, st->d, wd0))
					continue;
				pm_civil(day, &y, &m, &dd);
				for (a = 0; a < hrs.n; a++)
					for (b = 0; b < mins.n; b++)
						for (c = 0; c < secs.n; c++) {
							if (nc == cap) {
								cap = cap ? cap * 2 : 64;
								cand = realloc(cand, cap * sizeof *cand);
							}
							cand[nc++] = pm_zmk(&z, day,
									    st->date ? 0 : pm_vat(&hrs, a),
									    st->date ? 0 : pm_vat(&mins, b),
									    st->date ? 0 : pm_vat(&secs, c));
						}
			}
			if (nc > 1)
				qsort(cand, nc, sizeof *cand, pm_llcmp);
		}
		if (r->bysetpos.n && nc) {
			long long *sel = xm(nc * sizeof *sel);
			size_t ns = 0;

			for (a = 0; a < r->bysetpos.n; a++) {
				int p = pm_vat(&r->bysetpos, a);
				long idx = p > 0 ? p - 1 : (long)nc + p;

				if (idx >= 0 && idx < (long)nc)
					sel[ns++] = cand[idx];
			}
			if (ns > 1)
				qsort(sel, ns, sizeof *sel, pm_llcmp);
			memcpy(cand, sel, ns * sizeof *sel);
			nc = ns;
			free(sel);
		}
		for (a = 0; a < nc && !done; a++) {
			t = cand[a];
			if (t <= t0)
				continue;
			if (a && t == cand[a - 1])
				continue;
			if (r->hasuntil && t > r->until) {
				done = 1;
				break;
			}
			if (t >= to) {
				done = 1;
				break;
			}
			count++;
			if (t + (dur > 0 ? dur : 1) > from) {
				if (out->n >= PM_MAXINST) {
					lg(HIBR_LDBG, "pim: %d instances, and no more", PM_MAXINST);
					done = 1;
					break;
				}
				keep = xm(sizeof *keep);
				*keep = t;
				v_add(out, keep);
			}
			if (r->count && count >= r->count)
				done = 1;
		}
		if (pstart >= to || (r->hasuntil && pstart > r->until))
			done = 1;
	}
	free(cand);
	v_free(&hrs);
	v_free(&mins);
	v_free(&secs);
	if (old)
		pm_tzpop(old);
	return HIBR_OK;
}
