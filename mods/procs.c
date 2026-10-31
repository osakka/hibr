/* procs -- one reader of the process table under /proc, for proctab and mon.
 * See procs.h for what is read and why.
 */
#include "procs.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* A whole small file into b, replacing what was there: 0 when it could not
   be read, which for a process is it having gone. */
int pq_slurp(const char *path, str *b)
{
	int fd = open(path, O_RDONLY | O_CLOEXEC);
	ssize_t k = 0;

	b->n = 0;
	s_grow(b, HIBR_IOCH);
	b->p[0] = 0;
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

/* One process into e; 0 when it went before it could be read. */
int pq_read(const char *pid, pq_ent *e, str *b, str *path)
{
	char *open_, *close_, *q;
	int f;

	e->pid = atol(pid);
	e->ut = e->st = 0;
	e->name.n = 0;
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
	s_add(&e->name, open_ + 1, (size_t)(close_ - open_ - 1));
	q = close_ + 1;
	for (f = 3; f <= 15 && *q; f++) {
		while (*q == ' ')
			q++;
		if (f == 14)
			e->ut = strtoull(q, 0, 10);
		else if (f == 15)
			e->st = strtoull(q, 0, 10);
		while (*q && *q != ' ')
			q++;
	}
	path->n = 0;
	s_cat(path, "/proc/");
	s_cat(path, pid);
	s_cat(path, "/status");
	if (!pq_slurp(path->p, b) || (e->uid = pq_field(b->p, "Uid:")) < 0)
		return 0;
	e->rss = pq_field(b->p, "VmRSS:");
	if (e->rss < 0)
		e->rss = 0;
	if (e->name.n >= 15) {
		path->n = 0;
		s_cat(path, "/proc/");
		s_cat(path, pid);
		s_cat(path, "/cmdline");
		if (pq_slurp(path->p, b) && b->n) {
			const char *a = b->p, *base = strrchr(a, '/');
			size_t al;

			base = base ? base + 1 : a;
			al = strlen(base);
			if (al > e->name.n && !strncmp(base, e->name.p, e->name.n)) {
				e->name.n = 0;
				s_add(&e->name, base, al);
			}
		}
	}
	return 1;
}

/* Every process under /proc, each handed to fn. */
int pq_each(void (*fn)(const pq_ent *, void *), void *ctx)
{
	DIR *d = opendir("/proc");
	struct dirent *de;
	str b, path;
	pq_ent e;

	if (!d)
		return 0;
	s_init(&b);
	s_init(&path);
	memset(&e, 0, sizeof e);
	s_init(&e.name);
	while ((de = readdir(d))) {
		const char *c = de->d_name;

		if (*c < '0' || *c > '9' || c[strspn(c, "0123456789")])
			continue;
		if (pq_read(c, &e, &b, &path))
			fn(&e, ctx);
	}
	closedir(d);
	s_free(&b);
	s_free(&path);
	s_free(&e.name);
	return 1;
}
