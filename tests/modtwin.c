/* Two modules declaring one builtin, as hibr's own web (Chromium) and
   hibr-browser's webkit both declare `web`, built from this one file: the
   plain one is named for the builtin and offers nothing, the other offers
   the interface of that name. A fixture of the suite's, so the check does
   not rest on which browser engines happen to be installed. */
#include <stdio.h>
#include "hibr.h"

int tw_ok;

/* Say which of the two answered. */
int tw_twin(sh *s, int ac, char **av)
{
	(void)s; (void)ac; (void)av;
	puts(TW_WHO);
	fflush(stdout);
	return 0;
}

const hibr_bi tw_bi[] = { { "twin", tw_twin, "twin -- say which module answered" }, { 0, 0, 0 } };

#ifdef TW_PROV
/* Offer the interface the builtin is named for. */
int tw_ini(sh *s)
{
	return hibr_provide(s, "twin", 1, &tw_ok);
}

/* Withdraw it again. */
void tw_fin(sh *s)
{
	hibr_unprovide(s, "twin");
}

HIBR_MODULE_P("twinkit", "1.0", "offers the twin interface", tw_bi, tw_ini, tw_fin, "twin");
#else
HIBR_MODULE("twin", "1.0", "named twin, offers nothing", tw_bi, 0, 0);
#endif
