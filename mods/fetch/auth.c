#define _GNU_SOURCE

#include "ft.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>

vec ft_auths;

/* What we have learned about logging in to a place as a user. */
ft_auth *ft_authfind(const char *key)
{
	size_t i;
	ft_auth *a;

	for (i = 0; i < ft_auths.n; i++) {
		a = ft_auths.p[i];
		if (!strcmp(a->key, key))
			return a;
	}
	a = xm(sizeof *a);
	memset(a, 0, sizeof *a);
	a->key = xs(key);
	v_add(&ft_auths, a);
	return a;
}

/* Forget what a challenge said, before reading a new one. */
void ft_authclear(ft_auth *a)
{
	free(a->realm);
	free(a->nonce);
	free(a->opaque);
	free(a->algo);
	a->realm = a->nonce = a->opaque = a->algo = 0;
	a->kind = a->qop = a->sess = 0;
	a->nc = 0;
}

/* Forget every login learned. */
void ft_authfree(void)
{
	size_t i;
	ft_auth *a;

	for (i = 0; i < ft_auths.n; i++) {
		a = ft_auths.p[i];
		ft_authclear(a);
		free(a->key);
		free(a);
	}
	v_free(&ft_auths);
}

/* Read a token of a challenge: letters, digits and the usual punctuation. */
const char *ft_tok(const char *p, str *o)
{
	o->n = 0;
	while (*p && !strchr(" \t,=\"", *p))
		s_ch(o, *p++);
	if (!o->p)
		s_cat(o, "");
	return p;
}

/* Read a challenge parameter's value, quoted or not. */
const char *ft_pval(const char *p, str *o)
{
	o->n = 0;
	if (*p == '"') {
		p++;
		while (*p && *p != '"') {
			if (*p == '\\' && p[1])
				p++;
			s_ch(o, *p++);
		}
		if (*p == '"')
			p++;
	} else {
		while (*p && *p != ',' && *p != ' ' && *p != '\t')
			s_ch(o, *p++);
	}
	if (!o->p)
		s_cat(o, "");
	return p;
}

/* Read the server's challenges and pick the best login it offers: Digest
   over Basic. Whether there was one fetch can answer, and stale says the
   server only wants a new nonce. */
int ft_authparse(ft_auth *a, vec *chal, int *stale)
{
	size_t i;
	const char *p;
	str tok, val, scheme;
	int basic = 0, digest = 0, dq = 0, dsess = 0, dstale = 0, dalgok = 1;
	char *realm = 0, *nonce = 0, *opaque = 0, *algo = 0, *brealm = 0;

	s_init(&tok);
	s_init(&val);
	s_init(&scheme);
	*stale = 0;
	for (i = 0; i < chal->n; i++) {
		p = chal->p[i];
		scheme.n = 0;
		while (*p) {
			while (*p == ' ' || *p == '\t' || *p == ',')
				p++;
			if (!*p)
				break;
			p = ft_tok(p, &tok);
			while (*p == ' ' || *p == '\t')
				p++;
			if (*p != '=') {
				scheme.n = 0;
				s_cat(&scheme, tok.p);
				if (!strcasecmp(tok.p, "basic"))
					basic = 1;
				if (!strcasecmp(tok.p, "digest"))
					digest = 1;
				continue;
			}
			p++;
			while (*p == ' ' || *p == '\t')
				p++;
			p = ft_pval(p, &val);
			if (!scheme.p)
				continue;
			if (!strcasecmp(scheme.p, "basic")) {
				if (!strcasecmp(tok.p, "realm")) {
					free(brealm);
					brealm = xs(val.p);
				}
				continue;
			}
			if (strcasecmp(scheme.p, "digest"))
				continue;
			if (!strcasecmp(tok.p, "realm")) {
				free(realm);
				realm = xs(val.p);
			} else if (!strcasecmp(tok.p, "nonce")) {
				free(nonce);
				nonce = xs(val.p);
			} else if (!strcasecmp(tok.p, "opaque")) {
				free(opaque);
				opaque = xs(val.p);
			} else if (!strcasecmp(tok.p, "qop")) {
				dq = strstr(val.p, "auth") != 0 &&
				     (strstr(val.p, "auth,") || strstr(val.p, "auth ") ||
				      !strcmp(val.p, "auth") ||
				      (strlen(val.p) >= 4 &&
				       !strcmp(val.p + strlen(val.p) - 4, "auth")) ||
				      strstr(val.p, ",auth"));
			} else if (!strcasecmp(tok.p, "algorithm")) {
				free(algo);
				algo = xs(val.p);
				dsess = !strcasecmp(val.p, "MD5-sess");
				dalgok = !strcasecmp(val.p, "MD5") || dsess;
			} else if (!strcasecmp(tok.p, "stale")) {
				dstale = !strcasecmp(val.p, "true");
			}
		}
	}
	ft_authclear(a);
	if (digest && nonce && dalgok) {
		a->kind = 2;
		a->realm = realm ? realm : xs("");
		a->nonce = nonce;
		a->opaque = opaque;
		a->algo = algo;
		a->qop = dq;
		a->sess = dsess;
		*stale = dstale;
		realm = nonce = opaque = algo = 0;
	} else if (basic) {
		a->kind = 1;
		a->realm = brealm;
		brealm = 0;
	}
	free(realm);
	free(nonce);
	free(opaque);
	free(algo);
	free(brealm);
	s_free(&tok);
	s_free(&val);
	s_free(&scheme);
	return a->kind != 0;
}

/* Append a quoted Digest parameter. */
void ft_dpar(str *o, const char *k, const char *v, int q)
{
	s_cat(o, ", ");
	s_cat(o, k);
	s_ch(o, '=');
	if (q)
		s_ch(o, '"');
	for (; *v; v++) {
		if (q && (*v == '"' || *v == '\\'))
			s_ch(o, '\\');
		s_ch(o, *v);
	}
	if (q)
		s_ch(o, '"');
}

/* The Authorization header for a request, from what the server asked. */
void ft_authhdr(ft_auth *a, const char *user, const char *pass,
		const char *method,
		const char *uri, str *o)
{
	str t, ha1, ha2, rsp, cn, nc;
	unsigned long r;

	if (!a || !a->kind || !user)
		return;
	s_init(&t);
	if (a->kind == 1) {
		s_cat(&t, user);
		s_ch(&t, ':');
		s_cat(&t, pass);
		s_cat(o, "Authorization: Basic ");
		ft_b64(o, t.p, t.n);
		s_cat(o, "\r\n");
		s_free(&t);
		return;
	}
	s_init(&ha1);
	s_init(&ha2);
	s_init(&rsp);
	s_init(&cn);
	s_init(&nc);
	a->nc++;
	s_fmt(&nc, "%08lx", a->nc);
	r = (unsigned long)time(0) ^ ((unsigned long)getpid() << 16) ^
	    (unsigned long)a->nc * 2654435761u ^ (unsigned long)clock();
	s_num(&t, (long)r);
	ft_md5hex(t.p, t.n, &cn);
	cn.p[16] = 0;
	cn.n = 16;
	t.n = 0;
	s_cat(&t, user);
	s_ch(&t, ':');
	s_cat(&t, a->realm);
	s_ch(&t, ':');
	s_cat(&t, pass);
	ft_md5hex(t.p, t.n, &ha1);
	if (a->sess) {
		t.n = 0;
		s_cat(&t, ha1.p);
		s_ch(&t, ':');
		s_cat(&t, a->nonce);
		s_ch(&t, ':');
		s_cat(&t, cn.p);
		ha1.n = 0;
		ft_md5hex(t.p, t.n, &ha1);
	}
	t.n = 0;
	s_cat(&t, method);
	s_ch(&t, ':');
	s_cat(&t, uri);
	ft_md5hex(t.p, t.n, &ha2);
	t.n = 0;
	s_cat(&t, ha1.p);
	s_ch(&t, ':');
	s_cat(&t, a->nonce);
	s_ch(&t, ':');
	if (a->qop) {
		s_cat(&t, nc.p);
		s_ch(&t, ':');
		s_cat(&t, cn.p);
		s_cat(&t, ":auth:");
	}
	s_cat(&t, ha2.p);
	ft_md5hex(t.p, t.n, &rsp);
	s_cat(o, "Authorization: Digest username=\"");
	s_cat(o, user);
	s_ch(o, '"');
	ft_dpar(o, "realm", a->realm, 1);
	ft_dpar(o, "nonce", a->nonce, 1);
	ft_dpar(o, "uri", uri, 1);
	if (a->algo)
		ft_dpar(o, "algorithm", a->algo, 0);
	ft_dpar(o, "response", rsp.p, 1);
	if (a->opaque)
		ft_dpar(o, "opaque", a->opaque, 1);
	if (a->qop) {
		ft_dpar(o, "qop", "auth", 0);
		ft_dpar(o, "nc", nc.p, 0);
		ft_dpar(o, "cnonce", cn.p, 1);
	}
	s_cat(o, "\r\n");
	s_free(&t);
	s_free(&ha1);
	s_free(&ha2);
	s_free(&rsp);
	s_free(&cn);
	s_free(&nc);
}
