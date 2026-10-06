# 0037 — A picture is a region the display owns

Status: accepted (amended in 0.99.76: the kitty graphics protocol)

## Context

A terminal that can put pixels on screen draws them over a rectangle of
cells, and the cell grid knows nothing about it. The console's whole
economy is the difference between two grids: it writes only the cells that
changed, which is what makes a full-screen desktop cost 8 bytes for a
keystroke. A bitmap sits outside that model, and the obvious ways of
putting one on screen are both wrong:

- written straight at the screen, the next flush draws cells through it
  (and this project has already made that mistake once, with a Control
  Panel preview, which is a trap record of its own);
- redrawn every frame, a photograph costs its own encoding and tens of
  kilobytes a frame for a picture nobody changed.

## Decision

A picture is a **region the display backend owns**, placed through the
display interface (`dp_api` version 5, `image`), never drawn by the caller:

- The caller hands over pixels at any size with the rectangle in cells; the
  backend scales, because only the backend knows what a cell measures.
  Four modules would otherwise each have grown a scaler.
- The backend encodes once and keeps the bytes. A flush emits them only
  when they are new, so a still picture costs nothing a frame.
- A flush skips the cells a region covers and leaves the front grid not
  knowing them, so a window clearing its own face before drawing the
  picture again does not paint over it.
- A region is kept until the cells underneath stop matching a hash taken
  when it was placed -- the window moved, closed, something came over it --
  and then it is dropped and those cells are painted again. Nothing has to
  tell the console that a picture has gone.
- `DP_IMG_UNDER` is for a caller that will draw text over the picture: it
  owns no cells, goes out before the text, and is forgotten once sent, so
  whoever wants it places it again.
- `DP_IMG_CHOSEN` asks for a palette taken from the picture (median cut,
  256 colours) rather than the fixed 6x6x6 levels. A still photograph wants
  it; a film does not, since the fixed palette is the same every frame and
  the terminal keeps its colour registers.
- Returning 0 means "this backend cannot place pixels", and every caller
  keeps its cell rendering for that: most terminals have no such thing.

There are two protocols, not one, and which a terminal speaks is not a
matter of taste. **kitty has never drawn a sixel** and says so in its own
documentation; its own graphics protocol is the only way to put pixels in
it. From 0.99.72 to 0.99.75 this file's own list said sixel for kitty, so
every picture there was a blank rectangle -- the console claimed the cells
and the terminal dropped the bytes. 0.99.76 adds `CN_GFX_KITTY`
(`mods/console/kitty.c`) and corrects the list.

The difference between the two is the whole of why the second needed more
than an encoder. A sixel is **paint**: once the bytes have gone the terminal
has forgotten where they came from, and writing over those cells is what
removes it. A kitty picture is an **object** with an id of its own: it is
transmitted, placed, and stays above the text until something deletes it. So
every region carries an id, and every path that drops or forgets one owes
the terminal a delete, sent before the next frame's diff so the text
underneath is painted in the same frame; a different picture in the same
rectangle keeps the id, because a transmission with an id already taken
replaces what was there, which is one escape rather than two. Everything is
sent with `q=2` -- answer nothing -- since a reply would arrive in the same
stream the key decoder owns, and `C=1`, so the cursor stays where the diff
left it. Ids are taken from a band of our own, derived from the process id:
a terminal is shared, and another program using the same id would replace
our picture.

Whether a terminal can is decided from two facts, not from a probe: it must
report the pixel size of a cell (`ws_xpixel`) -- sixel paints 1:1, and a
wrong cell size spills the bitmap into its neighbours, while the kitty
protocol is given the rectangle in cells and scales for itself, but every
caller sizes its bitmap from the cell either way -- and it must be one that
understands a format, taken from what it calls itself:

| it calls itself | protocol |
|---|---|
| kitty, ghostty, WezTerm, konsole | kitty |
| foot, mlterm, contour, yaft, iTerm2, mintty | sixel |

WezTerm and konsole do both, and the kitty protocol is the better of the two
there: true colour rather than a palette, and a picture the terminal keeps
rather than one repainted. `HIBR_GFX=kitty|sixel|off` says outright.

A Primary Device Attributes probe would be more general and was rejected:
the reply arrives in the same stream the key decoder owns, racing a
keystroke, and that stream already has a trap record (the Alt/Escape
window). A name and a setting cost nobody a lost keypress.

## Consequences

- `console gfx` names the protocol in force and what a cell measures
  (`kitty 10 20`, `sixel 8 16`, `none 0 0`), so a script chooses rather
  than guessing from `$TERM` itself. A script asks whether there are pixels
  to be had, never which protocol: the desktop's `dt_imgpix` is that
  question, and the Pictures setting is `pixels`, not the name of a format.
- The desktop has one setting, Control Panel > Pictures, and every picture
  it draws goes through it: the Image Viewer, the wallpaper, Mail's
  pictures, the film player.
- The test harness keeps DCS payloads out of its screen model and records
  them instead, so a check can ask where a picture landed and how big it
  was; a pty can be told what a cell measures (`Term(cellw=, cellh=)`).
- A film in pixels costs what the frame costs, and the two protocols are
  expensive in different currencies. Measured on a 640 by 360 clip through
  the player into a pty, timed around the draw alone (the write's cost is
  whatever the terminal drains), per frame that actually went out, 20 frames
  a run, two runs agreeing (0.99.76):

  | drawn as | 60x20 cells | 100x34 cells |
  |---|---|---|
  | half blocks | 0.2 ms, 6.7 kB | 0.5 ms, 14 kB |
  | sixel | 9.1 ms, 21 kB | 23 ms, 34 kB |
  | kitty | 2.7 ms, 128 kB | 6.8 ms, 351 kB |

  Sixel's cost is the processor: a band uses most of the fixed palette on
  photographic content and each used colour is a pass over the band's
  width, which bounds a film at 100 by 34 to about 40 frames a second
  before the terminal paints any of it. (It was 21 ms and 61 ms until
  0.99.74, when the encoder stopped scanning each band once per palette
  colour, 216 times over.) The kitty protocol has no palette to build, so
  its encoding is three times cheaper -- and its payload is an order of
  magnitude larger, because base64 of the raw pixels is what goes down the
  wire. Which is why a picture that is **not** a still -- no palette chosen
  from it, which is what a film asks for -- is sent at half the pixels in
  each direction and the terminal scales it back: four times fewer bytes,
  and the numbers above are with that in place. At full resolution the same
  frames were 511 kB and 1.4 MB, and the pty could not keep up with them.
  Two things would cut sixel further and neither is built: a smaller fixed
  palette for film, and emitting only the bands that changed between
  frames. For kitty it is the transmission that would have to change --
  zlib (`o=z`, and this tree has inflate but no deflate) or a shared file
  (`t=t`, which only works where the terminal shares the filesystem, so
  never over ssh).
- The browser is converted (0.99.73): `web mode T pixels` takes the
  screenshot at the viewport's own size and draws it through `image`, with
  **no text layer at all**, because a text cell paints its own background
  and would box out the bitmap behind every glyph. "Page as a picture" and
  "text over colours" are two different renderings, and that is a product
  decision rather than a detail.
- **A held desktop had no pixels at all until 0.99.77** (Gitea #103), and
  `dt_autohold` holds every desktop, so for five releases this whole
  mechanism reached `img draw` in a terminal and not the desktop anyone
  actually runs. Both halves were hold's, and both were measured rather
  than reasoned about -- by driving a held program through a pty and
  reading what came out the other side:
  - `hold` gives the program a pty of its own and nobody had told it what
    a cell measures, so `console gfx` answered `none 0 0` inside every
    held program. A client now reports its own `ws_xpixel`/`ws_ypixel`
    with its size, hold picks the primary client's and sets it on the pty
    (`pty resizepx`, `PY_API_VER` 3).
  - hold draws each client cells from its own emulator, which consumed
    every sixel and every kitty escape and drew nothing for either: the
    OSC 52 trap, again. The emulator keeps them as regions now
    (`mods/term/img.c`, `TM_API_VER` 4) and hold re-emits them per client
    after the cells, once per change, skipping any that does not fit that
    client's own rectangle -- a bitmap cannot be clipped.
  `tests/holdpix.py` is the measurement kept: eleven checks, with the
  unheld sixel and kitty suites as the control.
- **A picture with text drawn over it in the same frame needs
  `DP_IMG_UNDER`**, and until 0.99.79 nothing passed it -- which is why the
  wallpaper had never been pixels on any terminal (Gitea #104). A region
  that owns its cells is dropped the moment they change, and the wallpaper
  is placed first and then drawn over by the bar, every window and the
  icons: it was dropped before the first flush ever sent it. Under the text
  a picture owns no cells, goes out before them, and is kept for as long as
  the caller places it again -- the first frame that does not is what takes
  it away, which is the invalidation rule, since the cell hash cannot be
  one here. `dt_wall` also blanks the cells it is about to cover with no
  colours of their own: a bitmap writes no cells, so last frame's text
  would stay on top of it, and a cell with a background colour paints over
  a picture the terminal is compositing below the glyphs -- the very fill
  that used to stand in for a wallpaper would have hidden it.

  What that costs is the protocol's rather than a choice, measured on a
  70x24 screen with 8x16 cells, dragging a window across the wallpaper:

  | | sent | on a drag |
  |---|---|---|
  | kitty | one placement, 860 kB | nothing |
  | sixel | one bitmap, 24 kB | 24 kB again |

  kitty composites the picture below the text, so it is transmitted once
  and nothing above it can disturb it. A sixel is paint: a cell written
  over it has destroyed that much of it, so the bitmap goes again whenever
  anything above it moves. Both work; only one of them is free.
- **A redirection on a `console` command used to reach the display**, which
  is how the film numbers above were first measured as 0.05 ms a frame:
  the console drew on, and selected on, whatever descriptor number stdout
  had, so `console flush > /dev/null` sent the frame to the void and
  `console key 1000 > /dev/null` returned at once. Since 0.99.77 the
  console keeps a descriptor of its own (`CN_FDBASE`), dup'ed at
  `console open` and closed at `console close`.
