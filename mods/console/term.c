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

/* Report and clear the pending resize flag. */
int cn_resized(void)
{
	int r = cn_winch;

	cn_winch = 0;
	return r;
}

/* Ask the terminal how large it is, falling back to a sane default. */
void cn_size(int *rows, int *cols)
{
	struct winsize w;

	if (cn_fd >= 0 && ioctl(cn_fd, TIOCGWINSZ, &w) == 0 && w.ws_row &&
	    w.ws_col) {
		*rows = w.ws_row;
		*cols = w.ws_col;
		return;
	}
	*rows = 24;
	*cols = ed_cols();
}

/* Install the handlers that keep the terminal recoverable. */
void cn_hook(void)
{
	struct sigaction a;

	memset(&a, 0, sizeof a);
	a.sa_handler = cn_onwinch;
	sigaction(SIGWINCH, &a, &cn_owin);
	a.sa_handler = cn_onfatal;
	sigaction(SIGINT, &a, &cn_oint);
	sigaction(SIGTERM, &a, &cn_oterm);
	sigaction(SIGHUP, &a, &cn_ohup);
}

/* Put back the handlers that were there before. */
void cn_unhook(void)
{
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
