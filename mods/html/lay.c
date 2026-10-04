#define _GNU_SOURCE

#include "tr.h"
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <unistd.h>

/* One laid-out line: its text, a style letter per character, its links as
   "col<TAB>len<TAB>url" lines in display columns, and an image that starts
   on it as "rows<TAB>cols<TAB>src<TAB>alt". */
typedef struct ly_line ly_line;
struct ly_line {
	str t, s, a, img;
	int w;
};

/* A prefix segment: a quote bar or a list item's indent, and the marker
   the item's first line shows in its place. */
typedef struct ly_seg ly_seg;
struct ly_seg {
	str t, s, mark, marks;
	int w, usemark;
};

/* The layout of one document, or of one table cell. */
typedef struct ly ly;
struct ly {
	int width, flags, depth;
	vec lines, segs, links, lists;
	str t, s, lk;
	int col, started;
	str wt, ws, wl;
	int wcol, space, blank, pre, hide;
	int b, it, u, st, code, head, quote, link;
};

enum { LY_IMAGES = 1, LY_ASCII = 2 };

void ly_node(ly *l, hl_n *n);
int ly_skip(hl_n *e);
void ly_flat(hl_n *n, str *o);

/* Prepare a layout of a width. */
void ly_init(ly *l, int width, int flags)
{
	memset(l, 0, sizeof *l);
	l->width = width < 8 ? 8 : width;
	l->flags = flags;
	l->link = -1;
	l->blank = 1;
	s_init(&l->t);
	s_init(&l->s);
	s_init(&l->lk);
	s_init(&l->wt);
	s_init(&l->ws);
	s_init(&l->wl);
}

/* Let go of a layout and everything in it. */
void ly_free(ly *l)
{
	size_t i;

	for (i = 0; i < l->lines.n; i++) {
		ly_line *x = l->lines.p[i];

		s_free(&x->t);
		s_free(&x->s);
		s_free(&x->a);
		s_free(&x->img);
		free(x);
	}
	for (i = 0; i < l->segs.n; i++) {
		ly_seg *g = l->segs.p[i];

		s_free(&g->t);
		s_free(&g->s);
		s_free(&g->mark);
		s_free(&g->marks);
		free(g);
	}
	for (i = 0; i < l->links.n; i++)
		free(l->links.p[i]);
	v_free(&l->lines);
	v_free(&l->segs);
	v_free(&l->links);
	v_free(&l->lists);
	s_free(&l->t);
	s_free(&l->s);
	s_free(&l->lk);
	s_free(&l->wt);
	s_free(&l->ws);
	s_free(&l->wl);
}

/* The display width of a UTF-8 string. */
int ly_w(const char *p, size_t n)
{
	unsigned c;
	int k, w = 0;

	while (n) {
		k = hl_utf8(p, n, &c);
		if (!k)
			break;
		w += u8w(c);
		p += k;
		n -= (size_t)k;
	}
	return w;
}

/* The style letter for text drawn now. */
char ly_sty(ly *l)
{
	if (l->head)
		return (char)('0' + l->head);
	if (l->link >= 0)
		return 'l';
	if (l->code)
		return 'c';
	if (l->b && l->it)
		return 'B';
	if (l->b)
		return 'b';
	if (l->it)
		return 'i';
	if (l->u)
		return 'u';
	if (l->st)
		return 's';
	if (l->quote)
		return 'q';
	return 'p';
}

/* Begin a line: the prefixes of the blocks it is in, a list item's marker
   in place of its indent on the item's first line. */
void ly_start(ly *l)
{
	size_t i;

	if (l->started)
		return;
	l->started = 1;
	l->t.n = l->s.n = l->lk.n = 0;
	l->col = 0;
	for (i = 0; i < l->segs.n; i++) {
		ly_seg *g = l->segs.p[i];

		if (g->usemark) {
			s_add(&l->t, g->mark.p, g->mark.n);
			s_add(&l->s, g->marks.p, g->marks.n);
			g->usemark = 0;
		} else {
			s_add(&l->t, g->t.p, g->t.n);
			s_add(&l->s, g->s.p, g->s.n);
		}
		l->col += g->w;
	}
	for (i = 0; i < l->s.n; i++) {
		int z = -1;

		s_add(&l->lk, (char *)&z, sizeof z);
	}
}

/* The column a line's own text starts at, after its prefixes. */
int ly_pcol(ly *l)
{
	size_t i;
	int w = 0;

	for (i = 0; i < l->segs.n; i++)
		w += ((ly_seg *)l->segs.p[i])->w;
	return w;
}

/* Finish the current line: kept with its links worked out from the link
   number each character carries. */
void ly_endline(ly *l)
{
	ly_line *x;
	const int *lk;
	size_t i, n;
	int col = 0, k, w;
	unsigned c;
	long run = -1, rcol = 0, rlen = 0;

	ly_start(l);
	x = xm(sizeof *x);
	memset(x, 0, sizeof *x);
	s_init(&x->t);
	s_init(&x->s);
	s_init(&x->a);
	s_init(&x->img);
	while (l->t.n && l->t.p[l->t.n - 1] == ' ' && !l->pre) {
		l->t.n--;
		l->s.n--;
		l->lk.n -= sizeof(int);
	}
	s_add(&x->t, l->t.p ? l->t.p : "", l->t.n);
	s_add(&x->s, l->s.p ? l->s.p : "", l->s.n);
	lk = (const int *)l->lk.p;
	n = l->lk.n / sizeof(int);
	for (i = 0, k = 0; i < n; i++) {
		int id = lk ? lk[i] : -1;

		w = 1;
		if ((size_t)k < l->t.n) {
			int b = hl_utf8(l->t.p + k, l->t.n - (size_t)k, &c);

			w = u8w(c);
			k += b;
		}
		if (id != run) {
			if (run >= 0 && rlen > 0) {
				s_num(&x->a, rcol);
				s_ch(&x->a, '\t');
				s_num(&x->a, rlen);
				s_ch(&x->a, '\t');
				s_cat(&x->a, l->links.p[run]);
				s_ch(&x->a, '\n');
			}
			run = id;
			rcol = col;
			rlen = 0;
		}
		rlen += w;
		col += w;
	}
	if (run >= 0 && rlen > 0) {
		s_num(&x->a, rcol);
		s_ch(&x->a, '\t');
		s_num(&x->a, rlen);
		s_ch(&x->a, '\t');
		s_cat(&x->a, l->links.p[run]);
		s_ch(&x->a, '\n');
	}
	x->w = col;
	v_add(&l->lines, x);
	l->started = 0;
	l->space = 0;
	l->blank = x->t.n == 0 || (size_t)ly_pcol(l) >= x->t.n ? l->blank + 1 : 0;
}

/* Append one character to the current line with a style and a link. */
void ly_put(ly *l, const char *p, int k, char sty, int link, int w)
{
	ly_start(l);
	s_add(&l->t, p, (size_t)k);
	s_ch(&l->s, sty);
	s_add(&l->lk, (char *)&link, sizeof link);
	l->col += w;
	l->blank = 0;
}

/* Put the word gathered so far on the line, breaking the line before it
   when it does not fit, and breaking the word itself when it is wider
   than a whole line. */
void ly_word(ly *l)
{
	size_t i = 0, j = 0;
	unsigned c;
	int k, w;
	const int *wl = (const int *)l->wl.p;

	if (!l->wt.n)
		return;
	ly_start(l);
	if (l->space && l->col > ly_pcol(l)) {
		if (l->col + 1 + l->wcol > l->width) {
			ly_endline(l);
			ly_start(l);
		} else {
			const int *lk = (const int *)l->lk.p;
			char prev = l->s.n ? l->s.p[l->s.n - 1] : 'p';
			int plk = lk && l->lk.n ? lk[l->lk.n / sizeof(int) - 1] : -1;

			ly_put(l, " ", 1, prev == l->ws.p[0] ? prev : 'p',
			       wl && plk == wl[0] ? plk : -1, 1);
		}
	} else if (l->col + l->wcol > l->width && l->col > ly_pcol(l)) {
		ly_endline(l);
		ly_start(l);
	}
	while (i < l->wt.n) {
		k = hl_utf8(l->wt.p + i, l->wt.n - i, &c);
		w = u8w(c);
		if (l->col + w > l->width && l->col > ly_pcol(l)) {
			ly_endline(l);
			ly_start(l);
		}
		ly_put(l, l->wt.p + i, k, l->ws.p[j], wl ? wl[j] : -1, w);
		i += (size_t)k;
		j++;
	}
	l->wt.n = l->ws.n = l->wl.n = 0;
	l->wcol = 0;
	l->space = 0;
}

/* Lay out text: whitespace collapsed to word breaks, except in pre. */
void ly_text(ly *l, const char *p, size_t n)
{
	unsigned c;
	int k, w;
	char sty = ly_sty(l);

	if (l->hide)
		return;
	while (n) {
		k = hl_utf8(p, n, &c);
		if (!k)
			break;
		if (l->pre) {
			if (c == '\n') {
				ly_word(l);
				ly_endline(l);
			} else if (c == '\t') {
				ly_word(l);
				ly_start(l);
				do
					ly_put(l, " ", 1, sty, l->link, 1);
				while ((l->col - ly_pcol(l)) % 8);
			} else {
				w = u8w(c);
				ly_start(l);
				if (l->col + w > l->width && l->col > ly_pcol(l))
					ly_endline(l);
				ly_put(l, p, k, sty, l->link, w);
			}
		} else if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f') {
			ly_word(l);
			l->space = 1;
		} else if (c == 0xA0) {
			s_add(&l->wt, " ", 1);
			s_ch(&l->ws, sty);
			s_add(&l->wl, (char *)&l->link, sizeof l->link);
			l->wcol++;
		} else if (c != 0xAD && c != 0x200B && c != 0x200C && c != 0xFEFF && c != 0x034F) {
			s_add(&l->wt, p, (size_t)k);
			s_ch(&l->ws, sty);
			s_add(&l->wl, (char *)&l->link, sizeof l->link);
			l->wcol += u8w(c);
		}
		p += k;
		n -= (size_t)k;
	}
}

/* Start a block: the line so far ended, and at least so many blank lines
   between it and what came before -- none at the very top. */
void ly_block(ly *l, int margin)
{
	ly_word(l);
	if (l->started && (l->col > ly_pcol(l) || l->t.n > (size_t)ly_pcol(l)))
		ly_endline(l);
	else if (l->started)
		l->started = 0;
	while (l->blank < margin && l->lines.n) {
		ly_start(l);
		ly_endline(l);
	}
	l->space = 0;
}

/* Push a prefix segment for the lines of a block. */
ly_seg *ly_push(ly *l, const char *t, char sty)
{
	ly_seg *g = xm(sizeof *g);
	size_t i;
	unsigned c;

	memset(g, 0, sizeof *g);
	s_init(&g->t);
	s_init(&g->s);
	s_init(&g->mark);
	s_init(&g->marks);
	s_cat(&g->t, t);
	for (i = 0; t[i]; i += (size_t)hl_utf8(t + i, strlen(t + i), &c))
		s_ch(&g->s, sty);
	g->w = ly_w(t, strlen(t));
	v_add(&l->segs, g);
	return g;
}

/* Pop the last prefix segment. */
void ly_pop(ly *l)
{
	ly_seg *g;

	if (!l->segs.n)
		return;
	g = l->segs.p[--l->segs.n];
	s_free(&g->t);
	s_free(&g->s);
	s_free(&g->mark);
	s_free(&g->marks);
	free(g);
}

/* Whether an element's own attributes hide it: hidden, display:none,
   visibility:hidden, or Outlook's mso-hide:all. */
int ly_hidden(hl_n *e)
{
	const char *st = hl_attr_get(e, "style"), *p;
	str low;
	int h = 0;
	size_t i;

	if (hl_attr_get(e, "hidden"))
		return 1;
	if (!st)
		return 0;
	s_init(&low);
	for (i = 0; st[i]; i++)
		if (st[i] != ' ' && st[i] != '\t' && st[i] != '\n')
			s_ch(&low, st[i] >= 'A' && st[i] <= 'Z' ? st[i] + 32 : st[i]);
	p = low.p ? low.p : "";
	if (strstr(p, "display:none") || strstr(p, "visibility:hidden") ||
	    strstr(p, "mso-hide:all") || strstr(p, "max-height:0") ||
	    (strstr(p, "font-size:0") && !strstr(p, "font-size:0.")))
		h = 1;
	s_free(&low);
	return h;
}

/* A number from an attribute, or a default. */
int ly_num(hl_n *e, const char *nm, int dflt)
{
	const char *v = hl_attr_get(e, nm);

	return v && *v >= '0' && *v <= '9' ? atoi(v) : dflt;
}

/* Lay out an element's children. */
void ly_kids(ly *l, hl_n *e)
{
	hl_n *k;

	for (k = e->kid; k; k = k->nx)
		ly_node(l, k);
}

/* The text of a node, whitespace collapsed, for a cell's natural width. */
void ly_flat(hl_n *n, str *o)
{
	hl_n *k;

	if (n->t == HL_TEXT)
		s_add(o, n->s.p ? n->s.p : "", n->s.n);
	for (k = n->kid; k; k = k->nx)
		if (k->t != HL_ELEM || !ly_skip(k))
			ly_flat(k, o);
}

/* Elements whose content is never shown: what belongs to the head, and
   SVG, which is pictures. */
int ly_skip(hl_n *e)
{
	if (e->ns == HL_NSSVG)
		return 1;
	return e->ns == HL_NSHTML &&
	       tr_one(e->tag, "head script style template title meta link base noframes param "
		      "source track");
}

/* Whether a table holds data rather than laying a page out: it has
   header cells or a border, no table inside it, and not too many cells. */
int ly_datatable(hl_n *t)
{
	vec st = { 0, 0, 0 };
	int th = ly_num(t, "border", 0) > 0, cells = 0, nested = 0;
	hl_n *n;

	v_add(&st, t);
	while (st.n && !nested) {
		n = st.p[--st.n];
		for (n = n->kid; n; n = n->nx) {
			if (n->t != HL_ELEM)
				continue;
			if (hl_isel(n, "table"))
				nested = 1;
			if (hl_isel(n, "th"))
				th = 1;
			if (hl_isel(n, "td") || hl_isel(n, "th"))
				cells++;
			v_add(&st, n);
		}
	}
	v_free(&st);
	return !nested && th && cells > 0 && cells <= 400;
}

/* The rows of a table, in order, through thead, tbody and tfoot. */
void ly_rows(hl_n *t, vec *rows)
{
	hl_n *k, *r;

	for (k = t->kid; k; k = k->nx) {
		if (hl_isel(k, "tr"))
			v_add(rows, k);
		else if (hl_isel(k, "thead") || hl_isel(k, "tbody") || hl_isel(k, "tfoot"))
			for (r = k->kid; r; r = r->nx)
				if (hl_isel(r, "tr"))
					v_add(rows, r);
	}
}

/* Lay out one cell's content at a width, into a layout of its own. */
void ly_cell(ly *outer, hl_n *c, int w, ly *l)
{
	ly_init(l, w, outer->flags & ~LY_IMAGES);
	l->depth = outer->depth + 1;
	if (hl_isel(c, "th"))
		l->b = 1;
	ly_kids(l, c);
	ly_block(l, 0);
	while (l->lines.n && !((ly_line *)l->lines.p[l->lines.n - 1])->t.n) {
		ly_line *x = l->lines.p[--l->lines.n];

		s_free(&x->t);
		s_free(&x->s);
		s_free(&x->a);
		s_free(&x->img);
		free(x);
	}
}

/* A data table as a grid: columns as wide as their content allows, cells
   wrapped within them, two spaces between, a rule under a header row. */
void ly_grid(ly *l, hl_n *t)
{
	vec rows = { 0, 0, 0 };
	int ncol = 0, *nat, *wid, total, avail, i, j, r, c, line, hdr;
	hl_n *cell;

	ly_rows(t, &rows);
	for (r = 0; r < (int)rows.n; r++) {
		c = 0;
		for (cell = ((hl_n *)rows.p[r])->kid; cell; cell = cell->nx)
			if (hl_isel(cell, "td") || hl_isel(cell, "th"))
				c++;
		if (c > ncol)
			ncol = c;
	}
	if (!ncol) {
		v_free(&rows);
		return;
	}
	nat = xm(sizeof(int) * (size_t)ncol);
	wid = xm(sizeof(int) * (size_t)ncol);
	memset(nat, 0, sizeof(int) * (size_t)ncol);
	for (r = 0; r < (int)rows.n; r++) {
		c = 0;
		for (cell = ((hl_n *)rows.p[r])->kid; cell; cell = cell->nx) {
			if (!hl_isel(cell, "td") && !hl_isel(cell, "th"))
				continue;
			{
				ly sub;
				size_t q;

				ly_cell(l, cell, 4000, &sub);
				for (q = 0; q < sub.lines.n; q++)
					if (((ly_line *)sub.lines.p[q])->w > nat[c])
						nat[c] = ((ly_line *)sub.lines.p[q])->w;
				ly_free(&sub);
			}
			c++;
		}
	}
	avail = l->width - ly_pcol(l) - 2 * (ncol - 1);
	total = 0;
	for (i = 0; i < ncol; i++) {
		if (nat[i] < 1)
			nat[i] = 1;
		total += nat[i];
		wid[i] = nat[i];
	}
	while (total > avail && avail > ncol) {
		j = 0;
		for (i = 1; i < ncol; i++)
			if (wid[i] > wid[j])
				j = i;
		if (wid[j] <= 3)
			break;
		wid[j]--;
		total--;
	}
	ly_block(l, 1);
	for (r = 0; r < (int)rows.n; r++) {
		vec cells = { 0, 0, 0 };
		int rowh = 0;

		hdr = 1;
		c = 0;
		for (cell = ((hl_n *)rows.p[r])->kid; cell; cell = cell->nx) {
			ly *sub;

			if (!hl_isel(cell, "td") && !hl_isel(cell, "th"))
				continue;
			if (!hl_isel(cell, "th"))
				hdr = 0;
			sub = xm(sizeof *sub);
			ly_cell(l, cell, wid[c] < 1 ? 1 : wid[c], sub);
			if ((int)sub->lines.n > rowh)
				rowh = (int)sub->lines.n;
			v_add(&cells, sub);
			c++;
		}
		for (line = 0; line < rowh; line++) {
			ly_start(l);
			for (c = 0; c < (int)cells.n; c++) {
				ly *sub = cells.p[c];
				int used = 0;

				if (c)
					for (j = 0; j < 2; j++)
						ly_put(l, " ", 1, 'p', -1, 1);
				if (line < (int)sub->lines.n) {
					ly_line *x = sub->lines.p[line];
					size_t q = 0, si = 0;
					unsigned ch;
					int k;

					while (q < x->t.n) {
						k = hl_utf8(x->t.p + q, x->t.n - q, &ch);
						ly_put(l, x->t.p + q, k, x->s.p[si], -1, u8w(ch));
						used += u8w(ch);
						q += (size_t)k;
						si++;
					}
				}
				if (c + 1 < (int)cells.n)
					for (; used < wid[c]; used++)
						ly_put(l, " ", 1, 'p', -1, 1);
			}
			ly_endline(l);
		}
		for (c = 0; c < (int)cells.n; c++) {
			ly_free(cells.p[c]);
			free(cells.p[c]);
		}
		v_free(&cells);
		if (hdr && r + 1 < (int)rows.n) {
			int tw = 2 * (ncol - 1);

			for (i = 0; i < ncol; i++)
				tw += wid[i];
			ly_start(l);
			for (i = 0; i < tw; i++)
				ly_put(l, l->flags & LY_ASCII ? "-" : "\xe2\x94\x80",
				       l->flags & LY_ASCII ? 1 : 3, 'm', -1, 1);
			ly_endline(l);
		}
	}
	ly_block(l, 1);
	free(nat);
	free(wid);
	v_free(&rows);
}

/* Whether a cell holds only inline content, so short cells of a layout
   row can share a line. */
int ly_inline(ly *l, hl_n *n)
{
	hl_n *k;

	for (k = n->kid; k; k = k->nx) {
		if (k->t != HL_ELEM)
			continue;
		if (ly_skip(k))
			continue;
		if (tr_isin(k, "p div table ul ol li h1 h2 h3 h4 h5 h6 blockquote pre hr center br "
			    "section article header footer form dl"))
			return 0;
		if (hl_isel(k, "img") && (l->flags & LY_IMAGES))
			return 0;
		if (!ly_inline(l, k))
			return 0;
	}
	return 1;
}

/* A layout table, linearised: a row's cells in turn, short inline ones
   sharing a line two spaces apart, the rest as blocks one after another. */
void ly_linear(ly *l, hl_n *t)
{
	vec rows = { 0, 0, 0 };
	size_t r;
	hl_n *cell;
	int allinl, first;

	ly_rows(t, &rows);
	for (r = 0; r < rows.n; r++) {
		allinl = 1;
		for (cell = ((hl_n *)rows.p[r])->kid; cell; cell = cell->nx)
			if ((hl_isel(cell, "td") || hl_isel(cell, "th")) && !ly_hidden(cell) &&
			    !ly_inline(l, cell))
				allinl = 0;
		ly_block(l, 0);
		first = 1;
		for (cell = ((hl_n *)rows.p[r])->kid; cell; cell = cell->nx) {
			if (!hl_isel(cell, "td") && !hl_isel(cell, "th"))
				continue;
			if (ly_hidden(cell))
				continue;
			if (allinl) {
				ly_word(l);
				if (!first && l->started && l->col > ly_pcol(l)) {
					ly_put(l, " ", 1, 'p', -1, 1);
					l->space = 1;
				}
				ly_kids(l, cell);
				first = 0;
			} else {
				ly_block(l, 0);
				ly_kids(l, cell);
				ly_block(l, 0);
			}
		}
		ly_block(l, 0);
	}
	v_free(&rows);
}

/* An image: a box reserved for the app to draw it in when images are
   wanted and it is not a tracking pixel, else its description. */
void ly_img(ly *l, hl_n *e)
{
	const char *src = hl_attr_get(e, "src"), *alt = hl_attr_get(e, "alt");
	int w = ly_num(e, "width", 0), h = ly_num(e, "height", 0), cols, rows, i;
	ly_line *x;

	if ((w && w <= 3) || (h && h <= 3))
		return;
	if ((l->flags & LY_IMAGES) && src && *src) {
		cols = w ? w / 8 : 32;
		rows = h ? h / 16 : 8;
		if (cols > l->width - ly_pcol(l))
			cols = l->width - ly_pcol(l);
		if (cols < 4)
			cols = 4;
		if (w && h)
			rows = (int)((long)cols * h / w / 2);
		if (rows < 2)
			rows = 2;
		if (rows > 20)
			rows = 20;
		ly_block(l, 0);
		for (i = 0; i < rows; i++) {
			ly_start(l);
			ly_endline(l);
			if (i == 0) {
				x = l->lines.p[l->lines.n - 1];
				s_num(&x->img, rows);
				s_ch(&x->img, '\t');
				s_num(&x->img, cols);
				s_ch(&x->img, '\t');
				s_cat(&x->img, src);
				s_ch(&x->img, '\t');
				s_cat(&x->img, alt ? alt : "");
			}
		}
		l->blank = 0;
		return;
	}
	if (alt && *alt) {
		ly_text(l, "[", 1);
		ly_text(l, alt, strlen(alt));
		ly_text(l, "]", 1);
	}
}

/* Lay out one node and what is under it. */
void ly_node(ly *l, hl_n *n)
{
	int sb, si, su, ss, sc, sh, sq, slk, spre, shide;
	const char *tag, *v;
	size_t nseg;

	if (n->t == HL_TEXT) {
		ly_text(l, n->s.p ? n->s.p : "", n->s.n);
		return;
	}
	if (n->t != HL_ELEM && n->t != HL_DOC && n->t != HL_FRAG)
		return;
	if (n->t != HL_ELEM) {
		ly_kids(l, n);
		return;
	}
	if (++l->depth > HL_DEPTH + 64) {
		l->depth--;
		return;
	}
	if (ly_skip(n) || ly_hidden(n)) {
		l->depth--;
		return;
	}
	tag = n->tag;
	sb = l->b;
	si = l->it;
	su = l->u;
	ss = l->st;
	sc = l->code;
	sh = l->head;
	sq = l->quote;
	slk = l->link;
	spre = l->pre;
	shide = l->hide;
	nseg = l->segs.n;
	if ((v = hl_attr_get(n, "style"))) {
		str low;
		size_t i;

		s_init(&low);
		for (i = 0; v[i]; i++)
			if (v[i] != ' ')
				s_ch(&low, v[i] >= 'A' && v[i] <= 'Z' ? v[i] + 32 : v[i]);
		if (low.p) {
			if (strstr(low.p, "font-weight:bold") || strstr(low.p, "font-weight:700") ||
			    strstr(low.p, "font-weight:800") || strstr(low.p, "font-weight:900") ||
			    strstr(low.p, "font-weight:600"))
				l->b = 1;
			if (strstr(low.p, "font-style:italic"))
				l->it = 1;
			if (strstr(low.p, "text-decoration:underline"))
				l->u = 1;
			if (strstr(low.p, "text-decoration:line-through"))
				l->st = 1;
		}
		s_free(&low);
	}
	if (!strcmp(tag, "br")) {
		ly_word(l);
		if (!l->started)
			ly_start(l);
		ly_endline(l);
	} else if (!strcmp(tag, "hr")) {
		int i, w;

		ly_block(l, 1);
		w = l->width - ly_pcol(l);
		ly_start(l);
		for (i = 0; i < w; i++)
			ly_put(l, l->flags & LY_ASCII ? "-" : "\xe2\x94\x80", l->flags & LY_ASCII ? 1 : 3,
			       'h', -1, 1);
		ly_endline(l);
		ly_block(l, 1);
	} else if (!strcmp(tag, "img")) {
		ly_img(l, n);
	} else if (!strcmp(tag, "table")) {
		if (ly_datatable(n))
			ly_grid(l, n);
		else
			ly_linear(l, n);
	} else if (!strcmp(tag, "a")) {
		v = hl_attr_get(n, "href");
		if (v && *v && strncasecmp(v, "javascript:", 11)) {
			l->link = (int)l->links.n;
			v_add(&l->links, xs(v));
		}
		ly_kids(l, n);
	} else if (!strcmp(tag, "b") || !strcmp(tag, "strong")) {
		l->b = 1;
		ly_kids(l, n);
	} else if (!strcmp(tag, "i") || !strcmp(tag, "em") || !strcmp(tag, "cite") ||
		   !strcmp(tag, "var") || !strcmp(tag, "dfn")) {
		l->it = 1;
		ly_kids(l, n);
	} else if (!strcmp(tag, "u") || !strcmp(tag, "ins")) {
		l->u = 1;
		ly_kids(l, n);
	} else if (!strcmp(tag, "s") || !strcmp(tag, "strike") || !strcmp(tag, "del")) {
		l->st = 1;
		ly_kids(l, n);
	} else if (!strcmp(tag, "code") || !strcmp(tag, "kbd") || !strcmp(tag, "samp") ||
		   !strcmp(tag, "tt")) {
		l->code = 1;
		ly_kids(l, n);
	} else if (!strcmp(tag, "q")) {
		ly_text(l, "\xe2\x80\x9c", 3);
		ly_kids(l, n);
		ly_text(l, "\xe2\x80\x9d", 3);
	} else if (tag[0] == 'h' && tag[1] >= '1' && tag[1] <= '6' && !tag[2]) {
		ly_block(l, 1);
		l->head = tag[1] - '0';
		ly_kids(l, n);
		ly_block(l, 1);
	} else if (!strcmp(tag, "p")) {
		ly_block(l, 1);
		ly_kids(l, n);
		ly_block(l, 1);
	} else if (!strcmp(tag, "pre") || !strcmp(tag, "listing") || !strcmp(tag, "xmp") ||
		   !strcmp(tag, "plaintext") || !strcmp(tag, "textarea")) {
		ly_block(l, 1);
		l->pre = 1;
		l->code = 1;
		ly_kids(l, n);
		ly_word(l);
		l->pre = spre;
		ly_block(l, 1);
	} else if (!strcmp(tag, "blockquote")) {
		ly_block(l, 1);
		ly_push(l, l->flags & LY_ASCII ? "| " : "\xe2\x94\x82 ", 'm');
		l->quote = 1;
		ly_kids(l, n);
		ly_block(l, 0);
		ly_pop(l);
		ly_block(l, 1);
	} else if (!strcmp(tag, "ul") || !strcmp(tag, "ol") || !strcmp(tag, "menu") ||
		   !strcmp(tag, "dir")) {
		long ctr = ly_num(n, "start", 1);

		ly_block(l, l->lists.n ? 0 : 1);
		v_add(&l->lists, (void *)(long)(tag[0] == 'o' ? ctr : -1));
		ly_kids(l, n);
		l->lists.n--;
		ly_block(l, l->lists.n ? 0 : 1);
	} else if (!strcmp(tag, "li")) {
		long ctr = l->lists.n ? (long)l->lists.p[l->lists.n - 1] : -1;
		str mk;
		ly_seg *g;
		size_t i;
		unsigned uc;

		ly_block(l, 0);
		s_init(&mk);
		if (ctr >= 0) {
			ctr = ly_num(n, "value", (int)ctr);
			s_num(&mk, ctr);
			s_cat(&mk, ". ");
			l->lists.p[l->lists.n - 1] = (void *)(ctr + 1);
		} else {
			s_cat(&mk, l->flags & LY_ASCII ? "- " : "\xe2\x80\xa2 ");
		}
		{
			str ind;
			int w = ly_w(mk.p, mk.n);

			s_init(&ind);
			for (i = 0; i < (size_t)w; i++)
				s_ch(&ind, ' ');
			g = ly_push(l, ind.p, 'p');
			s_free(&ind);
		}
		s_cat(&g->mark, mk.p);
		for (i = 0; i < mk.n; i += (size_t)hl_utf8(mk.p + i, mk.n - i, &uc))
			s_ch(&g->marks, 'm');
		g->usemark = 1;
		s_free(&mk);
		ly_kids(l, n);
		ly_block(l, 0);
		ly_pop(l);
	} else if (!strcmp(tag, "dd")) {
		ly_block(l, 0);
		ly_push(l, "    ", 'p');
		ly_kids(l, n);
		ly_block(l, 0);
		ly_pop(l);
	} else if (!strcmp(tag, "dt")) {
		ly_block(l, 0);
		l->b = 1;
		ly_kids(l, n);
		ly_block(l, 0);
	} else if (!strcmp(tag, "select")) {
		hl_n *first = 0, *sel = 0;

		hl_selopt(n, &first, &sel);
		if (!sel)
			sel = first;
		if (sel) {
			ly_text(l, "[", 1);
			ly_kids(l, sel);
			ly_text(l, " \xe2\x96\xbe]", 5);
		}
	} else if (!strcmp(tag, "input")) {
		v = hl_attr_get(n, "type");
		if (!v || !strcasecmp(v, "text") || !strcasecmp(v, "submit") || !strcasecmp(v, "button") ||
		    !strcasecmp(v, "email") || !strcasecmp(v, "search")) {
			const char *val = hl_attr_get(n, "value");

			if (!val)
				val = hl_attr_get(n, "placeholder");
			ly_text(l, "[", 1);
			if (val)
				ly_text(l, val, strlen(val));
			ly_text(l, "]", 1);
		}
	} else if (!strcmp(tag, "button")) {
		ly_text(l, "[", 1);
		ly_kids(l, n);
		ly_text(l, "]", 1);
	} else if (tr_isin(n, "div section article header footer nav main aside address center "
			   "form fieldset figure figcaption details summary caption legend dl tr body "
			   "html")) {
		ly_block(l, 0);
		ly_kids(l, n);
		ly_block(l, 0);
	} else {
		ly_kids(l, n);
	}
	while (l->segs.n > nseg)
		ly_pop(l);
	l->b = sb;
	l->it = si;
	l->u = su;
	l->st = ss;
	l->code = sc;
	l->head = sh;
	l->quote = sq;
	l->link = slk;
	l->pre = spre;
	l->hide = shide;
	l->depth--;
}

/* Lay a document or an element out at a width, into $RET when bound --
   r[i]["t"] the text, ["s"] a style letter a character, ["a"] its links
   and ["img"] an image box starting there -- else printed as plain text.
   Flags: images given boxes, ASCII in place of box-drawing. */
int hl_lines(sh *s, hl_n *root, int width, int flags)
{
	ly l;
	size_t i;
	str all, k;
	char *ks[2];

	ly_init(&l, width, flags);
	ly_node(&l, root);
	ly_block(&l, 0);
	while (l.lines.n && !((ly_line *)l.lines.p[l.lines.n - 1])->t.n &&
	       !((ly_line *)l.lines.p[l.lines.n - 1])->img.n) {
		ly_line *x = l.lines.p[--l.lines.n];

		s_free(&x->t);
		s_free(&x->s);
		s_free(&x->a);
		s_free(&x->img);
		free(x);
	}
	s_init(&all);
	s_init(&k);
	for (i = 0; i < l.lines.n; i++) {
		ly_line *x = l.lines.p[i];

		if (!s->bind) {
			s_add(&all, x->t.p ? x->t.p : "", x->t.n);
			s_ch(&all, '\n');
			continue;
		}
		k.n = 0;
		s_num(&k, (long)i);
		ks[0] = k.p;
		ks[1] = "t";
		hibr_setp(s, "RET", ks, 2, x->t.p ? x->t.p : "");
		ks[1] = "s";
		hibr_setp(s, "RET", ks, 2, x->s.p ? x->s.p : "");
		if (x->a.n) {
			x->a.p[--x->a.n] = 0;
			ks[1] = "a";
			hibr_setp(s, "RET", ks, 2, x->a.p);
		}
		if (x->img.n) {
			ks[1] = "img";
			hibr_setp(s, "RET", ks, 2, x->img.p);
		}
	}
	if (!s->bind && all.n) {
		fflush(stdout);
		if (write(1, all.p, all.n) < 0)
			lg(HIBR_LDBG, "html: could not write the lines");
	}
	s_free(&all);
	s_free(&k);
	ly_free(&l);
	return HIBR_OK;
}
