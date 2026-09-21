#include "hibr.h"
#include <dirent.h>
#include <errno.h>
#include <grp.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

struct lsent { char *nm, *path, *link; struct stat st; int ok; };
struct lsopt { int lng, all, almost, human, bytime, bysize, rev, dironly, one,
	       classify, color, quiet; };

/* Order entries by name. */
int ls_byname(const void *a, const void *b)
{
	return strcmp((*(struct lsent *const *)a)->nm,
		      (*(struct lsent *const *)b)->nm);
}

/* Order entries newest first. */
int ls_bytime(const void *a, const void *b)
{
	const struct lsent *x = *(struct lsent *const *)a;
	const struct lsent *y = *(struct lsent *const *)b;

	if (x->st.st_mtime != y->st.st_mtime)
		return x->st.st_mtime < y->st.st_mtime ? 1 : -1;
	return strcmp(x->nm, y->nm);
}

/* Order entries largest first. */
int ls_bysize(const void *a, const void *b)
{
	const struct lsent *x = *(struct lsent *const *)a;
	const struct lsent *y = *(struct lsent *const *)b;

	if (x->st.st_size != y->st.st_size)
		return x->st.st_size < y->st.st_size ? 1 : -1;
	return strcmp(x->nm, y->nm);
}

/* Render a size, optionally in human units. */
void ls_size(str *o, long long n, int human)
{
	const char *u = "BKMGTP";
	double v = (double)n;
	int i = 0;
	char *t;

	if (!human) {
		s_num(o, (long)n);
		return;
	}
	while (v >= 1024 && u[i + 1]) {
		v /= 1024;
		i++;
	}
	t = xm(32);
	if (i == 0)
		sprintf(t, "%lld", n);
	else if (v < 10)
		sprintf(t, "%.1f%c", v, u[i]);
	else
		sprintf(t, "%.0f%c", v, u[i]);
	s_cat(o, t);
	free(t);
}

/* Render a mode as the classic ten character string. */
void ls_mode(str *o, mode_t m)
{
	s_ch(o, S_ISDIR(m) ? 'd' : S_ISLNK(m) ? 'l' : S_ISCHR(m) ? 'c' :
	       S_ISBLK(m) ? 'b' : S_ISFIFO(m) ? 'p' : S_ISSOCK(m) ? 's' : '-');
	s_ch(o, m & S_IRUSR ? 'r' : '-');
	s_ch(o, m & S_IWUSR ? 'w' : '-');
	s_ch(o, m & S_ISUID ? (m & S_IXUSR ? 's' : 'S') : m & S_IXUSR ? 'x' : '-');
	s_ch(o, m & S_IRGRP ? 'r' : '-');
	s_ch(o, m & S_IWGRP ? 'w' : '-');
	s_ch(o, m & S_ISGID ? (m & S_IXGRP ? 's' : 'S') : m & S_IXGRP ? 'x' : '-');
	s_ch(o, m & S_IROTH ? 'r' : '-');
	s_ch(o, m & S_IWOTH ? 'w' : '-');
	s_ch(o, m & S_ISVTX ? (m & S_IXOTH ? 't' : 'T') : m & S_IXOTH ? 'x' : '-');
}

/* Colour escape for an entry, or empty. */
const char *ls_hue(struct lsent *e, struct lsopt *op)
{
	if (!op->color || !e->ok)
		return "";
	if (S_ISLNK(e->st.st_mode))
		return "\033[36m";
	if (S_ISDIR(e->st.st_mode))
		return "\033[1;34m";
	if (e->st.st_mode & 0111)
		return "\033[32m";
	return "";
}

/* Classification suffix for -F. */
char ls_tag(struct lsent *e, struct lsopt *op)
{
	if (!op->classify || !e->ok)
		return 0;
	if (S_ISDIR(e->st.st_mode))
		return '/';
	if (S_ISLNK(e->st.st_mode))
		return '@';
	if (S_ISFIFO(e->st.st_mode))
		return '|';
	if (S_ISSOCK(e->st.st_mode))
		return '=';
	if (e->st.st_mode & 0111)
		return '*';
	return 0;
}

/* Print one entry in long format. */
void ls_long(struct lsent *e, struct lsopt *op, int wl, int wu, int wg, int ws)
{
	str o;
	struct passwd *pw;
	struct group *gr;
	struct tm *tm;
	char *when = xm(32);
	time_t now = time(0);

	s_init(&o);
	ls_mode(&o, e->st.st_mode);
	printf("%s %*lu ", o.p, wl, (unsigned long)e->st.st_nlink);
	pw = getpwuid(e->st.st_uid);
	gr = getgrgid(e->st.st_gid);
	printf("%-*s %-*s ", wu, pw ? pw->pw_name : "?", wg, gr ? gr->gr_name : "?");
	o.n = 0;
	ls_size(&o, (long long)e->st.st_size, op->human);
	printf("%*s ", ws, o.p);
	tm = localtime(&e->st.st_mtime);
	strftime(when, 32,
		 now - e->st.st_mtime > 15552000 ? "%b %e  %Y" : "%b %e %H:%M", tm);
	printf("%s %s%s%s", when, ls_hue(e, op), e->nm, op->color ? "\033[0m" : "");
	if (e->link)
		printf(" -> %s", e->link);
	{
		char c = ls_tag(e, op);
		if (c)
			putchar(c);
	}
	putchar('\n');
	s_free(&o);
	free(when);
}

/* Print entries in columns that fit the terminal. */
void ls_cols(struct lsent **es, size_t n, struct lsopt *op)
{
	struct winsize w;
	size_t width = 80, maxw = 0, i, r, c, cols, rows;
	char t;

	if (ioctl(1, TIOCGWINSZ, &w) == 0 && w.ws_col > 0)
		width = w.ws_col;
	for (i = 0; i < n; i++) {
		size_t l = strlen(es[i]->nm) + (ls_tag(es[i], op) ? 1 : 0);
		if (l > maxw)
			maxw = l;
	}
	maxw += 2;
	cols = op->one || !isatty(1) ? 1 : width / maxw;
	if (!cols)
		cols = 1;
	rows = (n + cols - 1) / cols;
	for (r = 0; r < rows; r++) {
		for (c = 0; c < cols; c++) {
			i = c * rows + r;
			if (i >= n)
				break;
			t = ls_tag(es[i], op);
			printf("%s%s%s", ls_hue(es[i], op), es[i]->nm,
			       op->color ? "\033[0m" : "");
			if (t)
				putchar(t);
			if (c + 1 < cols && (c + 1) * rows + r < n)
				printf("%*s", (int)(maxw - strlen(es[i]->nm) - (t ? 1 : 0)),
				       "");
		}
		putchar('\n');
	}
}

/* Build an entry for a path. */
struct lsent *ls_ent(const char *dir, const char *nm)
{
	struct lsent *e = xm(sizeof *e);
	str p;
	char *buf;
	ssize_t n;

	memset(e, 0, sizeof *e);
	e->nm = xs(nm);
	s_init(&p);
	if (dir && *dir) {
		s_cat(&p, dir);
		if (p.p[p.n - 1] != '/')
			s_ch(&p, '/');
	}
	s_cat(&p, nm);
	e->path = p.p;
	e->ok = lstat(e->path, &e->st) == 0;
	if (e->ok && S_ISLNK(e->st.st_mode)) {
		buf = xm(4096);
		n = readlink(e->path, buf, 4095);
		if (n >= 0) {
			buf[n] = 0;
			e->link = xs(buf);
		}
		free(buf);
	}
	return e;
}

/* Release an entry. */
void ls_free(struct lsent *e)
{
	free(e->nm);
	free(e->path);
	free(e->link);
	free(e);
}

/* Sort, print and record a set of entries. */
void ls_emit(sh *s, struct lsent **es, size_t n, struct lsopt *op, vec *names)
{
	size_t i, half;
	int wl = 1, wu = 1, wg = 1, ws = 1;
	str t;

	if (n > 1)
		qsort(es, n, sizeof *es,
		      op->bytime ? ls_bytime : op->bysize ? ls_bysize : ls_byname);
	if (op->rev)
		for (i = 0, half = n / 2; i < half; i++) {
			struct lsent *x = es[i];
			es[i] = es[n - 1 - i];
			es[n - 1 - i] = x;
		}
	for (i = 0; i < n; i++)
		v_add(names, xs(es[i]->nm));
	if (op->quiet)
		return;
	if (!op->lng) {
		if (n)
			ls_cols(es, n, op);
		return;
	}
	for (i = 0; i < n; i++) {
		struct passwd *pw = getpwuid(es[i]->st.st_uid);
		struct group *gr = getgrgid(es[i]->st.st_gid);
		int l;
		s_init(&t);
		s_num(&t, (long)es[i]->st.st_nlink);
		if ((int)t.n > wl)
			wl = (int)t.n;
		t.n = 0;
		ls_size(&t, (long long)es[i]->st.st_size, op->human);
		if ((int)t.n > ws)
			ws = (int)t.n;
		s_free(&t);
		l = pw ? (int)strlen(pw->pw_name) : 1;
		if (l > wu)
			wu = l;
		l = gr ? (int)strlen(gr->gr_name) : 1;
		if (l > wg)
			wg = l;
	}
	for (i = 0; i < n; i++)
		ls_long(es[i], op, wl, wu, wg, ws);
}

/* List directories and files without leaving the shell. */
int m_ls(sh *s, int ac, char **av)
{
	struct lsopt op;
	vec paths = { 0, 0, 0 }, names = { 0, 0, 0 };
	int i, rc = HIBR_OK;
	size_t p;

	memset(&op, 0, sizeof op);
	op.color = isatty(1);
	op.quiet = s->bind;
	for (i = 1; i < ac; i++) {
		const char *a = av[i];
		if (a[0] != '-' || !a[1] || !strcmp(a, "--")) {
			if (!strcmp(a, "--")) {
				for (i++; i < ac; i++)
					v_add(&paths, av[i]);
				break;
			}
			v_add(&paths, av[i]);
			continue;
		}
		if (!strcmp(a, "--color")) {
			op.color = 1;
			continue;
		}
		if (!strcmp(a, "--no-color")) {
			op.color = 0;
			continue;
		}
		for (a++; *a; a++) {
			switch (*a) {
			case 'l': op.lng = 1; break;
			case 'a': op.all = 1; break;
			case 'A': op.almost = 1; break;
			case 'h': op.human = 1; break;
			case 't': op.bytime = 1; break;
			case 'S': op.bysize = 1; break;
			case 'r': op.rev = 1; break;
			case 'd': op.dironly = 1; break;
			case '1': op.one = 1; break;
			case 'F': op.classify = 1; break;
			case 'G': op.color = 1; break;
			case 'q': op.quiet = 1; break;
			default:
				lg(HIBR_LERR, "ls: -%c: unknown option", *a);
				v_free(&paths);
				return 2;
			}
		}
	}
	if (!paths.n)
		v_add(&paths, ".");
	for (p = 0; p < paths.n; p++) {
		const char *path = (char *)paths.p[p];
		struct stat st;
		DIR *d;
		struct dirent *de;
		vec es = { 0, 0, 0 };
		size_t k;
		if (stat(path, &st) < 0) {
			lg(HIBR_LERR, "ls: %s: %s", path, strerror(errno));
			rc = HIBR_FAIL;
			continue;
		}
		if (!S_ISDIR(st.st_mode) || op.dironly) {
			v_add(&es, ls_ent(0, path));
			ls_emit(s, (struct lsent **)es.p, es.n, &op, &names);
			for (k = 0; k < es.n; k++)
				ls_free(es.p[k]);
			v_free(&es);
			continue;
		}
		d = opendir(path);
		if (!d) {
			lg(HIBR_LERR, "ls: %s: %s", path, strerror(errno));
			rc = HIBR_FAIL;
			continue;
		}
		if (paths.n > 1 && !op.quiet)
			printf("%s%s:\n", p ? "\n" : "", path);
		while ((de = readdir(d))) {
			if (de->d_name[0] == '.') {
				if (!op.all && !op.almost)
					continue;
				if (op.almost && (!strcmp(de->d_name, ".") ||
						  !strcmp(de->d_name, "..")))
					continue;
			}
			v_add(&es, ls_ent(path, de->d_name));
		}
		closedir(d);
		ls_emit(s, (struct lsent **)es.p, es.n, &op, &names);
		for (k = 0; k < es.n; k++)
			ls_free(es.p[k]);
		v_free(&es);
	}
	fflush(stdout);
	hibr_retn(s, (char **)names.p, names.n);
	for (p = 0; p < names.n; p++)
		free(names.p[p]);
	v_free(&names);
	v_free(&paths);
	return rc;
}

const hibr_bi ls_bi[] = {
	{ "ls", m_ls, "list files in this process; names also land in $RET" },
	HIBR_BI_END
};

HIBR_MODULE("ls", HIBR_VER, "in-process ls with columns, -l, sorting and colour",
	   ls_bi, 0, 0);
