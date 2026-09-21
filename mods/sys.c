#include "hibr.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* Print seconds since the epoch. */
int m_epoch(sh *s, int ac, char **av)
{
	(void)s;
	(void)ac;
	(void)av;
	printf("%ld\n", (long)time(0));
	return HIBR_OK;
}

/* Sleep for a number of milliseconds. */
int m_sleepms(sh *s, int ac, char **av)
{
	long ms = ac > 1 ? atol(av[1]) : 0;
	struct timespec t;

	(void)s;
	if (ms < 0) {
		lg(HIBR_LERR, "sleepms: negative duration");
		return HIBR_FAIL;
	}
	t.tv_sec = ms / 1000;
	t.tv_nsec = (ms % 1000) * 1000000L;
	lg(HIBR_LDBG, "sleeping %ld ms", ms);
	nanosleep(&t, 0);
	return HIBR_OK;
}

/* Write arguments in upper case. */
int m_upper(sh *s, int ac, char **av)
{
	int i;
	size_t j;
	str b;

	(void)s;
	s_init(&b);
	for (i = 1; i < ac; i++) {
		if (i > 1)
			s_ch(&b, ' ');
		s_cat(&b, av[i]);
	}
	for (j = 0; j < b.n; j++)
		b.p[j] = (char)toupper((unsigned char)b.p[j]);
	puts(b.n ? b.p : "");
	s_free(&b);
	return HIBR_OK;
}

/* Report the shell state this module can reach. */
int m_state(sh *s, int ac, char **av)
{
	(void)ac;
	(void)av;
	printf("status %d  params %d  functions %lu  loglevel %d\n", s->st, s->ac,
	       (unsigned long)s->fns.n, hibr_lv);
	return HIBR_OK;
}

const hibr_bi sys_bi[] = {
	{ "epoch", m_epoch, "seconds since the epoch" },
	{ "sleepms", m_sleepms, "sleep for milliseconds" },
	{ "state", m_state, "report shell state" },
	{ "upper", m_upper, "write arguments in upper case" },
	HIBR_BI_END
};

/* Prepare the module. */
int sys_ini(sh *s)
{
	hibr_set(s, "SYS_MOD", "1", 0);
	lg(HIBR_LINF, "sys module ready");
	return HIBR_OK;
}

/* Tear the module down. */
void sys_fin(sh *s)
{
	lg(HIBR_LINF, "sys module going away");
	(void)s;
}

HIBR_MODULE("sys", HIBR_VER, "epoch, sleepms, state, upper", sys_bi, sys_ini,
	   sys_fin);
