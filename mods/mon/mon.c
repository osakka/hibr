#define _GNU_SOURCE

#include "mn.h"
#include "../display.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const dp_api *dp;

/* A colour for a percentage: calm, then warm, then loud. */
unsigned mn_tone(double pct)
{
	if (pct < 25)
		return DP_PAL | 46u;
	if (pct < 50)
		return DP_PAL | 82u;
	if (pct < 75)
		return DP_PAL | 226u;
	if (pct < 90)
		return DP_PAL | 208u;
	return DP_PAL | 196u;
}

/* One of the eight block heights, for a value against a ceiling. */
const char *mn_block(double v, double top)
{
	static const char *b[] = { "\342\226\201", "\342\226\202",
				   "\342\226\203", "\342\226\204",
				   "\342\226\205", "\342\226\206",
				   "\342\226\207", "\342\226\210" };
	int i;

	if (top <= 0)
		return b[0];
	i = (int)(v / top * 7.999);
	if (i < 0)
		i = 0;
	if (i > 7)
		i = 7;
	return b[i];
}

/* A number of bytes, in whatever unit keeps it short. */
void mn_bytes(str *o, double v)
{
	static const char *u[] = { "B", "K", "M", "G", "T" };
	int i = 0;
	long whole;

	while (v >= 1024 && i < 4) {
		v /= 1024;
		i++;
	}
	whole = (long)(v * 10 + 0.5);
	s_num(o, whole / 10);
	s_ch(o, '.');
	s_num(o, whole % 10);
	s_cat(o, u[i]);
}

/* A number with one decimal, right-aligned. */
void mn_pct(str *o, double v, int w)
{
	str t;
	size_t i;

	s_init(&t);
	s_num(&t, (long)(v * 10 + 0.5));
	if (t.n < 2) {
		str z;
		s_init(&z);
		s_cat(&z, "0");
		s_cat(&z, t.p ? t.p : "0");
		s_free(&t);
		t = z;
	}
	for (i = t.n + 1; (int)i < w; i++)
		s_ch(o, ' ');
	s_add(o, t.p, t.n - 1);
	s_ch(o, '.');
	s_add(o, t.p + t.n - 1, 1);
	s_free(&t);
}

/* Draw a proportion as a run of filled and empty cells. */
int mn_bar(int row, int col, int w, double pct, unsigned tone)
{
	int i, on = (int)(pct / 100.0 * w + 0.5);
	int c = col;

	if (on > w)
		on = w;
	if (on < 0)
		on = 0;
	for (i = 0; i < w; i++) {
		dp->pen(i < on ? tone : DP_PAL | 238u, DP_DEFAULT, 0);
		c += dp->put(row, c, i < on ? "\342\226\210" : "\342\226\221");
	}
	return c;
}

/* Draw a history as block heights, scaled to its own tallest value. */
void mn_spark(int row, int col, int w, double *h, int n, int scale_pct)
{
	int i, from, c = col;
	double top = 0;

	if (n <= 0)
		return;
	from = n > w ? n - w : 0;
	for (i = from; i < n; i++)
		if (h[i % MN_HIST] > top)
			top = h[i % MN_HIST];
	if (scale_pct)
		top = 100.0;
	for (i = from; i < n; i++) {
		double v = h[i % MN_HIST];
		dp->pen(mn_tone(scale_pct ? v : (top > 0 ? v / top * 100 : 0)),
			DP_DEFAULT, 0);
		c += dp->put(row, c, mn_block(v, top));
	}
}

/* Sort the processes, heaviest first. */
int mn_bycpu(const void *a, const void *b)
{
	const mn_proc *x = *(mn_proc *const *)a, *y = *(mn_proc *const *)b;

	if (y->pct > x->pct)
		return 1;
	if (y->pct < x->pct)
		return -1;
	return y->rss > x->rss ? 1 : (y->rss < x->rss ? -1 : 0);
}

/* Sort the processes, largest first. */
int mn_bymem(const void *a, const void *b)
{
	const mn_proc *x = *(mn_proc *const *)a, *y = *(mn_proc *const *)b;

	return y->rss > x->rss ? 1 : (y->rss < x->rss ? -1 : 0);
}

/* How long the machine has been up, in words. */
void mn_uptime(str *o, double s)
{
	long d = (long)(s / 86400), h = (long)(s / 3600) % 24;
	long m = (long)(s / 60) % 60;

	if (d) {
		s_num(o, d);
		s_ch(o, 'd');
		s_ch(o, ' ');
	}
	s_num(o, h);
	s_ch(o, 'h');
	s_ch(o, ' ');
	s_num(o, m);
	s_ch(o, 'm');
}

/* Draw the whole thing. */
void mn_draw(mn_st *s, int rows, int cols, int bymem, int paused, double every)
{
	int r, i, c, bw;
	str t;
	double used;

	dp->pen(DP_DEFAULT, DP_DEFAULT, 0);
	dp->clear();
	s_init(&t);

	s_cat(&t, " ");
	{
		char hn[64];
		if (gethostname(hn, sizeof hn) != 0)
			strcpy(hn, "?");
		hn[sizeof hn - 1] = 0;
		s_cat(&t, hn);
	}
	s_cat(&t, "   up ");
	mn_uptime(&t, s->uptime);
	s_cat(&t, "   load ");
	mn_pct(&t, s->load1, 0);
	s_ch(&t, ' ');
	mn_pct(&t, s->load5, 0);
	s_ch(&t, ' ');
	mn_pct(&t, s->load15, 0);
	if (paused)
		s_cat(&t, "   PAUSED");
	s_cat(&t, "   q quit  p pause  m by memory  c by cpu");
	dp->pen(DP_PAL | 16u, DP_PAL | 110u, 0);
	dp->fill(0, 0, 1, cols, " ");
	dp->put(0, 0, t.p);

	bw = cols - 34;
	if (bw > 40)
		bw = 40;
	if (bw < 8)
		bw = 8;

	r = 2;
	t.n = 0;
	if (t.p)
		t.p[0] = 0;
	dp->pen(DP_PAL | 252u, DP_DEFAULT, 0);
	dp->put(r, 1, "CPU");
	c = mn_bar(r, 6, bw, s->all.pct, mn_tone(s->all.pct));
	mn_pct(&t, s->all.pct, 6);
	s_ch(&t, '%');
	dp->pen(mn_tone(s->all.pct), DP_DEFAULT, 0);
	dp->put(r, c + 1, t.p);
	mn_spark(r, c + 10, cols - c - 11, s->cpuhist, s->hn, 1);

	r++;
	for (i = 0; i < s->ncore && i < 16; i++) {
		int cc = 6 + (i % 4) * ((cols - 8) / 4);
		int rr = r + i / 4;
		t.n = 0;
		if (t.p)
			t.p[0] = 0;
		s_num(&t, (long)i);
		dp->pen(DP_PAL | 244u, DP_DEFAULT, 0);
		dp->put(rr, cc, t.p);
		mn_bar(rr, cc + 2, 8, s->core[i].pct, mn_tone(s->core[i].pct));
		t.n = 0;
		if (t.p)
			t.p[0] = 0;
		mn_pct(&t, s->core[i].pct, 5);
		dp->pen(mn_tone(s->core[i].pct), DP_DEFAULT, 0);
		dp->put(rr, cc + 11, t.p);
	}
	r += (s->ncore + 3) / 4;

	r++;
	used = s->mtotal ? (double)(s->mtotal - s->mavail) * 100.0 /
				   (double)s->mtotal
			 : 0.0;
	t.n = 0;
	if (t.p)
		t.p[0] = 0;
	dp->pen(DP_PAL | 252u, DP_DEFAULT, 0);
	dp->put(r, 1, "MEM");
	c = mn_bar(r, 6, bw, used, mn_tone(used));
	mn_bytes(&t, (double)(s->mtotal - s->mavail) * 1024);
	s_cat(&t, " / ");
	mn_bytes(&t, (double)s->mtotal * 1024);
	dp->pen(mn_tone(used), DP_DEFAULT, 0);
	dp->put(r, c + 1, t.p);
	mn_spark(r, c + 20, cols - c - 21, s->memhist, s->hn, 1);

	if (s->stotal) {
		double sw = (double)(s->stotal - s->sfree) * 100.0 /
			    (double)s->stotal;
		r++;
		t.n = 0;
		if (t.p)
			t.p[0] = 0;
		dp->pen(DP_PAL | 252u, DP_DEFAULT, 0);
		dp->put(r, 1, "SWP");
		c = mn_bar(r, 6, bw, sw, mn_tone(sw));
		mn_bytes(&t, (double)(s->stotal - s->sfree) * 1024);
		s_cat(&t, " / ");
		mn_bytes(&t, (double)s->stotal * 1024);
		dp->pen(mn_tone(sw), DP_DEFAULT, 0);
		dp->put(r, c + 1, t.p);
	}

	for (i = 0; i < (int)s->nets.n && i < 2; i++) {
		mn_dev *d = (mn_dev *)s->nets.p[i];
		r++;
		t.n = 0;
		if (t.p)
			t.p[0] = 0;
		s_cat(&t, d->name);
		while (t.n < 10)
			s_ch(&t, ' ');
		s_cat(&t, "\342\206\223 ");
		mn_bytes(&t, d->ra);
		s_cat(&t, "/s   \342\206\221 ");
		mn_bytes(&t, d->rb);
		s_cat(&t, "/s");
		dp->pen(DP_PAL | 111u, DP_DEFAULT, 0);
		dp->put(r, 6, t.p);
	}
	for (i = 0; i < (int)s->disks.n && i < 2; i++) {
		mn_dev *d = (mn_dev *)s->disks.p[i];
		if (d->ra < 1 && d->rb < 1)
			continue;
		r++;
		t.n = 0;
		if (t.p)
			t.p[0] = 0;
		s_cat(&t, d->name);
		while (t.n < 10)
			s_ch(&t, ' ');
		s_cat(&t, "r ");
		mn_bytes(&t, d->ra);
		s_cat(&t, "/s   w ");
		mn_bytes(&t, d->rb);
		s_cat(&t, "/s");
		dp->pen(DP_PAL | 180u, DP_DEFAULT, 0);
		dp->put(r, 6, t.p);
	}

	r += 2;
	dp->pen(DP_PAL | 252u, DP_PAL | 238u, 0);
	dp->fill(r, 0, 1, cols, " ");
	dp->put(r, 0, bymem
			      ? "    PID  USER          CPU%       RSS  COMMAND"
			      : "    PID  USER          CPU%       RSS  COMMAND");
	r++;
	{
		vec o;
		size_t k;
		o.p = 0;
		o.n = 0;
		o.cap = 0;
		for (k = 0; k < s->procs.n; k++)
			v_add(&o, s->procs.p[k]);
		if (o.n > 1)
			qsort(o.p, o.n, sizeof *o.p,
			      bymem ? mn_bymem : mn_bycpu);
		for (k = 0; k < o.n && r < rows; k++, r++) {
			mn_proc *p = (mn_proc *)o.p[k];
			t.n = 0;
			if (t.p)
				t.p[0] = 0;
			{
				str n;
				size_t z;
				s_init(&n);
				s_num(&n, p->pid);
				for (z = n.n; z < 7; z++)
					s_ch(&t, ' ');
				s_cat(&t, n.p);
				s_free(&n);
			}
			s_cat(&t, "  ");
			s_cat(&t, p->user);
			while (t.n < 22)
				s_ch(&t, ' ');
			mn_pct(&t, p->pct, 6);
			s_cat(&t, "  ");
			{
				str b;
				size_t z;
				s_init(&b);
				mn_bytes(&b, (double)p->rss * 1024);
				for (z = b.n; z < 8; z++)
					s_ch(&t, ' ');
				s_cat(&t, b.p);
				s_free(&b);
			}
			s_cat(&t, "  ");
			s_cat(&t, p->name);
			dp->pen(p->pct > 10 ? mn_tone(p->pct) : DP_DEFAULT,
				DP_DEFAULT, 0);
			dp->put(r, 0, t.p);
		}
		v_free(&o);
	}
	(void)every;
	dp->pen(DP_DEFAULT, DP_DEFAULT, 0);
	s_free(&t);
	dp->flush();
}

/* Keep reading and keep it on screen. */
int mn_live(sh *s, double every, int bymem)
{
	mn_st st;
	str key;
	int rows, cols, quit = 0, paused = 0;

	dp = (const dp_api *)hibr_require(s, "display", DP_API_VER);
	if (!dp) {
		lg(HIBR_LERR, "mon: no display is available; nothing on the\n"
			      "      module path offers one");
		return HIBR_FAIL;
	}
	mn_init(&st);
	s_init(&key);
	mn_sample(&st, 0);
	if (dp->open(s) != HIBR_OK) {
		mn_free(&st);
		return HIBR_FAIL;
	}
	while (!quit) {
		dp->size(&rows, &cols);
		if (!paused)
			mn_sample(&st, every);
		mn_draw(&st, rows, cols, bymem, paused, every);
		key.n = 0;
		if (key.p)
			key.p[0] = 0;
		if (dp->key(paused ? -1 : (int)(every * 1000), &key) == 1) {
			const char *k = key.p ? key.p : "";
			if (!strcmp(k, "q") || !strcmp(k, "ctrl-c"))
				quit = 1;
			else if (!strcmp(k, "p") || !strcmp(k, " "))
				paused = !paused;
			else if (!strcmp(k, "m"))
				bymem = 1;
			else if (!strcmp(k, "c"))
				bymem = 0;
		}
	}
	dp->close(s);
	mn_free(&st);
	s_free(&key);
	return HIBR_OK;
}

/* Watch the machine. */
int m_mon(sh *s, int ac, char **av)
{
	double every = 1.0;
	int i, bymem = 0;

	for (i = 1; i < ac; i++) {
		if (!strcmp(av[i], "-d") && i + 1 < ac) {
			every = atof(av[++i]);
			if (every < 0.1)
				every = 0.1;
		} else if (!strcmp(av[i], "-m")) {
			bymem = 1;
		} else {
			lg(HIBR_LERR, "usage: mon [-d seconds] [-m]");
			return 2;
		}
	}
	return mn_live(s, every, bymem);
}

const hibr_bi mon_bi[] = {
	{ "mon", m_mon, "watch the machine: processors, memory, disks, processes" },
	HIBR_BI_END
};

HIBR_MODULE("mon", "0.21", "a system monitor on the display interface", mon_bi,
	    0, 0);
