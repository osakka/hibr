/* proctab -- every process under /proc, one line each, in one call.
 *
 * Task Manager read the process table in the shell: for each process, its
 * stat, its status line by line to the two fields it wanted, and for a
 * name comm had cut short its cmdline too. On a machine running a browser
 * engine and a held desktop that is hundreds of processes and a few
 * thousand reads parsed one word at a time, and it ran once a second --
 * 200 to 350 ms each time on the owner's mini PC, during which the desktop
 * drew nothing, so a playing video stopped for a quarter of a second every
 * second (0.99.399). This is the same reading, done by the one reader the
 * system monitor uses too (mods/procs.c):
 *
 *   proctab      pid utime stime uid rss name, a line a process
 *
 * Into the result slot, and printed unless := is catching it. Where there
 * is no /proc it fails, and the caller uses ps.
 */
#include "procs.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>

/* One process's line onto the string ctx is. */
void pq_line(const pq_ent *e, void *ctx)
{
	str *out = ctx;

	s_num(out, e->pid);
	s_ch(out, ' ');
	s_num(out, (long)e->ut);
	s_ch(out, ' ');
	s_num(out, (long)e->st);
	s_ch(out, ' ');
	s_num(out, e->uid);
	s_ch(out, ' ');
	s_num(out, e->rss);
	s_ch(out, ' ');
	s_add(out, e->name.p ? e->name.p : "", e->name.n);
	s_ch(out, '\n');
}

/* proctab: every process, a line each. */
int m_proctab(sh *s, int ac, char **av)
{
	str out;

	(void)ac;
	(void)av;
	s_init(&out);
	if (!pq_each(pq_line, &out)) {
		lg(HIBR_LERR, "proctab: /proc: %s", strerror(errno));
		s_free(&out);
		return HIBR_FAIL;
	}
	if (out.n && out.p[out.n - 1] == '\n')
		out.p[--out.n] = 0;
	hibr_ret(s, out.p ? out.p : "");
	if (!s->bind && out.n)
		printf("%s\n", out.p);
	s_free(&out);
	return HIBR_OK;
}

const hibr_bi proctab_bi[] = {
	{ "proctab", m_proctab, "every process: pid utime stime uid rss name" },
	HIBR_BI_END
};

/* Prepare the module. */
int pqt_ini(sh *s)
{
	(void)s;
	return HIBR_OK;
}

/* Tear the module down. */
void pqt_fin(sh *s)
{
	(void)s;
}

HIBR_MODULE("proctab", "1.0", "every process under /proc in one call",
	    proctab_bi, pqt_ini, pqt_fin);
