#define _GNU_SOURCE

#include "hl.h"
#include <stdlib.h>
#include <string.h>

/* A new node of a kind, numbered in its document. */
hl_n *hl_new(hl_doc *d, int t)
{
	hl_n *n = xm(sizeof *n);

	memset(n, 0, sizeof *n);
	n->t = t;
	s_init(&n->s);
	n->id = (int)d->nodes.n;
	v_add(&d->nodes, n);
	return n;
}

/* Take a node out of its parent. */
void hl_unlink(hl_n *n)
{
	if (!n->up)
		return;
	if (n->pv)
		n->pv->nx = n->nx;
	else
		n->up->kid = n->nx;
	if (n->nx)
		n->nx->pv = n->pv;
	else
		n->up->last = n->pv;
	n->up = n->nx = n->pv = 0;
}

/* Put a node last in a parent. */
void hl_append(hl_n *up, hl_n *n)
{
	hl_unlink(n);
	n->up = up;
	n->pv = up->last;
	if (up->last)
		up->last->nx = n;
	else
		up->kid = n;
	up->last = n;
}

/* Put a node in a parent before another of its children, or last. */
void hl_insbefore(hl_n *up, hl_n *n, hl_n *ref)
{
	if (!ref) {
		hl_append(up, n);
		return;
	}
	hl_unlink(n);
	n->up = up;
	n->nx = ref;
	n->pv = ref->pv;
	if (ref->pv)
		ref->pv->nx = n;
	else
		up->kid = n;
	ref->pv = n;
}

/* An attribute's value, or none. */
const char *hl_attr_get(hl_n *e, const char *nm)
{
	size_t i;

	for (i = 0; i < e->attrs.n; i++) {
		hl_attr *a = e->attrs.p[i];

		if (!a->pfx && !strcmp(a->nm, nm))
			return a->val;
	}
	return 0;
}

/* Set an attribute, replacing one of the same name. */
void hl_setattr(hl_n *e, const char *nm, const char *val)
{
	hl_attr *a;
	size_t i;

	for (i = 0; i < e->attrs.n; i++) {
		a = e->attrs.p[i];
		if (!a->pfx && !strcmp(a->nm, nm)) {
			free(a->val);
			a->val = xs(val);
			return;
		}
	}
	a = xm(sizeof *a);
	memset(a, 0, sizeof *a);
	a->nm = xs(nm);
	a->val = xs(val);
	v_add(&e->attrs, a);
}

/* Whether a node is an HTML element with this name. */
int hl_isel(hl_n *n, const char *tag)
{
	return n && n->t == HL_ELEM && n->ns == HL_NSHTML && !strcmp(n->tag, tag);
}

/* Whether a node is an element of a namespace with this name. */
int hl_isnsel(hl_n *n, int ns, const char *tag)
{
	return n && n->t == HL_ELEM && n->ns == ns && !strcmp(n->tag, tag);
}

/* Free a document and every node in it. */
void hl_docfree(hl_doc *d)
{
	size_t i, j;

	if (!d)
		return;
	for (i = 0; i < d->nodes.n; i++) {
		hl_n *n = d->nodes.p[i];

		for (j = 0; j < n->attrs.n; j++) {
			hl_attr *a = n->attrs.p[j];

			free(a->nm);
			free(a->val);
			free(a->pfx);
			free(a);
		}
		v_free(&n->attrs);
		s_free(&n->s);
		free(n->tag);
		free(n->pub);
		free(n->sys);
		free(n);
	}
	v_free(&d->nodes);
	free(d);
}

/* Order attributes for the dump: by their printed name, a prefix and a
   space before the local name. */
int hl_acmp(const void *a, const void *b)
{
	const hl_attr *x = *(hl_attr *const *)a, *y = *(hl_attr *const *)b;
	str p, q;
	int c;

	s_init(&p);
	s_init(&q);
	if (x->pfx) {
		s_cat(&p, x->pfx);
		s_ch(&p, ' ');
	}
	s_cat(&p, x->nm);
	if (y->pfx) {
		s_cat(&q, y->pfx);
		s_ch(&q, ' ');
	}
	s_cat(&q, y->nm);
	c = strcmp(p.p, q.p);
	s_free(&p);
	s_free(&q);
	return c;
}

/* A deep copy of a node, in its document. */
hl_n *hl_clone(hl_doc *d, hl_n *n)
{
	hl_n *c = hl_new(d, n->t), *k;
	size_t i;

	c->ns = n->ns;
	c->fl = n->fl;
	c->tag = n->tag ? xs(n->tag) : 0;
	s_cat(&c->s, n->s.p ? n->s.p : "");
	for (i = 0; i < n->attrs.n; i++) {
		hl_attr *a = n->attrs.p[i], *b = xm(sizeof *b);

		memset(b, 0, sizeof *b);
		b->nm = xs(a->nm);
		b->val = xs(a->val ? a->val : "");
		b->pfx = a->pfx ? xs(a->pfx) : 0;
		v_add(&c->attrs, b);
	}
	for (k = n->kid; k; k = k->nx)
		hl_append(c, hl_clone(d, k));
	return c;
}

/* The first descendant that is an HTML element of a name, or none. */
hl_n *hl_findel(hl_n *n, const char *tag)
{
	hl_n *k, *f;

	for (k = n->kid; k; k = k->nx) {
		if (hl_isel(k, tag))
			return k;
		if ((f = hl_findel(k, tag)))
			return f;
	}
	return 0;
}

/* The selected option among a select's descendants: the last one marked
   selected, else the first. */
void hl_selopt(hl_n *n, hl_n **first, hl_n **sel)
{
	hl_n *k;

	for (k = n->kid; k; k = k->nx) {
		if (hl_isel(k, "option")) {
			if (!*first)
				*first = k;
			if (hl_attr_get(k, "selected"))
				*sel = k;
		}
		hl_selopt(k, first, sel);
	}
}

/* Fill each select's selectedcontent with a copy of its selected option's
   content, as the standard's parser does for a customizable select. */
void hl_selected(hl_doc *d, hl_n *n)
{
	hl_n *k, *sc, *first, *sel;

	for (k = n->kid; k; k = k->nx) {
		if (hl_isel(k, "select") && (sc = hl_findel(k, "selectedcontent"))) {
			first = sel = 0;
			hl_selopt(k, &first, &sel);
			if (!sel)
				sel = first;
			if (sel) {
				while (sc->kid)
					hl_unlink(sc->kid);
				for (first = sel->kid; first; first = first->nx)
					hl_append(sc, hl_clone(d, first));
			}
		}
		hl_selected(d, k);
	}
}

/* Write the indent for a depth in html5lib's format. */
void hl_ind(str *o, int dep)
{
	int i;

	s_cat(o, "| ");
	for (i = 0; i < dep; i++)
		s_cat(o, "  ");
}

/* Dump a node's children as html5lib's tests write a tree: one line a
   node, "| " and two spaces a level, attributes sorted under their
   element, a template's contents under "content". */
void hl_dump(hl_n *n, int dep, str *o)
{
	hl_n *k;
	vec as = { 0, 0, 0 };
	size_t i;

	for (k = n->kid; k; k = k->nx) {
		hl_ind(o, dep);
		switch (k->t) {
		case HL_DOCTYPE:
			s_cat(o, "<!DOCTYPE ");
			s_cat(o, k->s.p ? k->s.p : "");
			if (k->haspub || k->hassys) {
				s_cat(o, " \"");
				s_cat(o, k->pub ? k->pub : "");
				s_cat(o, "\" \"");
				s_cat(o, k->sys ? k->sys : "");
				s_cat(o, "\"");
			}
			s_cat(o, ">\n");
			break;
		case HL_COMMENT:
			s_cat(o, "<!-- ");
			s_cat(o, k->s.p ? k->s.p : "");
			s_cat(o, " -->\n");
			break;
		case HL_TEXT:
			s_ch(o, '"');
			s_cat(o, k->s.p ? k->s.p : "");
			s_cat(o, "\"\n");
			break;
		case HL_ELEM:
			s_ch(o, '<');
			if (k->ns == HL_NSSVG)
				s_cat(o, "svg ");
			else if (k->ns == HL_NSMATH)
				s_cat(o, "math ");
			s_cat(o, k->tag);
			s_cat(o, ">\n");
			as.n = 0;
			for (i = 0; i < k->attrs.n; i++)
				v_add(&as, k->attrs.p[i]);
			if (as.n)
				qsort(as.p, as.n, sizeof(void *), hl_acmp);
			for (i = 0; i < as.n; i++) {
				hl_attr *a = as.p[i];

				hl_ind(o, dep + 1);
				if (a->pfx) {
					s_cat(o, a->pfx);
					s_ch(o, ' ');
				}
				s_cat(o, a->nm);
				s_cat(o, "=\"");
				s_cat(o, a->val ? a->val : "");
				s_cat(o, "\"\n");
			}
			if (k->tmpl) {
				hl_ind(o, dep + 1);
				s_cat(o, "content\n");
				hl_dump(k->tmpl, dep + 2, o);
			}
			hl_dump(k, dep + 1, o);
			break;
		}
	}
	v_free(&as);
}
