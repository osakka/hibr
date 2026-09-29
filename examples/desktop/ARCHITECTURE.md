# The desktop's architecture

[Windows on a console](README.md) says what the desktop does. This says how
it is built — the three layers, the event loop, the rendering model, and the
contract an app is written against. Every screenshot below is a real render
of real hibr output, made with [`tools/screenshot.py`](../../tools/README.md),
not a mockup.

![An empty desktop: the menu bar, the wallpaper, and the three fixed icons](img/desktop-empty.png)

## Three layers, X's shape

| X | here | what it is |
|---|---|---|
| the server | the `console` module | owns the terminal, the grid, the mouse, stacking, hit testing |
| the window manager | `examples/desktop/desktop.hibr`, sourcing `wm/` | the event loop, focus, dragging, title bars, menus — one concern to a file, see [`wm/README.md`](wm/README.md) |
| the widget library | `examples/desktop/widgets/` | checkbox, dropdown, slider, text field, scrollbar — see [`widgets/README.md`](widgets/README.md) |
| `~/.xinitrc` | your own session file | which windows open, and where |

None of it is C beyond the display module itself. The window manager is a
hibr script; every app is a hibr script; the session that starts them is a
hibr script you write and own, the same way an X session is whatever
`~/.xinitrc` says to run — see
[decision 0020](../../docs/adr/0020-windows-are-drawn-not-composited.md) for why this
split was chosen over compositing panes together in the display module
itself.

![Three windows open: a calculator, the file browser, and a real terminal running a nested hibr shell](img/desktop-apps.png)

That terminal window is not a screenshot of a screenshot — it is a real
`hibr` process, started by `term.hibr` on a pseudo terminal, showing its own
real prompt with the actual git branch this repository is on. Nothing about
what a window shows is faked or simulated for the picture.

## The event loop

`dt_run`, in `wm/session.hibr`, is the whole of it:

```
while [ "$DT_QUIT" = 0 ]; do
    dt_draw
    k := console key "${DT_WANT:-$DT_IDLEMS}"
    dt_event "$k"
done
```

(Simplified — the real loop also debounces a burst of resize events into one
settled redraw, described in `wm/session.hibr`'s own comments.) `console key
MS` blocks for up to `MS` milliseconds waiting for one decoded key or mouse
report, and returns the instant one arrives. A resize interrupts the wait
the same way (`SIGWINCH`), and so does output from any terminal window's
program, since `term.hibr` asks `console watch` to include its pty.

Nothing else wakes it. There is no refresh rate: `DT_WANT` is the soonest
of every `dt_want MS` any `_draw` asked for during the frame just drawn,
and each ask lasts that one frame. Everything that changes on its own asks
for its own next change — the bar clock for the next minute, the Clock desk
accessory and the Date & Time pane for the next second, a blinking terminal
cursor for its next half-second, a game for its next step, Tasks and About
for their own refresh settings, a note for the moment it expires, the
desktop icons for their 5-second mount rescan. When nothing asks, the loop
sleeps for `DT_IDLEMS` (60s), a net under anything that forgot to.

It used to be a periodic poll: `DT_TICK`, first 200ms, later 2000ms,
redrew the whole desktop that often whether anything had changed or not.
Raising the default did not reach anyone whose settings file had already
saved 200 — `dt_save` writes every kept setting, so a default frozen into
it outlives the code that set it — and the shipped `session.hibr` set 200
itself. A live session measured 19% of a core doing nothing. With the tick
gone an idle desktop with a terminal window open measures about 0.2 wakes
a second and 0.2% CPU; a blinking cursor costs about 1.5%, because each
blink still runs every window's `_draw` — redrawing only the windows that
changed is the next step, not yet taken. `tests/desktop.py` counts the
process's own wakes with nothing happening, so a tick cannot come back
unnoticed.

`dt_draw` walks every open window bottom to top, drawing its face and letting
its own `_draw` callback fill it in, then the menu bar and whatever floats
above everything — a menu, a drag, the confirm box. `dt_event` decodes one
key or mouse line and dispatches it: to a confirm box or a rebind capture if
one is pending, to the open menu if one is, to the focused window's own key
handler, and only once all of those have declined it, to the handful of
global shortcuts (Control Panel has the current list).

## Redrawing costs only what changed

The console module keeps two grids, front and back. Every `dt_draw` writes
into the back grid; `console flush` compares it against the front and sends
only the cells that differ, then makes the two match. A full first paint of
an 80x24 screen costs a few kilobytes; a completely unchanged frame costs
zero bytes, not a redraw with nothing in it — the comparison itself is real
CPU work, but nothing crosses the pty. This is what makes the periodic poll
above affordable at all: the tick costs a wake-up and a grid diff, not a
full terminal repaint five times a second.

## An app is a prefix, not a class

There is no window object, no base class, no registration beyond a name. An
app called `calc` is whichever of these functions it happens to define —
`calc_draw`, `calc_key`, `calc_click` or `calc_mouse`, `calc_open`,
`calc_close`, `calc_menus` — checked for with `command -v` once, when the
window is made, and only the ones that exist are ever called. Everything an
app remembers is keyed by the window id it was handed, never a bare global,
since two windows of the same app must not share one:

```sh
fn calc_draw(id, h, w, row, col) {     # window id, body size, where it sits
    console put -p "w$id" 1 2 "${CA[$id]["out"]}"
}
```

Every function the desktop defines declares its parameters this way, and a
callback declares the whole of what it is called with, whether it uses all
of it or not -- a declared function refuses an argument it has no name for,
so `fn calc_draw(id)` would fail on the first frame. The calls:

| callback | arguments |
|---|---|
| `_open` | `id arg` -- `arg` is whatever `dt_new` was given last, often empty |
| `_close` | `id` |
| `_draw` | `id h w row col` -- the body's size, and the window's place on screen |
| `_key` | `id key` |
| `_click` | `id r c btn` |
| `_mouse` | `id act btn r c mods` |
| `_wheel` | `id dir r c` |
| `_drop` | `id r c op paths...` |
| `_menus`, `_context` | `id` |

A Control Panel pane is called with its own set -- `_draw id h w x`,
`_key id key`, `_click id r c`, `_drop id r c key`, `_wheel id dir` -- and a
Control Strip module with `_draw row col` and `_click row col`. A function
that takes a list ends in one, as `fn dt_row(tick, dim, lb, k, ...rest)`
does; the old `name() { local id=$1 ...` form still works for an app of
your own, unchecked.

`console put -p "w$id" row col text` writes into that window's own pane,
in the coordinates the window draws in — row 0 is its own top border, not
some outer coordinate system the app has to translate into. A `_click`
callback is told a click in exactly those same coordinates; a richer
`_mouse` callback additionally gets the raw press, drag, and release, which
is what a terminal emulator or a file browser's own drag-and-drop needs.

## Menus, two kinds

The bar across the top is rebuilt every frame from whatever has focus: the
hibr menu, Edit and Window (which belong to the desktop, not any app), and
the focused app's own menus if it defines `<app>_menus`, built with
`dt_menu`, `dt_item`, `dt_sub`, and `dt_sep`.

![The hibr menu open, showing About hibr, Clean Up Icons, Detach and Quit](img/desktop-menu.png)

A right-click gets a different menu, specific to what was clicked rather
than to what has focus — the desktop background, a title bar, or an app's
own `<app>_context` if it defines one:

![A right-click on the desktop background showing Arrange Icons and Change Wallpaper](img/desktop-context.png)

## Control Panel settings are a script, not a format

Nothing about the desktop's configuration is a config file format of its
own. `dt_save` writes plain assignments and setter calls to
`~/.config/hibr/desktop.hibr`, `dt_load` sources it back — a script like
any other, editable by hand, and it is exactly the plain variables the
window manager already reads every frame (`DT_WALL`, `DT_ICONS`, `DT_KEYS`,
and so on), so a change takes effect the moment it is written, in every
window at once, without anything being told to refresh. `panel.hibr`, the
Control Panel app, is what edits it interactively — not with settings of
its own, but as a host for panes, each an ordinary file under
`examples/desktop/control-panel`, System 7's Control Panels folder rather than one
long scrolling list:

![Control Panel's picker, Appearance selected and its Theme row cycled to slate](img/desktop-controlpanel.png)

A pane down the left, its own rows on the right — arrows move the picker,
tab or right or enter steps into the selected pane, and the same keys then
move its row cursor instead. Appearance and Behaviour hold what used to be
one flat list's worth of settings; Shortcuts holds the four fixed desktop
bindings — Close Window, Detach, Quit, Cycle Windows — rebindable by
selecting the row, pressing enter, then the new key; App Shortcuts lists
every registered app the same way, empty by default, so any app can be
given a global launch shortcut, not just the two (Terminal and Task
Manager) that ship with one; Window Style sets a window's own chrome —
frame, button side, title alignment, button glyphs — read by `dt_win` and
`dt_btn` themselves, not by the pane; Date & Time draws its own body
instead of rows — the clock, the date, and a small
world map with a mark near the machine's own time zone, reusing
`examples/traceroute.hibr`'s own map and zone1970.tab lookup rather than a
second copy of either. See
[Control Panel panes](README.md#control-panel-panes) for the two shapes a
pane can take and how `CP_PANEDIRS` finds them.

## A real app, for scale

The task manager reads `/proc` directly on Linux, no forking, throttled to
once a second; it is not a mockup of a process list, it is this machine's
actual processes at the moment the screenshot was taken:

![The task manager, sorted by CPU, showing real processes from this machine](img/desktop-tasks.png)

## What this is not

It is not a compositor: nothing is transparent, nothing overlaps with
blending, and a window's shadow is a darkened copy of the cells under it,
computed once per frame, not a rendering effect. It is not accelerated:
every cell is a real character cell, drawn with real SGR escape sequences,
at whatever rate the terminal it runs in can keep up with. And it does not
yet match a purely event-driven multiplexer's idle cost — see the note under
[the event loop](#the-event-loop) above, and the project's own
[backlog](../../docs/backlog.md) for what is still open about it.
