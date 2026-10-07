# Changelog

## 0.99.84

**A resize killed a desktop with a signal 11, and three shadows pulsed.**
All four are 0.99.82's, reported from a live session within a day of it.

**The crash.** The map that says which cells a pane covers -- what lets the
wallpaper paint around the windows -- is allocated for the grid as it was
when it was built, and `cn_skip` bounds-checked the *current* grid before
indexing it. `cn_fitq` acts on a SIGWINCH in the middle of a frame, so a
terminal resized larger left the map smaller than the grid and the index ran
past the end of its own allocation: an out-of-bounds read, and the desktop
died. The supervisor restarted it and put the windows back from the
snapshot, but a terminal's program is a child of the process that died and
nothing could adopt it -- so what the owner saw was every window in place
and every shell gone. The map carries its own rows and columns now and
checks against those; a cell it does not cover is one the screen has only
just grown into, which no pane owns, so it is painted rather than skipped.
Found by reading rather than by a test: the window is a signal arriving
between two statements, which no suite can be made to hit on purpose.

**The shadows.** `console darken` is cumulative, and 0.99.82 paired only the
*windows'* shadows to "cast only onto wallpaper this frame painted". The
bar's, an open menu's and the Control Strip's were still cast on every
frame, so each grew a shade darker until the once-a-second wallpaper repaint
put it back: a shadow visibly pulsing, once a second. All four are paired
now.

**The Control Strip.** It is drawn on the screen itself rather than through
a pane, so the wallpaper paints over it -- and its own gate let it skip
exactly that frame, leaving it missing until the next frame it chose to
draw. "It disappears every second or so" was that. It now always draws on a
frame that painted the wallpaper.

And the wallpaper is not gated at all while a menu is open: a drop-down is
drawn on every frame it stays open and casts a shadow on whatever is under
it, window or wallpaper, so the cells beneath it have to be painted that
frame. Painting the wallpaper for as long as a menu is up costs 1.4 ms a
frame during an interactive moment, against a shadow that fades the instant
the window under it redraws.

## 0.99.83

**A program writing does not wake the desktop for a frame it has already
decided to hold.** `DT_TERMMS` holds an output-driven frame a while, and
until now every byte a program wrote still woke the desktop to be told so:
three programs writing a hundred times a second woke it three hundred times
a second, and each wake drained a pty and went back to sleep. The pty's own
buffer is where those bytes belong until there is a frame to put them in.

`cn_wait` leaves the watched descriptors out of its `pselect` when asked
(`console key -q`), so a wait that is only running down the clock ends on the
clock -- or at once on a key, a click or a resize, because the terminal's own
descriptor stays in. `dt_run` uses it for the remainder of a held frame,
having drained first. The desktop's own share of three programs each writing
every 10 ms: **16.6% of a core, now 9.9%**.

**What that did not buy, said plainly.** The point of holding frames was to
be able to *raise* the rate once wakes were cheap -- to pair the desktop to a
display's own refresh rather than to a cost. It is not there yet:

| `DT_TERMMS` | frames a second | CPU |
|---|---|---|
| 16 | 60.5 | 39.5% |
| 33 | 29.8 | 23.9% |
| 70 | 14.2 | 13.2% |
| 140 | 7.1 | 8.1% |

About 6 ms a frame whatever the rate -- of which `dt_draw` is **1.57 ms**.
The rest is `dt_run`'s own loop, twice a frame at about 2.2 ms a pass, and
stubbing every periodic thing it calls (`dt_updcheck`, `dt_remotepoll`,
`dt_snapcheck`, `dt_tickers`, `dt_saverwait`) accounts for only 0.6 ms of
that. So the loop, not the drawing, is the ceiling now, and finding the rest
means timing its sections rather than guessing at them. 60 Hz is affordable
when a pass costs what a frame costs.

Two measurement traps met on the way, both worth more than the numbers.
Emptying `DT_TESTIDLE` to measure a desktop without the harness's idle
marker also stops `dt_supervise` returning early, so a supervisor starts and
the pid the harness forked is no longer the one drawing -- it measured 0.0%
of a core, which is what measuring the wrong process looks like. And once the
whole tree was measured instead, the marker turned out to cost nothing at
all (14.0% against 13.9%): the suspicion was wrong and the measurement said
so.

## 0.99.82

**The desktop draws only what has changed.** A frame on the owner's own
shape -- 232x71, three terminals, two sticky notes, a 3840x2160 wallpaper --
timed by calling `dt_draw` fifty times in a loop:

| | ms a frame |
|---|---|
| 0.99.80 | 7.0 |
| 0.99.81 | 5.8 |
| **0.99.82** | **1.05** |

and what that is worth where it shows: typing eight keys a second costs 3.0%
of a core against 8.5%, the same typing with pictures as real pixels 3.6%
against 93.3%, and an idle desktop 0.2% either way.

**What made it possible, and it is not what it looks like.** A pane is a
name and a rectangle -- it has no cells of its own -- and `cn_flush` copies
each changed cell into the front grid and leaves the back grid alone. So a
cell nobody rewrites keeps last frame's value and flushes as nothing: the
screen already remembers. The one thing stopping a window being left alone
was `dt_wall` painting over it every frame.

So the console learned to **paint behind the panes**. It keeps one byte a
cell saying whether a pane covers it, and `console behind on` makes writes
at absolute coordinates skip those cells. `dt_wall` paints around the
windows now, and because the img module draws through the same `dp->put`, a
picture wallpaper skips them too with no change to img at all. Three things
fall out of that, each of which the alternatives needed machinery for: a note
or a menu that has gone had its cells outside the windows, so the wallpaper
paints them back without anything tracking it; a window that moved leaves
cells that are outside every pane, likewise; and nothing has to be told a
window closed -- though the map does have to be thrown away when a pane is
dropped, which the first version forgot, leaving closed windows on screen.

**A window is left alone when its app says nothing changed.** `<app>_dirty`
is opt-in: an app without one is drawn every frame exactly as before, so a
missed case is impossible rather than unlikely. `term_dirty` reads a count
the emulator keeps of what its program has been fed (`term gen`), with the
size, the focus, the scroll position and whether the program is still there;
`stickies_dirty` a count its own keys and mouse bump. And `dt_alone` decides
which windows may be left alone at all: one whose rectangle *and the strip
its shadow falls on* touch no other window's. Two that overlap are always
both redrawn.

**Shadows, which is where this would have gone quietly wrong.** `console
darken` is cumulative: the same cells darkened twice come out twice as dark.
So a shadow may only be cast onto cells painted this frame -- `dt_wall` says
whether it painted and `dt_win` obeys -- and where two windows overlap, one's
shadow falls on the other and the only arrangement in which every shadow is
sure of fresh cells beneath it is the one where the wallpaper paints as well.
Overlapping windows therefore cost the wallpaper's own saving. That is a real
cost, stated rather than hidden; a layout with nothing overlapping keeps it.

**The wallpaper and the Control Strip draw when something changed, and at
least once a second.** The wallpaper's signature holds everything it paints
and everything that is drawn over it and can go away again -- the menus, a
drag, the notes, the identify box, standby, a confirm, a screenshot, the
saver, the lock, the workspace, the displays. The ceiling is the honest part:
anything the signature does not know about cannot be seen for longer than a
second.

**Three controls that would have gone on claiming to work.** Each of these
has its own `dt_want` or its own timer *inside* the drawing that is now
sometimes skipped, and each would have failed silently:

- Cursor Blink asks for its next frame from inside `term_draw`, so a focused
  terminal stays dirty while the setting is on. With it off -- the default --
  a terminal nobody has typed into is not drawn at all. Its own comment used
  to say blinking was free because a focused terminal redrew every 30 ms,
  which stopped being true when that was fixed and is now plainly false.
- The desktop icons' rescan sat inside `dt_wall`; gating the wallpaper would
  have stopped a mounted disk ever being noticed again. It is outside now.
- The bar's clock asks for its next tick from inside `dt_bar`. Gating the bar
  would leave it telling the right time once and then lying, so the bar still
  draws every frame and keeps its 0.19 ms. That is the trade, said plainly.

**And the oracle that guards all of it** (`DT_FORCEDRAW`, deliberately not a
setting anybody keeps). Inside one session, with the screen settled, turning
the skipping off must not change a single cell -- the pens as well as the
glyphs, since a shadow cast twice differs only in colour. Two overlapping
windows, and a drag that moves one of them first, because a move is what
leaves cells where a pane used to be. It found the double-darkened shadow
above. The first version of it compared two separate sessions and failed one
run in three with nothing wrong: two correct desktops settle at their own
pace, so the last frame before a quitting key is not the same frame. One
session, two frames, is the only honest form.

Also: `cn_fill` and the shadow strip of 0.99.81 are in this too, and
`tests/desktop.py`'s own scratch files are named after the process that
writes them, which the gate caught the hard way.

## 0.99.81

**A frame is 17% cheaper, and the measurement that found it is the news.**
Timing a whole desktop under load has about a millisecond of run-to-run
noise, which is the same size as the savings worth having -- so both of
these changes measured as *nothing* until the frame itself was timed
directly: `dt_draw` called fifty times in a loop with the clock read either
side, on their own session's shape (232x71, three terminals, two stickies,
a 3840x2160 wallpaper). 7.0 ms a frame before, 5.8 after, with the two runs
of each within 150 us. Anything measuring a desktop by sampling CPU wants
that instrument first.

**`cn_fill` decodes its glyph once rather than once a cell.** Every cell of
a fill went through `cn_put`, paying a `strlen`, a `u8dec` and a `u8w` to
learn again what the cell before it had already said. A wallpaper is 16,472
cells and every window's own face is another fill:

| | before | after |
|---|---|---|
| fill 71x232 with a space | 626 us | 199 us |
| fill 71x232 with a wallpaper glyph | 742 us | 204 us |

The fast path takes only a single codepoint one column wide and writes
exactly the cell `cn_put` would have, `cn_split` and the freed combining
characters included; a wide glyph or a string still goes the old way.

**A window's shadow darkens the strip that shows, not the whole window.**
The shadow is the window's own rectangle moved a row down and two columns
right, and the window's own face is drawn over it a few lines later -- so
`h` by `w` cells were darkened to leave an L visible: 4,648 of them for one
terminal against 193. It is now two calls, the row under the window and the
two columns beside it, which is the same cells and measured the shadow down
to nothing at all (6.047 ms a frame against 6.017 with shadows off).

Where the 6.0 ms that remains goes, from stubbing one phase at a time:

| | ms |
|---|---|
| the five windows' own `_draw`s | 1.89 (3 terminals 0.96, 2 stickies 0.93) |
| the window chrome around them | 1.50 |
| the wallpaper | 1.45 |
| the Control Strip | 0.49 |
| the menu gate and the bar | 0.58 |
| shadows | about nothing |

Two things that says, for whoever goes further. A terminal's content costs
0.32 ms because `term draw` blits its cells in one call, where a sticky note
costs 0.47 for a tenth of the area, because `tb_draw` walks it row by row in
shell -- the text widgets, not the terminals, are what is expensive now. And
the rest needs the desktop to stop redrawing what has not changed, which is
possible without touching the console (`cn_flush` leaves the back grid
intact, so a cell nobody rewrites costs nothing) but is a real piece of work:
a window kept from last frame means the wallpaper must not be painted over
it, and painting the wallpaper around the windows means the console has to
know which cells a pane owns.

## 0.99.80

**A desktop with busy terminals in it was spending a core on frames nobody
asked for, and the cap it got in 0.99.79 was set too high.** The owner's own
session sat at 33-48% of a core doing nothing: three terminals, two of them
running an AI assistant whose title carries a spinner. Sampling its run state
3000 times found it *running* in 52% of them and otherwise asleep in
`poll_schedule_timeout` -- a timed wait, so it was not blocked on anything;
it was drawing.

Rebuilt exactly -- their screen (71x232), their wallpaper, their calendar,
three terminals each writing every 10 ms -- and measured:

| | frames/s | CPU |
|---|---|---|
| 0.99.78 | 105 | 76.5% |
| 0.99.79, `DT_TERMMS=33` | 29.9 | 28.7% |
| 0.99.80, `DT_TERMMS=70` | 14.1 | 21.1% |
| `DT_TERMMS=200` | 4.9 | 13.4% |

0.99.78 redrew the whole desktop once per burst of a program's output, with
nothing holding it back: **105 frames a second**. 0.99.79 held an
output-driven frame to 33 ms from the last, which is thirty a second -- more
than any text program needs, and twice the cost of fifteen. The default is
70 ms now (`DT_SETVER` 8 moves a file that still holds 33; a value somebody
chose stays), and Control Panel > Desktop > Frames For Output still goes to
200 for whoever wants it lower.

**Pictures as real pixels cost twenty times what they should** (Gitea #108),
reported as "I switched to pixel and look at the load". The cell path in
`img draw` has had a cache for releases; the pixel path had **none**, so it
decoded and resampled the whole picture on every call -- and the wallpaper
calls it every frame, because a region drawn under the text is kept only
while its caller keeps placing it (ADR 0037). Five consecutive draws of a
3840x2160 photograph onto a 232x71 screen:

| | before | after |
|---|---|---|
| first draw | 415 ms | 454 ms |
| and again | 328-362 ms each | **10-22 ms** |

The console's own side was always right: it transmits the picture once and
sends nothing again (the flush measured 0 ms after the first). All of the
cost was `img draw` redoing work for a picture that had not changed. The
whole desktop, typing eight keys a second:

| | frames/s | CPU |
|---|---|---|
| Pixels, before | 2.7 | **93.3%** |
| Pixels, after | 7.5 | 9.5% |
| Half blocks | 7.5 | 8.7% |

So real pixels now cost what blocks do. It is kept as one entry rather than
`IM_CACHEN` of them because the sizes are not comparable: that screen's
pixels are 1856x1136x3, **6.3 MB**, where its cell grid is 130 kB -- four
slots would cost more resident memory than the whole shell. That 6.3 MB is
the price, and it is paid only while something is drawn as pixels.

**The hibr menu's app list is worked out once** (Gitea #109). `dt_draw`
rebuilds every menu whenever anything is typed, and `dt_appmenu` walked the
registry, lowercased every title, sorted them and worked out an accelerator
letter for each of twenty-six apps -- 2.9 ms of every keystroke, to produce
the identical list again, since the registry does not change while a desktop
runs. `dt_appplan` computes it once into `DT_APPPLAN` and `dt_appmenu`
replays it; `dt_app` clears it. 2.9 ms becomes 1.4, and a keystroke's frame
11.2 ms becomes 10.0.

Where the rest of a keystroke goes, measured by stubbing one phase of
`dt_draw` at a time -- a function defined again replaces the first, so a
session file can do this without touching the tree:

| phase | ms of a keystroke frame |
|---|---|
| `dt_menus` (`dt_ctxbuild` is free) | 4.8 |
| every window's `_draw` | 3.1 |
| `dt_wall` | 1.6 |
| `dt_bar` | 0.6 |
| `dt_idle` | 0.6 |
| `dt_stripdraw` | 0.3 |

and the frame is about 7 ms of *fixed* work whatever the screen measures --
an 80x24 desktop with a window costs the same 7.0 ms as a bare 232x71 one --
so this is per-frame script work, not per-cell drawing. The prize still on
the table is not rebuilding the menus for input that cannot have changed
one, worth about 3.5 ms of every keystroke; it needs the contract already
written down (anything changing a shut menu bar clears `DT_MENUIN`) to be
tightened and every app's `_menus` audited against it, which is #109 and a
job of its own rather than a line.

Two things that fall out of the measurement and are worth writing down.
**The frame is the whole cost**: with the same three programs writing and
every window minimised -- so the output is still drained but nothing is
drawn -- the desktop costs **0.1%** of a core. Draining is free; drawing is
not. And **a frame gets dearer as the cap gets longer** (11 ms at thirty a
second, 27 ms at five), because damage accumulates between frames, which is
why the table flattens rather than falling to nothing. Past 100 ms there is
little left to win; what is left is making a frame cheaper, and the bar is
still 2.9 ms of it.

What this was *not*, each ruled out by measuring rather than by reading --
recorded because every one of them looked plausible:

| candidate | what the measurement said |
|---|---|
| its terminals' output volume | the children wrote 0.003-0.05 MB/s against the 2.4 MB/s it read |
| `hold` writing to its stdin | the hold server wrote 0.005 MB/s |
| the 2.28 MB wallpaper decoded per frame | `img draw` warm is 0 kB and 1.2 ms, on 0.99.78 as well |
| the Hijri calendar, asked per frame | `hcal info` plus `hcal format` is 47 B and 0.1 reads a call |
| `/proc` polling | the desktop has no per-frame `/proc` read at all |
| the control socket | listening cost 9.0% against 9.5% with none |
| an error loop under `keepgoing` | `desktop.log` was 2 kB and not growing |

## 0.99.79

**The wallpaper is a picture** (Gitea #104). It never had been, on any
terminal: a region that owns its cells is dropped the moment they change,
and the wallpaper is placed first and then drawn over by the bar, every
window and the icons -- so it was dropped before the first flush ever sent
it, and every desktop fell back to half blocks however much the terminal
could do. `img draw -u` is the path 0.99.78 prepared: the picture owns no
cells, goes out before the text of each frame, and is kept for as long as
the desktop keeps placing it.

`dt_wall` also blanks the cells it is about to cover, with no colours of
their own. Both halves of that matter: a bitmap writes no cells, so last
frame's text would stay on top of it, and a cell with a background colour
paints over a picture the terminal is compositing below the glyphs -- the
wallpaper-glyph fill that used to stand in for a picture would have hidden
the picture itself.

What it costs is the protocol's rather than a choice, measured on a 70x24
screen with 8x16 cells while dragging a window across the wallpaper:

| | sent | on a drag |
|---|---|---|
| kitty | one placement, 860 kB | nothing |
| sixel | one bitmap, 24 kB | 24 kB again |

kitty composites the picture below the text, so it is transmitted once and
nothing above it disturbs it. A sixel is paint: a cell written over it has
destroyed that much, so the bitmap goes again whenever anything above it
moves. Both work; only one is free.

Checked at three levels: `tests/kitgfx.py` and `tests/sixel.py` for what the
flag does to a region (sent once against painted again, and the frame that
stops placing it taking it away), and `tests/desktop.py` for the thing
itself -- one picture at the top left of a real desktop, the bar and a
window still text over it, and nothing sent as a bitmap when the setting
asks for blocks.

**`read` and `mapfile` read a block at a time** (Gitea #105). A shell `read`
must not consume a byte the next command wants, so it read **one byte a
syscall**: `while read -r l; do :; done` over a 950 kB file made 948,892
read calls and spent 0.30 s of its 0.50 s in the kernel. A descriptor that
can be seeked can be put back, which is how `src/main.c` has always read a
script -- a block, the newline, `lseek` back -- and `bi_rddelim` is now that,
shared by both builtins:

| | before | after |
|---|---|---|
| `while read` over 950 kB | 948,892 reads, 0.502 s | 20,002 reads, 0.066 s |
| `mapfile -t` over the same | 948,892 reads | 234 reads, 0.021 s |
| an idle desktop | 515 reads a second | 22 |

A pipe, a socket and a terminal are still read a byte at a time, because
nothing can be put back on them, and `read -n` still stops exactly where it
should on either. The desktop's own mount scan for disk icons read
`/proc/mounts` that way on every scan, which on a machine with a long mount
table was thousands of syscalls each time.

**`img size` reads the header instead of decoding the picture** (Gitea
#105). It called the full decoder to answer with two integers: 206 ms for
the 3840x2160 photograph on this machine's own desktop, which `dt_wallfit`
asks for on every change of screen size. It is 0.01 ms now -- a PNG says
its size in the IHDR after the signature, a JPEG in whichever SOF marker
opens its frame -- and a JPEG's own EXIF orientation is applied to the
answer, so `img size` and `img draw` agree about a photograph from a phone
rather than disagreeing by a quarter turn. A header this does not
understand still decodes.

And the resampled grids are evicted least recently **used** rather than
round-robin: four other pictures drawn in a frame could between them throw
out a wallpaper redrawn on every one of them, which then paid the whole
decode again. A use stamp costs an integer a slot.

**A frame costs a third less, and the clock is not recomputed on every one
of them.** Measured at 220x62 with three terminal windows, a warm frame was
8.76 ms and is 6.21 ms. Nearly all of the difference is `dt_clocktext`:
the bar asks it for the time on every frame, and a format with the Hijri
codes in it (ADR 0034) answered through three module calls -- `need hcal`,
`hcal info`, `hcal format` -- for 2.38 ms, to say what changes once a day.
It keeps its last answer and what that answer depended on (the second, the
format, the calendar, the adjustment, the language, the digits), so the
most it can be worked out is once a second: 0.02 ms, and `dt_bar` went
from 5.30 ms to 2.79.

What is left of a frame, for whoever looks next: `dt_bar` 2.9 ms, the
wallpaper 1.4, every window 0.7, the flush 0.2. The bar is one row and it
is the most expensive thing in the frame because it is composed and drawn
again every time; the Control Strip, which draws into a pane of its own and
is only rebuilt when it changes, costs 0.01 ms. A bar in a pane is the next
real saving.

**A frame nothing a person did asked for is held to `DT_TERMMS`** (33 ms,
thirty a second; 0 turns it off, Control Panel > Desktop > Frames For
Output). A frame redraws every window, the bar and the wallpaper, and a
program writing in small bursts -- a spinner in a title, a session
streaming its output -- asks for one per burst: a real desktop with three
such programs was drawing tens of frames a second and spending half a core
on it. A key or a mouse report is never held back, so nothing a person does
feels slower, and a held frame still *drains* every window's own program
(`dt_drain`), because a watched descriptor nobody reads stays ready and
skipping the read would turn a held frame into a spin. Honestly: the gain
is bounded by construction and the desktop here could not be made to draw
faster than 12 frames a second to show it off, so this is a limit rather
than a measured saving.

**hold sends a client only the pictures that changed.** It re-placed every
picture it was holding whenever any one of them changed, so a viewer's own
picture moving re-sent an 860 kB wallpaper with it -- which is what made
pixels unusable over a slow link, reported from a remote kitty. Each client
now carries the id and a hash of the bytes last sent for each picture: one
whose bytes have not changed is not sent again, and an id the session no
longer has gets a delete of its own rather than everything being deleted
and placed again. Measured on a wallpaper plus a small picture changed
twice: 3 placements where it used to be 6, and no delete at all, since a
picture replaced in the same rectangle keeps its id and a transmission with
that id replaces it.

**The harness leaked the desktop it was run from, and shared its scratch
files** (Gitea #106). `tests/screen.py` strips `HIBR_HOLD` so a suite run
inside a held desktop does not attach to it, and it strips `DT_SUPERVISED`,
`DT_RESTORE` and `DT_T0` now for the same reason: a suite run from a
terminal window *inside* a running desktop started every test desktop with
`DT_SUPERVISED` already set, so each believed it was already supervised,
none started a supervisor, and the checks that kill a desktop and wait for
it to come back failed -- on the released tag too, hours after that tag's
own gate had passed them, which is what an environment leak looks like:
same code, same box, different shell. Four checks across `desktop`, `vault`
and `calapp` were that one variable.

And every scratch file a suite writes is named after its own process, by
`screen.scratch`. all.py and asan.py run side by side in the gate, so two
copies of a suite are live at once, and `tests/desktop.py` still had
thirteen fixed `/tmp` names and a marker file: this release's own gate
caught one, where a run unlinked `/tmp/hibr-desktop-mshadow.hibr` as the
other was about to start a desktop on it, and the suite died at a check
about a menu's shadow with the other copy's clean-up in the traceback.
`run()` in the same file had used the pid since the beginning; the helpers
written beside it had not.

## 0.99.78

**A picture that goes is taken off the screen, in a held desktop too.**
Reported live on kitty against 0.99.77: moving a window left its picture
behind, so a desktop ended up with pictures of windows that were no longer
there, stacked where each had been. A kitty picture is an object the
terminal keeps until it is deleted, and the console does delete it -- but
hold's clients never saw that escape, because hold draws them cells from
its own emulator and the emulator consumed it. hold now deletes what it is
holding and places what remains whenever the set of pictures changes: one
escape, no per-id bookkeeping, and a transmission with an id already taken
would have replaced it anyway. A sixel needs none of this -- the text drawn
over it is what removes it.

**A picture drawn under text is kept while its caller keeps placing it.**
`DP_IMG_UNDER` used to mean "forgotten once sent", which made a wallpaper
cost its whole bitmap on every frame. Such a region now lives as long as
the caller places it again each frame and goes, with a delete, on the first
frame that does not -- and what it costs between those depends on the
protocol, not on a choice: a kitty picture sits below the text (`z=-1`) and
the terminal composites it, so nothing is re-sent, while a sixel is paint
and goes again whenever a cell above it has been written. `DP_API_VER` is 6
for the changed contract. Nothing passes the flag yet; the wallpaper
(Gitea #104) is what will.

`tests/holdpix.py` has a check for the reported bug -- a picture that moves
leaves nothing where it was -- and its "text through a picture" check now
asserts the invariant that holds whichever way hold's own timing falls:
the text is there and no picture is left behind. Which of the two a client
sees is not ours to decide, since hold renders snapshots of its emulator
rather than the program's byte stream, and under the sanitizers two flushes
a fraction of a second apart arrive in one read.

## 0.99.77

**Pictures reach a held desktop, which is every desktop** (Gitea #103). The
kitty protocol shipped in 0.99.76 and still nothing in the desktop drew a
pixel, because `dt_autohold` holds every desktop and nothing about hold
carried a picture. Measured by driving a held program through a pty, not
reasoned about, and both halves were hold's:

- `hold` starts the program on a pty of its own and nobody had told that
  pty what a cell measures, so `console gfx` answered `none 0 0` inside
  every held program and every picture fell back to half blocks. A client
  now reports its own `ws_xpixel`/`ws_ypixel` alongside its size, hold
  takes the primary client's (a bitmap is 1:1; there is no second size to
  draw it at) and sets it on the program's pty -- `pty resize id rows cols
  xpixel ypixel`, `PY_API_VER` 3. The message goes beside the old two-int
  one rather than replacing it, so a client from this version can still
  resize a session still running the last one.
- hold draws each client cells from its own emulator, and the emulator
  consumed every sixel and every kitty escape and drew nothing for either:
  the OSC 52 trap, a third time. `mods/term/img.c` keeps them as regions --
  a sixel's own raster size turned into cells, a kitty transmission with
  its id, chunk by chunk -- and `"terminal"` version 4 hands them over for
  hold to re-emit at their own corners after each frame's cells. Once per
  change, so a still picture costs a settled frame nothing; skipped rather
  than cut in half for a client whose own rectangle does not hold the whole
  picture, because a bitmap cannot be clipped; and dropped when something
  writes a cell inside it, which is how a window that closed takes its
  picture with it. A terminal *window* still shows a space where a program
  drew a picture -- a window is cells inside somebody else's grid -- and now
  loses nothing on the way through hold.

**A redirection on a `console` command no longer reaches the display.** The
console drew on, and waited on, whatever descriptor number stdout happened
to have, so `console flush > /dev/null` sent a whole frame to the void and
`console key 1000 > /dev/null` returned at once -- a regular file is always
ready to read. Both looked like the console being broken and were a shell
redirection doing exactly what it says; one film measurement in this tree
read 0.05 ms a frame for that reason. The console now dups the terminal to
a descriptor of its own at `console open` (`CN_FDBASE`, 120, close-on-exec,
clear of 0-9, of a `{var}` redirection's 10 upward and of a process
substitution's 60) and closes it at `console close`.

**The YouTube player follows Control Panel > Pictures.** It kept a picture
setting of its own, so the one setting meant to reach every picture the
desktop draws reached the Image Viewer, the wallpaper, Mail and the browser
and not the player. `YT_MODE=follow` is the default now and what its Picture
menu and its pane offer first, with `half`, `ascii`, `mono` and `pixels`
still there for a window somebody wants different; settings version 7 moves
a file that still holds the old default and leaves a choice alone. Its
cycling key walks the list the menu offers rather than a second one written
out beside it, which is how `pixels` was missing from that key for a
release.

`tests/holdpix.py` is eleven checks of the whole path -- the cell size
reaching a held program, both protocols arriving at the client, a display
that joins afterwards getting what it never saw, text through a picture
taking it away, and a client that says nothing about pixels getting blocks
and no bitmap at all. The unheld sixel and kitty suites are its control.

What works and what does not, measured in a real desktop rather than
assumed: a picture in a **window** is pixels -- the Image Viewer places
270 kB of kitty picture at its own corner, and Mail, the browser and the
film player draw the same way -- and the **wallpaper** is not, on any
terminal and never has been (Gitea #104). A picture with text drawn over it
in the same frame is dropped before the first flush ever sends it, which is
exactly what the wallpaper is: placed, then the bar, the windows and the
icons go over it. `DP_IMG_UNDER` is the flag for that case and nothing
passes it yet; doing it needs an invalidation rule of its own, `z=-1` so
the picture is placed once rather than re-sent every frame, and cells over
it left at the default background.

## 0.99.76

**The kitty graphics protocol, because kitty draws no sixel** (Gitea #94).
It never has, and says so in its own documentation -- its own protocol is
the only way to put pixels in it. From 0.99.72 to 0.99.75 the console
nonetheless sent it a sixel on the strength of its name: the region claimed
its cells, the diff refused to paint them, and the terminal dropped the
bytes, so every picture on the terminal most likely to be in front of a
person was a blank rectangle. `mods/console/kitty.c` sends the real thing,
and the list of which terminal speaks which protocol is corrected: kitty,
ghostty, WezTerm and konsole take the kitty protocol, and foot, mlterm,
contour, yaft, iTerm2 and mintty sixel. `HIBR_GFX=kitty|sixel|off` says
outright, and `console gfx` names the one in force.

A kitty picture is an object with an id, not paint: it stays above the text
until deleted, so every region carries one, every path that drops or forgets
a region owes the terminal a delete -- sent before the next diff, so the
text underneath is painted in the same frame -- a replacement in the same
rectangle keeps the id, and closing the display deletes everything it was
holding. Nothing is asked to reply (`q=2`), since the answer would arrive
in the stream the key decoder owns.

Measured per frame of a 640x360 film, two runs agreeing: the kitty protocol
encodes three times faster than sixel (2.7 ms against 9.1 at 60x20) and
sends an order of magnitude more, because the payload is base64 of the raw
pixels (128 kB against 21 kB). So a picture that is not a still -- a film,
which asks for no palette of its own -- is sent at half the pixels in each
direction and the terminal scales it back up: four times fewer bytes, and
at full resolution a 100x34 frame was 1.4 MB and the pty could not keep up.
ADR 0037 has the whole table.

`img -m pixels` is the mode's name now, `sixel` still accepted, since which
protocol carries them is the display's business; the Control Panel's
Pictures setting says Pixels and keeps `pixels`, reading an older file's
`sixel` as the same thing. The YouTube player's Picture menu gained Pixels,
which it never had. The desktop asks `dt_imgpix` whether there are pixels to
be had rather than matching a protocol's name.

Honestly, and it is the reason this is not the end of the story: **a held
desktop still has no pixels.** `console gfx` answers `none 0 0` inside a
program `hold` started, because hold's own pty reports no cell size, and
`dt_autohold` holds every desktop -- so all of this reaches `img draw` in a
terminal and not the desktop the owner runs. That is Gitea #103, and it is
hold's work on both sides: carry the attaching client's cell size to the
program's pty, and give hold's emulator the picture regions so a bitmap
reaches the clients at all.

`tests/kitgfx.py` is 23 checks of its own -- the placement, the id, the
chunking, the deletes, the replacement, a pane's corner -- and
`tests/screen.py` now keeps APC payloads out of its screen model the way it
already did DCS, recording every control string in `Screen.apc`.

## 0.99.75

**A tarball read as a folder** (Gitea #102). A new module, `archive`, opens a
`.tar` or `.tar.gz`, takes its index once, and makes every member a filename:
`/dev/archive/NAME/path/inside` works anywhere a filename goes -- `read`, a
redirection, any builtin that takes a path -- through the scheme mechanism
`http` and `dav` already use, with no kernel mount and no FUSE. Beside that,
`archive ls|stat|cat`, shaped the way `dav ls` is, and `archive list` for
what is open.

Folders the archive never declared are implied from the member names, which
is what most archives need: anything Python's `tarfile` writes, and plenty of
real ones, hold only the files. ustar and GNU headers, the prefix field,
GNU's long name and long link blocks, pax headers read for their `path=`, and
octal or GNU base-256 numbers. An archive of 1024 zero bytes is an empty
archive and opens; a file that is neither a tar nor that is refused rather
than opening with no members, which is what the first version did with a
40-byte text file.

It is called `archive` and not `tar` on purpose: a module's builtins become
commands, and a builtin called `tar` would make `/usr/bin/tar` unreachable
the moment the module loaded. The cat module shadows `cat` because it is
byte-identical in a pipe and nothing can tell; this is no replacement for
tar, so it does not take the name.

A gzip archive is expanded once, at open, into a file nothing else can see,
so every later read is a seek; `ARCHIVE_MAX` bounds how far one may expand
(512 MB by default). The inflate is the prompt module's own, which read git's
objects and packs: it moved to `mods/inflate.c` and is shared rather than
written twice, with a gzip wrapper added beside the zlib one, and still no
zlib linked. Reading only -- a tar is append-only in practice, and rewriting
one to change a member is what archivemount does badly.

`tests/archive.py` builds its own fixtures with Python's tarfile -- plain,
gzip, pax, GNU long names, no folder entries, five hundred members -- and
checks all of it, including that a closed archive's paths stop being files.

## 0.99.74

**Maps are indexed** (Gitea #73). A map is a list, so a lookup walked it and
an append walked it twice: 20,000 appends took 2.7 seconds and building
20,000 rows of three fields took 12.6. Past sixteen entries a chain now
keeps an open-addressed index and its own tail, both in its first entry, so
nothing above `mp_find` and `mp_add` had to change and the order entries come
back in is still the order they went in.

| | before | after |
|---|---|---|
| 20,000 appends | 2746 ms | 74 ms |
| 20,000 rows of three fields | 12,616 ms | 136 ms |
| one field from each of 20,000 | 4016 ms | 109 ms |
| 5000 appends | 157 ms | 18 ms |

A 50,000-row `csv read` is 97 ms, and 2000 lookups into that map cost 9 ms
where each used to walk 25,000 entries -- the csv module asks for an index
once it has linked a large result, since a chain built by hand has none.
Resident memory for a 20,000-entry map goes from 2500 kB to 3092 kB: 16
bytes an entry, and the index at three quarters load rather than half, which
costs 12 ms on those 20,000 appends and saves 272 kB. The ABI is 16, since
`struct ent` grew.

**The sixel encoder is 2.5 times faster**, which is what measuring it was
for. A film frame at 100 by 34 cells (800 by 544 pixels) cost 61 ms to
encode and now costs 23.7 ms; at 60 by 20 it was 21 ms and is now 8.9 ms.
Half blocks cost 0.3 and 0.8 ms, so pixels are still about thirty times the
price, and a film at 100 by 34 is bound to roughly 40 frames a second by the
encoding alone -- measured on a 640 by 360 clip through the player, median of
twenty frames, and written down in ADR 0037 rather than left as a feeling.

The cause was in the first version: it scanned a band once for every palette
colour, 216 times over, where one pass over the band's own pixels can fill
every colour's column pattern at once. The fixed palette's level maths is a
table now as well. What is left is the emission itself, since photographic
content uses most of the palette in every band; a smaller palette for film,
emitting only the bands that changed, and the kitty graphics protocol are
the three ways further, none of them built.

## 0.99.73

**The browser draws the page as a picture** where the terminal can paint
pixels (ADR 0037): the screenshot is taken at the viewport's own size and
drawn as pixels, so a page looks like the page -- its own fonts, its
pictures, its layout -- rather than text over coloured blocks. There is no
text layer in that mode, on purpose: a text cell paints its own background,
so anything drawn over the bitmap boxes itself out of it. Clicks and typing
still reach the page, which go by coordinate; what is lost is reading the
page as cells, so no copying text out of it and no link cells. The window's
own chrome -- tabs, address, the status line -- stays text either way, and
**Control Panel > Pictures** chooses, as it does for every other picture.
`web mode T cells|pixels` is the same thing for a script.

## 0.99.72

**Pictures as pixels** (sixel). Where the terminal can paint them -- kitty,
foot, WezTerm, mlterm, iTerm2, mintty, contour -- a picture is drawn as
pixels instead of coloured half blocks: the Image Viewer, the wallpaper,
Mail's pictures and the film player. **Control Panel > Pictures** chooses:
Automatic, Half Blocks, Grey Blocks, Characters or Pixels, and says what
this terminal can do beside it. `img draw -m MODE` and `media mode ID sixel`
are the same thing for a script, and `console gfx` answers "sixel 8 16" or
"none 0 0" so nothing has to guess from `$TERM`.

A bitmap is not cells, so the console owns it (ADR 0037, `dp_api` version
5): a picture is placed as a *region*, encoded once, and the ordinary cell
diff keeps it honest -- the cells it covers are left alone, so a window
clearing its own face each frame does not paint over it, and the region is
dropped the moment those cells stop matching what they were, so a window
that moves or closes paints its text again with nothing having to say so. A
still picture therefore costs nothing on every flush after the first. The
palette is chosen per picture for a still one (median cut, 256 colours) and
fixed 6x6x6 for a film, where the same palette every frame is what lets a
terminal keep its colour registers.

Whether a terminal can paint pixels is decided from two facts rather than a
probe: it must report the pixel size of a cell, since sixel paints 1:1 and a
wrong cell size spills the bitmap into its neighbours, and it must be one
known to understand the format. `HIBR_GFX=sixel` or `off` overrides. A
Primary Device Attributes probe was rejected on purpose: its reply arrives
in the stream the key decoder owns, racing a keystroke.

`tests/sixel.py` drives all of it through a pty -- the picture lands where
it was put, the text around it survives, the same picture again sends
nothing, text drawn through it brings the text back, a pane clips it, and a
terminal that says nothing about cells gets blocks. The harness now keeps
DCS payloads out of its screen model and records where each landed, and a
test pty can say what a cell measures.

The browser is not converted: it draws text over its screenshot, and a text
cell paints its own background, so "page as a picture" and "text over
colours" are two different renderings -- a decision, not a detail.

## 0.99.71

**Who orders right-to-left text is asked of the terminal** (Gitea #68, ADR
0031). 0.99.70's Arabic was reported unreadable within the hour, and the
reason was a setting rather than the catalogue: hibr put the text in display
order, kitty shaped it with HarfBuzz -- which orders an RTL run along with
the shaping -- and the two reversals cancelled, so Arabic read left to
right. **Control Panel > Language > Order Right-to-Left Text** now offers
Automatic, hibr or The Terminal, and Automatic is the default: hibr orders
unless the terminal says it is kitty, VTE 0.60 or later, or iTerm2 3.5 or
later, each of which does its own. Automatic shows what it settled on in
brackets. A settings file that still held the old frozen "on" is moved to
Automatic once (`DT_SETVER` 6); someone who had chosen The Terminal keeps
it.

A prayer-note check in `tests/desktop.py` could only pass between Fajr and
Isha -- it marked "last looked" a second before today's Fajr and expected a
note for each prayer since, which is none at all before dawn. It gives the
ticker six moments just passed instead; what the sun does is
`tests/salat_adhan.py`'s, all year, to the minute.

The pty harness stops passing the terminal's own identity through to a test
session, so a suite run from kitty draws Arabic the same way as one run
anywhere else, and `tests/desktop.py` checks both ways round: shaped forms
in the cells by default, the letters as written under kitty, and hibr
ordering again when it is asked for outright.

## 0.99.70

**Arabic** (Gitea #68, ADR 0032). `examples/desktop/lang/ar.json` is the
first real translation: all 806 strings the desktop draws, right to left,
with Arabic's own six plural categories where English has two -- so the
Trash's own message counts correctly at none, one, two, a few and many.
Choose it in **Control Panel > Language** and the desktop both translates
and turns round, since the catalogue says it is right to left; English
stays the default and costs nothing. Eight strings stay in Latin on purpose
-- product and protocol names, an example address, the project's own
tagline -- and `tests/993-lang-ar.t` names them, so any *other* string added
without a translation fails there.

**Write's toolbar is mirrored** (Gitea #68, ADR 0033), the last part of the
desktop that was not: its buttons run from the right edge in a
right-to-left desktop, and each is registered where it is drawn, so a click
lands on the button a person sees rather than on its unmirrored twin.
"Plain text" moves to the right for a file that is not markdown.

## 0.99.69

**The vault on the desktop** (Gitea #71). A **Vault** desk accessory reads
the Bitwarden or Vaultwarden vault 0.99.68 brought to the shell: type to
find an item, then Password, Username or Code puts it on the clipboard.
A copied password is never written to the clipboard history, and the
clipboard clears itself afterwards -- 30 seconds by default, a setting.
**Control Panel > Passwords** holds the rest: the server and account, Log
In, Lock, how long it stays unlocked, the PIN, how often it syncs while it
is, and that clipboard timeout.

The desktop is unlocked once for all of it, by master password or PIN, and
exports the session key, so a Terminal window opened afterwards can use
`vw` too; Lock, or the timeout, ends it everywhere. Nothing decrypted is
kept in the desktop: every read runs the `vw` command as a child and the
keys stay in its own module. Syncs run as jobs, so the desktop never waits
on the network, and the vault is kept here encrypted, so everything but a
sync works with none.

`tests/vault.py` drives all of it through a pty against the stand-in
server, including that no password reaches the clipboard history or any
file. A desk accessory missing from the desktop README's own table since
0.99.67 (Prayer Times) is listed now, next to the new one.

## 0.99.68

**Bitwarden and Vaultwarden from the shell** (Gitea #71). A new command,
`vw`, logs in to a Bitwarden-compatible server -- Vaultwarden,
self-hosted, first -- syncs the vault and reads it: `vw list`, `vw get
password|username|totp|notes|uri|item NAME`. The vault is kept here
encrypted, exactly as the server sends it, so `vw unlock` and everything
after it work offline; `vw sync` fetches it again with the stored login,
no password. Unlocking prints `export VW_SESSION=...`, as Bitwarden's own
CLI does with BW_SESSION, and the vault locks after `vw timeout` minutes
unused (15 unless set). A **PIN** can unlock it too (`vw pin set`), until
the next lock, or for good with `--keep`.

The keys never leave a new module, `vw`, whose builtin `vwk` does the
crypto -- PBKDF2 or Argon2id, Bitwarden's EncStrings (AES-256-CBC with an
HMAC checked first), the session and PIN wrapping, TOTP -- with libcrypto
loaded at run time (ADR 0036). Nothing decrypted is ever written to disk,
and the suite checks that it is not. The crypto is tested against an
account built independently with Python's cryptography package, and the
command against a stand-in server. It reads; changing items,
organisations, two-step login and a desktop app are still to come.

`dav request` is a plain HTTP request to any address, for an API that is
not WebDAV -- the vw command's requests go through it.

**tests/apps.py runs as four parts** (Gitea #101): `apps_panel`,
`apps_reach`, `apps_core` and `apps_more`, side by side, each cut from
apps.py at run time by `tests/appslice.py`, so apps.py is still the one
place a check is written and still runs whole. The same 548 checks;
the suite's twelve minutes become the slowest part's eight and a half.
`tests/affected.py` now also picks the dav, web, media, YouTube and
markdown suites when their own module changes -- it never did.

## 0.99.67

**Prayer times** (Gitea #70). A new module, `salat`, works out the day's
prayer times for a place: the sun's position computed as Meeus gives it,
the method -- angles, Isha by angle or minutes after Maghrib, offsets,
rounding -- read from a JSON file in a folder, like calendars (ADR 0035).
Twelve methods are bundled (Muslim World League, ISNA, Egypt, Umm
al-Qura, Karachi, Dubai, Moonsighting Committee, Kuwait, Qatar,
Singapore, Tehran, Diyanet) and each is checked against adhan-js to the
minute, at sixteen places from the equator to inside the Arctic circle,
through the year, with both Asr schools. A method of one's own is a file.

On the desktop, each a switch in **Control Panel > Prayer Times**: a
**Prayer Times** desk accessory (today's six times, the next lit with how
long until it); the next prayer beside the bar's clock; a note at each
prayer, and minutes before if asked; a Control Strip module (shown when switched on); and the
**athan**, a sound file you choose (another for Fajr if you like), played
through the media module -- hibr ships no recording. The place comes
from the time zone, as Date & Time finds it, or from a latitude and
longitude set by hand; the Asr school, the high-latitude rule and a
minute's adjustment to each prayer are settings too.

`hcal list` and `salat list` are sorted by title, and both now refuse a
date with a month or day out of range, or anything after it.

**The Hijri date is part of the date format** (Gitea #69). Rather than a
switch that put a fixed Hijri date beside the clock, a date format now
has six Hijri codes beside strftime's own -- `%id %ie %im %iY %iB %ib` --
so the menu bar's clock, a custom format and every date the desktop
draws can carry the Hijri date wherever the format puts it:
`%a %ie %ib %H:%M` reads "Mon 24 Rab II 14:40". `hcal format` does it for
scripts too. **Date & Time** now holds the date settings: **Hijri** (None,
Umm al-Qura, or a tabular calendar -- None leaves the codes empty and
Calendar's days plain), **Adjust** and **Week**; Hijri bar formats are
offered when a calendar is chosen. **Digits** stays in Language. Month and
weekday names in any format follow the language. Settings saved with
0.99.66's Show Hijri Dates on keep a Hijri date in the bar.

## 0.99.66

**Hijri dates, and dates as a region writes them** (Gitea #69, #68).
A new module, `hcal`, gives the Hijri date of any day and the day of any
Hijri date in whichever reckoning a person follows. Calendar systems are
data -- JSON files in a folder, like themes (ADR 0034) -- so a region's
own can be added with no code: hibr ships Umm al-Qura (from ICU's table,
1300-1600 AH) and the civil and astronomical tabular calendars, and each
is checked against ICU on every day from 1870 to 2200. A whole-day
adjustment covers local sighting of the moon.

In Control Panel > Language, under Dates: **Show Hijri Dates** puts the
Hijri date beside the bar's clock and the Gregorian date in Date & Time,
and each of Calendar's days carries its Hijri day, its title naming the
Hijri months the month runs across; **Hijri Calendar** and **Hijri
Adjustment** choose the reckoning; **Digits** writes numbers in
Arabic-Indic digits; and **First Day of Week** starts Calendar's week on
Saturday, Sunday or Monday, or, on Auto, where the locale's region does
(CLDR's table: Saturday in Egypt, Sunday in Saudi Arabia and the United
States, Monday elsewhere). Month and weekday names now go through the
catalogue too. Gregorian dates are always shown; Hijri ones only when
asked for.

## 0.99.65

**The rest of the lists turn round** (Gitea #68, phase 4c). Mirrored,
the Clipboard reads each entry from the right -- its time, its mark,
then the text -- with its scrollbar at the left; Task Manager's columns
run the other way, Mem first and PID last, the names right-aligned and
each heading still sorting where it is drawn; YouTube's results put the
title at the right and the channel and length at the left; and
Calendar's agenda is right-aligned. dBASE keeps dBASE III Plus's own
command line, as a terminal keeps its program's screen, and games, the
calculator's keypad and the clock keep their shapes.

## 0.99.64

**Calendar and Contacts turn round** (Gitea #68, phase 4c). Mirrored,
Calendar's month runs its week from the right, each day's number and
events right-aligned, the month's name at the right of the bar between
a previous arrow pointing right and a next one pointing left, and the
left arrow goes to the next day, as it looks; the chosen day's events
are right-aligned below. Contacts puts its list at the right and the
person chosen at the left, each field's label at the right of its
value. The agenda view is not mirrored yet.

## 0.99.63

**Sheet and the file dialog turn round** (Gitea #68, phase 4c). Mirrored,
Sheet puts column A at the right with the row numbers outside it and the
columns running leftward, the cell's name at the right of the formula
bar, its scrollbar at the left; a column is widened from its left edge,
and the left arrow goes to the next column, as it looks. The file dialog
used for Open, Save As and Export everywhere puts its list and the name
at the right, the preview and the type at the left, and its divider
drags the other way.

## 0.99.62

**Mail turns round** (Gitea #68, phase 4c). Mirrored, the sidebar is at
the right with each view's count at its left, the search field at the
right and the account at the left, and a conversation's row reads date,
subject and snippet, senders, star from left to right. An open
conversation puts its subject, sender, notes and attachments at the right
and the date at the left, Back at the right. A message's own lines keep
the layout the HTML module gives them -- the console orders right-to-left
text within each, but a right-to-left message is not yet right-aligned.

## 0.99.61

**Restart Desktop works on a Mac.** `json parse NAME < file` read its
input through the C library's stream, and on macOS a stream that has once
reached the end of a file stays there: the second `json parse < file` in
a process read nothing. The desktop reads its clipboard history that way
just before putting a restart's windows back, so on a Mac every restart
found its saved state "malformed", set it aside as `state.json.bad`, and
came back with nothing -- a terminal's shell included -- and Control
Panel > Appearance listed no themes and no colour schemes, every file
after the first read as empty and refused. Each read now
starts and ends with the stream's end-of-file cleared; `uni levels`
likewise.

**Task Manager logs nothing for a process with no memory figure.** A
zombie or a kernel thread has no `VmRSS`, and the Linux scan stored that
empty, so every redraw logged `tasks_human: kb expects int, got ''` for
each one; it counts as 0 now, as the macOS scan already did.

**The Terminal's menus begin with File and Edit**, then Shell, as every
other app's do: File has New Window and Close Window.

**The log says how long the desktop took to start**: one line in
`desktop.log` after the first frame -- `started in 94 ms: window manager
21, apps 37, console 0, open 2, first frame 32` -- timed from the first
process, across the hold re-exec and the supervisor, and again after a
restart. A slow start says where it went.

## 0.99.60

**Control Panel's panes and Files turn round** (Gitea #68, phase 4c).
Mirrored, every row of a Control Panel pane puts its value, checkbox,
slider or dropdown at the left and its label at the right, a pane's
scrollbar on its left edge. Files puts its path at the right and the
search box at the left, its names at the right of the list, the details
columns in the other order with the name last, and its icon grid from
the right; a click on a tile or a column heading reaches what is drawn
there. Mail, Sheet and the rest of the apps follow.

## 0.99.59

**A script reaps its background jobs.** A non-interactive hibr collected
a finished `( ... ) &` only at `wait` or `jobs`, never on its own, so a
long-running script that started jobs and read their results from files
-- the desktop's remote transfers and syncs -- kept every one as a zombie
for as long as it ran: a day-old desktop had a hundred. Each finished job
is now reaped before the next one starts, so at most the newest lingers;
a script keeps the status of the last 256 finished jobs for `wait`, and
`wait PID` on one already reaped returns its status rather than 0.

## 0.99.58

**The rest of the desktop's own layout turns round** (Gitea #68, phase
4b). Mirrored, the desktop icons start from the left edge, the Control
Strip docks at the right, notifications stack from the top-left corner, a
tiled workspace keeps its main window on the right and the stack on the
left, and Control Panel puts its list of panes on the right of the
divider, which drags the other way. Your own strip side and notification
corner are still yours: they are read the other way round, never rewritten
(ADR 0033). The inside of a Control Panel pane, scrollbars and each app's
own layout come next.

## 0.99.57

**The desktop turns round for a right-to-left language** (Gitea #68,
phase 4a). With Arabic or Hebrew -- or Control Panel > Language > Mirror
Layout set to on -- the menu bar runs from the right edge, the
application menu, the clock, notifications and the workspaces from the
left; menus open under their title's right edge, submenus to the left,
each row's shortcut on the left and its label right-aligned; a window's
buttons sit on its left and its title on its right; a row of dialog
buttons runs the other way. It is a layer over the settings, never a
change to them, so your own button side and title alignment still mean
what they did (ADR 0033). A right-to-left pseudo-language, `xy`, drives
the suites through it. Control Panel, the desktop icons, the Control
Strip and tiling follow next, then each app's own layout.

## 0.99.56

**The rest of the desktop's text is translatable** (Gitea #68). 677
distinct strings now go through the catalogue, from 448: notes built from
values are templates (`dt_note "Saved %s" "$f"`, 104 of them), confirms
and menu items with a value are filled through `dt_tr` first, counts that
need a plural use `dt_trn`, text an app draws itself goes through
`dt_tput` (93 places), and a window title translates its name and keeps
what is in brackets ("Files [~/notes]"). What is left in English is data
-- file names, a message's subject -- and a few messages that splice in a
word from elsewhere ("Moved", "to the trash"); `xx` shows them.
`printf -v NAME -- FORMAT` takes `--` as the end of its options, as bash
does; hibr used to print `--`.

## 0.99.55

**The menu bar follows the terminal's size.** On a resize -- and a
reattach is one, the new terminal's size arriving as a resize -- the
screen was drawn again at the new size but the menu bar was not laid out
again, so its right end (the workspaces, the clock, the app's menu) stayed
where the old width had put it: past the edge of a narrower terminal, or
short of a wider one. The bar's layout is kept between frames and rebuilt
only on input or a change of focus; a resize now asks for it too. And a
menu item in a right-to-left language is drawn whole, its shortcut beside
it, where the underline split it into three pieces, each ordered on its
own. A note's text may be a template now, `dt_note "Saved %s" "$f"`,
translated and filled in one call.

## 0.99.54

**The desktop can speak another language** (Gitea #68, phase 3). A
language is a JSON catalogue, English as the key, in `lang/` or the
person's own `~/.config/hibr/lang`; Control Panel > Language chooses it,
beside the switch for terminals that reorder right-to-left text
themselves. The `lang` module holds the catalogue in a hash, with CLDR
plural rules (Arabic's six forms among them) and arguments a translation
can reorder (`%2$s`). The desktop translates at its widgets -- menus,
items, notes, confirms, buttons, checkboxes, icons, window titles and
Control Panel -- so the 450 distinct strings that reach one need no edit
where they are written; templates and text an app draws itself follow
app by app. `tools/strings.py` finds every string drawn, keeps
`lang/strings.txt`, and writes a pseudo-language, `xx`, that marks each
one, so a desktop running in it shows what is not yet translated; a test
fails when the list and the code differ. English pays one variable read
a widget, about 1.3% of building a menu and nothing on an idle frame
(ADR 0032). The Arabic catalogue ships once reviewed.

## 0.99.53

**hvi edits Arabic and Hebrew** (Gitea #68, phase 2 complete). A line
holding right-to-left text is drawn in display order, Arabic joined, each
character keeping its own colour, selection or search highlight; motions,
deletions and the cursor stay logical, the cursor standing on the column
its position landed in. `uni`'s interface is version 3 for it (`line`, a
line with each drawn character's source and column). With this, every
place hibr draws text a person reads or edits -- the desktop, the prompt,
Write, Stickies and hvi -- puts right-to-left text in display order.

## 0.99.52

**Write edits Arabic and Hebrew** (Gitea #68, phase 2c). Each row of a
line holding right-to-left text is drawn in display order, Arabic joined,
each cell in its own style -- bold, italic, a link -- while the cursor,
typing, clicks and the selection stay logical: typed letters go where
they read, a click lands on the letter under it, and the file saves as
written. Markdown and plain text both, the plain path through the
textarea widget, so Stickies has it too. `widgets/bidi.hibr` is the
shared piece, over a new `uni map` (a row of a line with where each
character went), and `console put -r` draws text already in display
order. `hvi` is next.

## 0.99.51

**The prompt reads Arabic and Hebrew too** (Gitea #68, phase 2b). A line
holding right-to-left text is drawn in display order, Arabic joined, one
screen row at a time, the whole line one paragraph -- levels and shaping
over all of it, each row reordered on its own as UAX #9 says, so a word
cut by the wrap still joins across it. The buffer, the keys and the
cursor stay logical: left and right step through the text as typed, and
the cursor stands on the column its position landed in. `uni`'s interface
is version 2 for it (`vismap`, a line of a paragraph with where each
character went). `HIBR_BIDI=off` turns it off for a terminal that reorders
itself, and is the console's default too. `^U` was documented as killing
the whole line; it kills back to the start, as in bash, and says so now.

## 0.99.50

**The desktop draws Arabic and Hebrew readably** (Gitea #68, phase 2a).
Text a script draws with `console put` that holds right-to-left characters
now goes through the `uni` module first: display order, Arabic in its
joined forms, marks after their base, brackets mirrored. A pane's text is
clipped first and ordered after. Plain text never reaches `uni`, and a
program's own cells -- a terminal window, `most`, `hvi` -- are drawn as the
program laid them out. `console bidi off` is for a terminal that reorders
right-to-left text itself (ADR 0031). The core's width table, written by
hand and incomplete, is generated from Unicode 15.1.0 now, like `uni`'s,
and is 6% cheaper; the test harness takes widths from Unicode as well.

## 0.99.49

**The `uni` module: bidirectional text and Arabic shaping for a cell grid**
(Gitea #68, phase 1 of 5). A terminal draws a character per cell, left to
right, and shapes nothing, so Arabic needs its joining forms chosen per
letter and any right-to-left script needs the Unicode Bidirectional
Algorithm to be read at all. `uni vis` gives the display order -- UAX #9
in full, every one of the 770,241 cases of Unicode's BidiTest and 91,707
of BidiCharacterTest passing -- with Arabic in its presentation forms,
lam-alef as one cell, marks kept after their base and brackets mirrored,
checked case by case against GNU FriBidi. Every table comes from one
Unicode version, 15.1.0, generated by `tools/unigen.py`, with the
conformance suites from the same download kept in `tests/uni` (ADR 0030).
Nothing draws through it yet: the console and the editors are phase 2.

## 0.99.48

**The `lines` module: show, search and edit files by line, exactly**
(Gitea #67). Work on this tree reached for `sed -n`, `grep -rn` and a
python replace that refuses an ambiguous anchor; a hibr script doing the
same was 60 to 270 times slower, so it is C. `lines show` prints a range
byte for byte; `lines grep` searches a tree with grep's output and
context, as fast as GNU grep on a plain pattern; `lines count` counts a
literal; `lines edit` takes `<<<<` old `====` new `>>>>` blocks on
standard input, applies them in order, and writes nothing unless each
old text occurs exactly once at its turn -- then through a new file
renamed over the old, its mode kept. CLAUDE.md now says to use it.

## 0.99.47

**An About card keeps the end of a long path.** The card cut the app's
file path to its width from the right, so a path longer than the card
lost the file's own name -- which is how the About check failed in every
full sanitizer run, made from a worktree deep under the job folder, and
in none of the ordinary ones. A long path now shows its end, after an
ellipsis. And the app menu's builder reads into a declared local rather
than `_`, which strict vars took for a new global; the change had been
sitting in the working tree, tested by every gate and shipped by none.

## 0.99.46

**`desktop ctl resize` is measured against the window's own display.**
Under Blit the desktop's screen is the primary's surface alone, 128
columns, while a joined display sits beside it at column 128; a resize
went through the same clamp as a drag, against that screen, so a window
moved to the joined display was clamped to no width and every resize
there was refused (Gitea #100). A ctl resize now finds the display the
window is on from the display list and checks the size against that
rectangle, and a size that does not fit is refused whole, the window
left as it was, where the clamp used to shrink it quietly. Tested on a
joined pair, and with given layouts for one display below another and
two panes side by side.

## 0.99.45

**About an app is found in one pass.** 0.99.44's grep-free search for the
file that declares an app read every app file through on a miss, 371 ms
each time and several times that under the sanitizers. The desktop now
reads each app file once, only as far as its first function (where every
`dt_app` line is), and keeps the answer: 36 ms, once a session. The
sanitizer wrapper names `setarch` by its path, so the test that runs the
desktop with only hibr on `PATH` can start under ASan too.

## 0.99.44

**`mv` is a builtin, and the desktop starts with no coreutils at all.**
Under Blit's busybox-only guest the desktop's crash snapshot could not be
written -- it is written beside and renamed into place with `mv` -- and
About Me was read with `head` at every start. `mv` joins `mkdir` and `rm`
(ADR 0029): renames and moves into a directory, `-f`, `-n`, `-v`, `-t`,
`-T`, answering as GNU's does, checked against it by `tests/987-mv.t`. A
move across filesystems is a copy, and goes to the program. About Me is
parsed straight from its file, the About box finds its app without `grep`,
dBASE's `DATE()` and `TIME()` use
`printf '%(...)T'`, and Mail makes its empty POP map with `: >>`. The
Blit-launch test now fails on any command not found. What the desktop
still forks (`sort` with keys, `cp -R`, `touch -r`, `ps`, `readlink`,
`uname`) is a system tool's job; a guest that wants those can link
busybox's applets.

## 0.99.43

**The control socket under Blit, for real this time; `mkdir` and `rm` are
builtins.** 0.99.41 logged why a socket was missing and retried, and it
was still missing in Blit's guest. The reason: that guest has busybox and
no applet links, so there is no `mkdir`, and the socket's private folder
was made with `mkdir -p -m 700`. The log's folder was made the same way,
so the log that would have said so never opened either. Hold makes its
folder in C, which is why its socket was there. `mkdir` and `rm` are
builtins now (ADR 0029), answering as GNU's do and handing any option
they do not implement to the program on `PATH`. The desktop makes its
folders with no fork, and with no coreutils at all. Under Blit, whose
display cannot watch a descriptor, the socket is polled twice a second
instead, or a request would wait for the next key. `tests/desktop.py`
launches the Blit way with `PATH` holding only hibr. The desktop still
forks other programs such a system lacks -- `mv` for its crash snapshot,
`date`, `sort` -- which is Gitea #99.

## 0.99.42

**A sanitizer gate in minutes, and the full run after.** The whole
sanitizer pass -- every suite under ASan and UBSan -- takes over twenty
minutes, which made every release that touched C wait for it.
`tests/asan.py --quick [--since REF]` is the gate now: run.sh, self.hibr
and 300 rounds of the parser fuzzer, the suites of every C module outside
the desktop, and the pty suites a module changed since the last tag
reaches (from `tests/affected.py`). It took 133 seconds here, the
sanitizer build included; the build now runs in parallel. A change to a
module the desktop uses widely -- csv, say -- brings the desktop and apps
suites in, and takes as long as they do. The full `tests/asan.py` runs
after each release, and any report it makes becomes a ticket.

## 0.99.41

**The control socket says why it is missing, and keeps trying.** A desktop
that cannot make its socket -- the folder not its own, the path in use,
a `TMPDIR` that is not there -- used to say nothing, and `desktop ctl`
could only report that nothing was listening. Every outcome now goes to
the desktop's log as one `desktop: control socket: ...` line (listening
and where, off and why, or what failed), a failure is retried every five
seconds, and a socket file removed from under a running desktop is made
again within half a minute. `desktop ctl` against a session that is held
and running but has no socket says so and points at those log lines.
Tested the way Blit launches it: held as `--session blit` with a display
name, the supervisor on, a second display joined, `ctl` through
`session.hibr`.

The pty harness no longer misses a shell error that is the first line of a session's `desktop.log`: it was joined to the terminal's output with no newline between, so the check for errors read past it. The one it had been hiding was the slip test's own, expected but declared too late. What it found next was real: the Wallpaper picker sized its first preview before loading the img module, so that preview filled the box instead of keeping the picture's shape, and the log said `img: command not found`. The theme checks know the bundled Retro Car theme.

## 0.99.40

**Calendar and Contacts point to Control Panel > PIM.** Their notes and
empty-window hints still named the pane by its old name, Calendars &
Contacts; the rename in 0.99.38 had left them out.

## 0.99.39

**The desktop has a control socket.** A program can ask a held desktop
which displays are attached and which windows are open, and move, resize
and focus them -- through the same operations the menus use, never by
typing keys at it. It is what Blit, a terminal server that draws hibr
straight to the screens with no X11 or Wayland, needs to drive a desktop
across several displays. `desktop ctl displays`, `desktop ctl windows`,
`desktop ctl move 3 right` print the desktop's JSON answer; errors are
named (`no-window`, `no-display`, `detached`, `bad-geometry`,
`unauthorized`...). Only the desktop's own user can use it.

**`listen` serves Unix sockets, and `accept` can wait a while or not at
all.** `listen -b /path` binds an owner-only Unix socket, `$REMOTE` is the
connecting process's `uid:N`, and `accept -t secs` gives up after that
long (`-t 0` only looks).

**`exec {fd}<>/dev/unix/...` and `/dev/tcp/...` work.** The socket went to
standard input and `fd` stayed empty.

## 0.99.38

**Appearance, regrouped.** Headings for Look (Theme, Colours, Glyphs),
Wallpaper (Pattern, Image, Mode), Menu Bar, Shadows and Controls. The Theme
row carries its own buttons: **Save** appears once the look no longer
matches the theme, and a theme of your own can be **renamed** and
**deleted** there (or with `s`, `r`, `d` on that row).

**Calendar and Contacts are Desk Accessories**, on the hibr menu with
Calculator and Clock. **Control Panel > Calendars & Contacts is now PIM**,
and it asks each Network Server what it keeps: servers with calendars or
contacts get a switch saying which, a plain file server is shown as files
only, and Add Server… opens Network Servers.

**A Nerd Font glyph set**, Appearance > Glyphs > nerd, for a terminal
whose font is a Nerd Font: folders, files, home, disks, the trash and the
marks drawn as its icons. **Checkboxes can be a tick**, `[✓]`.

## 0.99.37

**A csv module.** CSV as RFC 4180 says -- quoted fields, doubled quotes,
line breaks inside quotes, CRLF or LF, a byte-order mark skipped, any
separator. `csv read FILE` binds every record as `r[i][j]`, or with `-H`
by the header's names; `csv open`/`row`/`close` stream a record at a time;
`csv line` writes one with only the quoting a field needs; `csv split`
takes one record apart. 50,000 rows bind in a quarter of a second. Sheet's
Import and Export CSV go through it, so a quoted cell with a line break in
it now imports whole.

**`printf` and `echo -e` read escapes as bash does.** A format's `\NNN`
and `\xHH`, `%b`'s `\NNN` and `\xHH`, `echo -e`'s `\xHH`, and `\c`
ending `%b` and `echo -e` -- nothing printed after it, not even the newline
-- while a format prints it as it is. hibr had none of these.

## 0.99.36

**YouTube's mini player.** `i`, Play > Mini Player or a double click on
the picture shrinks the player to the picture alone -- no border, title or
bar -- in a small window at the bottom right, on every workspace. Drag it
anywhere; right-click it for pause, seek, next, volume, Full Player and
Close; a double click or escape goes back to the full player where it was.

**Windows with no chrome.** `DT[$id]["chrome"]=none` gives an app every
cell of its window; any press moves it, a double click and a right click
are the app's. **Double clicks reach apps**: `dt_isdbl` says whether a press
is the second of one -- Calendar's double click on a day, which never
worked, now makes an event there.

## 0.99.35

**Windows on every workspace.** Window > On Every Workspace, and the same
on a title bar's right-click menu, makes a window sticky: it stays in view
whichever workspace is current, and floats over a tiled one. Taking it off
leaves it on the workspace you see it on; sending it to one workspace takes
it off too. Restart Desktop keeps it. Control Panel > Stickies > Notes on
Every Workspace does the same for every note, open now or opened later.

## 0.99.34

**The desktop is the Finder.** With no window focused the menu bar reads
File, Edit, View and Special, as System 7's Finder did: File > New Window,
Open (the icons chosen) and Find…; View > Desktop Icons and Refresh;
Special > Clean Up Desktop (moved from the hibr menu) and a new Empty
Trash…, which asks and then deletes for good.

**About follows the front app.** The hibr menu's first item is About
Files…, About Calendar… and so on for whatever is in front -- a helper
window answering for the app it belongs to -- with About hibr Desktop
below it. An app may define `<app>_about`; otherwise the desktop shows a
card of its icon, name, description, file and the shell's version.

## 0.99.33

**Themes are the whole look; colours are their own choice.** What used to
be a theme -- seven colours and a shadow -- is now a colour scheme,
Appearance > Colours. A theme, Appearance > Theme, is the whole look at
once: a colour scheme, the wallpaper (a glyph or a picture), the window
frame, the title bar, the buttons and where they sit, checkboxes, dialog
buttons, shadows and the glyph set. Every setting it carries can still be
changed on its own afterwards, and Save Current Look as Theme… keeps the
look as it stands under a name of your own. Themes and colour schemes are
both JSON files (`examples/desktop/themes/`, `examples/desktop/colours/`,
and your own in `~/.config/hibr/`), each checked whole; a colour file you
kept in `~/.config/hibr/themes/` is still read as colours.

**Two new looks.** Under Construction: black-and-yellow hazard stripes,
yellow windows, double frames and solid black title bars. Meadow: a sky
over a green hill, beige windows with rounded frames and blue title bars,
after the desktops of around 2001. Classic is the desktop as it has
always looked.

**Solid title bars.** Control Panel > Windows > Title Bar: `line`, the
title sitting in the frame's top edge as before, or `solid`, the whole top
row a bar of the frame's colour with the title and buttons on it.

## 0.99.32

**Calendars and contacts sync on their own.** The servers chosen in
Control Panel > Calendars & Contacts are synced every so often whether or
not Contacts or Calendar is open -- before, nothing synced until one of
them was, so a reminder could be read from an old copy and Mail finished
addresses from one. What the three apps share about syncing now lives in
`examples/desktop/lib/pim.hibr` instead of inside Contacts.

**Copy on Select in Terminal.** Control Panel > Terminal > Copy on Select
copies a mouse selection the moment the button comes up, as xterm does. The
text stays lit, and the next key only puts the highlight out -- it is not
sent to the program, so the enter pressed out of habit after a copy does
not run a half-typed line. Off by default.

## 0.99.31

**Calendar.** A new app keeps your calendars on this machine from any
CalDAV server, through the same Calendars & Contacts settings as Contacts:
a month at a time, each day's events in their calendar's colour and the
chosen day's listed with their times, or an agenda of the next sixty days.
Events are made, edited and removed here -- title, date and times or all
day, a repeat, a reminder, the calendar, a place, notes, who is invited --
written in this machine's own zone and sent at the next sync. Reminders go
off as notes whether a Calendar window is open or not.

**Invitations, both ways.** Inviting people to an event mails each of them
an invitation from your Mail account, the event in it as a calendar
request any mail program understands. An invitation that reaches you is a
card in Mail -- what, when, where, from whom -- with Yes, Maybe and No: the
answer goes to the organiser and the event into your calendar. An answer
to your own invitation can be recorded in your copy, and a cancellation
removes the event.

**The pieces underneath.** `email build -C file -M METHOD` adds a calendar
part beside a message's text; `pim ics store` makes the copy a calendar
keeps of an invitation, with an attendee's answer set; `pim when` turns a
local date and time in any zone into seconds and back.

**A sync is no longer held up by a mail being sent.** A change made while
an invitation was going out waited for the next timed sync, because the
sending job was taken for a sync already running.

## 0.99.30

**Contacts.** A new app keeps the people in your address books on this
machine, from any CardDAV server -- Nextcloud, Fastmail, iCloud, Radicale:
a searchable list, each person's emails, phones, organisation and note, and
new, edit and remove, sent to the server at the next sync and only if its
copy has not changed meanwhile. Control Panel > Calendars & Contacts
chooses which Network Servers to use and how often to sync; a background
job finds the address books itself and keeps them up to date with sync
tokens.

**Mail finishes addresses.** Typing in To, Cc or Bcc offers the people who
match, from Contacts and from everyone Mail has had a message from; tab or
enter takes one. Message > Add Sender to Contacts adds whoever wrote the
conversation, and a click on an email in Contacts writes to them.

**Mail's New Message is no longer an app on the hibr menu.** It was meant
to be hidden and was not, from 0.99.24, because of one argument too many.

**Clicking in a new message's fields works.** A click on To, Cc, Subject or
a button in Mail's compose window did nothing: the window's mouse handler,
there for selecting text, took every press and dropped the ones outside the
body. Now it passes them on.

## 0.99.29

**A calendars-and-contacts module.** `pim` reads and writes iCalendar and
vCard: every event and task with its times, people and reminders; every
contact with its addresses, phones and emails, from vCard 2.1 to 4.0. It
expands a recurring event into its occurrences over any window -- every
part of RRULE, with RDATE, EXDATE and moved occurrences -- and is checked
against RFC 5545's own examples, occurrence by occurrence, with
python-dateutil as the reference: all 46 agree. Times in a named zone go
through the system's zoneinfo; a zone only the calendar describes is read
from its own VTIMEZONE. It builds events (with a VTIMEZONE made from
zoneinfo), contacts, and the reply to an invitation.

This, with 0.99.28's additions to `dav`, is the foundation for calendar
and contacts on the desktop, which come next: contacts and Mail's address
completion, then the calendar and invitations.

## 0.99.28

**A mail account with a space in its name no longer ends the desktop.** An
account named "Home Email" was read, in two places, as arithmetic -- an
unquoted subscript -- and the error ended the desktop. Every name a person
chose is now quoted, and the suite's accounts have spaces in their names.

**One app's error no longer ends the desktop.** The shell has a new option,
`set -o keepgoing`: an arithmetic error, or nesting too deep, fails the
command it happened in instead of ending the script, as it already did
inside `try`. The desktop turns it on, so a slip in any app is written to
`desktop.log` and everything else carries on. Without it, a script behaves
as before, and as in bash.

**Also in this release, not yet used by an app:** the `dav` module learns
`propfind`, `report` and `sync` (WebDAV properties, reports such as CalDAV's
and CardDAV's, and RFC 6578 sync tokens), `put -t` for a content type (and
guesses `.ics` and `.vcf`), `rm -m` for If-Match, and lets a server's login
follow it to another host of the same domain over TLS -- iCloud sends its
calendars to a numbered host. These are the first part of calendar and
contacts.

## 0.99.27

**Restart Desktop and the new supervisor get on.** Restarting a desktop
that has terminals open made the restarted process a supervisor, which left
the terminals' programs as its children rather than the new desktop's, and
the desktop that should have come back did not draw. A restart that carries
running programs now keeps them with the desktop and runs without a
supervisor until the next fresh start; `tests/desktop.py` restarts into
that case and checks the program is still the desktop's own child.

## 0.99.26

**alt-left and alt-right go between workspaces.** They were Snap Left and
Snap Right, which move to ctrl-alt-left and ctrl-alt-right -- the keys the
workspaces had -- so the two swap. A window that uses alt-left and alt-right
itself keeps them while it has focus: the Browser's back and forward. A
saved settings file still holding the old defaults is moved over once
(settings version 3); a key someone chose is kept.

**Runaway recursion is an error, not a crash.** A function calling itself
for ever, a file sourcing itself, an `eval` evaluating itself: each is now
stopped before the stack runs out, with a message naming it, and the script
ends as it does for an arithmetic error. bash's `FUNCNEST` is honoured too.
This is what took the desktop down in 0.99.24; it would now have been an
error in the log.

**The desktop starts again by itself when it dies.** A held desktop has a
small supervisor: if the desktop dies on a signal or an error, it is started
again and its windows are put back where they were, from a snapshot kept
while you work, with a note saying so and the reason in `desktop.log`.
Terminals come back with a fresh shell. One that dies within ten seconds, or
three times in five minutes, is left stopped. SIGTERM and SIGHUP -- a
shutdown -- write the windows down and exit cleanly, and the next start
reopens them once.

**`exit` in a trap keeps its status.** `trap 'exit 143' TERM` exited 1, and
a bare `exit` in a trap did too: the earlier status was put back after the
trap. Both now behave as in bash.

## 0.99.25

**Adding a mail account no longer takes the desktop down.** Choosing Add
Account (or an account, or Signature) in Control Panel > Mail ended the
whole desktop at once, with nothing in its log. The dialog's opener was
named `mlad_open`, which is also the name the window manager calls when a
`mlad` window opens -- so it opened a window, which called it again, until
the shell ran out of stack. It is `mlad_show` now, and
`tests/540-examples.t` fails on any window whose `_open` opens another of
its own kind. `tests/mailapp.py` opens the dialog, is refused a server it
cannot work out, and saves a Gmail account.

The dialog is also a row taller, so its Gmail hint and an error from
saving are shown whole rather than cut off at the edge.

## 0.99.24

**Mail.** A mail app for the desktop, in the Internet folder, laid out the
way Gmail is: the views and labels down the left with their unread counts,
conversations on the right with a message and its replies together, and a
conversation opened in place -- HTML laid out with its headings, lists,
quotes, tables and links, attached pictures drawn, attachments saved with a
click. Gmail's keys work (`j` `k` `o` `u` `e` `#` `!` `s` `l` `v` `c` `r`
`a` `f` `/`, and `g` then a view), and the File, Message and Go menus have
them all. It is offline: a sync job keeps each account in a `db` file, what
you do happens at once and is queued for the server, and a dropped
connection loses nothing. New mail is pushed through IMAP IDLE. IMAP with
Gmail's labels and threads, POP3 (downloaded and kept here, flags this
machine's own, Trash deleting from the server) and SMTP. Control Panel >
Mail adds accounts -- for Gmail or Outlook an address and a password are
enough -- and sets how much is kept, how often to check, pictures and a
signature.

What it does not do yet: Google sign-in -- Gmail wants an app password,
made in the Google account's security settings; fetch pictures from the
web, which would tell a sender the message was opened; save a message being
written as a draft (closing one with text in it asks first); or keep more
than about 2000 messages quickly, because the shell's maps are lists. It
keeps 1000 by default -- the main folder that many, every other folder a
fifth -- and with 1000 opening takes 0.2 s and changing view 0.35 s.

**An HTML module.** `html` parses HTML as the WHATWG standard says a browser
must -- our own tokenizer and tree builder, passing all 1792 of html5lib's
tree-construction tests -- and answers CSS selector queries, text,
attributes and the tree, and lays a page out as lines of cells with a style
per character, link columns and boxes for pictures. An 813 KB page parses
in 159 ms. It is what Mail reads messages with, and the start of a browser
of our own; JavaScript, through QuickJS, is for a release of its own.

**An email module.** `email` speaks IMAP (IDLE, special-use folders,
Gmail's labels, threads and ids, MOVE or COPY and expunge), POP3 and SMTP
(STARTTLS or TLS, PLAIN or LOGIN, Bcc taken out), over the shell's TLS
relay, with accounts in a file only you can read that never shows its
passwords, and does the MIME: RFC 2047 and 2231 headers, quoted-printable,
base64, charsets into UTF-8 (iconv found at run time), parts out to files,
and messages built. `tests/mailserve.py` is a stand-in IMAP, POP3 and SMTP
server, and `tests/mail.py` and `tests/mailapp.py` drive the module and the
app against it.

**`:=` binds a map into a map.** `m["rows"] := db query "$h"` kept only the
scalar and dropped the rows; the result's map is now copied under the
target, at any depth, with its JSON types, and binding over a map frees the
old one.

**Smaller things.** `dt_keep` no longer lists a variable twice when two
files keep it.

## 0.99.23

**YouTube no longer freezes a minute in.** On many videos YouTube's own
player stopped about a minute in with "Something went wrong", and ours
played out what it already had and froze on the last picture. The cause
was the browser announcing itself: the web module's Chromium said
"HeadlessChrome" and set the automation flag, and YouTube treats such a
browser as a robot. It is now started without the automation flag and its
tabs give the browser's ordinary Chrome user agent (`HIBR_WEB_UA` to
choose another). Measured on the video that always froze: five runs in
five before, none of five after.

**And it recovers when YouTube's player stops anyway.** Every few seconds
the app checks YouTube's own player; if it has shown its error or fallen
back to unstarted, the video is loaded again and taken up where it was,
with a note saying so. A player that does not start within 25 seconds is
tried again too, where before it gave up at once; after three tries the
app says what went wrong. And it no longer waits for YouTube's page to
finish loading before starting its player -- that could take twenty
seconds, during which the video played under "starting" and then jumped
back to the beginning.

**The list's highlight is readable.** The chosen row in YouTube's list was
drawn in a colour no theme defines -- dark text on black. It is the
theme's selection now, like every other list, and so is the text on the
progress bar; a test fails if any pen names a colour no theme has.

## 0.99.22

**A login screen.** `login/login.hibr` logs people in on a text terminal,
in place of getty: a screen saver always behind it, and on a key a box
asking for a user name, then that person's picture, name and password, and
whether to start the Desktop or a Shell -- Tab changes it, and the choice
is remembered. When the session ends the screen comes back. Linux only;
root cannot log in there. The Debian package ships `hibr-login@.service`
and `/etc/pam.d/hibr-login` **switched off**: `systemctl enable --now
hibr-login@tty2` puts it on tty2, and `disable` gives getty back. Its own
settings (saver, title, a message, the PAM service, glyphs) are root's, in
`/etc/hibr/login.json`.

It runs as root, and gives that up only by forking children that drop for
good: one reads the person's About Me and decodes their picture, handing
back JSON and a small PPM, so root never reads a person's files or hands
their picture to an image library; another runs their session. The auth
module gains `auth open` (password, account, credentials, a PAM session on
the terminal), `auth run` (the command as the user, with PAM's
environment, recorded in utmp and wtmp) and `auth close`.

**About Me** -- Control Panel > About Me: your name, a line under it, a
picture and the session the login screen starts, kept in
`~/.config/hibr/me.json`. It is data, never run.

**`exec -a name`, `-l` and `-c`**, as in bash: the program's `$0`, a login
shell's leading dash, and no environment. A shell started from the login
screen is a login shell.

**`img -o ppm`** writes a picture as a binary PPM at the size it would be
drawn, and `img` reads PPM back with no library.

## 0.99.21

**Screen lock.** Lock Screen on the hibr menu, or ctrl-alt-l (changed in
Shortcuts like any other), puts the screen saver on and keeps the desktop
behind it until your password is given. A key or a click shows a box
over the saver with your name and a password field -- a letter typed at
the saver is already the password's first -- enter checks it, escape puts
the box away, and a wrong password holds the box for two seconds. Locked,
no key reaches the desktop: not Detach, not Quit, not a shortcut; a held
desktop attached to while locked is still locked. Control Panel > Screen
Saver > Lock After locks on its own some minutes after the saver starts
("at once", or never, the default); Preview never locks.

The password is checked through PAM by a new module, `auth` (`auth check
[-s service] [-c dir] user password`), with libpam opened on first use
and no privilege: checking your own password is what PAM's own helper is
for, and hibr is never setuid. The Debian package installs
`/etc/pam.d/hibr` (the system's usual rules); without it the lock asks
`login`, or `screensaver` on macOS. With no PAM, Lock Screen says so and
does not lock. It locks this desktop, not the machine: another terminal,
console or login on the same machine is still open to whoever reaches it.

**The screen saver no longer takes a whole core over a busy terminal.**
While a saver ran nothing read the terminals' ptys, so a program writing
in a terminal behind it woke every wait at once and the saver redrew as
fast as it could: 100% of a core, measured; now 1%. Each window's idle
callback reads its pty while the saver runs, as it does for a hidden
window.

## 0.99.20

**Screen savers.** After ten idle minutes the screen is given to a screen
saver until the next key or click, which ends it and goes nowhere else.
Six to start: Matrix rain, Flying Toasters, a Classic Mac drifting about
with its little screen going from the happy face to a desktop to a window,
Pipes, Mystify and a big Clock that moves each minute. Control Panel >
Screen Saver chooses one or none at random, the idle minutes (or never),
and previews; the hibr menu's Screen Saver starts one now. Each saver is
a file in `savers/` -- a `_start` and a `_frame` -- and one of your own goes
in `~/.config/hibr/savers/`, listed beside them; `savers/saver.hibr` runs
any of them on its own in a terminal, by name or by path. Waiting costs nothing: the
desktop sleeps until the idle time is up. This is the first part of the
login and lock screens, which will run a saver behind them.

## 0.99.19

**Switches, rounded frames, diamonds and dashes.** An on/off choice can be
drawn as a switch: Control Panel > Appearance > Checkboxes is `box` (as
before), `knob` -- a knob that slides right when on, `(  ●)` and `(●  )`
-- or `block`, a block that does the same between brackets, `[  █]` and
`[█  ]`, each in the theme's colours: on in the accent, off dimmed. It is
used everywhere there is one: the Control Panel's rows and every dialog's
boxes. Left at `theme`, the theme decides -- a theme's file may suggest a
style with `"checks"`, and neon suggests the knob. Windows > Frame gains
`rounded`, with arcs at the corners, and the title-bar buttons two styles,
`diamonds` and `dashes`, coloured like circles and squares. Get Info's
permission boxes sit six columns apart now, to fit any style.

## 0.99.18

**A terminal's shell closes its window however it ends.** ctrl-d (or
`exit`) in a terminal closed the window only when the shell's status was
0 -- and a shell's status is its last command's, so after a command that
failed, ctrl-d left "exited 127 -- close this window" on a window closed
on purpose. A shell's window -- a plain terminal running a shell, or
Terminal Here -- now closes whatever it ended with. A program run in a
terminal is unchanged: it closes on success and stays to show why it
failed. A plain terminal counts as a shell when `TW_CMD` is one on its
own (hibr, bash, zsh, sh, dash, ksh, fish, tcsh, csh).

Control Panel > Terminal > **Close When a Program Ends** chooses what a
program's window does: `success` (the default, as before), `always`, or
`never`, which keeps even a successful program's window open to say so.

## 0.99.17

**JPEG pictures.** The image module decodes JPEG as well as PNG, through
libturbojpeg, loaded at run time the way libpng already is -- so the Image
Viewer opens `.jpg` and `.jpeg`, the Wallpaper picker offers them, and a
JPEG can be the desktop's wallpaper in every mode. A file is decoded by
what its first bytes say it is, not by its name. A photo's EXIF
orientation is honoured, so a picture a phone stored on its side shows
the right way up. `img` also writes its pictures in order with anything
printed before them now, where in a pipe a line printed first could come
out after. On macOS this needs Homebrew's `jpeg-turbo`, which the formula
now depends on; on Debian and Ubuntu it is `libturbojpeg0`.

## 0.99.16

**YouTube: playlists, channels, history and favourites.** A search lists
playlists and channels as well as videos, read in every form YouTube draws
them today; enter opens one -- its videos under its own title -- and
escape goes back to the list before. A playlist's, a channel's or a
video's address typed or pasted in opens it. `n` and `p` play the next
video in the list and the one before, and at the end of one the next plays
by itself (Play > Play Next Automatically, or Control Panel > YouTube).
What was watched is kept in a history (`h`) with where it was left, and
choosing it again takes it up there; `f` keeps a video, a playlist or a
channel as a favourite, starred wherever it is listed, and `v` lists them.
Both are files of their own under `~/.local/share/hibr/youtube/`; Control
Panel > YouTube has Clear History. This finishes #27.

**Fixed: a video could start with no picture.** When a page starts a new
media source -- every video, and every ad -- the app restarted its player
after taking the new stream's first bytes, and the restart could throw
away the init segment those began with, leaving the player waiting for
one that never came. `web take` now stops at a reset, so the player is
started afresh before any of the new stream arrives. A video's end is also
found from YouTube's own length, not only the stream's.

## 0.99.15

**YouTube, in a window.** The YouTube app, in the Internet folder,
searches YouTube and plays what it finds: type, enter, choose, enter.
Space pauses, the arrows seek and change the volume, `m` changes the
picture between half blocks, ASCII and plain, a click on the bar goes
there. Nothing here is a YouTube client of its own: YouTube's own page
runs in the web module's headless Chromium, its player asked for the
lowest quality and muted, and every chunk it plays is copied into a media
player here and decoded with FFmpeg's libraries -- the page fetches, hibr
watches. Search reads the results page's own data the same way. Control
Panel > YouTube keeps the picture, the quality and the frame rate. Ads
play as YouTube plays them.

Two module pieces make it, each usable on its own: `web tap`, `web take`
and `web tapseek` copy what any page hands its media source, video and
audio apart; `media feed` and `media pipe` make a player whose streams
arrive on pipes. The Browser no longer stops Chromium while a YouTube
window still uses it. This is #27's second part; playlists, channels,
history and favourites come next.

## 0.99.14

**Video and sound.** The `media` module plays video and sound from a file
or an address -- MP4, WebM, MKV, MP3, HLS, http -- with FFmpeg's own
libraries loaded at run time, the way hibr already loads libssl: nothing
linked, no headers to build, and no ffmpeg or mpv process. A decoding
thread runs a few seconds ahead, a sound thread feeds the device (ALSA on
Linux, AudioQueue on macOS) and keeps the clock, and the picture follows
it, drawn as coloured half blocks, or as ASCII in colour or plain. Seek,
pause, volume, a frame-rate cap and a colour detail setting for slow
terminals; `media info` says where it is. FFmpeg 5.1 to 8 are known, the
few structure fields read located per release from that release's own
headers (`tools/mvoffsets.sh`); anything else is refused by name.

`examples/play.hibr` is a terminal player built on it. This is the first
part of the YouTube player (#27); the desktop app comes next.

## 0.99.13

**Folders on servers, in Files.** Files reaches any WebDAV server as
though it were a folder here: **Servers > Connect to Server…** (or Control
Panel > Network Servers > Add Server…) takes a name, an address, a user, a
password and whether to check the certificate, and **Test** tries them
before anything is saved. Each server is then on the Servers menu and
shows as `NAME:/` in the title. Enter, backspace, rename, New Folder, Get
Info (size, time, type, ETag), drag, Copy and Paste all work there, to and
from local folders and between servers, never over something already
there; transfers run in the background with a note at each end, so a
large file never stops the desktop. Delete asks first, since a server has
no trash.

**Opening a file on a server** fetches a copy into `~/.cache/hibr/dav` and
opens that. With **Upload Edits on Save** on, saving the copy puts it back
over the version it was fetched as -- and if the server's copy changed in
the meantime, the edit goes up beside it as `name (conflict DATE).ext`, so
nobody's work is overwritten. Control Panel > Network Servers lists the
servers, each opening the dialog that changes it, and keeps the upload
switch, a timeout and Clear Cache. Passwords are never shown. Files also
gains **New Folder** for local folders.

Two shell fixes came out of it. `-nt` and `-ot` compare times to the
nanosecond, as bash does, so two changes in one second are told apart.
And `&` inside a function under `strict vars` no longer fails: it sets
`$!`, a special parameter, which the check had taken for a global the
function created.

## 0.99.12

**A WebDAV client.** The `dav` module lists, fetches and changes files on
any WebDAV server -- Nextcloud, ownCloud, a NAS, Apache or nginx DAV,
rclone -- with no curl and no libraries: its own HTTP/1.1 keeps a
connection per server and reuses it, redialling one the server dropped
while idle; replies may be chunked; redirects are followed; a login is
Basic or Digest, whichever the server asks for; TLS goes through the
shell's own relay, with a per-server switch for a NAS that signs its own
certificate. `dav ls`, `stat`, `get` and `put` (each `-r` for a whole
folder), `mkdir`, `rm`, `mv` and `cp`; `ls` and `stat` give maps with
`:=`, and `$DAV_CODE` the last status. `put -m ETAG` uploads only over the
version it was given and `put -n` only where nothing is, checked before
sending as well as asked of the server, since some servers ignore the
headers. A name in a listing that could step outside its folder is never
listed or written. Servers are set up with `dav server set`, kept in
`~/.config/hibr/dav` with mode 600 -- a list anyone else can read is
refused -- and `dav servers` never shows a password.

This is the first half of WebDAV in Files; the window comes next.

## 0.99.11

**A web browser.** The `web` module runs Chromium (or Chrome) headless and
drives it over its own DevTools protocol on a pipe -- no port, no
WebSocket -- turning each page into cells: the page's text placed where the
browser laid it out, in its colours and weights, over a half-block picture
of its backgrounds and images, taken with the text made transparent so the
two never fight. Links carry their addresses; wide characters take two
cells. Scripts get it too: `web open`, `web text`, `web links` (a map with
`:=`), `web eval`, `web click`, `web type`, `web key`, history and tabs.

**Browser,** in a new Internet folder on the hibr menu, is built on it:
tabs (click to switch, x to close, + for another), back, forward, reload
and an address bar that goes to an address or searches for anything else;
links followed with a click, fields typed into, the wheel scrolling, alt-
left and alt-right through history; bookmarks kept in a file of their own;
every tab back after a restart. Without Chromium installed it says what is
missing. This is ticket #38.

The image module's PNG decoder also reads from memory now (`im_pngmem`).

## 0.99.10

**Sheet formats its cells.** A Format menu works on the selection: **bold**
and *italic*; a text colour and a fill from the theme's own roles, so a
sheet follows the theme; alignment left, centre or right (numbers right
and text left unless told); a number's decimals, thousands separators,
percent and currency (`SS_CURRENCY`, `$` unless set); a bottom or right
border; a rule that colours a number by its value -- Negatives Bad,
Positives Good, or Rule... for anything like `> 100 good`; and frozen rows
and columns that stay while the rest scrolls. A format is kept with its
cell in the file, and Undo takes it back like any change.

## 0.99.9

**Standby displays.** `desktop --standby --name NAME` is a terminal that
waits to be a screen of the desktop: a quiet screen with its name until the
desktop runs, then it joins on its own; let go from the desktop (Detach),
it waits again -- marked *let go*, so it is not pulled straight back -- and
when the desktop quits it waits for the next. Control Panel > Displays has
a **Standby** line listing the ones waiting (a click asks one to join) and
**Standby displays join on their own**, on unless switched off, for
whether they come by themselves or only when asked. `q` stops waiting.

**Blank a display.** Right-click it in the Displays pane: it stays joined
and keeps its place in the arrangement, but goes dark and out of use --
windows on it move to the primary, none are placed there -- until it is
unblanked. Kept across restarts. The primary cannot be blanked.

## 0.99.8

**Sheet, a spreadsheet whose formulas are hibr,** in Office. A cell holds
text, a number, or `=` and hibr -- what a formula prints is its value
(`=math "A1 * 1.2"`, `=sum "${B1_B9[@]}"`), and one that is an expansion
is that (`=$((A1 * 2))`, `="${A1} each"`). Every cell a formula names is a
variable holding its value and a range `A1_B9` an array of them; formulas
are worked out in the order they need each other, and one that needs
itself says `#CYCLE`.

Formulas run in a hibr of their own under `--plan`, with `math` loaded,
two seconds of CPU and ten of the clock: they compute and read what you
can read, and a formula that would write, connect or start a program says
`#REFUSED` and what it would have done; one that loops says `#TIME`.
**Sheet > Trust This Sheet** asks, then lets one sheet's formulas run for
real -- remembered on this machine, never inside the sheet, so a sheet
cannot arrive trusted.

A sheet is a `db` file (`.hsheet`), one row per cell, written as it
changes: there is nothing to save, and Save As copies it. Typing replaces
a cell, enter or f2 edits it, shift and the arrows select, delete clears;
a column's edge in the header drags its width; the Sheet menu inserts and
deletes rows and columns -- formulas follow the cells they name -- and
adds more of either. Copy, cut and paste are tab-separated; CSV comes in
and goes out through File. Undo and Redo cover the session.

**Assignments in one command are made left to right,** as in bash:
`x=1 y=$x` sets `y` to 1, and `p=/a/b d=${p%/*}` sees `p`. hibr expanded
every assignment before making any, so the later ones saw the old values.
The command's own words are still expanded first, so `x=1 echo $x` prints
nothing, as in bash.

## 0.99.7

**Floating point: the `math` module.** `$(( ))` has only integers, as in
bash; `math` has the rest -- `+ - * / % ^`, comparisons, `c ? a : b`,
`round(x, n)`, `sqrt`, `abs`, `pow`, `ln`, `log`, `sin` and the like, `pi`
and `e`, shell variables by name, and `sum avg min max count` over arrays.
It prints a number the way a spreadsheet shows one: a whole number whole,
anything else to at most fifteen significant digits, so `math 0.1+0.2` is
0.3; `-s n` gives exactly n decimals, rounding half away from zero.

**A plan may load a module that only computes.** `--plan` refused every
`mod load` and `need`, since a module's builtins can do anything; `math`
and `md` are now let through by name -- never by path -- and found on the
module path, never in the current folder. Finding a module means opening
it, which runs its code, so the folder a planned script sits in must not
be able to supply one; the same now holds for any plan's search, as it
always did for root's. The spreadsheet's formulas run this way.

## 0.99.6

**The db module changes and deletes rows.** `db set h N col val...` changes
a record by the number `query -n` gives it; `db update h where ... set col
val...` changes every row that matches; `db delete h N...` or `db delete h
where ...` deletes (a `where` is required -- nothing deletes every row by
accident); `db compact h` writes the file again without its deleted rows,
the one thing that renumbers. Values are written where they are, zone
maps widened when a value moves outside them, and a deleted row is marked
in its group and passed over by every query, count and aggregate.

A database made before this (`HIBRDB1`, dBASE's included) reads and updates
as it was, and its first delete writes it again beside itself with the
marks, keeping every record's number. This is the ground the spreadsheet
stands on.

## 0.99.5

**Files has a search box,** on the right of the path. `/`, ctrl-f, View >
Search… or a click puts the keyboard in it, and the list narrows to the
names that hold what is typed, whatever its case. Enter keeps the filter
and goes back to the list; escape clears it; another folder starts clear.
A paste while it is open goes into it.

## 0.99.4

**The Control Panel has a search.** It sits at the top of the list: type
while the list has the keyboard, and only the panes that match are shown
-- by title, or by any row inside them, so "wall" finds Appearance through
its Wallpaper rows. The arrows move among what was found, escape clears
it, and a key the desktop holds still goes to the desktop.

## 0.99.3

**Notifications say who they are from.** The window manager marks the
window whose app it is handling -- a key, a click, a menu item, a frame --
and a note made meanwhile is from that app; the desktop's own are from
Desktop, and a terminal program's are from its title. The sender is on the
note's top border and in its own column in Notifications.

**Priorities mean what they say.** Feedback on what you just did -- Record
4 added, Wallpaper set, Not a valid name -- is low: it pops up and is not
counted. A failure -- could not rename, cannot write -- is high.

**Open Terminal Here closes on exit,** as any terminal does, instead of
staying up to say it exited with 0. And a window that closes itself is
taken off the screen at once, not at the next key.

**The Control Panel's list sits inside its window,** its highlight a
column in from the border and the divider, reaching the bottom border, and
the window opens larger (26 by 70). A setting's label too long for the pane
ends in an ellipsis instead of running into its control.

## 0.99.2

**Markdown, complete: the `md` module.** CommonMark with GitHub's
extensions -- tables, strikethrough, task lists, extended autolinks, the tag
filter -- parsed in C the way cmark does it. Every example in both specs
passes, compared byte for byte: 652 of 652 CommonMark 0.31.2, 24 of 24 GFM
extensions (`tests/md_spec.py`, in `tests/all.py`). `md html` writes HTML as
cmark-gfm does; `md lines` gives each line a style letter per character, for
a program that draws markdown itself. Nesting is bounded, every
pathological input runs in linear time, and it is clean under ASan and
UBSan after fuzzing.

**Write is built on it.** What was a line-by-line guess is now the real
document: setext headings, emphasis by the spec's rules, nested lists that
keep their indent, quotes in quotes, tables with their rules, reference
links, autolinks, multi-backtick code, entities, inline HTML. The selection
is drawn exactly over what is selected, and Export to HTML is `md html`.

**A split window's divider drags** -- a new widget, `widgets/split.hibr`.
The Control Panel's list and the file dialog's list and preview use it, and
where the divider is left is kept.

**The terminal's scrollbar** runs from its first row to its last; it began
in the title bar and stopped a row short.

**The desktop's menu:** Change Wallpaper… opens the Control Panel at
Appearance, Next Wallpaper steps through them as before, and Refresh
Desktop draws the whole screen again and looks again at what is on disk.

**The prompt** no longer takes an empty `.git` directory for a repository;
git does not either.

## 0.99.1

**One Open, Save As and Export dialog for everything.** A folder's
contents, folders first, filtered to the app's kinds of file (and All
files), with a preview of what is selected; Save As adds the extension and
asks before replacing. Write's Open…, Save As… and a new Export… (HTML, or
plain text as it reads), and dBASE's New Database… and Open Database…, all
use it; `dt_filepick` gives it to any app.

**Stickies have no frame.** A new window style, `bare`: the window's own
colour, a strip across the top a shade darker with a close box -- drag it
to move -- and a grow mark, and no shadow. Stickies use it.

**Every glyph is named again.** Write, Stickies, the Clipboard and the file
dialog had written •, ☐, ☑, ★, ⏎ and … straight into the code; they are in
`GL` now, with ASCII stand-ins, and `tests/540-examples.t` fails on the
next one. A one-line field takes ctrl-u and ctrl-k.

## 0.99

**Write, a word processor for markdown,** in Office. Markdown is shown as it
reads -- headings, bold, italic, strikethrough, code, links, bulleted,
numbered and task lists, quotes, rules, fenced code -- and only the cursor's
line shows its marks, dimmed, to edit exactly, as Typora does. A toolbar and
a Format menu add the marks to the selection or the line; a click on a
task's box ticks it. File > New, Open…, Save (ctrl-s), Save As…; Find… and
Find Next; undo, redo, cut, copy, paste and select all. Plain text files
are edited plain. Files opens `.md` and `.txt` in it.

**An app can keep a window open:** `_canclose` is asked by the close
button, Close and the Close Window key -- Write uses it to ask before
losing unsaved changes. **Save** (ctrl-s) is a desktop key that goes only
to a window with `_savefile`, so a terminal still gets ctrl-s. A file's
double-click uses its type's first Open With app.

## 0.98

**Stickies replace Note Pad.** Notes stuck on the desktop, as many as you
like, each yellow, blue, green, pink, purple or grey, saved as you type with
their place and size. Launching Stickies opens every note; the Note menu
makes a new one, changes its colour, or deletes it after asking. Text wraps
at the note's width, and its first line is its title. Control Panel >
Stickies sets new notes' colour and whether the notes open with the
desktop. Note Pad's note becomes the first sticky.

**Undo, Redo and Select All** are on the Edit menu, undo and redo on alt-z
and alt-y -- settable, like Copy -- through each window's `_undo`, `_redo`
and `_selall`.

**`widgets/textarea.hibr`**, a multi-line editor any app can use: cursor,
selection with shift and the mouse, word moves with ctrl, cut, copy,
paste, undo and redo, and soft wrap.

**`$(< file)`** gives the file's contents, as in bash, without a fork; it
gave nothing. And `dt_atstart` lets an app run something as the desktop
starts.

## 0.97

**The Clipboard.** Everything copied -- in any window, pasted from the
machine, or set by a program in a terminal -- is kept, and the Clipboard
desk accessory lists it newest first: Enter puts one back on the
clipboard, p pins it so it is never dropped, delete removes it, and a
paste into the Clipboard keeps that too. The last 50 are kept between
desktops in a file only you can read; Control Panel > Clipboard sets how
many, or none on disk, and clears it. Every clipboard change goes through
`dt_clipset`.

**A restart that cannot read its saved desktop says why** -- in
desktop.log, with the parser's own message -- and keeps the file as
`state.json.bad`, saying separately when it was saved by another version.

## 0.96

**Open With, as on a Mac.** Right-click a file in Files: Open With lists
every app that can open it, the default first, then Other… to type a
command for it; a choice opens it there once and is not remembered.
Control Panel > File Types gives every type an Open With row -- tick what
it lists, choose its default, which is what a double-click does. Apps
declare what they open with `dt_opener`.

**Open Terminal Here** on Files' right-click starts a terminal in the folder.
Control Panel > Files can take either item off the menu and chooses the
shell it runs.

**About hibr Desktop** (renamed) shows the module ABI, the uptime and who is
logged in, each user with their number of sessions. **Screenshot…** moves to
Desk Accessories, beside the Image Viewer that shows what it took.

**`cd` takes `-L`, `-P` and `--`**, as in bash; it refused all three.

## 0.95

**Screenshots show up and open.** A screenshot appears at once in a Files
window open on its folder -- it waited for the folder to be left and
entered again. An `.ans` screenshot opens in the Image Viewer, double-clicked
in Files or from the note that announces it: the viewer plays it into a
terminal emulator of its own and draws those cells, colours and wide
characters exactly, in a window sized to the shot.

## 0.94

**Copy and paste reach everything.** Every dialog's text field -- Rename,
Get Info, File Type, Clock Format, Set Date & Time, Time Zone, Screenshot
Folder -- takes copy (the whole field), cut (which empties it) and paste
(at the cursor, on one line), with the shortcuts, the Edit menu and a
paste from the real terminal alike. dBASE copies and cuts the line being
typed. Task Manager copies the selected process, Process Details and About
what they show, Notifications the selected note (cut clears it), and Clock
the time. `dt_textpaste` is the one-line paste every field uses.

**The Control Panel scrolls on both sides, each on its own.** The list of
panes has a scrollbar and its own scrolling, so it no longer stops at the
window's height; the pane beside it has its own, which shows when a pane is
longer than the window -- Shortcuts, where Screenshot sat out of sight.
The wheel scrolls whichever side the pointer is over.

## 0.93

**Screenshots.** ctrl-alt-g (settable, as Screenshot) or the hibr menu's
Screenshot… asks Screen, Window or Area -- an area dragged out with the
mouse, or drawn with the arrows and enter -- the last one taken already
chosen, so the shortcut and enter repeat it. The picture is the screen's
cells: ANSI text by default, which `cat` shows as it was, or HTML or plain
text, into `~/Pictures/hibr`, set in Control Panel > Desktop. The chooser,
the band and any notes are kept out of it.

**`console shot`** writes what is on screen, or a rectangle of it, as ANSI,
HTML or text.

## 0.92

**Notifications have a priority, and the count means something again.**
Low, normal, high, urgent. The desktop's own chatter -- switching or moving
to a workspace, tiling, Copied and Cut, keyboard move and resize, shortcut
capture, the Control Strip -- is low; a program's notification and anything
an app reports is normal; a failed restart is high. Every note still pops
up (high in the warning colour, urgent in the error colour, low dimmed) and
goes to the history, but only notes at Control Panel > Notifications >
Count From (normal by default) or above raise the bar's count.

The history shows each note's time and priority. Up and down select, delete
clears the selected one, and a History menu clears the low ones or all of
them. `dt_notep` and `dt_notifyp` post at a priority.

## 0.91

**Cycle through every workspace.** Control Panel > Desktop > Cycle Windows
On: `workspace`, the default, goes through this workspace's windows as
before; `all` goes through every workspace's -- from this one on and round,
each one's windows in the order they were opened, minimised ones passed
over -- switching to whichever workspace holds the next window. Both alt-tab
and Window > Cycle follow it.

## 0.90

**A minimised terminal no longer holds a core.** A terminal window's
output was read only while it was drawn, so one minimised or on another
workspace whose program kept writing -- `screen -r`, a build, a clock --
left its pty readable: the desktop woke at once, drew nothing that read
it, and went round again at 100% of a core. Windows that are not drawn now
have their app's `_idle` called each frame, and the terminal reads its
program there; a minimised one costs nothing again, and its screen is
current when it comes back. A Restart Desktop made it likely, by bringing
back a busy terminal minimised or on another workspace, but any of these
did it.

**The wheel flips workspaces.** Over the bare desktop or the menu bar, the
wheel goes to the next workspace (down) or the previous (up), wrapping
round; a burst of reports from one notch, or a trackpad, is one step.

**A title bar's menu moves the window to another workspace.** Right-click a
title: Move to Workspace, beside Hide. Move to Display is there only when
another display is attached -- it used to appear with just the one.

## 0.89

**Restart Desktop keeps your sessions.** On the hibr menu: the desktop
replaces itself with the hibr installed now, in the same process, and
carries on -- every window in its place, workspace, stacking and focus, and
tiling as they were. A terminal's program never stops: the same shell, with
its variables and jobs, and its screen and scrollback come back. Note Pad,
Calculator, Puzzle, Mines, Snake, Bricks, Files, Image Viewer and dBASE keep
their state; a game in play comes back paused. When apt or brew installs a
newer hibr under a running desktop, a note says so, and clicking it
restarts (Linux; it reads `/proc`). No screen or tmux needed around the
terminals any more. The update check rides the clock's once-a-minute wake,
so an idle desktop draws no more frames than before.

An app keeps state with `dt_keepstate app MAP...`, and `<app>_stash` /
`<app>_resume` for what a map cannot hold.

**`pty adopt` and `pty release`; `term adopt`, `term save` and `term pid`.**
A pty survives `exec` -- its master is not close-on-exec and the program
stays the process's child -- and the new image takes it over with its
screen, scrollback, cursor and modes.

**`json parse` into a subscript** -- `json parse m[3] "$doc"` replaces just
that entry.

**`export -n`** takes a name out of the environment, as in bash. hibr took
`-n` for a variable name and exported it.

## 0.88

**dBASE's menus are System 7's.** File (New Database, Open Database, Close
Database, Directory, Quit dBASE), Edit, Records (Append, Browse, Display
Structure), Query (List, Count, Sum, Average), Help -- in place of dBASE
III's own Assistant names, which nobody reaches for any more. dBASE moves
to a new Office folder, a submenu of the hibr menu.

**An app's File menu comes before Edit.** An app that calls `dt_editmenu`
after declaring its File menu gets the desktop's Edit there, as on a Mac;
one that does not still has it after its own menus. dBASE and Files do.

## 0.87

**dBASE, a desktop app.** A little dBASE III on the `db` module: the dot
prompt (`CREATE`, `USE`, `APPEND`, `BROWSE`, `LIST FOR load > 2.5 .AND.
host = 'web1'`, `COUNT`, `SUM`, `AVERAGE`, `DISPLAY STRUCTURE`, `?`,
`DIR`), an `APPEND` form, a scrolling `BROWSE`, and the Assistant's menus
typing the same commands. Records are only appended, as `db` has no update.

**`"$*"` and `"${a[*]}"` join with IFS's first character**, as in bash, and
`"${!a[*]}"` and the `[*]` slices with them. Every one of them joined with
a space whatever IFS said, so `local IFS=:; echo "${a[*]}"` printed spaces,
and a quoted `"${a[*]:0:2}"` came out as separate words.

**Bracket patterns know the POSIX classes.** `[[:space:]]`, `[[:alpha:]]`
and the other ten matched nothing in `case`, `[[ == ]]`, `${x#...}` or a
glob; they work now, alongside escaped members in a bracket.

**`db query -n` and `from n`.** `-n` gives each row's record number, from
1, first (`r[i]["#"]` in a map); `from n` starts at record n, skipping the
groups before it unread -- what a browser needs to page through a file.

**The bar gives way to long menus.** An app with many menus no longer has
the workspace numbers, the bell or the clock drawn over its titles; they
step aside until there is room.

## 0.86

**`db`, a small column store module.** `db create stats.db ts:int
host:str:16 load:float` makes a database of typed columns (int, float,
fixed-width str) in one file; `db insert` and `db import` (tab-separated)
append rows; `db query h where load gt 2.5 and host eq web1 limit 10`
prints the matching rows, or with `r := db query ...` gives them as a map
with numbers kept as numbers; `db count|sum|min|max|avg` aggregate. Rows
live in groups of 1024 with a zone map of each column's least and greatest
value, so a filter skips any group that cannot match without reading it.
A million rows import in under 0.4 s and a full-scan count takes about
30 ms. No SQL, no update or delete, one writer, no crash recovery -- see
`mods/db/README.md`.

**A map copied with `:=` keeps its JSON types.** `r := f`, where `f` left
a JSON document in `$RET`, gave back every number and boolean as a string.

## 0.85

**Every colour follows the theme.** 87 colours in the desktop were written
as midnight's values -- Files' selection, Note Pad's cursor, the
calculator's keys and answers, dim labels, the traffic-light buttons, exit
statuses, Mines' cells -- so on paper they were dark-theme greys on a light
face. A theme may now set seven colour roles beside its seven colours: dim,
selink (text on a selection), good, warn, bad, info and well (the surface
keys and cells sit in), each optional and defaulting to what the dark
themes share, so a theme file with only the seven colours still works.
Paper, phosphor and amber set their own; the others look as they did. A
game's own art -- the bricks, Snake's board -- stays as drawn.

## 0.84

**Tiling.** Window > Tile Workspace has the desktop lay out the current
workspace's windows itself: the main one on the left (Control Panel >
Windows > Tiled Main, 30 to 70 per cent), the rest stacked down the right,
laid out again as windows open, close, hide or move between workspaces,
and when the screen changes size. Make Main puts the focused window on
the left; a tiled window dragged onto another swaps places with it, and
anywhere else goes back. Each workspace is tiled or not on its own; a
fixed window floats over the layout; resizing a tiled window by hand is
refused with a note. Tile Workspace and Make Main are shortcuts with no
key until given one. With 0.81 to 0.83 this completes placement, snapping,
workspaces and tiling.

## 0.83

**Workspaces.** Three by default, for the whole desktop -- Control Panel >
Desktop > Workspaces sets 1 to 9. The bar shows the numbers with the
current one lit: click one to switch, or drag a window by its title onto
one to send it there. alt-1 to alt-3 switch, ctrl-alt-right and
ctrl-alt-left step, Window > Move to Workspace sends the focused window --
every key a shortcut you can change. A hidden workspace's windows have no
pane, so they cost nothing; each comes back stacked as it was left.
Opening an app that is open on another workspace goes there rather than
opening a second; new windows are placed as if the other workspaces' were
not there; the application menu lists every window with the number of the
workspace it is on. Fewer workspaces moves the windows of the ones that go
onto the last that stays.

## 0.82

**Snapping.** alt with an arrow puts the focused window on the left,
right, top or bottom half of the display, below the menu bar; the same
again puts it back where it was. Window > Snap has the four and Center.
Each is a shortcut in Control Panel > Shortcuts -- Center has no key until
given one. A fixed window can only be centred. With Shortcuts in Terminals
on, alt with an arrow snaps from inside a terminal too, as any chord the
desktop holds does; clear the shortcut, or Pass Every Key on that window,
to give it to the program.

## 0.81

**A new window goes somewhere sensible.** Every launched window used to
step down and across from the top left whatever was open, so the third
covered the first two. Now it goes in the first spot on the display that
overlaps nothing -- below the menu bar, clear of the Control Strip, with
room for each window's shadow -- and where there is none, where it covers
least; a window bigger than the display is shrunk to fit. Control Panel >
Windows > Placement: smart (the default), cascade (as before) or center.
An app can say its size as it opens with `<app>_size`.

**A terminal opens at 24 rows by 80 columns** -- the classic size, where
it was 12 by 50 -- shrunk to fit a smaller screen. Control Panel >
Terminal > Rows and Columns change it.

## 0.80

**The arrows belong to your apps again.** Since 0.77 a shortcut could be
set to any key, and the Shortcuts pane takes the very next key after a row
is activated -- so the arrow pressed to move to the next row became that
row's shortcut. A Menu Bar on the right arrow opened the menu bar from
every window, and the open menu then took the other arrows: no arrow
reached any app. Now:

- An arrow, enter, tab, space, backspace, delete, home, end or a page key
  pressed during a capture ends it unchanged ("Not changed") and moves on
  as it would have. With a modifier -- alt-left, ctrl-enter -- each is
  still a shortcut you can set.
- Settings saved by 0.77 to 0.79 with a shortcut on one of those keys have
  it put back to its default when the desktop starts, and entries for
  actions that no longer exist are dropped.
- Moving or resizing a window from the keyboard, a file drag, and the
  Control Strip's arrow mode each end on a click or on any key they have
  no use for, which then goes where it would have. Each used to swallow
  every key until its one way out, with a note that had already gone.

## 0.79

**The desktop's glyphs have names, and an ASCII set.** Every character the
desktop draws that is not plain text is named in `wm/glyphs.hibr` --
`${GL[vline]}` in the code, not a literal `│` -- written as its code point
with its Unicode name beside it. Appearance > Glyphs switches the whole
desktop to plain ASCII for a terminal or a font without box drawing. An
app's or pane's own icon stays as written on its registration line.

**`$'\u2502'` and `$'\U0001F514'` work**, as in bash: hibr dropped the
backslash and kept the digits. Found because the glyph table wanted them.

**Date & Time is under Hardware** -- it sets the machine's clock and time
zone. **The scrollbar follows the theme**: its thumb in the theme's accent,
its track in the idle colour, where it was always midnight's blue.

**`tests/750-pty.t` stops flaking**: two of its checks counted matching
lines, and how many arrive in one read is timing; each asks whether the
thing appeared, reading until it has.

## 0.78

**A held session rings, notifies, names its tab and keeps its links.**
hold redraws each attached terminal from an emulator of its own, and
anything that is not drawn text died there: the bell, notifications (OSC 9
and 777), the window title (OSC 0 and 2) and clickable links (OSC 8) --
checked against the same program run unheld, where every one arrives.
Each reaches every attached terminal now: the bell and notifications as
the program sent them, the title whenever it changes and to a terminal as
it attaches, and a link per character, so it stays clickable.

**The desktop does the same for a program in a Terminal window.** Its
notifications become desktop notifications -- clicking one brings the
window up -- and are sent on to your real terminal; a bell rings the real
terminal and marks the window's title with a dot until you click it; a link
it prints is a link on the real terminal. `console link`, `console bell` and
`console notify` do the same for a script, and the display interface
(version 4) carries links, so any tool drawing through it can make one.

## 0.77

**Every key the desktop acts on is a setting.** The menu bar's F10 and
escape, Copy, Cut and Paste (alt-c, alt-x, alt-v), Select All Icons
(ctrl-a) and the terminal's page back and on (shift-pageup and
shift-pagedown) were fixed in code; each is now an action in Control
Panel > Shortcuts like Close Window or Cycle Windows -- changed or cleared
there, and the change applies at once in every window and on the Edit
menu, which shows each one's current key. No key is reserved any more:
taking one another action has asks first and moves it. What stays fixed
is how an open menu or a dialog is driven -- the arrows, enter, escape,
y and n.

**The Keyboard pane is two settings and nothing to decipher.** Shortcuts
in Terminals (was Desktop Shortcuts Win): the desktop's shortcuts work
while a terminal has focus. Terminals Keep Ctrl+A-Z: ctrl with a letter
always reaches the program in a terminal -- ctrl-c, a shell's ctrl-w --
even where it is a shortcut; it was a fixed rule, and is now a setting,
on by default. The lines of text that explained the old fixed keys are
gone, since there are no fixed keys left to explain.

## 0.76

**The desktop costs half as much sitting idle.** Measured over the same
four windows (a terminal, the clock, Files and Note Pad): 0.57% of a core
idle before, 0.31% now, and a 30-report window drag 32 ms of CPU against
39. Two things were most of an idle frame, and neither was a window:

- **Every console write asked the kernel for the terminal's size**, so
  filling the wallpaper was 1920 system calls a frame. Writes now use the
  size found when the frame was last flushed, unless a resize has arrived;
  the fill went from 0.80 ms to 0.11.
- **The menus were rebuilt on every frame**, including the clock's once a
  second, though only input or a change of focus can alter a shut menu
  bar. A frame a timer asked for reuses the last build.

Redrawing only the windows that changed was weighed and not done: what is
left to save is a few tenths of a percent, against a change to the console
and a rule every app would have to keep. The numbers are in CLAUDE.md.

## 0.75

**One clipboard, everywhere.**

- **A copy in the desktop reaches your machine's clipboard again.** The
  desktop runs held, and `hold` draws from an emulator that kept nothing
  it did not draw -- so the OSC 52 a copy sends was dropped, and no copy
  ever reached any machine's clipboard. The emulator keeps it now and hold
  passes it to every attached terminal, so with a second machine joined, a
  copy lands on both.
- **Note Pad copies, cuts and pastes**, with a selection: shift and the
  arrows, home and end, or a drag. Text copied in a terminal pastes into it
  and back, which it could not before -- Note Pad had no copy or paste.
- **A program in a Terminal window can set the clipboard** (OSC 52: vim's
  `"+y` through a provider that uses it, tmux's `set-clipboard`), reaching
  the desktop and every attached machine. On by default; Control Panel >
  Terminal > Programs Set Clipboard turns it off. A request to read the
  clipboard is never answered.
- **A paste from your machine becomes the desktop's clipboard**, so alt-v
  pastes it again in any window, on either display.
- **Files and pictures copy as files.** Image Viewer copies its picture as
  its file; Files pastes copied files as copies (after Cut, moves them),
  and text that names no file as a new `Pasted text.txt`.

What does not cross: a terminal carries text only, so a picture or a file
on a machine's own clipboard cannot reach the desktop. That is recorded in
the backlog, with the cross-machine pointer.

The `"terminal"` interface is version 2, adding `clip`; `term clip t`
gives a script the text a program set.

## 0.74

**Escape works straight away in a held desktop, and a click after it is
no longer lost.** `hold` -- which the desktop runs under, so it can be
detached -- kept a lone escape back until the next byte arrived, to see
whether it began a mouse report. So escape did nothing until another key
or click came, and then the two reached the desktop stuck together: a
click straight after escape read as alt-escape and stray characters, and
did nothing. That was the mouse "not working sometimes". A read that ends
on an escape is now passed on at once.

**Inside GNU screen too.** Screen 4.09 has the same habit of its own,
whenever the program in it has the mouse on, and that cannot be fixed from
inside: escape still waits there for the next key (F10 does not). But the
desktop now reads an escape that arrives glued to a following sequence as
an escape and then that sequence, so the click or key after it still
lands. tmux sends escape on after its `escape-time`. Both are described in
the desktop README, under Inside GNU screen or tmux.

**An Arch package.** `packaging/aur/` holds a PKGBUILD built with gcc,
checked by running its build, check and package steps against the release
tarball; `packaging/aur/update.sh` points it at a release and publishes it
to the AUR once the account to push from exists.

## 0.73

**Themes are files.** Each of the ten themes is now a small JSON file --
seven colours and a shadow strength -- in `examples/desktop/themes/`, and
`~/.config/hibr/themes/` is read first: a file there replaces the bundled
theme of its name, and any other name adds a theme to Appearance and the
Control Strip. A file is checked whole, every colour `#rrggbb` and the
shadow 1 to 100, and one that fails is left out rather than half applied.
JSON rather than a script because a theme is only data and is the thing
people share: one you were given can only ever be colours
([decision 0028](docs/adr/0028-a-theme-is-data.md)). The list is now
sorted by name, so the order the Theme row cycles through has changed;
midnight is still the default.

**New defaults**, chosen to match what most desktops do:

- **alt-tab** cycles windows (was tab) -- and works from inside a terminal.
- **ctrl-w** closes the focused window (was alt-f4). A terminal keeps it
  for its program, where it deletes a word or drives vi's windows: a
  terminal now keeps every ctrl with a single letter, not only ctrl-c, d
  and z, since those are the control characters programs read.
- **Quit has no key.** It is on the hibr menu; one stray `q` no longer
  ends the desktop. Shortcuts can give it one.
- **Holding alt while dragging** anywhere in a window moves it.
- **A terminal window shows its scrollbar.**

Settings saved by an earlier version are brought up to these once, on
load: a key still at its old default moves to the new one, and a key you
had changed stays as you set it. Neither super nor ctrl-escape can open
the menus: most terminals never report super on its own, and most
terminals send ctrl-escape as a plain escape -- which already opens the
menu bar, as F10 does.

## 0.72

**The Control Panel is grouped by what a pane is about.** Three headings
now: **Hardware** -- Displays, Keyboard, and a new Mouse pane; **Desktop**
-- Appearance, Control Strip, Date & Time, Desktop, File Types,
Notifications, Shortcuts, Windows; and **Apps**, one pane per app. Every
shortcut binding moved from Keyboard to its own **Shortcuts** pane, and
**Keyboard** is now about the keyboard: Desktop Shortcuts Win (which was in
Terminal) and the keys the desktop always keeps. **Mouse** holds what was
the mouse half of Windows -- edge resizing, modifier drag and its key, and
what a double click on a title bar does. The window opens two rows taller,
22, so the picker fits; it used to draw its last row over the border.

**Desktop Shortcuts Win is on by default.** A chord shortcut such as
alt-tab now works while a terminal has focus, without finding the setting
first; plain keys and ctrl-c/d/z still reach the program, and Pass Every
Key in the Window menu still gives one window everything. Settings saved by
an earlier version have it switched on once, on load (`DT_SETVER`), since
a saved file holds every default as it was when written; switch it off
again in Keyboard and that stays.

**A Mac's disks are on the desktop.** The disk icons were read from
`/proc/mounts`, which macOS does not have, so only Home showed. With no
mount table the desktop lists `/Volumes` instead, the startup disk as `/`.

**`make CC=gcc` builds the modules.** They were linked without `-fPIC`, which
tcc does not need and gcc does; the shell built and every module failed to
link.

**The README says how to install it**: Homebrew, the apt repository, Arch
(from source for now) and from source, and how to take it out again.

## 0.71

**A terminal window can give the desktop its shortcuts back.** Every key
used to reach the program inside a focused terminal, so a shortcut bound to
a chord -- alt-tab for Cycle Windows -- never worked while one had focus.
Control Panel > Terminal > **Desktop Shortcuts Win** (off by default) makes
a terminal decline any shortcut the desktop or an app launcher has been
given, so the desktop runs it instead -- but only a chord (ctrl or alt with
a key) or a function key. A plain key such as `q` or `tab` always reaches
the program, and so do ctrl-c, ctrl-d and ctrl-z, whatever is bound to them.
One window can still be given everything: **Pass Every Key** in the Window
menu, ticked, for a program that needs the chord or a desktop running inside
that terminal. Shift-right-click opens the terminal's own menu even when the
program has taken the mouse, which was already so and is now tested.
`tests/apps.py` checks each case through a real pty: the chord raising the
next window, reaching the program with the setting off, reaching it with
the window's override, the menu item, and the right-click.

## 0.70

**The six bash differences found while checking the documentation, fixed.**

- **`mapfile -O n` keeps the array**, as bash does: the lines go to `n`,
  `n+1`, ... and everything else stays. It padded the keys below `n` with
  empty strings and replaced the rest; `mapfile` without `-O` still clears.
- **`exec 3< <(cmd)` reads what `cmd` writes.** The substitution's own pipe
  took the lowest free descriptor -- 3 -- so `/dev/fd/3` became the
  redirection, and cleaning up after the substitution closed it. Its
  descriptor now starts at 60, clear of the 0-9 scripts use and of `{var}`
  descriptors from 10.
- **An arithmetic error in an expansion ends a non-interactive script**, as
  in bash: `echo $(( 1 / 0 ))`, `x=$((08))`, `${a[1/0]}`, in a function, in
  a condition. `(( ))` and `let` still only fail, with status 1 (and `let`
  no longer leaves its error to fail the next command); an interactive
  shell abandons the line; inside `try` only the command fails. The desktop
  calculator, which relied on `$(( ))` failing quietly, evaluates with
  `(( v = in, 1 ))` now.
- **Case conversion knows more than ASCII**: `${x^}`, `${x^^}`, `${x,}`,
  `${x,,}`, `${x@U}`, `${x@L}`, `${x@u}`, `declare -u`/`-l` and `str
  upper`/`lower` change the cased letters of Latin (with Turkish `ı`/`İ` and
  the `ǅ` digraphs), Greek and Cyrillic, and Armenian, by a table of hibr's
  own, whatever the locale -- the same stance as ADR 0021. Bytes that are
  not UTF-8 pass through untouched.
- **`kill -l` and `kill -L`** list the signals, or translate a number (an
  exit status above 128 too) or a name; `kill -s name` and `kill -n num`
  work; signal names are matched in any case; the signal table knows the
  standard set rather than twelve.
- **`recv` on a UDP socket takes a whole datagram**: a line drops its
  trailing newline, `-n` keeps that many bytes. It read a byte at a time,
  which on a datagram socket throws the rest of the datagram away and then
  waits for ever.

`tests/900-bash-gaps.t` compares the shell behaviour against bash;
`tests/905-case-udp.t` records the case table, `recv` over UDP and `try`.

## 0.69

**The documentation, all of it, held to what the shell does.**

- **Every example is checked or says why not.** `tests/531-doc-examples.t`
  reads every page with examples -- all of `docs/`, the READMEs, the
  decision records and the desktop guides. A ```` ```sh ```` block must be
  followed by its real ```` ```output ```` or carry `<!-- not run: why -->`
  (it needs a terminal, root or the network), and any other code block must
  say what it is. Every example was run and its output pasted back.
- **That found the pages wrong in places.** `language.md`'s declared-arguments
  transcript could not have come from its declarations; `cookbook.md` and
  `llm.md` each described `set -e` backwards in one line; `display.md` still
  said `mod load screen` and that panes have no order; `prompt.md` said
  right-hand prompts did not exist; the desktop README described a Displays
  pane that works differently; and builtin counts, ABI numbers, module lists,
  test counts and callback tables were stale across a dozen pages.
- **Every page was then read end to end** for what 0.57-0.68 added and the
  pages had not caught up with, and for duplication; the README and the docs
  index were rewritten around a start-here path, and the README's
  measurements were taken again on this release (398 KB, 87 ms on the loop
  against bash's 211, 1852 kB at startup).
- **`docs/tutorial.md`: from bash to hibr in ten minutes**, install to
  `--plan`, nine checked examples.
- **`man hibr`.** `docs/hibr.1.in`, with the version and module directory
  filled in by `make`, installed under `share/man/man1`.

**Four bugs the checking found, fixed:**

- A plain assignment refused because the variable is readonly, or typed and
  given a value of the wrong kind, ends a non-interactive script with status
  1, as bash does; hibr carried on. An interactive shell abandons the line; a
  prefix assignment (`r=2 cmd`) still only warns; a strict-mode refusal
  stays catchable.
- `send` and `recv` refuse a descriptor that is not a number: `send "" x`
  wrote to standard input.
- The `sys` module's `epoch` fills `:=`.
- The console module's messages said `screen:`, its old name.

Six more were found and are recorded in `docs/backlog.md` rather than
fixed in a documentation release.

## 0.68

**Four differences from bash, found by running real system scripts under
both shells and read one at a time.** All twelve invocations of the four
scripts that showed them now agree with bash:

- **A `set -e` pipeline whose last stage fails stops with that stage's
  status**, as bash does: `set -e; false | (exit 2)` exits 2, not 1. hibr
  still stops when only an earlier stage fails (ADR 0002), and then the
  failing stage's status is the one it has. (`uz`)
- **`select` over an empty list does nothing**, without a prompt or a read,
  and at end of input it writes its newline to standard output, not to
  standard error. (`tzselect`, which probes for `select` that way)
- **`$[ … ]` is arithmetic**, bash's old spelling of `$(( … ))`; hibr left
  it as text. (`byobu-ulevel`, which shifts by `$[$OPTIND-1]`)
- **`tests/corpus.py` normalises a `mktemp` suffix** inside the sandbox, the
  way it already normalises the sandbox path: `aptitude-run-state-bundle`
  differed only in its temporary directory's random name.

`tests/895-corpus-four.t` compares each against bash. The full set of 465
was not re-run here.

**The desktop's backlog entry said five steps of six.** The sixth, the
terminal window -- a hibr inside a hibr -- had been built for some time;
the backlog and decision 0020 now say so.

## 0.67

**A quoted key works inside arithmetic.** `$(( m["content-type"] + 1 ))`,
`(( m["x-y"] = 5 ))` and `for (( m["i"] = 0; ... ))` reach the literal key,
the same as `${m["content-type"]}` always has (ADR 0006). The text of
`$(( ))` and `(( ))` used to lose its quotes before the evaluator read it,
so the key was evaluated -- `content` minus `type`, key 0 -- and the only
way round it was to read the field into a variable first. A quoted variable
works as the key too (`$(( m["$k"] ))`), and so does a key with an
apostrophe in it. Quotes outside a subscript are removed as before, as bash
does. Cost: whether a piece of arithmetic holds a quoted key is decided
once and kept, so an arithmetic loop pays 0.04-0.14%.
`tests/890-arith-quoted-keys.t`.

## 0.66

**`hibr --plan script`: a dry run.** The script's own logic runs --
variables, functions, loops, reading files -- but everything that would
change the machine is refused, and each refusal is listed with its line:

```
hibr: plan: job.sh:1: would write out.txt
hibr: plan: job.sh:2: would run curl -sO https://example.com/app.tgz
hibr: plan: job.sh:5: would run rm -rf build
```

Writes outside the plan's own scratch `$TMPDIR` open `/dev/null` instead, as
do network endpoints; a program runs only if it is known to read (`cat`,
`grep`, `find` without `-delete`/`-exec`, `sed` without `-i`, `git status`
and the other reading subcommands, ...), and anything else fails with status
1 and no output -- so a plan stops where the script needed a result it could
not have, rather than carrying on with empty values and listing writes to
the wrong paths. `exec`, `kill` (but `-0`), `listen`, `mod load` and `need`
are refused. The scratch `$TMPDIR` is real and removed at the end, so temp
files work; the record goes to a copy of standard error a script's
`2>/dev/null` cannot reach; with `--agent` each record is a JSON line keyed
`plan`. `awk` and every program that runs another (`env` with arguments,
`timeout`, `xargs`, `sudo`, `sh -c`) are refused: whether they write cannot
be told from their arguments. It is not a sandbox, and says so: a program on
the read-only list runs for real. ADR 0027; `tests/885-plan.t` checks every
kind of refusal and that nothing the script named was created.

A policy that runs for real, the other half of the backlog item, waits for a
machine with Landlock: this one's kernel has it switched off.

## 0.65

**`[[ =~ ]]` fills `BASH_REMATCH` as well as `M`.** Measured, not argued:
every reply `tools/llm-measure/` got from a model writing hibr without its
reference page read its captures from `BASH_REMATCH` -- six of six -- and
printed nothing, because hibr filled only `M`. ADR 0004 had left the name out
so scripts would not work "by accident"; these were bash scripts failing for
a name, so it is amended. `=~` copies its captures into `BASH_REMATCH` after
filling `M` and clears it on a miss, as bash does; `M` stays the documented
place, and `match`, which bash does not have, fills only `M`. The same
replies, unchanged, now pass that task six times of six, and three that
pulled JSON fields out with a regex pass as well. `tests/880-bash-rematch.t`
compares against bash.

The README's "not implemented" list said `declare`, `shopt`, `set -o`,
coprocesses, anchored replacement, extended globs and `BASH_REMATCH`; all of
them work now. What is left there is bash's compound coprocess, `coproc name
{ ...; }`, since hibr's `coproc` takes a command.

## 0.64

**Dragging no longer leaves a window crawling after the mouse.** Every drag
report was drawn as its own frame, so whenever a frame took longer than the
gap between reports -- a big terminal, a busy screen, a held session -- the
reports queued up and the window went on replaying them in slow motion after
the hand had stopped. Redraw Skip only thinned that by a fixed count. Now a
drag or a wheel is drawn only once it has caught up: while the next report
is already waiting, the frame is skipped, and Redraw Skip thins what is left.
A burst of a hundred drag reports drew 102 frames and now draws 2; a hundred
and fifty, on a 140-column desktop with the Control Panel open, took 1.2
seconds to catch up, held or not, and now take 0.03. `console waiting` is
the new question the console module answers for it. `tests/desktop.py`
counts the frames, through a frame count the idle marker now carries.

Measured at the same time, so that it is not taken for the cause: an idle
desktop of this release, held or not, uses 0.2% of a core, and Redraw Skip
does work -- set to 5, it cut the same burst from 1.2 seconds to 0.23 before
this change.

## 0.63

**What `docs/llm.md` is worth, measured.** `tools/llm-measure/` sets a model
thirteen tasks -- three ordinary, four where a bash habit means something
else in hibr, six that need something only hibr has -- and runs what it
writes. From an empty directory, with no project context and tools off, three
runs each:

| | plain | trap | feature |
|---|---|---|---|
| Haiku 4.5, no page | 9/9 | 7/12 | 10/18 |
| Haiku 4.5, with the page | 9/9 | 12/12 | 18/18 |
| Sonnet 5.5, no page | 9/9 | 9/12 | 5/18 |
| Sonnet 5.5, with the page | 9/9 | 12/12 | 18/18 |

Ordinary bash was never broken. Without the page every regex capture read
`BASH_REMATCH` (6 of 6) and nothing parsed JSON or declared arguments. The
"with the page" rows are the page as fixed by this release: the page as 0.62
shipped it left Haiku at 11/12 and 16/18, and each miss named a gap --
`json get` on an array gives JSON text, a regex with a blank after `=~` goes
in a variable, `arr sort` is textual without `-n`, and `fn` always has a
parameter list. `docs/llm.md` says all four now. Since those fixes came from
these tasks, a fresh set is the honest next check; the runs, both prompts
and the replies are kept in `tools/llm-measure/runs/2026-10-01/`.

**The cookbook and the data page are held to their examples.**
`tests/531-doc-examples.t` runs each example with an output block in
`docs/llm.md`, `docs/cookbook.md` and `docs/data.md` on every build; a hidden
`<!-- setup -->` comment supplies the log file or helper an example assumes.
`docs/data.md`'s results were `# 11`-style comments and are printed and
compared now -- every one was right. Running them found 0.62's background
function bug.

## 0.62

**A function run in the background or as a pipeline stage ran only up to
its first program.** `f &` and `f | cat` fork a child for the stage, and
the child may replace itself with the stage's program rather than fork
again. That permission was a flag on the whole child, so when the stage was
a function it was used by the first program *inside* the function: the
child became that program, and everything after it in the body -- the
`echo`, the `return 3` -- never ran. The status was the program's. `f() {
sleep 1; return 3; }; f & wait $!` reported 0; bash reports 3. `ex_cmd` now
takes the flag as it starts and clears it, so only the command the fork was
made for can use it. A plain program in a pipeline or in the background is
still exec'd without a second fork. Found by running the cookbook's `wait
-n` example, which printed status 0. `tests/875-function-in-child.t`
compares each case against bash.

**Control Panel, Displays:**

- **The pane list could not be clicked while Displays was showing** -- only
  the arrow keys left it. The pane's own drawing cleared every clickable
  region in the window, the list's included, which the panel had just
  registered. It clears nothing now; the panel already has.
- **"Primary:" and its dropdown were on two rows.** They share one, the way
  every other pane lays out a label and its choice.

`tests/desktop.py` checks both. `tests/531-llm.t` is `tests/531-doc-examples.t`
now, ready to hold `docs/cookbook.md` and `docs/data.md` to their examples
as well as `docs/llm.md`.

## 0.61

**`return`, `break` and `continue` where they mean nothing are reported, and
the script runs on**, as in bash. A `return` at the top level of a script,
of `-c` text or of `eval`, a `break` or `continue` outside any loop, and
hibr's own `ret` outside a function all used to end the whole script
silently. Now each says why -- `return: can only `return' from a function or
sourced script` -- and the script continues: status 2 for `return` and
`ret`, 0 for `break` and `continue`, bash's numbers.

**A function cannot break its caller's loop**, and neither can a `( )`
subshell, as in bash: `f() { break; }` called from a loop used to end that
loop, and now it is an error inside `f` and the loop goes on. A `$( )`, a
pipeline stage or a `&` job inside a loop may still `break`, which ends only
itself, and `eval` and `source` are not boundaries, both as in bash. A loop
counts its depth, and a function call and a `( )` start it again from none.

`tests/870-return-break-outside.t` compares each of these against bash. The
cost: 9 instructions per function call (0.055% on a loop calling one) and
nothing measurable on a plain loop.

## 0.60

**A script runs as it is read, as in bash.** hibr used to read a whole
script, parse all of it, and only then run it. Now it reads one complete
command, runs it and reads the next -- for a script file, `-c` text,
`source`, `eval` and standard input -- so:

- **A late syntax error** stops the script there, after the lines before it
  have run, with status 2. Before, none of it ran.
- **A script on a pipe streams.** `(echo 'echo a'; sleep 1; echo 'echo b') |
  hibr` prints `a` at once, not after the second; stdout is flushed before
  each wait for more input. Measured: `a` at 0.002s, `b` at 1.00s.
- **A `read` in a script on standard input takes the script's next line**,
  as POSIX asks and bash does: a pipe is read a byte at a time, and a file
  on standard input a chunk at a time, seeked back to just past the line.
- **"Unexpected end of input" is status 2**, as bash's is, not 1.

**`checkfirst`: parse the whole script first, when that is what you want.**
`hibr --checkfirst`, `set -o checkfirst` or `shopt -s checkfirst` parses a
script file, `-c` text, standard input (then read whole) or a `source`d file
before running any of it, and runs none of it when it does not parse -- the
old behaviour, now a choice. Agent mode turns it on. `eval`, traps and
`$(…)` run as read either way. ADR 0026 records both, and the one case a
pipe cannot match: a command still incomplete after 64 lines is re-parsed
only as it grows by a quarter, since every line made a 3,000-line piped
function take 3.1 seconds (now 0.02), and lines already waiting past its end
are read with it -- so a `read` straight after such a command, in a piped
script, misses the line bash would give it. From a file it is exact.

Cost: the `while` loop's instruction count moved 0.001%; sourcing the whole
desktop is 0.02% cheaper, since each command's parse memory is released as
it finishes rather than when the script ends. `tests/860-read-as-you-go.t`
runs a late syntax error through a file, a pipe, a redirect, `-c`, `source`
and `-n`, with and without `checkfirst`, and a `read` taking the script's
next line from a pipe and from a file.

**Found by fuzzing the new pipe path, and fixed:** a malformed `${…}` in the
regex after `=~` -- `[[ x =~ ${^(a) ]]` -- crashed the parser on a null
word. It did in 0.59 too; only the new path's fuzz run reached it. It is a
syntax error with status 2 now, as in bash (`tests/865-regex-bad-sub.t`),
and an error inside a re-lexed `${…}` now stops the parse rather than being
dropped.

## 0.59

**`hibr --explain script`: the mistakes in a script, named, without running
it.** It parses, runs nothing, and reports each finding with its line, what
is wrong and what to write instead; with `--agent` each is a line of JSON
keyed `warning`. Status is 0 when clean, 1 when something was found, 2 for
a syntax error. Ten rules, each for a mistake models (and people) make:
`unquoted-path` (`rm $f`), `cd-unchecked`, `for-ls`, `test-unquoted`
(`[ $x = y ]`), `bind-program` (`x := uname`), `unset-quoted-key`
(`unset 'm[$k]'`), `local-self-ref` (`local a=$1 b=${m[$a]}`),
`local-masks-status` (`$?` after `local x=$(cmd)`), `dead-return` (a failing
`return` after `ret`) and `bare-key` (`${m[row]}`). The rules are a module,
`mods/lint/`, offering `"lint"`; `--explain` asks for it, so a script that
is run rather than explained never loads it. bash's `set -e`-in-a-condition
trap is not a rule, because hibr's errexit already reaches those functions
(ADR 0002).

Every rule was run over hibr's own 68 example scripts and `tests/self.hibr`
before shipping, and each finding was either fixed or the rule narrowed: 62
findings became 2, both in `tests/self.hibr`, which reads an unquoted key on
purpose.
`test-unquoted` passes `$#`, `$?`, `${#x}` and a variable the script only
ever sets to a number; `dead-return` only a failing `return`;
`bind-program` a name some module makes a builtin (`ls`); `bare-key` an
array made in another file, which may be `-A` there. Then over 259 shell
scripts in `/usr/bin` and `/usr/sbin`, parse only: no crash, and a sample
of what it reported read as real.

`tests/850-lint.t` has a firing case and a quiet case beside it for every
rule, and fails if any example stops linting clean.

**Found by it, and fixed:**

- **`dt_textkey` told every caller it had handled every key.** It ended an
  unhandled key with `ret "$text $cur"; return 1`, and `ret` had already
  returned 0 -- the `dead-return` rule's own example. It returns 1 now.

**Found while testing it against system scripts, and fixed:**

- **bash's `function` keyword did not parse at all**: `function f { ...; }`
  and `function f() { ...; }` were both syntax errors, which stopped
  `iptables-apply` and `ip6tables-apply` before their first line. Both forms
  work, as does a name bash allows there and `name()` does not
  (`function e-f`).
- **An `exit` inside a condition exited 0.** `if exit 3; then :; fi` exited
  with the `if`'s own status rather than 3, and the same for `while`,
  `until` and `!`, for `return` inside them, and for a `set -e` stop in a
  function called from one -- which is how it was found: that script
  printed its errexit message and then reported success. Each keeps the
  status it was stopped with now. `tests/855-exit-in-condition.t` compares
  every case against bash. The check costs 0.12% of the instructions on a
  `while` loop.

## 0.58

**Agent mode: `hibr --agent`, or `set -o agent`.** For a script a program
runs rather than a person -- a language model's, a build's, a harness's:

- **Errors are one line of JSON each** on stderr, keyed by level, with the
  file, the line and the source line's text, and a column for a syntax
  error: `{"error":"missing: unbound variable","file":"run.sh","line":2,
  "source":"echo \"$missing\""}`. Nothing is invented -- no codes, no hints.
- **`set -u` and strict expansion** are on: an unset variable is an error,
  and an unquoted expansion is one argument. `set -e` is left to the
  script, since hibr scopes it differently from bash (ADR 0002).
- **Nothing waits on a terminal:** a terminal on standard input is replaced
  by `/dev/null`, so `read`, a pager or an editor ends at once.
- **`HIBR_TIMEOUT=seconds`** bounds each foreground process: TERM, then KILL
  two seconds later, and status 124, as timeout(1) reports. Under it each
  foreground child gets a process group of its own, so what the child
  started is ended with it, rather than left running and holding the
  output pipe open. It bounds processes, not the shell's own work -- a
  builtin, a function or a `while` loop is not a process to end.

ADR 0025 records each choice. `tests/605-agent.t` covers the JSON, `set -u`,
strict expansion, piped input and the timeout; `tests/editor.py` checks a
terminal read through a pty; the language guide and `docs/llm.md` show it.

**Found while building it, and fixed for everyone:**

- **A syntax error exited with status 0**; it is 2, as in bash. An agent
  would have taken a script that never ran for one that succeeded.
- **An empty `then`, `else`, `do`, `{ }` or `( )` was accepted**: `if true;
  then fi` passed, and `while true; do done` looped for ever. Each is a
  syntax error naming the token that came too soon, as bash says it --
  `syntax error near unexpected token `fi'`. The error that used to say only
  "near unexpected token" names it too, and after the first error nothing
  more is reported, so `eval "if then"` is one error and status 2.
- **An error could come out before what the script printed ahead of it**,
  because stdout is buffered in a pipe and stderr is not; `echo start; echo
  "$missing"` logged the error first. Errors flush stdout now. Eight
  recorded tests had captured the old order and are re-recorded; each
  recording is the same size, since only where lines fall has changed.

**Array elements keep a bracketed key whole.** `m=([c d]=2)` split at the
blank into `[c` and `d]=2`, and `a=([k]=$v)` with a spaced `$v` made two
elements. An element opening with `[` is read to its matching `]`, and a
`[key]=value` element is expanded as an assignment -- never split or globbed
-- while a plain element still splits, as in bash.
`tests/604-assoc-keys.t` compares with bash.

One difference is recorded, not fixed: bash runs a script a command at a
time as it reads it, and hibr parses the whole script first, so a syntax
error late in a file stops hibr before its first line runs. It is in the
backlog under "Stay a drop-in for bash", and `docs/llm.md`'s table says so.

HIBR_VER -> 0.58.

Verified: tests/all.py, all thirteen suites green in 167 seconds;
tests/asan.py, every suite against the sanitizer build with no report;
`tests/diff.py` 1,000 snippets against bash with no difference;
`tests/fuzz.py` 400 parser rounds on the sanitizer build, no crash or hang.

## 0.57

**A page for a language model: `docs/llm.md`, and `llms.txt`.** The whole
language on one page. One table covers every place hibr deliberately
differs from bash; then each addition has a run example: signatures and
types, `ret` and `:=`, nested maps, `str` and `arr`, `match` and `rsub`,
`json`, `try` and `fail`, `opt` and `args`, sockets, and `strict`, and
the page ends with the mistakes to avoid. Every example was run and its
output pasted under it, and `tests/531-llm.t` runs each again on every
build, naming any whose output no longer matches -- a check none of the
other guides has had. `llms.txt` at the root points a model to it, and the
project README and the docs index link it.

**Arrays declared `-A` take every subscript as a literal key, as in bash.**
Writing the page's table found this. bash reads an indexed array's
subscript as arithmetic and an associative array's as a key; hibr has one
kind of array and read both as arithmetic. So everyday bash like
`declare -A seen; seen[$line]=1` sent any line holding a dash, a plus or a
space to key `0` or to a parse error, silently or noisily. `declare -A`,
`local -A` and `typeset -A` now mark the variable, and its subscripts stay
literal after expansion: `h[content-type]`, `h[$k]` with `k=a-b`, and
`h[-1]`, as bash has them. Other arrays keep hibr's rules, `declare -p`
prints `-a` or `-A` accordingly, and ADR 0006 records it.
`tests/604-assoc-keys.t` compares with bash. The desktop's maps are all
`declare -gA`, so the unquoted-subscript trap no longer applies to them. It
costs 0.04% on a loop reading a map: the check runs only where a subscript
would otherwise be evaluated. The first version, a lookup on every
subscript, cost 0.7%. One gap is known and recorded: `declare -A m=([c d]=2)`
splits at the space, where bash keeps `c d` as one key.

**`args` works when a function calls it more than once.** `opt` added to
one table for the whole shell and never replaced, so a function that
declared its options and parsed them got them right on its first call and
only defaults afterwards. A redeclared option overwrote the parsed value
with its default, and every function's options leaked into every later
`args`, a required one failing calls elsewhere. The page's `args` example
showed it, printing `count=1` for `--count=5`. Now `args` uses its
declarations up after parsing, and redeclaring a name replaces its entry,
so a function declares and parses on every call, and two functions never
see each other's options. `tests/350-args.t`'s own workaround, `opt -clear`,
is now documented rather than required.

HIBR_VER -> 0.57.

Verified: tests/all.py, all thirteen suites green in 166 seconds;
tests/asan.py, every suite against the sanitizer build with no report;
`tests/diff.py` 1,000 snippets against bash with no difference.

## 0.56

Desktop moves to **0.34** alongside this release.

**A dialog shows which button Enter presses.** While focus is in a dialog's
field or list, its first button is drawn as the default: Apply in the
wallpaper picker, Rename, Save, OK, Yes. It gets its label in the accent
colour, bold, because that button is what Enter does from there. A button
that has focus is still filled with the accent, so the two never look
alike. In the `brackets` style a focused button now fills as well, and the
default is `[Apply]` in the accent. Every dialog puts its action first, and
Enter from each dialog's field does that action, so marking the first
button was enough.

**"Confirm Starts On" is gone.** 0.54 added it to Appearance's Dialog
Buttons, but it only ever affected the Yes/No question boxes -- "Quit
hibr?", the shortcut question, the unregistered-file prompt -- while its
label read as though it covered dialogs in general. Question boxes start on
Yes, as they did before 0.54, and are now marked like any other default. A
saved `DT_CONFIRMDEF` line is left as a harmless assignment, and the next
save drops it.

**Every desktop function but one is reached by a test.** The census went
from 73 functions no suite called to 1:

- **Dead code removed:** `dt_put`, `dt_iconplace`, `files_top` and
  `files_end`, which nothing called and no key or menu reached. `dt_put` also
  added a frame offset that contradicted the rule that an app draws in its
  window's own coordinates.
- **Every dropdown in every Control Panel pane**, walked by one data-driven
  test: open it, take another value, check it changed. The rows come from
  each pane's own `_rows` and `_drop`, so a new dropdown is tested without
  being named. That is 21 checks, and 30 callbacks nothing else reached.
- **The Window menu's own** Zoom, Hide and Close, from the menu bar and from
  a title bar. **The desktop menu's** Change Wallpaper, shift-click ranges
  of desktop icons, and the **Control Strip's** Theme and Wallpaper modules.
- **Apps:** the calculator's Use Answer, and Note Pad joining lines. From
  Files: hidden files, File > Home, and Get Info applying a new name. A
  picture dragged from Files onto Image Viewer, closing Notifications and
  Image Viewer, a terminal's own right-click menu, and Task Manager's View
  Details from both its menus.
- **Date & Time's dialogs through to their OK**, with nothing able to
  change: the one function that runs sudo is replaced by a stub that only
  records what it was asked, and a fake `sudo` first on `PATH` writes a
  marker a check refuses. Time Zone asks to set the zone chosen; Set Date &
  Time and Clock Format open, take a click and close.
- **Task Manager's End Task** is tested directly, not through the window,
  by the new `tests/603-tasks-direct.t` on a `sleep` of the test's own:
  through the window it would end whatever row the machine running the
  suite has selected. The same test covers the `ps` fallback macOS uses.

The one left is `displays_detach`, which needs a held session with a second
client. **96 more parameters are typed** from what these tests passed them:
18 `int` and 78 `int?`, with no literal call site disagreeing.

**The pty harness sees colour.** `tests/screen.py` keeps the pen each cell
was drawn with, and `sc.style(r, c)` gives its colours and boldness, so a
check can say what is highlighted and not only what is written. That is how
the default button is tested, in both styles.

HIBR_VER -> 0.56.

Verified: tests/all.py, all thirteen suites green in 162 seconds;
tests/asan.py, every suite against the sanitizer build with no report;
uifuzz clean on three fresh seeds of 300 events each; the census at 1 of 537.

## 0.55

Desktop moves to **0.33** alongside this release.

**A type can allow empty: `int?`.** A declared parameter's type is checked on
every call, and `int` refuses the empty string, which is not an integer. So
an optional integer could not be typed at all: `int col = ""` failed the
first time the argument was left out. Now a type may end in `?`, and
`int? col = ""` accepts an integer or nothing, and refuses anything else. It
works for every type and for a return type (`-> int?`), and plain `int`
still refuses empty. ADR 0024 records why a mark in the signature won over
not checking defaults. `tests/220-fn.t` covers both forms.

**The desktop's parameters are typed.** The census now records each
argument's shape, and it found 562 parameters that were an integer on every
call the suites made: 528 are now `int` and 34 `int?`, in the 299 functions
the suites call. Before applying them, every literal call site in the
source was checked against the plan, and none disagreed. The 145 functions
no suite calls are left untyped, because the census has nothing to say
about them -- which is 0.44.1's lesson. A wrong argument now fails at the
call, naming the parameter, rather than drawing in the wrong place. Three
fresh fuzz seeds of 300 events each ran clean on top of the suites.

**A type check costs a fifth of what it did.** The check compared the
type's name against every type there is, on every call: 428 instructions per
typed argument. The type is now read once, when the signature is parsed,
into bits on the parameter, and a call only looks at the value: about 100
instructions, most of them the loop over the digits. An unknown type is now
reported once, when the function is defined, not on every call.

HIBR_VER -> 0.55.

Verified: tests/all.py, all thirteen suites green in 163 seconds;
tests/asan.py, every suite against the sanitizer build with no report;
`tests/diff.py` 500 snippets against bash with no difference.

## 0.54

Desktop moves to **0.32** alongside this release.

**Dialog buttons have a group of their own in Appearance.** Under a new
**Dialog Buttons** heading:

- **Style** -- `filled`, as they have been since 0.45 (a block of colour,
  the accent while focused), or `brackets`: `[OK]` on the window's face,
  the focused one in the accent. The width is the same either way, so no
  dialog's layout moves when it changes.
- **Shadow** -- the button shadow, moved here from the Shadows heading.
- **Confirm Starts On** -- `yes`, as before, so enter goes ahead, or `no`,
  so enter is the safe way out of "Quit hibr?" and every other confirm box.
  Every other dialog starts in its text field or list, where enter already
  means the dialog's own action.

They are `DT_DLGBTN` and `DT_CONFIRMDEF` in `wm/settings.hibr`, kept with
the rest of the settings. The Appearance tests find the rows under their new
names, and new checks draw `[Yes]` and `[No]` in brackets and cancel a
confirm box with a bare enter when it starts on no.

**The census records each argument's shape.** Every call it logs now says
whether each argument was an integer, empty or anything else, and
`tests/census.py --types` lists every declared desktop parameter by what the
suites passed it. The first run found 558 parameters that were an integer on
every call, 30 of them optional, and 4 that take an integer or nothing on
purpose. That is the groundwork for typing them, the next release.

HIBR_VER -> 0.54.

Verified: tests/all.py, all thirteen suites green in 163 seconds.

## 0.53

**Under `-S`, what is written inside `${x:+…}` splits and globs.** The word
inside `${x:+word}`, `${x:-word}` and `${x:=word}` is text the author typed.
Until now it was treated as the expansion's result, so under `set -S` and
`strict expansion` it neither split nor globbed. That is what nearly broke
the Files app's hidden-file listing in 0.52. Now it behaves as it would
outside the braces, and only an expansion *inside* it is protected:
`${on:+a b}` is two arguments, `${on:+./.*/}` lists the hidden directories,
and `${on:+$g}` with `g='*.c'` is the single argument `*.c`. ADR 0009 and
`docs/language.md` record the rule with its example run, and
`tests/340-strict.t` checks six forms of it.

**The default mode now agrees with bash on those words too.** Honouring what
was written meant keeping the word's own quoting instead of flattening it to
a string, which is what hibr had done: `${x:-"a b"}` split, `${x:-"*"}`
globbed the directory, and `${x:-"$y"}` split `$y`. A new `xarg` expands the
word with its per-byte quote mask. `tests/602-tilde-operators.t` compares
these with bash.

**A tilde expands where bash expands it.** hibr never expanded one in an
assignment -- `x=~`, `PATH=~/bin:~/x`, `local l=~/l` and `export E=~/b` all
kept the `~` -- nor in `case ~ in` or `${x:-~}`, because the single-word fast
path returned before tilde expansion ran. Now an assignment's value expands
a tilde after the `=` and after each `:`, as bash does, and single words
expand a leading one. A quoted `"${x:-~}"` and arithmetic (`$((~0))`) are
left alone, as in bash. One bash quirk is not copied: bash also expands
`echo a=~`, a command argument that only looks like an assignment.

**Tilde expansion no longer edits the parse tree.** It used to advance the
first part of the word in place and restore it afterwards, and a `$(…)` in
the same word forks in between. The child then ran with that part still
cut short. Once assignments went through the same step, a recursive function
lost the `n=` of its own `local n=$1` inside a substitution, and
`tests/130-local.t` caught it. Each expansion now works on a copy of the
part.

Measured by instruction count against 0.52, with glibc's address-sensitive
`strcmp` taken out: a loop of assignments is 0.56% cheaper, a bare `while`
0.45%, a loop calling a function with two `local`s 0.18%, and a `case` loop
unchanged. The first version cost 3.5%, scanning every assignment for a
tilde character by character; one `memchr` gate brought that down, and
dropping the save and restore of the first part paid for the rest.

HIBR_VER -> 0.53.

Verified: tests/all.py, all thirteen suites green in 162 seconds;
tests/asan.py, every suite against the sanitizer build with no report;
`tests/diff.py` 1,000 snippets against bash with no difference;
`tests/602-tilde-operators.t` compared with bash. `tests/corpus.py` was not
run this time.

## 0.52

Desktop moves to **0.31** alongside this release.

**The whole desktop runs under every strict check.** In 0.51 that was the
window manager and its widgets. Now it is `desktop.hibr` and every app,
game, desk accessory, Control Panel pane and strip module: 33 more files,
each saying `strict`. `tests/540-examples.t` fails if any of them stops
saying it. `session.hibr` stays as it was, because it is the user's own
script.

The method was 0.51's. `tests/census.py --expansion` listed ten places in
the apps where an expansion split: Minesweeper's neighbour lists, the
snake's body, a `/proc/stat` line in Task Manager, a terminal's colours,
the Keyboard pane's app names, the puzzle's four directions, and Files'
selection. Each became `read -ra` into an array. The puzzle's directions
became an array constant, which also takes a `set --` out of a
400-iteration loop.

A search for the forms the suites might not reach found one the probe could
not see. Files lists hidden entries with `${hidden:+"$d"/.*}`, and under
strict expansion the text inside `${…:+…}` is the expansion's result, so it
would have listed a file literally named `.*` instead. It now adds that
glob on a line of its own. `docs/language.md` records the rule with the
example run. Whether `-S` *should* treat that text as written is a real
question about the shell, and it is in `docs/backlog.md` for the owner
rather than decided here. With every site converted and strict switched
off, the probe listed nothing.

**`tests/strictvars.py` finds every global a strict function would create,
by reading.** `strict vars` refuses those at run time, but only on paths
that run, and each refusal stops its path before the next name is reached.
Turning it on for the apps found six in the first round of suites and a
seventh in the second. The checker reads every function in every strict
file and fails on any assignment that is not a parameter, a local, a
file-level name or one of the shell's own: `x=`, `x[k]=`, `x := f`, `read`,
`for`, `printf -v` and `(( ))`. It found the seventh without a suite run,
found nothing else, and is now a suite in `tests/all.py` that takes under a
second. The seven are all out-parameters, where a function hands back a
list or a pair: `DTP_LAT`, `DTP_LON`, `DTP_ROW`, `DTP_COL`, `DTP_WHY` and
`DTZ_HIT` in the Date & Time pane, and `FB_CRUMBS` in Files. Each is now
declared just above the function that fills it.

HIBR_VER -> 0.52.

Verified: tests/all.py, all thirteen suites green in 161 seconds, strictvars
included; tests/asan.py, every suite against the sanitizer build with no
report; `tests/census.py --expansion` listing nothing in the apps with strict
switched off; uifuzz clean on three fresh seeds of 300 events each.

## 0.51

Desktop moves to **0.30** alongside this release.

**The window manager and its widgets run under every strict check.** Since
0.49 all 27 files in `wm/` and `widgets/` said `strict functions vars`. Now
they say `strict`, and `strict expansion` is on as well, so an expansion
there never splits or globs.

Getting there took evidence rather than reading. The census build
(`make census`) gained a probe for the places strict expansion would change.
It logs an unquoted expansion that splits, and a value holding `*`, `?`,
`[` or `\` wherever it could glob or match as a pattern, with its file and
line. `python3 tests/census.py --expansion` runs the suites under that
build and lists the sites. The first run listed 20. Ten were a value with a
`?` in it being assigned or used as a `case` word, which neither splits nor
globs, so the probe now looks at each word's expansion flags and stays
quiet there. The other ten, plus three more loops found by searching for
the forms the suites might not reach, were lists the window manager splits
on purpose: the pane list, a selection of icons, the window ids, the
confirm box's geometry, a mouse report. Each now goes through `read -ra`
into an array, or `read -r` for a fixed tuple. With those converted and
strict still off, the probe listed nothing. Then `strict` went on, and
`tests/540-examples.t` now fails if any file in `wm/` or `widgets/` stops
saying it.

The probe also found something the documentation had never said: under
`set -S`, a pattern that comes from a variable (`case $x in $p)`,
`[[ $x == $p ]]`, `${x#$p}`) matches only itself, as a quoted `"$p"` does.
`docs/language.md` and ADR 0009 now say so, with the example run. The window
manager had no such pattern anywhere.

**`$LINENO` is right in a `for`, `case`, `if`, loop, `(( ))` or `[[ ]]`.**
Only simple commands set the current line, so `for i in $LINENO` and
`[[ $LINENO == 5 ]]` read the line of whatever command ran before. A strict
check refusing a loop variable therefore named the line that called the
function rather than the loop. `tests/600-strict.expected` moves by exactly
that one line, and a new `tests/601-lineno.t` compares these cases with
bash. It costs one store per compound command: 6 instructions, 0.04% of a
`case` loop. The first measurement said 0.33% on a loop that never reached
the change, and all of that was glibc's `strcmp` running different paths for
the same calls once the binary's strings moved.

HIBR_VER -> 0.51.

Verified: tests/all.py, all twelve suites green in 162 seconds with the window
manager fully strict; tests/asan.py, every suite against the sanitizer build
with no report; `tests/census.py --expansion` listing nothing in `wm/` or
`widgets/` before strict went on.

## 0.50

Desktop moves to **0.29** alongside this release.

**`x := rsub ...` binds its result instead of printing it.** `rsub` wrote
its answer to standard output unless given a variable to put it in, and
never looked at the result slot, so `x := rsub ...` printed the text and
left `x` empty -- `:=` was silently a no-op for it, as it is for a program
on the PATH. It now answers the way `str` does: into a named variable, into
the slot under `:=`, and to standard output otherwise, with `$RET` set in
every case. `tests/210-regex.t` records both a bound match and a bound miss.

**Four testing tools, from asking how the suites could be quicker and worth
more.** All four are described in `tests/README.md`.

- **`tests/affected.py`** names the suites a change reaches, from `git diff`
  or from paths given: `--run` runs only those, and `--why` says which path
  picked each suite. What a module reaches is read from the source, not
  kept in a list: every module's `hibr_require` calls, resolved through the
  others' `hibr_provide`. So a change to `mods/cat` reaches `hvi` and both
  desktop suites, because hvi asks for the `highlight` interface and cat
  provides it. A path it does not recognise reaches every suite.
- **`tests/census.py`** reports which desktop functions no suite calls.
  `make census` builds the shell a second time with `-DHIBR_CENSUS`, and
  that build logs every function call to `$HIBR_CENSUS`. The real shell
  compiles the logging out and pays nothing. It lists what was never called,
  file by file, along with any call whose arguments did not bind, and keeps
  the count so the next run can say which way it moved. The first run found
  79 of 540 never called; with the fuzzer below added to the
  runs, the count is 71.
- **`tests/uifuzz.py`** sends random keys, clicks, drags and wheel turns
  into one app's window. It then checks that the desktop is still running,
  still answering, and has printed nothing. Runs are seeded, and a failure
  prints the command that replays it. It runs as a suite in `tests/all.py`
  at a fixed seed; `SEED=random` goes looking somewhere new. It found the
  bug below on its first run. Task Manager, Files, Terminal and the Date &
  Time pane are left out on purpose: they signal processes, move files, run
  a shell and run sudo, on whatever machine is running the tests.
- **`tests/asan.py`** runs every suite against `make asan`: the shell *and
  every module* built with AddressSanitizer and UBSan into `build/asan`.
  Until now the console, term, pty, hold and img modules ran end to end only
  as the tcc build. Sanitizer reports go to files rather than to stderr, so
  a report from a desktop, whose stderr is its log, or from a forked child
  is still found, and any report fails the run. `HIBR_TESTMODS` points the
  pty harness at another module directory, and `HIBR_TESTLOGS` gives
  `tests/all.py` another log directory.

**A terminal window whose program had exited held a whole core.** Once
the program inside a terminal window exits, the pty's master side reads as
ready for ever. The desktop watches that descriptor so a program's output
wakes it at once, and a window that closes on a clean exit stops watching.
A window left open to show a non-zero status did not, so the desktop woke,
drew and woke again as fast as it could: 1,836 frames in six seconds, 100%
of a core, until the window was closed. The window now stops watching the
descriptor the moment it sees the program has gone.

The ASan run found this: the terminal's shell exited there for reasons of
its own, and the idle check failed. The ordinary suite never could. That
check counted voluntary context switches, and a loop that never blocks
makes none, so a spinning desktop measured as perfectly asleep. The idle
checks now read the CPU time the desktop used as well, and a new one opens
a terminal on `false` and requires it to stay under 5% of a core.

**The detach shortcut printed a `hold` error on a desktop that is not
held.** The Detach menu item is dimmed when there is no session to detach
from, but its key, `ctrl-\` by default, called `hold detach` anyway, and
hold complained into the log. The key now says "Not held -- nothing to
detach from", the same thing the dimmed item means.

HIBR_VER -> 0.50.

`tests/mon.py`'s "its pids are real processes" asked this of the three
heaviest rows, which under a parallel sanitizer run are the other suites'
children, gone before `/proc` was read. It now asks it of most of the
table.

Verified: tests/all.py, all twelve suites green in 162 seconds, uifuzz
included; tests/asan.py, every suite against the sanitizer shell and
modules with no sanitizer report; the exited-terminal check fails at 100%
of a core without the fix and passes with it.

## 0.49.1

**On macOS, `match` and `rsub` read and wrote the wrong memory -- and closing
a window froze the desktop.** hibr declares the C library's regex types
itself, because tcc cannot read glibc's `<regex.h>`, and it declared a match
offset as a 32-bit `int`, which is glibc's `regoff_t`. On macOS and the BSDs
it is 64 bits. So on a Mac every capture the library wrote overran the array
hibr gave it, and hibr read each offset from the wrong half: a `match` with
captures could loop for ever and an `rsub -g` could crash. The desktop draws
a pressed close button in a lighter shade of its red, worked out with
`match` -- which is why closing any window hung it, reported as "if I close
any window on Mac, everything gets stuck". Reproduced on Linux by declaring
the Mac's width there: `match` hung and `rsub -g` segfaulted.

`include/re.h` now uses each C library's own width -- 64 bits on macOS and
the BSDs, `int` on glibc, `long` on musl -- and a new `src/recheck.c`, compiled
by every compiler that can read the real `<regex.h>` (gcc here, clang on a
Mac; not tcc), asserts that hibr's declarations match it: the offset, the
match record, room for a compiled pattern, and the flag values. Getting any
of them wrong now stops the build instead of corrupting a running shell.

Found from a log sent from the Mac, which ended mid-frame, just after the
title bar's button colours were drawn.

HIBR_VER -> 0.49.1.

Verified: tests/all.py, every suite green in 153 seconds; tests/run.sh under
ASan and UBSan 93/93; the check fails the build when the width is forced
wrong.

## 0.49

Desktop moves to **0.28** alongside this release. **The module ABI is now
15**: a module built for 14 is refused until it is rebuilt. Every bundled
module is rebuilt with the shell.

**`strict`: a file can ask to be refused what is usually a mistake.** Perl's
`use strict`, as three named checks, for the file that runs it:

- **`strict functions`** -- defining a function a second time in the same
  file is refused, naming both lines:
  `wm/menus.hibr:313: dt_drop is already defined at line 98`. The first stays.
  Another file may still replace it, which is how a file of your own
  overrides a bundled one.
- **`strict vars`** -- a function creating a global because it forgot
  `local` is refused: `total is not declared -- local total, or declare -g
  total`. Assigning something that already exists, a local of its own or of
  a caller, or anything made with `local`, `declare` or `declare -g`, is not.
- **`strict expansion`** -- `set -S`, for this file alone.

`strict` alone is all three; `strict off vars` puts one back and `strict -p`
lists what is on. "The file" is `$BASH_SOURCE`, so a function is checked by
the file that defined it, wherever it is called from. A refused command fails
-- so `set -e` stops -- and a script that never says `strict` pays 0.25% on a
loop, measured. The design and what it deliberately does not catch are in
`docs/adr/0023`.

**`$LINENO`**, which hibr never had: the running command's line, counted in
its own file -- a function's in the file that defined it -- the same as bash
on every case compared. It is also what lets every strict message name a
line, and why the ABI moved: a node now carries the line it was parsed from.

**The window manager and the widgets run under `strict functions vars`.**
What it found:

- `dt_appmenu` read each app's kind into a global `kind` it never declared.
- Shared state -- the pending shortcut being reassigned, the wallpaper's
  geometry, the menu bar's last hit, the button shadow's cache, Quit's
  button focus -- was created from inside functions; each is declared at the
  top of its file now, where a reader can see it.

**Found on the way, and fixed:**

- **alt-c, alt-x and alt-v were never reserved.** `DT_KEYHELD[alt-c]`, a key
  with a dash in it and no quotes, is arithmetic: `alt` minus `c`, key 0. All
  three were one entry named 0, so Keyboard would let any of them be taken;
  capturing ctrl-\ also printed an arithmetic error. Every subscript in the
  shortcut code is quoted now, and a test takes alt-c.
- **The suites were blind to errors in a running desktop.** It sends its
  stderr to `desktop.log` so an error cannot draw over the screen, and 0.48's
  check for shell errors read only the terminal -- so its "none found in the
  desktop suites" was wrong. Every session's `desktop.log` is read too now.
- **Task Manager logged an error each time a process ended mid-scan**, from a
  redirection written in the order that lets the error escape.
- **The suites use this build's modules**, not whatever is installed:
  `tests/screen.py` puts `build/mods` first on `HIBR_MODPATH`.

HIBR_VER -> 0.49, HIBR_ABI -> 15, DT_VER -> 0.28.

Verified: tests/all.py -- run.sh 93/93 (and under ASan and UBSan, leak-free),
desktop.py 299/299, apps.py 272/272, most 23/23, hvi 32/32, console 61/61,
cat 29/29, mon 16/16, mtr 12/12, editor 11/11, term_diff 66/66 -- with
desktop.log read for errors; tests/fuzz.py 500 rounds under ASan, no crash
or hang.

## 0.48

Desktop moves to **0.27** alongside this release.

**The whole test run takes two and a half minutes, not thirty-five.** The
full-screen suites spent 99% of their time asleep -- `most.py` did 0.2
seconds of work in 35 -- because every key waited a fixed 0.45s, every
session a fixed start, and every quit a full 1.2s whether or not the program
had already gone. Now:

- **The desktop says when it is ready.** Run with `HIBR_TESTIDLE`, it prints
  an escape a terminal ignores each time a frame is on screen and it is about
  to wait, carrying how many bytes of input it has read -- `console consumed`,
  new -- so the harness waits for the frame that includes its key and no
  longer, and a frame a timer asked for is never mistaken for it.
  `desktop.py` went from about 15 minutes to 150 seconds, `apps.py` from
  about 10 to 75.
- **A session ends when its program does**, not after a fixed wait.
- **Every suite runs at once**, one per core: `tests/all.py`, or
  `make check-all`, with a line per suite and full logs in `build/test-logs`.

Speeding it up exposed two tests that had only passed because they were
slow: a snake and a brick game were being given time by pressing a harmless
key several times (a pause is now written as a pause), and a terminal
window's shell could be sent ctrl-c before it had set its trap (a terminal
test now gives its program a moment to start). And running side by side
explained `term_diff`'s one odd run in 0.46: every case started a tmux
server on the same socket and killed it, and a kill returns before the
server has gone, so under load the next case's start failed now and then.
Each case has a socket of its own now, and removes it after.

**A shell error printed anywhere fails the suite.** A too-many-arguments or a
command-not-found in a desktop session used to scroll past and leave the
test green if the screen still looked right -- the kind of error 0.44.1 was
made of. Every session is now scanned, and a suite fails on any shell error
nothing asked for; a test that provokes one on purpose declares it beside
itself. None was found in the desktop suites.

Four more ideas are in `docs/backlog.md` under Testing: running only the
suites a change touches, a standing report of desktop functions no suite
calls, fuzzing each app, and the pty suites under the sanitizers.

HIBR_VER -> 0.48, DT_VER -> 0.27.

Verified: tests/all.py, four full runs green after the last change -- run.sh
92/92, desktop.py 299/299, apps.py 271/271, most 23/23, hvi 32/32, console
61/61, cat 29/29, mon 16/16, mtr 12/12, editor 11/11, term_diff 66/66 -- in
153 seconds each.

## 0.47

**A command in a shell with many functions costs what one in a shell with
few does.** Every simple command asks first whether its name is a function,
and a miss -- every builtin, every program -- compared it against every
function defined. With the desktop's own 201 loaded that was about 11,600
instructions per command. From 16 functions on, the lookup goes through an
index now, and the same loop over builtins, with the desktop loaded, takes
418M instructions instead of 878M: 2.2 times faster per command. Loading the
desktop itself is 3.6% cheaper, since each definition used to scan for an
earlier one of the same name. The cost is one comparison per lookup below 16
functions -- 0.17% on a loop with none at all, measured against the same
build without it.

**`.` and `source` look a bare name up on `PATH`**, as bash does: the first
readable file there, executable or not, then the current directory. hibr's
option table had always said `sourcepath` was on, and it was not; now it is.
A folder of files that only define functions, put on `PATH`, is a library --
`. mylib` -- with `$BASH_SOURCE` the path it was found at.

**A command that is not found says so through its own redirections.**
`cmd 2>/dev/null || fallback` printed "command not found" regardless, which
bash does not; found while testing the lookup above.

HIBR_VER -> 0.47. The desktop is unchanged.

Verified: tests/run.sh 92/92 (and under ASan and UBSan), tests/apps.py
270/270, tests/desktop.py 298/298.

## 0.46

Desktop moves to **0.26** alongside this release.

**The Control Panel is organised the way a person looks for things.** The
picker lists two groups under headings: **System** -- Appearance, Control
Strip, Date & Time, Desktop, Displays, File Types, Keyboard, Notifications,
Windows -- and **Apps**, one pane per app with something to choose: About
hibr, Files, Task Manager, Terminal. Behaviour, which had become a drawer of
everything, is gone:

- Appearance has the theme, the wallpaper glyph, **Wallpaper Image…** -- the
  image picker, a window of its own now with Apply and Close buttons -- a new
  **Wallpaper Mode** row, the menu bar's spacing and icon, and the four
  shadows (windows, menus, menu bar, buttons) under a Shadows heading.
- **Desktop** has the icons and Redraw Skip.
- **Windows** is what Window Style was, plus Titlebar Click.
- **About hibr** has its refresh; **Files** its default view and Reset All
  Views; **Terminal** gains Cursor and Cursor Blink.

An app's pane is always listed, and says so when its app is not loaded,
rather than going blank or moving the list about. And there is a rule now,
with a test behind it: **everything an app keeps across restarts has a row
in some pane.** `tests/540-examples.t` reads every `dt_keep` and the
desktop's own list, and fails on one no pane shows -- which found the
wallpaper mode on its first run, settable only inside the picker until now.
Three things kept but not chosen (where the Control Strip sits, how long it
is, whether it is folded) are named there with the reason.

**Keyboard** replaces Shortcuts and App Shortcuts: one list, the desktop's
actions then every app's, and the Control Strip's key, which no pane had
shown.

- **A ✕ clears a shortcut**, beside every key that is set, and delete or
  backspace clears the selected row. A cleared shortcut is saved as cleared,
  so one that ships with a default does not come back at the next start.
- **A key never has two owners.** Pressing one another action holds asks --
  "ctrl-\\ is Detach's: give it to Control Panel?" -- and Yes moves it.
- **Keys the desktop keeps are refused with the reason**: f10 and escape
  open the menu bar, alt-c, alt-x and alt-v are Copy, Cut and Paste.
- A duplicate that only a settings file edited by hand could make is marked
  ⚠ on both rows.

The shortcut handling is a part of the window manager of its own,
`wm/keys.hibr`.

**The full-screen test suites count what they check.** `report(N)` used to
print N minus the failures, however many checks had actually run, so a
check silently skipped still counted as passed. `tests/screen.py` counts
every check made now, and a suite whose count differs from its plan fails
and says so. Its first run found five plans wrong: `console.py`'s was one
too high and `cat.py`, `most.py`, `hvi.py` and `editor.py` were each one too
low. Every check in them is unconditional, so none of the five had been
hiding a skipped check -- the plans had drifted as checks came and went --
and each is now the count the suite makes.

HIBR_VER -> 0.46, DT_VER -> 0.26.

Verified: tests/run.sh 90/90, tests/apps.py 270/270, tests/desktop.py
298/298, tests/console.py 60/60, tests/cat.py 28/28, tests/most.py 22/22,
tests/hvi.py 31/31, tests/editor.py 10/10, tests/mon.py 15/15, tests/mtr.py
11/11, tests/term_diff.py 66/66 -- each count now the checks actually made.
(A term_diff run that overlapped another one, both driving tmux, stopped
at 62 partway through its own checks; alone it makes all 66 in 12 seconds.)
tests/750-pty.t failed once in the full run and passed 23 times alone
since; nothing in this release touches the pty module, so it is recorded
here as intermittent rather than as fixed.

## 0.45

Desktop moves to **0.25** alongside this release.

**Dialogs have buttons.** Quit's confirm box, Rename, Get Info, File Type,
Clock Format, Time Zone and Set Date & Time each end in a row of real
buttons -- Yes and No; Rename and Cancel; Apply and Close; Save, Delete and
Cancel; Set (sudo) and Cancel -- in place of a grey line saying which keys
did what. A button is filled: the accent colour while it has focus, muted
while it has not, with Turbo Vision's shadow, a `▄` beside it and a row of
`▀` under it. Tab and shift-tab move through a dialog's fields and then its
buttons; the arrows move between the buttons once one has focus, and leave a
text field its cursor keys and a list its scrolling until then; enter does
whatever has focus, or the dialog's own action from a field; escape cancels;
a click does the button it lands on. y and n still answer Quit at once. File
Type's Delete is a button now, where before only a hint said ctrl-d.

The shadow is its own setting, **Button Shadow** in Behaviour, beside the
window, menu and bar ones. Its colour is the face darkened by the theme's
own shadow depth -- twice over, since a thin row of half blocks at a
window's depth vanished into midnight's near-black face -- and comes from a
new `console shade colour [pct]`, which answers the colour `console darken`
would produce, so there is one formula for both.

The buttons are a widget, `widgets/button.hibr`: `dt_button` and
`dt_buttons` draw one or a row, `dt_dlgbtns` centres a dialog's along its
bottom, and `dt_focus` and `dt_focuskey` move focus. A button can be drawn
into a window's pane or, as the confirm box's are, onto the screen itself,
and `dt_hit` finds it either way.

**Four themes**: **black** (pure black, near-black windows, soft grey text,
one cool accent), **neon** (synthwave -- purple-black, electric-cyan text,
hot-magenta accent), **phosphor** (a green monochrome CRT) and **amber** (the
amber one), each one hue family throughout. Every button reads at 5.5:1 or
better on all four; paper's unfocused one is 3.3:1, its muted colour being
a mid grey, and is the one theme where it falls short. Black's shadows cannot show -- nothing darkens black --
which is the look it is for. A terminal window takes each theme's text and
background, not yet its sixteen program colours.

HIBR_VER -> 0.45, DT_VER -> 0.25.

Verified: tests/run.sh 90/90 (and under ASan and UBSan), tests/console.py
61/61, tests/apps.py 246/246, tests/desktop.py 298/298.

## 0.44.2

Desktop moves to **0.24.2**. A second fix to 0.44.

**Set Date & Time set the clock to nothing.** The date reaches the command
that sets it through a small `sh -c '...'` script run under sudo, which reads
it as its own `$1`. The 0.44 conversion rewrote positionals to parameter
names everywhere outside single quotes -- and judged quoting a line at a
time, so on the script's second line, still inside the quote opened on the
first, `$1` looked like the function's own and became `$id`. It is `$1`
again. Those two were the only positionals anywhere in the desktop inside a
single-quoted string that spans lines, checked with the quoting followed
across lines this time; there are no heredocs to check.

`tests/apps.py` now runs that script for real against a `timedatectl` that
only reports what it was asked -- with sudo taken off the front, so nothing
on the machine running it changes -- and fails on 0.44.1.

`tests/desktop.py` counted Files windows by their whole title, `Files
[~/hibr]`, which is only that when the checkout lives at `~/hibr`; run from
anywhere else, one check failed. It counts by the start of the title now.

HIBR_VER -> 0.44.2, DT_VER -> 0.24.2.

Verified: tests/run.sh 90/90, tests/apps.py 235/235, tests/desktop.py 294/294.

## 0.44.1

Desktop moves to **0.24.1**. A fix to 0.44, which it should not have needed.

**Thirteen callbacks refused what the window manager hands them.** 0.44
wrote each function's parameters from what the test suites showed it being
called with, and for a callback the suites never reach, that was only the
names its old `local` line gave -- three for a `_click` the window manager
calls with four, two for a `_wheel` it calls with four. A declared function
refuses an argument it has no name for, so the call failed and the body
never ran: clicking in Rename, File Type, Time Zone or Set Date & Time did
nothing, nor did the wheel over Task Manager or the notification history,
nor dropping a file on the image viewer, and Process Details and Set Date &
Time drew nothing. Each now names its whole contract, the extra names
optional so a direct call with fewer still works, and a drop takes its paths
as `...paths` instead of shifting past four arguments to reach them.

`tests/540-examples.t` now checks every callback against the arguments it
is called with -- read from the desktop's own `dt_app`, `dt_new`, `cp_pane`
and `cs_module` calls, so a new app is covered without being listed -- and
it names all thirteen when run against 0.44. `tests/apps.py` calls
`tasks_wheel` the way the window manager does as well as the way the test
used to, which is how the census had seen two arguments where there are
four. `dt_confirm_key`'s parameter is a key, not an `id`, which the
conversion had guessed from its suffix.

HIBR_VER -> 0.44.1, DT_VER -> 0.24.1.

Verified: tests/run.sh 90/90, tests/desktop.py 294/294, tests/apps.py 234/234.

## 0.44

Desktop moves to **0.24** alongside this release.

**Every desktop function that takes arguments declares them.** 400 of them,
across the window manager, the widgets, every app, pane, desk accessory and
Control Strip module, went from `name() { local id=$1 h=$2 ...` to
`fn name(id, h, w, row, col) {`. The signature is now the first thing a
reader sees; a callback's says exactly what the window manager hands it,
and `ARCHITECTURE.md` lists every one. Three things came with it:

- **Cheaper calls.** Binding a declared parameter is done in C, where the
  `local` line it replaces was an expansion and an assignment per word:
  526M instructions against 711M for 20,000 calls of four arguments. Loading
  the whole desktop costs 0.25% fewer.
- **The `local` trap is closed.** A parameter is bound before the body
  runs, so `local b=${M[$id]}` can see `id`. `About hibr`'s bar width read
  `w` in the same `local` statement that assigned it, and was wrong until
  now; nothing had caught it because `w` was bare inside arithmetic.
- **A wrong number of arguments fails the call** rather than running with a
  missing value. So that the conversion changed nothing else, every call's
  argument count was recorded across the suites first, and the signatures
  were written from what was seen: a parameter the suites never showed being
  passed is optional (`= ""`), and a function handed more than it names
  takes `...rest`. The 71 functions the suites never call have every
  parameter optional. With the conversion applied the suites pass unchanged
  and no call anywhere in them failed to bind.

Types are not declared yet -- an `int` would refuse the empty window id the
Control Strip passes on purpose -- and are in `docs/backlog.md` as the
second pass. The undeclared form still works, for an app of your own.

`tests/540-examples.t` had not been parsing `apps/` or checking
`apps/Games/` for a function defined twice; it does both now, and knows
both ways of defining one.

HIBR_VER -> 0.44, DT_VER -> 0.24.

Verified: tests/run.sh 90/90 (and under ASan and UBSan), tests/desktop.py
294/294, tests/apps.py 233/233, tests/term_diff.py 66/66 -- the last three
unchanged by the conversion, and under a build that logged every failed
bind, none across 419,306 calls.

## 0.43

Desktop moves to **0.23** alongside this release.

**`desktop.hibr` is a table of contents now.** Five thousand lines became a
page that asks for a display and sources the window manager's parts, one
concern to a file, from `examples/desktop/wm/` -- state, settings, the
session, windows, frames, drawing, wallpaper, input, menus, the bar,
context menus, apps, files, handlers, icons, hold, notifications, confirm,
the Control Strip -- and the widget library from `examples/desktop/widgets/`:
hit regions, checkbox, dropdown, slider, text field, scrollbar. Each part
owns its own state and settings, and has a header saying what it holds;
`wm/README.md` and `widgets/README.md` map them, and the latter sets out
what makes a file a library, since hibr has no separate notion of one. The
split moved text and rewrote none: loaded, the old file and the new parts
leave the same 185 function definitions, byte for byte, and the same
variables and maps behind. `tests/540-examples.t` now fails on a function
defined twice across all the parts together, not only within one file.

Three things in the shell itself made that possible, each checked against
bash:

- **`BASH_SOURCE`**: the file the running code came from -- the script, a
  file being sourced, or, inside a function, the file the function was
  defined in, wherever it is called from -- which is how `desktop.hibr`
  finds its own directory however a session reached it. A function carries
  its file from definition; a call costs 0.55% more instructions on a loop
  that does nothing but call a function, and nothing on one that does not.
- **`declare -f` and `declare -F`**, which bash scripts use to print and list
  functions: `-F` exactly as bash does, `-f` printing a definition as it was
  written, which reads back in as the same function.
- **`[0]` of a computed name** (`${RANDOM[0]}`, `${SECONDS[0]}`) reads its
  value, as `[0]` of any scalar does; and an assignment to `EPOCHSECONDS` or
  `EPOCHREALTIME` is ignored, as in bash, rather than hiding the clock.
- **`HIBR`**: this shell's own absolute path, asked of the system rather
  than guessed from the name it was started under, as bash's `BASH` is. A
  held desktop restarts itself under `hold new`, and it used to do so as
  plain `hibr`, which is whichever one `PATH` finds first -- so a desktop
  run from a build directory came back up under the installed shell. Until
  now the two could run the same file and nobody noticed; with the desktop
  asking for `BASH_SOURCE`, an older installed shell found none of its parts
  and the held session ended at once. It restarts under `"$HIBR"` now, and
  `desktop.hibr` run by a shell without `BASH_SOURCE` says which version it
  needs instead of reporting twenty missing files.

`docs/language.md` had still described `RANDOM=5` as reading 5 for ever
after; that stopped being true in 0.42 and it now says so. Three tickets
go into `docs/backlog.md` under "The language": `.` searching `PATH` for a
bare name, as bash's does -- which is all a library would need; looking a
function up without reading every name, since every command in the
desktop pays for its four hundred; and a per-file strict mode. All three
are agreed, and follow the dialog buttons and the Control Panel.

HIBR_VER -> 0.43, DT_VER -> 0.23.

Verified: tests/run.sh 90/90 (and under ASan and UBSan), tests/desktop.py
294/294, tests/apps.py 233/233, tests/term_diff.py 66/66.

## 0.42

Desktop moves to **0.22** alongside this release.

**Files sorts by a heading in the details view**, the way Task Manager does:
click Name, Size, Modified or Mode to sort by it, and again to turn it
round, ▲ or ▼ on the heading. `..` stays first and folders stay ahead of
files; names sort without regard to case, sizes largest first, times newest
first -- the one `ls -l` the view already makes now runs with `-t`, and the
order of its lines is each entry's age. The selection and any marks stay on
their entries. Until a heading is clicked the order is the one it always
was.

**A pressed title-bar button changes colour, not shape.** It used to be
drawn inverted, a block of its own colour behind the glyph. Now only the
glyph changes: a grey button takes the theme's accent, and a coloured one --
close's red, the traffic lights -- a lighter version of its own colour.

Ties in both lists now keep their order when a number column is sorted
descending: the key turns round rather than the list, so equal sizes and
equal CPU still read A to Z and by pid.

**`RANDOM=n` seeds the generator, and `SECONDS=n` restarts the count**, as
in bash. Both used to become plain variables the moment they were assigned,
reading the same value for ever after -- found because a Minesweeper test
needed a seeded layout: it failed one run in 72, when the one safe cell
among 72 happened to be the corner it opened first and won the game before
a mine could be hit. The test is seeded now. `tests/585-dynamic-vars.t`
checks both against bash.

HIBR_VER -> 0.42, DT_VER -> 0.22.

Verified: tests/run.sh 87/87 (and under ASan and UBSan), tests/desktop.py
294/294, tests/apps.py 233/233.

## 0.41

Desktop moves to **0.21** alongside this release.

**Task Manager sorts by any column, either way.** Click a heading -- PID,
Name, Owner, CPU% or Mem -- to sort by it, and click it again to turn the
order round; the sorted heading carries ▲ or ▼. Numbers start largest first
and names at A, case aside. The sort now happens in C (`arr sort`) over one
padded key per row, where an insertion sort in the shell used to take one
comparison at a time and would have crawled the moment a click reversed a
few hundred processes.

**The wheel scrolls the list** three rows at a time, leaving the selection
where it is, and **a scrollbar** in the list's right margin shows where the
view is; a click on its track jumps there.

**Task Manager has its own Control Panel pane**: Refresh (moved from
Behaviour), Scrollbar on or off, and Sort By and Order, the order a new
window starts in.

HIBR_VER -> 0.41, DT_VER -> 0.21.

Verified: tests/run.sh 86/86, tests/desktop.py 294/294, tests/apps.py
230/230.

## 0.40

Desktop moves to **0.20** alongside this release.

**The notification icon is a choice**, in Appearance: flag ⚑ (the default),
bell 🔔, note ♪, mail ✉, dot ●, diamond ◆ or star ✱, with the unseen count
beside whichever it is. The bell 0.39 introduced is an emoji: a terminal
without an emoji font -- the Linux console, many older setups -- draws it as
an empty box, and one with an older width table counts it as one cell and
pushes the clock along. The flag is not an emoji, so every monospace font
has it, one cell wide; the bell is still there for a terminal that shows it.

HIBR_VER -> 0.40, DT_VER -> 0.20.

Verified: tests/run.sh 86/86, tests/desktop.py 294/294, tests/apps.py
218/218.

## 0.39

Desktop moves to **0.19** alongside this release; the `term` module to 0.24.

**Terminal windows speak UTF-8 to their programs.** The `?` and the `?` in
a diamond left in terminal windows came from programs running in the C
locale -- inherited from a desktop started without a UTF-8 one, and on a
machine that has only `C.utf8` installed, a `LANG=en_GB.UTF-8` is the C
locale too, since a locale that cannot load leaves every category in C.
screen, ls and ncurses programs print `?` for what they believe cannot be
shown, and raw bytes for the rest, which a UTF-8 terminal shows as U+FFFD.
A program started in a terminal window now gets a UTF-8 `LC_CTYPE` when the
locale it would inherit is not one, checked by asking the C library rather
than reading the name; language, sorting and dates stay as they were.

**The notification bell is a bell**, 🔔, with the number of notes not yet
seen beside it (9+ past nine) and nothing beside it once the history has
been opened. It replaces the dot, and the circle before that.

`tests/screen.py`'s terminal model now lets a wide character cover the cell
after it, as a real terminal does; it had left that cell's old character
showing wherever a damage-based redraw moved a wide glyph along.

HIBR_VER -> 0.39, DT_VER -> 0.19.

Verified: tests/run.sh 86/86, tests/term_diff.py 66/66, tests/desktop.py
292/292, tests/apps.py 216/216, and the console, most, hvi, mon, cat and
editor pty suites.

## 0.38

Desktop moves to **0.18** alongside this release.

**Terminal windows wear the theme.** Their default text and background are
now the theme's own ink and face, like every other window, and a program
that asks what its background is (OSC 10/11) is told -- which is how Claude
Code, vim and others choose a light or dark scheme for themselves. Control
Panel's Terminal pane has a Colours setting: `theme`, the default, or
`terminal`, which leaves them to the real terminal the desktop runs in; that
one hibr cannot see, so the question goes unanswered rather than guessed. A
theme change reaches every open terminal window on its next frame.

**Menu Bar Spacing is a slider** in Appearance, 1 to 4 cells between the
notification dot, the clock and the application menu; 2 by default.

HIBR_VER -> 0.38, DT_VER -> 0.18.

Seven tickets recorded in `docs/backlog.md` under "For language models":
a reference written for a model to read, an agent mode, a linter for the
mistakes models make, a dry run and a policy for commands an agent runs,
an MCP server, staying a drop-in for bash, and being where models look.

Verified: tests/run.sh 86/86, tests/term_diff.py 64/64, tests/desktop.py
292/292, tests/apps.py 216/216.

## 0.37

Desktop moves to **0.17** alongside this release.

**The right-hand side of the menu bar is spaced like the left.** The menu
titles on the left sit two cells apart, from the space either side of each
name; the notification dot, the clock and the application menu on the right
were one cell apart and read as crowded. They are two apart now, and the
clicks follow them, whatever clock format is chosen.

HIBR_VER -> 0.37, DT_VER -> 0.17.

Verified: tests/run.sh 86/86, tests/desktop.py 292/292, tests/apps.py
212/212.

## 0.36

Desktop moves to **0.16** alongside this release; the `term` module to 0.23.

**The terminal emulator is rebuilt around a real VT parser.** Programs
built on ncurses and modern TUIs such as Claude Code, screen, htop and nano
drew stuck underlines, jumped text about and swapped letters in a hibr
terminal window. Every cause was found by comparing the emulator against
tmux on the same bytes, not by guessing:

- The parser threw away a sequence's `?`, `>` and `<` prefix, so a
  keyboard option (`CSI > 4 ; 2 m`) became "underline on" and the kitty
  keyboard queries (`CSI ? u`, `CSI > 1 u`) became "restore cursor".
  `vt.c` is now Paul Williams' DEC state machine, which dispatches on
  prefix, intermediates and final together, runs C0 controls inside
  sequences, lets CAN and SUB abandon one, and consumes DCS, OSC, APC, PM
  and SOS strings whole, however they are split across reads, instead of
  printing them.
- Combining marks, joiners and variation selectors took a column of their
  own; they now join the character before them. A wide character is kept
  whole, and writing over either half blanks the other.
- Insert mode, REP, CHT/CBT/HTS/TBC tab stops, origin mode, margins for
  cursor movement, DEC line drawing (`ESC ( 0`, SO/SI), full DECSC/DECRC
  with one slot per screen, 47/1047/1048/1049 each done properly, soft and
  hard reset, application cursor keys, focus reports, synchronized output,
  conceal, colon sub-parameters, underline colour, and replies to DA2,
  XTVERSION, DECRQM, DECRQSS, XTGETTCAP and the window size.
- The program is given `TERM=xterm-256color` and `COLORTERM=truecolor`,
  what this emulator implements, instead of inheriting whatever the desktop
  itself runs in.

**`tests/term_diff.py`** makes this permanent: 62 checks feeding the same
bytes to the emulator and to a private tmux server, cell by cell, over
short sequences and the recorded output of less, nano, vi, screen and
whiptail. Where xterm and tmux disagree, hibr follows xterm and the test
says why. The emulator was also run through 700 random byte streams under
ASan and UBSan with leak detection, and stayed clean.

HIBR_VER -> 0.36, DT_VER -> 0.16.

Verified: tests/run.sh 86/86, tests/term_diff.py 62/62, tests/desktop.py
291/291, tests/apps.py 212/212.

## 0.35

Desktop moves to **0.15** alongside this release.

**The menu bar clock's format is a setting**, in Control Panel's Date &
Time pane: a dropdown of examples (14:05, 14:05:09, 2:05 PM, 2:05:09 PM,
Tue 14:05, Tue 29 Sep 2:05 PM, 2026-09-29 14:05 and more), or Custom…
for any strftime format, checked as it is typed and limited to 24 columns
so it cannot run into the menus. A format with seconds asks for one frame
a second; one without asks once a minute. The clock and everything beside
it are placed from the width of what was actually drawn, so a wider
format moves left and its clicks follow it.

**The notification icon is a dot**: ● while there are notes you have not
seen, ○ once the history has been opened.

**Date & Time can change the time zone**, through a finder that narrows
tzdata's own list as you type, and **the date and time** where the machine
owns its clock. In a container, which shares its host's clock, it says
"set by the host" instead of offering something that cannot work. Both run
sudo in a terminal window of their own, so a password prompt and the
result stay on screen; setting the clock turns automatic time (NTP) off
first, and the dialog says so. A zone changed while the pane is open is
picked up within five seconds, by every clock the desktop draws.

**A dropdown value with a space in it never reached its callback whole.**
Menu items kept their command as one string and ran it split, so "2:05 PM"
arrived as two arguments. Items now keep their words separately. The
File Type dialog, and Date & Time's three, now name themselves in the
application menu rather than showing their internal prefix.

HIBR_VER -> 0.35, DT_VER -> 0.15.

Verified: tests/run.sh 86/86, tests/desktop.py 291/291, tests/apps.py
212/212.

## 0.34

Desktop moves to **0.14** alongside this release.

**An idle desktop sleeps instead of redrawing on a timer.** A live session
was measured at 19% of a core with nothing happening: it redrew the whole
desktop five times a second. The default refresh (`DT_TICK`) had been
raised from 200ms to 2000ms long ago, but that never reached anyone whose
settings file had saved the old value, and the shipped `session.hibr` set
200 itself. The tick is gone. The desktop now wakes for input, a resize,
output from a terminal window's program, or the soonest thing that asked
for a frame: the menu-bar clock asks for the next minute, the Clock desk
accessory and the Date & Time pane for the next second, a blinking cursor
for its next half-second, Minesweeper's timer, games, Tasks, About, notes
and the desktop icons' mount rescan for their own. Measured in a pty with
a terminal window open: about 0.2 wakes a second and 0.2% CPU, or 1.5%
with a blinking cursor. The Behaviour pane's Refresh row is gone with it,
and `tests/desktop.py` now counts the desktop's own wakes while idle, so a
tick cannot come back unnoticed.

**A resize signal that arrived just before the console started waiting was
lost until the wait timed out.** Found because the tick had been hiding it:
choosing a new primary display made `hold` signal the desktop while it was
still handling the click, and the bar stayed on the old display for up to
a minute. `cn_wait` now counts resizes and waits in `pselect`, so each one
ends exactly one wait, whenever it lands.

HIBR_VER -> 0.34, DT_VER -> 0.14.

Verified: tests/run.sh 86/86, tests/desktop.py 278/278, tests/apps.py
212/212, and the console, most, hvi, mon, cat and editor pty suites.

## 0.33

Desktop moves to **0.13** alongside this release.

**A Growl-style notification center**, the other idea ticketed in 0.32's
own `docs/backlog.md` and now built. `dt_note "$msg"` keeps its exact
signature and all ~30 existing call sites are untouched, but several can
now be on screen at once, corner-stacked (`DT_NOTEPOS`, four corners,
top-right by default) rather than one replacing whatever was already
showing -- the old single `DT_NOTE` slot is gone. A new `dt_notify "$msg"
cmd args...` is the clickable form: clicking that note runs `cmd` and
dismisses it; clicking a plain `dt_note` just dismisses it. Every note
that has shown, on screen or already gone, is kept in a capped, in-memory
history (50 entries) -- a new bell in the menu bar, and a "View History"
row in a new Notifications Control Panel pane (which also sets the
corner and the timeout), open a read-only window over it, timestamped.
Trash and shortcut-rebind notes are now clickable, straight to the
Trash folder or to Control Panel. Themed the same way everything else
in the desktop is (`DT_ACTIVE`/`DT_FACE`/`DT_INK`), since it draws with
the same calls `dt_confirm` already does.

Two batched notes that used to silently coalesce into one line under the
old single-slot mechanism (`dt_fileop`'s and `dt_trash`'s own "moved N
items" wording, each followed immediately by a more specific second
`dt_note` call that used to just overwrite the first) would have shown
as two separate stacked notes for the exact same batch under the new
queue -- `dt_tally` now takes the specific detail as an optional
argument instead, so there is exactly one note per batch, same as before.

HIBR_VER -> 0.33, DT_VER -> 0.13.

Verified: tests/run.sh 86/86, tests/desktop.py 276/276, tests/apps.py
212/212.

## 0.32

Desktop moves to **0.12** alongside this release.

**A new "File Types" pane in Control Panel** for `dt_handler`'s own
extension-to-program table -- list, add, edit and delete a mapping
through the UI, not only through a `dt_handler` line in a script of your
own (which still works exactly as before, and is what `dt_save` now
writes these back out as, the same way it already does for shortcuts
and icon positions). Double-clicking a file with no registered handler,
no `.png`/`.txt` default and no executable bit now asks first --
"Unregistered extension .ext -- open in hvi anyway?" -- rather than
silently opening it; declining leaves it alone.

**A real, pre-existing bug found building that pane's own Command
field**, and fixed everywhere it already existed: `dt_textkey`'s own
`"text cursor"` return has no separator of its own besides a plain
space, and every caller extracting the cursor back out
(`${res#* }`, the *shortest*-prefix form, splitting at the *first*
space) silently assumed the text itself never has one. A command line
always does ("feh -g"); Files' own Rename (a filename can) and Get Info
(name, owner, group) had the identical latent bug, just harder to hit in
practice. Typing past an embedded space scrambled every character after
it -- confirmed by the *saved* value, not just the display, once
isolated. Fixed at every one of the five call sites to `${res##* }`,
the longest-prefix form, which correctly splits at the *last* space
regardless of how many are in the text itself.

Two tickets recorded in `docs/backlog.md`, not built this release: a
Growl-style notification queue/history (today there is only ever one
note on screen at a time, replaced rather than queued, with no history
to look back at), and an "ultra small" `mods/db.c` -- a column-store
engine sketch (mmap'd row groups, zone maps for scan-skipping,
branch-light vectorized filters) with the real gaps it would need
closed before it is more than a demo: no static per-group row limit
inherited without deciding to, a real insert path, a composeable query
surface instead of two hardcoded comparisons, and an honest statement
of what "single writer, `msync`, no crash recovery" actually means.

HIBR_VER -> 0.32, DT_VER -> 0.12.

Verified: tests/run.sh 86/86, tests/desktop.py 267/267, tests/apps.py
212/212.

## 0.31

Desktop moves to **0.11** alongside this release.

**A shortcut still would not register after the first one that did**,
reported live as "I tried ctrl-l, alt-ctrl-l, and l, and none of them
registered" -- and reproduced exactly: the first attempt on a fresh row
worked, and every attempt after it silently failed, regardless of the
key. Root cause was a second bug in the same capture flow 0.29 had
already fixed one bug in. `panel_click`'s own "a second click on an
already-selected row activates it" means a shortcut row already selected
from a previous attempt arms capture on the very first click of the
next one -- so the click right after that, the other half of an ordinary
double-click or just a habitual re-click, arrives as its own event and
was read by `dt_event`'s own capture check as "the next key," silently
cancelling the capture it had itself just armed, before the intended
key was ever pressed. A mouse event no longer cancels a capture in
progress; only escape does.

**A terminal window's title now reads "Terminal [x]", not just "x"** --
whether x is what the program inside reported through its own OSC
title, the name of a file opened for editing, or the name of an
executable run directly (0.30's own new feature). The window stays
identifiable as a terminal no matter what it is showing.

**Files can now run an executable directly, from a script of your own**:
`dt_handler <ext> [term] <cmd...>` (documented in
`examples/desktop/README.md`, called from `session.hibr`, never from the
desktop itself) is where an extension-to-program mapping is added --
there is no default one, and none is meant to ship, since what opens
what is a choice the user's own session file makes.

**The wallpaper can now scale, zoom, or center, not only stretch** --
`DT_WALLMODE`, a new row in Control Panel's own Wallpaper pane (a
dropdown, or `m` on the keyboard): stretch ignores the image's own
aspect ratio (the original and only behaviour); scale fits it entirely
within the screen, keeping that ratio, the glyph pattern showing
through any letterboxed margin; zoom fills the screen, keeping the
ratio, with the overflow cropped; center is the image's own native
size, unscaled, centred. The same cell-aspect correction (`2*sw/sh`,
since a cell is one column but two source pixel rows) the wallpaper
picker's own preview already used for "fitted, not stretched."

HIBR_VER -> 0.31, DT_VER -> 0.11.

Verified: tests/run.sh 86/86, tests/desktop.py 267/267, tests/apps.py
203/203.

## 0.30

Desktop moves to **0.10** alongside this release.

**Double-clicking an executable in Files now runs it, in a terminal
window, instead of opening it in hvi** -- closing the file-handler
question left open in 0.29 (extension → handler mapping, `.txt` → Note
Pad). The executable bit decides run vs edit; it does not decide
windowed-app vs terminal. A hibr desktop app is only an app while it is
*sourced into the running desktop process* -- `dt_apps()` reads it so
its `dt_app`/`<name>_draw` registration runs in the desktop's own
namespace -- so making Files source an arbitrary double-clicked file
into that same live process, on a click, would cross a trust boundary
the hibr menu's own `DT_APPDIRS` allow-list deliberately does not. A
script wanting a window belongs there, where the menu already finds
it. Two details the naive version would have gotten wrong: the
existing `DT_OPENCMD` plumbing (built for a registered handler's own
interpreter, e.g. `python3 script.py`) always appends the path as a
final argument, which for an executable *of* itself would silently run
it against its own path a second time as an unwanted argument -- a new
`DT_OPENSELF`/`TW[$id]["selfrun"]` flag skips that append; and a
terminal window closes itself on a clean exit, which for a registered
handler is the point but for a double-clicked script would flash and
vanish before there was anything to read, so a self-run window stays
open regardless of status, same as a non-zero exit already does.

Verified: tests/run.sh 86/86, tests/desktop.py 264/264, tests/apps.py
200/200.

## 0.29

Desktop moves to **0.9** alongside this release.

**Control Panel shortcuts that appeared to do nothing when you tried to
change them**, reported live as "I cannot change shortcuts/keybindings
in general -- they work, but I cannot change them," specifically for
combinations like `alt-ctrl-x`. Root cause: the "press a key to rebind
this" capture treats a lone Escape as a silent cancel, and `cn_key`'s
own Alt/Escape disambiguation -- the same "is this a lone Escape or the
first byte of Alt-something" wait every terminal program needs -- could
be cut short by a *different*, unrelated terminal window's own program
merely having something to say at the wrong moment, deciding "Escape"
before the combination's second byte was ever read. Fixed in two parts:
`cn_key` now tracks a real wall-clock deadline for the disambiguation
instead of collapsing it the moment any watched pty stirs, and a
cancelled capture now says "Cancelled" instead of silently doing
nothing either way.

**File manager, four small things asked for together**: opening a `.txt`
file now offers Note Pad, the same way `.png` already offers Image
Viewer. A window's own title now names what it is showing --
`Files [~/some/dir]`, `~` for home the same way a prompt's `\w` already
would -- an app knows what it is showing better than the menu that
opened it. The path shown at the top of the window is a breadcrumb now:
click it for a dropdown of every ancestor directory, root included, and
choose one to jump straight there.

**A terminal window's own title now follows what the program inside it
reports**, through the same OSC 0/1/2 escape every real terminal
already honours -- a shell's own `PS1` is the "settable based on
variables" way to choose one (`\[\e]0;Terminal [\w]\a\]`, see
`docs/interactive.md`'s prompt escapes), not something new added here.
On by default (`DT_TERMTITLE`), and a new **Terminal** pane in Control
Panel holds it next to the terminal's scrollbar toggle, moved there out
of Behaviour since both are terminal settings, not general ones.

**Games no longer litter the hibr menu** -- Snake, Mines and Bricks moved
into their own `Games` subfolder, which the existing app-folder
mechanism already turns into a submenu automatically, the same way any
folder of apps does.

**Task Manager's own gap under its graphs**: `dt_win` already excludes
both border rows before an app's own `_draw` ever runs, and the layout
math subtracted three rows for them (as if the borders were still
included) instead of the one actual header row -- so a real border's
worth of dead space sat under the graphs for no reason. Also: the PID
column widens to whatever the widest PID on screen actually needs
(previously a fixed 6 characters, overrun and misaligned at seven
digits).

**The wallpaper picker's own mouse wheel did nothing** -- Control
Panel's generic scroll fallback only ever applies to a pane with no
`_draw` of its own; Wallpaper draws its own body and needs its own
`_wheel`, which nothing had written yet.

**`sysinfo`'s own uptime read now knows macOS**, via `sysctl` and
`KERN_BOOTTIME` rather than `/proc/uptime`, which does not exist there;
it read 0 minutes on every Mac. Unverified beyond a syntax read --
`sys/sysctl.h` does not exist on this Linux box even under
`-D__APPLE__`, so this could not be compile-checked here at all.

**About hibr**: relabelled "Desktop 0.8" / "hibr 0.28" as "hibr desktop
v0.9" / "hibr v0.29" with a blank top margin row; added Hostname (a
real FQDN, `hostname -f`, not the legacy and usually-empty
`/etc/domainname`) and Kernel rows underneath the version lines.

HIBR_VER -> 0.29, DT_VER -> 0.9.

Verified: tests/run.sh 86/86, tests/desktop.py 264/264, tests/apps.py
197/197.

## 0.28

Desktop moves to **0.8** alongside this release.

**Task Manager misaligned itself for any process with a wide enough
PID**, reported live at seven digits (real on macOS; Linux's own default
32768 ceiling never reaches it, which is why this went unnoticed until
now). The PID column was a fixed 6 characters; a wider one overran it
unpadded and pushed every column after it out of place. The column now
widens to whatever the widest PID actually on screen needs, never
narrower than 6, so the common case costs nothing extra.

**Homebrew's `post_install` -- used across the last two releases to
write a starter `~/.hibrc` -- never reliably ran, and now we know why**:
it is deprecated in current Homebrew ("Warning: Calling `post_install`
is deprecated! Use `post_install_steps` instead", seen live), and its
declarative replacement (`post_install_steps`) has no way to write into
a user's home directory at all -- by design, every `base:` it offers is
relative to the formula's own prefix. Homebrew formulas not writing
into `$HOME` is a deliberate choice worth respecting, not a limitation
to route around: two formulas could collide on one dotfile, an upgrade
could clobber a customised one. The formula now uses `caveats` instead
-- always shown, never deprecated, and it tells the user what to add
rather than acting on their behalf.

**`rc_load`'s own "no startup file" line was debug-only**, which is
exactly why "does hibr even load `.hibrc`" took several rounds of the
wrong theory to chase down. Raised to `HIBR_LINF` -- visible under
`hibr -d 2`, still below the default level, so a user who has simply
never wanted a `.hibrc` is not nagged about it every login.

HIBR_VER -> 0.28, DT_VER -> 0.8.

Verified: tests/run.sh 86/86, tests/desktop.py 261/261, tests/apps.py
184/184.

## 0.27

Desktop moves to **0.7** alongside this release.

**The macOS black screen, actually fixed this time** -- 0.26's fix landed
in the wrong process. The desktop always auto-holds by default
(`dt_autohold` -> `hold new -d`), so `console open` runs inside a
detached, headless held process that never touches the real terminal at
all; the real terminal only receives anything once it attaches, through
entirely separate code in `mods/hold/cli.c`. The same one-second
settle delay 0.26 gave `cn_open`, confirmed live to be the right fix in
principle, is now also in `hd_attach` -- the function that actually owns
the real terminal on every ordinary desktop launch.

**Keyboard shortcuts silently doing nothing while a window is focused**,
reported live as "I cannot use ctrl-alt-t for Terminal, and I cannot
change it either." Not a key-decoding bug -- confirmed hibr correctly
decodes real Mac keypresses (`alt-ctrl-t` came back exactly as pressed).
The actual cause: `dt_event` gives the focused window's own key handler
first refusal on every key, and only checks global shortcuts if that
handler declines. Control Panel's own handler (`panel_key`,
`examples/desktop/apps/panel.hibr`) discarded whatever its active pane's
key handler actually returned and always reported "handled" -- so
Control Panel with certain panes open (Wallpaper, at least) swallowed
*every* global shortcut unconditionally. Fixed to propagate the real
result. Audited every other app with its own key handler
(`bricks`/`mines`/`snake`/`tasks`/`files`/`calc`/`notepad`/`puzzle`/
`term`) for the same shape; all already decline correctly. One
by-design exception worth knowing: while a Terminal window itself has
focus, shortcuts legitimately go to the shell running inside it instead
of the desktop -- correct behaviour for a terminal emulator, not a bug,
but it means trying to open a *second* terminal via its own shortcut
while a *first* one is focused won't work, and never will.

HIBR_VER -> 0.27, DT_VER -> 0.7.

Verified: tests/run.sh 86/86, tests/desktop.py 261/261, tests/apps.py
184/184. The Darwin branch of hd_attach is untested here -- no way to
build it without Mach headers -- syntax-checked with gcc -D__APPLE__ only.

## 0.26

Desktop moves to **0.6** alongside this release.

**The real fix for the macOS black screen**, after two false starts. The
actual cause: some Mac terminal apps do not render the alternate screen
until they have had a moment to finish their own window setup after
opening, confirmed with a plain `sleep 1` before `console open` fixing it
with nothing else different. `cn_open` now waits one second before
entering the alternate screen, on Darwin only. The two earlier attempts
(widening `cn_size`'s retry budget, then a `dt_run` startup self-heal)
were reasoned from a stale-size theory that turned out to be wrong: given
that a `console flush` already re-fits the grid to the real terminal size
on every single call, size staleness was never actually the problem, and
both fixes are removed as dead ends -- recorded in CLAUDE.md so the same
theory doesn't get re-tried.

**A display could not be dragged to the left of, or above, whatever
happened to be at hold's own (0,0)**, reported live: the Displays Control
Panel pane's drag-release handler clamped the new position to zero in
both row and column before sending it to `hold move`, even though the
same file's own drawing code already handles negative *relative*
positions correctly (a display "before" the primary in hold's own
coordinates is exactly what happens the moment the primary is not the
leftmost one) -- confirmed the underlying protocol has no such
restriction of its own. The clamp was the whole bug; removed.

Also fixed properly this time: writing a starter `~/.hibrc` -- including
the new `command_not_found` autoloader -- only ever happened via
`deploy.sh`'s own install path, which a plain `brew install hibr` never
runs at all. The Homebrew formula (both this repo's copy and the live
`osakka/homebrew-hibr` one) now writes the same file via `post_install`.

## 0.25

Desktop moves to **0.5** alongside this release.

**A process substitution's child could become a permanent zombie.**
`xpsub_done` closed a `<(...)`'s descriptor and tried a non-blocking
`waitpid`, but discarded the tracking record regardless of whether that
wait actually reaped anything -- for `while read; do ...; done < <(cmd)`,
the loop's own first `read` reaches that code microseconds after the fork,
almost always before `cmd` has exited, so the wait reliably answered
"not yet" and the pid was forgotten anyway. Confirmed with a hundred-
iteration loop: 97 zombies on the old code, 0 fixed. Not desktop- or
macOS-specific -- any fast loop around `<(...)` hit this on any platform;
it surfaced first in Task Manager's own macOS process scan because that
is where the pattern was already in real use. The record now stays
tracked until a wait actually resolves it, one way or the other.

**The black screen on some Mac terminals was confirmed indefinite**, not
merely slower than `cn_size`'s widened 1s retry budget: waiting alone
never revealed it on a real Mac, only an actual resize did, meaning
nothing was ever going to self-correct without one. `dt_run`'s startup
self-heal is back, and unconditional this time -- a forced `console
reassert` on the first idle tick regardless of whether the re-read size
even changed, since that covers a stale-size theory and a terminal-
render-lag theory at once, without needing to know which one it is.

**A bare command name can autoload its module.** `command_not_found` in
`.hibrc` -- written into a new install's starter file by default -- calls
the new `mod find <builtin>`, which walks the module path the same way
`need` already does but matches a candidate's own builtin table by name
instead of its declared interface. `console key`, `img draw`, anything a
module registers, now works without an explicit `mod load` first, the
same shape bash's `command_not_found_handle` is. Interactive only --
`.hibrc` is never read by a script -- and deliberately only reachable
after PATH and every builtin/function has already refused the name, so
it can never shadow a real program. (Considered and set aside: moving
`json.c` itself to a module on the strength of this -- the autoloader
doesn't help scripts, which is how `json` is actually used, and the
underlying typed-map data it operates on is core infrastructure either
way. Recorded in `docs/backlog.md`.)

Smaller: About hibr shows the desktop's own version on its own line above
hibr's, rather than combined onto one; `sysinfo` gained a Darwin picture
and an explicit fallback to it when `/etc/os-release` doesn't exist,
which it never does on macOS.

## 0.24

Desktop moves to **0.4** alongside this release. A macOS hardening pass,
found live on a real Mac rather than guessed at from this project's own
Linux development machine.

**A window's centred title** was centred within the room left over after
excluding the button cluster, not across the bar's true full width --
pushing it off-centre by about half the cluster's own width, in opposite
directions depending on which side the buttons dock. Both sides were
wrong; only one was reported.

**A black screen until the first manual resize**, on terminals that settle
their real size more slowly than a plain local pty does: `cn_size`'s own
retry budget widens from 200ms to 1s, and `dt_run` now re-checks the real
size once more on its first idle tick regardless of whether a resize ever
fires -- free, since that tick's own wait for a key takes as long as
`DT_TICK` anyway.

**Wallpaper and TLS silently never worked on macOS**: `libpng` and
`libssl` were dlopen'd by bare Linux `.so` names only. Both gain macOS
`.dylib` paths (Homebrew's own versioned builds, by full keg-only path --
confirmed against Apple's own developer forums that the system's
unversioned copies hard-abort third-party code that loads them) and,
more importantly, a negative cache: a missing library was previously
rediscovered-and-failed on every call, which for a wallpaper once set
meant three failed `dlopen`s and a log line on every single frame, a
real and separate source of the reported choppiness. The Homebrew
formula now `depends_on` both on macOS.

**`DT_DRAWSKIP`** (Control Panel, Behaviour) is renumbered 0-9, default
0: the loop compared it against `DT_DRAWSKIP - 1`, so the old default of
1 skipped nothing, and the number never meant what it said.

**A native `darwin` module**: `cpu` and `mem`, from one
`host_statistics(64)` call each, no fork and no wait for a second
sample. Task Manager's system-wide meters were never implemented on
macOS at all; About hibr's own `top -l 2 -n 0` blocked the whole
single-threaded draw loop for about a second every three, whenever its
window was open -- a second real, separate source of the choppiness.
Both now use the module when it loaded, falling back to the previous
`top`/`vm_stat`/`ps` behaviour otherwise. Built and reasoned through on
a Linux box with no Mach headers at all; verified loading and resolving
its symbols on a real Mac, the numeric correctness of the readings
themselves still to be confirmed there.

**`zone1970.tab`**, tzdata's own place table, is not reliably at
`/usr/share/zoneinfo` on macOS -- reported missing there, where that
path is usually a symlink chain down through a version-stamped
`/var/db/timezone/...` and apparently does not always resolve. The
Date & Time Control Panel pane and `examples/traceroute.hibr` now check
`$TZDIR`, then the standard path, then the direct macOS path, and fail
silently -- no map mark, rather than a shell error printed at every
desktop startup -- if none exist.

## 0.23

Desktop moves to **0.3** alongside this release.

**Multiple monitors**, built on `hold` learning to hold more than one
attached client at once (`-m`), each with its own viewport into the
union of every attached screen's size, its own front grid for damage
diffing, and mouse reports translated by its own offset before the
desktop ever sees them. The desktop confines the bar, the icon layout
and the control strip to the first screen rather than stretching them
across the union, and gains `--join` to attach a second terminal
beside an already-running session, `hold`'s own named-client and
query/move/drop/primary protocol for a Control Panel to drive it, and
a Move-to submenu and Displays menu on every window built straight
from `hold clients`.

The Control Panel's own **Displays pane** went through several real
redesigns rather than one: first a WYSIWYG picture of the actual
layout in place of a list, then a dropdown to choose the primary
display instead of a click gesture that turned out to collide with
dragging, then pinning the primary as the pane's own fixed anchor so
dragging a secondary never renumbers what "primary" means mid-drag.
It now drops the little close-box corner glyph that never did
anything a menu couldn't do better, and gains a right-click context
menu on any display's rectangle — Detach, and Identify, which flashes
the display's own name in a bordered box drawn on *that* display, not
wherever the Control Panel itself happens to be open.

Two bugs found while verifying that arc, both silent: `HIBR_HOLD` is
an ordinary environment variable, so a terminal already attached to
one session inherited it into anything it spawned, and `dt_autohold`'s
own `[ -n "$HIBR_HOLD" ]` guard mistook any non-empty value — even one
naming an unrelated session — for "already held", starting a nested
`desktop --session X` un-held with no visible symptom. Fixed by
comparing basenames instead of merely checking for a value. The second
was `DT_SELFARGS`, captured too late in `desktop.hibr`'s own top level
to still see `"$@"` once `session.hibr`'s own `opt`/`args` had already
consumed it into named variables — so a re-exec through `hold` silently
dropped `--session` and everything else on the command line, always
re-launching as plain "desktop". The capture now happens at the very
top of `session.hibr`, before anything reads its arguments.

A third, unrelated bug surfaced by the user's own bug report: the menu
bar's left/right arrow keys walked `MB[]` by raw index, assuming each
bar menu sat next to the one before it — true until a submenu (Desk
Accessories' own flyout) got allocated an entry in the same array,
after which arrowing right from the **hibr** menu could land on the
submenu instead of Edit. `dt_mbar` now walks the array skipping
anything that isn't itself a bar-level menu, keyed off each entry's
own `bar` flag rather than its position.

**A configurable redraw skip**, `DT_DRAWSKIP` (Control Panel, Behaviour
pane, 0–9, default 0 — today's behaviour unchanged unless raised): that
many consecutive `mouse drag` reports beyond the first are absorbed
without a redraw, while a press or release always forces one
immediately and resets the count. Measured with an instrumented
redraw counter: a fixed 20-drag sequence drew 77 frames at the
default and 45 at `DT_DRAWSKIP=4` — a real reduction on a slow link or
a slow terminal, at the cost of the pointer visibly catching up in
jumps rather than gliding. Built alongside it: `dt_slider`, a
reusable horizontal slider widget (`▸────●─── 10`) for the Control
Panel, joining the existing checkbox and dropdown widgets and driven
by the same left/right-arrow cycling convention as a dropdown.

Smaller fixes: `term draw` and `img draw` gain `-p pane` and lose
their last absolute-coordinate paths, so nothing a window draws can
land outside its own pane regardless of which module drew it;
`deploy.sh`'s own fingerprint now covers `examples/desktop` and its
launcher, not just the C sources, so a desktop-only change is not
silently skipped on `deploy.sh update`; and `tools/homebrew-sync.sh`
keeps `osakka/homebrew-hibr`'s formula on hibr's latest GitHub tag on
its own six-hourly timer, so a tagged release reaches `brew install
hibr` without a manual step.

## 0.22

Desktop moves to **0.2** alongside this release.

**A real colour preview for the Wallpaper Control Panel pane**, drawn
straight into its own window rather than a grayscale ASCII render
captured into text. `img draw` gains `-p pane`, and `dp_api` gains
`prect`, a named pane's own rectangle, so any module can translate and
clip its own drawing into a window without reading the window manager's
own position table — the same discipline `console put -p` already gives
text, reused rather than reinvented. The module ABI for "display" moves
to `DP_API_VER` 3, since a module built against the old headers can no
longer link — the same kind of break 0.21's own ABI move to 3 was, and
which stayed a minor release then too. `img`'s resampled-grid cache
grows from one entry to four, round-robin, since a pane's own preview
and the desktop's wallpaper can now both call `img draw` with different
files in the same frame.

Applying a wallpaper dithers the resample (a 4x4 ordered Bayer pattern)
before it is ever darkened under a window's shadow, so a real photo's
own faint gradients read as fine grain instead of a repeating band once
darkened; a window's shadow itself now rounds its own darken math rather
than truncating it.

Desktop notifications (`dt_note`) get a real border and a timeout,
rather than three plain rows that only ever cleared on the next click or
key — which is what made applying a wallpaper from a window filling
most of the screen look like the display had come out wrong, rather
than like a transient toast.

Task Manager: process name folded into the CPU-time read already
happening every scan, fixing a latent bug where a name containing a
space was silently truncated, and halving what the read still cost
beyond that. Its own refresh interval no longer gets silently
overridden by a hardcoded one. Terminals now wake on real pty output,
rather than polling every 30ms after each key.

Glob patterns: escaping an extglob-trigger character (`(` `)` `|` `!`
`@` `+`) now stays literal, matching bash's own extglob-on behaviour —
it silently did nothing before, since the pattern-quoting pass only
recognised `* ? [ \` as characters needing the escape reinserted.

`tools/next-version.sh` (`make next-version`) suggests the next
`HIBR_VER`/`DT_VER` from what changed since the last tag. Fixed here,
found while cutting this exact release: it originally suggested a major
bump (a jump to 1.0) for any module ABI change or `Breaking:` trailer,
without checking this project's own precedent first — 0.21 carried a
real ABI break (the `nsh` rename) and stayed a minor release, the
standard meaning of staying below 1.0 at all: anything may still break
between any two 0.x releases, and 1.0 is a deliberate declaration of
stability, not a mechanical consequence of one breaking change. The tool
now bumps the minor version for a break too, once still below 1.0, and
only suggests an actual major bump once the project already has one to
count from. Still a suggestion, not an authority either way: a single
commit spanning both core and desktop can carry a break that only
applies to one of them, which this release's own `DT_VER` needed a
human judgement call to catch regardless.

## 0.21

**Renamed from `nsh` to `hibr`** — Hackable In-process Bash Runtime, and
**حِبر**, Arabic for ink. Every macro, exported symbol, filename and
user-visible name changed with it: `$HIBR_RC`, `$HIBR_MODPATH`,
`$HIBR_HISTFILE`, `$HIBR_TLS_INSECURE`, `$HIBR_DEBUG`, `~/.hibrc`,
`~/.hibr_history`, and `include/hibr.h`. The module ABI moved to **3**, since
a module built against the old headers can no longer link. Reasoning and the
names considered: [decision 0015](docs/adr/0015-the-name-is-hibr.md).

Documentation reorganised. The README is an overview; the detail moved to
`docs/`, every directory carries its own `README.md`, and the deliberate
divergences from bash each became a decision record under `docs/adr/`.

`deploy.sh` builds with the right module directory, runs the suite, keeps the
previous installation for rollback, verifies the copy it installed, and can
keep itself current with a systemd timer or cron. A `MODDIR`-related bug is
fixed along the way: `make install PREFIX=X` produced a binary that could not
find its own modules, because the module directory is compiled in and changing
it did not force a rebuild.

Every count in the git segment carries its own colour — staged green, modified
yellow, untracked cyan, conflicted and deleted red, ahead green, behind red —
rather than the whole block sharing one style, which it previously did not
have at all: `status_style`, `ahead_behind_style` and `state_style` had no
defaults, so the most information-dense part of the prompt rendered plain.

A prompt built in process: `PROMPT_FN` names a function, builtin or module
builtin whose result slot becomes the prompt, with `$STATUS` and `$DURATION`
set for it. The `prompt` module renders segments from a `PROMPT[...]` map with
a `[text](style)` and `(conditional)` format language — directory, git branch
and repository state, exit status, command duration, jobs, and the usual
environment and language markers, all without forking. Modules are searched for
on `$HIBR_MODPATH` and in the install directory, so `mod load prompt` works.
Multi-line prompts are measured correctly by the line editor.

The module now reads git's object store itself: a DEFLATE decoder, loose
objects, pack indexes v1 and v2, and delta chains through both `OFS_DELTA` and
`REF_DELTA`, with alternates followed. No zlib, no `git`, no forks.
`prompt object [-p] <sha>` reads one object, for inspection.

The `git` segment now reports the working tree: `.git/index` in versions 2, 3
and 4, staged changes by diffing the index against HEAD's tree, modified and
deleted files by stat data and by hashing when that is inconclusive, untracked
files with `.gitignore`, `info/exclude` and `core.excludesFile` honoured, exact
renames, conflicts, and distance from the upstream branch by walking both
histories. It carries its own SHA-1 for this. All of it in process.

Fixed a signed overflow in the lexer: a run of more than ten digits before `<`
or `>` overflowed the descriptor number it was scanning for.

## 0.20
Arithmetic statements `((…))`, `let`, `for ((;;))`, a rebuilt evaluator with
assignment operators, `++`/`--`, `?:`, comma and `**`, two's-complement overflow
defined and UBSan-clean. `+=` for strings, arrays and map entries. `[[ ]]` with
short-circuiting and `=~` into `M`. `select`, `!!` history expansion,
`history -s/-p`, `CDPATH`.

## 0.19
`${x:off:len}`, `${a[@]:off:len}`, `${x^^}` family, `$RANDOM $SECONDS $PPID
$UID $EUID $HOSTNAME`, `printf -v`, `echo -e`, `unset a[k]`, `wait PID`,
`local a=(…)`. `:=` clears the slot and suppresses builtin output.

## 0.18
Declared arguments with `opt` and `args`; `title` renames the process. `ls`
module. `printf` width overflow fixed.

## 0.17
Module ABI v2: map access, result slot, errors, sockets, and `/dev/<name>/`
scheme registration; `http` reference module. `make install`, `make check`,
`--help`, `-n`. Parser fuzzer; recursion bounded. Opt-in strict expansion
`set -S`.

## 0.16 and earlier
Brace expansion, `$'…'`, globstar. Completion over PATH, variables and user
hooks; `&>`, `<<<`, `>|`, `{fd}>`, `read` options, `command`, `builtin`,
`getopts`, `umask`, `time`, `disown`. `fail`/`try`/`trap ERR`. JSON, `str`,
`arr`. Networking and TLS. Daily-driver features: `.hibrc`, prompts, aliases,
dirstack, `^R`. Nested maps, regex, typed `fn`, result slots. Job control,
`case`, `local`, heredocs, errexit, the line editor, modules, and the core
shell.
