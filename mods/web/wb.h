#ifndef HIBR_WB_H
#define HIBR_WB_H

#include "hibr.h"
#include "../display.h"
#include <stddef.h>
#include <sys/types.h>

/* The size of a cell, in the page's own CSS pixels: the viewport is a
   window's columns times WB_CW wide and its rows times WB_CH tall, and a
   screenshot taken at 1/WB_CW is exactly one pixel a column and two a row,
   one for each half of a half-block cell. */
#ifndef WB_CW
#define WB_CW 8
#define WB_CH 16
#endif

/* How long a reply may take before a call gives up, in milliseconds. */
#ifndef WB_WAITMS
#define WB_WAITMS 8000
#endif
/* How long after a browser stopped answering before another is started. */
#ifndef WB_RESTMS
#define WB_RESTMS 30000
#endif

enum { JV_NULL, JV_BOOL, JV_NUM, JV_STR, JV_ARR, JV_OBJ };

typedef struct jv jv;
struct jv {
	int t;
	double n;
	str s;
	vec k, v;
};

/* The second cell of a character two columns wide: drawn by the first. */
#ifndef WB_CONT
#define WB_CONT 0xFFFFFFFEu
#endif

/* One cell of a rendered page. */
typedef struct wb_cell wb_cell;
struct wb_cell {
	unsigned cp, fg, bg, attr;
	int link;
};

/* A tab: a page target in the browser, attached on a session of its own. */
typedef struct wb_tab wb_tab;
struct wb_tab {
	int id, rows, cols, loading, dirty, focus;
	char *target, *session;
	str url, title;
	wb_cell *grid;
	int grows, gcols;
	vec links;
	long sx, sy;
	/* The page as pixels, for a terminal that can paint them: a
	   screenshot at the viewport's own size, kept from one render to the
	   next. A page drawn this way is the page -- real fonts, pictures,
	   layout -- and has no text layer at all, because a text cell paints
	   its own background and would box out the bitmap behind every glyph
	   (ADR 0037). pixels is set by `web mode ID pixels`. */
	int pixels;
	unsigned char *shot;
	int shotw, shoth;
	/* Set by `web set` and `web mode`: the page's zoom in percent (0 is
	   100), whether a zoom style is on the page now, whether image
	   requests are refused, and text only -- a render that reads nothing,
	   since the caller draws the page's text itself. */
	int zoom, zoomed, noimg, textonly;
	/* Gitea #285: the languages the pages are asked for (null for the
	   browser's own) and whether the user agent override has carried
	   one, so clearing it is sent too; the user agent `web set ua` gave;
	   whether the network domain is on, which the extra headers and the
	   blocked addresses both need; and the scripts added to every new
	   document for Do Not Track and for notifications, kept by the
	   identifiers the browser gave them so they can be taken off. */
	char *lang, *uaset, *dntjs, *notejs;
	int langsent, netup;
	/* Gitea #289, #292: how tall the page is and how much of it shows,
	   from the last render, for a scroll bar; and live, a screencast
	   kept running only as the signal that the page has repainted, with
	   repaints a render itself causes kept quiet until hushto. */
	long sh, vh, hushto;
	int live, hush;
	/* Gitea #305: when the tab's sound is recorded (`web sound`: 0 off,
	   1 at the first press, 2 as each page loads), the script that says
	   so to each new document, and sndgo, a page loaded with snd 2 whose
	   recording the next take starts -- not the event, which arrives
	   inside another call's wait. */
	int snd, sndgo;
	char *sndjs;
};

/* The browser: one Chromium for the whole shell, driven over a pipe. */
typedef struct wb_br wb_br;
struct wb_br {
	pid_t pid;
	int in, out, next;
	str buf;
	vec tabs;
	vec events;
	char *profile;
	int tmpprofile;
	char *ua;
	/* `web config`: what the next start runs, over the environment. */
	char *cfbin, *cfprof, *cfargs;
	/* Downloads (Gitea #285): where they go, each one begun as its guid
	   and name in turn, and each one ended as a line for `web
	   downloaded`; and a page's notifications as lines for `web notes`.
	   Browser-wide, since that is what the protocol offers. */
	char *dldir;
	vec dlbegun, dlended, notes;
	/* A browser that stops answering is stopped: stuck counts calls in a
	   row that got no answer (two, or one at the browser's own level, end
	   it), and failat is when it was stopped or failed to start, so no
	   other is started for WB_RESTMS -- every call meanwhile fails at
	   once, where each used to wait WB_WAITMS and a hung browser held the
	   desktop's every frame for seconds (owner, 2026-10-20). */
	int stuck;
	long failat;
};

extern wb_br wb;

jv *jv_parse(const char *p, size_t n);
void jv_free(jv *v);
jv *jv_get(jv *o, const char *k);
jv *jv_path(jv *o, const char *path);
const char *jv_str(jv *v);
double jv_num(jv *v);
void jv_quote(str *o, const char *p);
void jv_write(str *o, jv *v);

int wb_start(void);
void wb_stop(void);
void wb_useragent(void);
int wb_alive(void);
jv *wb_call(const char *method, const char *params, const char *session);
int wb_send(const char *method, const char *params, const char *session);
int wb_pump(int ms);
int wb_events(void);

wb_tab *wb_tabget(const char *id);
wb_tab *wb_tabopen(const char *url);
void wb_tabclose(wb_tab *t);
int wb_go(wb_tab *t, const char *url);
int wb_size(wb_tab *t, int rows, int cols);
int wb_render(wb_tab *t);
int wb_eval(wb_tab *t, const char *js, str *out);
int wb_evalg(wb_tab *t, const char *js, str *out, int gesture);
int wb_sndguard(wb_tab *t);
int wb_sound(wb_tab *t, const char *mode);
int wb_docjs(wb_tab *t, char **slot, const char *js);
int wb_click(wb_tab *t, int row, int col);
int wb_wheel(wb_tab *t, int dy);
int wb_key(wb_tab *t, const char *key);
int wb_type(wb_tab *t, const char *text);
int wb_hist(wb_tab *t, int d);
int wb_draw(sh *s, const dp_api *dp, wb_tab *t, int row, int col, int h, int w,
	    int prow, int pcol, int ph, int pw);
void wb_gridfree(wb_tab *t);
int wb_b64(const char *in, size_t n, str *out);
int wb_tap(wb_tab *t);
int wb_tapseek(wb_tab *t);
int wb_take(sh *s, wb_tab *t, int vfd, int afd);
int wb_set(wb_tab *t, const char *k, const char *v);
int wb_setua(wb_tab *t, const char *ua);
void wb_imgpaused(wb_tab *t, jv *m);
void wb_zoomapply(wb_tab *t);
int wb_config(const char *k, const char *v);
int wb_clear(int ac, char **av);
int wb_permit(const char *name, const char *how);
int wb_downloads(const char *dir);
void wb_drain(sh *s, vec *q);
void wb_dlevent(const char *meth, jv *m);
void wb_noteevent(jv *m);
int wb_pdf(wb_tab *t, const char *path);

#endif
