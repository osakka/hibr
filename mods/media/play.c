#define _GNU_SOURCE

#include "mv.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* The size a frame is scaled to for the cells it fills: the whole picture
   inside the grid, its shape kept. A half-block cell holds two square-ish
   pixels one above the other; an ASCII cell is one pixel twice as tall as
   wide, so its height is halved after fitting. */
void mv_fit(mv_pl *p, int *w, int *h)
{
	int cols = p->tcols > 0 ? p->tcols : 80, rows = p->trows > 0 ? p->trows : 24;
	double a = p->vh > 0 ? (double)p->vw / p->vh : 16.0 / 9;
	int gw = cols, gh = rows * 2;

	if ((double)gw / gh > a) {
		*h = gh;
		*w = (int)(gh * a + 0.5);
	} else {
		*w = gw;
		*h = (int)(gw / a + 0.5);
	}
	if (*w < 1)
		*w = 1;
	if (*h < 2)
		*h = 2;
	if (p->mode != 0)
		*h = (*h + 1) / 2;
}

/* Wake whoever waits on the player. */
void mv_wake(mv_pl *p)
{
	pthread_cond_broadcast(&p->cv);
}

/* Wait on the player's condition for up to ms, the mutex held. */
void mv_wait(mv_pl *p, int ms)
{
	struct timespec ts;

	clock_gettime(CLOCK_REALTIME, &ts);
	ts.tv_nsec += (long)ms * 1000000L;
	ts.tv_sec += ts.tv_nsec / 1000000000L;
	ts.tv_nsec %= 1000000000L;
	pthread_cond_timedwait(&p->cv, &p->mu, &ts);
}

/* Throw away every frame and run of sound waiting, the mutex held. */
void mv_drain(mv_pl *p)
{
	mv_af *a;

	while (p->vqn) {
		free(p->vq[p->vqh].rgb);
		p->vq[p->vqh].rgb = 0;
		p->vqh = (p->vqh + 1) % MV_VQ;
		p->vqn--;
	}
	while (p->aq) {
		a = p->aq;
		p->aq = a->next;
		free(a->s);
		free(a);
	}
	p->aqt = 0;
	p->aqd = 0;
}

/* Record why the player stopped, once. */
void mv_fail(mv_pl *p, const char *why)
{
	pthread_mutex_lock(&p->mu);
	if (!p->err)
		p->err = xs(why);
	p->failed = 1;
	p->eof = 1;
	mv_wake(p);
	pthread_mutex_unlock(&p->mu);
}

/* Scale a decoded picture to the cells and queue it, dropping the oldest
   when the queue is full. Colours are rounded to the player's detail, so
   noise from one frame to the next does not redraw every cell. */
void mv_vframe(mv_pl *p, void *fr, double t)
{
	int w, h, sw = mv_rd32(fr, mv.fr_w), sh = mv_rd32(fr, mv.fr_h);
	int fmt = mv_rd32(fr, mv.fr_fmt), stride, i, gen;
	uint8_t *rgb, *dst[4], mask;
	int dstride[4];
	mv_vf *v;
	static const uint8_t masks[] = { 0xF0, 0xF8, 0xFC, 0xFF };

	pthread_mutex_lock(&p->mu);
	mv_fit(p, &w, &h);
	gen = p->sgen;
	mask = masks[p->detail & 3];
	pthread_mutex_unlock(&p->mu);
	if (!p->sws || gen != p->gen || sw != p->sfw || sh != p->sfh ||
	    w != p->sw || h != p->sh || fmt != p->vfmt) {
		if (p->sws)
			mv.sws_free(p->sws);
		p->sws = mv.sws_get(sw, sh, fmt, w, h, MV_RGB24, MV_SWS_AREA, 0, 0, 0);
		p->gen = gen;
		p->sfw = sw;
		p->sfh = sh;
		p->sw = w;
		p->sh = h;
		p->vfmt = fmt;
		if (!p->sws) {
			mv_fail(p, "cannot scale this video");
			return;
		}
	}
	stride = w * 3;
	rgb = xm((size_t)stride * h);
	memset(dst, 0, sizeof dst);
	memset(dstride, 0, sizeof dstride);
	dst[0] = rgb;
	dstride[0] = stride;
	mv.sws_scale(p->sws, (const uint8_t *const *)((char *)fr + mv.fr_data),
		     (const int *)((char *)fr + mv.fr_line), 0, sh, dst, dstride);
	if (mask != 0xFF)
		for (i = 0; i < stride * h; i++)
			rgb[i] &= mask;
	pthread_mutex_lock(&p->mu);
	if (p->vqn == MV_VQ) {
		free(p->vq[p->vqh].rgb);
		p->vqh = (p->vqh + 1) % MV_VQ;
		p->vqn--;
	}
	v = &p->vq[(p->vqh + p->vqn) % MV_VQ];
	v->pts = t;
	v->w = w;
	v->h = h;
	v->rgb = rgb;
	p->vqn++;
	mv_wake(p);
	pthread_mutex_unlock(&p->mu);
}

/* Convert decoded sound to 16-bit interleaved at the device's rate and
   queue it. The converter is made from the first frame, which knows its
   own layout, format and rate. */
void mv_aframe(mv_pl *p, void *fr, double t)
{
	int ns = mv_rd32(fr, mv.fr_ns), out, max;
	int16_t *buf;
	uint8_t *op;
	mv_af *a;
	char *lay;

	if (!p->swr) {
		lay = xm(64);
		memset(lay, 0, 64);
		mv.chl_default(lay, p->ao.ch);
		if (mv.swr_alloc2(&p->swr, lay, MV_S16, p->ao.rate,
				  (char *)fr + mv.fr_chl, mv_rd32(fr, mv.fr_fmt),
				  mv_rd32(fr, mv.fr_rate), 0, 0) < 0 ||
		    mv.swr_init(p->swr) < 0) {
			free(lay);
			mv_fail(p, "cannot convert this sound");
			return;
		}
		free(lay);
	}
	max = ns + 512;
	buf = xm((size_t)max * p->ao.ch * sizeof *buf);
	op = (uint8_t *)buf;
	out = mv.swr_convert(p->swr, &op, max,
			     (const uint8_t **)((char *)fr + mv.fr_data), ns);
	if (out <= 0) {
		free(buf);
		return;
	}
	a = xm(sizeof *a);
	memset(a, 0, sizeof *a);
	a->pts = t;
	a->n = out;
	a->s = buf;
	pthread_mutex_lock(&p->mu);
	if (p->aqt)
		p->aqt->next = a;
	else
		p->aq = a;
	p->aqt = a;
	p->aqd += (double)out / p->ao.rate;
	mv_wake(p);
	pthread_mutex_unlock(&p->mu);
}

/* Take every frame a decoder has ready and pass each on, skipping those
   before a seek's target. */
void mv_recv(mv_pl *p, void *cc, void *fr, int video)
{
	int64_t ts;
	double t, skip;

	while (mv.recv(cc, fr) >= 0) {
		ts = mv_rd64(fr, mv.fr_bets);
		if (ts == MV_NOPTS)
			ts = mv_rd64(fr, mv.fr_pts);
		pthread_mutex_lock(&p->mu);
		skip = p->skipto;
		pthread_mutex_unlock(&p->mu);
		if (video) {
			t = ts == MV_NOPTS ? 0 : ts * p->vtb;
			if (t + 0.001 >= skip)
				mv_vframe(p, fr, t);
		} else {
			t = ts == MV_NOPTS ? p->alast : ts * p->atb;
			p->alast = t + (double)mv_rd32(fr, mv.fr_ns) /
					       (p->arate > 0 ? p->arate : 48000);
			if (p->alast >= skip)
				mv_aframe(p, fr, t);
		}
		mv.fr_unref(fr);
	}
}

/* Whether decoding has run far enough ahead to wait. Video alone never
   holds it back while the sound is about to run dry -- a file whose
   streams are far apart would otherwise stop both. */
int mv_full(mv_pl *p)
{
	int vfull = p->vi >= 0 && p->vqn >= MV_VQ - 1;
	int afull = p->ai >= 0 && p->aqd >= MV_AHEAD;

	if (afull)
		return 1;
	return vfull && (p->ai < 0 || p->aqd > 0.3);
}

/* The decoding thread: read, decode, convert, queue; seek when asked;
   at the end, drain the decoders and wait for a seek or for closing. */
void *mv_dec(void *arg)
{
	mv_pl *p = arg;
	void *pkt = mv.pkt_alloc(), *fr = mv.fr_alloc();
	int r, si, drained = 0;
	double to;

	for (;;) {
		pthread_mutex_lock(&p->mu);
		while (!p->quit && !p->seekreq && (mv_full(p) || p->eof))
			mv_wait(p, 50);
		if (p->quit) {
			pthread_mutex_unlock(&p->mu);
			break;
		}
		if (p->seekreq) {
			to = p->seekto;
			p->seekreq = 0;
			pthread_mutex_unlock(&p->mu);
			if (mv.seek(p->fc, -1, INT64_MIN, (int64_t)(to * MV_TB),
				    (int64_t)(to * MV_TB), 0) < 0)
				mv.seek(p->fc, -1, INT64_MIN, (int64_t)(to * MV_TB),
					INT64_MAX, 0);
			if (p->vc)
				mv.flush(p->vc);
			if (p->ac)
				mv.flush(p->ac);
			pthread_mutex_lock(&p->mu);
			mv_drain(p);
			p->skipto = to;
			p->wbase = to;
			p->wstart = mv_now();
			p->pausedat = to;
			p->haveclock = 0;
			p->agen++;
			p->eof = 0;
			p->ended = 0;
			drained = 0;
			mv_wake(p);
			pthread_mutex_unlock(&p->mu);
			continue;
		}
		pthread_mutex_unlock(&p->mu);
		r = mv.read(p->fc, pkt);
		if (r < 0) {
			if (!drained) {
				if (p->vc && mv.send(p->vc, 0) >= 0)
					mv_recv(p, p->vc, fr, 1);
				if (p->ac && mv.send(p->ac, 0) >= 0)
					mv_recv(p, p->ac, fr, 0);
				drained = 1;
			}
			pthread_mutex_lock(&p->mu);
			p->eof = 1;
			if (r != MV_EOF && !p->err)
				p->err = mv_averr(r);
			mv_wake(p);
			pthread_mutex_unlock(&p->mu);
			continue;
		}
		si = mv_rd32(pkt, mv.pk_stream);
		if (si == p->vi && p->vc) {
			mv.send(p->vc, pkt);
			mv_recv(p, p->vc, fr, 1);
		} else if (si == p->ai && p->ac) {
			mv.send(p->ac, pkt);
			mv_recv(p, p->ac, fr, 0);
		}
		mv.pkt_unref(pkt);
	}
	mv.pkt_free(&pkt);
	mv.fr_free(&fr);
	return 0;
}

/* The sound thread: hand queued sound to the device a slice at a time,
   at the player's volume, and keep the clock the video follows. */
void *mv_aud(void *arg)
{
	mv_pl *p = arg;
	int16_t *tmp = xm(1024 * 8 * sizeof *tmp);
	mv_af *a;
	int n, i, gen, ch = p->ao.ch, vol;
	double pts, d;

	for (;;) {
		pthread_mutex_lock(&p->mu);
		if (p->aflush) {
			p->aflush = 0;
			pthread_mutex_unlock(&p->mu);
			p->ao.flush(&p->ao);
			continue;
		}
		while (!p->quit && !p->aflush && (p->paused || !p->aq))
			mv_wait(p, 100);
		if (p->quit) {
			pthread_mutex_unlock(&p->mu);
			break;
		}
		if (p->aflush) {
			pthread_mutex_unlock(&p->mu);
			continue;
		}
		a = p->aq;
		n = a->n - a->off;
		if (n > 1024)
			n = 1024;
		pts = a->pts + (double)a->off / p->ao.rate;
		vol = p->vol;
		for (i = 0; i < n * ch; i++)
			tmp[i] = (int16_t)(a->s[a->off * ch + i] * vol / 100);
		gen = p->agen;
		a->off += n;
		p->aqd -= (double)n / p->ao.rate;
		if (a->off >= a->n) {
			p->aq = a->next;
			if (!p->aq)
				p->aqt = 0;
			free(a->s);
			free(a);
		}
		mv_wake(p);
		pthread_mutex_unlock(&p->mu);
		p->ao.write(&p->ao, tmp, n);
		d = p->ao.delay(&p->ao);
		pthread_mutex_lock(&p->mu);
		if (gen == p->agen && !p->paused) {
			p->aclock = pts + (double)n / p->ao.rate - d;
			p->ats = mv_now();
			p->haveclock = 1;
		}
		pthread_mutex_unlock(&p->mu);
	}
	free(tmp);
	return 0;
}

/* The clock, the mutex held: the sound's, moved on by the time since it
   was last set; with sound not playing yet -- at the start, after a seek
   or a pause -- held where it starts, so the sound taking over late never
   pulls it back; and the wall's when there is no sound at all. */
double mv_clocklk(mv_pl *p)
{
	if (p->paused)
		return p->pausedat;
	if (p->ai >= 0)
		return p->haveclock ? p->aclock + (mv_now() - p->ats) : p->wbase;
	return p->wbase + (mv_now() - p->wstart);
}

/* The time the player is at, in seconds. */
double mv_clock(mv_pl *p)
{
	double c;

	pthread_mutex_lock(&p->mu);
	c = mv_clocklk(p);
	pthread_mutex_unlock(&p->mu);
	return c;
}

/* Pause or play. Pausing freezes the clock and drops the sound waiting in
   the device; playing starts the wall clock from where it froze until the
   sound takes over again. */
void mv_pause(mv_pl *p, int on)
{
	pthread_mutex_lock(&p->mu);
	if (on && !p->paused) {
		p->pausedat = mv_clocklk(p);
		p->paused = 1;
		if (p->ai >= 0)
			p->aflush = 1;
	} else if (!on && p->paused) {
		p->paused = 0;
		p->wbase = p->pausedat;
		p->wstart = mv_now();
		p->haveclock = 0;
		p->agen++;
	}
	mv_wake(p);
	pthread_mutex_unlock(&p->mu);
}

/* Go to a time, in seconds, within the source. */
void mv_seek(mv_pl *p, double t)
{
	if (t < 0)
		t = 0;
	if (p->dur > 0 && t > p->dur)
		t = p->dur;
	pthread_mutex_lock(&p->mu);
	if (p->feed) {
		mv_feedseek(p);
		mv_drain(p);
		p->skipto = t;
		p->wbase = t;
		p->wstart = mv_now();
		p->haveclock = 0;
		p->agen++;
		p->eof = 0;
		p->ended = 0;
	} else {
		p->seekreq = 1;
		p->seekto = t;
	}
	p->pausedat = t;
	if (p->ai >= 0)
		p->aflush = 1;
	mv_wake(p);
	pthread_mutex_unlock(&p->mu);
}

/* Set the cells the picture fills and how it is drawn: 0 half blocks, 1
   ASCII in colour, 2 ASCII plain. Frames already decoded keep their size;
   the next ones are made at the new one. */
void mv_size(mv_pl *p, int cols, int rows, int mode)
{
	pthread_mutex_lock(&p->mu);
	if (cols != p->tcols || rows != p->trows || mode != p->mode) {
		p->tcols = cols;
		p->trows = rows;
		p->mode = mode;
		p->sgen++;
	}
	pthread_mutex_unlock(&p->mu);
}

/* Bring the shown frame up to the clock: 0 when it changed, 1 when not.
   A paused player still shows its first frame; frames come no faster than
   the player's cap, the ones in between dropped. */
int mv_frame(mv_pl *p)
{
	double c, now = mv_now();
	int changed = 0;
	mv_vf *v;

	pthread_mutex_lock(&p->mu);
	c = mv_clocklk(p);
	if (!p->cur.rgb && p->vqn) {
		v = &p->vq[p->vqh];
		p->cur = *v;
		v->rgb = 0;
		p->vqh = (p->vqh + 1) % MV_VQ;
		p->vqn--;
		changed = 1;
	}
	if (p->fpsmax <= 0 || now - p->lastdraw >= 1.0 / p->fpsmax) {
		while (p->vqn && p->vq[p->vqh].pts <= c) {
			v = &p->vq[p->vqh];
			free(p->cur.rgb);
			p->cur = *v;
			v->rgb = 0;
			p->vqh = (p->vqh + 1) % MV_VQ;
			p->vqn--;
			changed = 1;
		}
	}
	if (p->eof && !p->vqn && !p->aq && !p->seekreq)
		p->ended = 1;
	if (p->feed && p->dur > 0 && c >= p->dur - 0.05 && !p->vqn)
		p->ended = 1;
	if (changed)
		mv_wake(p);
	pthread_mutex_unlock(&p->mu);
	if (changed)
		p->lastdraw = now;
	return changed ? HIBR_OK : HIBR_FAIL;
}

/* Milliseconds until the next frame is due, for a caller that sleeps
   between them; -1 when nothing will change without being asked. */
long mv_next(mv_pl *p)
{
	double c, ms, gap;
	long r;

	pthread_mutex_lock(&p->mu);
	if (p->paused || p->ended) {
		r = !p->cur.rgb && !p->failed ? 50 : -1;
		pthread_mutex_unlock(&p->mu);
		return r;
	}
	if (!p->vqn) {
		pthread_mutex_unlock(&p->mu);
		return p->vi >= 0 || p->ai >= 0 ? 30 : -1;
	}
	c = mv_clocklk(p);
	ms = (p->vq[p->vqh].pts - c) * 1000;
	pthread_mutex_unlock(&p->mu);
	gap = p->fpsmax > 0 ? (1.0 / p->fpsmax - (mv_now() - p->lastdraw)) * 1000 : 0;
	if (gap > ms)
		ms = gap;
	if (ms < 5)
		ms = 5;
	if (ms > 1000)
		ms = 1000;
	return (long)ms;
}

/* Open a codec for a stream; 0 when it cannot be. */
void *mv_codec(void *st, const void *dec, char **why)
{
	void *cc, *par = mv_rdp(st, mv.st_par);

	if (!dec) {
		*why = xs("no decoder for it");
		return 0;
	}
	cc = mv.cc_alloc(dec);
	if (!cc) {
		*why = xs("out of memory");
		return 0;
	}
	if (mv.par_to(cc, par) < 0) {
		mv.cc_free(&cc);
		*why = xs("its parameters do not fit the decoder");
		return 0;
	}
	memcpy((char *)cc + mv.cc_pkttb, (char *)st + mv.st_tb, 2 * sizeof(int));
	mv.opt_set(cc, "threads", "auto", 0);
	if (mv.cc_open(cc, dec, 0) < 0) {
		mv.cc_free(&cc);
		*why = xs("the decoder would not open");
		return 0;
	}
	return cc;
}

/* Open a source -- a file, or anything libav reads, http included -- and
   start decoding it, paused or playing. */
mv_pl *mv_open(sh *s, const char *src, int paused)
{
	mv_pl *p;
	void *fc = 0, **sts, *st, *par;
	const void *vd = 0, *ad = 0;
	char *why = 0;
	int r, nst;

	(void)s;
	if (mv_load() != HIBR_OK)
		return 0;
	r = mv.open_input(&fc, src, 0, 0);
	if (r < 0) {
		why = mv_averr(r);
		lg(HIBR_LERR, "media: %s: %s", src, why);
		free(why);
		return 0;
	}
	if (mv.find_info(fc, 0) < 0) {
		lg(HIBR_LERR, "media: %s: cannot read its streams", src);
		mv.close_input(&fc);
		return 0;
	}
	p = xm(sizeof *p);
	memset(p, 0, sizeof *p);
	p->fc = fc;
	p->src = xs(src);
	p->vol = 100;
	p->detail = 2;
	p->fpsmax = MV_FPS;
	p->paused = paused;
	p->vi = mv.best(fc, MV_VIDEO, -1, -1, &vd, 0);
	p->ai = mv.best(fc, MV_AUDIO, -1, p->vi, &ad, 0);
	if (p->vi < 0)
		p->vi = -1;
	if (p->ai < 0)
		p->ai = -1;
	nst = mv_rd32(fc, mv.fc_nb);
	sts = mv_rdp(fc, mv.fc_streams);
	if (p->vi >= nst || p->ai >= nst || (p->vi < 0 && p->ai < 0)) {
		lg(HIBR_LERR, "media: %s: no picture and no sound in it", src);
		mv.close_input(&fc);
		free(p->src);
		free(p);
		return 0;
	}
	pthread_mutex_init(&p->mu, 0);
	pthread_cond_init(&p->cv, 0);
	if (p->vi >= 0) {
		st = sts[p->vi];
		par = mv_rdp(st, mv.st_par);
		p->vtb = mv_rdq(st, mv.st_tb);
		p->vw = mv_rd32(par, mv.cp_w);
		p->vh = mv_rd32(par, mv.cp_h);
		p->vcodec = xs(mv.codec_name(mv_rd32(par, mv.cp_id)));
		p->vc = mv_codec(st, vd, &why);
		if (!p->vc) {
			lg(HIBR_LERR, "media: %s: the picture (%s): %s", src,
			   p->vcodec, why);
			free(why);
			why = 0;
			p->vi = -1;
		}
	}
	if (p->ai >= 0) {
		st = sts[p->ai];
		par = mv_rdp(st, mv.st_par);
		p->atb = mv_rdq(st, mv.st_tb);
		p->arate = mv_rd32(par, mv.cp_rate);
		p->ach = mv_rd32(par, mv.cp_chl + 4);
		p->acodec = xs(mv.codec_name(mv_rd32(par, mv.cp_id)));
		p->ac = mv_codec(st, ad, &why);
		if (!p->ac) {
			lg(HIBR_LERR, "media: %s: the sound (%s): %s", src,
			   p->acodec, why);
			free(why);
			why = 0;
			p->ai = -1;
		}
	}
	if (p->vi < 0 && p->ai < 0) {
		mv_close(p);
		return 0;
	}
	if (mv_rd64(fc, mv.fc_dur) != MV_NOPTS)
		p->dur = (double)mv_rd64(fc, mv.fc_dur) / MV_TB;
	if (p->ai >= 0) {
		mv_aopen(&p->ao, p->arate > 0 ? p->arate : 48000,
			 p->ach >= 2 ? 2 : 1, &why);
		if (why) {
			p->err = why;
			why = 0;
		}
	}
	p->wstart = mv_now();
	if (pthread_create(&p->dth, 0, mv_dec, p) == 0)
		p->dstarted = 1;
	if (p->ai >= 0 && pthread_create(&p->ath, 0, mv_aud, p) == 0)
		p->astarted = 1;
	return p;
}

/* Stop both threads and free everything a player holds. */
void mv_close(mv_pl *p)
{
	pthread_mutex_lock(&p->mu);
	p->quit = 1;
	mv_wake(p);
	pthread_mutex_unlock(&p->mu);
	if (p->feed)
		mv_feedclose(p);
	if (p->dstarted)
		pthread_join(p->dth, 0);
	if (p->astarted)
		pthread_join(p->ath, 0);
	mv_drain(p);
	free(p->cur.rgb);
	if (p->ao.close)
		p->ao.close(&p->ao);
	if (p->vc)
		mv.cc_free(&p->vc);
	if (p->ac)
		mv.cc_free(&p->ac);
	if (p->sws)
		mv.sws_free(p->sws);
	if (p->swr)
		mv.swr_free(&p->swr);
	if (p->fc)
		mv.close_input(&p->fc);
	pthread_mutex_destroy(&p->mu);
	pthread_cond_destroy(&p->cv);
	free(p->src);
	free(p->vcodec);
	free(p->acodec);
	free(p->err);
	free(p);
}
