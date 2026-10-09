#include "mm.h"
#include <stdlib.h>
#include <string.h>

static const unsigned mm_uni[MM_GN] = {
	0x2500, 0x2502, 0x250C, 0x2510, 0x2514, 0x2518,
	0x2534, 0x252C, 0x2524, 0x251C, 0x253C,
	0x25BC, 0x25B2, 0x25C0, 0x25B6,
	0x254C, 0x254E, 0x2501, 0x2503,
	0x256D, 0x256E, 0x2570, 0x256F, 0x2588, 0x25CB, 0x00D7
};

static const unsigned mm_asc[MM_GN] = {
	'-', '|', '+', '+', '+', '+',
	'+', '+', '+', '+', '+',
	'v', '^', '<', '>',
	'.', ':', '=', '|',
	'+', '+', '+', '+', '#', 'o', 'x'
};

/* The character a named part of a drawing is made of, in either set. The
   module draws its own lines rather than naming the desktop's glyphs,
   because it has no GL and a script is not the only caller -- so it
   carries both sets itself and is told which. */
unsigned mm_glyph(int g, int ascii)
{
	if (g < 0 || g >= MM_GN)
		return ' ';
	return ascii ? mm_asc[g] : mm_uni[g];
}

/* A codepoint as UTF-8. */
void mm_u8(str *o, unsigned cp)
{
	if (cp < 0x80) {
		s_ch(o, (int)cp);
	} else if (cp < 0x800) {
		s_ch(o, (int)(0xC0 | (cp >> 6)));
		s_ch(o, (int)(0x80 | (cp & 0x3F)));
	} else if (cp < 0x10000) {
		s_ch(o, (int)(0xE0 | (cp >> 12)));
		s_ch(o, (int)(0x80 | ((cp >> 6) & 0x3F)));
		s_ch(o, (int)(0x80 | (cp & 0x3F)));
	} else {
		s_ch(o, (int)(0xF0 | (cp >> 18)));
		s_ch(o, (int)(0x80 | ((cp >> 12) & 0x3F)));
		s_ch(o, (int)(0x80 | ((cp >> 6) & 0x3F)));
		s_ch(o, (int)(0x80 | (cp & 0x3F)));
	}
}

/* One cell, if it is on the grid at all. */
void mm_put(mm_grid *g, int x, int y, unsigned cp, int sty)
{
	if (x < 0 || y < 0 || x >= g->w || y >= g->h)
		return;
	g->cp[y * g->w + x] = cp;
	g->sty[y * g->w + x] = (char)sty;
}

/* Text from a column, a character at a time, each cell its own. */
void mm_text(mm_grid *g, int x, int y, const char *t, int sty)
{
	size_t n = t ? strlen(t) : 0, i = 0;
	unsigned cp;
	int l;

	while (i < n) {
		l = u8dec(t + i, n - i, &cp);
		if (l < 1)
			l = 1;
		mm_put(g, x, y, cp, sty);
		x += u8w(cp);
		i += (size_t)l;
	}
}

/* A node's box: three rows, with the sides that say what shape it is. */
void mm_box(mm_grid *g, mm_dia *d, mm_node *n, int ascii)
{
	int i, tl, tr, bl, br, sl, sr, pad;

	if (n->dummy) {
		mm_put(g, n->x, n->y,
		       mm_glyph(mm_vert(d) ? MM_GVLINE : MM_GHLINE, ascii),
		       'e');
		return;
	}
	tl = MM_GTL;
	tr = MM_GTR;
	bl = MM_GBL;
	br = MM_GBR;
	sl = sr = MM_GVLINE;
	if (n->shape == MM_ROUND || n->shape == MM_CIRCLE) {
		tl = MM_GDIAL;
		tr = MM_GDIAR;
		bl = MM_GDIBL;
		br = MM_GDIBR;
	}
	for (i = 1; i < n->w - 1; i++) {
		mm_put(g, n->x + i, n->y, mm_glyph(MM_GHLINE, ascii), 'b');
		mm_put(g, n->x + i, n->y + 2, mm_glyph(MM_GHLINE, ascii), 'b');
	}
	mm_put(g, n->x, n->y, mm_glyph(tl, ascii), 'b');
	mm_put(g, n->x + n->w - 1, n->y, mm_glyph(tr, ascii), 'b');
	mm_put(g, n->x, n->y + 2, mm_glyph(bl, ascii), 'b');
	mm_put(g, n->x + n->w - 1, n->y + 2, mm_glyph(br, ascii), 'b');
	if (n->shape == MM_DIAMOND) {
		mm_put(g, n->x, n->y + 1, '<', 'b');
		mm_put(g, n->x + n->w - 1, n->y + 1, '>', 'b');
	} else if (n->shape == MM_CIRCLE) {
		mm_put(g, n->x, n->y + 1, '(', 'b');
		mm_put(g, n->x + n->w - 1, n->y + 1, ')', 'b');
	} else {
		mm_put(g, n->x, n->y + 1, mm_glyph(sl, ascii), 'b');
		mm_put(g, n->x + n->w - 1, n->y + 1,
		       mm_glyph(sr, ascii), 'b');
	}
	for (i = 1; i < n->w - 1; i++)
		mm_put(g, n->x + i, n->y + 1, ' ', 't');
	pad = 1 + (n->w - 2 - (int)mm_width(n->label.p)) / 2;
	if (pad < 1)
		pad = 1;
	mm_text(g, n->x + pad, n->y + 1, n->label.p, 't');
}

/* The line a kind of link is drawn with, along and across. */
int mm_lineg(int style, int along, int ascii)
{
	(void)ascii;
	if (style == MM_DOTTED)
		return along ? MM_GDVLINE : MM_GDHLINE;
	if (style == MM_THICK)
		return along ? MM_GTVLINE : MM_GTHLINE;
	return along ? MM_GVLINE : MM_GHLINE;
}

/* One edge, as three runs: out of the face it leaves, along its own track
   in the gutter, and into the face it enters. A segment whose two faces
   line up has no track and is one straight run. */
void mm_wire(mm_grid *g, mm_dia *d, mm_edge *e, int ascii)
{
	mm_node *a = (mm_node *)d->nodes.p[e->from];
	mm_node *b = (mm_node *)d->nodes.p[e->to];
	int up = d->dir == MM_BT || d->dir == MM_RL;
	int vert = mm_vert(d);
	int sg = up ? -1 : 1;
	int lo, hi, i, t, head, c0 = e->c0, c1 = e->c1;
	int from, to;

	if (vert) {
		from = up ? a->y - 1 : a->y + a->h;
		to = up ? b->y + b->h : b->y - 1;
	} else {
		from = up ? a->x - 1 : a->x + a->w;
		to = up ? b->x + b->w : b->x - 1;
	}
	head = e->last && e->arrow >= 0;
	t = e->track;
	if (t < 0) {
		for (i = from; i != to + sg; i += sg)
			if (vert)
				mm_put(g, c0, i,
				       mm_glyph(mm_lineg(e->style, 1, ascii),
						ascii), 'e');
			else
				mm_put(g, i, c0,
				       mm_glyph(mm_lineg(e->style, 0, ascii),
						ascii), 'e');
	} else {
		for (i = from; i != t; i += sg)
			if (vert)
				mm_put(g, c0, i,
				       mm_glyph(mm_lineg(e->style, 1, ascii),
						ascii), 'e');
			else
				mm_put(g, i, c0,
				       mm_glyph(mm_lineg(e->style, 0, ascii),
						ascii), 'e');
		lo = c0 < c1 ? c0 : c1;
		hi = c0 < c1 ? c1 : c0;
		for (i = lo; i <= hi; i++)
			if (vert)
				mm_put(g, i, t,
				       mm_glyph(mm_lineg(e->style, 0, ascii),
						ascii), 'e');
			else
				mm_put(g, t, i,
				       mm_glyph(mm_lineg(e->style, 1, ascii),
						ascii), 'e');
		/* The two bends. Which corner each is depends on the way the
		   ranks run as well as on which side the other end is. */
		if (vert) {
			mm_put(g, c0, t, mm_glyph(up ?
						  (c1 > c0 ? MM_GTL : MM_GTR) :
						  (c1 > c0 ? MM_GBL : MM_GBR),
						  ascii), 'e');
			mm_put(g, c1, t, mm_glyph(up ?
						  (c1 > c0 ? MM_GBR : MM_GBL) :
						  (c1 > c0 ? MM_GTR : MM_GTL),
						  ascii), 'e');
		} else {
			/* Running sideways the corners are the other pair:
			   the first joins the way the line came in to the way
			   it turns, so for LR going down it is ┐ and not ┌.
			   Having them swapped drew a diagram made of corners
			   that did not meet, which is how this was found. */
			mm_put(g, t, c0, mm_glyph(up ?
						  (c1 > c0 ? MM_GTL : MM_GBL) :
						  (c1 > c0 ? MM_GTR : MM_GBR),
						  ascii), 'e');
			mm_put(g, t, c1, mm_glyph(up ?
						  (c1 > c0 ? MM_GBR : MM_GTR) :
						  (c1 > c0 ? MM_GBL : MM_GTL),
						  ascii), 'e');
		}
		for (i = t + sg; i != to + sg; i += sg)
			if (vert)
				mm_put(g, c1, i,
				       mm_glyph(mm_lineg(e->style, 1, ascii),
						ascii), 'e');
			else
				mm_put(g, i, c1,
				       mm_glyph(mm_lineg(e->style, 0, ascii),
						ascii), 'e');
	}
	/* The head of an edge that was reversed to break a cycle belongs at
	   the end it originally pointed at, which after the reversal is the
	   face it leaves rather than the one it enters -- or the arrow on
	   every loop in a diagram would point the wrong way round. */
	if (head) {
		int hg = e->arrow == MM_CROSS ? MM_GXMARK :
			e->arrow == MM_RING ? MM_GRING :
			vert ? (up ? MM_GUP : MM_GDOWN) :
			(up ? MM_GLEFT : MM_GRIGHT);
		if (vert)
			mm_put(g, c1, to, mm_glyph(hg, ascii), 'e');
		else
			mm_put(g, to, c1, mm_glyph(hg, ascii), 'e');
	}
	if (e->rev && e->first) {
		int hg = e->arrow == MM_CROSS ? MM_GXMARK :
			e->arrow == MM_RING ? MM_GRING :
			vert ? (up ? MM_GDOWN : MM_GUP) :
			(up ? MM_GRIGHT : MM_GLEFT);
		if (vert)
			mm_put(g, c0, from, mm_glyph(hg, ascii), 'e');
		else
			mm_put(g, from, c0, mm_glyph(hg, ascii), 'e');
	}
	/* Clear of both verticals, so a label can never be read as part of
	   a line or hide one. */
	if (e->label.n && e->first) {
		int at = (c0 > c1 ? c0 : c1) + 2;
		if (t >= 0) {
			if (vert)
				mm_text(g, at, t, e->label.p, 'l');
			else
				mm_text(g, t, at, e->label.p, 'l');
		} else if (vert) {
			mm_text(g, c0 + 2, from, e->label.p, 'l');
		} else {
			mm_text(g, from, c0 + 2, e->label.p, 'l');
		}
	}
}

/* How far right an edge's own label reaches, so the grid can be made wide
   enough for it rather than cutting it off. */
int mm_labelroom(mm_dia *d)
{
	size_t i;
	int w = d->w, at;
	mm_edge *e;

	for (i = 0; i < d->edges.n; i++) {
		e = (mm_edge *)d->edges.p[i];
		if (!e->label.n || !e->first)
			continue;
		if (!mm_vert(d))
			continue;
		at = (e->track >= 0 ? (e->c0 > e->c1 ? e->c0 : e->c1) : e->c0)
			+ 2 + (int)mm_width(e->label.p);
		if (at > w)
			w = at;
	}
	return w;
}

/* A sequence diagram: a column for each participant in the order they were
   declared or first spoken to, and a row for each message in the order it
   was sent. There is no layout to do -- which is why it is here at all in
   the release flowcharts arrived in. */
void mm_seq(mm_grid **gp, mm_dia *d, int ascii)
{
	size_t i, j;
	int *cx, w = 0, h, y, lw = 0, gap, pad = 0;
	mm_node *n;
	mm_msg *m;
	mm_grid *g;

	/* The room between two lifelines is the longest message plus two, so
	   a label always fits between them with a cell clear on each side:
	   centred in a gap that was too small it was written over the
	   lifeline itself. */
	for (i = 0; i < d->msgs.n; i++) {
		m = (mm_msg *)d->msgs.p[i];
		if ((int)mm_width(m->text.p) > lw)
			lw = (int)mm_width(m->text.p);
	}
	gap = lw + 2 > 4 ? lw + 2 : 4;
	cx = xm((d->nodes.n + 1) * sizeof *cx);
	for (i = 0; i < d->nodes.n; i++) {
		n = (mm_node *)d->nodes.p[i];
		n->w = (int)mm_width(n->label.p) + 4;
		n->h = 3;
		cx[i] = w;
		n->x = w;
		n->y = 0;
		w += n->w + gap;
	}
	w -= gap;
	h = 2 * (int)d->msgs.n + 3;
	if (w < lw + 4)
		w = lw + 4;
	/* A note written beside the outermost lifeline needs room made for
	   it on that side, before anything is placed: clamped into the grid
	   instead, it was drawn over the lifeline on the left and cut off
	   on the right. */
	for (i = 0; i < d->msgs.n; i++) {
		int c, need;
		m = (mm_msg *)d->msgs.p[i];
		if (!m->note || m->from < 0)
			continue;
		n = (mm_node *)d->nodes.p[m->from];
		c = cx[m->from] + n->w / 2;
		if (m->note == MM_NOTE_LEFT) {
			need = (int)mm_width(m->text.p) + 2 - c;
			if (need > pad)
				pad = need;
		} else if (m->note == MM_NOTE_RIGHT) {
			int t2 = m->to >= 0 ? m->to : m->from;
			c = cx[t2] + ((mm_node *)d->nodes.p[t2])->w / 2;
			need = c + 2 + (int)mm_width(m->text.p);
			if (need > w)
				w = need;
		}
	}
	if (pad) {
		for (i = 0; i < d->nodes.n; i++) {
			cx[i] += pad;
			((mm_node *)d->nodes.p[i])->x += pad;
		}
		w += pad;
	}
	g = xm(sizeof *g);
	memset(g, 0, sizeof *g);
	g->w = w;
	g->h = h;
	g->cp = xm((size_t)w * h * sizeof *g->cp);
	g->sty = xm((size_t)w * h + 1);
	for (i = 0; i < (size_t)w * h; i++) {
		g->cp[i] = ' ';
		g->sty[i] = '.';
	}
	for (i = 0; i < d->nodes.n; i++) {
		n = (mm_node *)d->nodes.p[i];
		mm_box(g, d, n, ascii);
		for (y = 3; y < h; y++)
			mm_put(g, n->x + n->w / 2, y,
			       mm_glyph(MM_GDVLINE, ascii), 'k');
	}
	y = 4;
	for (i = 0; i < d->msgs.n; i++) {
		int a, b, lo, hi, mid;
		m = (mm_msg *)d->msgs.p[i];
		if (m->from < 0 || m->to < 0)
			continue;
		a = cx[m->from] + ((mm_node *)d->nodes.p[m->from])->w / 2;
		b = cx[m->to] + ((mm_node *)d->nodes.p[m->to])->w / 2;
		lo = a < b ? a : b;
		hi = a < b ? b : a;
		if (m->note) {
			/* A note is written beside its participants rather
			   than on the line, and takes a row of its own. */
			int at = m->note == MM_NOTE_LEFT ? lo - 2 -
				(int)mm_width(m->text.p) :
				m->note == MM_NOTE_RIGHT ? hi + 2 : lo + 1;
			if (at < 0)
				at = 0;
			mm_text(g, at, y, m->text.p, 'l');
			y += 2;
			continue;
		}
		if (a == b) {
			mm_text(g, a + 2, y, m->text.p, 'l');
			y += 2;
			continue;
		}
		for (j = (size_t)lo + 1; j < (size_t)hi; j++)
			mm_put(g, (int)j, y,
			       mm_glyph(m->style == MM_DOTTED ? MM_GDHLINE :
					MM_GHLINE, ascii), 'e');
		mm_put(g, b, y, mm_glyph(m->arrow == MM_CROSS ? MM_GXMARK :
					 m->arrow == MM_RING ? MM_GRING :
					 b > a ? MM_GRIGHT : MM_GLEFT, ascii),
		       'e');
		mid = lo + (hi - lo - (int)mm_width(m->text.p)) / 2;
		if (mid < lo + 1)
			mid = lo + 1;
		mm_text(g, mid, y - 1, m->text.p, 'l');
		y += 2;
	}
	free(cx);
	*gp = g;
}

/* A pie: one bar a slice, longest first, with its share written after it.
   Mermaid's own legend keeps the order they were written in and its chart
   is drawn by d3; this sorts, which is a choice made here and recorded in
   the tests rather than copied from Mermaid. */
void mm_pie(mm_grid **gp, mm_dia *d, int ascii, int wide)
{
	size_t i, j, k;
	int w, h, lw = 0, bar, y;
	double tot = 0;
	mm_grid *g;
	mm_slice *s, *t;
	str num;

	if (wide < 12)
		wide = 12;
	for (i = 0; i < d->slices.n; i++) {
		s = (mm_slice *)d->slices.p[i];
		tot += s->v;
		if ((int)mm_width(s->label.p) > lw)
			lw = (int)mm_width(s->label.p);
	}
	for (i = 0; i < d->slices.n; i++)
		for (j = i + 1; j < d->slices.n; j++) {
			s = (mm_slice *)d->slices.p[i];
			t = (mm_slice *)d->slices.p[j];
			if (t->v > s->v) {
				d->slices.p[i] = t;
				d->slices.p[j] = s;
			}
		}
	bar = wide - lw - 9;
	if (bar < 4)
		bar = 4;
	w = lw + 1 + bar + 8;
	h = (int)d->slices.n + (d->title.n ? 2 : 0);
	if ((int)mm_width(d->title.p) + 1 > w)
		w = (int)mm_width(d->title.p) + 1;
	g = xm(sizeof *g);
	memset(g, 0, sizeof *g);
	g->w = w;
	g->h = h;
	g->cp = xm((size_t)w * h * sizeof *g->cp);
	g->sty = xm((size_t)w * h + 1);
	for (i = 0; i < (size_t)w * h; i++) {
		g->cp[i] = ' ';
		g->sty[i] = '.';
	}
	y = 0;
	if (d->title.n) {
		mm_text(g, 0, 0, d->title.p, 'k');
		y = 2;
	}
	s_init(&num);
	for (i = 0; i < d->slices.n; i++) {
		int fill;
		s = (mm_slice *)d->slices.p[i];
		mm_text(g, 0, y, s->label.p, 't');
		fill = tot > 0 ? (int)(s->v * bar / tot + 0.5) : 0;
		for (k = 0; k < (size_t)bar; k++)
			mm_put(g, lw + 1 + (int)k, y,
			       (int)k < fill ? mm_glyph(MM_GBLOCK, ascii) : ' ',
			       (int)k < fill ? 'e' : '.');
		num.n = 0;
		if (num.p)
			num.p[0] = 0;
		s_num(&num, tot > 0 ? (long)(s->v * 100 / tot + 0.5) : 0L);
		s_cat(&num, "%");
		mm_text(g, lw + 1 + bar + 1, y, num.p, 'l');
		y++;
	}
	s_free(&num);
	*gp = g;
}

/* A diagram as cells, with a style for every one of them: `b` a box's
   border, `t` a node's own label, `e` a line or an arrowhead, `l` a label
   on an edge or a message, `k` a title or a lifeline, `.` nothing. */
mm_grid *mm_render(mm_dia *d, int ascii, int wide)
{
	size_t i;
	int w, h;
	mm_grid *g = 0;

	if (d->kind == MM_SEQ) {
		mm_seq(&g, d, ascii);
		d->w = g->w;
		d->h = g->h;
		return g;
	}
	if (d->kind == MM_PIE) {
		mm_pie(&g, d, ascii, wide);
		d->w = g->w;
		d->h = g->h;
		return g;
	}
	w = mm_labelroom(d);
	h = d->h;
	if (w < 1)
		w = 1;
	if (h < 1)
		h = 1;
	g = xm(sizeof *g);
	memset(g, 0, sizeof *g);
	g->w = w;
	g->h = h;
	g->cp = xm((size_t)w * h * sizeof *g->cp);
	g->sty = xm((size_t)w * h + 1);
	for (i = 0; i < (size_t)w * h; i++) {
		g->cp[i] = ' ';
		g->sty[i] = '.';
	}
	for (i = 0; i < d->edges.n; i++)
		mm_wire(g, d, (mm_edge *)d->edges.p[i], ascii);
	for (i = 0; i < d->nodes.n; i++)
		mm_box(g, d, (mm_node *)d->nodes.p[i], ascii);
	d->w = g->w;
	d->h = g->h;
	return g;
}

/* Give a drawing back. */
void mm_gfree(mm_grid *g)
{
	if (!g)
		return;
	free(g->cp);
	free(g->sty);
	free(g);
}
