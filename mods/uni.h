#ifndef HIBR_UNI_H
#define HIBR_UNI_H

#include "hibr.h"

#ifndef UNI_VER
#define UNI_VER 1
#endif

/* What the uni module offers as "uni": text for a cell grid. vis writes t's
   display order, shaped and mirrored, into o (dir 0 LTR, 1 RTL, 2 auto);
   cw is a code point's width in columns; rtl says whether a text holds
   anything right to left, so a caller can skip vis cheaply. */
typedef struct uni_api {
	void (*vis)(const char *t, size_t n, int dir, str *o);
	int (*cw)(unsigned c);
	int (*rtl)(const char *t, size_t n);
} uni_api;

#endif
