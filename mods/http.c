#define _GNU_SOURCE

#include "hibr.h"
#include "fetch.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Where a reply's body goes while it arrives: a file nobody else can name. */
int http_tofd(void *ctx, const char *p, size_t n)
{
	int *fd = ctx;

	return hibr_wrall(*fd, p, n) == (ssize_t)n ? 0 : 1;
}

/* A file to hold a body, already unlinked, so it goes when it is closed. */
int http_tmp(void)
{
	const char *t = getenv("TMPDIR");
	str nm;
	int fd;

	s_init(&nm);
	s_cat(&nm, t && *t ? t : "/tmp");
	s_cat(&nm, "/hibr-http-XXXXXX");
	fd = mkstemp(nm.p);
	if (fd >= 0) {
		unlink(nm.p);
		fcntl(fd, F_SETFD, FD_CLOEXEC);
	}
	s_free(&nm);
	return fd;
}

/* Open /dev/http/host/port/path: the request is made through the fetch
   module, and the descriptor handed back reads the body from its start.
   HTTP_STATUS is the reply's status, 0 when none came. */
int http_open(sh *s, const char *rest)
{
	ft_api *api;
	ft_req rq;
	ft_res rs;
	str url, num;
	const char *a = strchr(rest, '/');
	const char *b = a ? strchr(a + 1, '/') : 0;
	int fd, rc;

	if (!a) {
		lg(HIBR_LERR, "/dev/http/ wants host/port/path");
		return -1;
	}
	api = hibr_require(s, "fetch", FT_API_VER);
	if (!api) {
		lg(HIBR_LERR, "http: the fetch module is not available");
		return -1;
	}
	s_init(&url);
	s_init(&num);
	s_cat(&url, "http://");
	s_add(&url, rest, (size_t)(a - rest));
	s_ch(&url, ':');
	if (b) {
		s_add(&url, a + 1, (size_t)(b - a - 1));
		s_cat(&url, b);
	} else {
		s_cat(&url, a + 1);
		s_ch(&url, '/');
	}
	fd = http_tmp();
	if (fd < 0) {
		lg(HIBR_LERR, "http: no file to hold the reply: %s", strerror(errno));
		s_free(&url);
		s_free(&num);
		return -1;
	}
	api->reqinit(&rq);
	rq.url = url.p;
	rq.flags = FT_NOCOOKIE;
	rq.ondata = http_tofd;
	rq.ctx = &fd;
	rc = api->fetch(s, &rq, &rs);
	s_fmt(&num, "%d", rs.code);
	hibr_set(s, "HTTP_STATUS", num.p, 0);
	num.n = 0;
	s_add(&num, rest, (size_t)(a - rest));
	hibr_set(s, "HTTP_HOST", num.p, 0);
	if (rc != 0) {
		lg(HIBR_LERR, "http: %s: %s", url.p, rs.err ? rs.err : "failed");
		close(fd);
		fd = -1;
	} else if (lseek(fd, 0, SEEK_SET) < 0) {
		close(fd);
		fd = -1;
	} else {
		lg(HIBR_LDBG, "http: %s %d ready on fd %d", url.p, rs.code, fd);
	}
	api->resfree(&rs);
	s_free(&url);
	s_free(&num);
	return fd;
}

/* Sum the numeric values in a map, showing the map API. */
int m_sum(sh *s, int ac, char **av)
{
	vec vals = { 0, 0, 0 };
	size_t i;
	long total = 0;
	str out;

	if (ac < 2) {
		lg(HIBR_LERR, "usage: msum mapvar");
		return HIBR_FAIL;
	}
	hibr_list(s, av[1], 0, 0, &vals, 0);
	for (i = 0; i < vals.n; i++)
		total += atol((char *)vals.p[i]);
	v_free(&vals);
	s_init(&out);
	s_num(&out, total);
	hibr_ret(s, out.p);
	printf("%s\n", out.p);
	s_free(&out);
	return HIBR_OK;
}

/* Report a structured error through the shell's error slot. */
int m_oops(sh *s, int ac, char **av)
{
	hibr_fail(s, ac > 1 ? av[1] : "module reported a problem");
	return HIBR_FAIL;
}

const hibr_bi http_bi[] = {
	{ "msum", m_sum, "sum the values of a map" },
	{ "oops", m_oops, "fail with a message from a module" },
	HIBR_BI_END
};

/* Register the http scheme. */
int http_ini(sh *s)
{
	return hibr_scheme(s, "http", http_open);
}

/* Withdraw the scheme. */
void http_fin(sh *s)
{
	hibr_unscheme(s, "http");
}

HIBR_MODULE("http", "1.1", "/dev/http/ scheme, msum, oops", http_bi,
	   http_ini, http_fin);
