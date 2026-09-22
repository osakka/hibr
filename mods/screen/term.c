#define _GNU_SOURCE

#include "scr.h"
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

int scr_fd = -1;
int scr_on;
struct termios scr_sv;
volatile sig_atomic_t scr_winch;
volatile sig_atomic_t scr_fatal;
struct sigaction scr_oint, scr_oterm, scr_ohup, scr_owin;

static const char scr_leave[] = "\033[?25h\033[0m\033[?1002l\033[?1006l"
			       "\033[?2004l\033[?1049l";
static const char scr_enter[] = "\033[?1049h\033[?25l\033[?2004h\033[2J";

/* Write a whole buffer, retrying a short or interrupted write. */
void scr_wr(int fd, const char *p, size_t n)
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
void scr_onwinch(int n)
{
	(void)n;
	scr_winch = 1;
}

/* Put the terminal back before a fatal signal is allowed to finish us. */
void scr_onfatal(int n)
{
	if (scr_on && scr_fd >= 0) {
		tcsetattr(scr_fd, TCSANOW, &scr_sv);
		scr_wr(scr_fd, scr_leave, sizeof scr_leave - 1);
		scr_on = 0;
	}
	scr_fatal = n;
	signal(n, SIG_DFL);
	raise(n);
}

/* Say whether the screen is currently held. */
int scr_isopen(void)
{
	return scr_on;
}

/* Report and clear the pending resize flag. */
int scr_resized(void)
{
	int r = scr_winch;

	scr_winch = 0;
	return r;
}

/* Ask the terminal how large it is, falling back to a sane default. */
void scr_size(int *rows, int *cols)
{
	struct winsize w;

	if (scr_fd >= 0 && ioctl(scr_fd, TIOCGWINSZ, &w) == 0 && w.ws_row &&
	    w.ws_col) {
		*rows = w.ws_row;
		*cols = w.ws_col;
		return;
	}
	*rows = 24;
	*cols = ed_cols();
}

/* Install the handlers that keep the terminal recoverable. */
void scr_hook(void)
{
	struct sigaction a;

	memset(&a, 0, sizeof a);
	a.sa_handler = scr_onwinch;
	sigaction(SIGWINCH, &a, &scr_owin);
	a.sa_handler = scr_onfatal;
	sigaction(SIGINT, &a, &scr_oint);
	sigaction(SIGTERM, &a, &scr_oterm);
	sigaction(SIGHUP, &a, &scr_ohup);
}

/* Put back the handlers that were there before. */
void scr_unhook(void)
{
	sigaction(SIGWINCH, &scr_owin, 0);
	sigaction(SIGINT, &scr_oint, 0);
	sigaction(SIGTERM, &scr_oterm, 0);
	sigaction(SIGHUP, &scr_ohup, 0);
}

/* Take the terminal: raw mode, alternate screen, no cursor. */
int scr_open(sh *s)
{
	struct termios r;
	int rows, cols;

	(void)s;
	if (scr_on)
		return HIBR_OK;
	scr_fd = isatty(1) ? 1 : (isatty(0) ? 0 : -1);
	if (scr_fd < 0) {
		scr_fd = open("/dev/tty", O_RDWR);
		if (scr_fd < 0) {
			lg(HIBR_LERR, "screen: no terminal to draw on");
			return HIBR_FAIL;
		}
	}
	if (tcgetattr(scr_fd, &scr_sv) != 0) {
		lg(HIBR_LERR, "screen: %s", strerror(errno));
		return HIBR_FAIL;
	}
	r = scr_sv;
	r.c_lflag &= ~(unsigned)(ECHO | ICANON | IEXTEN);
	r.c_iflag &= ~(unsigned)(IXON | ICRNL | INLCR | ISTRIP);
	r.c_oflag &= ~(unsigned)OPOST;
	r.c_cc[VMIN] = 0;
	r.c_cc[VTIME] = 0;
	if (tcsetattr(scr_fd, TCSADRAIN, &r) != 0) {
		lg(HIBR_LERR, "screen: %s", strerror(errno));
		return HIBR_FAIL;
	}
	fflush(0);
	scr_wr(scr_fd, scr_enter, sizeof scr_enter - 1);
	scr_hook();
	scr_on = 1;
	scr_winch = 0;
	scr_size(&rows, &cols);
	lg(HIBR_LDBG, "screen open, %d rows by %d columns", rows, cols);
	scr_clear();
	return HIBR_OK;
}

/* Give the terminal back exactly as it was found. */
void scr_close(sh *s)
{
	(void)s;
	if (!scr_on)
		return;
	scr_wr(scr_fd, scr_leave, sizeof scr_leave - 1);
	tcsetattr(scr_fd, TCSADRAIN, &scr_sv);
	scr_unhook();
	scr_on = 0;
	lg(HIBR_LDBG, "screen closed");
}
