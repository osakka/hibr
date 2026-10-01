#include "pri.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

int pl_fd = -1;
char *pl_tmp;
long pl_owner;

/* Turn the dry run on: a private copy of stderr for the record, a scratch TMPDIR, no terminal on standard input. */
void pl_init(sh *s)
{
	const char *base = getenv("TMPDIR");
	char *real;
	str d;
	int fd;

	s->sopt |= O_PLAN;
	pl_owner = (long)getpid();
	pl_fd = fcntl(2, F_DUPFD_CLOEXEC, 250);
	s_init(&d);
	s_cat(&d, base && *base ? base : "/tmp");
	s_cat(&d, "/hibr-plan-XXXXXX");
	if (mkdtemp(d.p)) {
		real = realpath(d.p, 0);
		pl_tmp = real ? real : xs(d.p);
		hibr_set(s, "TMPDIR", pl_tmp, 1);
		setenv("TMPDIR", pl_tmp, 1);
		lg(HIBR_LDBG, "plan: scratch TMPDIR %s", pl_tmp);
	} else
		lg(HIBR_LWRN, "plan: no scratch TMPDIR: %s", strerror(errno));
	s_free(&d);
	if (isatty(0) && (fd = open("/dev/null", O_RDONLY)) >= 0) {
		dup2(fd, 0);
		close(fd);
	}
}

/* Remove a directory tree, not following symbolic links. */
void pl_rm(const char *p)
{
	DIR *d;
	struct dirent *e;
	struct stat st;
	str c;

	if (lstat(p, &st) != 0)
		return;
	if (!S_ISDIR(st.st_mode)) {
		unlink(p);
		return;
	}
	d = opendir(p);
	if (d) {
		s_init(&c);
		while ((e = readdir(d))) {
			if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))
				continue;
			c.n = 0;
			s_cat(&c, p);
			s_ch(&c, '/');
			s_cat(&c, e->d_name);
			pl_rm(c.p);
		}
		s_free(&c);
		closedir(d);
	}
	rmdir(p);
}

/* Remove the scratch TMPDIR, in the process that made it. */
void pl_fini(void)
{
	if (pl_tmp && (long)getpid() == pl_owner)
		pl_rm(pl_tmp);
	free(pl_tmp);
	pl_tmp = 0;
}

/* A record's text with the scratch TMPDIR written as $TMPDIR, so it reads the same on every run. */
char *pl_tidy(char *m)
{
	size_t n = pl_tmp ? strlen(pl_tmp) : 0;
	char *p, *q = m;
	str o;

	if (!n || !strstr(m, pl_tmp))
		return m;
	s_init(&o);
	while ((p = strstr(q, pl_tmp))) {
		s_add(&o, q, (size_t)(p - q));
		s_cat(&o, "$TMPDIR");
		q = p + n;
	}
	s_cat(&o, q);
	free(m);
	return o.p;
}

/* Record one thing the plan did not do: a line of JSON in agent mode, file:line otherwise, on the private stderr. */
void pl_note(sh *s, const char *f, ...)
{
	va_list ap;
	str o;
	char *m;
	int n, fd = pl_fd >= 0 && fcntl(pl_fd, F_GETFD) != -1 ? pl_fd : 2;

	va_start(ap, f);
	n = vsnprintf(0, 0, f, ap);
	va_end(ap);
	m = xm((size_t)(n < 0 ? 0 : n) + 1);
	va_start(ap, f);
	vsnprintf(m, (size_t)(n < 0 ? 0 : n) + 1, f, ap);
	va_end(ap);
	fflush(stdout);
	m = pl_tidy(m);
	s_init(&o);
	if (s->sopt & O_AGENT)
		lg_jline(&o, "plan", m);
	else {
		s_cat(&o, "hibr: plan: ");
		s_cat(&o, s->src ? s->src : "command line");
		s_ch(&o, ':');
		s_num(&o, (long)s->ln);
		s_cat(&o, ": ");
		s_cat(&o, m);
		s_ch(&o, '\n');
	}
	if (write(fd, o.p, o.n) < 0)
		lg(HIBR_LDBG, "plan: record lost: %s", strerror(errno));
	s_free(&o);
	free(m);
}

/* Whether a path lies inside the plan's scratch TMPDIR, where writes are real. */
int pl_scratch(const char *t)
{
	char *cwd, *cut, *real;
	size_t n;
	int in;
	str p;

	if (!pl_tmp || !*t)
		return 0;
	s_init(&p);
	if (t[0] != '/') {
		cwd = getcwd(0, 0);
		if (!cwd)
			return 0;
		s_cat(&p, cwd);
		s_ch(&p, '/');
		free(cwd);
	}
	s_cat(&p, t);
	cut = strrchr(p.p, '/');
	if (cut == p.p)
		cut[1] = 0;
	else
		*cut = 0;
	real = realpath(p.p, 0);
	s_free(&p);
	if (!real)
		return 0;
	n = strlen(pl_tmp);
	in = !strncmp(real, pl_tmp, n) && (real[n] == 0 || real[n] == '/');
	free(real);
	return in;
}

/* Whether a redirection must be refused, recording it: a write outside the scratch, or any network endpoint. */
int pl_redir(sh *s, int k, const char *t)
{
	if (net_is(s, t)) {
		pl_note(s, "would connect to %s", t);
		return 1;
	}
	if (k == R_IN)
		return 0;
	if (!strcmp(t, "/dev/null") || !strcmp(t, "/dev/stdout") ||
	    !strcmp(t, "/dev/stderr") || !strncmp(t, "/dev/fd/", 8) ||
	    pl_scratch(t))
		return 0;
	pl_note(s, k == R_APP ? "would append to %s" :
		   k == R_RW ? "would open %s for writing" : "would write %s", t);
	return 1;
}

/* Whether a name is in a null-ended list. */
int pl_in(const char *nm, const char **l)
{
	for (; *l; l++)
		if (!strcmp(nm, *l))
			return 1;
	return 0;
}

/* Whether a program and its arguments only read, as far as the plan can tell. */
int pl_safe(char **av)
{
	static const char *ro[] = { "cat", "head", "tail", "wc", "grep",
		"egrep", "fgrep", "cut", "tr", "comm", "diff", "cmp", "ls",
		"stat", "file", "du", "df", "date", "basename", "dirname",
		"realpath", "readlink", "pwd", "id", "whoami", "groups",
		"uname", "hostname", "printenv", "which", "test", "[", "true",
		"false", "echo", "printf", "seq", "expr", "nproc", "tty",
		"ps", "pgrep", "free", "uptime", "getent", "sha256sum",
		"sha1sum", "sha512sum", "md5sum", "b2sum", "cksum", "base64",
		"od", "hexdump", "column", "fold", "fmt", "nl", "paste",
		"join", "rev", "tac", "expand", "unexpand", "strings",
		"numfmt", "locale", "jq", "sleep", "tput", 0 };
	static const char *fsw[] = { "rm", "rmdir", "mkdir", "touch", "cp",
		"mv", "ln", "chmod", 0 };
	static const char *findw[] = { "-delete", "-exec", "-execdir", "-ok",
		"-okdir", "-fprint", "-fprint0", "-fprintf", "-fls", 0 };
	static const char *gitr[] = { "status", "log", "diff", "show",
		"rev-parse", "ls-files", "describe", "blame", "shortlog",
		"cat-file", "grep", "rev-list", "merge-base", 0 };
	const char *nm = strrchr(av[0], '/') ? strrchr(av[0], '/') + 1 : av[0];
	int i, plain = 0;

	if (!strcmp(nm, "env"))
		return av[1] == 0;
	if (pl_in(nm, fsw)) {
		for (i = 1; av[i]; i++)
			if (av[i][0] != '-' && ++plain && !pl_scratch(av[i]))
				return 0;
		return plain > 0;
	}
	if (pl_in(nm, ro))
		return 1;
	if (!strcmp(nm, "sed")) {
		for (i = 1; av[i]; i++)
			if (!strncmp(av[i], "-i", 2) || !strncmp(av[i], "--in-place", 10) ||
			    (av[i][0] != '-' && (strstr(av[i], "w ") || strstr(av[i], "/w"))))
				return 0;
		return 1;
	}
	if (!strcmp(nm, "sort")) {
		for (i = 1; av[i]; i++)
			if (!strncmp(av[i], "-o", 2) || !strncmp(av[i], "--output", 8))
				return 0;
		return 1;
	}
	if (!strcmp(nm, "uniq") || !strcmp(nm, "xxd")) {
		for (i = 1; av[i]; i++)
			if (av[i][0] != '-' && ++plain > 1)
				return 0;
		return 1;
	}
	if (!strcmp(nm, "tee")) {
		for (i = 1; av[i]; i++)
			if (av[i][0] != '-' && !pl_scratch(av[i]))
				return 0;
		return 1;
	}
	if (!strcmp(nm, "find")) {
		for (i = 1; av[i]; i++)
			if (pl_in(av[i], findw))
				return 0;
		return 1;
	}
	if (!strcmp(nm, "mktemp")) {
		for (i = 1; av[i]; i++)
			if (strchr(av[i], '/') && !pl_scratch(av[i]))
				return 0;
		return pl_tmp != 0;
	}
	if (!strcmp(nm, "git")) {
		for (i = 1; av[i] && av[i][0] == '-'; i++)
			if (!strcmp(av[i], "-C") || !strcmp(av[i], "-c"))
				i++;
		if (!av[i])
			return 0;
		if (pl_in(av[i], gitr))
			return 1;
		if (!strcmp(av[i], "branch") || !strcmp(av[i], "tag") ||
		    !strcmp(av[i], "remote")) {
			for (i++; av[i]; i++)
				if (strcmp(av[i], "-a") && strcmp(av[i], "-r") &&
				    strcmp(av[i], "-v") && strcmp(av[i], "-l") &&
				    strcmp(av[i], "--list"))
					return 0;
			return 1;
		}
		if (!strcmp(av[i], "config")) {
			for (i++; av[i]; i++)
				if (!strncmp(av[i], "--get", 5) || !strcmp(av[i], "--list") ||
				    !strcmp(av[i], "-l"))
					return 1;
			return 0;
		}
		return 0;
	}
	return 0;
}

/* Whether to run a program, recording it when the plan will not: only what is known to read and nothing more. */
int pl_prog(sh *s, char **av)
{
	str c;
	int i;

	if (pl_safe(av))
		return 1;
	s_init(&c);
	for (i = 0; av[i]; i++) {
		if (i)
			s_ch(&c, ' ');
		s_cat(&c, av[i]);
	}
	pl_note(s, "would run %s", c.p);
	s_free(&c);
	return 0;
}
