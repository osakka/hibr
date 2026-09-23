#define _GNU_SOURCE

#include "hd.h"
#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

volatile sig_atomic_t hd_term;

void hd_onterm(int n)
{
	(void)n;
	hd_term = 1;
}

/* Ask the program to draw itself again.  A resize to the same size sends no
   SIGWINCH, and a terminal that has just attached has nothing on it, so the
   foreground of the held terminal is told directly. */
void hd_poke(int m)
{
	pid_t g;

	if (ioctl(m, TIOCGPGRP, &g) == 0 && g > 0)
		kill(-g, SIGWINCH);
}

/* Handle one new connection: an attach takes over as the client, anything
   else is a request answered and closed.  Returns the new client, or the
   old one. */
int hd_conn(int c, int cl, int id, int m, int *quit)
{
	struct pollfd q;
	str in, o;
	int t = 0, sz[2];

	q.fd = c;
	q.events = POLLIN;
	s_init(&in);
	if (poll(&q, 1, 2000) <= 0 || hd_recv(c, &t, &in) <= 0) {
		close(c);
		s_free(&in);
		return cl;
	}
	switch (t) {
	case HD_ATTACH:
		if (cl >= 0) {
			hd_send(cl, HD_DETACH, "attached elsewhere", 18);
			close(cl);
		}
		if (in.n == sizeof sz) {
			memcpy(sz, in.p, sizeof sz);
			hd_pty->resize(id, sz[0], sz[1]);
		}
		hd_poke(m);
		lg(HIBR_LDBG, "hold: attached");
		s_free(&in);
		return c;
	case HD_DETACH:
		if (cl >= 0) {
			hd_send(cl, HD_DETACH, "detached", 8);
			close(cl);
			cl = -1;
		}
		hd_send(c, HD_DETACH, 0, 0);
		break;
	case HD_KILL:
		*quit = 1;
		hd_send(c, HD_KILL, 0, 0);
		break;
	case HD_INFO:
		s_init(&o);
		s_cat(&o, cl >= 0 ? "attached" : "detached");
		s_ch(&o, ' ');
		s_num(&o, hd_pty->pid(id));
		hd_send(c, HD_INFO, o.p, o.n);
		s_free(&o);
		break;
	}
	close(c);
	s_free(&in);
	return cl;
}

/* The session itself: a program on a terminal, and whoever is attached.

   This runs in a process of its own that has left the shell's session, so
   a hangup -- a closed terminal, a logout -- reaches only the client, which
   dies, and the server carries on with nobody attached.  While nobody is,
   what the program writes is read and dropped, or it would fill the pty and
   stop; the next attach asks it to draw everything again anyway. */
void hd_serve(sh *s, const char *path, int rows, int cols, char **av,
	      int ready)
{
	struct sockaddr_un a;
	struct sigaction sa;
	struct pollfd q[3];
	int l, cl = -1, m, id, n, t, quit = 0, i, st;
	ssize_t k;
	str rb, in;

	signal(SIGHUP, SIG_IGN);
	signal(SIGPIPE, SIG_IGN);
	signal(SIGINT, SIG_IGN);
	signal(SIGQUIT, SIG_IGN);
	signal(SIGTSTP, SIG_IGN);
	signal(SIGCHLD, SIG_DFL);
	memset(&sa, 0, sizeof sa);
	sa.sa_handler = hd_onterm;
	sigaction(SIGTERM, &sa, 0);
	hd_selftitle("hold", path);

	l = socket(AF_UNIX, SOCK_STREAM, 0);
	hd_cloexec(l);
	memset(&a, 0, sizeof a);
	a.sun_family = AF_UNIX;
	strncpy(a.sun_path, path, sizeof a.sun_path - 1);
	unlink(path);
	if (l < 0 || bind(l, (struct sockaddr *)&a, sizeof a) < 0 ||
	    listen(l, 8) < 0) {
		hd_wall(ready, "0", 1);
		_exit(1);
	}
	chmod(path, 0600);
	setenv("HIBR_HOLD", path, 1);
	id = hd_pty->spawn(s, rows, cols, av);
	hd_wall(ready, id ? "1" : "0", 1);
	close(ready);
	if (!id) {
		unlink(path);
		_exit(1);
	}
	m = hd_pty->fd(id);
	s_init(&rb);
	s_init(&in);
	s_grow(&rb, HIBR_IOCH);
	while (!quit && !hd_term) {
		q[0].fd = l;
		q[0].events = POLLIN;
		q[1].fd = m;
		q[1].events = POLLIN;
		q[2].fd = cl;
		q[2].events = POLLIN;
		n = cl >= 0 ? 3 : 2;
		if (poll(q, (nfds_t)n, -1) < 0) {
			if (errno == EINTR)
				continue;
			break;
		}
		if (q[1].revents) {
			k = read(m, rb.p, rb.cap - 1);
			if (k > 0) {
				if (cl >= 0 && !hd_send(cl, HD_DATA, rb.p,
							(size_t)k)) {
					close(cl);
					cl = -1;
				}
			} else if (!(k < 0 && (errno == EAGAIN ||
					       errno == EINTR))) {
				break;
			}
		}
		if (n == 3 && q[2].revents) {
			i = hd_recv(cl, &t, &in);
			if (i <= 0) {
				close(cl);
				cl = -1;
			} else if (t == HD_DATA) {
				hd_wall(m, in.p, in.n);
			} else if (t == HD_SIZE && in.n == 2 * sizeof(int)) {
				int sz[2];
				memcpy(sz, in.p, sizeof sz);
				hd_pty->resize(id, sz[0], sz[1]);
			} else if (t == HD_DETACH) {
				close(cl);
				cl = -1;
			}
		}
		if (q[0].revents & POLLIN) {
			int c = accept(l, 0, 0);
			if (c >= 0) {
				hd_cloexec(c);
				cl = hd_conn(c, cl, id, m, &quit);
			}
		}
	}
	unlink(path);
	close(l);
	if (quit || hd_term)
		hd_pty->drop(id);
	for (i = 0; i < 300 && hd_pty->alive(id); i++)
		usleep(10000);
	st = hd_pty->status(id);
	if (cl >= 0) {
		hd_send(cl, HD_EXIT, (const char *)&st, sizeof st);
		close(cl);
	}
	hd_pty->drop(id);
	s_free(&rb);
	s_free(&in);
	_exit(0);
}
