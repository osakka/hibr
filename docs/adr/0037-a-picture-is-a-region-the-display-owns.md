# 0037 — A picture is a region the display owns

Status: accepted

## Context

A terminal that can paint pixels (sixel) draws them over a rectangle of
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

Whether a terminal can is decided from two facts, not from a probe: it must
report the pixel size of a cell (`ws_xpixel`), since sixel paints 1:1 and a
wrong cell size spills the bitmap into its neighbours, and it must be one
that understands the format -- taken from what it calls itself (kitty, foot,
WezTerm, mlterm, iTerm2, mintty, contour). `HIBR_GFX` says outright.

A Primary Device Attributes probe would be more general and was rejected:
the reply arrives in the same stream the key decoder owns, racing a
keystroke, and that stream already has a trap record (the Alt/Escape
window). A name and a setting cost nobody a lost keypress.

## Consequences

- `console gfx` says what the terminal can do and what a cell measures, so
  a script chooses rather than guessing from `$TERM` itself.
- The desktop has one setting, Control Panel > Pictures, and every picture
  it draws goes through it: the Image Viewer, the wallpaper, Mail's
  pictures, the film player.
- The test harness keeps DCS payloads out of its screen model and records
  them instead, so a check can ask where a picture landed and how big it
  was; a pty can be told what a cell measures (`Term(cellw=, cellh=)`).
- A film in pixels is bounded by what the encoder costs per frame. Measured
  on a 640 by 360 clip, drawn through the player into a pty, median of 20
  frames (0.99.74):

  | drawn as | 60x20 cells | 100x34 cells |
  |---|---|---|
  | half blocks | 0.3 ms | 0.8 ms |
  | pixels | 8.9 ms | 23.7 ms |

  So pixels cost 30 times what blocks do, and a film at 100 by 34 is bound
  to about 40 frames a second by the encoding alone, before the terminal
  paints any of it. The first version was 21 ms and 61 ms: it scanned each
  band once per palette colour, 216 times over, where one pass per band
  fills every colour's column pattern at once. What is left is the emission
  itself -- a band uses most of the fixed palette on photographic content,
  and each used colour is a pass over the band's width.
  Three things would cut it further and none is built: a smaller fixed
  palette for film, emitting only the bands that changed between frames,
  and the kitty graphics protocol where it is there (true colour, no
  palette at all). The player's frame rate setting bounds it meanwhile.
- The browser is not converted. It draws text over its screenshot, and a
  text cell paints its own background, which would box out the bitmap
  behind every glyph: "page as a picture" and "text over colours" are two
  different renderings and that is a product decision, not a detail.
