#ifndef HIBR_FETCH_H
#define HIBR_FETCH_H

/* The interface the fetch module offers, "fetch" version FT_API_VER: an
   HTTP/1.1 client for every module that speaks to a web server, so that
   the web module, DAV and /dev/http stop each carrying one of their own
   (Gitea #399). Asked for with hibr_require(s, "fetch", FT_API_VER), which
   loads the module if it is not loaded yet.

   One request at a time, and it blocks: fetch() returns once the reply has
   been read, or the request failed or was cancelled. It keeps connections
   open between requests to the same place, and a TLS connection is carried
   by a relay that fetch() forks -- so a caller that must not block calls it
   from a child process of its own, never from a thread of a process that
   has others (a fork from a threaded process can leave the child stuck on
   a lock another thread held). Nothing in it writes to the terminal --
   what went wrong is said in the reply's err -- and it reads its settings
   from the environment (HIBR_FETCH_TIMEOUT, XDG_DATA_HOME, https_proxy,
   http_proxy, no_proxy, HIBR_TLS_INSECURE), so s may be 0. */

#include "hibr.h"
#include <sys/types.h>

#ifndef FT_API_VER
#define FT_API_VER 1
#endif

/* Request flags. */
#ifndef FT_NOFOLLOW
#define FT_NOFOLLOW 1u   /* hand a redirect back rather than follow it */
#define FT_NOVERIFY 2u   /* skip the certificate check, for this request */
#define FT_IDENTITY 4u   /* ask for the body uncompressed, so ondata gets it
			    as it arrives: a gzip or deflate body is only
			    decoded once all of it is in, since the decoder
			    takes a whole stream */
#define FT_NOCOOKIE 8u   /* neither send nor keep cookies, whatever jar says */
#endif

typedef struct ft_res ft_res;

/* A request. Start from ft_api.reqinit, which sets every field to its
   default, then fill what is wanted; nothing in it is freed by fetch. */
typedef struct ft_req ft_req;
struct ft_req {
	const char *method;	/* "GET" when 0 */
	const char *url;	/* http:// or https://, percent-encoded */
	const char **hdr;	/* nhdr lines of "Name: value" */
	size_t nhdr;
	const char *body;	/* a body from memory, blen bytes ... */
	size_t blen;
	int bfd;		/* ... or from a descriptor, bflen bytes, read
				   from its current offset; -1 for none. A
				   307 or 308 sends it again from that same
				   offset, which needs a descriptor that seeks;
				   one that cannot (a pipe) ends the request
				   there with err saying so */
	long long bflen;
	const char *user;	/* a login for a server that asks (Basic or */
	const char *pass;	/* Digest); never sent to another origin */
	const char *jar;	/* the cookie jar, by profile name: "default",
				   or a web app's own; 0 for no cookies */
	unsigned flags;		/* FT_ above */
	int timeout;		/* seconds without progress before giving up;
				   0 for the default (HIBR_FETCH_TIMEOUT, 30) */
	int maxredirects;	/* 0 for the default, 10 */
	/* Called once the final reply's status and headers are in, before
	   its body; ondata then for each piece of the body, decoded. Either
	   returning nonzero cancels the request: fetch() then returns -1 with
	   err saying it was cancelled. With no ondata the body is collected
	   into the reply's body, up to FT_MAXBODY. */
	int (*onhead)(void *ctx, const ft_res *rs);
	int (*ondata)(void *ctx, const char *p, size_t n);
	void *ctx;
	/* Looked at at least every FT_TICK milliseconds while a request waits
	   on the network, so a signal handler (or the parent, through memory
	   it shares with the child) can stop a request held by a slow server:
	   nonzero cancels. */
	volatile int *cancel;
};

/* A reply: the final one, after any redirects followed. Free it with
   ft_api.resfree. */
struct ft_res {
	int code;		/* the status, 0 when no reply came at all */
	char *reason;		/* the status line's own words */
	char *url;		/* where the reply came from, after redirects */
	char *ctype;		/* Content-Type, or 0 */
	vec hdr;		/* every header line of the reply, "Name: value",
				   as it came (char *) */
	str body;		/* the body, when the request had no ondata */
	long long length;	/* the body's length as decoded, so far */
	int redirects;		/* how many were followed */
	char *err;		/* why fetch() failed, or 0 when it did not */
};

/* The table the module offers. */
typedef struct ft_api ft_api;
struct ft_api {
	unsigned ver;
	void (*reqinit)(ft_req *rq);
	/* s may be 0. 0 when a reply came, whatever its status -- a 404 is a
	   reply --
	   and -1 when none did, the request was refused before it was sent,
	   or it was cancelled; rs->err then says why. rs is filled either
	   way and must be freed. */
	int (*fetch)(sh *s, const ft_req *rq, ft_res *rs);
	void (*resfree)(ft_res *rs);
	/* The value of the first header of that name in a reply, or 0. */
	const char *(*header)(const ft_res *rs, const char *name);
	/* Close every kept connection; the next request opens its own. */
	void (*closeall)(void);
	/* Forget a jar's cookies, in memory and on disk. */
	int (*jarclear)(sh *s, const char *jar);
};

#ifndef FT_TICK
#define FT_TICK 250
#endif

#ifndef FT_MAXBODY
#define FT_MAXBODY (256L * 1024 * 1024)
#endif

#endif
