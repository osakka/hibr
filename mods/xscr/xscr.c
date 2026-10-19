#define _GNU_SOURCE

#include "hibr.h"
#include "../display.h"
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/wait.h>
#ifdef __linux__
#include <sys/prctl.h>
#endif
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

/* An X screen in a window (Gitea #290). Xvfb draws the screen into a file
   it maps (-fbdir); this maps the same file, so the pixels are read with
   no X library at all. What the module says to the server is a few
   requests of the X protocol over its Unix socket, written here: the
   connection, QueryExtension, DAMAGE so the server says when the screen
   changed -- an idle window then costs nothing -- XTEST so keys, buttons
   and the wheel reach whatever has the focus, and GetKeyboardMapping so a
   key name becomes the keycode the server's own map gives it. */

#ifndef XS_WAITMS
#define XS_WAITMS 3000
#endif

typedef struct xs_scr xs_scr;
struct xs_scr {
	int id, fd, fbfd;
	unsigned char *fb;
	size_t fbn;
	int w, h, bpl, bpp, off, lsb;
	unsigned rmask, gmask, bmask;
	int rsh, gsh, bsh;
	unsigned root, ridbase, ridmask, damage;
	int dmgop, dmgev, xtop;
	int minkc, maxkc, kpc;
	unsigned *kmap;
	int dirty, px, py;
	pid_t srv;
	unsigned short seq;
	str in;
	unsigned char *rgb;
	/* The window manager this module is for the screen (Gitea #291):
	   whether it holds the root's redirect, the region a normal window is
	   fitted to, the toplevels it manages and those waiting to be, the one
	   that has focus and the main one whose title the window wears. */
	int wm, fitw, fith, titledirty;
	vec mwin, mtr, pend;
	unsigned focus, main, netname, utf8;
	unsigned short errseq;
	int errcode;
	str title;
};

static vec xs_scrs;
static int xs_next = 1;
static const dp_api *xs_dp;
/* The servers `xscr start` began and nothing has ended yet, as pids: the
   module ends them when the shell goes, which is how quitting the desktop
   takes every X screen and the programs on it with it. */
static vec xs_srvs;

/* Forget a server's pid, and end it when kill_it is set, waiting for it:
   it is this shell's child, in a session of its own so no job's wait can
   take it, and nothing else will reap it. */
void xs_srvdrop(pid_t pid, int kill_it)
{
	size_t i;
	int st, k;

	if (pid <= 0)
		return;
	for (i = 0; i < xs_srvs.n; i++)
		if ((pid_t)(intptr_t)xs_srvs.p[i] == pid) {
			xs_srvs.p[i] = xs_srvs.p[xs_srvs.n - 1];
			xs_srvs.n--;
			break;
		}
	if (!kill_it)
		return;
	kill(pid, SIGTERM);
	for (k = 0; k < 50; k++) {
		if (waitpid(pid, &st, WNOHANG) != 0)
			return;
		usleep(20000);
	}
	kill(pid, SIGKILL);
	while (waitpid(pid, &st, 0) < 0 && errno == EINTR)
		;
}

/* Start Xvfb on display n, size "WxH", its screen kept in dir, as this
   shell's own child in a session of its own. On Linux the kernel ends it
   when this process does (PR_SET_PDEATHSIG): a desktop that crashes or is
   killed takes its X screens with it, where an orphaned Xvfb would run on
   for ever, while Restart Desktop -- an exec, the same process -- keeps
   them. Elsewhere it is ended by xs_fini as the shell exits. The signals
   the desktop ignores or handles go back to their defaults, since an
   ignored one survives exec. */
pid_t xs_start(int n, const char *size, const char *dir)
{
	int fd, sig;
	pid_t pid, me = getpid();
	sigset_t none;
	char dn[32], scr[64];

	fflush(0);
	pid = fork();
	if (pid < 0)
		return 0;
	if (!pid) {
		setsid();
#ifdef __linux__
		prctl(PR_SET_PDEATHSIG, SIGTERM);
		if (getppid() != me)
			_exit(0);
#endif
		for (sig = 1; sig < 32; sig++)
			if (sig != SIGKILL && sig != SIGSTOP)
				signal(sig, SIG_DFL);
		sigemptyset(&none);
		sigprocmask(SIG_SETMASK, &none, 0);
		fd = open("/dev/null", O_RDWR);
		if (fd >= 0) {
			dup2(fd, 0);
			dup2(fd, 1);
			dup2(fd, 2);
		}
		snprintf(dn, sizeof dn, ":%d", n);
		snprintf(scr, sizeof scr, "%sx24", size);
		execlp("Xvfb", "Xvfb", dn, "-screen", "0", scr, "-fbdir", dir,
		       "-nolisten", "tcp", (char *)0);
		_exit(127);
	}
	(void)me;
	v_add(&xs_srvs, (void *)(intptr_t)pid);
	lg(HIBR_LDBG, "xscr: Xvfb :%d started, pid %d", n, (int)pid);
	return pid;
}

/* A screen by the number `xscr open` gave it. */
xs_scr *xs_get(const char *id)
{
	size_t i;
	int n = atoi(id);
	xs_scr *x;

	for (i = 0; i < xs_scrs.n; i++) {
		x = xs_scrs.p[i];
		if (x && x->id == n)
			return x;
	}
	lg(HIBR_LERR, "xscr: no screen %s", id);
	return 0;
}

/* Put a 16- or 32-bit number on a request, in the client's own order. */
void xs_p16(str *o, unsigned v)
{
	s_ch(o, (int)(v & 0xff));
	s_ch(o, (int)((v >> 8) & 0xff));
}
void xs_p32(str *o, unsigned v)
{
	xs_p16(o, v & 0xffff);
	xs_p16(o, v >> 16);
}

/* Read a 16- or 32-bit number from a reply. */
unsigned xs_g16(const unsigned char *p)
{
	return (unsigned)p[0] | (unsigned)p[1] << 8;
}
unsigned xs_g32(const unsigned char *p)
{
	return xs_g16(p) | xs_g16(p + 2) << 16;
}

/* Send a whole request, without a SIGPIPE if the server has gone. */
int xs_send(xs_scr *x, str *o)
{
	if (hibr_wrall(x->fd, o->p, o->n) != (ssize_t)o->n)
		return HIBR_FAIL;
	x->seq++;
	return HIBR_OK;
}

/* Read what the server has sent into x->in, waiting up to ms for something;
   0 when the connection has closed. */
int xs_fill(xs_scr *x, int ms)
{
	struct pollfd pf;
	char b[HIBR_IOCH];
	ssize_t n;
	int r;

	pf.fd = x->fd;
	pf.events = POLLIN;
	r = poll(&pf, 1, ms);
	if (r < 0 && errno == EINTR)
		return 1;
	if (r <= 0)
		return 1;
	n = recv(x->fd, b, sizeof b, MSG_DONTWAIT);
	if (n < 0 && (errno == EAGAIN || errno == EINTR))
		return 1;
	if (n <= 0)
		return 0;
	s_add(&x->in, b, (size_t)n);
	return 1;
}

/* Send one simple request: the opcode, a byte, and words after. */
void xs_simple(xs_scr *x, int op, int b, const unsigned *v, int n)
{
	str o;
	int i;

	s_init(&o);
	s_ch(&o, op);
	s_ch(&o, b);
	xs_p16(&o, (unsigned)(1 + n));
	for (i = 0; i < n; i++)
		xs_p32(&o, v[i]);
	xs_send(x, &o);
	s_free(&o);
}

/* Move and size a window: ConfigureWindow with x, y, width, height and no
   border, which is all a window here needs. */
void xs_place(xs_scr *x, unsigned w, int px, int py, unsigned ww, unsigned wh)
{
	str o;

	if (ww < 1)
		ww = 1;
	if (wh < 1)
		wh = 1;
	s_init(&o);
	s_ch(&o, 12);
	s_ch(&o, 0);
	xs_p16(&o, 8);
	xs_p32(&o, w);
	xs_p16(&o, 1 | 2 | 4 | 8 | 16);
	xs_p16(&o, 0);
	xs_p32(&o, (unsigned)px);
	xs_p32(&o, (unsigned)py);
	xs_p32(&o, ww);
	xs_p32(&o, wh);
	xs_p32(&o, 0);
	xs_send(x, &o);
	s_free(&o);
}

/* Give a window the keyboard and bring it to the top. */
void xs_focus(xs_scr *x, unsigned w)
{
	unsigned v[2];
	str o;

	x->focus = w;
	v[0] = w;
	v[1] = 0;
	xs_simple(x, 42, 2, v, 2);
	s_init(&o);
	s_ch(&o, 12);
	s_ch(&o, 0);
	xs_p16(&o, 4);
	xs_p32(&o, w);
	xs_p16(&o, 64);
	xs_p16(&o, 0);
	xs_p32(&o, 0);
	xs_send(x, &o);
	s_free(&o);
}

/* An error the server sent for a request nobody waits on: kept, so a
   caller that sent one and then a round trip can ask whether it failed --
   which is how a window manager already on the screen is told apart. */
void xs_err(xs_scr *x)
{
	const unsigned char *e = (const unsigned char *)x->in.p;

	x->errseq = (unsigned short)xs_g16(e + 2);
	x->errcode = e[1];
	lg(HIBR_LDBG, "xscr: X error %d on request %d.%d", e[1], e[10],
	   xs_g16(e + 8));
}

/* Whether a window is one this module manages, and its place in the list. */
int xs_managed(xs_scr *x, unsigned w)
{
	size_t i;

	for (i = 0; i < x->mwin.n; i++)
		if ((unsigned)(uintptr_t)x->mwin.p[i] == w)
			return (int)i;
	return -1;
}

/* An event, as the server's packets arrive. A DamageNotify is the screen
   having changed. The damage is not taken off here but once the screen has
   been drawn (xs_seen): at the NonEmpty level the server says nothing more
   until then, so a window that is hidden or minimised hears of one change
   and then nothing, however busy the program on the screen is -- taking it
   off on arrival woke the desktop for every frame such a program drew,
   whether or not anything was shown. The rest is the window manager's:
   a window asking to be shown waits for xs_pending, which needs round
   trips an event handler must not make; one asking to move or resize is
   fitted again if it is a normal window this manages, and given what it
   asked for otherwise; one that goes takes the focus with it. */
void xs_event(xs_scr *x, const unsigned char *e)
{
	int code = e[0] & 0x7f, i;
	unsigned w;

	if (x->dmgop && code == x->dmgev) {
		x->dirty = 1;
		return;
	}
	if (!x->wm)
		return;
	switch (code) {
	case 20:
		v_add(&x->pend, (void *)(uintptr_t)xs_g32(e + 8));
		break;
	case 23:
		w = xs_g32(e + 8);
		i = xs_managed(x, w);
		if (i >= 0 && !(uintptr_t)x->mtr.p[i])
			xs_place(x, w, 0, 0, x->fitw, x->fith);
		else
			xs_place(x, w, (short)xs_g16(e + 16), (short)xs_g16(e + 18),
				 xs_g16(e + 20), xs_g16(e + 22));
		break;
	case 17:
	case 18:
		w = xs_g32(e + 8);
		i = xs_managed(x, w);
		if (i >= 0 && code == 17) {
			x->mwin.p[i] = x->mwin.p[x->mwin.n - 1];
			x->mtr.p[i] = x->mtr.p[x->mtr.n - 1];
			x->mwin.n--;
			x->mtr.n--;
		}
		if (w == x->main && code == 17) {
			x->main = 0;
			x->titledirty = 1;
		}
		if (w == x->focus) {
			x->focus = x->mwin.n ?
				(unsigned)(uintptr_t)x->mwin.p[x->mwin.n - 1] : 0;
			if (x->focus)
				xs_focus(x, x->focus);
		}
		break;
	case 28:
		if (xs_g32(e + 4) == x->main &&
		    (xs_g32(e + 8) == 39 || xs_g32(e + 8) == x->netname))
			x->titledirty = 1;
		break;
	}
}

/* The screen has been drawn as it is now: clear the mark and take the
   damage off, so the next change sends one event again. */
void xs_seen(xs_scr *x)
{
	str o;

	x->dirty = 0;
	if (!x->dmgop)
		return;
	s_init(&o);
	s_ch(&o, x->dmgop);
	s_ch(&o, 3);
	xs_p16(&o, 4);
	xs_p32(&o, x->damage);
	xs_p32(&o, 0);
	xs_p32(&o, 0);
	xs_send(x, &o);
	s_free(&o);
}

/* The length of the packet at the head of x->in, or 0 when it has not
   all arrived: events and errors are 32 bytes, a reply 32 and its own
   length in words more. */
size_t xs_pktlen(xs_scr *x)
{
	const unsigned char *p = (const unsigned char *)x->in.p;
	size_t n;

	if (x->in.n < 32)
		return 0;
	n = 32;
	if (p[0] == 1)
		n += (size_t)xs_g32(p + 4) * 4;
	return x->in.n >= n ? n : 0;
}

/* Drop the head packet of x->in. */
void xs_shift(xs_scr *x, size_t n)
{
	memmove(x->in.p, x->in.p + n, x->in.n - n);
	x->in.n -= n;
}

/* Act on every whole packet that has arrived, events alone: a reply
   nobody is waiting for is dropped. */
void xs_drain(xs_scr *x)
{
	size_t n;

	while ((n = xs_pktlen(x))) {
		if (x->in.p[0] == 0) {
			xs_err(x);
		} else if (x->in.p[0] != 1)
			xs_event(x, (const unsigned char *)x->in.p);
		xs_shift(x, n);
	}
}

/* Wait for the reply to the request just sent -- events that arrive first
   are acted on -- into out; whether one came. */
/* The number of the request is taken on the way in: an event acted on
   while waiting can send a request of its own (DamageSubtract), and the
   reply wanted is still the one before it. */
int xs_reply(xs_scr *x, str *out)
{
	struct timespec t0, t;
	size_t n;
	long ms;
	unsigned short want = x->seq;

	clock_gettime(CLOCK_MONOTONIC, &t0);
	for (;;) {
		while ((n = xs_pktlen(x))) {
			if (x->in.p[0] == 1 &&
			    xs_g16((unsigned char *)x->in.p + 2) == want) {
				out->n = 0;
				s_add(out, x->in.p, n);
				xs_shift(x, n);
				return HIBR_OK;
			}
			if (x->in.p[0] == 0 &&
			    xs_g16((unsigned char *)x->in.p + 2) == want) {
				lg(HIBR_LDBG, "xscr: X error %d",
				   (unsigned char)x->in.p[1]);
				xs_shift(x, n);
				return HIBR_FAIL;
			}
			if (x->in.p[0] == 0)
				xs_err(x);
			else if (x->in.p[0] > 1)
				xs_event(x, (const unsigned char *)x->in.p);
			xs_shift(x, n);
		}
		clock_gettime(CLOCK_MONOTONIC, &t);
		ms = (t.tv_sec - t0.tv_sec) * 1000 + (t.tv_nsec - t0.tv_nsec) / 1000000;
		if (ms >= XS_WAITMS || !xs_fill(x, (int)(XS_WAITMS - ms)))
			return HIBR_FAIL;
	}
}

/* An extension's major opcode and first event; 0 when the server has none
   by that name. */
int xs_ext(xs_scr *x, const char *nm, int *ev)
{
	str o, r;
	size_t n = strlen(nm);
	int op = 0;

	s_init(&o);
	s_init(&r);
	s_ch(&o, 98);
	s_ch(&o, 0);
	xs_p16(&o, (unsigned)(2 + (n + 3) / 4));
	xs_p16(&o, (unsigned)n);
	xs_p16(&o, 0);
	s_add(&o, nm, n);
	while (o.n % 4)
		s_ch(&o, 0);
	if (xs_send(x, &o) == HIBR_OK && xs_reply(x, &r) == HIBR_OK &&
	    r.p[8]) {
		op = (unsigned char)r.p[9];
		if (ev)
			*ev = (unsigned char)r.p[10];
	}
	s_free(&o);
	s_free(&r);
	return op;
}

/* The server's keyboard map: keysyms per keycode, as GetKeyboardMapping
   answers it. */
int xs_keymap(xs_scr *x)
{
	str o, r;
	int n = x->maxkc - x->minkc + 1, i;

	s_init(&o);
	s_init(&r);
	s_ch(&o, 101);
	s_ch(&o, 0);
	xs_p16(&o, 2);
	s_ch(&o, x->minkc);
	s_ch(&o, n);
	xs_p16(&o, 0);
	if (xs_send(x, &o) != HIBR_OK || xs_reply(x, &r) != HIBR_OK) {
		s_free(&o);
		s_free(&r);
		return HIBR_FAIL;
	}
	x->kpc = (unsigned char)r.p[1];
	free(x->kmap);
	x->kmap = xm((size_t)n * (x->kpc ? x->kpc : 1) * sizeof(unsigned));
	for (i = 0; i < n * x->kpc && 32 + (size_t)i * 4 + 4 <= r.n; i++)
		x->kmap[i] = xs_g32((unsigned char *)r.p + 32 + i * 4);
	s_free(&o);
	s_free(&r);
	return HIBR_OK;
}

/* The first one bit of a mask, so a pixel's channel can be shifted down. */
int xs_shiftof(unsigned m)
{
	int s = 0;

	if (!m)
		return 0;
	while (!(m & 1)) {
		m >>= 1;
		s++;
	}
	return s;
}

/* Map the screen Xvfb keeps in path, an XWD file whose header is in
   network order and whose pixels follow its own colour map. */
int xs_map(xs_scr *x, const char *path)
{
	struct stat st;
	const unsigned char *h;
	int i;
	unsigned f[25];

	x->fbfd = open(path, O_RDONLY | O_CLOEXEC);
	if (x->fbfd < 0 || fstat(x->fbfd, &st) < 0 || st.st_size < 100) {
		lg(HIBR_LERR, "xscr: %s: cannot be read", path);
		return HIBR_FAIL;
	}
	x->fbn = (size_t)st.st_size;
	x->fb = mmap(0, x->fbn, PROT_READ, MAP_SHARED, x->fbfd, 0);
	if (x->fb == MAP_FAILED) {
		x->fb = 0;
		lg(HIBR_LERR, "xscr: %s: cannot be mapped", path);
		return HIBR_FAIL;
	}
	h = x->fb;
	for (i = 0; i < 25; i++)
		f[i] = (unsigned)h[i * 4] << 24 | (unsigned)h[i * 4 + 1] << 16 |
		       (unsigned)h[i * 4 + 2] << 8 | h[i * 4 + 3];
	x->w = (int)f[4];
	x->h = (int)f[5];
	x->lsb = f[7] == 0;
	x->bpp = (int)f[11];
	x->bpl = (int)f[12];
	x->rmask = f[14];
	x->gmask = f[15];
	x->bmask = f[16];
	x->off = (int)(f[0] + f[19] * 12);
	if (x->bpp != 32 || x->w <= 0 || x->h <= 0 ||
	    (size_t)x->off + (size_t)x->bpl * (size_t)x->h > x->fbn) {
		lg(HIBR_LERR, "xscr: %s: not a 32-bit screen this can read", path);
		return HIBR_FAIL;
	}
	x->rsh = xs_shiftof(x->rmask);
	x->gsh = xs_shiftof(x->gmask);
	x->bsh = xs_shiftof(x->bmask);
	lg(HIBR_LDBG, "xscr: %s is %dx%d, %d bits, pixels at %d", path, x->w,
	   x->h, x->bpp, x->off);
	return HIBR_OK;
}

/* Wait until x->in holds at least n bytes, for no longer than XS_WAITMS
   in all; whether it does. */
int xs_need(xs_scr *x, size_t n)
{
	struct timespec t0, t;
	long ms;

	clock_gettime(CLOCK_MONOTONIC, &t0);
	while (x->in.n < n) {
		clock_gettime(CLOCK_MONOTONIC, &t);
		ms = (t.tv_sec - t0.tv_sec) * 1000 + (t.tv_nsec - t0.tv_nsec) / 1000000;
		if (ms >= XS_WAITMS || !xs_fill(x, (int)(XS_WAITMS - ms)))
			return HIBR_FAIL;
	}
	return HIBR_OK;
}

/* Connect to display n's socket and set the connection up, with no
   authentication -- Xvfb started by the app listens on its socket alone. */
int xs_connect(xs_scr *x, int n)
{
	struct sockaddr_un a;
	str o, r;
	unsigned char *p;
	size_t vl, at;
	int nf;

	x->fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (x->fd < 0)
		return HIBR_FAIL;
	fcntl(x->fd, F_SETFD, FD_CLOEXEC);
	memset(&a, 0, sizeof a);
	a.sun_family = AF_UNIX;
	snprintf(a.sun_path, sizeof a.sun_path, "/tmp/.X11-unix/X%d", n);
	if (connect(x->fd, (struct sockaddr *)&a, sizeof a) < 0) {
		lg(HIBR_LERR, "xscr: display :%d: %s", n, strerror(errno));
		return HIBR_FAIL;
	}
	s_init(&o);
	s_init(&r);
	s_ch(&o, 'l');
	s_ch(&o, 0);
	xs_p16(&o, 11);
	xs_p16(&o, 0);
	xs_p16(&o, 0);
	xs_p16(&o, 0);
	xs_p16(&o, 0);
	if (xs_send(x, &o) != HIBR_OK)
		goto bad;
	x->seq = 0;
	if (xs_need(x, 8) != HIBR_OK)
		goto bad;
	p = (unsigned char *)x->in.p;
	at = 8 + (size_t)xs_g16(p + 6) * 4;
	if (xs_need(x, at) != HIBR_OK)
		goto bad;
	p = (unsigned char *)x->in.p;
	if (p[0] != 1) {
		lg(HIBR_LERR, "xscr: display :%d refused the connection", n);
		goto bad;
	}
	x->ridbase = xs_g32(p + 12);
	x->ridmask = xs_g32(p + 16);
	vl = xs_g16(p + 24);
	nf = p[29];
	x->minkc = p[34];
	x->maxkc = p[35];
	x->root = xs_g32(p + 40 + ((vl + 3) & ~(size_t)3) + (size_t)nf * 8);
	xs_shift(x, at);
	s_free(&o);
	s_free(&r);
	return HIBR_OK;
bad:
	s_free(&o);
	s_free(&r);
	return HIBR_FAIL;
}

/* Ask the screen to say when it changes: DAMAGE's version, then a damage
   object on the root window at the NonEmpty level, which is one event per
   change until it is taken off. A server without DAMAGE is drawn when the
   caller asks, as if it always had changed. */
void xs_damage(xs_scr *x)
{
	str o, r;

	x->dmgop = xs_ext(x, "DAMAGE", &x->dmgev);
	if (!x->dmgop) {
		lg(HIBR_LDBG, "xscr: no DAMAGE; every frame is drawn");
		return;
	}
	s_init(&o);
	s_init(&r);
	s_ch(&o, x->dmgop);
	s_ch(&o, 0);
	xs_p16(&o, 3);
	xs_p32(&o, 1);
	xs_p32(&o, 1);
	if (xs_send(x, &o) != HIBR_OK || xs_reply(x, &r) != HIBR_OK) {
		x->dmgop = 0;
		goto out;
	}
	x->damage = x->ridbase | (1 & x->ridmask);
	o.n = 0;
	s_ch(&o, x->dmgop);
	s_ch(&o, 1);
	xs_p16(&o, 4);
	xs_p32(&o, x->damage);
	xs_p32(&o, x->root);
	s_ch(&o, 3);
	s_ch(&o, 0);
	xs_p16(&o, 0);
	if (xs_send(x, &o) != HIBR_OK)
		x->dmgop = 0;
out:
	s_free(&o);
	s_free(&r);
}

/* XTEST's version, which the server wants asked before a fake event. */
void xs_xtest(xs_scr *x)
{
	str o, r;

	x->xtop = xs_ext(x, "XTEST", 0);
	if (!x->xtop) {
		lg(HIBR_LERR, "xscr: the server has no XTEST, so nothing can be typed");
		return;
	}
	s_init(&o);
	s_init(&r);
	s_ch(&o, x->xtop);
	s_ch(&o, 0);
	xs_p16(&o, 2);
	s_ch(&o, 2);
	s_ch(&o, 0);
	xs_p16(&o, 2);
	if (xs_send(x, &o) != HIBR_OK || xs_reply(x, &r) != HIBR_OK)
		x->xtop = 0;
	s_free(&o);
	s_free(&r);
}

/* A property of a window as GetProperty answers it: its data into out,
   up to words 32-bit words of it; whether there was any. */
int xs_prop(xs_scr *x, unsigned w, unsigned atom, unsigned type, unsigned words,
	    str *out)
{
	str o, r;
	unsigned n;
	int ok = 0;

	s_init(&o);
	s_init(&r);
	s_ch(&o, 20);
	s_ch(&o, 0);
	xs_p16(&o, 6);
	xs_p32(&o, w);
	xs_p32(&o, atom);
	xs_p32(&o, type);
	xs_p32(&o, 0);
	xs_p32(&o, words);
	out->n = 0;
	if (xs_send(x, &o) == HIBR_OK && xs_reply(x, &r) == HIBR_OK) {
		n = xs_g32((unsigned char *)r.p + 16) *
		    ((unsigned char)r.p[1] / 8);
		if (n && 32 + (size_t)n <= r.n) {
			s_add(out, r.p + 32, n);
			ok = 1;
		}
	}
	s_free(&o);
	s_free(&r);
	return ok;
}

/* An atom by name, made if the server has none yet. */
unsigned xs_atom(xs_scr *x, const char *nm)
{
	str o, r;
	size_t n = strlen(nm);
	unsigned a = 0;

	s_init(&o);
	s_init(&r);
	s_ch(&o, 16);
	s_ch(&o, 0);
	xs_p16(&o, (unsigned)(2 + (n + 3) / 4));
	xs_p16(&o, (unsigned)n);
	xs_p16(&o, 0);
	s_add(&o, nm, n);
	while (o.n % 4)
		s_ch(&o, 0);
	if (xs_send(x, &o) == HIBR_OK && xs_reply(x, &r) == HIBR_OK)
		a = xs_g32((unsigned char *)r.p + 8);
	s_free(&o);
	s_free(&r);
	return a;
}

/* Wait until the server has answered everything sent so far: a
   GetInputFocus round trip. */
void xs_sync(xs_scr *x)
{
	str r;

	s_init(&r);
	xs_simple(x, 43, 0, 0, 0);
	xs_reply(x, &r);
	s_free(&r);
}

/* Become the screen's window manager: the root's SubstructureRedirect,
   which only one client may hold. Another one already holding it -- a
   window manager the person started on this screen -- answers BadAccess,
   which arrives as an error on that request and is read back after a round
   trip, so a screen with a manager of its own is known to be unmanaged
   rather than looking managed. Status 0 when this manages it. */
int xs_manage(xs_scr *x)
{
	unsigned v[3];
	unsigned short seq;

	v[0] = x->root;
	v[1] = 1u << 11;
	v[2] = (1u << 20) | (1u << 19);
	x->errseq = 0;
	xs_simple(x, 2, 0, v, 3);
	seq = x->seq;
	xs_sync(x);
	if (x->errseq == seq && x->errcode == 10) {
		lg(HIBR_LDBG, "xscr: another window manager has the screen");
		x->wm = 0;
		return HIBR_FAIL;
	}
	x->wm = 1;
	if (!x->fitw) {
		x->fitw = x->w;
		x->fith = x->h;
	}
	x->netname = xs_atom(x, "_NET_WM_NAME");
	x->utf8 = xs_atom(x, "UTF8_STRING");
	return HIBR_OK;
}

/* Show the windows that asked to be shown: a dialog -- one with
   WM_TRANSIENT_FOR -- centred at its own size, any other filling the
   region; each watched for its title, mapped, and given the keyboard. */
void xs_pending(xs_scr *x)
{
	unsigned w, v[3], gw, gh;
	str t, r, o;
	int tr, guard = 0;

	if (!x->wm)
		return;
	s_init(&t);
	s_init(&r);
	s_init(&o);
	while (x->pend.n && guard++ < 64) {
		w = (unsigned)(uintptr_t)x->pend.p[0];
		memmove(x->pend.p, x->pend.p + 1,
			(x->pend.n - 1) * sizeof *x->pend.p);
		x->pend.n--;
		tr = xs_prop(x, w, 68, 33, 1, &t);
		if (tr) {
			o.n = 0;
			s_ch(&o, 14);
			s_ch(&o, 0);
			xs_p16(&o, 2);
			xs_p32(&o, w);
			gw = gh = 200;
			if (xs_send(x, &o) == HIBR_OK && xs_reply(x, &r) == HIBR_OK) {
				gw = xs_g16((unsigned char *)r.p + 16);
				gh = xs_g16((unsigned char *)r.p + 18);
			}
			if (gw > (unsigned)x->fitw)
				gw = (unsigned)x->fitw;
			if (gh > (unsigned)x->fith)
				gh = (unsigned)x->fith;
			xs_place(x, w, (x->fitw - (int)gw) / 2,
				 (x->fith - (int)gh) / 2, gw, gh);
		} else {
			xs_place(x, w, 0, 0, (unsigned)x->fitw, (unsigned)x->fith);
			x->main = w;
			x->titledirty = 1;
		}
		v[0] = w;
		v[1] = 1u << 11;
		v[2] = 1u << 22;
		xs_simple(x, 2, 0, v, 3);
		if (xs_managed(x, w) < 0) {
			v_add(&x->mwin, (void *)(uintptr_t)w);
			v_add(&x->mtr, (void *)(uintptr_t)tr);
		}
		v[0] = w;
		xs_simple(x, 8, 0, v, 1);
		xs_focus(x, w);
	}
	s_free(&t);
	s_free(&r);
	s_free(&o);
}

/* The region a normal window fills, w by h pixels of the screen, and
   every normal window this manages fitted to it again. */
void xs_fit(xs_scr *x, int w, int h)
{
	size_t i;

	if (w > x->w)
		w = x->w;
	if (h > x->h)
		h = x->h;
	if (w < 1 || h < 1)
		return;
	if (w == x->fitw && h == x->fith)
		return;
	x->fitw = w;
	x->fith = h;
	for (i = 0; i < x->mwin.n; i++)
		if (!(uintptr_t)x->mtr.p[i])
			xs_place(x, (unsigned)(uintptr_t)x->mwin.p[i], 0, 0,
				 (unsigned)w, (unsigned)h);
	x->dirty = 1;
}

/* The main window's title, read again when it has changed: its
   _NET_WM_NAME, or its WM_NAME. */
void xs_title(xs_scr *x)
{
	str t;

	if (!x->titledirty)
		return;
	x->titledirty = 0;
	x->title.n = 0;
	if (!x->main)
		return;
	s_init(&t);
	if ((x->netname && xs_prop(x, x->main, x->netname, x->utf8, 64, &t)) ||
	    xs_prop(x, x->main, 39, 31, 64, &t))
		s_add(&x->title, t.p, strcspn(t.p, "\n\t"));
	s_free(&t);
}

/* A press goes to the window under the pointer, which this then gives the
   keyboard to, as a window manager does on a click. */
void xs_clickfocus(xs_scr *x)
{
	str r;
	unsigned c;
	unsigned v[1];

	if (!x->wm)
		return;
	s_init(&r);
	v[0] = x->root;
	xs_simple(x, 38, 0, v, 1);
	if (xs_reply(x, &r) == HIBR_OK) {
		c = xs_g32((unsigned char *)r.p + 12);
		if (c && c != x->focus && xs_managed(x, c) >= 0)
			xs_focus(x, c);
	}
	s_free(&r);
}

/* Forget a screen: its connection, its mapping and what it kept. */
void xs_free(xs_scr *x)
{
	size_t i;

	if (x->fd >= 0)
		close(x->fd);
	xs_srvdrop(x->srv, 1);
	if (x->fb)
		munmap(x->fb, x->fbn);
	if (x->fbfd >= 0)
		close(x->fbfd);
	free(x->kmap);
	free(x->rgb);
	s_free(&x->in);
	s_free(&x->title);
	v_free(&x->mwin);
	v_free(&x->mtr);
	v_free(&x->pend);
	for (i = 0; i < xs_scrs.n; i++)
		if (xs_scrs.p[i] == x)
			xs_scrs.p[i] = 0;
	free(x);
}

/* Open display n, whose screen Xvfb keeps in path. */
xs_scr *xs_open(int n, const char *path)
{
	xs_scr *x = xm(sizeof *x);

	memset(x, 0, sizeof *x);
	x->fd = x->fbfd = -1;
	s_init(&x->in);
	s_init(&x->title);
	if (xs_map(x, path) != HIBR_OK || xs_connect(x, n) != HIBR_OK) {
		xs_free(x);
		return 0;
	}
	xs_damage(x);
	xs_xtest(x);
	if (xs_keymap(x) != HIBR_OK)
		lg(HIBR_LDBG, "xscr: no keyboard map");
	x->id = xs_next++;
	x->dirty = 1;
	v_add(&xs_scrs, x);
	return x;
}

/* A fake key, button or motion through XTEST: type 2/3 key press/release,
   4/5 button press/release, 6 motion to x, y. */
int xs_fake(xs_scr *x, int type, int detail, int px, int py)
{
	str o;
	int r;

	if (!x->xtop)
		return HIBR_FAIL;
	s_init(&o);
	s_ch(&o, x->xtop);
	s_ch(&o, 2);
	xs_p16(&o, 9);
	s_ch(&o, type);
	s_ch(&o, detail);
	xs_p16(&o, 0);
	xs_p32(&o, 0);
	xs_p32(&o, type == 6 ? x->root : 0);
	xs_p32(&o, 0);
	xs_p32(&o, 0);
	xs_p16(&o, (unsigned)(px < 0 ? 0 : px));
	xs_p16(&o, (unsigned)(py < 0 ? 0 : py));
	xs_p32(&o, 0);
	xs_p32(&o, 0);
	r = xs_send(x, &o);
	s_free(&o);
	return r;
}

/* The keycode a keysym is on in the server's map, and whether shift is
   needed to reach it; 0 when no key has it. */
int xs_kc(xs_scr *x, unsigned ks, int *shift)
{
	int i, n = x->maxkc - x->minkc + 1, col;

	if (!x->kmap || !x->kpc)
		return 0;
	for (col = 0; col < 2 && col < x->kpc; col++)
		for (i = 0; i < n; i++)
			if (x->kmap[i * x->kpc + col] == ks) {
				*shift = col;
				return x->minkc + i;
			}
	return 0;
}

/* The keysym of a key the console names that has no glyph. */
unsigned xs_named(const char *k)
{
	static const struct { const char *n; unsigned ks; } t[] = {
		{ "enter", 0xff0d }, { "tab", 0xff09 }, { "backspace", 0xff08 },
		{ "escape", 0xff1b }, { "delete", 0xffff }, { "insert", 0xff63 },
		{ "home", 0xff50 }, { "left", 0xff51 }, { "up", 0xff52 },
		{ "right", 0xff53 }, { "down", 0xff54 }, { "pageup", 0xff55 },
		{ "pagedown", 0xff56 }, { "end", 0xff57 }, { "space", 0x20 },
	};
	size_t i;

	for (i = 0; i < sizeof t / sizeof t[0]; i++)
		if (!strcmp(k, t[i].n))
			return t[i].ks;
	if (k[0] == 'f' && k[1] >= '1' && k[1] <= '9') {
		int f = atoi(k + 1);

		if (f >= 1 && f <= 12)
			return 0xffbe + (unsigned)(f - 1);
	}
	return 0;
}

/* The keysym of one character, as X spells it: Latin-1 is itself, the
   rest of Unicode is 0x1000000 above. */
unsigned xs_charks(const char *p, int *len)
{
	const unsigned char *u = (const unsigned char *)p;
	unsigned c;

	*len = 1;
	if (u[0] < 0x80)
		return u[0];
	if ((u[0] & 0xe0) == 0xc0 && u[1]) {
		*len = 2;
		c = (u[0] & 0x1fu) << 6 | (u[1] & 0x3fu);
	} else if ((u[0] & 0xf0) == 0xe0 && u[1] && u[2]) {
		*len = 3;
		c = (u[0] & 0x0fu) << 12 | (u[1] & 0x3fu) << 6 | (u[2] & 0x3fu);
	} else if ((u[0] & 0xf8) == 0xf0 && u[1] && u[2] && u[3]) {
		*len = 4;
		c = (u[0] & 0x07u) << 18 | (u[1] & 0x3fu) << 12 |
		    (u[2] & 0x3fu) << 6 | (u[3] & 0x3fu);
	} else {
		return 0;
	}
	return c < 0x100 ? c : 0x1000000 + c;
}

/* Press and release one keysym, with the modifiers asked for and shift
   when the map needs it. */
int xs_press(xs_scr *x, unsigned ks, int ctrl, int alt, int shift)
{
	int kc, sh = 0, kcs = 0, kcc = 0, kca = 0, d;

	kc = xs_kc(x, ks, &sh);
	if (!kc) {
		lg(HIBR_LDBG, "xscr: no key has keysym 0x%x", ks);
		return HIBR_FAIL;
	}
	if (shift || sh)
		kcs = xs_kc(x, 0xffe1, &d);
	if (ctrl)
		kcc = xs_kc(x, 0xffe3, &d);
	if (alt)
		kca = xs_kc(x, 0xffe9, &d);
	if (kcc)
		xs_fake(x, 2, kcc, 0, 0);
	if (kca)
		xs_fake(x, 2, kca, 0, 0);
	if (kcs)
		xs_fake(x, 2, kcs, 0, 0);
	xs_fake(x, 2, kc, 0, 0);
	xs_fake(x, 3, kc, 0, 0);
	if (kcs)
		xs_fake(x, 3, kcs, 0, 0);
	if (kca)
		xs_fake(x, 3, kca, 0, 0);
	if (kcc)
		xs_fake(x, 3, kcc, 0, 0);
	return HIBR_OK;
}

/* A key as the console names it -- a character, or a name such as enter,
   with ctrl-, alt- and shift- in front for the modifiers held. */
int xs_key(xs_scr *x, const char *k)
{
	int ctrl = 0, alt = 0, shift = 0, len;
	unsigned ks;

	for (;;) {
		if (!strncmp(k, "ctrl-", 5) && k[5]) {
			ctrl = 1;
			k += 5;
		} else if (!strncmp(k, "alt-", 4) && k[4]) {
			alt = 1;
			k += 4;
		} else if (!strncmp(k, "shift-", 6) && k[6]) {
			shift = 1;
			k += 6;
		} else {
			break;
		}
	}
	ks = xs_named(k);
	if (!ks) {
		ks = xs_charks(k, &len);
		if (!ks || k[len]) {
			lg(HIBR_LERR, "xscr key: %s: not a key this knows", k);
			return 2;
		}
	}
	return xs_press(x, ks, ctrl, alt, shift);
}

/* Type text, one character after another. A character the keyboard map
   has no key for is passed over and said so in the log. */
int xs_type(xs_scr *x, const char *t)
{
	unsigned ks;
	int len;

	while (*t) {
		ks = xs_charks(t, &len);
		if (*t == '\n')
			ks = 0xff0d;
		else if (*t == '\t')
			ks = 0xff09;
		if (ks)
			xs_press(x, ks, 0, 0, 0);
		t += len;
	}
	return HIBR_OK;
}

/* One pixel of the screen as RGB. */
unsigned xs_pix(xs_scr *x, int px, int py)
{
	const unsigned char *p = x->fb + x->off + (size_t)py * x->bpl + (size_t)px * 4;
	unsigned v = x->lsb ? xs_g32(p) :
		(unsigned)p[0] << 24 | (unsigned)p[1] << 16 |
		(unsigned)p[2] << 8 | p[3];

	return ((v & x->rmask) >> x->rsh & 0xff) << 16 |
	       ((v & x->gmask) >> x->gsh & 0xff) << 8 |
	       ((v & x->bmask) >> x->bsh & 0xff);
}

/* The top-left rw by rh of the screen as RGB triples, what the display's
   picture call takes: the whole screen, or the region a fitted window
   fills, which is then all that is sent. */
void xs_torgb(xs_scr *x, int rw, int rh)
{
	size_t n = (size_t)x->w * x->h;
	unsigned char *o;
	unsigned v;
	int px, py;

	if (!x->rgb)
		x->rgb = xm(n * 3);
	o = x->rgb;
	for (py = 0; py < rh; py++)
		for (px = 0; px < rw; px++) {
			v = xs_pix(x, px, py);
			*o++ = (unsigned char)(v >> 16);
			*o++ = (unsigned char)(v >> 8);
			*o++ = (unsigned char)v;
		}
}

/* The screen as half blocks over h by w cells at row, col of the display
   -- two pixels a cell, sampled -- clipped to the pane's rectangle. */
void xs_cells(xs_scr *x, int row, int col, int h, int w, int prow, int pcol,
	      int ph, int pw, int rw, int rh)
{
	int r, c, c0, sr;
	unsigned a, b, fa = 0, ba = 0;
	str run;

	s_init(&run);
	for (r = 0; r < h; r++) {
		sr = row + r;
		if (sr < prow || sr >= prow + ph)
			continue;
		c = 0;
		while (c < w) {
			c0 = c;
			run.n = 0;
			while (c < w) {
				a = xs_pix(x, (int)((long)c * rw / w),
					   (int)((long)(2 * r) * rh / (2 * h)));
				b = xs_pix(x, (int)((long)c * rw / w),
					   (int)((long)(2 * r + 1) * rh / (2 * h)));
				if (c > c0 && (a != fa || b != ba))
					break;
				fa = a;
				ba = b;
				if (col + c >= pcol && col + c < pcol + pw)
					s_cat(&run, "\xe2\x96\x80");
				c++;
			}
			if (!run.n)
				continue;
			xs_dp->pen(DP_RGB | fa, DP_RGB | ba, 0);
			xs_dp->put(sr, col + c0 < pcol ? pcol : col + c0, run.p);
		}
	}
	s_free(&run);
}

/* xscr draw ID row col h w [-p pane] [-m pixels|half]: the screen, scaled
   to h by w cells, as pixels where the display can place them and half
   blocks where it cannot or is told not to. */
int xs_draw(sh *s, xs_scr *x, int ac, char **av)
{
	const char *pane = 0, *mode = "pixels";
	int i, n = 0, v[4] = { 0, 0, 0, 0 }, prow = 0, pcol = 0, ph = 0, pw = 0;
	int rows, cols, crop = 0, rw, rh;

	for (i = 3; i < ac; i++) {
		if (!strcmp(av[i], "-p") && i + 1 < ac) {
			pane = av[++i];
			continue;
		}
		if (!strcmp(av[i], "-m") && i + 1 < ac) {
			mode = av[++i];
			continue;
		}
		if (!strcmp(av[i], "-c")) {
			crop = 1;
			continue;
		}
		if (n < 4)
			v[n++] = atoi(av[i]);
	}
	if (n < 4 || v[2] < 1 || v[3] < 1) {
		lg(HIBR_LERR, "usage: xscr draw ID row col h w [-p pane] [-m pixels|half] [-c]");
		return 2;
	}
	if (!xs_dp)
		xs_dp = (const dp_api *)hibr_require(s, "display", DP_API_VER);
	if (!xs_dp || !xs_dp->isopen()) {
		lg(HIBR_LERR, "xscr draw: no display open");
		return HIBR_FAIL;
	}
	xs_dp->size(&rows, &cols);
	prow = pcol = 0;
	ph = rows;
	pw = cols;
	if (pane && (!xs_dp->prect ||
		     !xs_dp->prect(pane, &prow, &pcol, &ph, &pw))) {
		lg(HIBR_LERR, "xscr draw: no such pane: %s", pane);
		return HIBR_FAIL;
	}
	rw = x->w;
	rh = x->h;
	if (crop && x->fitw > 0 && x->fith > 0) {
		rw = x->fitw;
		rh = x->fith;
	}
	if (strcmp(mode, "half") && xs_dp->image) {
		xs_torgb(x, rw, rh);
		xs_seen(x);
		if (xs_dp->image(s, pane, v[0], v[1], v[2], v[3], x->rgb, rw,
				 rh, 0))
			return HIBR_OK;
	}
	xs_seen(x);
	if (pane) {
		v[0] += prow;
		v[1] += pcol;
	}
	xs_cells(x, v[0], v[1], v[2], v[3], prow, pcol, ph, pw, rw, rh);
	return HIBR_OK;
}

/* Say a result: into $RET, and printed unless := is taking it. */
void xs_say(sh *s, const char *t)
{
	hibr_ret(s, t);
	if (!s->bind)
		printf("%s\n", t);
}

void xs_usage(void)
{
	lg(HIBR_LERR, "usage: xscr start N WxH DIR | stop PID | open N FILE [PID] | "
		      "fd|poll|dirty|seen|size|manage|title|close ID | fit ID W H | "
		      "draw ID row col h w [-p pane] [-m pixels|half] [-c] | "
		      "key ID name | type ID text | move ID x y | "
		      "button ID press|release N x y | click ID N x y | "
		      "wheel ID up|down x y | list");
}

/* xscr: an X screen Xvfb draws, mapped and typed into. */
int m_xscr(sh *s, int ac, char **av)
{
	const char *sub = ac > 1 ? av[1] : "";
	xs_scr *x;
	char b[64];
	str o;
	size_t i;
	int bt;

	if (ac < 2) {
		xs_usage();
		return 2;
	}
	if (!strcmp(sub, "open")) {
		if (ac < 4) {
			xs_usage();
			return 2;
		}
		x = xs_open(atoi(av[2][0] == ':' ? av[2] + 1 : av[2]), av[3]);
		if (!x)
			return HIBR_FAIL;
		/* The server this screen is, so closing it ends the server:
		   one this shell started, or after a restart the pid the app
		   kept, adopted again. */
		if (ac > 4 && atoi(av[4]) > 0) {
			x->srv = (pid_t)atoi(av[4]);
			xs_srvdrop(x->srv, 0);
			v_add(&xs_srvs, (void *)(intptr_t)x->srv);
		}
		snprintf(b, sizeof b, "%d", x->id);
		xs_say(s, b);
		return HIBR_OK;
	}
	if (!strcmp(sub, "start")) {
		pid_t pid;

		if (ac < 5) {
			xs_usage();
			return 2;
		}
		pid = xs_start(atoi(av[2][0] == ':' ? av[2] + 1 : av[2]), av[3],
			       av[4]);
		if (pid <= 0) {
			lg(HIBR_LERR, "xscr start: Xvfb could not be started");
			return HIBR_FAIL;
		}
		snprintf(b, sizeof b, "%d", (int)pid);
		xs_say(s, b);
		return HIBR_OK;
	}
	if (!strcmp(sub, "stop") && ac > 2) {
		xs_srvdrop((pid_t)atoi(av[2]), 1);
		return HIBR_OK;
	}
	if (!strcmp(sub, "list")) {
		s_init(&o);
		for (i = 0; i < xs_scrs.n; i++) {
			x = xs_scrs.p[i];
			if (!x)
				continue;
			if (o.n)
				s_ch(&o, ' ');
			s_num(&o, x->id);
		}
		xs_say(s, o.p ? o.p : "");
		s_free(&o);
		return HIBR_OK;
	}
	if (ac < 3) {
		xs_usage();
		return 2;
	}
	x = xs_get(av[2]);
	if (!x)
		return HIBR_FAIL;
	if (!strcmp(sub, "close")) {
		xs_free(x);
		return HIBR_OK;
	}
	if (!strcmp(sub, "fd")) {
		snprintf(b, sizeof b, "%d", x->fd);
		xs_say(s, b);
		return HIBR_OK;
	}
	/* What the server has sent, acted on: status 0 when the screen has
	   changed since it was last drawn, 1 when it has not, so a caller
	   asks for a frame only then. A server that has gone is 2. */
	if (!strcmp(sub, "poll") || !strcmp(sub, "dirty")) {
		if (sub[0] == 'p') {
			if (!xs_fill(x, 0)) {
				lg(HIBR_LERR, "xscr: the server has gone");
				return 2;
			}
			xs_drain(x);
			xs_pending(x);
		}
		return x->dirty || !x->dmgop ? HIBR_OK : HIBR_FAIL;
	}
	/* Become the screen's window manager: 0 when this is, 1 when a window
	   manager of the person's own already is (Gitea #291). */
	if (!strcmp(sub, "manage")) {
		if (xs_manage(x) != HIBR_OK)
			return HIBR_FAIL;
		xs_pending(x);
		return HIBR_OK;
	}
	if (!strcmp(sub, "fit") && ac > 4) {
		xs_fit(x, atoi(av[3]), atoi(av[4]));
		xs_pending(x);
		return HIBR_OK;
	}
	if (!strcmp(sub, "title")) {
		xs_title(x);
		xs_say(s, x->title.p ? x->title.p : "");
		return HIBR_OK;
	}
	if (!strcmp(sub, "seen")) {
		xs_seen(x);
		return HIBR_OK;
	}
	if (!strcmp(sub, "size")) {
		snprintf(b, sizeof b, "%d %d", x->w, x->h);
		xs_say(s, b);
		return HIBR_OK;
	}
	if (!strcmp(sub, "draw"))
		return xs_draw(s, x, ac, av);
	if (!strcmp(sub, "key") && ac > 3)
		return xs_key(x, av[3]);
	if (!strcmp(sub, "type") && ac > 3)
		return xs_type(x, av[3]);
	if (!strcmp(sub, "move") && ac > 4) {
		x->px = atoi(av[3]);
		x->py = atoi(av[4]);
		return xs_fake(x, 6, 0, x->px, x->py);
	}
	if ((!strcmp(sub, "button") && ac > 6) ||
	    (!strcmp(sub, "click") && ac > 5) ||
	    (!strcmp(sub, "wheel") && ac > 5)) {
		int a = sub[0] == 'b' ? 4 : 3;

		if (sub[0] == 'w')
			bt = !strcmp(av[3], "up") ? 4 : 5;
		else
			bt = atoi(av[a]);
		if (bt < 1 || bt > 7)
			return 2;
		x->px = atoi(av[a + 1]);
		x->py = atoi(av[a + 2]);
		xs_fake(x, 6, 0, x->px, x->py);
		if (sub[0] != 'w' && (sub[0] != 'b' || strcmp(av[3], "release")))
			xs_clickfocus(x);
		if (sub[0] == 'b')
			return xs_fake(x, strcmp(av[3], "release") ? 4 : 5, bt, 0, 0);
		xs_fake(x, 4, bt, 0, 0);
		return xs_fake(x, 5, bt, 0, 0);
	}
	xs_usage();
	return 2;
}

void xs_fini(sh *s)
{
	size_t i;

	(void)s;
	for (i = 0; i < xs_scrs.n; i++)
		if (xs_scrs.p[i])
			xs_free(xs_scrs.p[i]);
	v_free(&xs_scrs);
	while (xs_srvs.n)
		xs_srvdrop((pid_t)(intptr_t)xs_srvs.p[0], 1);
	v_free(&xs_srvs);
}

const hibr_bi xs_bi[] = {
	{ "xscr", m_xscr, "an X screen in a window: Xvfb's, mapped and typed into" },
	HIBR_BI_END
};

HIBR_MODULE("xscr", "0.1", "an X screen in a window, drawn from Xvfb's own framebuffer",
	    xs_bi, 0, xs_fini);
