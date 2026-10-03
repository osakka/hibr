#define _GNU_SOURCE

#include "wb.h"
#include <errno.h>
#include <fcntl.h>
#include <ftw.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

wb_br wb;

typedef struct wb_resp wb_resp;
struct wb_resp {
	int id;
	jv *v;
};

vec wb_resps;

/* Milliseconds on a clock that only goes forward. */
long wb_ms(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

/* Whether a path is a program this user can run. */
int wb_isexe(const char *p)
{
	struct stat st;

	return !stat(p, &st) && S_ISREG(st.st_mode) && !access(p, X_OK);
}

/* The browser to run: HIBR_WEB_BROWSER, else the first Chromium or Chrome
   on PATH, else where macOS keeps them. */
char *wb_find(void)
{
	static const char *names[] = { "chromium", "chromium-browser",
		"google-chrome", "google-chrome-stable", "chrome", 0 };
	static const char *mac[] = {
		"/Applications/Google Chrome.app/Contents/MacOS/Google Chrome",
		"/Applications/Chromium.app/Contents/MacOS/Chromium", 0 };
	const char *e = getenv("HIBR_WEB_BROWSER"), *path = getenv("PATH");
	const char *p, *q;
	str c;
	int i;

	if (e && *e)
		return wb_isexe(e) ? xs(e) : 0;
	for (i = 0; names[i]; i++) {
		for (p = path ? path : "/usr/bin:/bin"; *p; p = *q ? q + 1 : q) {
			q = strchr(p, ':');
			if (!q)
				q = p + strlen(p);
			s_init(&c);
			s_add(&c, p, (size_t)(q - p));
			s_ch(&c, '/');
			s_cat(&c, names[i]);
			if (wb_isexe(c.p)) {
				char *r = xs(c.p);
				s_free(&c);
				return r;
			}
			s_free(&c);
		}
	}
	for (i = 0; mac[i]; i++)
		if (wb_isexe(mac[i]))
			return xs(mac[i]);
	return 0;
}

/* Make a folder and the ones above it. */
void wb_mkdirs(const char *d)
{
	char *p = xs(d), *s;

	for (s = p + 1; *s; s++)
		if (*s == '/') {
			*s = 0;
			mkdir(p, 0700);
			*s = '/';
		}
	mkdir(p, 0700);
	free(p);
}

/* The profile the browser keeps its cookies and history in. */
char *wb_profdir(void)
{
	const char *e = getenv("HIBR_WEB_PROFILE"), *x, *h;
	str p;

	if (e && *e)
		return xs(e);
	s_init(&p);
	x = getenv("XDG_DATA_HOME");
	h = getenv("HOME");
	if (x && *x) {
		s_cat(&p, x);
	} else {
		s_cat(&p, h ? h : "/tmp");
		s_cat(&p, "/.local/share");
	}
	s_cat(&p, "/hibr/web/profile");
	return p.p;
}

/* Remove one entry while walking a folder away. */
int wb_rmone(const char *p, const struct stat *st, int flag, struct FTW *f)
{
	(void)st;
	(void)flag;
	(void)f;
	remove(p);
	return 0;
}

/* The user agent every tab is given: HIBR_WEB_UA, else the browser's own
   with "HeadlessChrome" said as "Chrome". YouTube's player gives up about
   a minute into a video -- "Something went wrong" -- in a browser that
   says it is headless and automated, which is why the browser is also
   started without the automation flag. */
void wb_useragent(void)
{
	const char *e = getenv("HIBR_WEB_UA"), *u, *h;
	jv *r;
	str s;

	free(wb.ua);
	wb.ua = 0;
	if (e) {
		wb.ua = *e ? xs(e) : 0;
		return;
	}
	r = wb_call("Browser.getVersion", "{}", 0);
	u = r ? jv_str(jv_path(r, "result.userAgent")) : 0;
	if (u && (h = strstr(u, "HeadlessChrome"))) {
		s_init(&s);
		s_add(&s, u, (size_t)(h - u));
		s_cat(&s, h + 8);
		wb.ua = s.p;
	}
	jv_free(r);
	lg(HIBR_LDBG, "web: tabs say they are %s", wb.ua ? wb.ua : "what the browser says");
}

/* Start the browser on a profile; whether it came up and answered. */
int wb_spawn(const char *bin, const char *prof)
{
	int toc[2], fromc[2], i, n;
	const char *extra = getenv("HIBR_WEB_ARGS");
	vec av = { 0, 0, 0 };
	str ud;
	char *xa = 0, *t;
	jv *r;
	pid_t pid;

	if (pipe(toc) < 0 || pipe(fromc) < 0) {
		lg(HIBR_LERR, "web: cannot make a pipe: %s", strerror(errno));
		return HIBR_FAIL;
	}
	s_init(&ud);
	s_cat(&ud, "--user-data-dir=");
	s_cat(&ud, prof);
	v_add(&av, (char *)bin);
	v_add(&av, "--headless=new");
	v_add(&av, "--remote-debugging-pipe");
	v_add(&av, "--no-first-run");
	v_add(&av, "--no-default-browser-check");
	v_add(&av, "--disable-gpu");
	v_add(&av, "--hide-scrollbars");
	v_add(&av, "--mute-audio");
	v_add(&av, "--disable-background-networking");
	v_add(&av, "--disable-blink-features=AutomationControlled");
	v_add(&av, ud.p);
	if (extra && *extra) {
		xa = xs(extra);
		for (t = strtok(xa, " "); t; t = strtok(0, " "))
			v_add(&av, t);
	}
	v_add(&av, "about:blank");
	v_add(&av, 0);
	fflush(0);
	pid = fork();
	if (pid < 0) {
		lg(HIBR_LERR, "web: cannot fork: %s", strerror(errno));
		goto fail;
	}
	if (!pid) {
		int a = fcntl(toc[0], F_DUPFD, 10), b = fcntl(fromc[1], F_DUPFD, 10);
		int nul = open("/dev/null", O_RDWR);

		for (i = 1; i < 32; i++)
			signal(i, SIG_DFL);
		setsid();
		dup2(nul, 0);
		dup2(nul, 1);
		dup2(nul, 2);
		dup2(a, 3);
		dup2(b, 4);
		n = (int)sysconf(_SC_OPEN_MAX);
		for (i = 5; i < n && i < 4096; i++)
			close(i);
		execv(bin, (char **)av.p);
		_exit(127);
	}
	close(toc[0]);
	close(fromc[1]);
	fcntl(toc[1], F_SETFD, FD_CLOEXEC);
	fcntl(fromc[0], F_SETFD, FD_CLOEXEC);
	fcntl(fromc[0], F_SETFL, fcntl(fromc[0], F_GETFL) | O_NONBLOCK);
	wb.pid = pid;
	wb.in = toc[1];
	wb.out = fromc[0];
	wb.next = 1;
	s_init(&wb.buf);
	free(xa);
	v_free(&av);
	s_free(&ud);
	r = wb_call("Target.setDiscoverTargets", "{\"discover\":true}", 0);
	if (!r) {
		wb_stop();
		return HIBR_FAIL;
	}
	jv_free(r);
	wb_useragent();
	lg(HIBR_LDBG, "web: %s is up, pid %d, profile %s", bin, (int)pid, prof);
	return HIBR_OK;
fail:
	close(toc[0]);
	close(toc[1]);
	close(fromc[0]);
	close(fromc[1]);
	free(xa);
	v_free(&av);
	s_free(&ud);
	return HIBR_FAIL;
}

/* Start the browser once; a profile another browser holds is not shared,
   one of its own for this shell is made instead and thrown away at the
   end. */
int wb_start(void)
{
	char *bin, *prof;
	str tmp;

	if (wb_alive())
		return HIBR_OK;
	bin = wb_find();
	if (!bin) {
		lg(HIBR_LERR, "web: no Chromium or Chrome found -- install one, "
			      "or set HIBR_WEB_BROWSER");
		return HIBR_FAIL;
	}
	prof = wb_profdir();
	wb_mkdirs(prof);
	if (wb_spawn(bin, prof) == HIBR_OK) {
		free(wb.profile);
		wb.profile = prof;
		wb.tmpprofile = 0;
		free(bin);
		return HIBR_OK;
	}
	free(prof);
	s_init(&tmp);
	s_cat(&tmp, getenv("TMPDIR") && *getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp");
	s_cat(&tmp, "/hibr-web-XXXXXX");
	if (!mkdtemp(tmp.p)) {
		s_free(&tmp);
		free(bin);
		return HIBR_FAIL;
	}
	lg(HIBR_LDBG, "web: the profile is in use; this shell uses %s", tmp.p);
	if (wb_spawn(bin, tmp.p) != HIBR_OK) {
		nftw(tmp.p, wb_rmone, 16, FTW_DEPTH | FTW_PHYS);
		s_free(&tmp);
		free(bin);
		lg(HIBR_LERR, "web: the browser did not start");
		return HIBR_FAIL;
	}
	free(wb.profile);
	wb.profile = tmp.p;
	wb.tmpprofile = 1;
	free(bin);
	return HIBR_OK;
}

/* Whether the browser is running. */
int wb_alive(void)
{
	int st;

	if (wb.pid <= 0)
		return 0;
	if (waitpid(wb.pid, &st, WNOHANG) == wb.pid) {
		lg(HIBR_LDBG, "web: the browser has gone");
		wb.pid = 0;
		close(wb.in);
		close(wb.out);
		wb.in = wb.out = -1;
		return 0;
	}
	return 1;
}

/* Stop the browser and forget every tab. */
void wb_stop(void)
{
	size_t i;
	int st, k;

	for (i = 0; i < wb.tabs.n; i++)
		if (wb.tabs.p[i])
			wb_tabclose(wb.tabs.p[i]);
	v_free(&wb.tabs);
	if (wb.pid > 0) {
		kill(wb.pid, SIGTERM);
		for (k = 0; k < 50; k++) {
			if (waitpid(wb.pid, &st, WNOHANG) == wb.pid)
				break;
			usleep(20000);
		}
		if (k == 50) {
			kill(wb.pid, SIGKILL);
			waitpid(wb.pid, &st, 0);
		}
		close(wb.in);
		close(wb.out);
	}
	wb.pid = 0;
	wb.in = wb.out = -1;
	s_free(&wb.buf);
	for (i = 0; i < wb_resps.n; i++) {
		jv_free(((wb_resp *)wb_resps.p[i])->v);
		free(wb_resps.p[i]);
	}
	v_free(&wb_resps);
	if (wb.profile && wb.tmpprofile)
		nftw(wb.profile, wb_rmone, 16, FTW_DEPTH | FTW_PHYS);
	free(wb.profile);
	wb.profile = 0;
}

/* The tab a session belongs to, or null. */
wb_tab *wb_bysession(const char *s)
{
	size_t i;
	wb_tab *t;

	for (i = 0; s && i < wb.tabs.n; i++) {
		t = wb.tabs.p[i];
		if (t && t->session && !strcmp(t->session, s))
			return t;
	}
	return 0;
}

/* The tab a target is, or null. */
wb_tab *wb_bytarget(const char *s)
{
	size_t i;
	wb_tab *t;

	for (i = 0; s && i < wb.tabs.n; i++) {
		t = wb.tabs.p[i];
		if (t && t->target && !strcmp(t->target, s))
			return t;
	}
	return 0;
}

/* An event: what it changes about a tab. */
void wb_event(jv *m)
{
	const char *meth = jv_str(jv_get(m, "method"));
	wb_tab *t = wb_bysession(jv_str(jv_get(m, "sessionId")));
	jv *ti;

	if (!strcmp(meth, "Target.targetInfoChanged")) {
		ti = jv_path(m, "params.targetInfo");
		t = wb_bytarget(jv_str(jv_get(ti, "targetId")));
		if (t) {
			t->url.n = 0;
			s_cat(&t->url, jv_str(jv_get(ti, "url")));
			t->title.n = 0;
			s_cat(&t->title, jv_str(jv_get(ti, "title")));
			t->dirty = 1;
		}
		return;
	}
	if (!t)
		return;
	if (!strcmp(meth, "Page.frameStartedLoading")) {
		t->loading = 1;
	} else if (!strcmp(meth, "Page.frameStoppedLoading") ||
		   !strcmp(meth, "Page.loadEventFired")) {
		t->loading = 0;
		t->dirty = 1;
	} else if (!strcmp(meth, "Page.domContentEventFired") ||
		   !strcmp(meth, "Page.navigatedWithinDocument") ||
		   !strcmp(meth, "Page.frameNavigated")) {
		t->dirty = 1;
	} else if (!strcmp(meth, "Page.javascriptDialogOpening")) {
		wb_send("Page.handleJavaScriptDialog", "{\"accept\":true}",
			t->session);
	}
}

/* Read what the browser has sent, waiting up to ms for something; replies
   are kept for whoever asked, events are acted on. How many messages. */
int wb_pump(int ms)
{
	fd_set rf;
	struct timeval tv;
	char *b;
	ssize_t r;
	size_t i, s0;
	int got = 0;
	jv *m;
	wb_resp *rp;

	if (wb.out < 0)
		return -1;
	FD_ZERO(&rf);
	FD_SET(wb.out, &rf);
	tv.tv_sec = ms / 1000;
	tv.tv_usec = (ms % 1000) * 1000;
	if (select(wb.out + 1, &rf, 0, 0, &tv) <= 0)
		return 0;
	b = xm(1 << 16);
	for (;;) {
		r = read(wb.out, b, 1 << 16);
		if (r < 0 && errno == EINTR)
			continue;
		if (r <= 0)
			break;
		s_add(&wb.buf, b, (size_t)r);
	}
	free(b);
	if (r == 0) {
		lg(HIBR_LDBG, "web: the browser closed its pipe");
		wb_alive();
	}
	s0 = 0;
	for (i = 0; i < wb.buf.n; i++) {
		if (wb.buf.p[i])
			continue;
		m = jv_parse(wb.buf.p + s0, i - s0);
		s0 = i + 1;
		if (!m)
			continue;
		got++;
		if (jv_get(m, "id")) {
			rp = xm(sizeof *rp);
			rp->id = (int)jv_num(jv_get(m, "id"));
			rp->v = m;
			v_add(&wb_resps, rp);
		} else {
			wb_event(m);
			jv_free(m);
		}
	}
	if (s0) {
		memmove(wb.buf.p, wb.buf.p + s0, wb.buf.n - s0);
		wb.buf.n -= s0;
		if (wb.buf.p)
			wb.buf.p[wb.buf.n] = 0;
	}
	return got;
}

/* Write one request; its id, or 0. */
int wb_req(const char *method, const char *params, const char *session)
{
	str m;
	size_t off = 0;
	ssize_t w;
	int id;

	if (wb.in < 0)
		return 0;
	id = wb.next++;
	s_init(&m);
	s_cat(&m, "{\"id\":");
	s_num(&m, id);
	s_cat(&m, ",\"method\":");
	jv_quote(&m, method);
	if (session && *session) {
		s_cat(&m, ",\"sessionId\":");
		jv_quote(&m, session);
	}
	s_cat(&m, ",\"params\":");
	s_cat(&m, params && *params ? params : "{}");
	s_cat(&m, "}");
	s_add(&m, "", 1);
	while (off < m.n) {
		w = write(wb.in, m.p + off, m.n - off);
		if (w < 0 && errno == EINTR)
			continue;
		if (w <= 0) {
			lg(HIBR_LERR, "web: cannot write to the browser: %s",
			   strerror(errno));
			s_free(&m);
			return 0;
		}
		off += (size_t)w;
	}
	s_free(&m);
	return id;
}

/* Send a request and not wait for what it says. */
int wb_send(const char *method, const char *params, const char *session)
{
	return wb_req(method, params, session) ? HIBR_OK : HIBR_FAIL;
}

/* Send a request and wait for its reply; the reply's result, or null with
   the reason said. */
jv *wb_call(const char *method, const char *params, const char *session)
{
	int id = wb_req(method, params, session);
	long until = wb_ms() + WB_WAITMS;
	size_t i;
	wb_resp *rp;
	jv *v, *e;

	if (!id)
		return 0;
	for (;;) {
		for (i = 0; i < wb_resps.n; i++) {
			rp = wb_resps.p[i];
			if (rp->id != id)
				continue;
			v = rp->v;
			free(rp);
			wb_resps.p[i] = wb_resps.p[--wb_resps.n];
			e = jv_get(v, "error");
			if (e) {
				lg(HIBR_LERR, "web: %s: %s", method,
				   jv_str(jv_get(e, "message")));
				jv_free(v);
				return 0;
			}
			return v;
		}
		if (wb_ms() > until || !wb_alive()) {
			lg(HIBR_LERR, "web: %s: no answer from the browser", method);
			return 0;
		}
		wb_pump(50);
	}
}

/* Act on whatever has come in, without waiting; how many messages. */
int wb_events(void)
{
	int n = 0, k;

	while ((k = wb_pump(0)) > 0)
		n += k;
	return n;
}
