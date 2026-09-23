#define _GNU_SOURCE

#include "hd.h"
#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

volatile sig_atomic_t hd_winch;

/* The byte ctrl-\ sends, which detaches rather than reaching the program. */
#ifndef HD_ESC
#define HD_ESC 0x1c
#endif

/* Whatever a full-screen program may have left switched on, switched off:
   the alternate screen, a hidden cursor, the pen, every mouse mode,
   bracketed paste and application keys.  The program is still running and
   still thinks they are on, and the terminal being given back must not. */
static const char hd_reset[] = "\033[?1049l\033[?25h\033[0m\033[?1000l"
			       "\033[?1002l\033[?1003l\033[?1006l\033[?2004l"
			       "\033[?1l\033>";

void hd_onwinch(int n)
{
	(void)n;
	hd_winch = 1;
}

/* The size of the terminal this client is on. */
void hd_size(int *sz)
{
	struct winsize w;

	sz[0] = 24;
	sz[1] = 80;
	if (ioctl(0, TIOCGWINSZ, &w) == 0 && w.ws_row && w.ws_col) {
		sz[0] = w.ws_row;
		sz[1] = w.ws_col;
	}
}

/* Put this terminal on a session until it detaches or the program ends.

   The terminal is raw, signals included, so ctrl-c reaches the program as
   a byte rather than killing the client.  Returns 0 after a detach, or the
   program's own status when it ended. */
int hd_attach(const char *name, const char *path)
{
	struct termios sv, raw;
	struct sigaction a, oa;
	struct pollfd q[2];
	const char *why = "detached";
	int s, sz[2], t, st = 0, ended = 0, lost = 0;
	ssize_t k;
	char *p;
	str rb, in;

	s = hd_dial(path);
	if (s < 0) {
		lg(HIBR_LERR, "hold: %s: no such session", name);
		return 1;
	}
	if (!isatty(0) || tcgetattr(0, &sv) < 0) {
		close(s);
		lg(HIBR_LERR, "hold attach: not on a terminal");
		return 2;
	}
	hd_size(sz);
	if (!hd_send(s, HD_ATTACH, (const char *)sz, sizeof sz)) {
		close(s);
		lg(HIBR_LERR, "hold: %s: the session did not answer", name);
		return 1;
	}
	hd_selftitle("attached", path);
	fflush(0);
	raw = sv;
	cfmakeraw(&raw);
	tcsetattr(0, TCSADRAIN, &raw);
	memset(&a, 0, sizeof a);
	a.sa_handler = hd_onwinch;
	sigaction(SIGWINCH, &a, &oa);
	hd_winch = 0;
	s_init(&rb);
	s_init(&in);
	s_grow(&rb, HIBR_IOCH);
	for (;;) {
		if (hd_winch) {
			hd_winch = 0;
			hd_size(sz);
			hd_send(s, HD_SIZE, (const char *)sz, sizeof sz);
		}
		q[0].fd = 0;
		q[0].events = POLLIN;
		q[1].fd = s;
		q[1].events = POLLIN;
		if (poll(q, 2, -1) < 0) {
			if (errno == EINTR)
				continue;
			break;
		}
		if (q[0].revents) {
			k = read(0, rb.p, rb.cap - 1);
			if (k <= 0) {
				if (k < 0 && errno == EINTR)
					continue;
				lost = 1;
				break;
			}
			p = memchr(rb.p, HD_ESC, (size_t)k);
			if (p) {
				if (p > rb.p)
					hd_send(s, HD_DATA, rb.p,
						(size_t)(p - rb.p));
				hd_send(s, HD_DETACH, 0, 0);
				break;
			}
			if (!hd_send(s, HD_DATA, rb.p, (size_t)k)) {
				why = "lost the session";
				break;
			}
		}
		if (q[1].revents) {
			if (hd_recv(s, &t, &in) <= 0) {
				why = "lost the session";
				break;
			}
			if (t == HD_DATA) {
				hd_wall(1, in.p, in.n);
			} else if (t == HD_DETACH) {
				if (in.n)
					why = in.p;
				break;
			} else if (t == HD_EXIT) {
				if (in.n == sizeof st)
					memcpy(&st, in.p, sizeof st);
				ended = 1;
				break;
			}
		}
	}
	close(s);
	sigaction(SIGWINCH, &oa, 0);
	if (!lost) {
		hd_wall(1, hd_reset, sizeof hd_reset - 1);
		tcsetattr(0, TCSADRAIN, &sv);
		if (ended)
			fprintf(stderr, "[%s ended, status %d]\n", name, st);
		else
			fprintf(stderr, "[%s: %s -- hold attach %s]\n", name,
				why, name);
	}
	s_free(&rb);
	s_free(&in);
	return ended ? st : 0;
}
