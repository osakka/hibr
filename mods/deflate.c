/* deflate -- the other direction of RFC 1951, written here for the same
 * reason inflate.c was: a few hundred lines against a dependency, and shared
 * by whoever needs it rather than copied.
 *
 * What it is for decided what it is. The kitty graphics protocol sends a
 * picture as three bytes a pixel, uncompressed, and a wallpaper at 232x71
 * with an 8x16 cell is 2.1 MB of base64 even halved -- which is free on a
 * local terminal and seconds of a reattach over a link, since hold re-emits
 * every picture to every client that attaches (Gitea #129). So this is sized
 * for "compress a still once, well enough", not for a stream.
 *
 * The shape, and why each piece is the size it is -- measured on the owner's
 * own wallpaper at 928x568, 1.58 MB of pixels, against zlib:
 *
 *   LZ77 with a hash chain, greedy, chain capped at DF_CHAIN. Lazy matching
 *   (try the next position before taking this match) is the next thing a
 *   real encoder does and is not here: it is about a tenth of the remaining
 *   ratio for a third more code, all of it the fiddly kind.
 *
 *   Dynamic Huffman codes per block, which is where the ratio actually is.
 *   Fixed codes are built for text; what a PNG hands over is filtered
 *   residuals, clustered hard around zero, and a code built from the data
 *   beats a fixed one by about a fifth on exactly that.
 *
 *   A stored block when a compressed one would be no smaller, so
 *   incompressible input grows by five bytes per 64 kB rather than by a
 *   quarter.
 *
 * The streams it writes are read back by inf_zlib in the file beside this
 * one *and* by Python's zlib in tests/998-deflate.t -- both, deliberately:
 * a mistake mirrored in our own two directions is invisible to a round trip
 * through them alone.
 */
#include "inflate.h"
#include <stdlib.h>
#include <string.h>

#ifndef DF_WBITS
#define DF_WBITS 15
#endif
#ifndef DF_WSIZE
#define DF_WSIZE (1 << DF_WBITS)
#endif
#ifndef DF_HBITS
#define DF_HBITS 15
#endif
#ifndef DF_HSIZE
#define DF_HSIZE (1 << DF_HBITS)
#endif
#ifndef DF_CHAIN
#define DF_CHAIN 32
#endif
#ifndef DF_MINM
#define DF_MINM 3
#endif
#ifndef DF_MAXM
#define DF_MAXM 258
#endif
/* How many symbols a block holds before it is flushed. A block pays for its
   own code lengths (about 60 bytes), so short blocks waste them and long
   ones stop the codes from following the data; 16384 is the shape zlib
   settles on too. */
#ifndef DF_BLOCK
#define DF_BLOCK 16384
#endif

/* One literal, or one length and distance pair, as the block collected it. */
typedef struct {
	unsigned short lit;		/* the literal, or the length */
	unsigned short dist;		/* 0 for a literal */
} df_sym;

struct df {
	const unsigned char *in;
	size_t n, pos;
	int *head;			/* hash -> the last position with it */
	int *prev;			/* position -> the one before it */
	df_sym *sym;
	int nsym;
	/* The counts the block's own codes are built from. */
	unsigned lfreq[286], dfreq[30];
	/* code[i] and len[i] for the literal/length and distance alphabets,
	   and for the code-length alphabet that describes them. */
	unsigned short lcode[286], dcode[30], ccode[19];
	unsigned char llen[286], dlen[30], clen[19];
	str *out;
	unsigned bitbuf;
	int bitcnt;
	int err;
};

/* Push bits out, least significant first, which is deflate's own order for
   everything except a Huffman code -- those go most significant first and
   are reversed as they are built. */
void df_bits(struct df *z, unsigned v, int n)
{
	z->bitbuf |= (v & ((1u << n) - 1)) << z->bitcnt;
	z->bitcnt += n;
	while (z->bitcnt >= 8) {
		s_ch(z->out, (int)(z->bitbuf & 0xff));
		z->bitbuf >>= 8;
		z->bitcnt -= 8;
	}
}

/* Round up to the next byte, zero filling. */
void df_align(struct df *z)
{
	if (z->bitcnt)
		df_bits(z, 0, 8 - z->bitcnt);
}

/* Reverse the low n bits, which is what turns a canonical code into the
   order deflate writes it in. */
unsigned df_rev(unsigned v, int n)
{
	unsigned r = 0;

	while (n--) {
		r = (r << 1) | (v & 1);
		v >>= 1;
	}
	return r;
}

/* Which length code covers a match of this length, and which distance code
   this distance, found in the tables inflate.c already has rather than in
   tables of our own: one list of the boundaries, read in both directions. */
int df_lsym(int len)
{
	int i;

	for (i = 28; i > 0; i--)
		if (len >= inf_lbase[i])
			return i;
	return 0;
}

int df_dsym(int dist)
{
	int i;

	for (i = 29; i > 0; i--)
		if (dist >= inf_dbase[i])
			return i;
	return 0;
}

/* Build canonical Huffman code lengths for n symbols with these counts,
   none longer than maxlen.
 *
 * Package-merge is the exact answer and this is not it: it is the
 * straightforward thing -- build a Huffman tree over the non-zero counts,
 * read off the depths, and if any came out past maxlen, halve every count
 * and build again. Deflate's limit is 15 bits, which a tree over 286
 * symbols only reaches on input contrived to make it (counts following the
 * Fibonacci sequence), so the retry is a correctness backstop that real
 * data does not take. One symbol needs a length of 1 all the same: a code of
 * no bits cannot be written down.
 */
void df_lengths(const unsigned *freq, int n, int maxlen, unsigned char *len)
{
	/* Heap of (count, node), and for each node its two children, as
	   indices into the same arrays. Nodes 0..n-1 are the symbols. */
	unsigned *cnt = xm(sizeof *cnt * (size_t)n * 2);
	int *left = xm(sizeof *left * (size_t)n * 2);
	int *right = xm(sizeof *right * (size_t)n * 2);
	int *heap = xm(sizeof *heap * ((size_t)n + 1));
	unsigned *f = xm(sizeof *f * (size_t)n);
	int i, nh, nn, a, b, over;

	for (i = 0; i < n; i++)
		f[i] = freq[i];
	for (;;) {
		memset(len, 0, (size_t)n);
		nh = 0;
		for (i = 0; i < n; i++) {
			cnt[i] = f[i];
			left[i] = right[i] = -1;
			if (f[i])
				heap[nh++] = i;
		}
		if (nh == 0)
			break;			/* nothing to code at all */
		if (nh == 1) {
			len[heap[0]] = 1;
			break;
		}
		/* Selection rather than a sift: n is 286 at most and this runs
		   once a block, so the clarity is worth more than the log. */
		nn = n;
		while (nh > 1) {
			int ia = 0, ib = -1;

			for (i = 1; i < nh; i++)
				if (cnt[heap[i]] < cnt[heap[ia]])
					ia = i;
			a = heap[ia];
			heap[ia] = heap[--nh];
			for (i = 0; i < nh; i++)
				if (ib < 0 || cnt[heap[i]] < cnt[heap[ib]])
					ib = i;
			b = heap[ib];
			heap[ib] = heap[nh - 1];
			cnt[nn] = cnt[a] + cnt[b];
			left[nn] = a;
			right[nn] = b;
			heap[nh - 1] = nn;
			nn++;
		}
		/* Depths, by walking down from the root with an explicit
		   stack: the tree is 2n deep in the worst case and recursion
		   is not what this file is allowed. */
		{
			int *st = xm(sizeof *st * (size_t)nn * 2);
			int *dp = xm(sizeof *dp * (size_t)nn * 2);
			int top = 0;

			st[top] = heap[0];
			dp[top++] = 0;
			over = 0;
			while (top) {
				int node = st[--top], d = dp[top];

				if (left[node] < 0) {
					len[node] = (unsigned char)(d ? d : 1);
					if (d > maxlen)
						over = 1;
					continue;
				}
				st[top] = left[node];
				dp[top++] = d + 1;
				st[top] = right[node];
				dp[top++] = d + 1;
			}
			free(st);
			free(dp);
		}
		if (!over)
			break;
		for (i = 0; i < n; i++)
			if (f[i])
				f[i] = (f[i] + 1) / 2;
	}
	free(cnt);
	free(left);
	free(right);
	free(heap);
	free(f);
}

/* Canonical codes from code lengths, RFC 1951 section 3.2.2, reversed as
   they are assigned so df_bits can write them straight out. */
void df_codes(const unsigned char *len, int n, unsigned short *code)
{
	unsigned short cnt[16], next[16];
	int i, bits;

	memset(cnt, 0, sizeof cnt);
	for (i = 0; i < n; i++)
		cnt[len[i]]++;
	cnt[0] = 0;
	next[0] = 0;
	for (bits = 1; bits < 16; bits++)
		next[bits] = (unsigned short)((next[bits - 1] +
					       cnt[bits - 1]) << 1);
	for (i = 0; i < n; i++) {
		if (!len[i]) {
			code[i] = 0;
			continue;
		}
		code[i] = (unsigned short)df_rev(next[len[i]]++, len[i]);
	}
}

/* The two code-length lists, run-length coded into the code-length alphabet
   (16, 17, 18 being the repeats) and appended to a vector of (symbol, extra
   bits, how many extra bits). Returns how many entries it wrote. */
int df_runs(const unsigned char *len, int n, unsigned char *sym,
	    unsigned char *xtra, unsigned char *nx)
{
	int i = 0, k = 0;

	while (i < n) {
		int v = len[i], run = 1;

		while (i + run < n && len[i + run] == v)
			run++;
		if (!v) {
			while (run >= 11) {
				int r = run > 138 ? 138 : run;

				sym[k] = 18;
				xtra[k] = (unsigned char)(r - 11);
				nx[k++] = 7;
				run -= r;
				i += r;
			}
			while (run >= 3) {
				int r = run > 10 ? 10 : run;

				sym[k] = 17;
				xtra[k] = (unsigned char)(r - 3);
				nx[k++] = 3;
				run -= r;
				i += r;
			}
			while (run--) {
				sym[k] = 0;
				xtra[k] = 0;
				nx[k++] = 0;
				i++;
			}
			continue;
		}
		/* A non-zero length is written once and then repeated with
		   code 16, which is why the first of the run is not folded
		   into the repeat count. */
		sym[k] = (unsigned char)v;
		xtra[k] = 0;
		nx[k++] = 0;
		i++;
		run--;
		while (run >= 3) {
			int r = run > 6 ? 6 : run;

			sym[k] = 16;
			xtra[k] = (unsigned char)(r - 3);
			nx[k++] = 2;
			run -= r;
			i += r;
		}
		while (run--) {
			sym[k] = (unsigned char)v;
			xtra[k] = 0;
			nx[k++] = 0;
			i++;
		}
	}
	return k;
}

/* How many bits a compressed block would take, so a stored one can be
   chosen when it would not. The header is counted by the caller. */
size_t df_cost(struct df *z, int nlit, int ndist)
{
	size_t bits = 0;
	int i;

	for (i = 0; i < nlit; i++)
		bits += (size_t)z->lfreq[i] * z->llen[i];
	for (i = 0; i < 286; i++)
		if (i >= 257 && z->lfreq[i])
			bits += (size_t)z->lfreq[i] * inf_lext[i - 257];
	for (i = 0; i < ndist; i++)
		bits += (size_t)z->dfreq[i] * (z->dlen[i] + inf_dext[i]);
	return bits;
}

/* Write the block this symbol list makes, as a dynamic Huffman block or as
   a stored one, whichever is smaller. raw and rawn are the bytes the
   symbols came from, which a stored block writes verbatim. */
void df_block(struct df *z, const unsigned char *raw, size_t rawn, int last)
{
	unsigned char csym[320], cxtra[320], cnx[320];
	unsigned cfreq[19];
	int nlit = 286, ndist = 30, ncl, nrun, i, k;
	size_t cbits, sbits;

	memset(cfreq, 0, sizeof cfreq);
	/* Two forced codes, which is the format's own requirement rather than
	   a nicety: a Huffman code over a single symbol is zero bits long and
	   cannot be written down, and a decoder handed one incomplete code is
	   entitled to refuse the stream. zlib forces two for the same reason
	   and says so in build_tree. An empty input is exactly this case --
	   nothing but the end-of-block symbol. */
	k = 0;
	for (i = 0; i < 286; i++)
		if (z->lfreq[i])
			k++;
	for (i = 0; k < 2 && i < 286; i++)
		if (!z->lfreq[i]) {
			z->lfreq[i] = 1;
			k++;
		}
	df_lengths(z->lfreq, 286, 15, z->llen);
	df_lengths(z->dfreq, 30, 15, z->dlen);
	/* A block of pure literals needs no distance code at all, and the
	   format has no way to say "none": one symbol of length 1 stands in,
	   which every decoder reads as the single-code case. */
	for (i = 0; i < 30; i++)
		if (z->dlen[i])
			break;
	if (i == 30)
		z->dlen[0] = 1;
	df_codes(z->llen, 286, z->lcode);
	df_codes(z->dlen, 30, z->dcode);
	while (nlit > 257 && !z->llen[nlit - 1])
		nlit--;
	while (ndist > 1 && !z->dlen[ndist - 1])
		ndist--;
	/* One run-length pass over both lists together, as the format wants
	   them: a repeat may cross from the last literal length into the
	   first distance length. */
	{
		unsigned char both[286 + 30];

		memcpy(both, z->llen, (size_t)nlit);
		memcpy(both + nlit, z->dlen, (size_t)ndist);
		nrun = df_runs(both, nlit + ndist, csym, cxtra, cnx);
	}
	for (i = 0; i < nrun; i++)
		cfreq[csym[i]]++;
	df_lengths(cfreq, 19, 7, z->clen);
	df_codes(z->clen, 19, z->ccode);
	ncl = 19;
	while (ncl > 4 && !z->clen[inf_ord[ncl - 1]])
		ncl--;
	/* Would a stored block be smaller? Its header is three bits, then
	   alignment to the byte, then two length words -- and a stored block
	   carries at most 65535 bytes, so a long one is several of them and
	   pays that header each time. A block here can hold 16384 symbols of
	   up to 258 bytes, so "several" is as many as 65. */
	cbits = df_cost(z, nlit, ndist) + 3 + 5 + 5 + 4 +
		(size_t)ncl * 3;
	for (i = 0; i < nrun; i++)
		cbits += z->clen[csym[i]] + cnx[i];
	sbits = rawn * 8 + ((rawn + 65534) / 65535) * (3 + 7 + 32);
	if (rawn && sbits <= cbits) {
		size_t off = 0;

		while (off < rawn || !off) {
			size_t len = rawn - off > 65535 ? 65535 : rawn - off;
			int lastb = last && off + len >= rawn;

			df_bits(z, lastb ? 1 : 0, 1);
			df_bits(z, 0, 2);
			df_align(z);
			s_ch(z->out, (int)(len & 0xff));
			s_ch(z->out, (int)(len >> 8));
			s_ch(z->out, (int)(~len & 0xff));
			s_ch(z->out, (int)((~len >> 8) & 0xff));
			s_add(z->out, (const char *)raw + off, len);
			off += len;
			if (off >= rawn)
				break;
		}
		return;
	}
	df_bits(z, last ? 1 : 0, 1);
	df_bits(z, 2, 2);
	df_bits(z, (unsigned)(nlit - 257), 5);
	df_bits(z, (unsigned)(ndist - 1), 5);
	df_bits(z, (unsigned)(ncl - 4), 4);
	for (i = 0; i < ncl; i++)
		df_bits(z, z->clen[inf_ord[i]], 3);
	for (i = 0; i < nrun; i++) {
		df_bits(z, z->ccode[csym[i]], z->clen[csym[i]]);
		if (cnx[i])
			df_bits(z, cxtra[i], cnx[i]);
	}
	for (k = 0; k < z->nsym; k++) {
		int lit = z->sym[k].lit, dist = z->sym[k].dist;

		if (!dist) {
			df_bits(z, z->lcode[lit], z->llen[lit]);
			continue;
		}
		i = df_lsym(lit);
		df_bits(z, z->lcode[257 + i], z->llen[257 + i]);
		if (inf_lext[i])
			df_bits(z, (unsigned)(lit - inf_lbase[i]),
				inf_lext[i]);
		i = df_dsym(dist);
		df_bits(z, z->dcode[i], z->dlen[i]);
		if (inf_dext[i])
			df_bits(z, (unsigned)(dist - inf_dbase[i]),
				inf_dext[i]);
	}
	df_bits(z, z->lcode[256], z->llen[256]);
}

/* The hash of the three bytes at p, which is what a chain is keyed on. */
unsigned df_hash(const unsigned char *p)
{
	return (((unsigned)p[0] << 10) ^ ((unsigned)p[1] << 5) ^ p[2]) &
	       (DF_HSIZE - 1);
}

/* The longest match for the bytes at pos, within the window and the chain
   budget. Answers its length, with the distance in *dist; 0 for no match
   worth taking. */
int df_match(struct df *z, size_t pos, int *dist)
{
	const unsigned char *in = z->in;
	size_t left = z->n - pos;
	int best = 0, bestd = 0, chain = DF_CHAIN;
	int cur;

	if (left < DF_MINM)
		return 0;
	if (left > DF_MAXM)
		left = DF_MAXM;
	/* The chain's head is the most recent earlier position with these
	   three bytes: this position is inserted after it is matched, never
	   before, or every match would be against itself. */
	cur = z->head[df_hash(in + pos)];
	while (chain-- && cur >= 0) {
		size_t d = pos - (size_t)cur;
		int l = 0;

		if (d == 0 || d > DF_WSIZE)
			break;
		while ((size_t)l < left && in[cur + l] == in[pos + l])
			l++;
		if (l > best) {
			best = l;
			bestd = (int)d;
			if ((size_t)best >= left)
				break;
		}
		cur = z->prev[(size_t)cur & (DF_WSIZE - 1)];
	}
	if (best < DF_MINM)
		return 0;
	*dist = bestd;
	return best;
}

/* Note the position of the three bytes at pos in its chain. */
void df_insert(struct df *z, size_t pos)
{
	unsigned h;

	if (z->n - pos < DF_MINM)
		return;
	h = df_hash(z->in + pos);
	z->prev[pos & (DF_WSIZE - 1)] = z->head[h];
	z->head[h] = (int)pos;
}

/* Compress in into out as a raw deflate stream. */
int def_raw(const unsigned char *in, size_t n, str *out)
{
	struct df z;
	size_t start;
	int i;

	memset(&z, 0, sizeof z);
	z.in = in;
	z.n = n;
	z.out = out;
	z.head = xm(sizeof *z.head * DF_HSIZE);
	z.prev = xm(sizeof *z.prev * DF_WSIZE);
	z.sym = xm(sizeof *z.sym * DF_BLOCK);
	for (i = 0; i < DF_HSIZE; i++)
		z.head[i] = -1;
	for (i = 0; i < DF_WSIZE; i++)
		z.prev[i] = -1;
	start = 0;
	for (;;) {
		int done;

		z.nsym = 0;
		memset(z.lfreq, 0, sizeof z.lfreq);
		memset(z.dfreq, 0, sizeof z.dfreq);
		start = z.pos;
		while (z.nsym < DF_BLOCK && z.pos < n) {
			int dist = 0, len = df_match(&z, z.pos, &dist);

			if (len) {
				size_t e = z.pos + (size_t)len;

				z.sym[z.nsym].lit = (unsigned short)len;
				z.sym[z.nsym++].dist = (unsigned short)dist;
				z.lfreq[257 + df_lsym(len)]++;
				z.dfreq[df_dsym(dist)]++;
				while (z.pos < e) {
					df_insert(&z, z.pos);
					z.pos++;
				}
				continue;
			}
			z.sym[z.nsym].lit = in[z.pos];
			z.sym[z.nsym++].dist = 0;
			z.lfreq[in[z.pos]]++;
			df_insert(&z, z.pos);
			z.pos++;
		}
		z.lfreq[256]++;
		done = z.pos >= n;
		df_block(&z, in + start, z.pos - start, done);
		if (done)
			break;
	}
	/* An empty input is still a stream: one last block with nothing in
	   it, which df_block has already written as the end-of-block symbol
	   of a code built for it. */
	df_align(&z);
	free(z.head);
	free(z.prev);
	free(z.sym);
	return 1;
}

/* Compress in into out as a zlib stream (RFC 1950): a two byte header whose
   check makes the pair a multiple of 31, the deflate data, then Adler-32 of
   the uncompressed bytes, most significant byte first. */
int def_zlib(const unsigned char *in, size_t n, str *out)
{
	unsigned a = 1, b = 0;
	size_t i;
	unsigned hdr;

	/* 0x78: deflate, a 32 kB window. The low five bits of the second byte
	   make the two a multiple of 31; level 2 of 0-3 says "default". */
	hdr = 0x7800u | (2u << 6);
	hdr |= 31u - (hdr % 31u);
	s_ch(out, (int)(hdr >> 8));
	s_ch(out, (int)(hdr & 0xff));
	if (!def_raw(in, n, out))
		return 0;
	for (i = 0; i < n; i++) {
		a = (a + in[i]) % 65521u;
		b = (b + a) % 65521u;
	}
	s_ch(out, (int)((b >> 8) & 0xff));
	s_ch(out, (int)(b & 0xff));
	s_ch(out, (int)((a >> 8) & 0xff));
	s_ch(out, (int)(a & 0xff));
	return 1;
}
