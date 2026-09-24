#define _GNU_SOURCE

#include "cn.h"
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

int cn_fd = -1;
int cn_on;
struct termios cn_sv;
volatile sig_atomic_t cn_winch;
volatile sig_atomic_t cn_fatal;
struct sigaction cn_oint, cn_oterm, cn_ohup, cn_owin;

/* The signals that end a process without asking it, each of which must
   put the terminal back first, and what was installed for them before. */
static const int cn_fatals[] = { SIGSEGV, SIGBUS, SIGABRT, SIGFPE, SIGILL };
struct sigaction cn_ofatal[sizeof cn_fatals / sizeof cn_fatals[0]];

/* Whether ctrl-c, ctrl-\ and ctrl-z raise signals, as they do by default,
   or arrive as keys for the program to use. */
int cn_isig = 1;

static const char cn_leave[] = "\033[?25h\033[0m\033[?1002l\033[?1006l"
			       "\033[?2004l\033[?1049l";
static const char cn_enter[] = "\033[?1049h\033[?25l\033[?2004h\033[2J";

/* Named, so their lengths come from the compiler rather than from counting.
   Counting by hand is what truncated the clear in cn_enter once already. */
static const char cn_moff[] = "\033[?1003l\033[?1002l\033[?1000l\033[?1006l";
static const char cn_mclick[] = "\033[?1000h\033[?1006h";
static const char cn_mdrag[] = "\033[?1002h\033[?1006h";
static const char cn_mmotion[] = "\033[?1003h\033[?1006h";

/* Mouse reporting is off until something asks. Turning it on takes click and
   drag text selection away from whoever is watching, which is too rude to do
   to every full-screen program. 1006 is the SGR form, the only one that can
   report a column past 223. */
int cn_mousemode;

void cn_wr(int fd, const char *p, size_t n);

/* Ask the terminal for mouse reports, or stop asking. */
void cn_mouseon(int mode)
{
	if (!cn_on) {
		cn_mousemode = mode;
		return;
	}
	if (cn_mousemode)
		cn_wr(cn_fd, cn_moff, sizeof cn_moff - 1);
	cn_mousemode = mode;
	if (mode == 1)
		cn_wr(cn_fd, cn_mclick, sizeof cn_mclick - 1);
	else if (mode == 2)
		cn_wr(cn_fd, cn_mdrag, sizeof cn_mdrag - 1);
	else if (mode == 3)
		cn_wr(cn_fd, cn_mmotion, sizeof cn_mmotion - 1);
	lg(HIBR_LDBG, "mouse reporting mode %d", mode);
}

/* Write a whole buffer, retrying a short or interrupted write. */
void cn_wr(int fd, const char *p, size_t n)
{
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
		break;
	}
}

/* Note a terminal resize; the grids are rebuilt outside the handler. */
void cn_onwinch(int n)
{
	(void)n;
	cn_winch = 1;
}

/* Put the terminal back before a fatal signal is allowed to finish us. */
void cn_onfatal(int n)
{
	if (cn_on && cn_fd >= 0) {
		tcsetattr(cn_fd, TCSANOW, &cn_sv);
		cn_wr(cn_fd, cn_leave, sizeof cn_leave - 1);
		cn_on = 0;
	}
	cn_fatal = n;
	signal(n, SIG_DFL);
	raise(n);
}

/* Say whether the screen is currently held. */
int cn_isopen(void)
{
	return cn_on;
}

/* Report and clear the pending resize flag, with none of cn_reassert's
   side effects -- a caller that wants to debounce a burst of resizes into
   one redraw asks this every tick and acts only once it goes quiet, rather
   than paying cn_reassert's clear-and-repaint on every intermediate size. */
int cn_pending(void)
{
	int r = cn_winch;

	cn_winch = 0;
	return r;
}

/* Assert terminal modes again and invalidate the front buffer, so the next
   flush repaints everything.  Unconditional: the caller has already decided
   a resize happened and it is time to act on it, not asked whether one is
   still pending.

   A resize also says the terminal on the other end may not be the one the
   screen was opened on: a session reattached from somewhere else arrives
   as a SIGWINCH on a terminal that has never seen the alternate screen,
   the hidden cursor or the mouse mode.  So every resize asserts them
   again and repaints everything, which costs one full frame on an event
   that is rare anyway. */
void cn_reassert(void)
{
	if (!cn_on)
		return;
	cn_wr(cn_fd, cn_enter, sizeof cn_enter - 1);
	if (cn_mousemode)
		cn_mouseon(cn_mousemode);
	cn_inval();
	lg(HIBR_LDBG, "resized: terminal modes asserted again");
}

/* Report and clear the pending resize flag, asserting terminal modes and
   invalidating the buffer when one was pending.  What a caller that redraws
   on every resize, rather than debouncing a burst of them, uses. */
int cn_resized(void)
{
	int r = cn_pending();

	if (r)
		cn_reassert();
	return r;
}

/* Let ctrl-c and its kind raise signals, or hand them over as keys. */
void cn_signals(int on)
{
	struct termios r;

	cn_isig = !!on;
	if (!cn_on || tcgetattr(cn_fd, &r) != 0)
		return;
	if (on)
		r.c_lflag |= (unsigned)ISIG;
	else
		r.c_lflag &= ~(unsigned)ISIG;
	tcsetattr(cn_fd, TCSADRAIN, &r);
}

/* Ask the terminal how large it is, falling back to a sane default.

   Some terminals answer a fresh window's TIOCGWINSZ with 0x0 for a moment,
   before they have settled on a size -- asked right at open, that reads as
   no terminal at all and falls back to the default, which then only ever
   changes on an actual resize. Retried a few times, milliseconds apart,
   before giving up: a terminal that already knows its size answers on the
   first try and never sees the wait. */
void cn_size(int *rows, int *cols)
{
	struct winsize w;
	int i;

	for (i = 0; i < 10; i++) {
		if (cn_fd >= 0 && ioctl(cn_fd, TIOCGWINSZ, &w) == 0 &&
		    w.ws_row && w.ws_col) {
			*rows = w.ws_row;
			*cols = w.ws_col;
			return;
		}
		if (ioctl(2, TIOCGWINSZ, &w) == 0 && w.ws_row && w.ws_col) {
			*rows = w.ws_row;
			*cols = w.ws_col;
			return;
		}
		usleep(20000);
	}
	*rows = 24;
	*cols = ed_cols();
}

/* Install the handlers that keep the terminal recoverable. */
void cn_hook(void)
{
	struct sigaction a;
	size_t i;

	memset(&a, 0, sizeof a);
	a.sa_handler = cn_onwinch;
	sigaction(SIGWINCH, &a, &cn_owin);
	a.sa_handler = cn_onfatal;
	sigaction(SIGINT, &a, &cn_oint);
	sigaction(SIGTERM, &a, &cn_oterm);
	sigaction(SIGHUP, &a, &cn_ohup);
	for (i = 0; i < sizeof cn_fatals / sizeof cn_fatals[0]; i++)
		sigaction(cn_fatals[i], &a, &cn_ofatal[i]);
}

/* Put back the handlers that were there before. */
void cn_unhook(void)
{
	size_t i;

	for (i = 0; i < sizeof cn_fatals / sizeof cn_fatals[0]; i++)
		sigaction(cn_fatals[i], &cn_ofatal[i], 0);
	sigaction(SIGWINCH, &cn_owin, 0);
	sigaction(SIGINT, &cn_oint, 0);
	sigaction(SIGTERM, &cn_oterm, 0);
	sigaction(SIGHUP, &cn_ohup, 0);
}

/* Take the terminal: raw mode, alternate screen, no cursor. */
int cn_open(sh *s)
{
	struct termios r;
	int rows, cols;

	(void)s;
	if (cn_on)
		return HIBR_OK;
	cn_fd = isatty(1) ? 1 : (isatty(0) ? 0 : -1);
	if (cn_fd < 0) {
		cn_fd = open("/dev/tty", O_RDWR);
		if (cn_fd < 0) {
			lg(HIBR_LERR, "screen: no terminal to draw on");
			return HIBR_FAIL;
		}
	}
	if (tcgetattr(cn_fd, &cn_sv) != 0) {
		lg(HIBR_LERR, "screen: %s", strerror(errno));
		return HIBR_FAIL;
	}
	r = cn_sv;
	r.c_lflag &= ~(unsigned)(ECHO | ICANON | IEXTEN);
	if (!cn_isig)
		r.c_lflag &= ~(unsigned)ISIG;
	r.c_iflag &= ~(unsigned)(IXON | ICRNL | INLCR | ISTRIP);
	r.c_oflag &= ~(unsigned)OPOST;
	r.c_cc[VMIN] = 0;
	r.c_cc[VTIME] = 0;
	if (tcsetattr(cn_fd, TCSADRAIN, &r) != 0) {
		lg(HIBR_LERR, "screen: %s", strerror(errno));
		return HIBR_FAIL;
	}
	fflush(0);
	cn_wr(cn_fd, cn_enter, sizeof cn_enter - 1);
	cn_hook();
	cn_on = 1;
	if (cn_mousemode) {
		int m = cn_mousemode;
		cn_mousemode = 0;
		cn_mouseon(m);
	}
	cn_winch = 0;
	cn_size(&rows, &cols);
	lg(HIBR_LDBG, "screen open, %d rows by %d columns", rows, cols);
	cn_clear();
	return HIBR_OK;
}

/* Give the terminal back exactly as it was found. */
void cn_close(sh *s)
{
	(void)s;
	if (!cn_on)
		return;
	cn_wr(cn_fd, cn_moff, sizeof cn_moff - 1);
	cn_wr(cn_fd, cn_leave, sizeof cn_leave - 1);
	tcsetattr(cn_fd, TCSADRAIN, &cn_sv);
	cn_unhook();
	cn_on = 0;
	lg(HIBR_LDBG, "screen closed");
}

/* Append n bytes as base64, which is what OSC 52 carries. */
void cn_b64(str *o, const unsigned char *p, size_t n)
{
	static const char al[] =
		"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	unsigned v;
	size_t i;

	for (i = 0; i + 2 < n; i += 3) {
		v = (unsigned)p[i] << 16 | (unsigned)p[i + 1] << 8 | p[i + 2];
		s_ch(o, al[v >> 18 & 63]);
		s_ch(o, al[v >> 12 & 63]);
		s_ch(o, al[v >> 6 & 63]);
		s_ch(o, al[v & 63]);
	}
	if (n - i == 1) {
		v = (unsigned)p[i] << 16;
		s_ch(o, al[v >> 18 & 63]);
		s_ch(o, al[v >> 12 & 63]);
		s_cat(o, "==");
	} else if (n - i == 2) {
		v = (unsigned)p[i] << 16 | (unsigned)p[i + 1] << 8;
		s_ch(o, al[v >> 18 & 63]);
		s_ch(o, al[v >> 12 & 63]);
		s_ch(o, al[v >> 6 & 63]);
		s_ch(o, '=');
	}
}

/* Put text on the clipboard of the terminal the screen is on, with OSC 52.

   The terminal decides whether to honour it: most modern ones do, some ask
   first, and some ignore it entirely -- which is harmless, since the copy
   the desktop keeps for itself does not depend on it. Through hold it
   reaches whichever terminal is attached, which is the one being used. */
void cn_clip(const char *t)
{
	str o;

	if (!cn_on)
		return;
	s_init(&o);
	s_cat(&o, "\033]52;c;");
	cn_b64(&o, (const unsigned char *)t, strlen(t));
	s_ch(&o, 7);
	cn_wr(cn_fd, o.p, o.n);
	s_free(&o);
}
