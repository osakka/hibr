# Windows on a console

hibr can put draggable windows on a text terminal, with apps written as
ordinary hibr functions. Nothing here is new C beyond the display module and
sixty lines of stacking in it: the window manager itself is a script, and so
is every app.

There are three layers, and they are X's, which is the clearest way to
describe them:

| X | here | what it is |
|---|---|---|
| the server | the `console` module | owns the terminal, the grid, the mouse, stacking, hit testing |
| the window manager | `examples/desktop.hibr` | the event loop, focus, dragging, title bars |
| `~/.xinitrc` | your own session file | which windows open, and where |

Try it:

    ./build/hibr examples/desktop-session.hibr

Drag a title bar to move a window. `_` minimises, `□` fills the screen, `x`
closes. `tab` cycles, `escape` brings back a minimised window, and `q` quits
and gives the terminal back.

A minimised window has no pane at all, so nothing draws it and nothing can
click it. Its label on the bar across the top is the only way back — click it
to restore the window. The bar also says how many windows are open and how
many are hidden.

## Writing a session

A session sources the window manager, defines or loads some apps, opens
windows and runs the loop:

```sh
#!/usr/bin/env hibr
. "${0%/*}/desktop.hibr"

hello_draw() {
	console put -p "w$1" 1 2 "Hello from a window."
}

dt_open || exit 1
dt_new "Hello" 8 34 5 10 hello
dt_run
dt_close
```

`dt_new title height width row col [app]` makes a window and puts it on top.
Its id lands in `$RET`, so `id := dt_new ...` works if you want to keep it.

## Writing an app

**An app is a prefix, not a command.** An app called `hello` provides any of
these, and the window manager calls only the ones that exist:

| function | called with | when |
|---|---|---|
| `hello_open` | `id` | once, when the window is made |
| `hello_draw` | `id inner_h inner_w` | every frame |
| `hello_key` | `id key` | a key, while this window has focus |
| `hello_click` | `id row col` | a click, in the same coordinates the app draws in |
| `hello_wheel` | `id up\|down row col` | the wheel, over this window whether or not it has focus |
| `hello_close` | `id` | once, when the window closes |

Draw with `console put -p "w$id" row col text` — the pane is the window, so
row 0 is its top border and the body starts at row 1, column 1. Drawing
outside the pane is clipped, and the frame is redrawn over the top of
whatever the app wrote, so an app cannot damage its own border.

**A click arrives in those same coordinates**, so a key the app drew at row 4
is clicked at row 4. There is no separate body coordinate system to convert
between; the window manager deals with row 0 itself, so an app never sees a
click on its own title bar.

**A key handler returns non-zero for a key it does not want.** That is how
`q` still quits while your window has focus:

```sh
hello_key() {
	case $2 in
	up)   N=$((N - 1)) ;;
	down) N=$((N + 1)) ;;
	*)    return 1 ;;
	esac
}
```

An app that never writes a `_key` function cannot swallow a key at all, which
is the reason the app name is a prefix rather than one function answering a
verb. See [decision 0020](adr/0020-windows-are-drawn-not-composited.md).

## What it does not do

It is cooperative: one process, one loop, apps called in turn, so an app that
takes a long time in `_draw` stalls the desktop. Windows snap to cells and
cannot be transparent. There is no widget library — each app draws its own
buttons, and if the same button code turns up in three apps, *then* it becomes
one. There is no way yet to run a program that is not written in hibr inside a
window; that needs a terminal emulator, which is the last thing on the list in
the decision record.

---

See also [full-screen programs](display.md) for the console module the whole
thing draws on, and [modules](modules.md) for `need`, which is how a script
asks for a display and fails cleanly when there is not one.
