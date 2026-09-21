#include "pri.h"
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

#ifdef HIBR_TLS
#include <dlfcn.h>

#define TLS_CTRL_HOST 55
#define TLS_NAME_HOST 0
#define TLS_VERIFY_PEER 1
#define TLS_X509_OK 0

struct tlsapi {
	void *(*meth)(void);
	void *(*ctx_new)(void *);
	int (*ctx_paths)(void *);
	void *(*new)(void *);
	int (*set_fd)(void *, int);
	long (*ctrl)(void *, int, long, void *);
	void (*set_verify)(void *, int, void *);
	int (*set1_host)(void *, const char *);
	int (*connect)(void *);
	long (*verify_result)(const void *);
	int (*read)(void *, void *, int);
	int (*write)(void *, const void *, int);
	int (*shutdown)(void *);
	unsigned long (*err_get)(void);
	const char *(*err_str)(unsigned long);
};

struct tlsapi tls;
void *tls_lib;

/* Bind one symbol from libssl. */
void *tls_sym(void *h, const char *nm, int *bad)
{
	void *p = dlsym(h, nm);

	if (!p) {
		lg(HIBR_LERR, "libssl has no %s", nm);
		*bad = 1;
	}
	return p;
}

/* Load libssl on first use so a shell that never speaks TLS never pays. */
int tls_load(void)
{
	int bad = 0;
	const char *names[] = { "libssl.so.3", "libssl.so.1.1", "libssl.so", 0 };
	int i;

	if (tls_lib)
		return HIBR_OK;
	for (i = 0; names[i]; i++) {
		tls_lib = dlopen(names[i], RTLD_NOW | RTLD_LOCAL);
		if (tls_lib)
			break;
	}
	if (!tls_lib) {
		lg(HIBR_LERR, "cannot load libssl: %s", dlerror());
		return HIBR_FAIL;
	}
	tls.meth = tls_sym(tls_lib, "TLS_client_method", &bad);
	tls.ctx_new = tls_sym(tls_lib, "SSL_CTX_new", &bad);
	tls.ctx_paths = tls_sym(tls_lib, "SSL_CTX_set_default_verify_paths", &bad);
	tls.new = tls_sym(tls_lib, "SSL_new", &bad);
	tls.set_fd = tls_sym(tls_lib, "SSL_set_fd", &bad);
	tls.ctrl = tls_sym(tls_lib, "SSL_ctrl", &bad);
	tls.set_verify = tls_sym(tls_lib, "SSL_set_verify", &bad);
	tls.set1_host = tls_sym(tls_lib, "SSL_set1_host", &bad);
	tls.connect = tls_sym(tls_lib, "SSL_connect", &bad);
	tls.verify_result = tls_sym(tls_lib, "SSL_get_verify_result", &bad);
	tls.read = tls_sym(tls_lib, "SSL_read", &bad);
	tls.write = tls_sym(tls_lib, "SSL_write", &bad);
	tls.shutdown = tls_sym(tls_lib, "SSL_shutdown", &bad);
	tls.err_get = dlsym(tls_lib, "ERR_get_error");
	tls.err_str = dlsym(tls_lib, "ERR_reason_error_string");
	if (bad)
		return HIBR_FAIL;
	lg(HIBR_LDBG, "libssl loaded on demand");
	return HIBR_OK;
}
#endif

/* Move a descriptor out of the way of the usual redirections. */
int fd_high(int fd)
{
	int n;

	if (fd < 0 || fd > 9)
		return fd;
	n = fcntl(fd, F_DUPFD, 10);
	if (n < 0)
		return fd;
	close(fd);
	return n;
}

/* Split host and port out of a virtual path segment. */
int net_split(const char *p, str *host, str *port)
{
	const char *sl = strchr(p, '/');

	if (!sl || !sl[1])
		return HIBR_FAIL;
	s_init(host);
	s_init(port);
	s_add(host, p, (size_t)(sl - p));
	s_cat(port, sl + 1);
	return HIBR_OK;
}

/* Open a client connection to a host and service. */
int net_dial(const char *host, const char *port, int udp)
{
	struct addrinfo hint, *res, *a;
	int fd = -1, rc;

	memset(&hint, 0, sizeof hint);
	hint.ai_family = AF_UNSPEC;
	hint.ai_socktype = udp ? SOCK_DGRAM : SOCK_STREAM;
	rc = getaddrinfo(host, port, &hint, &res);
	if (rc) {
		lg(HIBR_LERR, "%s:%s: %s", host, port, gai_strerror(rc));
		return -1;
	}
	for (a = res; a; a = a->ai_next) {
		fd = socket(a->ai_family, a->ai_socktype, a->ai_protocol);
		if (fd < 0)
			continue;
		if (connect(fd, a->ai_addr, a->ai_addrlen) == 0)
			break;
		close(fd);
		fd = -1;
	}
	freeaddrinfo(res);
	if (fd < 0)
		lg(HIBR_LERR, "connect %s:%s: %s", host, port, strerror(errno));
	else
		lg(HIBR_LDBG, "connected to %s:%s on fd %d", host, port, fd);
	return fd_high(fd);
}

/* Open a listening socket on a service. */
int net_bind(const char *port, int udp)
{
	struct addrinfo hint, *res, *a;
	int fd = -1, on = 1, rc;

	memset(&hint, 0, sizeof hint);
	hint.ai_family = AF_INET;
	hint.ai_socktype = udp ? SOCK_DGRAM : SOCK_STREAM;
	hint.ai_flags = AI_PASSIVE;
	rc = getaddrinfo(0, port, &hint, &res);
	if (rc) {
		lg(HIBR_LERR, "port %s: %s", port, gai_strerror(rc));
		return -1;
	}
	for (a = res; a; a = a->ai_next) {
		fd = socket(a->ai_family, a->ai_socktype, a->ai_protocol);
		if (fd < 0)
			continue;
		setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof on);
		if (bind(fd, a->ai_addr, a->ai_addrlen) == 0)
			break;
		close(fd);
		fd = -1;
	}
	freeaddrinfo(res);
	if (fd < 0) {
		lg(HIBR_LERR, "bind %s: %s", port, strerror(errno));
		return -1;
	}
	if (!udp && listen(fd, 16) < 0) {
		lg(HIBR_LERR, "listen %s: %s", port, strerror(errno));
		close(fd);
		return -1;
	}
	lg(HIBR_LDBG, "listening on port %s, fd %d", port, fd);
	return fd_high(fd);
}

/* Connect to a Unix domain socket. */
int net_unix(const char *path)
{
	struct sockaddr_un a;
	int fd;

	if (strlen(path) >= sizeof a.sun_path) {
		lg(HIBR_LERR, "%s: socket path too long", path);
		return -1;
	}
	memset(&a, 0, sizeof a);
	a.sun_family = AF_UNIX;
	strcpy(a.sun_path, path);
	fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (fd < 0)
		return -1;
	if (connect(fd, (struct sockaddr *)&a, sizeof a) < 0) {
		lg(HIBR_LERR, "connect %s: %s", path, strerror(errno));
		close(fd);
		return -1;
	}
	return fd_high(fd);
}

#ifdef HIBR_TLS
/* Pump plaintext between a socket pair and a TLS connection. */
void tls_relay(int plain, int sock, const char *host)
{
	void *ctx, *ssl;
	fd_set rd;
	char *buf;
	int hi = (plain > sock ? plain : sock) + 1;
	long n;

	ctx = tls.ctx_new(tls.meth());
	if (!ctx)
		_exit(1);
	tls.ctx_paths(ctx);
	ssl = tls.new(ctx);
	tls.set_fd(ssl, sock);
	tls.ctrl(ssl, TLS_CTRL_HOST, TLS_NAME_HOST, (void *)host);
	if (!getenv("HIBR_TLS_INSECURE")) {
		tls.set_verify(ssl, TLS_VERIFY_PEER, 0);
		tls.set1_host(ssl, host);
	}
	if (tls.connect(ssl) != 1) {
		lg(HIBR_LERR, "tls handshake with %s failed: %s", host,
		   tls.err_str && tls.err_get ?
			   tls.err_str(tls.err_get()) : "handshake error");
		_exit(1);
	}
	if (!getenv("HIBR_TLS_INSECURE") &&
	    tls.verify_result(ssl) != TLS_X509_OK) {
		lg(HIBR_LERR, "tls certificate for %s not trusted", host);
		_exit(1);
	}
	buf = xm(HIBR_IOCH);
	for (;;) {
		FD_ZERO(&rd);
		FD_SET(plain, &rd);
		FD_SET(sock, &rd);
		if (select(hi, &rd, 0, 0, 0) < 0)
			break;
		if (FD_ISSET(plain, &rd)) {
			n = read(plain, buf, HIBR_IOCH);
			if (n <= 0)
				break;
			if (tls.write(ssl, buf, (int)n) <= 0)
				break;
		}
		if (FD_ISSET(sock, &rd)) {
			n = tls.read(ssl, buf, HIBR_IOCH);
			if (n <= 0)
				break;
			if (write(plain, buf, (size_t)n) != n)
				break;
		}
	}
	free(buf);
	tls.shutdown(ssl);
	_exit(0);
}

/* Give the shell a plain descriptor backed by a TLS relay process. */
int net_tls(const char *host, const char *port)
{
	int sp[2], sock;
	pid_t pid;

	if (tls_load() != HIBR_OK)
		return -1;
	sock = net_dial(host, port, 0);
	if (sock < 0)
		return -1;
	if (socketpair(AF_UNIX, SOCK_STREAM, 0, sp) < 0) {
		lg(HIBR_LERR, "socketpair: %s", strerror(errno));
		close(sock);
		return -1;
	}
	fflush(0);
	pid = fork();
	if (pid < 0) {
		lg(HIBR_LERR, "fork: %s", strerror(errno));
		close(sock);
		close(sp[0]);
		close(sp[1]);
		return -1;
	}
	if (pid == 0) {
		close(sp[0]);
		signal(SIGINT, SIG_DFL);
		tls_relay(sp[1], sock, host);
		_exit(0);
	}
	close(sp[1]);
	close(sock);
	lg(HIBR_LDBG, "tls relay for %s on pid %ld", host, (long)pid);
	return fd_high(sp[0]);
}
#endif

struct scheme { char *nm; hibr_open_fn fn; };

/* Find a registered scheme by the /dev/<name>/ prefix of a path. */
struct scheme *sc_find(sh *s, const char *p, const char **rest)
{
	size_t i, n;
	struct scheme *c;

	if (strncmp(p, "/dev/", 5))
		return 0;
	for (i = 0; i < s->schemes.n; i++) {
		c = (struct scheme *)s->schemes.p[i];
		n = strlen(c->nm);
		if (!strncmp(p + 5, c->nm, n) && p[5 + n] == '/') {
			if (rest)
				*rest = p + 6 + n;
			return c;
		}
	}
	return 0;
}

/* Register a /dev/<name>/ handler, replacing one of the same name. */
int hibr_scheme(sh *s, const char *nm, hibr_open_fn fn)
{
	struct scheme *c;
	size_t i;

	if (!nm || !*nm || strchr(nm, '/') || !fn)
		return HIBR_FAIL;
	for (i = 0; i < s->schemes.n; i++) {
		c = (struct scheme *)s->schemes.p[i];
		if (!strcmp(c->nm, nm)) {
			c->fn = fn;
			lg(HIBR_LDBG, "scheme %s replaced", nm);
			return HIBR_OK;
		}
	}
	c = xm(sizeof *c);
	c->nm = xs(nm);
	c->fn = fn;
	v_add(&s->schemes, c);
	lg(HIBR_LDBG, "scheme /dev/%s/ registered", nm);
	return HIBR_OK;
}

/* Remove a registered scheme. */
int hibr_unscheme(sh *s, const char *nm)
{
	size_t i;
	struct scheme *c;

	for (i = 0; i < s->schemes.n; i++) {
		c = (struct scheme *)s->schemes.p[i];
		if (strcmp(c->nm, nm))
			continue;
		free(c->nm);
		free(c);
		s->schemes.p[i] = s->schemes.p[s->schemes.n - 1];
		s->schemes.n--;
		return HIBR_OK;
	}
	return HIBR_FAIL;
}

/* Release the scheme table. */
void sc_fini(sh *s)
{
	struct scheme *c;

	while (s->schemes.n) {
		c = (struct scheme *)s->schemes.p[--s->schemes.n];
		free(c->nm);
		free(c);
	}
	v_free(&s->schemes);
}

/* Public name for opening a client connection. */
int hibr_dial(const char *host, const char *port, int udp)
{
	return net_dial(host, port, udp);
}

/* True if a redirection target names a socket rather than a file. */
int net_is(sh *s, const char *p)
{
	return !strncmp(p, "/dev/tcp/", 9) || !strncmp(p, "/dev/udp/", 9) ||
	       !strncmp(p, "/dev/tls/", 9) || !strncmp(p, "/dev/unix/", 10) ||
	       sc_find(s, p, 0) != 0;
}

/* Open a socket named by a virtual path. */
int net_open(sh *s, const char *p)
{
	str host, port;
	int fd = -1;
	const char *rest;
	struct scheme *c = sc_find(s, p, &rest);

	if (c) {
		fd = c->fn(s, rest);
		lg(HIBR_LTRC, "scheme %s opened fd %d", c->nm, fd);
		return fd < 0 ? -1 : fd_high(fd);
	}
	if (!strncmp(p, "/dev/unix/", 10))
		return net_unix(p + 9);
	if (net_split(p + 9, &host, &port) != HIBR_OK) {
		lg(HIBR_LERR, "%s: expected /dev/<proto>/host/port", p);
		return -1;
	}
	if (!strncmp(p, "/dev/tls/", 9)) {
#ifdef HIBR_TLS
		fd = net_tls(host.p, port.p);
#else
		lg(HIBR_LERR, "this build has no TLS support");
#endif
	} else {
		fd = net_dial(host.p, port.p, p[5] == 'u');
	}
	s_free(&host);
	s_free(&port);
	return fd;
}

/* Record a descriptor in a named variable and in the result slot. */
void net_slot(sh *s, const char *nm, int fd)
{
	str b;

	s_init(&b);
	s_num(&b, (long)fd);
	hibr_set(s, nm, b.p, 0);
	hibr_set(s, "RET", b.p, 0);
	s_free(&b);
}

/* Open a client connection and report its descriptor. */
int b_connect(sh *s, int ac, char **av)
{
	int i = 1, udp = 0, tls = 0, fd;
	const char *nm = "FD";

	for (; i < ac && av[i][0] == '-' && av[i][1]; i++) {
		if (!strcmp(av[i], "-u"))
			udp = 1;
		else if (!strcmp(av[i], "-s"))
			tls = 1;
		else {
			lg(HIBR_LERR, "connect: %s: unknown option", av[i]);
			return 2;
		}
	}
	if (ac - i < 2) {
		lg(HIBR_LERR, "usage: connect [-u|-s] host port [var]");
		return 2;
	}
	if (ac - i > 2)
		nm = av[i + 2];
	if (tls) {
#ifdef HIBR_TLS
		fd = net_tls(av[i], av[i + 1]);
#else
		lg(HIBR_LERR, "this build has no TLS support");
		return HIBR_FAIL;
#endif
	} else {
		fd = net_dial(av[i], av[i + 1], udp);
	}
	if (fd < 0)
		return HIBR_FAIL;
	net_slot(s, nm, fd);
	return HIBR_OK;
}

/* Describe the peer on the other end of a connection. */
char *net_peer(struct sockaddr *sa, socklen_t n)
{
	str b;
	char *h = xm(NI_MAXHOST), *p = xm(NI_MAXSERV);

	s_init(&b);
	if (getnameinfo(sa, n, h, NI_MAXHOST, p, NI_MAXSERV,
			NI_NUMERICHOST | NI_NUMERICSERV) == 0) {
		s_cat(&b, h);
		s_ch(&b, ':');
		s_cat(&b, p);
	} else {
		s_cat(&b, "unknown");
	}
	free(h);
	free(p);
	return b.p;
}

/* Wait for one connection on a listening descriptor. */
int b_accept(sh *s, int ac, char **av)
{
	struct sockaddr_storage ss;
	socklen_t sl = sizeof ss;
	const char *nm = ac > 2 ? av[2] : "FD";
	char *peer;
	int fd;

	if (ac < 2) {
		lg(HIBR_LERR, "usage: accept listenfd [var]");
		return 2;
	}
	fd = accept(atoi(av[1]), (struct sockaddr *)&ss, &sl);
	if (fd < 0) {
		lg(HIBR_LERR, "accept: %s", strerror(errno));
		return HIBR_FAIL;
	}
	peer = net_peer((struct sockaddr *)&ss, sl);
	hibr_set(s, "REMOTE", peer, 0);
	free(peer);
	net_slot(s, nm, fd_high(fd));
	return HIBR_OK;
}

/* Quote an argument for re-parsing. */
void net_q(str *o, const char *a)
{
	s_ch(o, '\'');
	for (; *a; a++) {
		if (*a == '\'')
			s_cat(o, "'\\''");
		else
			s_ch(o, *a);
	}
	s_ch(o, '\'');
}

/* Serve connections, running a handler with the socket as stdin and stdout. */
int b_listen(sh *s, int ac, char **av)
{
	struct sockaddr_storage ss;
	socklen_t sl;
	int i = 1, forky = 0, udp = 0, lim = 0, bnd = 0, lfd, cfd, served = 0;
	int o0, o1;
	char *peer;
	str cmd;
	pid_t pid;

	for (; i < ac && av[i][0] == '-' && av[i][1]; i++) {
		if (!strcmp(av[i], "-b"))
			bnd = 1;
		else if (!strcmp(av[i], "-f"))
			forky = 1;
		else if (!strcmp(av[i], "-u"))
			udp = 1;
		else if (!strcmp(av[i], "-n") && i + 1 < ac)
			lim = atoi(av[++i]);
		else {
			lg(HIBR_LERR, "listen: %s: unknown option", av[i]);
			return 2;
		}
	}
	if (bnd && (forky || lim)) {
		lg(HIBR_LERR, "listen: -b only binds, so -f and -n mean nothing");
		return 2;
	}
	if (ac - i < (bnd ? 1 : 2)) {
		lg(HIBR_LERR, bnd ? "usage: listen -b port [var]"
				  : "usage: listen [-f] [-n count] port handler");
		return 2;
	}
	lfd = net_bind(av[i], udp);
	if (lfd < 0)
		return HIBR_FAIL;
	if (bnd) {
		lg(HIBR_LINF, "bound port %s as fd %d, not serving", av[i],
		   lfd);
		net_slot(s, ac - i > 1 ? av[i + 1] : "FD", lfd);
		return HIBR_OK;
	}
	lg(HIBR_LINF, "serving port %s with %s", av[i], av[i + 1]);
	for (;;) {
		sl = sizeof ss;
		cfd = accept(lfd, (struct sockaddr *)&ss, &sl);
		if (cfd < 0) {
			if (errno == EINTR && !s->quit)
				continue;
			break;
		}
		peer = net_peer((struct sockaddr *)&ss, sl);
		hibr_set(s, "REMOTE", peer, 0);
		s_init(&cmd);
		s_cat(&cmd, av[i + 1]);
		s_ch(&cmd, ' ');
		net_q(&cmd, peer);
		free(peer);
		if (forky) {
			fflush(0);
			pid = fork();
			if (pid == 0) {
				close(lfd);
				dup2(cfd, 0);
				dup2(cfd, 1);
				close(cfd);
				signal(SIGINT, SIG_DFL);
				s->it = 0;
				hibr_run(s, cmd.p);
				fflush(0);
				_exit(s->st);
			}
			close(cfd);
			waitpid(-1, 0, WNOHANG);
		} else {
			fflush(0);
			o0 = dup(0);
			o1 = dup(1);
			dup2(cfd, 0);
			dup2(cfd, 1);
			hibr_run(s, cmd.p);
			fflush(0);
			dup2(o0, 0);
			dup2(o1, 1);
			close(o0);
			close(o1);
			close(cfd);
		}
		s_free(&cmd);
		served++;
		if (s->quit || (lim && served >= lim))
			break;
	}
	close(lfd);
	lg(HIBR_LDBG, "served %d connection(s)", served);
	return HIBR_OK;
}

/* Write arguments to a descriptor. */
int b_send(sh *s, int ac, char **av)
{
	int i = 1, nl = 1, fd;
	str b;

	(void)s;
	for (; i < ac && av[i][0] == '-' && av[i][1]; i++) {
		if (!strcmp(av[i], "-n"))
			nl = 0;
		else if (!strcmp(av[i], "-r"))
			nl = 2;
		else {
			lg(HIBR_LERR, "send: %s: unknown option", av[i]);
			return 2;
		}
	}
	if (ac - i < 1) {
		lg(HIBR_LERR, "usage: send [-n|-r] fd text...");
		return 2;
	}
	fd = atoi(av[i++]);
	for (; i < ac && av[i][0] == '-' && av[i][1]; i++) {
		if (!strcmp(av[i], "-n"))
			nl = 0;
		else if (!strcmp(av[i], "-r"))
			nl = 2;
		else
			break;
	}
	fflush(0);
	s_init(&b);
	for (; i < ac; i++) {
		if (b.n)
			s_ch(&b, ' ');
		s_cat(&b, av[i]);
	}
	if (nl == 2)
		s_cat(&b, "\r\n");
	else if (nl)
		s_ch(&b, '\n');
	if (write(fd, b.p ? b.p : "", b.n) != (ssize_t)b.n) {
		lg(HIBR_LERR, "send: %s", strerror(errno));
		s_free(&b);
		return HIBR_FAIL;
	}
	s_free(&b);
	return HIBR_OK;
}

/* Read from a descriptor into a variable. */
int b_recv(sh *s, int ac, char **av)
{
	int i = 1, fd, all = 0, want = 0;
	const char *nm;
	str b;
	char *buf;
	ssize_t n;
	char c;

	for (; i < ac && av[i][0] == '-' && av[i][1]; i++) {
		if (!strcmp(av[i], "-a"))
			all = 1;
		else if (!strcmp(av[i], "-n") && i + 1 < ac)
			want = atoi(av[++i]);
		else {
			lg(HIBR_LERR, "recv: %s: unknown option", av[i]);
			return 2;
		}
	}
	if (ac - i < 1) {
		lg(HIBR_LERR, "usage: recv [-a|-n bytes] fd [var]");
		return 2;
	}
	fd = atoi(av[i++]);
	fflush(0);
	nm = i < ac ? av[i] : "REPLY";
	s_init(&b);
	if (all || want) {
		buf = xm(HIBR_IOCH);
		while ((n = read(fd, buf, want && want - (int)b.n < HIBR_IOCH ?
					     (size_t)(want - (int)b.n) :
					     HIBR_IOCH)) > 0) {
			s_add(&b, buf, (size_t)n);
			if (want && (int)b.n >= want)
				break;
		}
		free(buf);
	} else {
		while ((n = read(fd, &c, 1)) == 1 && c != '\n')
			if (c != '\r')
				s_ch(&b, c);
		if (n <= 0 && !b.n) {
			s_free(&b);
			return HIBR_FAIL;
		}
	}
	hibr_set(s, nm, b.p ? b.p : "", 0);
	hibr_set(s, "RET", b.p ? b.p : "", 0);
	s_free(&b);
	return HIBR_OK;
}
