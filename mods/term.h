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
#define TM_API_VER 8u
#endif

/* DECSCUSR's six shapes collapse to three: blinking is never drawn as
   blinking (that would cost a redraw every blink, on every terminal window,
   whether or not anyone is looking at it), so its blinking and steady forms
   share one drawn shape. */
enum { TM_BLOCK, TM_UNDER, TM_BAR };

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
	/* The mode state a caller re-serialising this grid onto a real
	   terminal has to reproduce, or the terminal is left on the wrong
	   buffer, unable to paste, or showing the wrong cursor shape: alt is
	   whether the alternate screen is active, bpaste whether the program
	   has asked for bracketed paste, cshape one of TM_BLOCK/TM_UNDER/
	   TM_BAR. 0 for no such id. */
	int (*modes)(int id, int *alt, int *bpaste, int *cshape);
	/* What the program last asked to put on the clipboard with OSC 52,
	   exactly as it sent it -- "selection;base64" -- handed over once and
	   then forgotten: 1 with it appended to out, 0 when nothing is
	   waiting or no such id. Added in version 2. */
	int (*clip)(int id, str *out);
	/* Version 3. What the program sent for the terminal rather than the
	   screen -- a bell, a notification (OSC 9, 777) -- exactly as it
	   came, handed over once: 1 with it appended to out, 0 when nothing
	   is waiting. */
	int (*pass)(int id, str *out);
	/* The title the program last set (OSC 0 or 2), appended to out; 0
	   when there is none. */
	int (*title)(int id, str *out);
	/* The link a cell belongs to (OSC 8), 0 for none -- a number this
	   terminal gives each address it has seen, and its address. */
	unsigned (*linkat)(int id, int r, int c);
	const char *(*linkuri)(int id, unsigned link);
	/* Version 4. The pictures a program put on this terminal, kept
	   rather than dropped, for a caller that re-serialises this grid
	   onto a real terminal: a bitmap is not cells, and an emulator that
	   redraws loses what it does not draw (ADR 0037, Gitea #103).

	   cellpx says what a cell measures where this grid will be drawn,
	   which is the only way a sixel's own pixel size can be turned into
	   cells -- without it a sixel is not kept at all. imgen changes
	   whenever the set changes, so a caller can tell "nothing new" from
	   "send them again" without comparing. image hands over one
	   picture's corner, the cells it covers, and its own escapes exactly
	   as the program sent them, to be re-emitted at that corner. */
	void (*cellpx)(int id, int w, int h);
	unsigned (*imgen)(int id);
	int (*images)(int id);
	int (*image)(int id, int i, int *r, int *c, int *rows, int *cols,
		     str *out);
	/* Version 5. The whole emulator, written out and read back: grid,
	   scrollback, modes, title, links and pictures. For a server that
	   re-execs and takes its own session back (Gitea #198), which is
	   what `term save` and `term adopt` already do for a script. The
	   file's fields are appended rather than inserted, so a new build
	   reads an old file and an old build reads a new one, each skipping
	   what it does not know -- which is what makes it safe to carry a
	   session across an upgrade at all. 1 on success. */
	int (*save)(int id, const char *path);
	int (*load)(int id, const char *path);
	/* Version 6. The kitty keyboard protocol's flags the program has
	   asked for, 0 for none or no such id -- for a relay to put on each
	   real terminal that speaks the protocol (Gitea #326). */
	int (*kbd)(int id);
	/* Version 7. The rectangle copies (DECCRA) the program made, for a
	   relay to replay on a real terminal that copies too rather than
	   sending every cell of the destination again (Gitea #337).
	   copyseq is how many there have been; copyop hands one over by its
	   number, counting from 1, as rows h by columns w from (r, c) to
	   (dr, dc), 0-based and clipped -- and 0 for one too old to be kept
	   or made before a resize, which the relay then simply diffs. */
	unsigned (*copyseq)(int id);
	int (*copyop)(int id, unsigned seq, int *r, int *c, int *h, int *w,
		      int *dr, int *dc);
	/* Version 8: how many milliseconds a frame the program opened with
	   synchronized output (mode 2026) may still hold back a render -- 0
	   when none is open, and 0 once its one-second guard has passed --
	   so a relay presents a frame whole, never half of one (Gitea #338). */
	long (*syncleft)(int id);
};

#endif
