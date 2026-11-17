#define _GNU_SOURCE

#include "mv.h"
#include <dlfcn.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* The size a frame is scaled to for the cells it fills: the whole picture
   inside the grid, its shape kept. A half-block cell holds two square-ish
   pixels one above the other; an ASCII cell is one pixel twice as tall as
   wide, so its height is halved after fitting.
   
   The mode is passed rather than read off the player, because a frame
   the display refuses to place as a bitmap is drawn as half blocks
   instead, and that wants the half-block grid rather than the one the
   bitmap was going to use. */
void mv_fit(mv_pl *p, int mode, int *w, int *h)
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
	if (mode != 0)
		*h = (*h + 1) / 2;
}

/* The size a frame is **decoded** at, which is not the size of the grid
   it fills.
   
   For the cell modes the two are the same: a cell can show one source
   pixel, or two for a half block, and decoding finer would be thrown
   away. For **pixels** they are not, and conflating them is why that
   mode looked worse than the one below it: the frame was scaled to the
   *cell count* -- one source pixel per character cell -- and the
   terminal then blew it back up by the whole size of a cell, so a
   picture asked for in pixels had a quarter of the detail of the same
   picture in half blocks, which is the opposite of what the setting
   promises (Gitea #199). A bitmap is drawn at the terminal's own
   pixels, so that is what it is decoded at. Before any draw has asked
   the display what a cell measures there is nothing to multiply by,
   and the grid's own size stands until the next frame. */
void mv_fitpx(mv_pl *p, int *w, int *h)
{
	mv_fit(p, p->mode, w, h);
	if (p->mode == 3 && p->cw > 1 && p->ch > 1) {
		*w *= p->cw;
		*h *= p->ch;
		/* And never past the source. Decoding a 640x360 stream to
		   a 1200x480 rectangle invents nothing -- the scaler has no
		   more detail to give than arrived -- and every one of those
		   pixels is then resampled and sent. The display caps the
		   same way; doing it here as well keeps the work out of the
		   decode thread rather than only off the wire. */
		if (p->vw > 0 && *w > p->vw)
			*w = p->vw;
		if (p->vh > 0 && *h > p->vh)
			*h = p->vh;
	}
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
	mv_fitpx(p, &w, &h);
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

/* Drop the frames waiting that a later one has already passed, the mutex
   held: nothing will show them now, and a queue full of them let a
   decoder nobody was drawing for go on dropping the oldest -- the present
   -- until all it held was the future, a picture frozen until the clock
   caught up (Gitea #288). The last frame due stays, for the next draw. */
void mv_prune(mv_pl *p)
{
	double c;
	mv_vf *v;

	if (p->vqn < 2)
		return;
	c = mv_clocklk(p);
	while (p->vqn >= 2 && p->vq[(p->vqh + 1) % MV_VQ].pts <= c) {
		v = &p->vq[p->vqh];
		free(v->rgb);
		v->rgb = 0;
		p->vqh = (p->vqh + 1) % MV_VQ;
		p->vqn--;
	}
}

/* Whether one fed input has run far enough ahead to wait. Each has a
   thread of its own, so each waits on its own queue: the one rule for
   both held the sound at 0.3 s whenever the picture's queue was full,
   and every time it dipped the picture decoded on past the clock. */
int mv_kfull(mv_pl *p, int video)
{
	if (!video)
		return p->aqd >= MV_AHEAD;
	mv_prune(p);
	return p->vqn >= MV_VQ - 1;
}

/* Whether decoding has run far enough ahead to wait. Video alone never
   holds it back while the sound is about to run dry -- a file whose
   streams are far apart would otherwise stop both. */
int mv_full(mv_pl *p)
{
	int vfull;

	mv_prune(p);
	vfull = p->vi >= 0 && p->vqn >= MV_VQ - 1;
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
	int n, i, gen, ch = p->ao.ch, vol, eq;
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
		eq = mv_eqsync(p, ch);
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
		if (eq)
			mv_eqrun(p, tmp, n, ch);
		p->ao.write(&p->ao, tmp, n);
		d = p->ao.delay(&p->ao);
		pthread_mutex_lock(&p->mu);
		mv_ringpush(p, tmp, n, ch);
		pthread_mutex_unlock(&p->mu);
		mv_specupdate(p);
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
	if (p->opening)
		return p->paused ? p->pausedat : p->wbase;
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
		if (!p->opening && p->ai >= 0)
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
	pthread_mutex_lock(&p->mu);
	/* Still opening: kept as a request, which the decoding takes up the
	   moment it starts. */
	if (p->opening) {
		p->seekreq = 1;
		p->seekto = t;
		p->pausedat = t;
		p->wbase = t;
		pthread_mutex_unlock(&p->mu);
		return;
	}
	if (p->dur > 0 && t > p->dur)
		t = p->dur;
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
	if (p->opening) {
		pthread_mutex_unlock(&p->mu);
		return 50;
	}
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

/* A new player for src, nothing opened yet. */
mv_pl *mv_new(const char *src, int paused)
{
	mv_pl *p = xm(sizeof *p);

	memset(p, 0, sizeof *p);
	p->src = xs(src);
	p->vol = 100;
	p->detail = 2;
	p->fpsmax = MV_FPS;
	p->paused = paused;
	p->vi = p->ai = -1;
	pthread_mutex_init(&p->mu, 0);
	pthread_cond_init(&p->cv, 0);
	return p;
}

/* Open and probe src into *fc: 1, or 0 with *why. Bounded on a network
   source by rw_timeout, so a server that stops answering ends it rather
   than holding the thread for ever. */
int mv_probe(const char *src, void **fc, char **why)
{
	void *opts = 0;
	int r;

	*fc = 0;
	if (strstr(src, "://"))
		mv.dict_set(&opts, "rw_timeout", MV_RWTIMEOUT, 0);
	r = mv.open_input(fc, src, 0, &opts);
	mv.dict_free(&opts);
	if (r < 0) {
		*why = mv_averr(r);
		*fc = 0;
		return 0;
	}
	if (mv.find_info(*fc, 0) < 0) {
		*why = xs("cannot read its streams");
		mv.close_input(fc);
		return 0;
	}
	return 1;
}

/* Choose the streams of an opened p->fc and open their decoders, and the
   sound device when sound is 1: 1, or 0 with *why. Logs nothing, since a
   thread may run it; a problem with one stream that leaves the other is
   put in *note. The device is never opened off the caller's thread: it
   reads the environment, which the shell may be changing. */
int mv_setup(mv_pl *p, char **why, char **note, int sound)
{
	void **sts, *st, *par, *fc = p->fc;
	const void *vd = 0, *ad = 0;
	char *w = 0;
	int nst;

	p->vi = mv.best(fc, MV_VIDEO, -1, -1, &vd, 0);
	p->ai = mv.best(fc, MV_AUDIO, -1, p->vi, &ad, 0);
	if (p->vi < 0)
		p->vi = -1;
	if (p->ai < 0)
		p->ai = -1;
	nst = mv_rd32(fc, mv.fc_nb);
	sts = mv_rdp(fc, mv.fc_streams);
	if (p->vi >= nst || p->ai >= nst || (p->vi < 0 && p->ai < 0)) {
		p->vi = p->ai = -1;
		*why = xs("no picture and no sound in it");
		return 0;
	}
	if (p->vi >= 0) {
		st = sts[p->vi];
		par = mv_rdp(st, mv.st_par);
		p->vtb = mv_rdq(st, mv.st_tb);
		p->vw = mv_rd32(par, mv.cp_w);
		p->vh = mv_rd32(par, mv.cp_h);
		p->vcodec = xs(mv.codec_name(mv_rd32(par, mv.cp_id)));
		p->vc = mv_codec(st, vd, &w);
		if (!p->vc) {
			free(*note);
			*note = w;
			w = 0;
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
		p->ac = mv_codec(st, ad, &w);
		if (!p->ac) {
			free(*note);
			*note = w;
			w = 0;
			p->ai = -1;
		}
	}
	if (p->vi < 0 && p->ai < 0) {
		*why = *note ? *note : xs("no stream in it can be decoded");
		*note = 0;
		return 0;
	}
	if (mv_rd64(fc, mv.fc_dur) != MV_NOPTS)
		p->dur = (double)mv_rd64(fc, mv.fc_dur) / MV_TB;
	if (sound && p->ai >= 0) {
		mv_aopen(&p->ao, p->arate > 0 ? p->arate : 48000,
			 p->ach >= 2 ? 2 : 1, &w);
		if (w)
			p->err = w;
	}
	return 1;
}

/* On the caller's thread, from any verb: a player opened by address whose
   streams are known gets its sound device, its waiting decoder the word to
   go, and its sound thread. */
void mv_aready(mv_pl *p)
{
	char *w = 0;
	int want;

	pthread_mutex_lock(&p->mu);
	want = !p->opening && p->aowait;
	pthread_mutex_unlock(&p->mu);
	if (!want)
		return;
	mv_aopen(&p->ao, p->arate > 0 ? p->arate : 48000, p->ach >= 2 ? 2 : 1, &w);
	pthread_mutex_lock(&p->mu);
	if (w) {
		free(p->err);
		p->err = w;
	}
	p->aowait = 0;
	mv_wake(p);
	pthread_mutex_unlock(&p->mu);
	if (pthread_create(&p->ath, 0, mv_aud, p) == 0)
		p->astarted = 1;
}

/* Keep this module mapped for good once a thread of its own may outlive a
   player's close: `mod drop media` must not unmap code still running. */
static int mv_pinned;
void mv_pin(void)
{
	Dl_info di;

	if (mv_pinned || !dladdr((void *)mv_pin, &di) || !di.dli_fname)
		return;
	if (dlopen(di.dli_fname, RTLD_NOW | RTLD_NOLOAD | RTLD_NODELETE))
		mv_pinned = 1;
}

/* The end of a player's own thread after a close: said, for the close to
   join it -- or, when the close could not wait (a read the network had
   stalled), the player freed here. */
void mv_done(mv_pl *p)
{
	pthread_mutex_lock(&p->mu);
	if (p->orphan) {
		pthread_mutex_unlock(&p->mu);
		p->dstarted = 0;
		mv_free(p);
		return;
	}
	p->dexit = 1;
	mv_wake(p);
	pthread_mutex_unlock(&p->mu);
}

/* A player opened by address: the connect, the probe and the decoders
   here, off the caller's thread, then the decoding in the same thread. A
   close meanwhile is an orphan's, and this frees it. */
void *mv_opener(void *a)
{
	mv_pl *p = a;
	void *fc = 0;
	char *why = 0, *note = 0;
	int ok, orphan;

	ok = mv_probe(p->src, &fc, &why);
	pthread_mutex_lock(&p->mu);
	orphan = p->orphan;
	pthread_mutex_unlock(&p->mu);
	if (orphan) {
		if (fc)
			mv.close_input(&fc);
		free(why);
		p->dstarted = 0;
		mv_free(p);
		return 0;
	}
	p->fc = fc;
	if (ok)
		ok = mv_setup(p, &why, &note, 0);
	free(note);
	pthread_mutex_lock(&p->mu);
	if (p->orphan) {
		pthread_mutex_unlock(&p->mu);
		p->dstarted = 0;
		free(why);
		mv_free(p);
		return 0;
	}
	p->opening = 0;
	p->wstart = mv_now();
	if (!ok) {
		p->failed = 1;
		p->ended = 1;
		free(p->err);
		p->err = why ? why : xs("it could not be opened");
		pthread_mutex_unlock(&p->mu);
		return 0;
	}
	free(why);
	/* Sound: decoding waits for the caller's thread to open the device
	   (mv_aready), since it converts to the device's rate. */
	p->aowait = p->ai >= 0;
	while (p->aowait && !p->quit)
		pthread_cond_wait(&p->cv, &p->mu);
	if (p->quit) {
		pthread_mutex_unlock(&p->mu);
		mv_done(p);
		return 0;
	}
	pthread_mutex_unlock(&p->mu);
	/* Decoding ends only on a close. */
	mv_dec(p);
	if (p->astarted)
		pthread_join(p->ath, 0);
	p->astarted = 0;
	mv_done(p);
	return 0;
}

/* Open a source -- a file, or anything libav reads, http included -- and
   start decoding it, paused or playing. A file is opened here, so one that
   cannot be is refused at once; an address never is: it connects and
   probes in the player's own thread, `media info` saying `loading` until
   it has, and `ended` with the `error` if it could not (the Quran's
   recitations held the desktop for a second an ayah, 2026-11-14). */
mv_pl *mv_open(sh *s, const char *src, int paused)
{
	mv_pl *p;
	void *fc = 0;
	char *why = 0, *note = 0;

	(void)s;
	if (mv_load() != HIBR_OK)
		return 0;
	if (strstr(src, "://")) {
		p = mv_new(src, paused);
		p->opening = 1;
		p->own = 1;
		mv_pin();
		if (pthread_create(&p->dth, 0, mv_opener, p) != 0) {
			lg(HIBR_LERR, "media: %s: no thread to open it in", src);
			mv_free(p);
			return 0;
		}
		p->dstarted = 1;
		return p;
	}
	if (!mv_probe(src, &fc, &why)) {
		lg(HIBR_LERR, "media: %s: %s", src, why);
		free(why);
		return 0;
	}
	p = mv_new(src, paused);
	p->fc = fc;
	if (!mv_setup(p, &why, &note, 1)) {
		lg(HIBR_LERR, "media: %s: %s", src, why);
		free(why);
		mv_free(p);
		return 0;
	}
	if (note) {
		lg(HIBR_LERR, "media: %s: %s", src, note);
		free(note);
	}
	p->wstart = mv_now();
	if (pthread_create(&p->dth, 0, mv_dec, p) == 0)
		p->dstarted = 1;
	if (p->ai >= 0 && pthread_create(&p->ath, 0, mv_aud, p) == 0)
		p->astarted = 1;
	return p;
}

/* Whether a player is still opening its source. */
int mv_opening(mv_pl *p)
{
	int o;

	pthread_mutex_lock(&p->mu);
	o = p->opening;
	pthread_mutex_unlock(&p->mu);
	return o;
}

/* Stop both threads and free everything a player holds -- or, for one
   opened by address that has not failed, tell its thread so and go: that
   thread frees it once its open or its read returns, so a close never
   waits on the network. */
void mv_close(mv_pl *p)
{
	double until = mv_now() + MV_CLOSEWAIT;

	pthread_mutex_lock(&p->mu);
	p->quit = 1;
	mv_wake(p);
	/* Its own thread, which a healthy stream lets finish at once: waited
	   for a moment, and if a read the network has stalled holds it, left
	   to free the player itself. Never while it is still connecting. */
	while (!p->opening && p->own && !p->failed && !p->dexit && mv_now() < until)
		mv_wait(p, 20);
	if (p->opening || (p->own && !p->failed && !p->dexit)) {
		p->orphan = 1;
		/* Before the lock goes: once it has, the thread may see the
		   orphan and free p, thread id and all. */
		pthread_detach(p->dth);
		pthread_mutex_unlock(&p->mu);
		return;
	}
	p->quit = 1;
	mv_wake(p);
	pthread_mutex_unlock(&p->mu);
	if (p->feed)
		mv_feedclose(p);
	if (p->dstarted)
		pthread_join(p->dth, 0);
	if (p->astarted)
		pthread_join(p->ath, 0);
	mv_free(p);
}

/* Free what a player holds, its threads already gone. */
void mv_free(mv_pl *p)
{
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
	free(p->eqz);
	free(p->ring);
	free(p->spec);
	free(p);
}
