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

clock_draw()  { console put -p "w$1" 0 0 "..." }
clock_key()   { case $2 in ...) ;; *) return 1 ;; esac }
```

**The app name is a prefix, not a command.** An app called `clock` provides
any of `clock_draw`, `clock_key`, `clock_click`, `clock_open` and
`clock_close`, and the window manager asks once, with `command -v`, which of
them exist and calls only those.

That is not cosmetic. The first version had one function answering a verb —
`clock draw`, `clock key` — and an app that only drew still swallowed every
key sent to it, because a `case` that matches nothing succeeds, and a
successful `key` means "handled". The symptom was that `q` stopped quitting
while a clock had focus. With a prefix, an app that has not written a key
handler cannot take a key, by construction rather than by remembering to add
a `*) return 1 ;;` arm to something it never thought about.

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

An app that *does* handle keys still returns non-zero for the ones it does not
want, and that arm sits inside its own `<name>_key`, which is where someone
writing a key handler is already thinking about it.

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

*As built*, the interface is the `term` builtin rather than a C table:
`term open`, `poll`, `draw`, `key`, `write`, `size` and the rest, described
in [`mods/term/README.md`](../../mods/term/README.md). The module still
registers `terminal`, so `need terminal` finds it unloaded, and it asks for
`pty` and `display` itself rather than carrying either.

## A menu bar, after the fact

Not in the original record, and added because the owner asked for System 7's
— which turned out to fit the decision rather than bend it. A menu bar
belongs to the *active application*, which is a window manager's concern and
not a window's, so it lives in `desktop.hibr` with everything else, and an
app declares its menus through the same prefix contract it declares `_draw`
through.

The one decision worth recording: **there are no modifier shortcuts.** F10 or
escape opens the bar, and letters only mean something while it is open. Ctrl
collides with everything a terminal window needs — and a terminal window is
the next step — and alt with what a program inside one might want. A desktop
that eats ctrl-c is a desktop nothing can run in. The cost is two keystrokes
instead of one, and it is worth it.

### Amended: two alt shortcuts, for copy and paste

The owner asked for keyboard copy and paste, and chose **alt-c and alt-v**.
It is the one exception, and the reasoning above still decides its shape.
Ctrl stays untouched, because a terminal window's program needs every ctrl
key and ctrl-shift-c cannot be told from ctrl-c on most terminals. Alt costs
less: a program in a terminal window loses exactly alt-c and alt-v (readline's
capitalise-word, for one), and nothing else. Both are also on an Edit menu
the desktop owns, so they can be reached without the keys. No other modifier
shortcut has been added, and one should need as good a reason.

## What the menu bar grew, and why

Ticks, items that cannot be chosen, and one level of submenu — each because
something wanted it rather than in advance. The control panel's Theme was a
"Next Theme" that cycled blindly; as a submenu with a tick against the
current one it is both shorter to write and better to use, and it is what
made submenus worth building at all.

**An item that cannot be chosen is drawn, not removed.** With no window
focused the Window menu still shows Move and Resize, without their letters.
A menu that changes shape between one moment and the next is one nobody can
learn the shape of.

**The Window menu is always last and always there**, after whatever the
application declared, because an app must not be able to hide the only way
to move or close its own window.

## Where the line is

This is a windowing layer, not a widget toolkit. Each app draws its own
buttons and its own text fields. If the same button-drawing code appears in
three apps, *then* it becomes a library, and not before — a toolkit designed in
advance for apps that do not exist yet is how this kind of thing dies.

## How it gets built

Each step is usable before the next one starts.

0. **`need` and `app`.** *Done.* The app contract, and useful today in any
   script that depends on a module being there.
1. **Stacking and hit testing in the console.** *Done.* `console pane raise`,
   `lower`, `drop` and `list`, and `console hit row col`. Sixty lines of C,
   and everything after this is script.
2. **One window that can be dragged and closed.** *Done.* `examples/desktop/desktop.hibr`
   is the window manager; `examples/desktop/desktop-session.hibr` is a session that
   opens three windows on it. `tests/desktop.py` drives both through a pty and
   reads the screen back.
3. **Two windows, focus, and minimise.** *Done*, with zoom as well, since it
   was the same four lines. Minimising drops the window's pane, which means
   nothing draws it and nothing can hit it — so the bar across the top grew a
   label per minimised window, because otherwise there is no way back. A
   feature that only goes one way is half a feature.
4. **Calculator and file browser.** *Done.* `examples/desktop/desk-accessories/calc.hibr` and
   `examples/desktop/apps/files.hibr`, each also a program that runs on its own. The
   calculator hands its expression to the shell's own evaluator, so it is
   fifty lines and does integers — there is no decimal point on the keypad
   because there would be nothing behind it. The browser scrolls with the
   keys, with the wheel, and with a scrollbar, and the wheel moves the view
   without moving the selection. Both are in `tests/apps.py`.

   Two things changed in the window manager for them: the wheel is routed to
   the window *under the pointer* rather than the focused one, and a click is
   reported in the same coordinates the app draws in. The second was a design
   error found by both apps getting the conversion wrong in opposite
   directions — an app that draws its keypad at pane row 4 is now told a
   click at pane row 4, and there is no second coordinate system.
5. **Control panel.** *Done.* `examples/desktop/apps/panel.hibr`: theme, wallpaper
   glyph, refresh rate, and a row per window that raises, hides or closes it.
   A theme is applied by setting the variables the window manager already
   reads every frame, so it takes effect on the next one, in every window at
   once, without anything being told.

   It needed the window manager to grow a surface an app may call — `dt_ids`,
   `dt_title`, `dt_hidden`, `dt_raise`, beside the `dt_new`, `dt_min` and
   `dt_del` that already existed — because the alternative was the panel
   reading `DT`, which is the window manager's own table and not an
   interface. The browser had already been moved off `DT` for the same
   reason, and had walked into a subscript trap on the way.
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
