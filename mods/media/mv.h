#ifndef HIBR_MV_H
#define HIBR_MV_H

#include "hibr.h"
#include "../display.h"
#include <pthread.h>
#include <stddef.h>
#include <stdint.h>

/* How far ahead decoding may run, in seconds, for audio and for video. */
#ifndef MV_AHEAD
#define MV_AHEAD 3.0
#endif

/* How many decoded video frames are kept waiting at most. */
#ifndef MV_VQ
#define MV_VQ 96
#endif

/* The fastest frames are drawn by default, a second. */
/* How long, in microseconds, a network source may stay silent before its
   open or a read gives up: ffmpeg's own default is for ever. */
#ifndef MV_RWTIMEOUT
#define MV_RWTIMEOUT "15000000"
#endif
/* How long, in seconds, a close lets a player opened by address finish
   before leaving it to free itself. */
#ifndef MV_CLOSEWAIT
#define MV_CLOSEWAIT 0.25
#endif
#ifndef MV_FPS
#define MV_FPS 24
#endif

/* libav's own constants this module uses, stable across 5.1 to 8. */
#ifndef MV_AVCONST
#define MV_AVCONST
#define MV_VIDEO 0
#define MV_AUDIO 1
#define MV_RGB24 2
#define MV_S16 1
#define MV_EAGAIN (-11)
#define MV_EOF (-541478725)
#define MV_NOPTS INT64_MIN
#define MV_SWS_AREA 0x20
#define MV_TB 1000000
#endif

/* The libav libraries, loaded on first use, with where each field this
   module reads sits in the version that was found. */
typedef struct mv_lib mv_lib;
struct mv_lib {
	int ok, tried, maj;
	size_t fc_pb, fc_nb, fc_streams, fc_dur;
	size_t st_par, st_tb, st_dur, st_start;
	size_t cp_type, cp_id, cp_fmt, cp_w, cp_h, cp_rate, cp_chl;
	size_t fr_data, fr_line, fr_w, fr_h, fr_ns, fr_fmt, fr_pts, fr_bets;
	size_t fr_chl, fr_rate;
	size_t pk_stream, pk_pts;
	size_t cc_pkttb;
	unsigned (*fver)(void);
	unsigned (*cver)(void);
	void *(*fmt_alloc)(void);
	int (*open_input)(void **, const char *, void *, void **);
	int (*find_info)(void *, void **);
	int (*best)(void *, int, int, int, const void **, int);
	int (*read)(void *, void *);
	int (*seek)(void *, int, int64_t, int64_t, int64_t, int);
	void (*close_input)(void **);
	void *(*avio_alloc)(unsigned char *, int, int, void *,
			    int (*)(void *, uint8_t *, int),
			    int (*)(void *, const uint8_t *, int),
			    int64_t (*)(void *, int64_t, int));
	void (*avio_free)(void **);
	int (*net_init)(void);
	void *(*cc_alloc)(const void *);
	int (*par_to)(void *, const void *);
	int (*cc_open)(void *, const void *, void **);
	void (*cc_free)(void **);
	int (*send)(void *, const void *);
	int (*recv)(void *, void *);
	void (*flush)(void *);
	void *(*pkt_alloc)(void);
	void (*pkt_free)(void **);
	void (*pkt_unref)(void *);
	const void *(*find_dec)(int);
	const char *(*codec_name)(int);
	void *(*fr_alloc)(void);
	void (*fr_free)(void **);
	void (*fr_unref)(void *);
	void *(*av_malloc)(size_t);
	void (*av_free)(void *);
	int (*opt_set)(void *, const char *, const char *, int);
	int (*dict_set)(void **, const char *, const char *, int);
	void (*dict_free)(void **);
	int (*av_err)(int, char *, size_t);
	void (*log_level)(int);
	void *(*sws_get)(int, int, int, int, int, int, int, void *, void *,
			 const double *);
	int (*sws_scale)(void *, const uint8_t *const *, const int *, int, int,
			 uint8_t *const *, const int *);
	void (*sws_free)(void *);
	int (*swr_alloc2)(void **, const void *, int, int, const void *, int, int,
			  int, void *);
	int (*swr_init)(void *);
	int (*swr_convert)(void *, uint8_t **, int, const uint8_t **, int);
	void (*swr_free)(void **);
	void (*chl_default)(void *, int);
};

extern mv_lib mv;

/* Where audio goes: a sound device, or nowhere at the pace of one. */
typedef struct mv_ao mv_ao;
struct mv_ao {
	const char *name;
	int rate, ch;
	void *h;
	void *st;
	int (*open)(mv_ao *, int rate, int ch);
	int (*write)(mv_ao *, const int16_t *, int frames);
	double (*delay)(mv_ao *);
	void (*flush)(mv_ao *);
	void (*close)(mv_ao *);
};

/* A decoded picture, already the size it is drawn at. */
typedef struct mv_vf mv_vf;
struct mv_vf {
	double pts;
	int w, h;
	uint8_t *rgb;
};

/* A run of decoded sound, interleaved 16-bit. */
typedef struct mv_af mv_af;
struct mv_af {
	double pts;
	int n, off;
	int16_t *s;
	mv_af *next;
};

/* One fed input of a player: bytes arriving on a pipe, read by FFmpeg
   through a callback, demuxed by a thread of its own. gen moves on at a
   seek, which ends the demuxer reading the old bytes. */
typedef struct mv_in mv_in;
struct mv_in {
	struct mv_pl *p;
	int kind, rfd, wfd, closed, gen, dgen;
	str q;
	size_t qr;
	pthread_t rth, dth;
	int rstarted, dstarted;
	void *fc, *avio;
};

/* A player: one source, its decoder and sound threads, and the frame
   shown. Everything below the mutex is shared with those threads. */
typedef struct mv_pl mv_pl;
struct mv_pl {
	int id;
	char *src;
	void *fc, *vc, *ac, *sws, *swr;
	int vi, ai;
	double vtb, atb, dur, fps;
	int vw, vh, arate, ach;
	char *vcodec, *acodec;
	mv_ao ao;
	pthread_t dth, ath;
	int dstarted, astarted;
	pthread_mutex_t mu;
	pthread_cond_t cv;
	int quit, paused, eof, seekreq, ended, failed;
	/* A source by address is opened in the player's own thread, never
	   the caller's: opening while it connects and probes, during which
	   nothing that thread sets may be read; orphan when it was closed
	   meanwhile, and frees itself. */
	int opening, orphan;
	/* Opened by address, its streams known, waiting for the caller's
	   thread to open the sound device: decoding needs its rate. */
	int aowait;
	/* Opened by address: its own thread frees it, so no close ever
	   waits on a read the network has stalled. */
	int own;
	/* Its thread has finished, for a close to join it. */
	int dexit;
	double seekto, skipto;
	int tcols, trows, mode, detail, gen, sgen, sw, sh, sfw, sfh, vfmt;
	/* What the last draw actually put on screen: 1 a bitmap, 0 cells.
	   `mode` is what was *asked for*, and the two differ whenever the
	   pixel path is refused -- by the display having no answer about
	   pictures yet, by the terminal not drawing them, or by the frame
	   not fitting the rectangle it was clipped to. The player then
	   draws cells and says nothing, which reads as the mode being
	   ignored (Gitea #199). A caller that shows the mode should show
	   this instead, or it reports an intention as a fact. */
	int drew;
	/* What a cell measures on the terminal, from the display, or 0
	   before any draw has asked it. A bitmap is drawn at the
	   terminal's own pixels, so it is what the frame is decoded at
	   for the pixel mode -- see mv_fitpx. */
	int cw, ch;
	int agen, aflush;
	double alast;
	mv_vf vq[MV_VQ];
	int vqh, vqn;
	mv_af *aq, *aqt;
	double aqd;
	double aclock, ats, wbase, wstart, pausedat;
	int haveclock;
	int vol;
	char *err;
	mv_vf cur;
	int curnew;
	double lastdraw;
	int fpsmax;
	int feed;
	mv_in *in[2];
	/* The EQ: what the verb sets, under mu -- on, the preamp and ten
	   bands in dB, a ten-band graphic equaliser's -- and a generation the
	   sound thread compares against the one it last made coefficients
	   for; the coefficients and each channel's history are the thread's. */
	int eqon, eqgen, eqdone, eqrate, eqch;
	double eqpre, eqdb[10], eqmul;
	double eqc[10][5];
	double *eqz;
	/* The newest sound written to the device, as stereo frames in a
	   ring, under mu: what spectrum and wave read. */
	int16_t *ring;
	int ringn, ringw;
	/* The sound thread's newest spectrum, each bin's squared magnitude,
	   under mu, and when it was made. */
	double *spec, specat;
};

#ifndef MV_RINGN
#define MV_RINGN 2048
#endif
#ifndef MV_FFTN
#define MV_FFTN 1024
#endif
#ifndef MV_SPECMS
#define MV_SPECMS 30
#endif
void mv_specupdate(mv_pl *p);
int mv_eqsync(mv_pl *p, int ch);
void mv_eqrun(mv_pl *p, int16_t *s, int n, int ch);
void mv_ringpush(mv_pl *p, const int16_t *s, int n, int ch);
int mv_ringcopy(mv_pl *p, double *l, double *r, int n);
void mv_spectrum(mv_pl *p, int bands, str *o);
void mv_wave(mv_pl *p, int points, str *o);

int mv_load(void);
void *mv_warm(void *arg);
void *mv_sym(void *h, const char *nm, int *bad);
int mv_rd32(void *p, size_t off);
int64_t mv_rd64(void *p, size_t off);
void *mv_rdp(void *p, size_t off);
double mv_rdq(void *p, size_t off);
char *mv_averr(int e);
double mv_now(void);

int mv_aopen(mv_ao *a, int rate, int ch, char **why);

mv_pl *mv_open(sh *s, const char *src, int paused);
void mv_close(mv_pl *p);
mv_pl *mv_new(const char *src, int paused);
int mv_probe(const char *src, void **fc, char **why);
int mv_setup(mv_pl *p, char **why, char **note, int sound);
void mv_aready(mv_pl *p);
void mv_pin(void);
void *mv_opener(void *a);
int mv_opening(mv_pl *p);
void mv_free(mv_pl *p);
void mv_done(mv_pl *p);
mv_pl *mv_feed(int paused);
int mv_pipe(mv_pl *p, int kind);
void mv_feedseek(mv_pl *p);
void mv_feedclose(mv_pl *p);
void mv_pause(mv_pl *p, int on);
void mv_seek(mv_pl *p, double t);
double mv_clock(mv_pl *p);
double mv_clocklk(mv_pl *p);
int mv_frame(mv_pl *p);
void mv_size(mv_pl *p, int cols, int rows, int mode);
long mv_next(mv_pl *p);
void mv_fit(mv_pl *p, int mode, int *w, int *h);
void mv_fitpx(mv_pl *p, int *w, int *h);

int mv_draw(sh *s, const dp_api *dp, mv_pl *p, const char *pane, int row,
	    int col, int prow, int pcol, int ph, int pw);

#endif
