#define _GNU_SOURCE

/* zygote: what the desktop's one prepared host for app processes needs and a
   script cannot do (ADR 0050, the zygote). The host has read the app runtime
   once and forks a fresh process for each app the desktop asks for, so every
   app process shares its pages and none parses the runtime again; the
   request arrives with a descriptor riding on it -- the new process's own
   channel to the desktop for its terminals' programs -- which no shell
   builtin can receive.

     zygote pair                    a connected pair of channels, "A B"
     zygote send FD TEXT [XFD...]   one message, with descriptors
     zygote recv FD [-t MS] [VAR]   the next message into RET, its
                                    descriptors into VAR; status 3 when none
                                    came in MS, 1 when the peer has gone
     zygote close FD                forget a channel and close it
     zygote fork [-k FD]...         a fresh process: 0 in it, its pid here
     zygote reaped                  "PID STATUS" for each forked one ended
     zygote end PID SIGNAL          signal one this process forked and has
                                    not yet seen end
     zygote kids                    those forked and not yet seen end

   The forked process is this process's child, so this process waits for it
   and says how it ended -- whatever forked a child is the thing entitled to
   its status (CLAUDE.md) -- and it is given what a process of its own needs
   that a fork does not give: its own $$, a session of its own, its own
   random numbers, and no descriptor of the host's but the standard three
   and those kept. */

#include "hibr.h"
#include "fdmsg.h"
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

/* Each channel read here, by descriptor; and every process forked and not
   yet seen end. */
vec zg_chs;
vec zg_kids;

/* The reader for a channel, made on first use. */
fm_ch *zg_ch(int fd)
{
	size_t i;
	fm_ch *c;

	for (i = 0; i < zg_chs.n; i++) {
		c = (fm_ch *)zg_chs.p[i];
		if (c->fd == fd)
			return c;
	}
	c = fm_open(fd);
	v_add(&zg_chs, c);
	return c;
}

/* Forget a channel's reader, closing the channel when asked. */
void zg_drop(int fd, int closefd)
{
	size_t i;
	fm_ch *c;

	for (i = 0; i < zg_chs.n; i++) {
		c = (fm_ch *)zg_chs.p[i];
		if (c->fd != fd)
			continue;
		fm_close(c, closefd);
		memmove(zg_chs.p + i, zg_chs.p + i + 1, (zg_chs.n - i - 1) * sizeof *zg_chs.p);
		zg_chs.n--;
		return;
	}
	if (closefd)
		close(fd);
}

/* Hand a value back, printing it only when nobody asked for it. */
void zg_ret(sh *s, const char *t)
{
	hibr_ret(s, t);
	if (!s->bind)
		printf("%s\n", t);
}

/* Whether a pid is one forked here and not yet seen end. */
int zg_kid(long pid)
{
	size_t i;

	for (i = 0; i < zg_kids.n; i++)
		if ((long)zg_kids.p[i] == pid)
			return 1;
	return 0;
}

/* The child's side of a fork: what makes it a process of its own. */
void zg_child(sh *s, const int *keep, int nk)
{
	struct timespec t;
	str r;
	size_t i;

	s->pid = (long)getpid();
	if (setsid() < 0)
		lg(HIBR_LWRN, "zygote: no session of its own: %s", strerror(errno));
	clock_gettime(CLOCK_REALTIME, &t);
	s_init(&r);
	s_num(&r, (long)((unsigned long)getpid() * 2654435761u ^ (unsigned long)t.tv_nsec) & 0x7fffffff);
	hibr_set(s, "RANDOM", r.p, 0);
	s_free(&r);
	for (i = 0; i < zg_chs.n; i++)
		fm_close((fm_ch *)zg_chs.p[i], 0);
	zg_chs.n = 0;
	zg_kids.n = 0;
	fm_closeall(keep, nk);
}

/* The zygote's verbs. */
int m_zygote(sh *s, int ac, char **av)
{
	const char *sub = ac > 1 ? av[1] : "";
	str o, m;
	vec keep = { 0, 0, 0 };
	int a, b, i, nf, ms = -1, fds[FM_MAXFD], r, w;
	long pid, end;
	fm_ch *c;
	const char *var = 0;

	if (!strcmp(sub, "pair")) {
		if (!fm_pair(&a, &b)) {
			lg(HIBR_LERR, "zygote pair: %s", strerror(errno));
			return HIBR_FAIL;
		}
		s_init(&o);
		s_num(&o, a);
		s_ch(&o, ' ');
		s_num(&o, b);
		zg_ret(s, o.p);
		s_free(&o);
		return HIBR_OK;
	}
	if (!strcmp(sub, "send")) {
		if (ac < 4) {
			lg(HIBR_LERR, "usage: zygote send fd text [fd...]");
			return 2;
		}
		nf = 0;
		for (i = 4; i < ac && nf < FM_MAXFD; i++)
			fds[nf++] = atoi(av[i]);
		return fm_send(atoi(av[2]), av[3], strlen(av[3]), fds, nf, 1000) ? HIBR_OK
										   : HIBR_FAIL;
	}
	if (!strcmp(sub, "recv")) {
		if (ac < 3) {
			lg(HIBR_LERR, "usage: zygote recv fd [-t ms] [var]");
			return 2;
		}
		for (i = 3; i < ac; i++) {
			if (!strcmp(av[i], "-t") && i + 1 < ac)
				ms = atoi(av[++i]);
			else
				var = av[i];
		}
		c = zg_ch(atoi(av[2]));
		s_init(&m);
		s_init(&o);
		r = 3;
		end = ms > 0 ? fm_ms() + ms : 0;
		for (;;) {
			fm_take(c);
			if (fm_next(c, &m, fds, &nf)) {
				for (i = 0; i < nf; i++) {
					if (!var) {
						close(fds[i]);
						continue;
					}
					if (o.n)
						s_ch(&o, ' ');
					s_num(&o, fds[i]);
				}
				r = HIBR_OK;
				break;
			}
			if (c->gone) {
				r = HIBR_FAIL;
				break;
			}
			if (!ms)
				break;
			if (ms > 0) {
				if (fm_ms() >= end)
					break;
				fm_wait(c, (int)(end - fm_ms()));
			} else {
				fm_wait(c, 3600000);
			}
		}
		if (var)
			hibr_set(s, var, o.p ? o.p : "", 0);
		hibr_ret(s, r == HIBR_OK && m.p ? m.p : "");
		if (r == HIBR_OK && !s->bind)
			printf("%s\n", m.p ? m.p : "");
		s_free(&m);
		s_free(&o);
		return r;
	}
	if (!strcmp(sub, "close")) {
		if (ac < 3)
			return 2;
		zg_drop(atoi(av[2]), 1);
		return HIBR_OK;
	}
	if (!strcmp(sub, "fork")) {
		for (i = 2; i < ac; i++) {
			if (!strcmp(av[i], "-k") && i + 1 < ac)
				v_add(&keep, (void *)(long)atoi(av[++i]));
			else {
				lg(HIBR_LERR, "usage: zygote fork [-k fd]...");
				v_free(&keep);
				return 2;
			}
		}
		fflush(0);
		pid = (long)fork();
		if (pid < 0) {
			lg(HIBR_LERR, "zygote fork: %s", strerror(errno));
			v_free(&keep);
			return HIBR_FAIL;
		}
		if (!pid) {
			int *k = xm(sizeof(int) * (keep.n + 1));

			for (i = 0; i < (int)keep.n; i++)
				k[i] = (int)(long)keep.p[i];
			zg_child(s, k, (int)keep.n);
			free(k);
			v_free(&keep);
			hibr_ret(s, "0");
			return HIBR_OK;
		}
		v_free(&keep);
		v_add(&zg_kids, (void *)pid);
		s_init(&o);
		s_num(&o, pid);
		zg_ret(s, o.p);
		s_free(&o);
		return HIBR_OK;
	}
	if (!strcmp(sub, "reaped")) {
		s_init(&o);
		i = 0;
		while ((size_t)i < zg_kids.n) {
			pid = (long)zg_kids.p[i];
			r = (int)waitpid((pid_t)pid, &w, WNOHANG);
			if (r < 0 && errno == EINTR)
				continue;
			if (r == 0) {
				i++;
				continue;
			}
			if (r < 0)
				w = 0;
			s_fmt(&o, "%s%ld %d", o.n ? "\n" : "", pid,
			      r < 0 ? -1 : WIFEXITED(w) ? WEXITSTATUS(w) :
			      WIFSIGNALED(w) ? 128 + WTERMSIG(w) : 0);
			memmove(zg_kids.p + i, zg_kids.p + i + 1, (zg_kids.n - i - 1) * sizeof *zg_kids.p);
			zg_kids.n--;
		}
		hibr_ret(s, o.p ? o.p : "");
		if (!s->bind && o.n)
			printf("%s\n", o.p);
		s_free(&o);
		return HIBR_OK;
	}
	if (!strcmp(sub, "kids")) {
		s_init(&o);
		for (i = 0; i < (int)zg_kids.n; i++)
			s_fmt(&o, "%s%ld", i ? " " : "", (long)zg_kids.p[i]);
		hibr_ret(s, o.p ? o.p : "");
		if (!s->bind && o.n)
			printf("%s\n", o.p);
		s_free(&o);
		return HIBR_OK;
	}
	if (!strcmp(sub, "end")) {
		if (ac < 4)
			return 2;
		pid = atol(av[2]);
		if (!zg_kid(pid)) {
			lg(HIBR_LDBG, "zygote end: %ld is no process forked here", pid);
			return HIBR_FAIL;
		}
		return kill((pid_t)pid, atoi(av[3])) == 0 ? HIBR_OK : HIBR_FAIL;
	}
	lg(HIBR_LERR, "usage: zygote pair|send|recv|close|fork|reaped|kids|end ...");
	return 2;
}

/* Every channel read here let go, for the module's end. */
void zg_fini(sh *s)
{
	(void)s;
	while (zg_chs.n)
		zg_drop(((fm_ch *)zg_chs.p[zg_chs.n - 1])->fd, 0);
	v_free(&zg_chs);
	v_free(&zg_kids);
}

const hibr_bi zygote_bi[] = {
	{ "zygote", m_zygote, "fork fresh app processes from a prepared host" },
	HIBR_BI_END
};

HIBR_MODULE("zygote", "0.1", "a prepared host's fork, and messages with descriptors",
	    zygote_bi, 0, zg_fini);
