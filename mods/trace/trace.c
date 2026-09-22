#define _GNU_SOURCE

#include "hibr.h"
#include <errno.h>
#include <netdb.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>

#ifdef __linux__
#include <linux/errqueue.h>
#endif

#ifndef TR_PORT
#define TR_PORT 33434
#endif

/* Milliseconds between two points in time. */
double tr_ms(struct timeval *a, struct timeval *b)
{
	return (b->tv_sec - a->tv_sec) * 1000.0 +
	       (b->tv_usec - a->tv_usec) / 1000.0;
}

/* Store one field of one hop in the map the script will read. */
void tr_put(sh *s, const char *nm, int hop, const char *fld, const char *v)
{
	char *ks[2];
	str h;

	s_init(&h);
	s_num(&h, (long)hop);
	ks[0] = h.p;
	ks[1] = (char *)fld;
	hibr_setp(s, nm, ks, 2, v);
	s_free(&h);
}

#ifdef __linux__
/* Send one probe at a given hop limit and wait for whoever complains. */
int tr_probe(struct sockaddr_in *d, int ttl, int wait, char *ip, double *ms,
	     int *final)
{
	int s, on = 1, got = 0;
	struct timeval t0, t1;
	struct pollfd p;
	struct msghdr m;
	struct iovec io;
	struct cmsghdr *cm;
	char cb[512], b[128];

	s = socket(AF_INET, SOCK_DGRAM, 0);
	if (s < 0)
		return -1;
	if (setsockopt(s, IPPROTO_IP, IP_RECVERR, &on, sizeof on) < 0 ||
	    setsockopt(s, IPPROTO_IP, IP_TTL, &ttl, sizeof ttl) < 0) {
		close(s);
		return -1;
	}
	gettimeofday(&t0, 0);
	if (sendto(s, "", 1, 0, (struct sockaddr *)d, sizeof *d) < 0) {
		close(s);
		return -1;
	}
	p.fd = s;
	p.events = POLLERR;
	p.revents = 0;
	if (poll(&p, 1, wait) > 0) {
		io.iov_base = b;
		io.iov_len = sizeof b;
		memset(&m, 0, sizeof m);
		m.msg_iov = &io;
		m.msg_iovlen = 1;
		m.msg_control = cb;
		m.msg_controllen = sizeof cb;
		if (recvmsg(s, &m, MSG_ERRQUEUE) >= 0) {
			gettimeofday(&t1, 0);
			for (cm = CMSG_FIRSTHDR(&m); cm; cm = CMSG_NXTHDR(&m, cm)) {
				struct sock_extended_err *e;
				struct sockaddr_in *o;
				if (cm->cmsg_level != IPPROTO_IP ||
				    cm->cmsg_type != IP_RECVERR)
					continue;
				e = (struct sock_extended_err *)CMSG_DATA(cm);
				o = (struct sockaddr_in *)SO_EE_OFFENDER(e);
				if (o->sin_family != AF_INET)
					continue;
				inet_ntop(AF_INET, &o->sin_addr, ip,
					  INET_ADDRSTRLEN);
				*ms = tr_ms(&t0, &t1);
				*final = e->ee_type == 3;
				got = 1;
			}
		}
	}
	close(s);
	return got;
}
#endif

/* Trace the path to a host, filling a map and printing as it goes. */
int m_trace(sh *s, int ac, char **av)
{
#ifndef __linux__
	(void)s;
	(void)ac;
	(void)av;
	lg(HIBR_LERR, "trace: needs IP_RECVERR, which is Linux only");
	return HIBR_FAIL;
#else
	const char *host = 0, *nm = "TRACE";
	int i, ttl, maxttl = 24, wait = 1000, quiet = 0, norev = 0, done = 0;
	struct addrinfo hint, *res = 0;
	struct sockaddr_in d;
	char ip[INET_ADDRSTRLEN], rev[NI_MAXHOST];
	double ms;

	for (i = 1; i < ac; i++) {
		if (!strcmp(av[i], "-m") && i + 1 < ac)
			maxttl = atoi(av[++i]);
		else if (!strcmp(av[i], "-w") && i + 1 < ac)
			wait = atoi(av[++i]);
		else if (!strcmp(av[i], "-v") && i + 1 < ac)
			nm = av[++i];
		else if (!strcmp(av[i], "-q"))
			quiet = 1;
		else if (!strcmp(av[i], "-n"))
			norev = 1;
		else if (av[i][0] == '-' && av[i][1]) {
			lg(HIBR_LERR, "usage: trace [-m hops] [-w ms] [-v var]"
				      " [-q] [-n] host");
			return 2;
		} else {
			host = av[i];
		}
	}
	if (!host) {
		lg(HIBR_LERR, "usage: trace [-m hops] [-w ms] host");
		return 2;
	}
	if (maxttl < 1 || maxttl > 64)
		maxttl = 24;
	memset(&hint, 0, sizeof hint);
	hint.ai_family = AF_INET;
	hint.ai_socktype = SOCK_DGRAM;
	if (getaddrinfo(host, 0, &hint, &res) != 0 || !res) {
		lg(HIBR_LERR, "trace: %s: cannot resolve", host);
		return HIBR_FAIL;
	}
	memcpy(&d, res->ai_addr, sizeof d);
	freeaddrinfo(res);
	d.sin_port = htons(TR_PORT);
	inet_ntop(AF_INET, &d.sin_addr, ip, sizeof ip);
	hibr_set(s, nm, "", 0);
	tr_put(s, nm, 0, "target", host);
	tr_put(s, nm, 0, "ip", ip);
	lg(HIBR_LDBG, "tracing %s at %s, up to %d hops", host, ip, maxttl);
	if (!quiet)
		printf("trace to %s (%s), %d hops max\n", host, ip, maxttl);
	for (ttl = 1; ttl <= maxttl && !done; ttl++) {
		int got = tr_probe(&d, ttl, wait, ip, &ms, &done);
		str v;
		if (got < 0) {
			lg(HIBR_LERR, "trace: %s", strerror(errno));
			return HIBR_FAIL;
		}
		if (!got) {
			tr_put(s, nm, ttl, "ip", "");
			if (!quiet)
				printf("%2d  *\n", ttl);
			continue;
		}
		tr_put(s, nm, ttl, "ip", ip);
		s_init(&v);
		s_num(&v, (long)(ms * 100));
		if (v.n < 3) {
			str z;
			s_init(&z);
			s_cat(&z, v.n == 1 ? "00" : "0");
			s_cat(&z, v.p ? v.p : "0");
			s_free(&v);
			v = z;
		}
		{
			str r;
			s_init(&r);
			s_add(&r, v.p, v.n - 2);
			s_ch(&r, '.');
			s_add(&r, v.p + v.n - 2, 2);
			s_free(&v);
			v = r;
		}
		tr_put(s, nm, ttl, "rtt", v.p ? v.p : "0");
		s_free(&v);
		rev[0] = 0;
		if (!norev) {
			struct sockaddr_in o;
			memset(&o, 0, sizeof o);
			o.sin_family = AF_INET;
			inet_pton(AF_INET, ip, &o.sin_addr);
			if (getnameinfo((struct sockaddr *)&o, sizeof o, rev,
					sizeof rev, 0, 0, NI_NAMEREQD) != 0)
				rev[0] = 0;
		}
		tr_put(s, nm, ttl, "host", rev);
		if (!quiet)
			printf("%2d  %-15s %8.2f ms  %s\n", ttl, ip, ms, rev);
	}
	{
		str n;
		s_init(&n);
		s_num(&n, (long)(ttl - 1));
		tr_put(s, nm, 0, "hops", n.p);
		s_free(&n);
	}
	return HIBR_OK;
#endif
}

const hibr_bi trace_bi[] = {
	{ "trace", m_trace, "trace the path to a host into a map" },
	HIBR_BI_END
};

HIBR_MODULE("trace", "0.21", "unprivileged traceroute over UDP", trace_bi, 0, 0);
