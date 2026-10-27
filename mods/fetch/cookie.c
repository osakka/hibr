#define _GNU_SOURCE

#include "ft.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#ifdef __APPLE__
#define st_mtim st_mtimespec
#endif

/* One cookie, as RFC 6265 keeps it. expires 0 is a session cookie, which
   is kept on disk too, so that two runs of `fetch` share a login. */
typedef struct ft_ck ft_ck;
struct ft_ck {
	char *name, *value, *domain, *path, *samesite;
	long expires, created;
	int hostonly, secure, httponly;
	int gone;	/* a change that removes the cookie */
};

/* A jar: its cookies as last read and changed, and the changes made since
   it was last written, which a save lays over what the file holds then --
   another process may have written it in between. */
typedef struct ft_jar ft_jar;
struct ft_jar {
	char *name;
	vec cks, pend;
	struct timespec mt;
	off_t sz;
	int loaded;
};

vec ft_jars;

/* Whether a jar's name is one that can be a file's: letters, digits, dots,
   dashes and underscores, and not a dot alone or two. */
int ft_jarok(const char *jar)
{
	const char *p;

	if (!jar || !*jar || strlen(jar) > 64 || !strcmp(jar, ".") ||
	    !strcmp(jar, ".."))
		return 0;
	for (p = jar; *p; p++)
		if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
		      (*p >= '0' && *p <= '9') || *p == '.' || *p == '-' ||
		      *p == '_'))
			return 0;
	return 1;
}

/* The folder the jars live in, or the file of one jar when jar is given. */
void ft_jarpath(const char *jar, str *o)
{
	const char *d = getenv("XDG_DATA_HOME"), *h = getenv("HOME");

	if (d && *d)
		s_cat(o, d);
	else {
		s_cat(o, h && *h ? h : "/tmp");
		s_cat(o, "/.local/share");
	}
	s_cat(o, "/hibr/cookies");
	if (jar) {
		s_ch(o, '/');
		s_cat(o, jar);
		s_cat(o, ".json");
	}
}

/* Free one cookie. */
void ft_ckfree(ft_ck *c)
{
	free(c->name);
	free(c->value);
	free(c->domain);
	free(c->path);
	free(c->samesite);
	free(c);
}

/* Free every cookie of a list. */
void ft_cklist(vec *v)
{
	size_t i;

	for (i = 0; i < v->n; i++)
		ft_ckfree(v->p[i]);
	v->n = 0;
}

/* A copy of a cookie. */
ft_ck *ft_ckdup(const ft_ck *c)
{
	ft_ck *d = xm(sizeof *d);

	memset(d, 0, sizeof *d);
	d->name = xs(c->name);
	d->value = xs(c->value);
	d->domain = xs(c->domain);
	d->path = xs(c->path);
	d->samesite = c->samesite ? xs(c->samesite) : 0;
	d->expires = c->expires;
	d->created = c->created;
	d->hostonly = c->hostonly;
	d->secure = c->secure;
	d->httponly = c->httponly;
	d->gone = c->gone;
	return d;
}

/* Put a cookie in a list, replacing the one with its name, domain and
   path, whose creation time it keeps; a removal only takes that one out. */
void ft_ckput(vec *v, const ft_ck *c)
{
	size_t i;
	ft_ck *o;

	for (i = 0; i < v->n; i++) {
		o = v->p[i];
		if (strcmp(o->name, c->name) || strcmp(o->domain, c->domain) ||
		    strcmp(o->path, c->path))
			continue;
		if (c->gone) {
			ft_ckfree(o);
			v->p[i] = v->p[--v->n];
			return;
		}
		o->created = o->created ? o->created : c->created;
		free(o->value);
		o->value = xs(c->value);
		free(o->samesite);
		o->samesite = c->samesite ? xs(c->samesite) : 0;
		o->expires = c->expires;
		o->hostonly = c->hostonly;
		o->secure = c->secure;
		o->httponly = c->httponly;
		return;
	}
	if (!c->gone)
		v_add(v, ft_ckdup(c));
}

/* Note a change for the next save: a cookie set, or one taken away, which
   must be kept as a removal -- the save lays the changes over the file as
   it is then, and a removal that was dropped here would leave the file's
   copy in place. */
void ft_ckpend(vec *v, const ft_ck *c)
{
	size_t i;
	ft_ck *o;

	for (i = 0; i < v->n; i++) {
		o = v->p[i];
		if (!strcmp(o->name, c->name) && !strcmp(o->domain, c->domain) &&
		    !strcmp(o->path, c->path)) {
			ft_ckfree(o);
			v->p[i] = ft_ckdup(c);
			return;
		}
	}
	v_add(v, ft_ckdup(c));
}

/* The jar of that name, made empty if this process has not seen it. */
ft_jar *ft_jarget(const char *jar)
{
	size_t i;
	ft_jar *j;

	for (i = 0; i < ft_jars.n; i++) {
		j = ft_jars.p[i];
		if (!strcmp(j->name, jar))
			return j;
	}
	j = xm(sizeof *j);
	memset(j, 0, sizeof *j);
	j->name = xs(jar);
	v_add(&ft_jars, j);
	return j;
}

/* The small reading of JSON a jar needs: an object holding "cookies", an
   array of flat objects of strings, numbers and booleans. Anything else in
   the file is passed over; a file that is not JSON at all reads as empty. */
typedef struct ft_jp ft_jp;
struct ft_jp {
	const char *p, *e;
};

void ft_jws(ft_jp *q)
{
	while (q->p < q->e && (*q->p == ' ' || *q->p == '\t' || *q->p == '\n' ||
			       *q->p == '\r'))
		q->p++;
}

/* A JSON string into o; 0 when there is none. */
int ft_jstr(ft_jp *q, str *o)
{
	unsigned c, d;
	int i;

	o->n = 0;
	ft_jws(q);
	if (q->p >= q->e || *q->p != '"')
		return 0;
	q->p++;
	while (q->p < q->e && *q->p != '"') {
		if (*q->p != '\\') {
			s_ch(o, *q->p++);
			continue;
		}
		if (++q->p >= q->e)
			return 0;
		switch (*q->p++) {
		case 'n': s_ch(o, '\n'); break;
		case 't': s_ch(o, '\t'); break;
		case 'r': s_ch(o, '\r'); break;
		case 'b': s_ch(o, '\b'); break;
		case 'f': s_ch(o, '\f'); break;
		case 'u':
			c = 0;
			for (i = 0; i < 4 && q->p < q->e; i++, q->p++) {
				d = (unsigned)*q->p;
				c = c * 16 + (d >= 'a' ? d - 'a' + 10 :
					      d >= 'A' ? d - 'A' + 10 : d - '0');
			}
			if (c < 0x80) {
				s_ch(o, (int)c);
			} else if (c < 0x800) {
				s_ch(o, (int)(0xc0 | c >> 6));
				s_ch(o, (int)(0x80 | (c & 63)));
			} else {
				s_ch(o, (int)(0xe0 | c >> 12));
				s_ch(o, (int)(0x80 | ((c >> 6) & 63)));
				s_ch(o, (int)(0x80 | (c & 63)));
			}
			break;
		default: s_ch(o, q->p[-1]); break;
		}
	}
	if (q->p >= q->e)
		return 0;
	q->p++;
	if (!o->p)
		s_cat(o, "");
	return 1;
}

/* Pass over one value of any kind. */
void ft_jskip(ft_jp *q)
{
	int depth = 0;
	str t;

	ft_jws(q);
	if (q->p >= q->e)
		return;
	if (*q->p == '"') {
		s_init(&t);
		ft_jstr(q, &t);
		s_free(&t);
		return;
	}
	if (*q->p != '{' && *q->p != '[') {
		while (q->p < q->e && !strchr(",}] \t\r\n", *q->p))
			q->p++;
		return;
	}
	while (q->p < q->e) {
		if (*q->p == '"') {
			s_init(&t);
			ft_jstr(q, &t);
			s_free(&t);
			continue;
		}
		if (*q->p == '{' || *q->p == '[')
			depth++;
		else if (*q->p == '}' || *q->p == ']')
			depth--;
		q->p++;
		if (!depth)
			return;
	}
}

/* One cookie object into a new cookie; 0 when it is not one. */
ft_ck *ft_jck(ft_jp *q)
{
	ft_ck *c;
	str k, v;

	ft_jws(q);
	if (q->p >= q->e || *q->p != '{')
		return 0;
	q->p++;
	c = xm(sizeof *c);
	memset(c, 0, sizeof *c);
	s_init(&k);
	s_init(&v);
	for (;;) {
		ft_jws(q);
		if (q->p < q->e && *q->p == '}') {
			q->p++;
			break;
		}
		if (!ft_jstr(q, &k))
			break;
		ft_jws(q);
		if (q->p >= q->e || *q->p != ':')
			break;
		q->p++;
		ft_jws(q);
		if (q->p < q->e && *q->p == '"') {
			ft_jstr(q, &v);
			if (!strcmp(k.p, "name"))
				c->name = xs(v.p);
			else if (!strcmp(k.p, "value"))
				c->value = xs(v.p);
			else if (!strcmp(k.p, "domain"))
				c->domain = xs(v.p);
			else if (!strcmp(k.p, "path"))
				c->path = xs(v.p);
			else if (!strcmp(k.p, "samesite"))
				c->samesite = xs(v.p);
		} else if (q->p < q->e && (*q->p == 't' || *q->p == 'f')) {
			int on = *q->p == 't';

			ft_jskip(q);
			if (!strcmp(k.p, "hostonly"))
				c->hostonly = on;
			else if (!strcmp(k.p, "secure"))
				c->secure = on;
			else if (!strcmp(k.p, "httponly"))
				c->httponly = on;
		} else if (q->p < q->e && (*q->p == '-' ||
					   (*q->p >= '0' && *q->p <= '9'))) {
			long n = strtol(q->p, 0, 10);

			ft_jskip(q);
			if (!strcmp(k.p, "expires"))
				c->expires = n;
			else if (!strcmp(k.p, "created"))
				c->created = n;
		} else {
			ft_jskip(q);
		}
		ft_jws(q);
		if (q->p < q->e && *q->p == ',')
			q->p++;
	}
	s_free(&k);
	s_free(&v);
	if (!c->name || !c->domain) {
		ft_ckfree(c);
		return 0;
	}
	if (!c->value)
		c->value = xs("");
	if (!c->path)
		c->path = xs("/");
	return c;
}

/* Read a jar's file into a list; an absent or broken file is an empty
   jar. The file's time and size are noted so that it is read again only
   when it changes. */
void ft_jarread(const char *jar, vec *into, struct timespec *mt, off_t *sz)
{
	str path, k;
	FILE *f;
	struct stat st;
	char *buf;
	long n;
	ft_jp q;
	ft_ck *c;

	s_init(&path);
	ft_jarpath(jar, &path);
	f = fopen(path.p, "r");
	s_free(&path);
	if (mt)
		memset(mt, 0, sizeof *mt);
	if (sz)
		*sz = 0;
	if (!f)
		return;
	if (fstat(fileno(f), &st) == 0) {
		if (mt)
			*mt = st.st_mtim;
		if (sz)
			*sz = st.st_size;
	}
	if (fseek(f, 0, SEEK_END) || (n = ftell(f)) < 0 || n > 64L * 1024 * 1024 ||
	    fseek(f, 0, SEEK_SET)) {
		fclose(f);
		return;
	}
	buf = xm((size_t)n + 1);
	n = (long)fread(buf, 1, (size_t)n, f);
	fclose(f);
	buf[n] = 0;
	q.p = buf;
	q.e = buf + n;
	s_init(&k);
	ft_jws(&q);
	if (q.p < q.e && *q.p == '{') {
		q.p++;
		for (;;) {
			if (!ft_jstr(&q, &k))
				break;
			ft_jws(&q);
			if (q.p >= q.e || *q.p != ':')
				break;
			q.p++;
			ft_jws(&q);
			if (strcmp(k.p, "cookies") || q.p >= q.e || *q.p != '[') {
				ft_jskip(&q);
			} else {
				q.p++;
				for (;;) {
					ft_jws(&q);
					if (q.p >= q.e || *q.p == ']') {
						q.p++;
						break;
					}
					c = ft_jck(&q);
					if (c) {
						ft_ckput(into, c);
						ft_ckfree(c);
					} else {
						ft_jskip(&q);
					}
					ft_jws(&q);
					if (q.p < q.e && *q.p == ',')
						q.p++;
				}
			}
			ft_jws(&q);
			if (q.p < q.e && *q.p == ',')
				q.p++;
			else
				break;
		}
	}
	s_free(&k);
	free(buf);
}

/* Make sure this process has the jar as the file holds it now. */
void ft_jarload(ft_jar *j)
{
	str path;
	struct stat st;
	size_t i;

	s_init(&path);
	ft_jarpath(j->name, &path);
	if (stat(path.p, &st) != 0) {
		memset(&st, 0, sizeof st);
		st.st_size = -1;
	}
	s_free(&path);
	if (j->loaded && st.st_mtim.tv_sec == j->mt.tv_sec &&
	    st.st_mtim.tv_nsec == j->mt.tv_nsec &&
	    (st.st_size < 0 ? 0 : st.st_size) == j->sz)
		return;
	ft_cklist(&j->cks);
	ft_jarread(j->name, &j->cks, &j->mt, &j->sz);
	for (i = 0; i < j->pend.n; i++)
		ft_ckput(&j->cks, j->pend.p[i]);
	j->loaded = 1;
}

/* JSON text of a string. */
void ft_jout(str *o, const char *p)
{
	s_ch(o, '"');
	for (; *p; p++) {
		unsigned char c = (unsigned char)*p;

		if (c == '"' || c == '\\') {
			s_ch(o, '\\');
			s_ch(o, c);
		} else if (c < 0x20) {
			s_fmt(o, "\\u%04x", c);
		} else {
			s_ch(o, c);
		}
	}
	s_ch(o, '"');
}

/* Write a jar: the file as it is now with this process's changes laid
   over it, through a file of its own renamed into place, readable by its
   owner alone. Expired cookies are left out. */
void ft_jarsave(const char *jar)
{
	ft_jar *j = ft_jarget(jar);
	vec cur = { 0, 0, 0 };
	str path, tmp, dir, o;
	size_t i;
	ft_ck *c;
	long now = (long)time(0);
	int fd;
	char *sl;

	ft_jarread(jar, &cur, 0, 0);
	for (i = 0; i < j->pend.n; i++)
		ft_ckput(&cur, j->pend.p[i]);
	s_init(&o);
	s_cat(&o, "{\n \"version\": 1,\n \"cookies\": [");
	for (i = 0; i < cur.n; i++) {
		c = cur.p[i];
		if (c->expires && c->expires <= now)
			continue;
		s_cat(&o, o.p[o.n - 1] == '[' ? "\n  {" : ",\n  {");
		s_cat(&o, "\"name\": ");
		ft_jout(&o, c->name);
		s_cat(&o, ", \"value\": ");
		ft_jout(&o, c->value);
		s_cat(&o, ", \"domain\": ");
		ft_jout(&o, c->domain);
		s_cat(&o, ", \"path\": ");
		ft_jout(&o, c->path);
		s_fmt(&o, ", \"expires\": %ld, \"created\": %ld", c->expires,
		      c->created);
		s_fmt(&o, ", \"hostonly\": %s, \"secure\": %s, \"httponly\": %s",
		      c->hostonly ? "true" : "false", c->secure ? "true" : "false",
		      c->httponly ? "true" : "false");
		if (c->samesite) {
			s_cat(&o, ", \"samesite\": ");
			ft_jout(&o, c->samesite);
		}
		s_ch(&o, '}');
	}
	s_cat(&o, "\n ]\n}\n");
	s_init(&path);
	s_init(&tmp);
	s_init(&dir);
	ft_jarpath(jar, &path);
	s_cat(&dir, path.p);
	for (i = 1; i < dir.n; i++)
		if (dir.p[i] == '/') {
			dir.p[i] = 0;
			mkdir(dir.p, 0700);
			dir.p[i] = '/';
		}
	sl = strrchr(dir.p, '/');
	if (sl) {
		*sl = 0;
		mkdir(dir.p, 0700);
		chmod(dir.p, 0700);
	}
	s_fmt(&tmp, "%s.%ld.tmp", path.p, (long)getpid());
	fd = open(tmp.p, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
	if (fd >= 0) {
		if (hibr_wrall(fd, o.p, o.n) == (ssize_t)o.n && fsync(fd) == 0 &&
		    close(fd) == 0 && rename(tmp.p, path.p) == 0) {
			ft_cklist(&j->pend);
			ft_cklist(&j->cks);
			for (i = 0; i < cur.n; i++)
				v_add(&j->cks, ft_ckdup(cur.p[i]));
			{
				struct stat st;

				if (stat(path.p, &st) == 0) {
					j->mt = st.st_mtim;
					j->sz = st.st_size;
				}
			}
			j->loaded = 1;
		} else {
			unlink(tmp.p);
			lg(HIBR_LDBG, "fetch: cannot write the jar %s: %s", jar,
			   strerror(errno));
		}
	}
	ft_cklist(&cur);
	v_free(&cur);
	s_free(&o);
	s_free(&path);
	s_free(&tmp);
	s_free(&dir);
}

/* Whether a host falls within a cookie's domain, as RFC 6265 section
   5.1.3 says: the same, or ending in a dot and the domain, and not an
   address. */
int ft_dommatch(const char *host, const char *dom)
{
	size_t h = strlen(host), d = strlen(dom);

	if (!strcasecmp(host, dom))
		return 1;
	if (d >= h || strcasecmp(host + h - d, dom) || host[h - d - 1] != '.')
		return 0;
	return strspn(host, "0123456789.") != h && !strchr(host, ':');
}

/* Whether a request's path falls within a cookie's, as section 5.1.4
   says. */
int ft_pathmatch(const char *rpath, const char *cpath)
{
	size_t c = strlen(cpath);

	if (!strncmp(rpath, cpath, c) &&
	    (rpath[c] == 0 || rpath[c] == '?' || cpath[c - 1] == '/' ||
	     rpath[c] == '/'))
		return 1;
	return 0;
}

/* The request's path without its query, for matching. */
void ft_rpath(const ft_url *u, str *o)
{
	s_add(o, u->path.p, strcspn(u->path.p, "?"));
	if (!o->n)
		s_ch(o, '/');
}

/* The Cookie header's value for a request: every cookie of the jar that
   belongs to it, the longest paths first and then the oldest. */
void ft_cookiehdr(const char *jar, const ft_url *u, str *o)
{
	ft_jar *j = ft_jarget(jar);
	vec m = { 0, 0, 0 };
	size_t i, k;
	ft_ck *c, *t;
	long now = (long)time(0);
	str rp;

	ft_jarload(j);
	s_init(&rp);
	ft_rpath(u, &rp);
	for (i = 0; i < j->cks.n; i++) {
		c = j->cks.p[i];
		if (c->expires && c->expires <= now)
			continue;
		if (c->hostonly ? strcasecmp(u->host.p, c->domain) :
				  !ft_dommatch(u->host.p, c->domain))
			continue;
		if (!ft_pathmatch(rp.p, c->path))
			continue;
		if (c->secure && !u->tls)
			continue;
		v_add(&m, c);
	}
	for (i = 1; i < m.n; i++)
		for (k = i; k > 0; k--) {
			c = m.p[k - 1];
			t = m.p[k];
			if (strlen(c->path) > strlen(t->path) ||
			    (strlen(c->path) == strlen(t->path) &&
			     c->created <= t->created))
				break;
			m.p[k - 1] = t;
			m.p[k] = c;
		}
	for (i = 0; i < m.n; i++) {
		c = m.p[i];
		if (o->n)
			s_cat(o, "; ");
		s_cat(o, c->name);
		s_ch(o, '=');
		s_cat(o, c->value);
	}
	v_free(&m);
	s_free(&rp);
}

/* Trim blanks off both ends of p[0..n), as a new string. */
char *ft_trim(const char *p, size_t n)
{
	str t;
	char *r;

	while (n && (*p == ' ' || *p == '\t')) {
		p++;
		n--;
	}
	while (n && (p[n - 1] == ' ' || p[n - 1] == '\t'))
		n--;
	s_init(&t);
	s_add(&t, p, n);
	r = xs(t.p ? t.p : "");
	s_free(&t);
	return r;
}

/* Keep what one Set-Cookie header says, as RFC 6265 section 5.2 reads it:
   a name and value, then attributes. A cookie for a domain the request's
   host is not within is refused, as is one for a bare top-level name --
   there is no public suffix list here, so a Domain with no dot inside it
   is the line drawn. */
void ft_cookieset(const char *jar, const ft_url *u, const char *line)
{
	ft_jar *j = ft_jarget(jar);
	ft_ck c;
	const char *p = line, *semi, *eq;
	char *k, *v;
	long now = (long)time(0), maxage = 0;
	int hasmax = 0, bad = 0;
	str rp;

	memset(&c, 0, sizeof c);
	semi = p + strcspn(p, ";");
	eq = memchr(p, '=', (size_t)(semi - p));
	if (!eq)
		return;
	c.name = ft_trim(p, (size_t)(eq - p));
	c.value = ft_trim(eq + 1, (size_t)(semi - eq - 1));
	if (!*c.name) {
		free(c.name);
		free(c.value);
		return;
	}
	c.created = now;
	p = *semi ? semi + 1 : semi;
	while (*p) {
		semi = p + strcspn(p, ";");
		eq = memchr(p, '=', (size_t)(semi - p));
		k = ft_trim(p, (size_t)((eq ? eq : semi) - p));
		v = eq ? ft_trim(eq + 1, (size_t)(semi - eq - 1)) : xs("");
		if (!strcasecmp(k, "expires")) {
			long t = ft_httpdate(v);

			/* A date at or before the epoch is a cookie taken
			   away, and 1 is the earliest "already gone" there
			   is, 0 meaning a session cookie. */
			if (t >= 0)
				c.expires = t > 0 ? t : 1;
		} else if (!strcasecmp(k, "max-age")) {
			char *end;

			maxage = strtol(v, &end, 10);
			hasmax = end != v;
		} else if (!strcasecmp(k, "domain")) {
			char *d = v;

			while (*d == '.')
				d++;
			free(c.domain);
			c.domain = xs(d);
			for (d = c.domain; *d; d++)
				if (*d >= 'A' && *d <= 'Z')
					*d = (char)(*d + 32);
		} else if (!strcasecmp(k, "path")) {
			free(c.path);
			c.path = *v == '/' ? xs(v) : 0;
		} else if (!strcasecmp(k, "secure")) {
			c.secure = 1;
		} else if (!strcasecmp(k, "httponly")) {
			c.httponly = 1;
		} else if (!strcasecmp(k, "samesite")) {
			free(c.samesite);
			c.samesite = xs(v);
		}
		free(k);
		free(v);
		p = *semi ? semi + 1 : semi;
	}
	if (hasmax)
		c.expires = maxage <= 0 ? 1 : now + maxage;
	if (c.domain && *c.domain) {
		if (!strchr(c.domain, '.') || !ft_dommatch(u->host.p, c.domain))
			bad = 1;
	} else {
		free(c.domain);
		c.domain = xs(u->host.p);
		c.hostonly = 1;
	}
	if (c.secure && !u->tls)
		bad = 1;
	if (!c.path) {
		s_init(&rp);
		ft_rpath(u, &rp);
		{
			char *sl = strrchr(rp.p, '/');

			if (!sl || sl == rp.p)
				c.path = xs("/");
			else {
				*sl = 0;
				c.path = xs(rp.p);
			}
		}
		s_free(&rp);
	}
	if (!bad) {
		c.gone = c.expires && c.expires <= now;
		ft_jarload(j);
		ft_ckput(&j->cks, &c);
		ft_ckpend(&j->pend, &c);
	} else {
		lg(HIBR_LDBG, "fetch: %s refused the cookie %s for %s", u->host.p,
		   c.name, c.domain ? c.domain : "");
	}
	free(c.name);
	free(c.value);
	free(c.domain);
	free(c.path);
	free(c.samesite);
}

/* Forget a jar: in this process and its file. */
int ft_jarclear(sh *s, const char *jar)
{
	ft_jar *j;
	str path;
	int rc = HIBR_OK;

	(void)s;
	if (!ft_jarok(jar))
		return HIBR_FAIL;
	j = ft_jarget(jar);
	ft_cklist(&j->cks);
	ft_cklist(&j->pend);
	j->loaded = 0;
	s_init(&path);
	ft_jarpath(jar, &path);
	if (unlink(path.p) != 0 && errno != ENOENT)
		rc = HIBR_FAIL;
	s_free(&path);
	return rc;
}

/* Free every jar this process holds. */
void ft_jarfree(void)
{
	size_t i;
	ft_jar *j;

	for (i = 0; i < ft_jars.n; i++) {
		j = ft_jars.p[i];
		ft_cklist(&j->cks);
		ft_cklist(&j->pend);
		v_free(&j->cks);
		v_free(&j->pend);
		free(j->name);
		free(j);
	}
	v_free(&ft_jars);
}
