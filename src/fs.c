#define _GNU_SOURCE

#include "hibr.h"
#include "pri.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef FS_F
#define FS_F 1
#endif
#ifndef FS_R
#define FS_R 2
#endif
#ifndef FS_D
#define FS_D 4
#endif
#ifndef FS_V
#define FS_V 8
#endif
#ifndef FS_P
#define FS_P 16
#endif
#ifndef FS_M
#define FS_M 32
#endif
#ifndef FS_N
#define FS_N 64
#endif

/* Report a failure in GNU's words, after anything already printed. */
void fs_err(const char *who, const char *what, const char *path, int e)
{
	fflush(stdout);
	fprintf(stderr, "%s: %s '%s': %s\n", who, what, path, strerror(e));
}

/* Read an octal mode, or fail for anything else (a symbolic mode). */
int fs_mode(const char *t, mode_t *m)
{
	char *e;
	long v;

	if (!*t || strspn(t, "01234567") != strlen(t))
		return 0;
	v = strtol(t, &e, 8);
	if (v > 07777)
		return 0;
	*m = (mode_t)v;
	return 1;
}

/* Make one directory; with -p an existing one is no failure. */
int fs_mk1(const char *p, mode_t m, int fl, int last)
{
	struct stat st;

	if (mkdir(p, m) == 0) {
		if (last && (fl & FS_M) && chmod(p, m) < 0) {
			fs_err("mkdir", "cannot set permissions of", p, errno);
			return 1;
		}
		if (fl & FS_V)
			printf("mkdir: created directory '%s'\n", p);
		return 0;
	}
	if ((fl & FS_P) && errno == EEXIST && stat(p, &st) == 0 &&
	    S_ISDIR(st.st_mode))
		return 0;
	fs_err("mkdir", "cannot create directory", p, errno);
	return 1;
}

/* Make a directory and, with -p, every one missing above it. */
int fs_mkdir(const char *p, mode_t m, int fl)
{
	str t;
	size_t i, n = strlen(p);
	mode_t um, up;
	int r = 0;

	if (!(fl & FS_P))
		return fs_mk1(p, m, fl, 1);
	um = umask(0);
	umask(um);
	up = (0777 & ~um) | 0300;
	s_init(&t);
	s_add(&t, p, n);
	for (i = 1; i < n && !r; i++) {
		if (t.p[i] != '/' || t.p[i - 1] == '/')
			continue;
		t.p[i] = 0;
		r = fs_mk1(t.p, up, fl & ~FS_M, 0);
		t.p[i] = '/';
	}
	if (!r)
		r = fs_mk1(p, m, fl, 1);
	s_free(&t);
	return r;
}

/* Hand av to the program for an option not done here; 1 in *none if there is none. */
int fs_ext(sh *s, char **av, int *none)
{
	char *p = findx(s, av[0]);

	if (!p) {
		if (none) {
			*none = 1;
			return HIBR_OK;
		}
		lg(HIBR_LERR, "%s: %s needs the %s program, and there is none on PATH",
		   av[0], av[1], av[0]);
		return HIBR_FAIL;
	}
	free(p);
	return cmd_ext(s, av, av[0]);
}

/* mkdir [-p] [-m octal] [-v] [--] dir...; any other option goes to the program. */
int b_mkdir(sh *s, int ac, char **av)
{
	int i, fl = 0, r = 0;
	mode_t m = 0777;
	const char *a, *mt = 0;

	for (i = 1; i < ac && av[i][0] == '-' && av[i][1]; i++) {
		a = av[i];
		if (!strcmp(a, "--")) {
			i++;
			break;
		}
		if (!strcmp(a, "--parents"))
			fl |= FS_P;
		else if (!strcmp(a, "--verbose"))
			fl |= FS_V;
		else if (!strncmp(a, "--mode=", 7))
			mt = a + 7;
		else if (a[1] == '-')
			return fs_ext(s, av, 0);
		else {
			for (a++; *a; a++) {
				if (*a == 'p')
					fl |= FS_P;
				else if (*a == 'v')
					fl |= FS_V;
				else if (*a == 'm') {
					mt = a[1] ? a + 1 : (i + 1 < ac ? av[++i] : 0);
					if (!mt)
						return fs_ext(s, av, 0);
					break;
				} else
					return fs_ext(s, av, 0);
			}
		}
	}
	if (mt) {
		if (!fs_mode(mt, &m))
			return fs_ext(s, av, 0);
		fl |= FS_M;
	}
	if (i >= ac) {
		fflush(stdout);
		fprintf(stderr, "mkdir: missing operand\n"
			"Try 'mkdir --help' for more information.\n");
		return HIBR_FAIL;
	}
	if ((s->sopt & O_PLAN) && !pl_prog(s, av))
		return HIBR_FAIL;
	for (; i < ac; i++)
		r |= fs_mkdir(av[i], m, fl);
	return r ? HIBR_FAIL : HIBR_OK;
}

/* Whether a name's last component is . or .., which rm refuses. */
int fs_dots(const char *p)
{
	size_t n = strlen(p);
	const char *b;

	while (n > 1 && p[n - 1] == '/')
		n--;
	b = p + n;
	while (b > p && b[-1] != '/')
		b--;
	n -= (size_t)(b - p);
	return (n == 1 && b[0] == '.') || (n == 2 && b[0] == '.' && b[1] == '.');
}

int fs_rmin(int fd, str *path, int fl);

/* Remove name, relative to the directory at dfd, shown as path. */
int fs_rm1(int dfd, const char *name, str *path, int fl, int top)
{
	struct stat st;
	int fd, r;

	if (fstatat(dfd, name, &st, AT_SYMLINK_NOFOLLOW) < 0) {
		if ((fl & FS_F) && (errno == ENOENT || errno == ENOTDIR) && top)
			return 0;
		fs_err("rm", "cannot remove", path->p, errno);
		return 1;
	}
	if (!S_ISDIR(st.st_mode)) {
		if (unlinkat(dfd, name, 0) < 0) {
			if ((fl & FS_F) && (errno == ENOENT || errno == ENOTDIR))
				return 0;
			fs_err("rm", "cannot remove", path->p, errno);
			return 1;
		}
		if (fl & FS_V)
			printf("removed '%s'\n", path->p);
		return 0;
	}
	if (!(fl & (FS_R | FS_D))) {
		fs_err("rm", "cannot remove", path->p, EISDIR);
		return 1;
	}
	r = 0;
	if (fl & FS_R) {
		fd = openat(dfd, name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
		if (fd < 0) {
			fs_err("rm", "cannot remove", path->p, errno);
			return 1;
		}
		r = fs_rmin(fd, path, fl);
		close(fd);
	}
	if (unlinkat(dfd, name, AT_REMOVEDIR) < 0) {
		if ((fl & FS_F) && errno == ENOTDIR && top)
			return r;
		if (!r)
			fs_err("rm", "cannot remove", path->p, errno);
		return 1;
	}
	if (fl & FS_V)
		printf("removed directory '%s'\n", path->p);
	return r;
}

/* Remove what is inside the directory at fd, its names read and closed first. */
int fs_rmin(int fd, str *path, int fl)
{
	DIR *d;
	struct dirent *e;
	vec nm = { 0, 0, 0 };
	size_t k, keep = path->n;
	int dd, r = 0;

	dd = dup(fd);
	if (dd < 0 || !(d = fdopendir(dd))) {
		if (dd >= 0)
			close(dd);
		fs_err("rm", "cannot remove", path->p, errno);
		return 1;
	}
	while ((e = readdir(d))) {
		if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))
			continue;
		v_add(&nm, xs(e->d_name));
	}
	closedir(d);
	for (k = 0; k < nm.n; k++) {
		if (keep && path->p[keep - 1] != '/')
			s_ch(path, '/');
		s_cat(path, nm.p[k]);
		r |= fs_rm1(fd, nm.p[k], path, fl, 0);
		path->n = keep;
		path->p[keep] = 0;
		free(nm.p[k]);
	}
	v_free(&nm);
	return r;
}

/* Whether GNU rm would stop to ask before removing p, so the program must. */
int fs_asks(const char *p, int fl)
{
	struct stat st;

	if ((fl & FS_F) || !isatty(0))
		return 0;
	if (lstat(p, &st) < 0 || S_ISLNK(st.st_mode))
		return 0;
	if (S_ISDIR(st.st_mode) && (fl & FS_R))
		return 1;
	return access(p, W_OK) < 0;
}

/* rm [-f] [-r|-R] [-d] [-v] [--] file...; prompts and other options go to the program. */
int b_rm(sh *s, int ac, char **av)
{
	int i, j, fl = 0, r = 0, none;
	const char *a;
	str path;

	for (i = 1; i < ac && av[i][0] == '-' && av[i][1]; i++) {
		a = av[i];
		if (!strcmp(a, "--")) {
			i++;
			break;
		}
		if (!strcmp(a, "--force"))
			fl |= FS_F;
		else if (!strcmp(a, "--recursive"))
			fl |= FS_R;
		else if (!strcmp(a, "--dir"))
			fl |= FS_D;
		else if (!strcmp(a, "--verbose"))
			fl |= FS_V;
		else if (a[1] == '-')
			return fs_ext(s, av, 0);
		else {
			for (a++; *a; a++) {
				if (*a == 'f')
					fl |= FS_F;
				else if (*a == 'r' || *a == 'R')
					fl |= FS_R;
				else if (*a == 'd')
					fl |= FS_D;
				else if (*a == 'v')
					fl |= FS_V;
				else
					return fs_ext(s, av, 0);
			}
		}
	}
	if (i >= ac) {
		if (fl & FS_F)
			return HIBR_OK;
		fflush(stdout);
		fprintf(stderr, "rm: missing operand\n"
			"Try 'rm --help' for more information.\n");
		return HIBR_FAIL;
	}
	for (j = i; j < ac; j++)
		if (fs_asks(av[j], fl)) {
			none = 0;
			r = fs_ext(s, av, &none);
			if (!none)
				return r;
			break;
		}
	if ((s->sopt & O_PLAN) && !pl_prog(s, av))
		return HIBR_FAIL;
	s_init(&path);
	for (; i < ac; i++) {
		if (fs_dots(av[i])) {
			fflush(stdout);
			fprintf(stderr, "rm: refusing to remove '.' or '..' "
				"directory: skipping '%s'\n", av[i]);
			r = 1;
			continue;
		}
		if ((fl & FS_R) && av[i][0] == '/' &&
		    strspn(av[i], "/") == strlen(av[i])) {
			fflush(stdout);
			fprintf(stderr, "rm: it is dangerous to operate "
				"recursively on '/'\nrm: use --no-preserve-root "
				"to override this failsafe\n");
			r = 1;
			continue;
		}
		path.n = 0;
		s_cat(&path, av[i]);
		r |= fs_rm1(AT_FDCWD, av[i], &path, fl, 1);
	}
	s_free(&path);
	return r ? HIBR_FAIL : HIBR_OK;
}

/* A move across filesystems, or one GNU would ask about: one source handed to the program. */
int fs_mvext(sh *s, const char *src, const char *dst, int fl, int *none)
{
	char *av[8];
	int n = 0;

	av[n++] = "mv";
	if (fl & FS_F)
		av[n++] = "-f";
	if (fl & FS_N)
		av[n++] = "-n";
	if (fl & FS_V)
		av[n++] = "-v";
	av[n++] = "-T";
	av[n++] = "--";
	av[n++] = (char *)src;
	av[n++] = (char *)dst;
	av[n] = 0;
	return fs_ext(s, av, none);
}

/* Move one source to exactly dst, in GNU's words when it cannot. */
int fs_mv1(sh *s, const char *src, const char *dst, int fl)
{
	struct stat a, b;
	int hb, none = 0, r;

	if (lstat(src, &a) < 0) {
		fs_err("mv", "cannot stat", src, errno);
		return 1;
	}
	hb = lstat(dst, &b) == 0;
	if (hb && a.st_dev == b.st_dev && a.st_ino == b.st_ino) {
		fflush(stdout);
		fprintf(stderr, "mv: '%s' and '%s' are the same file\n", src, dst);
		return 1;
	}
	if (hb && (fl & FS_N))
		return 0;
	if (hb && S_ISDIR(a.st_mode) && !S_ISDIR(b.st_mode)) {
		fflush(stdout);
		fprintf(stderr, "mv: cannot overwrite non-directory '%s' with directory '%s'\n",
			dst, src);
		return 1;
	}
	if (hb && !S_ISDIR(a.st_mode) && S_ISDIR(b.st_mode)) {
		fflush(stdout);
		fprintf(stderr, "mv: cannot overwrite directory '%s' with non-directory\n", dst);
		return 1;
	}
	if (hb && !(fl & FS_F) && isatty(0) && !S_ISLNK(b.st_mode) &&
	    access(dst, W_OK) < 0) {
		r = fs_mvext(s, src, dst, fl, &none);
		if (!none)
			return r != 0;
	}
	if (rename(src, dst) == 0) {
		if (fl & FS_V)
			printf("renamed '%s' -> '%s'\n", src, dst);
		return 0;
	}
	if (errno == EXDEV) {
		r = fs_mvext(s, src, dst, fl, &none);
		if (!none)
			return r != 0;
		errno = EXDEV;
	}
	fflush(stdout);
	if (errno == EINVAL)
		fprintf(stderr, "mv: cannot move '%s' to a subdirectory of itself, '%s'\n",
			src, dst);
	else
		fprintf(stderr, "mv: cannot move '%s' to '%s': %s\n", src, dst,
			strerror(errno));
	return 1;
}

/* Where src lands in the directory dir: dir/basename, as GNU spells it. */
void fs_into(str *o, const char *dir, const char *src)
{
	size_t n = strlen(src);
	const char *b;

	while (n > 1 && src[n - 1] == '/')
		n--;
	b = src + n;
	while (b > src && b[-1] != '/')
		b--;
	o->n = 0;
	s_cat(o, dir);
	if (!o->n || o->p[o->n - 1] != '/')
		s_ch(o, '/');
	s_add(o, b, (size_t)(src + n - b));
}

/* mv [-f] [-n] [-v] [-t dir] [-T] [--] src... dst; other options and prompts go to the program. */
int b_mv(sh *s, int ac, char **av)
{
	int i, fl = 0, r = 0, noT = 0;
	const char *a, *tdir = 0, *dst;
	struct stat st;
	str o;

	for (i = 1; i < ac && av[i][0] == '-' && av[i][1]; i++) {
		a = av[i];
		if (!strcmp(a, "--")) {
			i++;
			break;
		}
		if (!strcmp(a, "--force"))
			fl = (fl | FS_F) & ~FS_N;
		else if (!strcmp(a, "--no-clobber"))
			fl = (fl | FS_N) & ~FS_F;
		else if (!strcmp(a, "--verbose"))
			fl |= FS_V;
		else if (!strcmp(a, "--no-target-directory"))
			noT = 1;
		else if (!strncmp(a, "--target-directory=", 19))
			tdir = a + 19;
		else if (a[1] == '-')
			return fs_ext(s, av, 0);
		else {
			for (a++; *a; a++) {
				if (*a == 'f')
					fl = (fl | FS_F) & ~FS_N;
				else if (*a == 'n')
					fl = (fl | FS_N) & ~FS_F;
				else if (*a == 'v')
					fl |= FS_V;
				else if (*a == 'T')
					noT = 1;
				else if (*a == 't') {
					tdir = a[1] ? a + 1 : (i + 1 < ac ? av[++i] : 0);
					if (!tdir)
						return fs_ext(s, av, 0);
					break;
				} else
					return fs_ext(s, av, 0);
			}
		}
	}
	if (tdir && noT)
		return fs_ext(s, av, 0);
	if (i >= ac || (!tdir && i + 1 >= ac)) {
		fflush(stdout);
		if (i >= ac)
			fprintf(stderr, "mv: missing file operand\n");
		else
			fprintf(stderr, "mv: missing destination file operand after '%s'\n", av[i]);
		fprintf(stderr, "Try 'mv --help' for more information.\n");
		return HIBR_FAIL;
	}
	if ((s->sopt & O_PLAN) && !pl_prog(s, av))
		return HIBR_FAIL;
	if (!tdir) {
		dst = av[--ac];
		if (noT || (ac - i == 1 && (stat(dst, &st) < 0 || !S_ISDIR(st.st_mode)))) {
			if (ac - i > 1) {
				fflush(stdout);
				fprintf(stderr, "mv: extra operand '%s'\n"
					"Try 'mv --help' for more information.\n", av[i + 1]);
				return HIBR_FAIL;
			}
			return fs_mv1(s, av[i], dst, fl) ? HIBR_FAIL : HIBR_OK;
		}
		tdir = dst;
	}
	if (stat(tdir, &st) < 0 || !S_ISDIR(st.st_mode)) {
		fs_err("mv", "target", tdir, errno ? errno : ENOTDIR);
		return HIBR_FAIL;
	}
	s_init(&o);
	for (; i < ac; i++) {
		fs_into(&o, tdir, av[i]);
		r |= fs_mv1(s, av[i], o.p, fl);
	}
	s_free(&o);
	return r ? HIBR_FAIL : HIBR_OK;
}
