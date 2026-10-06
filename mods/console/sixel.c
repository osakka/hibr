/* sixel -- a picture as the bytes a terminal paints.
 *
 * Sixel is a palette format: a header, up to 256 colour registers, then
 * bands six pixels tall, each column of six a character. So a picture has
 * to be reduced to a palette first, and that is the only interesting part.
 *
 * Two ways, because they want different things (the owner's call):
 *
 *   fixed    6x6x6 levels, 216 colours, no analysis at all. A cell icon, a
 *            diagram, anything redrawn every frame: the palette is the same
 *            every time, so a terminal keeps its registers and the cost is
 *            one pass.
 *   chosen   median cut over the picture's own colours, up to 256. A still
 *            photograph wants this; a gradient banded by the fixed levels
 *            is the first thing anyone notices.
 *
 * `six_encode` takes RGB triples and writes the whole DCS string, header and
 * terminator included. Nothing here reads a file or touches the terminal.
 */
#include "cn.h"
#include <stdlib.h>
#include <string.h>

/* A box of colours being cut, as median cut holds one. */
typedef struct six_box {
	int lo[3], hi[3];
	unsigned *px;
	size_t n;
} six_box;

/* The level a channel takes in the fixed palette: 0..5 from 0..255. */
int six_lvl(unsigned v)
{
	return (int)((v * 5 + 127) / 255);
}

/* The fixed palette's index for one colour. */
int six_fixed(const unsigned char *p)
{
	return six_lvl(p[0]) * 36 + six_lvl(p[1]) * 6 + six_lvl(p[2]);
}

/* A fixed palette entry's own colour, in sixel's 0..100 per channel. */
void six_fixedrgb(int i, int *r, int *g, int *b)
{
	*r = (i / 36) * 20;
	*g = ((i / 6) % 6) * 20;
	*b = (i % 6) * 20;
}

/* The widest channel of a box, which is the one to cut on. */
int six_widest(const six_box *b)
{
	int i, w = 0, best = 0;

	for (i = 0; i < 3; i++)
		if (b->hi[i] - b->lo[i] > w) {
			w = b->hi[i] - b->lo[i];
			best = i;
		}
	return best;
}

static int six_ch;

/* Order two packed colours by the channel being cut. */
int six_cmp(const void *a, const void *b)
{
	unsigned x = *(const unsigned *)a, y = *(const unsigned *)b;
	int sh = 16 - 8 * six_ch;

	return (int)((x >> sh) & 255) - (int)((y >> sh) & 255);
}

/* Work out a box's own bounds from the colours in it. */
void six_bounds(six_box *b)
{
	size_t i;
	int c;

	for (c = 0; c < 3; c++) {
		b->lo[c] = 255;
		b->hi[c] = 0;
	}
	for (i = 0; i < b->n; i++)
		for (c = 0; c < 3; c++) {
			int v = (int)((b->px[i] >> (16 - 8 * c)) & 255);

			if (v < b->lo[c])
				b->lo[c] = v;
			if (v > b->hi[c])
				b->hi[c] = v;
		}
}

/* Median cut: fill pal with up to want colours chosen from the picture, and
   return how many there are. */
int six_choose(const unsigned char *rgb, size_t np, int want, unsigned *pal)
{
	six_box *bx;
	unsigned *px;
	size_t i;
	int n = 1, j, k, best;

	if (want > 256)
		want = 256;
	px = xm(np * sizeof *px);
	for (i = 0; i < np; i++)
		px[i] = (unsigned)rgb[i * 3] << 16 | (unsigned)rgb[i * 3 + 1] << 8 |
			rgb[i * 3 + 2];
	bx = xm((size_t)want * sizeof *bx);
	memset(bx, 0, (size_t)want * sizeof *bx);
	bx[0].px = px;
	bx[0].n = np;
	six_bounds(&bx[0]);
	while (n < want) {
		/* the box with the widest spread, so the cut buys the most */
		best = -1;
		for (j = 0, k = 0; j < n; j++) {
			int w = bx[j].hi[six_widest(&bx[j])] - bx[j].lo[six_widest(&bx[j])];

			if (bx[j].n > 1 && w > k) {
				k = w;
				best = j;
			}
		}
		if (best < 0)
			break;
		six_ch = six_widest(&bx[best]);
		qsort(bx[best].px, bx[best].n, sizeof *bx[best].px, six_cmp);
		i = bx[best].n / 2;
		bx[n].px = bx[best].px + i;
		bx[n].n = bx[best].n - i;
		bx[best].n = i;
		six_bounds(&bx[best]);
		six_bounds(&bx[n]);
		n++;
	}
	for (j = 0; j < n; j++) {
		unsigned long sum[3] = { 0, 0, 0 };
		int c;

		for (i = 0; i < bx[j].n; i++)
			for (c = 0; c < 3; c++)
				sum[c] += (bx[j].px[i] >> (16 - 8 * c)) & 255;
		if (!bx[j].n) {
			pal[j] = 0;
			continue;
		}
		pal[j] = 0;
		for (c = 0; c < 3; c++)
			pal[j] |= (unsigned)(sum[c] / bx[j].n) << (16 - 8 * c);
	}
	free(px);
	free(bx);
	return n;
}

/* The palette entry nearest a colour, by square distance. */
int six_near(const unsigned *pal, int n, const unsigned char *p)
{
	int i, best = 0;
	long bd = -1;

	for (i = 0; i < n; i++) {
		long dr = (long)((pal[i] >> 16) & 255) - p[0];
		long dg = (long)((pal[i] >> 8) & 255) - p[1];
		long db = (long)(pal[i] & 255) - p[2];
		long d = dr * dr + dg * dg + db * db;

		if (bd < 0 || d < bd) {
			bd = d;
			best = i;
		}
	}
	return best;
}

/* Write a number without printf, which this is called too often to pay for. */
void six_num(str *o, int v)
{
	char b[12];
	int i = 0;

	if (v <= 0) {
		s_ch(o, '0');
		return;
	}
	while (v > 0 && i < (int)sizeof b) {
		b[i++] = (char)('0' + v % 10);
		v /= 10;
	}
	while (i > 0)
		s_ch(o, b[--i]);
}

/* A run of the same sixel column: !n before it once it is worth the three
   characters that takes. */
void six_run(str *o, int ch, int n)
{
	if (n <= 0)
		return;
	if (n > 3) {
		s_ch(o, '!');
		six_num(o, n);
		s_ch(o, (char)ch);
		return;
	}
	while (n-- > 0)
		s_ch(o, (char)ch);
}

/* A picture as a DCS sixel string: w by h pixels of RGB, in at most 256
   colours, chosen from the picture when chosen is set and from the fixed
   6x6x6 levels otherwise. */
void six_encode(const unsigned char *rgb, int w, int h, int chosen, str *o)
{
	unsigned pal[256];
	unsigned char *ix, *bits, *inseen;
	int *seen;
	int n, x, y, band, c, i;

	if (w < 1 || h < 1)
		return;
	ix = xm((size_t)w * h);
	if (chosen) {
		n = six_choose(rgb, (size_t)w * h, 256, pal);
		for (i = 0; i < w * h; i++)
			ix[i] = (unsigned char)six_near(pal, n, rgb + (size_t)i * 3);
	} else {
		/* Every register is declared whether this picture uses it or
		   not: the palette is then the same from frame to frame, which
		   is the whole reason a moving picture takes the fixed one. */
		/* The level of each channel comes from a table rather than two
		   divisions a pixel: the same answer, a quarter of the work. */
		unsigned char lv[256];

		for (i = 0; i < 256; i++)
			lv[i] = (unsigned char)six_lvl((unsigned)i);
		for (i = 0; i < w * h; i++) {
			const unsigned char *q = rgb + (size_t)i * 3;

			ix[i] = (unsigned char)(lv[q[0]] * 36 + lv[q[1]] * 6 + lv[q[2]]);
		}
		n = 216;
		for (i = 0; i < 216; i++) {
			int r, g, b;

			six_fixedrgb(i, &r, &g, &b);
			pal[i] = (unsigned)(r * 255 / 100) << 16 |
				 (unsigned)(g * 255 / 100) << 8 | (unsigned)(b * 255 / 100);
		}
	}
	/* P1=0 any pixel aspect, P2=1 leave untouched pixels alone, P3=0 */
	s_cat(o, "\033P0;1;0q\"1;1;");
	six_num(o, w);
	s_ch(o, ';');
	six_num(o, h);
	for (i = 0; i < n; i++) {
		int r = (int)(((pal[i] >> 16) & 255) * 100 / 255);
		int g = (int)(((pal[i] >> 8) & 255) * 100 / 255);
		int b = (int)((pal[i] & 255) * 100 / 255);

		s_ch(o, '#');
		six_num(o, i);
		s_cat(o, ";2;");
		six_num(o, r);
		s_ch(o, ';');
		six_num(o, g);
		s_ch(o, ';');
		six_num(o, b);
	}
	/* One pass over a band's own pixels fills, for each colour it uses, the
	   six-bit column pattern of every column; then each of those colours is
	   emitted from that. The first version worked the other way round --
	   for every colour, scan the band -- which rescanned a band 216 times
	   and cost 61 ms a frame at 800 by 544. Measured: that is 25 to 75
	   times what half blocks cost, and most of it was this loop. */
	bits = xm((size_t)n * w);
	seen = xm((size_t)n * sizeof *seen);
	inseen = xm((size_t)n);
	memset(bits, 0, (size_t)n * w);
	memset(inseen, 0, (size_t)n);
	for (band = 0; band < h; band += 6) {
		int nseen = 0, j, first = 1;

		for (y = band; y < band + 6 && y < h; y++) {
			const unsigned char *row = ix + (size_t)y * w;
			int bit = 1 << (y - band);

			for (x = 0; x < w; x++) {
				c = row[x];
				bits[(size_t)c * w + x] |= (unsigned char)bit;
				if (!inseen[c]) {
					inseen[c] = 1;
					seen[nseen++] = c;
				}
			}
		}
		for (j = 0; j < nseen; j++) {
			unsigned char *bp;
			int run = -1, runn = 0;

			c = seen[j];
			bp = bits + (size_t)c * w;
			if (!first)
				s_ch(o, '$');
			first = 0;
			s_ch(o, '#');
			six_num(o, c);
			for (x = 0; x < w; x++) {
				int v = bp[x] + 63;

				if (v == run) {
					runn++;
					continue;
				}
				six_run(o, run, runn);
				run = v;
				runn = 1;
			}
			six_run(o, run, runn);
			memset(bp, 0, (size_t)w);
			inseen[c] = 0;
		}
		if (band + 6 < h)
			s_ch(o, '-');
	}
	free(bits);
	free(seen);
	free(inseen);
	s_cat(o, "\033\\");
	free(ix);
}
