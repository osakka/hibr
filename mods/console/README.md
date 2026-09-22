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

## Giving the terminal back

The one thing a full-screen program must never do is leave a terminal in raw
mode. `SIGINT`, `SIGTERM` and `SIGHUP` restore the terminal and then re-raise
the signal, so the shell still dies of what killed it; the module's finaliser
does the same on `mod drop` and at exit. `SIGWINCH` only sets a flag — the
grids are rebuilt in the next flush, never in the handler.

## Testing

`tests/console.py` drives all of it through a pseudo terminal, because none of
it happens without one; `tests/640-console.t` covers what `run.sh` can reach,
which is the no-terminal behaviour and the argument checking.

    python3 tests/console.py
