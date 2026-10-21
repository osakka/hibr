#define _GNU_SOURCE

#include "wb.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/* Send one call on a tab's session and drop its reply; whether it came. */
int wb_callq(const char *method, const char *params, const char *session)
{
	jv *r = wb_call(method, params, session);

	if (!r)
		return HIBR_FAIL;
	jv_free(r);
	return HIBR_OK;
}

/* Whether a word means on: on, 1, yes or true. */
int wb_on(const char *v)
{
	return !strcmp(v, "on") || !strcmp(v, "1") || !strcmp(v, "yes") ||
	       !strcmp(v, "true");
}

/* Whether a word means on or off at all. */
int wb_onoff(const char *v)
{
	return wb_on(v) || !strcmp(v, "off") || !strcmp(v, "0") ||
	       !strcmp(v, "no") || !strcmp(v, "false");
}

/* What the browser itself says it is, for an override that has to name a
   user agent when the module keeps none of its own; null if it will not say. */
char *wb_browserua(void)
{
	jv *r = wb_call("Browser.getVersion", "{}", 0);
	const char *u = r ? jv_str(jv_path(r, "result.userAgent")) : 0;
	char *o = u && *u ? xs(u) : 0;

	jv_free(r);
	return o;
}

/* Set the tab's user agent: the one given, or the module's own -- and the
   languages it asks for, which ride on the same override (Gitea #285), so
   one without the other would lose it. The override names a user agent
   whatever happens, so the browser's own is asked for when the module
   keeps none and there are languages to send, or to take back. */
int wb_setua(wb_tab *t, const char *ua)
{
	str p;
	int r;
	char *own = 0;

	if (!ua || !*ua)
		ua = wb.ua;
	if ((!ua || !*ua) && (t->lang || t->langsent))
		ua = own = wb_browserua();
	if (!ua || !*ua)
		return HIBR_OK;
	s_init(&p);
	s_cat(&p, "{\"userAgent\":");
	jv_quote(&p, ua);
	if (t->lang) {
		s_cat(&p, ",\"acceptLanguage\":");
		jv_quote(&p, t->lang);
	}
	s_cat(&p, "}");
	r = wb_callq("Emulation.setUserAgentOverride", p.p, t->session);
	t->langsent = t->lang != 0;
	s_free(&p);
	free(own);
	return r;
}

/* The locale a tab's scripts see -- dates, numbers, Intl -- from the first
   of its languages, or the browser's own again when it has none. One in
   force has to be taken off before another is put on. */
int wb_setlocale(wb_tab *t)
{
	str p;
	size_t n;
	int r;

	wb_callq("Emulation.setLocaleOverride", "{}", t->session);
	if (!t->lang)
		return HIBR_OK;
	n = strcspn(t->lang, ",; ");
	s_init(&p);
	s_cat(&p, "{\"locale\":\"");
	s_add(&p, t->lang, n);
	s_cat(&p, "\"}");
	r = wb_callq("Emulation.setLocaleOverride", p.p, t->session);
	s_free(&p);
	return r;
}

/* Turn on the tab's network domain, once: the extra headers and the
   blocked addresses both belong to it. */
int wb_netup(wb_tab *t)
{
	if (t->netup)
		return HIBR_OK;
	if (wb_callq("Network.enable", "{}", t->session) != HIBR_OK)
		return HIBR_FAIL;
	t->netup = 1;
	return HIBR_OK;
}

/* Add a script to every document the tab loads from now on, keeping the
   identifier the browser gives it in *slot; or, with js null, take the
   one in *slot off again. */
int wb_docjs(wb_tab *t, char **slot, const char *js)
{
	str p;
	jv *r;

	if (*slot) {
		s_init(&p);
		s_cat(&p, "{\"identifier\":");
		jv_quote(&p, *slot);
		s_cat(&p, "}");
		wb_callq("Page.removeScriptToEvaluateOnNewDocument", p.p,
			 t->session);
		s_free(&p);
		free(*slot);
		*slot = 0;
	}
	if (!js)
		return HIBR_OK;
	s_init(&p);
	s_cat(&p, "{\"source\":");
	jv_quote(&p, js);
	s_cat(&p, "}");
	r = wb_call("Page.addScriptToEvaluateOnNewDocument", p.p, t->session);
	s_free(&p);
	if (!r)
		return HIBR_FAIL;
	*slot = xs(jv_str(jv_path(r, "result.identifier")));
	jv_free(r);
	return HIBR_OK;
}

/* Do Not Track and Global Privacy Control, for the tab: the two headers on
   every request, and what a page's scripts read of them. The headers are
   the whole set the tab sends, so off sends an empty one. */
int wb_setdnt(wb_tab *t, int on)
{
	if (wb_netup(t) != HIBR_OK)
		return HIBR_FAIL;
	if (wb_callq("Network.setExtraHTTPHeaders",
		     on ? "{\"headers\":{\"DNT\":\"1\",\"Sec-GPC\":\"1\"}}"
			: "{\"headers\":{}}", t->session) != HIBR_OK)
		return HIBR_FAIL;
	return wb_docjs(t, &t->dntjs, on ?
		"Object.defineProperty(Navigator.prototype,'globalPrivacyControl',"
		"{get:()=>true,configurable:true});"
		"Object.defineProperty(Navigator.prototype,'doNotTrack',"
		"{get:()=>'1',configurable:true});" : 0);
}

/* Addresses the tab never fetches: patterns with * for anything, separated
   by spaces, commas or new lines. Empty fetches everything again. */
int wb_setblock(wb_tab *t, const char *v)
{
	str p;
	const char *q = v;
	char *one;
	size_t n;
	int first = 1, r;

	if (wb_netup(t) != HIBR_OK)
		return HIBR_FAIL;
	s_init(&p);
	s_cat(&p, "{\"urls\":[");
	while (*q) {
		q += strspn(q, " ,\t\r\n");
		n = strcspn(q, " ,\t\r\n");
		if (!n)
			break;
		if (!first)
			s_ch(&p, ',');
		first = 0;
		one = strndup(q, n);
		jv_quote(&p, one);
		free(one);
		q += n;
	}
	s_cat(&p, "]}");
	r = wb_callq("Network.setBlockedURLs", p.p, t->session);
	s_free(&p);
	return r;
}

/* Where the tab says it is when a page asks and location is allowed:
   "lat lon" in degrees, or empty for no position at all. */
int wb_setgeo(wb_tab *t, const char *v)
{
	char b[160];
	double la, lo;

	if (!*v)
		return wb_callq("Emulation.clearGeolocationOverride", "{}",
				t->session);
	if (sscanf(v, "%lf %lf", &la, &lo) != 2 || la < -90 || la > 90 ||
	    lo < -180 || lo > 180)
		return 2;
	snprintf(b, sizeof b, "{\"latitude\":%.6f,\"longitude\":%.6f,"
		 "\"accuracy\":100}", la, lo);
	return wb_callq("Emulation.setGeolocationOverride", b, t->session);
}

/* A page's notifications, handed to `web notes` rather than lost: the
   browser has no screen to show them on. Notification is replaced, in every
   document the tab loads from now on, by one that passes the title and the
   body to a binding, whose calls arrive as Runtime.bindingCalled. A binding
   reaches only the contexts the runtime domain inspects, so it is turned on
   first: measured, without it the binding was never there. */
int wb_setnotes(wb_tab *t, int on)
{
	if (on && (wb_callq("Runtime.enable", "{}", t->session) != HIBR_OK ||
		   wb_callq("Runtime.addBinding", "{\"name\":\"__hibrNote\"}",
			    t->session) != HIBR_OK))
		return HIBR_FAIL;
	if (!on)
		wb_callq("Runtime.removeBinding", "{\"name\":\"__hibrNote\"}",
			 t->session);
	return wb_docjs(t, &t->notejs, on ?
		"(()=>{class N{constructor(t,o){o=o||{};try{window.__hibrNote("
		"JSON.stringify({title:String(t),body:String(o.body||'')}))}"
		"catch(e){}}static get permission(){return 'granted'}"
		"static requestPermission(c){if(c)c('granted');"
		"return Promise.resolve('granted')}close(){}"
		"addEventListener(){}removeEventListener(){}}"
		"window.Notification=N})()" : 0);
}

/* One of a tab's own settings: what the browser is told for this page only,
   so the YouTube player sharing the same Chromium keeps its own. Each takes
   effect on the next render; images and the user agent want the page
   loaded again to reach what was already fetched, which is the caller's
   to decide. */
int wb_set(wb_tab *t, const char *k, const char *v)
{
	int on = wb_on(v), z;

	if (!strcmp(k, "js")) {
		if (!wb_onoff(v))
			goto bad;
		if (wb_callq("Emulation.setScriptExecutionDisabled",
			     on ? "{\"value\":false}" : "{\"value\":true}",
			     t->session) != HIBR_OK)
			return HIBR_FAIL;
	} else if (!strcmp(k, "dark")) {
		if (!wb_onoff(v))
			goto bad;
		if (wb_callq("Emulation.setEmulatedMedia",
			     on ? "{\"features\":[{\"name\":\"prefers-color-scheme\","
				  "\"value\":\"dark\"}]}"
				: "{\"features\":[{\"name\":\"prefers-color-scheme\","
				  "\"value\":\"\"}]}", t->session) != HIBR_OK)
			return HIBR_FAIL;
	} else if (!strcmp(k, "images")) {
		if (!wb_onoff(v))
			goto bad;
		t->noimg = !on;
		if (wb_callq(on ? "Fetch.disable" : "Fetch.enable",
			     on ? "{}" : "{\"patterns\":[{\"resourceType\":\"Image\","
					 "\"requestStage\":\"Request\"}]}",
			     t->session) != HIBR_OK)
			return HIBR_FAIL;
	} else if (!strcmp(k, "zoom")) {
		z = atoi(v);
		if (z < 25 || z > 500)
			goto bad;
		t->zoom = z;
	} else if (!strcmp(k, "ua")) {
		free(t->uaset);
		t->uaset = *v ? xs(v) : 0;
		if (wb_setua(t, t->uaset) != HIBR_OK)
			return HIBR_FAIL;
	} else if (!strcmp(k, "lang")) {
		free(t->lang);
		t->lang = *v ? xs(v) : 0;
		if (wb_setua(t, t->uaset) != HIBR_OK ||
		    wb_setlocale(t) != HIBR_OK)
			return HIBR_FAIL;
	} else if (!strcmp(k, "dnt")) {
		if (!wb_onoff(v))
			goto bad;
		if (wb_setdnt(t, on) != HIBR_OK)
			return HIBR_FAIL;
	} else if (!strcmp(k, "block")) {
		if (wb_setblock(t, v) != HIBR_OK)
			return HIBR_FAIL;
	} else if (!strcmp(k, "geo")) {
		z = wb_setgeo(t, v);
		if (z == 2)
			goto bad;
		if (z != HIBR_OK)
			return HIBR_FAIL;
	} else if (!strcmp(k, "live")) {
		if (!wb_onoff(v))
			goto bad;
		if (on != t->live && wb_callq(on ? "Page.startScreencast" :
					      "Page.stopScreencast",
					      on ? "{\"format\":\"jpeg\",\"quality\":1,"
						   "\"maxWidth\":32,\"maxHeight\":32,"
						   "\"everyNthFrame\":1}" : "{}",
					      t->session) != HIBR_OK)
			return HIBR_FAIL;
		t->live = on;
	} else if (!strcmp(k, "notes")) {
		if (!wb_onoff(v))
			goto bad;
		if (wb_setnotes(t, on) != HIBR_OK)
			return HIBR_FAIL;
	} else {
		wb_err(HIBR_LERR, "web set: js, images, dark, zoom, ua, lang, dnt, "
			      "block, geo, notes or live, not %s", k);
		return 2;
	}
	lg(HIBR_LDBG, "web: tab %d: %s %s", t->id, k, v);
	t->dirty = 1;
	return HIBR_OK;
bad:
	wb_err(HIBR_LERR, "web set: %s: not a value for %s", v, k);
	return 2;
}

/* Answer an image request a tab with images off has paused: refused, as an
   ad blocker refuses one, so the page lays out without it. */
void wb_imgpaused(wb_tab *t, jv *m)
{
	str p;

	s_init(&p);
	s_cat(&p, "{\"requestId\":");
	jv_quote(&p, jv_str(jv_path(m, "params.requestId")));
	if (t->noimg) {
		s_cat(&p, ",\"errorReason\":\"BlockedByClient\"}");
		wb_send("Fetch.failRequest", p.p, t->session);
	} else {
		s_cat(&p, "}");
		wb_send("Fetch.continueRequest", p.p, t->session);
	}
	s_free(&p);
}

/* Put the tab's zoom on the page before it is read: a style on the root, so
   the layout, the positions the extraction reads and the screenshot all
   agree. Taken off again once the tab is back to 100. */
void wb_zoomapply(wb_tab *t)
{
	str js, o;
	int z = t->zoom ? t->zoom : 100;

	if (z == 100 && !t->zoomed)
		return;
	s_init(&js);
	s_init(&o);
	if (z == 100) {
		s_cat(&js, "(()=>{const s=document.getElementById('__hibr_zoom');"
			   "if(s)s.remove();return 1})()");
	} else {
		s_cat(&js, "(()=>{let s=document.getElementById('__hibr_zoom');"
			   "if(!s){s=document.createElement('style');"
			   "s.id='__hibr_zoom';}s.textContent='html{zoom:");
		s_num(&js, z);
		s_cat(&js, "%!important}';(document.head||document.documentElement)"
			   ".appendChild(s);return 1})()");
	}
	wb_eval(t, js.p, &o);
	t->zoomed = z != 100;
	s_free(&js);
	s_free(&o);
}

/* What the browser is started with, where the environment used to be the
   only way: a program, a profile folder, extra flags. Empty puts back the
   environment's value. Read when the browser next starts. */
int wb_config(const char *k, const char *v)
{
	char **slot;

	if (!strcmp(k, "browser"))
		slot = &wb.cfbin;
	else if (!strcmp(k, "profile"))
		slot = &wb.cfprof;
	else if (!strcmp(k, "args"))
		slot = &wb.cfargs;
	else {
		wb_err(HIBR_LERR, "web config: browser, profile or args, not %s", k);
		return 2;
	}
	free(*slot);
	*slot = v && *v ? xs(v) : 0;
	lg(HIBR_LDBG, "web: next start: %s %s", k, v ? v : "");
	return HIBR_OK;
}

/* A session to ask something of the whole browser on: any open tab's, or
   a page opened for the purpose and closed by wb_tmpdone. */
char *wb_anysession(char **tmptarget)
{
	size_t i;
	wb_tab *t;
	jv *r;
	str p;
	char *sess;

	*tmptarget = 0;
	for (i = 0; i < wb.tabs.n; i++) {
		t = wb.tabs.p[i];
		if (t && t->session)
			return xs(t->session);
	}
	r = wb_call("Target.createTarget", "{\"url\":\"about:blank\"}", 0);
	if (!r)
		return 0;
	*tmptarget = xs(jv_str(jv_path(r, "result.targetId")));
	jv_free(r);
	s_init(&p);
	s_cat(&p, "{\"targetId\":");
	jv_quote(&p, *tmptarget);
	s_cat(&p, ",\"flatten\":true}");
	r = wb_call("Target.attachToTarget", p.p, 0);
	s_free(&p);
	if (!r)
		return 0;
	sess = xs(jv_str(jv_path(r, "result.sessionId")));
	jv_free(r);
	return sess;
}

/* Close the page wb_anysession opened, if it opened one. */
void wb_tmpdone(char *target)
{
	str p;

	if (!target)
		return;
	s_init(&p);
	s_cat(&p, "{\"targetId\":");
	jv_quote(&p, target);
	s_cat(&p, "}");
	wb_callq("Target.closeTarget", p.p, 0);
	s_free(&p);
	free(target);
}

/* Forget what the browser keeps: cookies or the cache for the whole
   profile, starting the browser if it is not running, or the back and
   forward lists of the tabs named (every tab when none is). */
int wb_clear(int ac, char **av)
{
	char *tmp, *sess;
	int r, i;
	size_t j;
	wb_tab *t;

	if (ac < 3) {
		wb_err(HIBR_LERR, "usage: web clear cookies|cache|history [T...]");
		return 2;
	}
	if (!strcmp(av[2], "history")) {
		if (!wb_alive())
			return HIBR_OK;
		r = HIBR_OK;
		if (ac > 3) {
			for (i = 3; i < ac; i++) {
				t = wb_tabget(av[i]);
				if (!t || wb_callq("Page.resetNavigationHistory", "{}",
						   t->session) != HIBR_OK)
					r = HIBR_FAIL;
			}
			return r;
		}
		for (j = 0; j < wb.tabs.n; j++) {
			t = wb.tabs.p[j];
			if (t && wb_callq("Page.resetNavigationHistory", "{}",
					  t->session) != HIBR_OK)
				r = HIBR_FAIL;
		}
		return r;
	}
	if (strcmp(av[2], "cookies") && strcmp(av[2], "cache")) {
		wb_err(HIBR_LERR, "web clear: cookies, cache or history, not %s", av[2]);
		return 2;
	}
	if (wb_start() != HIBR_OK)
		return HIBR_FAIL;
	if (!strcmp(av[2], "cookies"))
		return wb_callq("Storage.clearCookies", "{}", 0);
	sess = wb_anysession(&tmp);
	r = sess ? wb_callq("Network.clearBrowserCache", "{}", sess) : HIBR_FAIL;
	free(sess);
	wb_tmpdone(tmp);
	return r;
}

/* What every page may ask for and be given -- location, camera,
   microphone, notifications -- allowed or refused, for the whole browser,
   which is what the protocol offers; or every one put back as it was. A
   headless browser can show no prompt, so asking is not one of the
   answers (Gitea #285). */
int wb_permit(const char *name, const char *how)
{
	str p;
	int r;

	if (wb_start() != HIBR_OK)
		return HIBR_FAIL;
	if (!strcmp(name, "reset"))
		return wb_callq("Browser.resetPermissions", "{}", 0);
	if (strcmp(name, "geolocation") && strcmp(name, "camera") &&
	    strcmp(name, "microphone") && strcmp(name, "notifications")) {
		wb_err(HIBR_LERR, "web permit: geolocation, camera, microphone, "
			      "notifications or reset, not %s", name);
		return 2;
	}
	if (!how || (strcmp(how, "allow") && strcmp(how, "refuse"))) {
		wb_err(HIBR_LERR, "web permit: allow or refuse, not %s",
		   how ? how : "");
		return 2;
	}
	s_init(&p);
	s_cat(&p, "{\"permission\":{\"name\":");
	jv_quote(&p, name);
	s_cat(&p, "},\"setting\":\"");
	s_cat(&p, strcmp(how, "allow") ? "denied" : "granted");
	s_cat(&p, "\"}");
	r = wb_callq("Browser.setPermission", p.p, 0);
	s_free(&p);
	return r;
}

/* Downloads into a folder, each told to `web downloaded` when it ends; or
   none at all, which is what the browser does unless told (Gitea #285). */
int wb_downloads(const char *dir)
{
	str p;
	int r;

	if (wb_start() != HIBR_OK)
		return HIBR_FAIL;
	free(wb.dldir);
	wb.dldir = 0;
	if (!dir || !*dir || !strcmp(dir, "off"))
		return wb_callq("Browser.setDownloadBehavior",
				"{\"behavior\":\"deny\"}", 0);
	wb.dldir = xs(dir);
	s_init(&p);
	s_cat(&p, "{\"behavior\":\"allow\",\"eventsEnabled\":true,"
		  "\"downloadPath\":");
	jv_quote(&p, dir);
	s_cat(&p, "}");
	r = wb_callq("Browser.setDownloadBehavior", p.p, 0);
	s_free(&p);
	return r;
}

/* A download beginning or moving on: a name kept by its guid as it
   begins, and a line for `web downloaded` once it has ended -- `done`
   and where it is, or `failed` and its name. */
void wb_dlevent(const char *meth, jv *m)
{
	const char *g = jv_str(jv_path(m, "params.guid")), *st;
	str l;
	size_t i;

	if (!strcmp(meth, "Browser.downloadWillBegin")) {
		v_add(&wb.dlbegun, xs(g));
		v_add(&wb.dlbegun,
		      xs(jv_str(jv_path(m, "params.suggestedFilename"))));
		return;
	}
	st = jv_str(jv_path(m, "params.state"));
	if (strcmp(st, "completed") && strcmp(st, "canceled"))
		return;
	for (i = 0; i + 1 < wb.dlbegun.n; i += 2) {
		if (strcmp(wb.dlbegun.p[i], g))
			continue;
		s_init(&l);
		if (!strcmp(st, "completed")) {
			s_cat(&l, "done\t");
			if (wb.dldir) {
				s_cat(&l, wb.dldir);
				s_ch(&l, '/');
			}
		} else {
			s_cat(&l, "failed\t");
		}
		s_cat(&l, wb.dlbegun.p[i + 1]);
		v_add(&wb.dlended, l.p);
		free(wb.dlbegun.p[i]);
		free(wb.dlbegun.p[i + 1]);
		wb.dlbegun.p[i] = wb.dlbegun.p[wb.dlbegun.n - 2];
		wb.dlbegun.p[i + 1] = wb.dlbegun.p[wb.dlbegun.n - 1];
		wb.dlbegun.n -= 2;
		lg(HIBR_LDBG, "web: download %s", st);
		return;
	}
}

/* A page's notification, from the binding wb_setnotes put there: its
   title and body, a tab between, for `web notes`. */
void wb_noteevent(jv *m)
{
	jv *n;
	str l;
	const char *pl;

	if (strcmp(jv_str(jv_path(m, "params.name")), "__hibrNote"))
		return;
	pl = jv_str(jv_path(m, "params.payload"));
	n = jv_parse(pl, strlen(pl));
	if (!n)
		return;
	s_init(&l);
	s_cat(&l, jv_str(jv_get(n, "title")));
	s_ch(&l, '\t');
	s_cat(&l, jv_str(jv_get(n, "body")));
	v_add(&wb.notes, l.p);
	jv_free(n);
}

/* Hand over and forget the lines a queue holds: one a line, or with := as
   one string. Status 1 when there were none, so a caller can ask cheaply. */
void wb_drain(sh *s, vec *q)
{
	str o;
	size_t i;

	s_init(&o);
	for (i = 0; i < q->n; i++) {
		if (i)
			s_ch(&o, '\n');
		s_cat(&o, q->p[i]);
		free(q->p[i]);
	}
	q->n = 0;
	hibr_ret(s, o.p ? o.p : "");
	if (!s->bind && o.n)
		printf("%s\n", o.p);
	s_free(&o);
}

/* Decode base64 onto out, skipping anything that is not part of it. */
void wb_unb64(const char *p, str *out)
{
	unsigned acc = 0;
	int bits = 0, v;

	for (; *p && *p != '='; p++) {
		if (*p >= 'A' && *p <= 'Z')
			v = *p - 'A';
		else if (*p >= 'a' && *p <= 'z')
			v = *p - 'a' + 26;
		else if (*p >= '0' && *p <= '9')
			v = *p - '0' + 52;
		else if (*p == '+')
			v = 62;
		else if (*p == '/')
			v = 63;
		else
			continue;
		acc = (acc << 6) | (unsigned)v;
		bits += 6;
		if (bits >= 8) {
			bits -= 8;
			s_ch(out, (int)((acc >> bits) & 0xFF));
		}
	}
}

/* The tab's page as a PDF, written to path: printed the way the browser
   prints it, backgrounds included. */
int wb_pdf(wb_tab *t, const char *path)
{
	jv *r;
	str d;
	FILE *f;
	int ok;

	r = wb_call("Page.printToPDF", "{\"printBackground\":true}", t->session);
	if (!r || !jv_path(r, "result.data")) {
		wb_err(HIBR_LERR, "web pdf: the browser did not print the page");
		jv_free(r);
		return HIBR_FAIL;
	}
	s_init(&d);
	wb_unb64(jv_str(jv_path(r, "result.data")), &d);
	jv_free(r);
	f = fopen(path, "wb");
	if (!f) {
		wb_err(HIBR_LERR, "web pdf: %s: cannot be written", path);
		s_free(&d);
		return HIBR_FAIL;
	}
	ok = d.n && fwrite(d.p, 1, d.n, f) == d.n;
	ok = fclose(f) == 0 && ok;
	s_free(&d);
	return ok ? HIBR_OK : HIBR_FAIL;
}
