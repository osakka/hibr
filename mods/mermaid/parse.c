#include "mm.h"
#include <stdlib.h>
#include <string.h>

/* Say why a diagram was not understood, and stop understanding it. A bad
   line fails the whole diagram rather than drawing part of it: Mermaid
   errors whole, and half a diagram is wronger than none. */
void mm_err(mm_dia *d, int ln, const char *msg, const char *what)
{
	if (d->kind == MM_NONE && d->why.n)
		return;
	d->kind = MM_NONE;
	d->why.n = 0;
	if (d->why.p)
		d->why.p[0] = 0;
	if (ln > 0) {
		s_cat(&d->why, "line ");
		s_num(&d->why, (long)ln);
		s_cat(&d->why, ": ");
	}
	s_cat(&d->why, msg);
	if (what && *what) {
		s_cat(&d->why, ": ");
		s_cat(&d->why, what);
	}
}

/* Say something about a diagram that is still drawn -- a thing in it this
   release leaves out. Never through mm_err: that stops the diagram, and
   worse, the first one would then hide a real error later on. */
void mm_note(mm_dia *d, const char *msg, const char *what)
{
	if (d->note.n)
		return;
	s_cat(&d->note, msg);
	if (what && *what) {
		s_cat(&d->note, ": ");
		s_cat(&d->note, what);
	}
}

/* Whether a byte is a blank. */
int mm_sp(int c)
{
	return c == ' ' || c == '\t';
}

/* Put text into a string with the blanks at either end left out. */
void mm_trim(str *o, const char *p, size_t n)
{
	size_t i = 0;

	while (i < n && mm_sp(p[i]))
		i++;
	while (n > i && mm_sp(p[n - 1]))
		n--;
	o->n = 0;
	if (o->p)
		o->p[0] = 0;
	s_add(o, p + i, n - i);
}

/* The node with this id, or a new one with no label of its own. */
int mm_nodeof(mm_dia *d, const char *id, int make)
{
	size_t i;
	mm_node *n;

	for (i = 0; i < d->nodes.n; i++) {
		n = (mm_node *)d->nodes.p[i];
		if (!n->dummy && n->id.p && !strcmp(n->id.p, id))
			return (int)i;
	}
	if (!make)
		return -1;
	n = xm(sizeof *n);
	memset(n, 0, sizeof *n);
	s_init(&n->id);
	s_init(&n->label);
	s_cat(&n->id, id);
	s_cat(&n->label, id);
	v_add(&d->nodes, n);
	return (int)d->nodes.n - 1;
}

/* The closing bracket for an opening one, and the shape it means. */
int mm_shape(const char *p, size_t n, size_t *end, int *shape)
{
	if (n >= 2 && p[0] == '(' && p[1] == '(') {
		*shape = MM_CIRCLE;
		*end = 2;
		return 2;
	}
	switch (p[0]) {
	case '[':
		*shape = MM_RECT;
		*end = 1;
		return 1;
	case '(':
		*shape = MM_ROUND;
		*end = 1;
		return 1;
	case '{':
		*shape = MM_DIAMOND;
		*end = 1;
		return 1;
	}
	return 0;
}

/* Whether a byte may be part of a node's id. Everything else -- a bracket,
   a blank, an `&`, and the `-` or `=` a link starts with -- ends it. */
int mm_idch(int c)
{
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
	       (c >= '0' && c <= '9') || c == '_' || (unsigned char)c >= 0x80;
}

/* Where a node's label closes: the matching bracket, with nesting counted
   for the single ones and a doubled closer wanted for a circle. */
int mm_close(const char *p, size_t n, size_t at, int open, int oc, int cc)
{
	size_t j, depth = 0;

	for (j = at; j < n; j++) {
		if (p[j] == oc) {
			depth++;
		} else if (p[j] == cc) {
			if (!depth) {
				if (open == 2) {
					if (j + 1 < n && p[j + 1] == cc)
						return (int)j;
					continue;
				}
				return (int)j;
			}
			depth--;
		}
	}
	return -1;
}

/* A node reference -- an id, with a shape and a label of its own or
   without -- and how many bytes of the line it took. A shape this release
   does not draw is named rather than drawn as something else. */
int mm_noderef(mm_dia *d, int ln, const char *p, size_t n, int *idx)
{
	size_t i = 0, j, open;
	int shape = MM_RECT, nb, cl, oc, k;
	str id, lab;
	char se[8];

	while (i < n && mm_sp(p[i]))
		i++;
	j = i;
	while (j < n && mm_idch(p[j]))
		j++;
	if (j == i)
		return 0;
	s_init(&id);
	mm_trim(&id, p + i, j - i);
	if (j < n && (p[j] == '[' || p[j] == '(' || p[j] == '{')) {
		nb = mm_shape(p + j, n - j, &open, &shape);
		if (!nb) {
			s_free(&id);
			return 0;
		}
		oc = p[j];
		cl = oc == '{' ? '}' : oc == '[' ? ']' : ')';
		/* A shape whose brackets this release has no drawing for --
		   [[ ]], [( )], [/ /], [\\ \\] -- is said rather than
		   flattened into a rectangle. */
		if (shape != MM_CIRCLE && j + 1 < n &&
		    (p[j + 1] == oc || p[j + 1] == '(' || p[j + 1] == '/' ||
		     p[j + 1] == '\\')) {
			se[0] = (char)oc;
			se[1] = p[j + 1];
			se[2] = 0;
			mm_err(d, ln, "this node shape is not understood yet",
			       se);
			s_free(&id);
			return 0;
		}
		i = j + open;
		k = mm_close(p, n, i, (int)open, oc, cl);
		if (k < 0) {
			mm_err(d, ln, "a node's label is not closed", id.p);
			s_free(&id);
			return 0;
		}
		j = (size_t)k;
		s_init(&lab);
		mm_trim(&lab, p + i, j - i);
		/* A quoted label is quoted for the parser's sake, not ours. */
		if (lab.n >= 2 && lab.p[0] == '"' && lab.p[lab.n - 1] == '"') {
			memmove(lab.p, lab.p + 1, lab.n - 2);
			lab.n -= 2;
			lab.p[lab.n] = 0;
		}
		*idx = mm_nodeof(d, id.p ? id.p : "", 1);
		if (*idx >= 0) {
			mm_node *nd = (mm_node *)d->nodes.p[*idx];
			nd->shape = shape;
			nd->label.n = 0;
			if (nd->label.p)
				nd->label.p[0] = 0;
			s_cat(&nd->label, lab.p ? lab.p : "");
		}
		s_free(&lab);
		s_free(&id);
		return (int)(j + open);
	}
	*idx = mm_nodeof(d, id.p ? id.p : "", 1);
	s_free(&id);
	return (int)j;
}

/* A link at p: which kind of line, whether it ends in an arrowhead, the
   label written inside it, and how many bytes it took. 0 if p is not one.
   The forms are Mermaid's own -- `-->` `---` `-.->` `==>` `--x` `--o`,
   each with a label written inside it (`-- yes -->`) or after it in
   pipes, which the caller reads. */
int mm_link(const char *p, size_t n, int *style, int *arrow, str *lab)
{
	size_t i = 0, b, e;
	int mk;

	if (n < 2)
		return 0;
	if (p[0] == '-' && p[1] == '.') {
		*style = MM_DOTTED;
		mk = '-';
		i = 2;
	} else if (p[0] == '-' && p[1] == '-') {
		*style = MM_SOLID;
		mk = '-';
		i = 2;
	} else if (p[0] == '=' && p[1] == '=') {
		*style = MM_THICK;
		mk = '=';
		i = 2;
	} else {
		return 0;
	}
	while (i < n && (p[i] == mk || p[i] == '.'))
		i++;
	*arrow = -1;
	if (i < n) {
		if (p[i] == '>')
			*arrow = MM_PLAIN;
		else if (p[i] == 'x')
			*arrow = MM_CROSS;
		else if (p[i] == 'o')
			*arrow = MM_RING;
	}
	if (*arrow >= 0)
		return (int)i + 1;
	/* No head yet, so either the link ends here or its own label does:
	   `-- yes -->` is an opening run, text, and a closing run. */
	if (i >= n || mm_sp(p[i])) {
		b = i;
		while (b < n && mm_sp(p[b]))
			b++;
		e = b;
		while (e < n && p[e] != mk && p[e] != '.')
			e++;
		if (e >= n || e == b) {
			*arrow = -1;
			return (int)i;
		}
		mm_trim(lab, p + b, e - b);
		i = e;
		while (i < n && (p[i] == mk || p[i] == '.'))
			i++;
		if (i < n) {
			if (p[i] == '>')
				*arrow = MM_PLAIN;
			else if (p[i] == 'x')
				*arrow = MM_CROSS;
			else if (p[i] == 'o')
				*arrow = MM_RING;
		}
		return (int)i + (*arrow >= 0 ? 1 : 0);
	}
	return (int)i;
}

/* Join two nodes. A second edge between the same pair is kept -- Mermaid
   draws both -- and an edge from a node to itself is dropped with a word
   about it, since this release has no loop to draw. */
void mm_join(mm_dia *d, int from, int to, int style, int arrow,
	     const char *lab)
{
	mm_edge *e;

	if (from < 0 || to < 0)
		return;
	if (from == to) {
		mm_note(d, "an edge from a node to itself is not drawn yet",
			((mm_node *)d->nodes.p[from])->id.p);
		return;
	}
	e = xm(sizeof *e);
	memset(e, 0, sizeof *e);
	s_init(&e->label);
	e->from = from;
	e->to = to;
	e->style = style;
	e->arrow = arrow;
	s_cat(&e->label, lab ? lab : "");
	v_add(&d->edges, e);
}

/* Every node reference up to the next link, which is one node or several
   joined by `&`. 0 if there is none, with the error already said. */
int mm_list(mm_dia *d, int ln, const char *p, size_t n, size_t *at, vec *out)
{
	size_t i = *at;
	int nb, idx;

	for (;;) {
		while (i < n && mm_sp(p[i]))
			i++;
		nb = mm_noderef(d, ln, p + i, n - i, &idx);
		if (nb <= 0)
			return 0;
		v_add(out, (void *)(long)idx);
		i += (size_t)nb;
		while (i < n && mm_sp(p[i]))
			i++;
		if (i < n && p[i] == '&') {
			i++;
			continue;
		}
		break;
	}
	*at = i;
	return 1;
}

/* One flowchart line: a chain of node references joined by links, with
   `&` lists on either side, which Mermaid reads as every node on the left
   joined to every node on the right. */
void mm_flowline(mm_dia *d, int ln, const char *p, size_t n)
{
	size_t i = 0, j;
	int style = MM_SOLID, arrow = -1, k, m;
	str lab;
	vec cur, nxt;

	cur.p = nxt.p = 0;
	cur.n = nxt.n = cur.cap = nxt.cap = 0;
	s_init(&lab);
	if (!mm_list(d, ln, p, n, &i, &cur)) {
		if (!d->why.n)
			mm_err(d, ln, "this is not a node or a link", "");
		goto out;
	}
	while (i < n) {
		lab.n = 0;
		if (lab.p)
			lab.p[0] = 0;
		arrow = -1;
		j = (size_t)mm_link(p + i, n - i, &style, &arrow, &lab);
		if (!j) {
			mm_err(d, ln, "this is not a link", p + i);
			goto out;
		}
		i += j;
		while (i < n && mm_sp(p[i]))
			i++;
		if (i < n && p[i] == '|') {
			j = ++i;
			while (i < n && p[i] != '|')
				i++;
			mm_trim(&lab, p + j, i - j);
			if (lab.n >= 2 && lab.p[0] == '"' &&
			    lab.p[lab.n - 1] == '"') {
				memmove(lab.p, lab.p + 1, lab.n - 2);
				lab.n -= 2;
				lab.p[lab.n] = 0;
			}
			if (i < n)
				i++;
		}
		nxt.n = 0;
		if (!mm_list(d, ln, p, n, &i, &nxt)) {
			if (!d->why.n)
				mm_err(d, ln, "a link with nothing after it",
				       "");
			goto out;
		}
		for (k = 0; k < (int)cur.n; k++)
			for (m = 0; m < (int)nxt.n; m++)
				mm_join(d, (int)(long)cur.p[k],
					(int)(long)nxt.p[m], style, arrow,
					lab.p);
		if (d->kind == MM_NONE && d->why.n)
			goto out;
		v_free(&cur);
		cur = nxt;
		nxt.p = 0;
		nxt.n = nxt.cap = 0;
		while (i < n && mm_sp(p[i]))
			i++;
	}
out:
	v_free(&cur);
	v_free(&nxt);
	s_free(&lab);
}

/* A sequence diagram's participant, declared or met for the first time. */
int mm_part(mm_dia *d, const char *id, const char *as)
{
	int i = mm_nodeof(d, id, 1);
	mm_node *n;

	if (i >= 0 && as && *as) {
		n = (mm_node *)d->nodes.p[i];
		n->label.n = 0;
		if (n->label.p)
			n->label.p[0] = 0;
		s_cat(&n->label, as);
	}
	return i;
}

/* One sequenceDiagram line. */
void mm_seqline(mm_dia *d, int ln, const char *p, size_t n)
{
	size_t i = 0, j, k;
	str a, b, t;
	mm_msg *m;
	int style, arrow, from, to;

	s_init(&a);
	s_init(&b);
	s_init(&t);
	while (i < n && mm_sp(p[i]))
		i++;
	if (!strncmp(p + i, "participant", 11) || !strncmp(p + i, "actor", 5)) {
		i += p[i] == 'p' ? 11u : 5u;
		j = i;
		while (j < n && strncmp(p + j, " as ", 4))
			j++;
		mm_trim(&a, p + i, j - i);
		if (j < n)
			mm_trim(&b, p + j + 4, n - j - 4);
		mm_part(d, a.p ? a.p : "", b.p);
		goto out;
	}
	if (!strncmp(p + i, "activate", 8) || !strncmp(p + i, "deactivate", 10))
		goto out;
	if (!strncmp(p + i, "autonumber", 10))
		goto out;
	if (!strncmp(p + i, "Note ", 5) || !strncmp(p + i, "note ", 5)) {
		i += 5;
		m = xm(sizeof *m);
		memset(m, 0, sizeof *m);
		s_init(&m->text);
		m->note = MM_NOTE_OVER;
		if (!strncmp(p + i, "over", 4)) {
			i += 4;
		} else if (!strncmp(p + i, "left of", 7)) {
			m->note = MM_NOTE_LEFT;
			i += 7;
		} else if (!strncmp(p + i, "right of", 8)) {
			m->note = MM_NOTE_RIGHT;
			i += 8;
		} else {
			s_free(&m->text);
			free(m);
			mm_err(d, ln, "a note must be over, left of or right "
			       "of a participant", "");
			goto out;
		}
		j = i;
		while (j < n && p[j] != ':')
			j++;
		mm_trim(&a, p + i, j - i);
		if (j < n)
			mm_trim(&m->text, p + j + 1, n - j - 1);
		for (k = 0; k < a.n; k++)
			if (a.p[k] == ',')
				a.p[k] = 0;
		m->from = mm_part(d, a.p ? a.p : "", 0);
		m->to = a.p && strlen(a.p) < a.n ?
			mm_part(d, a.p + strlen(a.p) + 1, 0) : m->from;
		v_add(&d->msgs, m);
		goto out;
	}
	if (!strncmp(p + i, "loop", 4) || !strncmp(p + i, "alt", 3) ||
	    !strncmp(p + i, "else", 4) || !strncmp(p + i, "opt", 3) ||
	    !strncmp(p + i, "par", 3) || !strncmp(p + i, "end", 3) ||
	    !strncmp(p + i, "rect", 4) || !strncmp(p + i, "critical", 8) ||
	    !strncmp(p + i, "break", 5)) {
		mm_err(d, ln, "sequence blocks (loop, alt, opt, par) are not "
		       "understood yet", "");
		goto out;
	}
	j = i;
	while (j < n && p[j] != '-')
		j++;
	if (j >= n) {
		mm_err(d, ln, "this is not a participant, a note or a message",
		       "");
		goto out;
	}
	mm_trim(&a, p + i, j - i);
	style = MM_SOLID;
	i = j;
	if (j + 1 < n && p[j + 1] == '-') {
		style = MM_DOTTED;
		i = j + 2;
	} else {
		i = j + 1;
	}
	arrow = MM_PLAIN;
	if (i < n && p[i] == '>') {
		i++;
		if (i < n && p[i] == '>') {
			arrow = MM_OPEN;
			i++;
		}
	} else if (i < n && p[i] == 'x') {
		arrow = MM_CROSS;
		i++;
	} else if (i < n && p[i] == ')') {
		arrow = MM_RING;
		i++;
	} else {
		mm_err(d, ln, "a message needs an arrow", "");
		goto out;
	}
	j = i;
	while (j < n && p[j] != ':')
		j++;
	mm_trim(&b, p + i, j - i);
	if (j < n)
		mm_trim(&t, p + j + 1, n - j - 1);
	from = mm_part(d, a.p ? a.p : "", 0);
	to = mm_part(d, b.p ? b.p : "", 0);
	m = xm(sizeof *m);
	memset(m, 0, sizeof *m);
	s_init(&m->text);
	m->from = from;
	m->to = to;
	m->style = style;
	m->arrow = arrow;
	s_cat(&m->text, t.p ? t.p : "");
	v_add(&d->msgs, m);
out:
	s_free(&a);
	s_free(&b);
	s_free(&t);
}

/* One pie line: the title, or a label and its value. */
void mm_pieline(mm_dia *d, int ln, const char *p, size_t n)
{
	size_t i = 0, j;
	mm_slice *s;
	str lab;

	while (i < n && mm_sp(p[i]))
		i++;
	if (!strncmp(p + i, "title", 5) && (i + 5 >= n || mm_sp(p[i + 5]))) {
		mm_trim(&d->title, p + i + 5, n - i - 5);
		return;
	}
	j = n;
	while (j > i && p[j - 1] != ':')
		j--;
	if (j <= i) {
		mm_err(d, ln, "a slice is a label and a number", "");
		return;
	}
	s_init(&lab);
	mm_trim(&lab, p + i, j - i - 1);
	if (lab.n >= 2 && lab.p[0] == '"' && lab.p[lab.n - 1] == '"') {
		memmove(lab.p, lab.p + 1, lab.n - 2);
		lab.n -= 2;
		lab.p[lab.n] = 0;
	}
	s = xm(sizeof *s);
	memset(s, 0, sizeof *s);
	s_init(&s->label);
	s_cat(&s->label, lab.p ? lab.p : "");
	s->v = strtod(p + j, 0);
	s_free(&lab);
	if (s->v < 0) {
		mm_err(d, ln, "a slice cannot be negative", s->label.p);
		s_free(&s->label);
		free(s);
		return;
	}
	v_add(&d->slices, s);
}

/* Read a diagram. The first line that says anything decides which kind it
   is; everything after it is that kind's own grammar. */
mm_dia *mm_parse(const char *t)
{
	mm_dia *d = xm(sizeof *d);
	const char *p = t, *e;
	size_t n;
	int ln = 0, head = 0;

	memset(d, 0, sizeof *d);
	s_init(&d->why);
	s_init(&d->note);
	s_init(&d->title);
	d->kind = MM_NONE;
	while (*p) {
		e = strchr(p, '\n');
		n = e ? (size_t)(e - p) : strlen(p);
		ln++;
		while (n && (p[n - 1] == '\r' || mm_sp(p[n - 1])))
			n--;
		{
			size_t i = 0;
			while (i < n && mm_sp(p[i]))
				i++;
			if (i == n || (n - i >= 2 && p[i] == '%' &&
				       p[i + 1] == '%'))
				goto next;
			if (!head) {
				head = 1;
				if (!strncmp(p + i, "flowchart", 9) ||
				    !strncmp(p + i, "graph", 5)) {
					size_t k = i + (p[i] == 'f' ? 9u : 5u);
					d->kind = MM_FLOW;
					while (k < n && mm_sp(p[k]))
						k++;
					if (k + 1 < n && p[k] == 'L')
						d->dir = MM_LR;
					else if (k + 1 < n && p[k] == 'R')
						d->dir = MM_RL;
					else if (k + 1 < n && p[k] == 'B' &&
						 p[k + 1] == 'T')
						d->dir = MM_BT;
					else
						d->dir = MM_TD;
					goto next;
				}
				if (!strncmp(p + i, "sequenceDiagram", 15)) {
					d->kind = MM_SEQ;
					goto next;
				}
				if (!strncmp(p + i, "pie", 3)) {
					const char *ti;
					d->kind = MM_PIE;
					if (n - i > 3 && strstr(p + i,
								"showData"))
						d->data = 1;
					/* `pie title Some title` is one line
					   in Mermaid, so the header carries
					   the title as well as the kind. */
					ti = strstr(p + i, "title ");
					if (ti && ti < p + n)
						mm_trim(&d->title, ti + 6,
							(size_t)(p + n - ti) -
							6);
					goto next;
				}
				{
					str w;
					size_t k = i;
					while (k < n && !mm_sp(p[k]))
						k++;
					s_init(&w);
					mm_trim(&w, p + i, k - i);
					mm_err(d, ln, "this kind of diagram is "
					       "not understood yet", w.p);
					s_free(&w);
					return d;
				}
			}
			if (!strncmp(p + i, "subgraph", 8)) {
				mm_err(d, ln, "subgraph is not understood yet",
				       "");
				return d;
			}
			if (d->kind == MM_FLOW && (!strncmp(p + i, "style", 5) ||
						   !strncmp(p + i, "classDef", 8) ||
						   !strncmp(p + i, "class ", 6) ||
						   !strncmp(p + i, "click", 5) ||
						   !strncmp(p + i, "linkStyle", 9)))
				goto next;
			switch (d->kind) {
			case MM_FLOW:
				mm_flowline(d, ln, p + i, n - i);
				break;
			case MM_SEQ:
				mm_seqline(d, ln, p + i, n - i);
				break;
			case MM_PIE:
				mm_pieline(d, ln, p + i, n - i);
				break;
			}
			if (d->kind == MM_NONE)
				return d;
		}
next:
		if (!e)
			break;
		p = e + 1;
	}
	if (!head)
		mm_err(d, 0, "there is nothing here to draw", "");
	else if (d->kind == MM_FLOW && !d->nodes.n)
		mm_err(d, 0, "a flowchart with no nodes in it", "");
	else if (d->kind == MM_SEQ && !d->nodes.n)
		mm_err(d, 0, "a sequence diagram with no participants in it",
		       "");
	else if (d->kind == MM_PIE && !d->slices.n)
		mm_err(d, 0, "a pie with no slices in it", "");
	return d;
}

/* Give back everything a diagram holds. */
void mm_free(mm_dia *d)
{
	size_t i;

	if (!d)
		return;
	for (i = 0; i < d->nodes.n; i++) {
		mm_node *n = (mm_node *)d->nodes.p[i];
		s_free(&n->id);
		s_free(&n->label);
		free(n);
	}
	for (i = 0; i < d->edges.n; i++) {
		mm_edge *e = (mm_edge *)d->edges.p[i];
		s_free(&e->label);
		free(e);
	}
	for (i = 0; i < d->msgs.n; i++) {
		mm_msg *m = (mm_msg *)d->msgs.p[i];
		s_free(&m->text);
		free(m);
	}
	for (i = 0; i < d->slices.n; i++) {
		mm_slice *s = (mm_slice *)d->slices.p[i];
		s_free(&s->label);
		free(s);
	}
	v_free(&d->nodes);
	v_free(&d->edges);
	v_free(&d->msgs);
	v_free(&d->slices);
	s_free(&d->why);
	s_free(&d->note);
	s_free(&d->title);
	free(d);
}
