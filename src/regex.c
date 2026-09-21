#include "pri.h"
#include "re.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Count the capturing groups a pattern can produce. */
size_t re_groups(const char *p)
{
	size_t n = 1;

	for (; *p; p++) {
		if (*p == '\\' && p[1]) {
			p++;
			continue;
		}
		if (*p == '(')
			n++;
	}
	return n;
}

/* Compile a pattern, reporting why it failed. */
int re_make(re_t *re, const char *pat, int icase)
{
	int rc = regcomp(re, pat, HIBR_REG_EXTENDED | (icase ? HIBR_REG_ICASE : 0));
	str b;

	if (!rc)
		return HIBR_OK;
	s_init(&b);
	s_grow(&b, 256);
	regerror(rc, re, b.p, 256);
	lg(HIBR_LERR, "bad pattern /%s/: %s", pat, b.p);
	s_free(&b);
	return HIBR_FAIL;
}

/* Store one capture group under a numeric key. */
void re_cap(sh *s, const char *nm, long i, const char *t, re_m *m)
{
	str v;

	s_init(&v);
	if (m->so >= 0)
		s_add(&v, t + m->so, (size_t)(m->eo - m->so));
	v_setel(s, nm, i, v.p ? v.p : "");
	s_free(&v);
}

/* Match a subject against a pattern and peel the groups into a variable. */
int b_match(sh *s, int ac, char **av)
{
	re_t re;
	re_m *m;
	const char *nm = "M";
	const char *sub, *pat, *t;
	size_t ng;
	int i = 1, icase = 0, all = 0, rc, found = 0;
	long slot = 0;

	for (; i < ac; i++) {
		if (!strcmp(av[i], "-i"))
			icase = 1;
		else if (!strcmp(av[i], "-a"))
			all = 1;
		else if (!strcmp(av[i], "--")) {
			i++;
			break;
		} else
			break;
	}
	if (ac - i < 2) {
		lg(HIBR_LERR, "usage: match [-i] [-a] subject pattern [var]");
		return 2;
	}
	sub = av[i++];
	pat = av[i++];
	if (i < ac)
		nm = av[i];
	if (re_make(&re, pat, icase) != HIBR_OK)
		return 2;
	ng = re_groups(pat);
	m = xm(ng * sizeof *m);
	v_del(s, nm);
	t = sub;
	for (;;) {
		rc = regexec(&re, t, ng, m, t == sub ? 0 : HIBR_REG_NOTBOL);
		if (rc)
			break;
		found = 1;
		if (all) {
			re_cap(s, nm, slot++, t, &m[0]);
		} else {
			size_t g;
			for (g = 0; g < ng; g++)
				re_cap(s, nm, (long)g, t, &m[g]);
			break;
		}
		if (m[0].eo == m[0].so)
			t += m[0].eo + 1;
		else
			t += m[0].eo;
		if (!*t || (size_t)(t - sub) > strlen(sub))
			break;
	}
	free(m);
	regfree(&re);
	lg(HIBR_LDBG, "match %s: %s", found ? "hit" : "miss", pat);
	return found ? HIBR_OK : HIBR_FAIL;
}

/* Expand \\1 style references in a replacement. */
void re_rep(str *o, const char *rep, const char *t, re_m *m, size_t ng)
{
	size_t g;

	while (*rep) {
		if (*rep == '\\' && rep[1] >= '0' && rep[1] <= '9') {
			g = (size_t)(rep[1] - '0');
			if (g < ng && m[g].so >= 0)
				s_add(o, t + m[g].so,
				      (size_t)(m[g].eo - m[g].so));
			rep += 2;
			continue;
		}
		if (*rep == '\\' && rep[1]) {
			s_ch(o, rep[1]);
			rep += 2;
			continue;
		}
		s_ch(o, *rep++);
	}
}

/* Substitute matches of a pattern, writing the result or setting a variable. */
int b_rsub(sh *s, int ac, char **av)
{
	re_t re;
	re_m *m;
	const char *sub, *pat, *rep, *t;
	str o;
	size_t ng;
	int i = 1, icase = 0, global = 0, n = 0;

	for (; i < ac; i++) {
		if (!strcmp(av[i], "-i"))
			icase = 1;
		else if (!strcmp(av[i], "-g"))
			global = 1;
		else if (!strcmp(av[i], "--")) {
			i++;
			break;
		} else
			break;
	}
	if (ac - i < 3) {
		lg(HIBR_LERR, "usage: rsub [-i] [-g] subject pattern replacement [var]");
		return 2;
	}
	sub = av[i++];
	pat = av[i++];
	rep = av[i++];
	if (re_make(&re, pat, icase) != HIBR_OK)
		return 2;
	ng = re_groups(pat);
	m = xm(ng * sizeof *m);
	s_init(&o);
	t = sub;
	while (!regexec(&re, t, ng, m, t == sub ? 0 : HIBR_REG_NOTBOL)) {
		s_add(&o, t, (size_t)m[0].so);
		re_rep(&o, rep, t, m, ng);
		n++;
		if (m[0].eo == m[0].so) {
			if (t[m[0].eo])
				s_ch(&o, t[m[0].eo]);
			t += m[0].eo + 1;
		} else {
			t += m[0].eo;
		}
		if (!global || !*t)
			break;
	}
	s_cat(&o, t);
	if (i < ac)
		hibr_set(s, av[i], o.p ? o.p : "", 0);
	else
		printf("%s\n", o.p ? o.p : "");
	free(m);
	regfree(&re);
	s_free(&o);
	lg(HIBR_LDBG, "rsub replaced %d", n);
	return n ? HIBR_OK : HIBR_FAIL;
}
