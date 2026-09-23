# mods/console

A text display for other things to be built on. The line editor owns one line; this
owns the terminal — an alternate screen, a grid of cells, a redraw that sends
only what changed, and keys decoded once into names.

It exists because four wanted tools need the same piece: a `vi`, a `most`, a
system monitor, and anything else full-screen. Building it once is the point.

| file | role |
|---|---|
| `cn.h` | the cell, the grid, the pane, and every `cn_` entry point |
| `term.c` | taking and giving back the terminal, and the signals that guarantee it |
| `grid.c` | the front and back buffers, placement, and the diffing flush |
| `key.c` | bytes to key names: CSI, SS3, modifiers, mouse, bracketed paste |
| `console.c` | panes, colour parsing, and the `console` builtin |

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

## Naming

Everything is `cn_`, not `sc_`. `src/net.c` already uses `sc_` for schemes and
exports `sc_fini(sh *)` — which is exactly the signature a module finaliser
has. A module named `sc_fini` is silently preempted by the shell's under
`-rdynamic`, so the terminal would never have been put back and nothing would
have crashed to say so. See the trap in `CLAUDE.md`.

## The mouse

Off until something asks for it:

    console mouse click     # presses and releases
    console mouse drag      # and dragging
    console mouse motion    # and every movement, which is a lot
    console mouse off

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
on its answer.

**A resize asserts the terminal's modes again and repaints everything.** The
terminal on the other end may not be the one the screen was opened on: a
session reattached with `hold` arrives as a `SIGWINCH` on a terminal that has
never seen the alternate screen, the hidden cursor or the mouse mode. So
`console resized` sends them again and invalidates the front grid, and the
next flush draws the whole frame. It costs one full frame on an event that is
rare anyway.

## Testing

`tests/console.py` drives all of it through a pseudo terminal, because none of
it happens without one; `tests/640-console.t` covers what `run.sh` can reach,
which is the no-terminal behaviour and the argument checking.

    python3 tests/console.py
