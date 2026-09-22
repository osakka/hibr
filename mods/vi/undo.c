#define _GNU_SOURCE

#include "vi.h"
#include <stdlib.h>
#include <string.h>

/* Record one edit so it can be taken back. The text kept is whatever would
   have to be put back: what was removed for a delete, what was added for an
   insert. Undo is linear with a redo stack, deliberately -- see the README. */
void vi_urec(vi_ed *e, int kind, size_t pos, const char *t, size_t n)
{
	vi_edit *u;
	size_t i;

	if (e->nored) {
		return;
	}
	for (i = 0; i < e->redo.n; i++) {
		free(((vi_edit *)e->redo.p[i])->t);
		free(e->redo.p[i]);
	}
	e->redo.n = 0;
	u = xm(sizeof *u);
	u->kind = kind;
	u->pos = pos;
	u->n = n;
	u->t = xm(n + 1);
	memcpy(u->t, t, n);
	u->t[n] = 0;
	u->cur = e->cur;
	u->group = e->group;
	v_add(&e->undo, u);
}

/* Insert, and remember it. */
void vi_eins(vi_ed *e, size_t pos, const char *t, size_t n)
{
	if (!n)
		return;
	vi_urec(e, VI_EINS, pos, t, n);
	vi_ins(&e->b, pos, t, n);
}

/* Delete, and remember what went. */
void vi_edel(vi_ed *e, size_t pos, size_t n)
{
	str t;
	size_t len = vi_len(&e->b);

	if (pos >= len || !n)
		return;
	if (pos + n > len)
		n = len - pos;
	s_init(&t);
	vi_get(&e->b, pos, n, &t);
	vi_urec(e, VI_EDEL, pos, t.p ? t.p : "", t.n);
	vi_del(&e->b, pos, n);
	s_free(&t);
}

/* Apply one recorded edit backwards. */
void vi_unapply(vi_ed *e, vi_edit *u)
{
	if (u->kind == VI_EINS)
		vi_del(&e->b, u->pos, u->n);
	else
		vi_ins(&e->b, u->pos, u->t, u->n);
}

/* Apply one recorded edit forwards again. */
void vi_reapply(vi_ed *e, vi_edit *u)
{
	if (u->kind == VI_EINS)
		vi_ins(&e->b, u->pos, u->t, u->n);
	else
		vi_del(&e->b, u->pos, u->n);
}

/* Take back the most recent group of edits; 0 when there is nothing left. */
int vi_undo1(vi_ed *e)
{
	vi_edit *u;
	int g;

	if (!e->undo.n)
		return 0;
	g = ((vi_edit *)e->undo.p[e->undo.n - 1])->group;
	while (e->undo.n) {
		u = (vi_edit *)e->undo.p[e->undo.n - 1];
		if (u->group != g)
			break;
		e->undo.n--;
		vi_unapply(e, u);
		e->cur = u->pos;
		v_add(&e->redo, u);
	}
	return 1;
}

/* Put back the most recent group that was taken away. */
int vi_redo1(vi_ed *e)
{
	vi_edit *u;
	int g;

	if (!e->redo.n)
		return 0;
	g = ((vi_edit *)e->redo.p[e->redo.n - 1])->group;
	while (e->redo.n) {
		u = (vi_edit *)e->redo.p[e->redo.n - 1];
		if (u->group != g)
			break;
		e->redo.n--;
		vi_reapply(e, u);
		e->cur = u->kind == VI_EINS ? u->pos + u->n : u->pos;
		v_add(&e->undo, u);
	}
	return 1;
}

/* Start a new group, so what follows undoes in one go. */
void vi_ugroup(vi_ed *e)
{
	e->group++;
}

/* Throw away both histories. */
void vi_ufree(vi_ed *e)
{
	size_t i;

	for (i = 0; i < e->undo.n; i++) {
		free(((vi_edit *)e->undo.p[i])->t);
		free(e->undo.p[i]);
	}
	for (i = 0; i < e->redo.n; i++) {
		free(((vi_edit *)e->redo.p[i])->t);
		free(e->redo.p[i]);
	}
	v_free(&e->undo);
	v_free(&e->redo);
}
