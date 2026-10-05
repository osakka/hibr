#ifndef HIBR_UNI_H
#define HIBR_UNI_H

#include "hibr.h"

#ifndef UNI_VER
#define UNI_VER 3
#endif

/* What the uni module offers as "uni": text for a cell grid. vis writes t's
   display order, shaped and mirrored, into o (dir 0 LTR, 1 RTL, 2 auto);
   cw is a code point's width in columns; rtl says whether a text holds
   anything right to left, so a caller can skip vis cheaply; vismap draws
   code points [a, b) of t as one line of t's paragraph (levels and shaping
   over all of t, rules L1 and L2 on the line), with in map, one int per
   character of the line plus one, the column each landed in and, last,
   where a cursor after its end goes; line is vismap with, for each
   character drawn, in gsrc and gcol, the character of the line it came
   from and its column, ng of them. */
typedef struct uni_api {
	void (*vis)(const char *t, size_t n, int dir, str *o);
	int (*cw)(unsigned c);
	int (*rtl)(const char *t, size_t n);
	void (*vismap)(const char *t, size_t n, size_t a, size_t b, int dir, str *o, int *map);
	void (*line)(const char *t, size_t n, size_t a, size_t b, int dir, str *o, int *map,
		     int *gsrc, int *gcol, size_t *ng);
} uni_api;

#endif
