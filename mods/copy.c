#define _GNU_SOURCE

#include "hibr.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#ifndef CY_CHUNK
#define CY_CHUNK (1 << 20)
#endif
#ifndef CY_SLOWCHUNK
#define CY_SLOWCHUNK (64 << 10)
#endif

typedef struct cy cy;
struct cy {
	long long total, done;
	long items, idone;
	long ms, slow;
	int exdev;
	double last;
	char *buf;
	size_t bufn;
	const char *stat, *stopf;
	str now, why;
};

int pl_prog(sh *s, char **av);

volatile sig_atomic_t cy_stopped;

/* A stop asked for by a signal: noted, and acted on between writes. */
void cy_onsig(int sig)
{
	(void)sig;
	cy_stopped = 1;
}

/* Milliseconds on a clock that only goes forward. */
double cy_ms(void)
{
	struct timespec t;

	clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec * 1000.0 + t.tv_nsec / 1e6;
}

/* Write the whole of a buffer, through interruptions; -1 on failure. */
int cy_wrall(int fd, const char *p, size_t n)
{
	ssize_t w;

	while (n) {
		w = write(fd, p, n);
		if (w < 0 && errno == EINTR) {
			if (cy_stopped)
				return -1;
			continue;
		}
		if (w <= 0)
			return -1;
		p += w;
		n -= (size_t)w;
	}
	return 0;
}

/* Put what is happening in the status file, at most once a period unless
   asked: one line, replaced whole, so a reader never sees half of one. A
   stop file appearing is noticed here too, as often. */
void cy_status(cy *c, const char *kind, int force)
{
	str t, l;
	double now;
	int fd;

	if (!c->stat && !c->stopf)
		return;
	now = cy_ms();
	if (!force && now - c->last < (double)c->ms)
		return;
	c->last = now;
	if (c->stopf && access(c->stopf, F_OK) == 0)
		cy_stopped = 1;
	if (!c->stat)
		return;
	s_init(&t);
	s_init(&l);
	s_fmt(&t, "%s.part", c->stat);
	s_fmt(&l, "%s %lld %lld %ld %ld %s\n", kind, c->done, c->total, c->idone,
	      c->items, c->now.p ? c->now.p : "");
	fd = open(t.p, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
	if (fd >= 0) {
		if (cy_wrall(fd, l.p, l.n) == 0 && close(fd) == 0)
			rename(t.p, c->stat);
		else
			close(fd);
	}
	s_free(&t);
	s_free(&l);
}

/* The name a path is known by: its last part, trailing slashes ignored. */
void cy_base(const char *p, str *o)
{
	size_t n = strlen(p), i;

	while (n > 1 && p[n - 1] == '/')
		n--;
	i = n;
	while (i > 0 && p[i - 1] != '/')
		i--;
	o->n = 0;
	s_add(o, p + i, n - i);
}

/* Count what a path holds: its bytes and its items, links not followed. */
void cy_scan(cy *c, const char *p, long long *bytes)
{
	struct stat st;
	struct dirent *e;
	DIR *d;
	str q;

	if (cy_stopped || lstat(p, &st) < 0)
		return;
	c->items++;
	if (S_ISREG(st.st_mode)) {
		*bytes += st.st_size;
		c->total += st.st_size;
	}
	cy_status(c, "scan", 0);
	if (!S_ISDIR(st.st_mode) || !(d = opendir(p)))
		return;
	s_init(&q);
	while (!cy_stopped && (e = readdir(d))) {
		if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))
			continue;
		q.n = 0;
		s_fmt(&q, "%s/%s", p, e->d_name);
		cy_scan(c, q.p, bytes);
	}
	closedir(d);
	s_free(&q);
}

/* Say why a step failed, naming the path it failed on. */
void cy_fail(cy *c, const char *what, const char *p)
{
	c->why.n = 0;
	s_fmt(&c->why, "%s %s: %s", what, p, strerror(errno));
}

/* Copy one regular file's bytes, then its owner, mode and times. */
int cy_file(cy *c, const char *src, const char *dst, struct stat *st)
{
	struct timespec tm[2];
	size_t want = c->slow ? CY_SLOWCHUNK : c->bufn;
	ssize_t n;
	int in, out, rc = -1;

	in = open(src, O_RDONLY | O_CLOEXEC);
	if (in < 0) {
		cy_fail(c, "cannot read", src);
		return -1;
	}
	out = open(dst, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
	if (out < 0) {
		cy_fail(c, "cannot write", dst);
		close(in);
		return -1;
	}
	for (;;) {
		if (cy_stopped)
			goto out;
		n = read(in, c->buf, want);
		if (n < 0 && errno == EINTR)
			continue;
		if (n < 0) {
			cy_fail(c, "cannot read", src);
			goto out;
		}
		if (n == 0)
			break;
		if (cy_wrall(out, c->buf, (size_t)n) < 0) {
			if (!cy_stopped)
				cy_fail(c, "cannot write", dst);
			goto out;
		}
		c->done += n;
		cy_status(c, "copy", 0);
		if (c->slow)
			usleep((useconds_t)c->slow * 1000);
	}
	if (fchown(out, st->st_uid, st->st_gid) < 0)
		lg(HIBR_LDBG, "copy: %s keeps this user as its owner", dst);
	tm[0] = st->st_atim;
	tm[1] = st->st_mtim;
	if (fchmod(out, st->st_mode & 07777) < 0 || futimens(out, tm) < 0) {
		cy_fail(c, "cannot set the mode or times of", dst);
		goto out;
	}
	rc = 0;
out:
	close(in);
	if (close(out) < 0 && rc == 0) {
		cy_fail(c, "cannot write", dst);
		rc = -1;
	}
	return rc;
}

/* Make a symbolic link again, pointing where the first one does. */
int cy_link(cy *c, const char *src, const char *dst, struct stat *st)
{
	struct timespec tm[2];
	str t;
	ssize_t n;
	int rc = -1;

	s_init(&t);
	s_grow(&t, (size_t)st->st_size + 64);
	for (;;) {
		n = readlink(src, t.p, t.cap);
		if (n < 0) {
			cy_fail(c, "cannot read the link", src);
			goto out;
		}
		if ((size_t)n < t.cap)
			break;
		s_grow(&t, t.cap * 2);
	}
	t.p[n] = 0;
	if (symlink(t.p, dst) < 0) {
		cy_fail(c, "cannot make the link", dst);
		goto out;
	}
	if (lchown(dst, st->st_uid, st->st_gid) < 0)
		lg(HIBR_LDBG, "copy: the link %s keeps this user as its owner", dst);
	tm[0] = st->st_atim;
	tm[1] = st->st_mtim;
	utimensat(AT_FDCWD, dst, tm, AT_SYMLINK_NOFOLLOW);
	rc = 0;
out:
	s_free(&t);
	return rc;
}

/* Copy a path and everything under it to dst, which must not exist. */
int cy_tree(cy *c, const char *src, const char *dst)
{
	struct timespec tm[2];
	struct stat st;
	struct dirent *e;
	DIR *d;
	str a, b;
	int rc = 0;

	if (cy_stopped)
		return -1;
	if (lstat(src, &st) < 0) {
		cy_fail(c, "cannot find", src);
		return -1;
	}
	c->now.n = 0;
	cy_base(src, &c->now);
	if (S_ISREG(st.st_mode))
		rc = cy_file(c, src, dst, &st);
	else if (S_ISLNK(st.st_mode))
		rc = cy_link(c, src, dst, &st);
	else if (!S_ISDIR(st.st_mode)) {
		c->why.n = 0;
		s_fmt(&c->why, "%s is not a file, a folder or a link", src);
		return -1;
	}
	if (!S_ISDIR(st.st_mode)) {
		if (rc == 0)
			c->idone++;
		return rc;
	}
	if (mkdir(dst, 0700) < 0) {
		cy_fail(c, "cannot make the folder", dst);
		return -1;
	}
	if (!(d = opendir(src))) {
		cy_fail(c, "cannot read the folder", src);
		return -1;
	}
	s_init(&a);
	s_init(&b);
	while (rc == 0 && (e = readdir(d))) {
		if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))
			continue;
		a.n = 0;
		b.n = 0;
		s_fmt(&a, "%s/%s", src, e->d_name);
		s_fmt(&b, "%s/%s", dst, e->d_name);
		rc = cy_tree(c, a.p, b.p);
	}
	closedir(d);
	s_free(&a);
	s_free(&b);
	if (rc)
		return rc;
	if (chown(dst, st.st_uid, st.st_gid) < 0)
		lg(HIBR_LDBG, "copy: the folder %s keeps this user as its owner", dst);
	tm[0] = st.st_atim;
	tm[1] = st.st_mtim;
	if (chmod(dst, st.st_mode & 07777) < 0 ||
	    utimensat(AT_FDCWD, dst, tm, 0) < 0) {
		cy_fail(c, "cannot set the mode or times of", dst);
		return -1;
	}
	c->idone++;
	return 0;
}

/* Remove a path and everything under it, never following a link. */
int cy_rmtree(const char *p)
{
	struct stat st;
	struct dirent *e;
	DIR *d;
	str q;
	int rc = 0;

	if (lstat(p, &st) < 0)
		return errno == ENOENT ? 0 : -1;
	if (!S_ISDIR(st.st_mode))
		return unlink(p);
	if (!(d = opendir(p)))
		return -1;
	s_init(&q);
	while ((e = readdir(d))) {
		if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))
			continue;
		q.n = 0;
		s_fmt(&q, "%s/%s", p, e->d_name);
		if (cy_rmtree(q.p) < 0)
			rc = -1;
	}
	closedir(d);
	s_free(&q);
	return rmdir(p) < 0 ? -1 : rc;
}

/* Whether dst is src or lies inside it, as the paths really resolve. */
int cy_inside(const char *src, const char *dst)
{
	char *a = realpath(src, 0), *b = realpath(dst, 0);
	size_t n;
	int in = 0;

	if (a && b) {
		n = strlen(a);
		in = !strncmp(a, b, n) && (b[n] == 0 || b[n] == '/' ||
					   (n == 1 && a[0] == '/'));
	}
	free(a);
	free(b);
	return in;
}

/* A target's own path, dst/name, into o. */
void cy_join(const char *dst, const char *name, str *o)
{
	size_t n = strlen(dst);

	o->n = 0;
	s_add(o, dst, n);
	if (!n || dst[n - 1] != '/')
		s_ch(o, '/');
	s_cat(o, name);
}

/* copy run [-m] [-i ms] [-s statusfile] [-k stopfile] dst src...: copy,
   or move, each source into the folder dst, saying on standard output what
   was done; a stop file appearing, or SIGTERM, stops it cleanly. */
int cy_run(sh *s, int ac, char **av)
{
	struct sigaction sa, oterm, oint;
	struct stat st;
	cy c;
	str name, to, part;
	long long *sizes;
	const char *dst, *e;
	int i, k, mv = 0, bad = 0, n;

	memset(&c, 0, sizeof c);
	c.ms = 100;
	for (i = 2; i < ac && av[i][0] == '-' && av[i][1]; i++) {
		if (!strcmp(av[i], "--")) {
			i++;
			break;
		}
		if (!strcmp(av[i], "-m"))
			mv = 1;
		else if (!strcmp(av[i], "-i") && i + 1 < ac)
			c.ms = atol(av[++i]);
		else if (!strcmp(av[i], "-s") && i + 1 < ac)
			c.stat = av[++i];
		else if (!strcmp(av[i], "-k") && i + 1 < ac)
			c.stopf = av[++i];
		else {
			lg(HIBR_LERR, "copy run: unknown option %s", av[i]);
			return 2;
		}
	}
	if (ac - i < 2) {
		lg(HIBR_LERR, "usage: copy run [-m] [-i ms] [-s statusfile] [-k stopfile] dst src...");
		return 2;
	}
	if ((s->sopt & O_PLAN) && !pl_prog(s, av))
		return HIBR_FAIL;
	dst = av[i++];
	if (stat(dst, &st) < 0 || !S_ISDIR(st.st_mode)) {
		lg(HIBR_LERR, "copy run: %s is not a folder", dst);
		return HIBR_FAIL;
	}
	n = ac - i;
	e = getenv("HIBR_COPYSLOW");
	c.slow = e ? atol(e) : 0;
	c.exdev = getenv("HIBR_COPYEXDEV") != 0;
	c.bufn = CY_CHUNK;
	c.buf = xm(c.bufn);
	sizes = xm((size_t)n * sizeof *sizes);
	s_init(&c.now);
	s_init(&c.why);
	s_init(&name);
	s_init(&to);
	s_init(&part);
	cy_stopped = 0;
	memset(&sa, 0, sizeof sa);
	sa.sa_handler = cy_onsig;
	sigemptyset(&sa.sa_mask);
	sigaction(SIGTERM, &sa, &oterm);
	sigaction(SIGINT, &sa, &oint);
	cy_status(&c, "scan", 1);
	for (k = 0; k < n; k++) {
		sizes[k] = 0;
		cy_scan(&c, av[i + k], &sizes[k]);
	}
	printf("total %lld %ld\n", c.total, c.items);
	fflush(stdout);
	cy_status(&c, "copy", 1);
	for (k = 0; k < n && !cy_stopped; k++) {
		const char *src = av[i + k];

		cy_base(src, &name);
		cy_join(dst, name.p, &to);
		c.now.n = 0;
		s_cat(&c.now, name.p);
		if (lstat(src, &st) < 0) {
			printf("err %s\t%s is not there any more\n", src, name.p);
			bad++;
			continue;
		}
		if (lstat(to.p, &st) == 0) {
			printf("err %s\t%s is already there\n", src, name.p);
			bad++;
			continue;
		}
		if (cy_inside(src, dst)) {
			printf("err %s\t%s cannot go inside itself\n", src, name.p);
			bad++;
			continue;
		}
		errno = EXDEV;
		if (mv && !c.exdev && rename(src, to.p) == 0) {
			c.done += sizes[k];
			printf("ok %s\t%s\n", src, to.p);
			fflush(stdout);
			cy_status(&c, "copy", 1);
			continue;
		}
		if (mv && errno != EXDEV) {
			printf("err %s\tcannot move %s: %s\n", src, name.p, strerror(errno));
			bad++;
			continue;
		}
		part.n = 0;
		s_fmt(&part, "%.*s.%s.hibr-part", (int)(to.n - name.n), to.p, name.p);
		cy_rmtree(part.p);
		if (cy_tree(&c, src, part.p) < 0) {
			cy_rmtree(part.p);
			if (cy_stopped)
				break;
			printf("err %s\t%s\n", src, c.why.p ? c.why.p : "cannot copy");
			bad++;
			continue;
		}
		if (rename(part.p, to.p) < 0) {
			printf("err %s\tcannot name %s: %s\n", src, name.p, strerror(errno));
			cy_rmtree(part.p);
			bad++;
			continue;
		}
		printf("ok %s\t%s\n", src, to.p);
		if (mv && cy_rmtree(src) < 0) {
			printf("err %s\tcopied, but the original could not be removed: %s\n",
			       src, strerror(errno));
			bad++;
		}
		fflush(stdout);
	}
	if (cy_stopped) {
		printf("stopped %lld %ld\n", c.done, c.idone);
		cy_status(&c, "stopped", 1);
	} else {
		printf("done %lld %ld\n", c.done, c.idone);
		cy_status(&c, "done", 1);
	}
	fflush(stdout);
	sigaction(SIGTERM, &oterm, 0);
	sigaction(SIGINT, &oint, 0);
	k = cy_stopped;
	cy_stopped = 0;
	free(c.buf);
	free(sizes);
	s_free(&c.now);
	s_free(&c.why);
	s_free(&name);
	s_free(&to);
	s_free(&part);
	return k || bad ? HIBR_FAIL : HIBR_OK;
}

/* copy rename dst src...: move each source into dst where that is only a
   rename, on the same filesystem; the result is a list of the rest. */
int cy_rename(sh *s, int ac, char **av)
{
	struct stat st;
	str name, to;
	vec rest;
	int i;

	if (ac < 4) {
		lg(HIBR_LERR, "usage: copy rename dst src...");
		return 2;
	}
	if ((s->sopt & O_PLAN) && !pl_prog(s, av))
		return HIBR_FAIL;
	s_init(&name);
	s_init(&to);
	memset(&rest, 0, sizeof rest);
	for (i = 3; i < ac; i++) {
		cy_base(av[i], &name);
		cy_join(av[2], name.p, &to);
		if (lstat(to.p, &st) == 0 || cy_inside(av[i], av[2]) ||
		    getenv("HIBR_COPYEXDEV") || rename(av[i], to.p) < 0)
			v_add(&rest, av[i]);
	}
	hibr_retn(s, (char **)rest.p, rest.n);
	free(rest.p);
	s_free(&name);
	s_free(&to);
	return HIBR_OK;
}

/* copy run|rename ... */
int cy_bi(sh *s, int ac, char **av)
{
	const char *sub = ac > 1 ? av[1] : "";

	if (!strcmp(sub, "run"))
		return cy_run(s, ac, av);
	if (!strcmp(sub, "rename"))
		return cy_rename(s, ac, av);
	lg(HIBR_LERR, "usage: copy run|rename ...");
	return 2;
}

const hibr_bi cy_bis[] = {
	{ "copy", cy_bi, "copy and move with progress: copy run|rename" },
	HIBR_BI_END
};

HIBR_MODULE("copy", "1.0", "copy and move trees with progress and a clean stop",
	    cy_bis, 0, 0);
