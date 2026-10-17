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

/* Set the tab's user agent: the one given, or the module's own. */
int wb_setua(wb_tab *t, const char *ua)
{
	str p;
	int r;

	if (!ua || !*ua)
		ua = wb.ua;
	if (!ua || !*ua)
		return HIBR_OK;
	s_init(&p);
	s_cat(&p, "{\"userAgent\":");
	jv_quote(&p, ua);
	s_cat(&p, "}");
	r = wb_callq("Emulation.setUserAgentOverride", p.p, t->session);
	s_free(&p);
	return r;
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
		if (wb_setua(t, v) != HIBR_OK)
			return HIBR_FAIL;
	} else {
		lg(HIBR_LERR, "web set: js, images, dark, zoom or ua, not %s", k);
		return 2;
	}
	lg(HIBR_LDBG, "web: tab %d: %s %s", t->id, k, v);
	t->dirty = 1;
	return HIBR_OK;
bad:
	lg(HIBR_LERR, "web set: %s: not a value for %s", v, k);
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
		lg(HIBR_LERR, "web config: browser, profile or args, not %s", k);
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
		lg(HIBR_LERR, "usage: web clear cookies|cache|history [T...]");
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
		lg(HIBR_LERR, "web clear: cookies, cache or history, not %s", av[2]);
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
