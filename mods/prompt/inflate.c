#include "pr.h"
#include <stdlib.h>
#include <string.h>

const short inf_ord[19] = { 16, 17, 18, 0, 8,  7, 9,  6, 10, 5,
			    11, 4,  12, 3, 13, 2, 14, 1, 15 };

const short inf_lbase[29] = { 3,  4,  5,  6,  7,  8,  9,  10,  11,  13,
			      15, 17, 19, 23, 27, 31, 35, 43,  51,  59,
			      67, 83, 99, 115, 131, 163, 195, 227, 258 };

const short inf_lext[29] = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
			     2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0 };

const short inf_dbase[30] = { 1,    2,    3,    4,    5,    7,     9,
			      13,   17,   25,   33,   49,   65,    97,
			      129,  193,  257,  385,  513,  769,   1025,
			      1537, 2049, 3073, 4097, 6145, 8193,  12289,
			      16385, 24577 };

const short inf_dext[30] = { 0, 0, 0,  0,  1,  1,  2,  2,  3,  3,  4,  4,
			     5, 5, 6,  6,  7,  7,  8,  8,  9,  9,  10, 10,
			     11, 11, 12, 12, 13, 13 };

struct huf { short cnt[16]; short *sym; };

struct inf {
	const unsigned char *in;
	size_t n, pos, max;
	unsigned bitbuf;
	int bitcnt, err;
	str out;
};

/* Pull the next bits, least significant first. */
int inf_bits(struct inf *z, int need)
{
	unsigned v = z->bitbuf;

	while (z->bitcnt < need) {
		if (z->pos >= z->n) {
			z->err = 1;
			return 0;
		}
		v |= (unsigned)z->in[z->pos++] << z->bitcnt;
		z->bitcnt += 8;
	}
	z->bitbuf = v >> need;
	z->bitcnt -= need;
	return (int)(v & ((1u << need) - 1));
}

/* Append one byte of output, respecting the size ceiling. */
void inf_put(struct inf *z, int c)
{
	if (z->out.n >= z->max) {
		z->err = 1;
		return;
	}
	s_ch(&z->out, c);
}

/* Copy a back reference out of the output produced so far. */
void inf_copy(struct inf *z, unsigned dist, unsigned len)
{
	size_t i, from;

	if (!dist || dist > z->out.n || z->out.n + len > z->max) {
		z->err = 1;
		return;
	}
	s_grow(&z->out, len);
	from = z->out.n - dist;
	for (i = 0; i < len; i++)
		z->out.p[z->out.n + i] = z->out.p[from + i];
	z->out.n += len;
	z->out.p[z->out.n] = 0;
}

/* Build a canonical decoding table from a list of code lengths. */
int inf_build(struct huf *h, const short *len, int n)
{
	int i, left;
	short *offs = xm(16 * sizeof *offs);

	for (i = 0; i < 16; i++)
		h->cnt[i] = 0;
	for (i = 0; i < n; i++)
		h->cnt[len[i]]++;
	if (h->cnt[0] == n) {
		free(offs);
		return 0;
	}
	left = 1;
	for (i = 1; i < 16; i++) {
		left <<= 1;
		left -= h->cnt[i];
		if (left < 0) {
			free(offs);
			return -1;
		}
	}
	offs[1] = 0;
	for (i = 1; i < 15; i++)
		offs[i + 1] = offs[i] + h->cnt[i];
	for (i = 0; i < n; i++)
		if (len[i])
			h->sym[offs[len[i]]++] = (short)i;
	free(offs);
	return left;
}

/* Decode one symbol against a canonical table. */
int inf_sym(struct inf *z, struct huf *h)
{
	int code = 0, first = 0, index = 0, len, b;

	for (len = 1; len < 16; len++) {
		b = inf_bits(z, 1);
		if (z->err)
			return -1;
		code |= b;
		if (code - first < h->cnt[len])
			return h->sym[index + (code - first)];
		index += h->cnt[len];
		first = (first + h->cnt[len]) << 1;
		code <<= 1;
	}
	z->err = 1;
	return -1;
}

/* Expand one block of literal and length codes. */
void inf_codes(struct inf *z, struct huf *lc, struct huf *dc)
{
	int sym, len, dist;

	for (;;) {
		sym = inf_sym(z, lc);
		if (z->err || sym < 0)
			return;
		if (sym < 256) {
			inf_put(z, sym);
			if (z->err)
				return;
			continue;
		}
		if (sym == 256)
			return;
		sym -= 257;
		if (sym >= 29) {
			z->err = 1;
			return;
		}
		len = inf_lbase[sym] + inf_bits(z, inf_lext[sym]);
		sym = inf_sym(z, dc);
		if (z->err || sym < 0 || sym >= 30) {
			z->err = 1;
			return;
		}
		dist = inf_dbase[sym] + inf_bits(z, inf_dext[sym]);
		if (z->err)
			return;
		inf_copy(z, (unsigned)dist, (unsigned)len);
		if (z->err)
			return;
	}
}

/* Copy an uncompressed block. */
void inf_stored(struct inf *z)
{
	unsigned len;

	z->bitbuf = 0;
	z->bitcnt = 0;
	if (z->pos + 4 > z->n) {
		z->err = 1;
		return;
	}
	len = (unsigned)z->in[z->pos] | ((unsigned)z->in[z->pos + 1] << 8);
	z->pos += 4;
	if (z->pos + len > z->n || z->out.n + len > z->max) {
		z->err = 1;
		return;
	}
	s_add(&z->out, (const char *)z->in + z->pos, len);
	z->pos += len;
}

/* Expand a block using the fixed code tables. */
void inf_fixed(struct inf *z)
{
	struct huf lc, dc;
	short *ll = xm(288 * sizeof *ll);
	short *dl = xm(30 * sizeof *dl);
	int i;

	lc.sym = xm(288 * sizeof *lc.sym);
	dc.sym = xm(30 * sizeof *dc.sym);
	for (i = 0; i < 144; i++)
		ll[i] = 8;
	for (; i < 256; i++)
		ll[i] = 9;
	for (; i < 280; i++)
		ll[i] = 7;
	for (; i < 288; i++)
		ll[i] = 8;
	for (i = 0; i < 30; i++)
		dl[i] = 5;
	inf_build(&lc, ll, 288);
	inf_build(&dc, dl, 30);
	inf_codes(z, &lc, &dc);
	free(ll);
	free(dl);
	free(lc.sym);
	free(dc.sym);
}

/* Read the code lengths of a dynamic block and expand it. */
void inf_dynamic(struct inf *z)
{
	struct huf lc, dc, cc;
	short *lens = xm(320 * sizeof *lens);
	int nlen, ndist, ncode, i = 0, sym, l;

	lc.sym = xm(288 * sizeof *lc.sym);
	dc.sym = xm(30 * sizeof *dc.sym);
	cc.sym = xm(19 * sizeof *cc.sym);
	nlen = inf_bits(z, 5) + 257;
	ndist = inf_bits(z, 5) + 1;
	ncode = inf_bits(z, 4) + 4;
	if (z->err || nlen > 286 || ndist > 30) {
		z->err = 1;
		goto out;
	}
	for (i = 0; i < 19; i++)
		lens[i] = 0;
	for (i = 0; i < ncode; i++)
		lens[inf_ord[i]] = (short)inf_bits(z, 3);
	if (z->err || inf_build(&cc, lens, 19) != 0) {
		z->err = 1;
		goto out;
	}
	i = 0;
	while (i < nlen + ndist) {
		sym = inf_sym(z, &cc);
		if (z->err || sym < 0)
			goto out;
		if (sym < 16) {
			lens[i++] = (short)sym;
			continue;
		}
		if (sym == 16) {
			if (!i) {
				z->err = 1;
				goto out;
			}
			l = lens[i - 1];
			sym = 3 + inf_bits(z, 2);
		} else if (sym == 17) {
			l = 0;
			sym = 3 + inf_bits(z, 3);
		} else {
			l = 0;
			sym = 11 + inf_bits(z, 7);
		}
		if (z->err || i + sym > nlen + ndist) {
			z->err = 1;
			goto out;
		}
		while (sym--)
			lens[i++] = (short)l;
	}
	if (lens[256] == 0) {
		z->err = 1;
		goto out;
	}
	if (inf_build(&lc, lens, nlen) < 0 ||
	    inf_build(&dc, lens + nlen, ndist) < 0) {
		z->err = 1;
		goto out;
	}
	inf_codes(z, &lc, &dc);
out:
	free(lens);
	free(lc.sym);
	free(dc.sym);
	free(cc.sym);
}

/* Expand a raw deflate stream, reporting the bytes consumed. */
int inf_raw(const unsigned char *in, size_t n, size_t max, str *out,
	    size_t *used)
{
	struct inf z;
	int last, type;

	memset(&z, 0, sizeof z);
	z.in = in;
	z.n = n;
	z.max = max;
	s_init(&z.out);
	s_grow(&z.out, 1);
	z.out.p[0] = 0;
	do {
		last = inf_bits(&z, 1);
		type = inf_bits(&z, 2);
		if (z.err)
			break;
		if (type == 0)
			inf_stored(&z);
		else if (type == 1)
			inf_fixed(&z);
		else if (type == 2)
			inf_dynamic(&z);
		else
			z.err = 1;
	} while (!last && !z.err);
	if (z.err) {
		lg(HIBR_LDBG, "inflate: stream damaged at byte %lu",
		   (unsigned long)z.pos);
		s_free(&z.out);
		return 0;
	}
	if (used)
		*used = z.pos;
	*out = z.out;
	return 1;
}

/* Expand a zlib wrapped deflate stream. */
int inf_zlib(const unsigned char *in, size_t n, size_t max, str *out,
	     size_t *used)
{
	size_t u = 0;

	if (n < 2 || (in[0] & 0x0f) != 8 ||
	    ((unsigned)in[0] << 8 | in[1]) % 31) {
		lg(HIBR_LDBG, "inflate: not a zlib stream");
		return 0;
	}
	if (in[1] & 0x20) {
		lg(HIBR_LDBG, "inflate: preset dictionary unsupported");
		return 0;
	}
	if (!inf_raw(in + 2, n - 2, max, out, &u))
		return 0;
	if (used)
		*used = u + 2 + 4;
	return 1;
}
