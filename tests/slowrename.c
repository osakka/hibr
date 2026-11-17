/* A slow disk, for the suites: a rename onto any path under HIBR_SLOWDIR
   waits HIBR_SLOWMS (default 2000) first, as a rename on the owner's
   USB-backed volume held their desktop 2.7 s (0.99.546). Preloaded with
   LD_PRELOAD into the shell under test; every other rename is untouched. */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Wait when the target is under the slow folder. */
static void sr_wait(const char *to)
{
	const char *d = getenv("HIBR_SLOWDIR");
	const char *m = getenv("HIBR_SLOWMS");
	long ms = m ? atol(m) : 2000;
	struct timespec t;

	if (!d || !*d || !to || strncmp(to, d, strlen(d)))
		return;
	t.tv_sec = ms / 1000;
	t.tv_nsec = (ms % 1000) * 1000000L;
	while (nanosleep(&t, &t) != 0)
		;
}

/* rename, made slow under the folder. */
int rename(const char *from, const char *to)
{
	int (*real)(const char *, const char *) =
		(int (*)(const char *, const char *))dlsym(RTLD_NEXT, "rename");

	sr_wait(to);
	return real(from, to);
}

/* renameat, made slow under the folder when given an absolute path. */
int renameat(int fd1, const char *from, int fd2, const char *to)
{
	int (*real)(int, const char *, int, const char *) =
		(int (*)(int, const char *, int, const char *))dlsym(RTLD_NEXT, "renameat");

	sr_wait(to);
	return real(fd1, from, fd2, to);
}
