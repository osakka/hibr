#define _GNU_SOURCE

#include "tt.h"
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

vec tt_list;
int tt_next = 1;

/* Find a pty by the id the script was given, or null. */
tt_p *tt_find(int id)
{
	size_t i;
	tt_p *p;

	for (i = 0; i < tt_list.n; i++) {
		p = (tt_p *)tt_list.p[i];
		if (p->id == id)
			return p;
	}
	return 0;
}

/* Tell the kernel how big the terminal is, so the child's SIGWINCH and its
   own ioctl agree with what the caller is going to draw. */
int tt_resize(tt_p *p, int rows, int cols)
{
	struct winsize w;

	memset(&w, 0, sizeof w);
	w.ws_row = (unsigned short)rows;
	w.ws_col = (unsigned short)cols;
	if (ioctl(p->fd, TIOCSWINSZ, &w) < 0)
		return 0;
	p->rows = rows;
	p->cols = cols;
	lg(HIBR_LDBG, "pty %d resized to %dx%d", p->id, rows, cols);
	return 1;
}

/* Open a pseudo terminal and start a program on the far side of it.

   The child gets a session of its own and the slave as its controlling
   terminal, which is the whole point: without TIOCSCTTY a shell started
   here has no terminal, does not run its line editor, and reports itself as
   non-interactive -- the same thing that makes every full-screen program
   behave differently in a pipe. */
tt_p *tt_spawn(sh *s, int rows, int cols, char **av)
{
	tt_p *p;
	int m, sl;
	char *nm;
	long pid;

	m = posix_openpt(O_RDWR | O_NOCTTY);
	if (m < 0) {
		lg(HIBR_LERR, "pty: %s", strerror(errno));
		return 0;
	}
	if (grantpt(m) < 0 || unlockpt(m) < 0) {
		lg(HIBR_LERR, "pty: %s", strerror(errno));
		close(m);
		return 0;
	}
	nm = ptsname(m);
	if (!nm) {
		lg(HIBR_LERR, "pty: no slave name");
		close(m);
		return 0;
	}
	nm = xs(nm);
	fflush(0);
	pid = (long)fork();
	if (pid < 0) {
		lg(HIBR_LERR, "pty: fork: %s", strerror(errno));
		free(nm);
		close(m);
		return 0;
	}
	if (!pid) {
		close(m);
		if (setsid() < 0)
			_exit(127);
		sl = open(nm, O_RDWR);
		if (sl < 0)
			_exit(127);
		if (ioctl(sl, TIOCSCTTY, 0) < 0)
			_exit(127);
		dup2(sl, 0);
		dup2(sl, 1);
		dup2(sl, 2);
		if (sl > 2)
			close(sl);
		signal(SIGHUP, SIG_DFL);
		signal(SIGINT, SIG_DFL);
		signal(SIGQUIT, SIG_DFL);
		signal(SIGTSTP, SIG_DFL);
		signal(SIGTTIN, SIG_DFL);
		signal(SIGTTOU, SIG_DFL);
		signal(SIGPIPE, SIG_DFL);
		execvp(av[0], av);
		_exit(127);
	}
	free(nm);
	fcntl(m, F_SETFL, O_NONBLOCK);
	p = xm(sizeof *p);
	memset(p, 0, sizeof *p);
	p->id = tt_next++;
	p->fd = m;
	p->pid = pid;
	p->rows = rows;
	p->cols = cols;
	v_add(&tt_list, p);
	tt_resize(p, rows, cols);
	lg(HIBR_LINF, "pty %d is pid %ld running %s", p->id, pid, av[0]);
	return p;
}

/* Send bytes to the program, as if they had been typed. */
long tt_write(tt_p *p, const char *t, size_t n)
{
	size_t off = 0;
	long w;

	while (off < n) {
		w = (long)write(p->fd, t + off, n - off);
		if (w > 0) {
			off += (size_t)w;
			continue;
		}
		if (w < 0 && (errno == EAGAIN || errno == EINTR))
			continue;
		break;
	}
	return (long)off;
}

/* Collect whatever the program has written, waiting up to ms for the first
   of it.  Returns 1 with something, 0 with nothing yet, -1 at end of file.

   A master whose child has gone reads EIO rather than zero, which is the
   one place a pty differs from a pipe and the one that is always got
   wrong. */
int tt_read(tt_p *p, int ms, str *out)
{
	struct timeval tv, *tp = 0;
	fd_set r;
	char *b;
	long n;
	int got = 0;

	if (p->eof)
		return -1;
	if (ms >= 0) {
		tv.tv_sec = ms / 1000;
		tv.tv_usec = (ms % 1000) * 1000;
		tp = &tv;
	}
	for (;;) {
		FD_ZERO(&r);
		FD_SET(p->fd, &r);
		if (select(p->fd + 1, &r, 0, 0, tp) <= 0)
			break;
		s_grow(out, HIBR_IOCH);
		b = out->p + out->n;
		n = (long)read(p->fd, b, HIBR_IOCH);
		if (n > 0) {
			out->n += (size_t)n;
			out->p[out->n] = 0;
			got = 1;
			tv.tv_sec = 0;
			tv.tv_usec = 0;
			tp = &tv;
			continue;
		}
		if (n < 0 && (errno == EAGAIN || errno == EINTR)) {
			if (got)
				break;
			continue;
		}
		p->eof = 1;
		lg(HIBR_LDBG, "pty %d reached end of file", p->id);
		break;
	}
	if (!got && p->eof)
		return -1;
	return got;
}

/* Is the program still running?  Reaps it if it is not. */
int tt_alive(tt_p *p)
{
	int w;
	long r;

	if (p->done)
		return 0;
	r = (long)waitpid((pid_t)p->pid, &w, WNOHANG);
	if (r == p->pid) {
		p->done = 1;
		p->st = WIFEXITED(w) ? WEXITSTATUS(w) :
		        WIFSIGNALED(w) ? 128 + WTERMSIG(w) : 0;
		lg(HIBR_LDBG, "pty %d exited with %d", p->id, p->st);
		return 0;
	}
	if (r < 0) {
		p->done = 1;
		p->st = 0;
		return 0;
	}
	return 1;
}

/* Wait for the program to finish, up to ms; -1 waits as long as it takes. */
int tt_wait(tt_p *p, int ms)
{
	int left = ms;

	while (!tt_alive(p)) {
		if (p->done)
			return 1;
		break;
	}
	while (tt_alive(p)) {
		if (ms >= 0 && left <= 0)
			return 0;
		usleep(10000);
		if (ms >= 0)
			left -= 10;
	}
	return 1;
}

/* Stop the program if it is still going, and release the pty. */
void tt_drop(tt_p *p)
{
	size_t i;

	if (!p->done) {
		kill((pid_t)p->pid, SIGKILL);
		waitpid((pid_t)p->pid, 0, 0);
		p->done = 1;
	}
	if (p->fd >= 0)
		close(p->fd);
	for (i = 0; i < tt_list.n; i++)
		if (tt_list.p[i] == (void *)p) {
			memmove(tt_list.p + i, tt_list.p + i + 1,
				(tt_list.n - i - 1) * sizeof *tt_list.p);
			tt_list.n--;
			break;
		}
	lg(HIBR_LDBG, "pty %d released", p->id);
	free(p);
}

/* Release every pty, for the module finaliser. */
void tt_all(void)
{
	while (tt_list.n)
		tt_drop((tt_p *)tt_list.p[tt_list.n - 1]);
	v_free(&tt_list);
}
