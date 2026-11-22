#define _GNU_SOURCE

/* A terminal's program whose parent is not the process showing it (ADR 0050,
   route A): the desktop starts every terminal window's program, so it stays
   the program's parent however often the app's own process is replaced, and
   hands the app a copy of the master over a channel of their own
   (mods/fdmsg.c). The app reads and writes the master itself -- no byte
   goes through the desktop -- and asks the desktop for the rest: a program
   started, one taken back by its pid, a signal, a close; the desktop tells
   it when the program has ended and with what. Behind py_api unchanged, so
   term, hold and the scripts above them do not know.

   The wire, a message each, fields separated by NUL:
     app to desktop:  S rows cols xpix ypix cwd argc argv... env...
                      A pid rows cols      K rid signal      C rid
     desktop to app:  s rid pid (+master)  a rid pid done status (+master)
                      e why                x rid status */

#include "tt.h"
#include "../fdmsg.h"
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

extern vec tt_list;
extern int tt_next;
extern char **environ;

/* How long an app's process waits for the desktop to answer a spawn or an
   adoption, and for the end of a program whose terminal has closed. */
#ifndef TT_FARMS
#define TT_FARMS 2000
#endif
#ifndef TT_ENDMS
#define TT_ENDMS 500
#endif

/* This process's channel to the program's parent, when it has one. */
fm_ch *tt_via;

/* The desktop's end of each app's channel: its reader and whose it is. */
typedef struct tt_ch {
	fm_ch *c;
	char *owner;
} tt_ch;
vec tt_chans;

/* Milliseconds on a clock no one sets. */
long tt_ms(void)
{
	return fm_ms();
}

/* The fields of a message, each ending at a NUL: pointers into m. */
void tt_fields(str *m, vec *f)
{
	size_t i = 0;

	f->n = 0;
	while (i < m->n) {
		v_add(f, m->p + i);
		i += strlen(m->p + i) + 1;
	}
}

/* Add a field, NUL and all. */
void tt_field(str *m, const char *t)
{
	s_add(m, t, strlen(t) + 1);
}

/* Add a number as a field. */
void tt_fieldn(str *m, long v)
{
	str t;

	s_init(&t);
	s_num(&t, v);
	tt_field(m, t.p);
	s_free(&t);
}

/* Set this process's channel to the desktop, or with -1 let it go. */
int tt_viaset(int fd)
{
	if (tt_via) {
		fm_close(tt_via, 1);
		tt_via = 0;
	}
	if (fd < 0)
		return 1;
	tt_via = fm_open(fd);
	lg(HIBR_LINF, "pty: a terminal's program is the desktop's, asked on channel %d", fd);
	return 1;
}

/* The channel's descriptor, or -1. */
int tt_viafd(void)
{
	return tt_via ? tt_via->fd : -1;
}

/* A program the desktop owns, by the desktop's number for it. */
tt_p *tt_byrid(int rid)
{
	size_t i;
	tt_p *p;

	for (i = 0; i < tt_list.n; i++) {
		p = (tt_p *)tt_list.p[i];
		if (p->far == 2 && p->rid == rid)
			return p;
	}
	return 0;
}

/* A message from the desktop that is news, not an answer: a program ended. */
void tt_farnews(str *m)
{
	vec f = { 0, 0, 0 };
	tt_p *p;

	tt_fields(m, &f);
	if (f.n >= 3 && !strcmp((char *)f.p[0], "x")) {
		p = tt_byrid(atoi((char *)f.p[1]));
		if (p && !p->done) {
			p->done = 1;
			p->st = atoi((char *)f.p[2]);
			lg(HIBR_LDBG, "pty %d: the desktop says its program ended with %d", p->id, p->st);
		}
	}
	v_free(&f);
}

/* Take every message waiting, never waiting: each is news. 1 while the
   channel is there, 0 once the desktop has gone. */
int tt_pump(void)
{
	str m;
	int fds[FM_MAXFD], nf, i, r;

	if (!tt_via)
		return 0;
	r = fm_take(tt_via);
	s_init(&m);
	while (fm_next(tt_via, &m, fds, &nf)) {
		for (i = 0; i < nf; i++)
			close(fds[i]);
		tt_farnews(&m);
	}
	s_free(&m);
	return r >= 0;
}

/* Ask the desktop and wait for its answer, a message of one of the kinds in
   want, taking the news that comes meanwhile. 1 with the answer in ans and
   its descriptor in *fd (-1 for none), 0 with none in time. */
int tt_farask(str *req, const char *want, str *ans, int *fd)
{
	int fds[FM_MAXFD], nf, i;
	long end = tt_ms() + TT_FARMS;

	*fd = -1;
	if (!tt_via || !fm_send(tt_via->fd, req->p, req->n, 0, 0, TT_FARMS)) {
		lg(HIBR_LERR, "pty: the desktop cannot be asked: its channel has gone");
		return 0;
	}
	for (;;) {
		while (fm_next(tt_via, ans, fds, &nf)) {
			if (ans->n && strchr(want, ans->p[0])) {
				for (i = 1; i < nf; i++)
					close(fds[i]);
				if (nf)
					*fd = fds[0];
				return 1;
			}
			for (i = 0; i < nf; i++)
				close(fds[i]);
			tt_farnews(ans);
		}
		if (tt_via->gone || tt_ms() >= end)
			break;
		fm_wait(tt_via, (int)(end - tt_ms()));
		fm_take(tt_via);
	}
	lg(HIBR_LERR, "pty: the desktop did not answer in %d ms", TT_FARMS);
	return 0;
}

/* A record for a program the desktop owns, on the master it handed over. */
tt_p *tt_farrec(int fd, int rid, long pid, int rows, int cols)
{
	tt_p *p = xm(sizeof *p);

	memset(p, 0, sizeof *p);
	p->id = tt_next++;
	p->fd = fd;
	p->pid = pid;
	p->rows = rows;
	p->cols = cols;
	p->far = 2;
	p->rid = rid;
	p->chan = -1;
	fcntl(fd, F_SETFL, O_NONBLOCK);
	v_add(&tt_list, p);
	return p;
}

/* Start a program through the desktop, in this process's directory and with
   its environment, which is what it would have had as this process's own. */
tt_p *tt_farspawn(int rows, int cols, int xpix, int ypix, char **av)
{
	str q, a;
	vec f = { 0, 0, 0 };
	char *cwd, **e;
	int n = 0, fd = -1;
	tt_p *p = 0;

	s_init(&q);
	s_init(&a);
	tt_field(&q, "S");
	tt_fieldn(&q, rows);
	tt_fieldn(&q, cols);
	tt_fieldn(&q, xpix);
	tt_fieldn(&q, ypix);
	cwd = getcwd(0, 0);
	tt_field(&q, cwd ? cwd : "");
	free(cwd);
	while (av[n])
		n++;
	tt_fieldn(&q, n);
	for (n = 0; av[n]; n++)
		tt_field(&q, av[n]);
	for (e = environ; e && *e; e++)
		tt_field(&q, *e);
	if (tt_farask(&q, "se", &a, &fd)) {
		tt_fields(&a, &f);
		if (a.p[0] == 's' && f.n >= 3 && fd >= 0) {
			p = tt_farrec(fd, atoi((char *)f.p[1]), atol((char *)f.p[2]), rows, cols);
			p->xpix = xpix;
			p->ypix = ypix;
			lg(HIBR_LINF, "pty %d is pid %ld running %s, the desktop's child (its %d)",
			   p->id, p->pid, av[0], p->rid);
		} else {
			lg(HIBR_LERR, "pty: the desktop did not start %s: %s", av[0],
			   f.n > 1 ? (char *)f.p[1] : "no reason given");
			if (fd >= 0)
				close(fd);
		}
	}
	v_free(&f);
	s_free(&q);
	s_free(&a);
	return p;
}

/* Take back a program the desktop owns for this app, by its pid: what a
   window resuming in a fresh process does (term_resume's pty adopt). The
   master is put at the number asked for when that is free here, so a
   caller that kept the number it had before finds it there. */
tt_p *tt_faradopt(int want, long pid, int rows, int cols)
{
	str q, a;
	vec f = { 0, 0, 0 };
	int fd = -1;
	tt_p *p = 0;

	s_init(&q);
	s_init(&a);
	tt_field(&q, "A");
	tt_fieldn(&q, pid);
	tt_fieldn(&q, rows);
	tt_fieldn(&q, cols);
	if (tt_farask(&q, "ae", &a, &fd)) {
		tt_fields(&a, &f);
		if (a.p[0] == 'a' && f.n >= 5 && fd >= 0) {
			if (want > 2 && want != fd && fcntl(want, F_GETFD) < 0 &&
			    dup2(fd, want) == want) {
				close(fd);
				fd = want;
				fm_cloexec(fd);
			}
			p = tt_farrec(fd, atoi((char *)f.p[1]), atol((char *)f.p[2]), rows, cols);
			p->done = atoi((char *)f.p[3]);
			p->st = atoi((char *)f.p[4]);
			lg(HIBR_LINF, "pty %d adopted from the desktop: pid %ld on fd %d%s", p->id,
			   pid, fd, p->done ? ", already ended" : "");
		} else {
			lg(HIBR_LERR, "pty adopt: the desktop has no program %ld for this app: %s", pid,
			   f.n > 1 ? (char *)f.p[1] : "no reason given");
			if (fd >= 0)
				close(fd);
		}
	}
	v_free(&f);
	s_free(&q);
	s_free(&a);
	return p;
}

/* Whether a program the desktop owns still runs: what the desktop last said.
   Once its terminal has closed, the desktop's word is waited for, a little
   and once, since the end and the hangup come apart by a pass of its loop. */
int tt_faralive(tt_p *p)
{
	long end;

	tt_pump();
	if (!p->done && p->eof && !p->eofwait) {
		p->eofwait = 1;
		end = tt_ms() + TT_ENDMS;
		while (!p->done && tt_via && !tt_via->gone && tt_ms() < end) {
			fm_wait(tt_via, (int)(end - tt_ms()));
			tt_pump();
		}
	}
	return !p->done;
}

/* Signal a program the desktop owns: the desktop sends it, being the one
   that knows the pid is still its child's. */
int tt_farsignal(tt_p *p, int sig)
{
	str q;
	int ok;

	if (!tt_via)
		return 0;
	s_init(&q);
	tt_field(&q, "K");
	tt_fieldn(&q, p->rid);
	tt_fieldn(&q, sig);
	ok = fm_send(tt_via->fd, q.p, q.n, 0, 0, TT_FARMS);
	s_free(&q);
	return ok;
}

/* Close a program the desktop owns: the desktop ends it if it runs and
   lets the terminal go; this process lets its copy of the master go. */
void tt_fardrop(tt_p *p)
{
	str q;

	if (tt_via && !tt_via->gone) {
		s_init(&q);
		tt_field(&q, "C");
		tt_fieldn(&q, p->rid);
		fm_send(tt_via->fd, q.p, q.n, 0, 0, TT_FARMS);
		s_free(&q);
	}
	if (p->fd >= 0)
		close(p->fd);
	tt_unlist(p);
	lg(HIBR_LDBG, "pty %d closed, the desktop's %d", p->id, p->rid);
	tt_free(p);
}

/* The desktop's end of a channel, by descriptor; made for owner on first use. */
tt_ch *tt_chan(int fd, const char *owner)
{
	size_t i;
	tt_ch *c;

	for (i = 0; i < tt_chans.n; i++) {
		c = (tt_ch *)tt_chans.p[i];
		if (c->c->fd == fd)
			return c;
	}
	if (!owner)
		return 0;
	c = xm(sizeof *c);
	memset(c, 0, sizeof *c);
	c->c = fm_open(fd);
	c->owner = xs(owner);
	v_add(&tt_chans, c);
	return c;
}

/* Answer an app on its channel, a message of fields and a descriptor. */
void tt_say(int fd, str *m, int xfd)
{
	if (!fm_send(fd, m->p, m->n, xfd >= 0 ? &xfd : 0, xfd >= 0, 100))
		lg(HIBR_LWRN, "pty: an app's channel %d took no answer", fd);
}

/* A refusal, with why. */
void tt_no(int fd, const char *why)
{
	str m;

	s_init(&m);
	tt_field(&m, "e");
	tt_field(&m, why);
	tt_say(fd, &m, -1);
	s_free(&m);
}

/* A program this process owns for the channel's app, by the app's number. */
tt_p *tt_served(int rid, tt_ch *c)
{
	tt_p *p = tt_find(rid);

	return p && p->far == 1 && p->chan == c->c->fd ? p : 0;
}

/* One request from an app's process. */
void tt_request(tt_ch *c, str *m, str *log)
{
	vec f = { 0, 0, 0 };
	vec av = { 0, 0, 0 }, env = { 0, 0, 0 };
	str a;
	const char *k;
	size_t i, n;
	long pid;
	tt_p *p;

	s_init(&a);
	tt_fields(m, &f);
	k = f.n ? (const char *)f.p[0] : "";
	if (!strcmp(k, "S") && f.n >= 7) {
		n = (size_t)atol((char *)f.p[6]);
		if (!n || 7 + n > f.n) {
			tt_no(c->c->fd, "no program named");
			goto out;
		}
		for (i = 0; i < n; i++)
			v_add(&av, f.p[7 + i]);
		v_add(&av, 0);
		for (i = 7 + n; i < f.n; i++)
			v_add(&env, f.p[i]);
		v_add(&env, 0);
		p = tt_spawnx(atoi((char *)f.p[1]), atoi((char *)f.p[2]), atoi((char *)f.p[3]),
			      atoi((char *)f.p[4]), (char *)f.p[5], (char **)av.p, (char **)env.p);
		if (!p) {
			tt_no(c->c->fd, strerror(errno));
			goto out;
		}
		p->far = 1;
		p->chan = c->c->fd;
		p->owner = xs(c->owner);
		tt_field(&a, "s");
		tt_fieldn(&a, p->id);
		tt_fieldn(&a, p->pid);
		tt_say(c->c->fd, &a, p->fd);
		s_fmt(log, "spawned %d %ld %s\n", p->id, p->pid, (char *)av.p[0]);
	} else if (!strcmp(k, "A") && f.n >= 4) {
		pid = atol((char *)f.p[1]);
		p = 0;
		for (i = 0; i < tt_list.n; i++) {
			p = (tt_p *)tt_list.p[i];
			if (p->far == 1 && p->pid == pid && p->owner && !strcmp(p->owner, c->owner) &&
			    (p->chan < 0 || p->chan == c->c->fd))
				break;
			p = 0;
		}
		if (!p) {
			tt_no(c->c->fd, "no such program of this app's, or another process holds it");
			s_fmt(log, "refused %ld\n", pid);
			goto out;
		}
		p->chan = c->c->fd;
		p->lone = 0;
		if (atoi((char *)f.p[2]) > 0 && atoi((char *)f.p[3]) > 0)
			tt_resize(p, atoi((char *)f.p[2]), atoi((char *)f.p[3]));
		tt_alive(p);
		tt_field(&a, "a");
		tt_fieldn(&a, p->id);
		tt_fieldn(&a, p->pid);
		tt_fieldn(&a, p->done);
		tt_fieldn(&a, p->st);
		tt_say(c->c->fd, &a, p->fd);
		s_fmt(log, "adopted %d %ld\n", p->id, p->pid);
	} else if (!strcmp(k, "K") && f.n >= 3) {
		p = tt_served(atoi((char *)f.p[1]), c);
		if (p && !p->done)
			kill((pid_t)p->pid, atoi((char *)f.p[2]));
	} else if (!strcmp(k, "C") && f.n >= 2) {
		p = tt_served(atoi((char *)f.p[1]), c);
		if (p) {
			s_fmt(log, "closed %d %ld\n", p->id, p->pid);
			tt_drop(p);
		}
	} else {
		lg(HIBR_LWRN, "pty: %s's process asked %s, which is no request", c->owner,
		   *k ? k : "nothing");
	}
out:
	v_free(&f);
	v_free(&av);
	v_free(&env);
	s_free(&a);
}

/* Serve an app's channel: every request it has sent, never waiting. 1 when
   the channel has ended -- its programs then held by no process, lone from
   now, and the channel forgotten and closed. */
int tt_serve(int fd, const char *owner, str *log)
{
	tt_ch *c = tt_chan(fd, owner);
	str m;
	int fds[FM_MAXFD], nf, i, r;

	r = fm_take(c->c);
	s_init(&m);
	while (fm_next(c->c, &m, fds, &nf)) {
		for (i = 0; i < nf; i++)
			close(fds[i]);
		tt_request(c, &m, log);
	}
	s_free(&m);
	if (r >= 0)
		return 0;
	tt_unserve(fd);
	return 1;
}

/* Let an app's channel go: its programs are held by no process from now. */
void tt_unserve(int fd)
{
	size_t i;
	tt_ch *c;
	tt_p *p;

	for (i = 0; i < tt_list.n; i++) {
		p = (tt_p *)tt_list.p[i];
		if (p->far == 1 && p->chan == fd) {
			p->chan = -1;
			p->lone = tt_ms();
		}
	}
	for (i = 0; i < tt_chans.n; i++) {
		c = (tt_ch *)tt_chans.p[i];
		if (c->c->fd != fd)
			continue;
		fm_close(c->c, 1);
		free(c->owner);
		free(c);
		memmove(tt_chans.p + i, tt_chans.p + i + 1, (tt_chans.n - i - 1) * sizeof *tt_chans.p);
		tt_chans.n--;
		break;
	}
}

/* Every channel served let go, for the module's end. */
void tt_unserveall(void)
{
	while (tt_chans.n)
		tt_unserve(((tt_ch *)tt_chans.p[tt_chans.n - 1])->c->fd);
	v_free(&tt_chans);
}

/* Every program this process owns for an app that has ended since asked:
   waited for, and its app told. 1 while it owns any. */
int tt_reap(str *log)
{
	size_t i;
	tt_p *p;
	str a;
	int any = 0;

	s_init(&a);
	for (i = 0; i < tt_list.n; i++) {
		p = (tt_p *)tt_list.p[i];
		if (p->far != 1)
			continue;
		any = 1;
		if (p->done || tt_alive(p))
			continue;
		s_fmt(log, "ended %d %ld %d\n", p->id, p->pid, p->st);
		if (p->chan < 0)
			continue;
		a.n = 0;
		tt_field(&a, "x");
		tt_fieldn(&a, p->id);
		tt_fieldn(&a, p->st);
		tt_say(p->chan, &a, -1);
	}
	s_free(&a);
	return any;
}

/* Close every program no app's process has held for ms. */
void tt_orphans(long ms, str *log)
{
	size_t i = 0;
	tt_p *p;
	long now = tt_ms();

	while (i < tt_list.n) {
		p = (tt_p *)tt_list.p[i];
		if (p->far == 1 && p->chan < 0 && p->lone && now - p->lone >= ms) {
			s_fmt(log, "%d %ld %s\n", p->id, p->pid, p->owner ? p->owner : "?");
			tt_drop(p);
			continue;
		}
		i++;
	}
}

/* What a restart carries: every program owned for an app and still running,
   "fd pid owner" a line. */
void tt_owned(str *out)
{
	size_t i;
	tt_p *p;

	for (i = 0; i < tt_list.n; i++) {
		p = (tt_p *)tt_list.p[i];
		if (p->far == 1 && (p->done || !tt_alive(p)))
			continue;
		if (p->far == 1)
			s_fmt(out, "%d %ld %s\n", p->fd, p->pid, p->owner ? p->owner : "");
	}
}

/* Take back across an exec a program owned for an app: lone until that
   app's process asks for it. */
tt_p *tt_adoptown(int fd, long pid, int rows, int cols, const char *owner)
{
	tt_p *p;

	if (fd < 0 || pid <= 0 || !isatty(fd)) {
		lg(HIBR_LERR, "pty adopt: %d is not an open terminal", fd);
		return 0;
	}
	fcntl(fd, F_SETFL, O_NONBLOCK);
	p = xm(sizeof *p);
	memset(p, 0, sizeof *p);
	p->id = tt_next++;
	p->fd = fd;
	p->pid = pid;
	p->rows = rows;
	p->cols = cols;
	p->far = 1;
	p->chan = -1;
	p->owner = xs(owner);
	p->lone = tt_ms();
	v_add(&tt_list, p);
	lg(HIBR_LINF, "pty %d adopted for %s: pid %ld on fd %d", p->id, owner, pid, fd);
	return p;
}
