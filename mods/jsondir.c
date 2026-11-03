#include "jsondir.h"
#include <dirent.h>
#include <stdlib.h>
#include <string.h>

/* Hand every *.json file in one folder to fn. */
void jd_dir(const char *d, void (*fn)(const char *path, const char *text))
{
	DIR *dp = opendir(d);
	struct dirent *e;
	str p, t;
	size_t l;

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
		t.n = 0;
		if (hibr_slurp(p.p, &t) == 0)
			fn(p.p, t.p);
	}
	closedir(dp);
	s_free(&p);
	s_free(&t);
}

/* Walk the catalogue's folders in order; see jsondir.h. */
void jd_walk(const char *env, const char *sub, const char *share,
	     void (*fn)(const char *path, const char *text))
{
	const char *e = getenv(env), *h = getenv("HOME"), *x = getenv("XDG_CONFIG_HOME");
	char *dup, *tok, *save;
	str d;

	s_init(&d);
	if (e && *e) {
		dup = xs(e);
		for (tok = strtok_r(dup, ":", &save); tok; tok = strtok_r(0, ":", &save))
			jd_dir(tok, fn);
		free(dup);
	}
	if (x && *x)
		s_fmt(&d, "%s/hibr/%s", x, sub);
	else if (h && *h)
		s_fmt(&d, "%s/.config/hibr/%s", h, sub);
	if (d.n)
		jd_dir(d.p, fn);
	d.n = 0;
	s_fmt(&d, "%s/%s", share, sub);
	jd_dir(d.p, fn);
	s_free(&d);
}
