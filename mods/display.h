#ifndef HIBR_DISPLAY_H
#define HIBR_DISPLAY_H

#include "hibr.h"

/* The interface a display backend offers through hibr_provide, under the name
   "display". The console backend draws cells on a terminal; a framebuffer or
   an SDL backend would offer this same table, and a tool written against it
   would not know the difference. Anything added here is a new version;
   anything reordered or removed breaks every tool already using it. */

#ifndef DP_API_VER
#define DP_API_VER 1u
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
};

#endif
