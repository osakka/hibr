#define _GNU_SOURCE

#include "dv.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

void v_del(sh *s, const char *k);
void dv_authfree(void);

static const char dv_propfind[] =
	"<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
	"<d:propfind xmlns:d=\"DAV:\"><d:prop><d:resourcetype/>"
	"<d:getcontentlength/><d:getlastmodified/><d:getetag/>"
	"<d:getcontenttype/></d:prop></d:propfind>\n";

/* What a status means, said plainly. */
const char *dv_why(int code)
{
	switch (code) {
	case 0: return "no answer";
	case 400: return "the server did not understand the request";
	case 401: return "the server refused the login";
	case 403: return "not allowed";
	case 404: return "not there";
	case 405: return "not allowed here";
	case 409: return "the folder it goes in is not there";
	case 412: return "changed on the server, or already there";
	case 413: return "too large for the server";
	case 415: return "the server does not take that";
	case 423: return "locked";
	case 502: return "the server's gateway failed";
	case 503: return "the server is not available";
	case 507: return "the server is out of space";
	}
	return code >= 500 ? "the server failed" : "the server said no";
}

/* Remember the last status for the script, as DAV_CODE. */
void dv_code(sh *s, int code)
{
	str b;

	s_init(&b);
	s_num(&b, code);
	hibr_set(s, "DAV_CODE", b.p, 0);
	s_free(&b);
}

/* Say why something failed: the location, and what the server meant. */
int dv_err(sh *s, const char *what, const char *loc, dv_res *rs)
{
	dv_code(s, rs->code);
	if (rs->code)
		lg(HIBR_LERR, "dav: %s %s: %s (%d)", what, loc, dv_why(rs->code),
		   rs->code);
	return HIBR_FAIL;
}

/* Free a listing. */
void dv_entfree(vec *v)
{
	size_t i;
	dv_ent *e;

	for (i = 0; i < v->n; i++) {
		e = v->p[i];
		free(e->name);
		free(e->etag);
		free(e->type);
		free(e);
	}
	v_free(v);
}

/* The path part of an href, decoded, with no trailing slash. */
void dv_hrefpath(const char *h, str *o)
{
	const char *p = h, *q;

	o->n = 0;
	while (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t')
		p++;
	if (!strncasecmp(p, "http://", 7) || !strncasecmp(p, "https://", 8)) {
		p = strstr(p, "://") + 3;
		p += strcspn(p, "/");
	}
	q = p + strcspn(p, "?# \r\n\t");
	dv_dec(o, p, (size_t)(q - p));
	while (o->n > 1 && o->p[o->n - 1] == '/')
		o->p[--o->n] = 0;
	if (!o->p)
		s_cat(o, "");
}

/* The last segment of an href, taken before decoding, so an encoded
   slash stays inside the name -- where the listing then refuses it. */
char *dv_hrefname(const char *h)
{
	str raw, o;
	const char *p = h, *q, *b;
	char *r;

	s_init(&raw);
	s_init(&o);
	while (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t')
		p++;
	q = p + strcspn(p, "?# \r\n\t");
	s_add(&raw, p, (size_t)(q - p));
	while (raw.n && raw.p[raw.n - 1] == '/')
		raw.p[--raw.n] = 0;
	b = raw.p ? strrchr(raw.p, '/') : 0;
	b = b ? b + 1 : raw.p ? raw.p : "";
	dv_dec(&o, b, strlen(b));
	r = xs(o.p ? o.p : "");
	s_free(&raw);
	s_free(&o);
	return r;
}

/* Read one response of a multistatus into an entry; 0 when it carries no
   successful properties. */
dv_ent *dv_entof(dv_x *r, str *path)
{
	dv_x *h = dv_xkid(r, "DAV:", "href"), *ps, *pr, *st, *v;
	dv_ent *e;
	size_t i;
	int ok = 0;

	if (!h)
		return 0;
	dv_hrefpath(h->text.p ? h->text.p : "", path);
	e = xm(sizeof *e);
	memset(e, 0, sizeof *e);
	e->mtime = 0;
	e->size = -1;
	for (i = 0; i < r->kids.n; i++) {
		ps = r->kids.p[i];
		if (strcmp(ps->ns, "DAV:") || strcmp(ps->name, "propstat"))
			continue;
		st = dv_xkid(ps, "DAV:", "status");
		if (st && st->text.p && !strstr(st->text.p, " 200"))
			continue;
		pr = dv_xkid(ps, "DAV:", "prop");
		if (!pr)
			continue;
		ok = 1;
		if ((v = dv_xkid(pr, "DAV:", "resourcetype")) &&
		    dv_xkid(v, "DAV:", "collection"))
			e->dir = 1;
		if ((v = dv_xkid(pr, "DAV:", "getcontentlength")) && v->text.p)
			e->size = strtoll(v->text.p, 0, 10);
		if ((v = dv_xkid(pr, "DAV:", "getlastmodified")) && v->text.p)
			e->mtime = dv_httpdate(v->text.p);
		if ((v = dv_xkid(pr, "DAV:", "getetag")) && v->text.p) {
			free(e->etag);
			e->etag = xs(v->text.p);
		}
		if ((v = dv_xkid(pr, "DAV:", "getcontenttype")) && v->text.p) {
			free(e->type);
			e->type = xs(v->text.p);
		}
	}
	if (!ok && !dv_xkid(r, "DAV:", "propstat")) {
		v = dv_xkid(r, "DAV:", "status");
		ok = !v || !v->text.p || strstr(v->text.p, " 200") != 0;
	}
	if (!ok) {
		free(e->etag);
		free(e->type);
		free(e);
		return 0;
	}
	e->name = dv_hrefname(h->text.p ? h->text.p : "");
	if (!e->etag)
		e->etag = xs("");
	if (!e->type)
		e->type = xs("");
	if (e->dir)
		e->size = 0;
	if (e->size < 0)
		e->size = 0;
	return e;
}

/* Order entries by name. */
int dv_entcmp(const void *a, const void *b)
{
	return strcmp((*(dv_ent *const *)a)->name, (*(dv_ent *const *)b)->name);
}

/* When no entry's path is the folder's own, the folder is the one whose
   path is the shortest and begins every other; none when nothing is. */
size_t dv_selfguess(vec *paths)
{
	size_t i, j, best = (size_t)-1, n;
	const char *b;

	for (i = 0; i < paths->n; i++)
		if (best == (size_t)-1 ||
		    strlen(paths->p[i]) < strlen(paths->p[best]))
			best = i;
	if (best == (size_t)-1)
		return best;
	b = paths->p[best];
	n = strlen(b);
	for (j = 0; j < paths->n; j++)
		if (j != best && (strncmp(paths->p[j], b, n) ||
				  (n > 1 && ((char *)paths->p[j])[n] != '/')))
			return (size_t)-1;
	return best;
}

/* List an address with PROPFIND: depth 1 gives what a folder holds, depth
   0 the thing itself. A name that could step outside a folder -- empty,
   a dot, two dots, or holding a slash once decoded -- is never listed. */
int dv_listurl(sh *s, const char *url, const dv_srv *sv, int depth, vec *out,
	       dv_res *rs)
{
	dv_req rq;
	dv_x *doc, *ms, *r;
	str self, path, u;
	vec all = { 0, 0, 0 }, paths = { 0, 0, 0 };
	dv_ent *e;
	size_t i, si = (size_t)-1;
	int rc;
	dv_url su;

	s_init(&u);
	s_cat(&u, url);
	if (depth && (!u.n || u.p[u.n - 1] != '/'))
		s_ch(&u, '/');
	dv_reqinit(&rq);
	rq.method = "PROPFIND";
	rq.loc = u.p;
	rq.sv = sv;
	rq.follow = 1;
	rq.body = dv_propfind;
	rq.blen = sizeof dv_propfind - 1;
	dv_hdr(&rq, "Depth", depth ? "1" : "0");
	dv_hdr(&rq, "Content-Type", "application/xml; charset=utf-8");
	rc = dv_do(s, &rq, rs);
	dv_reqfree(&rq);
	if (rc != HIBR_OK || rs->code != 207) {
		if (rc == HIBR_OK)
			rs->code = rs->code == 200 ? 501 : rs->code;
		s_free(&u);
		return HIBR_FAIL;
	}
	doc = dv_xparse(rs->body.p ? rs->body.p : "", rs->body.n);
	ms = dv_xkid(doc, "DAV:", "multistatus");
	if (!ms) {
		lg(HIBR_LERR, "dav: %s did not answer in WebDAV's own form", url);
		dv_xfree(doc);
		s_free(&u);
		rs->code = 502;
		return HIBR_FAIL;
	}
	s_init(&self);
	s_init(&path);
	memset(&su, 0, sizeof su);
	if (dv_urlsplit(u.p, &su) == HIBR_OK)
		dv_hrefpath(su.path.p, &self);
	dv_urlfree(&su);
	for (i = 0; i < ms->kids.n; i++) {
		r = ms->kids.p[i];
		if (strcmp(r->ns, "DAV:") || strcmp(r->name, "response"))
			continue;
		e = dv_entof(r, &path);
		if (!e)
			continue;
		if (si == (size_t)-1 && self.p && !strcmp(path.p, self.p))
			si = all.n;
		v_add(&all, e);
		v_add(&paths, xs(path.p));
	}
	if (si == (size_t)-1 && depth && all.n)
		si = dv_selfguess(&paths);
	for (i = 0; i < paths.n; i++)
		free(paths.p[i]);
	v_free(&paths);
	if (depth == 0) {
		if (all.n) {
			e = all.p[si == (size_t)-1 ? 0 : si];
			v_add(out, e);
			for (i = 0; i < all.n; i++)
				if (all.p[i] != e) {
					vec one = { 0, 0, 0 };

					v_add(&one, all.p[i]);
					dv_entfree(&one);
				}
		}
	} else {
		for (i = 0; i < all.n; i++) {
			e = all.p[i];
			if (i == si || !*e->name || !strcmp(e->name, ".") ||
			    !strcmp(e->name, "..") || strchr(e->name, '/')) {
				vec one = { 0, 0, 0 };

				v_add(&one, e);
				dv_entfree(&one);
				continue;
			}
			v_add(out, e);
		}
		if (out->n > 1)
			qsort(out->p, out->n, sizeof *out->p, dv_entcmp);
	}
	v_free(&all);
	s_free(&self);
	s_free(&path);
	dv_xfree(doc);
	s_free(&u);
	return HIBR_OK;
}

/* List a location. */
int dv_list(sh *s, const char *loc, int depth, vec *out)
{
	str url;
	const dv_srv *sv;
	dv_res rs;
	int rc;

	s_init(&url);
	if (dv_resolve(s, loc, &url, &sv) != HIBR_OK) {
		s_free(&url);
		return HIBR_FAIL;
	}
	rc = dv_listurl(s, url.p, sv, depth, out, &rs);
	if (rc != HIBR_OK)
		dv_err(s, "cannot list", loc, &rs);
	else
		dv_code(s, rs.code);
	dv_resfree(&rs);
	s_free(&url);
	return rc;
}

/* Set one field of an entry in the result map. */
void dv_entset(sh *s, const char *key, const char *f, const char *v)
{
	char *ks[2];
	int n = 0;

	if (key)
		ks[n++] = (char *)key;
	ks[n++] = (char *)f;
	hibr_setp(s, "RET", ks, n, v);
}

/* Put one entry in the result: printed, or as r[key][field] with :=, or
   r[field] when there is no key. */
void dv_entout(sh *s, dv_ent *e, const char *key)
{
	str b;

	if (!s->bind) {
		printf("%c\t%lld\t%ld\t%s\n", e->dir ? 'd' : 'f', e->size,
		       e->mtime, e->name);
		return;
	}
	s_init(&b);
	dv_entset(s, key, "name", e->name);
	dv_entset(s, key, "dir", e->dir ? "1" : "0");
	s_num(&b, (long)e->size);
	dv_entset(s, key, "size", b.p);
	b.n = 0;
	s_num(&b, e->mtime);
	dv_entset(s, key, "mtime", b.p);
	dv_entset(s, key, "etag", e->etag);
	dv_entset(s, key, "type", e->type);
	s_free(&b);
}

/* Empty the result slot, ready to hold a map. */
void dv_retmap(sh *s)
{
	if (!s->bind)
		return;
	v_del(s, "RET");
	hibr_set(s, "RET", "", 0);
}

/* dav ls LOC: what a folder holds. */
int dv_ls(sh *s, const char *loc)
{
	vec out = { 0, 0, 0 };
	size_t i;
	str k;

	if (dv_list(s, loc, 1, &out) != HIBR_OK)
		return HIBR_FAIL;
	dv_retmap(s);
	s_init(&k);
	for (i = 0; i < out.n; i++) {
		k.n = 0;
		s_num(&k, (long)i);
		dv_entout(s, out.p[i], k.p);
	}
	s_free(&k);
	dv_entfree(&out);
	return HIBR_OK;
}

/* dav stat LOC: one thing, folder or file. */
int dv_stat(sh *s, const char *loc)
{
	vec out = { 0, 0, 0 };

	if (dv_list(s, loc, 0, &out) != HIBR_OK)
		return HIBR_FAIL;
	if (!out.n) {
		dv_code(s, 404);
		lg(HIBR_LERR, "dav: %s: not there", loc);
		return HIBR_FAIL;
	}
	dv_retmap(s);
	dv_entout(s, out.p[0], 0);
	dv_entfree(&out);
	return HIBR_OK;
}

/* The last segment of a path or address, decoded, with no slash. */
void dv_base(const char *p, str *o)
{
	size_t n = strlen(p);
	const char *b;

	o->n = 0;
	while (n && p[n - 1] == '/')
		n--;
	b = p + n;
	while (b > p && b[-1] != '/')
		b--;
	if (!strncmp(p, "dav://", 6) || strstr(p, "://"))
		dv_dec(o, b, (size_t)(p + n - b));
	else
		s_add(o, b, (size_t)(p + n - b));
	if (!o->p)
		s_cat(o, "");
}

/* Download one address to a file, through a .part beside it so a failure
   never leaves half a file where the whole one should be. */
int dv_get1(sh *s, const char *url, const dv_srv *sv, const char *dest,
	    dv_res *rs)
{
	dv_req rq;
	str part;
	int fd, rc, tostd = !strcmp(dest, "-");

	s_init(&part);
	if (tostd) {
		fflush(stdout);
		fd = 1;
	} else {
		s_cat(&part, dest);
		s_cat(&part, ".part");
		fd = open(part.p, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0666);
		if (fd < 0) {
			lg(HIBR_LERR, "dav: cannot write %s: %s", dest,
			   strerror(errno));
			s_free(&part);
			dv_resinit(rs);
			return HIBR_FAIL;
		}
	}
	dv_reqinit(&rq);
	rq.method = "GET";
	rq.loc = url;
	rq.sv = sv;
	rq.follow = 1;
	rq.out = fd;
	rc = dv_do(s, &rq, rs);
	dv_reqfree(&rq);
	if (!tostd) {
		if (close(fd) < 0)
			rc = HIBR_FAIL;
		if (rc == HIBR_OK && rename(part.p, dest) < 0) {
			lg(HIBR_LERR, "dav: cannot put %s in place: %s", dest,
			   strerror(errno));
			rc = HIBR_FAIL;
		}
		if (rc != HIBR_OK)
			unlink(part.p);
	}
	s_free(&part);
	return rc;
}

/* Download a folder and everything in it into a local folder. */
int dv_getr(sh *s, const char *url, const dv_srv *sv, const char *dest,
	    int depth, const char *loc)
{
	vec out = { 0, 0, 0 };
	dv_res rs;
	size_t i;
	dv_ent *e;
	str cu, cd;
	int rc = HIBR_OK;

	if (depth > DV_RDEPTH) {
		lg(HIBR_LERR, "dav: %s goes deeper than %d folders", loc, DV_RDEPTH);
		return HIBR_FAIL;
	}
	if (dv_listurl(s, url, sv, 1, &out, &rs) != HIBR_OK) {
		dv_err(s, "cannot list", loc, &rs);
		dv_resfree(&rs);
		return HIBR_FAIL;
	}
	dv_resfree(&rs);
	if (mkdir(dest, 0777) < 0 && errno != EEXIST) {
		lg(HIBR_LERR, "dav: cannot make %s: %s", dest, strerror(errno));
		dv_entfree(&out);
		return HIBR_FAIL;
	}
	s_init(&cu);
	s_init(&cd);
	for (i = 0; i < out.n && rc == HIBR_OK; i++) {
		e = out.p[i];
		cu.n = 0;
		s_cat(&cu, url);
		if (!cu.n || cu.p[cu.n - 1] != '/')
			s_ch(&cu, '/');
		dv_enc(&cu, e->name, 0);
		cd.n = 0;
		s_cat(&cd, dest);
		s_ch(&cd, '/');
		s_cat(&cd, e->name);
		if (e->dir) {
			s_ch(&cu, '/');
			rc = dv_getr(s, cu.p, sv, cd.p, depth + 1, e->name);
		} else {
			rc = dv_get1(s, cu.p, sv, cd.p, &rs);
			if (rc != HIBR_OK)
				dv_err(s, "cannot download", e->name, &rs);
			dv_resfree(&rs);
		}
	}
	s_free(&cu);
	s_free(&cd);
	dv_entfree(&out);
	return rc;
}

/* dav get [-r] LOC [DEST]: download, to a file named after it by default. */
int dv_get(sh *s, int ac, char **av)
{
	int i = 2, rec = 0, rc;
	const char *loc, *dest;
	str url, base;
	const dv_srv *sv;
	dv_res rs;

	if (i < ac && !strcmp(av[i], "-r")) {
		rec = 1;
		i++;
	}
	if (i >= ac) {
		lg(HIBR_LERR, "usage: dav get [-r] location [file|folder|-]");
		return 2;
	}
	loc = av[i++];
	s_init(&url);
	s_init(&base);
	if (dv_resolve(s, loc, &url, &sv) != HIBR_OK) {
		s_free(&url);
		return HIBR_FAIL;
	}
	dv_base(loc, &base);
	dest = i < ac ? av[i] : base.p;
	if (!*dest || !strcmp(dest, ".") || !strcmp(dest, "..")) {
		lg(HIBR_LERR, "dav: name a file to download %s to", loc);
		s_free(&url);
		s_free(&base);
		return HIBR_FAIL;
	}
	if (rec) {
		rc = dv_getr(s, url.p, sv, dest, 0, loc);
		if (rc == HIBR_OK)
			dv_code(s, 200);
	} else {
		rc = dv_get1(s, url.p, sv, dest, &rs);
		if (rc != HIBR_OK)
			dv_err(s, "cannot download", loc, &rs);
		else
			dv_code(s, rs.code);
		dv_resfree(&rs);
	}
	s_free(&url);
	s_free(&base);
	return rc;
}

/* Upload one file, with a condition on what is there: an ETag it must
   still have, or nothing there at all. The new ETag goes in etag. */
/* The type to upload a file as, from its name, when none is given: a
   calendar or a contact as what CalDAV and CardDAV expect. */
const char *dv_typeof(const char *src)
{
	size_t n = strlen(src);

	if (n > 4 && !strcasecmp(src + n - 4, ".ics"))
		return "text/calendar; charset=utf-8";
	if (n > 4 && !strcasecmp(src + n - 4, ".vcf"))
		return "text/vcard; charset=utf-8";
	return "application/octet-stream";
}

int dv_put1(sh *s, const char *src, const char *url, const dv_srv *sv,
	    const char *ifmatch, int ifnone, dv_res *rs, str *etag, const char *ctype)
{
	dv_req rq;
	struct stat st;
	int fd, rc;

	dv_resinit(rs);
	fd = open(src, O_RDONLY | O_CLOEXEC);
	if (fd < 0 || fstat(fd, &st) < 0 || !S_ISREG(st.st_mode)) {
		lg(HIBR_LERR, "dav: cannot read %s: %s", src,
		   fd < 0 ? strerror(errno) : "not a plain file");
		if (fd >= 0)
			close(fd);
		return HIBR_FAIL;
	}
	dv_reqinit(&rq);
	rq.method = "PUT";
	rq.loc = url;
	rq.sv = sv;
	rq.follow = 1;
	rq.bfd = fd;
	rq.bflen = st.st_size;
	dv_hdr(&rq, "Content-Type", ctype && *ctype ? ctype : dv_typeof(src));
	if (ifmatch && *ifmatch)
		dv_hdr(&rq, "If-Match", ifmatch);
	if (ifnone)
		dv_hdr(&rq, "If-None-Match", "*");
	if ((ifmatch && *ifmatch) || ifnone) {
		vec cur = { 0, 0, 0 };
		dv_res r0;
		int there, same;

		there = dv_listurl(s, url, sv, 0, &cur, &r0) == HIBR_OK && cur.n;
		same = there && ifmatch &&
		       !strcmp(((dv_ent *)cur.p[0])->etag, ifmatch);
		dv_resfree(&r0);
		dv_entfree(&cur);
		if ((ifnone && there) || (ifmatch && *ifmatch && !same)) {
			lg(HIBR_LDBG, "dav: %s fails its condition before upload",
			   url);
			dv_reqfree(&rq);
			close(fd);
			rs->code = 412;
			return HIBR_FAIL;
		}
	}
	if (sv && sv->user && *sv->user) {
		dv_res pre;
		dv_req op;

		dv_reqinit(&op);
		op.method = "OPTIONS";
		op.loc = url;
		op.sv = sv;
		dv_do(s, &op, &pre);
		dv_reqfree(&op);
		dv_resfree(&pre);
	}
	rc = dv_do(s, &rq, rs);
	dv_reqfree(&rq);
	close(fd);
	if (rc == HIBR_OK && etag) {
		etag->n = 0;
		if (rs->etag) {
			s_cat(etag, rs->etag);
		} else {
			vec out = { 0, 0, 0 };
			dv_res r2;

			if (dv_listurl(s, url, sv, 0, &out, &r2) == HIBR_OK &&
			    out.n)
				s_cat(etag, ((dv_ent *)out.p[0])->etag);
			dv_resfree(&r2);
			dv_entfree(&out);
		}
	}
	return rc;
}

/* Make a folder; one already there is fine when ok_there says so. */
int dv_mkcol(sh *s, const char *url, const dv_srv *sv, int ok_there,
	     dv_res *rs)
{
	dv_req rq;
	int rc;

	dv_reqinit(&rq);
	rq.method = "MKCOL";
	rq.loc = url;
	rq.sv = sv;
	rq.follow = 1;
	rc = dv_do(s, &rq, rs);
	dv_reqfree(&rq);
	if (rc != HIBR_OK && ok_there && rs->code == 405)
		rc = HIBR_OK;
	return rc;
}

/* Upload a local folder and everything in it. Symbolic links to folders
   are left behind, so a loop cannot make the upload endless. */
int dv_putr(sh *s, const char *src, const char *url, const dv_srv *sv,
	    int depth)
{
	DIR *d;
	struct dirent *de;
	struct stat st;
	str cs, cu;
	dv_res rs;
	vec names = { 0, 0, 0 };
	size_t i;
	int rc = HIBR_OK;

	if (depth > DV_RDEPTH) {
		lg(HIBR_LERR, "dav: %s goes deeper than %d folders", src, DV_RDEPTH);
		return HIBR_FAIL;
	}
	if (dv_mkcol(s, url, sv, 1, &rs) != HIBR_OK) {
		dv_err(s, "cannot make", src, &rs);
		dv_resfree(&rs);
		return HIBR_FAIL;
	}
	dv_resfree(&rs);
	d = opendir(src);
	if (!d) {
		lg(HIBR_LERR, "dav: cannot read %s: %s", src, strerror(errno));
		return HIBR_FAIL;
	}
	while ((de = readdir(d)))
		if (strcmp(de->d_name, ".") && strcmp(de->d_name, ".."))
			v_add(&names, xs(de->d_name));
	closedir(d);
	s_init(&cs);
	s_init(&cu);
	for (i = 0; i < names.n; i++) {
		if (rc != HIBR_OK) {
			free(names.p[i]);
			continue;
		}
		cs.n = 0;
		s_cat(&cs, src);
		s_ch(&cs, '/');
		s_cat(&cs, names.p[i]);
		cu.n = 0;
		s_cat(&cu, url);
		if (!cu.n || cu.p[cu.n - 1] != '/')
			s_ch(&cu, '/');
		dv_enc(&cu, names.p[i], 0);
		if (lstat(cs.p, &st) == 0 && S_ISDIR(st.st_mode)) {
			s_ch(&cu, '/');
			rc = dv_putr(s, cs.p, cu.p, sv, depth + 1);
		} else if (stat(cs.p, &st) == 0 && S_ISREG(st.st_mode)) {
			rc = dv_put1(s, cs.p, cu.p, sv, 0, 0, &rs, 0, 0);
			if (rc != HIBR_OK)
				dv_err(s, "cannot upload", cs.p, &rs);
			dv_resfree(&rs);
		} else {
			lg(HIBR_LDBG, "dav: %s left behind: not a file or folder",
			   cs.p);
		}
		free(names.p[i]);
	}
	v_free(&names);
	s_free(&cs);
	s_free(&cu);
	return rc;
}

/* dav put [-r] [-m etag] [-n] SRC LOC: upload; a location ending in /
   takes the file's own name. The new ETag is the result. */
int dv_put(sh *s, int ac, char **av)
{
	int i = 2, rec = 0, ifnone = 0, rc;
	const char *ifmatch = 0, *ctype = 0, *src, *loc;
	str url, base, etag;
	const dv_srv *sv;
	dv_res rs;

	while (i < ac && av[i][0] == '-' && av[i][1]) {
		if (!strcmp(av[i], "-r"))
			rec = 1;
		else if (!strcmp(av[i], "-n"))
			ifnone = 1;
		else if (!strcmp(av[i], "-m") && i + 1 < ac)
			ifmatch = av[++i];
		else if (!strcmp(av[i], "-t") && i + 1 < ac)
			ctype = av[++i];
		else
			break;
		i++;
	}
	if (i + 1 >= ac) {
		lg(HIBR_LERR, "usage: dav put [-r] [-m etag] [-n] [-t type] path location");
		return 2;
	}
	src = av[i];
	loc = av[i + 1];
	s_init(&url);
	s_init(&base);
	s_init(&etag);
	if (dv_resolve(s, loc, &url, &sv) != HIBR_OK) {
		s_free(&url);
		return HIBR_FAIL;
	}
	if (loc[strlen(loc) - 1] == '/' ||
	    (!strncmp(loc, "dav://", 6) && !strchr(loc + 6, '/'))) {
		dv_base(src, &base);
		if (!url.n || url.p[url.n - 1] != '/')
			s_ch(&url, '/');
		dv_enc(&url, base.p, 0);
	}
	if (rec) {
		if (url.n && url.p[url.n - 1] != '/')
			s_ch(&url, '/');
		rc = dv_putr(s, src, url.p, sv, 0);
		if (rc == HIBR_OK)
			dv_code(s, 201);
	} else {
		rc = dv_put1(s, src, url.p, sv, ifmatch, ifnone, &rs, &etag, ctype);
		if (rc != HIBR_OK) {
			if (rs.code == 412 && ifnone)
				lg(HIBR_LERR, "dav: %s is already there", loc);
			else if (rs.code == 412)
				lg(HIBR_LERR, "dav: %s changed on the server "
					      "since it was read", loc);
			if (rs.code == 412)
				dv_code(s, 412);
			else
				dv_err(s, "cannot upload to", loc, &rs);
		} else {
			dv_code(s, rs.code);
			hibr_ret(s, etag.p ? etag.p : "");
			if (!s->bind && etag.n)
				printf("%s\n", etag.p);
		}
		dv_resfree(&rs);
	}
	s_free(&url);
	s_free(&base);
	s_free(&etag);
	return rc;
}

/* dav mkdir LOC and dav rm LOC. */
int dv_simple(sh *s, const char *method, const char *loc, const char *ifmatch)
{
	str url;
	const dv_srv *sv;
	dv_req rq;
	dv_res rs;
	int rc;

	s_init(&url);
	if (dv_resolve(s, loc, &url, &sv) != HIBR_OK) {
		s_free(&url);
		return HIBR_FAIL;
	}
	if (!strcmp(method, "MKCOL")) {
		rc = dv_mkcol(s, url.p, sv, 0, &rs);
	} else {
		dv_reqinit(&rq);
		rq.method = method;
		rq.loc = url.p;
		rq.sv = sv;
		rq.follow = 1;
		if (ifmatch && *ifmatch)
			dv_hdr(&rq, "If-Match", ifmatch);
		rc = dv_do(s, &rq, &rs);
		dv_reqfree(&rq);
	}
	if (rc != HIBR_OK) {
		if (!strcmp(method, "MKCOL") && rs.code == 405) {
			dv_code(s, 405);
			lg(HIBR_LERR, "dav: %s is already there", loc);
		} else {
			dv_err(s, !strcmp(method, "MKCOL") ? "cannot make" :
							   "cannot remove",
			       loc, &rs);
		}
	} else {
		dv_code(s, rs.code);
	}
	dv_resfree(&rs);
	s_free(&url);
	return rc;
}

/* dav mv|cp [-f] SRC DST: on the server, within one server. Nothing is
   put over something already there unless -f says so. */
int dv_mvcp(sh *s, int ac, char **av, const char *method)
{
	int i = 2, force = 0, rc;
	str a, b;
	const dv_srv *sa, *sb;
	dv_url ua, ub;
	dv_req rq;
	dv_res rs;

	if (i < ac && !strcmp(av[i], "-f")) {
		force = 1;
		i++;
	}
	if (i + 1 >= ac) {
		lg(HIBR_LERR, "usage: dav %s [-f] location location", av[1]);
		return 2;
	}
	s_init(&a);
	s_init(&b);
	if (dv_resolve(s, av[i], &a, &sa) != HIBR_OK ||
	    dv_resolve(s, av[i + 1], &b, &sb) != HIBR_OK) {
		s_free(&a);
		s_free(&b);
		return HIBR_FAIL;
	}
	memset(&ua, 0, sizeof ua);
	memset(&ub, 0, sizeof ub);
	rc = dv_urlsplit(a.p, &ua) == HIBR_OK && dv_urlsplit(b.p, &ub) == HIBR_OK;
	if (rc && (ua.tls != ub.tls || strcasecmp(ua.host.p, ub.host.p) ||
		   strcmp(ua.port.p, ub.port.p))) {
		lg(HIBR_LERR, "dav: %s and %s are on different servers; "
			      "copy with get and put", av[i], av[i + 1]);
		rc = 0;
	}
	dv_urlfree(&ua);
	dv_urlfree(&ub);
	if (!rc) {
		s_free(&a);
		s_free(&b);
		return HIBR_FAIL;
	}
	dv_reqinit(&rq);
	rq.method = method;
	rq.loc = a.p;
	rq.sv = sa;
	rq.follow = 0;
	dv_hdr(&rq, "Destination", b.p);
	dv_hdr(&rq, "Overwrite", force ? "T" : "F");
	rc = dv_do(s, &rq, &rs);
	dv_reqfree(&rq);
	if (rc != HIBR_OK) {
		if (rs.code == 412) {
			dv_code(s, 412);
			lg(HIBR_LERR, "dav: %s is already there", av[i + 1]);
		} else {
			dv_err(s, !strcmp(method, "MOVE") ? "cannot move" :
							   "cannot copy",
			       av[i], &rs);
		}
	} else {
		dv_code(s, rs.code);
	}
	dv_resfree(&rs);
	s_free(&a);
	s_free(&b);
	return rc;
}

/* dav servers: every server set up, without passwords. */
int dv_servers(sh *s)
{
	size_t i;
	dv_srv *v;
	char *ks[2];

	if (dv_conf(s) != HIBR_OK)
		return HIBR_FAIL;
	dv_retmap(s);
	for (i = 0; i < dv_srvn(); i++) {
		v = dv_srvat(i);
		if (s->bind) {
			ks[0] = v->name;
			ks[1] = "url";
			hibr_setp(s, "RET", ks, 2, v->url);
			ks[1] = "user";
			hibr_setp(s, "RET", ks, 2, v->user);
			ks[1] = "verify";
			hibr_setp(s, "RET", ks, 2, v->noverify ? "0" : "1");
			ks[1] = "haspass";
			hibr_setp(s, "RET", ks, 2, *v->pass ? "1" : "0");
		} else {
			printf("%s\t%s\t%s\t%s\n", v->name, v->url, v->user,
			       v->noverify ? "unchecked" : "checked");
		}
	}
	return HIBR_OK;
}

/* dav server set|rm|rename ...: change the list of servers. */
int dv_server(sh *s, int ac, char **av)
{
	const char *sub = ac > 2 ? av[2] : "";
	const char *user = 0, *pass = 0;
	int i, noverify = 0, verify = 0;
	dv_srv *old;

	if (!strcmp(sub, "rm") && ac == 4)
		return dv_srvdel(s, av[3]);
	if (!strcmp(sub, "rename") && ac == 5)
		return dv_srvren(s, av[3], av[4]);
	if (!strcmp(sub, "set") && ac >= 5) {
		for (i = 5; i < ac; i++) {
			if (!strcmp(av[i], "-u") && i + 1 < ac)
				user = av[++i];
			else if (!strcmp(av[i], "-p") && i + 1 < ac)
				pass = av[++i];
			else if (!strcmp(av[i], "-k"))
				noverify = 1;
			else if (!strcmp(av[i], "-K"))
				verify = 1;
			else
				goto usage;
		}
		if (dv_conf(s) != HIBR_OK)
			return HIBR_FAIL;
		old = dv_srvfind(av[3]);
		if (!user)
			user = old ? old->user : "";
		if (!noverify && !verify && old)
			noverify = old->noverify;
		return dv_srvset(s, av[3], av[4], user, pass ? pass : "", noverify,
				 !pass && old);
	}
usage:
	lg(HIBR_LERR, "usage: dav server set name address [-u user] [-p password] "
		      "[-k|-K] | rm name | rename old new");
	return 2;
}

/* dav test NAME|LOC: can the server be reached and logged in to. */
int dv_test(sh *s, const char *what)
{
	str loc;
	vec out = { 0, 0, 0 };
	int rc;

	s_init(&loc);
	if (strncmp(what, "dav://", 6) && !strstr(what, "://"))
		s_cat(&loc, "dav://");
	s_cat(&loc, what);
	rc = dv_list(s, loc.p, 0, &out);
	dv_entfree(&out);
	if (rc == HIBR_OK) {
		hibr_ret(s, "ok");
		if (!s->bind)
			printf("ok\n");
	}
	s_free(&loc);
	return rc;
}

/* The usage line. */
void dv_usage(void)
{
	lg(HIBR_LERR, "usage: dav ls|stat|mkdir location | rm [-m etag] location | "
		      "get [-r] location [dest] | put [-r] [-m etag] [-n] [-t type] path location | "
		      "mv|cp [-f] location location | propfind [-d 0|1] location prop... | "
		      "report [-d depth] location -b body|-f file | sync location [-t token] [prop...] | "
		      "test server | servers | server set|rm|rename ... | close");
}

/* dav: a WebDAV client. */
int m_dav(sh *s, int ac, char **av)
{
	const char *sub = ac > 1 ? av[1] : "";

	if (!strcmp(sub, "servers"))
		return dv_servers(s);
	if (!strcmp(sub, "server"))
		return dv_server(s, ac, av);
	if (!strcmp(sub, "close")) {
		dv_closeall();
		dv_authfree();
		return HIBR_OK;
	}
	if (!strcmp(sub, "get"))
		return dv_get(s, ac, av);
	if (!strcmp(sub, "put"))
		return dv_put(s, ac, av);
	if (!strcmp(sub, "mv"))
		return dv_mvcp(s, ac, av, "MOVE");
	if (!strcmp(sub, "cp"))
		return dv_mvcp(s, ac, av, "COPY");
	if (!strcmp(sub, "propfind"))
		return dv_cpropfind(s, ac, av);
	if (!strcmp(sub, "report"))
		return dv_creport(s, ac, av);
	if (!strcmp(sub, "sync"))
		return dv_csync(s, ac, av);
	if (!strcmp(sub, "rm") && ac == 5 && !strcmp(av[2], "-m"))
		return dv_simple(s, "DELETE", av[4], av[3]);
	if (ac != 3) {
		dv_usage();
		return 2;
	}
	if (!strcmp(sub, "ls"))
		return dv_ls(s, av[2]);
	if (!strcmp(sub, "stat"))
		return dv_stat(s, av[2]);
	if (!strcmp(sub, "mkdir"))
		return dv_simple(s, "MKCOL", av[2], 0);
	if (!strcmp(sub, "rm"))
		return dv_simple(s, "DELETE", av[2], 0);
	if (!strcmp(sub, "test"))
		return dv_test(s, av[2]);
	dv_usage();
	return 2;
}

/* Nothing to set up until the first request. */
int dv_ini(sh *s)
{
	(void)s;
	return HIBR_OK;
}

/* Close every connection and forget every login when the module goes. */
void dv_fini(sh *s)
{
	(void)s;
	dv_closeall();
	dv_authfree();
	dv_conffree();
}

const hibr_bi dav_bi[] = {
	{ "dav", m_dav, "a WebDAV client: list, fetch and change files on a server" },
	HIBR_BI_END
};

HIBR_MODULE("dav", "0.1", "a WebDAV client", dav_bi, dv_ini, dv_fini);
