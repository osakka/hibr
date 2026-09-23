#define _GNU_SOURCE

#include "hd.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

const py_api *hd_pty;

/* Hand a value back through the result slot, printing it only when nobody
   asked for it. */
void hd_ret(sh *s, const char *t)
{
	hibr_ret(s, t);
	if (!s->bind && *t)
		printf("%s\n", t);
}

/* Close every descriptor the shell had open except the one kept, so the
   server holds nothing of the terminal it was started from. */
void hd_shed(int keep)
{
	DIR *d = opendir("/proc/self/fd");
	struct dirent *e;
	vec fds;
	size_t i;
	long fd;

	if (!d)
		return;
	memset(&fds, 0, sizeof fds);
	while ((e = readdir(d))) {
		fd = strtol(e->d_name, 0, 10);
		if (e->d_name[0] >= '0' && e->d_name[0] <= '9' && fd > 2 &&
		    fd != keep && fd != dirfd(d))
			v_add(&fds, (void *)fd);
	}
	closedir(d);
	for (i = 0; i < fds.n; i++)
		close((int)(long)fds.p[i]);
	v_free(&fds);
}

/* Start a program in a session of its own, and attach to it unless -d. */
int hd_new(sh *s, int ac, char **av)
{
	int i = 2, det = 0, pp[2], sz[2] = { 24, 80 }, fd;
	struct winsize w;
	const char *name;
	pid_t pid;
	char ok = '0';
	str path;

	if (i < ac && !strcmp(av[i], "-d")) {
		det = 1;
		i++;
	}
	if (i + 1 >= ac) {
		lg(HIBR_LERR, "usage: hold new [-d] name command [args...]");
		return 2;
	}
	name = av[i++];
	s_init(&path);
	if (!hd_path(name, &path)) {
		s_free(&path);
		return 2;
	}
	fd = hd_dial(path.p);
	if (fd >= 0) {
		close(fd);
		lg(HIBR_LERR, "hold: %s: already running", name);
		s_free(&path);
		return HIBR_FAIL;
	}
	if (ioctl(0, TIOCGWINSZ, &w) == 0 && w.ws_row && w.ws_col) {
		sz[0] = w.ws_row;
		sz[1] = w.ws_col;
	}
	if (pipe(pp) < 0) {
		lg(HIBR_LERR, "hold: %s", strerror(errno));
		s_free(&path);
		return HIBR_FAIL;
	}
	hd_cloexec(pp[0]);
	hd_cloexec(pp[1]);
	fflush(0);
	pid = fork();
	if (pid < 0) {
		lg(HIBR_LERR, "hold: fork: %s", strerror(errno));
		close(pp[0]);
		close(pp[1]);
		s_free(&path);
		return HIBR_FAIL;
	}
	if (!pid) {
		setsid();
		if (fork())
			_exit(0);
		close(pp[0]);
		fd = open("/dev/null", O_RDWR);
		if (fd >= 0) {
			dup2(fd, 0);
			dup2(fd, 1);
			dup2(fd, 2);
			if (fd > 2)
				close(fd);
		}
		hd_shed(pp[1]);
		hd_serve(s, path.p, sz[0], sz[1], av + i, pp[1]);
		_exit(1);
	}
	close(pp[1]);
	waitpid(pid, 0, 0);
	if (hd_rall(pp[0], &ok, 1) <= 0)
		ok = '0';
	close(pp[0]);
	if (ok != '1') {
		lg(HIBR_LERR, "hold: %s: could not start %s", name, av[i]);
		s_free(&path);
		return HIBR_FAIL;
	}
	lg(HIBR_LDBG, "hold: %s is %s", name, path.p);
	if (det) {
		s_free(&path);
		return HIBR_OK;
	}
	i = hd_attach(name, path.p);
	s_free(&path);
	return i;
}

/* Every live session, one per line with whether it is attached and the
   program's pid.  A socket nobody answers on is left from a session that
   died without cleaning up, and is removed. */
int hd_list(sh *s)
{
	struct dirent *e;
	struct stat st;
	str dir, p, r, o;
	DIR *d;

	s_init(&dir);
	s_init(&o);
	if (!hd_dir(&dir)) {
		s_free(&dir);
		return HIBR_FAIL;
	}
	d = opendir(dir.p);
	while (d && (e = readdir(d))) {
		if (!hd_nameok(e->d_name))
			continue;
		s_init(&p);
		s_init(&r);
		s_cat(&p, dir.p);
		s_ch(&p, '/');
		s_cat(&p, e->d_name);
		if (lstat(p.p, &st) == 0 && S_ISSOCK(st.st_mode)) {
			if (hd_ask(p.p, HD_INFO, &r)) {
				if (o.n)
					s_ch(&o, '\n');
				s_cat(&o, e->d_name);
				s_ch(&o, ' ');
				s_cat(&o, r.p ? r.p : "");
			} else {
				unlink(p.p);
				lg(HIBR_LDBG, "hold: removed stale %s", p.p);
			}
		}
		s_free(&p);
		s_free(&r);
	}
	if (d)
		closedir(d);
	hd_ret(s, o.p ? o.p : "");
	s_free(&o);
	s_free(&dir);
	return HIBR_OK;
}

/* The session a request is for: named, or the one this shell runs in. */
int hd_which(const char *name, str *path)
{
	const char *h = getenv("HIBR_HOLD");

	if (name)
		return hd_path(name, path);
	if (!h || !*h) {
		lg(HIBR_LERR, "hold: not in a session; name one");
		return 0;
	}
	s_cat(path, h);
	return 1;
}

/* Keep a program running on a terminal nobody has to stay attached to. */
int m_hold(sh *s, int ac, char **av)
{
	const char *sub = ac > 1 ? av[1] : "";
	str path;
	int r;

	if (!hd_pty) {
		lg(HIBR_LERR, "hold: no pty module");
		return HIBR_FAIL;
	}
	if (!strcmp(sub, "new"))
		return hd_new(s, ac, av);
	if (!strcmp(sub, "list"))
		return hd_list(s);
	s_init(&path);
	if (!strcmp(sub, "attach")) {
		if (ac < 3 || !hd_path(av[2], &path)) {
			if (ac < 3)
				lg(HIBR_LERR, "usage: hold attach name");
			s_free(&path);
			return 2;
		}
		r = hd_attach(av[2], path.p);
		s_free(&path);
		return r;
	}
	if (!strcmp(sub, "detach") || !strcmp(sub, "kill")) {
		if (!hd_which(ac > 2 ? av[2] : 0, &path)) {
			s_free(&path);
			return 2;
		}
		r = hd_ask(path.p, sub[0] == 'd' ? HD_DETACH : HD_KILL, 0);
		if (!r)
			lg(HIBR_LERR, "hold: %s: no such session",
			   ac > 2 ? av[2] : path.p);
		s_free(&path);
		return r ? HIBR_OK : HIBR_FAIL;
	}
	s_free(&path);
	lg(HIBR_LERR, "usage: hold new|attach|detach|list|kill ...");
	return 2;
}

int hd_ini(sh *s)
{
	hd_pty = (const py_api *)hibr_require(s, "pty", PY_API_VER);
	if (!hd_pty) {
		lg(HIBR_LERR, "hold: needs the pty module");
		return HIBR_FAIL;
	}
	return hibr_provide(s, "hold", 1u, (void *)&m_hold);
}

void hd_fini(sh *s)
{
	hibr_unprovide(s, "hold");
}

const hibr_bi hold_bi[] = {
	{ "hold", m_hold, "keep a program on a terminal you can detach from" },
	HIBR_BI_END
};

HIBR_MODULE_P("hold", "0.21",
	      "sessions that outlive the terminal they were started on",
	      hold_bi, hd_ini, hd_fini, "hold");
