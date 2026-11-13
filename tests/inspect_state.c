#define _GNU_SOURCE
#include "hibr.h"
#include "../mods/inspect/inspect.h"
#include <string.h>
#include <stdlib.h>
#ifdef __linux__
#ifdef __TINYC__
#include <stdint.h>
struct dl_phdr_info { uintptr_t dlpi_addr; const char *dlpi_name; };
extern int dl_iterate_phdr(int (*)(struct dl_phdr_info *, size_t, void *), void *);
#else
#include <link.h>
#endif
#endif

static const ix_api *ixs_api;

#ifdef __linux__
/* Snapshot loaded objects without consulting the host's process filesystem. */
static int ixs_loaded(struct dl_phdr_info *info, size_t size, void *data)
{
	str *out = data;

	(void)size;
	s_fmt(out, "%p:%s\n", (void *)info->dlpi_addr, info->dlpi_name);
	return 0;
}
#endif

/* Check the C interface preserves the caller on successful and invalid input. */
static int ixs_check(sh *s, int ac, char **av)
{
	const char *value;
	char *before;
	str out, loaded, after;
	size_t modules = s->mods.n;
	int status = s->st, same;
	unsigned line = s->ln;

	if (ac != 2) return 2;
	value = hibr_get(s, "RET"); before = xs(value ? value : "");
	s_init(&out); s_init(&loaded); s_init(&after);
#ifdef __linux__
	dl_iterate_phdr(ixs_loaded, &loaded);
#endif
	ixs_api->query(s, "functions", av[1], 0, &out);
	ixs_api->query(s, "calls", av[1], 0, &out);
	ixs_api->query(s, "refs", av[1], 0, &out);
	ixs_api->query(s, "check", av[1], 0, &out);
#ifdef __linux__
	dl_iterate_phdr(ixs_loaded, &after);
#endif
	value = hibr_get(s, "RET");
	same = modules == s->mods.n && loaded.n == after.n && (!loaded.n || !memcmp(loaded.p, after.p, loaded.n)) && status == s->st && line == s->ln && !strcmp(before, value ? value : "");
	free(before); s_free(&out); s_free(&loaded); s_free(&after);
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
