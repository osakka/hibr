#include "pri.h"
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

struct termios jc_modes;
int jc_have;

int wstat(int w);

const struct signm jc_sigs[] = {
	{ "HUP", SIGHUP },   { "INT", SIGINT },   { "QUIT", SIGQUIT },
	{ "KILL", SIGKILL }, { "TERM", SIGTERM }, { "STOP", SIGSTOP },
	{ "CONT", SIGCONT }, { "TSTP", SIGTSTP }, { "USR1", SIGUSR1 },
	{ "USR2", SIGUSR2 }, { "ALRM", SIGALRM }, { "PIPE", SIGPIPE },
	{ 0, 0 }
};

/* Claim the terminal and take control of our own process group. */
void jc_init(sh *s)
{
	s->tty = dup(0);
	if (s->tty < 0) {
		lg(HIBR_LWRN, "job control off: cannot duplicate the terminal");
		s->tty = 0;
		return;
	}
	fcntl(s->tty, F_SETFD, FD_CLOEXEC);
	while (tcgetpgrp(s->tty) != (s->pgid = getpgrp()))
		kill(-s->pgid, SIGTTIN);
	signal(SIGTSTP, SIG_IGN);
	signal(SIGTTIN, SIG_IGN);
	signal(SIGTTOU, SIG_IGN);
	signal(SIGQUIT, SIG_IGN);
	s->pgid = getpid();
	if (getpgrp() != getpid() && setpgid((pid_t)s->pgid, (pid_t)s->pgid) < 0)
		lg(HIBR_LWRN, "cannot make own process group: %s", strerror(errno));
	tcsetpgrp(s->tty, (pid_t)s->pgid);
	if (tcgetattr(s->tty, &jc_modes) == 0)
		jc_have = 1;
	lg(HIBR_LINF, "job control active, shell group %ld", s->pgid);
}

/* Add a job to the table. */
job *jc_new(sh *s, const char *tx, int bg)
{
	job *j = xm(sizeof *j);

	memset(j, 0, sizeof *j);
	if (!s->jobs.n)
		s->jid = 0;
	j->id = ++s->jid;
	j->bg = bg;
	j->state = J_RUN;
	j->tx = xs(tx && *tx ? tx : "(command)");
	v_add(&s->jobs, j);
	s->jprv = s->jcur;
	s->jcur = j->id;
	lg(HIBR_LDBG, "job [%d] created: %s", j->id, j->tx);
	return j;
}

/* Record a process belonging to a job. */
void jc_pid(sh *s, job *j, long p)
{
	(void)s;
	j->np++;
	j->last = p;
	if (!j->pgid)
		j->pgid = p;
}

/* Drop a job from the table. */
void jc_drop(sh *s, job *j)
{
	size_t i;

	for (i = 0; i < s->jobs.n; i++)
		if (s->jobs.p[i] == j) {
			s->jobs.p[i] = s->jobs.p[s->jobs.n - 1];
			s->jobs.n--;
			break;
		}
	if (s->jcur == j->id)
		s->jcur = s->jprv;
	free(j->tx);
	free(j);
}

/* Find the job that owns a process. */
/* Wait for the next process in any of our own jobs to finish.

   This is what `wait` and `wait -n` used to get from waitpid(-1), which
   also collected children the shell never started. Polling the groups in
   turn is a little cruder and correct. */
long jc_anyone(sh *s, int *w)
{
	size_t k;
	job *j;
	pid_t p;
	int any;

	for (;;) {
		any = 0;
		for (k = 0; k < s->jobs.n; k++) {
			j = (job *)s->jobs.p[k];
			if (j->state == J_DONE)
				continue;
			any = 1;
			p = waitpid(-(pid_t)j->pgid, w, WNOHANG | WUNTRACED);
			if (p > 0)
				return (long)p;
		}
		if (!any)
			return -1;
		usleep(2000);
	}
}

job *jc_bypid(sh *s, long p)
{
	size_t i;
	job *j;

	for (i = 0; i < s->jobs.n; i++) {
		j = (job *)s->jobs.p[i];
		if (j->pgid == p || j->last == p)
			return j;
	}
	for (i = 0; i < s->jobs.n; i++) {
		j = (job *)s->jobs.p[i];
		if (getpgid((pid_t)p) == (pid_t)j->pgid)
			return j;
	}
	return 0;
}

/* Mark the current and previous job indicator. */
char jc_mark(sh *s, job *j)
{
	if (j->id == s->jcur)
		return '+';
	if (j->id == s->jprv)
		return '-';
	return ' ';
}

/* Describe a job on one line. */
void jc_show(sh *s, job *j)
{
	const char *w = "Running";
	str b;

	s_init(&b);
	if (j->state == J_STOP)
		w = "Stopped";
	else if (j->state == J_DONE && j->st > 128) {
		const struct signm *sm;
		for (sm = jc_sigs; sm->nm; sm++)
			if (sm->sig == j->st - 128)
				break;
		if (sm->nm) {
			s_cat(&b, "SIG");
			s_cat(&b, sm->nm);
		} else {
			s_cat(&b, "Signal ");
			s_num(&b, j->st - 128);
		}
		w = b.p;
	} else if (j->state == J_DONE && j->st) {
		s_cat(&b, "Exit ");
		s_num(&b, j->st);
		w = b.p;
	} else if (j->state == J_DONE)
		w = "Done";
	printf("[%d]%c  %-22s %s\n", j->id, jc_mark(s, j), w, j->tx);
	fflush(stdout);
	s_free(&b);
}

/* Hand the terminal to a process group. */
void jc_grab(sh *s, long pgid)
{
	if (!s->it || !s->tty)
		return;
	tcsetpgrp(s->tty, (pid_t)pgid);
}

/* Take the terminal back and restore the shell's line settings. */
void jc_reclaim(sh *s)
{
	if (!s->it || !s->tty)
		return;
	tcsetpgrp(s->tty, (pid_t)s->pgid);
	if (jc_have)
		tcsetattr(s->tty, TCSADRAIN, &jc_modes);
}

/* Wait for a job in the foreground, returning its exit status. */
int jc_fg(sh *s, job *j)
{
	int w, st = 0;
	pid_t p;

	jc_grab(s, j->pgid);
	while (j->ndone < j->np && j->state == J_RUN) {
		p = waitpid(-(pid_t)j->pgid, &w, WUNTRACED);
		if (p < 0) {
			if (errno == EINTR)
				continue;
			lg(HIBR_LTRC, "wait: %s", strerror(errno));
			break;
		}
		if (WIFSTOPPED(w)) {
			j->state = J_STOP;
			st = 128 + WSTOPSIG(w);
			break;
		}
		j->ndone++;
		if (wstat(w) && !j->fail)
			j->fail = wstat(w);
		if ((long)p == j->last)
			st = wstat(w);
	}
	jc_reclaim(s);
	s->pfs = j->fail;
	if (j->state == J_STOP) {
		j->bg = 1;
		j->st = st;
		fputc('\n', stderr);
		jc_show(s, j);
		j->note = 1;
		return st;
	}
	jc_drop(s, j);
	return st;
}

/* Announce a job that has been put in the background. */
void jc_bgnote(sh *s, job *j)
{
	fprintf(stderr, "[%d] %ld\n", j->id, j->last);
	(void)s;
}

/* Reap finished background jobs and report state changes.

   Each job is waited on by its own process group, never with -1. A shell
   that reaps anything reaps a module's children too -- and then throws the
   status away, because jc_bypid does not know them -- which is how the pty
   module's exit codes all came back as zero. Whatever forked a child is the
   thing entitled to its status. */
void jc_poll(sh *s, int report)
{
	int w;
	pid_t p;
	size_t i, k;
	job *j;

	for (k = 0; k < s->jobs.n; k++) {
		j = (job *)s->jobs.p[k];
		while ((p = waitpid(-(pid_t)j->pgid, &w,
				    WNOHANG | WUNTRACED)) > 0) {
		if (WIFSTOPPED(w)) {
			if (j->state != J_STOP)
				j->note = 0;
			j->state = J_STOP;
			continue;
		}
		j->ndone++;
		if (wstat(w) && !j->fail)
			j->fail = wstat(w);
		if ((long)p == j->last)
			j->st = wstat(w);
		if (j->ndone >= j->np) {
			j->state = J_DONE;
			j->note = 0;
		}
		}
	}
	if (!report)
		return;
	for (i = 0; i < s->jobs.n;) {
		j = (job *)s->jobs.p[i];
		if (j->state == J_RUN || j->note) {
			i++;
			continue;
		}
		jc_show(s, j);
		j->note = 1;
		if (j->state == J_DONE) {
			jc_drop(s, j);
			continue;
		}
		i++;
	}
}

/* Resolve a job specification such as %1, %+ or %name. */
job *jc_find(sh *s, const char *spec)
{
	size_t i;
	job *j;
	int id;

	if (!s->jobs.n)
		return 0;
	if (!spec || !*spec)
		spec = "%+";
	if (*spec == '%')
		spec++;
	if (!*spec || *spec == '+' || *spec == '%')
		id = s->jcur;
	else if (*spec == '-')
		id = s->jprv;
	else if (*spec >= '0' && *spec <= '9')
		id = atoi(spec);
	else {
		for (i = 0; i < s->jobs.n; i++) {
			j = (job *)s->jobs.p[i];
			if (!strncmp(j->tx, spec, strlen(spec)))
				return j;
		}
		return 0;
	}
	for (i = 0; i < s->jobs.n; i++) {
		j = (job *)s->jobs.p[i];
		if (j->id == id)
			return j;
	}
	return 0;
}

/* List the job table. */
int b_jobs(sh *s, int ac, char **av)
{
	size_t i;

	job *j;

	(void)ac;
	(void)av;
	jc_poll(s, 0);
	for (i = 0; i < s->jobs.n;) {
		j = (job *)s->jobs.p[i];
		jc_show(s, j);
		j->note = 1;
		if (j->state == J_DONE) {
			jc_drop(s, j);
			continue;
		}
		i++;
	}
	return HIBR_OK;
}

/* Resume a job in the foreground. */
int b_fg(sh *s, int ac, char **av)
{
	job *j = jc_find(s, ac > 1 ? av[1] : 0);

	if (!j) {
		lg(HIBR_LERR, "fg: no such job");
		return HIBR_FAIL;
	}
	printf("%s\n", j->tx);
	fflush(stdout);
	j->state = J_RUN;
	j->bg = 0;
	j->note = 0;
	s->jprv = s->jcur;
	s->jcur = j->id;
	if (kill(-(pid_t)j->pgid, SIGCONT) < 0)
		lg(HIBR_LWRN, "fg: continue: %s", strerror(errno));
	return s->st = jc_fg(s, j);
}

/* Resume a stopped job in the background. */
int b_bg(sh *s, int ac, char **av)
{
	job *j = jc_find(s, ac > 1 ? av[1] : 0);

	if (!j) {
		lg(HIBR_LERR, "bg: no such job");
		return HIBR_FAIL;
	}
	j->state = J_RUN;
	j->bg = 1;
	j->note = 1;
	if (kill(-(pid_t)j->pgid, SIGCONT) < 0) {
		lg(HIBR_LERR, "bg: continue: %s", strerror(errno));
		return HIBR_FAIL;
	}
	printf("[%d]%c %s &\n", j->id, jc_mark(s, j), j->tx);
	fflush(stdout);
	return HIBR_OK;
}

/* Wait for jobs to finish. */
int b_wait(sh *s, int ac, char **av)
{
	job *j;
	int w, st = 0;
	pid_t p;

	if (ac > 1 && (!strcmp(av[1], "-n") || !strcmp(av[1], "-p"))) {
		const char *nm = 0;
		int k = 1;
		for (; k < ac && av[k][0] == '-' && av[k][1]; k++)
			if (!strcmp(av[k], "-p") && k + 1 < ac)
				nm = av[++k];
		p = jc_anyone(s, &w);
		if (p <= 0) {
			lg(HIBR_LDBG, "wait -n: nothing left to wait for");
			return s->st = 127;
		}
		st = wstat(w);
		j = jc_bypid(s, (long)p);
		if (j) {
			j->ndone++;
			if (j->ndone >= j->np) {
				j->state = J_DONE;
				jc_drop(s, j);
			}
		}
		if (nm)
			hibr_set(s, nm, xnum(s, (long)p), 0);
		lg(HIBR_LDBG, "wait -n reaped %ld", (long)p);
		return s->st = st;
	}
	if (ac > 1) {
		j = av[1][0] == '%' ? jc_find(s, av[1]) : jc_bypid(s, atol(av[1]));
		if (!j && av[1][0] != '%' && atol(av[1]) > 0) {
			p = waitpid((pid_t)atol(av[1]), &w, 0);
			if (p < 0) {
				lg(HIBR_LERR, "wait: %s: not a child", av[1]);
				return HIBR_FAIL;
			}
			return s->st = wstat(w);
		}
		if (!j) {
			lg(HIBR_LERR, "wait: %s: no such job", av[1]);
			return HIBR_FAIL;
		}
		while (j->ndone < j->np && j->state == J_RUN) {
			p = waitpid(-(pid_t)j->pgid, &w, 0);
			if (p < 0) {
				if (errno == EINTR)
					continue;
				break;
			}
			j->ndone++;
			if ((long)p == j->last)
				st = wstat(w);
		}
		if (j->state == J_RUN)
			jc_drop(s, j);
		return s->st = st;
	}
	while ((p = jc_anyone(s, &w)) > 0) {
		j = jc_bypid(s, (long)p);
		st = wstat(w);
		if (!j)
			continue;
		j->ndone++;
		if (j->ndone >= j->np) {
			j->state = J_DONE;
			jc_drop(s, j);
		}
	}
	return s->st = st;
}

/* Send a signal to a job or process. */
int b_kill(sh *s, int ac, char **av)
{
	int sig = SIGTERM, i = 1, rc = HIBR_OK;
	const struct signm *sm;
	job *j;
	const char *nm;

	if (ac > 1 && av[1][0] == '-' && av[1][1]) {
		nm = av[1] + 1;
		if (nm[0] >= '0' && nm[0] <= '9') {
			sig = atoi(nm);
		} else {
			if (!strncmp(nm, "SIG", 3))
				nm += 3;
			for (sm = jc_sigs; sm->nm; sm++)
				if (!strcmp(sm->nm, nm))
					break;
			if (!sm->nm) {
				lg(HIBR_LERR, "kill: %s: unknown signal", av[1]);
				return HIBR_FAIL;
			}
			sig = sm->sig;
		}
		i++;
	}
	if (i >= ac) {
		lg(HIBR_LERR, "usage: kill [-signal] %%job | pid");
		return HIBR_FAIL;
	}
	for (; i < ac; i++) {
		if (av[i][0] == '%') {
			j = jc_find(s, av[i]);
			if (!j) {
				lg(HIBR_LERR, "kill: %s: no such job", av[i]);
				rc = HIBR_FAIL;
				continue;
			}
			if (kill(-(pid_t)j->pgid, sig) < 0) {
				lg(HIBR_LERR, "kill: %s", strerror(errno));
				rc = HIBR_FAIL;
				continue;
			}
			if (j->state == J_STOP && sig != SIGSTOP &&
			    sig != SIGTSTP) {
				lg(HIBR_LDBG, "waking stopped job [%d]", j->id);
				kill(-(pid_t)j->pgid, SIGCONT);
			}
			if (sig == SIGCONT)
				j->state = J_RUN;
			continue;
		}
		if (kill((pid_t)atol(av[i]), sig) < 0) {
			lg(HIBR_LERR, "kill: %s: %s", av[i], strerror(errno));
			rc = HIBR_FAIL;
		}
	}
	return rc;
}

/* Release the job table at shutdown. */
void jc_fini(sh *s)
{
	job *j;

	while (s->jobs.n) {
		j = (job *)s->jobs.p[--s->jobs.n];
		if (j->state == J_STOP) {
			lg(HIBR_LWRN, "leaving stopped job [%d] %s", j->id, j->tx);
			kill(-(pid_t)j->pgid, SIGCONT);
			kill(-(pid_t)j->pgid, SIGHUP);
		}
		free(j->tx);
		free(j);
	}
	v_free(&s->jobs);
	if (s->tty > 0)
		close(s->tty);
}

/* Drop a job from the table without signalling it. */
int b_disown(sh *s, int ac, char **av)
{
	job *j = jc_find(s, ac > 1 ? av[1] : 0);

	if (!j) {
		lg(HIBR_LERR, "disown: no such job");
		return HIBR_FAIL;
	}
	lg(HIBR_LINF, "disowned job [%d] %s", j->id, j->tx);
	jc_drop(s, j);
	return HIBR_OK;
}
