#include "mm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Everything on a stream, which is how a file and standard input are the
   same thing to the rest of this. */
int mm_slurp(FILE *f, str *o)
{
	char buf[HIBR_IOCH];
	size_t n;

	clearerr(f);
	while ((n = fread(buf, 1, sizeof buf, f)) > 0)
		s_add(o, buf, n);
	clearerr(f);
	return 0;
}

/* One row of a drawing as text, and the style of every character in it as
   runs -- `b:3 t:9 b:3`, a letter and how many characters it covers, the
   whole row covered. The same shape mods/sysinfo answers in, so a window
   draws either with one piece of code and never parses an escape.
   Counted in characters, which is what a shell slice counts; a glyph two
   columns wide takes one of them and the cell beside it is skipped. */
void mm_row(mm_grid *g, int y, str *text, str *runs)
{
	int x = 0, w;
	unsigned cp;
	char last = 0;
	long run = 0;
	str st;

	s_init(&st);
	text->n = 0;
	if (text->p)
		text->p[0] = 0;
	runs->n = 0;
	if (runs->p)
		runs->p[0] = 0;
	while (x < g->w) {
		cp = g->cp[y * g->w + x];
		mm_u8(text, cp);
		s_ch(&st, g->sty[y * g->w + x]);
		w = u8w(cp);
		x += w < 1 ? 1 : w;
	}
	/* A trailing blank is not worth drawing, and a blank is one byte and
	   one character, so the styles lose exactly as many as the text. */
	while (text->n && text->p[text->n - 1] == ' ') {
		text->n--;
		st.n--;
	}
	if (text->p)
		text->p[text->n] = 0;
	for (x = 0; x < (int)st.n; x++) {
		if (st.p[x] == last) {
			run++;
			continue;
		}
		if (run) {
			if (runs->n)
				s_ch(runs, ' ');
			s_ch(runs, last);
			s_ch(runs, ':');
			s_num(runs, run);
		}
		last = st.p[x];
		run = 1;
	}
	if (run) {
		if (runs->n)
			s_ch(runs, ' ');
		s_ch(runs, last);
		s_ch(runs, ':');
		s_num(runs, run);
	}
	s_free(&st);
}

/* The graph itself, a line an item, for a script or a person reading it
   rather than a window drawing it. */
void mm_dump(mm_dia *d)
{
	size_t i;
	static const char *shp[] = { "rect", "round", "diamond", "circle" };
	static const char *sty[] = { "solid", "dotted", "thick" };
	static const char *arw[] = { "plain", "open", "cross", "ring" };

	printf("kind\t%s\n", d->kind == MM_FLOW ? "flowchart" :
	       d->kind == MM_SEQ ? "sequence" : "pie");
	if (d->kind == MM_FLOW)
		printf("dir\t%s\n", d->dir == MM_TD ? "TD" :
		       d->dir == MM_LR ? "LR" :
		       d->dir == MM_BT ? "BT" : "RL");
	if (d->title.n)
		printf("title\t%s\n", d->title.p);
	if (d->note.n)
		printf("note\t%s\n", d->note.p);
	for (i = 0; i < d->nodes.n; i++) {
		mm_node *n = (mm_node *)d->nodes.p[i];
		if (n->dummy)
			continue;
		/* The box it was laid out into comes with it, so a test can
		   ask whether two of them overlap without working the layout
		   out again -- which would only be checking the layout
		   against a second copy of itself. */
		printf("%s\t%s\t%s\t%d\t%d\t%d\t%d\t%s\n",
		       d->kind == MM_SEQ ? "part" : "node",
		       n->id.p ? n->id.p : "",
		       shp[n->shape], n->x, n->y, n->w, n->h,
		       n->label.p ? n->label.p : "");
	}
	for (i = 0; i < d->edges.n; i++) {
		mm_edge *e = (mm_edge *)d->edges.p[i];
		mm_node *a = (mm_node *)d->nodes.p[e->from];
		mm_node *b = (mm_node *)d->nodes.p[e->to];
		if (a->dummy || b->dummy)
			continue;
		printf("edge\t%s\t%s\t%s\t%s\t%s\n", a->id.p ? a->id.p : "",
		       b->id.p ? b->id.p : "", sty[e->style],
		       e->arrow < 0 ? "none" : arw[e->arrow],
		       e->label.p ? e->label.p : "");
	}
	for (i = 0; i < d->msgs.n; i++) {
		mm_msg *m = (mm_msg *)d->msgs.p[i];
		const char *a = m->from >= 0 ?
			((mm_node *)d->nodes.p[m->from])->id.p : "";
		const char *b = m->to >= 0 ?
			((mm_node *)d->nodes.p[m->to])->id.p : "";
		if (m->note)
			printf("note\t%s\t%s\t%s\t%s\n",
			       m->note == MM_NOTE_OVER ? "over" :
			       m->note == MM_NOTE_LEFT ? "left" : "right",
			       a ? a : "", b ? b : "",
			       m->text.p ? m->text.p : "");
		else
			printf("msg\t%s\t%s\t%s\t%s\t%s\n", a ? a : "",
			       b ? b : "", sty[m->style], arw[m->arrow],
			       m->text.p ? m->text.p : "");
	}
	for (i = 0; i < d->slices.n; i++) {
		mm_slice *s = (mm_slice *)d->slices.p[i];
		printf("slice\t%s\t%g\n", s->label.p ? s->label.p : "", s->v);
	}
}

/* Put a field in the result slot. */
void mm_set(sh *s, const char *a, const char *b, const char *v)
{
	char *ks[2];

	ks[0] = (char *)a;
	ks[1] = (char *)b;
	hibr_setp(s, "RET", ks, b ? 2 : 1, v);
}

/* mermaid parse|render: a diagram's graph, or its drawing. */
int m_mermaid(sh *s, int ac, char **av)
{
	int i = 2, ascii = 0, wide = 40, rc = HIBR_OK, render;
	const char *path = 0, *text = 0;
	str in, row, runs, num;
	mm_dia *d;
	mm_grid *g = 0;
	FILE *f;

	if (ac < 2 || (strcmp(av[1], "parse") && strcmp(av[1], "render"))) {
		lg(HIBR_LERR, "mermaid: parse|render [-a] [-w n] "
		   "[-t text | file]");
		return 2;
	}
	render = !strcmp(av[1], "render");
	for (; i < ac; i++) {
		if (!strcmp(av[i], "-a")) {
			ascii = 1;
		} else if (!strcmp(av[i], "-w") && i + 1 < ac) {
			wide = atoi(av[++i]);
		} else if (!strcmp(av[i], "-t") && i + 1 < ac) {
			text = av[++i];
		} else if (av[i][0] == '-' && av[i][1] && strcmp(av[i], "-")) {
			lg(HIBR_LERR, "mermaid: no option %s", av[i]);
			return 2;
		} else {
			path = av[i];
		}
	}
	s_init(&in);
	if (text) {
		s_cat(&in, text);
	} else if (path && strcmp(path, "-")) {
		f = fopen(path, "r");
		if (!f) {
			lg(HIBR_LERR, "mermaid: %s: cannot read it", path);
			s_free(&in);
			return HIBR_FAIL;
		}
		mm_slurp(f, &in);
		fclose(f);
	} else {
		mm_slurp(stdin, &in);
	}
	d = mm_parse(in.p ? in.p : "");
	s_free(&in);
	if (d->kind == MM_NONE) {
		if (s->bind) {
			mm_set(s, "kind", 0, "");
			mm_set(s, "why", 0, d->why.p ? d->why.p : "");
			mm_free(d);
			return HIBR_OK;
		}
		lg(HIBR_LERR, "mermaid: %s", d->why.p ? d->why.p : "no");
		mm_free(d);
		return HIBR_FAIL;
	}
	mm_lay(d);
	if (!render) {
		if (s->bind) {
			lg(HIBR_LERR, "mermaid: parse prints; render is the "
			   "one that fills a result slot");
			rc = HIBR_FAIL;
		} else {
			mm_dump(d);
		}
		mm_free(d);
		return rc;
	}
	g = mm_render(d, ascii, wide);
	s_init(&row);
	s_init(&runs);
	s_init(&num);
	if (s->bind) {
		mm_set(s, "kind", 0, d->kind == MM_FLOW ? "flowchart" :
		       d->kind == MM_SEQ ? "sequence" : "pie");
		mm_set(s, "why", 0, "");
		mm_set(s, "note", 0, d->note.p ? d->note.p : "");
		mm_set(s, "title", 0, d->title.p ? d->title.p : "");
		/* From the grid, not from the layout: the layout's own height
		   is what it needed for the boxes, and anything that grows
		   the drawing afterwards -- room made for a label -- is only
		   in the grid. A window told the layout's number would scroll
		   off the end of the drawing. */
		num.n = 0;
		s_num(&num, (long)g->w);
		mm_set(s, "w", 0, num.p);
		num.n = 0;
		if (num.p)
			num.p[0] = 0;
		s_num(&num, (long)g->h);
		mm_set(s, "h", 0, num.p);
	}
	for (i = 0; i < g->h; i++) {
		mm_row(g, i, &row, &runs);
		if (s->bind) {
			num.n = 0;
			if (num.p)
				num.p[0] = 0;
			s_num(&num, (long)i);
			mm_set(s, "text", num.p, row.p ? row.p : "");
			mm_set(s, "runs", num.p, runs.p ? runs.p : "");
		} else {
			printf("%s\n", row.p ? row.p : "");
		}
	}
	if (!s->bind && d->note.n)
		lg(HIBR_LWRN, "mermaid: %s", d->note.p);
	s_free(&row);
	s_free(&runs);
	s_free(&num);
	mm_gfree(g);
	mm_free(d);
	return rc;
}

const hibr_bi mermaid_bi[] = {
	{ "mermaid", m_mermaid, "a Mermaid diagram as cells" },
	HIBR_BI_END
};

HIBR_MODULE("mermaid", "0.1", "Mermaid diagrams: flowcharts, sequences, pies",
	    mermaid_bi, 0, 0);
