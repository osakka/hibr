/* blitfr -- picture frames handed to Blit by descriptor (Blit ADR 0202).
 *
 * See blitfr.h for the wire. Every call here answers "no" rather than
 * failing loudly: a frame socket that is not there, refuses us or goes
 * away is a reason to put pictures back on the terminal, not an error a
 * person can do anything about.
 */
#include "blitfr.h"
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <fcntl.h>
#include <poll.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Put a 32-bit number into four bytes, little-endian. */
void bf_put32(unsigned char *p, unsigned long v)
{
	p[0] = (unsigned char)(v & 0xff);
	p[1] = (unsigned char)((v >> 8) & 0xff);
	p[2] = (unsigned char)((v >> 16) & 0xff);
	p[3] = (unsigned char)((v >> 24) & 0xff);
}

/* Where a picture's line names its picture: the third word, the one rule
   of the channel's version 5 grammar, or null when it has none. Plain text,
   so on every platform: the console asks it of every answer, and kept in
   the Linux part a Mac's console.so would not load (Gitea #503). */
const char *bf_idat(const char *l)
{
	const char *p = l;
	int w;

	for (w = 0; w < 2; w++) {
		p = strchr(p, ' ');
		if (!p)
			return 0;
		while (*p == ' ')
			p++;
	}
	return *p >= '0' && *p <= '9' ? p : 0;
}

#ifdef __linux__
#include <sys/socket.h>
#include <sys/syscall.h>
#include <sys/un.h>

#ifndef BF_FMTMAX
#define BF_FMTMAX 512
#endif
int bf_dmabuf, bf_fence;
char bf_formats[BF_FMTMAX];
void (*bf_answer)(const char *line);

#ifndef MFD_CLOEXEC
#define MFD_CLOEXEC 0x0001U
#endif
#ifndef MFD_ALLOW_SEALING
#define MFD_ALLOW_SEALING 0x0002U
#endif
#ifndef F_ADD_SEALS
#define F_ADD_SEALS 1033
#endif
#ifndef F_SEAL_SHRINK
#define F_SEAL_SHRINK 0x0002
#endif
#ifndef F_SEAL_GROW
#define F_SEAL_GROW 0x0004
#endif
#ifndef F_SEAL_WRITE
#define F_SEAL_WRITE 0x0008
#endif
#ifndef SOCK_CLOEXEC
#define SOCK_CLOEXEC 02000000
#endif
#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0x4000
#endif


/* Connect to a frame socket and say hello with a token at wire version
   ver. -1 when refused; *inval set when the refusal was EINVAL, which is
   what a Blit older than 0.3.22 says to a version it does not know. */
int bf_hello(const char *sock, const char *tok, int ver, int *inval)
{
	struct sockaddr_un a;
	struct pollfd q;
	size_t sl, tl;
	unsigned char *m;
	char r[1024];
	ssize_t k;
	int fd;

	*inval = 0;
	sl = strlen(sock);
	tl = strlen(tok);
	if (tl > 64 || sl >= sizeof a.sun_path)
		return -1;
	fd = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0);
	if (fd < 0)
		return -1;
	memset(&a, 0, sizeof a);
	a.sun_family = AF_UNIX;
	memcpy(a.sun_path, sock, sl);
	if (sock[0] == '@')
		a.sun_path[0] = 0;
	if (connect(fd, (struct sockaddr *)&a,
		    (socklen_t)(offsetof(struct sockaddr_un, sun_path) + sl)) < 0) {
		lg(HIBR_LDBG, "frames: %s: %s", sock, strerror(errno));
		close(fd);
		return -1;
	}
	m = xm(16 + tl);
	memcpy(m, "BLITFRM1", 8);
	bf_put32(m + 8, (unsigned long)ver);
	bf_put32(m + 12, (unsigned long)tl);
	memcpy(m + 16, tok, tl);
	k = send(fd, m, 16 + tl, MSG_NOSIGNAL);
	free(m);
	if (k != (ssize_t)(16 + tl)) {
		close(fd);
		return -1;
	}
	q.fd = fd;
	q.events = POLLIN;
	k = poll(&q, 1, 2000) == 1 ? recv(fd, r, sizeof r - 1, 0) : -1;
	if (k >= 3 && ver >= 3 && !memcmp(r, "OK ", 3)) {
		char *p;

		r[k] = 0;
		bf_dmabuf = strstr(r, "dmabuf=1") != 0;
		bf_fence = strstr(r, "fence=1") != 0;
		bf_formats[0] = 0;
		p = strstr(r, "formats=");
		if (p) {
			size_t n = strcspn(p + 8, " \n");

			if (n >= BF_FMTMAX)
				n = BF_FMTMAX - 1;
			memcpy(bf_formats, p + 8, n);
			bf_formats[n] = 0;
		}
		lg(HIBR_LDBG, "frames: %s open at version %d, dmabuf %d", sock, ver, bf_dmabuf);
		return fd;
	}
	if (k < 3 || memcmp(r, "OK\n", 3)) {
		if (k > 0) {
			r[k] = 0;
			*inval = strstr(r, "EINVAL") != 0;
		}
		lg(HIBR_LDBG, "frames: %s refused a version %d hello", sock, ver);
		close(fd);
		return -1;
	}
	lg(HIBR_LDBG, "frames: %s open at version %d", sock, ver);
	return fd;
}

/* Connect to a frame socket and say hello with a token at version 5, the
   channel's one version since Blit 0.3.74 (one grammar: a picture's line
   is WORD VERB ID ..., its id the third word), which this hibr and that
   Blit only ever speak together -- each package Breaks the other's older
   one -- so nothing older is tried. */
int bf_dial(const char *sock, const char *tok)
{
	int inval;

	if (!sock || !*sock || !tok || !*tok)
		return -1;
	bf_dmabuf = bf_fence = 0;
	bf_formats[0] = 0;
	return bf_hello(sock, tok, 5, &inval);
}


/* Connect to the frame socket this environment names. */
int bf_open(void)
{
	return bf_dial(getenv("BLIT_FRAME_SOCKET"), getenv("BLIT_FRAME_TOKEN"));
}

/* A sealed memfd of n bytes. Unmapped before it is sealed against writing,
   which the kernel refuses while a writable mapping is live -- written with
   write(), so there is none. */
int bf_memfd(const unsigned char *p, size_t n)
{
	int fd = (int)syscall(SYS_memfd_create, "hibr-frame",
			      MFD_CLOEXEC | MFD_ALLOW_SEALING);
	size_t o = 0;
	ssize_t k;

	if (fd < 0)
		return -1;
	while (o < n) {
		k = write(fd, p + o, n - o);
		if (k < 0 && errno == EINTR)
			continue;
		if (k <= 0) {
			close(fd);
			return -1;
		}
		o += (size_t)k;
	}
	if (fcntl(fd, F_ADD_SEALS, F_SEAL_SHRINK | F_SEAL_GROW | F_SEAL_WRITE) < 0) {
		close(fd);
		return -1;
	}
	return fd;
}

/* A whole frame's header. */
void bf_hdr(unsigned char *h, unsigned id, unsigned long long seq, int w,
	    int ht)
{
	memset(h, 0, BF_HDR);
	memcpy(h, "FRM1", 4);
	bf_put32(h + 4, id);
	bf_put32(h + 8, (unsigned long)(seq & 0xffffffffULL));
	bf_put32(h + 12, (unsigned long)(seq >> 32));
	bf_put32(h + 16, (unsigned long)w);
	bf_put32(h + 20, (unsigned long)ht);
	bf_put32(h + 24, 24);
	bf_put32(h + 28, (unsigned long)w * 3);
}

/* One frame by dmabuf, a header and its descriptors, in one message. A full
   socket drops it, newest wins, as bf_send does. */
int bf_send3(int fd, const unsigned char *h, const int *fds, int n)
{
	struct msghdr m;
	struct iovec v;
	struct cmsghdr *c;
	union {
		char b[CMSG_SPACE(sizeof(int) * BF_MAXFD)];
		struct cmsghdr align;
	} u;
	ssize_t k;

	if (fd < 0 || n < 1 || n > BF_MAXFD)
		return 0;
	memset(&m, 0, sizeof m);
	memset(&u, 0, sizeof u);
	v.iov_base = (void *)h;
	v.iov_len = BF_HDR3;
	m.msg_iov = &v;
	m.msg_iovlen = 1;
	m.msg_control = u.b;
	m.msg_controllen = CMSG_SPACE(sizeof(int) * (size_t)n);
	c = CMSG_FIRSTHDR(&m);
	c->cmsg_level = SOL_SOCKET;
	c->cmsg_type = SCM_RIGHTS;
	c->cmsg_len = CMSG_LEN(sizeof(int) * (size_t)n);
	memcpy(CMSG_DATA(c), fds, sizeof(int) * (size_t)n);
	do
		k = sendmsg(fd, &m, MSG_NOSIGNAL | MSG_DONTWAIT);
	while (k < 0 && errno == EINTR);
	if (k < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
		lg(HIBR_LDBG, "frames: the socket is full, a dmabuf frame dropped");
		return 1;
	}
	return k == BF_HDR3;
}

/* One header and one descriptor, in one message. */
int bf_send(int fd, const unsigned char *h, int mfd)
{
	struct msghdr m;
	struct iovec v;
	struct cmsghdr *c;
	union {
		char b[CMSG_SPACE(sizeof(int))];
		struct cmsghdr align;
	} u;
	ssize_t k;

	if (fd < 0 || mfd < 0)
		return 0;
	memset(&m, 0, sizeof m);
	memset(&u, 0, sizeof u);
	v.iov_base = (void *)h;
	v.iov_len = BF_HDR;
	m.msg_iov = &v;
	m.msg_iovlen = 1;
	m.msg_control = u.b;
	m.msg_controllen = sizeof u.b;
	c = CMSG_FIRSTHDR(&m);
	c->cmsg_level = SOL_SOCKET;
	c->cmsg_type = SCM_RIGHTS;
	c->cmsg_len = CMSG_LEN(sizeof(int));
	memcpy(CMSG_DATA(c), &mfd, sizeof mfd);
	do
		k = sendmsg(fd, &m, MSG_NOSIGNAL | MSG_DONTWAIT);
	while (k < 0 && errno == EINTR);
	if (k < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
		struct pollfd q;

		q.fd = fd;
		q.events = POLLOUT;
		if (poll(&q, 1, 100) == 1)
			do
				k = sendmsg(fd, &m, MSG_NOSIGNAL | MSG_DONTWAIT);
			while (k < 0 && errno == EINTR);
		if (k < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
			lg(HIBR_LDBG, "frames: the socket stayed full, a frame dropped");
			return 1;
		}
	}
	return k == BF_HDR;
}

/* One line the far side said: HUD1 into *hud, anything else into the
   log. -1 for EACCES. */
int bf_line(const char *l, int *hud)
{
	if (!strncmp(l, "HUD1 ", 5)) {
		if (hud)
			*hud = !strcmp(l + 5, "on");
		lg(HIBR_LDBG, "frames: the performance panel %s status",
		   strcmp(l + 5, "on") ? "no longer wants" : "wants");
		return 0;
	}
	/* What came of an input route (Blit 0.3.64, Gitea #486): an answer
	   about one picture's route, never about the connection, so an EACCES
	   here -- an EIS socket somebody else owns -- closes nothing. */
	if (!strncmp(l, "INPUT ok ", 9) || !strncmp(l, "INPUT error ", 12)) {
		if (bf_answer)
			bf_answer(l);
		else
			lg(HIBR_LDBG, "frames: %s", l);
		return 0;
	}
	/* The keyboards Blit has and which is current (Blit 0.3.94, Gitea
	   #529), on a connection that asked with `KEYBOARD list`; and the
	   display's refresh rate (Blit 0.3.98, Gitea #534), on one that asked
	   with `DISPLAY rate`. */
	if (!strncmp(l, "KEYBOARD ", 9) || !strncmp(l, "DISPLAY ", 8)) {
		if (bf_answer)
			bf_answer(l);
		else
			lg(HIBR_LDBG, "frames: %s", l);
		return 0;
	}
	if (bf_answer && (!strncmp(l, "FRAME release ", 14) ||
			  !strncmp(l, "FRAME error ", 12))) {
		bf_answer(l);
		return strstr(l, "EACCES") ? -1 : 0;
	}
	lg(HIBR_LDBG, "frames: %s", l);
	/* The person this connection was opened for has signed out (Blit
	   0.3.19): it is closed, and pictures go back on the terminal. */
	return strstr(l, "EACCES") ? -1 : 0;
}

/* One line of the wire's text -- an INPUT request (Blit 0.3.64) -- as a
   message of its own. 1 when it went. */
/* Blit's version, as BLIT_VERSION says in every session Blit starts (Blit
   0.2.39), as one number: 3098 for 0.3.98. A process Blit did not start
   is taken to talk to a current one. */
long bf_blitver(void)
{
	static long v = -1;
	const char *e;
	long a = 0, b = 0, c = 0;

	if (v >= 0)
		return v;
	e = getenv("BLIT_VERSION");
	if (!e || sscanf(e, "%ld.%ld.%ld", &a, &b, &c) != 3)
		v = LONG_MAX;
	else
		v = a * 1000000 + b * 1000 + c;
	return v;
}

int bf_say(int fd, const char *line)
{
	size_t n = strlen(line);
	ssize_t k;

	if (fd < 0 || !n)
		return 0;
	/* A line an older Blit does not know is read by it as a malformed
	   frame and counted as one refused: `KEYBOARD` before 0.3.94,
	   `DISPLAY` before 0.3.98. Not sent, and taken as said. */
	if ((!strncmp(line, "KEYBOARD ", 9) && bf_blitver() < 3094) ||
	    (!strncmp(line, "DISPLAY ", 8) && bf_blitver() < 3098)) {
		lg(HIBR_LDBG, "frames: Blit %ld does not know \"%s\"", bf_blitver(), line);
		return 1;
	}
	do
		k = send(fd, line, n, MSG_NOSIGNAL | MSG_DONTWAIT);
	while (k < 0 && errno == EINTR);
	return k == (ssize_t)n;
}

/* What the far side has said since: errors into the log, HUD1 into
   *hud when that is not null. */
int bf_drain(int fd, int *hud)
{
	char r[128], *l, *e;
	ssize_t k;
	int bad;

	if (fd < 0)
		return -1;
	for (;;) {
		k = recv(fd, r, sizeof r - 1, MSG_DONTWAIT);
		if (k > 0) {
			r[k] = 0;
			bad = 0;
			for (l = r; *l; l = e) {
				e = strchr(l, '\n');
				if (e)
					*e++ = 0;
				else
					e = l + strlen(l);
				if (*l && bf_line(l, hud) < 0)
					bad = 1;
			}
			if (bad)
				return -1;
			continue;
		}
		if (k == 0)
			return -1;
		if (errno == EINTR)
			continue;
		return errno == EAGAIN || errno == EWOULDBLOCK ? 0 : -1;
	}
}

/* The longest prefix of p, at most max bytes, that ends on a character. */
size_t bf_cut(const char *p, size_t n, size_t max)
{
	if (n <= max)
		return n;
	n = max;
	while (n && ((unsigned char)p[n] & 0xc0) == 0x80)
		n--;
	return n;
}

/* Send the panel its status (Blit ADR 0202 version 2): the lines of the
   n bytes at p, at most BF_STLINES of them and each cut to BF_STBYTES on a
   character, a control character made a space, an empty line skipped. 1
   when it went, or when the socket was full and it was dropped -- the next
   second's replaces it; 0 when there was nothing to send or it has gone. */
int bf_status(int fd, const char *p, size_t n)
{
	str m;
	size_t i, j, ll;
	unsigned long cnt = 0;
	ssize_t k;
	unsigned char c;

	if (fd < 0)
		return 0;
	s_init(&m);
	s_add(&m, "STA1\0\0\0\0", 8);
	for (i = 0; i < n && cnt < BF_STLINES; i = j + 1) {
		for (j = i; j < n && p[j] != '\n'; j++)
			;
		ll = bf_cut(p + i, j - i, BF_STBYTES);
		if (!ll)
			continue;
		for (k = 0; (size_t)k < ll; k++) {
			c = (unsigned char)p[i + k];
			s_ch(&m, c < 0x20 || c == 0x7f ? ' ' : (char)c);
		}
		s_ch(&m, '\n');
		cnt++;
	}
	if (!cnt) {
		s_free(&m);
		return 0;
	}
	bf_put32((unsigned char *)m.p + 4, cnt);
	do
		k = send(fd, m.p, m.n, MSG_NOSIGNAL | MSG_DONTWAIT);
	while (k < 0 && errno == EINTR);
	s_free(&m);
	if (k < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
		lg(HIBR_LDBG, "frames: the socket was full, a status dropped");
		return 1;
	}
	return k > 0;
}

#else

/* Not Linux: no frame socket, so never connected. */
int bf_dial(const char *sock, const char *tok)
{
	(void)sock;
	(void)tok;
	return -1;
}

/* Not Linux: no frame socket. */
int bf_open(void)
{
	return -1;
}

/* Not Linux: no dmabufs either. */
int bf_dmabuf, bf_fence;
char bf_formats[1];
void (*bf_answer)(const char *line);
int bf_send3(int fd, const unsigned char *h, const int *fds, int n)
{
	(void)fd; (void)h; (void)fds; (void)n;
	return 0;
}

/* Not Linux: no memfd. */
int bf_memfd(const unsigned char *p, size_t n)
{
	(void)p;
	(void)n;
	return -1;
}

/* Not Linux: a header nobody will read. */
void bf_hdr(unsigned char *h, unsigned id, unsigned long long seq, int w,
	    int ht)
{
	(void)id;
	(void)seq;
	(void)w;
	(void)ht;
	memset(h, 0, BF_HDR);
}

/* Not Linux: never sent. */
int bf_send(int fd, const unsigned char *h, int mfd)
{
	(void)fd;
	(void)h;
	(void)mfd;
	return 0;
}

/* Not Linux: never sent. */
int bf_say(int fd, const char *line)
{
	(void)fd;
	(void)line;
	return 0;
}

/* Not Linux: never connected. */
int bf_drain(int fd, int *hud)
{
	(void)fd;
	(void)hud;
	return -1;
}

/* Not Linux: never sent. */
int bf_status(int fd, const char *p, size_t n)
{
	(void)fd;
	(void)p;
	(void)n;
	return 0;
}

#endif
