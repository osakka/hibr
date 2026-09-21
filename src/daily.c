#include "pri.h"
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

struct al { char *k, *v; };

/* Look up an alias body by name. */
const char *al_get(sh *s, const char *k)
{
	size_t i;
	struct al *a;

	for (i = 0; i < s->als.n; i++) {
		a = (struct al *)s->als.p[i];
		if (!strcmp(a->k, k))
			return a->v;
	}
	return 0;
}

/* True while this alias is already being expanded. */
int al_busy(sh *s, const char *k)
{
	size_t i;

	for (i = 0; i < s->axp.n; i++)
		if (!strcmp((char *)s->axp.p[i], k))
			return 1;
	return 0;
}

/* Define or replace an alias. */
void al_set(sh *s, const char *k, const char *v)
{
	size_t i;
	struct al *a;

	for (i = 0; i < s->als.n; i++) {
		a = (struct al *)s->als.p[i];
		if (!strcmp(a->k, k)) {
			free(a->v);
			a->v = xs(v);
			return;
		}
	}
	a = xm(sizeof *a);
	a->k = xs(k);
	a->v = xs(v);
	v_add(&s->als, a);
	lg(HIBR_LDBG, "alias %s defined", k);
}

/* Quote one argument so it survives being re-parsed. */
void al_quote(str *o, const char *a)
{
	const char *p;

	if (!*a || strchr(a, '\n')) {
		s_ch(o, '\'');
		for (p = a; *p; p++) {
			if (*p == '\'')
				s_cat(o, "'\\''");
			else
				s_ch(o, *p);
		}
		s_ch(o, '\'');
		return;
	}
	for (p = a; *p; p++) {
		if (!isalnum((unsigned char)*p) && !strchr("_+-%^=./:,@", *p))
			s_ch(o, '\\');
		s_ch(o, *p);
	}
}

/* Run an alias body with the original arguments appended. */
int al_run(sh *s, const char *body, int ac, char **av)
{
	str b;
	int i, st;

	s_init(&b);
	s_cat(&b, body);
	for (i = 1; i < ac; i++) {
		s_ch(&b, ' ');
		al_quote(&b, av[i]);
	}
	v_add(&s->axp, xs(av[0]));
	lg(HIBR_LTRC, "alias %s -> %s", av[0], b.p);
	st = hibr_run(s, b.p);
	free(s->axp.p[--s->axp.n]);
	s_free(&b);
	return st;
}

/* Define aliases, or list the ones already defined. */
int b_alias(sh *s, int ac, char **av)
{
	int i;
	size_t j;
	char *q;
	struct al *a;

	if (ac == 1) {
		for (j = 0; j < s->als.n; j++) {
			a = (struct al *)s->als.p[j];
			printf("alias %s='%s'\n", a->k, a->v);
		}
		return HIBR_OK;
	}
	for (i = 1; i < ac; i++) {
		q = strchr(av[i], '=');
		if (!q) {
			const char *v = al_get(s, av[i]);
			if (v)
				printf("alias %s='%s'\n", av[i], v);
			else
				lg(HIBR_LERR, "alias: %s: not found", av[i]);
			continue;
		}
		*q = 0;
		al_set(s, av[i], q + 1);
		*q = '=';
	}
	return HIBR_OK;
}

/* Remove aliases. */
int b_unalias(sh *s, int ac, char **av)
{
	int i;
	size_t j;
	struct al *a;

	for (i = 1; i < ac; i++)
		for (j = 0; j < s->als.n; j++) {
			a = (struct al *)s->als.p[j];
			if (strcmp(a->k, av[i]))
				continue;
			free(a->k);
			free(a->v);
			free(a);
			s->als.p[j] = s->als.p[s->als.n - 1];
			s->als.n--;
			break;
		}
	return HIBR_OK;
}

/* Release the alias table. */
void al_fini(sh *s)
{
	struct al *a;

	while (s->als.n) {
		a = (struct al *)s->als.p[--s->als.n];
		free(a->k);
		free(a->v);
		free(a);
	}
	v_free(&s->als);
	while (s->axp.n)
		free(s->axp.p[--s->axp.n]);
	v_free(&s->axp);
	while (s->dirs.n)
		free(s->dirs.p[--s->dirs.n]);
	v_free(&s->dirs);
}

/* Abbreviate a path with a tilde when it sits under home. */
char *dir_short(sh *s, const char *p)
{
	const char *h = hibr_get(s, "HOME");
	size_t n = h ? strlen(h) : 0;
	str b;

	s_init(&b);
	if (h && n && !strncmp(p, h, n) && (!p[n] || p[n] == '/')) {
		s_ch(&b, '~');
		s_cat(&b, p + n);
	} else {
		s_cat(&b, p);
	}
	return b.p;
}

/* Print the directory stack, newest first. */
int b_dirs(sh *s, int ac, char **av)
{
	char *c = cwd(), *t;
	size_t i = s->dirs.n;

	(void)ac;
	(void)av;
	if (c) {
		t = dir_short(s, c);
		printf("%s", t);
		free(t);
		free(c);
	}
	while (i--) {
		t = dir_short(s, (char *)s->dirs.p[i]);
		printf(" %s", t);
		free(t);
	}
	printf("\n");
	return HIBR_OK;
}

/* Change directory, remembering where we were. */
int b_pushd(sh *s, int ac, char **av)
{
	char *c;
	char *args[2];

	if (ac < 2) {
		if (!s->dirs.n) {
			lg(HIBR_LERR, "pushd: the stack is empty");
			return HIBR_FAIL;
		}
		return b_popd(s, 1, av);
	}
	c = cwd();
	args[0] = "cd";
	args[1] = av[1];
	if (b_cd(s, 2, args) != HIBR_OK) {
		free(c);
		return HIBR_FAIL;
	}
	if (c)
		v_add(&s->dirs, c);
	return b_dirs(s, 1, av);
}

/* Return to the directory on top of the stack. */
int b_popd(sh *s, int ac, char **av)
{
	char *d;
	char *args[2];
	int rc;

	(void)ac;
	if (!s->dirs.n) {
		lg(HIBR_LERR, "popd: the stack is empty");
		return HIBR_FAIL;
	}
	d = (char *)s->dirs.p[--s->dirs.n];
	args[0] = "cd";
	args[1] = d;
	rc = b_cd(s, 2, args);
	free(d);
	if (rc != HIBR_OK)
		return rc;
	return b_dirs(s, 1, av);
}

/* Expand one prompt escape, returning the bytes consumed. */
int pr_esc(sh *s, str *o, const char *p)
{
	char *c;
	time_t now;
	struct tm *tm;
	str h;

	switch (*p) {
	case 'u':
		c = getenv("USER");
		if (!c)
			c = getenv("LOGNAME");
		s_cat(o, c ? c : "user");
		return 1;
	case 'h':
	case 'H':
		s_init(&h);
		s_grow(&h, 256);
		if (gethostname(h.p, 256) == 0) {
			h.n = strlen(h.p);
			if (*p == 'h' && (c = strchr(h.p, '.')) != 0)
				*c = 0;
			s_cat(o, h.p);
		}
		s_free(&h);
		return 1;
	case 'w':
	case 'W':
		c = cwd();
		if (c) {
			char *t = *p == 'w' ? dir_short(s, c) : 0;
			if (t) {
				s_cat(o, t);
				free(t);
			} else {
				char *b = strrchr(c, '/');
				s_cat(o, b && b[1] ? b + 1 : c);
			}
			free(c);
		}
		return 1;
	case 's':
		s_cat(o, "hibr");
		return 1;
	case 'v':
		s_cat(o, HIBR_VER);
		return 1;
	case 'j':
		s_num(o, (long)s->jobs.n);
		return 1;
	case '$':
		s_ch(o, getuid() == 0 ? '#' : '$');
		return 1;
	case 'n':
		s_ch(o, '\n');
		return 1;
	case 'e':
		s_ch(o, 27);
		return 1;
	case 'a':
		s_ch(o, 7);
		return 1;
	case '\\':
		s_ch(o, '\\');
		return 1;
	case '[':
	case ']':
		return 1;
	case 't':
	case 'T':
	case '@':
	case 'd':
		now = time(0);
		tm = localtime(&now);
		s_grow(o, 64);
		if (*p == 't')
			o->n += (size_t)strftime(o->p + o->n, 64, "%H:%M:%S", tm);
		else if (*p == 'T')
			o->n += (size_t)strftime(o->p + o->n, 64, "%I:%M:%S", tm);
		else if (*p == '@')
			o->n += (size_t)strftime(o->p + o->n, 64, "%I:%M %p", tm);
		else
			o->n += (size_t)strftime(o->p + o->n, 64, "%a %b %d", tm);
		o->p[o->n] = 0;
		return 1;
	}
	s_ch(o, '\\');
	s_ch(o, *p);
	return 1;
}

/* Build a prompt: escapes first, then ordinary word expansion. */
char *pr_make(sh *s, const char *ps)
{
	str o;
	lex l;
	word *w;
	char *r;
	amark m;

	s_init(&o);
	for (; *ps; ps++) {
		if (*ps == '\\' && ps[1]) {
			ps += pr_esc(s, &o, ps + 1);
			continue;
		}
		s_ch(&o, *ps);
	}
	if (!strchr(o.p ? o.p : "", '$') && !strchr(o.p ? o.p : "", '`'))
		return o.p;
	m = ar_mark(s->xa);
	lx_init(&l, s, o.p);
	l.nb = 1;
	w = lx_word(&l);
	r = w ? xs(xone(s, w)) : xs(o.p);
	ar_rel(s->xa, m);
	s_free(&o);
	return r;
}

/* Run the configured prompt hook in process and take its result slot. */
char *pr_hook(sh *s)
{
	const char *nm = hibr_get(s, "PROMPT_FN");
	const hibr_bi *b = 0;
	node *f;
	char *av[2];
	char *r;
	int ost = s->st, ostop = s->stop, oin = s->intry, ob = s->bind;
	int oxe = s->xerr, obrk = s->brk, ocont = s->cont, oret = s->ret;
	str n;

	if (!nm || !*nm)
		return 0;
	f = fn_find(s, nm);
	if (!f) {
		b = m_find(s, nm);
		if (!b)
			b = bi_find(nm);
	}
	if (!f && !b) {
		lg(HIBR_LWRN, "PROMPT_FN: %s: not found", nm);
		return 0;
	}
	s_init(&n);
	s_num(&n, (long)ost);
	hibr_set(s, "STATUS", n.p, 0);
	s_free(&n);
	hibr_set(s, "RET", "", 0);
	s->bind = f ? 0 : 1;
	s->intry = 1;
	s->brk = 0;
	s->cont = 0;
	av[0] = (char *)nm;
	av[1] = 0;
	if (f)
		fn_call(s, f, 1, av);
	else
		b->fn(s, 1, av);
	r = xs(hibr_get(s, "RET") ? hibr_get(s, "RET") : "");
	s->bind = ob;
	s->intry = oin;
	s->stop = ostop;
	s->st = ost;
	s->xerr = oxe;
	s->brk = obrk;
	s->cont = ocont;
	s->ret = oret;
	lg(HIBR_LDBG, "prompt hook %s produced %lu bytes", nm,
	   (unsigned long)strlen(r));
	return r;
}

/* Read the startup file when the shell is interactive. */
void rc_load(sh *s)
{
	const char *p = hibr_get(s, "HIBR_RC");
	const char *home;
	str b;
	char *args[2];

	s_init(&b);
	if (p && *p) {
		s_cat(&b, p);
	} else {
		home = hibr_get(s, "HOME");
		if (!home || !*home)
			return;
		s_cat(&b, home);
		s_cat(&b, "/.hibrc");
	}
	if (access(b.p, R_OK) != 0) {
		lg(HIBR_LDBG, "no startup file at %s", b.p);
		s_free(&b);
		return;
	}
	args[0] = "source";
	args[1] = b.p;
	lg(HIBR_LINF, "reading %s", b.p);
	b_src(s, 2, args);
	s_free(&b);
}

/* Options that are not negotiable, and why is in the ADRs. */
int sh_optfix(const char *nm)
{
	if (!strcmp(nm, "extglob") || !strcmp(nm, "globstar") ||
	    !strcmp(nm, "expand_aliases") || !strcmp(nm, "sourcepath"))
		return O_FIXON;
	if (!strcmp(nm, "pipefail"))
		return O_FIXOFF;
	return 0;
}

/* The bit a globbing option occupies, or zero when it is not one. */
unsigned sh_optbit(const char *nm)
{
	if (!strcmp(nm, "nullglob"))
		return O_NULLGLOB;
	if (!strcmp(nm, "nocaseglob"))
		return O_NOCASEGLOB;
	if (!strcmp(nm, "dotglob"))
		return O_DOTGLOB;
	if (!strcmp(nm, "failglob"))
		return O_FAILGLOB;
	if (!strcmp(nm, "nocasematch"))
		return O_NOCASEMATCH;
	return 0;
}

/* The shell flag an option controls, or null when it is not one. */
int *sh_optflag(sh *s, const char *nm)
{
	if (!strcmp(nm, "errexit"))
		return &s->errx;
	if (!strcmp(nm, "nounset"))
		return &s->uset;
	if (!strcmp(nm, "xtrace"))
		return &s->xtr;
	if (!strcmp(nm, "noclobber"))
		return &s->noclob;
	if (!strcmp(nm, "noexec"))
		return &s->noexec;
	if (!strcmp(nm, "histexpand"))
		return &s->hx;
	if (!strcmp(nm, "strict"))
		return &s->strict;
	return 0;
}

/* Every option name, in one list, whatever spelling asked for it. */
const char *sh_optnames[] = { "errexit", "nounset", "xtrace", "noclobber",
			      "noexec", "histexpand", "strict", "nullglob",
			      "nocaseglob", "dotglob", "failglob",
			      "nocasematch", "extglob", "globstar",
			      "expand_aliases", "pipefail", 0 };

/* Read one option by name, or -1 when there is no such option. */
int sh_optget(sh *s, const char *nm)
{
	int *f = sh_optflag(s, nm);
	unsigned b;

	if (f)
		return *f != 0;
	b = sh_optbit(nm);
	if (b)
		return (s->sopt & b) != 0;
	if (sh_optfix(nm) == O_FIXON)
		return 1;
	if (sh_optfix(nm) == O_FIXOFF)
		return 0;
	return -1;
}

/* Set one option by name, refusing the ones that do not move. */
int sh_optset(sh *s, const char *nm, int on)
{
	int *f = sh_optflag(s, nm);
	int fix = sh_optfix(nm);
	unsigned b;

	if (fix) {
		if ((fix == O_FIXON) == (on != 0)) {
			lg(HIBR_LDBG, "%s is always %s here", nm,
			   fix == O_FIXON ? "on" : "off");
			return HIBR_OK;
		}
		lg(HIBR_LERR, "%s is always %s in hibr and cannot be changed",
		   nm, fix == O_FIXON ? "on" : "off");
		return HIBR_FAIL;
	}
	if (f) {
		*f = on;
		return HIBR_OK;
	}
	b = sh_optbit(nm);
	if (!b) {
		lg(HIBR_LERR, "%s: no such option", nm);
		return HIBR_FAIL;
	}
	if (on)
		s->sopt |= b;
	else
		s->sopt &= ~b;
	return HIBR_OK;
}

/* Print every option and whether it is on. */
void sh_optlist(sh *s, int setstyle)
{
	int i, v;

	for (i = 0; sh_optnames[i]; i++) {
		v = sh_optget(s, sh_optnames[i]);
		if (setstyle)
			printf("%-16s%s\n", sh_optnames[i], v ? "on" : "off");
		else
			printf("%-16s%s\n", sh_optnames[i], v ? "on" : "off");
	}
}

/* One namespace of options, reached by shopt or by set -o alike. */
int b_shopt(sh *s, int ac, char **av)
{
	int i = 1, on = -1, quiet = 0, n = 0, v;

	for (; i < ac && av[i][0] == '-' && av[i][1]; i++) {
		if (!strcmp(av[i], "-s"))
			on = 1;
		else if (!strcmp(av[i], "-u"))
			on = 0;
		else if (!strcmp(av[i], "-q"))
			quiet = 1;
		else if (!strcmp(av[i], "-o"))
			continue;
		else {
			lg(HIBR_LERR, "shopt: %s: unknown option", av[i]);
			return 2;
		}
	}
	if (i >= ac) {
		if (on < 0)
			sh_optlist(s, 0);
		return HIBR_OK;
	}
	for (; i < ac; i++) {
		if (on < 0) {
			v = sh_optget(s, av[i]);
			if (v < 0) {
				lg(HIBR_LERR, "shopt: %s: no such option",
				   av[i]);
				n = 1;
				continue;
			}
			if (!quiet)
				printf("%-16s%s\n", av[i], v ? "on" : "off");
			if (!v)
				n = 1;
			continue;
		}
		if (sh_optset(s, av[i], on) != HIBR_OK)
			n = 1;
	}
	return n ? HIBR_FAIL : HIBR_OK;
}

/* Run a command, ignoring any function or alias of the same name. */
int b_command(sh *s, int ac, char **av)
{
	const hibr_bi *b;
	char *path, **env;
	pid_t pid;
	int w;

	if (ac < 2) {
		lg(HIBR_LERR, "usage: command name [args...]");
		return 2;
	}
	b = m_find(s, av[1]);
	if (!b)
		b = bi_find(av[1]);
	if (b) {
		char **oam = s->amask;
		int st;
		s->amask = oam ? oam + 1 : 0;
		st = b->fn(s, ac - 1, av + 1);
		s->amask = oam;
		return st;
	}
	path = findx(s, av[1]);
	if (!path) {
		lg(HIBR_LERR, "command: %s: not found", av[1]);
		return HIBR_NOCMD;
	}
	env = v_envp(s, 0);
	fflush(0);
	pid = fork();
	if (pid == 0) {
		execve(path, av + 1, env);
		_exit(HIBR_NOEXEC);
	}
	free(path);
	waitpid(pid, &w, 0);
	return WIFEXITED(w) ? WEXITSTATUS(w) : HIBR_FAIL;
}

/* Run a builtin, ignoring functions, aliases and modules. */
int b_builtin(sh *s, int ac, char **av)
{
	const hibr_bi *b;

	if (ac < 2) {
		lg(HIBR_LERR, "usage: builtin name [args...]");
		return 2;
	}
	b = bi_find(av[1]);
	if (!b) {
		lg(HIBR_LERR, "builtin: %s: not a builtin", av[1]);
		return HIBR_NOCMD;
	}
	return b->fn(s, ac - 1, av + 1);
}

/* Show or set the file creation mask. */
int b_umask(sh *s, int ac, char **av)
{
	mode_t m;

	(void)s;
	if (ac < 2) {
		m = umask(0);
		umask(m);
		printf("%04o\n", (unsigned)m);
		return HIBR_OK;
	}
	umask((mode_t)strtol(av[1], 0, 8));
	return HIBR_OK;
}

/* Time a command and report how long it took. */
int b_time(sh *s, int ac, char **av)
{
	struct timeval a, b;
	struct rusage r0, r1;
	str cmd;
	int i, st;

	if (ac < 2) {
		lg(HIBR_LERR, "usage: time command [args...]");
		return 2;
	}
	s_init(&cmd);
	for (i = 1; i < ac; i++) {
		if (cmd.n)
			s_ch(&cmd, ' ');
		al_quote(&cmd, av[i]);
	}
	gettimeofday(&a, 0);
	getrusage(RUSAGE_CHILDREN, &r0);
	hibr_run(s, cmd.p);
	st = s->st;
	getrusage(RUSAGE_CHILDREN, &r1);
	gettimeofday(&b, 0);
	fprintf(stderr, "real %.3fs  user %.3fs  sys %.3fs\n",
		(double)(b.tv_sec - a.tv_sec) +
			(double)(b.tv_usec - a.tv_usec) / 1e6,
		(double)(r1.ru_utime.tv_sec - r0.ru_utime.tv_sec) +
			(double)(r1.ru_utime.tv_usec - r0.ru_utime.tv_usec) / 1e6,
		(double)(r1.ru_stime.tv_sec - r0.ru_stime.tv_sec) +
			(double)(r1.ru_stime.tv_usec - r0.ru_stime.tv_usec) / 1e6);
	s_free(&cmd);
	return s->st = st;
}

/* Parse option letters out of the positional parameters. */
int b_getopts(sh *s, int ac, char **av)
{
	const char *spec = ac > 1 ? av[1] : "";
	const char *nm = ac > 2 ? av[2] : "opt";
	const char *oi = hibr_get(s, "OPTIND");
	int idx = oi && *oi ? atoi(oi) : 1;
	char **args = ac > 3 ? av + 3 : s->av;
	int n = ac > 3 ? ac - 3 : s->ac;
	const char *cur;
	const char *hit;
	str one, nx;

	if (ac < 2) {
		lg(HIBR_LERR, "usage: getopts optstring var [args...]");
		return 2;
	}
	if (idx > n)
		return HIBR_FAIL;
	cur = args[idx - 1];
	if (!cur || cur[0] != '-' || !cur[1] || !strcmp(cur, "--"))
		return HIBR_FAIL;
	hit = strchr(spec, cur[1]);
	s_init(&one);
	s_ch(&one, cur[1]);
	if (!hit) {
		hibr_set(s, nm, "?", 0);
		lg(HIBR_LWRN, "getopts: illegal option -%s", one.p);
		s_free(&one);
		idx++;
	} else {
		hibr_set(s, nm, one.p, 0);
		s_free(&one);
		idx++;
		if (hit[1] == ':') {
			if (idx > n) {
				hibr_set(s, nm, ":", 0);
			} else {
				hibr_set(s, "OPTARG", args[idx - 1], 0);
				idx++;
			}
		}
	}
	s_init(&nx);
	s_num(&nx, (long)idx);
	hibr_set(s, "OPTIND", nx.p, 0);
	s_free(&nx);
	return HIBR_OK;
}
