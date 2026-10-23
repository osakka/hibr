#ifndef HIBR_GFXASK_H
#define HIBR_GFXASK_H

#include "hibr.h"

/* Asking a terminal which pictures it draws, and reading what it says --
   one parser for the two places that ask (Gitea #258). The console asks
   the terminal it draws on, as answers arrive among the keys; hold's
   attaching client asks its own terminal once, before it joins a session,
   so the server knows what each display can be sent.

   Both capabilities answer for themselves. The kitty protocol has a query
   of its own -- one pixel, `a=q`, no `q=` so the answer is not suppressed
   -- which kitty replies `OK` to and a terminal that does not speak it
   replies nothing to. Sixel is in the primary device attributes: a reply
   carrying 4 among its parameters says so, and one without it says the
   opposite, which is the one *negative* either query can give. They are
   asked in that order, so a device-attributes reply with no kitty answer
   before it is a real no to kitty as well (Gitea #216). */

/* What a display's terminal draws, and what each answer means on either
   side of hold:

     answer    hold sends pictures             the desktop draws pixels
     kitty     a kitty transmission            if the session draws kitty
     sixel     a sixel                         if the session draws sixel
     none      never                           never
     unknown   never                           never
     unasked   any it contains, as before      by the session's own answer

   none is a terminal that answered and draws neither; unknown, one asked
   that answered nothing. unasked is a display attached before displays
   were asked at all -- carried across `hold upgrade` from an older server,
   or seen by a desktop whose server cannot answer `hold gfx` -- and gets
   what it got before 0.99.235 (Gitea #274). gx_probe never returns it,
   and no client can declare it: it is only ever a default. */
#ifndef GX_UNKNOWN
#define GX_UNKNOWN 0
#define GX_NONE 1
#define GX_SIXEL 2
#define GX_KITTY 3
#define GX_UNASKED 4
#endif

/* The questions, kitty's first and the device attributes last, as one
   string to write. Between them is the kitty keyboard protocol's own query,
   `CSI ? u`, answered `CSI ? flags u` by a terminal that speaks it (Gitea
   #326): asked before the device attributes for the same reason kitty's
   picture query is, so an answer to those with none before it is a no.
   The mode request for 1016, SGR-Pixels (Gitea #370), goes the same way:
   a terminal that reports the mouse in pixels says so, and one that says
   nothing before its device attributes does not. */
extern const char gx_ask[];

/* An APC at the front of p: 0 when more bytes are needed, -1 when it is not
   one (or is longer than max), 2 when it was taken, with *used its length
   and *kit 1 when it was kitty's `OK`. */
int gx_apc(const char *p, size_t n, size_t max, size_t *used, int *kit);

/* A control sequence with the private `?` prefix at the front of p -- a
   report, never a key: the same three answers as gx_apc, with *da 1 when it
   was the device attributes (final `c`) and *sixel 1 when 4 was among its
   parameters, and *kbd 1 when it was the keyboard protocol's answer (final
   `u`). *mpix is 1 when it was the answer to the mode request for 1016 and
   said the terminal knows the mode, 0 when it said it does not, and -1
   for any other report. */
int gx_report(const char *p, size_t n, size_t max, size_t *used, int *da,
	      int *sixel, int *kbd, int *copy, int *mpix);

/* Ask the terminal on in/out, wait up to ms for its answers, and say what it
   draws: GX_KITTY, GX_SIXEL, GX_NONE, or GX_UNKNOWN when nothing answered.
   HIBR_GFX, when set, is the answer without asking, as it is for the
   console. Anything else read meanwhile -- a key pressed while it waited --
   is appended to rest, for the caller to pass on. *kbd is 1 when the
   terminal answered the keyboard protocol's query, and *mpix when it said
   it reports the mouse in pixels. HIBR_GFX skips the asking, and with it
   those questions, which then answer 0. */
int gx_probe(int in, int out, long ms, str *rest, int *kbd, int *copy,
	     int *mpix);

/* Milliseconds on a clock that only goes forward. */
long gx_now(void);

/* A capability as the word `hold clients` prints, and back. */
const char *gx_name(int g);
int gx_byname(const char *s);

#endif
