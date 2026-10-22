#define _GNU_SOURCE

#include "hd.h"
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

/* Where sessions live: a directory of our own under TMPDIR, which is where
   tmux keeps its sockets and for the same reason.  XDG_RUNTIME_DIR would be
   tidier, but it is removed at logout -- the very moment a held session
   exists to outlast -- and a session nobody can find is as good as gone. */
int hd_dir(str *out)
{
	const char *t = getenv("TMPDIR");
	struct stat st;

	s_cat(out, t && *t ? t : "/tmp");
	s_cat(out, "/hibr-hold-");
	s_num(out, (long)getuid());
	if (mkdir(out->p, 0700) < 0 && errno != EEXIST) {
		lg(HIBR_LERR, "hold: %s: %s", out->p, strerror(errno));
		return 0;
	}
	if (lstat(out->p, &st) < 0 || !S_ISDIR(st.st_mode) ||
	    st.st_uid != getuid() || (st.st_mode & 077)) {
		lg(HIBR_LERR, "hold: %s is not a private directory of ours",
		   out->p);
		return 0;
	}
	return 1;
}

/* A session name is a file name, so it is kept to characters that cannot
   climb out of the directory or hide in it. */
int hd_nameok(const char *name)
{
	const char *p;

	if (!name || !*name || *name == '.' || strlen(name) > 64)
		return 0;
	for (p = name; *p; p++)
		if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
		      (*p >= '0' && *p <= '9') || *p == '.' || *p == '_' ||
		      *p == '-'))
			return 0;
	return 1;
}

/* Keep a descriptor out of whatever this process execs next.  SOCK_CLOEXEC,
   accept4 and pipe2's O_CLOEXEC do this in one call on Linux; this is the
   POSIX form every platform this runs on has, macOS included, where none
   of the three exist. A failure here is not worth stopping over -- the
   descriptor still works, it would just survive an exec that never
   happens on any path that reaches this module. */
void hd_cloexec(int fd)
{
	int f;

	if (fd < 0)
		return;
	f = fcntl(fd, F_GETFD);
	if (f >= 0)
		fcntl(fd, F_SETFD, f | FD_CLOEXEC);
}

/* Say who a process is, from the session's socket path: hibr [hold: desk]
   for the server, hibr [attached: desk] for whatever is attached to it.
   Neither is running a script, so ps showing the command line it was forked
   from -- the whole `hold new ...` invocation, or nothing at all for the
   double fork -- would say nothing useful about which session it is. */
void hd_selftitle(const char *what, const char *path)
{
	const char *nm = strrchr(path, '/');
	str t, c;
	size_t room;

	nm = nm ? nm + 1 : path;
	s_init(&t);
	s_cat(&t, "hibr [");
	s_cat(&t, what);
	s_cat(&t, ": ");
	s_cat(&t, nm);
	s_ch(&t, ']');
	/* And the short name the kernel keeps, which is 15 usable bytes and
	   what `top`, `pgrep` and `killall` read: role first, no brackets, as
	   much of the session's name as fits after it. Passing the sentence
	   to both is what left `hibr [hold: bli` on screen for a session
	   called blit-direct-test-2 (Gitea #168). */
	s_init(&c);
	s_cat(&c, what);
	s_ch(&c, ':');
	room = c.n < 15 ? 15 - c.n : 0;
	s_add(&c, nm, strlen(nm) < room ? strlen(nm) : room);
	hibr_title2(c.p, t.p);
	s_free(&c);
	s_free(&t);
}

/* The socket for a session, in the private directory. */
int hd_path(const char *name, str *out)
{
	struct sockaddr_un a;

	if (!hd_nameok(name)) {
		lg(HIBR_LERR, "hold: %s: a name is letters, digits, . _ and -",
		   name ? name : "");
		return 0;
	}
	if (!hd_dir(out))
		return 0;
	s_ch(out, '/');
	s_cat(out, name);
	if (out->n >= sizeof a.sun_path) {
		lg(HIBR_LERR, "hold: %s: path too long for a socket", out->p);
		return 0;
	}
	return 1;
}

/* Write all of a buffer, riding out interruptions and a full socket. */
int hd_wall(int fd, const char *p, size_t n)
{
	struct pollfd q;
	ssize_t k;

	while (n) {
		k = write(fd, p, n);
		if (k > 0) {
			p += k;
			n -= (size_t)k;
			continue;
		}
		if (k < 0 && errno == EINTR)
			continue;
		if (k < 0 && errno == EAGAIN) {
			q.fd = fd;
			q.events = POLLOUT;
			poll(&q, 1, 1000);
			continue;
		}
		return 0;
	}
	return 1;
}

/* Read exactly n bytes: 1 when they came, 0 at end of file, -1 on error. */
int hd_rall(int fd, char *p, size_t n)
{
	ssize_t k;

	while (n) {
		k = read(fd, p, n);
		if (k > 0) {
			p += k;
			n -= (size_t)k;
			continue;
		}
		if (k == 0)
			return 0;
		if (errno == EINTR)
			continue;
		return -1;
	}
	return 1;
}

/* Send one message: a type byte, a length, and that many bytes. */
int hd_send(int fd, int type, const char *p, size_t n)
{
	unsigned len = (unsigned)n;
	str b;
	int r;

	s_init(&b);
	s_ch(&b, type);
	s_add(&b, (const char *)&len, sizeof len);
	if (n)
		s_add(&b, p, n);
	r = hd_wall(fd, b.p, b.n);
	s_free(&b);
	return r;
}

/* Receive one message into out, replacing what was there. */
int hd_recv(int fd, int *type, str *out)
{
	unsigned len;
	char t;
	int r;

	r = hd_rall(fd, &t, 1);
	if (r <= 0)
		return r;
	r = hd_rall(fd, (char *)&len, sizeof len);
	if (r <= 0)
		return r < 0 ? -1 : 0;
	if (len > (1u << 24))
		return -1;
	out->n = 0;
	s_grow(out, len);
	r = len ? hd_rall(fd, out->p, len) : 1;
	if (r <= 0)
		return r < 0 ? -1 : 0;
	out->n = len;
	out->p[len] = 0;
	*type = (unsigned char)t;
	return 1;
}

/* How long to wait, in milliseconds: what HIBR_CONNTIMEO says, or the
   default the caller names. Zero or less means no bound at all, which is
   what the shell's own knob means and is kept here so one value can turn
   every one of these back into the call it used to be. */
long hd_timeo(long dflt)
{
	return hibr_conntimeo(dflt);
}

/* Milliseconds on the monotonic clock, for a deadline no signal and no
   clock change can move. */
static long hd_now(void)
{
	struct timespec t;

	clock_gettime(CLOCK_MONOTONIC, &t);
	return (long)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

/* Connect to a session's socket; -1 when nothing answers there, with errno
   saying which kind of nothing -- ENOENT or ECONNREFUSED for a socket that
   is gone or unlistened, and ETIMEDOUT for one that is listening and not
   accepting. Every caller has to tell those apart: `hold new` must refuse
   rather than unlink a live session's socket and bind its own, and
   `hold list` must not remove as stale a server that is merely busy.

   The shape is *not* net_conn's. A non-blocking AF_UNIX connect against a
   full backlog answers EAGAIN -- "try again", nothing is in progress -- and
   measured on this box a poll for writable then returns at once with
   POLLOUT|POLLHUP and SO_ERROR **0**: by TCP's rules that reads as a
   successful connect, and hands back a descriptor that is not connected to
   anything. So the connect itself is what is retried, against a wall-clock
   deadline, since only a connect can learn that the queue has room. Each
   try that fails this way enqueues nothing, which is also why this is
   kinder to a wedged server than the blocking call was: that one left
   another connection on the queue every time anything retried. */
int hd_dial(const char *path)
{
	struct sockaddr_un a;
	long ms = hd_timeo(HD_DIALMS), end;
	int fd, fl, e;

	fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (fd < 0)
		return -1;
	hd_cloexec(fd);
	memset(&a, 0, sizeof a);
	a.sun_family = AF_UNIX;
	strncpy(a.sun_path, path, sizeof a.sun_path - 1);
	if (ms <= 0) {
		if (connect(fd, (struct sockaddr *)&a, sizeof a) < 0) {
			e = errno;
			close(fd);
			errno = e;
			return -1;
		}
		return fd;
	}
	fl = fcntl(fd, F_GETFL, 0);
	fcntl(fd, F_SETFL, fl | O_NONBLOCK);
	end = hd_now() + ms;
	for (;;) {
		if (connect(fd, (struct sockaddr *)&a, sizeof a) == 0) {
			fcntl(fd, F_SETFL, fl);
			return fd;
		}
		e = errno;
		if (e == EINTR)
			continue;
		if (e == EISCONN) {
			fcntl(fd, F_SETFL, fl);
			return fd;
		}
		if (e != EAGAIN && e != EINPROGRESS && e != EALREADY) {
			close(fd);
			errno = e;
			return -1;
		}
		if (hd_now() >= end) {
			lg(HIBR_LDBG, "hold: %s: no answer in %ld ms", path,
			   ms);
			close(fd);
			errno = ETIMEDOUT;
			return -1;
		}
		usleep(20000);
	}
}

/* Send one request on a socket already connected and, if a reply is
   wanted, wait for it. The receive bound goes here and nowhere else:
   hd_rall answers EAGAIN as an error, so SO_RCVTIMEO is enough to end a
   read on a server that accepted and then went quiet -- and the server's
   own reads and an attached client's stream go through that same hd_rall
   and must wait for ever, which they do because this is the one socket the
   option is ever set on. */
int hd_askfd(int fd, int type, const char *p, size_t n, str *reply)
{
	long ms = hd_timeo(HD_REPLYMS);
	struct timeval tv;
	int t = 0, r = 1;

	if (ms > 0) {
		tv.tv_sec = ms / 1000;
		tv.tv_usec = (ms % 1000) * 1000;
		setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
	}
	if (!hd_send(fd, type, p, n))
		r = 0;
	else if (reply)
		r = hd_recv(fd, &t, reply) > 0 && t == type;
	return r;
}

/* Send a session one request with a payload and, if a reply is wanted,
   wait for it. */
int hd_askp(const char *path, int type, const char *p, size_t n, str *reply)
{
	int fd = hd_dial(path), r;

	if (fd < 0)
		return 0;
	r = hd_askfd(fd, type, p, n, reply);
	close(fd);
	return r;
}

/* Send a session one request with no payload and, if a reply is wanted,
   wait for it. */
int hd_ask(const char *path, int type, str *reply)
{
	return hd_askp(path, type, 0, 0, reply);
}
