#define _GNU_SOURCE

#include "dv.h"
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
#include <strings.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

#ifndef DV_MAXBODY
#define DV_MAXBODY (256L * 1024 * 1024)
#endif

int fd_high(int fd);

vec dv_conns;
vec dv_auths;

/* Split an http or https address into its parts; the path stays as it is
   written, already encoded. */
int dv_urlsplit(const char *u, dv_url *o)
{
	const char *p, *e, *hp, *at, *c;

	s_init(&o->host);
	s_init(&o->port);
	s_init(&o->path);
	if (!strncasecmp(u, "https://", 8)) {
		o->tls = 1;
		p = u + 8;
	} else if (!strncasecmp(u, "http://", 7)) {
		o->tls = 0;
		p = u + 7;
	} else {
		lg(HIBR_LERR, "dav: %s is not an http:// or https:// address", u);
		return HIBR_FAIL;
	}
	e = p + strcspn(p, "/?#");
	hp = p;
	for (at = p; at < e; at++)
		if (*at == '@')
			hp = at + 1;
	if (hp < e && *hp == '[') {
		c = memchr(hp, ']', (size_t)(e - hp));
		if (!c) {
			lg(HIBR_LERR, "dav: %s: an unclosed [ in the host", u);
			return HIBR_FAIL;
		}
		s_add(&o->host, hp + 1, (size_t)(c - hp - 1));
		c++;
		if (c < e && *c == ':')
			s_add(&o->port, c + 1, (size_t)(e - c - 1));
	} else {
		c = hp;
		while (c < e && *c != ':')
			c++;
		s_add(&o->host, hp, (size_t)(c - hp));
		if (c < e)
			s_add(&o->port, c + 1, (size_t)(e - c - 1));
	}
	if (!o->host.n) {
		lg(HIBR_LERR, "dav: %s names no host", u);
		return HIBR_FAIL;
	}
	if (!o->port.n)
		s_cat(&o->port, o->tls ? "443" : "80");
	if (*e == '/' || *e == '?')
		s_add(&o->path, e, strcspn(e, "#"));
	if (!o->path.n || o->path.p[0] != '/') {
		str t;

		s_init(&t);
		s_ch(&t, '/');
		if (o->path.n)
			s_cat(&t, o->path.p);
		s_free(&o->path);
		o->path = t;
	}
	return HIBR_OK;
}

/* Free a split address. */
void dv_urlfree(dv_url *u)
{
	s_free(&u->host);
	s_free(&u->port);
	s_free(&u->path);
}

/* Percent-encode text for a path: everything but the unreserved
   characters, and the slash when it separates segments. */
void dv_enc(str *o, const char *p, int keepslash)
{
	static const char hx[] = "0123456789ABCDEF";
	unsigned char c;

	for (; *p; p++) {
		c = (unsigned char)*p;
		if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
		    (c >= '0' && c <= '9') || c == '-' || c == '.' || c == '_' ||
		    c == '~' || (keepslash && c == '/')) {
			s_ch(o, c);
		} else {
			s_ch(o, '%');
			s_ch(o, hx[c >> 4]);
			s_ch(o, hx[c & 15]);
		}
	}
}

/* The value of one hex digit, or -1. */
int dv_hex(int c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}

/* Undo percent-encoding. */
void dv_dec(str *o, const char *p, size_t n)
{
	size_t i;
	int a, b;

	for (i = 0; i < n; i++) {
		if (p[i] == '%' && i + 2 < n) {
			a = dv_hex((unsigned char)p[i + 1]);
			b = dv_hex((unsigned char)p[i + 2]);
			if (a >= 0 && b >= 0) {
				s_ch(o, a * 16 + b);
				i += 2;
				continue;
			}
		}
		s_ch(o, p[i]);
	}
}

/* Turn a location into a full address and the server it belongs to:
   dav://name/path names a server set up with `dav server`; an http or
   https address is taken as it is, with the login of the server whose
   address it starts with, if any. */
int dv_resolve(sh *s, const char *loc, str *url, const dv_srv **sv)
{
	const char *p, *slash;
	str nm;
	dv_srv *v;
	size_t n;

	*sv = 0;
	url->n = 0;
	if (dv_conf(s) != HIBR_OK)
		return HIBR_FAIL;
	if (!strncmp(loc, "dav://", 6)) {
		p = loc + 6;
		slash = strchr(p, '/');
		s_init(&nm);
		s_add(&nm, p, slash ? (size_t)(slash - p) : strlen(p));
		v = dv_srvfind(nm.p ? nm.p : "");
		if (!v) {
			lg(HIBR_LERR, "dav: no server called %s (see dav servers)",
			   nm.p ? nm.p : "");
			s_free(&nm);
			return HIBR_FAIL;
		}
		s_free(&nm);
		n = strlen(v->url);
		while (n && v->url[n - 1] == '/')
			n--;
		s_add(url, v->url, n);
		s_ch(url, '/');
		if (slash) {
			while (*slash == '/')
				slash++;
			dv_enc(url, slash, 1);
		}
		*sv = v;
		return HIBR_OK;
	}
	if (strncasecmp(loc, "http://", 7) && strncasecmp(loc, "https://", 8)) {
		lg(HIBR_LERR, "dav: %s is neither dav://server/path nor an "
			      "http(s) address", loc);
		return HIBR_FAIL;
	}
	s_cat(url, loc);
	*sv = dv_srvurl(loc);
	return HIBR_OK;
}

/* The timeout, in seconds, from HIBR_DAV_TIMEOUT or the default. */
int dv_timeout(sh *s)
{
	const char *v = hibr_get(s, "HIBR_DAV_TIMEOUT");
	int n = v ? atoi(v) : 0;

	return n > 0 ? n : DV_TIMEOUT;
}

/* Give a descriptor the send and receive timeouts, close-on-exec, and no
   SIGPIPE where the system can say so per socket. */
void dv_fdprep(int fd, int tmo)
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
int dv_dial(const char *host, const char *port, int tmo)
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
		lg(HIBR_LERR, "dav: %s: %s", host, gai_strerror(rc));
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
		lg(HIBR_LERR, "dav: cannot reach %s:%s: %s", host, port,
		   strerror(errno ? errno : ETIMEDOUT));
		return -1;
	}
	fd = fd_high(fd);
	dv_fdprep(fd, tmo);
	return fd;
}

/* In a freshly forked relay: close every descriptor but the two it needs
   and the standard three, so it holds nobody else's terminal or pipe. */
void dv_closerest(int a, int b)
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
   only for a server set up that way. */
int dv_tls(dv_conn *c, int sock, int tmo)
{
	void (*relay)(int, int, const char *);
	int (*load)(void);
	int sp[2];
	pid_t pid;

	relay = (void (*)(int, int, const char *))dlsym(RTLD_DEFAULT,
							   "tls_relay");
	load = (int (*)(void))dlsym(RTLD_DEFAULT, "tls_load");
	if (!relay || !load) {
		lg(HIBR_LERR, "dav: this hibr was built without TLS");
		close(sock);
		return -1;
	}
	if (load() != HIBR_OK) {
		close(sock);
		return -1;
	}
	if (socketpair(AF_UNIX, SOCK_STREAM, 0, sp) < 0) {
		lg(HIBR_LERR, "dav: socketpair: %s", strerror(errno));
		close(sock);
		return -1;
	}
	fflush(0);
	pid = fork();
	if (pid < 0) {
		lg(HIBR_LERR, "dav: fork: %s", strerror(errno));
		close(sock);
		close(sp[0]);
		close(sp[1]);
		return -1;
	}
	if (pid == 0) {
		close(sp[0]);
		dv_closerest(sp[1], sock);
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
	c->fd = fd_high(sp[0]);
	dv_fdprep(c->fd, tmo);
	lg(HIBR_LDBG, "dav: tls relay for %s on pid %ld", c->host, (long)pid);
	return c->fd;
}

/* Close a connection and collect its relay, if this process started it. */
void dv_conndrop(dv_conn *c)
{
	size_t i;

	for (i = 0; i < dv_conns.n; i++) {
		if (dv_conns.p[i] != c)
			continue;
		dv_conns.p[i] = dv_conns.p[--dv_conns.n];
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
void dv_closeall(void)
{
	while (dv_conns.n)
		dv_conndrop(dv_conns.p[dv_conns.n - 1]);
	v_free(&dv_conns);
}

/* Whether a kept connection has been closed by the other end, or has
   something unasked-for waiting, either of which makes it useless. */
int dv_stale(dv_conn *c)
{
	struct pollfd pf;

	if (c->rp < c->rb.n)
		return 1;
	pf.fd = c->fd;
	pf.events = POLLIN;
	pf.revents = 0;
	return poll(&pf, 1, 0) != 0;
}

/* A connection to the place an address names: the one kept from before
   if it is still good, else a new one. A child of the process that opened
   a kept one never uses it -- the two would talk over each other. */
dv_conn *dv_connget(sh *s, dv_url *u, int noverify)
{
	str key;
	size_t i;
	dv_conn *c = 0;
	int sock, tmo = dv_timeout(s);

	s_init(&key);
	s_num(&key, u->tls);
	s_ch(&key, ' ');
	s_cat(&key, u->host.p);
	s_ch(&key, ' ');
	s_cat(&key, u->port.p);
	s_cat(&key, noverify ? " k" : " v");
	for (i = 0; i < dv_conns.n; i++) {
		c = dv_conns.p[i];
		if (c->owner != getpid()) {
			close(c->fd);
			c->fd = -1;
			dv_conndrop(c);
			i = (size_t)-1;
			c = 0;
			continue;
		}
		if (!strcmp(c->key, key.p))
			break;
		c = 0;
	}
	if (c && dv_stale(c)) {
		lg(HIBR_LDBG, "dav: kept connection to %s went stale", c->host);
		dv_conndrop(c);
		c = 0;
	}
	if (c) {
		c->reused = 1;
		s_free(&key);
		return c;
	}
	sock = dv_dial(u->host.p, u->port.p, tmo);
	if (sock < 0) {
		s_free(&key);
		return 0;
	}
	c = xm(sizeof *c);
	memset(c, 0, sizeof *c);
	c->key = xs(key.p);
	c->host = xs(u->host.p);
	c->tls = u->tls;
	c->noverify = noverify;
	c->owner = getpid();
	c->fd = sock;
	s_init(&c->rb);
	s_free(&key);
	if (u->tls && dv_tls(c, sock, tmo) < 0) {
		c->fd = -1;
		free(c->key);
		free(c->host);
		free(c);
		return 0;
	}
	v_add(&dv_conns, c);
	return c;
}

/* Write everything, or fail. */
int dv_wall(int fd, const char *p, size_t n)
{
	ssize_t w;

	while (n) {
		w = send(fd, p, n, MSG_NOSIGNAL);
		if (w < 0 && errno == ENOTSOCK)
			w = write(fd, p, n);
		if (w < 0 && errno == EINTR)
			continue;
		if (w <= 0)
			return HIBR_FAIL;
		p += w;
		n -= (size_t)w;
	}
	return HIBR_OK;
}

/* Read more from a connection into its buffer: bytes read, 0 at the end,
   -1 on an error or a timeout. */
long dv_fill(dv_conn *c)
{
	ssize_t n;

	if (c->rp == c->rb.n) {
		c->rb.n = 0;
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

/* One line of a reply, without its line ending. */
int dv_line(dv_conn *c, str *o)
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
		if (o->n > 65536 || dv_fill(c) <= 0)
			return HIBR_FAIL;
	}
}

/* Where a reply's body goes: a descriptor, or the reply, within bounds. */
int dv_sink(dv_res *rs, int out, const char *p, size_t n)
{
	if (out >= 0) {
		while (n) {
			ssize_t w = write(out, p, n);

			if (w < 0 && errno == EINTR)
				continue;
			if (w <= 0) {
				lg(HIBR_LERR, "dav: cannot write what arrived: %s",
				   strerror(errno));
				return HIBR_FAIL;
			}
			p += w;
			n -= (size_t)w;
		}
		return HIBR_OK;
	}
	if (rs->body.n + n > (size_t)DV_MAXBODY) {
		lg(HIBR_LERR, "dav: a reply larger than %ld bytes",
		   (long)DV_MAXBODY);
		return HIBR_FAIL;
	}
	s_add(&rs->body, p, n);
	return HIBR_OK;
}

/* Pass n bytes of the body on, or all of it to the end when n is -1. */
int dv_body(dv_conn *c, dv_res *rs, int out, long long n)
{
	size_t k;
	long r;

	while (n != 0) {
		if (c->rp == c->rb.n) {
			r = dv_fill(c);
			if (r < 0)
				return HIBR_FAIL;
			if (r == 0)
				return n < 0 ? HIBR_OK : HIBR_FAIL;
		}
		k = c->rb.n - c->rp;
		if (n > 0 && (long long)k > n)
			k = (size_t)n;
		if (dv_sink(rs, out, c->rb.p + c->rp, k) != HIBR_OK)
			return HIBR_FAIL;
		c->rp += k;
		if (n > 0)
			n -= (long long)k;
	}
	return HIBR_OK;
}

/* Whether a header line names the header asked for, and its value. */
const char *dv_hval(const char *line, const char *name)
{
	size_t n = strlen(name);

	if (strncasecmp(line, name, n) || line[n] != ':')
		return 0;
	line += n + 1;
	while (*line == ' ' || *line == '\t')
		line++;
	return line;
}

/* Read a reply: its status, the headers that matter, and its body, framed
   by chunks, a length or the end of the connection. got says whether any
   of it arrived, so a dead kept connection can be told from a bad reply. */
int dv_resp(dv_conn *c, const char *method, dv_res *rs, int out, int *closeit,
	    int *got)
{
	str l, cl;
	const char *v;
	int chunked = 0, keep = 1, sink;
	long long len = -1, n;
	char *end;

	*got = 0;
	*closeit = 0;
	s_init(&l);
	s_init(&cl);
	for (;;) {
		if (dv_line(c, &l) != HIBR_OK) {
			*got = l.n > 0;
			goto bad;
		}
		*got = 1;
		if (!l.n)
			continue;
		if (strncmp(l.p, "HTTP/", 5) || !strchr(l.p, ' '))
			goto bad;
		rs->code = atoi(strchr(l.p, ' ') + 1);
		keep = strncmp(l.p, "HTTP/1.0", 8) != 0;
		v = strchr(strchr(l.p, ' ') + 1, ' ');
		free(rs->reason);
		rs->reason = xs(v ? v + 1 : "");
		chunked = 0;
		len = -1;
		for (;;) {
			if (dv_line(c, &l) != HIBR_OK)
				goto bad;
			if (!l.n)
				break;
			if ((v = dv_hval(l.p, "content-length")))
				len = strtoll(v, &end, 10);
			else if ((v = dv_hval(l.p, "transfer-encoding")))
				chunked = strcasestr(v, "chunked") != 0;
			else if ((v = dv_hval(l.p, "connection"))) {
				if (strcasestr(v, "close"))
					keep = 0;
				else if (strcasestr(v, "keep-alive"))
					keep = 1;
			} else if ((v = dv_hval(l.p, "etag"))) {
				free(rs->etag);
				rs->etag = xs(v);
			} else if ((v = dv_hval(l.p, "location"))) {
				free(rs->loc);
				rs->loc = xs(v);
			} else if ((v = dv_hval(l.p, "content-type"))) {
				free(rs->ctype);
				rs->ctype = xs(v);
			} else if ((v = dv_hval(l.p, "www-authenticate"))) {
				v_add(&rs->auth, xs(v));
			}
		}
		if (rs->code >= 200 || rs->code == 101)
			break;
	}
	sink = rs->code >= 200 && rs->code < 300 ? out : -1;
	if (!strcmp(method, "HEAD") || rs->code == 204 || rs->code == 304) {
		;
	} else if (chunked) {
		for (;;) {
			if (dv_line(c, &cl) != HIBR_OK)
				goto bad;
			n = strtoll(cl.p ? cl.p : "", &end, 16);
			if (end == cl.p || n < 0)
				goto bad;
			if (n == 0) {
				do {
					if (dv_line(c, &cl) != HIBR_OK)
						goto bad;
				} while (cl.n);
				break;
			}
			if (dv_body(c, rs, sink, n) != HIBR_OK ||
			    dv_line(c, &cl) != HIBR_OK)
				goto bad;
		}
	} else if (len >= 0) {
		if (dv_body(c, rs, sink, len) != HIBR_OK)
			goto bad;
	} else {
		if (dv_body(c, rs, sink, -1) != HIBR_OK)
			goto bad;
		keep = 0;
	}
	*closeit = !keep;
	s_free(&l);
	s_free(&cl);
	return HIBR_OK;
bad:
	*closeit = 1;
	s_free(&l);
	s_free(&cl);
	return HIBR_FAIL;
}

/* Base64, for a Basic login. */
void dv_b64(str *o, const char *p, size_t n)
{
	static const char t[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz"
				"0123456789+/";
	size_t i;
	unsigned v;

	for (i = 0; i + 2 < n; i += 3) {
		v = (unsigned char)p[i] << 16 | (unsigned char)p[i + 1] << 8 |
		    (unsigned char)p[i + 2];
		s_ch(o, t[v >> 18]);
		s_ch(o, t[(v >> 12) & 63]);
		s_ch(o, t[(v >> 6) & 63]);
		s_ch(o, t[v & 63]);
	}
	if (n - i == 1) {
		v = (unsigned char)p[i] << 16;
		s_ch(o, t[v >> 18]);
		s_ch(o, t[(v >> 12) & 63]);
		s_cat(o, "==");
	} else if (n - i == 2) {
		v = (unsigned char)p[i] << 16 | (unsigned char)p[i + 1] << 8;
		s_ch(o, t[v >> 18]);
		s_ch(o, t[(v >> 12) & 63]);
		s_ch(o, t[(v >> 6) & 63]);
		s_ch(o, '=');
	}
}

/* What we have learned about logging in to a place as a user. */
dv_auth *dv_authfind(const char *key)
{
	size_t i;
	dv_auth *a;

	for (i = 0; i < dv_auths.n; i++) {
		a = dv_auths.p[i];
		if (!strcmp(a->key, key))
			return a;
	}
	a = xm(sizeof *a);
	memset(a, 0, sizeof *a);
	a->key = xs(key);
	v_add(&dv_auths, a);
	return a;
}

/* Forget what a challenge said, before reading a new one. */
void dv_authclear(dv_auth *a)
{
	free(a->realm);
	free(a->nonce);
	free(a->opaque);
	free(a->algo);
	a->realm = a->nonce = a->opaque = a->algo = 0;
	a->kind = a->qop = a->sess = 0;
	a->nc = 0;
}

/* Forget every login learned. */
void dv_authfree(void)
{
	size_t i;
	dv_auth *a;

	for (i = 0; i < dv_auths.n; i++) {
		a = dv_auths.p[i];
		dv_authclear(a);
		free(a->key);
		free(a);
	}
	v_free(&dv_auths);
}

/* Read a token of a challenge: letters, digits and the usual punctuation. */
const char *dv_tok(const char *p, str *o)
{
	o->n = 0;
	while (*p && !strchr(" \t,=\"", *p))
		s_ch(o, *p++);
	if (!o->p)
		s_cat(o, "");
	return p;
}

/* Read a challenge parameter's value, quoted or not. */
const char *dv_pval(const char *p, str *o)
{
	o->n = 0;
	if (*p == '"') {
		p++;
		while (*p && *p != '"') {
			if (*p == '\\' && p[1])
				p++;
			s_ch(o, *p++);
		}
		if (*p == '"')
			p++;
	} else {
		while (*p && *p != ',' && *p != ' ' && *p != '\t')
			s_ch(o, *p++);
	}
	if (!o->p)
		s_cat(o, "");
	return p;
}

/* Read the server's challenges and pick the best login it offers: Digest
   over Basic. Whether there was one dav can answer, and stale says the
   server only wants a new nonce. names gets the schemes offered. */
int dv_authparse(dv_auth *a, vec *chal, int *stale, str *names)
{
	size_t i;
	const char *p;
	str tok, val, scheme;
	int basic = 0, digest = 0, dq = 0, dsess = 0, dstale = 0, dalgok = 1;
	char *realm = 0, *nonce = 0, *opaque = 0, *algo = 0, *brealm = 0;

	s_init(&tok);
	s_init(&val);
	s_init(&scheme);
	*stale = 0;
	for (i = 0; i < chal->n; i++) {
		p = chal->p[i];
		scheme.n = 0;
		while (*p) {
			while (*p == ' ' || *p == '\t' || *p == ',')
				p++;
			if (!*p)
				break;
			p = dv_tok(p, &tok);
			while (*p == ' ' || *p == '\t')
				p++;
			if (*p != '=') {
				scheme.n = 0;
				s_cat(&scheme, tok.p);
				if (names->n)
					s_cat(names, ", ");
				s_cat(names, tok.p);
				if (!strcasecmp(tok.p, "basic"))
					basic = 1;
				if (!strcasecmp(tok.p, "digest"))
					digest = 1;
				continue;
			}
			p++;
			while (*p == ' ' || *p == '\t')
				p++;
			p = dv_pval(p, &val);
			if (!scheme.p)
				continue;
			if (!strcasecmp(scheme.p, "basic")) {
				if (!strcasecmp(tok.p, "realm")) {
					free(brealm);
					brealm = xs(val.p);
				}
				continue;
			}
			if (strcasecmp(scheme.p, "digest"))
				continue;
			if (!strcasecmp(tok.p, "realm")) {
				free(realm);
				realm = xs(val.p);
			} else if (!strcasecmp(tok.p, "nonce")) {
				free(nonce);
				nonce = xs(val.p);
			} else if (!strcasecmp(tok.p, "opaque")) {
				free(opaque);
				opaque = xs(val.p);
			} else if (!strcasecmp(tok.p, "qop")) {
				dq = strstr(val.p, "auth") != 0 &&
				     (strstr(val.p, "auth,") || strstr(val.p, "auth ") ||
				      !strcmp(val.p, "auth") ||
				      (strlen(val.p) >= 4 &&
				       !strcmp(val.p + strlen(val.p) - 4, "auth")) ||
				      strstr(val.p, ",auth"));
			} else if (!strcasecmp(tok.p, "algorithm")) {
				free(algo);
				algo = xs(val.p);
				dsess = !strcasecmp(val.p, "MD5-sess");
				dalgok = !strcasecmp(val.p, "MD5") || dsess;
			} else if (!strcasecmp(tok.p, "stale")) {
				dstale = !strcasecmp(val.p, "true");
			}
		}
	}
	dv_authclear(a);
	if (digest && nonce && dalgok) {
		a->kind = 2;
		a->realm = realm ? realm : xs("");
		a->nonce = nonce;
		a->opaque = opaque;
		a->algo = algo;
		a->qop = dq;
		a->sess = dsess;
		*stale = dstale;
		realm = nonce = opaque = algo = 0;
	} else if (basic) {
		a->kind = 1;
		a->realm = brealm;
		brealm = 0;
	}
	free(realm);
	free(nonce);
	free(opaque);
	free(algo);
	free(brealm);
	s_free(&tok);
	s_free(&val);
	s_free(&scheme);
	return a->kind != 0;
}

/* Append a quoted Digest parameter. */
void dv_dpar(str *o, const char *k, const char *v, int q)
{
	s_cat(o, ", ");
	s_cat(o, k);
	s_ch(o, '=');
	if (q)
		s_ch(o, '"');
	for (; *v; v++) {
		if (q && (*v == '"' || *v == '\\'))
			s_ch(o, '\\');
		s_ch(o, *v);
	}
	if (q)
		s_ch(o, '"');
}

/* The Authorization header for a request, from what the server asked. */
void dv_authhdr(dv_auth *a, const dv_srv *sv, const char *method,
		const char *uri, str *o)
{
	str t, ha1, ha2, rsp, cn;
	char nc[16];
	unsigned long r;

	if (!a || !a->kind || !sv)
		return;
	s_init(&t);
	if (a->kind == 1) {
		s_cat(&t, sv->user);
		s_ch(&t, ':');
		s_cat(&t, sv->pass);
		s_cat(o, "Authorization: Basic ");
		dv_b64(o, t.p, t.n);
		s_cat(o, "\r\n");
		s_free(&t);
		return;
	}
	s_init(&ha1);
	s_init(&ha2);
	s_init(&rsp);
	s_init(&cn);
	a->nc++;
	snprintf(nc, sizeof nc, "%08lx", a->nc);
	r = (unsigned long)time(0) ^ ((unsigned long)getpid() << 16) ^
	    (unsigned long)a->nc * 2654435761u ^ (unsigned long)clock();
	s_num(&t, (long)r);
	dv_md5hex(t.p, t.n, &cn);
	cn.p[16] = 0;
	cn.n = 16;
	t.n = 0;
	s_cat(&t, sv->user);
	s_ch(&t, ':');
	s_cat(&t, a->realm);
	s_ch(&t, ':');
	s_cat(&t, sv->pass);
	dv_md5hex(t.p, t.n, &ha1);
	if (a->sess) {
		t.n = 0;
		s_cat(&t, ha1.p);
		s_ch(&t, ':');
		s_cat(&t, a->nonce);
		s_ch(&t, ':');
		s_cat(&t, cn.p);
		ha1.n = 0;
		dv_md5hex(t.p, t.n, &ha1);
	}
	t.n = 0;
	s_cat(&t, method);
	s_ch(&t, ':');
	s_cat(&t, uri);
	dv_md5hex(t.p, t.n, &ha2);
	t.n = 0;
	s_cat(&t, ha1.p);
	s_ch(&t, ':');
	s_cat(&t, a->nonce);
	s_ch(&t, ':');
	if (a->qop) {
		s_cat(&t, nc);
		s_ch(&t, ':');
		s_cat(&t, cn.p);
		s_cat(&t, ":auth:");
	}
	s_cat(&t, ha2.p);
	dv_md5hex(t.p, t.n, &rsp);
	s_cat(o, "Authorization: Digest username=\"");
	s_cat(o, sv->user);
	s_ch(o, '"');
	dv_dpar(o, "realm", a->realm, 1);
	dv_dpar(o, "nonce", a->nonce, 1);
	dv_dpar(o, "uri", uri, 1);
	if (a->algo)
		dv_dpar(o, "algorithm", a->algo, 0);
	dv_dpar(o, "response", rsp.p, 1);
	if (a->opaque)
		dv_dpar(o, "opaque", a->opaque, 1);
	if (a->qop) {
		dv_dpar(o, "qop", "auth", 0);
		dv_dpar(o, "nc", nc, 0);
		dv_dpar(o, "cnonce", cn.p, 1);
	}
	s_cat(o, "\r\n");
	s_free(&t);
	s_free(&ha1);
	s_free(&ha2);
	s_free(&rsp);
	s_free(&cn);
}

/* An empty request. */
void dv_reqinit(dv_req *r)
{
	memset(r, 0, sizeof *r);
	r->bfd = -1;
	r->out = -1;
}

/* Free a request's extra headers. */
void dv_reqfree(dv_req *r)
{
	size_t i;

	for (i = 0; i < r->hdr.n; i++)
		free(r->hdr.p[i]);
	v_free(&r->hdr);
}

/* Add a header to a request. */
void dv_hdr(dv_req *r, const char *name, const char *val)
{
	str h;

	s_init(&h);
	s_cat(&h, name);
	s_cat(&h, ": ");
	s_cat(&h, val);
	v_add(&r->hdr, xs(h.p));
	s_free(&h);
}

/* An empty reply. */
void dv_resinit(dv_res *r)
{
	memset(r, 0, sizeof *r);
	s_init(&r->body);
}

/* Free a reply. */
void dv_resfree(dv_res *r)
{
	size_t i;

	s_free(&r->body);
	free(r->etag);
	free(r->loc);
	free(r->ctype);
	free(r->reason);
	for (i = 0; i < r->auth.n; i++)
		free(r->auth.p[i]);
	v_free(&r->auth);
	dv_resinit(r);
}

/* Send a request's body: from memory, or from its descriptor, rewound. */
int dv_sendbody(dv_conn *c, dv_req *rq)
{
	char *buf;
	off_t left;
	ssize_t n;
	int ok = HIBR_OK;

	if (rq->bfd < 0)
		return rq->blen ? dv_wall(c->fd, rq->body, rq->blen) : HIBR_OK;
	if (lseek(rq->bfd, 0, SEEK_SET) < 0)
		return HIBR_FAIL;
	buf = xm(HIBR_IOCH);
	left = rq->bflen;
	while (left > 0) {
		n = read(rq->bfd, buf, left > HIBR_IOCH ? HIBR_IOCH : (size_t)left);
		if (n < 0 && errno == EINTR)
			continue;
		if (n <= 0 || dv_wall(c->fd, buf, (size_t)n) != HIBR_OK) {
			ok = HIBR_FAIL;
			break;
		}
		left -= n;
	}
	free(buf);
	return ok;
}

/* The authentication key: the place and the user. */
void dv_authkey(dv_url *u, const dv_srv *sv, str *o)
{
	o->n = 0;
	s_num(o, u->tls);
	s_ch(o, ' ');
	s_cat(o, u->host.p);
	s_ch(o, ' ');
	s_cat(o, u->port.p);
	s_ch(o, ' ');
	s_cat(o, sv && sv->user ? sv->user : "");
}

/* Make an address from a redirect's Location, relative to where it came
   from. */
void dv_relurl(dv_url *u, const char *loc, str *o)
{
	o->n = 0;
	if (!strncasecmp(loc, "http://", 7) || !strncasecmp(loc, "https://", 8)) {
		s_cat(o, loc);
		return;
	}
	s_cat(o, u->tls ? "https://" : "http://");
	if (strchr(u->host.p, ':')) {
		s_ch(o, '[');
		s_cat(o, u->host.p);
		s_ch(o, ']');
	} else {
		s_cat(o, u->host.p);
	}
	s_ch(o, ':');
	s_cat(o, u->port.p);
	if (*loc == '/') {
		s_cat(o, loc);
	} else {
		const char *sl = strrchr(u->path.p, '/');

		s_add(o, u->path.p, sl ? (size_t)(sl - u->path.p + 1) : 1);
		s_cat(o, loc);
	}
}

/* Do a request: connect or reuse, log in as the server asks, follow
   redirects, retry once on a kept connection that turned out dead. The
   status is OK for a 2xx reply; rs->code says what came back, 0 when
   nothing did. */
int dv_do(sh *s, dv_req *rq, dv_res *rs)
{
	str url, head, key, nu;
	dv_url u;
	dv_conn *c;
	dv_auth *a;
	const dv_srv *sv = rq->sv;
	int redirects = 0, tries = 0, retried = 0, closeit, got, stale, rc;
	int noverify = sv ? sv->noverify : 0;
	size_t i;
	str names;

	s_init(&url);
	s_init(&head);
	s_init(&key);
	s_init(&nu);
	s_cat(&url, rq->loc);
	dv_resinit(rs);
	for (;;) {
		memset(&u, 0, sizeof u);
		if (dv_urlsplit(url.p, &u) != HIBR_OK) {
			rc = HIBR_FAIL;
			break;
		}
		a = 0;
		if (sv && sv->user && *sv->user) {
			dv_authkey(&u, sv, &key);
			a = dv_authfind(key.p);
		}
		c = dv_connget(s, &u, noverify);
		if (!c) {
			dv_urlfree(&u);
			rc = HIBR_FAIL;
			break;
		}
		head.n = 0;
		s_cat(&head, rq->method);
		s_ch(&head, ' ');
		s_cat(&head, u.path.p);
		s_cat(&head, " HTTP/1.1\r\nHost: ");
		if (strchr(u.host.p, ':')) {
			s_ch(&head, '[');
			s_cat(&head, u.host.p);
			s_ch(&head, ']');
		} else {
			s_cat(&head, u.host.p);
		}
		if (strcmp(u.port.p, u.tls ? "443" : "80")) {
			s_ch(&head, ':');
			s_cat(&head, u.port.p);
		}
		s_cat(&head, "\r\nUser-Agent: hibr-dav/" HIBR_VER "\r\n");
		dv_authhdr(a, sv, rq->method, u.path.p, &head);
		for (i = 0; i < rq->hdr.n; i++) {
			s_cat(&head, rq->hdr.p[i]);
			s_cat(&head, "\r\n");
		}
		if (rq->bfd >= 0 || rq->blen || !strcmp(rq->method, "PUT") ||
		    !strcmp(rq->method, "PROPFIND")) {
			s_cat(&head, "Content-Length: ");
			s_num(&head, rq->bfd >= 0 ? (long)rq->bflen : (long)rq->blen);
			s_cat(&head, "\r\n");
		}
		s_cat(&head, "\r\n");
		lg(HIBR_LDBG, "dav: %s %s%s", rq->method, u.host.p, u.path.p);
		if (dv_wall(c->fd, head.p, head.n) != HIBR_OK ||
		    dv_sendbody(c, rq) != HIBR_OK) {
			got = 0;
			goto lost;
		}
		if (dv_resp(c, rq->method, rs, rq->out, &closeit, &got) != HIBR_OK)
			goto lost;
		if (closeit)
			dv_conndrop(c);
		if (rs->code == 401 && a && tries < 2) {
			s_init(&names);
			if (!dv_authparse(a, &rs->auth, &stale, &names)) {
				lg(HIBR_LERR, "dav: %s asks for a login dav does "
					      "not speak: %s", u.host.p,
				   names.p ? names.p : "none named");
				s_free(&names);
				dv_urlfree(&u);
				rc = HIBR_FAIL;
				break;
			}
			s_free(&names);
			if (tries == 0 || stale) {
				tries++;
				dv_resfree(rs);
				dv_urlfree(&u);
				continue;
			}
		}
		if (rq->follow && rs->loc && redirects < DV_REDIRECTS &&
		    (rs->code == 301 || rs->code == 302 || rs->code == 303 ||
		     rs->code == 307 || rs->code == 308)) {
			dv_relurl(&u, rs->loc, &nu);
			lg(HIBR_LDBG, "dav: %d to %s", rs->code, nu.p);
			if (sv && !dv_srvurl(nu.p)) {
				dv_url nw;

				memset(&nw, 0, sizeof nw);
				if (dv_urlsplit(nu.p, &nw) == HIBR_OK &&
				    (strcasecmp(nw.host.p, u.host.p) ||
				     strcmp(nw.port.p, u.port.p) || nw.tls != u.tls))
					sv = 0;
				dv_urlfree(&nw);
			}
			url.n = 0;
			s_cat(&url, nu.p);
			redirects++;
			dv_resfree(rs);
			dv_urlfree(&u);
			continue;
		}
		dv_urlfree(&u);
		rc = rs->code >= 200 && rs->code < 300 ? HIBR_OK : HIBR_FAIL;
		break;
lost:
		{
			int reused = c->reused, relay = c->relay;
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
			dv_conndrop(c);
			if (!got && reused && !retried) {
				retried = 1;
				dv_resfree(rs);
				dv_urlfree(&u);
				continue;
			}
			if (relay && !got && WIFEXITED(st) && WEXITSTATUS(st))
				lg(HIBR_LERR, "dav: the secure connection to %s "
					      "failed (its certificate?)", u.host.p);
			else
				lg(HIBR_LERR, "dav: %s did not answer %s",
				   u.host.p, rq->method);
		}
		dv_urlfree(&u);
		rs->code = 0;
		rc = HIBR_FAIL;
		break;
	}
	s_free(&url);
	s_free(&head);
	s_free(&key);
	s_free(&nu);
	return rc;
}
