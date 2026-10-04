#define _GNU_SOURCE
#include "pm.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

/* A new component of a name. */
pm_comp *pm_new(const char *name)
{
	pm_comp *c = xm(sizeof *c);

	memset(c, 0, sizeof *c);
	c->name = xs(name);
	return c;
}

/* Free a property. */
void pm_propfree(pm_prop *p)
{
	size_t i;

	for (i = 0; i < p->pars.n; i++) {
		pm_par *a = p->pars.p[i];

		free(a->name);
		free(a->val);
		free(a);
	}
	v_free(&p->pars);
	free(p->group);
	free(p->name);
	free(p->val);
	free(p);
}

/* Free a component and everything in it. */
void pm_free(pm_comp *c)
{
	size_t i;

	if (!c)
		return;
	for (i = 0; i < c->props.n; i++)
		pm_propfree(c->props.p[i]);
	for (i = 0; i < c->kids.n; i++)
		pm_free(c->kids.p[i]);
	v_free(&c->props);
	v_free(&c->kids);
	free(c->name);
	free(c);
}

/* Upper-case a name in place: property and parameter names are not case
   sensitive, and are kept upper case. */
void pm_upper(char *p)
{
	for (; *p; p++)
		if (*p >= 'a' && *p <= 'z')
			*p -= 32;
}

/* Split one unfolded content line into a property: [group.]NAME, then
   ;PARAM=value[,value] with values optionally quoted, then :value. A bare
   parameter with no = (vCard 2.1's EMAIL;WORK:) becomes TYPE=it. Null for
   a line with no colon. */
pm_prop *pm_split(const char *l, size_t n)
{
	const char *e = l + n, *p = l, *q;
	pm_prop *pr;
	pm_par *a;
	str v;

	while (p < e && *p != ';' && *p != ':')
		p++;
	if (p >= e)
		return 0;
	pr = xm(sizeof *pr);
	memset(pr, 0, sizeof *pr);
	s_init(&v);
	s_add(&v, l, (size_t)(p - l));
	q = memchr(v.p ? v.p : "", '.', v.n);
	if (q) {
		pr->group = xm((size_t)(q - v.p) + 1);
		memcpy(pr->group, v.p, (size_t)(q - v.p));
		pr->group[q - v.p] = 0;
		pr->name = xs(q + 1);
	} else {
		pr->name = xs(v.p ? v.p : "");
	}
	pm_upper(pr->name);
	while (p < e && *p == ';') {
		const char *n0 = ++p;

		while (p < e && *p != '=' && *p != ';' && *p != ':')
			p++;
		a = xm(sizeof *a);
		memset(a, 0, sizeof *a);
		v.n = 0;
		s_add(&v, n0, (size_t)(p - n0));
		if (p < e && *p == '=') {
			a->name = xs(v.p ? v.p : "");
			pm_upper(a->name);
			p++;
			v.n = 0;
			while (p < e && *p != ';' && *p != ':') {
				if (*p == '"') {
					p++;
					while (p < e && *p != '"')
						s_ch(&v, *p++);
					if (p < e)
						p++;
				} else {
					s_ch(&v, *p++);
				}
			}
			a->val = xs(v.p ? v.p : "");
		} else {
			a->name = xs("TYPE");
			a->val = xs(v.p ? v.p : "");
		}
		v_add(&pr->pars, a);
	}
	if (p < e && *p == ':')
		p++;
	v.n = 0;
	s_add(&v, p, (size_t)(e - p));
	pr->val = xs(v.p ? v.p : "");
	s_free(&v);
	return pr;
}

/* Parse an iCalendar or vCard text: lines unfolded (a line break followed
   by a space or tab continues the line, for CRLF and bare LF alike),
   BEGIN and END building the tree. The root returned is a holder whose
   kids are the top-level components; a line outside any is dropped. A
   vCard 2.1 quoted-printable value's soft line breaks are joined. */
pm_comp *pm_parse(const char *p, size_t n)
{
	const char *e = p + n, *l, *x;
	pm_comp *root = pm_new("#doc"), *top, *c;
	vec stack = { 0, 0, 0 };
	str line;
	pm_prop *pr;

	v_add(&stack, root);
	s_init(&line);
	while (p < e) {
		line.n = 0;
		for (;;) {
			l = p;
			while (p < e && *p != '\n')
				p++;
			x = p;
			if (x > l && x[-1] == '\r')
				x--;
			s_add(&line, l, (size_t)(x - l));
			if (p < e)
				p++;
			if (line.n && line.p[line.n - 1] == '=' && p < e &&
			    memmem(line.p, line.n, "QUOTED-PRINTABLE", 16)) {
				line.n--;
				continue;
			}
			if (p < e && (*p == ' ' || *p == '\t')) {
				p++;
				continue;
			}
			break;
		}
		if (!line.n)
			continue;
		s_ch(&line, 0);
		line.n--;
		pr = pm_split(line.p, line.n);
		if (!pr)
			continue;
		top = stack.p[stack.n - 1];
		if (!strcmp(pr->name, "BEGIN")) {
			if (stack.n > PM_DEPTH) {
				lg(HIBR_LERR, "pim: components nested deeper than %d", PM_DEPTH);
				pm_propfree(pr);
				break;
			}
			pm_upper(pr->val);
			c = pm_new(pr->val);
			v_add(&top->kids, c);
			v_add(&stack, c);
			pm_propfree(pr);
		} else if (!strcmp(pr->name, "END")) {
			if (stack.n > 1)
				stack.n--;
			pm_propfree(pr);
		} else if (top != root) {
			v_add(&top->props, pr);
		} else {
			pm_propfree(pr);
		}
	}
	s_free(&line);
	v_free(&stack);
	return root;
}

/* The first property of a name in a component, or null. */
pm_prop *pm_get(pm_comp *c, const char *name)
{
	size_t i;

	for (i = 0; c && i < c->props.n; i++)
		if (!strcmp(((pm_prop *)c->props.p[i])->name, name))
			return c->props.p[i];
	return 0;
}

/* A parameter's value, or null. Several of the same name (TYPE=a;TYPE=b)
   give the first; pm_types joins them. */
const char *pm_pget(pm_prop *p, const char *name)
{
	size_t i;

	for (i = 0; p && i < p->pars.n; i++)
		if (!strcmp(((pm_par *)p->pars.p[i])->name, name))
			return ((pm_par *)p->pars.p[i])->val;
	return 0;
}

/* A TEXT value with its escapes taken out: \n and \N a line break, \, \;
   and \\ the character. */
void pm_untext(str *o, const char *p)
{
	for (; *p; p++) {
		if (*p == '\\' && p[1]) {
			p++;
			s_ch(o, *p == 'n' || *p == 'N' ? '\n' : *p);
		} else {
			s_ch(o, *p);
		}
	}
}

/* Text made into a TEXT value: backslash, comma, semicolon and line breaks
   escaped, carriage returns dropped. */
void pm_text(str *o, const char *p)
{
	for (; *p; p++) {
		if (*p == '\\' || *p == ',' || *p == ';') {
			s_ch(o, '\\');
			s_ch(o, *p);
		} else if (*p == '\n') {
			s_cat(o, "\\n");
		} else if (*p != '\r') {
			s_ch(o, *p);
		}
	}
}

/* Append one content line, folded at PM_FOLD octets -- never inside a
   UTF-8 sequence -- and ended with CRLF. */
void pm_lineb(str *o, str *line)
{
	size_t i = 0, w = 0, n = line->n, k;
	const unsigned char *u = (const unsigned char *)(line->p ? line->p : "");

	while (i < n) {
		k = 1;
		if (u[i] >= 0xF0)
			k = 4;
		else if (u[i] >= 0xE0)
			k = 3;
		else if (u[i] >= 0xC0)
			k = 2;
		if (i + k > n)
			k = n - i;
		if (w + k > PM_FOLD) {
			s_cat(o, "\r\n ");
			w = 1;
		}
		s_add(o, (const char *)u + i, k);
		w += k;
		i += k;
	}
	s_cat(o, "\r\n");
}

/* The same, for a line given as a C string. */
void pm_line(str *o, const char *line)
{
	str l;

	s_init(&l);
	s_cat(&l, line);
	pm_lineb(o, &l);
	s_free(&l);
}

/* The whole of a file. */
int pm_slurp(const char *path, str *o)
{
	int fd = open(path, O_RDONLY | O_CLOEXEC);
	char *buf;
	ssize_t r;

	if (fd < 0) {
		lg(HIBR_LERR, "pim: cannot read %s: %s", path, strerror(errno));
		return HIBR_FAIL;
	}
	buf = xm(HIBR_IOCH);
	while ((r = read(fd, buf, HIBR_IOCH)) > 0)
		s_add(o, buf, (size_t)r);
	free(buf);
	close(fd);
	return r < 0 ? HIBR_FAIL : HIBR_OK;
}

/* The text a subcommand works on, from av[i]: -t TEXT, a file, or - (or
   nothing) for standard input. next is the first argument after it. */
int pm_input(sh *s, int ac, char **av, int i, str *o, int *next)
{
	(void)s;
	if (i + 1 < ac && !strcmp(av[i], "-t")) {
		s_cat(o, av[i + 1]);
		*next = i + 2;
		return HIBR_OK;
	}
	if (i < ac && strcmp(av[i], "-")) {
		*next = i + 1;
		return pm_slurp(av[i], o);
	}
	*next = i < ac ? i + 1 : i;
	return pm_slurp("/dev/stdin", o);
}

/* Set $RET at a path of keys. */
void pm_set(sh *s, char **ks, int nk, const char *v)
{
	hibr_setp(s, "RET", ks, nk, v ? v : "");
}

/* The same, for a number. */
void pm_setn(sh *s, char **ks, int nk, long long v)
{
	str b;

	s_init(&b);
	s_num(&b, (long)v);
	hibr_setp(s, "RET", ks, nk, b.p);
	s_free(&b);
}
