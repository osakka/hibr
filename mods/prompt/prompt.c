#include "pr.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Fill a render context from the shell's current state. */
void pr_ctx(sh *s, pctx *c)
{
	const char *v;
	char *b = xm(4096);

	memset(c, 0, sizeof *c);
	c->s = s;
	c->cwd = getcwd(b, 4096) ? b : 0;
	if (!c->cwd) {
		free(b);
		v = hibr_get(s, "PWD");
		c->cwd = v ? xs(v) : xs("?");
	}
	v = hibr_get(s, "HOME");
	c->home = v && *v ? xs(v) : 0;
	v = hibr_get(s, "STATUS");
	c->st = v ? atoi(v) : s->st;
	v = hibr_get(s, "DURATION");
	c->dur = v ? atol(v) : 0;
	c->jobs = (int)s->jobs.n;
}

/* Release a render context. */
void pr_ctxfree(pctx *c)
{
	gt_close(c);
	free(c->cwd);
	free(c->home);
}

/* List the segments this module knows. */
int m_prlist(sh *s)
{
	const seg *g;

	(void)s;
	for (g = pr_segs; g->nm; g++)
		printf("  %-10s%s%s\n", g->nm, g->style ? g->style : "",
		       g->off ? " (off by default)" : "");
	return HIBR_OK;
}

/* Read one object out of the repository, for inspection and testing. */
int m_probj(sh *s, int ac, char **av)
{
	pctx c;
	unsigned char sha[20];
	str body;
	int type, raw = 0, i = 2;
	int rc = HIBR_FAIL;

	if (ac > 2 && !strcmp(av[2], "-p")) {
		raw = 1;
		i = 3;
	}
	if (i >= ac || strlen(av[i]) != 40 || !ob_hex(av[i], sha)) {
		lg(HIBR_LERR, "usage: prompt object [-p] <40 hex digits>");
		return 2;
	}
	pr_ctx(s, &c);
	if (!gt_open(&c)) {
		lg(HIBR_LERR, "prompt object: not inside a repository");
		pr_ctxfree(&c);
		return HIBR_FAIL;
	}
	c.g->omax = OB_MAX;
	if (ob_get(c.g, sha, &type, &body)) {
		if (raw) {
			hibr_ret(s, body.p);
			if (!s->bind)
				fwrite(body.p, 1, body.n, stdout);
		} else {
			str d;
			s_init(&d);
			s_cat(&d, ob_tname(type));
			s_ch(&d, ' ');
			s_num(&d, (long)body.n);
			hibr_ret(s, d.p);
			if (!s->bind)
				printf("%s\n", d.p);
			s_free(&d);
		}
		s_free(&body);
		rc = HIBR_OK;
	} else {
		lg(HIBR_LERR, "prompt object: %s not found", av[i]);
	}
	pr_ctxfree(&c);
	return rc;
}

/* Print the object name git would give a file, for testing the hash. */
int m_prhash(sh *s, int ac, char **av)
{
	unsigned char sha[20];
	char hex[41];
	struct stat st;
	int i, rc = HIBR_OK;

	if (ac < 3) {
		lg(HIBR_LERR, "usage: prompt hash <file>...");
		return 2;
	}
	for (i = 2; i < ac; i++) {
		if (lstat(av[i], &st) != 0 ||
		    !sh_blob(av[i], S_ISLNK(st.st_mode), sha)) {
			lg(HIBR_LERR, "prompt hash: %s: unreadable", av[i]);
			rc = HIBR_FAIL;
			continue;
		}
		ob_unhex(sha, hex);
		printf("%s\n", hex);
	}
	return rc;
}

/* Dump the index the way git ls-files does, for testing the parser. */
int m_prindex(sh *s, int ac, char **av)
{
	pctx c;
	gidx *x;
	size_t i;
	char hex[41];

	(void)ac;
	(void)av;
	pr_ctx(s, &c);
	if (!gt_open(&c)) {
		lg(HIBR_LERR, "prompt index: not inside a repository");
		pr_ctxfree(&c);
		return HIBR_FAIL;
	}
	x = ix_read(c.g);
	if (!x) {
		lg(HIBR_LERR, "prompt index: no readable index");
		pr_ctxfree(&c);
		return HIBR_FAIL;
	}
	for (i = 0; i < x->ents.n; i++) {
		struct ie *e = (struct ie *)x->ents.p[i];
		ob_unhex(e->sha, hex);
		printf("%06o %s %d\t%s\n", e->mode, hex, e->stage, e->path);
	}
	ix_free(x);
	pr_ctxfree(&c);
	return HIBR_OK;
}

/* Print the repository status counts, for testing against git. */
int m_prstatus(sh *s, int ac, char **av)
{
	pctx c;
	gstat *t;

	(void)ac;
	(void)av;
	pr_ctx(s, &c);
	if (!gt_open(&c)) {
		lg(HIBR_LERR, "prompt status: not inside a repository");
		pr_ctxfree(&c);
		return HIBR_FAIL;
	}
	t = st_get(&c);
	{
		str d;
		s_init(&d);
		s_cat(&d, "staged ");
		s_num(&d, t->staged);
		s_cat(&d, " modified ");
		s_num(&d, t->modified);
		s_cat(&d, " deleted ");
		s_num(&d, t->deleted);
		s_cat(&d, " untracked ");
		s_num(&d, t->untracked);
		s_cat(&d, " conflicted ");
		s_num(&d, t->conflicted);
		s_cat(&d, " renamed ");
		s_num(&d, t->renamed);
		s_cat(&d, " ahead ");
		s_num(&d, t->ahead);
		s_cat(&d, " behind ");
		s_num(&d, t->behind);
		s_cat(&d, " unborn ");
		s_num(&d, t->unborn);
		hibr_ret(s, d.p);
		if (!s->bind)
			printf("%s\n", d.p);
		s_free(&d);
	}
	pr_ctxfree(&c);
	return HIBR_OK;
}

/* Render the prompt into the result slot, printing when unbound. */
int m_prompt(sh *s, int ac, char **av)
{
	pctx c;
	char *r;

	if (ac > 1 && !strcmp(av[1], "list"))
		return m_prlist(s);
	if (ac > 1 && !strcmp(av[1], "object"))
		return m_probj(s, ac, av);
	if (ac > 1 && !strcmp(av[1], "hash"))
		return m_prhash(s, ac, av);
	if (ac > 1 && !strcmp(av[1], "index"))
		return m_prindex(s, ac, av);
	if (ac > 1 && !strcmp(av[1], "status"))
		return m_prstatus(s, ac, av);
	if (ac > 1 && strcmp(av[1], "render")) {
		lg(HIBR_LERR, "usage: prompt [render|list|object]");
		return 2;
	}
	pr_ctx(s, &c);
	r = pr_render(&c);
	pr_ctxfree(&c);
	hibr_ret(s, r);
	if (!s->bind)
		fputs(r, stdout);
	free(r);
	return HIBR_OK;
}

/* Announce the module and point PROMPT_FN at it. */
int m_prini(sh *s)
{
	const char *v = hibr_get(s, "PROMPT_FN");

	if (!v || !*v)
		hibr_set(s, "PROMPT_FN", "prompt", 0);
	lg(HIBR_LINF, "prompt module ready, PROMPT_FN=%s",
	   hibr_get(s, "PROMPT_FN"));
	return HIBR_OK;
}

/* Stop the shell calling a hook that is about to disappear. */
void m_prfin(sh *s)
{
	const char *v = hibr_get(s, "PROMPT_FN");

	if (v && !strcmp(v, "prompt")) {
		hibr_set(s, "PROMPT_FN", "", 0);
		lg(HIBR_LINF, "prompt module gone, PROMPT_FN cleared");
	}
}

const hibr_bi pr_bi[] = {
	{ "prompt", m_prompt, "render the configured prompt" },
	HIBR_BI_END
};

HIBR_MODULE("prompt", HIBR_VER, "segment based prompt", pr_bi, m_prini,
	   m_prfin);
