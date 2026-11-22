#define _GNU_SOURCE

#include "fdmsg.h"
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

/* A message's header: its length as four bytes in this machine's order (both
   ends are one machine) and its count of descriptors as one, which this
   format fixes. */
#define FM_HDR 5

#ifdef MSG_CMSG_CLOEXEC
#define FM_RXFL MSG_CMSG_CLOEXEC
#else
#define FM_RXFL 0
#endif
#ifdef MSG_NOSIGNAL
#define FM_TXFL MSG_NOSIGNAL
#else
#define FM_TXFL 0
#endif

/* The clock in milliseconds, for a bound that a signal cannot restart. */
long fm_ms(void)
{
	struct timespec t;

	clock_gettime(CLOCK_MONOTONIC, &t);
	return (long)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

/* Make a descriptor close-on-exec. */
void fm_cloexec(int fd)
{
	int f = fcntl(fd, F_GETFD);

	if (f >= 0)
		fcntl(fd, F_SETFD, f | FD_CLOEXEC);
}

/* A connected pair of stream sockets, both close-on-exec. */
int fm_pair(int *a, int *b)
{
	int sv[2];
#ifdef SO_NOSIGPIPE
	int on = 1;
#endif

	if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) < 0)
		return 0;
	fm_cloexec(sv[0]);
	fm_cloexec(sv[1]);
#ifdef SO_NOSIGPIPE
	setsockopt(sv[0], SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof on);
	setsockopt(sv[1], SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof on);
#endif
	*a = sv[0];
	*b = sv[1];
	return 1;
}

/* Wait until fd can be written, at most until the deadline end. */
int fm_room(int fd, long end)
{
	struct pollfd q;
	long left;

	for (;;) {
		left = end - fm_ms();
		if (left <= 0)
			return 0;
		q.fd = fd;
		q.events = POLLOUT;
		q.revents = 0;
		if (poll(&q, 1, (int)left) == 1)
			return !(q.revents & (POLLERR | POLLNVAL));
		if (errno != EINTR)
			return 0;
	}
}

/* Send one message: its header and bytes, the descriptors on the header's
   first byte, the rest written as room comes, never past ms. */
int fm_send(int fd, const char *t, size_t n, const int *fds, int nf, int ms)
{
	struct msghdr m;
	struct iovec v;
	struct cmsghdr *c;
	union {
		char b[CMSG_SPACE(sizeof(int) * FM_MAXFD)];
		struct cmsghdr align;
	} u;
	str buf;
	uint32_t len = (uint32_t)n;
	size_t off = 0;
	long end = fm_ms() + (ms > 0 ? ms : 0);
	ssize_t k;
	int ok = 0;

	if (nf < 0 || nf > FM_MAXFD)
		return 0;
	s_init(&buf);
	s_add(&buf, (const char *)&len, sizeof len);
	s_ch(&buf, nf);
	s_add(&buf, t, n);
	while (off < buf.n) {
		memset(&m, 0, sizeof m);
		v.iov_base = buf.p + off;
		v.iov_len = buf.n - off;
		m.msg_iov = &v;
		m.msg_iovlen = 1;
		if (!off && nf) {
			memset(&u, 0, sizeof u);
			m.msg_control = u.b;
			m.msg_controllen = CMSG_SPACE(sizeof(int) * (size_t)nf);
			c = CMSG_FIRSTHDR(&m);
			c->cmsg_level = SOL_SOCKET;
			c->cmsg_type = SCM_RIGHTS;
			c->cmsg_len = CMSG_LEN(sizeof(int) * (size_t)nf);
			memcpy(CMSG_DATA(c), fds, sizeof(int) * (size_t)nf);
		}
		k = sendmsg(fd, &m, FM_TXFL | MSG_DONTWAIT);
		if (k > 0) {
			off += (size_t)k;
			continue;
		}
		if (k < 0 && errno == EINTR)
			continue;
		if (k < 0 && (errno == EAGAIN || errno == EWOULDBLOCK) &&
		    fm_room(fd, end))
			continue;
		break;
	}
	ok = off == buf.n;
	s_free(&buf);
	return ok;
}

/* A reader for a channel, the channel made close-on-exec: one a restart
   carried across its exec as an inheritable copy is this process's alone
   again from the first read, never held by what it starts after. */
fm_ch *fm_open(int fd)
{
	fm_ch *c = xm(sizeof *c);

	fm_cloexec(fd);
	memset(c, 0, sizeof *c);
	c->fd = fd;
	s_init(&c->in);
	return c;
}

/* Let a reader go: the descriptors it holds unhanded are closed, and its
   channel too when asked. */
void fm_close(fm_ch *c, int closefd)
{
	size_t i;

	if (!c)
		return;
	for (i = 0; i < c->fq.n; i++)
		close((int)(long)c->fq.p[i]);
	v_free(&c->fq);
	s_free(&c->in);
	if (closefd && c->fd >= 0)
		close(c->fd);
	free(c);
}

/* Read what has arrived, never waiting; a descriptor that came with it is
   queued for the message it rode on. */
int fm_take(fm_ch *c)
{
	struct msghdr m;
	struct iovec v;
	struct cmsghdr *h;
	union {
		char b[CMSG_SPACE(sizeof(int) * FM_MAXFD)];
		struct cmsghdr align;
	} u;
	ssize_t k;
	int got = 0, i, n, fd;

	if (c->gone)
		return -1;
	for (;;) {
		s_grow(&c->in, HIBR_IOCH);
		memset(&m, 0, sizeof m);
		v.iov_base = c->in.p + c->in.n;
		v.iov_len = HIBR_IOCH;
		m.msg_iov = &v;
		m.msg_iovlen = 1;
		m.msg_control = u.b;
		m.msg_controllen = sizeof u.b;
		k = recvmsg(c->fd, &m, FM_RXFL | MSG_DONTWAIT);
		if (k < 0 && errno == EINTR)
			continue;
		if (k < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
			return got;
		if (k <= 0) {
			c->gone = 1;
			return -1;
		}
		c->in.n += (size_t)k;
		c->in.p[c->in.n] = 0;
		got = 1;
		for (h = CMSG_FIRSTHDR(&m); h; h = CMSG_NXTHDR(&m, h)) {
			if (h->cmsg_level != SOL_SOCKET || h->cmsg_type != SCM_RIGHTS)
				continue;
			n = (int)((h->cmsg_len - CMSG_LEN(0)) / sizeof(int));
			for (i = 0; i < n; i++) {
				memcpy(&fd, CMSG_DATA(h) + sizeof(int) * (size_t)i, sizeof fd);
				if (!FM_RXFL)
					fm_cloexec(fd);
				v_add(&c->fq, (void *)(long)fd);
			}
		}
	}
}

/* The next whole message off the front of what has arrived. */
int fm_next(fm_ch *c, str *out, int *fds, int *nf)
{
	uint32_t len;
	int want, i;

	*nf = 0;
	if (c->in.n < FM_HDR)
		return 0;
	memcpy(&len, c->in.p, sizeof len);
	if (c->in.n < FM_HDR + (size_t)len)
		return 0;
	want = (unsigned char)c->in.p[sizeof len];
	out->n = 0;
	s_add(out, c->in.p + FM_HDR, len);
	memmove(c->in.p, c->in.p + FM_HDR + len, c->in.n - FM_HDR - len);
	c->in.n -= FM_HDR + len;
	c->in.p[c->in.n] = 0;
	for (i = 0; i < want && c->fq.n; i++) {
		if (i < FM_MAXFD)
			fds[(*nf)++] = (int)(long)c->fq.p[0];
		else
			close((int)(long)c->fq.p[0]);
		memmove(c->fq.p, c->fq.p + 1, (c->fq.n - 1) * sizeof *c->fq.p);
		c->fq.n--;
	}
	if (i < want)
		lg(HIBR_LWRN, "fdmsg: a message said %d descriptors and %d came", want, i);
	return 1;
}

/* Wait for the channel to be readable, at most ms, through any signal. */
int fm_wait(fm_ch *c, int ms)
{
	struct pollfd q;
	long end = fm_ms() + ms, left;

	if (c->gone)
		return 1;
	for (;;) {
		left = end - fm_ms();
		if (left < 0)
			left = 0;
		q.fd = c->fd;
		q.events = POLLIN;
		q.revents = 0;
		if (poll(&q, 1, (int)left) == 1)
			return 1;
		if (errno != EINTR || left == 0)
			return 0;
	}
}

/* Close descriptors lo..hi, every one above lo when hi is negative. */
void fm_closerange(int lo, int hi)
{
	long max = sysconf(_SC_OPEN_MAX), fd;

#if defined(__linux__) && defined(SYS_close_range)
	if (syscall(SYS_close_range, (unsigned)lo, hi < 0 ? ~0u : (unsigned)hi, 0) == 0)
		return;
#endif
	if (max < 0 || max > 65536)
		max = 65536;
	for (fd = lo; fd < max && (hi < 0 || fd <= hi); fd++)
		close((int)fd);
}

/* Close everything above the standard three except the keep list. */
void fm_closeall(const int *keep, int n)
{
	int lo = 3, i, best;

	for (;;) {
		best = -1;
		for (i = 0; i < n; i++)
			if (keep[i] >= lo && (best < 0 || keep[i] < best))
				best = keep[i];
		if (best < 0) {
			fm_closerange(lo, -1);
			return;
		}
		if (best > lo)
			fm_closerange(lo, best - 1);
		lo = best + 1;
	}
}
