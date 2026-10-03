#define _GNU_SOURCE

#include "dv.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef DV_NSEC
#ifdef __APPLE__
#define DV_NSEC(st) ((st).st_mtimespec.tv_nsec)
#else
#define DV_NSEC(st) ((st).st_mtim.tv_nsec)
#endif
#endif

vec dv_srvs;
str dv_cpath;
time_t dv_cmt;
long dv_cmtn;
off_t dv_csz;
int dv_cread;

/* Where the server list is kept: HIBR_DAV_CONF, else the config folder. */
void dv_confpath(sh *s, str *o)
{
	const char *p = hibr_get(s, "HIBR_DAV_CONF");

	o->n = 0;
	if (p && *p) {
		s_cat(o, p);
		return;
	}
	p = hibr_get(s, "XDG_CONFIG_HOME");
	if (p && *p) {
		s_cat(o, p);
	} else {
		p = hibr_get(s, "HOME");
		s_cat(o, p ? p : "");
		s_cat(o, "/.config");
	}
	s_cat(o, "/hibr/dav");
}

/* Forget one server. */
void dv_srvfree(dv_srv *v)
{
	free(v->name);
	free(v->url);
	free(v->user);
	free(v->pass);
	free(v);
}

/* Forget every server read. */
void dv_conffree(void)
{
	size_t i;

	for (i = 0; i < dv_srvs.n; i++)
		dv_srvfree(dv_srvs.p[i]);
	v_free(&dv_srvs);
	s_free(&dv_cpath);
	dv_cread = 0;
}

/* The next tab-separated field of a line, or the rest of it. */
char *dv_field(char **p)
{
	char *f = *p, *t;

	if (!f)
		return xs("");
	t = strchr(f, '\t');
	if (t) {
		*t = 0;
		*p = t + 1;
	} else {
		*p = 0;
	}
	return xs(f);
}

/* Read the server list again if the file has changed since it was read.
   A file anyone else can read is refused: it holds passwords. */
int dv_conf(sh *s)
{
	struct stat st;
	FILE *f;
	char *line = 0, *p, *fl;
	size_t cap = 0;
	ssize_t n;
	dv_srv *v;
	str path;

	s_init(&path);
	dv_confpath(s, &path);
	if (stat(path.p, &st) < 0) {
		if (dv_cread && dv_cpath.p && !strcmp(dv_cpath.p, path.p) &&
		    !dv_srvs.n) {
			s_free(&path);
			return HIBR_OK;
		}
		dv_conffree();
		s_cat(&dv_cpath, path.p);
		dv_cread = 1;
		s_free(&path);
		return HIBR_OK;
	}
	if (dv_cread && dv_cpath.p && !strcmp(dv_cpath.p, path.p) &&
	    st.st_mtime == dv_cmt && DV_NSEC(st) == dv_cmtn &&
	    st.st_size == dv_csz) {
		s_free(&path);
		return HIBR_OK;
	}
	if (st.st_mode & 077) {
		lg(HIBR_LERR, "dav: %s can be read by others; chmod 600 it", path.p);
		s_free(&path);
		return HIBR_FAIL;
	}
	f = fopen(path.p, "r");
	if (!f) {
		lg(HIBR_LERR, "dav: %s: %s", path.p, strerror(errno));
		s_free(&path);
		return HIBR_FAIL;
	}
	dv_conffree();
	s_cat(&dv_cpath, path.p);
	while ((n = getline(&line, &cap, f)) >= 0) {
		while (n && (line[n - 1] == '\n' || line[n - 1] == '\r'))
			line[--n] = 0;
		if (!n || line[0] == '#')
			continue;
		v = xm(sizeof *v);
		memset(v, 0, sizeof *v);
		p = line;
		v->name = dv_field(&p);
		v->url = dv_field(&p);
		v->user = dv_field(&p);
		v->pass = dv_field(&p);
		fl = dv_field(&p);
		v->noverify = strchr(fl, 'k') != 0;
		free(fl);
		if (!*v->name || !*v->url) {
			dv_srvfree(v);
			continue;
		}
		v_add(&dv_srvs, v);
	}
	free(line);
	fclose(f);
	dv_cmt = st.st_mtime;
	dv_cmtn = DV_NSEC(st);
	dv_csz = st.st_size;
	dv_cread = 1;
	lg(HIBR_LDBG, "dav: %zu servers from %s", dv_srvs.n, path.p);
	s_free(&path);
	return HIBR_OK;
}

/* How many servers there are. */
size_t dv_srvn(void)
{
	return dv_srvs.n;
}

/* The i-th server. */
dv_srv *dv_srvat(size_t i)
{
	return i < dv_srvs.n ? dv_srvs.p[i] : 0;
}

/* A server by its name. */
dv_srv *dv_srvfind(const char *name)
{
	size_t i;
	dv_srv *v;

	for (i = 0; i < dv_srvs.n; i++) {
		v = dv_srvs.p[i];
		if (!strcmp(v->name, name))
			return v;
	}
	return 0;
}

/* The server whose address is the longest prefix of a URL, if any. */
dv_srv *dv_srvurl(const char *url)
{
	size_t i, n, best = 0;
	dv_srv *v, *b = 0;

	for (i = 0; i < dv_srvs.n; i++) {
		v = dv_srvs.p[i];
		n = strlen(v->url);
		while (n && v->url[n - 1] == '/')
			n--;
		if (n <= best || strncmp(url, v->url, n))
			continue;
		if (url[n] && url[n] != '/' && url[n] != '?')
			continue;
		best = n;
		b = v;
	}
	return b;
}

/* Make a folder and the folders above it, private. */
int dv_mkdirs(char *p)
{
	char *q;

	for (q = p + 1; *q; q++) {
		if (*q != '/')
			continue;
		*q = 0;
		if (mkdir(p, 0700) < 0 && errno != EEXIST) {
			*q = '/';
			return HIBR_FAIL;
		}
		*q = '/';
	}
	return HIBR_OK;
}

/* Write the server list back, readable only by its owner, replacing the
   file in one step so a crash never leaves half of it. */
int dv_confwrite(sh *s)
{
	str path, tmp, o;
	size_t i;
	dv_srv *v;
	int fd, ok;
	struct stat st;

	s_init(&path);
	s_init(&tmp);
	s_init(&o);
	dv_confpath(s, &path);
	dv_mkdirs(path.p);
	s_cat(&tmp, path.p);
	s_cat(&tmp, ".new");
	s_cat(&o, "# hibr WebDAV servers: name, address, user, password, flags "
		  "(k: certificate not checked).\n# Kept private: mode 600. "
		  "Written by dav server; edit with care.\n");
	for (i = 0; i < dv_srvs.n; i++) {
		v = dv_srvs.p[i];
		s_cat(&o, v->name);
		s_ch(&o, '\t');
		s_cat(&o, v->url);
		s_ch(&o, '\t');
		s_cat(&o, v->user);
		s_ch(&o, '\t');
		s_cat(&o, v->pass);
		s_ch(&o, '\t');
		s_cat(&o, v->noverify ? "k" : "");
		s_ch(&o, '\n');
	}
	unlink(tmp.p);
	fd = open(tmp.p, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
	ok = fd >= 0 && write(fd, o.p, o.n) == (ssize_t)o.n;
	if (fd >= 0 && close(fd) < 0)
		ok = 0;
	if (ok && rename(tmp.p, path.p) < 0)
		ok = 0;
	if (!ok) {
		lg(HIBR_LERR, "dav: cannot write %s: %s", path.p, strerror(errno));
		unlink(tmp.p);
	} else if (stat(path.p, &st) == 0) {
		dv_cmt = st.st_mtime;
		dv_cmtn = DV_NSEC(st);
		dv_csz = st.st_size;
	}
	s_free(&path);
	s_free(&tmp);
	s_free(&o);
	return ok ? HIBR_OK : HIBR_FAIL;
}

/* Whether a field can be written to the list: no tab, no line break. */
int dv_okfield(const char *what, const char *v)
{
	if (strpbrk(v, "\t\r\n")) {
		lg(HIBR_LERR, "dav: a %s cannot hold a tab or a line break", what);
		return 0;
	}
	return 1;
}

/* Add a server or change one; keeppass leaves its password as it was. */
int dv_srvset(sh *s, const char *name, const char *url, const char *user,
	      const char *pass, int noverify, int keeppass)
{
	dv_srv *v;
	dv_url u;
	char *nurl, *nuser, *npass;

	if (dv_conf(s) != HIBR_OK)
		return HIBR_FAIL;
	if (!*name || strchr(name, '/') || !dv_okfield("name", name) ||
	    !dv_okfield("address", url) || !dv_okfield("user", user) ||
	    !dv_okfield("password", pass)) {
		if (!*name || strchr(name, '/'))
			lg(HIBR_LERR, "dav: a server's name cannot be empty or "
				      "hold a /");
		return HIBR_FAIL;
	}
	memset(&u, 0, sizeof u);
	if (dv_urlsplit(url, &u) != HIBR_OK)
		return HIBR_FAIL;
	dv_urlfree(&u);
	v = dv_srvfind(name);
	if (!v) {
		v = xm(sizeof *v);
		memset(v, 0, sizeof *v);
		v->name = xs(name);
		v->pass = xs("");
		v_add(&dv_srvs, v);
	}
	nurl = xs(url);
	nuser = xs(user);
	npass = keeppass ? 0 : xs(pass);
	free(v->url);
	free(v->user);
	v->url = nurl;
	v->user = nuser;
	if (npass) {
		free(v->pass);
		v->pass = npass;
	}
	v->noverify = noverify;
	return dv_confwrite(s);
}

/* Remove a server. */
int dv_srvdel(sh *s, const char *name)
{
	size_t i;

	if (dv_conf(s) != HIBR_OK)
		return HIBR_FAIL;
	for (i = 0; i < dv_srvs.n; i++) {
		if (strcmp(((dv_srv *)dv_srvs.p[i])->name, name))
			continue;
		dv_srvfree(dv_srvs.p[i]);
		memmove(dv_srvs.p + i, dv_srvs.p + i + 1,
			(dv_srvs.n - i - 1) * sizeof *dv_srvs.p);
		dv_srvs.n--;
		return dv_confwrite(s);
	}
	lg(HIBR_LERR, "dav: no server %s", name);
	return HIBR_FAIL;
}

/* Rename a server, keeping everything else about it. */
int dv_srvren(sh *s, const char *from, const char *to)
{
	dv_srv *v;

	if (dv_conf(s) != HIBR_OK)
		return HIBR_FAIL;
	v = dv_srvfind(from);
	if (!v) {
		lg(HIBR_LERR, "dav: no server %s", from);
		return HIBR_FAIL;
	}
	if (!*to || strchr(to, '/') || !dv_okfield("name", to)) {
		lg(HIBR_LERR, "dav: a server's name cannot be empty or hold a /");
		return HIBR_FAIL;
	}
	if (strcmp(from, to) && dv_srvfind(to)) {
		lg(HIBR_LERR, "dav: there is already a server %s", to);
		return HIBR_FAIL;
	}
	free(v->name);
	v->name = xs(to);
	return dv_confwrite(s);
}
