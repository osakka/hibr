#ifndef HIBR_DV_H
#define HIBR_DV_H

#include "hibr.h"
#include <stddef.h>
#include <sys/types.h>

/* How long a connection or a reply may take, in seconds, unless
   HIBR_DAV_TIMEOUT says otherwise. */
#ifndef DV_TIMEOUT
#define DV_TIMEOUT 30
#endif

/* How many redirects a request follows before it gives up. */
#ifndef DV_REDIRECTS
#define DV_REDIRECTS 5
#endif

/* How deep an XML reply may nest before it is refused. */
#ifndef DV_XDEPTH
#define DV_XDEPTH 200
#endif

/* How deep a recursive get or put goes. */
#ifndef DV_RDEPTH
#define DV_RDEPTH 64
#endif

/* A server the person has set up: a name, where it is, who to be there. */
typedef struct dv_srv dv_srv;
struct dv_srv {
	char *name, *url, *user, *pass;
	int noverify;
};

/* An address, split: scheme by tls, host, port, and the path as it goes
   on the wire, already percent-encoded. */
typedef struct dv_url dv_url;
struct dv_url {
	int tls;
	str host, port, path;
};

/* An open connection, kept for the next request to the same place. */
typedef struct dv_conn dv_conn;
struct dv_conn {
	char *key, *host;
	int fd, tls, noverify;
	pid_t relay, owner;
	str rb;
	size_t rp;
	int reused;
};

/* What a server asked to be told about who we are, per place and user. */
typedef struct dv_auth dv_auth;
struct dv_auth {
	char *key;
	int kind;
	char *realm, *nonce, *opaque, *algo;
	int qop, sess;
	unsigned long nc;
};

/* A request: the method, where, who, extra headers, and a body from
   memory or from a descriptor. A reply's body goes to out when it is 0 or
   more and the reply is a success, else into the reply. */
typedef struct dv_req dv_req;
struct dv_req {
	const char *method;
	const char *loc;
	const dv_srv *sv;
	vec hdr;
	const char *body;
	size_t blen;
	int bfd;
	off_t bflen;
	int out;
	int follow;
};

/* A reply: the status, the headers kept, and the body when not streamed. */
typedef struct dv_res dv_res;
struct dv_res {
	int code;
	str body;
	char *etag, *loc, *ctype, *reason;
	vec auth;
};

/* An XML element: its namespace and local name, its text, its children. */
typedef struct dv_x dv_x;
struct dv_x {
	char *ns, *name;
	str text;
	vec kids;
};

/* One entry of a listing. */
typedef struct dv_ent dv_ent;
struct dv_ent {
	char *name, *etag, *type;
	int dir;
	long long size;
	long mtime;
};

int dv_md5hex(const char *p, size_t n, str *out);

int dv_conf(sh *s);
dv_srv *dv_srvfind(const char *name);
dv_srv *dv_srvurl(const char *url);
int dv_srvset(sh *s, const char *name, const char *url, const char *user,
	      const char *pass, int noverify, int keeppass);
int dv_srvdel(sh *s, const char *name);
int dv_srvren(sh *s, const char *from, const char *to);
size_t dv_srvn(void);
dv_srv *dv_srvat(size_t i);
void dv_conffree(void);

int dv_urlsplit(const char *u, dv_url *o);
void dv_urlfree(dv_url *u);
void dv_enc(str *o, const char *p, int keepslash);
void dv_dec(str *o, const char *p, size_t n);
int dv_resolve(sh *s, const char *loc, str *url, const dv_srv **sv);

void dv_reqinit(dv_req *r);
void dv_reqfree(dv_req *r);
void dv_hdr(dv_req *r, const char *name, const char *val);
void dv_resinit(dv_res *r);
void dv_resfree(dv_res *r);
int dv_do(sh *s, dv_req *rq, dv_res *rs);
void dv_closeall(void);
int dv_timeout(sh *s);

dv_x *dv_xparse(const char *p, size_t n);
void dv_xfree(dv_x *x);
dv_x *dv_xkid(dv_x *x, const char *ns, const char *name);
long dv_httpdate(const char *p);

int dv_list(sh *s, const char *loc, int depth, vec *out);
void dv_entfree(vec *v);
const char *dv_why(int code);

#endif
