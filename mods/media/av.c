#define _GNU_SOURCE

#include "mv.h"
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

mv_lib mv;

/* Where each field sits, per FFmpeg release, by libavformat's major
   number: 59 is 5.1, 60 is 6, 61 is 7, 62 is 8. Each line was taken with
   offsetof from that release's own headers (tools/mvoffsets.sh), never
   written by hand. Within one major the layout is FFmpeg's ABI promise. */
typedef struct mv_tab mv_tab;
struct mv_tab {
	int maj, umaj, smaj, rmaj;
	size_t fc_pb, fc_nb, fc_streams, fc_dur;
	size_t st_par, st_tb, st_dur, st_start;
	size_t cp_type, cp_id, cp_fmt, cp_w, cp_h, cp_rate, cp_chl;
	size_t fr_bets, fr_chl, fr_rate;
	size_t cc_pkttb;
};

static const mv_tab mv_tabs[] = {
	{ 62, 60, 9, 6, 32, 44, 48, 104, 16, 32, 48, 40,
	  0, 4, 44, 72, 76, 152, 128, 304, 384, 180, 92 },
	{ 61, 59, 8, 5, 32, 44, 48, 104, 16, 32, 48, 40,
	  0, 4, 44, 72, 76, 152, 128, 320, 408, 192, 92 },
	{ 60, 58, 7, 4, 32, 44, 48, 72, 16, 32, 48, 40,
	  0, 4, 28, 56, 60, 116, 144, 344, 448, 208, 716 },
	{ 59, 57, 6, 4, 32, 44, 48, 72, 208, 16, 32, 24,
	  0, 4, 28, 56, 60, 116, 144, 344, 448, 208, 724 },
	{ 0 }
};

/* The folders a library is looked for in, first HIBR_MEDIA_LIBDIR. */
static const char *mv_dirs[] = { "", "/opt/homebrew/lib/", "/usr/local/lib/",
				 "/opt/local/lib/", 0 };

/* Open one library by name and major number, trying each folder. */
void *mv_dlopen(const char *nm, int maj)
{
	str p;
	const char *env = getenv("HIBR_MEDIA_LIBDIR");
	void *h = 0;
	int i, k;

	s_init(&p);
	for (k = -1; !h && mv_dirs[k < 0 ? 0 : k]; k++) {
		const char *d = k < 0 ? (env ? env : 0) : mv_dirs[k];

		if (!d)
			continue;
		for (i = 0; i < 2 && !h; i++) {
			p.n = 0;
			s_cat(&p, d);
			if (k < 0 && *d && d[strlen(d) - 1] != '/')
				s_ch(&p, '/');
			s_cat(&p, "lib");
			s_cat(&p, nm);
			if (i == 0) {
				s_cat(&p, ".so.");
				s_num(&p, maj);
			} else {
				s_ch(&p, '.');
				s_num(&p, maj);
				s_cat(&p, ".dylib");
			}
			h = dlopen(p.p, RTLD_NOW | RTLD_LOCAL);
		}
	}
	if (h)
		lg(HIBR_LDBG, "media: loaded %s", p.p);
	s_free(&p);
	return h;
}

/* Look up one symbol, counting what is missing. */
void *mv_sym(void *h, const char *nm, int *bad)
{
	void *f = h ? dlsym(h, nm) : 0;

	if (!f) {
		lg(HIBR_LDBG, "media: no %s", nm);
		(*bad)++;
	}
	return f;
}

/* Load libav once: the newest release whose five libraries are all here,
   with its own table of where things are. A release this module was not
   built for is refused by name rather than guessed at. */
int mv_load(void)
{
	const mv_tab *t;
	void *f = 0, *c = 0, *u = 0, *sw = 0, *sr = 0;
	int bad = 0;

	if (mv.tried)
		return mv.ok ? HIBR_OK : HIBR_FAIL;
	mv.tried = 1;
	for (t = mv_tabs; t->maj; t++) {
		f = mv_dlopen("avformat", t->maj);
		if (!f)
			continue;
		c = mv_dlopen("avcodec", t->maj);
		u = mv_dlopen("avutil", t->umaj);
		sw = mv_dlopen("swscale", t->smaj);
		sr = mv_dlopen("swresample", t->rmaj);
		if (c && u && sw && sr)
			break;
		lg(HIBR_LDBG, "media: FFmpeg %d is incomplete", t->maj);
		f = 0;
	}
	if (!t->maj) {
		lg(HIBR_LERR, "media: no FFmpeg libraries (libavformat 59 to 62 "
			      "with libavcodec, libavutil, libswscale and "
			      "libswresample); HIBR_MEDIA_LIBDIR says where");
		return HIBR_FAIL;
	}
	mv.maj = t->maj;
	mv.fc_pb = t->fc_pb;
	mv.fc_nb = t->fc_nb;
	mv.fc_streams = t->fc_streams;
	mv.fc_dur = t->fc_dur;
	mv.st_par = t->st_par;
	mv.st_tb = t->st_tb;
	mv.st_dur = t->st_dur;
	mv.st_start = t->st_start;
	mv.cp_type = t->cp_type;
	mv.cp_id = t->cp_id;
	mv.cp_fmt = t->cp_fmt;
	mv.cp_w = t->cp_w;
	mv.cp_h = t->cp_h;
	mv.cp_rate = t->cp_rate;
	mv.cp_chl = t->cp_chl;
	mv.fr_data = 0;
	mv.fr_line = 64;
	mv.fr_w = 104;
	mv.fr_h = 108;
	mv.fr_ns = 112;
	mv.fr_fmt = 116;
	mv.fr_pts = 136;
	mv.fr_bets = t->fr_bets;
	mv.fr_chl = t->fr_chl;
	mv.fr_rate = t->fr_rate;
	mv.pk_stream = 36;
	mv.pk_pts = 8;
	mv.cc_pkttb = t->cc_pkttb;
	mv.fver = mv_sym(f, "avformat_version", &bad);
	mv.cver = mv_sym(c, "avcodec_version", &bad);
	mv.fmt_alloc = mv_sym(f, "avformat_alloc_context", &bad);
	mv.open_input = mv_sym(f, "avformat_open_input", &bad);
	mv.find_info = mv_sym(f, "avformat_find_stream_info", &bad);
	mv.best = mv_sym(f, "av_find_best_stream", &bad);
	mv.read = mv_sym(f, "av_read_frame", &bad);
	mv.seek = mv_sym(f, "avformat_seek_file", &bad);
	mv.close_input = mv_sym(f, "avformat_close_input", &bad);
	mv.avio_alloc = mv_sym(f, "avio_alloc_context", &bad);
	mv.avio_free = mv_sym(f, "avio_context_free", &bad);
	mv.net_init = mv_sym(f, "avformat_network_init", &bad);
	mv.cc_alloc = mv_sym(c, "avcodec_alloc_context3", &bad);
	mv.par_to = mv_sym(c, "avcodec_parameters_to_context", &bad);
	mv.cc_open = mv_sym(c, "avcodec_open2", &bad);
	mv.cc_free = mv_sym(c, "avcodec_free_context", &bad);
	mv.send = mv_sym(c, "avcodec_send_packet", &bad);
	mv.recv = mv_sym(c, "avcodec_receive_frame", &bad);
	mv.flush = mv_sym(c, "avcodec_flush_buffers", &bad);
	mv.pkt_alloc = mv_sym(c, "av_packet_alloc", &bad);
	mv.pkt_free = mv_sym(c, "av_packet_free", &bad);
	mv.pkt_unref = mv_sym(c, "av_packet_unref", &bad);
	mv.find_dec = mv_sym(c, "avcodec_find_decoder", &bad);
	mv.codec_name = mv_sym(c, "avcodec_get_name", &bad);
	mv.fr_alloc = mv_sym(u, "av_frame_alloc", &bad);
	mv.fr_free = mv_sym(u, "av_frame_free", &bad);
	mv.fr_unref = mv_sym(u, "av_frame_unref", &bad);
	mv.av_malloc = mv_sym(u, "av_malloc", &bad);
	mv.av_free = mv_sym(u, "av_free", &bad);
	mv.opt_set = mv_sym(u, "av_opt_set", &bad);
	mv.dict_set = mv_sym(u, "av_dict_set", &bad);
	mv.dict_free = mv_sym(u, "av_dict_free", &bad);
	mv.av_err = mv_sym(u, "av_strerror", &bad);
	mv.log_level = mv_sym(u, "av_log_set_level", &bad);
	mv.chl_default = mv_sym(u, "av_channel_layout_default", &bad);
	mv.sws_get = mv_sym(sw, "sws_getContext", &bad);
	mv.sws_scale = mv_sym(sw, "sws_scale", &bad);
	mv.sws_free = mv_sym(sw, "sws_freeContext", &bad);
	mv.swr_alloc2 = mv_sym(sr, "swr_alloc_set_opts2", &bad);
	mv.swr_init = mv_sym(sr, "swr_init", &bad);
	mv.swr_convert = mv_sym(sr, "swr_convert", &bad);
	mv.swr_free = mv_sym(sr, "swr_free", &bad);
	if (bad) {
		lg(HIBR_LERR, "media: FFmpeg %d lacks %d functions this needs",
		   t->maj, bad);
		return HIBR_FAIL;
	}
	if ((int)(mv.fver() >> 16) != t->maj || (int)(mv.cver() >> 16) != t->maj) {
		lg(HIBR_LERR, "media: libavformat %u and libavcodec %u are not one "
			      "FFmpeg release", mv.fver() >> 16, mv.cver() >> 16);
		return HIBR_FAIL;
	}
	mv.log_level(-8);
	mv.net_init();
	mv.ok = 1;
	lg(HIBR_LDBG, "media: FFmpeg libavformat %d", t->maj);
	return HIBR_OK;
}

/* Read an int at an offset into a libav struct. */
int mv_rd32(void *p, size_t off)
{
	int v;

	memcpy(&v, (char *)p + off, sizeof v);
	return v;
}

/* Read a 64-bit integer at an offset. */
int64_t mv_rd64(void *p, size_t off)
{
	int64_t v;

	memcpy(&v, (char *)p + off, sizeof v);
	return v;
}

/* Read a pointer at an offset. */
void *mv_rdp(void *p, size_t off)
{
	void *v;

	memcpy(&v, (char *)p + off, sizeof v);
	return v;
}

/* Read an AVRational at an offset, as a number. */
double mv_rdq(void *p, size_t off)
{
	int q[2];

	memcpy(q, (char *)p + off, sizeof q);
	return q[1] ? (double)q[0] / q[1] : 0;
}

/* What a libav error means, as allocated text. */
char *mv_averr(int e)
{
	char *b = xm(HIBR_IOCH);

	if (!mv.av_err || mv.av_err(e, b, HIBR_IOCH) < 0)
		snprintf(b, HIBR_IOCH, "error %d", e);
	return b;
}

/* Seconds on a clock that only goes forward. */
double mv_now(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec + ts.tv_nsec / 1e9;
}
