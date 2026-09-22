#define _GNU_SOURCE

#include "hibr.h"
#include <ctype.h>
#include <errno.h>
#include <grp.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* Print seconds since the epoch. */
int m_epoch(sh *s, int ac, char **av)
{
	(void)s;
	(void)ac;
	(void)av;
	printf("%ld\n", (long)time(0));
	return HIBR_OK;
}

/* Sleep for a number of milliseconds. */
int m_sleepms(sh *s, int ac, char **av)
{
	long ms = ac > 1 ? atol(av[1]) : 0;
	struct timespec t;

	(void)s;
	if (ms < 0) {
		lg(HIBR_LERR, "sleepms: negative duration");
		return HIBR_FAIL;
	}
	t.tv_sec = ms / 1000;
	t.tv_nsec = (ms % 1000) * 1000000L;
	lg(HIBR_LDBG, "sleeping %ld ms", ms);
	nanosleep(&t, 0);
	return HIBR_OK;
}

/* Write arguments in upper case. */
int m_upper(sh *s, int ac, char **av)
{
	int i;
	size_t j;
	str b;

	(void)s;
	s_init(&b);
	for (i = 1; i < ac; i++) {
		if (i > 1)
			s_ch(&b, ' ');
		s_cat(&b, av[i]);
	}
	for (j = 0; j < b.n; j++)
		b.p[j] = (char)toupper((unsigned char)b.p[j]);
	puts(b.n ? b.p : "");
	s_free(&b);
	return HIBR_OK;
}

/* Report the shell state this module can reach. */
int m_state(sh *s, int ac, char **av)
{
	(void)ac;
	(void)av;
	printf("status %d  params %d  functions %lu  loglevel %d\n", s->st, s->ac,
	       (unsigned long)s->fns.n, hibr_lv);
	return HIBR_OK;
}

/* Look up a user by name, or by number when the name is all digits. */
struct passwd *sy_user(const char *n)
{
	struct passwd *p = getpwnam(n);
	const char *d;

	if (p)
		return p;
	for (d = n; *d; d++)
		if (!isdigit((unsigned char)*d))
			return 0;
	return getpwuid((uid_t)atol(n));
}

/* Look up a group by name, or by number when the name is all digits. */
struct group *sy_group(const char *n)
{
	struct group *g = getgrnam(n);
	const char *d;

	if (g)
		return g;
	for (d = n; *d; d++)
		if (!isdigit((unsigned char)*d))
			return 0;
	return getgrgid((gid_t)atol(n));
}

/* Record a number in a shell variable. */
void sy_setn(sh *s, const char *k, long v)
{
	str b;

	s_init(&b);
	s_num(&b, v);
	hibr_set(s, k, b.p, 0);
	s_free(&b);
}

/* Abandon the shell after a drop failed with the change half made. */
int sy_halfway(sh *s, const char *what)
{
	lg(HIBR_LERR, "drop: %s: %s", what, strerror(errno));
	lg(HIBR_LERR, "drop: privileges are half given up; leaving");
	s->quit = 1;
	return s->st = HIBR_FAIL;
}

/* Give up root for a user, and that user's group unless another is named. */
int sy_drop(sh *s, int ac, char **av)
{
	struct passwd *pw;
	struct group *gr;
	char *spec, *colon, *nm;
	uid_t uid;
	gid_t gid;

	if (ac != 2) {
		lg(HIBR_LERR, "usage: drop user[:group]");
		return 2;
	}
	if (geteuid() != 0) {
		lg(HIBR_LERR, "drop: only root can give up privileges");
		return HIBR_FAIL;
	}
	spec = xs(av[1]);
	colon = strchr(spec, ':');
	if (colon)
		*colon = 0;
	pw = sy_user(spec);
	if (!pw) {
		lg(HIBR_LERR, "drop: %s: no such user", spec);
		free(spec);
		return HIBR_FAIL;
	}
	uid = pw->pw_uid;
	gid = pw->pw_gid;
	nm = xs(pw->pw_name);
	if (colon && colon[1]) {
		gr = sy_group(colon + 1);
		if (!gr) {
			lg(HIBR_LERR, "drop: %s: no such group", colon + 1);
			free(nm);
			free(spec);
			return HIBR_FAIL;
		}
		gid = gr->gr_gid;
	}
	free(spec);
	if (uid == 0) {
		lg(HIBR_LERR, "drop: %s is root; there is nothing to give up",
		   nm);
		free(nm);
		return HIBR_FAIL;
	}
	lg(HIBR_LDBG, "dropping to %s, uid %ld, gid %ld", nm, (long)uid,
	   (long)gid);
	if (initgroups(nm, gid) != 0) {
		lg(HIBR_LERR, "drop: initgroups %s: %s", nm, strerror(errno));
		free(nm);
		return HIBR_FAIL;
	}
#ifdef __APPLE__
	lg(HIBR_LDBG, "no setresuid here; setgid and setuid move all three ids "
		      "while the effective id is still root");
	if (setgid(gid) != 0)
		return sy_halfway(s, "setgid");
	if (setuid(uid) != 0)
		return sy_halfway(s, "setuid");
#else
	if (setresgid(gid, gid, gid) != 0)
		return sy_halfway(s, "setresgid");
	if (setresuid(uid, uid, uid) != 0)
		return sy_halfway(s, "setresuid");
#endif
	if (getuid() != uid || geteuid() != uid || getgid() != gid ||
	    getegid() != gid) {
		errno = 0;
		return sy_halfway(s, "the ids did not take");
	}
	if (setuid(0) == 0) {
		errno = 0;
		return sy_halfway(s, "root can still be taken back");
	}
	sy_setn(s, "UID", (long)uid);
	sy_setn(s, "EUID", (long)uid);
	lg(HIBR_LINF, "gave up root for %s (uid %ld, gid %ld)", nm, (long)uid,
	   (long)gid);
	free(nm);
	return HIBR_OK;
}

const hibr_bi sys_bi[] = {
	{ "drop", sy_drop, "give up root for a user" },
	{ "epoch", m_epoch, "seconds since the epoch" },
	{ "sleepms", m_sleepms, "sleep for milliseconds" },
	{ "state", m_state, "report shell state" },
	{ "upper", m_upper, "write arguments in upper case" },
	HIBR_BI_END
};

/* Prepare the module. */
int sys_ini(sh *s)
{
	hibr_set(s, "SYS_MOD", "1", 0);
	lg(HIBR_LINF, "sys module ready");
	return HIBR_OK;
}

/* Tear the module down. */
void sys_fin(sh *s)
{
	lg(HIBR_LINF, "sys module going away");
	(void)s;
}

HIBR_MODULE("sys", HIBR_VER, "drop, epoch, sleepms, state, upper", sys_bi,
	   sys_ini,
	   sys_fin);
