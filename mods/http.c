#include "hibr.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Open /dev/http/host/port/path as a descriptor positioned at the body. */
int http_open(sh *s, const char *rest)
{
	str host, port, path, req;
	const char *a = strchr(rest, '/');
	const char *b = a ? strchr(a + 1, '/') : 0;
	int fd, state = 0;
	char c;

	if (!a) {
		lg(HIBR_LERR, "/dev/http/ wants host/port/path");
		return -1;
	}
	s_init(&host);
	s_init(&port);
	s_init(&path);
	s_add(&host, rest, (size_t)(a - rest));
	if (b) {
		s_add(&port, a + 1, (size_t)(b - a - 1));
		s_cat(&path, b);
	} else {
		s_cat(&port, a + 1);
		s_ch(&path, '/');
	}
	fd = hibr_dial(host.p, port.p, 0);
	if (fd < 0) {
		s_free(&host);
		s_free(&port);
		s_free(&path);
		return -1;
	}
	s_init(&req);
	s_cat(&req, "GET ");
	s_cat(&req, path.p);
	s_cat(&req, " HTTP/1.0\r\nHost: ");
	s_cat(&req, host.p);
	s_cat(&req, "\r\nConnection: close\r\n\r\n");
	if (write(fd, req.p, req.n) != (ssize_t)req.n) {
		lg(HIBR_LERR, "http: request write failed");
		close(fd);
		fd = -1;
	}
	while (fd >= 0 && read(fd, &c, 1) == 1) {
		if (c == '\r')
			continue;
		if (c == '\n') {
			if (state == 1)
				break;
			state = 1;
			continue;
		}
		state = 0;
	}
	lg(HIBR_LDBG, "http: %s:%s%s ready on fd %d", host.p, port.p, path.p, fd);
	s_free(&req);
	s_free(&host);
	s_free(&port);
	s_free(&path);
	hibr_set(s, "HTTP_HOST", host.p ? host.p : "", 0);
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

HIBR_MODULE("http", HIBR_VER, "/dev/http/ scheme, msum, oops", http_bi,
	   http_ini, http_fin);
