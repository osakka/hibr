#define _GNU_SOURCE

#include "hibr.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#ifdef __linux__
#include <sys/inotify.h>
#endif

typedef struct dw_watch dw_watch;
struct dw_watch {
	int wd;
	char *path;
};

int dw_fd = -1;
vec dw_list;

/* Say a number as the command's answer, or print it. */
int dw_say(sh *s, long n)
{
	str o;

	s_init(&o);
	s_num(&o, n);
	if (s->bind)
		hibr_ret(s, o.p);
	else
		printf("%s\n", o.p);
	s_free(&o);
	return HIBR_OK;
}

#ifdef __linux__
/* The descriptor every watch reports on, opened on first use: non-blocking, so a read with nothing waiting answers at once, and closed on exec. */
int dw_open(void)
{
	if (dw_fd >= 0)
		return dw_fd;
	dw_fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
	if (dw_fd < 0)
		lg(HIBR_LERR, "dirwatch: %s", strerror(errno));
	return dw_fd;
}

/* Watch a folder or a file for things added, removed, renamed or written in it; the number it reports under. */
int dw_add(sh *s, const char *path)
{
	int wd;
	size_t i;
	dw_watch *w;

	if (dw_open() < 0)
		return HIBR_FAIL;
	wd = inotify_add_watch(dw_fd, path, IN_CREATE | IN_DELETE | IN_MOVED_FROM |
			       IN_MOVED_TO | IN_CLOSE_WRITE | IN_ATTRIB |
			       IN_DELETE_SELF | IN_MOVE_SELF);
	if (wd < 0) {
		lg(HIBR_LERR, "dirwatch: %s: %s", path, strerror(errno));
		return HIBR_FAIL;
	}
	for (i = 0; i < dw_list.n; i++)
		if (((dw_watch *)dw_list.p[i])->wd == wd)
			return dw_say(s, wd);
	w = xm(sizeof *w);
	memset(w, 0, sizeof *w);
	w->wd = wd;
	w->path = xs(path);
	v_add(&dw_list, w);
	lg(HIBR_LDBG, "dirwatch: %s is watch %d", path, wd);
	return dw_say(s, wd);
}

/* Stop watch n; a watch the kernel already ended (its folder gone) is let go quietly. */
int dw_drop(int wd)
{
	size_t i;
	dw_watch *w;

	for (i = 0; i < dw_list.n; i++) {
		w = dw_list.p[i];
		if (w->wd != wd)
			continue;
		inotify_rm_watch(dw_fd, wd);
		free(w->path);
		free(w);
		dw_list.p[i] = dw_list.p[--dw_list.n];
		return HIBR_OK;
	}
	return HIBR_FAIL;
}

/* Everything waiting, drained: the watches something happened under, each once, as the answer's list; status 1 when nothing had. */
int dw_read(sh *s)
{
	char *buf = xm(HIBR_IOCH * 4), *p;
	ssize_t n;
	vec seen;
	str one;
	struct inotify_event *e;
	size_t i;
	int dup;

	memset(&seen, 0, sizeof seen);
	if (dw_fd < 0) {
		free(buf);
		return HIBR_FAIL;
	}
	for (;;) {
		n = read(dw_fd, buf, HIBR_IOCH * 4);
		if (n < 0 && errno == EINTR)
			continue;
		if (n <= 0)
			break;
		for (p = buf; p < buf + n; p += sizeof *e + e->len) {
			e = (struct inotify_event *)p;
			if (e->mask & IN_IGNORED)
				continue;
			if (e->mask & IN_Q_OVERFLOW) {
				lg(HIBR_LDBG, "dirwatch: the queue overflowed: every watch counts as changed");
				for (i = 0; i < dw_list.n; i++) {
					s_init(&one);
					s_num(&one, ((dw_watch *)dw_list.p[i])->wd);
					v_add(&seen, xs(one.p));
					s_free(&one);
				}
				continue;
			}
			s_init(&one);
			s_num(&one, e->wd);
			for (dup = 0, i = 0; i < seen.n; i++)
				if (!strcmp(seen.p[i], one.p))
					dup = 1;
			if (!dup)
				v_add(&seen, xs(one.p));
			s_free(&one);
		}
	}
	free(buf);
	if (!seen.n) {
		v_free(&seen);
		return HIBR_FAIL;
	}
	if (s->bind)
		hibr_retn(s, (char **)seen.p, seen.n);
	else
		for (i = 0; i < seen.n; i++)
			printf("%s\n", (char *)seen.p[i]);
	for (i = 0; i < seen.n; i++)
		free(seen.p[i]);
	v_free(&seen);
	return HIBR_OK;
}
#endif

/* dirwatch add PATH | drop N | fd | read | list: folders and files watched by the kernel, so a change wakes whoever watches the descriptor. */
int dw_bi(sh *s, int ac, char **av)
{
#ifdef __linux__
	size_t i;

	if (ac >= 3 && !strcmp(av[1], "add"))
		return dw_add(s, av[2]);
	if (ac >= 3 && !strcmp(av[1], "drop"))
		return dw_drop(atoi(av[2]));
	if (ac >= 2 && !strcmp(av[1], "fd")) {
		if (dw_open() < 0)
			return HIBR_FAIL;
		return dw_say(s, dw_fd);
	}
	if (ac >= 2 && !strcmp(av[1], "read"))
		return dw_read(s);
	if (ac >= 2 && !strcmp(av[1], "list")) {
		for (i = 0; i < dw_list.n; i++)
			printf("%d\t%s\n", ((dw_watch *)dw_list.p[i])->wd,
			       ((dw_watch *)dw_list.p[i])->path);
		return HIBR_OK;
	}
	fprintf(stderr, "usage: dirwatch add PATH | drop N | fd | read | list\n");
	return 2;
#else
	(void)s;
	(void)ac;
	(void)av;
	lg(HIBR_LERR, "dirwatch: not on this system yet (Linux's inotify only)");
	return HIBR_FAIL;
#endif
}

/* Close the descriptor and forget every watch. */
void dw_fini(sh *s)
{
	size_t i;

	(void)s;
	for (i = 0; i < dw_list.n; i++) {
		free(((dw_watch *)dw_list.p[i])->path);
		free(dw_list.p[i]);
	}
	v_free(&dw_list);
	memset(&dw_list, 0, sizeof dw_list);
	if (dw_fd >= 0)
		close(dw_fd);
	dw_fd = -1;
}

const hibr_bi dw_bis[] = {
	{ "dirwatch", dw_bi, "folders watched by the kernel: dirwatch add|drop|fd|read|list" },
	HIBR_BI_END
};

HIBR_MODULE("dirwatch", "0.1", "folders and files watched by the kernel (inotify), so a change wakes the desktop at once",
	    dw_bis, 0, dw_fini);
