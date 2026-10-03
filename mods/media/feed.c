#define _GNU_SOURCE

#include "mv.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

void *mv_codec(void *st, const void *dec, char **why);
void mv_recv(mv_pl *p, void *cc, void *fr, int video);
void mv_wait(mv_pl *p, int ms);
void mv_wake(mv_pl *p);
int mv_full(mv_pl *p);
void *mv_aud(void *arg);
int fd_high(int fd);

#ifndef MV_AVIOBUF
#define MV_AVIOBUF 65536
#endif

/* A player with no source of its own: its picture and its sound arrive on
   pipes (media pipe), each a stream in its own container -- what a web
   page hands its media source, taken by web take. */
mv_pl *mv_feed(int paused)
{
	mv_pl *p;

	if (mv_load() != HIBR_OK)
		return 0;
	p = xm(sizeof *p);
	memset(p, 0, sizeof *p);
	p->feed = 1;
	p->src = xs("(fed)");
	p->vol = 100;
	p->detail = 2;
	p->fpsmax = MV_FPS;
	p->paused = paused;
	p->vi = p->ai = -1;
	p->wstart = mv_now();
	pthread_mutex_init(&p->mu, 0);
	pthread_cond_init(&p->cv, 0);
	return p;
}

/* The pipe's reader: everything written to it goes into the input's
   queue at once, so a writer is never kept waiting by a decoder that has
   run as far ahead as it may. */
void *mv_prd(void *arg)
{
	mv_in *in = arg;
	mv_pl *p = in->p;
	char *buf = xm(MV_AVIOBUF);
	ssize_t n;

	for (;;) {
		n = read(in->rfd, buf, MV_AVIOBUF);
		if (n < 0 && errno == EINTR)
			continue;
		pthread_mutex_lock(&p->mu);
		if (n <= 0 || p->quit) {
			in->closed = 1;
			mv_wake(p);
			pthread_mutex_unlock(&p->mu);
			break;
		}
		if (in->qr > 0 && in->qr * 2 > in->q.n) {
			memmove(in->q.p, in->q.p + in->qr, in->q.n - in->qr);
			in->q.n -= in->qr;
			in->qr = 0;
		}
		s_add(&in->q, buf, (size_t)n);
		mv_wake(p);
		pthread_mutex_unlock(&p->mu);
	}
	free(buf);
	return 0;
}

/* FFmpeg reads the input through this: what has arrived, waiting while
   nothing has, and the end once a seek has moved the input on or the
   player is closing. */
int mv_rcb(void *op, uint8_t *buf, int n)
{
	mv_in *in = op;
	mv_pl *p = in->p;
	int gen, k;

	pthread_mutex_lock(&p->mu);
	gen = in->gen;
	for (;;) {
		if (p->quit || in->gen != gen || in->dgen != in->gen) {
			pthread_mutex_unlock(&p->mu);
			return MV_EOF;
		}
		if (in->qr < in->q.n)
			break;
		if (in->closed) {
			pthread_mutex_unlock(&p->mu);
			return MV_EOF;
		}
		mv_wait(p, 100);
	}
	k = (int)(in->q.n - in->qr);
	if (k > n)
		k = n;
	memcpy(buf, in->q.p + in->qr, (size_t)k);
	in->qr += (size_t)k;
	pthread_mutex_unlock(&p->mu);
	return k;
}

/* Where an init segment begins in the queue -- an MP4's ftyp box or a
   WebM's EBML header -- or -1. Anything before one is left over from
   before a seek and is dropped. */
long mv_initat(mv_in *in)
{
	size_t i;
	const unsigned char *q = (const unsigned char *)in->q.p;

	for (i = in->qr; i + 8 <= in->q.n; i++) {
		if (!memcmp(q + i + 4, "ftyp", 4))
			return (long)i;
		if (q[i] == 0x1A && q[i + 1] == 0x45 && q[i + 2] == 0xDF &&
		    q[i + 3] == 0xA3)
			return (long)i;
	}
	return -1;
}

/* Open the codec an input's stream needs, the player's own for its kind,
   and for sound the device and the sound thread the first time. */
int mv_fcodec(mv_in *in, void *st, const void *dec)
{
	mv_pl *p = in->p;
	void *cc, *par = mv_rdp(st, mv.st_par);
	char *why = 0;
	int video = in->kind == MV_VIDEO;

	cc = mv_codec(st, dec, &why);
	if (!cc) {
		pthread_mutex_lock(&p->mu);
		if (!p->err)
			p->err = why ? why : xs("cannot decode it");
		else
			free(why);
		pthread_mutex_unlock(&p->mu);
		return HIBR_FAIL;
	}
	pthread_mutex_lock(&p->mu);
	if (video) {
		if (p->vc)
			mv.cc_free(&p->vc);
		p->vc = cc;
		p->vtb = mv_rdq(st, mv.st_tb);
		p->vw = mv_rd32(par, mv.cp_w);
		p->vh = mv_rd32(par, mv.cp_h);
		free(p->vcodec);
		p->vcodec = xs(mv.codec_name(mv_rd32(par, mv.cp_id)));
		p->vi = 0;
	} else {
		if (p->ac)
			mv.cc_free(&p->ac);
		p->ac = cc;
		p->atb = mv_rdq(st, mv.st_tb);
		p->arate = mv_rd32(par, mv.cp_rate);
		p->ach = mv_rd32(par, mv.cp_chl + 4);
		free(p->acodec);
		p->acodec = xs(mv.codec_name(mv_rd32(par, mv.cp_id)));
		p->ai = 0;
	}
	if (in->fc && mv_rd64(in->fc, mv.fc_dur) != MV_NOPTS &&
	    (double)mv_rd64(in->fc, mv.fc_dur) / MV_TB > p->dur)
		p->dur = (double)mv_rd64(in->fc, mv.fc_dur) / MV_TB;
	pthread_mutex_unlock(&p->mu);
	if (!video && !p->ao.write) {
		mv_aopen(&p->ao, p->arate > 0 ? p->arate : 48000,
			 p->ach >= 2 ? 2 : 1, &why);
		pthread_mutex_lock(&p->mu);
		if (why && !p->err)
			p->err = why;
		else
			free(why);
		pthread_mutex_unlock(&p->mu);
		if (pthread_create(&p->ath, 0, mv_aud, p) == 0)
			p->astarted = 1;
	}
	return HIBR_OK;
}

/* An input's demuxer: wait for an init segment, open the container on
   the queue, decode its one stream until a seek or the end, and start
   again on the next init segment. */
void *mv_fdec(void *arg)
{
	mv_in *in = arg;
	mv_pl *p = in->p;
	void *pkt = mv.pkt_alloc(), *fr = mv.fr_alloc(), **sts, *cc;
	unsigned char *ab;
	const void *dec = 0;
	long at;
	int r, idx, gen;

	for (;;) {
		pthread_mutex_lock(&p->mu);
		for (;;) {
			if (p->quit)
				break;
			at = mv_initat(in);
			if (at >= 0) {
				in->qr = (size_t)at;
				break;
			}
			if (in->q.n - in->qr > 16)
				in->qr = in->q.n - 7;
			mv_wait(p, 100);
		}
		if (p->quit) {
			pthread_mutex_unlock(&p->mu);
			break;
		}
		gen = in->gen;
		in->dgen = gen;
		pthread_mutex_unlock(&p->mu);
		ab = mv.av_malloc(MV_AVIOBUF);
		in->avio = mv.avio_alloc(ab, MV_AVIOBUF, 0, in, mv_rcb, 0, 0);
		in->fc = mv.fmt_alloc();
		memcpy((char *)in->fc + mv.fc_pb, &in->avio, sizeof(void *));
		r = mv.open_input(&in->fc, 0, 0, 0);
		if (r >= 0)
			r = mv.find_info(in->fc, 0);
		idx = r >= 0 ? mv.best(in->fc, in->kind, -1, -1, &dec, 0) : -1;
		cc = 0;
		if (idx >= 0) {
			sts = mv_rdp(in->fc, mv.fc_streams);
			if (mv_fcodec(in, sts[idx], dec) == HIBR_OK)
				cc = in->kind == MV_VIDEO ? p->vc : p->ac;
		}
		while (cc) {
			pthread_mutex_lock(&p->mu);
			while (!p->quit && in->gen == gen && mv_full(p))
				mv_wait(p, 50);
			r = p->quit || in->gen != gen;
			pthread_mutex_unlock(&p->mu);
			if (r)
				break;
			if (mv.read(in->fc, pkt) < 0)
				break;
			if (mv_rd32(pkt, mv.pk_stream) == idx) {
				mv.send(cc, pkt);
				mv_recv(p, cc, fr, in->kind == MV_VIDEO);
			}
			mv.pkt_unref(pkt);
		}
		if (cc)
			mv.flush(cc);
		if (in->fc)
			mv.close_input(&in->fc);
		in->fc = 0;
		if (in->avio) {
			memcpy(&ab, (char *)in->avio + sizeof(void *), sizeof ab);
			mv.av_free(ab);
			mv.avio_free(&in->avio);
		}
		in->avio = 0;
		pthread_mutex_lock(&p->mu);
		if (in->gen == gen && !p->quit) {
			in->qr = in->q.n;
			if (in->closed)
				p->eof = 1;
			mv_wake(p);
		}
		pthread_mutex_unlock(&p->mu);
		if (in->closed && in->gen == gen)
			break;
	}
	mv.pkt_free(&pkt);
	mv.fr_free(&fr);
	return 0;
}

/* Give a fed player a pipe for its picture or its sound: the end to write
   to is the result, the other is read by a thread of the player's own. */
int mv_pipe(mv_pl *p, int kind)
{
	int fd[2], k = kind == MV_VIDEO ? 0 : 1;
	mv_in *in;

	if (!p->feed) {
		lg(HIBR_LERR, "media pipe: player %d plays its own source", p->id);
		return -1;
	}
	if (p->in[k]) {
		lg(HIBR_LERR, "media pipe: player %d has that pipe already", p->id);
		return -1;
	}
	if (pipe(fd) < 0) {
		lg(HIBR_LERR, "media pipe: %s", strerror(errno));
		return -1;
	}
	in = xm(sizeof *in);
	memset(in, 0, sizeof *in);
	in->p = p;
	in->kind = kind;
	in->rfd = fd_high(fd[0]);
	in->wfd = fd_high(fd[1]);
	fcntl(in->rfd, F_SETFD, FD_CLOEXEC);
	fcntl(in->wfd, F_SETFD, FD_CLOEXEC);
	s_init(&in->q);
	pthread_mutex_lock(&p->mu);
	p->in[k] = in;
	if (kind == MV_AUDIO)
		p->ai = 0;
	else if (p->vi < 0)
		p->vi = 0;
	pthread_mutex_unlock(&p->mu);
	if (pthread_create(&in->rth, 0, mv_prd, in) == 0)
		in->rstarted = 1;
	if (pthread_create(&in->dth, 0, mv_fdec, in) == 0)
		in->dstarted = 1;
	return in->wfd;
}

/* Move every fed input on, the mutex held: what was queued is dropped
   and each demuxer starts again on the next init segment. */
void mv_feedseek(mv_pl *p)
{
	int k;

	for (k = 0; k < 2; k++) {
		if (!p->in[k])
			continue;
		p->in[k]->gen++;
		p->in[k]->q.n = 0;
		p->in[k]->qr = 0;
	}
}

/* Stop a fed player's inputs: close the pipes, wait for their threads. */
void mv_feedclose(mv_pl *p)
{
	int k;
	mv_in *in;

	for (k = 0; k < 2; k++) {
		in = p->in[k];
		if (!in)
			continue;
		if (in->wfd >= 0)
			close(in->wfd);
		in->wfd = -1;
		if (in->rstarted)
			pthread_join(in->rth, 0);
		if (in->dstarted)
			pthread_join(in->dth, 0);
		close(in->rfd);
		s_free(&in->q);
		free(in);
		p->in[k] = 0;
	}
}
