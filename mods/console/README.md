# mods/console

A text display for other things to be built on. The line editor owns one line; this
owns the terminal — an alternate screen, a grid of cells, a redraw that sends
only what changed, and keys decoded once into names.

It exists because four wanted tools need the same piece: a `vi`, a `most`, a
system monitor, and anything else full-screen. Building it once is the point.

| file | role |
|---|---|
| `cn.h` | the cell, the grid, the pane, and every `cn_` entry point |
| `sixel.c` | a picture as the bytes a terminal paints: the fixed 6x6x6 palette (a film: the same palette every frame, so the terminal keeps its registers) or one chosen by median cut (a still picture). A band is collected in one pass over its own pixels rather than one pass per palette colour, which is 2.5 times faster -- 23 ms a frame at 800 by 544, against 0.5 ms for half blocks |
| `kitty.c` | the same picture as the kitty graphics protocol's own escape, which kitty, ghostty, WezTerm and konsole speak and kitty alone accepts (it has never drawn a sixel). A picture is an object with an id rather than paint: cheaper to encode, an order of magnitude more bytes, and every one placed is deleted later. A **still** picture's payload is compressed first (`console imgcomp`, Gitea #129): `o=z`, a zlib stream of the same pixels, by default, or `f=100`, a PNG, which is smaller again and is not the default because a terminal that takes `f=24` and not `f=100` draws nothing and `q=2` means it cannot say so. The owner's own wallpaper at 232x71 is 2.11 MB of base64 raw, 0.65 MB as `o=z` and 0.47 MB as a PNG, which is what a reattach over a link costs. A film's frame is never compressed: a different picture every frame, so the cost would be the encoding rather than the bytes |
| `../deflate.c`, `../png.c` | the encoder behind that, and the PNG it writes with it -- deflate as RFC 1951 defines it (hash-chain LZ77, dynamic Huffman per block, a stored block when a compressed one would be no smaller) and a PNG of filtered scanlines around it, with nothing linked. `../inftab.c` holds the tables this and `../inflate.c` both read |
| `image.c` | pictures as regions of the grid the console owns -- placed, kept, dropped when their cells are drawn through, and for the kitty protocol deleted from the terminal when they are (ADR 0037) |
| `term.c` | taking and giving back the terminal, and the signals that guarantee it |
| `grid.c` | the front and back buffers, placement, and the diffing flush |
| `key.c` | bytes to key names: CSI, SS3, modifiers, mouse, bracketed paste, the kitty keyboard protocol's `CSI u` named as the old form of each key with `super-` in front (Gitea #326); the wait, with the descriptors it watches and the guard that stops waking for a watched pipe nobody reads (Gitea #323) |
| `console.c` | panes, colour parsing, and the `console` builtin |
| `shot.c` | `console shot`: the front grid written out as ANSI, HTML or plain text |

Every `console` subcommand, with what it takes, is in the command table of
[Full-screen programs](../../docs/display.md); this page is how the module
works underneath.

## The model

Two grids. `cn_put` writes into the **back** buffer, which is just memory;
nothing reaches the terminal until `cn_flush` compares back against **front**
and emits the difference. So a program redraws everything it wants on screen,
every frame, and pays only for what actually moved.

Measured, on an eighty by twenty-four terminal:

| | bytes written |
|---|---|
| the first flush, painting everything | 2095 |
| writing four characters | 11 |
| changing one of them | 8 |
| flushing with nothing changed | **0** |

A cell holds a codepoint, a width, a pen, and a pointer that is null until
combining marks arrive — so the common case costs no allocation and `e` plus
U+0301 still shares one cell with its mark. A wide glyph occupies two cells,
the second marked as a continuation, and writing over either half clears both:
half a glyph is never left on screen.

`console darken row col h w [pct]` scales a rectangle of the **back** buffer's
existing colours toward black by `pct` percent (55 if not given) and sets the
dim attribute, in place — a shadow cast on whatever is already drawn there,
before whatever casts it draws its own opaque content over its own footprint
on top. Only an RGB colour can be scaled exactly; a palette index or the
terminal's own default colour has no table to scale by, so those cells get
the dim attribute alone. It reads back what `cn_put` already wrote, which
`cn_fill` and `cn_put` themselves never do.

A cell left at the terminal's own default colour is the one case where "scale
what is there" has no answer, and it means two different things. `console
darkdefault fg bg` names the colours such a cell should be taken to be -- the
desktop's own plain ink on face -- so a shadow over a terminal's uncoloured
output darkens like everything around it. But over a **picture drawn under
the text** a default cell means the opposite: the wallpaper writes exactly
such cells so the terminal composites the picture through them, and darkening
toward the theme's face painted an opaque near-black band over the photograph
and took the picture with it (Gitea #184). So a cell with no background of its
own that sits over an under-text picture is darkened from **the picture's own
colour there**, which is what a shadow on a photograph looks like; everywhere
else `darkdefault` still decides.

One colour is the display's to change rather than the caller's, and only
over a picture drawn under the text. A terminal compositing such a picture
decides *per cell* whether that cell has a background of its own, and kitty
decides it by comparing the cell's **resolved** value with the window's own
background colour -- `cell_has_default_bg = 1 - step(1, abs(bg_as_uint -
bg_colors0))` in its own shader. So a cell a program painted an explicit
black, on a terminal whose background is black, counts as having no
background and the picture is let through it: every ncurses program in a
terminal window over a picture wallpaper was see-through, while every window
drawn in a theme's own RGB stayed solid (Gitea #215). Nothing the display
computes is wrong there, so the only way to keep such a cell opaque is to
paint it a colour that is not the terminal's own -- one in the last channel,
the same colour to look at and not the one being compared against. That
needs knowing what the terminal's background is, so the display **asks**:
OSC 11 for the background and OSC 4 for each of the sixteen palette entries
an index can name, at open and again whenever a resize says the terminal may
not be the one that was asked. The replies arrive as input and are swallowed
by the key decoder (`cn_osc`), never handed on as keys. `console termbg`
answers what it learned, and whether it was answered or assumed -- black is
the assumption, since every terminal this was reported from has a dark
background and the cost of assuming wrong is a cell a shade off rather than
a cell that disappears. A **translucent** write goes through the same test,
since its colour is the picture's own scaled and can land there by
arithmetic rather than by anybody's choice.

Which of the two protocols is in force is asked of the terminal rather than
read off its name (Gitea #216). A name answers what a *family* usually
does, and inside `hold` it is the **session's own** name, frozen when the
session was started -- so a session begun in kitty went on sending kitty
escapes to whatever terminal it was later attached from, and one begun
anywhere nameless went on drawing half blocks to a terminal that can place
pixels. Both capabilities answer for themselves: the kitty protocol has a
query of its own (one pixel, `a=q`, no `q=` so the reply is not suppressed)
which kitty answers `OK` and a terminal that does not speak it answers not
at all, and sixel is in the primary device attributes reply, whose
parameters carry 4 where it is supported. The queries go out beside the
colour ones, in that order -- kitty's first, since a terminal answers in
the order it was asked, which is what makes *"answered what it is without
answering kitty's query"* a real no rather than a silence. A positive is
kept across a re-ask, so a plain resize never drops to the name list for a
frame; `HIBR_GFX` still wins over everything, being a person's own choice.
A terminal that answers neither query is read by name exactly as before.

**A name is enough to decide how to draw text and not enough to commit a
bitmap** (Gitea #249). Between a display opening and the terminal's reply
arriving, nothing has answered and the name list is all there is -- so a
held session whose frozen name says kitty placed a kitty bitmap on a plain
xterm, once, in that window. The console claims the cells a region covers
and skips them in the diff, the terminal drops the payload, and an
under-text region is kept for as long as its caller places it again, which
a wallpaper does on every frame: a black rectangle nothing repaints, for
the life of the session. `cn_gfxsure` is that distinction -- a protocol
*answered*, or forced with `HIBR_GFX`, as against one read off a name --
and `cn_image` places nothing while it is not sure, so `img draw` falls
through to the cells path every caller of `dp->image` already keeps. A
frame or two of half blocks at startup costs nothing. `console gfx` itself
is unchanged: it is asked what a terminal most likely draws, and a name is
a fair answer to that question.

`console translucent pct [colour]` is the same substitution for a *write*
rather than a darken: while it is set, a cell written with no background of
its own takes the picture's colour at that cell scaled to `pct`, or `colour`
where no picture is under it. That is how a window the wallpaper shows
through is drawn -- a cell has no alpha, so the colour has to be computed,
and it is a different colour in every cell, which no single background could
say. `off` ends it, and so does the next `console flush`: it lasts for a
drawing, not for ever, so no caller can leak it into whatever is drawn next.
The averaged colour per cell is kept with the picture's own region, worked
out in the pass that already walks those pixels to hash them, and it costs
about 49 kB for a full screen.

`console consumed` answers how many bytes of input have been turned into keys
since the console was loaded -- what the desktop's idle marker reports, under
`HIBR_TESTIDLE`, so a test harness knows its keys have been read.

`console drawn` answers how many times something has been written at absolute
coordinates rather than through a pane. Its own value means nothing and it is
never reset: what it is for is being compared with itself a moment later. The
console cannot tell a caller that owns its cells from one drawing over the
whole finished screen, so this is how a script learns that *something* did --
the desktop reads it either side of the part of a frame where only notes,
menus, dialogs and a drag's ghost draw, and makes the frame after one of those
a full one, since nothing else would repaint what it covered. Counting it here
is what stops that being a list of overlays somebody has to remember to add to.

`console waiting` succeeds when more input is already there -- read and not
yet handed out, or readable on the terminal right now -- and never waits.
The desktop asks it before drawing a drag or a wheel: while the next report
is already waiting, the frame would only be overwritten, so a burst of a
hundred drag reports draws two frames, not a hundred and two, and the window
never trails behind the mouse.

`console shade colour [pct]` answers the colour `darken` would turn a
`#rrggbb` into, as another `#rrggbb` — through `:=`, or printed. It is how
something drawn inside a pane, where `darken` cannot reach, gets a shadow of
exactly the same depth: the desktop's buttons draw theirs as `▄` and `▀` in
this colour. The formula is `darken`'s own, in one place.

`console pane top name` puts one pane above everything (Gitea #293): its
cells are written only by drawing through it. The check lives in the grid's
three writers -- `cn_put`, `cn_fill`, `cn_darken1` -- as `cn_guard`, two
comparisons against the rows any top pane spans (`cn_ttop`, `cn_tbot`) for
every cell outside that band, so a full-screen fill pays nothing measurable;
`cn_pput` and `cn_pfill` name the pane being drawn through in `cn_wpane`,
which is how the pane's own drawing gets past. A bitmap cannot leave a hole,
so `cn_image` places a picture that owns its cells as the parts above and
below the pane, each its own region over its own rows of the bitmap (a row
of RGB at an offset into the buffer, nothing copied); one drawn under the
text is left whole, since the pane's cells are over it already. A picture
that reaches past the pane's columns is still cut by whole rows, so the part
of it level with the bar and beside it goes too, on a display beside the
primary -- one placement is a rectangle, not an L. `console hit` asks the
top panes first. The desktop's menu bar is the one user: a window dragged
across it, its shadow, a menu or a picture goes under the bar.

## Naming

Everything is `cn_`, not `sc_`. `src/net.c` already uses `sc_` for schemes and
exports `sc_fini(sh *)` — which is exactly the signature a module finaliser
has. A module named `sc_fini` is silently preempted by the shell's under
`-rdynamic`, so the terminal would never have been put back and nothing would
have crashed to say so. See the trap in `CLAUDE.md`.

## Right-to-left text

Text a script puts with `console put` that holds Hebrew, Arabic or another
right-to-left script is drawn in display order: the `uni` module's
bidirectional algorithm, Arabic in its joined presentation forms, marks after
their base, brackets mirrored (ADR 0031). A pane's text is clipped first and
ordered after, so a right-to-left line keeps its start. Plain text never
reaches `uni` -- a byte scan decides, and box drawing is never mistaken for
it -- and a program's own cells (a terminal window, `most`, `hvi`) go through
the display interface untouched, since the program decides their order.

    console bidi off    # for a terminal that reorders right-to-left itself
    console bidi on
    console bidi        # status 0 when on

Until a script says, it follows `HIBR_BIDI`: on unless that is `off`, the
same setting the line editor reads.

## The mouse

Off until something asks for it:

    console mouse click     # presses and releases
    console mouse drag      # and dragging
    console mouse motion    # and every movement, which is a lot
    console mouse off
    console mouse           # which of those is in force now

With no mode it answers rather than sets: `off`, `click`, `drag` or
`motion`. That is the mode the console was *asked* for, which is all
anything can know -- a terminal never says whether it obeyed.

**Off is the default on purpose.** Turning reporting on takes click-and-drag
text selection away from whoever is watching, and that is too rude to do to
every full-screen program.

A report comes back through the same `key` call as everything else, named the
same way:

    mouse press left 3 12
    mouse release left 3 12
    mouse drag left 9 40
    mouse wheelup 4 4
    mouse ctrl-press left 6 6
    mouse shift-press left 8 8

Row then column, counted from zero, matching `put` — so a press can be used as
a position without arithmetic. Modifiers prefix the action in the same order
keys use. The SGR form (`1006`) is always requested alongside, because the
older encoding cannot report a column past 223.

## Giving the terminal back

The one thing a full-screen program must never do is leave a terminal in raw
mode. `SIGINT`, `SIGTERM` and `SIGHUP` restore the terminal and then re-raise
the signal, so the shell still dies of what killed it, and so do the crashes:
`SIGSEGV`, `SIGBUS`, `SIGABRT`, `SIGFPE` and `SIGILL`. The module's finaliser
does the same on `mod drop` and at exit. `SIGWINCH` only sets a flag — the
grids are rebuilt in the next flush, never in the handler.

`console signals off` hands ctrl-c, ctrl-\\ and ctrl-z to the program as the
keys `ctrl-c`, `ctrl-\` and `ctrl-z` instead of raising signals; `on` puts
them back, and `console close` restores whatever the terminal had anyway. A
program that is left only through its own Quit wants this -- the desktop does.

`console clip text` puts text on the clipboard of the terminal the screen is
on, with OSC 52. The terminal decides whether to honour it; nothing depends
on its answer. `console bell` rings its bell, and `console notify text`
asks it for a notification (OSC 9), which a terminal that does not know the
sequence simply ignores.

`console link url` makes what is written after it a link, the way `console
pen` colours it, until `console link -`; a cell keeps its link like its
colours, the diff sends a link only where it changed, and each flush closes
an open one before it ends, so nothing written outside the console's frame
is ever caught in a link. On a terminal that knows OSC 8 the text is
clickable; elsewhere it is plain text. The display interface carries it as
`link` (version 4), which is how the `term` module keeps a link a program
printed in a terminal window a link on the real terminal.

**A resize asserts the terminal's modes again and repaints everything.** The
terminal on the other end may not be the one the screen was opened on: a
session reattached with `hold` arrives as a `SIGWINCH` on a terminal that has
never seen the alternate screen, the hidden cursor or the mouse mode. So
`console resized` sends them again and invalidates the front grid, and the
next flush draws the whole frame. It costs one full frame on an event that is
rare anyway.

**`console resizing` and `console reassert` split that in two**, for a caller
that wants to debounce a burst of them into one redraw at the settled size
rather than repainting once per pixel a drag moves through. `resizing` peeks
at the same flag `resized` consumes, with none of the side effects -- cheap
enough to poll every tick -- and `reassert` does the mode-reassertion and
invalidation unconditionally, once the caller has decided the resize is
over. The desktop uses exactly this: freeze on the first `resizing`, keep
polling without redrawing while it keeps reporting one, and only call
`reassert` and redraw once it has gone quiet for a while.

**A resize that lands before the wait begins still ends the wait.** A
`SIGWINCH` interrupts a `select` that is already blocking, but one that
arrives a moment earlier — while the caller is still handling the key
before — does not, and the wait then runs its full timeout with the resize
unseen. That was invisible while the desktop redrew on a fixed tick, and
became a 60-second stall once it stopped: choosing a new primary display
makes `hold` send the signal while the click that chose it is still being
handled. The handler counts resizes, `cn_wait` blocks `SIGWINCH` while it
compares the count with the last one it reported, and waits in `pselect`,
which unblocks it atomically. Each resize ends exactly one wait early; a
program that never asks `resized` does not spin on a flag it never clears.

## Testing

`tests/console.py` drives all of it through a pseudo terminal, because none of
it happens without one; `tests/640-console.t` covers what `run.sh` can reach,
which is the no-terminal behaviour and the argument checking.

    python3 tests/console.py
