#define _GNU_SOURCE

#include "hibr.h"
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <grp.h>
#include <pwd.h>
#include <signal.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#ifdef __linux__
#include <utmpx.h>
#endif

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
#define AU_NEWTOK 12
#define AU_TTY 3
#ifdef __APPLE__
#define AU_ESTCRED 0x1
#define AU_DELCRED 0x2
#else
#define AU_ESTCRED 0x2
#define AU_DELCRED 0x4
#endif
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
	int (*setitem)(void *, int, const void *);
	int (*setcred)(void *, int);
	int (*opens)(void *, int);
	int (*closes)(void *, int);
	char **(*envlist)(void *);
};

/* The one login session a module holds open between `auth open` and
   `auth close`: PAM's handle, the conversation it keeps a pointer to, and
   who it is for. */
typedef struct au_sess au_sess;
struct au_sess {
	void *h;
	au_talk t;
	char *user, *home, *shell;
	uid_t uid;
	gid_t gid;
	int cred, open;
};

au_sess au_s;
extern char **environ;

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
	au.setitem = dlsym(au_lib, "pam_set_item");
	au.setcred = dlsym(au_lib, "pam_setcred");
	au.opens = dlsym(au_lib, "pam_open_session");
	au.closes = dlsym(au_lib, "pam_close_session");
	au.envlist = dlsym(au_lib, "pam_getenvlist");
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

/* Say why PAM refused, into $RET and, unless quiet, the log. */
void au_why(sh *s, void *h, au_talk *t, int r, const char *user, int quiet)
{
	const char *why = t->said.n ? t->said.p : h ? au.err(h, r) : "PAM failed";

	if (r == AU_NEWTOK)
		why = "the password has expired; change it at a text login";
	hibr_ret(s, why);
	if (!quiet)
		lg(HIBR_LERR, "auth: %s: %s", user, why);
}

/* Let go of the session's PAM handle and everything kept with it. */
void au_sfree(void)
{
	if (au_s.h) {
		if (au_s.open)
			au.closes(au_s.h, 0);
		if (au_s.cred)
			au.setcred(au_s.h, AU_DELCRED);
		au.end(au_s.h, 0);
	}
	if (au_s.t.pass)
		au_wipe((char *)au_s.t.pass, strlen(au_s.t.pass));
	free((char *)au_s.t.pass);
	s_free(&au_s.t.said);
	free(au_s.user);
	free(au_s.home);
	free(au_s.shell);
	memset(&au_s, 0, sizeof au_s);
}

/* auth open [-s service] [-c confdir] [-q] user password: a login -- the
   password checked, the account allowed, credentials set and a session
   opened on this terminal -- kept open for `auth run` and `auth close`.
   Root is refused: a login screen is for people. Status as for check. */
int au_open(sh *s, int ac, char **av)
{
	const char *svc = 0, *dir = 0;
	char *pass, *tty;
	int i = 2, quiet = 0, r;
	struct passwd *pw;
	au_conv conv;
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
		lg(HIBR_LERR, "usage: auth open [-s service] [-c confdir] [-q] user "
			      "password");
		return 2;
	}
	pn = strlen(av[i + 1]);
	pass = xm(pn + 1);
	memcpy(pass, av[i + 1], pn + 1);
	au_wipe(av[i + 1], pn);
	if (au_s.h) {
		lg(HIBR_LERR, "auth: a session is already open; auth close first");
		au_wipe(pass, pn);
		free(pass);
		return 2;
	}
	if (au_load() != HIBR_OK || !au.setitem || !au.setcred || !au.opens ||
	    !au.closes || !au.envlist || (dir && !au.startdir)) {
		if (au_lib)
			lg(HIBR_LERR, "auth: this PAM cannot open a session here");
		au_wipe(pass, pn);
		free(pass);
		return 2;
	}
	pw = getpwnam(av[i]);
	if (!pw || pw->pw_uid == 0) {
		hibr_ret(s, pw ? "root cannot log in here" : "Authentication failure");
		if (!quiet)
			lg(HIBR_LERR, "auth: %s: %s", av[i],
			   pw ? "root cannot log in here" : "no such user");
		au_wipe(pass, pn);
		free(pass);
		return HIBR_FAIL;
	}
	memset(&au_s, 0, sizeof au_s);
	au_s.user = xs(pw->pw_name);
	au_s.home = xs(pw->pw_dir && *pw->pw_dir ? pw->pw_dir : "/");
	au_s.shell = xs(pw->pw_shell && *pw->pw_shell ? pw->pw_shell : "/bin/sh");
	au_s.uid = pw->pw_uid;
	au_s.gid = pw->pw_gid;
	au_s.t.user = au_s.user;
	au_s.t.pass = pass;
	s_init(&au_s.t.said);
	conv.conv = au_talkfn;
	conv.data = &au_s.t;
	if (!svc)
		svc = au_service();
	if (dir)
		r = au.startdir(svc, au_s.user, &conv, dir, &au_s.h);
	else
		r = au.start(svc, au_s.user, &conv, &au_s.h);
	tty = isatty(0) ? ttyname(0) : 0;
	if (r == AU_SUCCESS && tty)
		r = au.setitem(au_s.h, AU_TTY, tty);
	if (r == AU_SUCCESS)
		r = au.authn(au_s.h, 0);
	if (r == AU_SUCCESS)
		r = au.acct(au_s.h, 0);
	au_wipe(pass, pn);
	if (r == AU_SUCCESS && geteuid() == 0 &&
	    initgroups(au_s.user, au_s.gid) != 0)
		lg(HIBR_LDBG, "initgroups before setcred failed: %s", strerror(errno));
	if (r == AU_SUCCESS) {
		r = au.setcred(au_s.h, AU_ESTCRED);
		au_s.cred = r == AU_SUCCESS;
	}
	if (r == AU_SUCCESS) {
		r = au.opens(au_s.h, 0);
		au_s.open = r == AU_SUCCESS;
	}
	if (r != AU_SUCCESS) {
		au_why(s, au_s.h, &au_s.t, r, au_s.user, quiet);
		au_sfree();
		return HIBR_FAIL;
	}
	lg(HIBR_LDBG, "auth: session open for %s on %s", au_s.user,
	   tty ? tty : "no terminal");
	hibr_ret(s, "");
	return HIBR_OK;
}

#ifdef __linux__
/* Write the login to utmp and wtmp, or its end -- the id the first four
   characters of the line, as systemd's UtmpIdentifier writes the record
   this one replaces. */
void au_utmp(pid_t pid, int dead)
{
	struct utmpx u;
	struct timespec ts;
	char *tty = isatty(0) ? ttyname(0) : 0;
	const char *line;

	if (!tty || geteuid() != 0)
		return;
	line = strncmp(tty, "/dev/", 5) ? tty : tty + 5;
	memset(&u, 0, sizeof u);
	u.ut_type = dead ? DEAD_PROCESS : USER_PROCESS;
	u.ut_pid = pid;
	strncpy(u.ut_line, line, sizeof u.ut_line);
	strncpy(u.ut_id, line, sizeof u.ut_id);
	if (!dead)
		strncpy(u.ut_user, au_s.user, sizeof u.ut_user);
	clock_gettime(CLOCK_REALTIME, &ts);
	u.ut_tv.tv_sec = ts.tv_sec;
	u.ut_tv.tv_usec = ts.tv_nsec / 1000;
	setutxent();
	pututxline(&u);
	endutxent();
	updwtmpx("/var/log/wtmp", &u);
}
#endif

/* An empty environment to start a login's from: clearenv is glibc's. */
char *au_noenv[1];

/* The child of `auth run`: become the user for good, take the login's
   environment, and run the command. It never returns. */
void au_child(char **cmd)
{
	char **env, *term = getenv("TERM");
	int i;

	for (i = 1; i < 32; i++)
		signal(i, SIG_DFL);
	if (geteuid() == 0) {
		if (initgroups(au_s.user, au_s.gid) != 0 ||
#ifdef __APPLE__
		    setgid(au_s.gid) != 0 || setuid(au_s.uid) != 0 ||
#else
		    setresgid(au_s.gid, au_s.gid, au_s.gid) != 0 ||
		    setresuid(au_s.uid, au_s.uid, au_s.uid) != 0 ||
#endif
		    getuid() != au_s.uid || geteuid() != au_s.uid ||
		    getgid() != au_s.gid || setuid(0) == 0) {
			fprintf(stderr, "hibr: auth: could not become %s\n", au_s.user);
			_exit(126);
		}
		term = term ? xs(term) : 0;
		environ = au_noenv;
		setenv("PATH", "/usr/local/bin:/usr/bin:/bin", 1);
		if (term)
			setenv("TERM", term, 1);
	}
	setenv("HOME", au_s.home, 1);
	setenv("USER", au_s.user, 1);
	setenv("LOGNAME", au_s.user, 1);
	setenv("SHELL", au_s.shell, 1);
	env = au.envlist(au_s.h);
	for (i = 0; env && env[i]; i++)
		putenv(env[i]);
	if (chdir(au_s.home) != 0 && chdir("/") != 0)
		_exit(126);
	execvp(cmd[0], cmd);
	fprintf(stderr, "hibr: auth: %s: %s\n", cmd[0], strerror(errno));
	_exit(127);
}

/* auth run cmd args...: run a command as the session's user, on this
   terminal, and wait for it -- the terminal's modes and foreground are
   put back afterwards. Its status is the command's. */
int au_run(sh *s, int ac, char **av)
{
	struct termios tm;
	int i = 2, havetm, st = 0, r;
	pid_t pid;
	void (*oi)(int), (*oq)(int), (*oz)(int), (*oo)(int);

	if (i < ac && !strcmp(av[i], "--"))
		i++;
	if (i >= ac) {
		lg(HIBR_LERR, "usage: auth run cmd [args...]");
		return 2;
	}
	if (!au_s.h) {
		lg(HIBR_LERR, "auth: no session is open; auth open first");
		return 2;
	}
	if (geteuid() != 0 && getuid() != au_s.uid) {
		lg(HIBR_LERR, "auth: only root can run a command as %s", au_s.user);
		return 2;
	}
	havetm = isatty(0) && tcgetattr(0, &tm) == 0;
	fflush(0);
	pid = fork();
	if (pid < 0) {
		lg(HIBR_LERR, "auth: fork: %s", strerror(errno));
		return HIBR_FAIL;
	}
	if (pid == 0)
		au_child(av + i);
	(void)s;
#ifdef __linux__
	au_utmp(pid, 0);
#endif
	oi = signal(SIGINT, SIG_IGN);
	oq = signal(SIGQUIT, SIG_IGN);
	oz = signal(SIGTSTP, SIG_IGN);
	while ((r = waitpid(pid, &st, 0)) < 0 && errno == EINTR)
		;
#ifdef __linux__
	au_utmp(pid, 1);
#endif
	oo = signal(SIGTTOU, SIG_IGN);
	if (isatty(0))
		tcsetpgrp(0, getpgrp());
	if (havetm)
		tcsetattr(0, TCSANOW, &tm);
	signal(SIGTTOU, oo);
	signal(SIGINT, oi);
	signal(SIGQUIT, oq);
	signal(SIGTSTP, oz);
	if (r < 0)
		return HIBR_FAIL;
	if (WIFEXITED(st))
		return WEXITSTATUS(st);
	return 128 + (WIFSIGNALED(st) ? WTERMSIG(st) : 0);
}

/* auth close: end the session `auth open` began. */
int au_close(sh *s)
{
	(void)s;
	if (!au_s.h)
		return HIBR_FAIL;
	lg(HIBR_LDBG, "auth: session closed for %s", au_s.user);
	au_sfree();
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
	if (ac > 1 && !strcmp(av[1], "open"))
		return au_open(s, ac, av);
	if (ac > 1 && !strcmp(av[1], "run"))
		return au_run(s, ac, av);
	if (ac > 1 && !strcmp(av[1], "close"))
		return au_close(s);
	lg(HIBR_LERR, "usage: auth check|open [-s service] [-c confdir] [-q] user "
		      "password | run cmd... | close | service | ready | whoami");
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
	if (au_s.h)
		au_sfree();
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
