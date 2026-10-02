#define _GNU_SOURCE

#include "mk.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

/* Read a whole descriptor into o. */
static int md_slurp(int fd, str *o)
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
static int md_input(int ac, char **av, int i, str *o)
{
	int fd, r;

	if (i < ac && !strcmp(av[i], "-t")) {
		if (i + 1 >= ac) {
			lg(HIBR_LERR, "md: -t needs the text");
			return -1;
		}
		s_cat(o, av[i + 1]);
		return 0;
	}
	if (i >= ac || !strcmp(av[i], "-"))
		return md_slurp(0, o);
	fd = open(av[i], O_RDONLY | O_CLOEXEC);
	if (fd < 0) {
		lg(HIBR_LERR, "md: %s: %s", av[i], strerror(errno));
		return -1;
	}
	r = md_slurp(fd, o);
	close(fd);
	return r;
}

/* Free the reference definitions a parse kept. */
static void md_refs(vec *r)
{
	size_t i;
	mk_ref *f;

	for (i = 0; i < r->cap; i++) {
		if (!(f = r->p[i]))
			continue;
		free(f->lab);
		free(f->url);
		free(f->title);
		free(f);
	}
	free(r->p);
}

/* Write text to standard output, or into the result slot under :=. */
static void md_emit(sh *s, const char *p, size_t n)
{
	if (s->bind) {
		str t;
		s_init(&t);
		s_add(&t, p, n && p[n - 1] == '\n' ? n - 1 : n);
		hibr_ret(s, t.p ? t.p : "");
		s_free(&t);
		return;
	}
	fflush(stdout);
	while (n) {
		ssize_t w = write(1, p, n);
		if (w < 0 && errno == EINTR)
			continue;
		if (w <= 0)
			break;
		p += w;
		n -= (size_t)w;
	}
}

/* md html: a document as HTML, as cmark-gfm writes it. */
static int md_html(sh *s, int ac, char **av, int i, int gfm)
{
	str in, o, clean;
	vec refs;
	mk_n *doc;
	size_t k;

	s_init(&in);
	s_init(&o);
	s_init(&clean);
	if (md_input(ac, av, i, &in) < 0) {
		s_free(&in);
		return HIBR_FAIL;
	}
	for (k = 0; k < in.n; k++) {
		if (in.p[k])
			s_ch(&clean, in.p[k]);
		else
			s_cat(&clean, "\xEF\xBF\xBD");
	}
	doc = mk_parse(clean.p ? clean.p : "", clean.n, gfm, &refs);
	mk_html(doc, &o, gfm);
	md_emit(s, o.p ? o.p : "", o.n);
	mk_free(doc);
	md_refs(&refs);
	s_free(&in);
	s_free(&o);
	s_free(&clean);
	return HIBR_OK;
}

/* md lines: one style string per source line, a letter per character. */
static int md_lines(sh *s, int ac, char **av, int i, int gfm)
{
	str in, line, all;
	vec refs, out = { 0, 0, 0 };
	mk_n *doc;
	char *sty;
	size_t k, b;
	int l;

	s_init(&in);
	if (md_input(ac, av, i, &in) < 0) {
		s_free(&in);
		return HIBR_FAIL;
	}
	doc = mk_parse(in.p ? in.p : "", in.n, gfm, &refs);
	sty = xm(in.n + 1);
	mk_styles(doc, in.p, in.n, sty);
	s_init(&line);
	s_init(&all);
	for (k = b = 0; k <= in.n; k++) {
		if (k < in.n && in.p[k] != '\n') {
			if (in.p[k] == '\r' && k + 1 < in.n && in.p[k + 1] == '\n')
				continue;
			mk_utf8(in.p + k, in.n - k, &l);
			s_ch(&line, sty[k]);
			k += (size_t)l - 1;
			continue;
		}
		if (k == in.n && k == b && in.n)
			break;
		v_add(&out, xs(line.p ? line.p : ""));
		s_cat(&all, line.p ? line.p : "");
		s_ch(&all, '\n');
		line.n = 0;
		if (line.p)
			line.p[0] = 0;
		b = k + 1;
	}
	if (s->bind)
		hibr_retn(s, (char **)out.p, out.n);
	else
		md_emit(s, all.p ? all.p : "", all.n);
	for (k = 0; k < out.n; k++)
		free(out.p[k]);
	free(out.p);
	free(sty);
	mk_free(doc);
	md_refs(&refs);
	s_free(&in);
	s_free(&line);
	s_free(&all);
	return HIBR_OK;
}

/* md: markdown, parsed as CommonMark with GitHub's extensions. */
int m_md(sh *s, int ac, char **av)
{
	int i = 2, gfm = 1;

	if (ac < 2) {
		lg(HIBR_LERR, "md: html|lines [-c] [-t text | file]");
		return 2;
	}
	if (i < ac && !strcmp(av[i], "-c")) {
		gfm = 0;
		i++;
	}
	if (!strcmp(av[1], "html"))
		return md_html(s, ac, av, i, gfm);
	if (!strcmp(av[1], "lines"))
		return md_lines(s, ac, av, i, gfm);
	lg(HIBR_LERR, "md: no subcommand %s", av[1]);
	return 2;
}

const hibr_bi md_bi[] = {
	{ "md", m_md, "markdown: CommonMark and GitHub's extensions, as HTML or styles" },
	HIBR_BI_END
};

HIBR_MODULE("md", "1.0", "markdown: CommonMark with GitHub's extensions", md_bi,
	    0, 0);
