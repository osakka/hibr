#include "pm.h"
#include <string.h>

/* pim: calendars and contacts as their files are written -- iCalendar and
   vCard read, expanded and made. */
int m_pim(sh *s, int ac, char **av)
{
	const char *sub = ac > 1 ? av[1] : "";

	if (!strcmp(sub, "ics"))
		return pm_ics(s, ac, av);
	if (!strcmp(sub, "vcf"))
		return pm_vcf(s, ac, av);
	lg(HIBR_LERR, "usage: pim ics events|expand|build|reply|vtimezone ... | pim vcf cards|build ...");
	return 2;
}

const hibr_bi pm_bi[] = {
	{ "pim", m_pim, "calendars and contacts: iCalendar and vCard read, expanded and made" },
	HIBR_BI_END
};

HIBR_MODULE("pim", "1.0", "calendars and contacts: iCalendar and vCard", pm_bi, 0, 0);
