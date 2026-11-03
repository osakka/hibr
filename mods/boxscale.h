#ifndef HIBR_BOXSCALE_H
#define HIBR_BOXSCALE_H

/* Box-filter a rectangle of an RGB bitmap into w by h pixels: iw is the
   whole bitmap's width, its stride, and rx,ry,rw,rh the part to take, so a
   picture can be cropped to the screen rather than squashed into it.
   Compiled into the console and img, which each wrote it out until
   0.99.435 (Gitea #356). */
void bx_scale(const unsigned char *in, int iw, int rx, int ry, int rw, int rh,
	      unsigned char *out, int w, int h);

#endif
