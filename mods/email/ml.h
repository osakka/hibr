#ifndef ML_H
#define ML_H

#include "hibr.h"
#include <stddef.h>
#include <sys/types.h>

#ifndef ML_TIMEOUT
#define ML_TIMEOUT 30
#endif

#ifndef ML_MAXMSG
#define ML_MAXMSG (128L * 1024 * 1024)
#endif

enum { ML_IMAP, ML_POP };
enum { ML_TLS, ML_STARTTLS, ML_PLAIN };

/* An account: where its mail is read from and sent through, and as whom. */
typedef struct ml_acct ml_acct;
struct ml_acct {
	char *name, *host, *port, *user, *pass, *email, *full;
	char *shost, *sport;
	int kind, sec, ssec, noverify;
};

/* A connection: a socket, or the pipe to a TLS relay this process forked,
   with what has been read and not yet used. */
typedef struct ml_conn ml_conn;
struct ml_conn {
	int fd, tls, noverify, tmo;
	pid_t relay, owner;
	char *host;
	str rb;
	size_t rp;
};

/* An IMAP value: an atom, a string (quoted or literal), NIL, or a
   parenthesised list. */
typedef struct ml_iv ml_iv;
struct ml_iv {
	int t;
	str s;
	vec kids;
};

/* An IMAP session: its connection, the next tag, what the server can do,
   and what the last command's untagged replies said. */
typedef struct ml_imap ml_imap;
struct ml_imap {
	ml_conn *c;
	long tag;
	int gmail, move, idle, uidplus, condstore, special;
	str caps, status;
	vec untagged;
	char *folder;
};

/* A MIME part found in a message. */
typedef struct ml_part ml_part;
struct ml_part {
	char *type, *charset, *name, *cid, *disp, *enc;
	size_t hs, bs, be;
	int leaf;
};

/* A parsed message: its decoded headers, the text and HTML bodies as
   UTF-8, and its parts. */
typedef struct ml_msg ml_msg;
struct ml_msg {
	vec hn, hv;
	str text, html;
	vec parts;
	const char *raw;
	size_t rawn;
};

int fd_high(int fd);
void ml_clr(str *s);

ml_conn *ml_dial(const char *host, const char *port, int sec, int noverify, int tmo);
int ml_starttls(ml_conn *c);
void ml_close(ml_conn *c);
int ml_wall(ml_conn *c, const char *p, size_t n);
int ml_line(ml_conn *c, str *o);
int ml_bytes(ml_conn *c, size_t n, str *o);
int ml_ready(ml_conn *c, int ms);

int ml_conf(sh *s);
ml_acct *ml_acctget(sh *s, const char *name);
int ml_acctset(sh *s, int ac, char **av);
int ml_acctrm(sh *s, const char *name);
int ml_acctmv(sh *s, const char *old, const char *nw);
int ml_accts(sh *s);
void ml_conffree(void);

ml_imap *im_open(sh *s, ml_acct *a);
void im_close(ml_imap *m);
int im_cmd(ml_imap *m, const char *fmt, str *extra);
void im_quote(str *o, const char *s);
ml_iv *im_parse(const char *p, size_t n, size_t *i);
void iv_free(ml_iv *v);
ml_iv *iv_get(ml_iv *list, const char *key);

void ml_b64enc(str *o, const char *p, size_t n);
void ml_b64dec(str *o, const char *p, size_t n);
void ml_qpdec(str *o, const char *p, size_t n, int header);
void ml_qpenc(str *o, const char *p, size_t n);
void ml_toutf8(str *o, const char *cs, const char *p, size_t n);
void ml_hdec(str *o, const char *p, size_t n);
void ml_henc(str *o, const char *p);
void ml_mutf7dec(str *o, const char *p);
void ml_mutf7enc(str *o, const char *p);
long ml_date(const char *s);
int ml_msgparse(const char *raw, size_t n, ml_msg *m);
void ml_msgfree(ml_msg *m);
const char *ml_hget(ml_msg *m, const char *name);
int ml_partbody(ml_msg *m, size_t i, str *o);
void ml_hraw(const char *raw, size_t n, const char *name, str *o);

#endif
