# mods/img

Decode an image and draw it into a terminal as coloured cells -- a jp2a-alike,
issue #39. `img file.png` prints ANSI to standard output; `img draw file row
col h w` blits directly into the open display instead, for a window or the
desktop's own wallpaper.

| file | role |
|---|---|
| `im.h` | the `image`/`cell` types, decode and resample entry points |
| `png.c` | libpng, dlopen'd on first use |
| `img.c` | resampling, both renderers, the `img` builtin |

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

**JPEG is not decoded yet.** Classic libjpeg's `jpeg_decompress_struct` is
caller-allocated with a compile-time layout, so dlopen-without-headers means
hand-copying a version-fragile struct rather than calling opaque functions.
libjpeg-turbo's own TurboJPEG API (`tjInitDecompress`, opaque `tjhandle`
throughout) is the dlopen-friendly alternative, the same shape as libpng --
but `libturbojpeg.so` was not installed on the machine this was written and
tested on, so it is left unimplemented rather than shipped unverified. See
issue #39.

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

`img draw file row col h w [-g]` requires `"display"` (`hibr_require`)
only inside this one subcommand, so printing an image in a plain pipe never
loads the console module. There is no bulk cell-write primitive in `dp_api`
-- drawing means one `pen`+`put` pair per cell, the same granularity a
script driving `console put` in a loop would pay, just without the shell's
own parsing and dispatch overhead per call.

## Testing

`tests/830-img.t` is recorded, not compared against bash -- there is no
reference decoder to diff against here. `tests/img-2x2.png` is a 2x2 fixture
(red, green / blue, yellow, one pixel each) decoded at exactly its own size,
so every cell maps to one source pixel with no averaging to make the
recorded RGB triples fuzzy.
