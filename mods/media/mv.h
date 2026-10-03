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
	double seekto, skipto;
	int tcols, trows, mode, detail, gen, sgen, sw, sh, sfw, sfh, vfmt;
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
};

int mv_load(void);
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
mv_pl *mv_feed(int paused);
int mv_pipe(mv_pl *p, int kind);
void mv_feedseek(mv_pl *p);
void mv_feedclose(mv_pl *p);
void mv_pause(mv_pl *p, int on);
void mv_seek(mv_pl *p, double t);
double mv_clock(mv_pl *p);
int mv_frame(mv_pl *p);
void mv_size(mv_pl *p, int cols, int rows, int mode);
long mv_next(mv_pl *p);
void mv_fit(mv_pl *p, int *w, int *h);

int mv_draw(const dp_api *dp, mv_pl *p, int row, int col, int prow, int pcol,
	    int ph, int pw);

#endif
