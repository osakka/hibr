/* blitfr -- picture frames handed to Blit by descriptor (Blit ADR 0202).
 *
 * See blitfr.h for the wire. Every call here answers "no" rather than
 * failing loudly: a frame socket that is not there, refuses us or goes
 * away is a reason to put pictures back on the terminal, not an error a
 * person can do anything about.
 */
#include "blitfr.h"
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifdef __linux__
#include <sys/socket.h>
#include <sys/syscall.h>
#include <sys/un.h>

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

/* Put a 32-bit number into four bytes, little-endian. */
void bf_put32(unsigned char *p, unsigned long v)
{
	p[0] = (unsigned char)(v & 0xff);
	p[1] = (unsigned char)((v >> 8) & 0xff);
	p[2] = (unsigned char)((v >> 16) & 0xff);
	p[3] = (unsigned char)((v >> 24) & 0xff);
}

/* Connect to a frame socket and say hello with a token at wire version
   ver. -1 when refused; *inval set when the refusal was EINVAL, which is
   what a Blit older than 0.3.22 says to a version it does not know. */
int bf_hello(const char *sock, const char *tok, int ver, int *inval)
{
	struct sockaddr_un a;
	struct pollfd q;
	size_t sl, tl;
	unsigned char *m;
	char r[64];
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

/* Connect to a frame socket and say hello with a token: version 2, which
   is what lets Blit's performance panel ask for status (ADR 0203), and
   version 1 again where that is refused as unknown. */
int bf_dial(const char *sock, const char *tok)
{
	int fd, inval;

	if (!sock || !*sock || !tok || !*tok)
		return -1;
	fd = bf_hello(sock, tok, 2, &inval);
	if (fd < 0 && inval)
		fd = bf_hello(sock, tok, 1, &inval);
	return fd;
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
	lg(HIBR_LDBG, "frames: %s", l);
	/* The person this connection was opened for has signed out (Blit
	   0.3.19): it is closed, and pictures go back on the terminal. */
	return strstr(l, "EACCES") ? -1 : 0;
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
