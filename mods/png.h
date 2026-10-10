#ifndef HIBR_PNG_H
#define HIBR_PNG_H

#include "hibr.h"

/* Write a PNG: w by h pixels of RGB, three bytes each, appended to out.
   Eight-bit truecolour, no alpha, no interlace -- the one shape every
   caller here has.

   Why this exists: the kitty graphics protocol takes a PNG as `f=100`, and
   a wallpaper sent that way is a fifth of the bytes it is raw (Gitea #129).
   The img module *reads* PNGs through libpng, dlopen'd on first use, and
   this writes one with nothing linked at all -- deflate is ours
   (mods/deflate.c) and so is the CRC, which is two dozen lines.

   The compression is where the saving is and the filters are where the
   compression is: a photograph's neighbouring pixels are near each other,
   so the residual after subtracting a neighbour is small and clusters
   around zero, which is what a Huffman code built from the data then eats.
   One filter is chosen per scanline, the five the format offers tried and
   the one with the smallest sum of absolute residuals taken -- which is the
   heuristic libpng itself uses and is within a percent or two of trying
   every combination.

   1 always: there is nothing a caller could do about a failure to
   compress, and nothing here can fail otherwise. */
int pw_write(const unsigned char *rgb, int w, int h, str *out);

/* The CRC-32 the format puts after every chunk, exposed because a test
   wants to check one without writing a whole file. */
unsigned pw_crc(const unsigned char *p, size_t n);

#endif
