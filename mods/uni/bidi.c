#include "un.h"
#include <stdlib.h>
#include <string.h>

typedef struct un_ent un_ent;
struct un_ent {
	signed char lv, ovr, iso;
};

/* A value from a sorted range table, or dflt for a code point outside it. */
int un_rngv(const un_rng *t, size_t n, unsigned c, int dflt)
{
	size_t lo = 0, hi = n, mid;

	while (lo < hi) {
		mid = (lo + hi) / 2;
		if (c < t[mid].lo)
			hi = mid;
		else if (c > t[mid].hi)
			lo = mid + 1;
		else
			return t[mid].v;
	}
	return dflt;
}

/* The bidi class of a code point. */
int un_class(unsigned c)
{
	return un_rngv(un_bidi, un_nbidi, c, UN_L);
}

/* The mirrored form of a code point, or the code point itself. */
unsigned un_mirrorof(unsigned c)
{
	size_t lo = 0, hi = un_nmirror, mid;

	while (lo < hi) {
		mid = (lo + hi) / 2;
		if (c < un_mirror[mid].a)
			hi = mid;
		else if (c > un_mirror[mid].a)
			lo = mid + 1;
		else
			return un_mirror[mid].b;
	}
	return c;
}

/* 1 for an opening bracket, 2 for a closing one, 0 otherwise; pair set to its partner. */
int un_bracket(unsigned c, unsigned *pair)
{
	size_t lo = 0, hi = un_nbrack, mid;

	while (lo < hi) {
		mid = (lo + hi) / 2;
		if (c < un_brack[mid].a)
			hi = mid;
		else if (c > un_brack[mid].a)
			lo = mid + 1;
		else {
			*pair = un_brack[mid].b;
			return un_brack[mid].open ? 1 : 2;
		}
	}
	return 0;
}

/* Whether a class is removed by rule X9. */
int un_removed(int c)
{
	return c == UN_RLE || c == UN_LRE || c == UN_RLO || c == UN_LRO ||
	       c == UN_PDF || c == UN_BN;
}

/* Whether a class is an isolate initiator. */
int un_isinit(int c)
{
	return c == UN_LRI || c == UN_RLI || c == UN_FSI;
}

/* Whether a class is a neutral or isolate for rules N1 and N2. */
int un_isni(int c)
{
	return c == UN_B || c == UN_S || c == UN_WS || c == UN_ON || c == UN_LRI ||
	       c == UN_RLI || c == UN_FSI || c == UN_PDI;
}

/* The matching PDI of the isolate initiator at i, before end; or end. */
size_t un_pdi(const unsigned char *cls, size_t i, size_t end)
{
	int d = 1;

	for (i++; i < end; i++) {
		if (un_isinit(cls[i]))
			d++;
		else if (cls[i] == UN_PDI && --d == 0)
			return i;
		else if (cls[i] == UN_B)
			break;
	}
	return end;
}

/* Rules P2 and P3 over [a, end): 1 for right to left, 0 otherwise, -1 if no strong one. */
int un_firststrong(const unsigned char *cls, size_t a, size_t end)
{
	size_t i;

	for (i = a; i < end; i++) {
		if (cls[i] == UN_L)
			return 0;
		if (cls[i] == UN_R || cls[i] == UN_AL)
			return 1;
		if (un_isinit(cls[i]))
			i = un_pdi(cls, i, end);
		else if (cls[i] == UN_B)
			break;
	}
	return -1;
}

/* The strong direction of a resolved type for N0 and N1: 0 L, 1 R, -1 neither. */
int un_strong(int t)
{
	if (t == UN_L)
		return 0;
	if (t == UN_R || t == UN_AL || t == UN_EN || t == UN_AN)
		return 1;
	return -1;
}

/* Whether two bracket code points pair, counting the canonical equivalents. */
int un_brkeq(unsigned a, unsigned b)
{
	if (a == 0x232A)
		a = 0x3009;
	if (b == 0x232A)
		b = 0x3009;
	if (a == 0x2329)
		a = 0x3008;
	if (b == 0x2329)
		b = 0x3008;
	return a == b;
}

/* Resolve one isolating run sequence s of m positions: rules W1 to I2. */
void un_seq(unsigned char *t, const unsigned char *cls, const unsigned *cp,
	    signed char *lv, size_t *s, size_t m, int sos, int eos)
{
	size_t k, j, a, b, np = 0, ns = 0;
	int lev = lv[s[0]], e = lev & 1, prev, c, found, d, x;
	size_t *op = 0, *cpos = 0, (*st)[2] = 0;
	unsigned pr;

	for (k = 0; k < m; k++) {
		if (t[s[k]] != UN_NSM)
			continue;
		if (!k)
			t[s[k]] = (unsigned char)(sos ? UN_R : UN_L);
		else if (un_isinit(t[s[k - 1]]) || t[s[k - 1]] == UN_PDI)
			t[s[k]] = UN_ON;
		else
			t[s[k]] = t[s[k - 1]];
	}
	prev = sos ? UN_R : UN_L;
	for (k = 0; k < m; k++) {
		c = t[s[k]];
		if (c == UN_L || c == UN_R || c == UN_AL)
			prev = c;
		else if (c == UN_EN && prev == UN_AL)
			t[s[k]] = UN_AN;
	}
	for (k = 0; k < m; k++)
		if (t[s[k]] == UN_AL)
			t[s[k]] = UN_R;
	for (k = 1; k + 1 < m; k++) {
		c = t[s[k]];
		if (c == UN_ES && t[s[k - 1]] == UN_EN && t[s[k + 1]] == UN_EN)
			t[s[k]] = UN_EN;
		else if (c == UN_CS && t[s[k - 1]] == UN_EN && t[s[k + 1]] == UN_EN)
			t[s[k]] = UN_EN;
		else if (c == UN_CS && t[s[k - 1]] == UN_AN && t[s[k + 1]] == UN_AN)
			t[s[k]] = UN_AN;
	}
	for (k = 0; k < m; k++) {
		if (t[s[k]] != UN_ET)
			continue;
		for (j = k; j < m && t[s[j]] == UN_ET; j++)
			;
		if ((k && t[s[k - 1]] == UN_EN) || (j < m && t[s[j]] == UN_EN))
			for (a = k; a < j; a++)
				t[s[a]] = UN_EN;
		k = j - 1;
	}
	for (k = 0; k < m; k++)
		if (t[s[k]] == UN_ES || t[s[k]] == UN_ET || t[s[k]] == UN_CS)
			t[s[k]] = UN_ON;
	prev = sos ? UN_R : UN_L;
	for (k = 0; k < m; k++) {
		c = t[s[k]];
		if (c == UN_L || c == UN_R)
			prev = c;
		else if (c == UN_EN && prev == UN_L)
			t[s[k]] = UN_L;
	}
	if (cp) {
		st = xm(sizeof *st * (UN_MAXBRACK + 1));
		op = xm(sizeof *op * (m + 1));
		cpos = xm(sizeof *cpos * (m + 1));
		for (k = 0; k < m; k++) {
			if (t[s[k]] != UN_ON)
				continue;
			x = un_bracket(cp[s[k]], &pr);
			if (x == 1) {
				if (ns == UN_MAXBRACK)
					break;
				st[ns][0] = k;
				st[ns][1] = pr;
				ns++;
			} else if (x == 2) {
				for (j = ns; j > 0; j--)
					if (un_brkeq(cp[s[k]], (unsigned)st[j - 1][1]))
						break;
				if (j) {
					op[np] = st[j - 1][0];
					cpos[np] = k;
					np++;
					ns = j - 1;
				}
			}
		}
		for (a = 0; a < np; a++)
			for (b = a + 1; b < np; b++)
				if (op[b] < op[a]) {
					k = op[a]; op[a] = op[b]; op[b] = k;
					k = cpos[a]; cpos[a] = cpos[b]; cpos[b] = k;
				}
		for (a = 0; a < np; a++) {
			found = -1;
			for (k = op[a] + 1; k < cpos[a]; k++) {
				d = un_strong(t[s[k]]);
				if (d == e) {
					found = e;
					break;
				}
				if (d >= 0)
					found = d;
			}
			if (found < 0)
				continue;
			if (found != e) {
				d = sos;
				for (k = op[a]; k > 0; k--)
					if ((x = un_strong(t[s[k - 1]])) >= 0) {
						d = x;
						break;
					}
				found = d == found ? found : e;
			}
			x = found ? UN_R : UN_L;
			t[s[op[a]]] = (unsigned char)x;
			t[s[cpos[a]]] = (unsigned char)x;
			for (k = op[a] + 1; k < m && cls[s[k]] == UN_NSM; k++)
				t[s[k]] = (unsigned char)x;
			for (k = cpos[a] + 1; k < m && cls[s[k]] == UN_NSM; k++)
				t[s[k]] = (unsigned char)x;
		}
		free(st);
		free(op);
		free(cpos);
	}
	for (k = 0; k < m; k++) {
		if (!un_isni(t[s[k]]))
			continue;
		for (j = k; j < m && un_isni(t[s[j]]); j++)
			;
		a = k ? (size_t)un_strong(t[s[k - 1]]) : (size_t)sos;
		b = j < m ? (size_t)un_strong(t[s[j]]) : (size_t)eos;
		d = a == b ? (int)a : e;
		for (x = (int)k; x < (int)j; x++)
			t[s[x]] = (unsigned char)(d ? UN_R : UN_L);
		k = j - 1;
	}
	for (k = 0; k < m; k++) {
		c = t[s[k]];
		if (!(lev & 1)) {
			if (c == UN_R)
				lv[s[k]] = (signed char)(lev + 1);
			else if (c == UN_AN || c == UN_EN)
				lv[s[k]] = (signed char)(lev + 2);
		} else if (c == UN_L || c == UN_EN || c == UN_AN)
			lv[s[k]] = (signed char)(lev + 1);
	}
}

/* Resolve one paragraph [a, end) at level plev: X1 to I2 and L1. */
void un_para(const unsigned char *cls, const unsigned *cp, size_t a, size_t end,
	     int plev, signed char *lv, unsigned char *t)
{
	un_ent *stk = xm(sizeof *stk * (UN_MAXDEPTH + 2));
	size_t *keep = xm(sizeof *keep * (end - a + 1)), *seq = xm(sizeof *seq * (end - a + 1));
	size_t *runs = xm(sizeof *runs * (end - a + 2)), *mpdi = xm(sizeof *mpdi * (end - a + 1));
	char *cont = xm(end - a + 1);
	signed char *xl = xm(end - a + 1);
	size_t i, nk = 0, nr = 0, top = 0, r, k, m, first, last, q;
	int oi = 0, oe = 0, vi = 0, c, nl, d, sos, eos, pl, ql;

	stk[0].lv = (signed char)plev;
	stk[0].ovr = -1;
	stk[0].iso = 0;
	for (i = a; i < end; i++) {
		c = cls[i];
		t[i] = (unsigned char)c;
		if (c == UN_RLE || c == UN_LRE || c == UN_RLO || c == UN_LRO) {
			lv[i] = stk[top].lv;
			nl = (c == UN_RLE || c == UN_RLO) ? ((stk[top].lv + 1) | 1)
							   : ((stk[top].lv + 2) & ~1);
			if (nl <= UN_MAXDEPTH && !oi && !oe) {
				top++;
				stk[top].lv = (signed char)nl;
				stk[top].ovr = (signed char)(c == UN_RLO ? UN_R : c == UN_LRO ? UN_L : -1);
				stk[top].iso = 0;
			} else if (!oi)
				oe++;
		} else if (un_isinit(c)) {
			if (c == UN_FSI)
				c = un_firststrong(cls, i + 1, un_pdi(cls, i, end)) == 1 ? UN_RLI : UN_LRI;
			lv[i] = stk[top].lv;
			if (stk[top].ovr >= 0)
				t[i] = (unsigned char)stk[top].ovr;
			nl = c == UN_RLI ? ((stk[top].lv + 1) | 1) : ((stk[top].lv + 2) & ~1);
			if (nl <= UN_MAXDEPTH && !oi && !oe) {
				vi++;
				top++;
				stk[top].lv = (signed char)nl;
				stk[top].ovr = -1;
				stk[top].iso = 1;
			} else
				oi++;
		} else if (c == UN_PDI) {
			if (oi)
				oi--;
			else if (vi) {
				oe = 0;
				while (!stk[top].iso)
					top--;
				top--;
				vi--;
			}
			lv[i] = stk[top].lv;
			if (stk[top].ovr >= 0)
				t[i] = (unsigned char)stk[top].ovr;
		} else if (c == UN_PDF) {
			lv[i] = stk[top].lv;
			if (oi)
				;
			else if (oe)
				oe--;
			else if (!stk[top].iso && top)
				top--;
		} else if (c == UN_B) {
			lv[i] = (signed char)plev;
		} else if (c == UN_BN) {
			lv[i] = stk[top].lv;
		} else {
			lv[i] = stk[top].lv;
			if (stk[top].ovr >= 0)
				t[i] = (unsigned char)stk[top].ovr;
		}
	}
	for (i = a; i < end; i++)
		if (!un_removed(cls[i]))
			keep[nk++] = i;
	for (k = 0; k < nk; k++) {
		mpdi[k] = nk;
		cont[k] = 0;
	}
	for (k = 0; k < nk; k++) {
		if (!un_isinit(cls[keep[k]]))
			continue;
		q = un_pdi(cls, keep[k], end);
		if (q < end)
			for (m = k + 1; m < nk; m++)
				if (keep[m] == q) {
					mpdi[k] = m;
					cont[m] = 1;
					break;
				}
	}
	for (k = 0; k < nk; k++)
		xl[k] = lv[keep[k]];
	for (k = 0; k < nk; k++)
		if (!k || xl[k] != xl[k - 1])
			runs[nr++] = k;
	runs[nr] = nk;
	for (r = 0; r < nr; r++) {
		if (cont[runs[r]])
			continue;
		m = 0;
		q = r;
		for (;;) {
			for (k = runs[q]; k < runs[q + 1]; k++)
				seq[m++] = keep[k];
			last = runs[q + 1] - 1;
			if (!un_isinit(cls[keep[last]]) || mpdi[last] >= nk)
				break;
			for (q = 0; q < nr && runs[q] != mpdi[last]; q++)
				;
			if (q >= nr)
				break;
		}
		first = runs[r];
		ql = xl[first];
		pl = first ? xl[first - 1] : plev;
		sos = (ql > pl ? ql : pl) & 1;
		if (un_isinit(cls[seq[m - 1]]) && un_pdi(cls, seq[m - 1], end) >= end)
			d = plev;
		else if (un_isinit(cls[seq[m - 1]]))
			d = plev;
		else {
			for (k = 0; k < nk && keep[k] != seq[m - 1]; k++)
				;
			d = k + 1 < nk ? xl[k + 1] : plev;
		}
		eos = (ql > d ? ql : d) & 1;
		un_seq(t, cls, cp, lv, seq, m, sos, eos);
	}
	for (i = a; i < end; i++)
		if (un_removed(cls[i]))
			lv[i] = -1;
	d = 1;
	for (i = end; i > a; i--) {
		c = cls[i - 1];
		if (c == UN_S || c == UN_B) {
			lv[i - 1] = (signed char)plev;
			d = 1;
		} else if (d && (c == UN_WS || un_isinit(c) || c == UN_PDI)) {
			lv[i - 1] = (signed char)plev;
		} else if (!un_removed(c))
			d = 0;
	}
	free(stk);
	free(keep);
	free(seq);
	free(runs);
	free(mpdi);
	free(cont);
	free(xl);
}

/* Resolve levels for n characters (cp may be NULL when only classes are known);
   dir is 0 LTR, 1 RTL, 2 auto. Removed characters get -1. Returns the level of
   the first paragraph. */
int un_levels(const unsigned char *cls, const unsigned *cp, size_t n, int dir,
	      signed char *lv)
{
	unsigned char *t = xm(n + 1);
	size_t a = 0, e;
	int pl, first = -1;

	while (a < n) {
		for (e = a; e < n && cls[e] != UN_B; e++)
			;
		if (e < n)
			e++;
		pl = dir == 2 ? un_firststrong(cls, a, e) : dir;
		if (pl < 0)
			pl = 0;
		if (first < 0)
			first = pl;
		un_para(cls, cp, a, e, pl, lv, t);
		a = e;
	}
	free(t);
	return first < 0 ? (dir == 1) : first;
}

/* Rule L2: the visual order of the characters that were not removed. */
void un_order(const signed char *lv, const unsigned char *cls, size_t n, int plev,
	      size_t *ord, size_t *no)
{
	size_t i, j, k, m = 0, x;
	int hi = 0, lo = 127, l;

	(void)cls;
	(void)plev;
	for (i = 0; i < n; i++) {
		if (lv[i] < 0)
			continue;
		ord[m++] = i;
		if (lv[i] > hi)
			hi = lv[i];
		if (lv[i] < lo)
			lo = lv[i];
	}
	if (!(lo & 1))
		lo++;
	for (l = hi; l >= lo; l--)
		for (i = 0; i < m; i++) {
			if (lv[ord[i]] < l)
				continue;
			for (j = i; j < m && lv[ord[j]] >= l; j++)
				;
			for (k = i; k < (i + j) / 2; k++) {
				x = ord[k];
				ord[k] = ord[i + j - 1 - k];
				ord[i + j - 1 - k] = x;
			}
			i = j;
		}
	*no = m;
}
