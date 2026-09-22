#define _GNU_SOURCE

#include "tr.h"
#include "../display.h"
#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#ifdef __linux__
#include <linux/errqueue.h>
#endif

static const dp_api *dp;

#ifdef __linux__
/* Collect whatever has arrived, timing each reply when it is seen rather than
   when the round ends. Sent as one burst, routers rate limit their ICMP and
   the queueing lands in the timings; spacing the sends fixes that, but only if
   replies are picked up between them -- otherwise the wait for later hops is
   charged to the earlier ones, and a LAN gateway reads two hundred
   milliseconds. */
void tr_drain(struct pollfd *p, struct timeval *t0, int n, int ms, tr_hop *h,
	      int *deepest)
{
	struct timeval now, a, z;
	struct msghdr m;
	struct iovec io;
	struct cmsghdr *cm;
	char cb[512], b[128];
	int i, left = ms;

	for (;;) {
		gettimeofday(&a, 0);
		if (poll(p, (nfds_t)n, left) <= 0)
			return;
		gettimeofday(&now, 0);
		for (i = 0; i < n; i++) {
			struct sock_extended_err *e;
			struct sockaddr_in *o;
			double v;
			if (p[i].fd < 0 || !(p[i].revents & POLLERR))
				continue;
			p[i].revents = 0;
			io.iov_base = b;
			io.iov_len = sizeof b;
			memset(&m, 0, sizeof m);
			m.msg_iov = &io;
			m.msg_iovlen = 1;
			m.msg_control = cb;
			m.msg_controllen = sizeof cb;
			if (recvmsg(p[i].fd, &m, MSG_ERRQUEUE) < 0)
				continue;
			for (cm = CMSG_FIRSTHDR(&m); cm;
			     cm = CMSG_NXTHDR(&m, cm)) {
				if (cm->cmsg_level != IPPROTO_IP ||
				    cm->cmsg_type != IP_RECVERR)
					continue;
				e = (struct sock_extended_err *)CMSG_DATA(cm);
				o = (struct sockaddr_in *)SO_EE_OFFENDER(e);
				if (o->sin_family != AF_INET)
					continue;
				v = tr_ms(&t0[i], &now);
				if (!h[i].seen ||
				    strcmp(h[i].ip, inet_ntoa(o->sin_addr))) {
					inet_ntop(AF_INET, &o->sin_addr,
						  h[i].ip, sizeof h[i].ip);
					h[i].named = 0;
				}
				h[i].seen = 1;
				h[i].final = e->ee_type == 3;
				h[i].recv++;
				if (h[i].recv > 1) {
					h[i].jsum += v > h[i].prev
							     ? v - h[i].prev
							     : h[i].prev - v;
					h[i].jn++;
				}
				h[i].prev = v;
				h[i].last = v;
				h[i].sum += v;
				if (h[i].recv == 1 || v < h[i].best)
					h[i].best = v;
				if (v > h[i].worst)
					h[i].worst = v;
				h[i].hist[h[i].hn % TR_HIST] = v;
				h[i].hn++;
				if (h[i].final && i + 1 < *deepest)
					*deepest = i + 1;
			}
			close(p[i].fd);
			p[i].fd = -1;
		}
		gettimeofday(&z, 0);
		left -= (int)tr_ms(&a, &z);
		if (left <= 0)
			return;
	}
}

/* One round: every hop limit is sent, spaced apart, with replies picked up in
   between. Probing hops strictly one at a time would make a round take the
   timeout multiplied by the number of hops, which is too slow to watch. */
int tr_round(struct sockaddr_in *d, int maxttl, int wait, tr_hop *h)
{
	struct pollfd *p = xm((size_t)maxttl * sizeof *p);
	struct timeval *t0 = xm((size_t)maxttl * sizeof *t0);
	int i, on = 1, deepest = maxttl;

	for (i = 0; i < maxttl; i++) {
		p[i].fd = -1;
		p[i].events = POLLERR;
		p[i].revents = 0;
	}
	for (i = 0; i < maxttl; i++) {
		int ttl = i + 1;
		p[i].fd = socket(AF_INET, SOCK_DGRAM, 0);
		if (p[i].fd < 0)
			continue;
		setsockopt(p[i].fd, IPPROTO_IP, IP_RECVERR, &on, sizeof on);
		setsockopt(p[i].fd, IPPROTO_IP, IP_TTL, &ttl, sizeof ttl);
		gettimeofday(&t0[i], 0);
		if (sendto(p[i].fd, "", 1, 0, (struct sockaddr *)d,
			   sizeof *d) < 0) {
			close(p[i].fd);
			p[i].fd = -1;
			continue;
		}
		h[i].sent++;
		if (i + 1 < maxttl)
			tr_drain(p, t0, i + 1, 12, h, &deepest);
	}
	tr_drain(p, t0, maxttl, wait, h, &deepest);
	for (i = 0; i < maxttl; i++)
		if (p[i].fd >= 0)
			close(p[i].fd);
	free(p);
	free(t0);
	return deepest;
}
#else
int tr_round(struct sockaddr_in *d, int maxttl, int wait, tr_hop *h)
{
	(void)d;
	(void)maxttl;
	(void)wait;
	(void)h;
	return 0;
}
#endif

/* Ask who a hop is, once, the first time it answers. */
void tr_name(tr_hop *h)
{
	struct sockaddr_in o;

	if (h->named || !h->seen)
		return;
	h->named = 1;
	h->host[0] = 0;
	memset(&o, 0, sizeof o);
	o.sin_family = AF_INET;
	if (inet_pton(AF_INET, h->ip, &o.sin_addr) != 1)
		return;
	if (getnameinfo((struct sockaddr *)&o, sizeof o, h->host,
			sizeof h->host, 0, 0, NI_NAMEREQD) != 0)
		h->host[0] = 0;
}

/* One of the eight block heights, for a value against a ceiling. */
const char *tr_block(double v, double top)
{
	static const char *b[] = { "\342\226\201", "\342\226\202",
				   "\342\226\203", "\342\226\204",
				   "\342\226\205", "\342\226\206",
				   "\342\226\207", "\342\226\210" };
	int i;

	if (top <= 0)
		return b[0];
	i = (int)(v / top * 7.999);
	if (i < 0)
		i = 0;
	if (i > 7)
		i = 7;
	return b[i];
}

/* Colour a latency the way the map does. */
unsigned tr_tone(double ms)
{
	if (ms < 25)
		return DP_PAL | 46u;
	if (ms < 60)
		return DP_PAL | 82u;
	if (ms < 120)
		return DP_PAL | 226u;
	if (ms < 250)
		return DP_PAL | 208u;
	return DP_PAL | 196u;
}

/* Put a number in a fixed width, one decimal place. */
void tr_num(str *o, double v, int w)
{
	str t;
	size_t i;

	s_init(&t);
	s_num(&t, (long)(v * 10 + 0.5));
	if (t.n < 2) {
		str z;
		s_init(&z);
		s_cat(&z, "0");
		s_cat(&z, t.p ? t.p : "0");
		s_free(&t);
		t = z;
	}
	for (i = t.n + 1; (int)i < w; i++)
		s_ch(o, ' ');
	s_add(o, t.p, t.n - 1);
	s_ch(o, '.');
	s_add(o, t.p + t.n - 1, 1);
	s_free(&t);
}

/* Right-align a whole number in a fixed width. */
void tr_int(str *o, long v, int w)
{
	str t;
	size_t i;

	s_init(&t);
	s_num(&t, v);
	for (i = t.n; (int)i < (size_t)w; i++)
		s_ch(o, ' ');
	s_cat(o, t.p ? t.p : "0");
	s_free(&t);
}

/* Draw the table. */
void tr_draw(tr_hop *h, int n, const char *host, const char *ip, long rounds,
	     int rows, int cols, int paused)
{
	int i, r, hw;
	str t;
	double top = 0;

	dp->pen(DP_DEFAULT, DP_DEFAULT, 0);
	dp->clear();
	s_init(&t);
	s_cat(&t, " ");
	s_cat(&t, host);
	s_cat(&t, " (");
	s_cat(&t, ip);
	s_cat(&t, ")   ");
	s_num(&t, rounds);
	s_cat(&t, rounds == 1 ? " round" : " rounds");
	if (paused)
		s_cat(&t, "   PAUSED");
	s_cat(&t, "   q quit  p pause  r reset");
	dp->pen(DP_PAL | 16u, DP_PAL | 110u, 0);
	dp->fill(0, 0, 1, cols, " ");
	dp->put(0, 0, t.p);
	t.n = 0;
	dp->pen(DP_PAL | 252u, DP_PAL | 238u, 0);
	dp->fill(1, 0, 1, cols, " ");
	dp->put(1, 0,
		" Hop  Address           Loss   Snt   Last    Avg   Best   "
		"Wrst   Jttr  History");
	hw = cols - 72;
	if (hw > TR_HIST)
		hw = TR_HIST;
	for (i = 0; i < n; i++) {
		int k;
		for (k = 0; k < h[i].hn && k < TR_HIST; k++)
			if (h[i].hist[k] > top)
				top = h[i].hist[k];
	}
	for (i = 0, r = 2; i < n && r < rows; i++, r++) {
		tr_hop *p = &h[i];
		double loss = p->sent ? (double)(p->sent - p->recv) * 100.0 /
						(double)p->sent
				      : 0.0;
		t.n = 0;
		if (t.p)
			t.p[0] = 0;
		tr_int(&t, (long)i + 1, 4);
		s_cat(&t, "  ");
		if (p->seen) {
			const char *nm = p->host[0] ? p->host : p->ip;
			size_t w = strlen(nm);
			s_cat(&t, nm);
			while (w++ < 17)
				s_ch(&t, ' ');
		} else {
			s_cat(&t, "???              ");
		}
		dp->pen(p->seen ? DP_DEFAULT : DP_PAL | 240u, DP_DEFAULT, 0);
		dp->put(r, 0, t.p);
		t.n = 0;
		if (t.p)
			t.p[0] = 0;
		tr_num(&t, loss, 6);
		s_ch(&t, '%');
		dp->pen(loss > 50 ? DP_PAL | 196u
				  : (loss > 0 ? DP_PAL | 208u : DP_PAL | 244u),
			DP_DEFAULT, 0);
		dp->put(r, 23, t.p);
		t.n = 0;
		if (t.p)
			t.p[0] = 0;
		tr_int(&t, p->sent, 6);
		dp->pen(DP_PAL | 244u, DP_DEFAULT, 0);
		dp->put(r, 30, t.p);
		if (!p->recv) {
			dp->pen(DP_PAL | 240u, DP_DEFAULT, 0);
			dp->put(r, 36, "      -      -      -      -      -");
			continue;
		}
		t.n = 0;
		if (t.p)
			t.p[0] = 0;
		tr_num(&t, p->last, 7);
		tr_num(&t, p->sum / (double)p->recv, 7);
		tr_num(&t, p->best, 7);
		tr_num(&t, p->worst, 7);
		tr_num(&t, p->jn ? p->jsum / (double)p->jn : 0.0, 7);
		dp->pen(tr_tone(p->sum / (double)p->recv), DP_DEFAULT, 0);
		dp->put(r, 36, t.p);
		{
			int c = 72, k, from = p->hn > hw ? p->hn - hw : 0;
			for (k = from; k < p->hn && c < cols; k++, c++) {
				double v = p->hist[k % TR_HIST];
				dp->pen(tr_tone(v), DP_DEFAULT, 0);
				c += dp->put(r, c, tr_block(v, top)) - 1;
			}
		}
	}
	dp->pen(DP_DEFAULT, DP_DEFAULT, 0);
	s_free(&t);
	dp->flush();
}

/* Keep probing and keep the table on screen until told to stop. */
int tr_live(sh *s, struct sockaddr_in *d, const char *host, int maxttl,
	    int wait, int resolve)
{
	tr_hop *h;
	str key;
	char ip[INET_ADDRSTRLEN];
	int rows, cols, n = maxttl, quit = 0, paused = 0, i;
	long rounds = 0;

	dp = (const dp_api *)hibr_require(s, "display", DP_API_VER);
	if (!dp) {
		lg(HIBR_LERR, "trace: live needs a display; mod load console");
		return HIBR_FAIL;
	}
	h = xm((size_t)maxttl * sizeof *h);
	memset(h, 0, (size_t)maxttl * sizeof *h);
	s_init(&key);
	inet_ntop(AF_INET, &d->sin_addr, ip, sizeof ip);
	if (dp->open(s) != HIBR_OK) {
		free(h);
		return HIBR_FAIL;
	}
	while (!quit) {
		dp->size(&rows, &cols);
		if (!paused) {
			n = tr_round(d, maxttl, wait, h);
			rounds++;
			if (resolve)
				for (i = 0; i < n; i++)
					tr_name(&h[i]);
		}
		tr_draw(h, n, host, ip, rounds, rows, cols, paused);
		key.n = 0;
		if (key.p)
			key.p[0] = 0;
		if (dp->key(paused ? -1 : 200, &key) == 1) {
			const char *k = key.p ? key.p : "";
			if (!strcmp(k, "q") || !strcmp(k, "ctrl-c"))
				quit = 1;
			else if (!strcmp(k, "p") || !strcmp(k, " "))
				paused = !paused;
			else if (!strcmp(k, "r")) {
				memset(h, 0, (size_t)maxttl * sizeof *h);
				rounds = 0;
			}
		}
	}
	dp->close(s);
	free(h);
	s_free(&key);
	return HIBR_OK;
}
