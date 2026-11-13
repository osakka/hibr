#include "hibr.h"
#include "../mods/inspect/inspect.h"
#include <string.h>
#include <stdlib.h>

static const ix_api *ixs_api;

/* Check the C interface preserves the caller on successful and invalid input. */
static int ixs_check(sh *s, int ac, char **av)
{
	const char *value;
	char *before;
	str out;
	int status = s->st, same;
	unsigned line = s->ln;

	if (ac != 2) return 2;
	value = hibr_get(s, "RET"); before = xs(value ? value : "");
	s_init(&out);
	ixs_api->query(s, "functions", av[1], 0, &out);
	value = hibr_get(s, "RET");
	same = status == s->st && line == s->ln && !strcmp(before, value ? value : "");
	free(before); s_free(&out);
	hibr_ret(s, same ? "preserved" : "changed");
	return same ? 0 : 1;
}

/* Load the producer before taking any caller-state snapshots. */
static int ixs_init(sh *s)
{
	ixs_api = hibr_require(s, "inspect", IX_API_VER);
	return ixs_api ? 0 : 1;
}

static const hibr_bi ixs_builtins[] = {
	{ "ixstate", ixs_check, "test inspection caller-state preservation" },
	HIBR_BI_END
};
HIBR_MODULE("inspect-state-test", "0", "inspection state test", ixs_builtins, ixs_init, 0);
