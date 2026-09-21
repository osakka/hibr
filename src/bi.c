#include "pri.h"
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <termios.h>
#include <sys/select.h>
#include <unistd.h>

extern const hibr_bi bitab[];

/* Return the working directory in freshly allocated memory. */
char *cwd(void)
{
	size_t n = 64;
	char *p = xm(n);

	while (!getcwd(p, n)) {
		if (errno != ERANGE) {
			free(p);
			return 0;
		}
		n *= 2;
		p = xr(p, n);
	}
	return p;
}

/* Change the working directory. */
int b_cd(sh *s, int ac, char **av)
{
	const char *t = ac > 1 ? av[1] : hibr_get(s, "HOME");
	char *old = cwd();
	char *now;

	if (!t)
		t = "/";
	if (ac > 1 && !strcmp(av[1], "-")) {
		t = hibr_get(s, "OLDPWD");
		if (!t) {
			lg(HIBR_LERR, "cd: OLDPWD not set");
			free(old);
			return HIBR_FAIL;
		}
		puts(t);
	}
	{
		const char *cp = hibr_get(s, "CDPATH");
		int done = 0;
		if (cp && *cp && t[0] != '/' && t[0] != '.') {
			while (*cp && !done) {
				const char *e = strchr(cp, ':');
				size_t n = e ? (size_t)(e - cp) : strlen(cp);
				str d;
				s_init(&d);
				s_add(&d, n ? cp : ".", n ? n : 1);
				s_ch(&d, '/');
				s_cat(&d, t);
				if (chdir(d.p) == 0) {
					done = 1;
					if (n && !(n == 1 && cp[0] == '.'))
						puts(d.p);
				}
				s_free(&d);
				if (!e)
					break;
				cp = e + 1;
			}
		}
		if (!done && chdir(t) < 0) {
			lg(HIBR_LERR, "cd: %s: %s", t, strerror(errno));
			free(old);
			return HIBR_FAIL;
		}
	}
	now = cwd();
	if (old)
		hibr_set(s, "OLDPWD", old, 1);
	if (now)
		hibr_set(s, "PWD", now, 1);
	lg(HIBR_LDBG, "cwd is now %s", now ? now : "?");
	free(old);
	free(now);
	return HIBR_OK;
}

/* Print the working directory. */
int b_pwd(sh *s, int ac, char **av)
{
	char *p = cwd();

	(void)s;
	(void)ac;
	(void)av;
	if (!p) {
		lg(HIBR_LERR, "pwd: %s", strerror(errno));
		return HIBR_FAIL;
	}
	puts(p);
	free(p);
	return HIBR_OK;
}

/* Write arguments to standard output. */
int b_echo(sh *s, int ac, char **av)
{
	int i = 1, nl = 1, esc = 0;
	str o;

	(void)s;
	for (; i < ac && av[i][0] == '-' && av[i][1]; i++) {
		const char *f = av[i] + 1;
		for (; *f && strchr("neE", *f); f++)
			;
		if (*f)
			break;
		for (f = av[i] + 1; *f; f++) {
			if (*f == 'n')
				nl = 0;
			else if (*f == 'e')
				esc = 1;
			else
				esc = 0;
		}
	}
	for (; i < ac; i++) {
		if (esc) {
			s_init(&o);
			pf_esc(&o, av[i], 1);
			fwrite(o.p ? o.p : "", 1, o.n, stdout);
			s_free(&o);
		} else {
			fputs(av[i], stdout);
		}
		if (i + 1 < ac)
			fputc(' ', stdout);
	}
	if (nl)
		fputc('\n', stdout);
	return HIBR_OK;
}

/* Mark variables for export or list the exported set. */
int b_exp(sh *s, int ac, char **av)
{
	int i;
	char *q;
	var *v;
	size_t j;

	if (ac == 1) {
		for (j = 0; j < s->tsz; j++)
			for (v = s->tab[j]; v; v = v->nx)
				if (v->ex)
					printf("export %s=%s\n", v->k, v->v);
		return HIBR_OK;
	}
	for (i = 1; i < ac; i++) {
		q = strchr(av[i], '=');
		if (q) {
			*q = 0;
			hibr_set(s, av[i], q + 1, 1);
			*q = '=';
			continue;
		}
		v = v_find(s, av[i]);
		if (v)
			v->ex = 1;
		else
			hibr_set(s, av[i], "", 1);
	}
	return HIBR_OK;
}

/* Remove variables or functions. */
int b_unset(sh *s, int ac, char **av)
{
	int i;
	size_t j;
	node *f;

	for (i = 1; i < ac; i++) {
		char *br = strchr(av[i], '[');
		if (br) {
			vec *ks = vb_get(s);
			char *r;
			*br = 0;
			for (r = br + 1; r && *r;) {
				char *end = strchr(r, ']');
				if (!end)
					break;
				*end = 0;
				v_add(ks, xkey(s, r));
				r = end + 1;
				if (*r == '[')
					r++;
				else
					break;
			}
			v_delp(s, av[i], (char **)ks->p, (int)ks->n);
			vb_put(s, ks);
			*br = '[';
			continue;
		}
		v_del(s, av[i]);
		for (j = 0; j < s->fns.n; j++) {
			f = (node *)s->fns.p[j];
			if (!strcmp(f->s, av[i])) {
				s->fns.p[j] = s->fns.p[s->fns.n - 1];
				s->fns.n--;
				break;
			}
		}
	}
	return HIBR_OK;
}

/* Leave the shell. */
int b_exit(sh *s, int ac, char **av)
{
	s->quit = 1;
	s->st = ac > 1 ? atoi(av[1]) : s->st;
	return s->st;
}

/* Place a function's result in the result slot and return. */
int b_ret(sh *s, int ac, char **av)
{
	const char *v = ac > 1 ? av[1] : "";
	vec *o;
	int i;

	if (s->rty && *s->rty && !ty_ok(s->rty, v)) {
		lg(HIBR_LERR, "ret: declared %s, got '%s'", s->rty, v);
		s->ret = 1;
		return s->st = 2;
	}
	if (ac > 2) {
		o = vb_get(s);
		for (i = 1; i < ac; i++)
			v_add(o, av[i]);
		v_arr(s, "RET", o);
		vb_put(s, o);
	} else {
		hibr_set(s, "RET", v, 0);
	}
	lg(HIBR_LTRC, "ret slot holds %d value(s)", ac - 1);
	s->ret = 1;
	return s->st = HIBR_OK;
}

/* Return from a function or sourced file. */
int b_retf(sh *s, int ac, char **av)
{
	s->ret = 1;
	s->st = ac > 1 ? atoi(av[1]) : s->st;
	return s->st;
}

/* Leave one or more enclosing loops. */
int b_brk(sh *s, int ac, char **av)
{
	s->brk = ac > 1 ? atoi(av[1]) : 1;
	if (s->brk < 1)
		s->brk = 1;
	return HIBR_OK;
}

/* Continue one or more enclosing loops. */
int b_cont(sh *s, int ac, char **av)
{
	s->cont = ac > 1 ? atoi(av[1]) : 1;
	if (s->cont < 1)
		s->cont = 1;
	return HIBR_OK;
}

/* Drop leading positional parameters. */
int b_shift(sh *s, int ac, char **av)
{
	int n = ac > 1 ? atoi(av[1]) : 1;

	if (n < 0 || n > s->ac) {
		lg(HIBR_LERR, "shift: %d: out of range", n);
		return HIBR_FAIL;
	}
	v_pos(s, s->ac - n, s->av + n);
	return HIBR_OK;
}

/* Set shell options and positional parameters. */
int b_set(sh *s, int ac, char **av)
{
	int i = 1;

	for (; i < ac; i++) {
		if (!strcmp(av[i], "--")) {
			i++;
			break;
		}
		if (av[i][0] != '-' && av[i][0] != '+')
			break;
		if (!strcmp(av[i], "-x")) {
			s->xtr = 1;
		} else if (!strcmp(av[i], "+x")) {
			s->xtr = 0;
		} else if (!strcmp(av[i], "-H")) {
			s->hx = 1;
		} else if (!strcmp(av[i], "+H")) {
			s->hx = 0;
		} else if (!strcmp(av[i], "-S")) {
			s->strict = 1;
			lg(HIBR_LDBG, "strict expansion on");
		} else if (!strcmp(av[i], "+S")) {
			s->strict = 0;
		} else if (!strcmp(av[i], "-C")) {
			s->noclob = 1;
		} else if (!strcmp(av[i], "+C")) {
			s->noclob = 0;
		} else if (!strcmp(av[i], "-u")) {
			s->uset = 1;
		} else if (!strcmp(av[i], "+u")) {
			s->uset = 0;
		} else if (!strcmp(av[i], "-e")) {
			s->errx = 1;
		} else if (!strcmp(av[i], "+e")) {
			s->errx = 0;
		} else if (!strcmp(av[i], "-d") && i + 1 < ac) {
			hibr_lv = atoi(av[++i]);
			lg(HIBR_LINF, "log level %d", hibr_lv);
		} else {
			lg(HIBR_LERR, "set: %s: unknown option", av[i]);
			return HIBR_FAIL;
		}
	}
	if (i < ac || (ac > 1 && !strcmp(av[ac - 1], "--")))
		v_pos(s, ac - i, av + i);
	return HIBR_OK;
}

/* Read a line from standard input into variables. */
int b_read(sh *s, int ac, char **av)
{
	str b;
	char c;
	ssize_t n;
	int i = 1, want = 0, silent = 0, fd = 0;
	long tmo = 0;
	const char *ifs = hibr_get(s, "IFS");
	size_t p = 0, st;
	struct termios sv, raw;
	fd_set rs;
	struct timeval tv;

	if (!ifs)
		ifs = " \t\n";
	for (; i < ac && av[i][0] == '-' && av[i][1]; i++) {
		if (!strcmp(av[i], "-r")) {
			continue;
		} else if (!strcmp(av[i], "-s")) {
			silent = 1;
		} else if (!strcmp(av[i], "-p") && i + 1 < ac) {
			fputs(av[++i], stderr);
			fflush(stderr);
		} else if (!strcmp(av[i], "-n") && i + 1 < ac) {
			want = atoi(av[++i]);
		} else if (!strcmp(av[i], "-t") && i + 1 < ac) {
			tmo = atol(av[++i]);
		} else if (!strcmp(av[i], "-u") && i + 1 < ac) {
			fd = atoi(av[++i]);
		} else {
			lg(HIBR_LERR, "read: %s: unknown option", av[i]);
			return 2;
		}
	}
	if (tmo > 0) {
		FD_ZERO(&rs);
		FD_SET(fd, &rs);
		tv.tv_sec = tmo;
		tv.tv_usec = 0;
		if (select(fd + 1, &rs, 0, 0, &tv) <= 0) {
			lg(HIBR_LDBG, "read timed out after %ld s", tmo);
			return HIBR_FAIL;
		}
	}
	if (silent && tcgetattr(fd, &sv) == 0) {
		raw = sv;
		raw.c_lflag &= ~(unsigned)ECHO;
		tcsetattr(fd, TCSADRAIN, &raw);
	} else {
		silent = 0;
	}
	s_init(&b);
	while ((n = read(fd, &c, 1)) == 1) {
		if (!want && c == '\n')
			break;
		s_ch(&b, c);
		if (want && (int)b.n >= want)
			break;
	}
	if (silent) {
		tcsetattr(fd, TCSADRAIN, &sv);
		fputc('\n', stderr);
	}
	if (n <= 0 && !b.n) {
		s_free(&b);
		return HIBR_FAIL;
	}
	if (i >= ac) {
		hibr_set(s, "REPLY", b.p ? b.p : "", 0);
		s_free(&b);
		return HIBR_OK;
	}
	for (; i < ac; i++) {
		while (p < b.n && strchr(ifs, b.p[p]))
			p++;
		st = p;
		if (i == ac - 1) {
			p = b.n;
			while (p > st && strchr(ifs, b.p[p - 1]))
				p--;
		} else {
			while (p < b.n && !strchr(ifs, b.p[p]))
				p++;
		}
		{
			char svc = b.p ? b.p[p] : 0;
			if (b.p)
				b.p[p] = 0;
			hibr_set(s, av[i], b.p ? b.p + st : "", 0);
			if (b.p)
				b.p[p] = svc;
		}
	}
	s_free(&b);
	return HIBR_OK;
}

/* Evaluate arithmetic expressions for their effect. */
int b_let(sh *s, int ac, char **av)
{
	long v = 0;
	int i;

	if (ac < 2) {
		lg(HIBR_LERR, "usage: let expression...");
		return 2;
	}
	for (i = 1; i < ac; i++)
		v = ax_run(s, av[i]);
	return v ? HIBR_OK : HIBR_FAIL;
}

/* Always succeed. */
int b_true(sh *s, int ac, char **av)
{
	(void)s;
	(void)ac;
	(void)av;
	return HIBR_OK;
}

/* Always fail. */
int b_false(sh *s, int ac, char **av)
{
	(void)s;
	(void)ac;
	(void)av;
	return HIBR_FAIL;
}

/* Evaluate a unary test operator. */
int t_one(const char *op, const char *a)
{
	struct stat st;

	if (op[0] != '-' || !op[1] || op[2])
		return -1;
	switch (op[1]) {
	case 'z':
		return *a == 0;
	case 'n':
		return *a != 0;
	case 'e':
		return stat(a, &st) == 0;
	case 'f':
		return stat(a, &st) == 0 && S_ISREG(st.st_mode);
	case 'd':
		return stat(a, &st) == 0 && S_ISDIR(st.st_mode);
	case 'r':
		return access(a, R_OK) == 0;
	case 'w':
		return access(a, W_OK) == 0;
	case 'x':
		return access(a, X_OK) == 0;
	case 's':
		return stat(a, &st) == 0 && st.st_size > 0;
	case 'L':
	case 'h':
		return lstat(a, &st) == 0 && S_ISLNK(st.st_mode);
	}
	return -1;
}

/* Evaluate a binary test operator. */
int t_two(const char *a, const char *op, const char *b)
{
	long x, y;

	if (!strcmp(op, "="))
		return !strcmp(a, b);
	if (!strcmp(op, "!="))
		return strcmp(a, b) != 0;
	if (!strcmp(op, "<"))
		return strcmp(a, b) < 0;
	if (!strcmp(op, ">"))
		return strcmp(a, b) > 0;
	x = strtol(a, 0, 10);
	y = strtol(b, 0, 10);
	if (!strcmp(op, "-eq"))
		return x == y;
	if (!strcmp(op, "-ne"))
		return x != y;
	if (!strcmp(op, "-lt"))
		return x < y;
	if (!strcmp(op, "-le"))
		return x <= y;
	if (!strcmp(op, "-gt"))
		return x > y;
	if (!strcmp(op, "-ge"))
		return x >= y;
	return -1;
}

/* Evaluate a conditional expression. */
int b_test(sh *s, int ac, char **av)
{
	int r = -1;

	(void)s;
	if (ac && !strcmp(av[0], "[")) {
		if (ac < 2 || strcmp(av[ac - 1], "]")) {
			lg(HIBR_LERR, "[: missing ]");
			return 2;
		}
		ac--;
	}
	if (ac == 1)
		return HIBR_FAIL;
	if (ac == 2)
		return *av[1] ? HIBR_OK : HIBR_FAIL;
	if (ac == 3) {
		if (!strcmp(av[1], "!"))
			return *av[2] ? HIBR_FAIL : HIBR_OK;
		r = t_one(av[1], av[2]);
	}
	if (ac == 4) {
		if (!strcmp(av[1], "!")) {
			r = t_one(av[2], av[3]);
			if (r >= 0)
				return r ? HIBR_FAIL : HIBR_OK;
		}
		r = t_two(av[1], av[2], av[3]);
	}
	if (ac == 5 && !strcmp(av[1], "!")) {
		r = t_two(av[2], av[3], av[4]);
		if (r >= 0)
			return r ? HIBR_FAIL : HIBR_OK;
	}
	if (r < 0) {
		lg(HIBR_LERR, "test: bad expression");
		return 2;
	}
	return r ? HIBR_OK : HIBR_FAIL;
}

/* Run its arguments as a command. */
int b_eval(sh *s, int ac, char **av)
{
	str b;
	int i;

	s_init(&b);
	for (i = 1; i < ac; i++) {
		if (i > 1)
			s_ch(&b, ' ');
		s_cat(&b, av[i]);
	}
	if (b.n)
		hibr_run(s, b.p);
	s_free(&b);
	return s->st;
}

/* Replace the shell with a program. */
int b_exec(sh *s, int ac, char **av)
{
	char *path;
	char **env;

	if (ac < 2)
		return HIBR_OK;
	path = findx(s, av[1]);
	if (!path) {
		lg(HIBR_LERR, "exec: %s: command not found", av[1]);
		return HIBR_NOCMD;
	}
	env = v_envp(s, 0);
	fflush(0);
	execve(path, av + 1, env);
	lg(HIBR_LERR, "exec: %s: %s", path, strerror(errno));
	free(path);
	return HIBR_NOEXEC;
}

/* Read and execute a file in the current shell. */
int b_src(sh *s, int ac, char **av)
{
	FILE *f;
	str b;
	char *buf;
	size_t n;
	int oret;

	if (ac < 2) {
		lg(HIBR_LERR, "source: filename required");
		return HIBR_FAIL;
	}
	f = fopen(av[1], "r");
	if (!f) {
		lg(HIBR_LERR, "source: %s: %s", av[1], strerror(errno));
		return HIBR_FAIL;
	}
	s_init(&b);
	buf = xm(HIBR_IOCH);
	while ((n = fread(buf, 1, HIBR_IOCH, f)) > 0)
		s_add(&b, buf, n);
	free(buf);
	fclose(f);
	oret = s->ret;
	hibr_run(s, b.p ? b.p : "");
	s->ret = oret;
	s_free(&b);
	return s->st;
}

/* Describe how a name would be resolved. */
int b_type(sh *s, int ac, char **av)
{
	int i, rc = HIBR_OK;
	char *p;

	for (i = 1; i < ac; i++) {
		if (fn_find(s, av[i])) {
			printf("%s is a function\n", av[i]);
			continue;
		}
		if (m_find(s, av[i])) {
			printf("%s is a module builtin\n", av[i]);
			continue;
		}
		if (bi_find(av[i])) {
			printf("%s is a shell builtin\n", av[i]);
			continue;
		}
		p = findx(s, av[i]);
		if (p) {
			printf("%s is %s\n", av[i], p);
			free(p);
			continue;
		}
		lg(HIBR_LERR, "type: %s: not found", av[i]);
		rc = HIBR_FAIL;
	}
	return rc;
}

/* Manage loadable modules. */
int b_mod(sh *s, int ac, char **av)
{
	if (ac < 2) {
		lg(HIBR_LERR, "usage: mod load <path> | mod drop <name> | mod list");
		return HIBR_FAIL;
	}
	if (!strcmp(av[1], "load")) {
		if (ac < 3) {
			lg(HIBR_LERR, "mod load: path required");
			return HIBR_FAIL;
		}
		return m_load(s, av[2]);
	}
	if (!strcmp(av[1], "drop")) {
		if (ac < 3) {
			lg(HIBR_LERR, "mod drop: name required");
			return HIBR_FAIL;
		}
		return m_drop(s, av[2]);
	}
	if (!strcmp(av[1], "list")) {
		m_list(s);
		return HIBR_OK;
	}
	lg(HIBR_LERR, "mod: %s: unknown subcommand", av[1]);
	return HIBR_FAIL;
}

/* Declare function local variables. */
int b_local(sh *s, int ac, char **av)
{
	vec *fr;
	char *q;
	int i;

	if (!s->scope.n) {
		lg(HIBR_LERR, "local: only valid inside a function");
		return HIBR_FAIL;
	}
	fr = (vec *)s->scope.p[s->scope.n - 1];
	for (i = 1; i < ac; i++) {
		q = strchr(av[i], '=');
		if (q)
			*q = 0;
		if (!isname(av[i])) {
			lg(HIBR_LERR, "local: %s: not a valid name", av[i]);
			if (q)
				*q = '=';
			return HIBR_FAIL;
		}
		asg_keep(s, fr, av[i]);
		if (q) {
			hibr_set(s, av[i], q + 1, 0);
			*q = '=';
		} else {
			v_del(s, av[i]);
		}
		lg(HIBR_LTRC, "local %s at depth %lu", av[i],
		   (unsigned long)s->scope.n);
	}
	return HIBR_OK;
}

/* Print the command history. */
int b_hist(sh *s, int ac, char **av)
{
	size_t i;
	int k, ch, bad;
	str b;
	char *ex;

	if (ac > 1 && !strcmp(av[1], "-c")) {
		for (i = 0; i < s->hist.n; i++)
			free(s->hist.p[i]);
		s->hist.n = 0;
		return HIBR_OK;
	}
	if (ac > 1 && !strcmp(av[1], "-s")) {
		s_init(&b);
		for (k = 2; k < ac; k++) {
			if (b.n)
				s_ch(&b, ' ');
			s_cat(&b, av[k]);
		}
		hs_add(s, b.p ? b.p : "");
		s_free(&b);
		return HIBR_OK;
	}
	if (ac > 1 && !strcmp(av[1], "-p")) {
		for (k = 2; k < ac; k++) {
			ex = hx_expand(s, av[k], &ch, &bad);
			if (bad) {
				free(ex);
				return HIBR_FAIL;
			}
			printf("%s\n", ex);
			free(ex);
		}
		return HIBR_OK;
	}
	for (i = 0; i < s->hist.n; i++)
		printf("%5lu  %s\n", (unsigned long)i + 1, (char *)s->hist.p[i]);
	return HIBR_OK;
}

/* List the available builtins. */
int b_help(sh *s, int ac, char **av)
{
	const hibr_bi *b;

	(void)ac;
	(void)av;
	printf("hibr %s builtins:\n", HIBR_VER);
	for (b = bitab; b->nm; b++)
		printf("  %-10s %s\n", b->nm, b->hp ? b->hp : "");
	m_help(s);
	return HIBR_OK;
}

const hibr_bi bitab[] = {
	{ ".", b_src, "run a file in this shell" },
	{ ":", b_true, "succeed" },
	{ "[", b_test, "evaluate a conditional expression" },
	{ "accept", b_accept, "wait for one connection" },
	{ "alias", b_alias, "define or list aliases" },
	{ "args", b_args, "parse arguments against declared options" },
	{ "arr", b_arr, "array operations" },
	{ "bg", b_bg, "resume a stopped job in the background" },
	{ "break", b_brk, "leave enclosing loops" },
	{ "builtin", b_builtin, "run a builtin, ignoring functions" },
	{ "cd", b_cd, "change directory" },
	{ "command", b_command, "run a command, ignoring functions" },
	{ "connect", b_connect, "open a client connection" },
	{ "continue", b_cont, "restart enclosing loops" },
	{ "dirs", b_dirs, "show the directory stack" },
	{ "disown", b_disown, "forget a job without signalling it" },
	{ "echo", b_echo, "write arguments" },
	{ "eval", b_eval, "run arguments as a command" },
	{ "exec", b_exec, "replace the shell, or apply redirections" },
	{ "exit", b_exit, "leave the shell" },
	{ "export", b_exp, "mark variables for export" },
	{ "fail", b_fail, "report a failure with a message" },
	{ "false", b_false, "fail" },
	{ "fg", b_fg, "resume a job in the foreground" },
	{ "getopts", b_getopts, "parse option letters" },
	{ "help", b_help, "list builtins" },
	{ "history", b_hist, "print the command history" },
	{ "jobs", b_jobs, "list active jobs" },
	{ "json", b_json, "parse, query and emit JSON" },
	{ "kill", b_kill, "signal a job or process" },
	{ "let", b_let, "evaluate arithmetic expressions" },
	{ "listen", b_listen, "serve connections with a handler" },
	{ "local", b_local, "declare function local variables" },
	{ "match", b_match, "match a regex and peel out the groups" },
	{ "mod", b_mod, "load, drop or list modules" },
	{ "opt", b_opt, "declare an option for args" },
	{ "popd", b_popd, "pop the directory stack" },
	{ "printf", b_printf, "write formatted output" },
	{ "pushd", b_pushd, "push a directory and change to it" },
	{ "pwd", b_pwd, "print the working directory" },
	{ "read", b_read, "read a line into variables" },
	{ "recv", b_recv, "read from a descriptor" },
	{ "ret", b_ret, "produce a value and return" },
	{ "return", b_retf, "return from a function" },
	{ "rsub", b_rsub, "substitute regex matches" },
	{ "send", b_send, "write to a descriptor" },
	{ "set", b_set, "set options and parameters" },
	{ "shift", b_shift, "drop positional parameters" },
	{ "source", b_src, "run a file in this shell" },
	{ "str", b_str, "text operations" },
	{ "test", b_test, "evaluate a conditional expression" },
	{ "time", b_time, "time a command" },
	{ "title", b_title, "rename the running process" },
	{ "trap", b_trap, "run a command on a signal or on exit" },
	{ "true", b_true, "succeed" },
	{ "try", b_try, "run a command, catching failure" },
	{ "type", b_type, "describe a command name" },
	{ "umask", b_umask, "show or set the file creation mask" },
	{ "unalias", b_unalias, "remove aliases" },
	{ "unset", b_unset, "remove variables or functions" },
	{ "wait", b_wait, "wait for jobs to finish" },
	HIBR_BI_END
};

/* Compare a name against a builtin table entry. */
int bicmp(const void *k, const void *e)
{
	return strcmp((const char *)k, ((const hibr_bi *)e)->nm);
}

/* Look up a static builtin by name. */
const hibr_bi *bi_find(const char *nm)
{
	size_t n = sizeof bitab / sizeof bitab[0] - 1;

	return (const hibr_bi *)bsearch(nm, bitab, n, sizeof bitab[0], bicmp);
}
