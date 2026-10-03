#define _GNU_SOURCE

#include "wb.h"
#include "../img/im.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* What a page is asked for each frame: every visible character with where
   it sits, its colour and weight and link; the form fields with what is in
   them; the links; where it is scrolled to. The page is laid out at
   WB_CW x WB_CH CSS pixels a cell, so a character's cell is its position
   divided by those. */
static const char wb_extract[] =
	"(function(CW,CH){"
	"const W=innerWidth,H=innerHeight;"
	"const o={t:[],l:[],f:[],u:location.href,ti:document.title,sx:scrollX,sy:scrollY};"
	"function col(c){const m=/rgba?\\(([\\d.]+),\\s*([\\d.]+),\\s*([\\d.]+)(?:,\\s*([\\d.]+))?/.exec(c||'');"
	"if(!m)return -1;if(m[4]!==undefined&&+m[4]<0.1)return -1;"
	"return ((+m[1]|0)<<16)|((+m[2]|0)<<8)|(+m[3]|0);}"
	"const lk=new Map();"
	"function li(el){const a=el.closest('a[href]');if(!a)return -1;let i=lk.get(a);"
	"if(i===undefined){i=o.l.length;lk.set(a,i);o.l.push([a.href,(a.innerText||'').trim().slice(0,200)]);}return i;}"
	"const root=document.body||document.documentElement;if(!root)return JSON.stringify(o);"
	"const w=document.createTreeWalker(root,NodeFilter.SHOW_TEXT);"
	"const r=document.createRange();let n;"
	"while(n=w.nextNode()){"
	"const s=n.textContent;if(!s.trim())continue;"
	"const el=n.parentElement;if(!el)continue;"
	"r.selectNodeContents(n);const bb=r.getBoundingClientRect();"
	"if(bb.bottom<0||bb.top>H||bb.right<0||bb.left>W||(bb.width==0&&bb.height==0))continue;"
	"const cs=getComputedStyle(el);"
	"if(cs.visibility!='visible'||+cs.opacity<0.05)continue;"
	"const fg=col(cs.color);"
	"const at=(+cs.fontWeight>=600?1:0)|(cs.fontStyle=='italic'?2:0)|"
	"((cs.textDecorationLine||'').indexOf('underline')>=0?4:0)|((cs.textDecorationLine||'').indexOf('line-through')>=0?8:0);"
	"const L=li(el);let run=null;let i=0;"
	"while(i<s.length){const cp=s.codePointAt(i);const k=cp>0xffff?2:1;"
	"r.setStart(n,i);r.setEnd(n,i+k);const q=r.getBoundingClientRect();i+=k;"
	"if(q.width==0||cp==10||cp==13||cp==9){continue;}"
	"const cx=Math.floor(q.left/CW+0.3),cy=Math.floor((q.top+q.height/2)/CH);"
	"if(cy<0||cy*CH>=H||cx<0||cx*CW>=W){run=null;continue;}"
	"const ch=String.fromCodePoint(cp);"
	"if(run&&run[1]==cy){run[2]+=ch;run[6]++;continue;}"
	"run=[cx,cy,ch,fg,at,L,1];o.t.push(run);}"
	"}"
	"for(const e of document.querySelectorAll('input,textarea,select')){"
	"const q=e.getBoundingClientRect();if(q.bottom<0||q.top>H||q.width==0||q.height==0)continue;"
	"const ty=(e.type||'').toLowerCase();if(ty=='hidden')continue;let v='',ph=0;"
	"if(e.tagName=='SELECT')v=((e.options[e.selectedIndex]||{}).text||'');"
	"else if(ty=='checkbox')v=e.checked?'[x]':'[ ]';"
	"else if(ty=='radio')v=e.checked?'(*)':'( )';"
	"else if(ty=='password')v='*'.repeat(e.value.length);"
	"else v=e.value||'';"
	"if(!v&&e.placeholder){v=e.placeholder;ph=1;}"
	"const cx=Math.floor(q.left/CW+0.3),cy=Math.floor((q.top+Math.min(q.height,CH)/2)/CH);"
	"o.f.push([cx,cy,Math.max(1,Math.round(q.width/CW)),v.replace(/\\n/g,' ').slice(0,1000),ph,"
	"document.activeElement===e?1:0,(ty=='checkbox'||ty=='radio'||ty=='submit'||ty=='button'||ty=='reset')?1:0]);}"
	"return JSON.stringify(o);})(%d,%d)";

/* The tab a handle names, or null with the reason said. */
wb_tab *wb_tabget(const char *id)
{
	size_t i;
	wb_tab *t;
	long n = strtol(id ? id : "", 0, 10);

	for (i = 0; i < wb.tabs.n; i++) {
		t = wb.tabs.p[i];
		if (t && t->id == n)
			return t;
	}
	lg(HIBR_LERR, "web: no tab %s", id ? id : "");
	return 0;
}

/* Free a tab's frame. */
void wb_gridfree(wb_tab *t)
{
	size_t i;

	free(t->grid);
	t->grid = 0;
	t->grows = t->gcols = 0;
	for (i = 0; i < t->links.n; i++)
		free(t->links.p[i]);
	v_free(&t->links);
}

/* Open a tab, at a page or a blank one; null with the reason said. */
wb_tab *wb_tabopen(const char *url)
{
	wb_tab *t;
	jv *r;
	str p;
	static int next = 1;

	if (wb_start() != HIBR_OK)
		return 0;
	r = wb_call("Target.createTarget", "{\"url\":\"about:blank\"}", 0);
	if (!r)
		return 0;
	t = xm(sizeof *t);
	memset(t, 0, sizeof *t);
	s_init(&t->url);
	s_init(&t->title);
	t->target = xs(jv_str(jv_path(r, "result.targetId")));
	jv_free(r);
	s_init(&p);
	s_cat(&p, "{\"targetId\":");
	jv_quote(&p, t->target);
	s_cat(&p, ",\"flatten\":true}");
	r = wb_call("Target.attachToTarget", p.p, 0);
	s_free(&p);
	if (!r) {
		free(t->target);
		free(t);
		return 0;
	}
	t->session = xs(jv_str(jv_path(r, "result.sessionId")));
	jv_free(r);
	t->id = next++;
	v_add(&wb.tabs, t);
	r = wb_call("Page.enable", "{}", t->session);
	jv_free(r);
	if (wb.ua) {
		s_init(&p);
		s_cat(&p, "{\"userAgent\":");
		jv_quote(&p, wb.ua);
		s_cat(&p, "}");
		r = wb_call("Emulation.setUserAgentOverride", p.p, t->session);
		s_free(&p);
		jv_free(r);
	}
	wb_size(t, 24, 80);
	if (url && *url)
		wb_go(t, url);
	return t;
}

/* Close a tab, and its page in the browser. */
void wb_tabclose(wb_tab *t)
{
	size_t i;
	str p;
	jv *r;

	if (wb_alive() && t->target) {
		s_init(&p);
		s_cat(&p, "{\"targetId\":");
		jv_quote(&p, t->target);
		s_cat(&p, "}");
		r = wb_call("Target.closeTarget", p.p, 0);
		jv_free(r);
		s_free(&p);
	}
	for (i = 0; i < wb.tabs.n; i++)
		if (wb.tabs.p[i] == t)
			wb.tabs.p[i] = 0;
	wb_gridfree(t);
	free(t->target);
	free(t->session);
	s_free(&t->url);
	s_free(&t->title);
	free(t);
}

/* Go to an address. */
int wb_go(wb_tab *t, const char *url)
{
	str p;
	jv *r;
	const char *err;

	s_init(&p);
	s_cat(&p, "{\"url\":");
	jv_quote(&p, url);
	s_cat(&p, "}");
	r = wb_call("Page.navigate", p.p, t->session);
	s_free(&p);
	if (!r)
		return HIBR_FAIL;
	err = jv_str(jv_path(r, "result.errorText"));
	if (*err) {
		lg(HIBR_LDBG, "web: %s: %s", url, err);
		t->url.n = 0;
		s_cat(&t->url, url);
	}
	t->loading = 1;
	t->dirty = 1;
	jv_free(r);
	return HIBR_OK;
}

/* Lay a tab's page out at rows by cols cells. */
int wb_size(wb_tab *t, int rows, int cols)
{
	char b[160];
	jv *r;

	if (rows < 1 || cols < 1)
		return HIBR_FAIL;
	if (rows == t->rows && cols == t->cols)
		return HIBR_OK;
	snprintf(b, sizeof b, "{\"width\":%d,\"height\":%d,\"deviceScaleFactor\":1,"
		 "\"mobile\":false}", cols * WB_CW, rows * WB_CH);
	r = wb_call("Emulation.setDeviceMetricsOverride", b, t->session);
	if (!r)
		return HIBR_FAIL;
	jv_free(r);
	t->rows = rows;
	t->cols = cols;
	t->dirty = 1;
	return HIBR_OK;
}

/* Run JavaScript in a tab's page; its value as text into out -- a string
   as it is, anything else as JSON. */
int wb_eval(wb_tab *t, const char *js, str *out)
{
	str p;
	jv *r, *v, *x;

	s_init(&p);
	s_cat(&p, "{\"expression\":");
	jv_quote(&p, js);
	s_cat(&p, ",\"returnByValue\":true,\"awaitPromise\":true}");
	r = wb_call("Runtime.evaluate", p.p, t->session);
	s_free(&p);
	if (!r)
		return HIBR_FAIL;
	x = jv_path(r, "result.exceptionDetails");
	if (x) {
		v = jv_path(x, "exception.description");
		lg(HIBR_LERR, "web: %s", v ? jv_str(v) : jv_str(jv_get(x, "text")));
		jv_free(r);
		return HIBR_FAIL;
	}
	v = jv_path(r, "result.result.value");
	if (v && v->t == JV_STR)
		s_cat(out, v->s.p ? v->s.p : "");
	else if (v)
		jv_write(out, v);
	jv_free(r);
	return HIBR_OK;
}

/* A colour as the display takes it. */
unsigned wb_rgb(int r, int g, int b)
{
	return DP_RGB | ((unsigned)r << 16) | ((unsigned)g << 8) | (unsigned)b;
}

/* Decode a UTF-8 character; its code point, and how long it was in *n. */
unsigned wb_utf8(const char *s, int *n)
{
	const unsigned char *u = (const unsigned char *)s;

	if (u[0] < 0x80) {
		*n = 1;
		return u[0];
	}
	if ((u[0] & 0xE0) == 0xC0 && u[1]) {
		*n = 2;
		return ((unsigned)(u[0] & 0x1F) << 6) | (u[1] & 0x3F);
	}
	if ((u[0] & 0xF0) == 0xE0 && u[1] && u[2]) {
		*n = 3;
		return ((unsigned)(u[0] & 0x0F) << 12) | ((unsigned)(u[1] & 0x3F) << 6) |
		       (u[2] & 0x3F);
	}
	if ((u[0] & 0xF8) == 0xF0 && u[1] && u[2] && u[3]) {
		*n = 4;
		return ((unsigned)(u[0] & 0x07) << 18) | ((unsigned)(u[1] & 0x3F) << 12) |
		       ((unsigned)(u[2] & 0x3F) << 6) | (u[3] & 0x3F);
	}
	*n = 1;
	return 0xFFFD;
}

/* Make the page's text invisible, or visible again, so the picture behind
   it has its backgrounds and images and no letters drawn into them. */
void wb_hidetext(wb_tab *t, int on)
{
	str o;

	s_init(&o);
	wb_eval(t, on ?
		"(()=>{let s=document.getElementById('__hibr_hide');"
		"if(!s){s=document.createElement('style');s.id='__hibr_hide';"
		"s.textContent='*,*::before,*::after,input,textarea{color:transparent!important;"
		"-webkit-text-fill-color:transparent!important;text-shadow:none!important;"
		"caret-color:transparent!important}';}"
		"(document.head||document.documentElement).appendChild(s);return 1})()" :
		"(()=>{const s=document.getElementById('__hibr_hide');"
		"if(s)s.remove();return 1})()", &o);
	s_free(&o);
}

/* The pixels behind the page: a screenshot at one pixel a column and two a
   row, each cell a half block of its two. */
void wb_pixels(wb_tab *t)
{
	char b[200];
	jv *r;
	str png, err;
	image im;
	int row, col;
	wb_cell *c;
	unsigned char *top, *bot;

	snprintf(b, sizeof b, "{\"format\":\"png\",\"clip\":{\"x\":%ld,\"y\":%ld,"
		 "\"width\":%d,\"height\":%d,\"scale\":%.6f}}", t->sx, t->sy,
		 t->cols * WB_CW, t->rows * WB_CH, 1.0 / WB_CW);
	wb_hidetext(t, 1);
	r = wb_call("Page.captureScreenshot", b, t->session);
	wb_hidetext(t, 0);
	if (!r)
		return;
	s_init(&png);
	s_init(&err);
	memset(&im, 0, sizeof im);
	if (wb_b64(jv_str(jv_path(r, "result.data")),
		   strlen(jv_str(jv_path(r, "result.data"))), &png) == HIBR_OK &&
	    im_pngmem((const unsigned char *)png.p, png.n, &im, &err) == HIBR_OK) {
		for (row = 0; row < t->rows; row++)
			for (col = 0; col < t->cols; col++) {
				c = t->grid + (size_t)row * t->cols + col;
				if (col >= im.w || row * 2 >= im.h)
					continue;
				top = im.px + ((size_t)(row * 2) * im.w + col) * 3;
				bot = row * 2 + 1 < im.h ? top + (size_t)im.w * 3 : top;
				c->fg = wb_rgb(top[0], top[1], top[2]);
				c->bg = wb_rgb(bot[0], bot[1], bot[2]);
				c->cp = memcmp(top, bot, 3) ? 0x2580 : ' ';
			}
	} else {
		lg(HIBR_LDBG, "web: the page's picture: %s", err.p ? err.p : "?");
	}
	free(im.px);
	s_free(&png);
	s_free(&err);
	jv_free(r);
}

/* The colour a cell's text sits on: the average of its two halves. */
unsigned wb_under(wb_cell *c)
{
	unsigned a = c->fg & 0xFFFFFF, b = c->bg & 0xFFFFFF;

	if (c->cp == ' ')
		return c->bg;
	return wb_rgb((int)((((a >> 16) & 255) + ((b >> 16) & 255)) / 2),
		      (int)((((a >> 8) & 255) + ((b >> 8) & 255)) / 2),
		      (int)(((a & 255) + (b & 255)) / 2));
}

/* A colour far enough from a background to be read on it. */
unsigned wb_legible(unsigned fg, unsigned bg)
{
	int fr = (fg >> 16) & 255, fgg = (fg >> 8) & 255, fb = fg & 255;
	int br = (bg >> 16) & 255, bgg = (bg >> 8) & 255, bb = bg & 255;
	int d = abs(fr - br) + abs(fgg - bgg) + abs(fb - bb);
	int lum = br * 3 + bgg * 6 + bb;

	if (d >= 120)
		return fg;
	return lum > 1280 ? wb_rgb(0, 0, 0) : wb_rgb(255, 255, 255);
}

int ed_width(const char *p);

/* How many columns a character takes: two for a wide one, none for one
   that combines with the last. */
int wb_width(const char *s, int n)
{
	char b[8];

	memcpy(b, s, (size_t)n);
	b[n] = 0;
	return ed_width(b);
}

/* Put text at a cell and on, a character to a cell or two to a wide one,
   in a colour and an attribute, over what is there. */
void wb_puttext(wb_tab *t, int col, int row, const char *s, long fg,
		unsigned attr, int link, int max)
{
	wb_cell *c;
	unsigned cp, bg;
	int n, k = 0, cw;

	if (row < 0 || row >= t->rows)
		return;
	while (*s && col < t->cols && (max < 0 || k < max)) {
		cp = wb_utf8(s, &n);
		cw = wb_width(s, n);
		s += n;
		if (cw == 0)
			continue;
		if (cw == 2 && col + 1 >= t->cols)
			break;
		if (col >= 0) {
			c = t->grid + (size_t)row * t->cols + col;
			bg = wb_under(c);
			c->cp = cp;
			c->bg = bg;
			c->fg = fg < 0 ? wb_legible(wb_rgb(0, 0, 0), bg) :
				wb_legible(wb_rgb((int)((fg >> 16) & 255),
						  (int)((fg >> 8) & 255),
						  (int)(fg & 255)), bg);
			c->attr = attr;
			c->link = link;
			if (cw == 2) {
				c[1] = c[0];
				c[1].cp = WB_CONT;
			}
		}
		col += cw;
		k += cw;
	}
}

/* Draw a tab's page into its frame: the pixels, then the text and the
   form fields over them. */
int wb_render(wb_tab *t)
{
	char *js;
	str out;
	jv *v, *a, *e;
	size_t i, n;
	unsigned at;
	int k, *rs, *re;

	if (t->rows < 1 || t->cols < 1)
		return HIBR_FAIL;
	js = xm(sizeof wb_extract + 32);
	snprintf(js, sizeof wb_extract + 32, wb_extract, WB_CW, WB_CH);
	s_init(&out);
	k = wb_eval(t, js, &out);
	free(js);
	if (k != HIBR_OK) {
		s_free(&out);
		return HIBR_FAIL;
	}
	v = jv_parse(out.p ? out.p : "", out.n);
	s_free(&out);
	if (!v)
		return HIBR_FAIL;
	t->sx = (long)jv_num(jv_get(v, "sx"));
	t->sy = (long)jv_num(jv_get(v, "sy"));
	t->url.n = 0;
	s_cat(&t->url, jv_str(jv_get(v, "u")));
	t->title.n = 0;
	s_cat(&t->title, jv_str(jv_get(v, "ti")));
	wb_gridfree(t);
	n = (size_t)t->rows * t->cols;
	t->grid = xm(n * sizeof *t->grid);
	for (i = 0; i < n; i++) {
		t->grid[i].cp = ' ';
		t->grid[i].fg = wb_rgb(0, 0, 0);
		t->grid[i].bg = wb_rgb(255, 255, 255);
		t->grid[i].attr = 0;
		t->grid[i].link = -1;
	}
	t->grows = t->rows;
	t->gcols = t->cols;
	wb_pixels(t);
	a = jv_get(v, "l");
	for (i = 0; a && i < a->v.n; i++)
		v_add(&t->links, xs(jv_str(jv_path(a->v.p[i], "0"))));
	a = jv_get(v, "t");
	rs = xm(sizeof *rs * (size_t)t->rows);
	re = xm(sizeof *re * (size_t)t->rows);
	for (k = 0; k < t->rows; k++)
		rs[k] = re[k] = -1;
	for (i = 0; a && i < a->v.n; i++) {
		int cx, cy, len;

		e = a->v.p[i];
		k = (int)jv_num(jv_path(e, "4"));
		at = (k & 1 ? DP_BOLD : 0) | (k & 2 ? DP_ITAL : 0) |
		     (k & 4 ? DP_UNDER : 0) | (k & 8 ? DP_STRIKE : 0);
		cx = (int)jv_num(jv_path(e, "0"));
		cy = (int)jv_num(jv_path(e, "1"));
		len = (int)jv_num(jv_path(e, "6"));
		if (cy >= 0 && cy < t->rows) {
			if (rs[cy] >= 0 && cx >= rs[cy] && cx < re[cy])
				cx = re[cy];
			if (rs[cy] < 0 || cx < rs[cy])
				rs[cy] = cx;
			if (cx + len > re[cy])
				re[cy] = cx + len;
		}
		wb_puttext(t, cx, cy, jv_str(jv_path(e, "2")),
			   (long)jv_num(jv_path(e, "3")), at,
			   (int)jv_num(jv_path(e, "5")), -1);
	}
	free(rs);
	free(re);
	a = jv_get(v, "f");
	t->focus = 0;
	for (i = 0; a && i < a->v.n; i++) {
		int cx, cy, w, ph, fo, btn, j;
		const char *val;

		e = a->v.p[i];
		cx = (int)jv_num(jv_path(e, "0"));
		cy = (int)jv_num(jv_path(e, "1"));
		w = (int)jv_num(jv_path(e, "2"));
		val = jv_str(jv_path(e, "3"));
		ph = (int)jv_num(jv_path(e, "4"));
		fo = (int)jv_num(jv_path(e, "5"));
		btn = (int)jv_num(jv_path(e, "6"));
		if (fo)
			t->focus = 1;
		if (!btn)
			for (j = 0; j < w; j++)
				wb_puttext(t, cx + j, cy, " ", 0x000000, DP_UNDER, -1, 1);
		wb_puttext(t, cx, cy, val, ph ? 0x808080 : 0x000000,
			   (btn ? 0 : DP_UNDER) | (fo ? DP_BOLD : 0), -1,
			   btn ? -1 : w);
		if (fo && !btn) {
			int vl = 0, nn;
			const char *q = val;

			while (*q && !ph) {
				wb_utf8(q, &nn);
				q += nn;
				vl++;
			}
			if (vl < w && cx + vl >= 0 && cx + vl < t->cols && cy >= 0 &&
			    cy < t->rows)
				t->grid[(size_t)cy * t->cols + cx + vl].attr |= DP_REV;
		}
	}
	jv_free(v);
	t->dirty = 0;
	return HIBR_OK;
}

/* A click on a cell, as the browser takes one: pressed and let go at the
   middle of the cell. */
int wb_click(wb_tab *t, int row, int col)
{
	char b[200];
	jv *r;
	int x = col * WB_CW + WB_CW / 2, y = row * WB_CH + WB_CH / 2;

	snprintf(b, sizeof b, "{\"type\":\"mousePressed\",\"x\":%d,\"y\":%d,"
		 "\"button\":\"left\",\"clickCount\":1}", x, y);
	r = wb_call("Input.dispatchMouseEvent", b, t->session);
	jv_free(r);
	snprintf(b, sizeof b, "{\"type\":\"mouseReleased\",\"x\":%d,\"y\":%d,"
		 "\"button\":\"left\",\"clickCount\":1}", x, y);
	r = wb_call("Input.dispatchMouseEvent", b, t->session);
	jv_free(r);
	t->dirty = 1;
	return HIBR_OK;
}

/* The wheel, dy rows down (up when negative), over the middle of the page. */
int wb_wheel(wb_tab *t, int dy)
{
	char b[200];
	jv *r;

	snprintf(b, sizeof b, "{\"type\":\"mouseWheel\",\"x\":%d,\"y\":%d,"
		 "\"deltaX\":0,\"deltaY\":%d}", t->cols * WB_CW / 2,
		 t->rows * WB_CH / 2, dy * WB_CH);
	r = wb_call("Input.dispatchMouseEvent", b, t->session);
	jv_free(r);
	t->dirty = 1;
	return HIBR_OK;
}

/* One key event of a type, with its name, code and number. */
void wb_keyev(wb_tab *t, const char *type, const char *key, const char *code,
	      int vk, const char *text, int mods)
{
	str p;
	jv *r;

	s_init(&p);
	s_cat(&p, "{\"type\":");
	jv_quote(&p, type);
	s_cat(&p, ",\"key\":");
	jv_quote(&p, key);
	s_cat(&p, ",\"code\":");
	jv_quote(&p, code);
	s_cat(&p, ",\"windowsVirtualKeyCode\":");
	s_num(&p, vk);
	s_cat(&p, ",\"nativeVirtualKeyCode\":");
	s_num(&p, vk);
	s_cat(&p, ",\"modifiers\":");
	s_num(&p, mods);
	if (text && *text) {
		s_cat(&p, ",\"text\":");
		jv_quote(&p, text);
		s_cat(&p, ",\"unmodifiedText\":");
		jv_quote(&p, text);
	}
	s_cat(&p, "}");
	r = wb_call("Input.dispatchKeyEvent", p.p, t->session);
	jv_free(r);
	s_free(&p);
}

/* A key by the name the console gives it -- enter, backspace, up, shift-tab,
   a letter -- pressed and let go in the page. */
int wb_key(wb_tab *t, const char *key)
{
	static const struct { const char *nm, *key, *code, *text; int vk; } ks[] = {
		{ "enter", "Enter", "Enter", "\r", 13 },
		{ "backspace", "Backspace", "Backspace", 0, 8 },
		{ "tab", "Tab", "Tab", 0, 9 },
		{ "escape", "Escape", "Escape", 0, 27 },
		{ "delete", "Delete", "Delete", 0, 46 },
		{ "up", "ArrowUp", "ArrowUp", 0, 38 },
		{ "down", "ArrowDown", "ArrowDown", 0, 40 },
		{ "left", "ArrowLeft", "ArrowLeft", 0, 37 },
		{ "right", "ArrowRight", "ArrowRight", 0, 39 },
		{ "home", "Home", "Home", 0, 36 },
		{ "end", "End", "End", 0, 35 },
		{ "pageup", "PageUp", "PageUp", 0, 33 },
		{ "pagedown", "PageDown", "PageDown", 0, 34 },
		{ " ", " ", "Space", " ", 32 },
		{ 0, 0, 0, 0, 0 }
	};
	int mods = 0, i, n;
	const char *k = key;
	char one[8];

	for (;;) {
		if (!strncmp(k, "shift-", 6)) {
			mods |= 8;
			k += 6;
		} else if (!strncmp(k, "ctrl-", 5)) {
			mods |= 2;
			k += 5;
		} else if (!strncmp(k, "alt-", 4)) {
			mods |= 1;
			k += 4;
		} else {
			break;
		}
	}
	for (i = 0; ks[i].nm; i++)
		if (!strcmp(k, ks[i].nm)) {
			wb_keyev(t, ks[i].text && !mods ? "keyDown" : "rawKeyDown",
				 ks[i].key, ks[i].code, ks[i].vk,
				 mods ? 0 : ks[i].text, mods);
			wb_keyev(t, "keyUp", ks[i].key, ks[i].code, ks[i].vk, 0, mods);
			t->dirty = 1;
			return HIBR_OK;
		}
	wb_utf8(k, &n);
	if (k[n] || !*k) {
		lg(HIBR_LERR, "web: no key %s", key);
		return HIBR_FAIL;
	}
	memcpy(one, k, (size_t)n);
	one[n] = 0;
	if (mods & 3) {
		int vk = (one[0] >= 'a' && one[0] <= 'z') ? one[0] - 32 : one[0];
		wb_keyev(t, "rawKeyDown", one, "", vk, 0, mods);
		wb_keyev(t, "keyUp", one, "", vk, 0, mods);
	} else {
		wb_keyev(t, "keyDown", one, "", 0, one, mods);
		wb_keyev(t, "keyUp", one, "", 0, 0, mods);
	}
	t->dirty = 1;
	return HIBR_OK;
}

/* Text typed into whatever has the focus. */
int wb_type(wb_tab *t, const char *text)
{
	str p;
	jv *r;

	s_init(&p);
	s_cat(&p, "{\"text\":");
	jv_quote(&p, text);
	s_cat(&p, "}");
	r = wb_call("Input.insertText", p.p, t->session);
	s_free(&p);
	if (!r)
		return HIBR_FAIL;
	jv_free(r);
	t->dirty = 1;
	return HIBR_OK;
}

/* Back (d -1) or forward (d 1) in a tab's history, or reload (d 0). */
int wb_hist(wb_tab *t, int d)
{
	jv *r, *ents;
	int cur, to;
	char b[64];

	if (!d) {
		r = wb_call("Page.reload", "{}", t->session);
		if (!r)
			return HIBR_FAIL;
		jv_free(r);
		t->loading = 1;
		t->dirty = 1;
		return HIBR_OK;
	}
	r = wb_call("Page.getNavigationHistory", "{}", t->session);
	if (!r)
		return HIBR_FAIL;
	cur = (int)jv_num(jv_path(r, "result.currentIndex"));
	ents = jv_path(r, "result.entries");
	to = cur + d;
	if (!ents || to < 0 || (size_t)to >= ents->v.n) {
		jv_free(r);
		return HIBR_FAIL;
	}
	snprintf(b, sizeof b, "{\"entryId\":%d}",
		 (int)jv_num(jv_get(ents->v.p[to], "id")));
	jv_free(r);
	r = wb_call("Page.navigateToHistoryEntry", b, t->session);
	if (!r)
		return HIBR_FAIL;
	jv_free(r);
	t->loading = 1;
	t->dirty = 1;
	return HIBR_OK;
}

/* Base64 into bytes. */
int wb_b64(const char *in, size_t n, str *out)
{
	unsigned v = 0;
	int bits = 0;
	size_t i;
	int d;
	char c;

	for (i = 0; i < n; i++) {
		c = in[i];
		if (c >= 'A' && c <= 'Z')
			d = c - 'A';
		else if (c >= 'a' && c <= 'z')
			d = c - 'a' + 26;
		else if (c >= '0' && c <= '9')
			d = c - '0' + 52;
		else if (c == '+')
			d = 62;
		else if (c == '/')
			d = 63;
		else if (c == '=' || c == '\n' || c == '\r')
			continue;
		else
			return HIBR_FAIL;
		v = (v << 6) | (unsigned)d;
		bits += 6;
		if (bits >= 8) {
			bits -= 8;
			s_ch(out, (int)((v >> bits) & 255));
		}
	}
	return HIBR_OK;
}
