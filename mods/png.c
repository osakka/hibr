/* png -- writing one, with nothing linked.
 *
 * See mods/png.h for why. The format is four pieces: an eight byte
 * signature, an IHDR saying the size and the colour type, one IDAT holding
 * a zlib stream of the filtered scanlines, and an IEND. Every chunk is a
 * length, a four letter tag, the data, and a CRC-32 over the tag and the
 * data but not the length -- which is the one part of this that is easy to
 * get subtly wrong.
 *
 * A scanline is a filter byte and then w*3 bytes, each the difference
 * between this byte and a prediction from its neighbours. The five
 * predictions are the format's, and the names below are its own: None, Sub
 * (the byte three to the left), Up (the byte above), Average of those two,
 * and Paeth, which picks whichever of left, above and above-left is nearest
 * to left + above - above-left. All arithmetic is modulo 256 by being done
 * in unsigned char, which is the format's rule and not an accident of C.
 */
#include "png.h"
#include "inflate.h"
#include <stdlib.h>
#include <string.h>

#ifndef PW_BPP
#define PW_BPP 3
#endif

static unsigned pw_tab[256];
static int pw_tabok;

/* The CRC-32 of the PNG and gzip world: the reversed polynomial 0xedb88320,
   a table built once. */
unsigned pw_crc(const unsigned char *p, size_t n)
{
	unsigned c = 0xffffffffu;
	size_t i;
	int k;

	if (!pw_tabok) {
		unsigned j;

		for (j = 0; j < 256; j++) {
			unsigned v = j;

			for (k = 0; k < 8; k++)
				v = (v & 1) ? 0xedb88320u ^ (v >> 1) : v >> 1;
			pw_tab[j] = v;
		}
		pw_tabok = 1;
	}
	for (i = 0; i < n; i++)
		c = pw_tab[(c ^ p[i]) & 0xff] ^ (c >> 8);
	return c ^ 0xffffffffu;
}

/* A four byte big-endian number, which is every length and every CRC here. */
void pw_be32(str *o, unsigned v)
{
	s_ch(o, (int)((v >> 24) & 0xff));
	s_ch(o, (int)((v >> 16) & 0xff));
	s_ch(o, (int)((v >> 8) & 0xff));
	s_ch(o, (int)(v & 0xff));
}

/* One chunk: its length, then tag and data with a CRC over both. */
void pw_chunk(str *o, const char *tag, const unsigned char *d, size_t n)
{
	unsigned char *b = xm(n + 4);

	memcpy(b, tag, 4);
	if (n)
		memcpy(b + 4, d, n);
	pw_be32(o, (unsigned)n);
	s_add(o, (const char *)b, n + 4);
	pw_be32(o, pw_crc(b, n + 4));
	free(b);
}

/* Paeth's predictor: whichever of the three neighbours is nearest to
   a + b - c, with a tie going to the one on the left, as the format says. */
int pw_paeth(int a, int b, int c)
{
	int p = a + b - c;
	int pa = p > a ? p - a : a - p;
	int pb = p > b ? p - b : b - p;
	int pc = p > c ? p - c : c - p;

	if (pa <= pb && pa <= pc)
		return a;
	return pb <= pc ? b : c;
}

/* Filter one scanline with filter f into dst, given the row above (or NULL
   for the first). Answers the sum of absolute residuals, read as a signed
   byte, which is what chooses between the five. */
unsigned long pw_filter(const unsigned char *row, const unsigned char *up,
			int n, int f, unsigned char *dst)
{
	unsigned long sum = 0;
	int i, v;

	for (i = 0; i < n; i++) {
		int a = i >= PW_BPP ? row[i - PW_BPP] : 0;
		int b = up ? up[i] : 0;
		int c = (up && i >= PW_BPP) ? up[i - PW_BPP] : 0;

		switch (f) {
		case 1: v = row[i] - a; break;
		case 2: v = row[i] - b; break;
		case 3: v = row[i] - ((a + b) >> 1); break;
		case 4: v = row[i] - pw_paeth(a, b, c); break;
		default: v = row[i]; break;
		}
		dst[i] = (unsigned char)v;
		v = (signed char)dst[i];
		sum += (unsigned long)(v < 0 ? -v : v);
	}
	return sum;
}

int pw_write(const unsigned char *rgb, int w, int h, str *out)
{
	unsigned char ihdr[13];
	unsigned char *raw, *try, *best, *up = 0;
	str z;
	size_t rawn, at = 0;
	int y, f, bw = w * PW_BPP;

	s_add(out, "\211PNG\r\n\032\n", 8);
	ihdr[0] = (unsigned char)((unsigned)w >> 24);
	ihdr[1] = (unsigned char)(((unsigned)w >> 16) & 0xff);
	ihdr[2] = (unsigned char)(((unsigned)w >> 8) & 0xff);
	ihdr[3] = (unsigned char)((unsigned)w & 0xff);
	ihdr[4] = (unsigned char)((unsigned)h >> 24);
	ihdr[5] = (unsigned char)(((unsigned)h >> 16) & 0xff);
	ihdr[6] = (unsigned char)(((unsigned)h >> 8) & 0xff);
	ihdr[7] = (unsigned char)((unsigned)h & 0xff);
	ihdr[8] = 8;			/* eight bits a channel */
	ihdr[9] = 2;			/* truecolour, no alpha */
	ihdr[10] = 0;			/* deflate, the only method there is */
	ihdr[11] = 0;			/* the adaptive filtering above */
	ihdr[12] = 0;			/* not interlaced */
	pw_chunk(out, "IHDR", ihdr, sizeof ihdr);

	rawn = ((size_t)bw + 1) * (size_t)h;
	raw = xm(rawn);
	try = xm((size_t)bw);
	best = xm((size_t)bw);
	for (y = 0; y < h; y++) {
		const unsigned char *row = rgb + (size_t)y * bw;
		unsigned long bestsum = 0;
		int bestf = 0;

		for (f = 0; f < 5; f++) {
			unsigned long s = pw_filter(row, up, bw, f, try);

			if (!f || s < bestsum) {
				bestsum = s;
				bestf = f;
				memcpy(best, try, (size_t)bw);
			}
		}
		raw[at++] = (unsigned char)bestf;
		memcpy(raw + at, best, (size_t)bw);
		at += (size_t)bw;
		up = (unsigned char *)row;
	}
	s_init(&z);
	def_zlib(raw, rawn, &z);
	pw_chunk(out, "IDAT", (const unsigned char *)z.p, z.n);
	pw_chunk(out, "IEND", 0, 0);
	s_free(&z);
	free(raw);
	free(try);
	free(best);
	return 1;
}
