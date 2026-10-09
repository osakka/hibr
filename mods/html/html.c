#define _GNU_SOURCE

#include "hl.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Read a whole descriptor into o. */
int ht_slurp(int fd, str *o)
{
	char *b = xm(HIBR_IOCH);
	ssize_t r;

	for (;;) {
		r = read(fd, b, HIBR_IOCH);
		if (r < 0 && errno == EINTR)
			continue;
		if (r <= 0)
			break;
		s_add(o, b, (size_t)r);
	}
	free(b);
	return r < 0 ? -1 : 0;
}

/* The document a command names: -t text, a file, or standard input. */
int ht_input(int ac, char **av, int i, str *o)
{
	int fd, r;

	if (i < ac && !strcmp(av[i], "-t")) {
		if (i + 1 >= ac) {
			lg(HIBR_LERR, "html: -t needs the text");
			return -1;
		}
		s_cat(o, av[i + 1]);
		return 0;
	}
	if (i >= ac || !strcmp(av[i], "-"))
		return ht_slurp(0, o);
	fd = open(av[i], O_RDONLY);
	if (fd < 0) {
		lg(HIBR_LERR, "html: %s: %s", av[i], strerror(errno));
		return -1;
	}
	r = ht_slurp(fd, o);
	close(fd);
	return r;
}

/* html dump [-s] [-f context] [-t text | file]: the tree, as html5lib's
   tests write it -- a document, or with -f a fragment parsed in a context
   element ("div", "svg path", "math mi"); -s parses with scripting on. */
int ht_dump(sh *s, int ac, char **av)
{
	int i = 2, scripting = 0, ns = HL_NSHTML;
	const char *ctx = 0;
	hl_doc *d;
	str in, o;

	(void)s;
	while (i < ac && av[i][0] == '-' && av[i][1] && strcmp(av[i], "-t")) {
		if (!strcmp(av[i], "-s"))
			scripting = 1;
		else if (!strcmp(av[i], "-f") && i + 1 < ac)
			ctx = av[++i];
		else
			break;
		i++;
	}
	s_init(&in);
	if (ht_input(ac, av, i, &in) < 0) {
		s_free(&in);
		return HIBR_FAIL;
	}
	if (ctx) {
		if (!strncmp(ctx, "svg ", 4)) {
			ns = HL_NSSVG;
			ctx += 4;
		} else if (!strncmp(ctx, "math ", 5)) {
			ns = HL_NSMATH;
			ctx += 5;
		}
		d = hl_parsefrag(in.p ? in.p : "", in.n, ctx, ns, scripting);
	} else {
		d = hl_parse(in.p ? in.p : "", in.n, scripting);
	}
	s_init(&o);
	hl_dump(ctx ? d->root->kid : d->root, 0, &o);
	fflush(stdout);
	if (o.p && write(1, o.p, o.n) < 0)
		lg(HIBR_LDBG, "html: could not write the tree");
	s_free(&o);
	s_free(&in);
	hl_docfree(d);
	return HIBR_OK;
}

/* The documents parsed and not yet closed, by handle less one. */
vec ht_docs;

/* Give back a word: into $RET when bound, else printed on its own line. */
void ht_say(sh *s, const char *v)
{
	hibr_ret(s, v);
	if (!s->bind)
		printf("%s\n", v);
}

/* Give back a number. */
void ht_sayn(sh *s, long n)
{
	str b;

	s_init(&b);
	s_num(&b, n);
	ht_say(s, b.p);
	s_free(&b);
}

/* The document a handle names, or none, said why. */
hl_doc *ht_doc(const char *h)
{
	long i = h ? strtol(h, 0, 10) : 0;

	if (i < 1 || (size_t)i > ht_docs.n || !ht_docs.p[i - 1]) {
		lg(HIBR_LERR, "html: %s: no such document", h ? h : "");
		return 0;
	}
	return ht_docs.p[i - 1];
}

/* The node a number names in a document -- the document itself when none
   is given -- or none, said why. */
hl_n *ht_node(hl_doc *d, int ac, char **av, int i)
{
	long k;

	if (i >= ac)
		return d->root;
	k = strtol(av[i], 0, 10);
	if (k < 0 || (size_t)k >= d->nodes.n) {
		lg(HIBR_LERR, "html: %s: no such node", av[i]);
		return 0;
	}
	return d->nodes.p[k];
}

/* html parse [-s] [-f context] [-t text | file]: parse a document, or a
   fragment, and give a handle to it. */
int ht_parse(sh *s, int ac, char **av)
{
	int i = 2, scripting = 0, ns = HL_NSHTML;
	const char *ctx = 0;
	hl_doc *d;
	str in;
	size_t k;

	while (i < ac && av[i][0] == '-' && av[i][1] && strcmp(av[i], "-t")) {
		if (!strcmp(av[i], "-s"))
			scripting = 1;
		else if (!strcmp(av[i], "-f") && i + 1 < ac)
			ctx = av[++i];
		else
			break;
		i++;
	}
	s_init(&in);
	if (ht_input(ac, av, i, &in) < 0) {
		s_free(&in);
		return HIBR_FAIL;
	}
	if (ctx) {
		if (!strncmp(ctx, "svg ", 4)) {
			ns = HL_NSSVG;
			ctx += 4;
		} else if (!strncmp(ctx, "math ", 5)) {
			ns = HL_NSMATH;
			ctx += 5;
		}
		d = hl_parsefrag(in.p ? in.p : "", in.n, ctx, ns, scripting);
	} else {
		d = hl_parse(in.p ? in.p : "", in.n, scripting);
	}
	s_free(&in);
	for (k = 0; k < ht_docs.n && ht_docs.p[k]; k++)
		;
	if (k == ht_docs.n)
		v_add(&ht_docs, d);
	else
		ht_docs.p[k] = d;
	ht_sayn(s, (long)k + 1);
	return HIBR_OK;
}

/* The text under a node, as textContent gives it. */
void ht_text(hl_n *n, str *o)
{
	hl_n *k;

	if (n->t == HL_TEXT) {
		s_add(o, n->s.p ? n->s.p : "", n->s.n);
		return;
	}
	for (k = n->kid; k; k = k->nx)
		if (k->t == HL_TEXT || k->t == HL_ELEM)
			ht_text(k, o);
}

/* Give back a list of nodes by number: an array when bound, else one a
   line. */
void ht_saylist(sh *s, vec *nodes)
{
	vec words = { 0, 0, 0 };
	str b;
	size_t i;

	for (i = 0; i < nodes->n; i++) {
		s_init(&b);
		s_num(&b, ((hl_n *)nodes->p[i])->id);
		v_add(&words, b.p);
	}
	if (s->bind)
		hibr_retn(s, (char **)words.p, words.n);
	else
		for (i = 0; i < words.n; i++)
			printf("%s\n", (char *)words.p[i]);
	for (i = 0; i < words.n; i++)
		free(words.p[i]);
	v_free(&words);
}

/* html: HTML parsed as the standard says browsers parse it, and asked
   about through handles. */
int m_html(sh *s, int ac, char **av)
{
	const char *sub = ac >= 2 ? av[1] : "";
	hl_doc *d = 0;
	hl_n *n;
	vec out = { 0, 0, 0 };
	str o;
	int rc;

	if (!strcmp(sub, "dump"))
		return ht_dump(s, ac, av);
	if (!strcmp(sub, "parse"))
		return ht_parse(s, ac, av);
	if (ac < 3 || !*sub) {
		lg(HIBR_LERR, "usage: html parse|dump [-s] [-f context] [-t text | file] | "
			      "close|title h | query h selector [node] | text|tag|kids|parent h [node] | "
			      "attr h node [name] | lines h width [-i] [-a] [node]");
		return 2;
	}
	if (!(d = ht_doc(av[2])))
		return HIBR_FAIL;
	if (!strcmp(sub, "close")) {
		ht_docs.p[strtol(av[2], 0, 10) - 1] = 0;
		hl_docfree(d);
		return HIBR_OK;
	}
	if (!strcmp(sub, "title")) {
		hl_n *t = hl_findel(d->root, "title");

		s_init(&o);
		if (t)
			ht_text(t, &o);
		ht_say(s, o.p ? o.p : "");
		s_free(&o);
		return HIBR_OK;
	}
	if (!strcmp(sub, "query")) {
		if (ac < 4) {
			lg(HIBR_LERR, "usage: html query h selector [node]");
			return 2;
		}
		if (!(n = ht_node(d, ac, av, 4)))
			return HIBR_FAIL;
		if (hl_query(n, av[3], &out) < 0) {
			lg(HIBR_LERR, "html: %s: not a selector this understands", av[3]);
			v_free(&out);
			return 2;
		}
		rc = out.n ? HIBR_OK : HIBR_FAIL;
		ht_saylist(s, &out);
		v_free(&out);
		return rc;
	}
	if (!strcmp(sub, "lines")) {
		int flags = 0, i = 4;
		long w;

		if (ac < 4) {
			lg(HIBR_LERR, "usage: html lines h width [-i] [-a] [node]");
			return 2;
		}
		w = strtol(av[3], 0, 10);
		for (; i < ac && av[i][0] == '-'; i++) {
			if (!strcmp(av[i], "-i"))
				flags |= 1;
			else if (!strcmp(av[i], "-a"))
				flags |= 2;
		}
		if (!(n = ht_node(d, ac, av, i)))
			return HIBR_FAIL;
		return hl_lines(s, n, (int)w, flags);
	}
	if (!(n = ht_node(d, ac, av, 3)))
		return HIBR_FAIL;
	if (!strcmp(sub, "text")) {
		s_init(&o);
		ht_text(n, &o);
		ht_say(s, o.p ? o.p : "");
		s_free(&o);
		return HIBR_OK;
	}
	if (!strcmp(sub, "tag")) {
		ht_say(s, n->t == HL_ELEM ? n->tag : n->t == HL_TEXT ? "#text" :
			  n->t == HL_COMMENT ? "#comment" : "#document");
		return HIBR_OK;
	}
	if (!strcmp(sub, "parent")) {
		if (!n->up)
			return HIBR_FAIL;
		ht_sayn(s, n->up->id);
		return HIBR_OK;
	}
	if (!strcmp(sub, "kids")) {
		hl_n *k;

		for (k = n->kid; k; k = k->nx)
			if (k->t == HL_ELEM)
				v_add(&out, k);
		ht_saylist(s, &out);
		v_free(&out);
		return HIBR_OK;
	}
	if (!strcmp(sub, "attr")) {
		size_t i;

		if (ac > 4) {
			const char *v = n->t == HL_ELEM ? hl_attr_get(n, av[4]) : 0;

			if (!v)
				return HIBR_FAIL;
			ht_say(s, v);
			return HIBR_OK;
		}
		for (i = 0; i < n->attrs.n; i++) {
			hl_attr *a = n->attrs.p[i];
			char *ks[1];

			ks[0] = a->nm;
			if (s->bind)
				hibr_setp(s, "RET", ks, 1, a->val ? a->val : "");
			else
				printf("%s=%s\n", a->nm, a->val ? a->val : "");
		}
		return HIBR_OK;
	}
	lg(HIBR_LERR, "html: no subcommand %s", sub);
	return 2;
}

/* Let every document go when the module does. */
void ht_fini(sh *s)
{
	size_t i;

	(void)s;
	for (i = 0; i < ht_docs.n; i++)
		hl_docfree(ht_docs.p[i]);
	v_free(&ht_docs);
}

const hibr_bi html_bi[] = {
	{ "html", m_html, "HTML parsed as the HTML5 standard says browsers parse it" },
	HIBR_BI_END
};

HIBR_MODULE("html", "1.0", "HTML5: the standard's parser, a DOM, queries and rendering",
	    html_bi, 0, ht_fini);
