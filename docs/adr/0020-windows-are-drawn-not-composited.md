# 0020 — Windows are drawn back to front, and an app is a hibr file

Status: accepted

## Context

The wanted thing is a desktop on a text console: windows that can be dragged,
moved, closed and minimised; a calculator, an analogue clock, a file browser, a
control panel; and eventually a hibr running inside a hibr window. The apps
should be hibr scripts.

Most of what that needs already exists, which is the reason this is worth
doing at all rather than being a second project:

- the console draws a grid of cells and sends only the ones that changed, so
  redrawing everything every frame is already cheap — 8 bytes for a changed
  character, nothing at all when nothing moved;
- mouse reports arrive with a row and a column counted from zero, the same
  coordinates `put` takes, so a click is a position and a hit test is a
  rectangle comparison;
- `console pane` already clips drawing to a rectangle;
- the module registry already lets one module offer an interface to another,
  and the autoloader already finds a provider without being told its name.

What does not exist is z-order, a hit test, and anything that owns a window.

## Three layers, and the owner named them

The shape of this is X's, and the analogy is the owner's: *"that's then closer
to xinitrc, right"*. It is, and saying so settles several arguments at once.

| X | here | what it is |
|---|---|---|
| the server | `mods/console` | owns the terminal, the grid, the mouse, and — after step 1 — the stacking order and the hit test. Knows nothing of windows beyond panes having an order. **A module, loaded and unloaded.** |
| the window manager | `desktop.hibr` | owns the event loop, focus, dragging, title bars, minimise. **A script, run and exited.** |
| `~/.xinitrc` | the user's own session file | says which apps to open and where. **Theirs, not ours.** |

So the answer to "is the desktop a module you load and unload?" is no, and the
reason is worth keeping: **the thing you load and unload is the display**, and
that is already a module. The desktop is what you *run on it*. When it returns,
`console close` gives the terminal back and nothing of the desktop is still
resident — which is more honest to a shell that replaces bash than a module
would be, because a module stays mapped until it is dropped and a script is
gone when it returns.

## Decision

### Windows are painted back to front, not composited

Every frame, the window manager walks its windows from the bottom of the stack
to the top and asks each to draw itself into its own rectangle. The last one to
write a cell wins. The console's damage model then sends only the difference.

There are no off-screen surfaces and no compositing. That is the decision, and
the alternative is the one that sounds more correct: giving each window its own
grid and combining them. That costs a rewrite of the console's grid model, a
second set of cell buffers, and a compositor — to buy transparency, which a
text console does not want, and clipping, which panes already do.

The cost of painting is that a window cannot be partly transparent and cannot
be drawn once and reused when only another window moved. Given that a redraw
of an untouched screen already costs nothing, that second one is not a cost at
all.

### An app is a hibr file that says what it needs

There is no app directory and no app format. An app is an ordinary `.hibr`
file that declares what it cannot run without, the same way a module declares
what it offers:

```sh
#!/usr/bin/env hibr
need display                    # or this line fails, with a status
app clock "An analogue clock"

draw() { console put -p "$win" 0 0 "..." }
key()  { ... }
```

`need <name>` does for a script what `hibr_require` does for a module: if
something loaded already offers the interface, succeed; otherwise walk the
module path for a module that says it offers it and load that; otherwise take
the name as a module's and load it; otherwise fail with status 1, naming what
could not be found. It is an ordinary builtin with an ordinary status, so
`need display || exit 1` behaves like any other command, and a script run over
a connection with no terminal fails at that line rather than halfway through
drawing.

**That symmetry is the design**: a module declares `prov`, a script declares
`need`, and the shell resolves both through one registry.

`app` is a declaration and nothing else — it sets `APP_NAME` and `APP_DESC`
and returns. So a file with an `app` line still runs on its own, taking the
whole console; a host that sources it sees the declaration and calls `draw`
into a window instead. It works without the desktop, and better with it.

Finding them needs no special home, in the same way loading a module does not:

- `desktop ./clock.hibr` — a path
- `desktop clock` — a bare name, searched on `HIBR_APPPATH`
- `desktop` — every `.hibr` on that path with an `app` line, offered in a list

The extension stays `.hibr`. A separate one would buy nothing: the `app` line
is the marker, the way `.so` rather than `.hibrmod` is enough for modules.

### The window manager is a hibr script too

It owns the event loop, keeps the window list and the stacking order, routes
keys to the focused window, routes mouse events by hit test, and handles the
title bar — drag, close, minimise — without the app knowing any of it happened.
An app is called to draw into a rectangle and to be told about a key; it never
sees a frame, a border or a stack.

So the calculator, the clock and the control panel are each a few dozen lines
of hibr, with nothing new in C.

**There is no performance case for writing it in C**, and that was measured
rather than assumed. A script redrawing a full screen with one `console put`
per *cell* — 920 builtin calls a frame, which is the worst any window manager
would ever do — runs at hundreds of frames per second. Add hit testing on every
mouse event, a dispatch per key, and three apps doing real work in `draw`, and
there is still an order of magnitude more headroom than a terminal can visibly
use. The script was never going to be the slow part; the terminal is.

### It is cooperative, and says so

One event loop, one process, apps called in turn. An app that takes a long time
in `draw` stalls the desktop. There is no pre-emption and there are no threads.

That is the trade for "everything is a hibr script", and for a desktop of
clocks and calculators it is the right one. It has one consequence that is not
negotiable: anything that waits on the outside world — and the terminal window
is exactly that — must be read without blocking, which is the main reason that
one piece is C.

### It is cells, not pixels

Windows snap to cells. Borders are box-drawing characters. An analogue clock is
drawn at cell resolution with block or braille glyphs, and its hands are
computed from a small table of sines in the script rather than from floating
point the shell does not have. There is no anti-aliasing and no sub-cell
placement; asking for them is asking for a different program.

### A window is a pane with a stack position

`console pane` gains two things and nothing else changes:

- panes are stacked, and the order is the order they are drawn in;
- `console hit <row> <col>` answers with the topmost pane covering that cell,
  or nothing.

That is the whole of the C change for everything except the terminal window.

### The terminal window is a module, and it is last

A hibr running inside a hibr window needs a pseudo terminal, a child process,
and a *terminal emulator*: something that reads what the child writes and turns
escape sequences into cells. Cursor movement, SGR, erase, scroll regions, the
alternate screen.

That is `mods/term/`, offering a `terminal` interface — `spawn`, `feed`,
`cell`, `resize` — and it is the largest single piece of the desktop, larger
than the window manager and all the other apps together. It is deliberately
last, so that everything else is working and useful before it starts.

## Where the line is

This is a windowing layer, not a widget toolkit. Each app draws its own
buttons and its own text fields. If the same button-drawing code appears in
three apps, *then* it becomes a library, and not before — a toolkit designed in
advance for apps that do not exist yet is how this kind of thing dies.

## How it gets built

Each step is usable before the next one starts.

0. **`need` and `app`.** *Done.* The app contract, and useful today in any
   script that depends on a module being there.
1. **Stacking and hit testing in the console.** Ten lines of C. Everything
   after this is script.
2. **One window that can be dragged and closed.** Not a clock — a plain
   window, so that the event loop, the drag and the back-to-front redraw are
   the only things being judged. If dragging does not feel right here, nothing
   built on it will.
3. **Two windows, focus, and minimise.** Now the stacking order earns its
   keep.
4. **Calculator and file browser.** The calculator proves keys reaching a
   focused window; the file browser proves scrolling *inside* a window and
   mouse events reaching content rather than only the frame.
5. **Control panel.** The window that configures the others.
6. **The terminal window.** The emulator, and a hibr inside a hibr.

## What this costs

A slow app stalls the desktop, and will keep doing so. Windows cannot overlap
transparently. There is no way to run an app that is not written in hibr until
the terminal window exists, and after that the only way is to run it inside
one. None of these is fixable without giving up "apps are hibr functions",
which is the thing that makes the rest of it small.

## Questions, both now closed

**Is the desktop a mode you enter, or the shell's interactive mode?** — *a
mode.* The analogy answers it: `xinit` does not replace your login shell, you
run it. `desktop` is the same. Anyone who wants it on login puts a line in
their `.hibrc`, which is their file and their choice, and the line editor and
the desktop never have to share a loop.

**Where do apps live?** — *closed, by the owner, before this record was
finished.* They do not live anywhere in particular. An app is a `.hibr` file
that declares what it needs and is found the way a module is: by path, by name
on a search path, or by looking for the `app` line. See the section above. The
piece that was missing was that scripts could not reach the autoloader; `need`
is that piece, and it is useful on its own — `need display` in any full-screen
script, `need highlight` in any script that colours text.

---

[← decisions](README.md)
