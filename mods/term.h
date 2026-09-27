#ifndef HIBR_TERM_H
#define HIBR_TERM_H

#include "hibr.h"

/* The interface the term module offers through hibr_provide, under the name
   "terminal". A caller that wants a terminal emulator's own cell grid --
   fed bytes and queried by cell, with nothing drawn anywhere and no pty of
   its own -- gets one here, addressed by id the same way pty's own spawn
   is. `hold` is the first user of this: its own union-of-viewports emulator
   for a multi-monitor session. Anything added here is a new version;
   anything reordered or removed breaks every module already using it. */

#ifndef TM_API_VER
#define TM_API_VER 1u
#endif

typedef struct tm_api tm_api;
struct tm_api {
	/* A terminal with nothing on it yet, sized rows x cols. Returns an
	   id, always -- there is no failure case, the same as tm_new's. */
	int (*new)(int rows, int cols);
	void (*free)(int id);
	/* Change the size; 0 for no such id. */
	int (*resize)(int id, int rows, int cols);
	int (*rows)(int id);
	int (*cols)(int id);
	void (*feed)(int id, const char *b, size_t n);
	/* The cell at r,c: 1 and every field filled in, or 0 for no such id
	   or a position off the grid. w is 0 for the right half of a wide
	   character, which a caller skips over exactly as tm_draw does. */
	int (*at)(int id, int r, int c, unsigned *cp, unsigned *fg,
		  unsigned *bg, unsigned *attr, unsigned *w);
	/* Whether the program has turned mouse reporting on, and which mode
	   (0 for off, else 1000, 1002 or 1003); *sgr is set to 1 for the
	   SGR-1006 encoding, 0 for the legacy one -- what a caller needs to
	   translate a click into the report the program actually asked for,
	   rather than one it never enabled. 0 for no such id. */
	int (*mouse)(int id, int *sgr);
	/* The real cursor: row, col, and whether it should be shown at all
	   right now -- off, or scrolled back to history rather than the
	   live screen. 0 for no such id. */
	int (*cursor)(int id, int *r, int *c, int *vis);
};

#endif
