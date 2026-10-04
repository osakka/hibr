#include "dv.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* The prefixes a property may be named by, and the namespaces they mean:
   WebDAV's, CalDAV's, CardDAV's, calendarserver.org's and Apple's. */
static const char *dv_nspre[] = {
	"d", "DAV:",
	"c", "urn:ietf:params:xml:ns:caldav",
	"cal", "urn:ietf:params:xml:ns:caldav",
	"card", "urn:ietf:params:xml:ns:carddav",
	"cs", "http://calendarserver.org/ns/",
	"ical", "http://apple.com/ns/ical/",
	0
};

/* The same prefixes declared, for the root element of a request. */
void dv_nsdecl(str *b)
{
	size_t i;

	for (i = 0; dv_nspre[i]; i += 2) {
		s_cat(b, " xmlns:");
		s_cat(b, dv_nspre[i]);
		s_cat(b, "=\"");
		s_cat(b, dv_nspre[i + 1]);
		s_ch(b, '"');
	}
}

/* Text made safe inside an XML element or attribute. */
void dv_xesc(str *b, const char *p)
{
	for (; *p; p++) {
		if (*p == '&')
			s_cat(b, "&amp;");
		else if (*p == '<')
			s_cat(b, "&lt;");
		else if (*p == '>')
			s_cat(b, "&gt;");
		else if (*p == '"')
			s_cat(b, "&quot;");
		else
			s_ch(b, *p);
	}
}

/* One empty property element, named pre:name or {namespace}name; 0 for a
   prefix nobody declared. */
int dv_propel(str *b, const char *p)
{
	const char *c;
	size_t i;

	if (*p == '{') {
		c = strchr(p, '}');
		if (!c || !c[1])
			return 0;
		s_cat(b, "<x:");
		s_cat(b, c + 1);
		s_cat(b, " xmlns:x=\"");
		s_add(b, p + 1, (size_t)(c - p - 1));
		s_cat(b, "\"/>");
		return 1;
	}
	c = strchr(p, ':');
	if (!c) {
		s_cat(b, "<d:");
		s_cat(b, p);
		s_cat(b, "/>");
		return 1;
	}
	for (i = 0; dv_nspre[i]; i += 2)
		if (strlen(dv_nspre[i]) == (size_t)(c - p) && !strncmp(dv_nspre[i], p, (size_t)(c - p)))
			break;
	if (!dv_nspre[i])
		return 0;
	s_ch(b, '<');
	s_cat(b, p);
	s_cat(b, "/>");
	return 1;
}

/* Text with the white space at both ends taken off. */
void dv_trim(str *o, const char *p, size_t n)
{
	while (n && strchr(" \t\r\n", *p))
		p++, n--;
	while (n && strchr(" \t\r\n", p[n - 1]))
		n--;
	s_add(o, p, n);
}

/* A property's value as text: its hrefs a line each, else the names of its
   child elements -- or their name attribute, as a calendar's components
   have -- a word each, else its own text. */
void dv_propval(dv_x *x, str *o)
{
	size_t i;
	dv_x *k;
	int hrefs = 0;

	for (i = 0; i < x->kids.n; i++) {
		k = x->kids.p[i];
		if (strcmp(k->ns, "DAV:") || strcmp(k->name, "href"))
			continue;
		if (hrefs++)
			s_ch(o, '\n');
		dv_trim(o, k->text.p ? k->text.p : "", k->text.n);
	}
	if (hrefs)
		return;
	if (x->kids.n) {
		for (i = 0; i < x->kids.n; i++) {
			k = x->kids.p[i];
			if (i)
				s_ch(o, ' ');
			s_cat(o, k->aname ? k->aname : k->name);
		}
		return;
	}
	dv_trim(o, x->text.p ? x->text.p : "", x->text.n);
}

/* The number in an HTTP status line: "HTTP/1.1 404 Not Found" is 404. */
int dv_statusof(dv_x *x)
{
	const char *p = x && x->text.p ? x->text.p : "";

	p = strchr(p, ' ');
	return p ? atoi(p + 1) : 0;
}

/* Set r[i][...] in $RET, one level or two below the entry. */
void dv_msset(sh *s, size_t i, const char *a, const char *b, const char *v)
{
	str k;
	char *ks[3];

	s_init(&k);
	s_num(&k, (long)i);
	ks[0] = k.p;
	ks[1] = (char *)a;
	ks[2] = (char *)b;
	hibr_setp(s, "RET", ks, b ? 3 : 2, v);
	s_free(&k);
}

/* Send a PROPFIND or REPORT and give back its multistatus: with := a map an
   entry -- r[i]["href"], ["status"], ["props"][name] -- else a line a
   property, href, status, name and value. A sync token in the reply goes in
   DAV_SYNC. */
int dv_ms(sh *s, const char *method, const char *loc, const char *depth,
	  const char *body, size_t blen)
{
	str url, v, href;
	const dv_srv *sv;
	dv_req rq;
	dv_res rs;
	dv_x *doc, *ms, *r, *k, *ps, *pr, *p;
	size_t i, j, m, n = 0;
	int rc, st, pst;

	s_init(&url);
	if (dv_resolve(s, loc, &url, &sv) != HIBR_OK) {
		s_free(&url);
		return HIBR_FAIL;
	}
	dv_reqinit(&rq);
	rq.method = method;
	rq.loc = url.p;
	rq.sv = sv;
	rq.follow = 1;
	rq.body = body;
	rq.blen = blen;
	if (depth)
		dv_hdr(&rq, "Depth", depth);
	dv_hdr(&rq, "Content-Type", "application/xml; charset=utf-8");
	rc = dv_do(s, &rq, &rs);
	dv_reqfree(&rq);
	if (rc != HIBR_OK || rs.code != 207) {
		if (rc == HIBR_OK && rs.code < 300)
			rs.code = 502;
		dv_err(s, "cannot ask", loc, &rs);
		dv_resfree(&rs);
		s_free(&url);
		return HIBR_FAIL;
	}
	doc = dv_xparse(rs.body.p ? rs.body.p : "", rs.body.n);
	ms = dv_xkid(doc, "DAV:", "multistatus");
	if (!ms) {
		lg(HIBR_LERR, "dav: %s did not answer in WebDAV's own form", loc);
		dv_xfree(doc);
		dv_code(s, 502);
		dv_resfree(&rs);
		s_free(&url);
		return HIBR_FAIL;
	}
	s_init(&v);
	s_init(&href);
	for (i = 0; i < ms->kids.n; i++) {
		r = ms->kids.p[i];
		if (strcmp(r->ns, "DAV:"))
			continue;
		if (!strcmp(r->name, "sync-token")) {
			v.n = 0;
			dv_trim(&v, r->text.p ? r->text.p : "", r->text.n);
			hibr_set(s, "DAV_SYNC", v.p ? v.p : "", 0);
			continue;
		}
		if (strcmp(r->name, "response"))
			continue;
		href.n = 0;
		k = dv_xkid(r, "DAV:", "href");
		if (k)
			dv_trim(&href, k->text.p ? k->text.p : "", k->text.n);
		st = dv_statusof(dv_xkid(r, "DAV:", "status"));
		for (j = 0; j < r->kids.n; j++) {
			ps = r->kids.p[j];
			if (strcmp(ps->ns, "DAV:") || strcmp(ps->name, "propstat"))
				continue;
			pst = dv_statusof(dv_xkid(ps, "DAV:", "status"));
			if (!st || (pst >= 200 && pst < 300))
				st = st && st < 300 ? st : pst;
			if (pst < 200 || pst >= 300)
				continue;
			pr = dv_xkid(ps, "DAV:", "prop");
			for (m = 0; pr && m < pr->kids.n; m++) {
				p = pr->kids.p[m];
				v.n = 0;
				dv_propval(p, &v);
				if (s->bind)
					dv_msset(s, n, "props", p->name, v.p ? v.p : "");
				else
					printf("%s\t%d\t%s\t%s\n", href.p ? href.p : "", pst, p->name,
					       v.p ? v.p : "");
			}
		}
		v.n = 0;
		s_num(&v, st);
		if (s->bind) {
			dv_msset(s, n, "href", 0, href.p ? href.p : "");
			dv_msset(s, n, "status", 0, v.p);
		} else if (st < 200 || st >= 300) {
			printf("%s\t%d\n", href.p ? href.p : "", st);
		}
		n++;
	}
	if (s->bind && !n)
		hibr_retn(s, 0, 0);
	lg(HIBR_LDBG, "dav: %s %s: %zu entries", method, loc, n);
	dv_code(s, rs.code);
	s_free(&v);
	s_free(&href);
	dv_xfree(doc);
	dv_resfree(&rs);
	s_free(&url);
	return HIBR_OK;
}

/* dav propfind [-d 0|1] LOC prop...: the named properties of a location,
   and with -d 1 of what it holds. */
int dv_cpropfind(sh *s, int ac, char **av)
{
	const char *depth = "0", *loc;
	const char *dflt[] = { "d:resourcetype", "d:displayname", "d:getetag", 0 };
	str b;
	int i = 2, j, rc;

	if (i + 1 < ac && !strcmp(av[i], "-d")) {
		depth = av[i + 1];
		i += 2;
	}
	if (i >= ac || (strcmp(depth, "0") && strcmp(depth, "1"))) {
		lg(HIBR_LERR, "usage: dav propfind [-d 0|1] location [prop...]");
		return 2;
	}
	loc = av[i++];
	s_init(&b);
	s_cat(&b, "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<d:propfind");
	dv_nsdecl(&b);
	s_cat(&b, "><d:prop>");
	for (j = i; j < ac || (i == ac && dflt[j - i]); j++) {
		const char *p = i == ac ? dflt[j - i] : av[j];

		if (!dv_propel(&b, p)) {
			lg(HIBR_LERR, "dav: %s: not a property this knows how to name "
				      "(use d: c: card: cs: ical: or {namespace}name)", p);
			s_free(&b);
			return 2;
		}
	}
	s_cat(&b, "</d:prop></d:propfind>\n");
	rc = dv_ms(s, "PROPFIND", loc, depth, b.p, b.n);
	s_free(&b);
	return rc;
}

/* The whole of a file, for a request body. */
int dv_slurp(const char *path, str *o)
{
	int fd = open(path, O_RDONLY | O_CLOEXEC);
	char *buf;
	ssize_t r;

	if (fd < 0) {
		lg(HIBR_LERR, "dav: cannot read %s: %s", path, strerror(errno));
		return HIBR_FAIL;
	}
	buf = xm(HIBR_IOCH);
	while ((r = read(fd, buf, HIBR_IOCH)) > 0)
		s_add(o, buf, (size_t)r);
	free(buf);
	close(fd);
	return r < 0 ? HIBR_FAIL : HIBR_OK;
}

/* dav report [-d N] LOC -b BODY | -f FILE: a REPORT with a body of the
   script's making -- a calendar-query, a multiget, an addressbook-query. */
int dv_creport(sh *s, int ac, char **av)
{
	const char *depth = 0, *loc = 0;
	str b;
	int i, rc, have = 0;

	s_init(&b);
	for (i = 2; i < ac; i++) {
		if (!strcmp(av[i], "-d") && i + 1 < ac) {
			depth = av[++i];
		} else if (!strcmp(av[i], "-b") && i + 1 < ac) {
			s_cat(&b, av[++i]);
			have = 1;
		} else if (!strcmp(av[i], "-f") && i + 1 < ac) {
			if (dv_slurp(av[++i], &b) != HIBR_OK) {
				s_free(&b);
				return HIBR_FAIL;
			}
			have = 1;
		} else if (!loc) {
			loc = av[i];
		} else {
			loc = 0;
			break;
		}
	}
	if (!loc || !have) {
		lg(HIBR_LERR, "usage: dav report [-d depth] location -b body | -f file");
		s_free(&b);
		return 2;
	}
	rc = dv_ms(s, "REPORT", loc, depth, b.p ? b.p : "", b.n);
	s_free(&b);
	return rc;
}

/* dav sync LOC [-t TOKEN] [prop...]: what changed in a collection since a
   sync token (RFC 6578) -- everything, with none; a removed member has
   status 404 -- and the new token in DAV_SYNC. */
int dv_csync(sh *s, int ac, char **av)
{
	const char *tok = "", *loc;
	str b;
	int i = 2, rc;

	if (i >= ac) {
		lg(HIBR_LERR, "usage: dav sync location [-t token] [prop...]");
		return 2;
	}
	loc = av[i++];
	if (i + 1 < ac && !strcmp(av[i], "-t")) {
		tok = av[i + 1];
		i += 2;
	}
	s_init(&b);
	s_cat(&b, "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<d:sync-collection");
	dv_nsdecl(&b);
	s_cat(&b, "><d:sync-token>");
	dv_xesc(&b, tok);
	s_cat(&b, "</d:sync-token><d:sync-level>1</d:sync-level><d:prop>");
	if (i >= ac)
		s_cat(&b, "<d:getetag/>");
	for (; i < ac; i++)
		if (!dv_propel(&b, av[i])) {
			lg(HIBR_LERR, "dav: %s: not a property this knows how to name", av[i]);
			s_free(&b);
			return 2;
		}
	s_cat(&b, "</d:prop></d:sync-collection>\n");
	hibr_set(s, "DAV_SYNC", "", 0);
	rc = dv_ms(s, "REPORT", loc, 0, b.p, b.n);
	s_free(&b);
	return rc;
}
