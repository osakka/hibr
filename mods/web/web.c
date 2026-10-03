#define _GNU_SOURCE

#include "wb.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

void v_setp(sh *s, const char *nm, char **ks, int nk, const char *val);
void v_del(sh *s, const char *k);
long wb_ms(void);

const dp_api *wb_dp;

/* Say a result: into $RET, and printed unless := is taking it. */
void wb_say(sh *s, const char *t)
{
	hibr_ret(s, t);
	if (!s->bind)
		printf("%s\n", t);
}

/* Say a number. */
void wb_sayn(sh *s, long n)
{
	char b[32];

	snprintf(b, sizeof b, "%ld", n);
	wb_say(s, b);
}

/* Wait for a tab's page to finish loading, up to ms; whether it did. */
int wb_wait(wb_tab *t, long ms)
{
	long until = wb_ms() + ms;

	wb_events();
	while (t->loading && wb_ms() < until && wb_alive())
		wb_pump(50);
	return t->loading ? HIBR_FAIL : HIBR_OK;
}

/* The page's visible text, as a reader would copy it. */
int wb_text(sh *s, wb_tab *t)
{
	str o;
	int r;

	s_init(&o);
	r = wb_eval(t, "document.body ? document.body.innerText : ''", &o);
	if (r == HIBR_OK) {
		hibr_ret(s, o.p ? o.p : "");
		if (!s->bind) {
			fputs(o.p ? o.p : "", stdout);
			if (o.n && o.p[o.n - 1] != '\n')
				putchar('\n');
		}
	}
	s_free(&o);
	return r;
}

/* Every link on the page, address and text: printed a line each, tab
   between, or with := a map, r[i]["href"] and r[i]["text"]. */
int wb_links(sh *s, wb_tab *t)
{
	str o, line, key;
	jv *a, *e;
	size_t i;
	char *ks[2];

	s_init(&o);
	if (wb_eval(t, "JSON.stringify(Array.from(document.querySelectorAll("
		       "'a[href]')).map(a=>[a.href,(a.innerText||a.title||'')"
		       ".trim().replace(/\\s+/g,' ')]))", &o) != HIBR_OK) {
		s_free(&o);
		return HIBR_FAIL;
	}
	a = jv_parse(o.p ? o.p : "", o.n);
	s_free(&o);
	if (!a)
		return HIBR_FAIL;
	if (s->bind) {
		v_del(s, "RET");
		hibr_set(s, "RET", "", 0);
	}
	s_init(&line);
	s_init(&key);
	for (i = 0; i < a->v.n; i++) {
		e = a->v.p[i];
		if (s->bind) {
			key.n = 0;
			s_num(&key, (long)i);
			ks[0] = key.p;
			ks[1] = "href";
			v_setp(s, "RET", ks, 2, jv_str(jv_path(e, "0")));
			ks[1] = "text";
			v_setp(s, "RET", ks, 2, jv_str(jv_path(e, "1")));
		} else {
			line.n = 0;
			s_cat(&line, jv_str(jv_path(e, "0")));
			s_ch(&line, '\t');
			s_cat(&line, jv_str(jv_path(e, "1")));
			puts(line.p);
		}
	}
	s_free(&line);
	s_free(&key);
	jv_free(a);
	return HIBR_OK;
}

/* web draw t row col [h w] [-p pane]: the last frame into the display. */
int wb_drawcmd(sh *s, wb_tab *t, int ac, char **av)
{
	const char *pane = 0;
	int i, n = 0, v[4] = { 0, 0, -1, -1 };
	int prow = 0, pcol = 0, ph = 0, pw = 0;

	for (i = 3; i < ac; i++) {
		if (!strcmp(av[i], "-p") && i + 1 < ac) {
			pane = av[++i];
			continue;
		}
		if (n < 4)
			v[n++] = atoi(av[i]);
	}
	if (n < 2) {
		lg(HIBR_LERR, "usage: web draw tab row col [h w] [-p pane]");
		return 2;
	}
	if (!wb_dp)
		wb_dp = (const dp_api *)hibr_require(s, "display", DP_API_VER);
	if (!wb_dp || !wb_dp->isopen()) {
		lg(HIBR_LERR, "web draw: no display open");
		return HIBR_FAIL;
	}
	if (v[2] < 0)
		v[2] = t->grows;
	if (v[3] < 0)
		v[3] = t->gcols;
	if (pane) {
		if (!wb_dp->prect || !wb_dp->prect(pane, &prow, &pcol, &ph, &pw)) {
			lg(HIBR_LERR, "web draw: no such pane: %s", pane);
			return HIBR_FAIL;
		}
		v[0] += prow;
		v[1] += pcol;
	} else {
		wb_dp->size(&ph, &pw);
	}
	return wb_draw(wb_dp, t, v[0], v[1], v[2], v[3], prow, pcol, ph, pw);
}

/* The usage line. */
void wb_usage(void)
{
	lg(HIBR_LERR, "usage: web open [url] | close|back|forward|reload|render|"
		      "url|title|text|links|loading|dirty|focus T | go T url | "
		      "wait T [ms] | size T rows cols | draw T row col [h w] "
		      "[-p pane] | click T row col | wheel T rows | key T name | "
		      "type T text | eval T js | tap|tapseek T | take T "
		      "[vfd|- [afd|-]] | tabs | fd | poll | quit");
}

/* web: a browser, headless Chromium driven over its own pipe. */
int m_web(sh *s, int ac, char **av)
{
	const char *sub = ac > 1 ? av[1] : "";
	wb_tab *t;
	str o;
	size_t i;
	int r;

	if (ac < 2) {
		wb_usage();
		return 2;
	}
	if (!strcmp(sub, "open")) {
		t = wb_tabopen(ac > 2 ? av[2] : "");
		if (!t)
			return HIBR_FAIL;
		wb_sayn(s, t->id);
		return HIBR_OK;
	}
	if (!strcmp(sub, "tabs")) {
		s_init(&o);
		for (i = 0; i < wb.tabs.n; i++) {
			t = wb.tabs.p[i];
			if (!t)
				continue;
			if (o.n)
				s_ch(&o, ' ');
			s_num(&o, t->id);
		}
		wb_say(s, o.p ? o.p : "");
		s_free(&o);
		return HIBR_OK;
	}
	if (!strcmp(sub, "fd")) {
		if (wb_start() != HIBR_OK)
			return HIBR_FAIL;
		wb_sayn(s, wb.out);
		return HIBR_OK;
	}
	if (!strcmp(sub, "poll")) {
		if (!wb_alive())
			return HIBR_FAIL;
		wb_events();
		for (i = 0; i < wb.tabs.n; i++) {
			t = wb.tabs.p[i];
			if (t && t->dirty)
				return HIBR_OK;
		}
		return HIBR_FAIL;
	}
	if (!strcmp(sub, "quit")) {
		wb_stop();
		return HIBR_OK;
	}
	if (ac < 3) {
		wb_usage();
		return 2;
	}
	t = wb_tabget(av[2]);
	if (!t)
		return HIBR_FAIL;
	if (!wb_alive()) {
		lg(HIBR_LERR, "web: the browser is not running");
		return HIBR_FAIL;
	}
	if (!strcmp(sub, "close")) {
		wb_tabclose(t);
		return HIBR_OK;
	}
	if (!strcmp(sub, "go")) {
		if (ac < 4) {
			wb_usage();
			return 2;
		}
		return wb_go(t, av[3]);
	}
	if (!strcmp(sub, "back"))
		return wb_hist(t, -1);
	if (!strcmp(sub, "forward"))
		return wb_hist(t, 1);
	if (!strcmp(sub, "reload"))
		return wb_hist(t, 0);
	if (!strcmp(sub, "wait"))
		return wb_wait(t, ac > 3 ? atol(av[3]) : 15000);
	if (!strcmp(sub, "size")) {
		if (ac < 5) {
			wb_usage();
			return 2;
		}
		return wb_size(t, atoi(av[3]), atoi(av[4]));
	}
	if (!strcmp(sub, "render")) {
		wb_events();
		return wb_render(t);
	}
	if (!strcmp(sub, "draw"))
		return wb_drawcmd(s, t, ac, av);
	if (!strcmp(sub, "click")) {
		if (ac < 5) {
			wb_usage();
			return 2;
		}
		return wb_click(t, atoi(av[3]), atoi(av[4]));
	}
	if (!strcmp(sub, "wheel"))
		return wb_wheel(t, ac > 3 ? atoi(av[3]) : 3);
	if (!strcmp(sub, "key")) {
		if (ac < 4) {
			wb_usage();
			return 2;
		}
		return wb_key(t, av[3]);
	}
	if (!strcmp(sub, "type")) {
		if (ac < 4) {
			wb_usage();
			return 2;
		}
		return wb_type(t, av[3]);
	}
	if (!strcmp(sub, "eval")) {
		if (ac < 4) {
			wb_usage();
			return 2;
		}
		s_init(&o);
		r = wb_eval(t, av[3], &o);
		if (r == HIBR_OK)
			wb_say(s, o.p ? o.p : "");
		s_free(&o);
		return r;
	}
	if (!strcmp(sub, "tap"))
		return wb_tap(t);
	if (!strcmp(sub, "tapseek"))
		return wb_tapseek(t);
	if (!strcmp(sub, "take"))
		return wb_take(s, t, ac > 3 && strcmp(av[3], "-") ? atoi(av[3]) : -1,
			       ac > 4 && strcmp(av[4], "-") ? atoi(av[4]) : -1);
	if (!strcmp(sub, "text"))
		return wb_text(s, t);
	if (!strcmp(sub, "links"))
		return wb_links(s, t);
	wb_events();
	if (!strcmp(sub, "url")) {
		wb_say(s, t->url.p ? t->url.p : "");
		return HIBR_OK;
	}
	if (!strcmp(sub, "title")) {
		s_init(&o);
		if (wb_eval(t, "document.title", &o) == HIBR_OK) {
			t->title.n = 0;
			s_cat(&t->title, o.p ? o.p : "");
		}
		s_free(&o);
		wb_say(s, t->title.p ? t->title.p : "");
		return HIBR_OK;
	}
	if (!strcmp(sub, "loading"))
		return t->loading ? HIBR_OK : HIBR_FAIL;
	if (!strcmp(sub, "dirty"))
		return t->dirty ? HIBR_OK : HIBR_FAIL;
	if (!strcmp(sub, "focus"))
		return t->focus ? HIBR_OK : HIBR_FAIL;
	lg(HIBR_LERR, "web: no subcommand %s", sub);
	return 2;
}

/* No browser yet: nothing open. */
int wb_ini(sh *s)
{
	(void)s;
	wb.in = wb.out = -1;
	wb.pid = 0;
	return HIBR_OK;
}

/* Stop the browser when the module goes. */
void wb_fini(sh *s)
{
	(void)s;
	wb_stop();
	wb_dp = 0;
}

const hibr_bi web_bi[] = {
	{ "web", m_web, "a browser: Chromium, headless, its pages drawn as cells" },
	HIBR_BI_END
};

HIBR_MODULE("web", "0.1", "a web browser: headless Chromium drawn as cells",
	    web_bi, wb_ini, wb_fini);
