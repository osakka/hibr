#include "un.h"
#include "../uni.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int u8dec(const char *p, size_t n, unsigned *cp);
void u8put(str *o, unsigned c);
int sx_out(sh *s, const char *nm, const char *v);

static const char *un_cname[] = { "L", "R", "AL", "EN", "ES", "ET", "AN", "CS",
	"NSM", "BN", "B", "S", "WS", "ON", "LRE", "LRO", "RLE", "RLO", "PDF", "LRI",
	"RLI", "FSI", "PDI" };

/* Decode UTF-8 into code points; returns how many. */
size_t un_dec(const char *t, size_t n, unsigned **out)
{
	unsigned *cp = xm(sizeof *cp * (n + 1));
	size_t i = 0, m = 0;

	while (i < n)
		i += (size_t)u8dec(t + i, n - i, &cp[m++]);
	*out = cp;
	return m;
}

/* Code points [a, b) of t as one line in display order, the whole text one paragraph, map per character. */
void un_vismap(const char *t, size_t n, size_t a, size_t b, int dir, str *o, int *map)
{
	unsigned *cp, *lc;
	unsigned char *cls;
	signed char *lv, *ll;
	size_t m, m0, i, j, k, no, nl = 0, *ord, *src, *ls;
	int pl, col = 0, lastw = 1, last;

	m = un_dec(t, n, &cp);
	m0 = m;
	if (b > m0)
		b = m0;
	if (a > b)
		a = b;
	cls = xm(m + 1);
	lv = xm(m + 1);
	src = xm(sizeof *src * (m + 1));
	for (i = 0; i < m; i++) {
		cls[i] = (unsigned char)un_class(cp[i]);
		src[i] = i;
	}
	if (map)
		for (i = 0; i <= b - a; i++)
			map[i] = -1;
	pl = un_levels(cls, cp, m, dir, lv);
	for (i = b; i > a; i--) {
		k = cls[i - 1];
		if (k == UN_WS || k == UN_LRI || k == UN_RLI || k == UN_FSI || k == UN_PDI)
			lv[i - 1] = (signed char)pl;
		else if (lv[i - 1] >= 0)
			break;
	}
	last = b > a ? lv[b - 1] : pl;
	for (i = 0; i < m; i++)
		if ((lv[i] & 1) && lv[i] > 0)
			cp[i] = un_mirrorof(cp[i]);
	m = un_shape(cp, lv, src, m);
	lc = xm(sizeof *lc * (m + 1));
	ll = xm(m + 1);
	ls = xm(sizeof *ls * (m + 1));
	ord = xm(sizeof *ord * (m + 1));
	for (i = 0; i < m; i++)
		if (src[i] >= a && src[i] < b) {
			lc[nl] = cp[i];
			ll[nl] = lv[i];
			ls[nl] = src[i] - a;
			nl++;
		}
	un_order(ll, 0, nl, pl, ord, &no);
	o->n = 0;
	for (i = 0; i < no; i++) {
		if ((ll[ord[i]] & 1) && un_cw(lc[ord[i]]) == 0) {
			for (j = i; j < no && un_cw(lc[ord[j]]) == 0 && ll[ord[j]] == ll[ord[i]]; j++)
				;
			if (j < no && ll[ord[j]] == ll[ord[i]]) {
				u8put(o, lc[ord[j]]);
				for (k = j + 1; k > i; k--)
					if (map)
						map[ls[ord[k - 1]]] = col;
				col += un_cw(lc[ord[j]]);
				for (k = j; k > i; k--)
					u8put(o, lc[ord[k - 1]]);
				i = j;
				continue;
			}
		}
		u8put(o, lc[ord[i]]);
		if (map)
			map[ls[ord[i]]] = col;
		if (ls[ord[i]] + 1 == b - a)
			lastw = un_cw(lc[ord[i]]);
		col += un_cw(lc[ord[i]]);
	}
	if (map) {
		for (i = 0; i < b - a; i++)
			if (map[i] < 0)
				map[i] = i ? map[i - 1] : 0;
		map[b - a] = b == a ? 0 : (last & 1) ? map[b - a - 1] : map[b - a - 1] + lastw;
	}
	s_grow(o, 1);
	o->p[o->n] = 0;
	free(cp);
	free(cls);
	free(lv);
	free(src);
	free(lc);
	free(ll);
	free(ls);
	free(ord);
}

/* A text in display order for a cell grid: shaped, mirrored, a mark kept after its base. */
void un_vis(const char *t, size_t n, int dir, str *o)
{
	un_vismap(t, n, 0, (size_t)-1, dir, o, 0);
}

/* Read -d ltr|rtl|auto; returns the next argument's index. */
int un_dir(int ac, char **av, int k, int *dir)
{
	*dir = 2;
	if (k + 1 < ac && !strcmp(av[k], "-d")) {
		*dir = !strcmp(av[k + 1], "ltr") ? 0 : !strcmp(av[k + 1], "rtl") ? 1 : 2;
		return k + 2;
	}
	return k;
}

/* One line of a levels query: dir;input where input is hex code points, or class names with cls. */
void un_lvline(char *l, int cls, str *o)
{
	unsigned char *c;
	unsigned *cp;
	signed char *lv;
	size_t n = 0, cap = 16, i, no, *ord;
	char *tok, *sv = 0, *sc;
	int dir, pl, k;

	sc = strchr(l, ';');
	if (!sc)
		return;
	*sc = 0;
	dir = atoi(l);
	c = xm(cap);
	cp = xm(sizeof *cp * cap);
	for (tok = strtok_r(sc + 1, " ", &sv); tok; tok = strtok_r(0, " ", &sv)) {
		if (n + 1 >= cap) {
			cap *= 2;
			c = xr(c, cap);
			cp = xr(cp, sizeof *cp * cap);
		}
		if (cls) {
			for (k = 0; k < UN_NCLASS && strcmp(tok, un_cname[k]); k++)
				;
			c[n] = (unsigned char)(k < UN_NCLASS ? k : UN_L);
			cp[n] = 0;
		} else {
			cp[n] = (unsigned)strtoul(tok, 0, 16);
			c[n] = (unsigned char)un_class(cp[n]);
		}
		n++;
	}
	lv = xm(n + 1);
	ord = xm(sizeof *ord * (n + 1));
	pl = un_levels(c, cls ? 0 : cp, n, dir, lv);
	un_order(lv, c, n, pl, ord, &no);
	s_num(o, pl);
	s_ch(o, '\t');
	for (i = 0; i < n; i++) {
		if (i)
			s_ch(o, ' ');
		if (lv[i] < 0)
			s_ch(o, 'x');
		else
			s_num(o, lv[i]);
	}
	s_ch(o, '\t');
	for (i = 0; i < no; i++) {
		if (i)
			s_ch(o, ' ');
		s_num(o, (long)ord[i]);
	}
	s_ch(o, '\n');
	free(c);
	free(cp);
	free(lv);
	free(ord);
}

/* uni levels [-c]: dir;input lines on stdin, one plevel TAB levels TAB order line out. */
int un_lvbi(sh *s, int ac, char **av)
{
	str o, l;
	int c, cls = ac > 2 && !strcmp(av[2], "-c");

	(void)s;
	s_init(&o);
	s_init(&l);
	while ((c = getchar()) != EOF) {
		if (c != '\n') {
			s_ch(&l, c);
			continue;
		}
		s_grow(&l, 1);
		l.p[l.n] = 0;
		un_lvline(l.p, cls, &o);
		if (o.n)
			fwrite(o.p, 1, o.n, stdout);
		o.n = 0;
		l.n = 0;
	}
	s_free(&o);
	s_free(&l);
	return HIBR_OK;
}

/* uni vis|shape|width|class|version ... */
int un_bi(sh *s, int ac, char **av)
{
	const char *sub = ac > 1 ? av[1] : "";
	str o;
	unsigned *cp;
	signed char *lv;
	size_t n, i;
	long w = 0;
	int k, dir;

	if (!strcmp(sub, "levels"))
		return un_lvbi(s, ac, av);
	if (!strcmp(sub, "version"))
		return sx_out(s, 0, un_ucdver);
	k = un_dir(ac, av, 2, &dir);
	if (k + 1 != ac || (strcmp(sub, "vis") && strcmp(sub, "shape") &&
			    strcmp(sub, "width") && strcmp(sub, "class"))) {
		lg(HIBR_LERR, "usage: uni vis [-d ltr|rtl|auto] text | shape text | width text | class text | levels [-c] | version");
		return 2;
	}
	s_init(&o);
	if (!strcmp(sub, "vis")) {
		un_vis(av[k], strlen(av[k]), dir, &o);
	} else {
		n = un_dec(av[k], strlen(av[k]), &cp);
		if (!strcmp(sub, "shape")) {
			lv = 0;
			n = un_shape(cp, lv, 0, n);
			for (i = 0; i < n; i++)
				u8put(&o, cp[i]);
		} else if (!strcmp(sub, "width")) {
			for (i = 0; i < n; i++)
				w += un_cw(cp[i]);
			s_num(&o, w);
		} else {
			for (i = 0; i < n; i++) {
				if (i)
					s_ch(&o, ' ');
				s_cat(&o, un_cname[un_class(cp[i])]);
			}
		}
		free(cp);
	}
	s_grow(&o, 1);
	o.p[o.n] = 0;
	k = sx_out(s, 0, o.p);
	s_free(&o);
	return k;
}

/* Whether a text holds a right-to-left character or an explicit direction mark. */
int un_rtl(const char *t, size_t n)
{
	unsigned cp;
	size_t i = 0;
	int c;

	while (i < n) {
		if ((unsigned char)t[i] < 0xC0) {
			i++;
			continue;
		}
		i += (size_t)u8dec(t + i, n - i, &cp);
		c = un_class(cp);
		if (c == UN_R || c == UN_AL || (c >= UN_LRE && c <= UN_PDI))
			return 1;
	}
	return 0;
}

const uni_api un_api = { un_vis, un_cw, un_rtl, un_vismap };

/* Offer "uni" to the modules that draw text. */
int un_init(sh *s)
{
	return hibr_provide(s, "uni", UNI_VER, (void *)&un_api);
}

/* Withdraw it before the module is unloaded. */
void un_fini(sh *s)
{
	hibr_unprovide(s, "uni");
}

const hibr_bi un_bis[] = {
	{ "uni", un_bi, "Unicode text for a cell grid: uni vis|shape|width|class|levels|version" },
	HIBR_BI_END
};

HIBR_MODULE_P("uni", "1.0", "bidirectional text, Arabic shaping and widths, from the UCD",
	      un_bis, un_init, un_fini, "uni");
