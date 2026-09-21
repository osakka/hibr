#include "pri.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Deliver a result to a variable, or to standard output. */
int sx_out(sh *s, const char *nm, const char *v)
{
	hibr_set(s, "RET", v, 0);
	if (nm)
		hibr_set(s, nm, v, 0);
	else if (!s->bind)
		printf("%s\n", v);
	return HIBR_OK;
}

/* Deliver a number the same way. */
int sx_num(sh *s, const char *nm, long n)
{
	str b;
	int rc;

	s_init(&b);
	s_num(&b, n);
	rc = sx_out(s, nm, b.p);
	s_free(&b);
	return rc;
}

/* Text operations that would otherwise cost a pipeline. */
int b_str(sh *s, int ac, char **av)
{
	const char *sub = ac > 1 ? av[1] : "";
	const char *a = ac > 2 ? av[2] : "";
	str o;
	size_t i, n;
	long k;
	int rc = HIBR_OK;

	if (ac < 3) {
		lg(HIBR_LERR, "usage: str len|upper|lower|trim|slice|index|replace|split|join|pad|starts|ends|contains|repeat ...");
		return 2;
	}
	s_init(&o);
	if (!strcmp(sub, "len")) {
		rc = sx_num(s, ac > 3 ? av[3] : 0, (long)strlen(a));
	} else if (!strcmp(sub, "upper") || !strcmp(sub, "lower")) {
		int up = sub[0] == 'u';
		s_cat(&o, a);
		for (i = 0; i < o.n; i++)
			o.p[i] = (char)(up ? toupper((unsigned char)o.p[i]) :
					     tolower((unsigned char)o.p[i]));
		rc = sx_out(s, ac > 3 ? av[3] : 0, o.p ? o.p : "");
	} else if (!strcmp(sub, "trim")) {
		const char *b = a;
		const char *e = a + strlen(a);
		while (*b == ' ' || *b == '\t' || *b == '\n' || *b == '\r')
			b++;
		while (e > b && (e[-1] == ' ' || e[-1] == '\t' ||
				 e[-1] == '\n' || e[-1] == '\r'))
			e--;
		s_add(&o, b, (size_t)(e - b));
		rc = sx_out(s, ac > 3 ? av[3] : 0, o.p ? o.p : "");
	} else if (!strcmp(sub, "slice")) {
		long st = ac > 3 ? atol(av[3]) : 0;
		long ln;
		n = strlen(a);
		if (st < 0)
			st += (long)n;
		if (st < 0)
			st = 0;
		ln = ac > 4 && av[4][0] != '-' ? atol(av[4]) :
					   (long)n - st;
		if (st > (long)n)
			st = (long)n;
		if (ln > (long)n - st)
			ln = (long)n - st;
		if (ln < 0)
			ln = 0;
		s_add(&o, a + st, (size_t)ln);
		rc = sx_out(s, ac > 5 ? av[5] : 0, o.p ? o.p : "");
	} else if (!strcmp(sub, "index")) {
		char *h = ac > 3 ? strstr(a, av[3]) : 0;
		rc = sx_num(s, ac > 4 ? av[4] : 0, h ? (long)(h - a) : -1L);
	} else if (!strcmp(sub, "replace")) {
		const char *p = a, *h;
		size_t ol = ac > 3 ? strlen(av[3]) : 0;
		if (!ol || ac < 5) {
			lg(HIBR_LERR, "usage: str replace text old new [var]");
			s_free(&o);
			return 2;
		}
		while ((h = strstr(p, av[3]))) {
			s_add(&o, p, (size_t)(h - p));
			s_cat(&o, av[4]);
			p = h + ol;
		}
		s_cat(&o, p);
		rc = sx_out(s, ac > 5 ? av[5] : 0, o.p ? o.p : "");
	} else if (!strcmp(sub, "split")) {
		vec *parts;
		const char *sep = ac > 3 ? av[3] : " ";
		const char *p = a, *h;
		size_t sl = strlen(sep);
		if (ac < 5) {
			lg(HIBR_LERR, "usage: str split text sep arrayvar");
			s_free(&o);
			return 2;
		}
		parts = vb_get(s);
		if (!sl) {
			lg(HIBR_LERR, "str split: empty separator");
			vb_put(s, parts);
			s_free(&o);
			return 2;
		}
		for (;;) {
			h = strstr(p, sep);
			o.n = 0;
			if (!h) {
				s_cat(&o, p);
				v_add(parts, xs(o.p ? o.p : ""));
				break;
			}
			s_add(&o, p, (size_t)(h - p));
			v_add(parts, xs(o.p ? o.p : ""));
			p = h + sl;
		}
		v_arr(s, av[4], parts);
		for (i = 0; i < parts->n; i++)
			free(parts->p[i]);
		vb_put(s, parts);
	} else if (!strcmp(sub, "join")) {
		vec *vals = vb_get(s);
		const char *sep = ac > 3 ? av[3] : " ";
		v_list(s, av[2], 0, 0, vals, 0);
		for (i = 0; i < vals->n; i++) {
			if (i)
				s_cat(&o, sep);
			s_cat(&o, (char *)vals->p[i]);
		}
		vb_put(s, vals);
		rc = sx_out(s, ac > 4 ? av[4] : 0, o.p ? o.p : "");
	} else if (!strcmp(sub, "pad")) {
		long w = ac > 3 ? atol(av[3]) : 0;
		char c = ac > 4 && av[4][0] ? av[4][0] : ' ';
		long need;
		n = strlen(a);
		need = (w < 0 ? -w : w) - (long)n;
		if (need < 0)
			need = 0;
		if (w < 0)
			for (k = 0; k < need; k++)
				s_ch(&o, c);
		s_cat(&o, a);
		if (w > 0)
			for (k = 0; k < need; k++)
				s_ch(&o, c);
		rc = sx_out(s, ac > 5 ? av[5] : 0, o.p ? o.p : "");
	} else if (!strcmp(sub, "repeat")) {
		long t = ac > 3 ? atol(av[3]) : 0;
		for (k = 0; k < t; k++)
			s_cat(&o, a);
		rc = sx_out(s, ac > 4 ? av[4] : 0, o.p ? o.p : "");
	} else if (!strcmp(sub, "starts") || !strcmp(sub, "ends") ||
		   !strcmp(sub, "contains")) {
		size_t al = strlen(a), bl = ac > 3 ? strlen(av[3]) : 0;
		int hit;
		if (!strcmp(sub, "starts"))
			hit = bl <= al && !memcmp(a, av[3], bl);
		else if (!strcmp(sub, "ends"))
			hit = bl <= al && !memcmp(a + al - bl, av[3], bl);
		else
			hit = strstr(a, av[3]) != 0;
		s_free(&o);
		return hit ? HIBR_OK : HIBR_FAIL;
	} else {
		lg(HIBR_LERR, "str: %s: unknown operation", sub);
		s_free(&o);
		return 2;
	}
	s_free(&o);
	return rc;
}

/* Compare two strings for sorting. */
int ax_cmp(const void *a, const void *b)
{
	return strcmp(*(char *const *)a, *(char *const *)b);
}

/* Compare two strings numerically. */
int ax_ncmp(const void *a, const void *b)
{
	double x = atof(*(char *const *)a), y = atof(*(char *const *)b);

	return x < y ? -1 : x > y ? 1 : 0;
}

/* Quote an argument for a nested command. */
void ax_q(str *o, const char *a)
{
	s_ch(o, '\'');
	for (; *a; a++) {
		if (*a == '\'')
			s_cat(o, "'\\''");
		else
			s_ch(o, *a);
	}
	s_ch(o, '\'');
}

/* Array operations that keep the data in this process. */
int b_arr(sh *s, int ac, char **av)
{
	const char *sub = ac > 1 ? av[1] : "";
	const char *nm = ac > 2 ? av[2] : "";
	vec *vals;
	size_t i;
	int rc = HIBR_OK;

	if (ac < 3) {
		lg(HIBR_LERR, "usage: arr len|push|pop|sort|uniq|reverse|contains|map|filter var ...");
		return 2;
	}
	vals = vb_get(s);
	v_list(s, nm, 0, 0, vals, 0);
	for (i = 0; i < vals->n; i++)
		vals->p[i] = xs((char *)vals->p[i]);
	if (!strcmp(sub, "len")) {
		rc = sx_num(s, ac > 3 ? av[3] : 0, (long)vals->n);
	} else if (!strcmp(sub, "push")) {
		for (i = 3; i < (size_t)ac; i++)
			v_add(vals, xs(av[i]));
		v_arr(s, nm, vals);
	} else if (!strcmp(sub, "pop")) {
		if (!vals->n) {
			rc = HIBR_FAIL;
		} else {
			char *last = (char *)vals->p[--vals->n];
			sx_out(s, ac > 3 ? av[3] : 0, last);
			free(last);
			v_arr(s, nm, vals);
		}
	} else if (!strcmp(sub, "sort")) {
		int num = 0, rev = 0;
		for (i = 3; i < (size_t)ac; i++) {
			if (!strcmp(av[i], "-n"))
				num = 1;
			if (!strcmp(av[i], "-r"))
				rev = 1;
		}
		if (vals->n > 1)
			qsort(vals->p, vals->n, sizeof *vals->p,
			      num ? ax_ncmp : ax_cmp);
		if (rev)
			for (i = 0; i < vals->n / 2; i++) {
				void *t = vals->p[i];
				vals->p[i] = vals->p[vals->n - 1 - i];
				vals->p[vals->n - 1 - i] = t;
			}
		v_arr(s, nm, vals);
	} else if (!strcmp(sub, "reverse")) {
		for (i = 0; i < vals->n / 2; i++) {
			void *t = vals->p[i];
			vals->p[i] = vals->p[vals->n - 1 - i];
			vals->p[vals->n - 1 - i] = t;
		}
		v_arr(s, nm, vals);
	} else if (!strcmp(sub, "uniq")) {
		vec *out = vb_get(s);
		size_t j;
		for (i = 0; i < vals->n; i++) {
			for (j = 0; j < out->n; j++)
				if (!strcmp((char *)out->p[j], (char *)vals->p[i]))
					break;
			if (j == out->n)
				v_add(out, vals->p[i]);
		}
		v_arr(s, nm, out);
		vb_put(s, out);
	} else if (!strcmp(sub, "contains")) {
		int hit = 0;
		for (i = 0; i < vals->n && ac > 3; i++)
			if (!strcmp((char *)vals->p[i], av[3]))
				hit = 1;
		rc = hit ? HIBR_OK : HIBR_FAIL;
	} else if (!strcmp(sub, "map") || !strcmp(sub, "filter")) {
		vec *out = vb_get(s);
		str cmd;
		int keep = !strcmp(sub, "filter");
		if (ac < 4) {
			lg(HIBR_LERR, "usage: arr %s var command", sub);
			vb_put(s, out);
			rc = 2;
			goto done;
		}
		for (i = 0; i < vals->n; i++) {
			s_init(&cmd);
			s_cat(&cmd, av[3]);
			s_ch(&cmd, ' ');
			ax_q(&cmd, (char *)vals->p[i]);
			hibr_run(s, cmd.p);
			s_free(&cmd);
			if (keep) {
				if (s->st == 0)
					v_add(out, xs((char *)vals->p[i]));
			} else {
				const char *r = hibr_get(s, "RET");
				v_add(out, xs(r ? r : ""));
			}
		}
		v_arr(s, nm, out);
		for (i = 0; i < out->n; i++)
			free(out->p[i]);
		vb_put(s, out);
	} else {
		lg(HIBR_LERR, "arr: %s: unknown operation", sub);
		rc = 2;
	}
done:
	for (i = 0; i < vals->n; i++)
		free(vals->p[i]);
	vb_put(s, vals);
	return rc;
}
