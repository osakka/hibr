#define _GNU_SOURCE

#include "mn.h"
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Read a whole /proc file, which cannot be stat'ed for its size. */
char *mn_slurp(const char *path)
{
	int fd = open(path, O_RDONLY);
	str b;
	char t[8192];
	ssize_t k;

	if (fd < 0)
		return 0;
	s_init(&b);
	while ((k = read(fd, t, sizeof t)) > 0)
		s_add(&b, t, (size_t)k);
	close(fd);
	if (!b.p) {
		b.p = xm(1);
		b.p[0] = 0;
	}
	b.p[b.n] = 0;
	return b.p;
}

/* Start an empty reading. */
void mn_init(mn_st *s)
{
	memset(s, 0, sizeof *s);
	s->clk = sysconf(_SC_CLK_TCK);
	if (s->clk <= 0)
		s->clk = 100;
	s->first = 1;
}

/* Release everything a reading holds. */
void mn_free(mn_st *s)
{
	size_t i;

	for (i = 0; i < s->procs.n; i++)
		free(s->procs.p[i]);
	for (i = 0; i < s->nets.n; i++)
		free(s->nets.p[i]);
	for (i = 0; i < s->disks.n; i++)
		free(s->disks.p[i]);
	v_free(&s->procs);
	v_free(&s->nets);
	v_free(&s->disks);
	free(s->core);
	memset(s, 0, sizeof *s);
}

/* The value after a key in a "Key: 1234 kB" file. */
unsigned long long mn_field(const char *t, const char *key)
{
	const char *p = strstr(t, key);

	if (!p)
		return 0;
	p += strlen(key);
	while (*p && !isdigit((unsigned char)*p))
		p++;
	return strtoull(p, 0, 10);
}

/* Work out how busy each processor has been since the last reading. */
void mn_cpus(mn_st *s)
{
	char *t = mn_slurp("/proc/stat"), *p;
	int i = -1, n = 0;

	if (!t)
		return;
	for (p = t; p; p = strchr(p, '\n')) {
		if (*p == '\n')
			p++;
		if (strncmp(p, "cpu", 3))
			continue;
		if (isdigit((unsigned char)p[3]))
			n++;
	}
	if (!s->core) {
		s->ncore = n;
		s->core = xm((size_t)(n + 1) * sizeof *s->core);
		memset(s->core, 0, (size_t)(n + 1) * sizeof *s->core);
	}
	for (p = t; p; p = strchr(p, '\n')) {
		unsigned long long v[10], busy = 0, total = 0;
		mn_cpu *c;
		int k;
		if (*p == '\n')
			p++;
		if (strncmp(p, "cpu", 3))
			continue;
		if (i >= s->ncore)
			break;
		for (k = 0; k < 10; k++)
			v[k] = 0;
		{
			const char *q = p + 3;
			while (*q && !isdigit((unsigned char)*q))
				q++;
			for (k = 0; k < 10 && *q; k++) {
				v[k] = strtoull(q, (char **)&q, 10);
				while (*q == ' ')
					q++;
				if (*q == '\n')
					break;
			}
		}
		for (k = 0; k < 10; k++)
			total += v[k];
		busy = total - v[3] - v[4];
		c = i < 0 ? &s->all : &s->core[i];
		if (c->total && total > c->total) {
			double dt = (double)(total - c->total);
			c->pct = dt > 0 ? (double)(busy - c->busy) * 100.0 / dt
					: 0.0;
		} else {
			c->pct = 0.0;
		}
		c->busy = busy;
		c->total = total;
		i++;
	}
	free(t);
}

/* Memory, swap, load and uptime, which are all plain readings. */
void mn_basics(mn_st *s)
{
	char *t;

	t = mn_slurp("/proc/meminfo");
	if (t) {
		s->mtotal = mn_field(t, "MemTotal:");
		s->mfree = mn_field(t, "MemFree:");
		s->mavail = mn_field(t, "MemAvailable:");
		s->mbuf = mn_field(t, "Buffers:");
		s->mcache = mn_field(t, "Cached:");
		s->stotal = mn_field(t, "SwapTotal:");
		s->sfree = mn_field(t, "SwapFree:");
		free(t);
	}
	t = mn_slurp("/proc/loadavg");
	if (t) {
		char *e;
		s->load1 = strtod(t, &e);
		s->load5 = strtod(e, &e);
		s->load15 = strtod(e, &e);
		free(t);
	}
	t = mn_slurp("/proc/uptime");
	if (t) {
		s->uptime = strtod(t, 0);
		free(t);
	}
}

/* Find a named entry in a list of counters, or add one. */
mn_dev *mn_dev_of(vec *v, const char *nm)
{
	size_t i;
	mn_dev *d;

	for (i = 0; i < v->n; i++) {
		d = (mn_dev *)v->p[i];
		if (!strcmp(d->name, nm))
			return d;
	}
	d = xm(sizeof *d);
	memset(d, 0, sizeof *d);
	strncpy(d->name, nm, sizeof d->name - 1);
	v_add(v, d);
	return d;
}

/* Bytes in and out of each interface, as a rate. */
void mn_net(mn_st *s, double secs)
{
	char *t = mn_slurp("/proc/net/dev"), *p, *ln;
	int skip = 2;

	if (!t)
		return;
	for (p = t; *p; p = ln) {
		char nm[32];
		unsigned long long rx, tx;
		size_t k = 0;
		const char *q;
		mn_dev *d;
		ln = strchr(p, '\n');
		if (ln)
			*ln++ = 0;
		else
			ln = p + strlen(p);
		if (skip-- > 0)
			continue;
		while (*p == ' ')
			p++;
		while (*p && *p != ':' && k < sizeof nm - 1)
			nm[k++] = *p++;
		nm[k] = 0;
		if (*p != ':' || !strcmp(nm, "lo"))
			continue;
		q = p + 1;
		rx = strtoull(q, (char **)&q, 10);
		for (k = 0; k < 7; k++)
			strtoull(q, (char **)&q, 10);
		tx = strtoull(q, (char **)&q, 10);
		d = mn_dev_of(&s->nets, nm);
		if (d->pa && secs > 0) {
			d->ra = (double)(rx - d->pa) / secs;
			d->rb = (double)(tx - d->pb) / secs;
		}
		d->pa = rx;
		d->pb = tx;
		d->a = rx;
		d->b = tx;
	}
	free(t);
}

/* Sectors read and written per disk, as a rate in bytes. */
void mn_disk(mn_st *s, double secs)
{
	char *t = mn_slurp("/proc/diskstats"), *p, *ln;

	if (!t)
		return;
	for (p = t; *p; p = ln) {
		char nm[32];
		unsigned long long v[14], rd, wr;
		int k;
		const char *q;
		mn_dev *d;
		ln = strchr(p, '\n');
		if (ln)
			*ln++ = 0;
		else
			ln = p + strlen(p);
		q = p;
		while (*q == ' ')
			q++;
		strtoull(q, (char **)&q, 10);
		strtoull(q, (char **)&q, 10);
		while (*q == ' ')
			q++;
		k = 0;
		while (*q && *q != ' ' && k < (int)sizeof nm - 1)
			nm[k++] = *q++;
		nm[k] = 0;
		if (!k || isdigit((unsigned char)nm[k - 1]) ||
		    !strncmp(nm, "loop", 4) || !strncmp(nm, "ram", 3))
			continue;
		for (k = 0; k < 14; k++)
			v[k] = strtoull(q, (char **)&q, 10);
		rd = v[2] * 512;
		wr = v[6] * 512;
		d = mn_dev_of(&s->disks, nm);
		if (d->pa && secs > 0) {
			d->ra = (double)(rd - d->pa) / secs;
			d->rb = (double)(wr - d->pb) / secs;
		}
		d->pa = rd;
		d->pb = wr;
	}
	free(t);
}

/* Find a process we already know about. */
mn_proc *mn_proc_of(vec *v, long pid)
{
	size_t i;
	mn_proc *p;

	for (i = 0; i < v->n; i++) {
		p = (mn_proc *)v->p[i];
		if (p->pid == pid)
			return p;
	}
	return 0;
}

/* Who owns a process, looked up once. */
void mn_owner(mn_proc *p)
{
	str path;
	char *t;
	struct passwd *pw;
	unsigned long uid;

	s_init(&path);
	s_cat(&path, "/proc/");
	s_num(&path, p->pid);
	s_cat(&path, "/status");
	t = mn_slurp(path.p);
	s_free(&path);
	if (!t) {
		strcpy(p->user, "?");
		return;
	}
	uid = (unsigned long)mn_field(t, "Uid:");
	free(t);
	pw = getpwuid((uid_t)uid);
	if (pw && pw->pw_name)
		strncpy(p->user, pw->pw_name, sizeof p->user - 1);
	else
		snprintf(p->user, sizeof p->user, "%lu", uid);
}

/* Every process, with how much processor it used since the last reading. */
void mn_procs(mn_st *s, double secs)
{
	DIR *d = opendir("/proc");
	struct dirent *e;
	size_t i;

	if (!d)
		return;
	for (i = 0; i < s->procs.n; i++)
		((mn_proc *)s->procs.p[i])->seen = 0;
	while ((e = readdir(d))) {
		long pid;
		str path;
		char *t, *a, *b;
		unsigned long long ut, st;
		mn_proc *p;
		if (!isdigit((unsigned char)e->d_name[0]))
			continue;
		pid = atol(e->d_name);
		s_init(&path);
		s_cat(&path, "/proc/");
		s_cat(&path, e->d_name);
		s_cat(&path, "/stat");
		t = mn_slurp(path.p);
		s_free(&path);
		if (!t)
			continue;
		a = strchr(t, '(');
		b = a ? strrchr(a, ')') : 0;
		if (!a || !b) {
			free(t);
			continue;
		}
		p = mn_proc_of(&s->procs, pid);
		if (!p) {
			p = xm(sizeof *p);
			memset(p, 0, sizeof *p);
			p->pid = pid;
			v_add(&s->procs, p);
			mn_owner(p);
		}
		{
			size_t nn = (size_t)(b - a - 1);
			if (nn > sizeof p->name - 1)
				nn = sizeof p->name - 1;
			memcpy(p->name, a + 1, nn);
			p->name[nn] = 0;
		}
		{
			/* Fields are one-based and the third is a letter, so
			   the skip has to step over it before counting
			   numbers: 4..13, then utime and stime, then 16..23,
			   then rss in pages. */
			const char *q = b + 2;
			int k;
			while (*q && *q != ' ')
				q++;
			for (k = 0; k < 10 && *q; k++)
				strtoull(q, (char **)&q, 10);
			ut = strtoull(q, (char **)&q, 10);
			st = strtoull(q, (char **)&q, 10);
			for (k = 0; k < 8 && *q; k++)
				strtoull(q, (char **)&q, 10);
			p->rss = (long)strtoull(q, (char **)&q, 10) *
				 (sysconf(_SC_PAGESIZE) / 1024);
		}
		p->prev = p->ticks;
		p->ticks = ut + st;
		if (secs > 0 && p->prev && p->ticks >= p->prev)
			p->pct = (double)(p->ticks - p->prev) * 100.0 /
				 ((double)s->clk * secs);
		else
			p->pct = 0.0;
		p->seen = 1;
		free(t);
	}
	closedir(d);
	for (i = s->procs.n; i-- > 0;) {
		mn_proc *p = (mn_proc *)s->procs.p[i];
		if (p->seen)
			continue;
		free(p);
		s->procs.p[i] = s->procs.p[--s->procs.n];
	}
}

/* Take one reading of everything. */
void mn_sample(mn_st *s, double secs)
{
	double used;

	mn_cpus(s);
	mn_basics(s);
	mn_net(s, secs);
	mn_disk(s, secs);
	mn_procs(s, secs);
	used = s->mtotal ? (double)(s->mtotal - s->mavail) * 100.0 /
				   (double)s->mtotal
			 : 0.0;
	s->cpuhist[s->hn % MN_HIST] = s->all.pct;
	s->memhist[s->hn % MN_HIST] = used;
	{
		double n = 0;
		size_t i;
		for (i = 0; i < s->nets.n; i++)
			n += ((mn_dev *)s->nets.p[i])->ra +
			     ((mn_dev *)s->nets.p[i])->rb;
		s->nethist[s->hn % MN_HIST] = n;
	}
	s->hn++;
	s->first = 0;
}
