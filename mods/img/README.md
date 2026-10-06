# mods/img

Decode an image and draw it into a terminal as coloured cells -- a jp2a-alike,
issue #39. PNG and JPEG, told apart by their first bytes rather than their
names. `img file.png` prints ANSI to standard output; `img draw file row
col h w [-m mode] [-p pane]` blits directly into the open display instead,
for a window or the desktop's own wallpaper.

`-m` picks how: `half` (coloured half blocks, two pixels a cell), `mono`
(the same in grey), `ascii` (the character ramp, what `-g` has always
meant), `pixels` (real pixels, where the terminal can place them; `sixel`
is the same thing by its old name) and `auto`, the default, which takes
pixels where they are to be had and half blocks everywhere else. Which
protocol carries them -- the kitty graphics protocol or sixel -- is the
display's business and not named here. Pixels go through its own `image`
entry (ADR 0037): the console keeps the picture as a region of its own,
scales it to the rectangle, and sends the bytes once -- so a still picture
in a window costs nothing on the frames after the first. A terminal that
cannot place pixels is not asked twice; `console gfx` is what says which it
is.

`-u` says text is going to be drawn over this picture in the same frame,
which the desktop's wallpaper needs: a picture that owns its cells is
dropped the moment they change, so without it the wallpaper was never sent
at all (Gitea #104). Such a picture owns no cells, goes out before the text
of each frame, and is kept for as long as it is placed again -- the first
frame that does not place it takes it away. What it then costs belongs to
the protocol: a terminal that keeps pictures composites it below the glyphs
and is sent it once, while sixel is paint and goes again whenever a cell
over it has been written. Whoever uses it leaves those cells without a
background colour of their own, or the colour paints over the picture.

| file | role |
|---|---|
| `im.h` | the `image`/`cell` types, decode and resample entry points |
| `png.c` | libpng, dlopen'd on first use |
| `jpeg.c` | libturbojpeg, dlopen'd on first use; EXIF orientation |
| `img.c` | resampling, every renderer, the `img` builtin |

## Decoding is dlopen'd, not linked

The same shape `src/net.c` already loads libssl in: a struct of function
pointers, resolved by name against a list of soname candidates the first time
an image is actually decoded, so a shell that never opens one never pays for
it and the build carries no image-library dependency at all.

libpng's `png_structp`/`png_infop` are opaque -- created and used entirely
through function calls -- which is what makes this safe without the real
headers. `png_create_read_struct`'s version string is asked of the library
itself (`png_get_libpng_ver(0)`) rather than assumed, since there is no
compile-time `png.h` here to have baked one in; passing back what it just
said always "matches". Its default error path `longjmp`s, and since libpng
1.5 the `jmp_buf` is opaque too, so `png_set_longjmp_fn` has to be called
before any read to get somewhere to `setjmp` into.

`png_read_png` with a transform mask (`STRIP_16 | PACKING | EXPAND |
GRAY_TO_RGB`) does all of palette-to-RGB, low-bit-depth expansion, 16-to-8-bit
and grey-to-RGB in one call, against `png_get_rows`' output -- about a dozen
symbols in total, roughly half what the row-by-row API would need.

**JPEG goes through libturbojpeg**, not classic libjpeg: libjpeg's
`jpeg_decompress_struct` is caller-allocated with a compile-time layout, so
dlopen-without-headers would mean hand-copying a version-fragile struct.
libjpeg-turbo's own TurboJPEG API is five plain functions on an opaque
handle (`tjInitDecompress`, `tjDecompressHeader3`, `tjDecompress2`,
`tjDestroy`, `tjGetErrorStr2`), present from TurboJPEG 1.4 through 3.x,
decoding straight to 8-bit RGB. It is `libturbojpeg0` on Debian and Ubuntu
and `jpeg-turbo` in Homebrew (looked for under `/opt/homebrew` and
`/usr/local` by full path, since macOS has no system copy).

A photo's EXIF orientation is honoured: a phone stores most pictures on
their side with a tag saying which way is up, so `jpeg.c` reads tag 0x0112
from the first APP1 "Exif" segment -- every offset checked against the
bytes there are -- and turns or mirrors the decoded picture to match, all
eight orientations. A JPEG over 64 million pixels, or a file over 256 MB,
is refused rather than decoded.

## Two render paths, one decode and resample

Both `img file` and `img draw` share `im_resample`, a box filter -- every
source pixel a cell covers is averaged in, not sampled once, which is the
difference between a downscaled photo and aliased noise. A cell is one
column wide and *two* source rows tall, kept as top and bottom halves
separately, so the colour renderer can show both at once.

- **Colour** (default): each cell as `▀` (upper half block), the top half's
  average as the foreground, the bottom half's as the background --
  effectively double the vertical resolution a plain block would give.
- **Grayscale** (`-g`): top and bottom halves averaged together into one
  luminance, mapped onto a ten-step density ramp (`" .:-=+*#%@"`), printed in
  whatever the terminal's own foreground colour already is -- jp2a's own
  classic, uncoloured look.

Alpha is not kept. A terminal cell has no sensible way to blend a
transparent pixel against whatever is meant to show through it, so every
pixel is treated as opaque.

## The two entry points

`img [-g] [-w cols] [-h rows] file` never touches the display module at
all -- terminal size comes from `TIOCGWINSZ` directly when neither `-w` nor
`-h` is given and stdout is a terminal, `80x24` otherwise. This is the
testable path: deterministic ANSI bytes, no pty required.

`img draw file row col h w [-g] [-p pane]` requires `"display"`
(`hibr_require`) only inside this one subcommand, so printing an image in a
plain pipe never loads the console module. There is no bulk cell-write
primitive in `dp_api` -- drawing means one `pen`+`put` pair per cell, the
same granularity a script driving `console put` in a loop would pay, just
without the shell's own parsing and dispatch overhead per call.

With `-p pane`, `row`/`col` are relative to that pane's own top-left corner
(via `dp_api`'s `prect`, DP_API_VER 3) rather than the root screen, and
anything past its own edge is silently clipped instead of drawn -- the same
discipline `console put -p` already gives text. This is what lets a
window's own preview draw a real image without reading the window
manager's own position table (`DT[$id]["row"]`/`["col"]`) directly, and
without breaking when the window moves or resizes. `dt_wall()` keeps using
the root screen directly: the wallpaper *is* the background everything
else composites onto, not a window's own content, so it has no pane of its
own to target. The resampled-grid cache (`im_cache`) holds `IM_CACHEN` (4)
entries, evicted round-robin, rather than one: once a pane preview and
`dt_wall()` can both call `img draw` with different files in the same
frame, a single slot would thrash between them, paying for a full decode
and resample of each on every frame instead of caching either.

## Testing

`tests/830-img.t` is recorded, not compared against bash -- there is no
reference decoder to diff against here. `tests/img-2x2.png` is a 2x2 fixture
(red, green / blue, yellow, one pixel each) decoded at exactly its own size,
so every cell maps to one source pixel with no averaging to make the
recorded RGB triples fuzzy.

## PPM, for a picture root must not decode

`img -o ppm -w cols -h rows file` writes the picture at exactly the size it
would be drawn, as a binary PPM -- each cell's two halves as two pixels
one above the other -- and `img` and `img draw` read a PPM back through a
few dozen lines of this module's own (`im_ppmload`: 8 bits, up to 4096
pixels a side, every byte counted), with no library. That is for the login
screen, which runs as root and must show a person's own picture: a child
that has dropped to the person decodes their PNG or JPEG to a PPM, and root
draws only that, so libpng and libturbojpeg never see a person's file with
root's rights.
