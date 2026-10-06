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
volatile sig_atomic_t cn_wgen;
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
	cn_wgen++;
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
   first try and never sees the wait.

   The size asked for is also checked for being merely small rather than
   exactly zero -- a terminal mid-transition (entering the alternate screen
   counts) can answer a real ioctl with a real but degenerate size, a 1x1
   or thereabouts, which passes a bare truthiness check and then builds a
   grid nothing can be seen in, rather than falling back or retrying. No
   real terminal is legitimately this small.

   The budget below (50 tries, 20ms apart -- 1s worst case) is wider than
   the 200ms this shipped with: some terminal apps settle their real size
   noticeably later than a plain local pty does, and unlike a resize this
   one has no SIGWINCH to fall back on if the window never sends one of
   its own on open -- once this gives up, only a real, later resize event
   corrects it. Widening the budget only costs time on the rare terminal
   that needs it; one that already knows its size still answers first try. */
void cn_size(int *rows, int *cols)
{
	struct winsize w;
	int i;

	for (i = 0; i < 50; i++) {
		if (cn_fd >= 0 && ioctl(cn_fd, TIOCGWINSZ, &w) == 0 &&
		    w.ws_row > 2 && w.ws_col > 2) {
			*rows = w.ws_row;
			*cols = w.ws_col;
			return;
		}
		if (ioctl(2, TIOCGWINSZ, &w) == 0 &&
		    w.ws_row > 2 && w.ws_col > 2) {
			*rows = w.ws_row;
			*cols = w.ws_col;
			return;
		}
		usleep(20000);
	}
	*rows = 24;
	*cols = ed_cols();
}

/* The pixel size of a cell, from whichever end of the terminal answers:
   kitty, foot and others fill ws_xpixel and ws_ypixel, and a plain pty
   leaves them zero, which is the signal that nothing here can place pixels
   at all. Asked on every call rather than cached, since a resize that keeps
   the cell count can still change the cell's own size (a font change does
   exactly that), and the call is one ioctl. */
void cn_cellpx(int *w, int *h)
{
	struct winsize ws;
	int i;

	*w = 0;
	*h = 0;
	for (i = 0; i < 2; i++) {
		int fd = i == 0 ? cn_fd : 2;

		if (fd < 0 || ioctl(fd, TIOCGWINSZ, &ws) != 0)
			continue;
		if (ws.ws_row < 1 || ws.ws_col < 1)
			continue;
		if (ws.ws_xpixel < ws.ws_col || ws.ws_ypixel < ws.ws_row)
			continue;
		*w = ws.ws_xpixel / ws.ws_col;
		*h = ws.ws_ypixel / ws.ws_row;
		return;
	}
}

static int cn_gfxk = -1;

/* Whether this terminal takes a picture as pixels, and how.
   
   Two things have to be true: it must say what a cell measures in pixels
   (ws_xpixel), since sixel paints 1:1 and a wrong cell size spills the
   bitmap into its neighbours; and it must understand sixel. The second is
   taken from what the terminal calls itself -- kitty, foot, WezTerm, mlterm
   and iTerm2 all do -- rather than from a Primary Device Attributes probe,
   which would mean reading the terminal's reply out of the same stream the
   key decoder owns, with a keypress possibly racing it: that stream already
   has a trap record of its own (the Alt/Escape window), and a name plus a
   setting costs nobody a lost keystroke. HIBR_GFX says outright: sixel, or
   off. */
int cn_gfx(sh *s)
{
	const char *v, *t;
	int w = 0, h = 0;

	if (cn_gfxk >= 0)
		return cn_gfxk;
	cn_gfxk = CN_GFX_NONE;
	v = hibr_get(s, "HIBR_GFX");
	if (v && (!strcmp(v, "off") || !strcmp(v, "none")))
		return cn_gfxk;
	cn_cellpx(&w, &h);
	if (w < 2 || h < 2) {
		lg(HIBR_LDBG, "gfx: the terminal does not say what a cell measures");
		return cn_gfxk;
	}
	if (v && !strcmp(v, "sixel")) {
		cn_gfxk = CN_GFX_SIXEL;
		lg(HIBR_LDBG, "gfx: sixel, as HIBR_GFX asks; cell %dx%d", w, h);
		return cn_gfxk;
	}
	t = hibr_get(s, "TERM");
	if (t && (strstr(t, "kitty") || strstr(t, "foot") || strstr(t, "mlterm") ||
		  strstr(t, "wezterm") || strstr(t, "contour") || strstr(t, "yaft")))
		cn_gfxk = CN_GFX_SIXEL;
	t = hibr_get(s, "TERM_PROGRAM");
	if (t && (!strcmp(t, "WezTerm") || !strcmp(t, "iTerm.app") || !strcmp(t, "mintty")))
		cn_gfxk = CN_GFX_SIXEL;
	lg(HIBR_LDBG, "gfx: %s; cell %dx%d", cn_gfxk ? "sixel" : "none", w, h);
	return cn_gfxk;
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
			lg(HIBR_LERR, "console: no terminal to draw on");
			return HIBR_FAIL;
		}
	}
	if (tcgetattr(cn_fd, &cn_sv) != 0) {
		lg(HIBR_LERR, "console: %s", strerror(errno));
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
		lg(HIBR_LERR, "console: %s", strerror(errno));
		return HIBR_FAIL;
	}
	fflush(0);
#ifdef __APPLE__
	/* A terminal freshly opened by some Mac terminal apps stays black
	   until an actual resize, no matter how long a script waits after
	   that before drawing anything -- confirmed live: cn_size's own
	   widened retry budget made no difference, and neither did an
	   unconditional repaint once the desktop's own main loop started,
	   but a plain `sleep 1` before this point, with nothing else
	   different, fixed it outright. So this is not a size the terminal
	   answers wrong and later corrects -- something in the terminal's
	   own window setup has not finished, and entering the alternate
	   screen before it has does not queue for replay once it does.
	   One second, tested and confirmed working; not yet narrowed to
	   whether less would do. Not applied on any other platform, where
	   this has never been reported. */
	usleep(1000000);
#endif
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
	cn_imgclear();
	cn_wr(cn_fd, cn_moff, sizeof cn_moff - 1);
	cn_wr(cn_fd, cn_leave, sizeof cn_leave - 1);
	tcsetattr(cn_fd, TCSADRAIN, &cn_sv);
	cn_unhook();
	cn_on = 0;
	lg(HIBR_LDBG, "screen closed");
}

/* Ring the terminal's bell. */
void cn_bell(void)
{
	if (cn_on)
		cn_wr(cn_fd, "\a", 1);
}

/* Ask the terminal to show a notification of its own, with OSC 9; one
   that does not know it shows nothing. */
void cn_notify(const char *t)
{
	str o;

	if (!cn_on)
		return;
	s_init(&o);
	s_cat(&o, "\033]9;");
	s_cat(&o, t);
	s_ch(&o, 7);
	cn_wr(cn_fd, o.p, o.n);
	s_free(&o);
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
