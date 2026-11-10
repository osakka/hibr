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
 *   frames input N on|off      from the window: whether it has the keys
 *   frames input N hold confine|relative   how a held pointer behaves (Blit 0.3.70)
 *   frames info N              what has passed, one line of key=value
 *   frames stop N              the socket closed and the picture gone
 *   frames list                every socket, "N SOCKET" a line
 *
 * And a program's windows (Gitea #494, Blit ADR 0228): a socket served with
 * -w is one a compositor speaks to, blit-compositor, at version 4, and it
 * is answered window=1. Each window it opens is a number of its own, as a
 * single picture's server is, so place, input and info take it the same
 * way, and the desktop never sees the compositor's ids:
 *
 *   frames serve -w [-n NAME -t TOKEN]   NAME and TOKEN kept across a restart
 *   frames events N            the compositor's news since last asked, a
 *                              line each: display NAME, new W PARENT|0,
 *                              title W TEXT, appid W TEXT, limits W ...,
 *                              decor W server|client, fullscreen W on|off,
 *                              maximize W on|off, minimize W, hide W,
 *                              parent W P|0, popup W PARENT X Y,
 *                              cursor W NAME, gone W -- W a window's number
 *   frames window W size PW PH [fullscreen] [activated] [maximized]
 *   frames window W close|output PW PH MHZ   said to the compositor
 *   frames quit N              the person quit: the compositor ends
 *   frames wait N MS           until the compositor has named its display,
 *                              no longer than MS: what the desktop starts
 *                              before its first frame needs the name
 *   frames input W member P X Y   a popup's picture joins its top-level's
 *                              input route, at X Y in P's surface pixels
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
#include <poll.h>
#include <sys/socket.h>
#include <time.h>
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
	unsigned ibid;		/* and the one its input route named */
	char *input;		/* what Blit said of the route: ok, a code, or null */
	unsigned long long frames, seq, rel, errs, refused;
	int dmabuf;		/* what its hello was answered */
	unsigned char *rgb;	/* the newest frame, where only RGB can go */
	int iw, ih;
	unsigned long long rgen;
	unsigned long fw, fh;	/* the newest frame's size in pixels */
	int win;		/* serves a compositor's windows (-w) */
	struct fl_srv *par;	/* a window's server; null for a server */
	str ev;			/* a server's news for the desktop */
	char *fixn, *fixt;	/* a name and token asked for (-n, -t) */
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

/* The descriptor a record's producer speaks on: a window's is its server's. */
int fl_cfd(fl_srv *f)
{
	return f->par ? f->par->cfd : f->cfd;
}

/* The words of a line on the frame channel, at version 4: its word, its
   verb, the id it is about and the rest. The id is the third word, except
   in an input route's socket line, where the path comes first and the id
   last. 1 when there are all three. Every reader of a word's place goes
   through this, so version 5's one grammar changes it alone. */
int fl_wsplit(const char *line, size_t n, str *word, str *verb,
	      unsigned long *id, str *rest)
{
	const char *p = line, *e = line + n, *q;

	while (e > p && (e[-1] == '\n' || e[-1] == '\r'))
		e--;
	word->n = verb->n = rest->n = 0;
	*id = 0;
	for (q = p; q < e && *q != ' '; q++)
		;
	s_add(word, p, (size_t)(q - p));
	if (q >= e)
		return 0;
	for (p = ++q; q < e && *q != ' '; q++)
		;
	s_add(verb, p, (size_t)(q - p));
	if (q >= e)
		return 0;
	p = q + 1;
	if (!strcmp(word->p, "WINDOW") && !strcmp(verb->p, "display")) {
		s_add(rest, p, (size_t)(e - p));
		return rest->n > 0;
	}
	if (!strcmp(word->p, "INPUT") &&
	    (!strcmp(verb->p, "eis") || !strcmp(verb->p, "sock"))) {
		for (q = e; q > p && q[-1] != ' '; q--)
			;
		if (q <= p)
			return 0;
		*id = strtoul(q, 0, 10);
		s_add(rest, p, (size_t)(q - 1 - p));
		return 1;
	}
	*id = strtoul(p, (char **)&q, 10);
	if (q == p)
		return 0;
	if (q < e && *q == ' ')
		q++;
	s_add(rest, q, (size_t)(e - q));
	return 1;
}

/* A line for the frame channel, at version 4, the other half of fl_wsplit:
   WORD VERB ID REST, or for an input route's socket WORD VERB REST ID. */
void fl_wfmt(str *o, const char *word, const char *verb, unsigned long id,
	     const char *rest)
{
	s_cat(o, word);
	s_ch(o, ' ');
	s_cat(o, verb);
	s_ch(o, ' ');
	if (!strcmp(word, "INPUT") &&
	    (!strcmp(verb, "eis") || !strcmp(verb, "sock"))) {
		if (rest && *rest) {
			s_cat(o, rest);
			s_ch(o, ' ');
		}
		s_num(o, (long)id);
	} else {
		s_num(o, (long)id);
		if (rest && *rest) {
			s_ch(o, ' ');
			s_cat(o, rest);
		}
	}
	s_ch(o, '\n');
}

/* Say a line to the producer. */
void fl_say(fl_srv *f, const char *line)
{
#ifdef __linux__
	int fd = fl_cfd(f);

	if (fd >= 0)
		send(fd, line, strlen(line), MSG_NOSIGNAL | MSG_DONTWAIT);
#else
	(void)f;
	(void)line;
#endif
}

/* A line of news for the desktop, from a compositor's server; waking it is
   the reader's to ask. */
void fl_ev(fl_srv *f, const char *verb, unsigned n, const char *rest)
{
	s_cat(&f->ev, verb);
	if (n) {
		s_ch(&f->ev, ' ');
		s_num(&f->ev, (long)n);
	}
	if (rest && *rest) {
		s_ch(&f->ev, ' ');
		s_cat(&f->ev, rest);
	}
	s_ch(&f->ev, '\n');
}

/* A server's window with the compositor's id bid, or null. */
fl_srv *fl_win(fl_srv *f, unsigned long bid)
{
	size_t i;
	fl_srv *w;

	for (i = 0; i < fl_srvs.n; i++) {
		w = fl_srvs.p[i];
		if (w->par == f && w->bid == bid)
			return w;
	}
	return 0;
}

/* A new window under a server, numbered as any stream is. */
fl_srv *fl_newwin(fl_srv *f, unsigned long bid)
{
	fl_srv *w = xm(sizeof *w);

	memset(w, 0, sizeof *w);
	w->s = f->s;
	w->n = ++fl_next;
	w->lfd = w->cfd = -1;
	w->par = f;
	w->ok = 1;
	w->ver = f->ver;
	w->dmabuf = f->dmabuf;
	w->bid = w->ibid = (unsigned)bid;
	s_init(&w->ev);
	v_add(&fl_srvs, w);
	lg(HIBR_LDBG, "frames %u: window %lu is %u", f->n, bid, w->n);
	return w;
}

/* A window gone: its picture and its record, out of the list by identity. */
void fl_winfree(fl_srv *w)
{
	size_t i;

	for (i = 0; i < fl_srvs.n; i++)
		if (fl_srvs.p[i] == w) {
			fl_srvs.p[i] = fl_srvs.p[--fl_srvs.n];
			break;
		}
	fl_dpf->drop(w->s, w->n);
	free(w->rgb);
	free(w->input);
	s_free(&w->ev);
	free(w);
}

/* Every window of a server gone, each said to the desktop. */
void fl_winsall(fl_srv *f)
{
	size_t i;
	fl_srv *w;

	for (i = fl_srvs.n; i-- > 0;) {
		if (i >= fl_srvs.n)
			continue;
		w = fl_srvs.p[i];
		if (w->par != f)
			continue;
		fl_ev(f, "gone", w->n, 0);
		fl_winfree(w);
	}
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
	fl_winsall(f);
	fl_dpf->drop(f->s, f->n);
	free(f->rgb);
	f->rgb = 0;
	free(f->input);
	f->input = 0;
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
	int ok = !strncmp(line, "OK input ", 9);
	int in = ok || !strncmp(line, "ERROR INPUT ", 12);

	(void)ctx;
	/* An input route's answer names the stream third, after its two
	   words, and goes back under the id the producer's route named. */
	p = in ? line + (ok ? 8 : 11) : strchr(line, ' ');
	if (!p)
		return;
	sid = strtoul(p + 1, &e, 10);
	f = fl_find((unsigned)sid);
	if (!f || fl_cfd(f) < 0)
		return;
	if (in) {
		free(f->input);
		f->input = xs(ok ? "ok" : *e == ' ' ? e + 1 : e);
	} else if (line[0] == 'R') {
		f->rel++;
	} else {
		f->errs++;
	}
	s_init(&o);
	s_add(&o, line, (size_t)(p + 1 - line));
	s_num(&o, (long)(in ? f->ibid : f->bid));
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
	if (k < 16 || memcmp(m, "BLITFRM1", 8) || ver < 1 || ver > 4) {
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
	} else {
		s_cat(&o, "OK dmabuf=0");
	}
	if (ver >= 4)
		s_cat(&o, f->win ? " window=1" : " window=0");
	s_ch(&o, '\n');
	fl_say(f, o.p);
	lg(HIBR_LDBG, "frames %u: hello at version %lu, %s", f->n, ver, o.p);
	s_free(&o);
	s_free(&fm);
	return 1;
}

/* An input route's line from the producer (Blit 0.3.64): "INPUT eis
   <path> <id>", "INPUT sock <path> <id>" (blit-compositor, which Blit
   writes the input to itself) or "INPUT on|off <id>", the id its own --
   a window's, from a compositor. Handed to the console under the stream,
   which tells every display's Blit once a frame has gone; what Blit says
   comes back by fl_ans. Whether the route is on is the window's to say,
   from its focus: a producer that turned it on itself would take the keys
   before its window had them (Blit 0.3.65). */
void fl_input(fl_srv *f, const char *m, size_t k)
{
	str w, v, r, o;
	unsigned long id = 0;
	fl_srv *t = f;
	int ok = 0, split;

	s_init(&w);
	s_init(&v);
	s_init(&r);
	split = fl_wsplit(m, k, &w, &v, &id, &r);
	if (split && (!strcmp(v.p, "on") || !strcmp(v.p, "off"))) {
		lg(HIBR_LDBG, "frames %u: the producer's INPUT %s left to the "
			      "window", f->n, v.p);
		ok = 1;
	} else if (split) {
		if (f->win)
			t = fl_win(f, id);
		if (t && r.n && !strchr(r.p, '\n') &&
		    (!strcmp(v.p, "eis") || !strcmp(v.p, "sock")) &&
		    fl_dpf->input(f->s, t->n, v.p, r.p)) {
			t->ibid = (unsigned)id;
			ok = 1;
			lg(HIBR_LDBG, "frames %u: input %s for %lu", t->n, v.p, id);
		}
	}
	if (!ok) {
		s_init(&o);
		fl_wfmt(&o, "ERROR", "INPUT", id, "EINVAL");
		fl_say(f, o.p);
		s_free(&o);
	}
	s_free(&w);
	s_free(&v);
	s_free(&r);
}

/* The number a compositor's window id is under here, as text: 0 for none. */
void fl_wnum(fl_srv *f, const char *idtext, str *o)
{
	fl_srv *w = fl_win(f, strtoul(idtext, 0, 10));

	s_num(o, w ? (long)w->n : 0L);
}

/* A WINDOW line from a compositor (Blit ADR 0228): kept as news for the
   desktop in its own numbers, a window made for a new one or a popup and
   taken away for a gone one. 1: the desktop wakes to read it. */
int fl_window(fl_srv *f, const char *m, size_t k)
{
	str w, v, r, o;
	unsigned long id;
	fl_srv *t;
	char *sp;

	s_init(&w);
	s_init(&v);
	s_init(&r);
	s_init(&o);
	if (!fl_wsplit(m, k, &w, &v, &id, &r)) {
		lg(HIBR_LWRN, "frames %u: a WINDOW line not understood", f->n);
	} else if (!strcmp(v.p, "display")) {
		fl_ev(f, "display", 0, r.p);
	} else if (!strcmp(v.p, "new") || !strcmp(v.p, "popup")) {
		t = fl_win(f, id);
		if (!t)
			t = fl_newwin(f, id);
		sp = strchr(r.p, ' ');
		if (sp)
			*sp = 0;
		fl_wnum(f, r.p, &o);
		if (sp) {
			s_ch(&o, ' ');
			s_cat(&o, sp + 1);
		}
		fl_ev(f, v.p, t->n, o.p);
	} else if ((t = fl_win(f, id)) == 0) {
		lg(HIBR_LDBG, "frames %u: WINDOW %s for %lu, which is not here",
		   f->n, v.p, id);
	} else if (!strcmp(v.p, "gone")) {
		fl_ev(f, "gone", t->n, 0);
		fl_winfree(t);
	} else if (!strcmp(v.p, "parent")) {
		fl_wnum(f, r.p, &o);
		fl_ev(f, "parent", t->n, o.p);
	} else if (!strcmp(v.p, "title") || !strcmp(v.p, "appid") ||
		   !strcmp(v.p, "limits") || !strcmp(v.p, "decor") ||
		   !strcmp(v.p, "fullscreen") || !strcmp(v.p, "maximize") ||
		   !strcmp(v.p, "minimize") || !strcmp(v.p, "hide") ||
		   !strcmp(v.p, "cursor") || !strcmp(v.p, "lock")) {
		fl_ev(f, v.p, t->n, r.p);
	} else {
		lg(HIBR_LDBG, "frames %u: WINDOW %s not known here", f->n, v.p);
	}
	s_free(&w);
	s_free(&v);
	s_free(&r);
	s_free(&o);
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
		fl_srv *t = f;
		unsigned long b = fl_u32(fl_buf + 4);

		if (f->win)
			t = fl_win(f, b);
		if (!t) {
			f->refused++;
			s_init(&o);
			s_cat(&o, "ERROR ");
			s_num(&o, (long)b);
			s_cat(&o, " ENOENT\n");
			fl_say(f, o.p);
			s_free(&o);
			for (k = 0; k < nfd; k++)
				close(fds[k]);
			return 0;
		}
		t->bid = (unsigned)b;
		t->seq = (unsigned long long)fl_u32(fl_buf + 8) |
			 (unsigned long long)fl_u32(fl_buf + 12) << 32;
		if (t->fw != fl_u32(fl_buf + 16) || t->fh != fl_u32(fl_buf + 20))
			wake = 1;
		t->fw = fl_u32(fl_buf + 16);
		t->fh = fl_u32(fl_buf + 20);
		if (!t->frames && !t->rgen)
			wake = 1;
		ok = fl_dpf->frame(f->s, t->n, fl_buf, (size_t)got, fds, nfd);
		if (ok) {
			t->frames++;
		} else if (got == BF_HDR && fl_rgb(t, fl_buf, fds[0])) {
			wake = 1;
		} else {
			t->refused++;
			s_init(&o);
			s_cat(&o, "ERROR ");
			s_num(&o, (long)t->bid);
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
	if (got > 6 && !memcmp(fl_buf, "INPUT ", 6)) {
		fl_input(f, (const char *)fl_buf, (size_t)got);
		return 0;
	}
	if (f->win && got > 7 && !memcmp(fl_buf, "WINDOW ", 7))
		return fl_window(f, (const char *)fl_buf, (size_t)got);
	if ((got > 3 && !memcmp(fl_buf, "OK ", 3)) ||
	    (got > 6 && !memcmp(fl_buf, "ERROR ", 6))) {
		lg(HIBR_LDBG, "frames %u: the compositor said %.*s", f->n,
		   (int)got, (const char *)fl_buf);
		return 0;
	}
	if (got < 4 || memcmp(fl_buf, "STA1", 4))
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
	f->tok = xs(f->fixt ? f->fixt : o.p);
	o.n = 0;
	s_cat(&o, "@hibr-frames-");
	s_num(&o, (long)getpid());
	s_ch(&o, '-');
	s_num(&o, (long)f->n);
	f->sock = xs(f->fixn ? f->fixn : o.p);
	s_free(&o);
	if (f->sock[0] != '@') {
		lg(HIBR_LERR, "frames: %s: a name begins with @", f->sock);
		return 0;
	}
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

/* Free a server and everything it holds, its windows too. */
void fl_free(fl_srv *f)
{
	if (f->cfd >= 0)
		fl_hangup(f);
	fl_winsall(f);
	s_free(&f->ev);
	free(f->fixn);
	free(f->fixt);
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
		lg(HIBR_LERR, "usage: frames serve|place|input|info|stop|list ...");
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
		s_init(&f->ev);
		for (i = 2; i < (size_t)ac; i++) {
			if (!strcmp(av[i], "-w"))
				f->win = 1;
			else if (!strcmp(av[i], "-n") && i + 1 < (size_t)ac)
				f->fixn = xs(av[++i]);
			else if (!strcmp(av[i], "-t") && i + 1 < (size_t)ac)
				f->fixt = xs(av[++i]);
		}
		if (!fl_listen(f)) {
			lg(HIBR_LERR, "frames: cannot listen");
			free(f->sock);
			free(f->tok);
			free(f->fixn);
			free(f->fixt);
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
			if (f->par)
				continue;
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
	if (!strcmp(av[1], "events")) {
		if (f->ev.n && f->ev.p[f->ev.n - 1] == '\n')
			f->ev.p[--f->ev.n] = 0;
		fl_out(s, &f->ev);
		f->ev.n = 0;
		if (f->ev.p)
			f->ev.p[0] = 0;
		s_free(&o);
		return HIBR_OK;
	}
	if (!strcmp(av[1], "wait")) {
#ifdef __linux__
		struct pollfd pf[2];
		struct timespec t0, t1;
		long ms = ac > 3 ? atol(av[3]) : 1000, el = 0;
		int np;

		clock_gettime(CLOCK_MONOTONIC, &t0);
		while (!strstr(f->ev.p ? f->ev.p : "", "display ") && el < ms) {
			np = 0;
			if (f->cfd >= 0) {
				pf[np].fd = f->cfd;
				pf[np++].events = POLLIN;
			} else if (f->lfd >= 0) {
				pf[np].fd = f->lfd;
				pf[np++].events = POLLIN;
			}
			if (!np || poll(pf, (nfds_t)np, (int)(ms - el)) < 0) {
				if (errno != EINTR)
					break;
			} else if (np && (pf[0].revents & (POLLIN | POLLHUP))) {
				if (f->cfd >= 0)
					fl_read(f, f->cfd);
				else
					fl_accept(f, f->lfd);
			}
			clock_gettime(CLOCK_MONOTONIC, &t1);
			el = (t1.tv_sec - t0.tv_sec) * 1000 +
			     (t1.tv_nsec - t0.tv_nsec) / 1000000;
		}
		s_free(&o);
		return strstr(f->ev.p ? f->ev.p : "", "display ") ? HIBR_OK : HIBR_FAIL;
#else
		s_free(&o);
		return HIBR_FAIL;
#endif
	}
	if (!strcmp(av[1], "window") || !strcmp(av[1], "quit")) {
		str rest;
		int a;

		if (!strcmp(av[1], "quit")) {
			if (f->par) {
				lg(HIBR_LERR, "frames quit: %s is a window, not a server", av[2]);
				s_free(&o);
				return 2;
			}
			fl_wfmt(&o, "WINDOW", "quit", 0, "");
			fl_say(f, o.p);
			s_free(&o);
			return HIBR_OK;
		}
		if (!f->par || ac < 4) {
			lg(HIBR_LERR, "usage: frames window W size|close|output ...");
			s_free(&o);
			return 2;
		}
		s_init(&rest);
		for (a = 4; a < ac; a++) {
			if (rest.n)
				s_ch(&rest, ' ');
			s_cat(&rest, av[a]);
		}
		fl_wfmt(&o, "WINDOW", av[3], f->bid, rest.p ? rest.p : "");
		fl_say(f, o.p);
		s_free(&rest);
		s_free(&o);
		return fl_cfd(f) >= 0 ? HIBR_OK : HIBR_FAIL;
	}
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
	if (!strcmp(av[1], "input")) {
		if (ac == 5 && !strcmp(av[3], "hold") &&
		    (!strcmp(av[4], "confine") || !strcmp(av[4], "relative"))) {
			s_free(&o);
			return fl_dpf->input(s, f->n, "hold", av[4]) ? HIBR_OK : HIBR_FAIL;
		}
		if (ac == 7 && !strcmp(av[3], "member")) {
			s_cat(&o, av[4]);
			s_ch(&o, ' ');
			s_cat(&o, av[5]);
			s_ch(&o, ' ');
			s_cat(&o, av[6]);
			i = (size_t)fl_dpf->input(s, f->n, "member", o.p);
			s_free(&o);
			return i ? HIBR_OK : HIBR_FAIL;
		}
		if (ac < 4 || (strcmp(av[3], "on") && strcmp(av[3], "off"))) {
			lg(HIBR_LERR, "usage: frames input N on|off|hold confine|relative");
			s_free(&o);
			return 2;
		}
		s_free(&o);
		return fl_dpf->input(s, f->n, av[3], 0) ? HIBR_OK : HIBR_FAIL;
	}
	if (!strcmp(av[1], "info")) {
		s_cat(&o, "connected=");
		s_num(&o, fl_cfd(f) >= 0 && f->ok);
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
		s_cat(&o, " input=");
		s_cat(&o, f->input ? f->input : "none");
		s_cat(&o, " width=");
		s_num(&o, (long)f->fw);
		s_cat(&o, " height=");
		s_num(&o, (long)f->fh);
		fl_out(s, &o);
		s_free(&o);
		return HIBR_OK;
	}
	if (!strcmp(av[1], "stop")) {
		if (f->par) {
			lg(HIBR_LERR, "frames stop: %s is a window; close it with frames window", av[2]);
			s_free(&o);
			return 2;
		}
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
