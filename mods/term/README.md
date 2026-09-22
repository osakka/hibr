# `term` — a terminal emulator

The last step of
[decision 0020](../../docs/adr/0020-windows-are-drawn-not-composited.md): a
program runs on a pseudo terminal, and this module keeps a picture of its
screen as cells and paints that picture into a window. What runs inside is a
real program with a real terminal. It edits its own line, draws its own
colours and does not know it is in a window.

It stands on two interfaces and implements neither itself.
[`mods/pty/`](../pty/README.md) opens the pseudo terminal and runs the
program, offered as `mods/pty.h`, and the display (`console`, through
`mods/display.h`) is what `term draw` paints into. It offers `"terminal"`, so
`need terminal` finds it unloaded.

| file | role |
|---|---|
| `tm.h` | the terminal record, cells, and every function the files share |
| `grid.c` | the screen model: cursor, scroll region, deferred wrap, insert and delete of lines and characters, resizing |
| `vt.c` | the escape parser: CSI, SGR in 16, 256 and 24-bit colour, OSC titles, the alternate screen, and replies to status queries |
| `draw.c` | blitting the grid into a rectangle through the display interface |
| `key.c` | turning a decoded key name (`up`, `ctrl-c`, `f5`) back into the bytes a program expects |
| `term.c` | the builtin and the module's life cycle |

## The builtin

    t := term open [-r rows] [-c cols] [--] cmd args...
    term poll  t [ms]            # read what the program wrote, parse it
    term draw  t row col h w     # paint the screen into a rectangle
    term key   t name            # a decoded key name, as the program expects it
    term write t text            # raw bytes
    term size  t [rows cols]     # ask, or resize (the program gets SIGWINCH)
    term alive t                 # status 0 while the program runs
    term status t                # its exit status once it has ended
    term title t                 # what it last called itself with OSC 0 or 2
    term cursor t                # row, column, and whether it is shown
    term row   t n               # one row of the screen as text
    term close t

Each `term open` is its own terminal, its own session and its own program.
Two windows of `examples/apps/term.hibr` are two shells on two ptys, and
`tests/apps.py` checks exactly that.

## Tests

`tests/760-term.t` drives the screen model through real programs: text,
cursor addressing, erasing, a scroll region, the alternate screen, deferred
wrap, UTF-8 with a wide character, the title, resizing, exit status, and a
hibr inside it editing its own line. `tests/apps.py` covers `term draw` and
keys through a pty, in a window of the desktop. The module is clean under
ASan and UBSan with leak detection on that test.

## What it does not do

No scrollback: what leaves the top of the screen is gone. No mouse reporting
to the program inside. Character sets beyond UTF-8 (`ESC ( 0` line drawing)
are not translated. Each is a small addition when something needs it.
