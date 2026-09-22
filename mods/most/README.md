# mods/most

A pager. `most` rather than `less` or `more`: two windows, horizontal
scrolling that works, every match highlighted rather than jumped between, and
colour in the input that survives being paged.

    most file...
    something | most

## The thing a pager must get right

Its input is the data, not the keyboard. `cmd | most` puts a pipe on standard
input, so a pager that reads commands from its own input works perfectly when
tested with a file argument and not at all in real use.

The screen module already handles this: it takes the terminal from standard
output when that is one, and falls back to `/dev/tty`. So `most` reads keys
through `scr_key` and never touches standard input except to read the text.
`tests/most.py` covers both shapes, and the piped one is the one that matters.

## Built on the screen module

This is the first module to use another. Modules are opened `RTLD_LOCAL`, so
`most.so` cannot see a symbol in `screen.so`; it asks the shell instead:

```c
sc = hibr_require(s, "screen", SCR_API_VER);
if (!sc) { lg(HIBR_LERR, "most: needs the screen module"); return HIBR_FAIL; }
```

`screen.so` offers the table in its init with `hibr_provide` and withdraws it
in its finaliser, so dropping the screen module makes `most` refuse rather than
call into an unloaded object. Both halves are checked in
`tests/670-module-api.t`.

## What it does

| | |
|---|---|
| `j k` arrows, `enter` | a line |
| `space` `ctrl-f` `ctrl-b` `pageup` `pagedown` | a screen |
| `g G` `home` `end` | the ends |
| `h l` arrows | eight columns sideways |
| `/` then text | search; every match on screen is highlighted |
| `N` | the next line that matches |
| `i` | fold case or not |
| `F` | follow the file as it grows |
| `s` | split into two windows, `tab` to switch |
| `n` | the next file in this window |
| `?` | the key list |
| `q` | leave |

**Colour survives.** ANSI in the input is parsed into screen-layer pens rather
than drawn as escape characters, so coloured output stays coloured through
paging *and* through horizontal scrolling — the escapes are not in the text
being scrolled, so they cannot be cut in half.

**It does not wait for the end.** Lines are shown as they arrive, so
`slow-thing | most` is readable immediately. What has arrived is kept, because
scrolling back needs it; what has not is not waited for.

## Testing

`tests/most.py`, through a pseudo terminal. It reassembles the screen from the
escape stream rather than grepping it — the screen layer sends only the cells
that changed, so a search of the raw bytes finds `78-200/200` where the display
reads `178-200/200`. Twenty checks; the one worth keeping is that `seq | most`
pages correctly, because that is the shape real use takes.
