#include "mm.h"
#include <stdlib.h>
#include <string.h>

#define MM_GAP 3

/* How many columns a string takes, which is what a box has to be wide
   enough for -- not how many characters it holds. */
size_t mm_width(const char *p)
{
	size_t n = p ? strlen(p) : 0, i = 0, w = 0;
	unsigned cp;
	int l;

	while (i < n) {
		l = u8dec(p + i, n - i, &cp);
		if (l < 1)
			l = 1;
		w += (size_t)u8w(cp);
		i += (size_t)l;
	}
	return w;
}

/* Reverse the edges that close a cycle, so what is left is a graph that
   can be ranked. Which edges those are depends on where the walk starts,
   as it does in dagre; the drawing is the same shape either way. */
void mm_cycles(mm_dia *d)
{
	size_t i, j;
	int *state = xm(d->nodes.n * sizeof *state);
	vec stack;
	mm_edge *e;

	memset(state, 0, d->nodes.n * sizeof *state);
	stack.p = 0;
	stack.n = stack.cap = 0;
	for (i = 0; i < d->nodes.n; i++) {
		if (state[i])
			continue;
		v_add(&stack, (void *)(long)i);
		while (stack.n) {
			long v = (long)stack.p[stack.n - 1];
			if (state[v] == 0) {
				state[v] = 1;
				for (j = 0; j < d->edges.n; j++) {
					e = (mm_edge *)d->edges.p[j];
					if (e->from != (int)v)
						continue;
					if (state[e->to] == 1) {
						int t = e->from;
						e->from = e->to;
						e->to = t;
						e->rev = 1;
					} else if (!state[e->to]) {
						v_add(&stack,
						      (void *)(long)e->to);
					}
				}
			} else {
				state[v] = 2;
				stack.n--;
			}
		}
	}
	v_free(&stack);
	free(state);
}

/* Rank every node by the longest path to it, which is the layering dagre
   calls the simplest honest one: a node sits one rank below the lowest of
   everything that points at it. Answers how many ranks there are. */
int mm_ranks(mm_dia *d)
{
	size_t i, j;
	int moved = 1, hi = 0, pass = 0;
	mm_edge *e;
	mm_node *n;

	for (i = 0; i < d->nodes.n; i++)
		((mm_node *)d->nodes.p[i])->rank = 0;
	while (moved && pass++ <= (int)d->nodes.n + 1) {
		moved = 0;
		for (j = 0; j < d->edges.n; j++) {
			e = (mm_edge *)d->edges.p[j];
			n = (mm_node *)d->nodes.p[e->to];
			if (n->rank <= ((mm_node *)d->nodes.p[e->from])->rank) {
				n->rank = ((mm_node *)
					   d->nodes.p[e->from])->rank + 1;
				moved = 1;
			}
		}
	}
	for (i = 0; i < d->nodes.n; i++) {
		n = (mm_node *)d->nodes.p[i];
		if (n->rank > hi)
			hi = n->rank;
	}
	return hi + 1;
}

/* Split an edge that spans more than one rank into a chain through a bend
   point on each rank between -- Sugiyama's dummy nodes. Everything after
   this can assume an edge joins neighbouring ranks, which is what makes
   both the ordering and the routing simple. The label stays on the first
   segment and the arrowhead goes on the last, which is where each of them
   belongs when the chain is drawn. */
void mm_dummies(mm_dia *d)
{
	size_t j, was = d->edges.n;
	int k, r0, r1, tgt, prev, cur;
	mm_edge *e, *s;
	mm_node *n;

	for (j = 0; j < d->edges.n; j++) {
		e = (mm_edge *)d->edges.p[j];
		e->first = 1;
		e->last = 1;
	}
	for (j = 0; j < was; j++) {
		e = (mm_edge *)d->edges.p[j];
		tgt = e->to;
		r0 = ((mm_node *)d->nodes.p[e->from])->rank;
		r1 = ((mm_node *)d->nodes.p[tgt])->rank;
		if (r1 - r0 <= 1)
			continue;
		prev = -1;
		for (k = r0 + 1; k < r1; k++) {
			n = xm(sizeof *n);
			memset(n, 0, sizeof *n);
			s_init(&n->id);
			s_init(&n->label);
			n->dummy = 1;
			n->rank = k;
			v_add(&d->nodes, n);
			cur = (int)d->nodes.n - 1;
			if (prev < 0) {
				e->to = cur;
				e->last = 0;
			} else {
				s = xm(sizeof *s);
				memset(s, 0, sizeof *s);
				s_init(&s->label);
				s->from = prev;
				s->to = cur;
				s->style = e->style;
				s->arrow = e->arrow;
				s->rev = e->rev;
				s->first = 0;
				s->last = 0;
				v_add(&d->edges, s);
			}
			prev = cur;
		}
		s = xm(sizeof *s);
		memset(s, 0, sizeof *s);
		s_init(&s->label);
		s->from = prev;
		s->to = tgt;
		s->style = e->style;
		s->arrow = e->arrow;
		s->rev = e->rev;
		s->first = 0;
		s->last = 1;
		v_add(&d->edges, s);
	}
}

/* Order the nodes within each rank so that fewer edges cross: the median
   heuristic, four passes down and up, which is what dagre's default does.
   Exact crossing minimisation is NP-hard and nobody needs it here. */
void mm_order(mm_dia *d)
{
	size_t i, j, k;
	int r, ranks = 0, pass;
	mm_node *n, *m;
	mm_edge *e;
	int *pos, *med, *cnt;

	for (i = 0; i < d->nodes.n; i++) {
		n = (mm_node *)d->nodes.p[i];
		if (n->rank + 1 > ranks)
			ranks = n->rank + 1;
	}
	for (r = 0; r < ranks; r++) {
		k = 0;
		for (i = 0; i < d->nodes.n; i++) {
			n = (mm_node *)d->nodes.p[i];
			if (n->rank == r)
				n->order = (int)k++;
		}
	}
	pos = xm(d->nodes.n * sizeof *pos);
	med = xm(d->nodes.n * sizeof *med);
	cnt = xm(d->nodes.n * sizeof *cnt);
	for (pass = 0; pass < 4; pass++) {
		int down = !(pass & 1);
		for (r = down ? 1 : ranks - 2; down ? r < ranks : r >= 0;
		     r += down ? 1 : -1) {
			for (i = 0; i < d->nodes.n; i++) {
				med[i] = -1;
				cnt[i] = 0;
				pos[i] = ((mm_node *)d->nodes.p[i])->order;
			}
			for (j = 0; j < d->edges.n; j++) {
				int a, b;
				e = (mm_edge *)d->edges.p[j];
				a = down ? e->to : e->from;
				b = down ? e->from : e->to;
				if (((mm_node *)d->nodes.p[a])->rank != r)
					continue;
				med[a] = (med[a] < 0 ? 0 : med[a]) + pos[b];
				cnt[a]++;
			}
			/* Every node in the rank is keyed in the same space,
			   or one with nothing in the next rank to be pulled
			   towards -- keeping its small 0..k-1 order -- would
			   sort in front of every node that has one. */
			for (i = 0; i < d->nodes.n; i++) {
				n = (mm_node *)d->nodes.p[i];
				if (n->rank != r)
					continue;
				n->order = cnt[i] ? med[i] * 64 / cnt[i] :
					pos[i] * 64;
			}
			/* Settle the new keys back into 0..k-1, keeping ties
			   in the order they were already in. */
			for (i = 0; i < d->nodes.n; i++) {
				n = (mm_node *)d->nodes.p[i];
				if (n->rank != r)
					continue;
				k = 0;
				for (j = 0; j < d->nodes.n; j++) {
					m = (mm_node *)d->nodes.p[j];
					if (m->rank != r || j == i)
						continue;
					if (m->order < n->order ||
					    (m->order == n->order && j < i))
						k++;
				}
				pos[i] = (int)k;
			}
			for (i = 0; i < d->nodes.n; i++) {
				n = (mm_node *)d->nodes.p[i];
				if (n->rank == r)
					n->order = pos[i];
			}
		}
	}
	free(pos);
	free(med);
	free(cnt);
}

/* How big each node's box is: wide enough for its label with a border and
   a space either side, three rows tall, and a bend point one cell. A
   diamond is given a column more each side, which is what makes it read
   as a decision rather than a rectangle. */
void mm_sizes(mm_dia *d)
{
	size_t i;
	mm_node *n;

	for (i = 0; i < d->nodes.n; i++) {
		n = (mm_node *)d->nodes.p[i];
		if (n->dummy) {
			n->w = 1;
			n->h = 1;
			continue;
		}
		n->w = (int)mm_width(n->label.p) + 4;
		if (n->shape == MM_DIAMOND || n->shape == MM_CIRCLE)
			n->w += 2;
		n->h = 3;
	}
}

/* Which way the ranks run: down the screen for TD and BT, across it for
   LR and RL. Everything the layout does is in two abstract axes -- along
   the ranks and across them -- and this is the only thing that says which
   is which, so there is one layout engine and not four. */
int mm_vert(mm_dia *d)
{
	return d->dir == MM_TD || d->dir == MM_BT;
}

/* How much of its rank a node takes across it. */
int mm_across(mm_dia *d, mm_node *n)
{
	return mm_vert(d) ? n->w : n->h;
}

/* How much it takes along the ranks. */
int mm_along(mm_dia *d, mm_node *n)
{
	return mm_vert(d) ? n->h : n->w;
}

/* How much room to leave between two neighbours in a rank. A bend point
   is one cell and needs no more than one cell beside it; giving it a whole
   box's worth pushed a long edge several cells off the straight line and
   drew it as a detour round everything, which is what this release's first
   LR diagram did. dagre keeps two separations for the same reason. */
int mm_gap(mm_node *a, mm_node *b)
{
	return (a && a->dummy) || (b && b->dummy) ? 1 : MM_GAP;
}

/* Where each node sits across its rank: packed in order, then each rank
   nudged so a parent sits over the middle of its children and a child
   under the middle of its parents, which is the priority method's cheap
   half. */
void mm_place(mm_dia *d, int ranks)
{
	size_t i, j;
	int r, pass, at, want, cnt, sum, lo, hi, wide = 0;
	mm_node *n, *m;
	mm_edge *e;

	for (r = 0; r < ranks; r++) {
		mm_node *prev = 0;
		at = 0;
		for (j = 0; j < d->nodes.n; j++)
			for (i = 0; i < d->nodes.n; i++) {
				n = (mm_node *)d->nodes.p[i];
				if (n->rank != r || n->order != (int)j)
					continue;
				if (prev)
					at += mm_gap(prev, n);
				n->pos = at;
				at += mm_across(d, n);
				prev = n;
			}
		if (at > wide)
			wide = at;
	}
	for (pass = 0; pass < 6; pass++) {
		int down = !(pass & 1);
		for (r = down ? 1 : ranks - 2; down ? r < ranks : r >= 0;
		     r += down ? 1 : -1) {
			for (i = 0; i < d->nodes.n; i++) {
				n = (mm_node *)d->nodes.p[i];
				if (n->rank != r)
					continue;
				sum = cnt = 0;
				for (j = 0; j < d->edges.n; j++) {
					e = (mm_edge *)d->edges.p[j];
					if (down && e->to == (int)i)
						m = (mm_node *)
							d->nodes.p[e->from];
					else if (!down && e->from == (int)i)
						m = (mm_node *)
							d->nodes.p[e->to];
					else
						continue;
					sum += m->pos + mm_across(d, m) / 2;
					cnt++;
				}
				if (!cnt)
					continue;
				want = sum / cnt - mm_across(d, n) / 2;
				lo = 0;
				hi = 1 << 20;
				for (j = 0; j < d->nodes.n; j++) {
					m = (mm_node *)d->nodes.p[j];
					if (m->rank != r || j == i)
						continue;
					if (m->order == n->order - 1 &&
					    m->pos + mm_across(d, m) +
					    mm_gap(m, n) > lo)
						lo = m->pos + mm_across(d, m) +
							mm_gap(m, n);
					if (m->order == n->order + 1 &&
					    m->pos - mm_gap(n, m) -
					    mm_across(d, n) < hi)
						hi = m->pos - mm_gap(n, m) -
							mm_across(d, n);
				}
				if (want < lo)
					want = lo;
				if (want > hi)
					want = hi;
				if (want >= 0)
					n->pos = want;
			}
		}
	}
	for (i = 0; i < d->nodes.n; i++) {
		n = (mm_node *)d->nodes.p[i];
		if (n->pos + mm_across(d, n) > wide)
			wide = n->pos + mm_across(d, n);
	}
	d->w = wide;
}

/* Where each edge leaves its parent and enters its child, across the
   ranks, and which row or column of the gutter between the two ranks its
   sideways run uses. Several edges in one gutter sharing a track would
   merge into a single line -- a diagram that looks right and is wrong --
   so the intervals are coloured greedily, widest first, which is enough
   for anything a person writes by hand. */
int mm_tracks(mm_dia *d, int r)
{
	size_t i, j;
	int k, out, in, used, bump, tracks = 0;
	mm_node *n;
	mm_edge *e, *f;
	int *ord;

	/* Fan the edges out across the face of the box they leave, and in
	   across the one they enter, so two edges never share a track. */
	for (i = 0; i < d->nodes.n; i++) {
		n = (mm_node *)d->nodes.p[i];
		if (n->rank != r)
			continue;
		out = 0;
		for (j = 0; j < d->edges.n; j++)
			if (((mm_edge *)d->edges.p[j])->from == (int)i)
				out++;
		k = 0;
		for (j = 0; j < d->edges.n; j++) {
			e = (mm_edge *)d->edges.p[j];
			if (e->from != (int)i)
				continue;
			e->c0 = n->dummy ? n->pos :
				n->pos + 1 + (mm_across(d, n) - 2) *
				(2 * k + 1) / (2 * out);
			k++;
		}
	}
	for (i = 0; i < d->nodes.n; i++) {
		n = (mm_node *)d->nodes.p[i];
		if (n->rank != r + 1)
			continue;
		in = 0;
		for (j = 0; j < d->edges.n; j++)
			if (((mm_edge *)d->edges.p[j])->to == (int)i)
				in++;
		k = 0;
		for (j = 0; j < d->edges.n; j++) {
			e = (mm_edge *)d->edges.p[j];
			if (e->to != (int)i)
				continue;
			e->c1 = n->dummy ? n->pos :
				n->pos + 1 + (mm_across(d, n) - 2) *
				(2 * k + 1) / (2 * in);
			k++;
		}
	}
	/* An edge reversed to break a cycle draws its head in the first row
	   of the gutter it leaves, so every track in that gutter moves down
	   one to leave the room -- without it the head lands on a corner and
	   the two are drawn over each other. */
	bump = 0;
	for (j = 0; j < d->edges.n; j++) {
		e = (mm_edge *)d->edges.p[j];
		if (e->rev && e->first &&
		    ((mm_node *)d->nodes.p[e->from])->rank == r)
			bump = 1;
	}
	ord = xm((d->edges.n + 1) * sizeof *ord);
	used = 0;
	for (j = 0; j < d->edges.n; j++) {
		e = (mm_edge *)d->edges.p[j];
		if (((mm_node *)d->nodes.p[e->from])->rank != r)
			continue;
		e->track = -1;
		if (e->c0 == e->c1)
			continue;
		ord[used++] = (int)j;
	}
	for (k = 0; k < used; k++)
		for (i = (size_t)k + 1; i < (size_t)used; i++) {
			e = (mm_edge *)d->edges.p[ord[k]];
			f = (mm_edge *)d->edges.p[ord[i]];
			if (abs(f->c1 - f->c0) > abs(e->c1 - e->c0)) {
				int t = ord[k];
				ord[k] = ord[i];
				ord[i] = t;
			}
		}
	for (k = 0; k < used; k++) {
		int t;
		e = (mm_edge *)d->edges.p[ord[k]];
		for (t = 0;; t++) {
			int clash = 0;
			for (i = 0; i < (size_t)k; i++) {
				int a0, a1, b0, b1;
				f = (mm_edge *)d->edges.p[ord[i]];
				if (f->track != t)
					continue;
				a0 = e->c0 < e->c1 ? e->c0 : e->c1;
				a1 = e->c0 < e->c1 ? e->c1 : e->c0;
				b0 = f->c0 < f->c1 ? f->c0 : f->c1;
				b1 = f->c0 < f->c1 ? f->c1 : f->c0;
				if (a0 <= b1 && b0 <= a1) {
					clash = 1;
					break;
				}
			}
			if (!clash)
				break;
		}
		e->track = t + bump;
		if (t + 1 + bump > tracks)
			tracks = t + 1 + bump;
	}
	free(ord);
	return tracks > bump ? tracks : bump;
}

/* Lay a flowchart out: break its cycles, rank it, bend its long edges,
   order each rank, size the boxes, place them across, then give each
   gutter as many rows or columns as its edges need tracks. The last step
   is the only one that knows which way the diagram runs: `pos` and `lvl`
   become x and y, flipped for BT and RL, and an edge's own track becomes
   a real coordinate with them. */
void mm_lay(mm_dia *d)
{
	size_t i;
	int r, ranks, at, t, hi;
	int *top;
	mm_node *n;
	mm_edge *e;

	if (d->kind != MM_FLOW)
		return;
	mm_cycles(d);
	mm_ranks(d);
	mm_dummies(d);
	ranks = mm_ranks(d);
	mm_order(d);
	mm_sizes(d);
	mm_place(d, ranks);
	top = xm((size_t)(ranks + 1) * sizeof *top);
	at = 0;
	for (r = 0; r < ranks; r++) {
		top[r] = at;
		hi = 0;
		for (i = 0; i < d->nodes.n; i++) {
			n = (mm_node *)d->nodes.p[i];
			if (n->rank == r && mm_along(d, n) > hi)
				hi = mm_along(d, n);
		}
		t = mm_tracks(d, r);
		at += hi + (r + 1 < ranks ? t + 2 : 0);
	}
	for (i = 0; i < d->nodes.n; i++) {
		n = (mm_node *)d->nodes.p[i];
		n->lvl = top[n->rank];
	}
	d->h = at;
	for (i = 0; i < d->edges.n; i++) {
		e = (mm_edge *)d->edges.p[i];
		if (e->track < 0)
			continue;
		n = (mm_node *)d->nodes.p[e->from];
		e->track += top[n->rank] + mm_along(d, n);
	}
	free(top);
	/* The one place a direction is read. `pos` and `lvl` become x and y,
	   and for BT and RL the level is measured from the far side -- the
	   edges' own tracks are levels too, so they are flipped here with
	   the nodes or the routing would point the wrong way. */
	for (i = 0; i < d->nodes.n; i++) {
		n = (mm_node *)d->nodes.p[i];
		if (mm_vert(d)) {
			n->x = n->pos;
			n->y = d->dir == MM_BT ? at - n->lvl - n->h : n->lvl;
		} else {
			n->y = n->pos;
			n->x = d->dir == MM_RL ? at - n->lvl - n->w : n->lvl;
		}
	}
	for (i = 0; i < d->edges.n; i++) {
		e = (mm_edge *)d->edges.p[i];
		if (e->track >= 0 && (d->dir == MM_BT || d->dir == MM_RL))
			e->track = at - 1 - e->track;
	}
	if (!mm_vert(d)) {
		t = d->w;
		d->w = at;
		d->h = t;
	}
}
