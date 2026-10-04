#include "pm.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

void pm_dtout(str *o, int y, int mo, int d, int h, int mi, int s);
int pm_llcmp(const void *a, const void *b);

/* Every event, task and journal entry in a document, in order, each with
   the calendar it sits in, for its VTIMEZONEs. */
void pm_items(pm_comp *c, pm_comp *cal, vec *items, vec *cals)
{
	size_t i;
	pm_comp *k;

	for (i = 0; i < c->kids.n; i++) {
		k = c->kids.p[i];
		if (!strcmp(k->name, "VEVENT") || !strcmp(k->name, "VTODO") ||
		    !strcmp(k->name, "VJOURNAL")) {
			v_add(items, k);
			v_add(cals, cal);
		} else if (!strcmp(k->name, "VCALENDAR")) {
			pm_items(k, k, items, cals);
		}
	}
}

/* A text property's value with its escapes taken out, into o; empty when
   the component has none. */
void pm_textof(pm_comp *c, const char *name, str *o)
{
	pm_prop *p = pm_get(c, name);

	o->n = 0;
	if (o->p)
		o->p[0] = 0;
	if (p)
		pm_untext(o, p->val);
}

/* An address from a CAL-ADDRESS value: mailto: taken off. */
const char *pm_addr(const char *v)
{
	return !strncasecmp(v, "mailto:", 7) ? v + 7 : v;
}

/* Where an item starts and ends, as seconds since the epoch, and whether it
   takes whole days: DTEND, else DURATION, else a day for a date and no time
   at all otherwise; a task's DUE stands in for its end. */
void pm_span(pm_comp *e, pm_comp *cal, long long *st, long long *en, int *allday)
{
	pm_prop *p;
	long long d;
	int isd;

	*st = pm_propt(pm_get(e, "DTSTART"), cal, allday);
	if ((p = pm_get(e, "DTEND")) || (p = pm_get(e, "DUE"))) {
		*en = pm_propt(p, cal, &isd);
		if (*st == -(1LL << 62))
			*st = *en;
	} else if ((p = pm_get(e, "DURATION")) && !pm_dur(p->val, &d)) {
		*en = *st + d;
	} else {
		*en = *st + (*allday ? 86400 : 0);
	}
}

/* The fields of one item into r[i]: what it is, its identity, times, words,
   people and reminders. */
void pm_evset(sh *s, size_t idx, pm_comp *e, pm_comp *cal)
{
	str k, v, sub;
	char *ks[4];
	const char *text[][2] = { { "SUMMARY", "summary" }, { "LOCATION", "location" },
				  { "DESCRIPTION", "description" }, { "STATUS", "status" },
				  { "TRANSP", "transp" }, { "CLASS", "class" }, { "URL", "url" },
				  { "CATEGORIES", "categories" }, { "PRIORITY", "priority" },
				  { "PERCENT-COMPLETE", "percent" }, { "UID", "uid" },
				  { "SEQUENCE", "seq" }, { "RRULE", "rrule" }, { "COLOR", "color" },
				  { 0, 0 } };
	long long st, en, rid;
	int allday, isd, n, j;
	size_t i, a = 0, al = 0;
	pm_prop *p;
	pm_comp *c;

	s_init(&k);
	s_init(&v);
	s_init(&sub);
	s_num(&k, (long)idx);
	ks[0] = k.p;
	ks[1] = "kind";
	pm_set(s, ks, 2, e->name);
	for (n = 0; text[n][0]; n++) {
		pm_textof(e, text[n][0], &v);
		if (!v.n)
			continue;
		ks[1] = (char *)text[n][1];
		pm_set(s, ks, 2, v.p);
	}
	pm_span(e, cal, &st, &en, &allday);
	ks[1] = "start";
	pm_setn(s, ks, 2, st == -(1LL << 62) ? 0 : st);
	ks[1] = "end";
	pm_setn(s, ks, 2, en == -(1LL << 62) ? 0 : en);
	ks[1] = "allday";
	pm_setn(s, ks, 2, allday);
	if ((p = pm_get(e, "DTSTART")) && pm_pget(p, "TZID")) {
		ks[1] = "tzid";
		pm_set(s, ks, 2, pm_pget(p, "TZID"));
	}
	if ((p = pm_get(e, "RECURRENCE-ID"))) {
		rid = pm_propt(p, cal, &isd);
		ks[1] = "recurid";
		pm_setn(s, ks, 2, rid);
	}
	if ((p = pm_get(e, "COMPLETED"))) {
		ks[1] = "completed";
		pm_setn(s, ks, 2, pm_propt(p, cal, &isd));
	}
	for (n = 0; n < 2; n++) {
		const char *pn = n ? "RDATE" : "EXDATE", *key = n ? "rdate" : "exdate";

		v.n = 0;
		for (i = 0; i < e->props.n; i++) {
			char *dup, *tok, *save = 0;

			p = e->props.p[i];
			if (strcmp(p->name, pn))
				continue;
			dup = xs(p->val);
			for (tok = strtok_r(dup, ",", &save); tok; tok = strtok_r(0, ",", &save)) {
				pm_dt t;
				const char *vt = pm_pget(p, "VALUE");

				if (pm_dtparse(tok, pm_pget(p, "TZID"), vt && !strcasecmp(vt, "DATE"), &t))
					continue;
				if (v.n)
					s_ch(&v, ' ');
				s_num(&v, (long)pm_epoch(&t, cal));
				pm_dtfree(&t);
			}
			free(dup);
		}
		if (v.n) {
			ks[1] = (char *)key;
			pm_set(s, ks, 2, v.p);
		}
	}
	if ((p = pm_get(e, "ORGANIZER"))) {
		ks[1] = "org";
		pm_set(s, ks, 2, pm_addr(p->val));
		if (pm_pget(p, "CN")) {
			ks[1] = "orgname";
			pm_set(s, ks, 2, pm_pget(p, "CN"));
		}
	}
	for (i = 0; i < e->props.n; i++) {
		p = e->props.p[i];
		if (strcmp(p->name, "ATTENDEE"))
			continue;
		sub.n = 0;
		s_num(&sub, (long)a++);
		ks[1] = "att";
		ks[2] = sub.p;
		ks[3] = "addr";
		pm_set(s, ks, 4, pm_addr(p->val));
		ks[3] = "name";
		pm_set(s, ks, 4, pm_pget(p, "CN") ? pm_pget(p, "CN") : "");
		ks[3] = "partstat";
		pm_set(s, ks, 4, pm_pget(p, "PARTSTAT") ? pm_pget(p, "PARTSTAT") : "NEEDS-ACTION");
		ks[3] = "role";
		pm_set(s, ks, 4, pm_pget(p, "ROLE") ? pm_pget(p, "ROLE") : "REQ-PARTICIPANT");
		ks[3] = "rsvp";
		pm_set(s, ks, 4, pm_pget(p, "RSVP") && !strcasecmp(pm_pget(p, "RSVP"), "TRUE") ? "1" : "0");
	}
	for (i = 0; i < e->kids.n; i++) {
		long long trig = 0;

		c = e->kids.p[i];
		if (strcmp(c->name, "VALARM"))
			continue;
		p = pm_get(c, "TRIGGER");
		if (!p)
			continue;
		if (pm_pget(p, "VALUE") && !strcasecmp(pm_pget(p, "VALUE"), "DATE-TIME"))
			trig = pm_propt(p, cal, &isd) - st;
		else if (!pm_dur(p->val, &trig) && pm_pget(p, "RELATED") &&
			 !strcasecmp(pm_pget(p, "RELATED"), "END"))
			trig += en - st;
		sub.n = 0;
		s_num(&sub, (long)al++);
		ks[1] = "alarm";
		ks[2] = sub.p;
		ks[3] = "trigger";
		pm_setn(s, ks, 4, trig);
		pm_textof(c, "ACTION", &v);
		ks[3] = "action";
		pm_set(s, ks, 4, v.n ? v.p : "DISPLAY");
		pm_textof(c, "DESCRIPTION", &v);
		ks[3] = "desc";
		pm_set(s, ks, 4, v.p ? v.p : "");
	}
	(void)j;
	s_free(&k);
	s_free(&v);
	s_free(&sub);
}

/* Set PIM_METHOD to a document's METHOD -- REQUEST, REPLY, CANCEL for an
   invitation, empty for plain calendar data. */
void pm_method(sh *s, pm_comp *doc)
{
	size_t i;
	pm_prop *p = 0;

	for (i = 0; i < doc->kids.n && !p; i++)
		if (!strcmp(((pm_comp *)doc->kids.p[i])->name, "VCALENDAR"))
			p = pm_get(doc->kids.p[i], "METHOD");
	hibr_set(s, "PIM_METHOD", p ? p->val : "", 0);
}

/* pim ics events [-t TEXT | FILE]: every event and task, a map each. */
int pm_events(sh *s, int ac, char **av)
{
	str in;
	pm_comp *doc;
	vec items = { 0, 0, 0 }, cals = { 0, 0, 0 };
	size_t i;
	int next;

	s_init(&in);
	if (pm_input(s, ac, av, 3, &in, &next) != HIBR_OK) {
		s_free(&in);
		return HIBR_FAIL;
	}
	doc = pm_parse(in.p ? in.p : "", in.n);
	pm_items(doc, 0, &items, &cals);
	pm_method(s, doc);
	for (i = 0; i < items.n; i++) {
		if (s->bind) {
			pm_evset(s, i, items.p[i], cals.p[i]);
		} else {
			str v;
			long long st, en;
			int ad;

			s_init(&v);
			pm_textof(items.p[i], "SUMMARY", &v);
			pm_span(items.p[i], cals.p[i], &st, &en, &ad);
			printf("%s\t%lld\t%lld\t%d\t%s\n", ((pm_comp *)items.p[i])->name, st, en, ad,
			       v.p ? v.p : "");
			s_free(&v);
		}
	}
	if (s->bind && !items.n)
		hibr_retn(s, 0, 0);
	lg(HIBR_LDBG, "pim: %zu items", items.n);
	v_free(&items);
	v_free(&cals);
	pm_free(doc);
	s_free(&in);
	return HIBR_OK;
}

/* One instance found by an expansion. */
typedef struct pm_inst pm_inst;
struct pm_inst {
	long long st, en, rid;
	size_t item;
	int allday, over, rec;
};

/* Sort instances by start, then by which item. */
int pm_instcmp(const void *a, const void *b)
{
	const pm_inst *x = *(pm_inst *const *)a, *y = *(pm_inst *const *)b;

	if (x->st != y->st)
		return x->st < y->st ? -1 : 1;
	return x->item < y->item ? -1 : x->item > y->item;
}

/* The moments a list property (EXDATE, RDATE) names, every one of them. */
void pm_moments(pm_comp *e, pm_comp *cal, const char *name, vec *out)
{
	size_t i;
	pm_prop *p;
	char *dup, *tok, *save;

	for (i = 0; i < e->props.n; i++) {
		p = e->props.p[i];
		if (strcmp(p->name, name))
			continue;
		dup = xs(p->val);
		save = 0;
		for (tok = strtok_r(dup, ",", &save); tok; tok = strtok_r(0, ",", &save)) {
			pm_dt t;
			const char *vt = pm_pget(p, "VALUE");
			long long *m;

			if (pm_dtparse(tok, pm_pget(p, "TZID"), vt && !strcasecmp(vt, "DATE"), &t))
				continue;
			m = xm(sizeof *m);
			*m = pm_epoch(&t, cal);
			v_add(out, m);
			pm_dtfree(&t);
		}
		free(dup);
	}
}

/* Whether a vec of moments holds one. */
int pm_hasmoment(vec *v, long long t)
{
	size_t i;

	for (i = 0; i < v->n; i++)
		if (*(long long *)v->p[i] == t)
			return 1;
	return 0;
}

/* Free a vec of heap moments. */
void pm_momfree(vec *v)
{
	size_t i;

	for (i = 0; i < v->n; i++)
		free(v->p[i]);
	v_free(v);
}

/* pim ics expand [-t TEXT | FILE] FROM TO: every instance overlapping the
   window, in order -- a recurring item's occurrences from its RRULE and
   RDATEs, less its EXDATEs, each replaced by the item whose RECURRENCE-ID
   names it -- as r[i]["start"], ["end"], ["allday"], ["recurid"], ["item"]
   (its number in pim ics events), ["uid"], ["summary"], ["location"],
   ["status"], ["override"] and ["recurring"]. */
int pm_expand(sh *s, int ac, char **av)
{
	str in, k, v;
	pm_comp *doc, *e;
	vec items = { 0, 0, 0 }, cals = { 0, 0, 0 }, insts = { 0, 0, 0 };
	size_t i, j, n;
	int next;
	long long from, to, st, en, dur;
	char *ks[2];

	s_init(&in);
	if (pm_input(s, ac, av, 3, &in, &next) != HIBR_OK) {
		s_free(&in);
		return HIBR_FAIL;
	}
	if (next + 1 >= ac) {
		lg(HIBR_LERR, "usage: pim ics expand [-t text | file] from to");
		s_free(&in);
		return 2;
	}
	from = strtoll(av[next], 0, 10);
	to = strtoll(av[next + 1], 0, 10);
	doc = pm_parse(in.p ? in.p : "", in.n);
	pm_items(doc, 0, &items, &cals);
	for (i = 0; i < items.n; i++) {
		pm_prop *rr, *rid;
		pm_inst *x;
		int allday;
		vec starts = { 0, 0, 0 }, ex = { 0, 0, 0 }, rd = { 0, 0, 0 };

		e = items.p[i];
		if (pm_get(e, "RECURRENCE-ID"))
			continue;
		pm_span(e, cals.p[i], &st, &en, &allday);
		if (st == -(1LL << 62))
			continue;
		dur = en - st;
		rr = pm_get(e, "RRULE");
		pm_moments(e, cals.p[i], "EXDATE", &ex);
		pm_moments(e, cals.p[i], "RDATE", &rd);
		if (rr) {
			pm_rule r;
			pm_dt t;
			pm_prop *ds = pm_get(e, "DTSTART");
			const char *vt = pm_pget(ds, "VALUE");

			if (!pm_dtparse(ds->val, pm_pget(ds, "TZID"), vt && !strcasecmp(vt, "DATE"), &t) &&
			    !pm_ruleparse(rr->val, &r, cals.p[i], pm_pget(ds, "TZID"))) {
				pm_rrule(&r, &t, cals.p[i], from, to, dur, &starts);
				pm_rulefree(&r);
			}
			pm_dtfree(&t);
		} else if (st < to && st + (dur > 0 ? dur : 1) > from) {
			long long *m = xm(sizeof *m);

			*m = st;
			v_add(&starts, m);
		}
		for (j = 0; j < rd.n; j++) {
			long long t = *(long long *)rd.p[j], *m;

			if (t < to && t + (dur > 0 ? dur : 1) > from && !pm_hasmoment(&starts, t)) {
				m = xm(sizeof *m);
				*m = t;
				v_add(&starts, m);
			}
		}
		for (j = 0; j < starts.n; j++) {
			long long t = *(long long *)starts.p[j];
			size_t o;
			int replaced = 0;

			if (pm_hasmoment(&ex, t))
				continue;
			for (o = 0; o < items.n && !replaced; o++) {
				pm_prop *u1 = pm_get(items.p[o], "UID"), *u0 = pm_get(e, "UID");

				rid = pm_get(items.p[o], "RECURRENCE-ID");
				if (!rid || !u1 || !u0 || strcmp(u1->val, u0->val))
					continue;
				if (pm_propt(rid, cals.p[o], 0) == t)
					replaced = 1;
			}
			if (replaced)
				continue;
			x = xm(sizeof *x);
			memset(x, 0, sizeof *x);
			x->st = t;
			x->en = t + dur;
			x->rid = t;
			x->item = i;
			x->allday = allday;
			x->rec = rr || rd.n;
			v_add(&insts, x);
		}
		pm_momfree(&starts);
		pm_momfree(&ex);
		pm_momfree(&rd);
	}
	for (i = 0; i < items.n; i++) {
		pm_prop *rid;
		pm_inst *x;
		int allday;

		e = items.p[i];
		rid = pm_get(e, "RECURRENCE-ID");
		if (!rid)
			continue;
		pm_span(e, cals.p[i], &st, &en, &allday);
		if (st == -(1LL << 62) || !(st < to && en > from - (en == st)))
			continue;
		x = xm(sizeof *x);
		memset(x, 0, sizeof *x);
		x->st = st;
		x->en = en;
		x->rid = pm_propt(rid, cals.p[i], 0);
		x->item = i;
		x->allday = allday;
		x->over = 1;
		x->rec = 1;
		v_add(&insts, x);
	}
	if (insts.n > 1)
		qsort(insts.p, insts.n, sizeof(void *), pm_instcmp);
	s_init(&k);
	s_init(&v);
	for (n = 0; n < insts.n; n++) {
		pm_inst *x = insts.p[n];
		const char *f[][2] = { { "UID", "uid" }, { "SUMMARY", "summary" },
				       { "LOCATION", "location" }, { "STATUS", "status" }, { 0, 0 } };
		int q;

		e = items.p[x->item];
		if (!s->bind) {
			pm_textof(e, "SUMMARY", &v);
			printf("%lld\t%lld\t%d\t%s\n", x->st, x->en, x->allday, v.p ? v.p : "");
			free(x);
			continue;
		}
		k.n = 0;
		s_num(&k, (long)n);
		ks[0] = k.p;
		ks[1] = "start";
		pm_setn(s, ks, 2, x->st);
		ks[1] = "end";
		pm_setn(s, ks, 2, x->en);
		ks[1] = "allday";
		pm_setn(s, ks, 2, x->allday);
		ks[1] = "recurid";
		pm_setn(s, ks, 2, x->rid);
		ks[1] = "item";
		pm_setn(s, ks, 2, (long long)x->item);
		ks[1] = "override";
		pm_setn(s, ks, 2, x->over);
		ks[1] = "recurring";
		pm_setn(s, ks, 2, x->rec);
		for (q = 0; f[q][0]; q++) {
			pm_textof(e, f[q][0], &v);
			ks[1] = (char *)f[q][1];
			pm_set(s, ks, 2, v.p ? v.p : "");
		}
		free(x);
	}
	if (s->bind && !insts.n)
		hibr_retn(s, 0, 0);
	lg(HIBR_LDBG, "pim: %zu instances between %lld and %lld", insts.n, from, to);
	s_free(&k);
	s_free(&v);
	v_free(&insts);
	v_free(&items);
	v_free(&cals);
	pm_free(doc);
	s_free(&in);
	return HIBR_OK;
}

/* A moment written for a property: a date, a local time in a zone, or UTC. */
void pm_when(str *l, const char *name, long long t, int allday, const char *tz)
{
	int y, mo, d, h, mi, s;
	str b;

	s_cat(l, name);
	if (allday) {
		pm_tolocal(t, 0, &y, &mo, &d, &h, &mi, &s);
		s_cat(l, ";VALUE=DATE:");
		s_init(&b);
		pm_dtout(&b, y, mo, d, 0, 0, 0);
		s_add(l, b.p, 8);
		s_free(&b);
	} else if (tz && *tz && strcmp(tz, "UTC")) {
		pm_tolocal(t, tz, &y, &mo, &d, &h, &mi, &s);
		s_cat(l, ";TZID=");
		s_cat(l, tz);
		s_ch(l, ':');
		pm_dtout(l, y, mo, d, h, mi, s);
	} else {
		pm_tolocal(t, "UTC", &y, &mo, &d, &h, &mi, &s);
		s_ch(l, ':');
		pm_dtout(l, y, mo, d, h, mi, s);
		s_ch(l, 'Z');
	}
}

/* A text property written out, escaped and folded. */
void pm_tline(str *o, const char *name, const char *v)
{
	str l;

	if (!v || !*v)
		return;
	s_init(&l);
	s_cat(&l, name);
	s_ch(&l, ':');
	pm_text(&l, v);
	pm_lineb(o, &l);
	s_free(&l);
}

/* A parameter value, quoted when it holds what would end it. */
void pm_parval(str *l, const char *v)
{
	if (strpbrk(v, ":;,")) {
		s_ch(l, '"');
		for (; *v; v++)
			if (*v != '"')
				s_ch(l, *v);
		s_ch(l, '"');
	} else {
		s_cat(l, v);
	}
}

/* A person as ORGANIZER or ATTENDEE, from addr[;name[;partstat[;role]]]. */
void pm_person(str *o, const char *prop, const char *spec, int attendee)
{
	char *dup = xs(spec), *f[4] = { 0, 0, 0, 0 }, *p = dup;
	int n = 0;
	str l;

	while (n < 4) {
		f[n++] = p;
		p = strchr(p, ';');
		if (!p)
			break;
		*p++ = 0;
	}
	s_init(&l);
	s_cat(&l, prop);
	if (f[1] && *f[1]) {
		s_cat(&l, ";CN=");
		pm_parval(&l, f[1]);
	}
	if (attendee) {
		s_cat(&l, ";ROLE=");
		s_cat(&l, f[3] && *f[3] ? f[3] : "REQ-PARTICIPANT");
		s_cat(&l, ";PARTSTAT=");
		s_cat(&l, f[2] && *f[2] ? f[2] : "NEEDS-ACTION");
		if (!f[2] || !*f[2] || !strcasecmp(f[2], "NEEDS-ACTION"))
			s_cat(&l, ";RSVP=TRUE");
	}
	s_cat(&l, ":mailto:");
	s_cat(&l, f[0]);
	pm_lineb(o, &l);
	s_free(&l);
	free(dup);
}

/* Write a text to a file, or to standard output for -. */
int pm_write(const char *out, str *o)
{
	FILE *f;

	if (!strcmp(out, "-")) {
		fwrite(o->p ? o->p : "", 1, o->n, stdout);
		return HIBR_OK;
	}
	f = fopen(out, "w");
	if (!f) {
		lg(HIBR_LERR, "pim: cannot write %s", out);
		return HIBR_FAIL;
	}
	fwrite(o->p ? o->p : "", 1, o->n, f);
	fclose(f);
	return HIBR_OK;
}

/* The DTSTAMP every item carries: now, in UTC. */
void pm_stamp(str *o)
{
	str l;

	s_init(&l);
	pm_when(&l, "DTSTAMP", (long long)time(0), 0, 0);
	pm_lineb(o, &l);
	s_free(&l);
}

/* pim ics build OUT [options]: write a calendar holding one event (or, with
   -K VTODO, a task). -u uid, -s summary, -b start, -e end (seconds since
   the epoch), -a for whole days, -z zone (with a VTIMEZONE written for it),
   -l location, -d description, -r RRULE, -x exdate (repeatable), -o
   organizer addr[;name], -A attendee addr[;name[;partstat[;role]]]
   (repeatable), -q sequence, -S status, -R recurrence-id, -L minutes of a
   reminder before the start (repeatable), -m METHOD for an invitation, -c
   colour. */
int pm_build(sh *s, int ac, char **av)
{
	const char *out, *uid = 0, *sum = 0, *loc = 0, *desc = 0, *rule = 0, *tz = 0, *org = 0;
	const char *seq = 0, *status = 0, *method = 0, *kind = "VEVENT", *color = 0;
	long long st = 0, en = 0, rid = 0;
	int i, allday = 0, hasst = 0, hasen = 0, hasrid = 0, y, mo, d, h, mi, sec;
	vec atts = { 0, 0, 0 }, exd = { 0, 0, 0 }, alarms = { 0, 0, 0 };
	str o, l;
	size_t j;

	(void)s;
	if (ac < 4) {
		lg(HIBR_LERR, "usage: pim ics build out -u uid -s summary -b start [-e end] [-a] "
			      "[-z zone] [-l location] [-d description] [-r rrule] [-x exdate]... "
			      "[-o organizer] [-A attendee]... [-q seq] [-S status] [-R recurid] "
			      "[-L minutes]... [-m method] [-K VEVENT|VTODO] [-c colour]");
		return 2;
	}
	out = av[3];
	for (i = 4; i < ac; i++) {
		const char *a = av[i], *v = i + 1 < ac ? av[i + 1] : 0;

		if (!strcmp(a, "-a")) {
			allday = 1;
			continue;
		}
		if (!v || a[0] != '-' || !a[1] || a[2]) {
			lg(HIBR_LERR, "pim: ics build: %s is not an option, or wants a value", a);
			v_free(&atts);
			v_free(&exd);
			v_free(&alarms);
			return 2;
		}
		i++;
		switch (a[1]) {
		case 'u': uid = v; break;
		case 's': sum = v; break;
		case 'b': st = strtoll(v, 0, 10); hasst = 1; break;
		case 'e': en = strtoll(v, 0, 10); hasen = 1; break;
		case 'z': tz = v; break;
		case 'l': loc = v; break;
		case 'd': desc = v; break;
		case 'r': rule = v; break;
		case 'x': v_add(&exd, (void *)v); break;
		case 'o': org = v; break;
		case 'A': v_add(&atts, (void *)v); break;
		case 'q': seq = v; break;
		case 'S': status = v; break;
		case 'R': rid = strtoll(v, 0, 10); hasrid = 1; break;
		case 'L': v_add(&alarms, (void *)v); break;
		case 'm': method = v; break;
		case 'K': kind = v; break;
		case 'c': color = v; break;
		default:
			lg(HIBR_LERR, "pim: ics build: %s is not an option", a);
			v_free(&atts);
			v_free(&exd);
			v_free(&alarms);
			return 2;
		}
	}
	if (!uid || (!hasst && strcasecmp(kind, "VTODO"))) {
		lg(HIBR_LERR, "pim: ics build: an item needs -u uid and -b start");
		v_free(&atts);
		v_free(&exd);
		v_free(&alarms);
		return 2;
	}
	if (tz && !pm_tzok(tz) && strcmp(tz, "UTC")) {
		lg(HIBR_LERR, "pim: ics build: %s is not a zone this system knows", tz);
		v_free(&atts);
		v_free(&exd);
		v_free(&alarms);
		return 2;
	}
	s_init(&o);
	s_init(&l);
	pm_line(&o, "BEGIN:VCALENDAR");
	pm_line(&o, "VERSION:2.0");
	pm_line(&o, "PRODID:-//hibr//pim//EN");
	pm_line(&o, "CALSCALE:GREGORIAN");
	if (method) {
		l.n = 0;
		s_cat(&l, "METHOD:");
		s_cat(&l, method);
		pm_lineb(&o, &l);
	}
	if (tz && strcmp(tz, "UTC") && !allday) {
		pm_tolocal(st, tz, &y, &mo, &d, &h, &mi, &sec);
		pm_vtimezone(&o, tz, y - 1, y + 10);
	}
	l.n = 0;
	s_cat(&l, "BEGIN:");
	s_cat(&l, kind);
	pm_lineb(&o, &l);
	l.n = 0;
	s_cat(&l, "UID:");
	s_cat(&l, uid);
	pm_lineb(&o, &l);
	pm_stamp(&o);
	if (hasst) {
		l.n = 0;
		pm_when(&l, "DTSTART", st, allday, tz);
		pm_lineb(&o, &l);
	}
	if (hasen) {
		l.n = 0;
		pm_when(&l, strcasecmp(kind, "VTODO") ? "DTEND" : "DUE", en, allday, tz);
		pm_lineb(&o, &l);
	}
	if (hasrid) {
		l.n = 0;
		pm_when(&l, "RECURRENCE-ID", rid, allday, tz);
		pm_lineb(&o, &l);
	}
	pm_tline(&o, "SUMMARY", sum);
	pm_tline(&o, "LOCATION", loc);
	pm_tline(&o, "DESCRIPTION", desc);
	if (rule) {
		l.n = 0;
		s_cat(&l, "RRULE:");
		s_cat(&l, rule);
		pm_lineb(&o, &l);
	}
	for (j = 0; j < exd.n; j++) {
		l.n = 0;
		pm_when(&l, "EXDATE", strtoll(exd.p[j], 0, 10), allday, tz);
		pm_lineb(&o, &l);
	}
	if (status) {
		l.n = 0;
		s_cat(&l, "STATUS:");
		s_cat(&l, status);
		pm_lineb(&o, &l);
	}
	if (seq) {
		l.n = 0;
		s_cat(&l, "SEQUENCE:");
		s_cat(&l, seq);
		pm_lineb(&o, &l);
	}
	if (color)
		pm_tline(&o, "COLOR", color);
	if (org)
		pm_person(&o, "ORGANIZER", org, 0);
	for (j = 0; j < atts.n; j++)
		pm_person(&o, "ATTENDEE", atts.p[j], 1);
	for (j = 0; j < alarms.n; j++) {
		long m = strtol(alarms.p[j], 0, 10);

		pm_line(&o, "BEGIN:VALARM");
		pm_line(&o, "ACTION:DISPLAY");
		pm_tline(&o, "DESCRIPTION", sum && *sum ? sum : "Reminder");
		l.n = 0;
		s_cat(&l, "TRIGGER:");
		s_cat(&l, m > 0 ? "-PT" : "PT");
		s_num(&l, m > 0 ? m : -m);
		s_ch(&l, 'M');
		pm_lineb(&o, &l);
		pm_line(&o, "END:VALARM");
	}
	l.n = 0;
	s_cat(&l, "END:");
	s_cat(&l, kind);
	pm_lineb(&o, &l);
	pm_line(&o, "END:VCALENDAR");
	i = pm_write(out, &o);
	s_free(&o);
	s_free(&l);
	v_free(&atts);
	v_free(&exd);
	v_free(&alarms);
	return i;
}

/* A property written back as it was read: its group, its parameters --
   quoted where they need it -- and its raw value. */
void pm_propout(str *o, pm_prop *p)
{
	str l;
	size_t i;

	s_init(&l);
	if (p->group) {
		s_cat(&l, p->group);
		s_ch(&l, '.');
	}
	s_cat(&l, p->name);
	for (i = 0; i < p->pars.n; i++) {
		pm_par *a = p->pars.p[i];

		s_ch(&l, ';');
		s_cat(&l, a->name);
		s_ch(&l, '=');
		pm_parval(&l, a->val);
	}
	s_ch(&l, ':');
	s_cat(&l, p->val);
	pm_lineb(o, &l);
	s_free(&l);
}

/* A component written back whole. */
void pm_compout(str *o, pm_comp *c)
{
	size_t i;
	str l;

	s_init(&l);
	s_cat(&l, "BEGIN:");
	s_cat(&l, c->name);
	pm_lineb(o, &l);
	for (i = 0; i < c->props.n; i++)
		pm_propout(o, c->props.p[i]);
	for (i = 0; i < c->kids.n; i++)
		pm_compout(o, c->kids.p[i]);
	l.n = 0;
	s_cat(&l, "END:");
	s_cat(&l, c->name);
	pm_lineb(o, &l);
	s_free(&l);
}

/* pim ics reply IN OUT ADDRESS PARTSTAT [-r RECURID]: an iTIP REPLY (RFC
   5546) to an invitation -- its UID, SEQUENCE, times, summary, organizer
   and recurrence, and only this attendee, answering ACCEPTED, TENTATIVE or
   DECLINED -- with the invitation's own VTIMEZONEs. */
int pm_reply(sh *s, int ac, char **av)
{
	str in, o, l;
	pm_comp *doc, *e = 0, *cal = 0;
	vec items = { 0, 0, 0 }, cals = { 0, 0, 0 };
	const char *addr, *ps, *keep[] = { "UID", "SEQUENCE", "DTSTART", "DTEND", "DURATION",
					   "SUMMARY", "ORGANIZER", "RECURRENCE-ID", "LOCATION", 0 };
	size_t i, j;
	int found = 0, rc;
	long long want = 0;
	int haswant = 0;

	(void)s;
	if (ac < 7) {
		lg(HIBR_LERR, "usage: pim ics reply in out address ACCEPTED|TENTATIVE|DECLINED [-r recurid]");
		return 2;
	}
	addr = av[5];
	ps = av[6];
	if (ac > 8 && !strcmp(av[7], "-r")) {
		want = strtoll(av[8], 0, 10);
		haswant = 1;
	}
	s_init(&in);
	if (pm_slurp(av[3], &in) != HIBR_OK) {
		s_free(&in);
		return HIBR_FAIL;
	}
	doc = pm_parse(in.p ? in.p : "", in.n);
	pm_items(doc, 0, &items, &cals);
	for (i = 0; i < items.n && !e; i++) {
		pm_prop *r = pm_get(items.p[i], "RECURRENCE-ID");

		if (strcmp(((pm_comp *)items.p[i])->name, "VEVENT"))
			continue;
		if (haswant ? (r && pm_propt(r, cals.p[i], 0) == want) : !r) {
			e = items.p[i];
			cal = cals.p[i];
		}
	}
	if (!e && items.n) {
		e = items.p[0];
		cal = cals.p[0];
	}
	if (!e) {
		lg(HIBR_LERR, "pim: %s holds no event to answer", av[3]);
		v_free(&items);
		v_free(&cals);
		pm_free(doc);
		s_free(&in);
		return HIBR_FAIL;
	}
	s_init(&o);
	s_init(&l);
	pm_line(&o, "BEGIN:VCALENDAR");
	pm_line(&o, "VERSION:2.0");
	pm_line(&o, "PRODID:-//hibr//pim//EN");
	pm_line(&o, "METHOD:REPLY");
	for (i = 0; cal && i < cal->kids.n; i++)
		if (!strcmp(((pm_comp *)cal->kids.p[i])->name, "VTIMEZONE"))
			pm_compout(&o, cal->kids.p[i]);
	pm_line(&o, "BEGIN:VEVENT");
	pm_stamp(&o);
	for (j = 0; keep[j]; j++) {
		pm_prop *p = pm_get(e, keep[j]);

		if (p)
			pm_propout(&o, p);
	}
	for (i = 0; i < e->props.n; i++) {
		pm_prop *p = e->props.p[i];
		size_t a;

		if (strcmp(p->name, "ATTENDEE") || strcasecmp(pm_addr(p->val), addr))
			continue;
		found = 1;
		l.n = 0;
		s_cat(&l, "ATTENDEE");
		for (a = 0; a < p->pars.n; a++) {
			pm_par *x = p->pars.p[a];

			if (!strcmp(x->name, "PARTSTAT") || !strcmp(x->name, "RSVP"))
				continue;
			s_ch(&l, ';');
			s_cat(&l, x->name);
			s_ch(&l, '=');
			pm_parval(&l, x->val);
		}
		s_cat(&l, ";PARTSTAT=");
		s_cat(&l, ps);
		s_cat(&l, ":mailto:");
		s_cat(&l, addr);
		pm_lineb(&o, &l);
		break;
	}
	if (!found) {
		l.n = 0;
		s_cat(&l, "ATTENDEE;PARTSTAT=");
		s_cat(&l, ps);
		s_cat(&l, ":mailto:");
		s_cat(&l, addr);
		pm_lineb(&o, &l);
	}
	pm_line(&o, "END:VEVENT");
	pm_line(&o, "END:VCALENDAR");
	rc = pm_write(av[4], &o);
	s_free(&o);
	s_free(&l);
	v_free(&items);
	v_free(&cals);
	pm_free(doc);
	s_free(&in);
	return rc;
}

/* pim ics: iCalendar -- events, expand, build, reply, vtimezone. */
int pm_ics(sh *s, int ac, char **av)
{
	const char *sub = ac > 2 ? av[2] : "";

	if (!strcmp(sub, "events"))
		return pm_events(s, ac, av);
	if (!strcmp(sub, "expand"))
		return pm_expand(s, ac, av);
	if (!strcmp(sub, "build"))
		return pm_build(s, ac, av);
	if (!strcmp(sub, "reply"))
		return pm_reply(s, ac, av);
	if (!strcmp(sub, "vtimezone") && ac > 3) {
		str o;
		int y0 = ac > 4 ? atoi(av[4]) : 2020, y1 = ac > 5 ? atoi(av[5]) : y0 + 10;

		if (!pm_tzok(av[3])) {
			lg(HIBR_LERR, "pim: %s is not a zone this system knows", av[3]);
			return HIBR_FAIL;
		}
		s_init(&o);
		pm_vtimezone(&o, av[3], y0, y1);
		fwrite(o.p ? o.p : "", 1, o.n, stdout);
		s_free(&o);
		return HIBR_OK;
	}
	lg(HIBR_LERR, "usage: pim ics events|expand|build|reply|vtimezone ...");
	return 2;
}
