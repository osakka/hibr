#define _GNU_SOURCE

#include "dv.h"
#include "../fetch.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

/* Split an http or https address into its parts; the path stays as it is
   written, already encoded. */
int dv_urlsplit(const char *u, dv_url *o)
{
	const char *p, *e, *hp, *at, *c;

	s_init(&o->host);
	s_init(&o->port);
	s_init(&o->path);
	if (!strncasecmp(u, "https://", 8)) {
		o->tls = 1;
		p = u + 8;
	} else if (!strncasecmp(u, "http://", 7)) {
		o->tls = 0;
		p = u + 7;
	} else {
		lg(HIBR_LERR, "dav: %s is not an http:// or https:// address", u);
		return HIBR_FAIL;
	}
	e = p + strcspn(p, "/?#");
	hp = p;
	for (at = p; at < e; at++)
		if (*at == '@')
			hp = at + 1;
	if (hp < e && *hp == '[') {
		c = memchr(hp, ']', (size_t)(e - hp));
		if (!c) {
			lg(HIBR_LERR, "dav: %s: an unclosed [ in the host", u);
			return HIBR_FAIL;
		}
		s_add(&o->host, hp + 1, (size_t)(c - hp - 1));
		c++;
		if (c < e && *c == ':')
			s_add(&o->port, c + 1, (size_t)(e - c - 1));
	} else {
		c = hp;
		while (c < e && *c != ':')
			c++;
		s_add(&o->host, hp, (size_t)(c - hp));
		if (c < e)
			s_add(&o->port, c + 1, (size_t)(e - c - 1));
	}
	if (!o->host.n) {
		lg(HIBR_LERR, "dav: %s names no host", u);
		return HIBR_FAIL;
	}
	if (!o->port.n)
		s_cat(&o->port, o->tls ? "443" : "80");
	if (*e == '/' || *e == '?')
		s_add(&o->path, e, strcspn(e, "#"));
	if (!o->path.n || o->path.p[0] != '/') {
		str t;

		s_init(&t);
		s_ch(&t, '/');
		if (o->path.n)
			s_cat(&t, o->path.p);
		s_free(&o->path);
		o->path = t;
	}
	return HIBR_OK;
}

/* Free a split address. */
void dv_urlfree(dv_url *u)
{
	s_free(&u->host);
	s_free(&u->port);
	s_free(&u->path);
}

/* Percent-encode text for a path: everything but the unreserved
   characters, and the slash when it separates segments. */
void dv_enc(str *o, const char *p, int keepslash)
{
	static const char hx[] = "0123456789ABCDEF";
	unsigned char c;

	for (; *p; p++) {
		c = (unsigned char)*p;
		if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
		    (c >= '0' && c <= '9') || c == '-' || c == '.' || c == '_' ||
		    c == '~' || (keepslash && c == '/')) {
			s_ch(o, c);
		} else {
			s_ch(o, '%');
			s_ch(o, hx[c >> 4]);
			s_ch(o, hx[c & 15]);
		}
	}
}

/* The value of one hex digit, or -1. */
int dv_hex(int c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}

/* Undo percent-encoding. */
void dv_dec(str *o, const char *p, size_t n)
{
	size_t i;
	int a, b;

	for (i = 0; i < n; i++) {
		if (p[i] == '%' && i + 2 < n) {
			a = dv_hex((unsigned char)p[i + 1]);
			b = dv_hex((unsigned char)p[i + 2]);
			if (a >= 0 && b >= 0) {
				s_ch(o, a * 16 + b);
				i += 2;
				continue;
			}
		}
		s_ch(o, p[i]);
	}
}

/* Turn a location into a full address and the server it belongs to:
   dav://name/path names a server set up with `dav server`; an http or
   https address is taken as it is, with the login of the server whose
   address it starts with, if any. */
int dv_resolve(sh *s, const char *loc, str *url, const dv_srv **sv)
{
	const char *p, *slash;
	str nm;
	dv_srv *v;
	size_t n;

	*sv = 0;
	url->n = 0;
	if (dv_conf(s) != HIBR_OK)
		return HIBR_FAIL;
	if (!strncmp(loc, "dav://", 6)) {
		p = loc + 6;
		slash = strchr(p, '/');
		s_init(&nm);
		s_add(&nm, p, slash ? (size_t)(slash - p) : strlen(p));
		v = dv_srvfind(nm.p ? nm.p : "");
		if (!v) {
			lg(HIBR_LERR, "dav: no server called %s (see dav servers)",
			   nm.p ? nm.p : "");
			s_free(&nm);
			return HIBR_FAIL;
		}
		s_free(&nm);
		n = strlen(v->url);
		while (n && v->url[n - 1] == '/')
			n--;
		s_add(url, v->url, n);
		s_ch(url, '/');
		if (slash) {
			while (*slash == '/')
				slash++;
			dv_enc(url, slash, 1);
		}
		*sv = v;
		return HIBR_OK;
	}
	if (strncasecmp(loc, "http://", 7) && strncasecmp(loc, "https://", 8)) {
		lg(HIBR_LERR, "dav: %s is neither dav://server/path nor an "
			      "http(s) address", loc);
		return HIBR_FAIL;
	}
	s_cat(url, loc);
	*sv = dv_srvurl(loc);
	return HIBR_OK;
}

/* The timeout, in seconds, from HIBR_DAV_TIMEOUT or the default. */
int dv_timeout(sh *s)
{
	const char *v = hibr_get(s, "HIBR_DAV_TIMEOUT");
	int n = v ? atoi(v) : 0;

	return n > 0 ? n : DV_TIMEOUT;
}


/* An empty request. */
void dv_reqinit(dv_req *r)
{
	memset(r, 0, sizeof *r);
	r->bfd = -1;
	r->out = -1;
}

/* Free a request's extra headers. */
void dv_reqfree(dv_req *r)
{
	size_t i;

	for (i = 0; i < r->hdr.n; i++)
		free(r->hdr.p[i]);
	v_free(&r->hdr);
}

/* Add a header to a request. */
void dv_hdr(dv_req *r, const char *name, const char *val)
{
	str h;

	s_init(&h);
	s_cat(&h, name);
	s_cat(&h, ": ");
	s_cat(&h, val);
	v_add(&r->hdr, xs(h.p));
	s_free(&h);
}

/* An empty reply. */
void dv_resinit(dv_res *r)
{
	memset(r, 0, sizeof *r);
	s_init(&r->body);
}

/* Free a reply. */
void dv_resfree(dv_res *r)
{
	size_t i;

	s_free(&r->body);
	free(r->etag);
	free(r->loc);
	free(r->ctype);
	free(r->reason);
	for (i = 0; i < r->auth.n; i++)
		free(r->auth.p[i]);
	v_free(&r->auth);
	dv_resinit(r);
}

/* The fetch module's interface, asked for each time so a module dropped
   and loaded again is never reached through a table it has unmapped. */
ft_api *dv_fetch(sh *s)
{
	ft_api *api = s ? hibr_require(s, "fetch", FT_API_VER) : 0;

	if (!api)
		lg(HIBR_LERR, "dav: the fetch module is not available");
	return api;
}

/* Where a reply's body goes while it arrives: the request's descriptor for
   a success, the reply otherwise. */
typedef struct dv_sinkc dv_sinkc;
struct dv_sinkc {
	int out, tofd, failed;
	dv_res *rs;
};

/* The final reply's head, before its body: whether the body is streamed. */
int dv_onhead(void *ctx, const ft_res *rs)
{
	dv_sinkc *k = ctx;

	k->tofd = k->out >= 0 && rs->code >= 200 && rs->code < 300;
	return 0;
}

/* A piece of the body, to the descriptor or into the reply. */
int dv_ondata(void *ctx, const char *p, size_t n)
{
	dv_sinkc *k = ctx;

	if (!k->tofd) {
		s_add(&k->rs->body, p, n);
		return 0;
	}
	if (hibr_wrall(k->out, p, n) != (ssize_t)n) {
		k->failed = errno ? errno : EIO;
		return 1;
	}
	return 0;
}

/* A copy of a reply header's value, or 0. */
char *dv_hcopy(ft_api *api, const ft_res *rs, const char *name)
{
	const char *v = api->header(rs, name);

	return v ? xs(v) : 0;
}

/* Do a request through the fetch module: it connects or reuses, logs in as
   the server asks, follows redirects and retries a kept connection that
   turned out dead. The status is OK for a 2xx reply; rs->code says what
   came back, 0 when nothing did. */
int dv_do(sh *s, dv_req *rq, dv_res *rs)
{
	ft_api *api = dv_fetch(s);
	const dv_srv *sv = rq->sv;
	ft_req fq;
	ft_res fr;
	dv_sinkc k;
	size_t i;
	int rc;

	dv_resinit(rs);
	if (!api)
		return HIBR_FAIL;
	api->reqinit(&fq);
	fq.method = rq->method;
	fq.url = rq->loc;
	fq.hdr = (const char **)rq->hdr.p;
	fq.nhdr = rq->hdr.n;
	fq.body = rq->body;
	fq.blen = rq->blen;
	if (rq->bfd >= 0) {
		if (lseek(rq->bfd, 0, SEEK_SET) < 0) {
			lg(HIBR_LERR, "dav: cannot read what to send: %s",
			   strerror(errno));
			return HIBR_FAIL;
		}
		fq.bfd = rq->bfd;
		fq.bflen = (long long)rq->bflen;
	}
	if (sv && sv->user && *sv->user) {
		fq.user = sv->user;
		fq.pass = sv->pass ? sv->pass : "";
	}
	fq.flags = FT_IDENTITY | FT_NOCOOKIE;
	if (sv && sv->noverify)
		fq.flags |= FT_NOVERIFY;
	if (!rq->follow)
		fq.flags |= FT_NOFOLLOW;
	fq.timeout = dv_timeout(s);
	fq.maxredirects = DV_REDIRECTS;
	memset(&k, 0, sizeof k);
	k.out = rq->out;
	k.rs = rs;
	fq.onhead = dv_onhead;
	fq.ondata = dv_ondata;
	fq.ctx = &k;
	lg(HIBR_LDBG, "dav: %s %s", rq->method, rq->loc);
	rc = api->fetch(s, &fq, &fr);
	rs->code = fr.code;
	rs->reason = fr.reason ? xs(fr.reason) : 0;
	rs->ctype = fr.ctype ? xs(fr.ctype) : 0;
	rs->etag = dv_hcopy(api, &fr, "ETag");
	rs->loc = dv_hcopy(api, &fr, "Location");
	for (i = 0; i < fr.hdr.n; i++)
		if (!strncasecmp(fr.hdr.p[i], "WWW-Authenticate:", 17))
			v_add(&rs->auth, xs(fr.hdr.p[i]));
	if (k.failed) {
		lg(HIBR_LERR, "dav: cannot write what arrived: %s",
		   strerror(k.failed));
		rs->code = 0;
	} else if (rc != 0) {
		lg(HIBR_LERR, "dav: %s", fr.err ? fr.err : "the request failed");
		rs->code = 0;
	}
	api->resfree(&fr);
	return rs->code >= 200 && rs->code < 300 ? HIBR_OK : HIBR_FAIL;
}

/* Close every kept connection; the next request opens its own. */
void dv_closeall(sh *s)
{
	ft_api *api = s ? hibr_require(s, "fetch", FT_API_VER) : 0;

	if (api)
		api->closeall();
}
