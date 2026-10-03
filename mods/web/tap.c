#define _GNU_SOURCE

#include "wb.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* The tap: run in the page before its own scripts, it notes every buffer
   a media source makes -- video or audio by its type -- and copies every
   chunk appended to one into a queue the shell takes from. A buffer's
   first chunk is kept as its init segment, for after a seek; a new media
   source (a new video, an ad break) queues a reset, and only the newest
   source's chunks are kept. */
static const char wb_tapjs[] =
	"(function(){if(window.__hibrTap)return;"
	"var T=window.__hibrTap={q:[],gen:0,init:{}};"
	"var add=MediaSource.prototype.addSourceBuffer;"
	"MediaSource.prototype.addSourceBuffer=function(m){"
	"var sb=add.call(this,m),k=/^audio/i.test(m)?'a':'v';"
	"if(this.__hibrGen===undefined){this.__hibrGen=++T.gen;T.init={};"
	"T.q=[['r','']];}"
	"sb.__hibrKind=k;sb.__hibrGen=this.__hibrGen;sb.__hibrFirst=1;"
	"T.q.push(['m'+k,m]);return sb};"
	"var app=SourceBuffer.prototype.appendBuffer;"
	"SourceBuffer.prototype.appendBuffer=function(d){try{"
	"if(this.__hibrKind&&this.__hibrGen===T.gen){"
	"var u=d instanceof ArrayBuffer?new Uint8Array(d):"
	"new Uint8Array(d.buffer,d.byteOffset,d.byteLength);u=u.slice(0);"
	"if(this.__hibrFirst){this.__hibrFirst=0;T.init[this.__hibrKind]=u}"
	"T.q.push([this.__hibrKind,u])}}catch(e){}"
	"return app.call(this,d)};"
	"window.__hibrSeek=function(){T.q=[];"
	"if(T.init.v)T.q.push(['v',T.init.v]);"
	"if(T.init.a)T.q.push(['a',T.init.a]);return 'ok'};"
	"window.__hibrTake=function(){var o=[],i,e,b,s,j;"
	"for(i=0;i<T.q.length;i++){e=T.q[i];"
	"if(typeof e[1]==='string'){o.push(e);continue}"
	"b=e[1];s='';for(j=0;j<b.length;j+=32768)"
	"s+=String.fromCharCode.apply(null,b.subarray(j,j+32768));"
	"o.push([e[0],btoa(s)])}"
	"T.q=[];return JSON.stringify(o)}})()";

/* Install the tap in a tab: on every page it loads from now on, before
   that page's own scripts, and in the page there now. */
int wb_tap(wb_tab *t)
{
	str p, o;
	jv *r;

	s_init(&p);
	s_cat(&p, "{\"source\":");
	jv_quote(&p, wb_tapjs);
	s_cat(&p, "}");
	r = wb_call("Page.addScriptToEvaluateOnNewDocument", p.p, t->session);
	s_free(&p);
	if (!r)
		return HIBR_FAIL;
	jv_free(r);
	s_init(&o);
	wb_eval(t, wb_tapjs, &o);
	s_free(&o);
	return HIBR_OK;
}

/* After a seek: drop what was queued and queue each buffer's init segment
   again, so what is taken next can be read from its start. */
int wb_tapseek(wb_tab *t)
{
	str o;
	int r;

	s_init(&o);
	r = wb_eval(t, "window.__hibrSeek ? __hibrSeek() : 'none'", &o);
	if (r == HIBR_OK && strcmp(o.p ? o.p : "", "ok")) {
		lg(HIBR_LERR, "web tapseek: the tab has no tap");
		r = HIBR_FAIL;
	}
	s_free(&o);
	return r;
}

/* Write all of a buffer to a descriptor. */
int wb_wall(int fd, const char *p, size_t n)
{
	ssize_t w;

	while (n) {
		w = write(fd, p, n);
		if (w < 0 && errno == EINTR)
			continue;
		if (w <= 0)
			return HIBR_FAIL;
		p += w;
		n -= (size_t)w;
	}
	return HIBR_OK;
}

/* Take what the tap has queued: video chunks to vfd, audio to afd, and
   say what happened -- bytes of each, a reset, the types -- as words:
   "v N a N reset 0|1 vtype T atype T". */
int wb_take(sh *s, wb_tab *t, int vfd, int afd)
{
	str o, b, sum, vt, at;
	jv *a, *e;
	size_t i, nv = 0, na = 0;
	const char *k;
	int reset = 0, rc = HIBR_OK;

	s_init(&o);
	if (wb_eval(t, "window.__hibrTake ? __hibrTake() : '[]'", &o) != HIBR_OK) {
		s_free(&o);
		return HIBR_FAIL;
	}
	a = jv_parse(o.p ? o.p : "[]", o.n);
	s_free(&o);
	if (!a)
		return HIBR_FAIL;
	s_init(&b);
	s_init(&vt);
	s_init(&at);
	for (i = 0; i < a->v.n; i++) {
		e = a->v.p[i];
		k = jv_str(jv_path(e, "0"));
		if (!strcmp(k, "r")) {
			reset = 1;
			nv = na = 0;
			continue;
		}
		if (!strcmp(k, "mv") || !strcmp(k, "ma")) {
			str *d = k[1] == 'v' ? &vt : &at;

			const char *m = jv_str(jv_path(e, "1"));

			d->n = 0;
			s_add(d, m, strcspn(m, "; "));
			continue;
		}
		b.n = 0;
		if (wb_b64(jv_str(jv_path(e, "1")), strlen(jv_str(jv_path(e, "1"))),
			   &b) != HIBR_OK)
			continue;
		if (k[0] == 'v' && vfd >= 0) {
			if (wb_wall(vfd, b.p ? b.p : "", b.n) != HIBR_OK)
				rc = HIBR_FAIL;
			nv += b.n;
		} else if (k[0] == 'a' && afd >= 0) {
			if (wb_wall(afd, b.p ? b.p : "", b.n) != HIBR_OK)
				rc = HIBR_FAIL;
			na += b.n;
		}
	}
	jv_free(a);
	s_init(&sum);
	s_cat(&sum, "v ");
	s_num(&sum, (long)nv);
	s_cat(&sum, " a ");
	s_num(&sum, (long)na);
	s_cat(&sum, " reset ");
	s_num(&sum, reset);
	s_cat(&sum, " vtype ");
	s_cat(&sum, vt.n ? vt.p : "-");
	s_cat(&sum, " atype ");
	s_cat(&sum, at.n ? at.p : "-");
	hibr_ret(s, sum.p);
	if (!s->bind)
		printf("%s\n", sum.p);
	if (rc != HIBR_OK)
		lg(HIBR_LERR, "web take: could not write what was taken: %s",
		   strerror(errno));
	s_free(&sum);
	s_free(&b);
	s_free(&vt);
	s_free(&at);
	return rc;
}
