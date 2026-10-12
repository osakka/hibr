#define _GNU_SOURCE

#include "ml.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

vec ml_accts_v;

/* Where the accounts are kept: HIBR_MAIL_CONF, else the config folder. */
void ml_confpath(sh *s, str *o)
{
	const char *p = hibr_get(s, "HIBR_MAIL_CONF");

	ml_clr(o);
	if (p && *p) {
		s_cat(o, p);
		return;
	}
	p = hibr_get(s, "XDG_CONFIG_HOME");
	if (p && *p) {
		s_cat(o, p);
	} else {
		p = hibr_get(s, "HOME");
		s_cat(o, p ? p : "");
		s_cat(o, "/.config");
	}
	s_cat(o, "/hibr/mail");
}

/* Forget one account. */
void ml_acctfree(ml_acct *a)
{
	free(a->name);
	free(a->host);
	free(a->port);
	free(a->user);
	free(a->pass);
	free(a->email);
	free(a->full);
	free(a->shost);
	free(a->sport);
	free(a->aliases);
	free(a);
}

/* Forget every account read. */
void ml_conffree(void)
{
	size_t i;

	for (i = 0; i < ml_accts_v.n; i++)
		ml_acctfree(ml_accts_v.p[i]);
	v_free(&ml_accts_v);
}

/* The next tab-separated field of a line, or the rest of it. */
char *ml_field(char **p)
{
	char *f = *p, *t;

	if (!f)
		return xs("");
	t = strchr(f, '\t');
	if (t) {
		*t = 0;
		*p = t + 1;
	} else {
		*p = 0;
	}
	return xs(f);
}

/* A security setting by name. */
int ml_secof(const char *v)
{
	if (!strcmp(v, "starttls"))
		return ML_STARTTLS;
	if (!strcmp(v, "plain"))
		return ML_PLAIN;
	return ML_TLS;
}

/* A security setting's name. */
const char *ml_secname(int s)
{
	return s == ML_STARTTLS ? "starttls" : s == ML_PLAIN ? "plain" : "tls";
}

/* Read the accounts. A file anyone else can read is refused: it holds
   passwords. */
int ml_conf(sh *s)
{
	struct stat st;
	FILE *f;
	char *line = 0, *p, *x;
	size_t cap = 0;
	ssize_t n;
	ml_acct *a;
	str path;

	ml_conffree();
	s_init(&path);
	ml_confpath(s, &path);
	if (stat(path.p, &st) < 0) {
		s_free(&path);
		return HIBR_OK;
	}
	if (st.st_mode & 077) {
		lg(HIBR_LERR, "email: %s can be read by others; chmod 600 it", path.p);
		s_free(&path);
		return HIBR_FAIL;
	}
	f = fopen(path.p, "r");
	s_free(&path);
	if (!f)
		return HIBR_FAIL;
	while ((n = getline(&line, &cap, f)) > 0) {
		if (line[n - 1] == '\n')
			line[--n] = 0;
		if (!n || line[0] == '#')
			continue;
		p = line;
		a = xm(sizeof *a);
		memset(a, 0, sizeof *a);
		a->name = ml_field(&p);
		x = ml_field(&p);
		a->kind = !strcmp(x, "pop") ? ML_POP : ML_IMAP;
		free(x);
		a->host = ml_field(&p);
		a->port = ml_field(&p);
		x = ml_field(&p);
		a->sec = ml_secof(x);
		free(x);
		a->user = ml_field(&p);
		a->pass = ml_field(&p);
		a->email = ml_field(&p);
		a->full = ml_field(&p);
		a->shost = ml_field(&p);
		a->sport = ml_field(&p);
		x = ml_field(&p);
		a->ssec = ml_secof(x);
		free(x);
		x = ml_field(&p);
		a->noverify = !strcmp(x, "1");
		free(x);
		/* A fourteenth field, appended rather than inserted, so the two
		   directions both work: ml_field answers "" past the end of a
		   line an older build wrote, and an older build reads the
		   thirteen it knows and never looks at this one -- nothing here
		   counts the fields. */
		a->aliases = ml_field(&p);
		v_add(&ml_accts_v, a);
	}
	free(line);
	fclose(f);
	return HIBR_OK;
}

/* Write the accounts back, through a new file only its owner can read. */
int ml_confwrite(sh *s)
{
	str path, tmp, o;
	size_t i;
	int fd, rc = HIBR_OK;

	s_init(&path);
	s_init(&tmp);
	s_init(&o);
	ml_confpath(s, &path);
	{
		char *d = xs(path.p), *sl = strrchr(d, '/');

		if (sl) {
			*sl = 0;
			mkdir(d, 0700);
		}
		free(d);
	}
	s_cat(&o, "# hibr mail accounts: name kind host port security user password email name "
		  "smtp-host smtp-port smtp-security noverify aliases\n");
	for (i = 0; i < ml_accts_v.n; i++) {
		ml_acct *a = ml_accts_v.p[i];
		const char *f[14];
		int k;

		f[0] = a->name;
		f[1] = a->kind == ML_POP ? "pop" : "imap";
		f[2] = a->host;
		f[3] = a->port;
		f[4] = ml_secname(a->sec);
		f[5] = a->user;
		f[6] = a->pass;
		f[7] = a->email;
		f[8] = a->full;
		f[9] = a->shost;
		f[10] = a->sport;
		f[11] = ml_secname(a->ssec);
		f[12] = a->noverify ? "1" : "0";
		f[13] = a->aliases ? a->aliases : "";
		for (k = 0; k < 14; k++) {
			if (k)
				s_ch(&o, '\t');
			s_cat(&o, f[k] ? f[k] : "");
		}
		s_ch(&o, '\n');
	}
	s_cat(&tmp, path.p);
	s_cat(&tmp, ".new");
	fd = open(tmp.p, O_WRONLY | O_CREAT | O_TRUNC, 0600);
	if (fd < 0 || write(fd, o.p, o.n) != (ssize_t)o.n) {
		lg(HIBR_LERR, "email: cannot write %s: %s", tmp.p, strerror(errno));
		rc = HIBR_FAIL;
	}
	if (fd >= 0)
		close(fd);
	if (rc == HIBR_OK && rename(tmp.p, path.p) < 0) {
		lg(HIBR_LERR, "email: cannot replace %s: %s", path.p, strerror(errno));
		rc = HIBR_FAIL;
	}
	s_free(&path);
	s_free(&tmp);
	s_free(&o);
	return rc;
}

/* An account by name, or none, said why. */
ml_acct *ml_acctget(sh *s, const char *name)
{
	size_t i;

	if (ml_conf(s) != HIBR_OK)
		return 0;
	for (i = 0; i < ml_accts_v.n; i++)
		if (!strcmp(((ml_acct *)ml_accts_v.p[i])->name, name))
			return ml_accts_v.p[i];
	lg(HIBR_LERR, "email: %s: no such account", name);
	return 0;
}

/* Replace a field with a new value, freeing the old. */
void ml_setf(char **f, const char *v)
{
	free(*f);
	*f = xs(v);
}

/* Whether a value can go in the file: no tab, no line break. */
int ml_clean(const char *v)
{
	return !strpbrk(v, "\t\r\n");
}

/* mail account set NAME [-k imap|pop] [-h host] [-p port] [-S sec]
   [-u user] [-w password] [-e email] [-n name] [-o smtp-host]
   [-q smtp-port] [-T smtp-sec] [-K]: add an account or change one, what
   is not given kept; Gmail's and Outlook's servers filled in from the
   address when no host is given. */
int ml_acctset(sh *s, int ac, char **av)
{
	ml_acct *a = 0;
	size_t i;
	int k, isnew = 0;
	const char *name;

	if (ac < 4) {
		lg(HIBR_LERR, "usage: email account set NAME [-k imap|pop] [-h host] [-p port] "
			      "[-S tls|starttls|plain] [-u user] [-w password] [-e email] [-n name] "
			      "[-o smtp-host] [-q smtp-port] [-T tls|starttls|plain] [-K]");
		return 2;
	}
	name = av[3];
	if (!*name || !ml_clean(name) || strchr(name, '/')) {
		lg(HIBR_LERR, "email: %s: an account's name has no tab, line break or /", name);
		return 2;
	}
	if (ml_conf(s) != HIBR_OK)
		return HIBR_FAIL;
	for (i = 0; i < ml_accts_v.n; i++)
		if (!strcmp(((ml_acct *)ml_accts_v.p[i])->name, name))
			a = ml_accts_v.p[i];
	if (!a) {
		a = xm(sizeof *a);
		memset(a, 0, sizeof *a);
		a->name = xs(name);
		a->host = xs("");
		a->port = xs("");
		a->user = xs("");
		a->pass = xs("");
		a->email = xs("");
		a->full = xs("");
		a->shost = xs("");
		a->sport = xs("");
		a->aliases = xs("");
		v_add(&ml_accts_v, a);
		isnew = 1;
	}
	for (k = 4; k < ac; k++) {
		const char *o = av[k], *v = k + 1 < ac ? av[k + 1] : 0;

		if (!strcmp(o, "-K")) {
			a->noverify = 1;
			continue;
		}
		if (!v || o[0] != '-' || !o[1] || o[2]) {
			lg(HIBR_LERR, "email: account set: %s is not an option", o);
			return 2;
		}
		if (!ml_clean(v)) {
			lg(HIBR_LERR, "email: account set: a value has no tab or line break");
			return 2;
		}
		k++;
		switch (o[1]) {
		case 'k': a->kind = !strcmp(v, "pop") ? ML_POP : ML_IMAP; break;
		case 'h': ml_setf(&a->host, v); break;
		case 'p': ml_setf(&a->port, v); break;
		case 'S': a->sec = ml_secof(v); break;
		case 'a': ml_setf(&a->aliases, v); break;
		case 'u': ml_setf(&a->user, v); break;
		case 'w': ml_setf(&a->pass, v); break;
		case 'e': ml_setf(&a->email, v); break;
		case 'n': ml_setf(&a->full, v); break;
		case 'o': ml_setf(&a->shost, v); break;
		case 'q': ml_setf(&a->sport, v); break;
		case 'T': a->ssec = ml_secof(v); break;
		default:
			lg(HIBR_LERR, "email: account set: -%c is not an option", o[1]);
			return 2;
		}
	}
	if (!*a->host && *a->email) {
		const char *at = strchr(a->email, '@');

		if (at && (!strcasecmp(at, "@gmail.com") || !strcasecmp(at, "@googlemail.com"))) {
			ml_setf(&a->host, a->kind == ML_POP ? "pop.gmail.com" : "imap.gmail.com");
			if (!*a->shost)
				ml_setf(&a->shost, "smtp.gmail.com");
		} else if (at && (!strcasecmp(at, "@outlook.com") || !strcasecmp(at, "@hotmail.com") ||
				  !strcasecmp(at, "@live.com"))) {
			ml_setf(&a->host, a->kind == ML_POP ? "outlook.office365.com" :
						     "outlook.office365.com");
			if (!*a->shost) {
				ml_setf(&a->shost, "smtp-mail.outlook.com");
				a->ssec = ML_STARTTLS;
			}
		}
	}
	if (!*a->port)
		ml_setf(&a->port, a->kind == ML_POP ? (a->sec == ML_TLS ? "995" : "110") :
						  (a->sec == ML_TLS ? "993" : "143"));
	if (!*a->sport)
		ml_setf(&a->sport, a->ssec == ML_TLS ? "465" : "587");
	if (!*a->user && *a->email)
		ml_setf(&a->user, a->email);
	if (!*a->host) {
		lg(HIBR_LERR, "email: account %s: no server given, and none known for its address",
		   name);
		if (isnew) {
			ml_accts_v.n--;
			ml_acctfree(a);
		}
		return HIBR_FAIL;
	}
	if (ml_pairbad(name, "", a->port, a->sec) != HIBR_OK ||
	    ml_pairbad(name, "SMTP ", a->sport, a->ssec) != HIBR_OK) {
		if (isnew) {
			ml_accts_v.n--;
			ml_acctfree(a);
		}
		return HIBR_FAIL;
	}
	return ml_confwrite(s);
}

/* Whether a port and a security setting can work together at all. The
   standard ports are not a convention here, they are what the port *is*:
   993, 995 and 465 are TLS from the first byte, and 143, 110, 587 and 25
   are plain until STARTTLS upgrades them. So `-p 993 -S starttls` is the
   one pairing that can never connect -- the client waits for a greeting
   the server will not send until it has had a handshake, and the server
   waits for a handshake the client will not start. Caught here because
   this is the moment it is written down: by the time anything connects,
   the symptom is a timeout and four errors, and the cause is two fields
   nobody looks at (Gitea #187). An unusual port is nobody's business but
   the person's, so only the known ones are judged. */
int ml_pairbad(const char *name, const char *which, const char *port, int sec)
{
	static const char *imp[] = { "993", "995", "465", 0 };
	static const char *upg[] = { "143", "110", "587", "25", 0 };
	int i;

	for (i = 0; imp[i]; i++)
		if (!strcmp(port, imp[i]) && sec != ML_TLS) {
			lg(HIBR_LERR, "email: account %s: %sport %s is TLS from "
				      "the first byte, so -S %s cannot connect "
				      "there -- use -S tls, or a port that "
				      "upgrades (143, 110, 587, 25)",
			   name, which, port, ml_secname(sec));
			return HIBR_FAIL;
		}
	for (i = 0; upg[i]; i++)
		if (!strcmp(port, upg[i]) && sec == ML_TLS) {
			lg(HIBR_LERR, "email: account %s: %sport %s begins in "
				      "plain, so -S tls cannot connect there -- "
				      "use -S starttls, or a port that is TLS "
				      "throughout (993, 995, 465)",
			   name, which, port);
			return HIBR_FAIL;
		}
	return HIBR_OK;
}

/* mail account rm NAME: forget an account. */
int ml_acctrm(sh *s, const char *name)
{
	size_t i;

	if (ml_conf(s) != HIBR_OK)
		return HIBR_FAIL;
	for (i = 0; i < ml_accts_v.n; i++) {
		ml_acct *a = ml_accts_v.p[i];

		if (strcmp(a->name, name))
			continue;
		ml_acctfree(a);
		memmove(ml_accts_v.p + i, ml_accts_v.p + i + 1,
			(ml_accts_v.n - i - 1) * sizeof(void *));
		ml_accts_v.n--;
		return ml_confwrite(s);
	}
	lg(HIBR_LERR, "email: %s: no such account", name);
	return HIBR_FAIL;
}

/* mail account mv OLD NEW: an account renamed, its password and the rest
   kept; refused when NEW is taken. */
int ml_acctmv(sh *s, const char *old, const char *nw)
{
	ml_acct *a = 0;
	size_t i;

	if (!*nw || !ml_clean(nw) || strchr(nw, '/')) {
		lg(HIBR_LERR, "email: %s: an account's name has no tab, line break or /", nw);
		return 2;
	}
	if (ml_conf(s) != HIBR_OK)
		return HIBR_FAIL;
	for (i = 0; i < ml_accts_v.n; i++) {
		ml_acct *b = ml_accts_v.p[i];

		if (!strcmp(b->name, nw) && strcmp(old, nw)) {
			lg(HIBR_LERR, "email: %s: there is already an account of that name", nw);
			return HIBR_FAIL;
		}
		if (!strcmp(b->name, old))
			a = b;
	}
	if (!a) {
		lg(HIBR_LERR, "email: %s: no such account", old);
		return HIBR_FAIL;
	}
	free(a->name);
	a->name = xs(nw);
	lg(HIBR_LDBG, "email: account %s is now %s", old, nw);
	return ml_confwrite(s);
}

/* mail accounts: every account, never its password -- into $RET as
   r[name][field] when bound, else one a line. */
int ml_accts(sh *s)
{
	size_t i;
	char *ks[2];

	if (ml_conf(s) != HIBR_OK)
		return HIBR_FAIL;
	for (i = 0; i < ml_accts_v.n; i++) {
		ml_acct *a = ml_accts_v.p[i];
		const char *nm[] = { "kind", "host", "port", "security", "user", "email", "name",
				     "smtphost", "smtpport", "smtpsecurity", "haspass", "verify", 0 };
		const char *v[12];
		int k;
		char ix[24];

		v[0] = a->kind == ML_POP ? "pop" : "imap";
		v[1] = a->host;
		v[2] = a->port;
		v[3] = ml_secname(a->sec);
		v[4] = a->user;
		v[5] = a->email;
		v[6] = a->full;
		v[7] = a->shost;
		v[8] = a->sport;
		v[9] = ml_secname(a->ssec);
		v[10] = *a->pass ? "1" : "0";
		v[11] = a->noverify ? "0" : "1";
		if (!s->bind) {
			printf("%s\t%s\t%s:%s\t%s\t%s\n", a->name, v[0], a->host, a->port, a->user,
			       a->email);
			continue;
		}
		ks[0] = a->name;
		for (k = 0; nm[k]; k++) {
			ks[1] = (char *)nm[k];
			hibr_setp(s, "RET", ks, 2, v[k] ? v[k] : "");
		}
		/* Every address this account answers to, as an array and with
		   the main one first, so a caller asks the list rather than
		   splitting a string of its own -- the comma is the file's
		   business and nobody else's. */
		{
			const char *p = a->aliases ? a->aliases : "";
			char *ks3[3];
			int n = 0;

			ks3[0] = a->name;
			ks3[1] = (char *)"addresses";
			ks3[2] = ix;
			snprintf(ix, sizeof ix, "%d", n++);
			hibr_setp(s, "RET", ks3, 3, a->email);
			while (*p) {
				const char *e = strchr(p, ',');
				size_t len = e ? (size_t)(e - p) : strlen(p);
				str one;

				while (len && (*p == ' ' || *p == '\t')) { p++; len--; }
				while (len && (p[len - 1] == ' ' || p[len - 1] == '\t'))
					len--;
				if (len) {
					s_init(&one);
					s_add(&one, p, len);
					snprintf(ix, sizeof ix, "%d", n++);
					hibr_setp(s, "RET", ks3, 3, one.p ? one.p : "");
					s_free(&one);
				}
				if (!e)
					break;
				p = e + 1;
			}
		}
	}
	return HIBR_OK;
}
