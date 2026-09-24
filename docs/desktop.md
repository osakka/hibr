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

A double click on the title bar itself does the same as `□` by default --
`DT_DBLACTION`, a Control Panel entry, can change that to minimise, close, or
nothing at all. It never zooms a fixed window, the same restriction `□`
itself already has.

A minimised window has no pane at all, so nothing draws it and nothing can
click it. Its label on the bar across the top is the only way back — click it
to restore the window. The bar also says how many windows are open and how
many are hidden.

A window casts a shadow, one row down and two columns right, darkening
whatever it falls across -- another window, or the desktop underneath.
`DT_SHADOW`, a Control Panel entry, turns it off; it costs one `console darken`
call per window, and nothing at all when off.

An open menu -- a bar menu, a submenu, a right-click context menu, or a
dropdown's own popup -- casts one the same way, governed by its own
`DT_MSHADOW` instead: a menu is drawn far more often than a window moves, so
whether to pay for its shadow is a separate choice from a window's.

## Icons on the desktop

A clean desktop, down from the top right: **Home**, the mounted **disks** if
`DT_DISKS` wants them (real block devices only -- `/proc/mounts` filtered by
filesystem and by a `/dev/` device, so tmpfs, proc, cgroups and the dozen
other things a kernel mounts never show up as one), and the **trash**, drawn
full when it is not empty. Apps stay in the hibr menu, not here, and nothing
in `~/Desktop` gets an icon just for being there -- the desktop is not a
second copy of a folder. A **double click** opens one: Home and a disk in
Files, the trash in Files on what it holds.

**Choose several**: ctrl and a click adds an icon or takes it away, shift
and a click takes the run from the last one chosen (where the terminal
passes shift-click on; many keep it for their own text selection), and a
drag across the empty desktop draws a band that selects every icon it
touches. With no window focused the keyboard works too: the arrows go from
icon to icon, shift with them extends the selection, space picks or drops
one, ctrl-a takes them all, enter opens what is selected. alt-c copies their
paths.

**Drag** an icon somewhere empty and it stays there, remembered in the
settings file with the rest; drag a selection and it moves together, keeping
its arrangement. Positions are clamped to the screen every time it is laid
out, not just when one is dropped, so a spot picked on a wide screen cannot
put an icon under the bar or off the edge after a resize to a narrower one --
`dt_size` calls `dt_iconlay` on every resize for exactly this. "Clean Up
Icons" on the hibr menu forgets every saved position and lays them out
fresh, for when they have drifted somewhere inconvenient anyway.

**Home and a disk are never something delete or a drag can lose.** They are
a kind of their own, `place`, deliberately left out of every path a move or
a trash can act on -- dragging Home onto the trash icon, or pressing delete
with it selected, does nothing, on purpose: dragging Home into the trash
used to mean the whole home directory, moved. Copying a place's path with
alt-c is still allowed, since that only ever produces text. Icons can be
switched off in Control Panel (`DT_ICONS`), and the disks specifically with
`DT_DISKS`, or with either in the session.

## Files between windows

Drag a file out of a Files window and let go over another: over a Files
window it goes into the folder shown, or into the folder it was let go on;
over a terminal window its path is typed in, quoted for a shell. A drop
**moves**, and **copies with ctrl held**. Nothing is ever put over something
already there, and a folder cannot go inside itself -- the desktop says so
instead. Escape while dragging cancels.

A selection drags whole: every file in it moves or copies, and one note
says how that went -- "Moved 3 items to project", or the first thing
refused and how many more were.

An app starts a drag with `dt_dnd label glyph path...` from its `_mouse`,
and takes one with `_drop`; the moving and copying is `dt_fileop`, once, so
every app refuses the same things. `dt_trash path` moves a file to the trash
at `~/.local/share/Trash`, the freedesktop one, so what is thrown away here
turns up in any other desktop's trash on the machine. `dt_openfile path`
opens a folder in Files, a registered handler if one matches the name's
extension (see **Application handlers** below), and anything else in a
terminal with hvi.

**Right-click** an entry, or empty space in the list, for a menu of its
own: Open, Open With (only when a handler is registered, listing every one
by the program it runs), Cut, Copy, Paste, Info, and Move to Trash --
dimmed for what does not apply to the entry under the pointer, or to `..`.
Cut marks the clipboard so the next Paste moves the files instead of
copying them; Paste itself, and the note it leaves, say which happened.
The desktop's own background and every window's title bar have their own
right-click menus too, unrelated to this one -- Arrange Icons and Change
Wallpaper on the desktop, Move/Resize/Zoom/Hide/Close on a title bar.

In the file browser a click selects one entry; ctrl and a click adds or
takes away; shift and a click takes a run; shift with the arrows carries
the selection along; space marks the entry under the cursor and steps on;
ctrl-a takes everything but `..`. A plain click on something already
selected keeps the rest until the button comes up, so the selection can be
picked up and dragged; the footer says how many are chosen.

The file browser has three views, which `v` cycles and the View menu picks:
a **list** of names; **details**, with size, time and permissions from one
`ls -l` per folder, dropping columns from the right as the window narrows;
and **icons**, a grid the arrows move across and down. Delete moves the
selection to the trash; alt-c copies its path, and alt-v in a Files window
copies the file whose path was pasted into the folder shown.

## Copy and paste

**alt-c** copies, **alt-x** cuts, and **alt-v** pastes, in every window, and
all three are on the **Edit** menu, which belongs to the desktop like Window
does: the same three items everywhere, dimmed where the focused app cannot
do them -- most apps have no Cut, since most things there are not files. An
app offers `<app>_cut` the same way it offers `<app>_copy`, and its
`<app>_paste` is handed a third argument, `cut` or `copy`, so the file
browser is the one thing so far that tells them apart. There is one
clipboard for the whole desktop, and a copy or cut also goes to the
clipboard of the terminal the desktop runs on through OSC 52, so it pastes
into anything else on the machine -- where that terminal allows it, which
most modern ones do and some ask about first.

## Application handlers

`dt_handler ext [term] program args...` says what opens a file of that
extension, from a session's own script -- the same idiom as `dt_app`,
registering something rather than editing a table:

    dt_handler jpg feh
    dt_handler json term less

Without `term` the program is started detached, for anything that draws a
window of its own outside the desktop -- an image viewer, a media player --
which must not be waited for, or the desktop would hang until it closed.
With `term` it runs in a terminal window instead, for anything that reads
one, in place of the hvi a file with no handler opens in. A name with no
extension, or one nothing was registered for, still opens in a terminal with
hvi. The file browser's Open With lists every registered handler by the
program it runs, letting an entry be opened by one other than its own.

In a terminal window, drag with the left button to select; the selection
stays put while more output scrolls past. When the program there has taken
the mouse, hold shift to select anyway, as in xterm. A paste goes in as the
terminal's own paste would, bracketed when the program asked for that. The
calculator copies its answer and keeps only arithmetic from a paste. These
are the only two keys the desktop takes from a program: see the amendment in
[decision 0020](adr/0020-windows-are-drawn-not-composited.md).

## Detaching, and coming back

The shipped session holds itself, so this is already detachable:

    hibr examples/desktop-session.hibr

**ctrl-\\** detaches, and so does **Detach** on the hibr menu; the desktop
keeps running, terminal windows and whatever runs in them included. Log off,
log in somewhere else, and

    hibr examples/desktop-session.hibr --resume

comes back to it exactly as it was, redrawn in full -- the same command,
with one flag, from anywhere. Closing the terminal or losing an ssh
connection detaches too. Running it again *without* `--resume` while it is
already running does not start a second, independent desktop under the same
name; it says so and leaves the running one alone, since a bare rerun is as
likely to be forgetting it is there as it is to mean "and another one."

For more than one, `--session` names which: `--session work` starts (or
refuses, exactly as above) one called "work"; `--session work --resume`
comes back to it specifically, leaving any other named session alone.
Plain `--resume` is short for `--session desktop --resume`.

Underneath, this is a session held by name -- `hold list` shows every one
running, and `hold kill work` ends that one outright. A session gets this
by calling `dt_autohold` once, before `dt_open`, with the name and whether
it was asked to resume:

```sh
opt -r --resume  resume          "Reattach to it if it is already running"
opt -s --session session str=desktop "Which held session this is"
args "$@"
. desktop.hibr
dt_autohold "$session" "$resume"
```

It does nothing, quietly, wherever holding cannot make sense -- no `hold`
module, no real terminal to attach from -- or if starting one fails, so a
session that calls it is never worse off than one that does not: Detach
stays dimmed and the desktop opens in this terminal as it always did. See
[`mods/hold/README.md`](../mods/hold/README.md) for how holding itself
works, and `hold attach desk`/`hold new desk ...` directly if you would
rather manage that yourself, under a name of your own.

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

Every change in the Control Panel window is saved the moment it is made, to
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
`dt_keep NAME` and `dt_save` after changing it; the Control Panel app keeps
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
| `hello_mouse` | `id press\|drag\|release button row col mods` | instead of `_click`, for an app that wants the whole of a press: after the press, its drags and its release come here wherever the pointer goes. `mods` is what was held, such as `shift` or `ctrl` |
| `hello_copy` | `id` | Edit > Copy or alt-c: hand back the selection with `ret`, or fail when nothing is selected |
| `hello_paste` | `id text` | Edit > Paste or alt-v, and a paste from the terminal the desktop runs on |
| `hello_drop` | `id row col move\|copy path...` | files dragged from elsewhere, let go over this one; there may be several |
| `hello_refresh` | `id` | something on disk changed: a move, a copy, the trash |
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
every key and otherwise every `DT_TICK` milliseconds, 2000 by default. A game
calls `dt_want 60` from its `_draw` to be drawn again within 60 ms. The
request lasts one frame, so a game that is paused, hidden or not focused
stops asking and the desktop goes back to idling. Step on the clock, not on
frames: frames also come with every key, and a snake that moved once per
frame would sprint while an arrow is held. `$EPOCHREALTIME` is the clock.

**A window that is done asks to be closed, from its own `_draw`, with
`dt_wantclose id`.** The terminal calls it once the program inside has
exited cleanly, instead of leaving `[exited 0 — close this window]` sitting
there for someone to close by hand; it still shows the message and waits to
be closed for a nonzero exit, since that is worth seeing before the window
goes. The close itself happens once the frame that asked is fully drawn, not
from inside `_draw` -- calling `dt_del` there would pull the window's pane
out from under the border the frame still has left to draw over it.

## Widgets

A checkbox and a dropdown, for the two things almost every settings screen
needs. Neither keeps state of its own -- they draw what an app tells them to
and hand back a tag when their region is clicked, the same contract as
`_click` itself; the app updates its own state, under its own window id, the
same as anywhere else.

`dt_wclear id` forgets a window's widget regions -- call it at the top of
`_draw`, before drawing any widget, since a scrolled list moves them and
last frame's regions must not answer this frame's click.

`dt_check id row col on label [tag]` draws `[x] label` or `[ ] label`.
`dt_wdrop id row col width value [tag]` draws a value padded to `width`
columns with a caret after it: `value ▾`. Both take pane-relative
coordinates, the same ones `_draw` and `_click` already use, and both
default `tag` to `label`/`value` if left off.

`dt_hit id row col` answers which widget, if any, is at a click -- an app's
`_click` asks it first, and falls back to its own per-row logic when it
comes back empty:

```sh
hello_click() {
	local id=$1 r=$2 c=$3 tag

	tag := dt_hit "$id" "$r" "$c"
	case $tag in
	sound) HS[$id]=$((1 - ${HS[$id]:-0})) ;;
	esac
}
```

A dropdown that offers more than a couple of values wants a real list rather
than a click-to-cycle: `dt_droplist id row col callback val1 val2...` opens
one, anchored just under the `dt_wdrop` that asked for it. `col` should be
the widget's own left column, not wherever inside it was clicked -- `dt_wcol
id row tag` answers that, looking up the same region `dt_hit` just matched,
so the popup lines up under the dropdown itself regardless of where across
it the click landed. Built the next frame the same one-frame lag `dt_want`
and a right-click's context menu both already have, choosing a value calls
`callback id value`; dismissing it calls nothing. It is a context menu with
nowhere on the desktop it belongs to, which is exactly what a dropdown is --
it reuses the same context-menu machinery a right-click already builds on,
rather than a second popup system
of its own.

## Control Panel panes

`panel.hibr` only hosts panes; it has none of its own. A pane down the left
picks what shows on the right, System 7's Control Panels folder rather than
one long scrolling list -- which is also what a world map (a Date & Time
pane's own body) needs room for that a shared list of rows never could.

Each pane is its own file, found in `CP_PANEDIRS` the same way apps are
found in `DT_APPDIRS`: a session adds `examples/control-panel` (and its own
`~/.config/hibr/control-panel`, the default) and calls `cp_panes`, which
sources every file and sorts the result by title -- the same trick
`dt_appnames` uses for the hibr menu, so load order and file names never
decide what the picker shows first.

A pane calls `cp_pane name title icon` to register, then is one of two
shapes. Most are a **row list**: `name_rows id` fills `CP[$id]` the same way
an app fills its own state and returns the count with `ret`; a `set` row
gets `name_do id key dir` to change it and, for a dropdown rather than a
checkbox, `name_drop id row col key` to open one with `dt_droplist`. A
`key`-kind row (a shortcut) and a `win`-kind row (an open window) need no
callback at all -- the host handles both generically, the same four rows
Shortcuts and Windows already are.

A pane that needs a body of its own -- a world map does not fit two columns
of text -- defines `name_draw id h w bx` and `name_click id row col`
instead, in the same `w$id` coordinates every other `_draw`/`_click` pair
already uses; `bx` is where its own body starts, since it shares the window
with the pane list to its left. Either shape may also define `name_key` for
keys the host's own row/pane navigation does not already handle, and the
body shape `name_wheel`.

The five bundled panes -- Appearance, Behaviour, Shortcuts, App Shortcuts,
Windows -- are ordinary files under `examples/control-panel` themselves, not
special-cased in `panel.hibr`: a file of your own with the same pane name
replaces one, the same rule `DT_APPDIRS` already has for apps.

## The apps

In `examples/apps/`, each one also a file you can read in a sitting:

| app | what it is |
|---|---|
| `files` | a file browser, with a scrollbar and the wheel |
| `calc` | a calculator, and `hibr calc.hibr '3 * 4'` on its own |
| `panel` | Control Panel: a picker of panes (see below), and a list of the other windows |
| `term` | a shell in a window. Each window is its own pty and its own session. The wheel or `shift-pageup` scrolls back, and a program that asks for the mouse gets it |
| `snake` | arrows turn, `p` pauses. It speeds up as it grows |
| `mines` | Minesweeper, 9 by 9 with ten mines. `space` or a click opens, `f` or a right click flags, and opening a number with its flags placed opens what is round it |
| `bricks` | after Arkanoid: the arrows or a click move the bat, `space` serves. Where the ball lands on the bat sets its angle |
| `about` | the version, the machine's hostname and kernel, and a CPU and a memory bar read live from `/proc` -- no forking, the same way `mods/sysinfo` reads them in C |
| `tasks` | every process, name, CPU% and memory, sorted by either (`c`, `m`); `x` ends the selected one, `shift-x` forces it |

A focused terminal gets every key except `f10`, so a program inside can have
`escape`; `f10` is the way back to the menu bar.

A new terminal's cursor is a block until the program inside sets its own with
DECSCUSR (`CSI Ps SP q`), as some editors do to mark insert mode with a
different shape. What a new one starts with is `DT_CURSOR` (block, underline
or bar), a Control Panel entry like any other; only the terminal that has focus
shows a cursor at all.

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

The hibr menu lists the apps found in a list of folders, sorted by title --
an app in a subfolder becomes a submenu named after that folder, sorted in
among the flat ones by the folder's own name, to any depth. Each app says
who it is in its own file, one line near the top:

```sh
command -v dt_app > /dev/null && dt_app calc "Calculator" 16 24 once "±"
```

`dt_app <name> <title> <height> <width> [once|many] [icon] [fixed]`. `once`
means one window at most: launching it again brings that window forward,
shown if it was hidden. The calculator, the settings, the clock and the games
are `once`; the terminal and the file browser are `many`. The icon is what
the desktop shows for it. The `command -v` guard is what lets the same file
run on its own, where there is no desktop to register with.

There are two window types, and the seventh argument picks between them.
Leave it off and a window is fully manipulable: it can be moved, resized (by
dragging its corner, the Window menu, or the keyboard's Resize grab) and
zoomed to fill the screen. Say `fixed` and none of that exists for it -- no
maximise button, no grow box drawn at its corner, a drag on that corner does
an ordinary body click instead of resizing, and Resize and Zoom are both
dimmed on its Window menu, whether reached from the menu bar or a right-click
on its own title bar -- for a board or a grid with one sensible size; the
games and About hibr use it. There is no half-fixed window: resizability
follows `fixed` everywhere at once, so a window is never left with a working
drag-resize but a dimmed menu item, or the reverse.

A session names the folders and loads them:

```sh
DT_APPDIRS+=("$d/apps")     # after ~/.config/hibr/apps, which is always first
dt_apps
```

A script dropped in `~/.config/hibr/apps` is on the menu at the next start,
in a subfolder of your own if you like -- `dt_apps` looks at every depth, not
just the top -- and one there with the same file name as a bundled app
replaces it, wherever in the tree either one sits. An app file is sourced
from inside a function, so its tables must be declared
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
