/* frames -- the socket a window's picture is sent to, as Blit's own.
 *
 * Gitea #486, Blit ADR 0223. A program that draws its pictures into
 * buffers -- a game, through Blit's gamescope bridge -- speaks Blit's frame
 * wire to whatever BLIT_FRAME_SOCKET and BLIT_FRAME_TOKEN name. In a hibr
 * window those name this module's socket instead of Blit's, and its frames
 * go through the console (the "dpframes" interface) onto the picture the
 * window places, so a window over it, a move or a close behaves as for any
 * picture, and every display of a held desktop is sent them:
 *
 *   frames serve              a socket and token: "N SOCKET TOKEN"
 *   frames place N PANE ROW COL H W
 *                              from the window's drawing: its picture here
 *   frames info N              what has passed, one line of key=value
 *   frames stop N              the socket closed and the picture gone
 *   frames list                every socket, "N SOCKET" a line
 *
 * The wire is Blit's, from the producer's side unchanged: a hello with the
 * token at version 1 to 3, answered with what every display can import;
 * FRM3 frames by dmabuf and FRM1 frames by sealed memfd; RELEASE and ERROR
 * back in the producer's own ids. Nothing here maps a dmabuf. A memfd of
 * RGB is mapped only where frames by descriptor cannot go at all -- a
 * plain terminal -- and is then drawn as any picture is, so the window
 * always shows something.
 *
 * The console's wait reads the sockets (its hooks), so a frame and its
 * RELEASE pass without the script running; it wakes for a hello, a hang
 * up, a first frame and a frame it has to draw itself.
 */
#include "hibr.h"
#include "display.h"
#include "blitfr.h"
#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifdef __linux__
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/un.h>
#endif

#ifndef FL_MSGMAX
#define FL_MSGMAX 4096
#endif

typedef struct fl_srv {
	sh *s;
	unsigned n;		/* its number, which is its stream's too */
	int lfd, cfd;
	int ok, ver;		/* the hello was taken, at this version */
	char *sock, *tok;
	unsigned bid;		/* the producer's own id for its picture */
	unsigned long long frames, seq, rel, errs, refused;
	int dmabuf;		/* what its hello was answered */
	unsigned char *rgb;	/* the newest frame, where only RGB can go */
	int iw, ih;
	unsigned long long rgen;
} fl_srv;

static vec fl_srvs;
static unsigned fl_next;
static const dp_api *fl_dp;
static const dpf_api *fl_dpf;
static unsigned char *fl_buf;

/* The server numbered n, or null. */
fl_srv *fl_find(unsigned n)
{
	size_t i;

	for (i = 0; i < fl_srvs.n; i++)
		if (((fl_srv *)fl_srvs.p[i])->n == n)
			return fl_srvs.p[i];
	return 0;
}

/* Say a line to the producer. */
void fl_say(fl_srv *f, const char *line)
{
#ifdef __linux__
	if (f->cfd >= 0)
		send(f->cfd, line, strlen(line), MSG_NOSIGNAL | MSG_DONTWAIT);
#else
	(void)f;
	(void)line;
#endif
}

/* The producer has gone: its picture goes with it. */
void fl_hangup(fl_srv *f)
{
	if (f->cfd >= 0) {
		fl_dpf->unhook(f->cfd);
		close(f->cfd);
	}
	f->cfd = -1;
	f->ok = 0;
	fl_dpf->drop(f->s, f->n);
	free(f->rgb);
	f->rgb = 0;
	lg(HIBR_LDBG, "frames %u: the producer hung up", f->n);
}

/* What a display's Blit said of a stream's frame, in the producer's id. */
void fl_ans(void *ctx, const char *line)
{
	const char *p;
	unsigned long sid;
	char *e;
	fl_srv *f;
	str o;

	(void)ctx;
	p = strchr(line, ' ');
	if (!p)
		return;
	sid = strtoul(p + 1, &e, 10);
	f = fl_find((unsigned)sid);
	if (!f || f->cfd < 0)
		return;
	if (!strncmp(line, "RELEASE", 7))
		f->rel++;
	else
		f->errs++;
	s_init(&o);
	s_add(&o, line, (size_t)(p + 1 - line));
	s_num(&o, (long)f->bid);
	s_cat(&o, e);
	s_ch(&o, '\n');
	fl_say(f, o.p);
	s_free(&o);
}

/* A 32-bit field of the wire. */
unsigned long fl_u32(const unsigned char *p)
{
	return (unsigned long)p[0] | (unsigned long)p[1] << 8 |
	       (unsigned long)p[2] << 16 | (unsigned long)p[3] << 24;
}

/* The hello: the token at a version this speaks, answered with what every
   display can import. 1 when taken. */
int fl_hello(fl_srv *f, const unsigned char *m, size_t k)
{
	unsigned long ver, tl;
	str fm, o;
	int fence = 0, can;

	ver = k >= 16 ? fl_u32(m + 8) : 0;
	tl = k >= 16 ? fl_u32(m + 12) : 0;
	if (k < 16 || memcmp(m, "BLITFRM1", 8) || ver < 1 || ver > 3) {
		fl_say(f, "ERROR 0 EINVAL\n");
		return 0;
	}
	if (tl != strlen(f->tok) || 16 + tl > k || memcmp(m + 16, f->tok, tl)) {
		fl_say(f, "ERROR 0 EACCES\n");
		return 0;
	}
	f->ver = (int)ver;
	if (ver < 3) {
		fl_say(f, "OK\n");
		return 1;
	}
	s_init(&fm);
	s_init(&o);
	can = fl_dpf->can(f->s, &fm, &fence);
	f->dmabuf = can == 2 && fm.n;
	if (f->dmabuf) {
		s_cat(&o, "OK dmabuf=1 fence=");
		s_num(&o, fence ? 1 : 0);
		s_cat(&o, " formats=");
		s_add(&o, fm.p, fm.n);
		s_ch(&o, '\n');
	} else {
		s_cat(&o, "OK dmabuf=0\n");
	}
	fl_say(f, o.p);
	lg(HIBR_LDBG, "frames %u: hello at version %lu, %s", f->n, ver, o.p);
	s_free(&o);
	s_free(&fm);
	return 1;
}

#ifdef __linux__
/* A frame of RGB by memfd, mapped and kept to be drawn as any picture,
   where frames by descriptor cannot go: the window still shows. 1 when
   kept. */
int fl_rgb(fl_srv *f, const unsigned char *h, int mfd)
{
	unsigned long w = fl_u32(h + 16), ht = fl_u32(h + 20);
	unsigned long fmt = fl_u32(h + 24), st = fl_u32(h + 28);
	unsigned long x = fl_u32(h + 32), y = fl_u32(h + 36);
	unsigned long rw = fl_u32(h + 40), rh = fl_u32(h + 44);
	unsigned long r, c, bpp = fmt == 32 ? 4 : 3;
	off_t sz = lseek(mfd, 0, SEEK_END);
	unsigned char *m;

	if ((fmt != 24 && fmt != 32) || !w || !ht || w > 16384 || ht > 16384)
		return 0;
	if (!rw || !rh) {
		x = y = 0;
		rw = w;
		rh = ht;
	}
	if (x + rw > w || y + rh > ht || st < rw * bpp ||
	    sz < (off_t)(st * rh))
		return 0;
	if ((!rw || rw != w || rh != ht) &&
	    (!f->rgb || f->iw != (int)w || f->ih != (int)ht))
		return 0;
	m = mmap(0, (size_t)sz, PROT_READ, MAP_SHARED, mfd, 0);
	if (m == MAP_FAILED)
		return 0;
	if (!f->rgb || f->iw != (int)w || f->ih != (int)ht) {
		free(f->rgb);
		f->rgb = xm((size_t)w * ht * 3);
		f->iw = (int)w;
		f->ih = (int)ht;
	}
	for (r = 0; r < rh; r++)
		for (c = 0; c < rw; c++) {
			const unsigned char *sp = m + r * st + c * bpp;
			unsigned char *dp = f->rgb + ((y + r) * w + x + c) * 3;

			dp[0] = sp[0];
			dp[1] = sp[1];
			dp[2] = sp[2];
		}
	munmap(m, (size_t)sz);
	f->rgen++;
	return 1;
}

/* Readable: a message from the producer. 1 when the script should wake. */
int fl_read(void *ctx, int fd)
{
	fl_srv *f = ctx;
	struct msghdr mh;
	struct iovec v;
	struct cmsghdr *c;
	union {
		char b[CMSG_SPACE(sizeof(int) * BF_MAXFD)];
		struct cmsghdr align;
	} u;
	int fds[BF_MAXFD], nfd = 0, k, wake = 0, ok;
	ssize_t got;
	str o;

	memset(&mh, 0, sizeof mh);
	memset(&u, 0, sizeof u);
	v.iov_base = fl_buf;
	v.iov_len = FL_MSGMAX;
	mh.msg_iov = &v;
	mh.msg_iovlen = 1;
	mh.msg_control = u.b;
	mh.msg_controllen = sizeof u.b;
	do
		got = recvmsg(fd, &mh, MSG_DONTWAIT | MSG_CMSG_CLOEXEC);
	while (got < 0 && errno == EINTR);
	if (got < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
		return 0;
	for (c = CMSG_FIRSTHDR(&mh); c; c = CMSG_NXTHDR(&mh, c))
		if (c->cmsg_level == SOL_SOCKET && c->cmsg_type == SCM_RIGHTS) {
			int m = (int)((c->cmsg_len - CMSG_LEN(0)) / sizeof(int));

			for (k = 0; k < m; k++) {
				int d;

				memcpy(&d, CMSG_DATA(c) + k * sizeof(int),
				       sizeof d);
				if (nfd < BF_MAXFD)
					fds[nfd++] = d;
				else
					close(d);
			}
		}
	if (got <= 0) {
		for (k = 0; k < nfd; k++)
			close(fds[k]);
		fl_hangup(f);
		return 1;
	}
	if (!f->ok) {
		for (k = 0; k < nfd; k++)
			close(fds[k]);
		if (!fl_hello(f, fl_buf, (size_t)got)) {
			fl_hangup(f);
			return 1;
		}
		f->ok = 1;
		return 1;
	}
	if ((got == BF_HDR3 && !memcmp(fl_buf, "FRM3", 4)) ||
	    (got == BF_HDR && !memcmp(fl_buf, "FRM1", 4) && nfd == 1)) {
		f->bid = (unsigned)fl_u32(fl_buf + 4);
		f->seq = (unsigned long long)fl_u32(fl_buf + 8) |
			 (unsigned long long)fl_u32(fl_buf + 12) << 32;
		if (!f->frames && !f->rgen)
			wake = 1;
		ok = fl_dpf->frame(f->s, f->n, fl_buf, (size_t)got, fds, nfd);
		if (ok) {
			f->frames++;
		} else if (got == BF_HDR && fl_rgb(f, fl_buf, fds[0])) {
			wake = 1;
		} else {
			f->refused++;
			s_init(&o);
			s_cat(&o, "ERROR ");
			s_num(&o, (long)f->bid);
			s_cat(&o, got == BF_HDR3 ? " ENOTSUP\n" : " EINVAL\n");
			fl_say(f, o.p);
			s_free(&o);
		}
		for (k = 0; k < nfd; k++)
			close(fds[k]);
		return wake;
	}
	for (k = 0; k < nfd; k++)
		close(fds[k]);
	if (got > 6 && !memcmp(fl_buf, "INPUT ", 6))
		fl_say(f, "ERROR INPUT 0 ENOTSUP\n");
	else if (got >= 4 && !memcmp(fl_buf, "STA1", 4))
		;
	else
		fl_say(f, "ERROR 0 EINVAL\n");
	return 0;
}

/* Readable: a producer connecting. One at a time; another is refused. */
int fl_accept(void *ctx, int fd)
{
	fl_srv *f = ctx;
	int c = accept(fd, 0, 0);

	if (c < 0)
		return 0;
	fcntl(c, F_SETFD, FD_CLOEXEC);
	if (f->cfd >= 0) {
		send(c, "ERROR 0 EBUSY\n", 14, MSG_NOSIGNAL | MSG_DONTWAIT);
		close(c);
		return 0;
	}
	f->cfd = c;
	f->ok = 0;
	fl_dpf->hook(c, fl_read, f);
	lg(HIBR_LDBG, "frames %u: a producer connected", f->n);
	return 0;
}

/* A socket and token for a new server. 1 when listening. */
int fl_listen(fl_srv *f)
{
	struct sockaddr_un a;
	unsigned char r[16];
	size_t i, sl;
	int rfd;
	str o;

	rfd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
	if (rfd < 0 || read(rfd, r, sizeof r) != (ssize_t)sizeof r) {
		if (rfd >= 0)
			close(rfd);
		return 0;
	}
	close(rfd);
	s_init(&o);
	for (i = 0; i < sizeof r; i++) {
		s_ch(&o, "0123456789abcdef"[r[i] >> 4]);
		s_ch(&o, "0123456789abcdef"[r[i] & 15]);
	}
	f->tok = xs(o.p);
	o.n = 0;
	s_cat(&o, "@hibr-frames-");
	s_num(&o, (long)getpid());
	s_ch(&o, '-');
	s_num(&o, (long)f->n);
	f->sock = xs(o.p);
	s_free(&o);
	sl = strlen(f->sock);
	f->lfd = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0);
	if (f->lfd < 0 || sl >= sizeof a.sun_path)
		return 0;
	memset(&a, 0, sizeof a);
	a.sun_family = AF_UNIX;
	memcpy(a.sun_path, f->sock, sl);
	a.sun_path[0] = 0;
	if (bind(f->lfd, (struct sockaddr *)&a,
		 (socklen_t)(offsetof(struct sockaddr_un, sun_path) + sl)) < 0 ||
	    listen(f->lfd, 4) < 0) {
		lg(HIBR_LERR, "frames: %s: %s", f->sock, strerror(errno));
		close(f->lfd);
		f->lfd = -1;
		return 0;
	}
	fl_dpf->hook(f->lfd, fl_accept, f);
	return 1;
}
#else
/* Not Linux: no sockets of this kind. */
int fl_listen(fl_srv *f)
{
	(void)f;
	return 0;
}
#endif

/* Free a server and everything it holds. */
void fl_free(fl_srv *f)
{
	if (f->cfd >= 0)
		fl_hangup(f);
	if (f->lfd >= 0) {
		fl_dpf->unhook(f->lfd);
		close(f->lfd);
	}
	fl_dpf->drop(f->s, f->n);
	free(f->sock);
	free(f->tok);
	free(f->rgb);
	free(f);
}

/* The display and its frames interface, asked for once. */
int fl_need(sh *s)
{
	if (fl_dpf)
		return 1;
	fl_dp = hibr_require(s, "display", DP_API_VER);
	if (fl_dp)
		fl_dpf = hibr_require(s, "dpframes", DPF_API_VER);
	if (!fl_dpf) {
		lg(HIBR_LERR, "frames: no display offers frames by descriptor");
		return 0;
	}
	if (!fl_buf)
		fl_buf = xm(FL_MSGMAX);
	fl_dpf->answers(fl_ans, 0);
	return 1;
}

/* An answer into the result slot, and printed unless := is catching it. */
void fl_out(sh *s, str *o)
{
	hibr_ret(s, o->p ? o->p : "");
	if (!s->bind && o->n)
		printf("%s\n", o->p);
}

/* frames: serve, place, info, stop, list. */
int m_frames(sh *s, int ac, char **av)
{
	fl_srv *f;
	str o;
	size_t i;

	if (ac < 2) {
		lg(HIBR_LERR, "usage: frames serve|place|info|stop|list ...");
		return 2;
	}
	if (!fl_need(s))
		return HIBR_FAIL;
	s_init(&o);
	if (!strcmp(av[1], "serve")) {
		f = xm(sizeof *f);
		memset(f, 0, sizeof *f);
		f->s = s;
		f->n = ++fl_next;
		f->lfd = f->cfd = -1;
		if (!fl_listen(f)) {
			lg(HIBR_LERR, "frames: cannot listen");
			free(f->sock);
			free(f->tok);
			free(f);
			return HIBR_FAIL;
		}
		v_add(&fl_srvs, f);
		s_num(&o, (long)f->n);
		s_ch(&o, ' ');
		s_cat(&o, f->sock);
		s_ch(&o, ' ');
		s_cat(&o, f->tok);
		fl_out(s, &o);
		s_free(&o);
		return HIBR_OK;
	}
	if (!strcmp(av[1], "list")) {
		for (i = 0; i < fl_srvs.n; i++) {
			f = fl_srvs.p[i];
			if (o.n)
				s_ch(&o, '\n');
			s_num(&o, (long)f->n);
			s_ch(&o, ' ');
			s_cat(&o, f->sock);
		}
		fl_out(s, &o);
		s_free(&o);
		return HIBR_OK;
	}
	f = ac > 2 ? fl_find((unsigned)strtoul(av[2], 0, 10)) : 0;
	if (!f) {
		lg(HIBR_LERR, "frames %s: no such server", av[1]);
		s_free(&o);
		return HIBR_FAIL;
	}
	f->s = s;
	if (!strcmp(av[1], "place")) {
		int r, c, h, w, ok = 0;

		if (ac < 8) {
			lg(HIBR_LERR, "usage: frames place N PANE ROW COL H W");
			s_free(&o);
			return 2;
		}
		r = atoi(av[4]);
		c = atoi(av[5]);
		h = atoi(av[6]);
		w = atoi(av[7]);
		if (f->frames)
			ok = fl_dpf->place(s, av[3], r, c, h, w, f->n);
		if (!ok && f->rgb)
			ok = fl_dp->image(s, av[3], r, c, h, w, f->rgb, f->iw,
					  f->ih, 0);
		s_free(&o);
		return ok ? HIBR_OK : HIBR_FAIL;
	}
	if (!strcmp(av[1], "info")) {
		s_cat(&o, "connected=");
		s_num(&o, f->cfd >= 0 && f->ok);
		s_cat(&o, " version=");
		s_num(&o, f->ver);
		s_cat(&o, " dmabuf=");
		s_num(&o, f->dmabuf);
		s_cat(&o, " frames=");
		s_num(&o, (long)f->frames);
		s_cat(&o, " seq=");
		s_num(&o, (long)f->seq);
		s_cat(&o, " released=");
		s_num(&o, (long)f->rel);
		s_cat(&o, " errors=");
		s_num(&o, (long)f->errs);
		s_cat(&o, " refused=");
		s_num(&o, (long)f->refused);
		s_cat(&o, " rgb=");
		s_num(&o, (long)f->rgen);
		fl_out(s, &o);
		s_free(&o);
		return HIBR_OK;
	}
	if (!strcmp(av[1], "stop")) {
		for (i = 0; i < fl_srvs.n; i++)
			if (fl_srvs.p[i] == f) {
				fl_srvs.p[i] = fl_srvs.p[fl_srvs.n - 1];
				fl_srvs.n--;
				break;
			}
		fl_free(f);
		s_free(&o);
		return HIBR_OK;
	}
	lg(HIBR_LERR, "frames: %s: no such verb", av[1]);
	s_free(&o);
	return 2;
}

/* Close every socket and let go of every picture. */
void fl_fini(sh *s)
{
	(void)s;
	while (fl_srvs.n)
		fl_free(fl_srvs.p[--fl_srvs.n]);
	if (fl_dpf)
		fl_dpf->answers(0, 0);
	free(fl_srvs.p);
	fl_srvs.p = 0;
	fl_srvs.n = 0;
	free(fl_buf);
	fl_buf = 0;
	fl_dpf = 0;
	fl_dp = 0;
}

const hibr_bi frames_bi[] = {
	{ "frames", m_frames, "a window's picture sent as Blit's frames" },
	HIBR_BI_END
};

HIBR_MODULE("frames", "0.1",
	    "the socket a window's picture is sent to, as Blit's own",
	    frames_bi, 0, fl_fini);
