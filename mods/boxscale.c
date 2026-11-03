#include <stddef.h>
#include "boxscale.h"

/* See boxscale.h. */
void bx_scale(const unsigned char *in, int iw, int rx, int ry, int rw, int rh,
	      unsigned char *out, int w, int h)
{
	int x, y, c;

	for (y = 0; y < h; y++) {
		int y0 = ry + (int)((long long)y * rh / h);
		int y1 = ry + (int)((long long)(y + 1) * rh / h);

		if (y1 <= y0)
			y1 = y0 + 1;
		if (y1 > ry + rh)
			y1 = ry + rh;
		for (x = 0; x < w; x++) {
			int x0 = rx + (int)((long long)x * rw / w);
			int x1 = rx + (int)((long long)(x + 1) * rw / w);
			unsigned long sum[3] = { 0, 0, 0 }, n = 0;
			int sx, sy;

			if (x1 <= x0)
				x1 = x0 + 1;
			if (x1 > rx + rw)
				x1 = rx + rw;
			for (sy = y0; sy < y1; sy++)
				for (sx = x0; sx < x1; sx++) {
					const unsigned char *p = in + ((size_t)sy * iw + sx) * 3;

					for (c = 0; c < 3; c++)
						sum[c] += p[c];
					n++;
				}
			for (c = 0; c < 3; c++)
				out[((size_t)y * w + x) * 3 + c] =
					(unsigned char)(n ? sum[c] / n : 0);
		}
	}
}
