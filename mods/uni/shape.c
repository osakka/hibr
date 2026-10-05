#include "un.h"
#include <stdlib.h>
#include <string.h>

/* The joining type of a code point. */
int un_jtype(unsigned c)
{
	return un_rngv(un_join, un_njoin, c, UN_JU);
}

/* The display width of a code point: 0, 1 or 2 columns. */
int un_cw(unsigned c)
{
	return un_rngv(un_width, un_nwidth, c, 1);
}

/* The four presentation forms of a letter, or NULL. */
const unsigned *un_formsof(unsigned c)
{
	size_t lo = 0, hi = un_nforms, mid;

	while (lo < hi) {
		mid = (lo + hi) / 2;
		if (c < un_forms[mid].c)
			hi = mid;
		else if (c > un_forms[mid].c)
			lo = mid + 1;
		else
			return un_forms[mid].f;
	}
	return 0;
}

/* The lam-alef ligature for an alef, 0 if there is none; fin picks the final form. */
unsigned un_ligof(unsigned alef, int fin)
{
	size_t k;

	for (k = 0; k < un_nligs; k++)
		if (un_ligs[k].alef == alef)
			return fin ? un_ligs[k].fin : un_ligs[k].iso;
	return 0;
}

/* The nearest character before i that is not transparent, on the same level; n if none. */
size_t un_jprev(const unsigned *cp, const signed char *lv, size_t n, size_t i)
{
	size_t j = i;

	while (j > 0) {
		j--;
		if (lv && lv[j] != lv[i])
			return n;
		if (un_jtype(cp[j]) != UN_JT)
			return j;
	}
	return n;
}

/* The nearest character after i that is not transparent, on the same level; n if none. */
size_t un_jnext(const unsigned *cp, const signed char *lv, size_t n, size_t i)
{
	size_t j;

	for (j = i + 1; j < n; j++) {
		if (lv && lv[j] != lv[i])
			return n;
		if (un_jtype(cp[j]) != UN_JT)
			return j;
	}
	return n;
}

/* Shape Arabic in place, in logical order: each letter its joining form, lam-alef
   as one ligature. lv, if given, keeps joining within one level. Returns the new
   length; a ligature's alef is removed, and lv with it. */
size_t un_shape(unsigned *cp, signed char *lv, size_t n)
{
	unsigned char *form = xm(n + 1);
	char *gone = xm(n + 1);
	size_t i, p, x, m = 0;
	int jt, jp, jn, bp, bn;
	const unsigned *f;
	unsigned lg;

	memset(form, 0, n + 1);
	memset(gone, 0, n + 1);
	for (i = 0; i < n; i++) {
		jt = un_jtype(cp[i]);
		if (jt != UN_JD && jt != UN_JR && jt != UN_JL)
			continue;
		p = un_jprev(cp, lv, n, i);
		x = un_jnext(cp, lv, n, i);
		jp = p < n ? un_jtype(cp[p]) : UN_JU;
		jn = x < n ? un_jtype(cp[x]) : UN_JU;
		bp = (jt == UN_JD || jt == UN_JR) && (jp == UN_JD || jp == UN_JL || jp == UN_JC);
		bn = (jt == UN_JD || jt == UN_JL) && (jn == UN_JD || jn == UN_JR || jn == UN_JC);
		form[i] = (unsigned char)(bp && bn ? 3 : bp ? 1 : bn ? 2 : 0);
		if (cp[i] == 0x0644 && i + 1 < n && (lg = un_ligof(cp[i + 1], bp))) {
			cp[i] = lg;
			gone[i + 1] = 1;
			form[i] = 4;
		}
	}
	for (i = 0; i < n; i++) {
		if (gone[i])
			continue;
		jt = un_jtype(cp[i]);
		if (form[i] < 4 && (jt == UN_JD || jt == UN_JR || jt == UN_JL) &&
		    (f = un_formsof(cp[i]))) {
			if (f[form[i]])
				cp[i] = f[form[i]];
			else if (form[i] == 3 && f[1])
				cp[i] = f[1];
			else if (form[i] == 2 && f[0])
				cp[i] = f[0];
		}
		cp[m] = cp[i];
		if (lv)
			lv[m] = lv[i];
		m++;
	}
	free(form);
	free(gone);
	return m;
}
