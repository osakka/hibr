#define _GNU_SOURCE

#include "hd.h"
#include <errno.h>
#include <fcntl.h>
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
static const char hd_kbdoff[] = "\033[=0;1u";
/* The terminal's own pointer back, on a terminal that knows pointer shapes
   (Gitea #373): a program that hid it over a window must not leave it
   hidden once this client has gone. */
static const char hd_ptroff[] = "\033]22;\033\\";

/* What this display's Blit said about a frame, for the server to count. */
static int hd_clis = -1;
void hd_clians(const char *line)
{
	if (hd_clis >= 0)
		hd_send(hd_clis, HD_FRMANS, line, strlen(line));
}

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

/* The same, with the terminal's own pixel size after it -- 0 when it does
   not say, which is what a program on the far side has to read as "no
   pictures here" (ADR 0037, Gitea #103). Sent as its own HD_SIZE rather
   than added to the attach, and beside the two-int form rather than
   instead of it: a client from this version may well be attaching to a
   session still running the previous one, which wants exactly two ints for
   a resize and would read a longer attach payload's tail as a display
   name. Both messages cost a few bytes; a resize that silently stopped
   working across an upgrade would cost an afternoon. */
void hd_sizepx(int *sz)
{
	struct winsize w;

	hd_size(sz);
	sz[2] = 0;
	sz[3] = 0;
	if (ioctl(0, TIOCGWINSZ, &w) == 0 && w.ws_row && w.ws_col) {
		sz[2] = w.ws_xpixel;
		sz[3] = w.ws_ypixel;
	}
}

/* Put this terminal on a session until it detaches or the program ends.

   The terminal is raw, signals included, so ctrl-c reaches the program as
   a byte rather than killing the client.  Plain attach takes the session
   over, detaching whoever was there; multi joins alongside them instead, at
   the given row/col offset into the session's own virtual space -- 0,0
   unless this client is one of several placed side by side.  Returns 0
   after a detach, or the program's own status when it ended. */
/* Why the last attach ended, for HOLD_WHY: `ended` when the session did. */
str hd_endwhy;
/* What a client the server renews carries to the code that takes it over
   (Gitea #491): its connection and where it is, for `hold attach -R`, and
   the modules it ran, by the paths they were loaded from, tab-separated,
   for the script to load again. Set only when HOLD_WHY is renew. */
str hd_carry;
str hd_carrymods;

/* Hand this client over to the modules on disk: the terminal goes back to
   how it was found but keeps what is on it -- none of the resets an ending
   sends, since the same session is about to be drawn there again -- and the
   connection stays open for the code that comes next. */
void hd_handover(int s, const char *path, const char *name, int multi,
		 int row, int col, const char *dname)
{
	static const char *mods[] = { "pty.so", "term.so", "hold.so" };
	size_t i;
	str mp;

	hd_carry.n = 0;
	s_num(&hd_carry, s);
	s_cat(&hd_carry, "\t");
	s_num(&hd_carry, multi);
	s_cat(&hd_carry, "\t");
	s_num(&hd_carry, row);
	s_cat(&hd_carry, "\t");
	s_num(&hd_carry, col);
	s_cat(&hd_carry, "\t");
	s_cat(&hd_carry, path);
	s_cat(&hd_carry, "\t");
	s_cat(&hd_carry, name);
	s_cat(&hd_carry, "\t");
	s_cat(&hd_carry, dname ? dname : "");
	hd_carrymods.n = 0;
	s_init(&mp);
	for (i = 0; i < 3; i++) {
		mp.n = 0;
		if (!hd_selfmod(mods[i], &mp))
			continue;
		if (hd_carrymods.n)
			s_cat(&hd_carrymods, "\t");
		s_cat(&hd_carrymods, mp.p);
	}
	s_free(&mp);
	/* Inheritable, for the one way a renewal goes on in another process:
	   a shell that could not load the new modules into itself starts a
	   fresh one on this connection. */
	fcntl(s, F_SETFD, fcntl(s, F_GETFD) & ~FD_CLOEXEC);
	lg(HIBR_LDBG, "hold attach: renewing onto %s",
	   hd_carrymods.p ? hd_carrymods.p : "the module path");
}

int hd_attach(const char *name, const char *path, int multi, int row,
	      int col, const char *dname, int fl, int cfd)
{
	struct termios sv, raw;
	struct sigaction a, oa;
	struct pollfd q[3];
	const char *why = "detached";
	int s, sz[4], px[4], t, st = 0, ended = 0, lost = 0, gx, kbd = 0, copy = 0,
	    mpix = 0, ptr = -1, lock = 0, edit = 0, one = 1, depth, bfd, nq, hud = 0,
	    renew = 0, cv[2];
	gx_tcap tc;
	ssize_t k;
	char *p;
	str rb, in, pay, early;

#ifdef __APPLE__
	/* The desktop always auto-holds (dt_autohold), so on a fresh launch
	   this is the first and only thing that ever writes to the real
	   terminal -- console open, inside the detached held process
	   `hold new -d` started, never touches it at all. Confirmed live
	   that some Mac terminal apps do not render an alternate-screen
	   entry sent too soon after the window opens; cn_open's own delay
	   (mods/console/term.c) fixes a plain, unheld run, but is the wrong
	   process for this one. Same fix, same duration, here instead --
	   also paid on an ordinary reattach to an already-settled terminal,
	   where it is not needed, since nothing here distinguishes a fresh
	   attach from that. */
	if (!(fl & HD_AT_RESUME))
		usleep(1000000);
#endif
	s = fl & HD_AT_RESUME ? cfd : hd_dial(path);
	if (s < 0) {
		lg(HIBR_LERR, "hold: %s: %s", name,
		   errno == ETIMEDOUT ? "there and not answering"
				      : "no such session");
		return 1;
	}
	if (!isatty(0) || tcgetattr(0, &sv) < 0) {
		close(s);
		lg(HIBR_LERR, "hold attach: not on a terminal");
		return 2;
	}
	/* Ask this terminal what pictures it draws, before joining, so its
	   answer cannot be mixed with anything the session sends and nothing
	   on the far side has to pick it out of the keys (Gitea #258). Raw for
	   the asking, or the reply would wait for a newline and echo; a key
	   pressed meanwhile is kept and sent once attached. */
	raw = sv;
	cfmakeraw(&raw);
	tcsetattr(0, TCSANOW, &raw);
	s_init(&early);
	gx = gx_probe(0, 1, HD_GFXMS, &early, &kbd, &copy, &mpix, &ptr, &lock, &edit, &tc);
	depth = gx_depth(&tc);
	tcsetattr(0, TCSANOW, &sv);
	lg(HIBR_LDBG, "hold attach: this terminal draws %s", gx_name(gx));
	hd_size(sz);
	sz[2] = row;
	sz[3] = col;
	s_init(&pay);
	s_add(&pay, (const char *)sz, sizeof sz);
	if (dname && *dname)
		s_add(&pay, dname, strlen(dname));
	/* A renewed client is already attached: it says so, and the server
	   forgets what the code before it could do before hearing it again. */
	cv[0] = fl & HD_AT_RESUME ? 2 : 1;
	cv[1] = HIBR_ABI;
	if (!(fl & HD_AT_RESUME) &&
	    !hd_send(s, multi ? HD_MATTACH : HD_ATTACH, pay.p, pay.n)) {
		s_free(&pay);
		s_free(&early);
		close(s);
		lg(HIBR_LERR, "hold: %s: the session did not answer", name);
		return 1;
	}
	s_free(&pay);
	if ((fl & (HD_AT_CARRY | HD_AT_RESUME)) &&
	    !hd_send(s, HD_CARRY, (const char *)cv, sizeof cv)) {
		s_free(&early);
		close(s);
		lg(HIBR_LERR, "hold: %s: the session did not answer", name);
		return 1;
	}
	/* Before the pixel size, which is what makes the server work out the
	   union and nudge the program: by then it knows what this display
	   can be sent. */
	if (tc.blit >= 0)
		hd_send(s, HD_BLIT, (const char *)&tc.blit, sizeof tc.blit);
	hd_send(s, HD_GFX, (const char *)&gx, sizeof gx);
	if (kbd) {
		int one = 1;

		hd_send(s, HD_KBD, (const char *)&one, sizeof one);
	}
	if (copy) {
		int one = 1;

		hd_send(s, HD_COPY, (const char *)&one, sizeof one);
	}
	if (mpix) {
		int one = 1;

		hd_send(s, HD_MPIX, (const char *)&one, sizeof one);
	}
	if (ptr >= 0)
		hd_send(s, HD_PTR, (const char *)&ptr, sizeof ptr);
	if (lock)
		hd_send(s, HD_LOCK, (const char *)&lock, sizeof lock);
	if (edit)
		hd_send(s, HD_EDIT, (const char *)&edit, sizeof edit);
	/* Pictures by descriptor, where this display is a Blit session that
	   offers a frame socket (Blit ADR 0202): this client's own session,
	   named by its own environment -- the held program's is frozen from
	   whatever shell started it, and may name another session or none. */
	hd_clis = s;
	bf_answer = hd_clians;
	bfd = bf_open();
	if (bfd >= 0)
		hd_send(s, HD_FDCAP, (const char *)&one, sizeof one);
	if (bfd >= 0 && bf_dmabuf)
		hd_send(s, HD_DMABUF, bf_formats, strlen(bf_formats));
	if (tc.bce == 1)
		hd_send(s, HD_BCE, (const char *)&one, sizeof one);
	if (tc.rep == 1)
		hd_send(s, HD_REP, (const char *)&one, sizeof one);
	hd_send(s, HD_DEPTH, (const char *)&depth, sizeof depth);
	hd_sizepx(px);
	hd_send(s, HD_SIZE, (const char *)px, 4 * sizeof(int));
	if (early.n)
		hd_send(s, HD_DATA, early.p, early.n);
	s_free(&early);
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
			hd_send(s, HD_SIZE, (const char *)sz, 2 * sizeof(int));
			hd_sizepx(px);
			hd_send(s, HD_SIZE, (const char *)px,
				4 * sizeof(int));
		}
		q[0].fd = 0;
		q[0].events = POLLIN;
		q[1].fd = s;
		q[1].events = POLLIN;
		nq = 2;
		if (bfd >= 0) {
			q[2].fd = bfd;
			q[2].events = POLLIN;
			q[2].revents = 0;
			nq = 3;
		}
		if (poll(q, (nfds_t)nq, -1) < 0) {
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
		/* What Blit says on the frame socket is errors, kept in the
		   log, and whether its performance panel asks for status,
		   passed to the server; the socket closing puts this display
		   back on the terminal, which the desktop learns from hold
		   descriptors. */
		if (nq == 3 && q[2].revents) {
			int was = hud, zero = 0;

			if (bf_drain(bfd, &hud) < 0) {
				close(bfd);
				bfd = -1;
				hud = 0;
				hd_send(s, HD_FDCAP, (const char *)&zero,
					sizeof zero);
			} else if (hud != was) {
				hd_send(s, HD_HUD, (const char *)&hud, sizeof hud);
			}
		}
		if (q[1].revents) {
			if (hd_recv(s, &t, &in) <= 0) {
				why = "lost the session";
				break;
			}
			if (t == HD_FRAME) {
				int mfd = hd_takefd();

				if (bfd >= 0 && mfd >= 0 && in.n == BF_HDR &&
				    !bf_send(bfd, (const unsigned char *)in.p, mfd)) {
					int zero = 0;

					close(bfd);
					bfd = -1;
					hd_send(s, HD_FDCAP, (const char *)&zero,
						sizeof zero);
				}
				if (mfd >= 0)
					close(mfd);
			} else if (t == HD_FRM3) {
				int fds[BF_MAXFD], nf = hd_takefds(fds);

				if (bfd < 0 || in.n != BF_HDR3 ||
				    !bf_send3(bfd, (const unsigned char *)in.p, fds, nf)) {
					str e;

					s_init(&e);
					s_cat(&e, "FRAME error ");
					s_num(&e, in.n >= 8 ? (long)hd_frmnum((const unsigned char *)in.p + 4, 4) : 0);
					s_cat(&e, " ENOTSUP");
					hd_send(s, HD_FRMANS, e.p, e.n);
					s_free(&e);
				}
				while (nf > 0)
					close(fds[--nf]);
			} else if (t == HD_INPUT) {
				/* An input route's line for this display's own
				   Blit; with no frame socket, the answer is ours. */
				s_ch(&in, 0);
				if (bfd < 0 || !bf_say(bfd, in.p)) {
					str e;

					s_init(&e);
					s_cat(&e, "INPUT error ");
					s_num(&e, (long)hd_inid(in.p, strlen(in.p)));
					s_cat(&e, " ENOTSUP");
					hd_send(s, HD_FRMANS, e.p, e.n);
					s_free(&e);
				}
			} else if (t == HD_STATUS) {
				if (bfd >= 0 && hud)
					bf_status(bfd, in.p, in.n);
			} else if (t == HD_DATA) {
				hd_wall(1, in.p, in.n);
			} else if (t == HD_RENEW && (fl & HD_AT_CARRY)) {
				renew = 1;
				break;
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
	/* The frame socket is closed either way: a renewed client says a
	   fresh hello, which is how it gains what the new code can take.
	   Blit keeps the session's pictures when a connection goes, and
	   takes a new connection's frames for the same ids. */
	if (bfd >= 0)
		close(bfd);
	hd_clis = -1;
	bf_answer = 0;
	sigaction(SIGWINCH, &oa, 0);
	if (renew) {
		tcsetattr(0, TCSADRAIN, &sv);
		hd_handover(s, path, name, multi, row, col, dname);
		hd_endwhy.n = 0;
		s_cat(&hd_endwhy, "renew");
		s_free(&rb);
		s_free(&in);
		return 0;
	}
	close(s);
	if (!lost) {
		/* The keyboard flags the session put on this terminal, taken
		   off on both screens, since such a terminal keeps a set for
		   each (Gitea #326). */
		if (kbd)
			hd_wall(1, hd_kbdoff, sizeof hd_kbdoff - 1);
		hd_wall(1, hd_reset, sizeof hd_reset - 1);
		if (kbd)
			hd_wall(1, hd_kbdoff, sizeof hd_kbdoff - 1);
		if (ptr >= 0)
			hd_wall(1, hd_ptroff, sizeof hd_ptroff - 1);
		tcsetattr(0, TCSADRAIN, &sv);
		if (ended)
			fprintf(stderr, "[%s ended, status %d]\n", name, st);
		else
			fprintf(stderr, "[%s: %s -- hold attach %s]\n", name,
				why, name);
	}
	hd_endwhy.n = 0;
	s_cat(&hd_endwhy, ended ? "ended" : why);
	s_free(&rb);
	s_free(&in);
	return ended ? st : 0;
}
