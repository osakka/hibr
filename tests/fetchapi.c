#define _GNU_SOURCE

/* A module for tests/fetch.py alone: it asks for the "fetch" interface the
   way the web module does, and drives it with a streamed body and a cancel
   set from a timer -- the parts of the C interface the script verb does not
   reach. `fqtest URL [cancel-ms]` prints "code ctype bytes heads err". */

#include "hibr.h"
#include "../mods/fetch.h"
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

volatile int fq_stop;
long long fq_bytes;
int fq_heads;

void fq_alarm(int sig)
{
	(void)sig;
	fq_stop = 1;
}

int fq_head(void *ctx, const ft_res *rs)
{
	(void)ctx;
	(void)rs;
	fq_heads++;
	return 0;
}

int fq_data(void *ctx, const char *p, size_t n)
{
	(void)ctx;
	(void)p;
	fq_bytes += (long long)n;
	return 0;
}

int fq_test(sh *s, int ac, char **av)
{
	ft_api *api = hibr_require(s, "fetch", FT_API_VER);
	ft_req rq;
	ft_res rs;
	struct itimerval it;
	int rc;

	if (!api || ac < 2) {
		printf("no fetch interface\n");
		return 1;
	}
	api->reqinit(&rq);
	rq.url = av[1];
	rq.onhead = fq_head;
	rq.ondata = fq_data;
	rq.cancel = &fq_stop;
	rq.flags = FT_IDENTITY;
	fq_stop = 0;
	fq_bytes = 0;
	fq_heads = 0;
	if (ac > 2) {
		signal(SIGALRM, fq_alarm);
		memset(&it, 0, sizeof it);
		it.it_value.tv_sec = atoi(av[2]) / 1000;
		it.it_value.tv_usec = (atoi(av[2]) % 1000) * 1000;
		setitimer(ITIMER_REAL, &it, 0);
	}
	rc = api->fetch(0, &rq, &rs);
	printf("%d %s %lld %d %s\n", rs.code, rs.ctype ? rs.ctype : "-",
	       fq_bytes, fq_heads, rs.err ? rs.err : "-");
	fflush(stdout);
	api->resfree(&rs);
	return rc == 0 ? 0 : 1;
}

void fq_fin(sh *s)
{
	(void)s;
}

const hibr_bi fq_bi[] = {
	{ "fqtest", fq_test, "a test of the fetch interface" },
	HIBR_BI_END
};

HIBR_MODULE("fqtest", "0.1", "a test of the fetch interface", fq_bi, 0, fq_fin);
