#ifndef HIBR_IMG_H
#define HIBR_IMG_H

#include "hibr.h"

/* A decoded image, top-left origin, row-major, no padding between rows.
   Always three bytes per pixel -- alpha is not kept: a terminal cell has no
   sensible way to blend against whatever half of it is not covered, so a
   transparent pixel is treated as opaque instead of guessing at a backdrop. */
typedef struct image image;
struct image {
	int w, h;
	unsigned char *px;
};

void im_free(image *im);
/* How big a picture is, from its header rather than by decoding it: 1 with w
   and h filled in (a JPEG's turned by its own EXIF orientation, as the
   decoder turns the pixels), 0 for a file whose header we do not know, which
   the caller then decodes. */
int im_hdrsize(const char *path, int *w, int *h);
/* jpeg.c: the EXIF orientation a JPEG declares, 1 to 8. */
int jp_orient(const unsigned char *b, size_t n);
int im_pngload(const char *path, image *out, str *err);
int im_pngmem(const unsigned char *b, size_t n, image *out, str *err);
int im_jpegload(const char *path, image *out, str *err);
int im_jpegmem(const unsigned char *b, size_t n, image *out, str *err);

/* One character cell's worth of a resampled image: the average colour of
   the source pixels it covers, top half and bottom half kept apart so a
   half-block cell can show both at once. */
typedef struct cell cell;
struct cell {
	unsigned char tr, tg, tb;
	unsigned char br, bg, bb;
};

/* Resample im into cols x rows*2 "half-rows" (a cell is two source rows
   tall), by box-filtering -- averaging every source pixel a cell covers,
   not sampling one -- so downscaling a real photo looks like a blurred
   photo rather than aliased noise. rows/cols must both be at least 1. */
void im_resample(const image *im, cell *out, int rows, int cols);

/* Resample im into exactly w by h pixels of RGB, the same box filter, for a
   backend that places pixels rather than cells (sixel). out holds w*h*3. */
void im_scale(const image *im, unsigned char *out, int w, int h);

/* How a picture is drawn. half and mono are cells (colour, and the same in
   grey); ascii is the character ramp; sixel is pixels, where the display can
   place them, and falls back to half when it cannot. */
#ifndef IM_HALF
#define IM_HALF 0
#define IM_MONO 1
#define IM_ASCII 2
#define IM_SIXEL 3
#define IM_AUTO 4
#endif
int im_mode(const char *t);

#endif
