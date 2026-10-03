#define _GNU_SOURCE

#include "mv.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void v_del(sh *s, const char *k);

vec mv_pls;
int mv_nextid;
const dp_api *mv_dp;

/* Say a result: into $RET, and printed unless := is taking it. */
void mv_say(sh *s, const char *t)
{
	hibr_ret(s, t);
	if (!s->bind)
		printf("%s\n", t);
}

/* A player by its number, saying so when there is none. */
mv_pl *mv_get(const char *id)
{
	size_t i;
	mv_pl *p;
	int n = atoi(id);

	for (i = 0; i < mv_pls.n; i++) {
		p = mv_pls.p[i];
		if (p->id == n)
			return p;
	}
	lg(HIBR_LERR, "media: no player %s", id);
	return 0;
}

/* Close a player and forget it. */
void mv_forget(mv_pl *p)
{
	size_t i;

	for (i = 0; i < mv_pls.n; i++) {
		if (mv_pls.p[i] != p)
			continue;
		memmove(mv_pls.p + i, mv_pls.p + i + 1,
			(mv_pls.n - i - 1) * sizeof *mv_pls.p);
		mv_pls.n--;
		break;
	}
	mv_close(p);
}

/* A drawing mode by name: half, ascii or mono. */
int mv_modeof(const char *m)
{
	if (!strcmp(m, "ascii"))
		return 1;
	if (!strcmp(m, "mono"))
		return 2;
	if (!strcmp(m, "half"))
		return 0;
	return -1;
}

/* Set one field of the info map, or print it as name, tab, value. */
void mv_field(sh *s, const char *k, const char *v)
{
	char *ks[1];

	if (s->bind) {
		ks[0] = (char *)k;
		hibr_setp(s, "RET", ks, 1, v ? v : "");
	} else {
		printf("%s\t%s\n", k, v ? v : "");
	}
}

/* A number with three decimals, as text. */
void mv_fnum(str *b, double v)
{
	char *t = xm(64);

	snprintf(t, 64, "%.3f", v);
	b->n = 0;
	s_cat(b, t);
	free(t);
}

/* media info ID: where it is and what it is. */
int mv_info(sh *s, mv_pl *p)
{
	str b;
	double c = mv_clock(p);

	s_init(&b);
	if (s->bind) {
		v_del(s, "RET");
		hibr_set(s, "RET", "", 0);
	}
	pthread_mutex_lock(&p->mu);
	if (p->dur > 0 && c > p->dur)
		c = p->dur;
	mv_fnum(&b, c < 0 ? 0 : c);
	mv_field(s, "pos", b.p);
	mv_fnum(&b, p->dur);
	mv_field(s, "dur", b.p);
	mv_fnum(&b, p->cur.rgb ? p->cur.pts : 0);
	mv_field(s, "shown", b.p);
	mv_field(s, "paused", p->paused ? "1" : "0");
	mv_field(s, "ended", p->ended ? "1" : "0");
	b.n = 0;
	s_num(&b, p->vw);
	mv_field(s, "width", b.p);
	b.n = 0;
	s_num(&b, p->vh);
	mv_field(s, "height", b.p);
	b.n = 0;
	s_num(&b, p->vol);
	mv_field(s, "volume", b.p);
	mv_field(s, "video", p->vi >= 0 ? p->vcodec : "");
	mv_field(s, "audio", p->ai >= 0 ? p->acodec : "");
	mv_field(s, "output", p->ai >= 0 ? p->ao.name : "");
	mv_field(s, "mode", p->mode == 1 ? "ascii" : p->mode == 2 ? "mono" : "half");
	mv_field(s, "error", p->err ? p->err : "");
	pthread_mutex_unlock(&p->mu);
	s_free(&b);
	return HIBR_OK;
}

/* media draw ID row col [-p pane]: the frame shown, into the display. */
int mv_drawcmd(sh *s, mv_pl *p, int ac, char **av)
{
	const char *pane = 0;
	int i, n = 0, v[2] = { 0, 0 }, prow = 0, pcol = 0, ph = 0, pw = 0;

	for (i = 3; i < ac; i++) {
		if (!strcmp(av[i], "-p") && i + 1 < ac) {
			pane = av[++i];
			continue;
		}
		if (n < 2)
			v[n++] = atoi(av[i]);
	}
	if (n < 2) {
		lg(HIBR_LERR, "usage: media draw id row col [-p pane]");
		return 2;
	}
	if (!mv_dp)
		mv_dp = (const dp_api *)hibr_require(s, "display", DP_API_VER);
	if (!mv_dp || !mv_dp->isopen()) {
		lg(HIBR_LERR, "media draw: no display open");
		return HIBR_FAIL;
	}
	if (pane) {
		if (!mv_dp->prect || !mv_dp->prect(pane, &prow, &pcol, &ph, &pw)) {
			lg(HIBR_LERR, "media draw: no such pane: %s", pane);
			return HIBR_FAIL;
		}
		v[0] += prow;
		v[1] += pcol;
	} else {
		mv_dp->size(&ph, &pw);
	}
	return mv_draw(mv_dp, p, v[0], v[1], prow, pcol, ph, pw);
}

/* The usage line. */
void mv_usage(void)
{
	lg(HIBR_LERR, "usage: media open source [-p] | play|pause|toggle|frame|"
		      "next|info|close id | seek id seconds [-r] | volume id "
		      "[0-100] | size id cols rows [half|ascii|mono] | mode id "
		      "half|ascii|mono | detail id 0-3 | fps id n | draw id row "
		      "col [-p pane] | list");
}

/* media: play video and sound, drawing the picture as cells. */
int m_media(sh *s, int ac, char **av)
{
	const char *sub = ac > 1 ? av[1] : "";
	mv_pl *p;
	str b;
	size_t i;
	int m;
	double t;

	if (!strcmp(sub, "open")) {
		if (ac < 3) {
			mv_usage();
			return 2;
		}
		p = mv_open(s, av[2], ac > 3 && !strcmp(av[3], "-p"));
		if (!p)
			return HIBR_FAIL;
		p->id = ++mv_nextid;
		v_add(&mv_pls, p);
		s_init(&b);
		s_num(&b, p->id);
		mv_say(s, b.p);
		s_free(&b);
		return HIBR_OK;
	}
	if (!strcmp(sub, "list")) {
		s_init(&b);
		for (i = 0; i < mv_pls.n; i++) {
			if (b.n)
				s_ch(&b, ' ');
			s_num(&b, ((mv_pl *)mv_pls.p[i])->id);
		}
		mv_say(s, b.p ? b.p : "");
		s_free(&b);
		return HIBR_OK;
	}
	if (ac < 3) {
		mv_usage();
		return 2;
	}
	if (!strcmp(sub, "close") && !strcmp(av[2], "all")) {
		while (mv_pls.n)
			mv_forget(mv_pls.p[mv_pls.n - 1]);
		return HIBR_OK;
	}
	p = mv_get(av[2]);
	if (!p)
		return HIBR_FAIL;
	if (!strcmp(sub, "close")) {
		mv_forget(p);
		return HIBR_OK;
	}
	if (!strcmp(sub, "play") || !strcmp(sub, "pause")) {
		mv_pause(p, sub[1] == 'a');
		return HIBR_OK;
	}
	if (!strcmp(sub, "toggle")) {
		mv_pause(p, !p->paused);
		return HIBR_OK;
	}
	if (!strcmp(sub, "seek")) {
		if (ac < 4) {
			mv_usage();
			return 2;
		}
		t = atof(av[3]);
		if ((ac > 4 && !strcmp(av[4], "-r")) || av[3][0] == '+' ||
		    av[3][0] == '-')
			t += mv_clock(p);
		mv_seek(p, t);
		return HIBR_OK;
	}
	if (!strcmp(sub, "volume")) {
		pthread_mutex_lock(&p->mu);
		if (ac > 3) {
			m = atoi(av[3]);
			p->vol = m < 0 ? 0 : m > 100 ? 100 : m;
		}
		m = p->vol;
		pthread_mutex_unlock(&p->mu);
		s_init(&b);
		s_num(&b, m);
		mv_say(s, b.p);
		s_free(&b);
		return HIBR_OK;
	}
	if (!strcmp(sub, "size")) {
		if (ac < 5) {
			mv_usage();
			return 2;
		}
		m = ac > 5 ? mv_modeof(av[5]) : p->mode;
		if (m < 0) {
			mv_usage();
			return 2;
		}
		mv_size(p, atoi(av[3]), atoi(av[4]), m);
		return HIBR_OK;
	}
	if (!strcmp(sub, "mode")) {
		m = ac > 3 ? mv_modeof(av[3]) : -1;
		if (m < 0) {
			mv_usage();
			return 2;
		}
		mv_size(p, p->tcols, p->trows, m);
		return HIBR_OK;
	}
	if (!strcmp(sub, "detail") && ac > 3) {
		pthread_mutex_lock(&p->mu);
		p->detail = atoi(av[3]) & 3;
		p->sgen++;
		pthread_mutex_unlock(&p->mu);
		return HIBR_OK;
	}
	if (!strcmp(sub, "fps") && ac > 3) {
		p->fpsmax = atoi(av[3]);
		return HIBR_OK;
	}
	if (!strcmp(sub, "frame"))
		return mv_frame(p);
	if (!strcmp(sub, "next")) {
		s_init(&b);
		s_num(&b, mv_next(p));
		mv_say(s, b.p);
		s_free(&b);
		return HIBR_OK;
	}
	if (!strcmp(sub, "info"))
		return mv_info(s, p);
	if (!strcmp(sub, "draw"))
		return mv_drawcmd(s, p, ac, av);
	lg(HIBR_LERR, "media: no subcommand %s", sub);
	return 2;
}

/* Nothing to set up until something is opened. */
int mv_ini(sh *s)
{
	(void)s;
	return HIBR_OK;
}

/* Close every player when the module goes. */
void mv_fini(sh *s)
{
	(void)s;
	while (mv_pls.n)
		mv_forget(mv_pls.p[mv_pls.n - 1]);
	v_free(&mv_pls);
	mv_dp = 0;
}

const hibr_bi media_bi[] = {
	{ "media", m_media, "play video and sound, the picture drawn as cells" },
	HIBR_BI_END
};

HIBR_MODULE("media", "0.1", "a video and sound player on FFmpeg's libraries",
	    media_bi, mv_ini, mv_fini);
