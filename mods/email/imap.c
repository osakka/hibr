#define _GNU_SOURCE

#include "ml.h"
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#ifndef IM_DEPTH
#define IM_DEPTH 64
#endif

/* Free a parsed IMAP value. */
void iv_free(ml_iv *v)
{
	size_t i;

	if (!v)
		return;
	for (i = 0; i < v->kids.n; i++)
		iv_free(v->kids.p[i]);
	v_free(&v->kids);
	s_free(&v->s);
	free(v);
}

/* A new value of a kind: 'a' atom, 's' string, 'n' NIL, 'l' list. */
ml_iv *iv_new(int t)
{
	ml_iv *v = xm(sizeof *v);

	memset(v, 0, sizeof *v);
	v->t = t;
	s_init(&v->s);
	return v;
}

/* Parse one value at i, nested no deeper than IM_DEPTH. */
ml_iv *im_val(const char *p, size_t n, size_t *i, int dep)
{
	ml_iv *v, *k;
	size_t j;

	while (*i < n && (p[*i] == ' ' || p[*i] == '\r' || p[*i] == '\n'))
		(*i)++;
	if (*i >= n || p[*i] == ')')
		return 0;
	if (p[*i] == '(') {
		(*i)++;
		v = iv_new('l');
		for (;;) {
			while (*i < n && (p[*i] == ' ' || p[*i] == '\r' || p[*i] == '\n'))
				(*i)++;
			if (*i >= n)
				return v;
			if (p[*i] == ')') {
				(*i)++;
				return v;
			}
			if (dep >= IM_DEPTH) {
				*i = n;
				return v;
			}
			k = im_val(p, n, i, dep + 1);
			if (!k)
				return v;
			v_add(&v->kids, k);
		}
	}
	if (p[*i] == '"') {
		v = iv_new('s');
		(*i)++;
		while (*i < n && p[*i] != '"') {
			if (p[*i] == '\\' && *i + 1 < n)
				(*i)++;
			s_ch(&v->s, p[*i]);
			(*i)++;
		}
		if (*i < n)
			(*i)++;
		if (!v->s.p)
			s_cat(&v->s, "");
		return v;
	}
	if (p[*i] == '{') {
		size_t len = strtoul(p + *i + 1, 0, 10);

		v = iv_new('s');
		while (*i < n && p[*i] != '\n')
			(*i)++;
		(*i)++;
		if (*i + len > n)
			len = *i < n ? n - *i : 0;
		s_add(&v->s, p + *i, len);
		*i += len;
		if (!v->s.p)
			s_cat(&v->s, "");
		return v;
	}
	v = iv_new('a');
	j = *i;
	while (j < n && p[j] != ' ' && p[j] != ')' && p[j] != '(' && p[j] != '\r' && p[j] != '\n') {
		if (p[j] == '[') {
			int d = 1;

			j++;
			while (j < n && d) {
				if (p[j] == '[')
					d++;
				else if (p[j] == ']')
					d--;
				j++;
			}
			continue;
		}
		j++;
	}
	if (j == *i) {
		iv_free(v);
		return 0;
	}
	s_add(&v->s, p + *i, j - *i);
	*i = j;
	if (!strcasecmp(v->s.p, "NIL"))
		v->t = 'n';
	return v;
}

/* Parse every value of a response line into a list. */
ml_iv *im_parse(const char *p, size_t n, size_t *i)
{
	ml_iv *all = iv_new('l'), *v;

	while (*i < n && (v = im_val(p, n, i, 0)))
		v_add(&all->kids, v);
	return all;
}

/* The value after a key in a FETCH list -- FLAGS, UID, X-GM-LABELS, or a
   BODY[...] section, matched on its start -- or none. */
ml_iv *iv_get(ml_iv *list, const char *key)
{
	size_t i, kl = strlen(key);

	if (!list || list->t != 'l')
		return 0;
	for (i = 0; i + 1 < list->kids.n; i += 2) {
		ml_iv *k = list->kids.p[i];

		if (k->t == 'a' && k->s.p && !strncasecmp(k->s.p, key, kl) &&
		    (key[kl - 1] == '[' || !k->s.p[kl]))
			return list->kids.p[i + 1];
	}
	return 0;
}

/* Write a string as IMAP wants it: quoted with its quotes and backslashes
   escaped. */
void im_quote(str *o, const char *s)
{
	s_ch(o, '"');
	for (; *s; s++) {
		if (*s == '"' || *s == '\\')
			s_ch(o, '\\');
		s_ch(o, *s);
	}
	s_ch(o, '"');
}

/* Read one whole response: a line, and every literal it announces with
   the lines that follow each. */
int im_resp(ml_imap *m, str *raw)
{
	str line;
	size_t n;

	ml_clr(raw);
	s_init(&line);
	for (;;) {
		if (ml_line(m->c, &line) != HIBR_OK) {
			s_free(&line);
			return HIBR_FAIL;
		}
		s_add(raw, line.p, line.n);
		s_cat(raw, "\r\n");
		if (line.n > 2 && line.p[line.n - 1] == '}') {
			char *b = strrchr(line.p, '{');

			if (b) {
				n = strtoul(b + 1, 0, 10);
				if (n > (size_t)ML_MAXMSG) {
					lg(HIBR_LERR, "email: %s offered %lu bytes; too many", m->c->host,
					   (unsigned long)n);
					s_free(&line);
					return HIBR_FAIL;
				}
				if (ml_bytes(m->c, n, raw) != HIBR_OK) {
					s_free(&line);
					return HIBR_FAIL;
				}
				continue;
			}
		}
		break;
	}
	s_free(&line);
	return HIBR_OK;
}

/* Forget the untagged replies of the last command. */
void im_clear(ml_imap *m)
{
	size_t i;

	for (i = 0; i < m->untagged.n; i++)
		free(m->untagged.p[i]);
	m->untagged.n = 0;
}

/* Run one command: tagged, sent, and every reply read until its own tag
   comes back; the untagged ones kept for the caller. A literal in the
   command is sent when the server says to go on. Whether it said OK, its
   words in status either way. */
int im_cmd(ml_imap *m, const char *cmd, str *lit)
{
	str out, raw, tag;
	int rc = HIBR_FAIL;

	im_clear(m);
	s_init(&out);
	s_init(&raw);
	s_init(&tag);
	s_ch(&tag, 'T');
	s_num(&tag, ++m->tag);
	s_cat(&out, tag.p);
	s_ch(&out, ' ');
	s_cat(&out, cmd);
	if (lit) {
		s_ch(&out, '{');
		s_num(&out, (long)lit->n);
		s_ch(&out, '}');
	}
	s_cat(&out, "\r\n");
	if (ml_wall(m->c, out.p, out.n) != HIBR_OK)
		goto done;
	if (lit) {
		if (im_resp(m, &raw) != HIBR_OK)
			goto done;
		if (raw.p[0] != '+') {
			ml_clr(&m->status);
			s_add(&m->status, raw.p, raw.n > 2 ? raw.n - 2 : raw.n);
			goto done;
		}
		if (ml_wall(m->c, lit->p, lit->n) != HIBR_OK || ml_wall(m->c, "\r\n", 2) != HIBR_OK)
			goto done;
	}
	for (;;) {
		if (im_resp(m, &raw) != HIBR_OK)
			goto done;
		if (raw.n > tag.n && !strncmp(raw.p, tag.p, tag.n) && raw.p[tag.n] == ' ') {
			const char *st = raw.p + tag.n + 1;

			ml_clr(&m->status);
			s_add(&m->status, st, raw.n - tag.n - 3);
			rc = !strncasecmp(st, "OK", 2) ? HIBR_OK : HIBR_FAIL;
			break;
		}
		if (raw.p[0] == '*')
			v_add(&m->untagged, xs(raw.p));
	}
done:
	s_free(&out);
	s_free(&raw);
	s_free(&tag);
	return rc;
}

/* Read what the server can do from a CAPABILITY reply, untagged or in a
   response code. */
void im_caps(ml_imap *m)
{
	size_t i;
	const char *c;

	for (i = 0; i < m->untagged.n; i++) {
		c = m->untagged.p[i];
		if (!strncasecmp(c, "* CAPABILITY ", 13)) {
			ml_clr(&m->caps);
			s_cat(&m->caps, c + 13);
		}
	}
	c = m->status.p ? strcasestr(m->status.p, "[CAPABILITY ") : 0;
	if (c) {
		const char *e = strchr(c, ']');

		ml_clr(&m->caps);
		s_add(&m->caps, c + 12, e ? (size_t)(e - c - 12) : strlen(c + 12));
	}
	c = m->caps.p ? m->caps.p : "";
	m->gmail = strcasestr(c, "X-GM-EXT-1") != 0;
	m->move = strcasestr(c, " MOVE") != 0 || !strncasecmp(c, "MOVE", 4);
	m->idle = strcasestr(c, "IDLE") != 0;
	m->uidplus = strcasestr(c, "UIDPLUS") != 0;
	m->condstore = strcasestr(c, "CONDSTORE") != 0;
	m->special = strcasestr(c, "SPECIAL-USE") != 0;
}

/* Close a session: LOGOUT said, the connection closed. */
void im_close(ml_imap *m)
{
	if (!m)
		return;
	if (m->c && m->c->owner == getpid())
		im_cmd(m, "LOGOUT", 0);
	ml_close(m->c);
	im_clear(m);
	v_free(&m->untagged);
	s_free(&m->caps);
	s_free(&m->status);
	free(m->folder);
	free(m);
}

/* Open an IMAP session for an account: connected, upgraded to TLS when
   it says starttls, and logged in -- SASL PLAIN in one round trip where
   the server allows it, LOGIN otherwise. */
ml_imap *im_open(sh *s, ml_acct *a)
{
	ml_imap *m;
	str raw, cmd, sasl, b;
	const char *v = hibr_get(s, "HIBR_MAIL_TIMEOUT");
	int tmo = v && atoi(v) > 0 ? atoi(v) : ML_TIMEOUT;

	m = xm(sizeof *m);
	memset(m, 0, sizeof *m);
	s_init(&m->caps);
	s_init(&m->status);
	m->c = ml_dial(a->host, a->port, a->sec, a->noverify, tmo);
	if (!m->c) {
		free(m);
		return 0;
	}
	s_init(&raw);
	if (im_resp(m, &raw) != HIBR_OK || strncasecmp(raw.p, "* OK", 4)) {
		if (raw.p && !strncasecmp(raw.p, "* PREAUTH", 9))
			goto authed;
		lg(HIBR_LERR, "email: %s did not greet as an IMAP server: %.80s", a->host,
		   raw.p ? raw.p : "(nothing)");
		s_free(&raw);
		im_close(m);
		return 0;
	}
	s_free(&raw);
	if (a->sec == ML_STARTTLS) {
		if (im_cmd(m, "STARTTLS", 0) != HIBR_OK || ml_starttls(m->c) != HIBR_OK) {
			lg(HIBR_LERR, "email: %s would not start TLS", a->host);
			im_close(m);
			return 0;
		}
	}
	if (im_cmd(m, "CAPABILITY", 0) == HIBR_OK)
		im_caps(m);
	s_init(&cmd);
	if (strcasestr(m->caps.p ? m->caps.p : "", "AUTH=PLAIN") &&
	    strcasestr(m->caps.p ? m->caps.p : "", "SASL-IR")) {
		s_init(&sasl);
		s_init(&b);
		s_ch(&sasl, 0);
		s_cat(&sasl, a->user);
		s_ch(&sasl, 0);
		s_cat(&sasl, a->pass);
		ml_b64enc(&b, sasl.p, sasl.n);
		while (b.n && (b.p[b.n - 1] == '\n' || b.p[b.n - 1] == '\r'))
			b.p[--b.n] = 0;
		s_cat(&cmd, "AUTHENTICATE PLAIN ");
		s_cat(&cmd, b.p ? b.p : "");
		memset(sasl.p, 0, sasl.n);
		s_free(&sasl);
		s_free(&b);
	} else {
		s_cat(&cmd, "LOGIN ");
		im_quote(&cmd, a->user);
		s_ch(&cmd, ' ');
		im_quote(&cmd, a->pass);
	}
	if (im_cmd(m, cmd.p, 0) != HIBR_OK) {
		lg(HIBR_LERR, "email: %s refused the login for %s: %s", a->host, a->user,
		   m->status.p ? m->status.p : "");
		memset(cmd.p, 0, cmd.n);
		s_free(&cmd);
		im_close(m);
		return 0;
	}
	memset(cmd.p, 0, cmd.n);
	s_free(&cmd);
authed:
	if (im_cmd(m, "CAPABILITY", 0) == HIBR_OK)
		im_caps(m);
	lg(HIBR_LDBG, "email: logged in to %s%s", a->host, m->gmail ? " (Gmail)" : "");
	return m;
}
