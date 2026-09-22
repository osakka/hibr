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
	char *q, *e;
	var *v;
	size_t j;

	i = 1;
	if (ac > 1 && !strcmp(av[1], "-p"))
		i++;
	if (i >= ac) {
		for (j = 0; j < s->tsz; j++)
			for (v = s->tab[j]; v; v = v->nx)
				if (v->ex)
					b_decl1(s, v);
		return HIBR_OK;
	}
	for (; i < ac; i++) {
		q = strchr(av[i], '[');
		if (q && (!(e = strchr(av[i], '=')) || q < e)) {
			char sv = *q;
			*q = 0;
			lg(HIBR_LERR, "export: %s[...]: not a valid identifier",
			   av[i]);
			*q = sv;
			return HIBR_FAIL;
		}
		q = strchr(av[i], '=');
		if (q) {
			*q = 0;
			hibr_set(s, av[i], q + 1, 1);
			*q = '=';
			continue;
		}
		v = v_find(s, av[i]);
		if (v) {
			v->ex = 1;
			setenv(av[i], v->v ? v->v : "", 1);
		} else {
			hibr_set(s, av[i], "", 1);
		}
	}
	return HIBR_OK;
}

/* Split name[sub]... into its name and key path, honouring a quoted
   subscript. The name is left truncated at the bracket the caller restores. */
char *bi_keys(sh *s, char *word, const char *mk, vec *ks)
{
	char *br = strchr(word, '[');
	char *r, *end, *k;

	if (!br)
		return 0;
	*br = 0;
	for (r = br + 1; r && *r;) {
		end = strchr(r, ']');
		if (!end)
			break;
		*end = 0;
		k = xkey_q(s, r, mk ? mk + (r - word) : 0);
		k = ar_dup(s->xa, k, strlen(k));
		v_add(ks, k);
		if (k[0] == '-' && isdigit((unsigned char)k[1]))
			ks->p[ks->n - 1] =
				xneg(s, word, (char **)ks->p, (int)ks->n - 1);
		*end = ']';
		r = end + 1;
		if (*r == '[')
			r++;
		else
			break;
	}
	return br;
}

/* True for a builtin that reads the quote mask of its own arguments. */
int bi_mask(const char *nm)
{
	return !strcmp(nm, "unset") || !strcmp(nm, "command") ||
	       !strcmp(nm, "read");
}

/* Remove variables or functions. */
int b_unset(sh *s, int ac, char **av)
{
	int i;
	size_t j;
	node *f;

	for (i = 1; i < ac; i++) {
		{
			vec *ks = vb_get(s);
			char *br = bi_keys(s, av[i],
					   s->amask ? s->amask[i] : 0, ks);
			if (br) {
				v_delp(s, av[i], (char **)ks->p, (int)ks->n);
				vb_put(s, ks);
				*br = '[';
				continue;
			}
			vb_put(s, ks);
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
		} else if (!strcmp(av[i], "-o") || !strcmp(av[i], "+o")) {
			int on = av[i][0] == '-';
			if (i + 1 >= ac) {
				sh_optlist(s, 1);
				continue;
			}
			if (sh_optset(s, av[++i], on) != HIBR_OK)
				return HIBR_FAIL;
		} else {
			lg(HIBR_LERR, "set: %s: unknown option", av[i]);
			return HIBR_FAIL;
		}
	}
	if (i < ac || (ac > 1 && !strcmp(av[ac - 1], "--")))
		v_pos(s, ac - i, av + i);
	return HIBR_OK;
}

/* The value of a short option, attached to it or the next argument. */
char *bi_oval(int ac, char **av, int *i, size_t len)
{
	if (av[*i][1 + len])
		return av[*i] + 1 + len;
	if (*i + 1 < ac)
		return av[++*i];
	return 0;
}

/* Read a line from standard input into variables. */
int b_read(sh *s, int ac, char **av)
{
	str b;
	char c;
	ssize_t n;
	int i = 1, want = 0, silent = 0, fd = 0, delim = '\n', eof = 0;
	const char *arr = 0, *prompt = 0;
	char *ov;
	long tmo = 0;
	const char *ifs = sh_ifs(s);
	size_t p = 0, st;
	struct termios sv, raw;
	fd_set rs;
	struct timeval tv;

	if (!ifs)
		ifs = " \t\n";
	for (; i < ac && av[i][0] == '-' && av[i][1]; i++) {
		const char *f = av[i] + 1;
		int taken = 0;
		while (*f && !taken) {
			char o = *f++;
			if (strchr("pntuad", o)) {
				if (*f) {
					ov = (char *)f;
				} else if (i + 1 < ac) {
					ov = av[++i];
				} else {
					lg(HIBR_LERR,
					   "read: -%c needs a value", o);
					return 2;
				}
				taken = 1;
			} else {
				ov = 0;
			}
			switch (o) {
			case 'r':
				break;
			case 's':
				silent = 1;
				break;
			case 'p':
				prompt = ov;
				break;
			case 'n':
				want = atoi(ov);
				break;
			case 't':
				tmo = atol(ov);
				break;
			case 'u':
				fd = atoi(ov);
				break;
			case 'a':
				arr = ov;
				break;
			case 'd':
				delim = ov[0] ? (unsigned char)ov[0] : 0;
				break;
			default:
				lg(HIBR_LERR, "read: -%c: unknown option", o);
				return 2;
			}
		}
	}
	if (prompt && isatty(fd)) {
		fputs(prompt, stderr);
		fflush(stderr);
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
		if (!want && (unsigned char)c == (unsigned char)delim)
			break;
		s_ch(&b, c);
		if (want && (int)b.n >= want)
			break;
	}
	if (silent) {
		tcsetattr(fd, TCSADRAIN, &sv);
		fputc('\n', stderr);
	}
	eof = n <= 0;
	if (arr) {
		vec *el = vb_get(s);
		while (p < b.n) {
			while (p < b.n && strchr(ifs, b.p[p]))
				p++;
			if (p >= b.n)
				break;
			st = p;
			while (p < b.n && !strchr(ifs, b.p[p]))
				p++;
			v_add(el, ar_dup(s->xa, b.p + st, p - st));
		}
		v_arr(s, arr, el);
		vb_put(s, el);
		s_free(&b);
		return eof ? HIBR_FAIL : HIBR_OK;
	}
	if (i >= ac) {
		hibr_set(s, "REPLY", b.p ? b.p : "", 0);
		s_free(&b);
		return eof ? HIBR_FAIL : HIBR_OK;
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
			vec *ks = vb_get(s);
			char *br;
			if (b.p)
				b.p[p] = 0;
			br = bi_keys(s, av[i], s->amask ? s->amask[i] : 0, ks);
			if (br) {
				v_setp(s, av[i], (char **)ks->p, (int)ks->n,
				       b.p ? b.p + st : "");
				*br = '[';
			} else {
				hibr_set(s, av[i], b.p ? b.p + st : "", 0);
			}
			vb_put(s, ks);
			if (b.p)
				b.p[p] = svc;
		}
	}
	s_free(&b);
	return eof ? HIBR_FAIL : HIBR_OK;
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
	case 'a':
		return stat(a, &st) == 0;
	case 'b':
		return stat(a, &st) == 0 && S_ISBLK(st.st_mode);
	case 'c':
		return stat(a, &st) == 0 && S_ISCHR(st.st_mode);
	case 'p':
		return stat(a, &st) == 0 && S_ISFIFO(st.st_mode);
	case 'S':
		return stat(a, &st) == 0 && S_ISSOCK(st.st_mode);
	case 'g':
		return stat(a, &st) == 0 && (st.st_mode & S_ISGID) != 0;
	case 'u':
		return stat(a, &st) == 0 && (st.st_mode & S_ISUID) != 0;
	case 'k':
		return stat(a, &st) == 0 && (st.st_mode & S_ISVTX) != 0;
	case 'O':
		return stat(a, &st) == 0 && st.st_uid == geteuid();
	case 'G':
		return stat(a, &st) == 0 && st.st_gid == getegid();
	case 'N':
		return stat(a, &st) == 0 && st.st_mtime > st.st_atime;
	case 't':
		return isatty((int)strtol(a, 0, 10));
	}
	return -1;
}

/* Say whether a word is a unary test operator. */
int t_isun(const char *w)
{
	return w[0] == '-' && w[1] && !w[2] &&
	       strchr("znefdrwxsLhabcpSgukOGNt", w[1]) != 0;
}

/* Say whether a word is a binary test operator. */
int t_isbin(const char *w)
{
	static const char *b[] = { "=", "!=", "<", ">", "-eq", "-ne", "-lt",
				   "-le", "-gt", "-ge", "-ef", "-nt", "-ot", 0 };
	int i;

	for (i = 0; b[i]; i++)
		if (!strcmp(w, b[i]))
			return 1;
	return 0;
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
	if (!strcmp(op, "-ef") || !strcmp(op, "-nt") || !strcmp(op, "-ot")) {
		struct stat sa, sb;
		int ha = stat(a, &sa) == 0, hb = stat(b, &sb) == 0;
		if (!strcmp(op, "-ef"))
			return ha && hb && sa.st_dev == sb.st_dev &&
			       sa.st_ino == sb.st_ino;
		if (!strcmp(op, "-nt"))
			return ha && (!hb || sa.st_mtime > sb.st_mtime);
		return hb && (!ha || sa.st_mtime < sb.st_mtime);
	}
	return -1;
}

/* Evaluate a primary: a negation, a parenthesised group, or a comparison. */
int tx_prim(tex *t)
{
	int r;

	if (t->i >= t->n) {
		t->err = 1;
		return 0;
	}
	if (!strcmp(t->v[t->i], "!")) {
		t->i++;
		return !tx_prim(t);
	}
	if (!strcmp(t->v[t->i], "(")) {
		t->i++;
		r = tx_or(t);
		if (t->i >= t->n || strcmp(t->v[t->i], ")")) {
			t->err = 1;
			return 0;
		}
		t->i++;
		return r;
	}
	if (t->i + 2 < t->n && t_isbin(t->v[t->i + 1])) {
		r = t_two(t->v[t->i], t->v[t->i + 1], t->v[t->i + 2]);
		t->i += 3;
		if (r < 0)
			t->err = 1;
		return r > 0;
	}
	if (t_isun(t->v[t->i]) && t->i + 1 < t->n) {
		r = t_one(t->v[t->i], t->v[t->i + 1]);
		t->i += 2;
		if (r < 0)
			t->err = 1;
		return r > 0;
	}
	r = *t->v[t->i] != 0;
	t->i++;
	return r;
}

/* Evaluate a sequence of primaries joined by -a. */
int tx_and(tex *t)
{
	int r;

	if (++t->d > HIBR_DEPTH) {
		t->err = 1;
		return 0;
	}
	r = tx_prim(t);
	while (!t->err && t->i < t->n && !strcmp(t->v[t->i], "-a")) {
		t->i++;
		r = tx_prim(t) && r;
	}
	t->d--;
	return r;
}

/* Evaluate a sequence of and-groups joined by -o. */
int tx_or(tex *t)
{
	int r;

	if (++t->d > HIBR_DEPTH) {
		t->err = 1;
		return 0;
	}
	r = tx_and(t);
	while (!t->err && t->i < t->n && !strcmp(t->v[t->i], "-o")) {
		t->i++;
		r = tx_and(t) || r;
	}
	t->d--;
	return r;
}

/* Evaluate a conditional expression, POSIX argument counts then a grammar. */
int b_test(sh *s, int ac, char **av)
{
	int r = -1;
	tex t;

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
	} else if (ac == 4) {
		r = t_two(av[1], av[2], av[3]);
		if (r < 0 && !strcmp(av[1], "!"))
			r = t_one(av[2], av[3]) < 0 ? -1 :
			    !t_one(av[2], av[3]);
		else if (r < 0 && !strcmp(av[1], "(") && !strcmp(av[3], ")"))
			return *av[2] ? HIBR_OK : HIBR_FAIL;
	} else if (ac == 5) {
		if (!strcmp(av[1], "!") && t_isbin(av[3]))
			r = t_two(av[2], av[3], av[4]) < 0 ? -1 :
			    !t_two(av[2], av[3], av[4]);
		else if (!strcmp(av[1], "(") && !strcmp(av[4], ")"))
			r = t_two(av[2], av[3], av[4]);
	}
	if (r < 0 && ac > 3) {
		t.v = av + 1;
		t.n = ac - 1;
		t.i = 0;
		t.err = 0;
		t.d = 0;
		r = tx_or(&t);
		if (t.err || t.i != t.n)
			r = -1;
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
	char **oav = 0;
	size_t n;
	int oret, oac = 0, oavo = 0, pos = ac > 2;

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
	if (pos) {
		lg(HIBR_LDBG, "source: %d positional arguments for %s", ac - 2,
		   av[1]);
		oav = s->av;
		oac = s->ac;
		oavo = s->avo;
		s->av = av + 2;
		s->ac = ac - 2;
		s->avo = 0;
	}
	hibr_run(s, b.p ? b.p : "");
	if (pos) {
		if (s->avo && s->av) {
			int i;
			for (i = 0; i < s->ac; i++)
				free(s->av[i]);
			free(s->av);
		}
		s->av = oav;
		s->ac = oac;
		s->avo = oavo;
	}
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
		lg(HIBR_LERR, "usage: mod load <path|name> | "
			      "mod drop <name|all> | mod list | mod avail");
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
			lg(HIBR_LERR, "mod drop: name required, or 'all'");
			return HIBR_FAIL;
		}
		if (!strcmp(av[2], "all")) {
			int n = m_dropall(s);
			str t;
			s_init(&t);
			s_num(&t, (long)n);
			hibr_ret(s, t.p);
			if (!s->bind)
				printf("dropped %d module%s\n", n,
				       n == 1 ? "" : "s");
			s_free(&t);
			return HIBR_OK;
		}
		return m_drop(s, av[2]);
	}
	if (!strcmp(av[1], "list")) {
		if (ac > 2 && !strcmp(av[2], "-a")) {
			m_avail(s);
			return HIBR_OK;
		}
		m_list(s);
		return HIBR_OK;
	}
	if (!strcmp(av[1], "avail")) {
		m_avail(s);
		return HIBR_OK;
	}
	lg(HIBR_LERR, "mod: %s: unknown subcommand", av[1]);
	return HIBR_FAIL;
}

/* Say what this script cannot run without. Each name is an interface some
   module offers, or a module; the first that cannot be found stops the
   script's dependencies from being met and says which one. */
int b_need(sh *s, int ac, char **av)
{
	int i;

	if (ac < 2) {
		lg(HIBR_LERR, "usage: need name...");
		return 2;
	}
	for (i = 1; i < ac; i++) {
		if (m_need(s, av[i]) == HIBR_OK) {
			lg(HIBR_LDBG, "need %s: met", av[i]);
			continue;
		}
		lg(HIBR_LERR, "need: %s: nothing offers it", av[i]);
		return HIBR_FAIL;
	}
	return HIBR_OK;
}

/* Name this script to whatever is running it, and say what it is for. */
int b_app(sh *s, int ac, char **av)
{
	if (ac < 2) {
		lg(HIBR_LERR, "usage: app name [description]");
		return 2;
	}
	hibr_set(s, "APP_NAME", av[1], 0);
	hibr_set(s, "APP_DESC", ac > 2 ? av[2] : "", 0);
	lg(HIBR_LDBG, "app %s declared", av[1]);
	return HIBR_OK;
}

/* Print one variable the way declare would state it. */
void b_decl1(sh *s, var *v)
{
	const char *ty = v_tyname(v->at);
	str f;

	(void)s;
	s_init(&f);
	if (v->at & A_REF)
		s_ch(&f, 'n');
	if (v->at & A_INT)
		s_ch(&f, 'i');
	if (v->at & A_LOW)
		s_ch(&f, 'l');
	if (v->at & A_UPP)
		s_ch(&f, 'u');
	if (v->ro)
		s_ch(&f, 'r');
	if (v->ex)
		s_ch(&f, 'x');
	if (v->am)
		s_ch(&f, 'A');
	if (*ty)
		printf("declare %s%s%s ", f.n ? "-" : "", f.n ? f.p : "", ty);
	else
		printf("declare -%s ", f.n ? f.p : "-");
	if (v->am)
		printf("%s=(...)\n", v->k);
	else
		printf("%s=\"%s\"\n", v->k, v->v ? v->v : "");
	s_free(&f);
}

/* Declare variables, with bash's flags or one of our own type names. */
int b_decl(sh *s, int ac, char **av)
{
	unsigned at = 0, code;
	int i = 1, ro = 0, ex = 0, pr = 0, map = 0, glob = 0, n = 0;
	char *q;
	var *v;
	size_t j;

	for (; i < ac && av[i][0] == '-' && av[i][1]; i++) {
		char *f;
		for (f = av[i] + 1; *f; f++) {
			switch (*f) {
			case 'i': at |= A_INT; break;
			case 'n': at |= A_REF; break;
			case 'l': at = (at & ~A_UPP) | A_LOW; break;
			case 'u': at = (at & ~A_LOW) | A_UPP; break;
			case 'r': ro = 1; break;
			case 'x': ex = 1; break;
			case 'a':
			case 'A': map = 1; break;
			case 'g': glob = 1; break;
			case 'p': pr = 1; break;
			default:
				lg(HIBR_LERR, "declare: -%c: unknown option",
				   *f);
				return 2;
			}
		}
	}
	if (i < ac && (code = v_tycode(av[i]))) {
		at |= code << A_TYSH;
		i++;
	}
	if (pr || i >= ac) {
		if (i >= ac) {
			for (j = 0; j < s->tsz; j++)
				for (v = s->tab[j]; v; v = v->nx)
					b_decl1(s, v);
			return HIBR_OK;
		}
		for (; i < ac; i++) {
			v = v_find(s, av[i]);
			if (!v) {
				lg(HIBR_LERR, "declare: %s: not found", av[i]);
				n = 1;
				continue;
			}
			b_decl1(s, v);
		}
		return n ? HIBR_FAIL : HIBR_OK;
	}
	for (; i < ac; i++) {
		q = strchr(av[i], '=');
		if (q)
			*q = 0;
		if (!isname(av[i])) {
			lg(HIBR_LERR, "declare: %s: not a valid name", av[i]);
			if (q)
				*q = '=';
			return HIBR_FAIL;
		}
		if (!glob && s->scope.n)
			asg_keep(s, (vec *)s->scope.p[s->scope.n - 1], av[i]);
		v = v_find(s, av[i]);
		if (!v) {
			hibr_set(s, av[i], "", 0);
			v = v_find(s, av[i]);
		}
		if (v) {
			v->at |= at;
			if (ex)
				v->ex = 1;
			if (map && !v->am)
				v->am = 1;
		}
		if (q && (at & A_REF) && v) {
			lg(HIBR_LDBG, "%s now refers to %s", av[i], q + 1);
			free(v->v);
			v->v = xs(q + 1);
		} else if (q && hibr_set(s, av[i], q + 1, ex) != HIBR_OK)
			n = 1;
		if (ro && (v = v_find(s, av[i])))
			v->ro = 1;
		if (q)
			*q = '=';
	}
	return n ? HIBR_FAIL : HIBR_OK;
}

/* Make variables readonly, or list the ones that are. */
int b_ro(sh *s, int ac, char **av)
{
	int i = 1, n = 0;
	char *q;
	var *v;
	size_t j;

	if (ac > 1 && !strcmp(av[1], "-p"))
		i++;
	if (i >= ac) {
		for (j = 0; j < s->tsz; j++)
			for (v = s->tab[j]; v; v = v->nx)
				if (v->ro)
					b_decl1(s, v);
		return HIBR_OK;
	}
	for (; i < ac; i++) {
		q = strchr(av[i], '=');
		if (q) {
			*q = 0;
			if (hibr_set(s, av[i], q + 1, 0) != HIBR_OK)
				n = 1;
		}
		v = v_find(s, av[i]);
		if (!v) {
			hibr_set(s, av[i], "", 0);
			v = v_find(s, av[i]);
		}
		if (v)
			v->ro = 1;
		if (q)
			*q = '=';
	}
	return n ? HIBR_FAIL : HIBR_OK;
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

/* Read lines into an array, one element each. */
int b_mapfile(sh *s, int ac, char **av)
{
	str b;
	vec *el;
	char c;
	ssize_t got;
	const char *nm = "MAPFILE";
	char *ov;
	long want = 0, skip = 0, origin = 0, seen = 0;
	int i = 1, fd = 0, strip = 0, delim = '\n';
	size_t st;

	for (; i < ac && av[i][0] == '-' && av[i][1]; i++) {
		if (!strcmp(av[i], "-t")) {
			strip = 1;
		} else if (!strncmp(av[i], "-n", 2)) {
			ov = bi_oval(ac, av, &i, 1);
			if (!ov)
				return 2;
			want = atol(ov);
		} else if (!strncmp(av[i], "-s", 2)) {
			ov = bi_oval(ac, av, &i, 1);
			if (!ov)
				return 2;
			skip = atol(ov);
		} else if (!strncmp(av[i], "-O", 2)) {
			ov = bi_oval(ac, av, &i, 1);
			if (!ov)
				return 2;
			origin = atol(ov);
		} else if (!strncmp(av[i], "-u", 2)) {
			ov = bi_oval(ac, av, &i, 1);
			if (!ov)
				return 2;
			fd = atoi(ov);
		} else if (!strncmp(av[i], "-d", 2)) {
			ov = bi_oval(ac, av, &i, 1);
			if (!ov)
				return 2;
			delim = ov[0] ? (unsigned char)ov[0] : 0;
		} else {
			lg(HIBR_LERR, "mapfile: %s: unknown option", av[i]);
			return 2;
		}
	}
	if (i < ac)
		nm = av[i];
	el = vb_get(s);
	if (origin > 0) {
		vec *cur = vb_get(s);
		long k;
		v_list(s, nm, 0, 0, cur, 1);
		for (k = 0; k < origin; k++)
			v_add(el, ar_dup(s->xa, "", 0));
		vb_put(s, cur);
	}
	s_init(&b);
	while ((got = read(fd, &c, 1)) == 1) {
		if ((unsigned char)c != (unsigned char)delim) {
			s_ch(&b, c);
			continue;
		}
		seen++;
		if (seen <= skip) {
			b.n = 0;
			continue;
		}
		if (!strip)
			s_ch(&b, c);
		v_add(el, ar_dup(s->xa, b.p ? b.p : "", b.n));
		b.n = 0;
		if (want && (long)el->n - origin >= want)
			break;
	}
	if (b.n && (!want || (long)el->n - origin < want)) {
		seen++;
		if (seen > skip)
			v_add(el, ar_dup(s->xa, b.p, b.n));
	}
	s_free(&b);
	st = el->n;
	v_arr(s, nm, el);
	vb_put(s, el);
	lg(HIBR_LDBG, "mapfile read %lu elements into %s",
	   (unsigned long)st, nm);
	return HIBR_OK;
}

/* Show or forget where commands were found. */
int b_hash(sh *s, int ac, char **av)
{
	int i = 1, n = 0;
	char *path;
	size_t j;

	for (; i < ac && av[i][0] == '-' && av[i][1]; i++) {
		if (!strcmp(av[i], "-r")) {
			hsh_clear(s, 0);
			return HIBR_OK;
		}
		if (!strcmp(av[i], "-d") && i + 1 < ac) {
			hsh_clear(s, av[++i]);
			continue;
		}
		lg(HIBR_LERR, "hash: %s: unknown option", av[i]);
		return 2;
	}
	if (i >= ac) {
		if (!s->cmds.n) {
			printf("no commands remembered\n");
			return HIBR_OK;
		}
		for (j = 0; j < s->cmds.n; j++) {
			char *e = (char *)s->cmds.p[j];
			char *eq = strchr(e, '=');
			if (!eq)
				continue;
			*eq = 0;
			printf("%-20s %s\n", e, eq + 1);
			*eq = '=';
		}
		return HIBR_OK;
	}
	for (; i < ac; i++) {
		path = findx(s, av[i]);
		if (!path) {
			lg(HIBR_LERR, "hash: %s: not found", av[i]);
			n = 1;
			continue;
		}
		free(path);
	}
	return n ? HIBR_FAIL : HIBR_OK;
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
	{ "app", b_app, "name this script as an app, for whatever runs it" },
	{ "args", b_args, "parse arguments against declared options" },
	{ "arr", b_arr, "array operations" },
	{ "bg", b_bg, "resume a stopped job in the background" },
	{ "break", b_brk, "leave enclosing loops" },
	{ "builtin", b_builtin, "run a builtin, ignoring functions" },
	{ "cd", b_cd, "change directory" },
	{ "command", b_command, "run a command, ignoring functions" },
	{ "connect", b_connect, "open a client connection" },
	{ "continue", b_cont, "restart enclosing loops" },
	{ "coproc", b_coproc, "run a command as a coprocess" },
	{ "declare", b_decl, "declare variables and their attributes" },
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
	{ "hash", b_hash, "show or forget where commands were found" },
	{ "help", b_help, "list builtins" },
	{ "history", b_hist, "print the command history" },
	{ "jobs", b_jobs, "list active jobs" },
	{ "json", b_json, "parse, query and emit JSON" },
	{ "kill", b_kill, "signal a job or process" },
	{ "let", b_let, "evaluate arithmetic expressions" },
	{ "listen", b_listen, "serve connections, or -b to only bind" },
	{ "local", b_local, "declare function local variables" },
	{ "mapfile", b_mapfile, "read lines into an array" },
	{ "match", b_match, "match a regex and peel out the groups" },
	{ "mod", b_mod, "load, drop or list modules" },
	{ "need", b_need, "make an interface or module available, or fail" },
	{ "opt", b_opt, "declare an option for args" },
	{ "popd", b_popd, "pop the directory stack" },
	{ "printf", b_printf, "write formatted output" },
	{ "pushd", b_pushd, "push a directory and change to it" },
	{ "pwd", b_pwd, "print the working directory" },
	{ "read", b_read, "read a line into variables" },
	{ "readarray", b_mapfile, "read lines into an array" },
	{ "readonly", b_ro, "make variables readonly" },
	{ "recv", b_recv, "read from a descriptor" },
	{ "ret", b_ret, "produce a value and return" },
	{ "return", b_retf, "return from a function" },
	{ "rsub", b_rsub, "substitute regex matches" },
	{ "send", b_send, "write to a descriptor" },
	{ "set", b_set, "set options and parameters" },
	{ "shift", b_shift, "drop positional parameters" },
	{ "shopt", b_shopt, "read or set shell options" },
	{ "source", b_src, "run a file in this shell" },
	{ "str", b_str, "text operations" },
	{ "test", b_test, "evaluate a conditional expression" },
	{ "time", b_time, "time a command" },
	{ "title", b_title, "rename the running process" },
	{ "trap", b_trap, "run a command on a signal or on exit" },
	{ "true", b_true, "succeed" },
	{ "try", b_try, "run a command, catching failure" },
	{ "type", b_type, "describe a command name" },
	{ "typeset", b_decl, "declare variables and their attributes" },
	{ "ulimit", b_ulimit, "read or set a resource limit" },
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
