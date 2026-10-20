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
   source's chunks are kept. A take stops at a reset: what came before it
   one time, the reset alone the next, what follows after -- so whoever
   takes can start its player afresh before any of the new stream arrives,
   rather than throw some of it away by starting afresh after. */
#define WB_QJS \
	"var T=window.__hibrTap||(window.__hibrTap={q:[],gen:0,init:{}});" \
	"if(!window.__hibrTake){" \
	"window.__hibrSeek=function(){T.q=[];" \
	"if(T.init.v)T.q.push(['v',T.init.v]);" \
	"if(T.init.a)T.q.push(['a',T.init.a]);return 'ok'};" \
	"window.__hibrTake=function(){var o=[],i,e,b,s,j;" \
	"for(i=0;i<T.q.length;i++){e=T.q[i];" \
	"if(e[0]==='r'){if(!o.length){o.push(e);i++}break}" \
	"if(typeof e[1]==='string'){o.push(e);continue}" \
	"b=e[1];s='';for(j=0;j<b.length;j+=32768)" \
	"s+=String.fromCharCode.apply(null,b.subarray(j,j+32768));" \
	"o.push([e[0],btoa(s)])}" \
	"T.q=T.q.slice(i);if(T.snd)o.push(['s',T.snd]);" \
	"return JSON.stringify(o)}}"

static const char wb_tapjs[] =
	"(function(){" WB_QJS
	"if(T.mse)return;T.mse=1;"
	"document.addEventListener('play',function(e){"
	"try{e.target.muted=true}catch(x){}},true);"
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
	"return app.call(this,d)}})()";

/* A tab's sound (Gitea #305), in every tab from its opening and in every
   frame. A page's own getDisplayMedia is refused: the browser accepts a
   tab's capture of itself without asking, which would hand any page the
   pixels of whatever it frames. The top frame keeps the real one, and
   __hibrSndStart records the tab's sound with it into the tap's queue --
   a reset, then WebM/Opus in quarter seconds, read with web take -- with
   the browser's voice processing off, since it wears a steady note down,
   and the sound kept off the machine's own device. The page's first
   click or key starts it once a tab's sound is on (__hibrSndClick) -- a
   tick after it, not in it: started inside a pointerdown, the capture
   swallowed the press, and a click on a link failed to follow it about
   one time in three. The press's own gesture lasts long enough for a
   tick.
   As each page loads, with that allowed, __hibrSndStart(1) comes as a
   gesture and plays again what the page was refused before it: its
   autoplay elements, and any play() turned down for want of a press.
   After two seconds of silence the recording pauses -- the tab's state
   `quiet`, against `on` -- so a tab that played something once does not
   keep a desktop taking from it four times a second for ever. */
static const char wb_sndjs[] =
	"(function(){var M=window.MediaDevices&&MediaDevices.prototype,"
	"g=M&&M.getDisplayMedia;if(!g||M.__hibrG)return;"
	"M.getDisplayMedia=function(){return Promise.reject("
	"new DOMException('Permission denied','NotAllowedError'))};"
	"try{Object.defineProperty(M,'__hibrG',{value:1})}catch(x){}"
	"if(window.top!==window)return;" WB_QJS
	"var R=window.MediaRecorder,S=window.MediaStream,P=Promise.resolve(),"
	"st='idle',rec=null,cap=null,ac=null,tick=0,pend=[],"
	"pl=HTMLMediaElement.prototype.play;"
	"HTMLMediaElement.prototype.play=function(){var el=this,p=pl.call(this);"
	"if(p&&p.catch)p.catch(function(e){if(e&&e.name==='NotAllowedError'&&"
	"pend.indexOf(el)<0)pend.push(el)});return p};"
	"function again(){var l=pend.concat([].slice.call(document.querySelectorAll("
	"'audio[autoplay],video[autoplay]')));pend=[];l.forEach(function(el){"
	"if(el.paused)pl.call(el).catch(function(){})})}"
	"window.__hibrSndStart=function(auto){if(st!=='idle'){if(auto)again();"
	"return Promise.resolve(st)}"
	"st='asking';return g.call(navigator.mediaDevices,{video:true,"
	"audio:{echoCancellation:false,noiseSuppression:false,"
	"autoGainControl:false,suppressLocalAudioPlayback:true},"
	"preferCurrentTab:true,selfBrowserSurface:'include',"
	"systemAudio:'exclude'}).then(function(s){"
	"s.getVideoTracks().forEach(function(v){v.stop()});"
	"var a=s.getAudioTracks();if(!a.length){st='none';return st}"
	"var r=new R(new S(a),{mimeType:'audio/webm;codecs=opus',"
	"audioBitsPerSecond:128000});"
	"T.gen++;T.init={};T.q=[['r',''],['ma','audio/webm']];"
	"r.ondataavailable=function(e){if(!e.data.size||rec!==r)return;"
	"P=P.then(function(){return e.data.arrayBuffer()}).then(function(b){"
	"if(rec===r)T.q.push(['a',new Uint8Array(b)])})};"
	"r.start(250);rec=r;cap=s;st='on';T.snd='on';if(auto)again();"
	"try{ac=new AudioContext();var an=ac.createAnalyser(),"
	"buf=new Float32Array(an.fftSize),quiet=0;"
	"ac.createMediaStreamSource(new S(a)).connect(an);ac.resume();"
	"tick=setInterval(function(){var i,pk=0;an.getFloatTimeDomainData(buf);"
	"for(i=0;i<buf.length;i++)if(Math.abs(buf[i])>pk)pk=Math.abs(buf[i]);"
	"if(pk>0.0001){quiet=0;if(r.state==='paused')r.resume();T.snd='on'}"
	"else if(++quiet>=8&&r.state==='recording'){r.pause();T.snd='quiet'}"
	"},250)}catch(x){}return st},"
	"function(e){st='idle';return 'refused: '+e.name})};"
	"window.__hibrSndStop=function(){if(rec)try{rec.stop()}catch(x){}"
	"if(cap)cap.getTracks().forEach(function(k){k.stop()});"
	"if(tick)clearInterval(tick);if(ac)try{ac.close()}catch(x){}"
	"rec=cap=ac=null;tick=0;st='idle';T.snd='';return 'ok'};"
	"function go(){if(window.__hibrSndClick&&st==='idle')"
	"setTimeout(function(){window.__hibrSndStart()},0)}"
	"window.addEventListener('click',go,true);"
	"window.addEventListener('keydown',go,true)})()";

/* Guard a new tab's getDisplayMedia and give it the means of recording
   its sound: on every document it loads, and on the one there now. */
int wb_sndguard(wb_tab *t)
{
	str o;
	char *id = 0;

	if (wb_docjs(t, &id, wb_sndjs) != HIBR_OK)
		return HIBR_FAIL;
	free(id);
	s_init(&o);
	wb_eval(t, wb_sndjs, &o);
	s_free(&o);
	return HIBR_OK;
}

/* web sound T off|click|load: when the tab's sound starts being recorded
   -- never, at the page's first press or key (the browser's own rule for
   sound), or as each page loads, which is a gesture of the page's own
   (Runtime.evaluate's userGesture) and so lets it play sound unasked. */
int wb_sound(wb_tab *t, const char *mode)
{
	str o;
	int m;

	if (!strcmp(mode, "off"))
		m = 0;
	else if (!strcmp(mode, "click"))
		m = 1;
	else if (!strcmp(mode, "load"))
		m = 2;
	else {
		lg(HIBR_LERR, "web sound: off, click or load, not %s", mode);
		return HIBR_FAIL;
	}
	if (wb_docjs(t, &t->sndjs, m ? "window.__hibrSndClick=1" : 0) != HIBR_OK)
		return HIBR_FAIL;
	t->snd = m;
	s_init(&o);
	wb_eval(t, m ? "window.__hibrSndClick=1" :
		"window.__hibrSndClick=0;window.__hibrSndStop?__hibrSndStop():''", &o);
	o.n = 0;
	if (m == 2)
		wb_evalg(t, "window.__hibrSndStart?__hibrSndStart(1):'none'", &o, 1);
	s_free(&o);
	return HIBR_OK;
}

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
		w = hibr_wrall(fd, p, n);
		if (w < 0 && errno == EINTR)
			continue;
		if (w <= 0)
			return HIBR_FAIL;
		p += w;
		n -= (size_t)w;
	}
	return HIBR_OK;
}

/* Take what the tap has queued, up to a reset: video chunks to vfd, audio
   to afd, and say what happened -- bytes of each, a reset, the types, and
   the tab's sound -- as words: "v N a N reset 0|1 vtype T atype T snd
   on|quiet|-". A reset comes alone, with no bytes; the stream after it
   comes on the next take. */
int wb_take(sh *s, wb_tab *t, int vfd, int afd)
{
	str o, b, sum, vt, at, sd;
	jv *a, *e;
	size_t i, nv = 0, na = 0;
	const char *k;
	int reset = 0, rc = HIBR_OK;

	s_init(&o);
	if (t->sndgo) {
		t->sndgo = 0;
		wb_evalg(t, "window.__hibrSndStart?__hibrSndStart(1):'none'", &o, 1);
		o.n = 0;
	}
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
	s_init(&sd);
	for (i = 0; i < a->v.n; i++) {
		e = a->v.p[i];
		k = jv_str(jv_path(e, "0"));
		if (!strcmp(k, "s")) {
			sd.n = 0;
			s_add(&sd, jv_str(jv_path(e, "1")),
			      strcspn(jv_str(jv_path(e, "1")), " "));
			continue;
		}
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
	s_cat(&sum, " snd ");
	s_cat(&sum, sd.n ? sd.p : "-");
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
	s_free(&sd);
	return rc;
}
