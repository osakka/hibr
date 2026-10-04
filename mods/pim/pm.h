#ifndef HIBR_PM_H
#define HIBR_PM_H

#include "hibr.h"
#include <stddef.h>

/* How many instances one expansion may produce before it stops. */
#ifndef PM_MAXINST
#define PM_MAXINST 20000
#endif

/* How many periods a recurrence is walked through before it gives up, so a
   rule that can never match cannot spin. */
#ifndef PM_MAXSTEPS
#define PM_MAXSTEPS 400000
#endif

/* How deep components may nest. */
#ifndef PM_DEPTH
#define PM_DEPTH 16
#endif

/* Content lines are folded at this many octets on output, as RFC 5545 and
   RFC 6350 ask. */
#ifndef PM_FOLD
#define PM_FOLD 75
#endif

/* One parameter of a property: NAME=value, the value unquoted. */
typedef struct pm_par pm_par;
struct pm_par {
	char *name, *val;
};

/* One content line: its group (vCard's item1.), name, parameters and raw
   value, escapes still in it. */
typedef struct pm_prop pm_prop;
struct pm_prop {
	char *group, *name, *val;
	vec pars;
};

/* A component: BEGIN:NAME ... END:NAME, its properties and the components
   inside it. */
typedef struct pm_comp pm_comp;
struct pm_comp {
	char *name;
	vec props, kids;
};

/* A moment as written in a calendar: a civil date, a time of day when it is
   not a whole day, and where it is -- UTC, a named zone, or floating. */
typedef struct pm_dt pm_dt;
struct pm_dt {
	int y, mo, d, h, mi, s;
	int date, utc;
	char *tz;
};

/* A parsed recurrence rule (RFC 5545 3.3.10). Each BY list is a vec of
   ints cast through intptr_t; BYDAY packs an ordinal and a weekday as
   ordinal * 8 + weekday, Monday being 0. */
typedef struct pm_rule pm_rule;
struct pm_rule {
	int freq, interval, count, wkst;
	int hasuntil;
	long long until;
	vec bymonth, byweekno, byyearday, bymonthday, byday, byhour, byminute,
		bysecond, bysetpos;
};

enum { PM_SECONDLY, PM_MINUTELY, PM_HOURLY, PM_DAILY, PM_WEEKLY, PM_MONTHLY, PM_YEARLY };

pm_comp *pm_parse(const char *p, size_t n);
void pm_free(pm_comp *c);
void pm_upper(char *p);
pm_comp *pm_new(const char *name);
pm_prop *pm_get(pm_comp *c, const char *name);
const char *pm_pget(pm_prop *p, const char *name);
void pm_untext(str *o, const char *p);
void pm_text(str *o, const char *p);
void pm_line(str *o, const char *line);
void pm_lineb(str *o, str *line);
int pm_slurp(const char *path, str *o);
int pm_input(sh *s, int ac, char **av, int i, str *o, int *next);

long long pm_days(int y, int m, int d);
void pm_civil(long long days, int *y, int *m, int *d);
int pm_wday(long long days);
int pm_mdays(int y, int m);
int pm_dtparse(const char *v, const char *tzid, int isdate, pm_dt *o);
void pm_dtfree(pm_dt *t);
long long pm_epoch(const pm_dt *t, pm_comp *cal);
long long pm_local(int y, int mo, int d, int h, int mi, int s, const char *tz, pm_comp *cal);
void pm_tolocal(long long t, const char *tz, int *y, int *mo, int *d, int *h, int *mi, int *s);
int pm_tzok(const char *tz);
int pm_dur(const char *v, long long *secs);
long long pm_propt(pm_prop *p, pm_comp *cal, int *isdate);
void pm_vtimezone(str *o, const char *tz, int y0, int y1);

int pm_ruleparse(const char *v, pm_rule *r, pm_comp *cal, const char *tz);
void pm_rulefree(pm_rule *r);
int pm_rrule(pm_rule *r, const pm_dt *start, pm_comp *cal, long long from, long long to,
	     long long dur, vec *out);

int pm_ics(sh *s, int ac, char **av);
int pm_vcf(sh *s, int ac, char **av);
void pm_set(sh *s, char **ks, int nk, const char *v);
void pm_setn(sh *s, char **ks, int nk, long long v);

#endif
