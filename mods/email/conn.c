#define _GNU_SOURCE

#include "ml.h"
#include <dirent.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <unistd.h>

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

/* Empty a string, truncated as well as counted, so nothing left from
   before reads as its value. */
void ml_clr(str *s)
{
	s->n = 0;
	if (s->p)
		s->p[0] = 0;
}

/* Give a descriptor the send and receive timeouts, close-on-exec, and no
   SIGPIPE where the system can say so per socket. */
void ml_fdprep(int fd, int tmo)
{
	struct timeval tv;

	tv.tv_sec = tmo;
	tv.tv_usec = 0;
	setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
	setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
#ifdef SO_NOSIGPIPE
	{
		int on = 1;

		setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof on);
	}
#endif
	fcntl(fd, F_SETFD, FD_CLOEXEC);
}

/* Connect to a host within the timeout, trying each address it has. */
int ml_sock(const char *host, const char *port, int tmo)
{
	struct addrinfo hint, *res, *a;
	struct pollfd pf;
	int fd = -1, rc, err = 0, fl;
	socklen_t el;

	memset(&hint, 0, sizeof hint);
	hint.ai_family = AF_UNSPEC;
	hint.ai_socktype = SOCK_STREAM;
	rc = getaddrinfo(host, port, &hint, &res);
	if (rc) {
		lg(HIBR_LERR, "email: %s: %s", host, gai_strerror(rc));
		return -1;
	}
	for (a = res; a; a = a->ai_next) {
		fd = socket(a->ai_family, a->ai_socktype, a->ai_protocol);
		if (fd < 0)
			continue;
		fl = fcntl(fd, F_GETFL);
		fcntl(fd, F_SETFL, fl | O_NONBLOCK);
		rc = connect(fd, a->ai_addr, a->ai_addrlen);
		if (rc < 0 && errno == EINPROGRESS) {
			pf.fd = fd;
			pf.events = POLLOUT;
			rc = poll(&pf, 1, tmo * 1000);
			if (rc == 1) {
				el = sizeof err;
				getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &el);
				rc = err ? -1 : 0;
				errno = err;
			} else {
				errno = rc == 0 || !errno ? ETIMEDOUT : errno;
				rc = -1;
			}
		}
		if (rc == 0) {
			fcntl(fd, F_SETFL, fl);
			break;
		}
		err = errno;
		close(fd);
		fd = -1;
		errno = err;
	}
	freeaddrinfo(res);
	if (fd < 0) {
		lg(HIBR_LERR, "email: cannot reach %s:%s: %s", host, port,
		   strerror(errno ? errno : ETIMEDOUT));
		return -1;
	}
	fd = fd_high(fd);
	ml_fdprep(fd, tmo);
	return fd;
}

/* In a freshly forked relay: close every descriptor but the two it needs
   and the standard three. */
void ml_closerest(int a, int b)
{
	DIR *d;
	struct dirent *e;
	vec fds = { 0, 0, 0 };
	size_t i;
	long fd;
	int dfd;

	d = opendir("/dev/fd");
	if (!d)
		return;
	dfd = dirfd(d);
	while ((e = readdir(d))) {
		if (e->d_name[0] < '0' || e->d_name[0] > '9')
			continue;
		fd = atol(e->d_name);
		if (fd > 2 && fd != a && fd != b && fd != dfd)
			v_add(&fds, (void *)fd);
	}
	closedir(d);
	for (i = 0; i < fds.n; i++)
		close((int)(long)fds.p[i]);
	v_free(&fds);
}

/* Wrap a connection's socket in TLS: the shell's own relay, run in a child
   of ours so its exit can be collected. Whatever was read before the
   handshake has to have been used up -- STARTTLS waits for that. */
int ml_tls(ml_conn *c)
{
	void (*relay)(int, int, const char *);
	int (*load)(void);
	int sp[2], sock = c->fd;
	pid_t pid;

	relay = (void (*)(int, int, const char *))dlsym(RTLD_DEFAULT, "tls_relay");
	load = (int (*)(void))dlsym(RTLD_DEFAULT, "tls_load");
	if (!relay || !load) {
		lg(HIBR_LERR, "email: this hibr was built without TLS");
		return HIBR_FAIL;
	}
	if (load() != HIBR_OK)
		return HIBR_FAIL;
	if (socketpair(AF_UNIX, SOCK_STREAM, 0, sp) < 0) {
		lg(HIBR_LERR, "email: socketpair: %s", strerror(errno));
		return HIBR_FAIL;
	}
	fflush(0);
	pid = fork();
	if (pid < 0) {
		lg(HIBR_LERR, "email: fork: %s", strerror(errno));
		close(sp[0]);
		close(sp[1]);
		return HIBR_FAIL;
	}
	if (pid == 0) {
		close(sp[0]);
		ml_closerest(sp[1], sock);
		signal(SIGINT, SIG_IGN);
		signal(SIGPIPE, SIG_DFL);
		fcntl(sock, F_SETFL, fcntl(sock, F_GETFL) & ~O_NONBLOCK);
		if (c->noverify)
			setenv("HIBR_TLS_INSECURE", "1", 1);
		else
			unsetenv("HIBR_TLS_INSECURE");
		relay(sp[1], sock, c->host);
		_exit(0);
	}
	close(sp[1]);
	close(sock);
	c->relay = pid;
	c->tls = 1;
	c->fd = fd_high(sp[0]);
	ml_fdprep(c->fd, c->tmo);
	lg(HIBR_LDBG, "email: tls relay for %s on pid %ld", c->host, (long)pid);
	return HIBR_OK;
}

/* Connect to a server: TLS from the start, or plain to be upgraded with
   STARTTLS by the protocol, or plain. */
ml_conn *ml_dial(const char *host, const char *port, int sec, int noverify, int tmo)
{
	ml_conn *c;
	int fd = ml_sock(host, port, tmo);

	if (fd < 0)
		return 0;
	c = xm(sizeof *c);
	memset(c, 0, sizeof *c);
	c->fd = fd;
	c->host = xs(host);
	c->noverify = noverify;
	c->tmo = tmo;
	c->owner = getpid();
	s_init(&c->rb);
	if (sec == ML_TLS && ml_tls(c) != HIBR_OK) {
		ml_close(c);
		return 0;
	}
	return c;
}

/* Upgrade a plain connection to TLS after the protocol's STARTTLS. */
int ml_starttls(ml_conn *c)
{
	if (c->rp < c->rb.n) {
		lg(HIBR_LERR, "email: %s sent more than it should before TLS; not trusting it",
		   c->host);
		return HIBR_FAIL;
	}
	return ml_tls(c);
}

/* Close a connection and collect its relay, if this process started it. */
void ml_close(ml_conn *c)
{
	if (!c)
		return;
	if (c->fd >= 0)
		close(c->fd);
	if (c->relay > 0 && c->owner == getpid()) {
		kill(c->relay, SIGTERM);
		while (waitpid(c->relay, 0, 0) < 0 && errno == EINTR)
			;
	}
	free(c->host);
	s_free(&c->rb);
	free(c);
}

/* Write everything, or fail. */
int ml_wall(ml_conn *c, const char *p, size_t n)
{
	ssize_t w;

	while (n) {
		w = send(c->fd, p, n, MSG_NOSIGNAL);
		if (w < 0 && errno == ENOTSOCK)
			w = write(c->fd, p, n);
		if (w < 0 && errno == EINTR)
			continue;
		if (w <= 0) {
			lg(HIBR_LERR, "email: %s: the connection broke while writing", c->host);
			return HIBR_FAIL;
		}
		p += w;
		n -= (size_t)w;
	}
	return HIBR_OK;
}

/* Read more into a connection's buffer: bytes read, 0 at the end, -1 on an
   error or a timeout. */
long ml_fill(ml_conn *c)
{
	ssize_t n;

	if (c->rp == c->rb.n) {
		ml_clr(&c->rb);
		c->rp = 0;
	} else if (c->rp > HIBR_IOCH) {
		memmove(c->rb.p, c->rb.p + c->rp, c->rb.n - c->rp);
		c->rb.n -= c->rp;
		c->rp = 0;
	}
	s_grow(&c->rb, HIBR_IOCH);
	do
		n = read(c->fd, c->rb.p + c->rb.n, HIBR_IOCH);
	while (n < 0 && errno == EINTR);
	if (n > 0)
		c->rb.n += (size_t)n;
	return (long)n;
}

/* One line, without its line ending; a line longer than a megabyte is
   refused. */
int ml_line(ml_conn *c, str *o)
{
	char *nl;
	size_t k;

	ml_clr(o);
	for (;;) {
		nl = c->rp < c->rb.n ? memchr(c->rb.p + c->rp, '\n', c->rb.n - c->rp) : 0;
		if (nl) {
			k = (size_t)(nl - (c->rb.p + c->rp));
			s_add(o, c->rb.p + c->rp, k);
			c->rp += k + 1;
			if (o->n && o->p[o->n - 1] == '\r')
				o->p[--o->n] = 0;
			if (!o->p)
				s_cat(o, "");
			return HIBR_OK;
		}
		s_add(o, c->rb.p + c->rp, c->rb.n - c->rp);
		c->rp = c->rb.n;
		if (o->n > 1024 * 1024) {
			lg(HIBR_LERR, "email: %s sent a line too long to be a reply", c->host);
			return HIBR_FAIL;
		}
		if (ml_fill(c) <= 0) {
			lg(HIBR_LERR, "email: %s closed the connection or timed out", c->host);
			return HIBR_FAIL;
		}
	}
}

/* Exactly n bytes, appended. */
int ml_bytes(ml_conn *c, size_t n, str *o)
{
	size_t k;

	while (n) {
		if (c->rp == c->rb.n && ml_fill(c) <= 0) {
			lg(HIBR_LERR, "email: %s closed the connection mid-message", c->host);
			return HIBR_FAIL;
		}
		k = c->rb.n - c->rp;
		if (k > n)
			k = n;
		s_add(o, c->rb.p + c->rp, k);
		c->rp += k;
		n -= k;
	}
	return HIBR_OK;
}

/* Whether something is there to read within so many milliseconds. */
int ml_ready(ml_conn *c, int ms)
{
	struct pollfd pf;
	int r;

	if (c->rp < c->rb.n)
		return 1;
	pf.fd = c->fd;
	pf.events = POLLIN;
	pf.revents = 0;
	do
		r = poll(&pf, 1, ms);
	while (r < 0 && errno == EINTR);
	return r > 0;
}
