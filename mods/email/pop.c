#define _GNU_SOURCE

#include "ml.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

/* A POP3 reply line: whether it says +OK; the line kept. */
int po_ok(ml_conn *c, str *line)
{
	if (ml_line(c, line) != HIBR_OK)
		return 0;
	return !strncmp(line->p, "+OK", 3);
}

/* Send a POP3 command and read its one-line reply. */
int po_cmd(ml_conn *c, const char *cmd, str *line)
{
	str o;
	int rc;

	s_init(&o);
	s_cat(&o, cmd);
	s_cat(&o, "\r\n");
	rc = ml_wall(c, o.p, o.n) == HIBR_OK && po_ok(c, line);
	memset(o.p, 0, o.n);
	s_free(&o);
	return rc;
}

/* A multi-line reply's body, the terminating dot removed and each line's
   stuffed dot taken off, lines ending CRLF. */
int po_multi(ml_conn *c, str *o)
{
	str line;

	s_init(&line);
	for (;;) {
		if (ml_line(c, &line) != HIBR_OK) {
			s_free(&line);
			return HIBR_FAIL;
		}
		if (line.n == 1 && line.p[0] == '.')
			break;
		s_add(o, line.p[0] == '.' ? line.p + 1 : line.p, line.p[0] == '.' ? line.n - 1 : line.n);
		s_cat(o, "\r\n");
		if (o->n > (size_t)ML_MAXMSG) {
			lg(HIBR_LERR, "email: a message larger than this reads");
			s_free(&line);
			return HIBR_FAIL;
		}
	}
	s_free(&line);
	return HIBR_OK;
}

/* Open a POP3 session: connected, TLS by STLS when asked, logged in with
   USER and PASS. */
ml_conn *po_open(sh *s, ml_acct *a)
{
	ml_conn *c;
	str line, cmd;
	const char *v = hibr_get(s, "HIBR_MAIL_TIMEOUT");
	int tmo = v && atoi(v) > 0 ? atoi(v) : ML_TIMEOUT;

	c = ml_dial(a->host, a->port, a->sec, a->noverify, tmo);
	if (!c)
		return 0;
	s_init(&line);
	s_init(&cmd);
	if (!po_ok(c, &line)) {
		lg(HIBR_LERR, "email: %s did not greet as a POP3 server", a->host);
		goto fail;
	}
	if (a->sec == ML_STARTTLS && (!po_cmd(c, "STLS", &line) || ml_starttls(c) != HIBR_OK)) {
		lg(HIBR_LERR, "email: %s would not start TLS", a->host);
		goto fail;
	}
	s_cat(&cmd, "USER ");
	s_cat(&cmd, a->user);
	if (!po_cmd(c, cmd.p, &line))
		goto refused;
	ml_clr(&cmd);
	s_cat(&cmd, "PASS ");
	s_cat(&cmd, a->pass);
	if (!po_cmd(c, cmd.p, &line))
		goto refused;
	memset(cmd.p, 0, cmd.n);
	s_free(&cmd);
	s_free(&line);
	return c;
refused:
	lg(HIBR_LERR, "email: %s refused the login for %s: %s", a->host, a->user,
	   line.p ? line.p : "");
fail:
	if (cmd.p)
		memset(cmd.p, 0, cmd.n);
	s_free(&cmd);
	s_free(&line);
	ml_close(c);
	return 0;
}

/* The messages on the server: number, unique id and size, into $RET as
   r[uid]["n"] and ["size"] when bound, else printed. */
int po_list(sh *s, ml_conn *c)
{
	str line, body, sizes, sz;
	char *p, *e, *ks[2];
	int rc = HIBR_FAIL;

	s_init(&line);
	s_init(&body);
	s_init(&sizes);
	s_init(&sz);
	if (!po_cmd(c, "LIST", &line) || po_multi(c, &sizes) != HIBR_OK)
		goto done;
	if (!po_cmd(c, "UIDL", &line) || po_multi(c, &body) != HIBR_OK) {
		lg(HIBR_LERR, "email: %s has no UIDL; it cannot be kept offline", c->host);
		goto done;
	}
	for (p = body.p; p && *p; p = e) {
		char *sp, *num, *uid, *q;

		e = strstr(p, "\r\n");
		if (e)
			*e = 0, e += 2;
		sp = strchr(p, ' ');
		if (!sp)
			continue;
		*sp = 0;
		num = p;
		uid = sp + 1;
		ml_clr(&sz);
		for (q = sizes.p; q && *q;) {
			char *qe = strstr(q, "\r\n"), *qs = strchr(q, ' ');

			if (qs && (!qe || qs < qe) && (size_t)(qs - q) == strlen(num) &&
			    !strncmp(q, num, strlen(num))) {
				s_add(&sz, qs + 1, strcspn(qs + 1, "\r\n"));
				break;
			}
			q = qe ? qe + 2 : 0;
		}
		if (!sz.n)
			s_cat(&sz, "0");
		if (!s->bind) {
			printf("%s\t%s\t%s\n", num, uid, sz.p);
			continue;
		}
		ks[0] = uid;
		ks[1] = "n";
		hibr_setp(s, "RET", ks, 2, num);
		ks[1] = "size";
		hibr_setp(s, "RET", ks, 2, sz.p);
	}
	rc = HIBR_OK;
done:
	s_free(&line);
	s_free(&body);
	s_free(&sizes);
	s_free(&sz);
	return rc;
}

/* One message by its number, its whole text into o. */
int po_retr(ml_conn *c, const char *num, str *o)
{
	str line, cmd;
	int rc;

	s_init(&line);
	s_init(&cmd);
	s_cat(&cmd, "RETR ");
	s_cat(&cmd, num);
	rc = po_cmd(c, cmd.p, &line) && po_multi(c, o) == HIBR_OK ? HIBR_OK : HIBR_FAIL;
	s_free(&line);
	s_free(&cmd);
	return rc;
}

/* Delete a message by number; it goes when the session ends. */
int po_dele(ml_conn *c, const char *num)
{
	str line, cmd;
	int rc;

	s_init(&line);
	s_init(&cmd);
	s_cat(&cmd, "DELE ");
	s_cat(&cmd, num);
	rc = po_cmd(c, cmd.p, &line) ? HIBR_OK : HIBR_FAIL;
	s_free(&line);
	s_free(&cmd);
	return rc;
}

/* End a session: QUIT, which makes deletions happen. */
void po_close(ml_conn *c)
{
	str line;

	if (!c)
		return;
	s_init(&line);
	if (c->owner == getpid())
		po_cmd(c, "QUIT", &line);
	s_free(&line);
	ml_close(c);
}
