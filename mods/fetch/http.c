#define _GNU_SOURCE

#include "ft.h"
#include "../inflate.h"
#include <dirent.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

int fd_high(int fd);

vec ft_conns;

/* Milliseconds on a clock that only goes forward. */
long long ft_now(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

/* The timeout, in seconds: the request's, HIBR_FETCH_TIMEOUT's, or 30. */
int ft_timeout(const ft_req *rq)
{
	const char *v = getenv("HIBR_FETCH_TIMEOUT");
	int n;

	if (rq && rq->timeout > 0)
		return rq->timeout;
	n = v ? atoi(v) : 0;
	return n > 0 ? n : FT_TIMEOUT;
}

/* Whether the request has been told to stop. */
int ft_stopped(ft_job *j)
{
	if (!j->cancelled && j->rq->cancel && *j->rq->cancel)
		j->cancelled = 1;
	return j->cancelled;
}

/* Say why a request failed, once: the first reason is the one that
   matters, and what follows it is usually its consequence. */
void ft_fail(ft_job *j, const char *f, ...)
{
	va_list ap;
	int n;

	if (j->err.n)
		return;
	va_start(ap, f);
	n = vsnprintf(0, 0, f, ap);
	va_end(ap);
	if (n <= 0)
		return;
	s_grow(&j->err, (size_t)n);
	va_start(ap, f);
	vsnprintf(j->err.p + j->err.n, (size_t)n + 1, f, ap);
	va_end(ap);
	j->err.n += (size_t)n;
}

/* Wait for fd to be ready, a tick at a time so a cancel is seen, until
   tmo seconds pass with nothing: 1 ready, 0 timed out, -1 cancelled. */
int ft_wait(ft_job *j, int fd, short ev)
{
	struct pollfd pf;
	long long until = ft_now() + (long long)j->tmo * 1000;
	int r;

	for (;;) {
		if (ft_stopped(j))
			return -1;
		pf.fd = fd;
		pf.events = ev;
		pf.revents = 0;
		r = poll(&pf, 1, FT_TICK);
		if (r > 0)
			return 1;
		if (r < 0 && errno != EINTR)
			return 1;
		if (ft_now() >= until)
			return 0;
	}
}

/* Connect to a host within the timeout, trying each address it has. */
int ft_dial(ft_job *j, const char *host, const char *port)
{
	struct addrinfo hint, *res, *a;
	int fd = -1, rc, err = 0, fl, w;
	socklen_t el;

	memset(&hint, 0, sizeof hint);
	hint.ai_family = AF_UNSPEC;
	hint.ai_socktype = SOCK_STREAM;
	rc = getaddrinfo(host, port, &hint, &res);
	if (rc) {
		ft_fail(j, "%s: %s", host, gai_strerror(rc));
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
			w = ft_wait(j, fd, POLLOUT);
			if (w == 1) {
				el = sizeof err;
				err = 0;
				getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &el);
				rc = err ? -1 : 0;
				errno = err;
			} else {
				errno = w == 0 ? ETIMEDOUT : ECANCELED;
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
		if (j->cancelled)
			break;
	}
	freeaddrinfo(res);
	if (fd < 0) {
		if (!j->cancelled)
			ft_fail(j, "cannot reach %s: %s", host,
				strerror(errno ? errno : ETIMEDOUT));
		return -1;
	}
	fd = fd_high(fd);
	fcntl(fd, F_SETFD, FD_CLOEXEC);
#ifdef SO_NOSIGPIPE
	{
		int on = 1;

		setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof on);
	}
#endif
	return fd;
}

/* In a freshly forked relay: close every descriptor but the two it needs
   and the standard three, so it holds nobody else's terminal or pipe. */
void ft_closerest(int a, int b)
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

/* Wrap a connected socket in TLS: the shell's own relay, run in a child of
   ours so its exit can be collected, told to skip the certificate check
   only when the request says so. */
int ft_tls(ft_job *j, ft_conn *c, int sock)
{
	void (*relay)(int, int, const char *);
	int (*load)(void);
	int sp[2];
	pid_t pid;

	relay = (void (*)(int, int, const char *))dlsym(RTLD_DEFAULT,
							   "tls_relay");
	load = (int (*)(void))dlsym(RTLD_DEFAULT, "tls_load");
	if (!relay || !load) {
		ft_fail(j, "this hibr was built without TLS");
		close(sock);
		return -1;
	}
	if (load() != HIBR_OK) {
		ft_fail(j, "TLS is not available: libssl could not be loaded");
		close(sock);
		return -1;
	}
	if (socketpair(AF_UNIX, SOCK_STREAM, 0, sp) < 0) {
		ft_fail(j, "socketpair: %s", strerror(errno));
		close(sock);
		return -1;
	}
	fflush(0);
	pid = fork();
	if (pid < 0) {
		ft_fail(j, "fork: %s", strerror(errno));
		close(sock);
		close(sp[0]);
		close(sp[1]);
		return -1;
	}
	if (pid == 0) {
		close(sp[0]);
		ft_closerest(sp[1], sock);
		signal(SIGINT, SIG_IGN);
		signal(SIGPIPE, SIG_DFL);
		fcntl(sock, F_SETFL, fcntl(sock, F_GETFL) & ~O_NONBLOCK);
		if (c->noverify)
			setenv("HIBR_TLS_INSECURE", "1", 1);
		relay(sp[1], sock, c->host);
		_exit(0);
	}
	close(sp[1]);
	close(sock);
	c->relay = pid;
	c->fd = fd_high(sp[0]);
	fcntl(c->fd, F_SETFD, FD_CLOEXEC);
	return c->fd;
}

/* Close a connection and collect its relay, if this process started it. */
void ft_conndrop(ft_conn *c)
{
	size_t i;

	for (i = 0; i < ft_conns.n; i++) {
		if (ft_conns.p[i] != c)
			continue;
		ft_conns.p[i] = ft_conns.p[--ft_conns.n];
		break;
	}
	if (c->fd >= 0)
		close(c->fd);
	if (c->relay > 0 && c->owner == getpid()) {
		kill(c->relay, SIGTERM);
		while (waitpid(c->relay, 0, 0) < 0 && errno == EINTR)
			;
	}
	free(c->key);
	free(c->host);
	s_free(&c->rb);
	free(c);
}

/* Close every connection. */
void ft_closeall(void)
{
	while (ft_conns.n)
		ft_conndrop(ft_conns.p[ft_conns.n - 1]);
	v_free(&ft_conns);
}

/* Whether a kept connection has been closed by the other end, or has
   something unasked-for waiting, either of which makes it useless. */
int ft_stale(ft_conn *c)
{
	struct pollfd pf;

	if (c->rp < c->rb.n)
		return 1;
	pf.fd = c->fd;
	pf.events = POLLIN;
	pf.revents = 0;
	return poll(&pf, 1, 0) != 0;
}

/* Write everything, or fail. */
int ft_wall(ft_job *j, int fd, const char *p, size_t n)
{
	ssize_t w;

	while (n) {
		if (ft_wait(j, fd, POLLOUT) != 1)
			return HIBR_FAIL;
		w = send(fd, p, n, MSG_NOSIGNAL | MSG_DONTWAIT);
		if (w < 0 && errno == ENOTSOCK)
			w = write(fd, p, n);
		if (w < 0 && (errno == EINTR || errno == EAGAIN ||
			      errno == EWOULDBLOCK))
			continue;
		if (w <= 0)
			return HIBR_FAIL;
		p += w;
		n -= (size_t)w;
	}
	return HIBR_OK;
}

/* Read more from a connection into its buffer: bytes read, 0 at the end,
   -1 on an error, a timeout or a cancel. */
long ft_fill(ft_job *j, ft_conn *c)
{
	ssize_t n;
	int w;

	if (c->rp == c->rb.n) {
		c->rb.n = 0;
		c->rp = 0;
	} else if (c->rp > HIBR_IOCH) {
		memmove(c->rb.p, c->rb.p + c->rp, c->rb.n - c->rp);
		c->rb.n -= c->rp;
		c->rp = 0;
	}
	s_grow(&c->rb, HIBR_IOCH);
	w = ft_wait(j, c->fd, POLLIN);
	if (w != 1) {
		if (w == 0)
			c->timedout = 1;
		return -1;
	}
	do
		n = read(c->fd, c->rb.p + c->rb.n, HIBR_IOCH);
	while (n < 0 && errno == EINTR);
	if (n > 0)
		c->rb.n += (size_t)n;
	return (long)n;
}

/* One line of a reply, without its line ending. */
int ft_line(ft_job *j, ft_conn *c, str *o)
{
	char *nl;
	size_t k;

	o->n = 0;
	for (;;) {
		nl = c->rp < c->rb.n ?
			     memchr(c->rb.p + c->rp, '\n', c->rb.n - c->rp) : 0;
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
		if (o->n > 65536 || ft_fill(j, c) <= 0)
			return HIBR_FAIL;
	}
}

/* Hand decoded body bytes on: to ondata, or into the reply. */
int ft_deliver(ft_job *j, ft_res *rs, const char *p, size_t n)
{
	if (!n)
		return HIBR_OK;
	rs->length += (long long)n;
	if (j->rq->ondata) {
		if (j->rq->ondata(j->rq->ctx, p, n)) {
			j->cancelled = 1;
			return HIBR_FAIL;
		}
		return HIBR_OK;
	}
	if (rs->body.n + n > (size_t)FT_MAXBODY) {
		ft_fail(j, "a reply larger than %ld bytes", (long)FT_MAXBODY);
		return HIBR_FAIL;
	}
	s_add(&rs->body, p, n);
	return HIBR_OK;
}

/* Where body bytes go as they arrive: nowhere, for a reply that is only
   passed through on the way to another; held whole, when compressed; or
   on, as they come. */
int ft_sink(ft_job *j, ft_res *rs, int keep, const char *p, size_t n)
{
	if (!keep)
		return HIBR_OK;
	if (ft_stopped(j))
		return HIBR_FAIL;
	if (j->decode) {
		if (j->zbuf.n + n > (size_t)FT_MAXBODY) {
			ft_fail(j, "a reply larger than %ld bytes", (long)FT_MAXBODY);
			return HIBR_FAIL;
		}
		s_add(&j->zbuf, p, n);
		return HIBR_OK;
	}
	return ft_deliver(j, rs, p, n);
}

/* Pass n bytes of the body on, or all of it to the end when n is -1. */
int ft_body(ft_job *j, ft_conn *c, ft_res *rs, int keep, long long n)
{
	size_t k;
	long r;

	while (n != 0) {
		if (c->rp == c->rb.n) {
			r = ft_fill(j, c);
			if (r < 0)
				return HIBR_FAIL;
			if (r == 0)
				return n < 0 ? HIBR_OK : HIBR_FAIL;
		}
		k = c->rb.n - c->rp;
		if (n > 0 && (long long)k > n)
			k = (size_t)n;
		if (ft_sink(j, rs, keep, c->rb.p + c->rp, k) != HIBR_OK)
			return HIBR_FAIL;
		c->rp += k;
		if (n > 0)
			n -= (long long)k;
	}
	return HIBR_OK;
}

/* Whether a header line names the header asked for, and its value. */
const char *ft_hval(const char *line, const char *name)
{
	size_t n = strlen(name);

	if (strncasecmp(line, name, n) || line[n] != ':')
		return 0;
	line += n + 1;
	while (*line == ' ' || *line == '\t')
		line++;
	return line;
}

/* The value of the first header of that name in a reply, or 0. */
const char *ft_header(const ft_res *rs, const char *name)
{
	size_t i;
	const char *v;

	for (i = 0; i < rs->hdr.n; i++)
		if ((v = ft_hval(rs->hdr.p[i], name)))
			return v;
	return 0;
}

/* Decode a compressed body once all of it is in, and pass it on. */
int ft_decoded(ft_job *j, ft_res *rs)
{
	str out;
	int ok;
	size_t i;

	if (!j->decode)
		return HIBR_OK;
	s_init(&out);
	if (j->decode == 1)
		ok = inf_gzip((const unsigned char *)j->zbuf.p, j->zbuf.n,
			      (size_t)FT_MAXBODY, &out, 0);
	else
		ok = inf_zlib((const unsigned char *)j->zbuf.p, j->zbuf.n,
			      (size_t)FT_MAXBODY, &out, 0) ||
		     inf_raw((const unsigned char *)j->zbuf.p, j->zbuf.n,
			     (size_t)FT_MAXBODY, &out, 0);
	j->zbuf.n = 0;
	if (!ok && j->zbuf.p) {
		s_free(&out);
		ft_fail(j, "the reply's %s body could not be decoded",
			j->decode == 1 ? "gzip" : "deflate");
		return HIBR_FAIL;
	}
	for (i = 0; i < out.n; i += HIBR_IOCH) {
		if (ft_deliver(j, rs, out.p + i,
			       out.n - i > HIBR_IOCH ? HIBR_IOCH : out.n - i) !=
		    HIBR_OK) {
			s_free(&out);
			return HIBR_FAIL;
		}
	}
	s_free(&out);
	return HIBR_OK;
}

/* Read a reply's status and headers into rs: every header kept, and the
   framing noted. got says whether anything arrived, so a dead kept
   connection can be told from a bad reply. */
int ft_head(ft_job *j, ft_conn *c, ft_res *rs, int *chunked, long long *len,
	    int *keep, int *got)
{
	str l;
	const char *v;
	char *end;
	size_t i;

	*got = 0;
	s_init(&l);
	for (;;) {
		if (ft_line(j, c, &l) != HIBR_OK) {
			*got = l.n > 0;
			goto bad;
		}
		*got = 1;
		if (!l.n)
			continue;
		if (strncmp(l.p, "HTTP/", 5) || !strchr(l.p, ' ')) {
			ft_fail(j, "%s answered with something that is not "
				   "HTTP", c->host);
			goto bad;
		}
		rs->code = atoi(strchr(l.p, ' ') + 1);
		*keep = strncmp(l.p, "HTTP/1.0", 8) != 0;
		v = strchr(strchr(l.p, ' ') + 1, ' ');
		free(rs->reason);
		rs->reason = xs(v ? v + 1 : "");
		for (i = 0; i < rs->hdr.n; i++)
			free(rs->hdr.p[i]);
		rs->hdr.n = 0;
		*chunked = 0;
		*len = -1;
		for (;;) {
			if (ft_line(j, c, &l) != HIBR_OK)
				goto bad;
			if (!l.n)
				break;
			v_add(&rs->hdr, xs(l.p));
			if ((v = ft_hval(l.p, "content-length")))
				*len = strtoll(v, &end, 10);
			else if ((v = ft_hval(l.p, "transfer-encoding")))
				*chunked = strcasestr(v, "chunked") != 0;
			else if ((v = ft_hval(l.p, "connection"))) {
				if (strcasestr(v, "close"))
					*keep = 0;
				else if (strcasestr(v, "keep-alive"))
					*keep = 1;
			}
		}
		if (rs->code >= 200 || rs->code == 101)
			break;
	}
	free(rs->ctype);
	rs->ctype = (v = ft_header(rs, "content-type")) ? xs(v) : 0;
	s_free(&l);
	return HIBR_OK;
bad:
	s_free(&l);
	return HIBR_FAIL;
}

/* Read a reply's body, framed by chunks, a length or the end of the
   connection; keep says whether it is wanted or only passed through. */
int ft_rbody(ft_job *j, ft_conn *c, const char *method, ft_res *rs, int keep,
	     int chunked, long long len, int *keepalive)
{
	str cl;
	long long n;
	char *end;

	s_init(&cl);
	if (!strcmp(method, "HEAD") || rs->code == 204 || rs->code == 304 ||
	    (rs->code >= 100 && rs->code < 200)) {
		;
	} else if (chunked) {
		for (;;) {
			if (ft_line(j, c, &cl) != HIBR_OK)
				goto bad;
			n = strtoll(cl.p ? cl.p : "", &end, 16);
			if (end == cl.p || n < 0)
				goto bad;
			if (n == 0) {
				do {
					if (ft_line(j, c, &cl) != HIBR_OK)
						goto bad;
				} while (cl.n);
				break;
			}
			if (ft_body(j, c, rs, keep, n) != HIBR_OK ||
			    ft_line(j, c, &cl) != HIBR_OK)
				goto bad;
		}
	} else if (len >= 0) {
		if (ft_body(j, c, rs, keep, len) != HIBR_OK)
			goto bad;
	} else {
		if (ft_body(j, c, rs, keep, -1) != HIBR_OK)
			goto bad;
		*keepalive = 0;
	}
	s_free(&cl);
	return HIBR_OK;
bad:
	s_free(&cl);
	return HIBR_FAIL;
}

/* An empty reply. */
void ft_resinit(ft_res *r)
{
	memset(r, 0, sizeof *r);
	s_init(&r->body);
}

/* Free a reply. */
void ft_resfree(ft_res *r)
{
	size_t i;

	s_free(&r->body);
	free(r->reason);
	free(r->url);
	free(r->ctype);
	free(r->err);
	for (i = 0; i < r->hdr.n; i++)
		free(r->hdr.p[i]);
	v_free(&r->hdr);
	ft_resinit(r);
}

/* Whether host is named by a no_proxy list: a name, a suffix after a dot,
   or * for every host. */
int ft_noproxy(const char *host)
{
	const char *np = getenv("no_proxy"), *p, *e;
	size_t hl = strlen(host), k;

	if (!np)
		np = getenv("NO_PROXY");
	if (!np)
		return 0;
	for (p = np; *p; p = *e ? e + 1 : e) {
		while (*p == ' ' || *p == '.')
			p++;
		e = p + strcspn(p, ",");
		k = (size_t)(e - p);
		while (k && p[k - 1] == ' ')
			k--;
		if (k == 1 && *p == '*')
			return 1;
		if (!k || k > hl)
			continue;
		if (!strncasecmp(host + hl - k, p, k) &&
		    (k == hl || host[hl - k - 1] == '.'))
			return 1;
	}
	return 0;
}

/* The proxy for an address, from https_proxy or http_proxy, split; 0 when
   there is none or the host is excluded. */
int ft_proxy(const ft_url *u, ft_url *px)
{
	const char *v;
	str err;
	int ok;

	if (ft_noproxy(u->host.p))
		return 0;
	v = getenv(u->tls ? "https_proxy" : "http_proxy");
	if (!v || !*v)
		v = getenv(u->tls ? "HTTPS_PROXY" : "HTTP_PROXY");
	if (!v || !*v)
		return 0;
	s_init(&err);
	memset(px, 0, sizeof *px);
	if (strncasecmp(v, "http://", 7) && strncasecmp(v, "https://", 8)) {
		str full;

		s_init(&full);
		s_cat(&full, "http://");
		s_cat(&full, v);
		ok = ft_urlsplit(full.p, px, &err) == HIBR_OK;
		s_free(&full);
	} else {
		ok = ft_urlsplit(v, px, &err) == HIBR_OK;
	}
	s_free(&err);
	if (!ok)
		ft_urlfree(px);
	return ok;
}

/* Ask an http proxy to open a tunnel to an https address. */
int ft_connect(ft_job *j, ft_conn *c, const ft_url *u)
{
	str rq, l;
	int code = 0;

	s_init(&rq);
	s_init(&l);
	s_fmt(&rq, "CONNECT %s:%s HTTP/1.1\r\nHost: %s:%s\r\n\r\n", u->host.p,
	      u->port.p, u->host.p, u->port.p);
	if (ft_wall(j, c->fd, rq.p, rq.n) != HIBR_OK ||
	    ft_line(j, c, &l) != HIBR_OK) {
		ft_fail(j, "the proxy did not answer a tunnel to %s",
			u->host.p);
		goto out;
	}
	if (!strncmp(l.p, "HTTP/", 5) && strchr(l.p, ' '))
		code = atoi(strchr(l.p, ' ') + 1);
	while (l.n && ft_line(j, c, &l) == HIBR_OK && l.n)
		;
	if (code < 200 || code > 299)
		ft_fail(j, "the proxy refused a tunnel to %s: %s", u->host.p,
			l.p ? l.p : "");
out:
	s_free(&rq);
	s_free(&l);
	return code >= 200 && code <= 299 ? HIBR_OK : HIBR_FAIL;
}

/* A connection to the place an address names, through a proxy where one
   is set: the one kept from before if it is still good, else a new one.
   A child of the process that opened a kept one never uses it -- the two
   would talk over each other. */
ft_conn *ft_connget(ft_job *j, const ft_url *u, int noverify, int *viaproxy)
{
	str key;
	size_t i;
	ft_conn *c = 0;
	ft_url px;
	int sock, haspx;

	memset(&px, 0, sizeof px);
	haspx = ft_proxy(u, &px);
	*viaproxy = haspx && !u->tls;
	s_init(&key);
	s_fmt(&key, "%d %s %s %c", u->tls, u->host.p, u->port.p,
	      noverify ? 'k' : 'v');
	if (haspx)
		s_fmt(&key, " via %s:%s", px.host.p, px.port.p);
	for (i = 0; i < ft_conns.n; i++) {
		c = ft_conns.p[i];
		if (c->owner != getpid()) {
			close(c->fd);
			c->fd = -1;
			ft_conndrop(c);
			i = (size_t)-1;
			c = 0;
			continue;
		}
		if (!strcmp(c->key, key.p))
			break;
		c = 0;
	}
	if (c && ft_stale(c)) {
		ft_conndrop(c);
		c = 0;
	}
	if (c) {
		c->reused = 1;
		s_free(&key);
		if (haspx)
			ft_urlfree(&px);
		return c;
	}
	sock = haspx ? ft_dial(j, px.host.p, px.port.p) :
		       ft_dial(j, u->host.p, u->port.p);
	if (sock < 0) {
		s_free(&key);
		if (haspx)
			ft_urlfree(&px);
		return 0;
	}
	c = xm(sizeof *c);
	memset(c, 0, sizeof *c);
	c->key = xs(key.p);
	c->host = xs(u->host.p);
	c->tls = u->tls;
	c->noverify = noverify;
	c->proxied = *viaproxy;
	c->owner = getpid();
	c->fd = sock;
	s_init(&c->rb);
	s_free(&key);
	if (haspx && u->tls && ft_connect(j, c, u) != HIBR_OK) {
		ft_urlfree(&px);
		close(c->fd);
		c->fd = -1;
		free(c->key);
		free(c->host);
		s_free(&c->rb);
		free(c);
		return 0;
	}
	if (haspx)
		ft_urlfree(&px);
	if (u->tls) {
		c->rb.n = 0;
		c->rp = 0;
		if (ft_tls(j, c, sock) < 0) {
			c->fd = -1;
			free(c->key);
			free(c->host);
			s_free(&c->rb);
			free(c);
			return 0;
		}
	}
	v_add(&ft_conns, c);
	return c;
}

/* Send a request's body: from memory, or from its descriptor, from the
   offset it was at when the request began. */
int ft_sendbody(ft_job *j, ft_conn *c)
{
	const ft_req *rq = j->rq;
	char *buf;
	long long left;
	ssize_t n;
	int ok = HIBR_OK;

	if (!j->sendbody)
		return HIBR_OK;
	if (rq->bfd < 0)
		return rq->blen ? ft_wall(j, c->fd, rq->body, rq->blen) : HIBR_OK;
	if (j->bstart >= 0 && lseek(rq->bfd, j->bstart, SEEK_SET) < 0) {
		ft_fail(j, "the body cannot be sent again: %s",
			strerror(errno));
		return HIBR_FAIL;
	}
	buf = xm(HIBR_IOCH);
	left = rq->bflen;
	while (left > 0) {
		n = read(rq->bfd, buf, left > HIBR_IOCH ? HIBR_IOCH : (size_t)left);
		if (n < 0 && errno == EINTR)
			continue;
		if (n <= 0 || ft_wall(j, c->fd, buf, (size_t)n) != HIBR_OK) {
			ok = HIBR_FAIL;
			break;
		}
		left -= n;
	}
	free(buf);
	return ok;
}

/* Whether a caller's header is one fetch writes itself, or one that must
   not follow the request to another origin. */
int ft_hdris(const char *h, const char *name)
{
	size_t n = strlen(name);

	return !strncasecmp(h, name, n) && h[n] == ':';
}

/* The request's head: the request line, the headers fetch writes, the
   caller's own, a login and cookies. */
void ft_reqhead(ft_job *j, const ft_url *u, int viaproxy, int crossed,
		ft_auth *a, str *h)
{
	const ft_req *rq = j->rq;
	size_t i;
	int ua = 0, ae = 0, acc = 0;
	str ck;

	h->n = 0;
	s_cat(h, j->method);
	s_ch(h, ' ');
	if (viaproxy)
		ft_urlstr(u, h);
	else
		s_cat(h, u->path.p);
	s_cat(h, " HTTP/1.1\r\nHost: ");
	if (strchr(u->host.p, ':')) {
		s_ch(h, '[');
		s_cat(h, u->host.p);
		s_ch(h, ']');
	} else {
		s_cat(h, u->host.p);
	}
	if (strcmp(u->port.p, u->tls ? "443" : "80")) {
		s_ch(h, ':');
		s_cat(h, u->port.p);
	}
	s_cat(h, "\r\n");
	for (i = 0; i < rq->nhdr; i++) {
		const char *x = rq->hdr[i];

		if (!x || !strchr(x, ':') || ft_hdris(x, "host") ||
		    ft_hdris(x, "content-length") ||
		    ft_hdris(x, "transfer-encoding"))
			continue;
		if (crossed && (ft_hdris(x, "authorization") ||
				ft_hdris(x, "cookie") ||
				ft_hdris(x, "proxy-authorization")))
			continue;
		if (ft_hdris(x, "user-agent"))
			ua = 1;
		if (ft_hdris(x, "accept-encoding"))
			ae = 1;
		if (ft_hdris(x, "accept"))
			acc = 1;
		s_cat(h, x);
		s_cat(h, "\r\n");
	}
	if (!ua)
		s_cat(h, "User-Agent: hibr-fetch/" HIBR_VER "\r\n");
	if (!acc)
		s_cat(h, "Accept: */*\r\n");
	if (!ae) {
		if (rq->flags & FT_IDENTITY)
			s_cat(h, "Accept-Encoding: identity\r\n");
		else
			s_cat(h, "Accept-Encoding: gzip, deflate\r\n");
	}
	if (a && !crossed)
		ft_authhdr(a, rq->user, rq->pass ? rq->pass : "", j->method,
			   u->path.p, h);
	if (rq->jar && !(rq->flags & FT_NOCOOKIE)) {
		s_init(&ck);
		ft_cookiehdr(rq->jar, u, &ck);
		if (ck.n) {
			s_cat(h, "Cookie: ");
			s_cat(h, ck.p);
			s_cat(h, "\r\n");
		}
		s_free(&ck);
	}
	if (j->sendbody) {
		s_cat(h, "Content-Length: ");
		s_fmt(h, "%lld", rq->bfd >= 0 ? rq->bflen : (long long)rq->blen);
		s_cat(h, "\r\n");
	}
	s_cat(h, "\r\n");
}

/* Keep what a reply's Set-Cookie headers say, in the request's jar. */
void ft_keepcookies(ft_job *j, const ft_url *u, ft_res *rs)
{
	size_t i;
	const char *v;
	int any = 0;

	if (!j->rq->jar || (j->rq->flags & FT_NOCOOKIE))
		return;
	for (i = 0; i < rs->hdr.n; i++)
		if ((v = ft_hval(rs->hdr.p[i], "set-cookie"))) {
			ft_cookieset(j->rq->jar, u, v);
			any = 1;
		}
	if (any)
		ft_jarsave(j->rq->jar);
}

/* Which content coding a reply's body is in: 0 none, 1 gzip, 2 deflate. */
int ft_coding(ft_res *rs)
{
	const char *v = ft_header(rs, "content-encoding");

	if (!v)
		return 0;
	if (!strncasecmp(v, "gzip", 4) || !strncasecmp(v, "x-gzip", 6))
		return 1;
	if (!strncasecmp(v, "deflate", 7))
		return 2;
	return 0;
}

/* Do a request: connect or reuse, log in as the server asks, follow
   redirects, retry once on a kept connection that turned out dead. 0 when
   a reply came, -1 when none did; rs->err says why. */
int ft_do(const ft_req *rq, ft_res *rs)
{
	ft_job j;
	str url, head, key, nu;
	ft_url u;
	ft_conn *c;
	ft_auth *a;
	int redirects = 0, tries = 0, retried = 0, got, stale, rc = -1;
	int chunked, keep, follow, retry, viaproxy, crossed = 0, maxr;
	long long len;

	memset(&j, 0, sizeof j);
	j.rq = rq;
	j.method = rq->method && *rq->method ? rq->method : "GET";
	j.sendbody = rq->bfd >= 0 || rq->blen || !strcmp(j.method, "POST") ||
		     !strcmp(j.method, "PUT") || !strcmp(j.method, "PATCH");
	j.bstart = rq->bfd >= 0 ? lseek(rq->bfd, 0, SEEK_CUR) : -1;
	j.tmo = ft_timeout(rq);
	maxr = rq->maxredirects > 0 ? rq->maxredirects : FT_REDIRECTS;
	s_init(&j.err);
	s_init(&j.zbuf);
	s_init(&url);
	s_init(&head);
	s_init(&key);
	s_init(&nu);
	ft_resinit(rs);
	if (!rq->url || !*rq->url) {
		ft_fail(&j, "no address given");
		goto done;
	}
	if (rq->jar && !ft_jarok(rq->jar)) {
		ft_fail(&j, "%s is not a cookie jar's name: letters, digits, "
			    "dots, dashes and underscores", rq->jar);
		goto done;
	}
	s_cat(&url, rq->url);
	for (;;) {
		memset(&u, 0, sizeof u);
		if (ft_urlsplit(url.p, &u, &j.err) != HIBR_OK)
			break;
		a = 0;
		if (rq->user && *rq->user && !crossed) {
			key.n = 0;
			s_fmt(&key, "%d %s %s %s", u.tls, u.host.p, u.port.p,
			      rq->user);
			a = ft_authfind(key.p);
		}
		c = ft_connget(&j, &u, (rq->flags & FT_NOVERIFY) != 0, &viaproxy);
		if (!c) {
			ft_urlfree(&u);
			break;
		}
		ft_reqhead(&j, &u, viaproxy, crossed, a, &head);
		lg(HIBR_LDBG, "fetch: %s %s%s", j.method, u.host.p, u.path.p);
		if (ft_wall(&j, c->fd, head.p, head.n) != HIBR_OK ||
		    ft_sendbody(&j, c) != HIBR_OK) {
			got = 0;
			goto lost;
		}
		if (ft_head(&j, c, rs, &chunked, &len, &keep, &got) != HIBR_OK)
			goto lost;
		ft_keepcookies(&j, &u, rs);
		retry = rs->code == 401 && a && tries < 2;
		if (retry) {
			retry = ft_authparse(a, &rs->hdr, &stale) &&
				(tries == 0 || stale);
		}
		follow = !retry && !(rq->flags & FT_NOFOLLOW) &&
			 redirects < maxr && ft_header(rs, "location") &&
			 (rs->code == 301 || rs->code == 302 ||
			  rs->code == 303 || rs->code == 307 || rs->code == 308);
		j.decode = retry || follow ? 0 : ft_coding(rs);
		if (!retry && !follow) {
			free(rs->url);
			nu.n = 0;
			ft_urlstr(&u, &nu);
			rs->url = xs(nu.p);
			nu.n = 0;
			rs->redirects = redirects;
			if (rq->onhead && rq->onhead(rq->ctx, rs)) {
				j.cancelled = 1;
				ft_conndrop(c);
				ft_urlfree(&u);
				break;
			}
		}
		if (ft_rbody(&j, c, j.method, rs, !retry && !follow, chunked, len,
			     &keep) != HIBR_OK) {
			ft_conndrop(c);
			if (!j.cancelled && !j.err.n)
				ft_fail(&j, "%s stopped part way through its "
					    "answer", u.host.p);
			ft_urlfree(&u);
			break;
		}
		if (!keep)
			ft_conndrop(c);
		if (retry) {
			tries++;
			ft_urlfree(&u);
			continue;
		}
		if (follow) {
			ft_resolve(url.p, ft_header(rs, "location"), &nu);
			if (!ft_sameorigin(url.p, nu.p))
				crossed = 1;
			if (rs->code == 303 ||
			    ((rs->code == 301 || rs->code == 302) &&
			     !strcmp(j.method, "POST"))) {
				j.method = rs->code == 303 &&
						   !strcmp(j.method, "HEAD") ?
					   "HEAD" :
					   "GET";
				j.sendbody = 0;
			} else if (j.sendbody && rq->bfd >= 0 && j.bstart < 0) {
				ft_fail(&j, "a %d redirect needs the body sent "
					    "again, and it came from a pipe",
					rs->code);
				ft_urlfree(&u);
				break;
			}
			url.n = 0;
			s_cat(&url, nu.p);
			redirects++;
			ft_urlfree(&u);
			continue;
		}
		if (ft_decoded(&j, rs) != HIBR_OK) {
			ft_urlfree(&u);
			break;
		}
		ft_urlfree(&u);
		rc = 0;
		break;
lost:
		{
			int reused = c->reused, relay = c->relay > 0;
			int timedout = c->timedout;
			int st = 0;
			pid_t rp = c->relay;

			if (c->relay > 0 && c->owner == getpid()) {
				close(c->fd);
				c->fd = -1;
				if (waitpid(rp, &st, WNOHANG) == 0) {
					usleep(50000);
					if (waitpid(rp, &st, WNOHANG) == 0) {
						kill(rp, SIGTERM);
						while (waitpid(rp, 0, 0) < 0 &&
						       errno == EINTR)
							;
						st = 0;
					}
				}
				c->relay = 0;
			}
			ft_conndrop(c);
			if (!got && reused && !retried && !j.cancelled) {
				retried = 1;
				ft_urlfree(&u);
				continue;
			}
			if (j.cancelled)
				;
			else if (relay && !got && WIFEXITED(st) && WEXITSTATUS(st))
				ft_fail(&j, "the secure connection to %s failed "
					    "(its certificate?)",
					u.host.p);
			else if (timedout)
				ft_fail(&j, "%s sent nothing for %d seconds "
					    "(HIBR_FETCH_TIMEOUT)",
					u.host.p, j.tmo);
			else
				ft_fail(&j, "%s did not answer", u.host.p);
		}
		ft_urlfree(&u);
		rs->code = 0;
		break;
	}
done:
	if (j.cancelled) {
		j.err.n = 0;
		s_cat(&j.err, "cancelled");
		rc = -1;
	}
	if (rc != 0 && j.err.n) {
		free(rs->err);
		rs->err = xs(j.err.p);
	} else if (rc != 0) {
		free(rs->err);
		rs->err = xs("the request failed");
	}
	s_free(&j.err);
	s_free(&j.zbuf);
	s_free(&url);
	s_free(&head);
	s_free(&key);
	s_free(&nu);
	return rc;
}
