#include "pri.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


/* One character's other case, for the scripts whose case pairs are regular: Latin, Greek, Cyrillic, Armenian. */
unsigned u8case(unsigned c, int up)
{
	if (c < 0x80)
		return up ? (unsigned)toupper((int)c) : (unsigned)tolower((int)c);
	if (c == 0x130 || c == 0x131)
		return up ? (c == 0x131 ? 'I' : c) : (c == 0x130 ? 'i' : c);
	if (c >= 0x1C4 && c <= 0x1CC) {
		unsigned b = 0x1C4 + (c - 0x1C4) / 3 * 3;
		return up ? b : b + 2;
	}
	if (up) {
		if (c == 0x3AC)
			return 0x386;
		if (c >= 0x3AD && c <= 0x3AF)
			return c - 0x25;
		if (c == 0x3CC)
			return 0x38C;
		if (c == 0x3CD || c == 0x3CE)
			return c - 0x3F;
		if ((c >= 0xE0 && c <= 0xFE && c != 0xF7) || (c >= 0x3B1 && c <= 0x3CB && c != 0x3C2) ||
		    (c >= 0x430 && c <= 0x44F))
			return c - 0x20;
		if (c == 0xFF)
			return 0x178;
		if (c == 0x3C2)
			return 0x3A3;
		if (c >= 0x450 && c <= 0x45F)
			return c - 0x50;
		if (c >= 0x561 && c <= 0x586)
			return c - 0x30;
		if ((c >= 0x100 && c <= 0x137) || (c >= 0x14A && c <= 0x177) ||
		    (c >= 0x460 && c <= 0x4BF))
			return c & ~1u;
		if ((c >= 0x139 && c <= 0x148) || (c >= 0x179 && c <= 0x17E))
			return (c & 1) ? c : c - 1;
		return c;
	}
	if (c == 0x386)
		return 0x3AC;
	if (c >= 0x388 && c <= 0x38A)
		return c + 0x25;
	if (c == 0x38C)
		return 0x3CC;
	if (c == 0x38E || c == 0x38F)
		return c + 0x3F;
	if ((c >= 0xC0 && c <= 0xDE && c != 0xD7) || (c >= 0x391 && c <= 0x3AB && c != 0x3A2) ||
	    (c >= 0x410 && c <= 0x42F))
		return c + 0x20;
	if (c == 0x178)
		return 0xFF;
	if (c >= 0x400 && c <= 0x40F)
		return c + 0x50;
	if (c >= 0x531 && c <= 0x556)
		return c + 0x30;
	if ((c >= 0x100 && c <= 0x137) || (c >= 0x14A && c <= 0x177) ||
	    (c >= 0x460 && c <= 0x4BF))
		return c | 1u;
	if ((c >= 0x139 && c <= 0x148) || (c >= 0x179 && c <= 0x17E))
		return (c & 1) ? c + 1 : c;
	return c;
}

/* Append one character as UTF-8. */
void u8put(str *o, unsigned c)
{
	if (c < 0x80)
		s_ch(o, (int)c);
	else if (c < 0x800) {
		s_ch(o, (int)(0xC0 | (c >> 6)));
		s_ch(o, (int)(0x80 | (c & 0x3F)));
	} else if (c < 0x10000) {
		s_ch(o, (int)(0xE0 | (c >> 12)));
		s_ch(o, (int)(0x80 | ((c >> 6) & 0x3F)));
		s_ch(o, (int)(0x80 | (c & 0x3F)));
	} else {
		s_ch(o, (int)(0xF0 | (c >> 18)));
		s_ch(o, (int)(0x80 | ((c >> 12) & 0x3F)));
		s_ch(o, (int)(0x80 | ((c >> 6) & 0x3F)));
		s_ch(o, (int)(0x80 | (c & 0x3F)));
	}
}

/* Append text in upper or lower case, all of it or only its first character; bytes that are not UTF-8 pass through. */
void u8cased(str *o, const char *v, int up, int first)
{
	size_t n = strlen(v), i = 0;
	unsigned c;
	int k;

	while (i < n) {
		k = u8dec(v + i, n - i, &c);
		if (k == 1 && (unsigned char)v[i] >= 0x80)
			s_ch(o, v[i]);
		else
			u8put(o, u8case(c, up));
		i += (size_t)k;
		if (first) {
			s_add(o, v + i, n - i);
			break;
		}
	}
}

/* Change the case of each character the pattern matches, or of the first
   alone, as ${x^^pat} and ${x^pat} do. */
void u8casep(str *o, const char *v, int up, int first, const char *pat)
{
	size_t n = strlen(v), i = 0;
	unsigned c;
	int k;
	str ch;

	s_init(&ch);
	while (i < n) {
		k = u8dec(v + i, n - i, &c);
		ch.n = 0;
		s_add(&ch, v + i, (size_t)k);
		if (!(k == 1 && (unsigned char)v[i] >= 0x80) && gmatch(pat, ch.p))
			u8put(o, u8case(c, up));
		else
			s_add(o, v + i, (size_t)k);
		i += (size_t)k;
		if (first) {
			s_add(o, v + i, n - i);
			break;
		}
	}
	s_free(&ch);
}

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

/* Run one call of arr map as `x := call` runs: a builtin fills the slot quietly, a function only through ret, a program with what it prints. */
void sx_bind1(sh *s, const char *src)
{
	amark m = ar_mark(s->ar), xm2 = ar_mark(s->xa);
	int okeep = s->keep, ob = s->bind, more = 0;
	node *n;

	s->keep = 0;
	v_del(s, "RET");
	hibr_set(s, "RET", "", 0);
	n = hibr_parse(s, src, &more);
	if (n && n->k == N_CMD && !n->f)
		n->f = 2;
	else if (n)
		lg(HIBR_LDBG, "arr map: %s is not one command, so it runs as written", src);
	if (n && !s->noexec)
		ex(s, n);
	if (!s->keep) {
		ar_rel(s->xa, xm2);
		ar_rel(s->ar, m);
	}
	s->keep = okeep || s->keep;
	s->bind = ob;
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
		lg(HIBR_LERR, "usage: str len|width|upper|lower|trim|slice|index|replace|split|join|pad|starts|ends|contains|repeat ...");
		return 2;
	}
	s_init(&o);
	if (!strcmp(sub, "len")) {
		rc = sx_num(s, ac > 3 ? av[3] : 0, (long)u8n(a, strlen(a)));
	} else if (!strcmp(sub, "width")) {
		rc = sx_num(s, ac > 3 ? av[3] : 0, (long)ed_width(a));
	} else if (!strcmp(sub, "upper") || !strcmp(sub, "lower")) {
		u8cased(&o, a, sub[0] == 'u', 0);
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
		size_t bn = strlen(a), bo, be;
		n = u8n(a, bn);
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
		bo = u8off(a, bn, (size_t)st);
		be = u8off(a, bn, (size_t)(st + ln));
		s_add(&o, a + bo, be - bo);
		rc = sx_out(s, ac > 5 ? av[5] : 0, o.p ? o.p : "");
	} else if (!strcmp(sub, "index")) {
		char *h = ac > 3 ? strstr(a, av[3]) : 0;
		rc = sx_num(s, ac > 4 ? av[4] : 0,
			    h ? (long)u8n(a, (size_t)(h - a)) : -1L);
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
		const char *pc = ac > 4 && av[4][0] ? av[4] : " ";
		size_t cl = sx_u8len(pc);
		long need, cw;
		str pad;
		char *one = xm(cl + 1);
		memcpy(one, pc, cl);
		one[cl] = 0;
		cw = ed_width(one);
		if (cw < 1)
			cw = 1;
		free(one);
		n = (size_t)ed_width(a);
		need = (w < 0 ? -w : w) - (long)n;
		if (need < 0)
			need = 0;
		s_init(&pad);
		for (k = 0; k + cw <= need; k += cw)
			s_add(&pad, pc, cl);
		for (; k < need; k++)
			s_ch(&pad, ' ');
		if (w < 0 && pad.n)
			s_add(&o, pad.p, pad.n);
		s_cat(&o, a);
		if (w > 0 && pad.n)
			s_add(&o, pad.p, pad.n);
		s_free(&pad);
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

/* The byte length of the UTF-8 character at p, 1 for a byte that does not begin one. */
size_t sx_u8len(const char *p)
{
	unsigned char c = (unsigned char)*p;
	size_t n = c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC0 ? 2 : 1, i;

	for (i = 1; i < n; i++)
		if (((unsigned char)p[i] & 0xC0) != 0x80)
			return 1;
	return n;
}

/* Compare two strings for sorting. */
int ax_cmp(const void *a, const void *b)
{
	return strcmp(*(char *const *)a, *(char *const *)b);
}

/* Compare two strings numerically, and equal numbers as text. */
int ax_ncmp(const void *a, const void *b)
{
	double x = atof(*(char *const *)a), y = atof(*(char *const *)b);

	return x < y ? -1 : x > y ? 1 : ax_cmp(a, b);
}

/* The first of each distinct value in order, found through a hash of them rather than by comparing each with all before it (Gitea #464). */
void arr_uniq(vec *in, vec *out)
{
	size_t cap, i, h;
	char **tab;

	for (cap = 16; cap < in->n * 2; cap <<= 1)
		;
	tab = xm(cap * sizeof *tab);
	memset(tab, 0, cap * sizeof *tab);
	for (i = 0; i < in->n; i++) {
		for (h = vh((char *)in->p[i]) & (cap - 1); tab[h];
		     h = (h + 1) & (cap - 1))
			if (!strcmp(tab[h], (char *)in->p[i]))
				break;
		if (tab[h])
			continue;
		tab[h] = (char *)in->p[i];
		v_add(out, in->p[i]);
	}
	free(tab);
}

/* arr: a write the variable refused -- it is readonly -- fails the
   command rather than being reported and answering 0 (Gitea #457). */
int b_arr(sh *s, int ac, char **av)
{
	int r;

	v_refused = 0;
	r = b_arr1(s, ac, av);
	if (v_refused && r == HIBR_OK)
		r = HIBR_FAIL;
	v_refused = 0;
	return r;
}

/* Array operations that keep the data in this process. */
int b_arr1(sh *s, int ac, char **av)
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
	if (!strcmp(sub, "len")) {
		vals = vb_get(s);
		v_list(s, nm, 0, 0, vals, 0);
		rc = sx_num(s, ac > 3 ? av[3] : 0, (long)vals->n);
		vb_put(s, vals);
		return rc;
	}
	if (!strcmp(sub, "contains")) {
		vals = vb_get(s);
		v_list(s, nm, 0, 0, vals, 0);
		rc = HIBR_FAIL;
		for (i = 0; i < vals->n && ac > 3; i++)
			if (!strcmp((char *)vals->p[i], av[3])) {
				rc = HIBR_OK;
				break;
			}
		vb_put(s, vals);
		return rc;
	}
	if (!strcmp(sub, "push")) {
		var *v;
		long next;
		const char *vn = nm;
		if ((v = v_findr(s, &vn)) && v->ro) {
			lg(HIBR_LERR, "%s: readonly variable", nm);
			return HIBR_FAIL;
		}
		next = v_arrnext(s, nm);
		if (ac == 3 && !v_find(s, nm)) {
			vec empty = { 0, 0, 0 };
			v_arr(s, nm, &empty);
		}
		for (i = 3; i < (size_t)ac; i++)
			v_setel(s, nm, next++, av[i]);
		lg(HIBR_LDBG, "arr push appends after the highest key, keeping the others (Gitea #464)");
		return HIBR_OK;
	}
	vals = vb_get(s);
	v_list(s, nm, 0, 0, vals, 0);
	for (i = 0; i < vals->n; i++)
		vals->p[i] = xs((char *)vals->p[i]);
	if (!strcmp(sub, "pop")) {
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
		arr_uniq(vals, out);
		v_arr(s, nm, out);
		vb_put(s, out);
	} else if (!strcmp(sub, "map") || !strcmp(sub, "filter")) {
		vec *out = vb_get(s);
		str cmd;
		int keep = !strcmp(sub, "filter");
		if (ac < 4) {
			lg(HIBR_LERR, "usage: arr %s var command [arg...]", sub);
			vb_put(s, out);
			rc = 2;
			goto done;
		}
		for (i = 0; i < vals->n; i++) {
			size_t j;
			s_init(&cmd);
			s_cat(&cmd, av[3]);
			s_ch(&cmd, ' ');
			for (j = 4; j < (size_t)ac; j++) {
				s_sq(&cmd, av[j]);
				s_ch(&cmd, ' ');
			}
			s_sq(&cmd, (char *)vals->p[i]);
			if (keep) {
				hibr_run(s, cmd.p);
				if (s->st == 0)
					v_add(out, xs((char *)vals->p[i]));
			} else {
				const char *r;
				sx_bind1(s, cmd.p);
				r = hibr_get(s, "RET");
				v_add(out, xs(r ? r : ""));
			}
			s_free(&cmd);
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
