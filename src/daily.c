#include "pri.h"
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
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
void al_quote(str *o, const char *a, const char *mk)
{
	const char *p, *q;

	if (mk) {
		for (p = a; *p; p++) {
			if (!mk[p - a]) {
				if (!isalnum((unsigned char)*p) &&
				    !strchr("_+-%^=./:,@", *p))
					s_ch(o, '\\');
				s_ch(o, *p);
				continue;
			}
			q = p;
			while (*p && mk[p - a])
				p++;
			s_ch(o, '\'');
			s_sqb(o, q, p - q);
			s_ch(o, '\'');
			p--;
		}
		if (!*a)
			s_cat(o, "''");
		return;
	}
	if (!*a || strchr(a, '\n')) {
		s_sq(o, a);
		return;
	}
	for (p = a; *p; p++) {
		if (!isalnum((unsigned char)*p) && !strchr("_+-%^=./:,@", *p))
			s_ch(o, '\\');
		s_ch(o, *p);
	}
}

/* Run an alias body with the original arguments appended. */
int al_run(sh *s, const char *body, int ac, char **av, char **am)
{
	str b;
	int i, st;

	s_init(&b);
	s_cat(&b, body);
	for (i = 1; i < ac; i++) {
		s_ch(&b, ' ');
		al_quote(&b, av[i], am && am[i] ? am[i] : 0);
	}
	v_add(&s->axp, xs(av[0]));
	lg(HIBR_LTRC, "alias %s -> %s", av[0], b.p);
	st = hibr_run(s, b.p);
	free(s->axp.p[--s->axp.n]);
	s_free(&b);
	return st;
}

/* Print one alias as the command that defines it. */
void al_print(const char *k, const char *v)
{
	str o;

	s_init(&o);
	s_cat(&o, "alias ");
	s_cat(&o, k);
	s_ch(&o, '=');
	s_sq(&o, v);
	puts(o.p);
	s_free(&o);
}

/* Order aliases by name for a listing. */
int al_cmp(const void *a, const void *b)
{
	return strcmp((*(struct al *const *)a)->k, (*(struct al *const *)b)->k);
}

/* Define aliases, or list the ones already defined, by name and quoted to
   read back; a name with no alias fails, as in bash (Gitea #458). */
int b_alias(sh *s, int ac, char **av)
{
	int i = 1, rc = HIBR_OK;
	size_t j;
	char *q;
	struct al *a;

	for (; i < ac && av[i][0] == '-' && av[i][1]; i++) {
		if (!strcmp(av[i], "--")) {
			i++;
			break;
		}
		if (strcmp(av[i], "-p")) {
			lg(HIBR_LERR, "alias: %s: invalid option", av[i]);
			return 2;
		}
	}
	if (i >= ac) {
		vec l = { 0, 0, 0 };

		for (j = 0; j < s->als.n; j++)
			v_add(&l, s->als.p[j]);
		if (l.n > 1)
			qsort(l.p, l.n, sizeof *l.p, al_cmp);
		for (j = 0; j < l.n; j++) {
			a = (struct al *)l.p[j];
			al_print(a->k, a->v);
		}
		v_free(&l);
		return HIBR_OK;
	}
	for (; i < ac; i++) {
		q = strchr(av[i], '=');
		if (!q) {
			const char *v = al_get(s, av[i]);
			if (v) {
				al_print(av[i], v);
			} else {
				lg(HIBR_LERR, "alias: %s: not found", av[i]);
				rc = HIBR_FAIL;
			}
			continue;
		}
		*q = 0;
		al_set(s, av[i], q + 1);
		*q = '=';
	}
	return rc;
}

/* Take one alias out of the table: 1 when there was one. */
int al_del(sh *s, const char *k)
{
	size_t j;
	struct al *a;

	for (j = 0; j < s->als.n; j++) {
		a = (struct al *)s->als.p[j];
		if (strcmp(a->k, k))
			continue;
		free(a->k);
		free(a->v);
		free(a);
		s->als.p[j] = s->als.p[s->als.n - 1];
		s->als.n--;
		return 1;
	}
	return 0;
}

/* Remove aliases, or all of them with -a; one that is not there fails. */
int b_unalias(sh *s, int ac, char **av)
{
	int i = 1, rc = HIBR_OK;
	struct al *a;

	if (i < ac && !strcmp(av[i], "-a")) {
		while (s->als.n) {
			a = (struct al *)s->als.p[--s->als.n];
			free(a->k);
			free(a->v);
			free(a);
		}
		return HIBR_OK;
	}
	if (i < ac && !strcmp(av[i], "--"))
		i++;
	if (i >= ac) {
		lg(HIBR_LERR, "unalias: usage: unalias [-a] name [name ...]");
		return 2;
	}
	for (; i < ac; i++)
		if (!al_del(s, av[i])) {
			lg(HIBR_LERR, "unalias: %s: not found", av[i]);
			rc = HIBR_FAIL;
		}
	return rc;
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

/* The directory stack entry ~N or ~-N names, or null when out of range. */
const char *dir_at(sh *s, long k)
{
	long len = (long)s->dirs.n + 1, i = k < 0 ? len + k : k;

	if (i < 0 || i >= len)
		return 0;
	if (!i)
		return hibr_get(s, "PWD");
	return (const char *)s->dirs.p[s->dirs.n - (size_t)i];
}

/* Read a +N or -N stack index: 1 with *k set, 0 when w is not one. */
int dir_idx(const char *w, long *k)
{
	char *end;

	if ((w[0] != '+' && w[0] != '-') || !isdigit((unsigned char)w[1]))
		return 0;
	*k = strtol(w + 1, &end, 10);
	if (*end)
		return 0;
	if (w[0] == '-')
		*k = -*k - 1;
	return 1;
}

/* The stack's position for a +N or -N index, entry 0 being the working
   directory: -1 when it is out of range. */
long dir_pos(sh *s, long k)
{
	long len = (long)s->dirs.n + 1;

	if (k < 0)
		k += len;
	return k < 0 || k >= len ? -1 : k;
}

/* Print the directory stack, newest first: -c clears it, -l keeps paths
   whole, -p prints one a line, -v numbers them, +N or -N prints one. */
int b_dirs(sh *s, int ac, char **av)
{
	char *c = cwd(), *t;
	long i, n = (long)s->dirs.n, k, pos = -1;
	int clr = 0, lng = 0, per = 0, num = 0, a = 1;
	const char *e, *f;

	for (; a < ac; a++) {
		if (dir_idx(av[a], &k)) {
			pos = dir_pos(s, k);
			if (pos < 0) {
				lg(HIBR_LERR, "dirs: %s: directory stack index out of range", av[a]);
				free(c);
				return HIBR_FAIL;
			}
			continue;
		}
		if (av[a][0] != '-' || !av[a][1]) {
			lg(HIBR_LERR, "dirs: %s: invalid argument", av[a]);
			free(c);
			return 2;
		}
		for (f = av[a] + 1; *f; f++) {
			if (*f == 'c')
				clr = 1;
			else if (*f == 'l')
				lng = 1;
			else if (*f == 'p')
				per = 1;
			else if (*f == 'v')
				per = num = 1;
			else {
				lg(HIBR_LERR, "dirs: -%c: invalid option", *f);
				free(c);
				return 2;
			}
		}
	}
	if (clr) {
		while (s->dirs.n)
			free(s->dirs.p[--s->dirs.n]);
		free(c);
		return HIBR_OK;
	}
	for (i = 0; i <= n; i++) {
		if (pos >= 0 && i != pos)
			continue;
		e = i ? (const char *)s->dirs.p[n - i] : (c ? c : "");
		t = lng ? xs(e) : dir_short(s, e);
		if (num)
			printf("%2ld  %s\n", i, t);
		else if (per || pos >= 0)
			printf("%s\n", t);
		else
			printf(i ? " %s" : "%s", t);
		free(t);
	}
	if (!per && pos < 0)
		printf("\n");
	free(c);
	return HIBR_OK;
}

/* cd for the directory stack: 0 on success, with OLDPWD and PWD set. */
int dir_cd(sh *s, const char *d)
{
	char *args[3];

	args[0] = "cd";
	args[1] = "--";
	args[2] = (char *)d;
	return b_cd(s, 3, args);
}

/* Change directory, remembering where we were: with no name the top two
   swap, -n adds without changing directory, +N or -N rotates the stack so
   that entry is on top, as bash's pushd does. */
int b_pushd(sh *s, int ac, char **av)
{
	char *c, *top;
	int a = 1, nocd = 0;
	long k, pos, i, n;
	vec l = { 0, 0, 0 };

	if (a < ac && !strcmp(av[a], "-n")) {
		nocd = 1;
		a++;
	}
	if (a < ac && !strcmp(av[a], "--"))
		a++;
	if (a >= ac) {
		if (!s->dirs.n) {
			lg(HIBR_LERR, "pushd: no other directory");
			return HIBR_FAIL;
		}
		top = (char *)s->dirs.p[s->dirs.n - 1];
		c = cwd();
		if (!nocd && dir_cd(s, top) != HIBR_OK) {
			free(c);
			return HIBR_FAIL;
		}
		free(top);
		s->dirs.p[s->dirs.n - 1] = c ? c : xs("");
		return b_dirs(s, 1, av);
	}
	if (dir_idx(av[a], &k)) {
		pos = dir_pos(s, k);
		if (pos < 0) {
			lg(HIBR_LERR, "pushd: %s: directory stack index out of range", av[a]);
			return HIBR_FAIL;
		}
		n = (long)s->dirs.n;
		c = cwd();
		v_add(&l, c ? c : xs(""));
		for (i = n - 1; i >= 0; i--)
			v_add(&l, s->dirs.p[i]);
		if (!nocd && pos && dir_cd(s, (char *)l.p[pos]) != HIBR_OK) {
			free(l.p[0]);
			v_free(&l);
			return HIBR_FAIL;
		}
		s->dirs.n = 0;
		for (i = (long)l.n - 1; i >= 1; i--)
			v_add(&s->dirs, l.p[(pos + i) % (long)l.n]);
		if (pos == 0)
			lg(HIBR_LDBG, "pushd +0 leaves the stack as it is");
		free(l.p[pos]);
		v_free(&l);
		return b_dirs(s, 1, av);
	}
	if (nocd) {
		v_add(&s->dirs, xs(av[a]));
		return b_dirs(s, 1, av);
	}
	c = cwd();
	if (dir_cd(s, av[a]) != HIBR_OK) {
		free(c);
		return HIBR_FAIL;
	}
	if (c)
		v_add(&s->dirs, c);
	return b_dirs(s, 1, av);
}

/* Return to the directory on top of the stack, or with +N or -N take that
   entry out; -n takes one out without changing directory. */
int b_popd(sh *s, int ac, char **av)
{
	char *d;
	int a = 1, nocd = 0;
	long k, pos = 1, at;

	if (a < ac && !strcmp(av[a], "-n")) {
		nocd = 1;
		a++;
	}
	if (a < ac && !strcmp(av[a], "--"))
		a++;
	if (!s->dirs.n) {
		lg(HIBR_LERR, "popd: directory stack empty");
		return HIBR_FAIL;
	}
	if (a < ac) {
		if (!dir_idx(av[a], &k) || (pos = dir_pos(s, k)) < 0) {
			lg(HIBR_LERR, "popd: %s: directory stack index out of range", av[a]);
			return HIBR_FAIL;
		}
	}
	if (pos == 0) {
		lg(HIBR_LDBG, "popd +0 leaves for the next entry");
		pos = 1;
		nocd = 0;
	} else if (a < ac || nocd) {
		at = (long)s->dirs.n - pos;
		d = (char *)s->dirs.p[at];
		memmove(s->dirs.p + at, s->dirs.p + at + 1,
			(s->dirs.n - (size_t)at - 1) * sizeof *s->dirs.p);
		s->dirs.n--;
		free(d);
		return b_dirs(s, 1, av);
	}
	d = (char *)s->dirs.p[s->dirs.n - 1];
	if (dir_cd(s, d) != HIBR_OK)
		return HIBR_FAIL;
	s->dirs.n--;
	free(d);
	return b_dirs(s, 1, av);
}

/* Expand one prompt escape, returning the bytes consumed.

   bash's \NNN: the character of up to three octal digits -- \033 is how
   most prompts found on the web say escape, and it printed as itself until
   0.99.294 (Gitea #350). */
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
	case 'r':
		s_ch(o, '\r');
		return 1;
	case 'V':
		s_cat(o, HIBR_VER);
		return 1;
	case '!':
		s_num(o, (long)s->hist.n + 1);
		return 1;
	case '#':
		s_num(o, hs_count + 1);
		return 1;
	case 'l':
		c = isatty(0) ? ttyname(0) : 0;
		if (c) {
			char *b = strrchr(c, '/');

			s_cat(o, b ? b + 1 : c);
		} else {
			s_cat(o, "tty");
		}
		return 1;
	case '0': case '1': case '2': case '3':
	case '4': case '5': case '6': case '7': {
		int v = 0, n = 0;

		while (n < 3 && p[n] >= '0' && p[n] <= '7') {
			v = v * 8 + (p[n] - '0');
			n++;
		}
		s_ch(o, (char)(v & 255));
		return n;
	}
	case 'D':
		if (p[1] == '{') {
			const char *e = strchr(p + 2, '}');
			str f;

			if (!e)
				break;
			s_init(&f);
			s_add(&f, p + 2, (size_t)(e - p - 2));
			now = time(0);
			tm = localtime(&now);
			s_grow(o, 256);
			o->n += strftime(o->p + o->n, 256, f.n ? f.p : "%X", tm);
			o->p[o->n] = 0;
			s_free(&f);
			return (int)(e - p) + 1;
		}
		break;
	case 't':
	case 'T':
	case '@':
	case 'A':
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
		else if (*p == 'A')
			o->n += (size_t)strftime(o->p + o->n, 64, "%H:%M", tm);
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
	w = lx_word(&l, 0);
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
	int ost = s->st;
	struct hksave hs;
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
	hibr_set(s, "RET_RIGHT", "", 0);
	hk_save(s, &hs);
	s->bind = f ? 0 : 1;
	av[0] = (char *)nm;
	av[1] = 0;
	if (f)
		fn_call(s, f, 1, av);
	else
		b->fn(s, 1, av);
	r = xs(hibr_get(s, "RET") ? hibr_get(s, "RET") : "");
	free(ed_rtext);
	ed_rtext = hibr_get(s, "RET_RIGHT") && *hibr_get(s, "RET_RIGHT") ?
		   xs(hibr_get(s, "RET_RIGHT")) : 0;
	hk_restore(s, &hs);
	lg(HIBR_LDBG, "prompt hook %s produced %lu bytes", nm,
	   (unsigned long)strlen(r));
	return r;
}

/* Keep the shell state a hook run between two commands must not change --
   the status, a stop, try, a bind, an error, break, continue, return, and
   an exit -- and set it up for the hook: inside a try, no loop to leave. */
void hk_save(sh *s, struct hksave *h)
{
	h->st = s->st;
	h->stop = s->stop;
	h->intry = s->intry;
	h->bind = s->bind;
	h->xerr = s->xerr;
	h->brk = s->brk;
	h->cont = s->cont;
	h->ret = s->ret;
	h->quit = s->quit;
	s->intry = 1;
	s->brk = 0;
	s->cont = 0;
}

/* Put back what hk_save kept. */
void hk_restore(sh *s, struct hksave *h)
{
	s->st = h->st;
	s->stop = h->stop;
	s->intry = h->intry;
	s->bind = h->bind;
	s->xerr = h->xerr;
	s->brk = h->brk;
	s->cont = h->cont;
	s->ret = h->ret;
	s->quit = h->quit;
}

/* Source one startup file if there is one to read, saying either way.
   Debug-only, which is exactly why "does hibr even load .hibrc" turned into
   several rounds of the wrong theory (a Homebrew post_install writing it, a
   login-shell quirk) rather than one look under -d 2: this is the one place
   that knows the real path checked and whether it found anything there at
   all. Still below HIBR_LWRN's own default level, so a user who has simply
   never wanted a startup file is not nagged about it every login. */
int rc_one(sh *s, const char *path)
{
	char *args[2];

	if (access(path, R_OK) != 0) {
		lg(HIBR_LINF, "no startup file at %s", path);
		return 0;
	}
	args[0] = "source";
	args[1] = (char *)path;
	lg(HIBR_LINF, "reading %s", path);
	b_src(s, 2, args);
	return 1;
}

/* What a login shell reads: the system profile every shell on the machine
   gets, and then the first personal one of hibr's own two names and the
   POSIX ~/.profile. A login shell reads these whether or not it is
   interactive, as bash does with --login, which is what makes the flag
   usable from a script. hibr's own names come first so that login setup
   meant for this shell has a file of its own, rather than a block guarded
   inside a ~/.profile that bash and dash also read. */
void rc_login(sh *s)
{
	static const char *nm[3] = { "/.hibr_profile", "/.hibr_login",
				     "/.profile" };
	const char *home = hibr_get(s, "HOME");
	const char *sys = hibr_get(s, "HIBR_PROFILE");
	str b;
	int i;

	rc_one(s, sys && *sys ? sys : "/etc/profile");
	if (!home || !*home)
		return;
	s_init(&b);
	for (i = 0; i < 3; i++) {
		b.n = 0;
		s_cat(&b, home);
		s_cat(&b, nm[i]);
		if (rc_one(s, b.p))
			break;
	}
	s_free(&b);
}

/* And what it reads on the way out. Called from main rather than from
   sh_fini, which frees the shell: running script code out of a finaliser is
   how a use-after-free gets written. */
void rc_logout(sh *s)
{
	const char *home = hibr_get(s, "HOME");
	str b;

	if (!home || !*home)
		return;
	s_init(&b);
	s_cat(&b, home);
	s_cat(&b, "/.hibr_logout");
	rc_one(s, b.p);
	s_free(&b);
}

/* Read the startup file when the shell is interactive. An interactive login
   shell reads this *as well as* the profile above, which is where hibr
   parts company with bash: bash reads only the profile for a login shell,
   so a distribution's ~/.bash_profile has to remember to source ~/.bashrc
   and an alias that works in one terminal silently does not in another.
   zsh reads both too (ADR 0039). */
void rc_load(sh *s)
{
	const char *p = hibr_get(s, "HIBR_RC");
	const char *home;
	str b;

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
	rc_one(s, b.p);
	s_free(&b);
}

/* The resource a ulimit letter names, or -1 when it names none. */
int ul_res(int c)
{
	switch (c) {
	case 'c': return RLIMIT_CORE;
	case 'd': return RLIMIT_DATA;
	case 'f': return RLIMIT_FSIZE;
	case 'n': return RLIMIT_NOFILE;
	case 's': return RLIMIT_STACK;
	case 't': return RLIMIT_CPU;
	case 'u': return RLIMIT_NPROC;
	case 'v': return RLIMIT_AS;
	case 'l': return RLIMIT_MEMLOCK;
	}
	return -1;
}

/* What a ulimit letter is called, and what it is counted in. */
const char *ul_name(int c)
{
	switch (c) {
	case 'c': return "core file size (blocks)";
	case 'd': return "data seg size (kbytes)";
	case 'f': return "file size (blocks)";
	case 'n': return "open files";
	case 's': return "stack size (kbytes)";
	case 't': return "cpu time (seconds)";
	case 'u': return "max user processes";
	case 'v': return "virtual memory (kbytes)";
	case 'l': return "max locked memory (kbytes)";
	}
	return "";
}

/* Divide a limit into the units ulimit reports it in. */
long ul_scale(int c)
{
	if (c == 'd' || c == 's' || c == 'v' || c == 'l')
		return 1024;
	if (c == 'c' || c == 'f')
		return 512;
	return 1;
}

/* Print one limit the way ulimit -a does. */
void ul_show(int c, int hard, int label)
{
	struct rlimit rl;
	rlim_t v;

	if (getrlimit(ul_res(c), &rl) != 0)
		return;
	v = hard ? rl.rlim_max : rl.rlim_cur;
	if (label)
		printf("-%c: %-32s ", c, ul_name(c));
	if (v == RLIM_INFINITY)
		printf("unlimited\n");
	else
		printf("%ld\n", (long)(v / (rlim_t)ul_scale(c)));
}

/* Read or set a resource limit. */
int b_ulimit(sh *s, int ac, char **av)
{
	int i = 1, hard = 0, soft = 0, c = 0, all = 0, r;
	struct rlimit rl;
	const char *val = 0;
	rlim_t nv;
	long n;

	(void)s;
	for (; i < ac && av[i][0] == '-' && av[i][1]; i++) {
		char *f;
		for (f = av[i] + 1; *f; f++) {
			if (*f == 'H')
				hard = 1;
			else if (*f == 'S')
				soft = 1;
			else if (*f == 'a')
				all = 1;
			else if (ul_res(*f) >= 0)
				c = *f;
			else {
				lg(HIBR_LERR, "ulimit: -%c: unknown limit",
				   *f);
				return 2;
			}
		}
	}
	if (all) {
		const char *k;
		for (k = "cdfilnstuv"; *k; k++)
			if (ul_res(*k) >= 0)
				ul_show(*k, hard, 1);
		return HIBR_OK;
	}
	if (!c)
		c = 'f';
	if (i < ac)
		val = av[i];
	if (!val) {
		ul_show(c, hard && !soft, 0);
		return HIBR_OK;
	}
	r = ul_res(c);
	if (getrlimit(r, &rl) != 0) {
		lg(HIBR_LERR, "ulimit: %s", strerror(errno));
		return HIBR_FAIL;
	}
	if (!strcmp(val, "unlimited")) {
		nv = RLIM_INFINITY;
	} else if (!strcmp(val, "hard")) {
		nv = rl.rlim_max;
	} else if (!strcmp(val, "soft")) {
		nv = rl.rlim_cur;
	} else if (t_int(val, &n) && n >= 0) {
		nv = (rlim_t)n * (rlim_t)ul_scale(c);
	} else {
		lg(HIBR_LERR, "ulimit: %s: invalid number", val);
		return HIBR_FAIL;
	}
	lg(HIBR_LDBG, "ulimit sets the %s limit", hard && !soft ? "hard" :
	   soft && !hard ? "soft" : "hard and the soft");
	if (soft || !hard)
		rl.rlim_cur = nv;
	if (hard || !soft)
		rl.rlim_max = nv;
	if (setrlimit(r, &rl) != 0) {
		lg(HIBR_LERR, "ulimit: %s", strerror(errno));
		return HIBR_FAIL;
	}
	lg(HIBR_LDBG, "limit -%c set", c);
	return HIBR_OK;
}

/* Options that are not negotiable, and why is in the ADRs. */
int sh_optfix(const char *nm)
{
	if (!strcmp(nm, "extglob") || !strcmp(nm, "globstar") ||
	    !strcmp(nm, "expand_aliases") || !strcmp(nm, "sourcepath") ||
	    !strcmp(nm, "braceexpand") || !strcmp(nm, "hashall"))
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
	if (!strcmp(nm, "patsub_replacement"))
		return O_PATSUB;
	if (!strcmp(nm, "execfail"))
		return O_EXECFAIL;
	if (!strcmp(nm, "allexport"))
		return O_ALLEXP;
	if (!strcmp(nm, "nocasematch"))
		return O_NOCASEMATCH;
	if (!strcmp(nm, "inherit_errexit"))
		return O_INHERITERR;
	if (!strcmp(nm, "agent"))
		return O_AGENT;
	if (!strcmp(nm, "checkfirst"))
		return O_CHECK;
	if (!strcmp(nm, "keepgoing"))
		return O_KEEPGOING;
	if (!strcmp(nm, "logtime"))
		return O_LOGTIME;
	if (!strcmp(nm, "noglob"))
		return O_NOGLOB;
	return 0;
}

/* Agent mode's companions: set -u, strict expansion, checkfirst, and /dev/null for a terminal on standard input. */
void sh_agent(sh *s)
{
	int fd;

	s->uset = 1;
	s->strictg = 1;
	s->sopt |= O_CHECK;
	sh_sfl(s);
	if (isatty(0) && (fd = open("/dev/null", O_RDONLY)) >= 0) {
		dup2(fd, 0);
		close(fd);
	}
	lg(HIBR_LDBG, "agent mode on");
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
		return &s->strictg;
	return 0;
}

/* Every option name, in one list, whatever spelling asked for it. */
const char *sh_optnames[] = { "errexit", "nounset", "xtrace", "noclobber",
			      "noexec", "histexpand", "strict", "nullglob",
			      "nocaseglob", "dotglob", "failglob",
			      "nocasematch", "extglob", "globstar",
			      "expand_aliases", "pipefail", "inherit_errexit",
			      "agent", "checkfirst", "keepgoing", "logtime",
			      "noglob", "patsub_replacement", "execfail",
			      "allexport", "braceexpand", "hashall", 0 };

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

/* Set one option by name, refusing the ones that do not move.

   noexec turning on stops the rest of the list it was turned on in, which
   is what makes `set -n; echo X` print nothing as in bash: the flag it
   borrows is one ex() already loads in its prologue, so the loop pays no
   instruction for this. The read loops clear it again before the next
   line, or a later syntax error would go unreported -- which is the whole
   point of noexec. An interactive shell ignores noexec, as bash's own
   documentation says it does, so it is never armed there. On the 0->1 edge
   only: hibr -n sets this from main, before anything has run. */
int sh_optset(sh *s, const char *nm, int on)
{
	int *f = sh_optflag(s, nm);
	int fix = sh_optfix(nm);
	unsigned b;

	if (fix && !on && !strcmp(nm, "hashall")) {
		lg(HIBR_LDBG, "hashall off: commands are still remembered, and a remembered one is checked before it is used");
		return HIBR_OK;
	}
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
		if (f == &s->noexec && on && !*f && !s->it)
			s->stop = 1;
		*f = on;
		if (f == &s->strictg)
			sh_sfl(s);
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
	if (on && b == O_AGENT)
		sh_agent(s);
	return HIBR_OK;
}

/* The option one short flag letter names, or null when it is not one. */
const char *sh_optltr(int c)
{
	switch (c) {
	case 'e':
		return "errexit";
	case 'u':
		return "nounset";
	case 'x':
		return "xtrace";
	case 'C':
		return "noclobber";
	case 'H':
		return "histexpand";
	case 'S':
		return "strict";
	case 'n':
		return "noexec";
	case 'f':
		return "noglob";
	case 'a':
		return "allexport";
	case 'B':
		return "braceexpand";
	case 'h':
		return "hashall";
	}
	return 0;
}

/* Set one option by its short letter, the one reader `set` and the command line share. */
int sh_optch(sh *s, int c, int on)
{
	const char *nm = sh_optltr(c);

	if (!nm)
		return HIBR_FAIL;
	return sh_optset(s, nm, on);
}

/* The letters $- shows: every short option that is on, then how this shell was started. */
char *sh_dash(sh *s)
{
	const char *ord = "aefhuxBCHSn";
	str b;
	char *r;
	int i;

	s_init(&b);
	for (i = 0; ord[i]; i++)
		if (sh_optget(s, sh_optltr(ord[i])) == 1)
			s_ch(&b, ord[i]);
	if (s->it)
		s_ch(&b, 'i');
	if (s->sopt & O_DASHC)
		s_ch(&b, 'c');
	if (s->sopt & O_DASHS)
		s_ch(&b, 's');
	r = ar_dup(s->xa, b.p ? b.p : "", b.n);
	s_free(&b);
	return r;
}

/* One option, as a table row (0), or as the command that puts it back:
   set -o/+o (1), the form `set +o` prints, or shopt -s/-u (2), the form
   `shopt -p` prints. Both commands reach one namespace (ADR 0017), so
   either restores every option. */
void sh_opt1(sh *s, str *o, const char *nm, int style)
{
	int v = sh_optget(s, nm);
	size_t w;

	if (style == 1)
		s_cat(o, v ? "set -o " : "set +o ");
	else if (style == 2)
		s_cat(o, v ? "shopt -s " : "shopt -u ");
	if (style) {
		s_cat(o, nm);
	} else {
		s_cat(o, nm);
		s_ch(o, ' ');
		for (w = strlen(nm) + 1; w < 16; w++)
			s_ch(o, ' ');
		s_cat(o, v ? "on" : "off");
	}
	s_ch(o, '\n');
}

/* Every option, in one of sh_opt1's three forms. */
void sh_optlist(sh *s, int style)
{
	str o;
	int i;

	s_init(&o);
	for (i = 0; sh_optnames[i]; i++)
		sh_opt1(s, &o, sh_optnames[i], style);
	b_out(s, &o);
	s_free(&o);
}

/* One namespace of options, reached by shopt or by set -o alike. */
int b_shopt(sh *s, int ac, char **av)
{
	int i = 1, on = -1, quiet = 0, n = 0, v, pr = 0;
	str o;

	for (; i < ac && av[i][0] == '-' && av[i][1]; i++) {
		if (!strcmp(av[i], "-p"))
			pr = 1;
		else if (!strcmp(av[i], "-s"))
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
			sh_optlist(s, pr ? 2 : 0);
		return HIBR_OK;
	}
	if (pr && on < 0) {
		s_init(&o);
		for (; i < ac; i++) {
			if (sh_optget(s, av[i]) < 0) {
				lg(HIBR_LERR, "shopt: %s: no such option",
				   av[i]);
				n = 1;
				continue;
			}
			sh_opt1(s, &o, av[i], 2);
		}
		b_out(s, &o);
		s_free(&o);
		return n ? HIBR_FAIL : HIBR_OK;
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
				printf("%-15s %s\n", av[i], v ? "on" : "off");
			if (!v)
				n = 1;
			continue;
		}
		if (sh_optset(s, av[i], on) != HIBR_OK)
			n = 1;
	}
	return n ? HIBR_FAIL : HIBR_OK;
}

/* Run av as a program found on PATH, never a builtin or a function, and
   wait for it: command's own path, and a builtin's way out to the real
   program for an option it does not implement. */
int cmd_ext(sh *s, char **av, const char *who)
{
	char *path, **env;
	pid_t pid;
	int w;

	path = findx(s, av[0]);
	if (!path && strchr(av[0], '/'))
		return ex_why(av[0], 0);
	if (!path) {
		lg(HIBR_LERR, "%s: %s: not found", who, av[0]);
		return HIBR_NOCMD;
	}
	if ((s->sopt & O_PLAN) && !pl_prog(s, &av, path)) {
		free(path);
		return HIBR_FAIL;
	}
	env = v_envp(s, 0);
	fflush(0);
	pid = (pid_t)jc_fork(s);
	if (pid == 0) {
		execve(path, av, env);
		ex_noshebang(s, path, av, env);
		lg(HIBR_LERR, "%s: %s", path, strerror(errno));
		_exit(errno == ENOENT ? HIBR_NOCMD : HIBR_NOEXEC);
	}
	free(path);
	envfree(env);
	jc_waitt(s, pid, pid, &w, 0);
	return WIFEXITED(w) ? WEXITSTATUS(w) : HIBR_FAIL;
}

/* command: run a name skipping functions and aliases, or with -v and -V
   say what it is; -p looks along the default path; -- ends the options. */
int b_command(sh *s, int ac, char **av)
{
	int i = 1, v = 0, vb = 0, dp = 0, st;
	const char *f;
	char **oam = s->amask;

	for (; i < ac && av[i][0] == '-' && av[i][1]; i++) {
		if (!strcmp(av[i], "--")) {
			i++;
			break;
		}
		for (f = av[i] + 1; *f; f++) {
			if (*f == 'v')
				v = 1;
			else if (*f == 'V')
				v = vb = 1;
			else if (*f == 'p')
				dp = 1;
			else {
				lg(HIBR_LERR, "command: -%c: invalid option", *f);
				return 2;
			}
		}
	}
	if (oam)
		s->amask = oam + (i - 1);
	if (dp) {
		const char *op = x_pathover;

		lg(HIBR_LDBG, "command -p looks along the default path, leaving PATH alone");
		x_pathover = HIBR_DEFPATH;
		st = b_command1(s, v, vb, ac - i + 1, av + i - 1);
		x_pathover = op;
	} else {
		st = b_command1(s, v, vb, ac - i + 1, av + i - 1);
	}
	s->amask = oam;
	return st;
}

/* command's work once its options are read: av[1] is the name. */
int b_command1(sh *s, int v, int vb, int ac, char **av)
{
	const hibr_bi *b;

	if (v) {
		int k, rc = HIBR_OK;

		if (ac < 2) {
			lg(HIBR_LERR, "usage: command -v name");
			return 2;
		}
		rc = HIBR_FAIL;
		for (k = 1; k < ac; k++)
			if (cmd_what(s, av[k], vb) == HIBR_OK)
				rc = HIBR_OK;
		return rc;
	}
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
	return cmd_ext(s, av + 1, "command");
}

/* True if a word is one of the shell's reserved words. */
int kw_name(const char *t)
{
	static const char *k[] = { "!", "[[", "]]", "{", "}", "case", "do",
				   "done", "elif", "else", "esac", "fi", "for",
				   "fn", "if", "in", "select", "then", "time",
				   "until", "while", 0 };
	int i;

	for (i = 0; k[i]; i++)
		if (!strcmp(t, k[i]))
			return 1;
	return 0;
}

/* What a name resolves to, in the order the shell looks: an alias, a
   keyword, a function, a builtin (a module's or the shell's), a file -- the
   one resolver type and command -v share (Gitea #458). *path is the file's,
   to be freed, for NK_FILE. -f leaves functions out. */
int nm_kind(sh *s, const char *nm, int nofn, char **path)
{
	*path = 0;
	if (al_get(s, nm))
		return NK_ALIAS;
	if (kw_name(nm))
		return NK_KEYWORD;
	if (!nofn && fn_find(s, nm))
		return NK_FUNC;
	if (m_find(s, nm))
		return NK_MODBI;
	if (bi_find(nm))
		return NK_BUILTIN;
	if ((*path = findx(s, nm)) != 0)
		return NK_FILE;
	return NK_NONE;
}

/* Say what a name resolves to, for command -v and -V. */
int cmd_what(sh *s, const char *nm, int vb)
{
	char *path;
	str o;
	int ok = 1, k = nm_kind(s, nm, 0, &path);

	s_init(&o);
	if (k == NK_ALIAS) {
		if (vb) {
			s_cat(&o, nm);
			s_cat(&o, " is aliased to `");
			s_cat(&o, al_get(s, nm));
			s_ch(&o, '\'');
		} else {
			s_cat(&o, "alias ");
			s_cat(&o, nm);
			s_ch(&o, '=');
			s_sq(&o, al_get(s, nm));
		}
	} else if (k == NK_FUNC) {
		s_cat(&o, nm);
		if (vb)
			s_cat(&o, " is a function");
	} else if (k == NK_KEYWORD) {
		s_cat(&o, nm);
		if (vb)
			s_cat(&o, " is a shell keyword");
	} else if (k == NK_MODBI || k == NK_BUILTIN) {
		s_cat(&o, nm);
		if (vb)
			s_cat(&o, " is a shell builtin");
	} else if (k == NK_FILE) {
		if (vb) {
			s_cat(&o, nm);
			s_cat(&o, " is ");
		}
		s_cat(&o, path);
		free(path);
	} else {
		ok = 0;
	}
	if (!ok) {
		if (vb)
			lg(HIBR_LERR, "command: %s: not found", nm);
		s_free(&o);
		return HIBR_FAIL;
	}
	hibr_ret(s, o.p ? o.p : "");
	if (!s->bind)
		printf("%s\n", o.p ? o.p : "");
	s_free(&o);
	return HIBR_OK;
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

/* A mask as `umask -S` writes it: what each class is allowed, not denied. */
void um_sym(mode_t m, str *o)
{
	const char *cl = "ugo";
	int i, sh, p;

	for (i = 0; i < 3; i++) {
		sh = 6 - 3 * i;
		p = (int)(~m >> sh) & 7;
		if (i)
			s_ch(o, ',');
		s_ch(o, cl[i]);
		s_ch(o, '=');
		if (p & 4)
			s_ch(o, 'r');
		if (p & 2)
			s_ch(o, 'w');
		if (p & 1)
			s_ch(o, 'x');
	}
}

/* Apply a symbolic mode (`u=rwx,g=rx,o=`, `a-w`, `g+w`) to mask m, as bash
   does: each clause is who, one operator and what they may do; 0 and an
   error when a character or an operator is not one it knows. */
int um_parse(const char *t, mode_t *m)
{
	unsigned allow = ~(unsigned)*m & 0777, who, perm, bits;
	const char *p = t;
	char op;

	for (;;) {
		who = 0;
		for (; *p == 'u' || *p == 'g' || *p == 'o' || *p == 'a'; p++)
			who |= *p == 'u' ? 0700 : *p == 'g' ? 070 : *p == 'o' ? 07 : 0777;
		if (!who)
			who = 0777;
		if (*p != '=' && *p != '+' && *p != '-') {
			lg(HIBR_LERR, "umask: `%c': invalid symbolic mode operator", *p ? *p : ' ');
			return 0;
		}
		{
			op = *p++;
			perm = 0;
			for (; *p && *p != ','; p++) {
				if (*p == 'r')
					perm |= 4;
				else if (*p == 'w')
					perm |= 2;
				else if (*p == 'x')
					perm |= 1;
				else {
					lg(HIBR_LERR, "umask: `%c': invalid symbolic mode character", *p);
					return 0;
				}
			}
			bits = (perm << 6 | perm << 3 | perm) & who;
			if (op == '=')
				allow = (allow & ~who) | bits;
			else if (op == '+')
				allow |= bits;
			else
				allow &= ~bits;
		}
		if (*p != ',')
			break;
		p++;
	}
	if (*p) {
		lg(HIBR_LERR, "umask: `%c': invalid symbolic mode character", *p);
		return 0;
	}
	*m = (mode_t)(~allow & 0777);
	return 1;
}

/* Show or set the file creation mask: octal or symbolic, `-S` to show it
   as what each class may do, `-p` in a form that sets it again. */
int b_umask(sh *s, int ac, char **av)
{
	mode_t m;
	int i = 1, sym = 0, re = 0;
	char *e;
	long v;
	str o;

	(void)s;
	for (; i < ac && av[i][0] == '-' && av[i][1]; i++) {
		if (!strcmp(av[i], "--")) {
			i++;
			break;
		}
		for (e = av[i] + 1; *e; e++) {
			if (*e == 'S')
				sym = 1;
			else if (*e == 'p')
				re = 1;
			else {
				lg(HIBR_LERR, "umask: -%c: invalid option", *e);
				return 2;
			}
		}
	}
	m = umask(0);
	umask(m);
	if (i < ac) {
		if (av[i][0] >= '0' && av[i][0] <= '9') {
			v = strtol(av[i], &e, 8);
			if (*e || v < 0 || v > 0777) {
				lg(HIBR_LERR, "umask: %s: octal number out of range", av[i]);
				return 1;
			}
			m = (mode_t)v;
		} else if (!um_parse(av[i], &m)) {
			return 1;
		}
		umask(m);
		if (!sym)
			return HIBR_OK;
	}
	s_init(&o);
	if (re)
		s_cat(&o, sym ? "umask -S " : "umask ");
	if (sym)
		um_sym(m, &o);
	else
		s_fmt(&o, "%04o", (unsigned)m);
	printf("%s\n", o.p);
	s_free(&o);
	return HIBR_OK;
}

/* Microseconds between two timevals. */
long long bi_tvus(struct timeval *a, struct timeval *b)
{
	return (long long)(b->tv_sec - a->tv_sec) * 1000000 +
	       (b->tv_usec - a->tv_usec);
}

/* One of time's three figures, as %[prec][l]R prints it in bash. */
void bi_tione(str *o, long long us, int prec, int lng)
{
	long long sec = us / 1000000, ms = us % 1000000 / 1000;

	if (lng)
		s_fmt(o, "%lldm%lld", sec / 60, sec % 60);
	else
		s_fmt(o, "%lld", sec);
	if (prec > 0)
		s_fmt(o, ".%0*lld", prec, prec == 1 ? ms / 100 : prec == 2 ? ms / 10 : ms);
	if (lng)
		s_ch(o, 's');
}

/* Fill o from a TIMEFORMAT, as bash reads one; 0 on a character it refuses. */
int bi_tifmt(str *o, const char *f, long long *t)
{
	int prec, lng;
	long long cpu;

	for (; *f; f++) {
		if (*f != '%' || !f[1]) {
			s_ch(o, *f);
			continue;
		}
		f++;
		if (*f == '%') {
			s_ch(o, '%');
			continue;
		}
		if (*f == 'P') {
			cpu = t[0] > 0 ? (t[1] + t[2]) * 10000 / t[0] : 0;
			s_fmt(o, "%lld.%02lld", cpu / 100, cpu % 100);
			continue;
		}
		prec = 3;
		lng = 0;
		if (*f >= '0' && *f <= '9') {
			prec = *f - '0' > 3 ? 3 : *f - '0';
			f++;
		}
		if (*f == 'l') {
			lng = 1;
			f++;
		}
		if (*f != 'R' && *f != 'U' && *f != 'S') {
			lg(HIBR_LERR, "TIMEFORMAT: `%c': invalid format character", *f);
			return 0;
		}
		bi_tione(o, t[*f == 'R' ? 0 : *f == 'U' ? 1 : 2], prec, lng);
	}
	return 1;
}

/* Run a pipeline under the time reserved word and report what it took on
   standard error: TIMEFORMAT when set, an empty one printing nothing, and
   POSIX's three lines for time -p, as in bash. The shell's own time counts
   as well as its children's, so a timed loop of builtins is not nothing. */
int ex_time(sh *s, node *n, int t)
{
	struct timeval a, b;
	struct rusage s0, s1, c0, c1;
	long long tv[3];
	const char *f;
	str o;
	int st;

	gettimeofday(&a, 0);
	getrusage(RUSAGE_SELF, &s0);
	getrusage(RUSAGE_CHILDREN, &c0);
	s->tst = t;
	st = n->l ? ex(s, n->l) : 0;
	gettimeofday(&b, 0);
	getrusage(RUSAGE_SELF, &s1);
	getrusage(RUSAGE_CHILDREN, &c1);
	if (s->ret)
		return s->st = st;
	tv[0] = bi_tvus(&a, &b);
	tv[1] = bi_tvus(&s0.ru_utime, &s1.ru_utime) +
		bi_tvus(&c0.ru_utime, &c1.ru_utime);
	tv[2] = bi_tvus(&s0.ru_stime, &s1.ru_stime) +
		bi_tvus(&c0.ru_stime, &c1.ru_stime);
	if (n->f & 1)
		f = "real %2R\nuser %2U\nsys %2S";
	else if (!(f = hibr_get(s, "TIMEFORMAT")))
		f = "\nreal\t%3lR\nuser\t%3lU\nsys\t%3lS";
	else if (!*f)
		return s->st = st;
	s_init(&o);
	if (bi_tifmt(&o, f, tv)) {
		s_ch(&o, '\n');
		fflush(0);
		fputs(o.p, stderr);
		fflush(stderr);
	}
	s_free(&o);
	return s->st = st;
}

/* One time as times prints it: minutes, then seconds to the millisecond. */
void bi_tvfmt(str *o, long sec, long usec)
{
	long ms = sec * 1000 + usec / 1000;

	s_fmt(o, "%ldm%ld.%03lds", ms / 60000, ms / 1000 % 60, ms % 1000);
}

/* The shell's own user and system time, then its children's, as bash's
   times prints them. Bound with :=, the same two lines fill the slot and
   nothing is printed -- which is how the desktop measures what it costs,
   the programs it ran included, the same way on every platform (Gitea
   #343). Children are those already waited for, as getrusage counts. */
int b_times(sh *s, int ac, char **av)
{
	struct rusage me, ch;
	str o;

	(void)ac;
	(void)av;
	getrusage(RUSAGE_SELF, &me);
	getrusage(RUSAGE_CHILDREN, &ch);
	s_init(&o);
	bi_tvfmt(&o, (long)me.ru_utime.tv_sec, (long)me.ru_utime.tv_usec);
	s_ch(&o, ' ');
	bi_tvfmt(&o, (long)me.ru_stime.tv_sec, (long)me.ru_stime.tv_usec);
	s_ch(&o, '\n');
	bi_tvfmt(&o, (long)ch.ru_utime.tv_sec, (long)ch.ru_utime.tv_usec);
	s_ch(&o, ' ');
	bi_tvfmt(&o, (long)ch.ru_stime.tv_sec, (long)ch.ru_stime.tv_usec);
	hibr_ret(s, o.p);
	if (!s->bind)
		printf("%s\n", o.p);
	s_free(&o);
	return HIBR_OK;
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
	int quiet = spec[0] == ':';
	const char *cur, *hit;
	char c;
	str one, nx;

	if (ac < 2) {
		lg(HIBR_LERR, "usage: getopts optstring var [args...]");
		return 2;
	}
	if (quiet)
		spec++;
	if (idx < 1) {
		lg(HIBR_LDBG, "getopts: OPTIND %s read as 1", oi);
		idx = 1;
		s->optpos = 0;
	}
	if (idx != s->optind)
		s->optpos = 0;
	hibr_set(s, "OPTARG", "", 0);
	if (idx > n) {
		hibr_set(s, nm, "?", 0);
		return HIBR_FAIL;
	}
	cur = args[idx - 1];
	if (!cur || cur[0] != '-' || !cur[1]) {
		hibr_set(s, nm, "?", 0);
		return HIBR_FAIL;
	}
	if (!strcmp(cur, "--")) {
		s->optpos = 0;
		s->optind = idx + 1;
		s_init(&nx);
		s_num(&nx, (long)(idx + 1));
		hibr_set(s, "OPTIND", nx.p, 0);
		s_free(&nx);
		hibr_set(s, nm, "?", 0);
		return HIBR_FAIL;
	}
	if (!s->optpos)
		s->optpos = 1;
	c = cur[s->optpos++];
	hit = c ? strchr(spec, c) : 0;
	if (!cur[s->optpos]) {
		s->optpos = 0;
		idx++;
	}
	s_init(&one);
	s_ch(&one, c);
	if (!hit || c == ':') {
		hibr_set(s, nm, "?", 0);
		if (quiet)
			hibr_set(s, "OPTARG", one.p, 0);
		else
			lg(HIBR_LWRN, "getopts: illegal option -%s", one.p);
		s_free(&one);
	} else if (hit[1] == ':') {
		hibr_set(s, nm, one.p, 0);
		if (s->optpos) {
			hibr_set(s, "OPTARG", cur + s->optpos, 0);
			s->optpos = 0;
			idx++;
		} else if (idx <= n) {
			hibr_set(s, "OPTARG", args[idx - 1], 0);
			idx++;
		} else if (quiet) {
			hibr_set(s, nm, ":", 0);
			hibr_set(s, "OPTARG", one.p, 0);
		} else {
			hibr_set(s, nm, "?", 0);
			lg(HIBR_LWRN, "getopts: option -%s needs an argument",
			   one.p);
		}
		s_free(&one);
	} else {
		hibr_set(s, nm, one.p, 0);
		s_free(&one);
	}
	s->optind = idx;
	s_init(&nx);
	s_num(&nx, (long)idx);
	hibr_set(s, "OPTIND", nx.p, 0);
	s_free(&nx);
	return HIBR_OK;
}
