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
| `sb.c` | the scrollback: a ring of the lines that went off the top, and the view that scrolls back through it |
| `vt.c` | the escape parser: CSI, SGR in 16, 256 and 24-bit colour, OSC titles, the alternate screen, mouse and paste modes, and replies to status queries |
| `draw.c` | blitting the grid into a rectangle through the display interface |
| `key.c` | turning a decoded key name (`up`, `ctrl-c`, `f5`) back into the bytes a program expects, and a mouse event into the report it asked for |
| `term.c` | the builtin and the module's life cycle |

## The builtin

    t := term open [-r rows] [-c cols] [-s lines] [--] cmd args...
    term poll  t [ms]            # read what the program wrote, parse it
    term draw  t row col h w     # paint the screen into a rectangle
    term key   t name            # a decoded key name, as the program expects it
    term write t text            # raw bytes
    term size  t [rows cols]     # ask, or resize (the program gets SIGWINCH)
    term alive t                 # status 0 while the program runs
    term status t                # its exit status once it has ended
    term title t                 # what it last called itself with OSC 0 or 2
    term cursor t                # row, column, and whether it is shown
    term row   t n               # one row of what is shown, as text
    term scroll t [n|top|bottom] # move the view back n lines, or ask where it is
    term mouse t [act [button] row col]  # send a mouse event, or ask the mode
    term screen t                # main or alt
    term select t start|to r c   # begin a selection at a shown cell, or carry it on
    term select t none
    term copy t                  # the selected text; fails when nothing is
    term close t

Each `term open` is its own terminal, its own session and its own program.
Two windows of `examples/apps/term.hibr` are two shells on two ptys, and
`tests/apps.py` checks exactly that.

## Scrollback

What scrolls off the top of the main screen is kept, 1000 lines unless
`term open -s` says otherwise (`-s 0` keeps none). The alternate screen never
adds to it, since what vi and less draw there is not history, and `ESC [3J`
clears it, as `clear` asks. `term scroll t 5` moves the view back five
lines, a negative count forward; `term scroll t` answers `view stored`. A key,
a paste or a mouse event sent to the program goes back to the live screen,
and so does a resize.

A line is stored without its trailing blanks, so the cost is the text: a cell
is 20 bytes, and 1000 lines of 40 characters are about 800 kB. A short session
costs next to nothing.

A window that shrinks under the cursor pushes its top lines into the
scrollback rather than losing the line being typed on, and one that grows
pulls them back, which is xterm's behaviour. Resizing also keeps the
alternate screen, so vi stays on it and redraws.

## Selection

Every line the terminal has held is numbered from the first ever pushed into
the scrollback, and a selection is two of those numbers with a column each.
So it stays on the same text while more output scrolls underneath it, and it
reaches back into the scrollback when the view is scrolled there. It is
drawn reversed; `term copy` gives it as text, each line without its trailing
blanks and joined by newlines, as a terminal's copy does. A resize clears it.

## The mouse

A program asks for the mouse with the private modes every terminal has:
9 (presses), 1000 (presses, releases and the wheel), 1002 (and drags), 1003
(and all motion), and 1006 for the SGR encoding. `term mouse t` answers
`off`, `click`, `drag` or `motion`, and `term mouse t press left 2 4` sends
the report, counted from zero, if the program asked for that kind of event.
It fails if not, so the caller can use the event for something else: the
terminal app scrolls back with the wheel then. Without 1006 the old encoding
is used, which cannot name a column past 223, and a report it cannot encode
is dropped rather than sent wrong.

Bracketed paste (2004) wraps a `paste` key in `ESC [200~` and `ESC [201~`
when the program asks.

## Tests

`tests/760-term.t` drives the screen model through real programs: text,
cursor addressing, erasing, a scroll region, the alternate screen, deferred
wrap, UTF-8 with a wide character, the title, resizing, exit status, a hibr
inside it editing its own line, the scrollback and its limits, and the exact
bytes a program reads for mouse events and a bracketed paste. `tests/apps.py` covers `term draw` and
keys through a pty, in a window of the desktop. The module is clean under
ASan and UBSan with leak detection on that test.

## What it does not do

Motion with no button held is not reported, even under 1003, because the
desktop turns on only click and drag reporting: all motion is a report per
cell crossed. Modifiers on a mouse event are not passed on. Nothing reflows
on a resize. Character sets beyond UTF-8 (`ESC ( 0` line drawing) are not
translated. Each is a small addition when something needs it.
