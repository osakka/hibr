#define _GNU_SOURCE

#include "ml.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

/* An SMTP reply, every continuation line read: its code, the text kept. */
int sm_reply(ml_conn *c, str *text)
{
	str line;
	int code = 0;

	s_init(&line);
	ml_clr(text);
	for (;;) {
		if (ml_line(c, &line) != HIBR_OK) {
			s_free(&line);
			return -1;
		}
		code = atoi(line.p);
		if (text->n)
			s_ch(text, '\n');
		s_cat(text, line.n > 4 ? line.p + 4 : "");
		if (line.n < 4 || line.p[3] != '-')
			break;
	}
	if (!text->p)
		s_cat(text, "");
	s_free(&line);
	return code;
}

/* Send an SMTP command and read its reply's code. */
int sm_cmd(ml_conn *c, const char *cmd, str *text)
{
	str o;
	int code;

	s_init(&o);
	s_cat(&o, cmd);
	s_cat(&o, "\r\n");
	code = ml_wall(c, o.p, o.n) == HIBR_OK ? sm_reply(c, text) : -1;
	memset(o.p, 0, o.n);
	s_free(&o);
	return code;
}

/* Every address in an address header -- a comma-separated list of
   "Name <a@b>" and bare a@b, quoted names allowed to hold commas. */
void sm_addrs(const char *h, vec *out)
{
	const char *p = h;
	int q = 0, ang = 0;
	str cur;

	s_init(&cur);
	for (;; p++) {
		if (*p == '"' && (p == h || p[-1] != '\\'))
			q = !q;
		if (!q && *p == '<')
			ang = 1;
		if (!q && *p == '>')
			ang = 0;
		if (*p && (q || ang || *p != ',')) {
			s_ch(&cur, *p);
			continue;
		}
		if (cur.n) {
			const char *lt = strrchr(cur.p, '<'), *gt;
			str a;

			s_init(&a);
			if (lt && (gt = strchr(lt, '>'))) {
				s_add(&a, lt + 1, (size_t)(gt - lt - 1));
			} else {
				const char *b = cur.p, *e = cur.p + cur.n;

				while (b < e && isspace((unsigned char)*b))
					b++;
				while (e > b && isspace((unsigned char)e[-1]))
					e--;
				s_add(&a, b, (size_t)(e - b));
			}
			if (a.n && strchr(a.p, '@'))
				v_add(out, a.p);
			else
				s_free(&a);
		}
		ml_clr(&cur);
		if (!*p)
			break;
	}
	s_free(&cur);
}

/* Send a message through an account's SMTP server: to the addresses given,
   or else to everyone in its To, Cc and Bcc; the Bcc header itself never
   sent. TLS from the start or by STARTTLS, logged in with AUTH PLAIN, or
   AUTH LOGIN where that is all the server offers. */
int sm_send(sh *s, ml_acct *a, const char *raw, size_t n, int ac, char **rcpt)
{
	ml_conn *c = 0;
	str text, cmd, b, from, body;
	vec to = { 0, 0, 0 };
	const char *v = hibr_get(s, "HIBR_MAIL_TIMEOUT");
	int tmo = v && atoi(v) > 0 ? atoi(v) : ML_TIMEOUT, code, rc = HIBR_FAIL, i;
	size_t k;
	const char *host = hibr_get(s, "HOSTNAME");

	if (!*a->shost) {
		lg(HIBR_LERR, "email: account %s has no outgoing server", a->name);
		return HIBR_FAIL;
	}
	s_init(&text);
	s_init(&cmd);
	s_init(&b);
	s_init(&from);
	s_init(&body);
	if (ac) {
		for (i = 0; i < ac; i++)
			v_add(&to, xs(rcpt[i]));
	} else {
		ml_hraw(raw, n, "To", &b);
		sm_addrs(b.p ? b.p : "", &to);
		ml_hraw(raw, n, "Cc", &b);
		sm_addrs(b.p ? b.p : "", &to);
		ml_hraw(raw, n, "Bcc", &b);
		sm_addrs(b.p ? b.p : "", &to);
	}
	ml_hraw(raw, n, "From", &b);
	{
		vec f = { 0, 0, 0 };

		sm_addrs(b.p ? b.p : "", &f);
		s_cat(&from, f.n ? (char *)f.p[0] : a->email);
		for (k = 0; k < f.n; k++)
			free(f.p[k]);
		v_free(&f);
	}
	if (!to.n) {
		lg(HIBR_LERR, "email: the message has nobody to go to");
		goto out;
	}
	c = ml_dial(a->shost, a->sport, a->ssec, a->noverify, tmo);
	if (!c)
		goto out;
	if ((code = sm_reply(c, &text)) != 220) {
		lg(HIBR_LERR, "email: %s did not greet as an SMTP server: %d %s", a->shost, code, text.p);
		goto close;
	}
	s_cat(&cmd, "EHLO ");
	s_cat(&cmd, host && *host && !strpbrk(host, " \t\r\n") ? host : "localhost");
	if (sm_cmd(c, cmd.p, &text) != 250)
		goto said;
	if (a->ssec == ML_STARTTLS) {
		if (sm_cmd(c, "STARTTLS", &text) != 220 || ml_starttls(c) != HIBR_OK) {
			lg(HIBR_LERR, "email: %s would not start TLS", a->shost);
			goto close;
		}
		if (sm_cmd(c, cmd.p, &text) != 250)
			goto said;
	}
	if (*a->pass) {
		if (strcasestr(text.p, "PLAIN") || !strcasestr(text.p, "LOGIN")) {
			str sasl;

			s_init(&sasl);
			s_ch(&sasl, 0);
			s_cat(&sasl, a->user);
			s_ch(&sasl, 0);
			s_cat(&sasl, a->pass);
			ml_clr(&b);
			ml_b64enc(&b, sasl.p, sasl.n);
			while (b.n && (b.p[b.n - 1] == '\n' || b.p[b.n - 1] == '\r'))
				b.p[--b.n] = 0;
			memset(sasl.p, 0, sasl.n);
			s_free(&sasl);
			ml_clr(&cmd);
			s_cat(&cmd, "AUTH PLAIN ");
			s_cat(&cmd, b.p);
			code = sm_cmd(c, cmd.p, &text);
		} else {
			code = sm_cmd(c, "AUTH LOGIN", &text);
			if (code == 334) {
				ml_clr(&b);
				ml_b64enc(&b, a->user, strlen(a->user));
				while (b.n && (b.p[b.n - 1] == '\n' || b.p[b.n - 1] == '\r'))
					b.p[--b.n] = 0;
				code = sm_cmd(c, b.p, &text);
			}
			if (code == 334) {
				ml_clr(&b);
				ml_b64enc(&b, a->pass, strlen(a->pass));
				while (b.n && (b.p[b.n - 1] == '\n' || b.p[b.n - 1] == '\r'))
					b.p[--b.n] = 0;
				code = sm_cmd(c, b.p, &text);
			}
		}
		if (b.p)
			memset(b.p, 0, b.n);
		if (cmd.p)
			memset(cmd.p, 0, cmd.n);
		if (code != 235) {
			lg(HIBR_LERR, "email: %s refused the login for %s: %d %s", a->shost, a->user, code,
			   text.p);
			goto close;
		}
	}
	ml_clr(&cmd);
	s_cat(&cmd, "MAIL FROM:<");
	s_cat(&cmd, from.p);
	s_ch(&cmd, '>');
	if (sm_cmd(c, cmd.p, &text) != 250)
		goto said;
	for (k = 0; k < to.n; k++) {
		ml_clr(&cmd);
		s_cat(&cmd, "RCPT TO:<");
		s_cat(&cmd, to.p[k]);
		s_ch(&cmd, '>');
		code = sm_cmd(c, cmd.p, &text);
		if (code != 250 && code != 251)
			goto said;
	}
	if (sm_cmd(c, "DATA", &text) != 354)
		goto said;
	{
		size_t i0 = 0, e;
		int inhead = 1, skip = 0;

		while (i0 < n) {
			e = i0;
			while (e < n && raw[e] != '\n')
				e++;
			{
				size_t le = e > i0 && raw[e - 1] == '\r' ? e - 1 : e;

				if (inhead && le == i0)
					inhead = 0;
				if (inhead) {
					if (raw[i0] != ' ' && raw[i0] != '\t')
						skip = le - i0 >= 4 && !strncasecmp(raw + i0, "Bcc:", 4);
				} else {
					skip = 0;
				}
				if (!skip) {
					if (raw[i0] == '.')
						s_ch(&body, '.');
					s_add(&body, raw + i0, le - i0);
					s_cat(&body, "\r\n");
				}
			}
			i0 = e + 1;
		}
		s_cat(&body, ".\r\n");
	}
	if (ml_wall(c, body.p, body.n) != HIBR_OK || sm_reply(c, &text) != 250)
		goto said;
	sm_cmd(c, "QUIT", &text);
	rc = HIBR_OK;
	goto close;
said:
	lg(HIBR_LERR, "email: %s: %s", a->shost, text.p ? text.p : "no reply");
close:
	ml_close(c);
out:
	for (k = 0; k < to.n; k++)
		free(to.p[k]);
	v_free(&to);
	s_free(&text);
	s_free(&cmd);
	s_free(&b);
	s_free(&from);
	s_free(&body);
	return rc;
}
