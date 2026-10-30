/* proctab -- every process under /proc, one line each, in one call.
 *
 * Task Manager read the process table in the shell: for each process, its
 * stat, its status line by line to the two fields it wanted, and for a
 * name comm had cut short its cmdline too. On a machine running a browser
 * engine and a held desktop that is hundreds of processes and a few
 * thousand reads parsed one word at a time, and it ran once a second --
 * 200 to 350 ms each time on the owner's mini PC, during which the desktop
 * drew nothing, so a playing video stopped for a quarter of a second every
 * second. This is the same reading in C:
 *
 *   proctab      pid utime stime uid rss name, a line a process
 *
 * utime and stime are clock ticks from stat (fields 14 and 15, counted
 * from the last ')' since a name may hold one), uid the real uid from
 * status, rss VmRSS in kB from status (0 for a kernel thread, which has
 * none), and name comm -- extended from argv[0]'s base name when comm sits
 * at the kernel's 15-byte limit and argv[0] begins with it, the rule
 * tasks_fullname had. A process that goes between two reads is left out,
 * never half read. Into the result slot, and printed unless := is
 * catching it. Where there is no /proc it fails, and the caller uses ps.
 */
#include "hibr.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* A whole small file into b, replacing what was there: 0 when it could not
   be read, which for a process is it having gone. */
int pq_slurp(const char *path, str *b)
{
	int fd = open(path, O_RDONLY | O_CLOEXEC);
	ssize_t k;

	b->n = 0;
	if (fd < 0)
		return 0;
	for (;;) {
		s_grow(b, HIBR_IOCH);
		k = read(fd, b->p + b->n, HIBR_IOCH);
		if (k < 0 && errno == EINTR)
			continue;
		if (k <= 0)
			break;
		b->n += (size_t)k;
	}
	close(fd);
	b->p[b->n] = 0;
	return k == 0;
}

/* The value of a "Key:" line in a status file, as a number; -1 when the
   key is not there. */
long pq_field(const char *t, const char *key)
{
	const char *p = t;
	size_t kl = strlen(key);

	while (p && *p) {
		if (!strncmp(p, key, kl))
			return strtol(p + kl, 0, 10);
		p = strchr(p, '\n');
		if (p)
			p++;
	}
	return -1;
}

/* One process's line onto out; 0 when it went before it could be read. */
int pq_one(const char *pid, str *out, str *b, str *path)
{
	char *open_, *close_, *q;
	str name;
	unsigned long long ut = 0, st = 0;
	long uid, rss;
	int f;

	path->n = 0;
	s_cat(path, "/proc/");
	s_cat(path, pid);
	s_cat(path, "/stat");
	if (!pq_slurp(path->p, b) || !b->n)
		return 0;
	open_ = strchr(b->p, '(');
	close_ = strrchr(b->p, ')');
	if (!open_ || !close_ || close_ < open_)
		return 0;
	s_init(&name);
	s_add(&name, open_ + 1, (size_t)(close_ - open_ - 1));
	q = close_ + 1;
	for (f = 3; f <= 15 && *q; f++) {
		while (*q == ' ')
			q++;
		if (f == 14)
			ut = strtoull(q, 0, 10);
		else if (f == 15)
			st = strtoull(q, 0, 10);
		while (*q && *q != ' ')
			q++;
	}
	path->n = 0;
	s_cat(path, "/proc/");
	s_cat(path, pid);
	s_cat(path, "/status");
	if (!pq_slurp(path->p, b) || (uid = pq_field(b->p, "Uid:")) < 0) {
		s_free(&name);
		return 0;
	}
	rss = pq_field(b->p, "VmRSS:");
	if (rss < 0)
		rss = 0;
	if (name.n >= 15) {
		path->n = 0;
		s_cat(path, "/proc/");
		s_cat(path, pid);
		s_cat(path, "/cmdline");
		if (pq_slurp(path->p, b) && b->n) {
			const char *a = b->p, *base = strrchr(a, '/');
			size_t al;

			base = base ? base + 1 : a;
			al = strlen(base);
			if (al > name.n && !strncmp(base, name.p, name.n)) {
				name.n = 0;
				s_add(&name, base, al);
			}
		}
	}
	s_cat(out, pid);
	s_ch(out, ' ');
	s_num(out, (long)ut);
	s_ch(out, ' ');
	s_num(out, (long)st);
	s_ch(out, ' ');
	s_num(out, uid);
	s_ch(out, ' ');
	s_num(out, rss);
	s_ch(out, ' ');
	s_add(out, name.p ? name.p : "", name.n);
	s_ch(out, '\n');
	s_free(&name);
	return 1;
}

/* proctab: every process, a line each. */
int m_proctab(sh *s, int ac, char **av)
{
	DIR *d = opendir("/proc");
	struct dirent *e;
	str out, b, path;

	(void)ac;
	(void)av;
	if (!d) {
		lg(HIBR_LERR, "proctab: /proc: %s", strerror(errno));
		return HIBR_FAIL;
	}
	s_init(&out);
	s_init(&b);
	s_init(&path);
	while ((e = readdir(d))) {
		const char *c = e->d_name;

		if (*c < '0' || *c > '9' || c[strspn(c, "0123456789")])
			continue;
		pq_one(c, &out, &b, &path);
	}
	closedir(d);
	if (out.n && out.p[out.n - 1] == '\n')
		out.p[--out.n] = 0;
	hibr_ret(s, out.p ? out.p : "");
	if (!s->bind && out.n)
		printf("%s\n", out.p);
	s_free(&out);
	s_free(&b);
	s_free(&path);
	return HIBR_OK;
}

const hibr_bi proctab_bi[] = {
	{ "proctab", m_proctab, "every process: pid utime stime uid rss name" },
	HIBR_BI_END
};

/* Prepare the module. */
int pq_ini(sh *s)
{
	(void)s;
	return HIBR_OK;
}

/* Tear the module down. */
void pq_fin(sh *s)
{
	(void)s;
}

HIBR_MODULE("proctab", "1.0", "every process under /proc in one call",
	    proctab_bi, pq_ini, pq_fin);
