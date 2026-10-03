#define _GNU_SOURCE

#include "hibr.h"
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pwd.h>
#include <unistd.h>

/* auth -- check a password through PAM, loaded on first use the way the
   shell loads libssl: nothing linked, no PAM headers needed. It never
   needs privilege of its own: checking one's own password as oneself is
   what PAM's own helper (unix_chkpwd) is for. hibr is never setuid --
   docs/adr/0016. */

#ifndef AU_PAM
#define AU_PAM
#define AU_ECHO_OFF 1
#define AU_ECHO_ON 2
#define AU_ERROR 3
#define AU_INFO 4
#define AU_SUCCESS 0
#define AU_BUF_ERR 5
#define AU_CONV_ERR 19
#endif

/* PAM's own message, response and conversation: the same layout in
   Linux-PAM and OpenPAM. */
typedef struct au_msg au_msg;
struct au_msg {
	int style;
	const char *msg;
};

typedef struct au_resp au_resp;
struct au_resp {
	char *resp;
	int code;
};

typedef struct au_conv au_conv;
struct au_conv {
	int (*conv)(int, const au_msg **, au_resp **, void *);
	void *data;
};

/* What a conversation needs: who, the password, and PAM's last word. */
typedef struct au_talk au_talk;
struct au_talk {
	const char *user, *pass;
	str said;
};

/* The few PAM calls used. */
typedef struct au_api au_api;
struct au_api {
	int (*start)(const char *, const char *, const au_conv *, void **);
	int (*startdir)(const char *, const char *, const au_conv *, const char *,
			void **);
	int (*authn)(void *, int);
	int (*acct)(void *, int);
	int (*end)(void *, int);
	const char *(*err)(void *, int);
};

au_api au;
void *au_lib;
int au_tried;

/* Load libpam once. */
int au_load(void)
{
	static const char *names[] = { "libpam.so.0", "libpam.so", "libpam.2.dylib",
				       "/usr/lib/libpam.2.dylib", "libpam.dylib", 0 };
	int i;

	if (au_lib)
		return HIBR_OK;
	if (au_tried)
		return HIBR_FAIL;
	au_tried = 1;
	for (i = 0; names[i] && !au_lib; i++)
		au_lib = dlopen(names[i], RTLD_NOW | RTLD_LOCAL);
	if (!au_lib) {
		lg(HIBR_LERR, "auth: no PAM library to check a password with");
		return HIBR_FAIL;
	}
	au.start = dlsym(au_lib, "pam_start");
	au.startdir = dlsym(au_lib, "pam_start_confdir");
	au.authn = dlsym(au_lib, "pam_authenticate");
	au.acct = dlsym(au_lib, "pam_acct_mgmt");
	au.end = dlsym(au_lib, "pam_end");
	au.err = dlsym(au_lib, "pam_strerror");
	if (!au.start || !au.authn || !au.acct || !au.end || !au.err) {
		lg(HIBR_LERR, "auth: the PAM library lacks a call this needs");
		dlclose(au_lib);
		au_lib = 0;
		return HIBR_FAIL;
	}
	return HIBR_OK;
}

/* Wipe memory a password was in, where an optimiser cannot skip it. */
void au_wipe(char *p, size_t n)
{
	volatile char *v = p;

	while (n--)
		*v++ = 0;
}

/* PAM asks; the conversation answers -- the password to a hidden prompt,
   the user to a shown one -- and keeps what PAM says to show. Every
   answer is malloc'd, since PAM frees them. */
int au_talkfn(int n, const au_msg **m, au_resp **out, void *data)
{
	au_talk *t = data;
	au_resp *r;
	int i;

	if (n <= 0 || n > 64)
		return AU_CONV_ERR;
	r = calloc((size_t)n, sizeof *r);
	if (!r)
		return AU_BUF_ERR;
	for (i = 0; i < n; i++) {
		switch (m[i]->style) {
		case AU_ECHO_OFF:
			r[i].resp = strdup(t->pass);
			break;
		case AU_ECHO_ON:
			r[i].resp = strdup(t->user);
			break;
		case AU_ERROR:
		case AU_INFO:
			if (m[i]->msg) {
				t->said.n = 0;
				s_cat(&t->said, m[i]->msg);
			}
			break;
		}
	}
	*out = r;
	return AU_SUCCESS;
}

/* The PAM service to ask when none is named: hibr's own when installed,
   else the system's login (macOS: screensaver). */
const char *au_service(void)
{
	if (access("/etc/pam.d/hibr", R_OK) == 0)
		return "hibr";
#ifdef __APPLE__
	if (access("/etc/pam.d/screensaver", R_OK) == 0)
		return "screensaver";
#endif
	return "login";
}

/* auth check [-s service] [-c confdir] [-q] user password: whether PAM
   takes the password for the user -- authenticated, and the account
   allowed. Status 0 yes, 1 no (PAM's reason in $RET, and said unless -q),
   2 when PAM cannot be asked at all. The password is wiped from the
   argument once read. */
int au_check(sh *s, int ac, char **av)
{
	const char *svc = 0, *dir = 0, *user;
	char *pass;
	int i = 2, quiet = 0, r;
	void *h = 0;
	au_conv conv;
	au_talk t;
	size_t pn;

	while (i < ac && av[i][0] == '-' && av[i][1]) {
		if (!strcmp(av[i], "-s") && i + 1 < ac)
			svc = av[++i];
		else if (!strcmp(av[i], "-c") && i + 1 < ac)
			dir = av[++i];
		else if (!strcmp(av[i], "-q"))
			quiet = 1;
		else
			break;
		i++;
	}
	if (i + 2 != ac) {
		lg(HIBR_LERR, "usage: auth check [-s service] [-c confdir] [-q] user "
			      "password");
		return 2;
	}
	user = av[i];
	pn = strlen(av[i + 1]);
	pass = xm(pn + 1);
	memcpy(pass, av[i + 1], pn + 1);
	au_wipe(av[i + 1], pn);
	if (au_load() != HIBR_OK) {
		au_wipe(pass, pn);
		free(pass);
		return 2;
	}
	if (!svc)
		svc = au_service();
	memset(&t, 0, sizeof t);
	t.user = user;
	t.pass = pass;
	s_init(&t.said);
	conv.conv = au_talkfn;
	conv.data = &t;
	if (dir) {
		if (!au.startdir) {
			lg(HIBR_LERR, "auth: this PAM cannot be given a folder of its own");
			au_wipe(pass, pn);
			free(pass);
			return 2;
		}
		r = au.startdir(svc, user, &conv, dir, &h);
	} else {
		r = au.start(svc, user, &conv, &h);
	}
	if (r == AU_SUCCESS)
		r = au.authn(h, 0);
	if (r == AU_SUCCESS)
		r = au.acct(h, 0);
	if (r != AU_SUCCESS) {
		const char *why = t.said.n ? t.said.p : h ? au.err(h, r) : "PAM failed";

		hibr_ret(s, why);
		if (!quiet)
			lg(HIBR_LERR, "auth: %s: %s", user, why);
	} else {
		hibr_ret(s, "");
	}
	if (h)
		au.end(h, r);
	au_wipe(pass, pn);
	free(pass);
	s_free(&t.said);
	return r == AU_SUCCESS ? HIBR_OK : HIBR_FAIL;
}

/* auth service: the PAM service a check asks when none is named. */
int au_svc(sh *s)
{
	hibr_ret(s, au_service());
	if (!s->bind)
		printf("%s\n", au_service());
	return HIBR_OK;
}

/* auth whoami: who this is -- the login name, a tab, and the real name
   the account gives (the first part of its GECOS field), the login name
   again when it gives none. */
int au_whoami(sh *s)
{
	struct passwd *pw = getpwuid(getuid());
	str o;
	const char *g;

	if (!pw) {
		lg(HIBR_LERR, "auth: this user has no account entry");
		return HIBR_FAIL;
	}
	s_init(&o);
	s_cat(&o, pw->pw_name);
	s_ch(&o, '\t');
	g = pw->pw_gecos ? pw->pw_gecos : "";
	if (*g && *g != ',')
		s_add(&o, g, strcspn(g, ","));
	else
		s_cat(&o, pw->pw_name);
	hibr_ret(s, o.p);
	if (!s->bind)
		printf("%s\n", o.p);
	s_free(&o);
	return HIBR_OK;
}

/* auth: check a password through PAM. */
int m_auth(sh *s, int ac, char **av)
{
	if (ac > 1 && !strcmp(av[1], "check"))
		return au_check(s, ac, av);
	if (ac > 1 && !strcmp(av[1], "service"))
		return au_svc(s);
	if (ac > 1 && !strcmp(av[1], "ready"))
		return au_load();
	if (ac > 1 && !strcmp(av[1], "whoami"))
		return au_whoami(s);
	lg(HIBR_LERR, "usage: auth check [-s service] [-c confdir] [-q] user password"
		      " | service | ready | whoami");
	return 2;
}

/* Nothing to set up until a password is checked. */
int au_ini(sh *s)
{
	(void)s;
	return HIBR_OK;
}

/* Let PAM go when the module does. */
void au_fini(sh *s)
{
	(void)s;
	if (au_lib)
		dlclose(au_lib);
	au_lib = 0;
	au_tried = 0;
}

const hibr_bi auth_bi[] = {
	{ "auth", m_auth, "check a password through PAM" },
	HIBR_BI_END
};

HIBR_MODULE("auth", HIBR_VER, "password checks through PAM", auth_bi, au_ini,
	    au_fini);
