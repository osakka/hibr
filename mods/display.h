#ifndef HIBR_DISPLAY_H
#define HIBR_DISPLAY_H

#include "hibr.h"

/* The interface a display backend offers through hibr_provide, under the name
   "display". The console backend draws cells on a terminal; a framebuffer or
   an SDL backend would offer this same table, and a tool written against it
   would not know the difference. Anything added here is a new version;
   anything reordered or removed breaks every tool already using it. */

#ifndef DP_API_VER
#define DP_API_VER 6u
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

/* Framing, ECMA-48's SGR 51 as Blit draws it: a line along each framed
   cell's top and bottom edge, which is how a button's label and a window
   title's cells are outlined (Blit ADR 0210). It is a kind, not a flag,
   held in three bits of the attributes as SGR 51's own sub-parameter: 1
   thin, 3 thick, 2 a Slate title bar's rows (4 is the same), 5 a rule
   along the cell's bottom edge alone (a note's strip). Nothing
   asks for it unless a pen names it, so a terminal that has never heard of
   SGR 51 is never sent one. */
#ifndef DP_FRAME
#define DP_FRSHIFT 7
#define DP_FRAME (7u << DP_FRSHIFT)
#define DP_FRTHIN (1u << DP_FRSHIFT)
#define DP_FRSLATE (2u << DP_FRSHIFT)
#define DP_FRTHICK (3u << DP_FRSHIFT)
#define DP_FRSLATEDARK (4u << DP_FRSHIFT)
#define DP_FRRULE (5u << DP_FRSHIFT)
#endif

/* A window's shadow as Blit draws it, SGR 66 (Blit ADR 0211): a thin line
   along the cell's edge, in the line colour or the foreground, behind the
   glyph, so a frame's cell and a shadow share one. Its kind in three bits
   (1 right, 2 right from a window's first row, 3 under, 4 under from the
   first cell, 5 the corner), a bit for one pixel rather than two, and a bit
   for flush rather than inset, the 0 a shadow beyond a box-drawn frame wants.
   Only the desktop asks for it, on a display that says it draws Blit's. */
#ifndef DP_SHADOW
#define DP_SHSHIFT 10
#define DP_SHKIND (7u << DP_SHSHIFT)
#define DP_SHW1 (1u << 13)
#define DP_SHFLUSH (1u << 14)
#define DP_SHADOW (DP_SHKIND | DP_SHW1 | DP_SHFLUSH)
#endif

/* How Blit draws its colour icons in this cell, SGR 68 (Blit ADR 0221): its
   own colours when clear, 1 in the cell's foreground, 2 black, 3 white. It
   touches nothing but plane 16's icons, so a menu bar can be monochrome
   beside a colour desktop. */
#ifndef DP_ICON
#define DP_ICSHIFT 18
#define DP_ICON (3u << DP_ICSHIFT)
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
	   been written.

	   DP_IMG_FILM says this is a frame of a film, one of a stream: the
	   backend may send it at less than the cells' own resolution when it
	   has been asked to (`console filmdetail half`), since a film's cost
	   is bytes a frame. A backend that does not know the flag ignores it,
	   which is the full picture, so it needed no new version. */
#ifndef DP_IMG_CHOSEN
#define DP_IMG_CHOSEN 1u
#define DP_IMG_UNDER 2u
#define DP_IMG_FILM 4u
#endif
	int (*image)(sh *s, const char *pane, int row, int col, int h, int w,
		     const unsigned char *rgb, int iw, int ih, unsigned flags);
	/* Whether pictures can be placed at all, and the pixel size of a cell:
	   0 in w and h when the terminal does not say, which is the same thing
	   as "no pictures". A caller needs this to decide how many pixels to
	   render for a rectangle of cells. */
	void (*cellpx)(int *w, int *h);
	/* "The picture you already have at this rectangle, drawn under the
	   text, is still wanted": 1 when there was one, 0 when the caller must
	   place it for real. Placing it again answers the same question, and
	   costs a hash of every pixel to do it -- 19.6 ms for a screenful at
	   232x71 -- so a caller that knows it would hand over the same picture
	   says it this way instead. Only an under-text picture can be kept:
	   one that owns its cells is dropped the moment they are drawn
	   through, which is its own answer (version 6). The rectangle is all
	   this matches on, so the caller must *know* the picture there is its
	   own and unchanged -- it is a way of answering a question the caller
	   has already settled, not a way of asking one. */
	int (*imgkeep)(sh *s, int row, int col, int h, int w);
};

/* A second, smaller interface beside "display", under the name "scroll",
   so a display can offer it without "display" changing version -- every
   tool outside this tree that asks for "display" by its exact version would
   otherwise stop finding it (Gitea #353). What a terminal window offers its
   display before drawing: "the band of cells at this rectangle has moved up
   n rows since I last drew it" (down when n is negative). A display that
   can move cells on its terminal does so, and then the drawing that follows
   costs only the rows that are new; one that cannot says 0 and the drawing
   costs what it always did. A hint that is wrong costs bytes, never a
   wrong screen: the display moves what it believes the terminal shows, and
   what it is drawn next is compared against that. */
#ifndef DPS_API_VER
#define DPS_API_VER 1u
#endif

typedef struct dps_api {
	int (*scroll)(int row, int col, int h, int w, int n);
} dps_api;

/* A third, under the name "dpframes" (Gitea #486, Blit ADR 0223): a
   window's picture whose frames arrive by descriptor from another program
   -- a dmabuf, or a sealed memfd of RGB -- and are passed on to Blit
   without the console ever holding a pixel. A stream is a number its
   caller chooses. Its picture keeps one kitty id and one placement for
   its whole life, and a move, a resize or a raise places that placement
   again where it now is, because Blit keys a game's input route by the
   image: a new id would end the route and let go of every held key.

   can: whether frames by descriptor can go now. 2 with dmabufs, the
   formats every display imports in fmts and whether every one waits on a
   fence in *fence; 1 with memfds only; 0 with neither, when the caller has
   to hand RGB to `image` itself.

   place: from the window's own drawing, every time it draws. The stream's
   picture stays in this rectangle of the pane, below the cells as any
   window's picture is, and goes when the pane draws without placing it,
   as any does. 1 when placed, 0 when it cannot be.

   frame: from the caller's own reading, whenever a frame arrives. h is
   the producer's header (BF_HDR for a memfd of RGB, BF_HDR3 for a
   dmabuf), with its own id and sequence; the descriptors stay the
   caller's. Kept as the stream's newest, and passed on at once under the
   console's id when the picture is placed. 1 when taken, 0 when frames by
   descriptor cannot go, which the caller answers as ENOTSUP.

   drop: the stream has ended; its picture goes.

   answers: what each display's Blit says of a stream's frames, handed to
   fn as "RELEASE <stream> <seq>" or "ERROR <stream> <code>": one RELEASE
   per frame however many displays showed it.

   hook, unhook: a descriptor the console's own wait services without
   waking the script, fn being called when it is readable; fn answers 1
   when the script should wake after all. This is what lets a frame and
   its RELEASE pass at sixty a second without costing the desktop a frame
   each. */
/* Version 2 (Blit 0.3.64): input, a stream's input route -- "sock" with the
   socket blit-compositor offers (an "eis" route went with Blit's gamescope
   bridge in 0.99.483), "on" or "off" as its window gains and loses
   the keys -- told to Blit once a frame of it has gone; a picture out of
   view keeps its id (Gitea #499), so the route stays; what Blit says of it comes back by
   answers as "OK input <stream>" or "ERROR INPUT <stream> <code>". */
/* Version 3 (Gitea #496, Blit 0.3.74): sub, a program window's subsurface
   -- a picture of its own over or under its window's (Firefox draws its
   page into one) -- from the window's own drawing, as place. The window's
   body is the h by w rectangle at row, col of the pane, as given to place;
   the sub is the pw by ph pixels at px, py of that body's own pixels, drawn
   at its own size, offset within its first cell and cut to the body, never
   scaled; slot is its place in the window's layers, below the window's
   picture when negative and above it when positive, the further from 0
   the further from it. 1 when placed, 0 when it cannot be or nothing of it
   is in the body.

   crop: place, showing only the sw by sh pixels at sx, sy of the stream's
   picture, scaled into the rectangle as place's whole picture is: a program
   window whose buffer holds a shadow round it (Wayland's window geometry,
   Gitea #496) is shown as its window alone. */
#ifndef DPF_API_VER
#define DPF_API_VER 3u
#endif

typedef struct dpf_api {
	int (*can)(sh *s, str *fmts, int *fence);
	int (*place)(sh *s, const char *pane, int row, int col, int h, int w,
		     unsigned sid);
	int (*frame)(sh *s, unsigned sid, const unsigned char *h, size_t hn,
		     const int *fds, int n);
	void (*drop)(sh *s, unsigned sid);
	void (*answers)(void (*fn)(void *ctx, const char *line), void *ctx);
	int (*hook)(int fd, int (*fn)(void *ctx, int fd), void *ctx);
	void (*unhook)(int fd);
	int (*input)(sh *s, unsigned sid, const char *verb, const char *path);
	int (*sub)(sh *s, const char *pane, int row, int col, int h, int w,
		   unsigned sid, int px, int py, int pw, int ph, int slot);
	int (*crop)(sh *s, const char *pane, int row, int col, int h, int w,
		    unsigned sid, int sx, int sy, int sw, int sh);
} dpf_api;

#endif
