#ifndef HIBR_FT_H
#define HIBR_FT_H

#include "hibr.h"
#include "../fetch.h"
#include <stddef.h>
#include <sys/types.h>

/* How long a connection or a reply may go without progress, in seconds,
   unless HIBR_FETCH_TIMEOUT or the request says otherwise. */
#ifndef FT_TIMEOUT
#define FT_TIMEOUT 30
#endif

/* How many redirects a request follows unless it says otherwise. */
#ifndef FT_REDIRECTS
#define FT_REDIRECTS 10
#endif

/* An address, split: scheme by tls, host, port, and the path with its
   query as it goes on the wire. */
typedef struct ft_url ft_url;
struct ft_url {
	int tls;
	str host, port, path;
};

/* An open connection, kept for the next request to the same place: by
   the address it reaches, through a proxy or not. */
typedef struct ft_conn ft_conn;
struct ft_conn {
	char *key, *host;
	int fd, tls, noverify, proxied;
	pid_t relay, owner;
	str rb;
	size_t rp;
	int reused;
	int timedout;
};

/* What a server asked to be told about who we are, per place and user. */
typedef struct ft_auth ft_auth;
struct ft_auth {
	char *key;
	int kind;
	char *realm, *nonce, *opaque, *algo;
	int qop, sess;
	unsigned long nc;
};

/* A request as it is being carried out: the caller's request, where it
   is now, and what the reply has said so far. */
typedef struct ft_job ft_job;
struct ft_job {
	const ft_req *rq;
	const char *method;
	int sendbody;
	off_t bstart;
	int tmo;
	str err;
	int cancelled;
	int decode;	/* 0 none, 1 gzip, 2 deflate */
	str zbuf;	/* a compressed body, held until it is all in */
};

int ft_md5hex(const char *p, size_t n, str *out);

int ft_urlsplit(const char *u, ft_url *o, str *err);
void ft_urlfree(ft_url *u);
void ft_urlstr(const ft_url *u, str *o);
void ft_resolve(const char *base, const char *ref, str *o);
int ft_sameorigin(const char *a, const char *b);
long ft_httpdate(const char *p);
void ft_b64(str *o, const char *p, size_t n);

int ft_timeout(const ft_req *rq);
void ft_closeall(void);
int ft_do(const ft_req *rq, ft_res *rs);
void ft_resinit(ft_res *r);
void ft_resfree(ft_res *r);
const char *ft_header(const ft_res *rs, const char *name);

ft_auth *ft_authfind(const char *key);
void ft_authfree(void);
int ft_authparse(ft_auth *a, vec *chal, int *stale);
void ft_authhdr(ft_auth *a, const char *user, const char *pass,
		const char *method, const char *uri, str *o);

int ft_jarok(const char *jar);
void ft_cookiehdr(const char *jar, const ft_url *u, str *o);
void ft_cookieset(const char *jar, const ft_url *u, const char *line);
void ft_jarsave(const char *jar);
int ft_jarclear(sh *s, const char *jar);
void ft_jarfree(void);

#endif
