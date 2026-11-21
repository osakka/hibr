#define _GNU_SOURCE

#include "cn.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/socket.h>

extern char **cn_lk;
extern int cn_lkn;
extern unsigned cn_fg, cn_bg, cn_penat, cn_ul, cn_kfb, cn_plink;
extern int cn_fitg;

int cn_hl;
unsigned cn_hlts;

/* An app's own console (ADR 0050, "The cell record"): each view is a pane
   of its headless screen, laid one under the next, and what the desktop
   was last sent of it is the same rectangle of the front grid. */
typedef struct cw_av {
	char *nm;
	long gen;
	int row, h, w;
	int sent;
	int drawn;
} cw_av;

/* A translucency an app set, by the ID the record names it with. */
typedef struct cw_ts {
	int on, pct;
	unsigned fb, mat;
	int dirty;
} cw_ts;

static int cw_afd = -1;
static vec cw_avs;
static unsigned *cw_lid;
static size_t cw_lidn;
/* The record fixes how many link IDs and translucencies a connection names
   at once (CN_CWLINKS, CN_CWTS): which of this app's links each ID holds,
   and whether the desktop has been told yet. */
static unsigned cw_lown[CN_CWLINKS + 1];
static size_t cw_lbytes;
static int cw_ldirty[CN_CWLINKS + 1];
static cw_ts cw_ats[CN_CWTS + 1];
static int cw_said, cw_tssaid;

/* The desktop's side: a connection, and each view it was given. */
typedef struct cw_view cw_view;
/* Its links and translucencies by ID, as many as the record lets it name. */
typedef struct cw_conn {
	int fd;
	str in;
	unsigned lk[CN_CWLINKS + 1];
	size_t lkb[CN_CWLINKS + 1];
	size_t lkbytes;
	cw_ts ts[CN_CWTS + 1];
	long frames, cells, bytes;
	int skip, broken, gone, echo;
} cw_conn;

struct cw_view {
	char *nm;
	cw_conn *k;
	long gen;
	char *pane;
	int r, c, h, w;
	cn_grid g;
	int *lo, *hi;
	int unc, again, chg;
};

/* One run or none of a frame, kept until its e has arrived. */
typedef struct cw_op {
	char kind;
	int row, col, n;
	unsigned fg, bg, at, ul, lk;
	size_t off;
} cw_op;

static vec cw_ks, cw_vs;

/* Append v in hex. */
void cw_hex(str *o, unsigned v)
{
	int sh = 28;

	while (sh > 0 && !(v >> sh))
		sh -= 4;
	for (; sh >= 0; sh -= 4)
		s_ch(o, "0123456789abcdef"[(v >> sh) & 15]);
}

/* A view of this app's own console by name, or null. */
cw_av *cw_afind(const char *nm)
{
	size_t i;

	for (i = 0; i < cw_avs.n; i++)
		if (!strcmp(((cw_av *)cw_avs.p[i])->nm, nm))
			return cw_avs.p[i];
	return 0;
}

/* Every cell of a grid none. */
void cw_none(cn_grid *g, int r0, int h)
{
	int i, n;
	cn_cell *k;

	n = h * g->cols;
	k = g->c + (size_t)r0 * g->cols;
	for (i = 0; i < n; i++) {
		free(k[i].ext);
		memset(&k[i], 0, sizeof k[i]);
		k[i].cp = CN_NONE;
		k[i].w = 1;
	}
}

/* Copy cell b into a, its combining characters with it. */
void cw_cellcopy(cn_cell *a, const cn_cell *b)
{
	char *e = a->ext;

	*a = *b;
	a->ext = 0;
	free(e);
	if (b->ext)
		a->ext = xs(b->ext);
}

/* Lay the views one under the next in the headless screen, keeping what
   each holds and what was sent of it: rows the sum of their heights, the
   widest of them across. */
void cw_layout(void)
{
	cn_grid nb, nf;
	size_t i;
	int rows = 0, cols = 1, r, c, at = 0;
	cw_av *v;

	for (i = 0; i < cw_avs.n; i++) {
		v = cw_avs.p[i];
		rows += v->h;
		if (v->w > cols)
			cols = v->w;
	}
	if (rows < 1)
		rows = 1;
	memset(&nb, 0, sizeof nb);
	memset(&nf, 0, sizeof nf);
	nb.rows = nf.rows = rows;
	nb.cols = nf.cols = cols;
	nb.c = xm((size_t)rows * cols * sizeof *nb.c);
	nf.c = xm((size_t)rows * cols * sizeof *nf.c);
	memset(nb.c, 0, (size_t)rows * cols * sizeof *nb.c);
	memset(nf.c, 0, (size_t)rows * cols * sizeof *nf.c);
	cw_none(&nb, 0, rows);
	cw_none(&nf, 0, rows);
	for (i = 0; i < cw_avs.n; i++) {
		v = cw_avs.p[i];
		for (r = 0; v->row >= 0 && r < v->h; r++) {
			if (v->row + r >= cn_back.rows)
				break;
			for (c = 0; c < v->w && c < cn_back.cols; c++) {
				cw_cellcopy(&nb.c[(size_t)(at + r) * cols + c],
					    &cn_back.c[(size_t)(v->row + r) *
						       cn_back.cols + c]);
				cw_cellcopy(&nf.c[(size_t)(at + r) * cols + c],
					    &cn_front.c[(size_t)(v->row + r) *
							cn_front.cols + c]);
			}
		}
		v->row = at;
		cn_pset(v->nm, at, 0, v->h, v->w);
		at += v->h;
	}
	cn_gfree(&cn_back);
	cn_gfree(&cn_front);
	cn_back = nb;
	cn_front = nf;
	cn_fitg = (int)cn_wgen;
	cn_owninval();
	lg(HIBR_LDBG, "cells: headless screen %d by %d, %d views", rows, cols,
	   (int)cw_avs.n);
}

/* The size of this app's headless screen, for cn_size. */
void cw_size(int *rows, int *cols)
{
	*rows = cn_back.rows;
	*cols = cn_back.cols;
}

/* Clear a view to none, before its app draws it. */
int cw_clear(const char *nm)
{
	cw_av *v = cw_afind(nm);

	if (!v) {
		lg(HIBR_LERR, "console cells clear: %s: no such view", nm);
		return 0;
	}
	cw_none(&cn_back, v->row, v->h);
	v->drawn = 1;
	cn_wrote++;
	return 1;
}

/* Which link IDs and translucencies the views still hold, so one no cell
   carries can name another: inuse[id] for links, ts[id] for the rest. */
void cw_inuse(unsigned char *lk, unsigned char *ts)
{
	size_t i, n = (size_t)cn_back.rows * cn_back.cols;
	cn_cell *k;

	for (i = 0; i < n; i++) {
		k = &cn_back.c[i];
		if (k->cp == CN_NONE)
			continue;
		if (lk && k->link && k->link < cw_lidn && cw_lid[k->link])
			lk[cw_lid[k->link]] = 1;
		if (ts)
			ts[(k->attr & CN_TSMASK) >> CN_TSSHIFT] = 1;
	}
}

/* The ID a record names one of this app's links by, giving it one when it
   has none: 0 when it cannot have one, a URI too long or every ID held by
   a cell that is still drawn. */
unsigned cw_lkid(unsigned li)
{
	unsigned char *use;
	unsigned id;
	size_t ln;

	if (!li || (int)li > cn_lkn)
		return 0;
	if (li >= cw_lidn) {
		size_t n = li + 16;

		cw_lid = xr(cw_lid, n * sizeof *cw_lid);
		memset(cw_lid + cw_lidn, 0, (n - cw_lidn) * sizeof *cw_lid);
		cw_lidn = n;
	}
	if (cw_lid[li])
		return cw_lid[li];
	ln = strlen(cn_lk[li - 1]);
	if (ln > CN_CWURI)
		return 0;
	for (id = 1; id <= CN_CWLINKS; id++)
		if (!cw_lown[id])
			break;
	if (id > CN_CWLINKS || cw_lbytes + ln > CN_CWLKB) {
		use = xm(CN_CWLINKS + 1);
		memset(use, 0, CN_CWLINKS + 1);
		cw_inuse(use, 0);
		for (id = 1; id <= CN_CWLINKS; id++)
			if (cw_lown[id] && !use[id]) {
				cw_lbytes -= strlen(cn_lk[cw_lown[id] - 1]);
				cw_lid[cw_lown[id]] = 0;
				cw_lown[id] = 0;
			}
		free(use);
		for (id = 1; id <= CN_CWLINKS; id++)
			if (!cw_lown[id])
				break;
		if (id > CN_CWLINKS || cw_lbytes + ln > CN_CWLKB) {
			if (!cw_said++)
				lg(HIBR_LWRN, "cells: more links at once than the "
					      "record carries; the rest are drawn "
					      "as text");
			return 0;
		}
	}
	cw_lown[id] = li;
	cw_lid[li] = id;
	cw_lbytes += ln;
	cw_ldirty[id] = 1;
	return id;
}

/* The translucency writes carry now, after the app's own console
   translucent: its ID in the attribute's bits, given one when it has none,
   out of the CN_CWTS the record lets a connection name at once. */
void cw_tsnow(int pct, unsigned fb, unsigned mat)
{
	unsigned char use[CN_CWTS + 1];
	int i;

	if (!pct) {
		fb = DP_DEFAULT;
		mat = DP_DEFAULT;
	}
	for (i = 1; i <= CN_CWTS; i++)
		if (cw_ats[i].on && cw_ats[i].pct == pct && cw_ats[i].fb == fb &&
		    cw_ats[i].mat == mat) {
			cn_hlts = (unsigned)i << CN_TSSHIFT;
			return;
		}
	for (i = 1; i <= CN_CWTS; i++)
		if (!cw_ats[i].on)
			break;
	if (i > CN_CWTS) {
		memset(use, 0, sizeof use);
		cw_inuse(0, use);
		for (i = 1; i <= CN_CWTS; i++)
			if (!use[i])
				break;
	}
	if (i > CN_CWTS) {
		if (!cw_tssaid++)
			lg(HIBR_LWRN, "cells: more than %d translucencies at once;"
				      " the rest are drawn as the window's",
			   CN_CWTS);
		cn_hlts = 0;
		return;
	}
	cw_ats[i].on = 1;
	cw_ats[i].pct = pct;
	cw_ats[i].fb = fb;
	cw_ats[i].mat = mat;
	cw_ats[i].dirty = 1;
	cn_hlts = (unsigned)i << CN_TSSHIFT;
}

/* A run's text: each cell's glyph and as many of its combining characters
   as CN_CWCELL holds. */
void cw_glyph(str *o, const cn_cell *k)
{
	size_t at = o->n, n, l;
	unsigned cp;

	u8put(o, k->cp ? k->cp : ' ');
	if (!k->ext)
		return;
	n = strlen(k->ext);
	for (l = 0; l < n;) {
		int d = u8dec(k->ext + l, n - l, &cp);

		if (o->n - at + (size_t)d > CN_CWCELL)
			break;
		s_add(o, k->ext + l, (size_t)d);
		l += (size_t)d;
	}
}

/* Whether two cells of an app's own console are the same to the desktop:
   what draws, and what the desktop resolves for it. */
int cw_same(const cn_cell *a, const cn_cell *b)
{
	if (a->cp == CN_NONE || b->cp == CN_NONE)
		return a->cp == b->cp;
	return a->attr == b->attr && cn_same(a, b);
}

/* The records for one view: what differs between what the app drew and
   what was last sent, as runs of one pen along a row and nones, the sent
   copy made the same as it goes. The count of lines. */
long cw_diff(cw_av *v, str *o)
{
	int r, c, c0, n;
	long lines = 0;
	cn_cell *b, *f;
	str t;

	if (!v->sent) {
		cw_none(&cn_front, v->row, v->h);
		s_cat(o, "s ");
		s_num(o, v->h);
		s_ch(o, ' ');
		s_num(o, v->w);
		s_ch(o, '\n');
		v->sent = 1;
		lines++;
	}
	s_init(&t);
	for (r = 0; r < v->h; r++) {
		b = &cn_back.c[(size_t)(v->row + r) * cn_back.cols];
		f = &cn_front.c[(size_t)(v->row + r) * cn_front.cols];
		c = 0;
		while (c < v->w) {
			if (b[c].cont || cw_same(&b[c], &f[c])) {
				if (b[c].cont)
					cw_cellcopy(&f[c], &b[c]);
				c++;
				continue;
			}
			c0 = c;
			if (b[c].cp == CN_NONE) {
				while (c < v->w && b[c].cp == CN_NONE &&
				       !cw_same(&b[c], &f[c])) {
					cw_cellcopy(&f[c], &b[c]);
					c++;
				}
				s_cat(o, "n ");
				s_num(o, r);
				s_ch(o, ' ');
				s_num(o, c0);
				s_ch(o, ' ');
				s_num(o, c - c0);
				s_ch(o, '\n');
				lines++;
				continue;
			}
			t.n = 0;
			n = 0;
			while (c < v->w && b[c].cp != CN_NONE && !b[c].cont &&
			       !cw_same(&b[c], &f[c]) && b[c].fg == b[c0].fg &&
			       b[c].bg == b[c0].bg && b[c].attr == b[c0].attr &&
			       b[c].ul == b[c0].ul && b[c].link == b[c0].link &&
			       c + b[c].w <= v->w && t.n < CN_CWLINE - 128) {
				cw_glyph(&t, &b[c]);
				cw_cellcopy(&f[c], &b[c]);
				if (b[c].w == 2) {
					cw_cellcopy(&f[c + 1], &b[c + 1]);
					c++;
				}
				c++;
				n++;
			}
			if (!n) {
				cw_cellcopy(&f[c], &b[c]);
				c++;
				continue;
			}
			s_cat(o, "r ");
			s_num(o, r);
			s_ch(o, ' ');
			s_num(o, c0);
			s_ch(o, ' ');
			cw_hex(o, b[c0].fg);
			s_ch(o, ' ');
			cw_hex(o, b[c0].bg);
			s_ch(o, ' ');
			cw_hex(o, b[c0].attr & ~CN_SHADOWED);
			s_ch(o, ' ');
			cw_hex(o, b[c0].ul);
			s_ch(o, ' ');
			s_num(o, cw_lkid(b[c0].link));
			s_ch(o, ' ');
			s_num(o, (long)t.n);
			s_ch(o, ' ');
			s_add(o, t.p, t.n);
			s_ch(o, '\n');
			lines++;
		}
	}
	s_free(&t);
	return lines;
}

/* Send what changed in every view drawn since the last flush: the links
   and translucencies first named, then a frame a view. Bytes sent, -1 when
   the desktop's end has gone. */
long cw_flush(void)
{
	str o, fr;
	size_t i;
	cw_av *v;
	long n = 0;
	int id;

	if (cw_afd < 0)
		return 0;
	s_init(&o);
	s_init(&fr);
	for (i = 0; i < cw_avs.n; i++) {
		cn_pane *p;

		v = cw_avs.p[i];
		p = cn_pfind(v->nm);
		if (!v->drawn && v->sent && !(p && p->drawn))
			continue;
		fr.n = 0;
		s_cat(&fr, "w ");
		s_cat(&fr, v->nm);
		s_ch(&fr, ' ');
		s_num(&fr, v->gen);
		s_ch(&fr, '\n');
		if (cw_diff(v, &fr)) {
			s_add(&o, fr.p, fr.n);
			s_cat(&o, "e\n");
		}
		v->drawn = 0;
		if (p)
			p->drawn = 0;
	}
	if (o.n) {
		str pre;

		s_init(&pre);
		for (id = 1; id <= CN_CWLINKS; id++) {
			if (!cw_ldirty[id] || !cw_lown[id])
				continue;
			cw_ldirty[id] = 0;
			s_cat(&pre, "l ");
			s_num(&pre, id);
			s_ch(&pre, ' ');
			s_num(&pre, (long)strlen(cn_lk[cw_lown[id] - 1]));
			s_ch(&pre, ' ');
			s_cat(&pre, cn_lk[cw_lown[id] - 1]);
			s_ch(&pre, '\n');
		}
		for (id = 1; id <= CN_CWTS; id++) {
			if (!cw_ats[id].dirty)
				continue;
			cw_ats[id].dirty = 0;
			s_cat(&pre, "t ");
			s_num(&pre, id);
			s_ch(&pre, ' ');
			s_num(&pre, cw_ats[id].pct);
			s_ch(&pre, ' ');
			cw_hex(&pre, cw_ats[id].fb);
			s_ch(&pre, ' ');
			cw_hex(&pre, cw_ats[id].mat);
			s_ch(&pre, '\n');
		}
		if (pre.n) {
			s_add(&pre, o.p, o.n);
			s_free(&o);
			o = pre;
		} else
			s_free(&pre);
		if (hibr_wrall(cw_afd, o.p, o.n) < 0) {
			lg(HIBR_LERR, "cells: the desktop's end has gone: %s",
			   strerror(errno));
			n = -1;
		} else
			n = (long)o.n;
	}
	cn_translucent(0, DP_DEFAULT);
	cn_hlts = 0;
	cn_iconpen(0);
	s_free(&o);
	s_free(&fr);
	return n;
}

/* Make this console an app's own, its records to fd. */
int cw_open(int fd)
{
	cw_afd = fd;
	cn_hl = 1;
	if (!cn_back.c)
		cw_layout();
	lg(HIBR_LDBG, "cells: an app's own console, records to %d", fd);
	return 1;
}

/* Define or resize a view of this app's own console. */
int cw_aview(const char *nm, long gen, int h, int w)
{
	cw_av *v = cw_afind(nm);

	if (h < 1 || w < 1 || h > 4096 || w > 4096) {
		lg(HIBR_LERR, "console cells view: %s: %d by %d is no size", nm,
		   h, w);
		return 0;
	}
	if (!v) {
		v = xm(sizeof *v);
		memset(v, 0, sizeof *v);
		v->nm = xs(nm);
		v->row = -1;
		v_add(&cw_avs, v);
	}
	if (v->h != h || v->w != w)
		v->sent = 0;
	v->gen = gen;
	v->h = h;
	v->w = w;
	cw_layout();
	return 1;
}

/* Forget a view of this app's own console. */
int cw_aforget(const char *nm)
{
	size_t i;
	cw_av *v;

	for (i = 0; i < cw_avs.n; i++) {
		v = cw_avs.p[i];
		if (strcmp(v->nm, nm))
			continue;
		cn_pdrop(nm);
		free(v->nm);
		free(v);
		cw_avs.p[i] = cw_avs.p[--cw_avs.n];
		cw_layout();
		return 1;
	}
	return 0;
}

/* The desktop's connection on fd, made when asked for. */
cw_conn *cw_conn4(int fd, int make)
{
	size_t i;
	cw_conn *k;

	for (i = 0; i < cw_ks.n; i++)
		if (((cw_conn *)cw_ks.p[i])->fd == fd)
			return cw_ks.p[i];
	if (!make)
		return 0;
	k = xm(sizeof *k);
	memset(k, 0, sizeof *k);
	k->fd = fd;
	v_add(&cw_ks, k);
	return k;
}

/* A view the desktop gave, by name. */
cw_view *cw_vfind(const char *nm)
{
	size_t i;

	for (i = 0; i < cw_vs.n; i++)
		if (!strcmp(((cw_view *)cw_vs.p[i])->nm, nm))
			return cw_vs.p[i];
	return 0;
}

/* Let a view go. */
void cw_vfree(cw_view *v)
{
	size_t i;

	for (i = 0; i < cw_vs.n; i++)
		if (cw_vs.p[i] == (void *)v) {
			cw_vs.p[i] = cw_vs.p[--cw_vs.n];
			break;
		}
	cn_gfree(&v->g);
	free(v->lo);
	free(v->hi);
	free(v->pane);
	free(v->nm);
	free(v);
}

/* Bind a view to connection fd: drawn into pane (null for the screen) at
   r, c, h by w of it, at generation gen. 0 when another connection holds
   the name or any of those cells. */
int cw_own(int fd, const char *nm, long gen, const char *pane, int r, int c,
	   int h, int w)
{
	cw_view *v = cw_vfind(nm), *o;
	cw_conn *k;
	size_t i;

	if (h < 1 || w < 1 || r < 0 || c < 0 || h > 4096 || w > 4096) {
		lg(HIBR_LERR, "console cells own: %s: no such place", nm);
		return 0;
	}
	if (v && v->k->fd != fd) {
		lg(HIBR_LERR, "console cells own: %s is another connection's",
		   nm);
		return 0;
	}
	for (i = 0; i < cw_vs.n; i++) {
		o = cw_vs.p[i];
		if (o == v || o->k->fd == fd ||
		    !(pane ? o->pane && !strcmp(o->pane, pane) : !o->pane))
			continue;
		if (r < o->r + o->h && o->r < r + h && c < o->c + o->w &&
		    o->c < c + w) {
			lg(HIBR_LERR, "console cells own: %s would cover %s",
			   nm, o->nm);
			return 0;
		}
	}
	k = cw_conn4(fd, 1);
	if (!v) {
		v = xm(sizeof *v);
		memset(v, 0, sizeof *v);
		v->nm = xs(nm);
		v_add(&cw_vs, v);
	}
	v->k = k;
	v->gen = gen;
	free(v->pane);
	v->pane = pane ? xs(pane) : 0;
	v->r = r;
	v->c = c;
	v->h = h;
	v->w = w;
	return 1;
}

/* Let a view go, or every view of fd and the connection with them. */
int cw_drop(int fd, const char *nm)
{
	cw_conn *k = cw_conn4(fd, 0);
	cw_view *v;
	size_t i;

	if (nm) {
		v = cw_vfind(nm);
		if (!v || v->k != k)
			return 0;
		cw_vfree(v);
		return 1;
	}
	if (!k)
		return 0;
	if (k->echo)
		cn_watchin(fd, 0, 1);
	for (i = cw_vs.n; i-- > 0;)
		if (((cw_view *)cw_vs.p[i])->k == k)
			cw_vfree(cw_vs.p[i]);
	s_free(&k->in);
	for (i = 0; i < cw_ks.n; i++)
		if (cw_ks.p[i] == (void *)k) {
			cw_ks.p[i] = cw_ks.p[--cw_ks.n];
			break;
		}
	free(k);
	return 1;
}

/* A whole number in decimal, or in hex, at p and before e, ended by a space
   or a newline, which *end is left at. 0 when there is none. */
int cw_num(const char *p, const char *e, int hex, unsigned long *out,
	   const char **end)
{
	unsigned long v = 0;
	const char *q = p;
	int d;

	while (q < e && *q != ' ' && *q != '\n') {
		if (*q >= '0' && *q <= '9')
			d = *q - '0';
		else if (hex && *q >= 'a' && *q <= 'f')
			d = *q - 'a' + 10;
		else
			return 0;
		if (v > 0xFFFFFFFFul)
			return 0;
		v = v * (hex ? 16 : 10) + (unsigned long)d;
		q++;
	}
	if (q == p || q >= e)
		return 0;
	*out = v;
	*end = q;
	return 1;
}

/* Read n numbers from a line's fields, the hex ones marked in hx. 1 when
   each was there, -1 when the line is not whole yet, 0 when it is broken. */
int cw_fields(const char **p, const char *e, int n, const char *hx,
	      unsigned long *out)
{
	int i;
	const char *q = *p, *x;

	for (i = 0; i < n; i++) {
		if (q >= e)
			return -1;
		if (*q != ' ')
			return 0;
		q++;
		x = q;
		while (x < e && *x != ' ' && *x != '\n')
			x++;
		if (x >= e)
			return -1;
		if (!cw_num(q, e, hx[i] == 'x', &out[i], &q))
			return 0;
	}
	*p = q;
	return 1;
}

/* The connection is broken: say why once. */
void cw_broke(cw_conn *k, const char *why)
{
	if (!k->broken)
		lg(HIBR_LERR, "cells: connection %d dropped: %s", k->fd, why);
	k->broken = 1;
}

/* Split a kept cell that is half of a wide glyph, as cn_split does. */
void cw_ksplit(cn_grid *g, int r, int c)
{
	cn_cell *k = &g->c[(size_t)r * g->cols + c];

	if (k->cont && c > 0) {
		cn_cellset(k - 1, ' ', 0, 0);
		(k - 1)->w = 1;
	}
	if (k->w == 2 && c + 1 < g->cols) {
		cn_cellset(k + 1, ' ', 0, 0);
		(k + 1)->w = 1;
		(k + 1)->cont = 0;
	}
}

/* A row's cells from c0 to c1, inclusive, changed. */
void cw_dmg(cw_view *v, int r, int c0, int c1)
{
	if (v->lo[r] < 0 || c0 < v->lo[r])
		v->lo[r] = c0;
	if (c1 > v->hi[r])
		v->hi[r] = c1;
	v->chg = 1;
}

/* One frame's runs and nones into its view's kept grid. */
void cw_apply(cw_conn *k, cw_view *v, cw_op *ops, size_t n, const char *base)
{
	size_t i, at, ln;
	int c, w, l;
	unsigned cp;
	cn_cell *x, *last;
	cw_op *o;

	for (i = 0; i < n; i++) {
		o = &ops[i];
		if (o->kind == 's') {
			int j;

			cn_gfree(&v->g);
			v->g.rows = v->h;
			v->g.cols = v->w;
			v->g.c = xm((size_t)v->h * v->w * sizeof *v->g.c);
			memset(v->g.c, 0, (size_t)v->h * v->w * sizeof *v->g.c);
			cw_none(&v->g, 0, v->h);
			free(v->lo);
			free(v->hi);
			v->lo = xm((size_t)v->h * sizeof *v->lo);
			v->hi = xm((size_t)v->h * sizeof *v->hi);
			for (j = 0; j < v->h; j++) {
				v->lo[j] = -1;
				v->hi[j] = -1;
			}
			v->unc = 1;
			v->chg = 1;
			continue;
		}
		if (o->kind == 'n') {
			for (c = o->col; c < o->col + o->n && c < v->g.cols; c++) {
				x = &v->g.c[(size_t)o->row * v->g.cols + c];
				free(x->ext);
				memset(x, 0, sizeof *x);
				x->cp = CN_NONE;
				x->w = 1;
			}
			if (c > o->col)
				cw_dmg(v, o->row, o->col, c - 1);
			v->unc = 1;
			k->cells += o->n;
			continue;
		}
		c = o->col;
		last = 0;
		ln = (size_t)o->n;
		for (at = 0; at < ln;) {
			l = u8dec(base + o->off + at, ln - at, &cp);
			w = u8w(cp);
			if (w == 0) {
				if (last)
					cn_cellext(last, base + o->off + at,
						   (size_t)l);
				at += (size_t)l;
				continue;
			}
			at += (size_t)l;
			if (c + w > v->g.cols) {
				last = 0;
				c += w;
				continue;
			}
			cw_ksplit(&v->g, o->row, c);
			if (w == 2)
				cw_ksplit(&v->g, o->row, c + 1);
			x = &v->g.c[(size_t)o->row * v->g.cols + c];
			cn_cellset(x, cp, 0, 0);
			x->w = (unsigned char)w;
			x->cont = 0;
			x->fg = o->fg;
			x->bg = o->bg;
			x->attr = o->at;
			x->ul = o->ul;
			x->link = o->lk;
			if (w == 2) {
				cn_cellset(x + 1, 0, 0, 0);
				x[1] = *x;
				x[1].ext = 0;
				x[1].cp = 0;
				x[1].w = 0;
				x[1].cont = 1;
			}
			last = x;
			k->cells++;
			c += w;
		}
		if (c > o->col)
			cw_dmg(v, o->row, o->col,
			       (c > v->g.cols ? v->g.cols : c) - 1);
	}
}


/* A line that is not whole yet, which stops the reading: one already past
   CN_CWLINE never will be. */
void cw_short(cw_conn *k, const char *ln, const char *e)
{
	if ((size_t)(e - ln) > CN_CWLINE)
		cw_broke(k, "a line too long");
}

/* Read what a connection has sent, without waiting, and apply each whole
   frame of a view it holds to that view's kept grid: CN_CWPASS bytes at
   most a pass, the rest left in the socket for the next. A frame of a view
   it does not hold now, of another generation, of another size, or past
   its bound is skipped to its end, line by line as it comes, never held;
   one still arriving is read again from its w next pass. -1 once broken. */
int cw_take(cw_conn *k)
{
	char *b;
	ssize_t got;
	size_t total = 0, pos = 0, done = 0, fb = 0, on = 0, ocap = 0;
	const char *p, *e, *ln, *q, *nl;
	unsigned long f[8];
	cw_op *ops = 0, *o;
	cw_view *cur = 0;
	int infr = k->skip, skip = k->skip;
	char kind;

	b = xm(HIBR_IOCH);
	while (total < CN_CWPASS) {
		got = recv(k->fd, b, HIBR_IOCH, MSG_DONTWAIT);
		if (got > 0) {
			s_add(&k->in, b, (size_t)got);
			total += (size_t)got;
			continue;
		}
		if (got == 0)
			k->gone = 1;
		else if (errno == EINTR)
			continue;
		break;
	}
	free(b);
	k->bytes += (long)total;
	p = k->in.p;
	e = p ? p + k->in.n : 0;
	while (p && p + pos < e && !k->broken) {
		ln = p + pos;
		kind = *ln;
		q = ln + 1;
		if (kind == 'r' || kind == 'l') {
			int rc = cw_fields(&q, e, kind == 'r' ? 8 : 2,
					   kind == 'r' ? "ddxxxxdd" : "dd", f);
			unsigned long len;

			if (rc < 0) {
				cw_short(k, ln, e);
				break;
			}
			if (!rc || *q != ' ') {
				cw_broke(k, "a field that is not a number");
				break;
			}
			len = f[kind == 'r' ? 7 : 1];
			if (len > CN_CWLINE) {
				cw_broke(k, "a line too long");
				break;
			}
			q++;
			if ((size_t)(e - q) < len + 1)
				break;
			nl = q + len;
			if (*nl != '\n') {
				cw_broke(k, "text not as long as it said");
				break;
			}
		} else {
			nl = memchr(ln, '\n', (size_t)(e - ln));
			if (!nl) {
				cw_short(k, ln, e);
				break;
			}
		}
		pos = (size_t)(nl + 1 - p);
		if (kind == 'l' || kind == 't') {
			if (infr) {
				cw_broke(k, "a link or translucency inside a "
					    "frame");
				break;
			}
			if (kind == 'l') {
				unsigned keep = cn_plink;
				char *u;

				if (f[0] < 1 || f[0] > CN_CWLINKS) {
					cw_broke(k, "a link ID out of range");
					break;
				}
				k->lkbytes -= k->lkb[f[0]];
				k->lkb[f[0]] = f[1];
				k->lkbytes += f[1];
				if (k->lkbytes > CN_CWLKB) {
					cw_broke(k, "more links at once than "
						    "it may have");
					break;
				}
				u = xm(f[1] + 1);
				memcpy(u, q, f[1]);
				u[f[1]] = 0;
				cn_link(u);
				k->lk[f[0]] = cn_plink;
				cn_plink = keep;
				free(u);
			} else {
				if (cw_fields(&q, nl + 1, 4, "ddxx", f) != 1 ||
				    q != nl || f[0] < 1 || f[0] > CN_CWTS) {
					cw_broke(k, "a translucency it cannot "
						    "have");
					break;
				}
				k->ts[f[0]].on = 1;
				k->ts[f[0]].pct = f[1] > 100 ? 100 : (int)f[1];
				k->ts[f[0]].fb = (unsigned)f[2];
				k->ts[f[0]].mat = (unsigned)f[3];
			}
			done = pos;
			continue;
		}
		if (kind == 'w') {
			const char *sp = memchr(ln, ' ', (size_t)(nl - ln));
			const char *g = sp ? memchr(sp + 1, ' ',
						    (size_t)(nl - sp - 1)) : 0;
			str nm;

			if (infr || sp != ln + 1 || !g ||
			    !cw_num(g + 1, nl + 1, 0, f, &q) || q != nl) {
				cw_broke(k, "a frame not as the record has it");
				break;
			}
			s_init(&nm);
			s_add(&nm, sp + 1, (size_t)(g - sp - 1));
			cur = cw_vfind(nm.p ? nm.p : "");
			infr = 1;
			fb = 0;
			on = 0;
			skip = !cur || cur->k != k || cur->gen != (long)f[0];
			if (skip)
				lg(HIBR_LDBG, "cells: a frame for %s at %lu, "
					      "which connection %d does not hold"
					      " now: skipped", nm.p ? nm.p : "",
				   f[0], k->fd);
			s_free(&nm);
			if (skip)
				done = pos;
			continue;
		}
		if (kind == 'e') {
			if (!infr || nl != ln + 1) {
				cw_broke(k, "an end outside a frame");
				break;
			}
			if (!skip && !(on && ops[0].kind == 's') &&
			    (!cur->g.c || cur->g.rows != cur->h ||
			     cur->g.cols != cur->w)) {
				cur->again = 1;
				lg(HIBR_LDBG, "cells: %s changed a grid the "
					      "desktop does not hold at its "
					      "size: skipped", cur->nm);
			} else if (!skip) {
				cw_apply(k, cur, ops, on, p);
				k->frames++;
			}
			infr = 0;
			skip = 0;
			cur = 0;
			done = pos;
			if (k->echo) {
				k->echo = 0;
				cn_watchin(k->fd, 0, 1);
			}
			continue;
		}
		if (kind != 's' && kind != 'r' && kind != 'n') {
			cw_broke(k, "a line it does not know");
			break;
		}
		if (!infr) {
			cw_broke(k, "cells outside a frame");
			break;
		}
		if (kind == 's' && cw_fields(&q, nl + 1, 2, "dd", f) != 1) {
			cw_broke(k, "a field that is not a number");
			break;
		}
		if (kind == 'n' && cw_fields(&q, nl + 1, 3, "ddd", f) != 1) {
			cw_broke(k, "a field that is not a number");
			break;
		}
		if (kind == 'r' && f[6] && (f[6] > CN_CWLINKS || !k->lk[f[6]])) {
			cw_broke(k, "a link it never named");
			break;
		}
		if (kind == 'r' && (f[4] & CN_TSMASK) &&
		    !k->ts[(f[4] & CN_TSMASK) >> CN_TSSHIFT].on) {
			cw_broke(k, "a translucency it never named");
			break;
		}
		if (skip) {
			done = pos;
			continue;
		}
		fb += pos - (size_t)(ln - p);
		if (kind == 's' && (on || f[0] != (unsigned long)cur->h ||
				    f[1] != (unsigned long)cur->w)) {
			lg(HIBR_LDBG, "cells: %s said %lu by %lu where it is %d "
				      "by %d: skipped", cur->nm, f[0], f[1],
			   cur->h, cur->w);
			skip = 1;
		} else if (fb > (size_t)CN_CWRUN * cur->h * cur->w) {
			lg(HIBR_LDBG, "cells: %s sent a frame larger than it "
				      "can be: skipped", cur->nm);
			skip = 1;
		}
		if (skip) {
			cur->again = 1;
			done = pos;
			continue;
		}
		if (kind != 's' && (f[0] >= (unsigned long)cur->h ||
				    f[1] >= (unsigned long)cur->w))
			continue;
		if (on == ocap) {
			ocap = ocap ? ocap * 2 : 64;
			ops = xr(ops, ocap * sizeof *ops);
		}
		o = &ops[on++];
		memset(o, 0, sizeof *o);
		o->kind = kind;
		if (kind == 's')
			continue;
		o->row = (int)f[0];
		o->col = (int)f[1];
		if (kind == 'n') {
			o->n = f[2] > (unsigned long)cur->w ? cur->w : (int)f[2];
			continue;
		}
		o->fg = (unsigned)f[2];
		o->bg = (unsigned)f[3];
		o->at = (unsigned)f[4] & ~CN_SHADOWED;
		o->ul = (unsigned)f[5];
		o->lk = f[6] ? k->lk[f[6]] : 0;
		o->n = (int)f[7];
		o->off = (size_t)(q - p);
	}
	free(ops);
	k->skip = infr && skip;
	if (done && p) {
		memmove(k->in.p, k->in.p + done, k->in.n - done);
		k->in.n -= done;
		k->in.p[k->in.n] = 0;
	}
	return k->broken ? -1 : 0;
}

/* Take what connection fd has sent and say what it came to: frames, cells
   and bytes since the last asking, then the views it changed, those it
   uncovered and those the desktop wants whole again, a line each. 0, 1
   once its far end has gone -- a frame it left unfinished is let go, since
   nothing will finish it -- 2 once it is broken. */
int cw_takev(sh *s, int fd)
{
	cw_conn *k = cw_conn4(fd, 0);
	str o, ch, un, ag;
	size_t i;
	cw_view *v;
	int rc;

	if (!k) {
		lg(HIBR_LERR, "console cells take: %d holds no view", fd);
		return HIBR_FAIL;
	}
	rc = cw_take(k);
	s_init(&o);
	s_init(&ch);
	s_init(&un);
	s_init(&ag);
	s_num(&o, k->frames);
	s_ch(&o, ' ');
	s_num(&o, k->cells);
	s_ch(&o, ' ');
	s_num(&o, k->bytes);
	for (i = 0; i < cw_vs.n; i++) {
		v = cw_vs.p[i];
		if (v->k != k)
			continue;
		if (v->chg) {
			s_ch(&ch, ' ');
			s_cat(&ch, v->nm);
		}
		if (v->unc) {
			s_ch(&un, ' ');
			s_cat(&un, v->nm);
		}
		if (v->again) {
			s_ch(&ag, ' ');
			s_cat(&ag, v->nm);
		}
		v->chg = v->unc = v->again = 0;
	}
	s_cat(&o, "\nchanged");
	if (ch.n)
		s_add(&o, ch.p, ch.n);
	s_cat(&o, "\nuncovered");
	if (un.n)
		s_add(&o, un.p, un.n);
	s_cat(&o, "\nagain");
	if (ag.n)
		s_add(&o, ag.p, ag.n);
	hibr_ret(s, o.p);
	if (!s->bind)
		printf("%s\n", o.p);
	k->frames = k->cells = k->bytes = 0;
	s_free(&o);
	s_free(&ch);
	s_free(&un);
	s_free(&ag);
	if (rc < 0)
		return 2;
	if (!k->gone)
		return HIBR_OK;
	if (k->in.n)
		lg(HIBR_LDBG, "cells: connection %d ended inside a frame, which "
			      "is let go", fd);
	k->in.n = 0;
	return 1;
}

/* A key, a click or anything else the person did has been told to the app
   that draws a view: its cells channel is input until the frame that
   answers it has come, so the echo ends a wait the output cap is holding
   and is drawn the moment it arrives, as in-process it was drawn in the
   frame the key began. */
int cw_input(const char *nm)
{
	cw_view *v = cw_vfind(nm);

	if (!v) {
		lg(HIBR_LERR, "console cells input: %s: no such view", nm);
		return HIBR_FAIL;
	}
	v->k->echo = 1;
	cn_watchin(v->k->fd, 1, 1);
	return HIBR_OK;
}

/* Draw a view's kept cells into the screen at its place: those that
   changed since it was last drawn, or all of them, which is the repaint a
   move, an uncovering or a workspace asks for. Each goes through the write
   every app's own drawing goes through (cn_putg), so what is beneath, the
   translucency in force and the panes above decide its background exactly
   as they did in-process; and the pen, the translucency and what is being
   written through are put back after. */
int cw_show(const char *nm, int all)
{
	cw_view *v = cw_vfind(nm);
	cn_pane *p = 0;
	int r, c, c0, c1, br, bc, ph, pw, row, g, was, tsid = 0, tsnow;
	unsigned sfg = cn_fg, sbg = cn_bg, sat = cn_penat, sul = cn_ul;
	unsigned slk = cn_plink, skf = cn_kfb, tfb, tmat;
	int tpct;
	const cn_pane *wp = cn_wpane;
	cn_cell *k;

	if (!v) {
		lg(HIBR_LERR, "console cells show: %s: no such view", nm);
		return HIBR_FAIL;
	}
	if (!v->g.c)
		return HIBR_OK;
	if (cn_fitq() != HIBR_OK)
		return HIBR_FAIL;
	if (v->pane) {
		p = cn_pfind(v->pane);
		if (!p)
			return HIBR_OK;
		br = p->row;
		bc = p->col;
		ph = p->h;
		pw = p->w;
	} else {
		br = 0;
		bc = 0;
		ph = cn_back.rows;
		pw = cn_back.cols;
	}
	cn_wrote++;
	if (p)
		p->drawn = 1;
	was = cn_behind(0);
	cn_wpane = p;
	tpct = cn_translucent(-1, DP_DEFAULT);
	cn_transget(&tfb, &tmat);
	for (r = 0; r < v->g.rows && r < v->h; r++) {
		c0 = all ? 0 : v->lo[r];
		c1 = all ? v->g.cols - 1 : v->hi[r];
		if (c1 >= v->w)
			c1 = v->w - 1;
		v->lo[r] = v->hi[r] = -1;
		if (c0 < 0 || v->r + r >= ph)
			continue;
		row = br + v->r + r;
		g = cn_gband(row);
		if (c0 > 0 && v->g.c[(size_t)r * v->g.cols + c0].cont)
			c0--;
		for (c = c0; c <= c1 && v->c + c < pw; c++) {
			k = &v->g.c[(size_t)r * v->g.cols + c];
			if (k->cp == CN_NONE || k->cont)
				continue;
			if (v->c + c + k->w > pw)
				break;
			tsnow = (int)((k->attr & CN_TSMASK) >> CN_TSSHIFT);
			if (tsnow != tsid) {
				if (tsnow) {
					cn_translucent(v->k->ts[tsnow].pct,
						       v->k->ts[tsnow].fb);
					cn_transmatch(v->k->ts[tsnow].mat);
				} else {
					cn_translucent(tpct, tfb);
					cn_transmatch(tmat);
				}
				tsid = tsnow;
			}
			cn_fg = k->fg;
			cn_bg = k->attr & CN_KEPT ? CN_KEEPBG : k->bg;
			cn_kfb = k->bg;
			cn_penat = k->attr & ~(CN_KEPT | CN_TSMASK);
			cn_ul = k->ul;
			cn_plink = k->link;
			cn_putg(row, bc + v->c + c, k->cp, k->ext,
				k->ext ? strlen(k->ext) : 0, k->w, g);
		}
	}
	if (tsid) {
		cn_translucent(tpct, tfb);
		cn_transmatch(tmat);
	}
	cn_fg = sfg;
	cn_bg = sbg;
	cn_penat = sat;
	cn_ul = sul;
	cn_plink = slk;
	cn_kfb = skf;
	cn_wpane = wp;
	cn_behind(was);
	return HIBR_OK;
}

/* console cells ...: an app's own side (open, view, forget, clear, again)
   and the desktop's (own, drop, take, show). */
int cw_verb(sh *s, int ac, char **av)
{
	const char *v = ac > 2 ? av[2] : "";
	cw_av *a;

	if (!strcmp(v, "open") && ac == 4 && cn_isint(av[3]))
		return cw_open(atoi(av[3])) ? HIBR_OK : HIBR_FAIL;
	if (!strcmp(v, "view") && ac == 7 && cn_isint(av[4]) &&
	    cn_isint(av[5]) && cn_isint(av[6]))
		return cw_aview(av[3], atol(av[4]), atoi(av[5]), atoi(av[6])) ?
			       HIBR_OK : HIBR_FAIL;
	if (!strcmp(v, "forget") && ac == 4)
		return cw_aforget(av[3]) ? HIBR_OK : HIBR_FAIL;
	if (!strcmp(v, "clear") && ac == 4)
		return cw_clear(av[3]) ? HIBR_OK : HIBR_FAIL;
	if (!strcmp(v, "again") && ac == 4) {
		a = cw_afind(av[3]);
		if (!a)
			return HIBR_FAIL;
		a->sent = 0;
		a->drawn = 1;
		return HIBR_OK;
	}
	if (!strcmp(v, "own") && ac == 11 && cn_isint(av[3]) &&
	    cn_isint(av[5]) && cn_isint(av[7]) && cn_isint(av[8]) &&
	    cn_isint(av[9]) && cn_isint(av[10]))
		return cw_own(atoi(av[3]), av[4], atol(av[5]),
			      strcmp(av[6], "-") ? av[6] : 0, atoi(av[7]),
			      atoi(av[8]), atoi(av[9]), atoi(av[10])) ?
			       HIBR_OK : HIBR_FAIL;
	if (!strcmp(v, "drop") && (ac == 4 || ac == 5) && cn_isint(av[3]))
		return cw_drop(atoi(av[3]), ac == 5 ? av[4] : 0) ? HIBR_OK :
								   HIBR_FAIL;
	if (!strcmp(v, "take") && ac == 4 && cn_isint(av[3]))
		return cw_takev(s, atoi(av[3]));
	if (!strcmp(v, "show") && (ac == 4 || (ac == 5 && !strcmp(av[4], "-a"))))
		return cw_show(av[3], ac == 5);
	if (!strcmp(v, "input") && ac == 4)
		return cw_input(av[3]);
	lg(HIBR_LERR, "usage: console cells open FD | view NAME GEN H W | "
		      "forget NAME | clear NAME | again NAME | own FD NAME GEN "
		      "PANE|- ROW COL H W | drop FD [NAME] | take FD | show "
		      "NAME [-a] | input NAME");
	return 2;
}

/* Let every view and connection go, at the module's end. */
void cw_fin(void)
{
	size_t i;

	while (cw_ks.n)
		cw_drop(((cw_conn *)cw_ks.p[0])->fd, 0);
	while (cw_vs.n)
		cw_vfree(cw_vs.p[0]);
	for (i = 0; i < cw_avs.n; i++) {
		free(((cw_av *)cw_avs.p[i])->nm);
		free(cw_avs.p[i]);
	}
	v_free(&cw_ks);
	v_free(&cw_vs);
	v_free(&cw_avs);
	free(cw_lid);
	cw_lid = 0;
	cw_lidn = 0;
	cn_hl = 0;
	cw_afd = -1;
}

/* Whether there is a display to draw on, for another module: a terminal,
   or an app's own console with no terminal, whose cells reach the desktop
   as records. */
int cn_dpopen(void)
{
	return cn_isopen() || cn_hl;
}
