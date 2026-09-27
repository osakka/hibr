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

/* Remove a client by fd, closing it.  A no-op if it is not (or no longer)
   in the list: a poll() snapshot taken at the top of the loop can still
   name a client that an earlier part of the same pass already dropped. */
void hd_cdrop(vec *cls, int fd)
{
	size_t i;
	struct hd_cli *cn;

	for (i = 0; i < cls->n; i++) {
		cn = cls->p[i];
		if (cn->fd == fd) {
			close(fd);
			free(cn->front);
			s_free(&cn->mbuf);
			s_free(&cn->name);
			free(cn);
			cls->p[i] = cls->p[--cls->n];
			return;
		}
	}
}

/* The client with this fd, or NULL if it is not (or no longer) attached. */
struct hd_cli *hd_cfind(vec *cls, int fd)
{
	size_t i;
	struct hd_cli *cn;

	for (i = 0; i < cls->n; i++) {
		cn = cls->p[i];
		if (cn->fd == fd)
			return cn;
	}
	return 0;
}

/* Whether a client with this fd is still in the list. */
int hd_chas(vec *cls, int fd)
{
	return hd_cfind(cls, fd) != 0;
}

/* The client named this, or NULL if none is. */
struct hd_cli *hd_cfindname(vec *cls, const char *name)
{
	size_t i;
	struct hd_cli *cn;

	for (i = 0; i < cls->n; i++) {
		cn = cls->p[i];
		if (cn->name.p && !strcmp(cn->name.p, name))
			return cn;
	}
	return 0;
}

/* Whether any attached client is already marked primary. */
int hd_anyprimary(vec *cls)
{
	size_t i;

	for (i = 0; i < cls->n; i++)
		if (((struct hd_cli *)cls->p[i])->primary)
			return 1;
	return 0;
}

/* The furthest row and column any attached client's viewport reaches --
   the union's own size, queried without resizing anything. 0,0 with
   nobody attached. */
void hd_ubox(vec *cls, int *rows, int *cols)
{
	size_t i;
	struct hd_cli *cn;

	*rows = 0;
	*cols = 0;
	for (i = 0; i < cls->n; i++) {
		cn = cls->p[i];
		if (cn->row + cn->rows > *rows)
			*rows = cn->row + cn->rows;
		if (cn->col + cn->cols > *cols)
			*cols = cn->col + cn->cols;
	}
}

/* The pty's own size, and the emulator's: sized to the union, so the
   program always sees one screen big enough for everyone at their own
   place, not just the largest single client. Left alone with nobody
   attached -- there is nothing to size it from. */
void hd_union(vec *cls, int id, int tid)
{
	int rows, cols;

	if (!cls->n)
		return;
	hd_ubox(cls, &rows, &cols);
	hd_pty->resize(id, rows, cols);
	hd_tm->resize(tid, rows, cols);
}

/* Detach every attached client, telling each one why, and empty the list. */
void hd_cclear(vec *cls, const char *why, size_t n)
{
	size_t i;
	struct hd_cli *cn;

	for (i = 0; i < cls->n; i++) {
		cn = cls->p[i];
		hd_send(cn->fd, HD_DETACH, why, n);
		close(cn->fd);
		free(cn->front);
		s_free(&cn->mbuf);
		s_free(&cn->name);
		free(cn);
	}
	cls->n = 0;
}

/* Handle one new connection: a plain attach takes the session over,
   detaching whoever was there; HD_MATTACH joins alongside them instead.
   Anything else is a one-shot request, answered and closed.

   The attach payload is either 2 ints (rows, cols -- an older client, or one
   with no offset of its own: it sits at 0,0) or 4 (rows, cols, row, col).
   Accepting both means an old client and a new server, or the reverse, still
   attach -- just without a viewport, until whichever side is stale gets
   reinstalled. */
void hd_conn(int c, vec *cls, int id, int tid, int m, int *quit)
{
	struct pollfd q;
	str in, o;
	int t = 0, v[4];
	struct hd_cli *cn;

	q.fd = c;
	q.events = POLLIN;
	s_init(&in);
	if (poll(&q, 1, 2000) <= 0 || hd_recv(c, &t, &in) <= 0) {
		close(c);
		s_free(&in);
		return;
	}
	switch (t) {
	case HD_ATTACH:
		hd_cclear(cls, "attached elsewhere", 18);
		/* fall through */
	case HD_MATTACH:
		cn = xm(sizeof *cn);
		memset(cn, 0, sizeof *cn);
		cn->fd = c;
		if (in.n >= 4 * sizeof(int)) {
			memcpy(v, in.p, sizeof v);
			cn->rows = v[0];
			cn->cols = v[1];
			cn->row = v[2];
			cn->col = v[3];
			if (in.n > 4 * sizeof(int))
				s_add(&cn->name, in.p + 4 * sizeof(int),
				      in.n - 4 * sizeof(int));
		} else if (in.n == 2 * sizeof(int)) {
			memcpy(v, in.p, 2 * sizeof(int));
			cn->rows = v[0];
			cn->cols = v[1];
		}
		if (!cn->name.n) {
			s_cat(&cn->name, "client-");
			s_num(&cn->name, cn->fd);
		}
		if (!hd_anyprimary(cls))
			cn->primary = 1;
		v_add(cls, cn);
		hd_union(cls, id, tid);
		lg(HIBR_LDBG, "hold: attached at %d,%d size %dx%d (%lu now)",
		   cn->row, cn->col, cn->rows, cn->cols,
		   (unsigned long)cls->n);
		/* cn may be freed by a failed send below -- nothing after
		   this point may still read it. */
		hd_rensend1(cls, cn, tid);
		hd_poke(m);
		s_free(&in);
		return;
	case HD_DETACH:
		hd_cclear(cls, "detached", 8);
		hd_send(c, HD_DETACH, 0, 0);
		break;
	case HD_KILL:
		*quit = 1;
		hd_send(c, HD_KILL, 0, 0);
		break;
	case HD_INFO: {
		int urows, ucols;

		hd_ubox(cls, &urows, &ucols);
		s_init(&o);
		s_cat(&o, cls->n ? "attached" : "detached");
		s_ch(&o, ' ');
		s_num(&o, hd_pty->pid(id));
		s_ch(&o, ' ');
		s_num(&o, urows);
		s_ch(&o, 'x');
		s_num(&o, ucols);
		hd_send(c, HD_INFO, o.p, o.n);
		s_free(&o);
		break;
	}
	case HD_CLIENTS: {
		size_t ci;

		s_init(&o);
		for (ci = 0; ci < cls->n; ci++) {
			struct hd_cli *cn2 = cls->p[ci];

			if (o.n)
				s_ch(&o, '\n');
			s_cat(&o, cn2->name.p ? cn2->name.p : "");
			s_ch(&o, ' ');
			s_num(&o, cn2->row);
			s_ch(&o, ' ');
			s_num(&o, cn2->col);
			s_ch(&o, ' ');
			s_num(&o, cn2->rows);
			s_ch(&o, ' ');
			s_num(&o, cn2->cols);
			s_ch(&o, ' ');
			s_num(&o, cn2->primary);
		}
		hd_send(c, HD_CLIENTS, o.p, o.n);
		s_free(&o);
		break;
	}
	case HD_MOVE: {
		struct hd_cli *cn2 = 0;
		char *sp1, *sp2;
		int nrow = 0, ncol = 0;
		const char *rep;

		sp1 = in.n ? memchr(in.p, ' ', in.n) : 0;
		if (sp1) {
			*sp1 = 0;
			sp2 = strchr(sp1 + 1, ' ');
			if (sp2) {
				*sp2 = 0;
				nrow = atoi(sp1 + 1);
				ncol = atoi(sp2 + 1);
				cn2 = hd_cfindname(cls, in.p);
			}
		}
		if (cn2) {
			cn2->row = nrow;
			cn2->col = ncol;
			free(cn2->front);
			cn2->front = 0;
			hd_union(cls, id, tid);
			/* cn2 may be freed by a failed send below. */
			hd_rensend1(cls, cn2, tid);
			/* A moved display may be the primary one: nudge the
			   program to notice, same as a fresh attach does. */
			hd_poke(m);
		}
		rep = cn2 ? "ok" : "no such display";
		hd_send(c, HD_MOVE, rep, strlen(rep));
		break;
	}
	case HD_DROP: {
		struct hd_cli *cn2 = in.n ? hd_cfindname(cls, in.p) : 0;
		int wasp = cn2 ? cn2->primary : 0;
		const char *rep;

		if (cn2) {
			hd_send(cn2->fd, HD_DETACH, "switched off", 12);
			hd_cdrop(cls, cn2->fd);
			hd_union(cls, id, tid);
			if (wasp && cls->n)
				((struct hd_cli *)cls->p[0])->primary = 1;
			/* The union shrank, or the primary changed, or both. */
			hd_poke(m);
		}
		rep = cn2 ? "ok" : "no such display";
		hd_send(c, HD_DROP, rep, strlen(rep));
		break;
	}
	case HD_PRIMARY: {
		struct hd_cli *cn2 = in.n ? hd_cfindname(cls, in.p) : 0;
		const char *rep;
		size_t pi;

		if (cn2) {
			for (pi = 0; pi < cls->n; pi++)
				((struct hd_cli *)cls->p[pi])->primary = 0;
			cn2->primary = 1;
			/* Nudge the program to re-read who is primary now. */
			hd_poke(m);
		}
		rep = cn2 ? "ok" : "no such display";
		hd_send(c, HD_PRIMARY, rep, strlen(rep));
		break;
	}
	}
	close(c);
	s_free(&in);
}

/* The session itself: a program on a terminal, and whoever is attached --
   any number at once, all fed the same bytes and all able to type into it.

   This runs in a process of its own that has left the shell's session, so
   a hangup -- a closed terminal, a logout -- reaches only that one client,
   which is dropped from the list, and the server carries on with whoever
   else (if anyone) is still attached.  While nobody is, what the program
   writes is read and dropped, or it would fill the pty and stop; the next
   attach asks it to draw everything again anyway. */
void hd_serve(sh *s, const char *path, int rows, int cols, char **av,
	      int ready)
{
	struct sockaddr_un a;
	struct sigaction sa;
	struct pollfd *q = 0, pm;
	vec cls;
	int l, m, id, tid, quit = 0, st, fed;
	size_t i, j, nc;
	ssize_t k;
	str rb, in, mo;

	memset(&cls, 0, sizeof cls);
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
	tid = hd_tm->new(rows, cols);
	s_init(&rb);
	s_init(&in);
	s_init(&mo);
	s_grow(&rb, HIBR_IOCH);
	while (!quit && !hd_term) {
		fed = 0;
		nc = cls.n;
		q = xr(q, sizeof *q * (2 + nc));
		q[0].fd = l;
		q[0].events = POLLIN;
		q[1].fd = m;
		q[1].events = POLLIN;
		for (i = 0; i < nc; i++) {
			q[2 + i].fd = ((struct hd_cli *)cls.p[i])->fd;
			q[2 + i].events = POLLIN;
		}
		if (poll(q, (nfds_t)(2 + nc), -1) < 0) {
			if (errno == EINTR)
				continue;
			break;
		}
		if (q[1].revents) {
			k = read(m, rb.p, rb.cap - 1);
			if (k > 0) {
				hd_tm->feed(tid, rb.p, (size_t)k);
				fed = 1;
			} else if (!(k < 0 && (errno == EAGAIN ||
					       errno == EINTR))) {
				break;
			}
		}
		for (j = 0; j < nc; j++) {
			int fd = q[2 + j].fd, t, r;

			if (!q[2 + j].revents || !hd_chas(&cls, fd))
				continue;
			r = hd_recv(fd, &t, &in);
			if (r <= 0) {
				hd_cdrop(&cls, fd);
			} else if (t == HD_DATA) {
				struct hd_cli *cn = hd_cfind(&cls, fd);

				if (cn) {
					mo.n = 0;
					hd_mtrans(cn, in.p, in.n, &mo);
					hd_wall(m, mo.p, mo.n);
				}
			} else if (t == HD_SIZE && in.n == 2 * sizeof(int)) {
				struct hd_cli *cn = hd_cfind(&cls, fd);
				int sz[2];

				memcpy(sz, in.p, sizeof sz);
				if (cn) {
					cn->rows = sz[0];
					cn->cols = sz[1];
					free(cn->front);
					cn->front = 0;
					hd_union(&cls, id, tid);
				}
			} else if (t == HD_DETACH) {
				hd_cdrop(&cls, fd);
			}
		}
		if (q[0].revents & POLLIN) {
			int c = accept(l, 0, 0);
			if (c >= 0) {
				hd_cloexec(c);
				hd_conn(c, &cls, id, tid, m, &quit);
			}
		}
		if (fed) {
			pm.fd = m;
			pm.events = POLLIN;
			if (poll(&pm, 1, 0) <= 0 || !(pm.revents & POLLIN))
				hd_rensend(&cls, tid);
		}
	}
	unlink(path);
	close(l);
	free(q);
	if (quit || hd_term)
		hd_pty->drop(id);
	for (i = 0; i < 300 && hd_pty->alive(id); i++)
		usleep(10000);
	st = hd_pty->status(id);
	for (i = 0; i < cls.n; i++) {
		struct hd_cli *cn = cls.p[i];

		hd_send(cn->fd, HD_EXIT, (const char *)&st, sizeof st);
		close(cn->fd);
		free(cn->front);
		s_free(&cn->mbuf);
		s_free(&cn->name);
		free(cn);
	}
	v_free(&cls);
	hd_pty->drop(id);
	hd_tm->free(tid);
	s_free(&rb);
	s_free(&in);
	s_free(&mo);
	_exit(0);
}
