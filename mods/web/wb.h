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
int wb_click(wb_tab *t, int row, int col);
int wb_wheel(wb_tab *t, int dy);
int wb_key(wb_tab *t, const char *key);
int wb_type(wb_tab *t, const char *text);
int wb_hist(wb_tab *t, int d);
int wb_draw(const dp_api *dp, wb_tab *t, int row, int col, int h, int w,
	    int prow, int pcol, int ph, int pw);
void wb_gridfree(wb_tab *t);
int wb_b64(const char *in, size_t n, str *out);
int wb_tap(wb_tab *t);
int wb_tapseek(wb_tab *t);
int wb_take(sh *s, wb_tab *t, int vfd, int afd);

#endif
