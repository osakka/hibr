#ifndef HIBR_DISPLAY_H
#define HIBR_DISPLAY_H

#include "hibr.h"

/* The interface a display backend offers through hibr_provide, under the name
   "display". The console backend draws cells on a terminal; a framebuffer or
   an SDL backend would offer this same table, and a tool written against it
   would not know the difference. Anything added here is a new version;
   anything reordered or removed breaks every tool already using it. */

#ifndef DP_API_VER
#define DP_API_VER 5u
#endif

#ifndef DP_ATTRS
#define DP_BOLD 1u
#define DP_DIM 2u
#define DP_ITAL 4u
#define DP_UNDER 8u
#define DP_BLINK 16u
#define DP_REV 32u
#define DP_STRIKE 64u
#define DP_ATTRS 127u
#endif

#ifndef DP_COLOUR
#define DP_DEFAULT 0u
#define DP_PAL 0x1000000u
#define DP_RGB 0x2000000u
#define DP_COLOUR 1
#endif

typedef struct dp_api dp_api;
struct dp_api {
	int (*open)(sh *s);
	void (*close)(sh *s);
	int (*isopen)(void);
	void (*size)(int *rows, int *cols);
	int (*resized)(void);
	void (*pen)(unsigned fg, unsigned bg, unsigned attr);
	void (*clear)(void);
	int (*put)(int row, int col, const char *t);
	void (*fill)(int row, int col, int h, int w, const char *t);
	void (*cursor)(int row, int col, int vis);
	long (*flush)(void);
	int (*key)(int ms, str *out);
	int (*colour)(const char *t, unsigned *out);
	unsigned (*attr)(const char *t);
	/* 0 none, 1 clicks, 2 clicks and drags, 3 every movement. Off unless
	   asked for: reporting takes the terminal's own text selection away
	   from whoever is watching. */
	void (*mouse)(int mode);
	/* A named pane's own rectangle (root row, col, h, w), so another
	   module can translate and clip its own drawing into one without
	   reaching into the display backend's internals -- img draw's -p
	   is the first user. 0 if no such pane is registered. */
	int (*prect)(const char *nm, int *row, int *col, int *h, int *w);
	/* Version 4. The link later writes carry, as the pen's colours are
	   carried: an address, or null or "" for none. A backend that can
	   make text a link (OSC 8 on a terminal) does; one that cannot
	   draws the text as it would anyway. */
	void (*link)(const char *uri);
	/* Version 5. A picture as pixels, where the backend can place them:
	   h by w cells at row, col -- a pane's own coordinates when pane names
	   one, the screen's otherwise -- from iw by ih pixels of RGB triples,
	   any size, which the backend scales to the rectangle itself (it is
	   the only one that knows what a cell measures).

	   1 once the backend has it, and 0 when it cannot place pixels at all,
	   which is the caller's cue to draw its own cells instead: every user
	   of this keeps that fallback, since most terminals have no such thing.

	   DP_IMG_CHOSEN asks for a palette taken from the picture rather than
	   fixed levels: a still photograph wants it, a film does not (the fixed
	   one is the same from frame to frame, so a terminal keeps its colour
	   registers).

	   DP_IMG_UNDER says text is going to be drawn over this picture.
	   Without the flag the backend owns the cells the picture covers,
	   draws nothing through them, and keeps the picture until something
	   does -- which is what makes a still picture free on every flush
	   after the first. With it the picture owns no cells at all, goes out
	   before the text of each frame, and is kept for as long as the
	   caller places it again: the first frame that does not place it is
	   what takes it away (version 6; until then it was forgotten the
	   moment it had been sent, which cost a wallpaper its whole bitmap
	   every frame). What it costs in between belongs to the protocol
	   rather than to a choice: a picture the terminal keeps below the
	   text is sent once, and paint goes again whenever a cell over it has
	   been written. */
#ifndef DP_IMG_CHOSEN
#define DP_IMG_CHOSEN 1u
#define DP_IMG_UNDER 2u
#endif
	int (*image)(sh *s, const char *pane, int row, int col, int h, int w,
		     const unsigned char *rgb, int iw, int ih, unsigned flags);
	/* Whether pictures can be placed at all, and the pixel size of a cell:
	   0 in w and h when the terminal does not say, which is the same thing
	   as "no pictures". A caller needs this to decide how many pixels to
	   render for a rectangle of cells. */
	void (*cellpx)(int *w, int *h);
};

#endif
