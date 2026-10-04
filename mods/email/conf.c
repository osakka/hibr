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
		  "smtp-host smtp-port smtp-security noverify\n");
	for (i = 0; i < ml_accts_v.n; i++) {
		ml_acct *a = ml_accts_v.p[i];
		const char *f[13];
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
		for (k = 0; k < 13; k++) {
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
	return ml_confwrite(s);
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
	}
	return HIBR_OK;
}
