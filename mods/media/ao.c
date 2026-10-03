#define _GNU_SOURCE

#include "mv.h"
#include <dlfcn.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* --- nowhere, at the pace of a device ----------------------------------- */

/* The pacing state of a device that plays nothing: when it started, and
   how many frames it has been given. */
typedef struct mv_pace mv_pace;
struct mv_pace {
	double t0;
	long long frames;
	FILE *wav;
	long long bytes;
};

/* Open the silent output. */
int ao_nopen(mv_ao *a, int rate, int ch)
{
	mv_pace *p = xm(sizeof *p);

	memset(p, 0, sizeof *p);
	a->st = p;
	a->rate = rate;
	a->ch = ch;
	return HIBR_OK;
}

/* Sleep until what was written would have played, keeping a tenth of a
   second ahead, the way a real device's buffer would. */
int ao_nwrite(mv_ao *a, const int16_t *s, int n)
{
	mv_pace *p = a->st;
	double ahead;

	(void)s;
	if (!p->t0)
		p->t0 = mv_now();
	if (p->wav) {
		fwrite(s, sizeof *s, (size_t)n * a->ch, p->wav);
		p->bytes += (long long)n * a->ch * 2;
	}
	p->frames += n;
	ahead = (double)p->frames / a->rate - (mv_now() - p->t0);
	if (ahead > 0.1)
		usleep((useconds_t)((ahead - 0.1) * 1e6));
	return HIBR_OK;
}

/* What has been written and not yet played. */
double ao_ndelay(mv_ao *a)
{
	mv_pace *p = a->st;
	double d;

	if (!p->t0)
		return 0;
	d = (double)p->frames / a->rate - (mv_now() - p->t0);
	return d > 0 ? d : 0;
}

/* Forget what was waiting: the next write starts the pace again. */
void ao_nflush(mv_ao *a)
{
	mv_pace *p = a->st;

	p->t0 = 0;
	p->frames = 0;
}

/* Write a little-endian number of n bytes. */
void ao_le(FILE *f, unsigned long v, int n)
{
	while (n--) {
		fputc((int)(v & 255), f);
		v >>= 8;
	}
}

/* Write a WAV header for the bytes so far. */
void ao_wavhead(mv_ao *a, mv_pace *p)
{
	fseek(p->wav, 0, SEEK_SET);
	fwrite("RIFF", 1, 4, p->wav);
	ao_le(p->wav, (unsigned long)(36 + p->bytes), 4);
	fwrite("WAVEfmt ", 1, 8, p->wav);
	ao_le(p->wav, 16, 4);
	ao_le(p->wav, 1, 2);
	ao_le(p->wav, (unsigned long)a->ch, 2);
	ao_le(p->wav, (unsigned long)a->rate, 4);
	ao_le(p->wav, (unsigned long)a->rate * a->ch * 2, 4);
	ao_le(p->wav, (unsigned long)a->ch * 2, 2);
	ao_le(p->wav, 16, 2);
	fwrite("data", 1, 4, p->wav);
	ao_le(p->wav, (unsigned long)p->bytes, 4);
	fseek(p->wav, 0, SEEK_END);
}

/* Close the silent output, finishing a WAV file if it wrote one. */
void ao_nclose(mv_ao *a)
{
	mv_pace *p = a->st;

	if (!p)
		return;
	if (p->wav) {
		ao_wavhead(a, p);
		fclose(p->wav);
	}
	free(p);
	a->st = 0;
}

/* --- ALSA, on Linux ------------------------------------------------------ */

/* The few libasound calls used, loaded at run time. */
typedef struct ao_alsa ao_alsa;
struct ao_alsa {
	void *pcm;
	int (*open)(void **, const char *, int, int);
	int (*setp)(void *, int, int, unsigned, unsigned, int, unsigned);
	long (*writei)(void *, const void *, unsigned long);
	int (*recover)(void *, int, int);
	int (*delay)(void *, long *);
	int (*drop)(void *);
	int (*prepare)(void *);
	int (*close)(void *);
	int (*errh)(void *);
};

/* Says nothing: libasound's own complaints would land in the desktop's log
   every time a machine with no sound card opened a video. */
void ao_quiet(const char *f, int l, const char *fn, int e, const char *fmt, ...)
{
	(void)f;
	(void)l;
	(void)fn;
	(void)e;
	(void)fmt;
}

/* Open the default ALSA device for 16-bit interleaved sound. */
int ao_aopen(mv_ao *a, int rate, int ch)
{
	ao_alsa *x;
	void *h = dlopen("libasound.so.2", RTLD_NOW | RTLD_LOCAL);
	int bad = 0, e;

	if (!h)
		return HIBR_FAIL;
	x = xm(sizeof *x);
	memset(x, 0, sizeof *x);
	x->open = mv_sym(h, "snd_pcm_open", &bad);
	x->setp = mv_sym(h, "snd_pcm_set_params", &bad);
	x->writei = mv_sym(h, "snd_pcm_writei", &bad);
	x->recover = mv_sym(h, "snd_pcm_recover", &bad);
	x->delay = mv_sym(h, "snd_pcm_delay", &bad);
	x->drop = mv_sym(h, "snd_pcm_drop", &bad);
	x->prepare = mv_sym(h, "snd_pcm_prepare", &bad);
	x->close = mv_sym(h, "snd_pcm_close", &bad);
	x->errh = dlsym(h, "snd_lib_error_set_handler");
	if (bad) {
		free(x);
		dlclose(h);
		return HIBR_FAIL;
	}
	if (x->errh)
		x->errh((void *)ao_quiet);
	e = x->open(&x->pcm, "default", 0, 0);
	if (e >= 0)
		e = x->setp(x->pcm, 2, 3, (unsigned)ch, (unsigned)rate, 1, 150000);
	if (e < 0) {
		if (x->pcm)
			x->close(x->pcm);
		free(x);
		dlclose(h);
		return HIBR_FAIL;
	}
	a->h = h;
	a->st = x;
	a->rate = rate;
	a->ch = ch;
	return HIBR_OK;
}

/* Write sound to ALSA, recovering from an underrun. */
int ao_awrite(mv_ao *a, const int16_t *s, int n)
{
	ao_alsa *x = a->st;
	long w;

	while (n > 0) {
		w = x->writei(x->pcm, s, (unsigned long)n);
		if (w < 0) {
			if (x->recover(x->pcm, (int)w, 1) < 0)
				return HIBR_FAIL;
			continue;
		}
		s += w * a->ch;
		n -= (int)w;
	}
	return HIBR_OK;
}

/* How long until the last sample written is heard. */
double ao_adelay(mv_ao *a)
{
	ao_alsa *x = a->st;
	long d = 0;

	if (x->delay(x->pcm, &d) < 0 || d < 0)
		return 0;
	return (double)d / a->rate;
}

/* Throw away what is waiting to be played, for a pause or a seek. */
void ao_aflush(mv_ao *a)
{
	ao_alsa *x = a->st;

	x->drop(x->pcm);
	x->prepare(x->pcm);
}

/* Close the ALSA device. */
void ao_aclose(mv_ao *a)
{
	ao_alsa *x = a->st;

	if (!x)
		return;
	x->drop(x->pcm);
	x->close(x->pcm);
	free(x);
	dlclose(a->h);
	a->st = 0;
	a->h = 0;
}

/* --- AudioQueue, on macOS ------------------------------------------------ */

/* The format AudioQueue is given: AudioStreamBasicDescription. */
typedef struct ao_asbd ao_asbd;
struct ao_asbd {
	double rate;
	unsigned fmt, flags, bpp, fpp, bpf, ch, bits, res;
};

/* AudioQueue's own buffer: AudioQueueBuffer. */
typedef struct ao_aqb ao_aqb;
struct ao_aqb {
	unsigned cap;
	void *data;
	unsigned size;
	void *user;
	unsigned pdcap;
	void *pd;
	unsigned pdn;
};

#ifndef AO_AQBUFS
#define AO_AQBUFS 3
#define AO_AQFRAMES 2048
#endif

/* The AudioQueue output: the queue, its buffers, and a ring the writer
   fills and the queue's own thread empties. */
typedef struct ao_aq ao_aq;
struct ao_aq {
	void *q;
	ao_aqb *bufs[AO_AQBUFS];
	int (*newout)(const ao_asbd *, void (*)(void *, void *, ao_aqb *), void *,
		      void *, void *, unsigned, void **);
	int (*alloc)(void *, unsigned, ao_aqb **);
	int (*enq)(void *, ao_aqb *, unsigned, void *);
	int (*start)(void *, void *);
	int (*stop)(void *, int);
	int (*dispose)(void *, int);
	int (*reset)(void *);
	int16_t *ring;
	size_t cap, rd, wr, fill;
	pthread_mutex_t mu;
	pthread_cond_t cv;
	int ch;
};

/* The queue wants a buffer filled: from the ring, silence for what the
   ring does not have, and back on the queue. */
void ao_qcb(void *user, void *q, ao_aqb *b)
{
	ao_aq *x = user;
	int16_t *d = b->data;
	size_t want = b->cap / sizeof(int16_t), i;

	pthread_mutex_lock(&x->mu);
	for (i = 0; i < want; i++) {
		if (x->fill) {
			d[i] = x->ring[x->rd];
			x->rd = (x->rd + 1) % x->cap;
			x->fill--;
		} else {
			d[i] = 0;
		}
	}
	pthread_cond_broadcast(&x->cv);
	pthread_mutex_unlock(&x->mu);
	b->size = b->cap;
	x->enq(q, b, 0, 0);
}

/* Open an AudioQueue for 16-bit interleaved sound. */
int ao_qopen(mv_ao *a, int rate, int ch)
{
	void *h = dlopen("/System/Library/Frameworks/AudioToolbox.framework/"
			 "AudioToolbox", RTLD_NOW | RTLD_LOCAL);
	ao_aq *x;
	ao_asbd f;
	int bad = 0, i;

	if (!h)
		return HIBR_FAIL;
	x = xm(sizeof *x);
	memset(x, 0, sizeof *x);
	x->newout = mv_sym(h, "AudioQueueNewOutput", &bad);
	x->alloc = mv_sym(h, "AudioQueueAllocateBuffer", &bad);
	x->enq = mv_sym(h, "AudioQueueEnqueueBuffer", &bad);
	x->start = mv_sym(h, "AudioQueueStart", &bad);
	x->stop = mv_sym(h, "AudioQueueStop", &bad);
	x->dispose = mv_sym(h, "AudioQueueDispose", &bad);
	x->reset = mv_sym(h, "AudioQueueReset", &bad);
	if (bad) {
		free(x);
		dlclose(h);
		return HIBR_FAIL;
	}
	memset(&f, 0, sizeof f);
	f.rate = rate;
	f.fmt = 0x6C70636Du;
	f.flags = 4u | 8u;
	f.bpf = f.bpp = (unsigned)ch * 2;
	f.fpp = 1;
	f.ch = (unsigned)ch;
	f.bits = 16;
	x->ch = ch;
	x->cap = (size_t)rate * ch / 2;
	x->ring = xm(x->cap * sizeof *x->ring);
	pthread_mutex_init(&x->mu, 0);
	pthread_cond_init(&x->cv, 0);
	if (x->newout(&f, ao_qcb, x, 0, 0, 0, &x->q)) {
		free(x->ring);
		free(x);
		dlclose(h);
		return HIBR_FAIL;
	}
	for (i = 0; i < AO_AQBUFS; i++) {
		if (x->alloc(x->q, AO_AQFRAMES * (unsigned)ch * 2, &x->bufs[i]))
			break;
		memset(x->bufs[i]->data, 0, x->bufs[i]->cap);
		x->bufs[i]->size = x->bufs[i]->cap;
		x->enq(x->q, x->bufs[i], 0, 0);
	}
	x->start(x->q, 0);
	a->h = h;
	a->st = x;
	a->rate = rate;
	a->ch = ch;
	return HIBR_OK;
}

/* Put sound in the ring, waiting while it is full. */
int ao_qwrite(mv_ao *a, const int16_t *s, int n)
{
	ao_aq *x = a->st;
	size_t left = (size_t)n * a->ch;

	pthread_mutex_lock(&x->mu);
	while (left) {
		while (x->fill == x->cap)
			pthread_cond_wait(&x->cv, &x->mu);
		while (left && x->fill < x->cap) {
			x->ring[x->wr] = *s++;
			x->wr = (x->wr + 1) % x->cap;
			x->fill++;
			left--;
		}
	}
	pthread_mutex_unlock(&x->mu);
	return HIBR_OK;
}

/* The ring and the queue's buffers, in seconds. */
double ao_qdelay(mv_ao *a)
{
	ao_aq *x = a->st;
	double d;

	pthread_mutex_lock(&x->mu);
	d = (double)x->fill / a->ch / a->rate;
	pthread_mutex_unlock(&x->mu);
	return d + (double)AO_AQBUFS * AO_AQFRAMES / a->rate;
}

/* Empty the ring. */
void ao_qflush(mv_ao *a)
{
	ao_aq *x = a->st;

	pthread_mutex_lock(&x->mu);
	x->rd = x->wr = x->fill = 0;
	pthread_cond_broadcast(&x->cv);
	pthread_mutex_unlock(&x->mu);
}

/* Stop and dispose of the queue. */
void ao_qclose(mv_ao *a)
{
	ao_aq *x = a->st;

	if (!x)
		return;
	x->stop(x->q, 1);
	x->dispose(x->q, 1);
	pthread_mutex_destroy(&x->mu);
	pthread_cond_destroy(&x->cv);
	free(x->ring);
	free(x);
	dlclose(a->h);
	a->st = 0;
	a->h = 0;
}

/* --- choosing one -------------------------------------------------------- */

/* Use the silent output, or the WAV file one when told a path. */
int ao_null(mv_ao *a, int rate, int ch, const char *wav)
{
	mv_pace *p;

	a->name = wav ? "wav" : "none";
	a->open = ao_nopen;
	a->write = ao_nwrite;
	a->delay = ao_ndelay;
	a->flush = ao_nflush;
	a->close = ao_nclose;
	ao_nopen(a, rate, ch);
	if (wav) {
		p = a->st;
		p->wav = fopen(wav, "wb");
		if (!p->wav) {
			a->name = "none";
			return HIBR_FAIL;
		}
		ao_wavhead(a, p);
	}
	return HIBR_OK;
}

/* Open somewhere for sound to go: HIBR_MEDIA_AUDIO names it -- alsa,
   audioqueue, none, or wav:PATH -- or the platform's own is tried, and
   no device at all plays silently at the same pace, saying why. */
int mv_aopen(mv_ao *a, int rate, int ch, char **why)
{
	const char *w = getenv("HIBR_MEDIA_AUDIO");

	memset(a, 0, sizeof *a);
	*why = 0;
	if (w && !strncmp(w, "wav:", 4))
		return ao_null(a, rate, ch, w + 4);
	if (w && !strcmp(w, "none"))
		return ao_null(a, rate, ch, 0);
	if (!w || !strcmp(w, "audioqueue")) {
		if (ao_qopen(a, rate, ch) == HIBR_OK) {
			a->name = "audioqueue";
			a->write = ao_qwrite;
			a->delay = ao_qdelay;
			a->flush = ao_qflush;
			a->close = ao_qclose;
			return HIBR_OK;
		}
	}
	if (!w || !strcmp(w, "alsa")) {
		if (ao_aopen(a, rate, ch) == HIBR_OK) {
			a->name = "alsa";
			a->write = ao_awrite;
			a->delay = ao_adelay;
			a->flush = ao_aflush;
			a->close = ao_aclose;
			return HIBR_OK;
		}
	}
	*why = xs("no sound device");
	return ao_null(a, rate, ch, 0);
}
