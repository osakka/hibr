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

Drag a title bar to move a window and the `◢` in its bottom-right corner to
resize it. `_` minimises, `□` fills the screen, `x` closes. `tab` cycles, `escape` brings back a minimised window, and `q` quits
and gives the terminal back.

A minimised window has no pane at all, so nothing draws it and nothing can
click it. Its label on the bar across the top is the only way back — click it
to restore the window. The bar also says how many windows are open and how
many are hidden.

## Detaching, and coming back

Start the desktop held, and it outlives the terminal it was started on:

    hold new desk hibr examples/desktop-session.hibr

**ctrl-\\** detaches, and so does **Detach** on the hibr menu; the desktop
keeps running, terminal windows and whatever runs in them included. Log off,
log in somewhere else, and

    hold attach desk

puts it back on the new terminal exactly as it was, redrawn in full. Closing
the terminal or losing an ssh connection detaches too. `hold list` shows what
is running and `hold kill desk` ends it. Started without `hold`, Detach is on
the menu, dimmed, so the menu does not change shape. See
[`mods/hold/README.md`](../mods/hold/README.md) for how it works.

## Keys that would be signals

ctrl-c, ctrl-\\ and ctrl-z are keys on the desktop, not signals: the
desktop is left by Quit and nothing else. Each goes to the focused window, so
ctrl-c in a terminal window interrupts the program running there, and does
nothing to a calculator.

The terminal is put back if the desktop dies anyway, whether from `kill`, a
hangup or a crash in a module. While it holds the screen its stderr goes to
`~/.local/state/hibr/desktop.log` (`$XDG_STATE_HOME`, or `DT_LOG`), so an
app's error message cannot scribble over the display and the reason for a
failure is still there to read afterwards.

## Settings are kept

Every change in the Settings window is saved the moment it is made, to
`~/.config/hibr/desktop.hibr` (`$XDG_CONFIG_HOME`, or `DT_CONF`), and read
back by `dt_open` at the next start. It is a script, not a format:

```sh
# The desktop's settings, written whenever one changes and
# read at the next start.  A script like any other.
DT_WALL=\#1a202c
DT_TICK=200
CP_THEME=slate
```

`dt_open` reads it after the session file has set its own defaults, so what
was chosen last wins. An app that wants a variable of its own kept calls
`dt_keep NAME` and `dt_save` after changing it; the Settings app keeps
`CP_THEME` that way.

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
| `hello_draw` | `id inner_h inner_w row col` | every frame; `row col` is where the window is on screen, for `console fill`, which takes screen coordinates |
| `hello_key` | `id key` | a key, while this window has focus |
| `hello_click` | `id row col button` | a click, in the same coordinates the app draws in; `button` is `left`, `middle` or `right` |
| `hello_mouse` | `id press\|drag\|release button row col` | instead of `_click`, for an app that wants the whole of a press: after the press, its drags and its release come here wherever the pointer goes |
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

**A window that animates asks for its next frame.** The desktop redraws on
every key and otherwise every `DT_TICK` milliseconds, 200 by default. A game
calls `dt_want 60` from its `_draw` to be drawn again within 60 ms. The
request lasts one frame, so a game that is paused, hidden or not focused
stops asking and the desktop goes back to idling. Step on the clock, not on
frames: frames also come with every key, and a snake that moved once per
frame would sprint while an arrow is held. `$EPOCHREALTIME` is the clock.

## The apps

In `examples/apps/`, each one also a file you can read in a sitting:

| app | what it is |
|---|---|
| `files` | a file browser, with a scrollbar and the wheel |
| `calc` | a calculator, and `hibr calc.hibr '3 * 4'` on its own |
| `panel` | settings, and a list of the other windows |
| `term` | a shell in a window. Each window is its own pty and its own session. The wheel or `shift-pageup` scrolls back, and a program that asks for the mouse gets it |
| `snake` | arrows turn, `p` pauses. It speeds up as it grows |
| `mines` | Minesweeper, 9 by 9 with ten mines. `space` or a click opens, `f` or a right click flags, and opening a number with its flags placed opens what is round it |
| `bricks` | after Arkanoid: the arrows or a click move the bat, `space` serves. Where the ball lands on the bat sets its angle |

A focused terminal gets every key except `f10`, so a program inside can have
`escape`; `f10` is the way back to the menu bar.

## The menu bar

Across the top, System 7's: the **hibr menu** on the left where the apple
went, then the menus of whatever window has focus, then the clock and the
**application menu** on the right. Windows cannot be dragged over it.

The menus belong to the active application, so they change when you click a
different window, and when nothing has focus they are the desktop's own.

**F10 or escape opens the bar.** Then the arrows move between menus and
items, a letter picks the item beside it, enter chooses and escape closes.
Nothing is reserved while the bar is shut — which is deliberate, and is what
keeps ctrl-c and the rest free for whatever is running inside a window.

The application menu lists every window, with `•` against the active one and
`·` against a hidden one; choosing a hidden one brings it back. That is the
only way back from minimising, and it is where System 7 put it too.

**A Window menu is always there**, after the application's own, so an app
cannot hide the only way to move or close its window. It holds Move, Resize,
Zoom, Hide, Close and Cycle — and with nothing focused those are drawn
without their letters, present but not choosable, rather than vanishing.

**Move and Resize take the arrows** until enter or escape gives them back,
which is the keyboard's answer to dragging. A desktop that can only be
arranged with a mouse is a desktop half its users cannot arrange.

## Menus for an app

An app declares them by providing `<app>_menus`, which calls `dt_menu`,
`dt_item` and `dt_sep` — the same shape `opt` uses to declare a command line,
and the same prefix contract as `_draw` and `_key`. An app with no `_menus`
simply has none, and the desktop's show instead.

```sh
clock_menus() {
	dt_menu "Clock"
	dt_item "Set Alarm"  a  clock_alarm
	dt_item "12 Hour"    h  clock_ampm
	dt_sep
	dt_item "Close"      w  dt_close_focused
}
```

| declarator | makes |
|---|---|
| `dt_item <label> <key> <cmd…>` | an ordinary item; an empty key means no letter |
| `dt_mark <label> <key> <on> <cmd…>` | the same, with a tick when `on` is not `0` |
| `dt_dim <label>` | an item that is there and cannot be chosen |
| `dt_sep` | a dividing line |
| `dt_sub <label>` … `dt_end` | a submenu; the items between belong to it |

`dt_item <label> <key> <command> [args…]` — the key is the letter that picks
it while the menu is open, and the command is run when it is chosen.

A submenu opens with `→` and closes with `←`, and its parent stays on screen
beside it. One level is all there is, because nothing has wanted two. The
control panel uses all of this: its Theme, Wallpaper and Refresh are
submenus with a tick against whichever is current, which is a better thing
than the "Next Theme" it had before.

`dt_dim` exists so a menu can say a thing is unavailable rather than
disappearing it — a menu that changes shape between one moment and the next
is one you cannot learn. Keys
are the app's own business inside its own menu; in the hibr menu the desktop
picks them, skipping any already taken, because Calculator and Clock both
start with a C and a menu where one item cannot be reached is a broken menu.

The hibr menu lists the apps found in a list of folders, sorted by title.
Each app says who it is in its own file, one line near the top:

```sh
command -v dt_app > /dev/null && dt_app calc "Calculator" 16 24 once "±"
```

`dt_app <name> <title> <height> <width> [once|many] [icon]`. `once` means
one window at most: launching it again brings that window forward, shown if
it was hidden. The calculator, the settings, the clock and the games are
`once`; the terminal and the file browser are `many`. The icon is what the
desktop shows for it. The `command -v` guard is what lets the same file run
on its own, where there is no desktop to register with.

A session names the folders and loads them:

```sh
DT_APPDIRS+=("$d/apps")     # after ~/.config/hibr/apps, which is always first
dt_apps
```

A script dropped in `~/.config/hibr/apps` is on the menu at the next start,
and one there with the same file name as a bundled app replaces it. An app
file is sourced from inside a function, so its tables must be declared
`declare -gA`, or they vanish when the loading function returns, exactly as
they would in bash.

## Managing other windows

Most apps mind their own business. One that manages *other* windows — a
control panel, a task switcher — uses these, and does not read the window
manager's own table:

| call | gives |
|---|---|
| `dt_ids` | every window id, hidden ones included, in the order they were opened |
| `dt_title <id>` | its title |
| `dt_hidden <id>` | status: is it minimised |
| `dt_raise <id>` | put it on top and give it the keyboard; un-minimises first |
| `dt_close_focused`, `dt_hide_focused`, `dt_zoom_focused` | act on whatever has focus, for menu items |
| `dt_note <text>` | say something in the middle of the screen until the next key |
| `dt_move <id> <row> <col>` | put a window somewhere, clamped to the screen |
| `dt_resize <id> <h> <w>` | give it a size, clamped to what is usable and what fits |
| `dt_min <id>` | minimise it, or restore it if it already is |
| `dt_del <id>` | close it |
| `dt_new <title> <h> <w> <row> <col> [app]` | open one; the id lands in `$RET` |

The first four exist so that reaching into `DT` is never the answer. An app
that reads it is depending on how the window manager happens to be written
today, and the one that tried it walked straight into a subscript trap for
its trouble.

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
cannot be transparent. Menus nest one level deep. There is no widget library — each app draws its own
buttons, and if the same button code turns up in three apps, *then* it becomes
one. A program in a terminal window hears about the mouse only while a
button is down, never plain motion.

---

See also [full-screen programs](display.md) for the console module the whole
thing draws on, and [modules](modules.md) for `need`, which is how a script
asks for a display and fails cleanly when there is not one.
