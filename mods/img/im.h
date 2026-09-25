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
int im_pngload(const char *path, image *out, str *err);

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

#endif
