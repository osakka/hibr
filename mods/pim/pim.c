#include "pm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* pim when epoch Y M D [h m s] [-z ZONE] -- a local time as seconds since
   the epoch; pim when civil T [-z ZONE] -- seconds as "Y M D h m s wday",
   Monday 0. In this process's zone unless one is named; a calendar draws
   days without forking date for each. */
int pm_cwhen(sh *s, int ac, char **av)
{
	const char *zone = 0;
	int i, n = 0, v[6] = { 0, 1, 1, 0, 0, 0 }, y, mo, d, h, mi, se;
	str o;

	for (i = 3; i < ac; i++) {
		if (!strcmp(av[i], "-z") && i + 1 < ac) {
			zone = av[++i];
			continue;
		}
		if (n < 6)
			v[n++] = atoi(av[i]);
	}
	if (zone && !pm_tzok(zone) && strcmp(zone, "UTC")) {
		lg(HIBR_LERR, "pim: when: %s is not a zone this system knows", zone);
		return 2;
	}
	s_init(&o);
	if (ac > 2 && !strcmp(av[2], "epoch") && n >= 3) {
		s_num(&o, (long)pm_local(v[0], v[1], v[2], v[3], v[4], v[5], zone, 0));
	} else if (ac > 3 && !strcmp(av[2], "civil")) {
		long long t = strtoll(av[3], 0, 10);

		pm_tolocal(t, zone, &y, &mo, &d, &h, &mi, &se);
		s_num(&o, y);
		s_ch(&o, ' ');
		s_num(&o, mo);
		s_ch(&o, ' ');
		s_num(&o, d);
		s_ch(&o, ' ');
		s_num(&o, h);
		s_ch(&o, ' ');
		s_num(&o, mi);
		s_ch(&o, ' ');
		s_num(&o, se);
		s_ch(&o, ' ');
		s_num(&o, pm_wday(pm_days(y, mo, d)));
	} else {
		lg(HIBR_LERR, "usage: pim when epoch Y M D [h m s] [-z zone] | pim when civil T [-z zone]");
		s_free(&o);
		return 2;
	}
	if (s->bind)
		hibr_ret(s, o.p);
	else
		printf("%s\n", o.p);
	s_free(&o);
	return HIBR_OK;
}

/* pim: calendars and contacts as their files are written -- iCalendar and
   vCard read, expanded and made. */
int m_pim(sh *s, int ac, char **av)
{
	const char *sub = ac > 1 ? av[1] : "";

	if (!strcmp(sub, "ics"))
		return pm_ics(s, ac, av);
	if (!strcmp(sub, "vcf"))
		return pm_vcf(s, ac, av);
	if (!strcmp(sub, "when"))
		return pm_cwhen(s, ac, av);
	lg(HIBR_LERR, "usage: pim ics events|expand|build|reply|store|vtimezone ... | pim vcf cards|build ... | pim when epoch|civil ...");
	return 2;
}

const hibr_bi pm_bi[] = {
	{ "pim", m_pim, "calendars and contacts: iCalendar and vCard read, expanded and made" },
	HIBR_BI_END
};

HIBR_MODULE("pim", "1.0", "calendars and contacts: iCalendar and vCard", pm_bi, 0, 0);
