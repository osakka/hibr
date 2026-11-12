/* frames -- the socket a window's picture is sent to, as Blit's own.
 *
 * Gitea #486, Blit ADR 0223. A program that draws its pictures into
 * buffers -- Blit's compositor for programs' windows (-w), and any other
 * producer of Blit's frame wire -- speaks it to whatever BLIT_FRAME_SOCKET
 * and BLIT_FRAME_TOKEN name. In a hibr window those name this module's
 * socket instead of Blit's, and its frames
 * go through the console (the "dpframes" interface) onto the picture the
 * window places, so a window over it, a move or a close behaves as for any
 * picture, and every display of a held desktop is sent them:
 *
 *   frames serve              a socket and token: "N SOCKET TOKEN"
 *   frames place N PANE ROW COL H W
 *                              from the window's drawing: its picture here
 *   frames input N on|off      from the window: whether it has the keys
 *   frames input N hold confine|relative   how a held pointer behaves (Blit 0.3.70)
 *   frames info N              what has passed, one line of key=value;
 *                              pid= the process connected, 0 for none
 *   frames stop N              the socket closed and the picture gone
 *   frames list                every socket, "N SOCKET" a line
 *
 * And a program's windows (Gitea #494, Blit ADR 0228): a socket served with
 * -w is one a compositor speaks to, blit-compositor, at version 5, and it
 * is answered window=1. Each window it opens is a number of its own, as a
 * single picture's server is, so place, input and info take it the same
 * way, and the desktop never sees the compositor's ids:
 *
 *   frames serve -w [-n NAME -t TOKEN]   NAME and TOKEN kept across a restart
 *   frames events N            the compositor's news since last asked, a
 *                              line each: display NAME, new W PARENT|0 ID
 *                              (ID the compositor's own, the same across a
 *                              desktop restart: how a window is found again),
 *                              title W TEXT, appid W TEXT, limits W ...,
 *                              decor W server|client, fullscreen W on|off,
 *                              maximize W on|off, minimize W, hide W,
 *                              parent W P|0, popup W PARENT X Y,
 *                              cursor W NAME, sub W PARENT X Y PW PH
 *                              above|below (a subsurface of PARENT, at X Y
 *                              in its pixels, placed by `frames place W`
 *                              with PARENT's body), geometry W X Y PW PH
 *                              (the window within its buffer; its picture
 *                              is cut to it), move W, resize W EDGE, menu W
 *                              X Y (its own title bar asking, a button
 *                              down), gone W -- W a window's number
 *   frames window W size PW PH [fullscreen] [activated] [maximized]
 *   frames window W close|output PW PH MHZ   said to the compositor
 *   frames quit N              the person quit: the compositor ends
 *   frames wait N MS           until the compositor has named its display,
 *                              no longer than MS: what the desktop starts
 *                              before its first frame needs the name
 *   frames clip N offer|clear|get clipboard|primary [-s] [TEXT]
 *                              the desktop's side of the clipboard with the
 *                              compositor's programs; news `clip SEL
 *                              text|secret|none|clear` when a program's
 *                              selection changes
 *   frames input W member P X Y   a popup's picture joins its top-level's
 *                              input route, at X Y in P's surface pixels
 *
 * The wire is Blit's, from the producer's side unchanged: a hello with the
 * token at version 5, the only one (Blit 0.3.74), answered with what every
 * display can import; FRM3 frames by dmabuf and FRM1 frames by sealed
 * memfd; FRAME release and FRAME error back in the producer's own ids,
 * which are always a line's third word. Nothing here maps a dmabuf. A memfd of
 * RGB is mapped only where frames by descriptor cannot go at all -- a
 * plain terminal -- and is then drawn as any picture is, so the window
 * always shows something.
 *
 * The console's wait reads the sockets (its hooks), so a frame and its
 * RELEASE pass without the script running; it wakes for a hello, a hang
 * up, a first frame and a frame it has to draw itself.
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
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
#include <sys/wait.h>
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
	/* A window's subsurface (Gitea #496): the compositor's id for its
	   top-level, where it is and how big in that top-level's pixels,
	   above (1) or below (-1) it, and when it was last said, which
	   orders the subs of one side bottom to top. */
	int sub, sx, sy, sw, sh, sab;
	unsigned spar;
	unsigned long sord;
	/* Where the window is in its own buffer, round which a program draws
	   its shadow (Wayland's window geometry, Gitea #496): gset once said. */
	int gset, gx, gy, gw, gh;
	/* A server's clipboard with its programs (Gitea #496, Blit ADR 0229),
	   [0] the clipboard and [1] the primary selection: the desktop's own
	   offers, each its text, whether a secret, and the number it went
	   under; a program's selection being read, and the last one read. */
	char *ctext[2];
	int csec[2];
	unsigned long cseq[2];
	struct fl_clipin *cin[2];
	char *ptext[2];
	int psec[2];
	unsigned long pseq[2];
	unsigned long clipn;
} fl_srv;

void fl_clipreset(fl_srv *f);
void fl_clipline(fl_srv *f, const char *m, size_t k, int fd);
int fl_clipverb(sh *s, fl_srv *f, int ac, char **av);

static vec fl_srvs;
static unsigned fl_next;
static unsigned long fl_subn;
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

/* The process at the far end of a server's connection -- the compositor,
   which the desktop starts detached and so never learns the pid of -- or
   0 where none is connected or the system cannot say. */
long fl_peerpid(fl_srv *f)
{
#if defined(__linux__) && defined(SO_PEERCRED)
	struct ucred u;
	socklen_t l = sizeof u;
	int c = fl_cfd(f);

	if (c < 0 || getsockopt(c, SOL_SOCKET, SO_PEERCRED, &u, &l) < 0)
		return 0;
	return (long)u.pid;
#else
	(void)f;
	return 0;
#endif
}

/* The words of a line on the frame channel, at version 5: its word, its
   verb, the id it is about -- always the third word -- and the rest; a
   compositor's WINDOW display names no id and is all rest. 1 when there
   are all three. Every reader of a word's place goes through this. */
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
	*id = strtoul(p, (char **)&q, 10);
	if (q == p)
		return 0;
	if (q < e && *q == ' ')
		q++;
	s_add(rest, q, (size_t)(e - q));
	return 1;
}

/* A line for the frame channel, at version 5, the other half of fl_wsplit:
   WORD VERB ID REST. */
void fl_wfmt(str *o, const char *word, const char *verb, unsigned long id,
	     const char *rest)
{
	s_cat(o, word);
	s_ch(o, ' ');
	s_cat(o, verb);
	s_ch(o, ' ');
	s_num(o, (long)id);
	if (rest && *rest) {
		s_ch(o, ' ');
		s_cat(o, rest);
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
	fl_clipreset(f);
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
	unsigned long sid;
	fl_srv *f;
	str w, v, r, o;
	int in;

	(void)ctx;
	s_init(&w);
	s_init(&v);
	s_init(&r);
	s_init(&o);
	/* The stream is the third word, as on every picture line, and goes
	   back under the id the producer named: its route's or its frames'. */
	if (!fl_wsplit(line, strlen(line), &w, &v, &sid, &r))
		goto out;
	f = fl_find((unsigned)sid);
	if (!f || fl_cfd(f) < 0)
		goto out;
	in = !strcmp(w.p, "INPUT");
	if (in) {
		free(f->input);
		f->input = xs(!strcmp(v.p, "ok") ? "ok" : r.p);
	} else if (!strcmp(v.p, "release")) {
		f->rel++;
	} else {
		f->errs++;
	}
	fl_wfmt(&o, w.p, v.p, in ? f->ibid : f->bid, r.p);
	fl_say(f, o.p);
out:
	s_free(&o);
	s_free(&r);
	s_free(&v);
	s_free(&w);
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
	if (k < 16 || memcmp(m, "BLITFRM1", 8) || ver != 5) {
		fl_say(f, "ERROR 0 EINVAL\n");
		return 0;
	}
	if (tl != strlen(f->tok) || 16 + tl > k || memcmp(m + 16, f->tok, tl)) {
		fl_say(f, "ERROR 0 EACCES\n");
		return 0;
	}
	f->ver = (int)ver;
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
	s_cat(&o, f->win ? " window=1" : " window=0");
	s_ch(&o, '\n');
	fl_say(f, o.p);
	lg(HIBR_LDBG, "frames %u: hello at version %lu, %s", f->n, ver, o.p);
	s_free(&o);
	s_free(&fm);
	return 1;
}

/* An input route's line from the producer (Blit 0.3.64, version 5): "INPUT
   sock <id> <path>" (blit-compositor's, which Blit writes the input to
   itself; the gamescope bridge's "INPUT eis" went with it in 0.99.483) or
   "INPUT on|off <id>", the id its own --
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
		    !strcmp(v.p, "sock") &&
		    fl_dpf->input(f->s, t->n, v.p, r.p)) {
			t->ibid = (unsigned)id;
			ok = 1;
			lg(HIBR_LDBG, "frames %u: input %s for %lu", t->n, v.p, id);
		}
	}
	if (!ok) {
		s_init(&o);
		fl_wfmt(&o, "INPUT", "error", id, "EINVAL");
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

/* A subsurface said (Gitea #496): "PARENT X Y W H above|below" after its
   own id, again whenever any of it changes; a window of its own here, and
   news for the desktop in its numbers. */
void fl_wsub(fl_srv *f, unsigned long id, const char *rest, str *o)
{
	unsigned long par;
	int x, y, w, h;
	char ab[8];
	fl_srv *t;

	if (sscanf(rest, "%lu %d %d %d %d %7s", &par, &x, &y, &w, &h, ab) != 6 ||
	    (strcmp(ab, "above") && strcmp(ab, "below"))) {
		lg(HIBR_LWRN, "frames %u: a WINDOW sub not understood", f->n);
		return;
	}
	t = fl_win(f, id);
	if (!t)
		t = fl_newwin(f, id);
	t->sub = 1;
	t->spar = (unsigned)par;
	t->sx = x;
	t->sy = y;
	t->sw = w;
	t->sh = h;
	t->sab = ab[0] == 'a' ? 1 : -1;
	t->sord = ++fl_subn;
	o->n = 0;
	s_num(o, (long)(fl_win(f, par) ? fl_win(f, par)->n : 0));
	s_ch(o, ' ');
	s_num(o, x);
	s_ch(o, ' ');
	s_num(o, y);
	s_ch(o, ' ');
	s_num(o, w);
	s_ch(o, ' ');
	s_num(o, h);
	s_ch(o, ' ');
	s_cat(o, ab);
	fl_ev(f, "sub", t->n, o->p);
}

/* A subsurface's slot in its top-level's layers: its place among the subs
   of its side, the nearest the top-level's own picture last. */
int fl_subslot(const fl_srv *t)
{
	size_t i;
	int n = 0, k = 0;
	const fl_srv *u;

	for (i = 0; i < fl_srvs.n; i++) {
		u = fl_srvs.p[i];
		if (u->par != t->par || !u->sub || u->spar != t->spar ||
		    u->sab != t->sab)
			continue;
		n++;
		if (u->sord < t->sord)
			k++;
	}
	return t->sab > 0 ? k + 1 : k - n;
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
		if (!strcmp(v.p, "new")) {
			s_ch(&o, ' ');
			s_num(&o, (long)id);
		}
		fl_ev(f, v.p, t->n, o.p);
	} else if (!strcmp(v.p, "sub")) {
		fl_wsub(f, id, r.p, &o);
	} else if ((t = fl_win(f, id)) == 0) {
		lg(HIBR_LDBG, "frames %u: WINDOW %s for %lu, which is not here",
		   f->n, v.p, id);
	} else if (!strcmp(v.p, "gone")) {
		fl_ev(f, "gone", t->n, 0);
		fl_winfree(t);
	} else if (!strcmp(v.p, "geometry")) {
		if (sscanf(r.p, "%d %d %d %d", &t->gx, &t->gy, &t->gw, &t->gh) == 4 &&
		    t->gw > 0 && t->gh > 0) {
			t->gset = 1;
			fl_ev(f, "geometry", t->n, r.p);
		}
	} else if (!strcmp(v.p, "parent")) {
		fl_wnum(f, r.p, &o);
		fl_ev(f, "parent", t->n, o.p);
	} else if (!strcmp(v.p, "title") || !strcmp(v.p, "appid") ||
		   !strcmp(v.p, "limits") || !strcmp(v.p, "decor") ||
		   !strcmp(v.p, "fullscreen") || !strcmp(v.p, "maximize") ||
		   !strcmp(v.p, "minimize") || !strcmp(v.p, "hide") ||
		   !strcmp(v.p, "cursor") || !strcmp(v.p, "lock") ||
		   !strcmp(v.p, "move") || !strcmp(v.p, "resize") ||
		   !strcmp(v.p, "menu") || !strcmp(v.p, "activate")) {
		/* activate: a program asking for the focus (Blit 0.3.86), which
		   the desktop grants or not (Gitea #507). */
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
			s_cat(&o, "FRAME error ");
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
			s_cat(&o, "FRAME error ");
			s_num(&o, (long)t->bid);
			s_cat(&o, got == BF_HDR3 ? " ENOTSUP\n" : " EINVAL\n");
			fl_say(f, o.p);
			s_free(&o);
		}
		for (k = 0; k < nfd; k++)
			close(fds[k]);
		return wake;
	}
	if (f->win && got > 5 && !memcmp(fl_buf, "CLIP ", 5)) {
		fl_clipline(f, (const char *)fl_buf, (size_t)got,
			    nfd ? fds[0] : -1);
		for (k = 1; k < nfd; k++)
			close(fds[k]);
		return 1;
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
		fl_say(f, "FRAME error 0 EINVAL\n");
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

/* The clipboard between programs and the desktop (Gitea #496, Blit ADR
   0229). Blit's compositor stores nothing: a selection is offered with a
   number, and whoever wants it asks for it with a pipe the owner writes
   into and closes. The desktop keeps the clipboard (wm/clip.hibr) and
   reads a program's at once, text alone and at most FL_CLIPMAX bytes, so
   it can keep it in its history -- or not, a password manager's marked
   secret. */
#ifndef FL_CLIPMAX
#define FL_CLIPMAX (1024 * 1024)
#endif
#ifndef FL_CLIPTEXT
#define FL_CLIPTEXT "text/plain;charset=utf-8"
#endif
#ifndef FL_CLIPSEC
#define FL_CLIPSEC "x-kde-passwordManagerHint"
#endif

typedef struct fl_clipin {
	fl_srv *f;
	int sel, fd;
	str buf;
} fl_clipin;

/* 0 for the clipboard, 1 for the primary selection, -1 for neither. */
int fl_clipsel(const char *w)
{
	if (!strcmp(w, "clipboard"))
		return 0;
	if (!strcmp(w, "primary"))
		return 1;
	return -1;
}

/* A read of a program's selection over: kept and said to the desktop when
   whole, dropped otherwise. */
void fl_clipdone(fl_clipin *ci, int ok)
{
	fl_srv *f = ci->f;
	const char *nm = ci->sel ? "primary" : "clipboard";
	str o;

	fl_dpf->unhook(ci->fd);
	close(ci->fd);
	f->cin[ci->sel] = 0;
	s_init(&o);
	s_cat(&o, nm);
	if (ok) {
		free(f->ptext[ci->sel]);
		f->ptext[ci->sel] = xs(ci->buf.p ? ci->buf.p : "");
		s_cat(&o, f->psec[ci->sel] ? " secret" : " text");
	} else {
		s_cat(&o, " none");
	}
	fl_ev(f, "clip", 0, o.p);
	lg(HIBR_LDBG, "frames %u: a program's %s, %zu bytes%s", f->n, nm,
	   ci->buf.n, ok ? "" : ", not kept");
	s_free(&o);
	s_free(&ci->buf);
	free(ci);
}

/* Readable: a program's selection arriving down the pipe asked for. */
int fl_clipread(void *ctx, int fd)
{
	fl_clipin *ci = ctx;
	char b[4096];
	ssize_t k;

	for (;;) {
		k = read(fd, b, sizeof b);
		if (k < 0 && errno == EINTR)
			continue;
		if (k < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
			return 0;
		if (k <= 0) {
			fl_clipdone(ci, k == 0);
			return 1;
		}
		if (ci->buf.n + (size_t)k > FL_CLIPMAX) {
			fl_clipdone(ci, 0);
			return 1;
		}
		s_add(&ci->buf, b, (size_t)k);
	}
}

/* Stop a read of a selection, if one is going. */
void fl_clipstop(fl_srv *f, int sel)
{
	if (!f->cin[sel])
		return;
	fl_dpf->unhook(f->cin[sel]->fd);
	close(f->cin[sel]->fd);
	s_free(&f->cin[sel]->buf);
	free(f->cin[sel]);
	f->cin[sel] = 0;
}

/* Everything a server's clipboard holds, gone with its compositor: its
   offers lapse with the channel, and a program's is offered again. */
void fl_clipreset(fl_srv *f)
{
	int i;

	for (i = 0; i < 2; i++) {
		fl_clipstop(f, i);
		free(f->ctext[i]);
		f->ctext[i] = 0;
		f->cseq[i] = 0;
		f->pseq[i] = 0;
	}
}

/* Send a line with a descriptor beside it, one packet. */
int fl_sayfd(fl_srv *f, const char *line, int fd)
{
	struct msghdr mh;
	struct iovec v;
	union {
		char b[CMSG_SPACE(sizeof(int))];
		struct cmsghdr align;
	} u;
	struct cmsghdr *c;
	int cfd = fl_cfd(f);

	if (cfd < 0)
		return 0;
	memset(&mh, 0, sizeof mh);
	memset(&u, 0, sizeof u);
	v.iov_base = (void *)line;
	v.iov_len = strlen(line);
	mh.msg_iov = &v;
	mh.msg_iovlen = 1;
	mh.msg_control = u.b;
	mh.msg_controllen = sizeof u.b;
	c = CMSG_FIRSTHDR(&mh);
	c->cmsg_level = SOL_SOCKET;
	c->cmsg_type = SCM_RIGHTS;
	c->cmsg_len = CMSG_LEN(sizeof(int));
	memcpy(CMSG_DATA(c), &fd, sizeof fd);
	return sendmsg(cfd, &mh, MSG_NOSIGNAL | MSG_DONTWAIT) == (ssize_t)v.iov_len;
}

/* Ask for a program's selection, offered under seq, as text. */
void fl_clipask(fl_srv *f, int sel, unsigned long seq)
{
	int p[2];
	fl_clipin *ci;
	str o;

	fl_clipstop(f, sel);
	if (pipe(p) < 0)
		return;
	fcntl(p[0], F_SETFD, FD_CLOEXEC);
	fcntl(p[1], F_SETFD, FD_CLOEXEC);
	s_init(&o);
	s_cat(&o, "CLIP read ");
	s_num(&o, (long)seq);
	s_cat(&o, " " FL_CLIPTEXT "\n");
	if (!fl_sayfd(f, o.p, p[1])) {
		close(p[0]);
		close(p[1]);
		s_free(&o);
		return;
	}
	close(p[1]);
	s_free(&o);
	fcntl(p[0], F_SETFL, fcntl(p[0], F_GETFL) | O_NONBLOCK);
	ci = xm(sizeof *ci);
	memset(ci, 0, sizeof *ci);
	ci->f = f;
	ci->sel = sel;
	ci->fd = p[0];
	s_init(&ci->buf);
	f->cin[sel] = ci;
	fl_dpf->hook(p[0], fl_clipread, ci);
}

/* Write the desktop's text to a program's pipe and close it: what does not
   go at once goes from a child of a child -- init's, so nobody here waits
   for it beyond the middle one -- so a slow reader cannot stop the
   desktop. */
void fl_clipwrite(int fd, const char *t)
{
	size_t n = strlen(t), o = 0;
	ssize_t k;
	pid_t pid;

	fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
	while (o < n) {
		k = write(fd, t + o, n - o);
		if (k < 0 && errno == EINTR)
			continue;
		if (k <= 0)
			break;
		o += (size_t)k;
	}
	if (o < n && (errno == EAGAIN || errno == EWOULDBLOCK)) {
		fflush(0);
		pid = fork();
		if (pid == 0) {
			if (fork() == 0) {
				fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) & ~O_NONBLOCK);
				while (o < n) {
					k = write(fd, t + o, n - o);
					if (k < 0 && errno == EINTR)
						continue;
					if (k <= 0)
						break;
					o += (size_t)k;
				}
			}
			_exit(0);
		}
		if (pid > 0)
			while (waitpid(pid, 0, 0) < 0 && errno == EINTR)
				;
	}
	close(fd);
}

/* A CLIP line from the compositor: a program's selection offered (read at
   once when it is text), a program pasting what the desktop offered (fd
   the pipe to write it into), or a program's selection gone. */
void fl_clipline(fl_srv *f, const char *m, size_t k, int fd)
{
	str w, v, r;
	unsigned long seq;
	char sel[16];
	int i, n;

	s_init(&w);
	s_init(&v);
	s_init(&r);
	if (!fl_wsplit(m, k, &w, &v, &seq, &r)) {
		if (fd >= 0)
			close(fd);
		goto out;
	}
	if (!strcmp(v.p, "offer") && sscanf(r.p, "%15s%n", sel, &n) == 1 &&
	    (i = fl_clipsel(sel)) >= 0) {
		f->pseq[i] = seq;
		f->psec[i] = strstr(r.p + n, FL_CLIPSEC) != 0;
		if (strstr(r.p + n, FL_CLIPTEXT)) {
			fl_clipask(f, i, seq);
		} else {
			fl_clipstop(f, i);
			fl_ev(f, "clip", 0, i ? "primary none" : "clipboard none");
		}
	} else if (!strcmp(v.p, "read")) {
		for (i = 0; i < 2; i++)
			if (f->cseq[i] == seq && f->ctext[i])
				break;
		if (fd >= 0 && i < 2)
			fl_clipwrite(fd, f->ctext[i]);
		else if (fd >= 0)
			close(fd);
		fd = -1;
	} else if (!strcmp(v.p, "clear")) {
		for (i = 0; i < 2; i++)
			if (f->pseq[i] == seq) {
				fl_clipstop(f, i);
				f->pseq[i] = 0;
				fl_ev(f, "clip", 0,
				      i ? "primary clear" : "clipboard clear");
			}
	}
	if (fd >= 0)
		close(fd);
out:
	s_free(&w);
	s_free(&v);
	s_free(&r);
}

/* frames clip N offer|clear|get clipboard|primary [-s] [TEXT]: the
   desktop's side of the clipboard. offer makes TEXT the desktop's selection
   for the server's programs, a secret with -s; clear takes it back; get
   answers the last text a program's selection was read as. */
int fl_clipverb(sh *s, fl_srv *f, int ac, char **av)
{
	int i, sec = 0, a = 5;
	str o;

	if (ac < 5 || (i = fl_clipsel(av[4])) < 0) {
		lg(HIBR_LERR, "usage: frames clip N offer|clear|get clipboard|primary [-s] [TEXT]");
		return 2;
	}
	s_init(&o);
	if (!strcmp(av[3], "get")) {
		if (!f->ptext[i]) {
			s_free(&o);
			return HIBR_FAIL;
		}
		s_cat(&o, f->ptext[i]);
		hibr_ret(s, o.p ? o.p : "");
		if (!s->bind)
			fputs(o.p ? o.p : "", stdout);
		s_free(&o);
		return HIBR_OK;
	}
	if (!strcmp(av[3], "clear")) {
		if (f->cseq[i]) {
			s_cat(&o, "CLIP clear ");
			s_num(&o, (long)f->cseq[i]);
			s_ch(&o, '\n');
			fl_say(f, o.p);
		}
		free(f->ctext[i]);
		f->ctext[i] = 0;
		f->cseq[i] = 0;
		s_free(&o);
		return HIBR_OK;
	}
	if (strcmp(av[3], "offer")) {
		s_free(&o);
		lg(HIBR_LERR, "frames clip: %s: no such verb", av[3]);
		return 2;
	}
	if (a < ac && !strcmp(av[a], "-s")) {
		sec = 1;
		a++;
	}
	if (a >= ac || strlen(av[a]) > FL_CLIPMAX) {
		s_free(&o);
		return HIBR_FAIL;
	}
	free(f->ctext[i]);
	f->ctext[i] = xs(av[a]);
	f->csec[i] = sec;
	f->cseq[i] = ++f->clipn;
	s_cat(&o, "CLIP offer ");
	s_num(&o, (long)f->cseq[i]);
	s_cat(&o, i ? " primary " : " clipboard ");
	s_cat(&o, FL_CLIPTEXT);
	if (sec)
		s_cat(&o, " " FL_CLIPSEC);
	s_ch(&o, '\n');
	fl_say(f, o.p);
	s_free(&o);
	return fl_cfd(f) >= 0 ? HIBR_OK : HIBR_FAIL;
}
#else
/* Not Linux: no sockets of this kind. */
int fl_listen(fl_srv *f)
{
	(void)f;
	return 0;
}

/* Nothing to reset where nothing is served. */
void fl_clipreset(fl_srv *f)
{
	(void)f;
}

/* Nor any clipboard to share. */
int fl_clipverb(sh *s, fl_srv *f, int ac, char **av)
{
	(void)s;
	(void)f;
	(void)ac;
	(void)av;
	return HIBR_FAIL;
}
#endif

/* Free a server and everything it holds, its windows too. */
void fl_free(fl_srv *f)
{
	if (f->cfd >= 0)
		fl_hangup(f);
	fl_clipreset(f);
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
/* The displays and the primary, told to the compositor (Gitea #514):
 * WINDOW layout NAME X Y W H MHZ a display, WINDOW layout end, WINDOW
 * primary NAME. No window id: they are about the screens, not a window. */
int fl_layout(fl_srv *f, int ac, char **av, str *o)
{
	int a, want = 4;

	if (f->par || ac < want) {
		lg(HIBR_LERR, "usage: frames layout N NAME X Y W H MHZ | end; frames primary N NAME");
		s_free(o);
		return 2;
	}
	s_cat(o, "WINDOW ");
	s_cat(o, av[1]);
	for (a = 3; a < ac; a++) {
		if (!*av[a] || strpbrk(av[a], " \t\r\n")) {
			lg(HIBR_LERR, "frames %s: a word with a blank in it", av[1]);
			s_free(o);
			return 2;
		}
		s_ch(o, ' ');
		s_cat(o, av[a]);
	}
	s_ch(o, '\n');
	fl_say(f, o->p);
	s_free(o);
	return fl_cfd(f) >= 0 ? HIBR_OK : HIBR_FAIL;
}

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
	if (!strcmp(av[1], "layout") || !strcmp(av[1], "primary"))
		return fl_layout(f, ac, av, &o);
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
	if (!strcmp(av[1], "clip")) {
		int r = fl_clipverb(s, f, ac, av);

		s_free(&o);
		return r;
	}
	if (!strcmp(av[1], "place")) {
		int r, c, h, w, ok = 0, gx, gy;
		fl_srv *pt;

		if (ac < 8) {
			lg(HIBR_LERR, "usage: frames place N PANE ROW COL H W");
			s_free(&o);
			return 2;
		}
		r = atoi(av[4]);
		c = atoi(av[5]);
		h = atoi(av[6]);
		w = atoi(av[7]);
		/* A sub's place is in its top-level's surface pixels, and the
		   body shows the top-level's geometry: one cut to it is moved
		   by its origin. */
		pt = f->sub && f->par ? fl_win(f->par, f->spar) : 0;
		gx = pt && pt->gset ? pt->gx : 0;
		gy = pt && pt->gset ? pt->gy : 0;
		if (f->frames && f->sub)
			ok = fl_dpf->sub(s, av[3], r, c, h, w, f->n, f->sx - gx,
					 f->sy - gy, f->sw, f->sh, fl_subslot(f));
		else if (f->frames && f->gset)
			ok = fl_dpf->crop(s, av[3], r, c, h, w, f->n, f->gx, f->gy,
					  f->gw, f->gh);
		else if (f->frames)
			ok = fl_dpf->place(s, av[3], r, c, h, w, f->n);
		/* Where frames cannot go by descriptor a window is drawn from
		   its RGB, and its subsurfaces are not drawn at all. */
		if (!ok && f->rgb && !f->sub)
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
		s_cat(&o, " pid=");
		s_num(&o, fl_peerpid(f));
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
