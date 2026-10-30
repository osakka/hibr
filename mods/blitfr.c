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

/* Connect to a frame socket and say hello with a token. */
int bf_dial(const char *sock, const char *tok)
{
	struct sockaddr_un a;
	struct pollfd q;
	size_t sl, tl;
	unsigned char *m;
	char r[64];
	ssize_t k;
	int fd;

	if (!sock || !*sock || !tok || !*tok)
		return -1;
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
	bf_put32(m + 8, 1);
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
	if (poll(&q, 1, 2000) != 1 ||
	    (k = recv(fd, r, sizeof r - 1, 0)) < 3 || memcmp(r, "OK\n", 3)) {
		lg(HIBR_LDBG, "frames: %s refused the hello", sock);
		close(fd);
		return -1;
	}
	lg(HIBR_LDBG, "frames: %s open", sock);
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

/* What the far side has said since, into the log. */
int bf_drain(int fd)
{
	char r[128];
	ssize_t k;

	if (fd < 0)
		return -1;
	for (;;) {
		k = recv(fd, r, sizeof r - 1, MSG_DONTWAIT);
		if (k > 0) {
			r[k] = 0;
			lg(HIBR_LDBG, "frames: %s", r);
			continue;
		}
		if (k == 0)
			return -1;
		if (errno == EINTR)
			continue;
		return errno == EAGAIN || errno == EWOULDBLOCK ? 0 : -1;
	}
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
int bf_drain(int fd)
{
	(void)fd;
	return -1;
}

#endif
