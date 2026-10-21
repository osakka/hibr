#define _GNU_SOURCE

#include "cn.h"
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

extern int cn_fd;
extern int cn_on;
extern volatile sig_atomic_t cn_wgen;

static sig_atomic_t cn_wseen;

str cn_pend;
int cn_pendo;

static int *cn_wfd;
static size_t cn_wn;

/* Per watched descriptor, in step with cn_wfd: whether it is a pipe, the
   bytes waiting on it when the last wait began, whether it was readable
   then, since when it has stayed readable with what waits never going down,
   whether it is muted, and when a muted one was last looked at. */
typedef struct {
	int fifo;
	long av;
	int ready;
	long long since;
	int mute;
	long long check;
} cn_wst;
static cn_wst *cn_ws;

/* The ready-and-never-drained span after which a watched pipe stops waking
   the wait, and how often a muted one is looked at again. */
#ifndef CN_WSTUCK
#define CN_WSTUCK 2000
#endif
#ifndef CN_WRECHECK
#define CN_WRECHECK 1000
#endif

long long cn_wms(void)
{
	struct timespec t;

	clock_gettime(CLOCK_MONOTONIC, &t);
	return (long long)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

/* Watch an extra descriptor -- a terminal's own pty master -- alongside key
   input, so a caller blocked in cn_key wakes the instant it has real output
   to show, rather than only finding out on its next scheduled redraw. */
void cn_watchadd(int fd)
{
	size_t i;

	if (fd < 0)
		return;
	struct stat st;

	for (i = 0; i < cn_wn; i++)
		if (cn_wfd[i] == fd)
			return;
	cn_wfd = xr(cn_wfd, (cn_wn + 1) * sizeof *cn_wfd);
	cn_ws = xr(cn_ws, (cn_wn + 1) * sizeof *cn_ws);
	memset(&cn_ws[cn_wn], 0, sizeof *cn_ws);
	cn_ws[cn_wn].av = -1;
	cn_ws[cn_wn].fifo = fstat(fd, &st) == 0 && S_ISFIFO(st.st_mode);
	cn_wfd[cn_wn++] = fd;
}

/* Stop watching it -- called once a terminal's own pty is closed, so a
   descriptor number the kernel reuses next is never mistaken for a live
   one still worth waking for. */
void cn_watchdel(int fd)
{
	size_t i;

	for (i = 0; i < cn_wn; i++) {
		if (cn_wfd[i] != fd)
			continue;
		memmove(cn_wfd + i, cn_wfd + i + 1,
			(cn_wn - i - 1) * sizeof *cn_wfd);
		memmove(cn_ws + i, cn_ws + i + 1,
			(cn_wn - i - 1) * sizeof *cn_ws);
		cn_wn--;
		return;
	}
}

/* Drop whichever watched descriptors the kernel no longer recognises -- a
   caller that forgot to unwatch one before closing it, most likely -- so
   one stale terminal can never take key input down for the whole desktop
   over a single EBADF. Returns whether it found one. */
static int cn_watchprune(void)
{
	int i, found = 0;

	for (i = (int)cn_wn - 1; i >= 0; i--) {
		if (fcntl(cn_wfd[i], F_GETFD) >= 0)
			continue;
		lg(HIBR_LDBG, "console: dropped a stale watched descriptor");
		cn_watchdel(cn_wfd[i]);
		found = 1;
	}
	return found;
}

/* How many bytes of input have been turned into keys since the start -- a
   test harness compares it with how many it has sent. */
size_t cn_eaten;

/* Drop the bytes already turned into keys, keeping the buffer small. */
void cn_eat(size_t n)
{
	cn_eaten += n;
	cn_pendo += (int)n;
	if ((size_t)cn_pendo >= cn_pend.n) {
		cn_pend.n = 0;
		cn_pendo = 0;
		if (cn_pend.p)
			cn_pend.p[0] = 0;
	}
}

/* Whether input is already waiting: decoded bytes not yet handed out, or the terminal readable right now. */
int cn_waiting(void)
{
	struct timeval tv = { 0, 0 };
	fd_set r;

	if (cn_pend.p && (size_t)cn_pendo < cn_pend.n)
		return 1;
	if (cn_fd < 0)
		return 0;
	FD_ZERO(&r);
	FD_SET(cn_fd, &r);
	return select(cn_fd + 1, &r, 0, 0, &tv) > 0;
}

/* Wait for the terminal to have something to say, or for a watched
   descriptor to. 1 means cn_fd itself is readable, the only case a caller
   should act on directly; 2 means only a watched one is -- wake up and let
   whoever owns it (term.hibr's own poll) notice, but there is nothing here
   to read from cn_fd. */
/* Wait without listening to the watched descriptors. A frame the output cap
   is holding back should end on a person -- a key, a click, a resize -- or on
   the clock, and never on a program writing: one writing a hundred times a
   second otherwise wakes the desktop a hundred times for a frame it has
   already decided to hold. Its bytes keep in the pty's own buffer, which is
   what a buffer is for, and one drain at the end of the wait takes the lot. */
int cn_quiet;

/* Whether a descriptor is readable this instant. */
int cn_wready(int fd)
{
	struct timeval tv = { 0, 0 };
	fd_set r;

	FD_ZERO(&r);
	FD_SET(fd, &r);
	return select(fd + 1, &r, 0, 0, &tv) > 0;
}

/* A watched pipe that stays ready while nothing reads it -- what it holds
   never going down from one wait to the next, end of file included, which
   stays ready at nothing -- would end every wait at once for ever: a desktop
   whose sound watcher's pipe was no longer drained went round its frame loop
   1,200 times a second at 82% of a core (Gitea #323). After CN_WSTUCK of
   that it stops waking the wait, and says which; it is looked at again every
   CN_WRECHECK and woken for once somebody has read from it. Only pipes: a
   terminal's pty is drained a share at a time and may stay full while it is
   being read, which is busy rather than stuck. */
void cn_wguard(void)
{
	long long now = cn_wms();
	int i, n;
	long av;

	for (i = 0; i < (int)cn_wn; i++) {
		cn_wst *w = &cn_ws[i];

		if (!w->fifo)
			continue;
		if (ioctl(cn_wfd[i], FIONREAD, &n) < 0)
			continue;
		av = n;
		if (w->mute) {
			if (now - w->check < CN_WRECHECK)
				continue;
			w->check = now;
			if (!cn_wready(cn_wfd[i]) || av < w->av) {
				w->mute = 0;
				w->since = 0;
				w->ready = 0;
				lg(HIBR_LWRN, "console: watched descriptor %d is being read again; waking for it",
				   cn_wfd[i]);
			}
			w->av = av;
			continue;
		}
		n = av > 0 || cn_wready(cn_wfd[i]);
		if (n && w->ready && av >= w->av) {
			if (!w->since)
				w->since = now;
			else if (now - w->since >= CN_WSTUCK) {
				w->mute = 1;
				w->check = now;
				lg(HIBR_LERR, "console: watched descriptor %d has been ready for %lld ms with nothing reading it (%ld bytes waiting); no longer waking for it",
				   cn_wfd[i], now - w->since, av);
			}
		} else {
			w->since = 0;
		}
		w->ready = n;
		w->av = av;
	}
}

/* Whether watched descriptor i is muted, for `console watching`. */
int cn_wmuted(int fd)
{
	size_t i;

	for (i = 0; i < cn_wn; i++)
		if (cn_wfd[i] == fd)
			return cn_ws[i].mute;
	return 0;
}

/* The watched descriptors, for `console watching`: how many, and each. */
size_t cn_wlist(int **fds)
{
	*fds = cn_wfd;
	return cn_wn;
}

int cn_wait(int ms)
{
	fd_set r;
	struct timespec ts;
	sigset_t blk, old;
	int k, mx, i, e;

	sigemptyset(&blk);
	sigaddset(&blk, SIGWINCH);
	sigprocmask(SIG_BLOCK, &blk, &old);
	if (cn_wgen != cn_wseen) {
		cn_wseen = cn_wgen;
		sigprocmask(SIG_SETMASK, &old, 0);
		lg(HIBR_LDBG, "console: a resize arrived before the wait began");
		return 0;
	}
	if (!cn_quiet)
		cn_wguard();
	FD_ZERO(&r);
	FD_SET(cn_fd, &r);
	mx = cn_fd;
	for (i = 0; !cn_quiet && i < (int)cn_wn; i++) {
		if (cn_ws[i].mute)
			continue;
		FD_SET(cn_wfd[i], &r);
		if (cn_wfd[i] > mx)
			mx = cn_wfd[i];
	}
	ts.tv_sec = ms / 1000;
	ts.tv_nsec = (long)(ms % 1000) * 1000000L;
	k = pselect(mx + 1, &r, 0, 0, ms < 0 ? 0 : &ts, &old);
	e = errno;
	cn_wseen = cn_wgen;
	sigprocmask(SIG_SETMASK, &old, 0);
	errno = e;
	if (k > 0)
		return FD_ISSET(cn_fd, &r) ? 1 : 2;
	if (k == 0)
		return 0;
	if (errno == EBADF)
		return cn_watchprune() ? 0 : -1;
	if (errno == EINTR) {
		lg(HIBR_LDBG, "console: wait interrupted, probably a resize");
		return 0;
	}
	return -1;
}

/* Read whatever is waiting onto the pending buffer. */
int cn_rdfill(void)
{
	char b[512];
	ssize_t k;

	k = read(cn_fd, b, sizeof b);
	if (k > 0) {
		s_add(&cn_pend, b, (size_t)k);
		return 1;
	}
	if (k < 0 && (errno == EINTR || errno == EAGAIN))
		return 0;
	return -1;
}

/* Name the modifier bits xterm packs into a CSI parameter: shift 1, alt 2,
   ctrl 4, and super 8, which only the kitty keyboard protocol ever sends
   (Gitea #326). Hyper, meta, caps lock and num lock (16 to 128) are left
   out, so a key reads the same with caps lock on as off. */
void cn_mods(str *o, int m)
{
	int b = m > 0 ? m - 1 : 0;

	if (b & 8)
		s_cat(o, "super-");
	if (b & 4)
		s_cat(o, "ctrl-");
	if (b & 2)
		s_cat(o, "alt-");
	if (b & 1)
		s_cat(o, "shift-");
}

/* The name of a tilde-terminated CSI key, or null. */
const char *cn_tilde(int n)
{
	switch (n) {
	case 1:
	case 7:
		return "home";
	case 2:
		return "insert";
	case 3:
		return "delete";
	case 4:
	case 8:
		return "end";
	case 5:
		return "pageup";
	case 6:
		return "pagedown";
	case 11:
		return "f1";
	case 12:
		return "f2";
	case 13:
		return "f3";
	case 14:
		return "f4";
	case 15:
		return "f5";
	case 17:
		return "f6";
	case 18:
		return "f7";
	case 19:
		return "f8";
	case 20:
		return "f9";
	case 21:
		return "f10";
	case 23:
		return "f11";
	case 24:
		return "f12";
	}
	return 0;
}

/* The name of a letter-terminated CSI or SS3 key, or null. */
const char *cn_final(char c)
{
	switch (c) {
	case 'A':
		return "up";
	case 'B':
		return "down";
	case 'C':
		return "right";
	case 'D':
		return "left";
	case 'H':
		return "home";
	case 'F':
		return "end";
	case 'P':
		return "f1";
	case 'Q':
		return "f2";
	case 'R':
		return "f3";
	case 'S':
		return "f4";
	case 'E':
		return "begin";
	}
	return 0;
}

/* Name a control byte the way a key map would want to see it. */
void cn_ctrl(str *o, unsigned char c)
{
	if (c == 9) {
		s_cat(o, "tab");
	} else if (c == 13 || c == 10) {
		s_cat(o, "enter");
	} else if (c == 127 || c == 8) {
		s_cat(o, "backspace");
	} else if (c == 0) {
		s_cat(o, "ctrl-space");
	} else if (c < 27) {
		s_cat(o, "ctrl-");
		s_ch(o, (char)('a' + c - 1));
	} else {
		s_cat(o, "ctrl-");
		s_ch(o, (char)('@' + c));
	}
}

/* Decode one mouse report in SGR form: \e[<b;col;rowM for a press or a drag,
   the same with a trailing m for a release. The button number carries the
   modifiers and whether it is a drag or the wheel. */
int cn_mouse(const char *p, size_t n, str *o, size_t *used)
{
	int b = 0, x = 0, y = 0, i = 3, rel;

	if (n < 9)
		return 0;
	for (; i < (int)n && p[i] >= '0' && p[i] <= '9'; i++)
		b = b * 10 + (p[i] - '0');
	if (i >= (int)n || p[i] != ';')
		return i >= (int)n ? 0 : -1;
	for (i++; i < (int)n && p[i] >= '0' && p[i] <= '9'; i++)
		x = x * 10 + (p[i] - '0');
	if (i >= (int)n || p[i] != ';')
		return i >= (int)n ? 0 : -1;
	for (i++; i < (int)n && p[i] >= '0' && p[i] <= '9'; i++)
		y = y * 10 + (p[i] - '0');
	if (i >= (int)n)
		return 0;
	if (p[i] != 'M' && p[i] != 'm')
		return -1;
	rel = p[i] == 'm';
	s_cat(o, "mouse ");
	if (b & 16)
		s_cat(o, "ctrl-");
	if (b & 8)
		s_cat(o, "alt-");
	if (b & 4)
		s_cat(o, "shift-");
	if (b & 64) {
		/* 64 up, 65 down, 66 left, 67 right: a terminal reports a
		   sideways wheel or a trackpad's horizontal swipe with the
		   last two, and testing only bit 0 made 66 read as wheelup
		   and 67 as wheeldown -- so a sideways scroll moved a list
		   up and down instead, which reads as a wild mouse. */
		switch (b & 3) {
		case 0:
			s_cat(o, "wheelup");
			break;
		case 1:
			s_cat(o, "wheeldown");
			break;
		case 2:
			s_cat(o, "wheelleft");
			break;
		default:
			s_cat(o, "wheelright");
			break;
		}
	} else {
		s_cat(o, rel ? "release" : (b & 32 ? "drag" : "press"));
		s_ch(o, ' ');
		switch (b & 3) {
		case 0:
			s_cat(o, "left");
			break;
		case 1:
			s_cat(o, "middle");
			break;
		case 2:
			s_cat(o, "right");
			break;
		default:
			s_cat(o, "none");
		}
	}
	/* Row then column, both counted from zero like everything else here. */
	s_ch(o, ' ');
	s_num(o, (long)y - 1);
	s_ch(o, ' ');
	s_num(o, (long)x - 1);
	*used = (size_t)i + 1;
	return 1;
}

/* Collect a bracketed paste up to its terminator. */
int cn_paste(const char *p, size_t n, str *o, size_t *used)
{
	size_t i;

	for (i = 6; i + 5 < n + 1; i++)
		if (i + 6 <= n && !memcmp(p + i, "\033[201~", 6)) {
			s_cat(o, "paste ");
			s_add(o, p + 6, i - 6);
			*used = i + 6;
			return 1;
		}
	return 0;
}

/* One component of an `rgb:` colour specification: one to four hex digits,
   scaled to eight bits the way xterm's own parser does, so `f` and `ffff`
   are both white. */
static int cn_hexpart(const char *p, size_t n, size_t *i, unsigned *out)
{
	unsigned v = 0, d, w = 0;

	while (*i < n && w < 4) {
		d = (unsigned char)p[*i];
		if (d >= '0' && d <= '9')
			d -= '0';
		else if (d >= 'a' && d <= 'f')
			d = d - 'a' + 10;
		else if (d >= 'A' && d <= 'F')
			d = d - 'A' + 10;
		else
			break;
		v = v * 16 + d;
		w++;
		(*i)++;
	}
	if (!w)
		return 0;
	while (w < 4) {
		v = v * 16 + (v & 15);
		w++;
	}
	*out = v >> 8;
	return 1;
}

/* An operating system command arriving as *input* is an answer to
   something this display asked: the terminal's own background (OSC 11) or
   one of its palette entries (OSC 4), each as `rgb:RRRR/GGGG/BBBB`. It is
   consumed and reported, never handed on as a key -- before this, every
   such reply was decoded as `alt-]` followed by its own text typed into
   whatever had focus, which is the reason the display never asked anything
   (Gitea #215).

   2 means "taken, and no key to report". An OSC that has not finished yet
   is 0 even once the Alt window has run out, as long as it still looks
   like one and is shorter than any reply could be -- a reply split across
   two reads over a slow link would otherwise be typed in as text -- and
   anything longer or holding an escape of its own is refused as unknown.*/
static int cn_osc(const char *p, size_t n, size_t *used)
{
	size_t i = 2, end = 0, skip = 0;
	long which, idx = -1;
	unsigned r, g, b;

	for (i = 2; i < n; i++) {
		if ((unsigned char)p[i] == 7) {
			end = i;
			skip = 1;
			break;
		}
		if ((unsigned char)p[i] == 27) {
			if (i + 1 >= n)
				return 0;
			if (p[i + 1] != '\\')
				return -1;
			end = i;
			skip = 2;
			break;
		}
	}
	if (!skip)
		return n < CN_OSCMAX ? 0 : -1;
	*used = end + skip;
	i = 2;
	which = 0;
	while (i < end && p[i] >= '0' && p[i] <= '9')
		which = which * 10 + (p[i++] - '0');
	if (i >= end || p[i] != ';')
		return 2;
	i++;
	if (which == 4) {
		idx = 0;
		while (i < end && p[i] >= '0' && p[i] <= '9')
			idx = idx * 10 + (p[i++] - '0');
		if (i >= end || p[i] != ';')
			return 2;
		i++;
	}
	if (end - i > 4 && !memcmp(p + i, "rgb:", 4))
		i += 4;
	else if (end - i > 5 && !memcmp(p + i, "rgba:", 5))
		i += 5;
	else if (i < end && p[i] == '#')
		i++;
	else
		return 2;
	if (!cn_hexpart(p, end, &i, &r))
		return 2;
	if (i < end && p[i] == '/')
		i++;
	if (!cn_hexpart(p, end, &i, &g))
		return 2;
	if (i < end && p[i] == '/')
		i++;
	if (!cn_hexpart(p, end, &i, &b))
		return 2;
	cn_gotcolour((int)which, (int)idx, (r << 16) | (g << 8) | b);
	return 2;
}

/* An application programming command arriving as *input* is an answer
   too: the kitty graphics protocol replies `Gi=<id>;OK` to a query of its
   own, which is how a terminal that speaks it says so rather than being
   recognised by name (Gitea #216). Taken and reported, never a key -- an
   APC decoded as one came out as `alt-_` and then its text. */
static int cn_apc(const char *p, size_t n, size_t *used)
{
	int kit, r = gx_apc(p, n, CN_OSCMAX, used, &kit);

	if (r == 2 && kit)
		cn_gotcap(CN_GFX_KITTY, 1);
	return r;
}

/* A control sequence with the private `?` prefix arriving as input is a
   report, never a key -- every key the decoder knows arrives without one.
   Primary device attributes (final `c`) is the one being asked for: a
   reply carrying 4 among its parameters says the terminal draws sixels,
   and one without it says it does not, which is the only negative answer
   either capability query can give (Gitea #216). */
static int cn_report(const char *p, size_t n, size_t *used)
{
	int da, sixel, kbd, copy, r = gx_report(p, n, CN_OSCMAX, used, &da,
						 &sixel, &kbd, &copy);

	if (r == 2 && da) {
		cn_gotcap(CN_GFX_SIXEL, sixel);
		cn_da28 = copy;
	}
	if (r == 2 && kbd)
		cn_gotkbd();
	return r;
}

int cn_dec(const char *p, size_t n, str *o, size_t *used, int last);

/* XTVERSION's answer, DCS > | text ST, taken rather than typed in: which
   emulator this is, and whether it copies rectangles itself (Gitea #337).
   Only hibr's own names itself hibr-term. 0 for one not finished yet, -1
   for something that is not one. */
static int cn_dcs(const char *p, size_t n, size_t *used)
{
	size_t i;

	for (i = 4; i + 1 < n; i++)
		if ((unsigned char)p[i] == 27 && p[i + 1] == '\\') {
			*used = i + 2;
			if (memmem(p, i, "hibr-term", 9)) {
				cn_hterm = 1;
				cn_vouch = memmem(p, i, "deccra", 6) != 0;
				lg(HIBR_LDBG, "console: hibr's emulator %s rectangles",
				   cn_vouch ? "copies" : "does not copy");
			}
			return 2;
		}
	return n < CN_OSCMAX ? 0 : -1;
}

/* A code point as UTF-8 into e, which has room for four bytes; how many. */
size_t cn_u8put(char *e, unsigned c)
{
	if (c < 0x80) {
		e[0] = (char)c;
		return 1;
	}
	if (c < 0x800) {
		e[0] = (char)(0xC0 | (c >> 6));
		e[1] = (char)(0x80 | (c & 0x3F));
		return 2;
	}
	if (c < 0x10000) {
		e[0] = (char)(0xE0 | (c >> 12));
		e[1] = (char)(0x80 | ((c >> 6) & 0x3F));
		e[2] = (char)(0x80 | (c & 0x3F));
		return 3;
	}
	e[0] = (char)(0xF0 | (c >> 18));
	e[1] = (char)(0x80 | ((c >> 12) & 0x3F));
	e[2] = (char)(0x80 | ((c >> 6) & 0x3F));
	e[3] = (char)(0x80 | (c & 0x3F));
	return 4;
}

/* What a US keyboard types with shift held: the kitty keyboard protocol
   reports the unshifted key and the shift beside it, and the shifted
   character only with a flag (4) not every terminal has -- so for
   punctuation this is a guess at the layout, and says so. */
unsigned cn_ushift(unsigned c)
{
	static const char from[] = "`1234567890-=[]\\;',./";
	static const char to[] = "~!@#$%^&*()_+{}|:\"<>?";
	const char *f;

	if (c >= 'a' && c <= 'z')
		return c - 32;
	if (c < 128 && c && (f = strchr(from, (int)c)))
		return (unsigned char)to[f - from];
	return c;
}

/* A key in the kitty keyboard protocol's own form, `CSI code ; mods u`,
   named exactly as the bytes a terminal without the protocol would have
   sent for it are named -- so a binding, an app's key handler and a program
   in a terminal window see what they saw before (Gitea #326). The legacy
   bytes are rebuilt and decoded by cn_dec: escape, alt as an ESC in front,
   ctrl folded into a control byte, shift into the character. What has no
   legacy form is put in front: `super-`. A code past the Unicode
   private-use start is a key with no character -- a lone modifier, a
   media key -- and is taken with nothing to report. */
int cn_csiu(unsigned code, int m, str *o)
{
	int b = m > 0 ? (m - 1) & 15 : 0;
	char e[8];
	size_t n = 0, used = 0;
	unsigned c = code;

	if ((code >= 57344 && code <= 63743) || code > 0x10FFFF) {
		lg(HIBR_LDBG, "console: key %u has no name here, dropped", code);
		return 2;
	}
	if (b & 8)
		s_cat(o, "super-");
	switch (code) {
	case 9:
		s_cat(o, b & 2 ? "alt-" : "");
		s_cat(o, b & 1 ? "shift-tab" : "tab");
		return 1;
	case 13:
	case 27:
	case 127:
	case 8:
		s_cat(o, b & 2 ? "alt-" : "");
		s_cat(o, code == 13 ? "enter" : code == 27 ? "escape" :
			 "backspace");
		return 1;
	}
	if (b & 1)
		c = cn_ushift(c);
	if (b & 4) {
		if ((c >= '@' && c <= '_') || (c >= 'a' && c <= 'z'))
			c &= 0x1f;
		else if (c == ' ' || c == '2')
			c = 0;
		else if (c == '?' || c == '8')
			c = 127;
		else if (c >= '3' && c <= '7')
			c = c - '3' + 27;
		else if (c == '/')
			c = 31;
		else
			s_cat(o, "ctrl-");
	}
	if (b & 2)
		e[n++] = 27;
	n += cn_u8put(e + n, c);
	if (cn_dec(e, n, o, &used, 1) != 1) {
		lg(HIBR_LDBG, "console: key %u could not be named", code);
		return 2;
	}
	return 1;
}

/* Turn the front of the buffer into one key name; 0 means need more bytes. */
int cn_dec(const char *p, size_t n, str *o, size_t *used, int last)
{
	unsigned cp;
	int l, np = 0, par[4], i, sub = 0, ev = 0;
	const char *nm;

	if (!n)
		return 0;
	if ((unsigned char)p[0] != 27) {
		if ((unsigned char)p[0] < 32 || (unsigned char)p[0] == 127) {
			cn_ctrl(o, (unsigned char)p[0]);
			*used = 1;
			return 1;
		}
		l = u8dec(p, n, &cp);
		if ((unsigned char)p[0] >= 0xC0 && (size_t)l > n)
			return 0;
		s_add(o, p, (size_t)l);
		*used = (size_t)l;
		return 1;
	}
	if (n == 1)
		return last ? (s_cat(o, "escape"), *used = 1, 1) : 0;
	if ((unsigned char)p[1] == 27 && n > 2) {
		lg(HIBR_LDBG, "console: an escape held back and sent with what followed");
		s_cat(o, "escape");
		*used = 1;
		return 1;
	}
	if (p[1] == '[' && n >= 3 && p[2] == '<')
		return cn_mouse(p, n, o, used);
	if (p[1] == '[' && n >= 6 && !memcmp(p + 2, "200~", 4))
		return cn_paste(p, n, o, used);
	if (p[1] == ']')
		return cn_osc(p, n, used);
	if (p[1] == '_')
		return cn_apc(p, n, used);
	if (p[1] == 'P' && n >= 4 && p[2] == '>' && p[3] == '|')
		return cn_dcs(p, n, used);
	if (p[1] == 'P' && n == 3 && p[2] == '>')
		return 0;
	if (p[1] == '[' && n >= 3 && p[2] == '?')
		return cn_report(p, n, used);
	if (p[1] == 'O' || p[1] == '[') {
		for (i = 0; i < 4; i++)
			par[i] = 0;
		i = 2;
		if (p[1] == '[' && i < (int)n && p[i] == '?')
			i++;
		while (i < (int)n) {
			if (p[i] >= '0' && p[i] <= '9') {
				if (sub && np == 1)
					ev = ev * 10 + (p[i] - '0');
				else if (!sub && np < 4)
					par[np] = par[np] * 10 + (p[i] - '0');
				i++;
				continue;
			}
			if (p[i] == ';') {
				if (++np > 3)
					np = 3;
				sub = 0;
				i++;
				continue;
			}
			if (p[i] == ':') {
				sub = 1;
				i++;
				continue;
			}
			break;
		}
		if (i >= (int)n)
			return 0;
		np++;
		/* A key's release, which only a terminal asked for event
		   types sends (kitty flag 2), is nothing to report. */
		if (ev == 3) {
			*used = (size_t)i + 1;
			return 2;
		}
		if (p[1] == '[' && p[i] == 'u') {
			*used = (size_t)i + 1;
			return cn_csiu((unsigned)par[0], np > 1 ? par[1] : 0, o);
		}
		if (p[i] == '~') {
			nm = cn_tilde(par[0]);
			if (!nm)
				return -1;
			cn_mods(o, np > 1 ? par[1] : 0);
			s_cat(o, nm);
			*used = (size_t)i + 1;
			return 1;
		}
		if (p[i] == 'Z') {
			s_cat(o, "shift-tab");
			*used = (size_t)i + 1;
			return 1;
		}
		nm = cn_final(p[i]);
		if (!nm)
			return -1;
		cn_mods(o, np > 1 ? par[1] : 0);
		s_cat(o, nm);
		*used = (size_t)i + 1;
		return 1;
	}
	s_cat(o, "alt-");
	if ((unsigned char)p[1] < 32 || (unsigned char)p[1] == 127) {
		cn_ctrl(o, (unsigned char)p[1]);
		*used = 2;
		return 1;
	}
	l = u8dec(p + 1, n - 1, &cp);
	if ((unsigned char)p[1] >= 0xC0 && (size_t)l > n - 1)
		return 0;
	s_add(o, p + 1, (size_t)l);
	*used = 1 + (size_t)l;
	return 1;
}

/* Read one key, waiting at most ms milliseconds; 0 means nothing arrived. */
int cn_key(int ms, str *out)
{
	size_t used = 0;
	int r, waited = 0, armed = 0;
	struct timespec deadline, now;
	long remain;

	if (!cn_on) {
		lg(HIBR_LERR, "console: not open");
		return -1;
	}
	/* Asked here as well as at a flush, since a program that only waits
	   for keys never flushes: either way the terminal is asked again
	   once a resize says it may not be the one that was asked before. */
	cn_askmaybe();
	for (;;) {
		if (cn_pend.n > (size_t)cn_pendo) {
			r = cn_dec(cn_pend.p + cn_pendo,
				   cn_pend.n - (size_t)cn_pendo, out, &used,
				   waited);
			if (r == 1) {
				cn_eat(used);
				return 1;
			}
			if (r == 2) {
				cn_eat(used);
				out->n = 0;
				if (out->p)
					out->p[0] = 0;
				armed = 0;
				continue;
			}
			if (r < 0) {
				lg(HIBR_LDBG, "console: unknown sequence, "
					      "skipping a byte");
				cn_eat(1);
				out->n = 0;
				if (out->p)
					out->p[0] = 0;
				armed = 0;
				continue;
			}
			if (waited)
				return 0;
			/* A real deadline, not a single retry: a watched
			   pty's own output becoming ready (cn_wait's 2) must
			   never cut this window short, or a busy terminal
			   window turns a real Alt-combination into a lone
			   Escape every time. */
			if (!armed) {
				clock_gettime(CLOCK_MONOTONIC, &deadline);
				deadline.tv_nsec += 50000000L;
				if (deadline.tv_nsec >= 1000000000L) {
					deadline.tv_sec++;
					deadline.tv_nsec -= 1000000000L;
				}
				armed = 1;
			}
			clock_gettime(CLOCK_MONOTONIC, &now);
			remain = (deadline.tv_sec - now.tv_sec) * 1000 +
				 (deadline.tv_nsec - now.tv_nsec) / 1000000;
			if (remain < 0)
				remain = 0;
			r = cn_wait((int)remain);
			if (r == 1) {
				if (cn_rdfill() < 0)
					return -1;
				continue;
			}
			if (r < 0)
				return -1;
			if (remain == 0)
				waited = 1;
			continue;
		}
		r = cn_wait(ms);
		if (r == 0 || r == 2)
			return 0;
		if (r < 0)
			return -1;
		if (cn_rdfill() < 0)
			return -1;
		waited = 0;
		armed = 0;
	}
}
