# Writing for the desktop

Everything you open on hibr's desktop is a hibr script, and so is
everything that manages it: the window manager, the Control Panel and each
of its panes, the Control Strip and each of its modules, every screen
saver, and the session that starts it all. So writing for the desktop is
writing a hibr file with a few functions in it, and putting it in a folder
the desktop already reads. Nothing registers, nothing compiles, nothing
needs root.

| to make | put a file in | how it is written |
|---|---|---|
| an application | `~/.config/hibr/apps/` | [Writing an app](#writing-an-app) |
| a Control Panel pane | `~/.config/hibr/control-panel/` | [Control Panel panes](#control-panel-panes) |
| a Control Strip module | `~/.config/hibr/control-strip/` | [Control Strip modules](#control-strip-modules) |
| a screen saver | `~/.config/hibr/savers/` | [Screen savers](README.md#screen-savers) |
| a theme or a colour scheme | `~/.config/hibr/themes/`, `~/.config/hibr/colours/` | [Themes and colours](README.md#themes-and-colours) |
| the whole session | anywhere; run it with `hibr` | [Writing a session](#writing-a-session) |

A file there is found at the next Refresh Desktop -- the desktop's
right-click menu -- or the next start, and one with the same name as an
installed or shipped file replaces it, which is the easiest
way to change something the desktop came with: copy it there and edit the
copy ([ADR 0045](../../docs/adr/0045-a-thing-has-a-scope.md)). Anything you
write may be shared through a catalogue later, so it follows the one rule
every installable does -- it reaches only what every scope has, and asks
before it uses another app ([ADR
0046](../../docs/adr/0046-an-installable-reaches-only-what-every-scope-has.md),
and the section of that name below). [The catalogue](../../docs/catalogue.md#publishing-an-app)
says how to publish one.

How it all works underneath -- the event loop, how a frame is drawn, the
full table of what each callback is handed -- is
[ARCHITECTURE.md](ARCHITECTURE.md). Using the desktop is
[README.md](README.md), and a first hour with it is
[the tour](../../docs/desktop.md).

## Writing a session

A session sources the window manager, defines or loads some apps, opens
windows and runs the loop:

<!-- not run: needs a terminal: it opens the desktop -->
```sh
#!/usr/bin/env hibr
. "${0%/*}/desktop.hibr"

fn hello_draw(id, h, w, row, col) {
	console put -p "w$id" 1 2 "Hello from a window."
}

dt_open || exit 1
dt_new "Hello" 8 34 5 10 hello
dt_run
dt_close
```

`dt_new title height width row col [app]` makes a window and puts it on top.
Its id lands in `$RET`, so `id := dt_new ...` works if you want to keep it.

A callback declares everything it is called with -- `_draw` is handed the
window id, its body's height and width, and where it sits -- because a
declared function refuses an argument it has no name for. The whole list
is in [ARCHITECTURE.md](ARCHITECTURE.md#an-app-is-a-prefix-not-a-class);
the undeclared `hello_draw() { ... "$1" ...; }` form still works too.

## Writing an app

**An app is a prefix, not a command.** An app called `hello` provides any of
these, and the window manager calls only the ones that exist:

| function | called with | when |
|---|---|---|
| `hello_open` | `id` | once, when the window is made |
| `hello_draw` | `id inner_h inner_w row col` | every frame; `row col` is where the window is on screen, for `console fill`, which takes screen coordinates. Under the `none` and `curved` frames (no border) the window is one row less and its whole width, and the app's row 1, column 1 is still where its content starts -- the pane's origin is moved, not the app |
| `hello_key` | `id key` | a key, while this window has focus |
| `hello_click` | `id row col button` | a click, in the same coordinates the app draws in; `button` is `left`, `middle` or `right` |
| `hello_mouse` | `id press\|drag\|release button row col mods` | instead of `_click`, for an app that wants the whole of a press: after the press, its drags and its release come here wherever the pointer goes. `mods` is what was held, such as `shift` or `ctrl` |
| `hello_copy` | `id` | Edit > Copy or alt-c: hand back the selection with `ret`, or fail when nothing is selected |
| `hello_paste` | `id text` | Edit > Paste or alt-v, and a paste from the terminal the desktop runs on |
| `hello_drop` | `id row col move\|copy path...` | files dragged from elsewhere, let go over this one; there may be several |
| `hello_refresh` | `id` | something on disk changed: a move, a copy, the trash |
| `hello_wheel` | `id up\|down row col` | the wheel, over this window whether or not it has focus |
| `hello_hover` | `id row col` | the pointer moving over this window with no button held, in the coordinates it draws in; `-1 -1` when it leaves. Only where hovering is on (Control Panel > Mouse > Hover), and a frame is drawn only when the cell changes |
| `hello_scroll` | `id key top` | a scrollbar drawn with a key, held and dragged -- set the offset |
| `hello_close` | `id` | once, when the window closes |

Draw with `console put -p "w$id" row col text` — the pane is the window, so
row 0 is its top border and the body starts at row 1, column 1. Drawing
outside the pane is clipped, and the frame is redrawn over the top of
whatever the app wrote, so an app cannot damage its own border.

**A click arrives in those same coordinates**, so a key the app drew at row 4
is clicked at row 4. There is no separate body coordinate system to convert
between; the window manager deals with row 0 itself, so an app never sees a
click on its own title bar.

**Where the pointer is in pixels** is there too, for an app that draws
pixels -- a picture, a page, a film: `DT_PX` and `DT_PY` say where the last
mouse report was, in pixels of the screen counted from zero, and are empty
when the terminal reports cells only (Gitea #370). No callback is handed
them, since each declares what it takes; the cell it is told is the one that
pixel is in. A window's own pixel is `DT_PX` less its column times the cell
width, which `console gfx` answers (`kitty 8 16`).

**A window that draws its own pointer** -- a game -- asks for the terminal's
to be hidden over it with `dt_pointer "$id" none` (Gitea #373), and gives it
back with `dt_pointer "$id"`. The desktop hides it only while the pointer is
over that window's content, the window has focus, no menu is open and
hovering is on, and gives it back anywhere else, on a frame, a menu or the
bar. `dt_ptrcan` says whether any display of this desktop can hide its
pointer at all; most terminals cannot, and a display that cannot simply
keeps its pointer. Call both behind `dt_has`, since a desktop older than
0.99.311 has neither.

**An app reaches only what every scope has** ([ADR 0046](../../docs/adr/0046-an-installable-reaches-only-what-every-scope-has.md)).
The catalogue installs an app into `~/.local/share/hibr/apps/`, which is read
before the shipped copy, so an app has to work wherever it lands -- not only
in this tree, where `apps/` and `lib/` happen to be siblings. Three rules,
each checked on every gate by `tests/deps.py` and `tests/scopes.py`:

- Anything the desktop ships is reached through **`$DT_HERE`**, its own
  directory -- `"$DT_HERE/lib/pim.hibr"`, `"$DT_HERE/themes"` -- and never
  through `${BASH_SOURCE%/*}/..`, which is right in exactly one of four
  places. The app's own files are under **`${DT_SRC%.hibr}/`**.
- A **library** several apps share lives in `lib/` and marks itself with a
  line `NAME_LIBDONE=1`; the desktop loads every marked library before any
  app. An app may still guard its own source of it, for when it is sourced
  on its own.
- **Another app** is reached only behind a check in the same function:
  `dt_has fb_scan && fb_scan ...` when the app can simply do less, or
  `dt_needs files fb_scan || return 0` when it cannot, which tells the person
  *"files is not installed. Find it in the Catalogue."* Links between apps
  are optional integrations, never dependencies: either side can be
  installed or removed and the other keeps working.

The Control Panel and the saver framework are the exception: every pane and
every saver is built on them, so they say `# platform:` in their headers,
count as part of the desktop, and are never installed apart from it.

**A window that animates asks for its next frame.** The desktop redraws on
every key, resize and terminal output, and otherwise sleeps: there is no
refresh rate. Anything that shows the time or moves on its own must ask, or
it stops changing. A game
calls `dt_want 60` from its `_draw` to be drawn again within 60 ms. The
request lasts one frame, so a game that is paused, hidden or not focused
stops asking and the desktop goes back to idling. Step on the clock, not on
frames: frames also come with every key, and a snake that moved once per
frame would sprint while an arrow is held. `$EPOCHREALTIME` is the clock.

**A window that plays something keeps the screen saver away by answering
`<app>_awake id`.** Watching is not typing, so a video would otherwise be
covered by the saver halfway through, and then locked under it. The desktop
asks only at the moment the saver is due, and only windows that still
exist: one that succeeds puts the idle clock back a whole period, and the
lock waits with it, since the lock's own timer starts with the saver. So
answer from what is true now -- the YouTube player asks its player whether
it is playing, not paused and not minimised -- and keep nothing: there is
no hold to set and no hold to forget, and a frame costs nothing while a
video plays. Control Panel > YouTube > Keep the Screen Saver Away While
Playing makes its answer always no.

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

A single-line editable field is the same shape, split in two rather than
one, since typing has to change what an app's own state holds and neither
of the widgets above ever does that: `dt_textdraw id row col width text
cursor focused` draws the text, padded or truncated to `width`, and --
while `focused` -- the character at `cursor` shown inverted. `dt_textkey
text cursor key` applies one key to that pair and returns the new one as
`"text cursor"`, for the app's own state to hold; left, right, home, end,
backspace and delete all do what they say, any other single character is
inserted at the cursor, and anything else -- enter, escape, tab -- comes
back unhandled (status 1, text and cursor unchanged), for the app's own
`_key` to decide instead. The Files app's Rename and Get Info windows
(`programs/desktop/apps/files.hibr`) are the bundled example: a name typed there
is not applied until enter, unlike a checkbox, which always acts the
moment it is clicked.

A field has a selection, which the widget keeps for the focused field and
nothing in an app has to: shift with an arrow, home or end draws it out,
typing replaces it, backspace and delete remove it, and a press and a drag
on the focused field set it with the mouse -- the window manager does that
by typing into the app's own `_key`. The desktop's Copy, Cut, Paste and
Select All reach a field in a window whose app has none of its own; an app
that does keep its own `_copy` and `_cut` for a field takes the selection
with `t := dt_fieldsel` and `t := dt_fieldcut id`, each failing when
nothing is selected.

`dt_scrollbar id row0 col h total shown pos` draws a vertical scrollbar:
a track of `│` over `h` rows starting at `row0`, one cell shown as the
thumb `█` at a position proportional to how far through `total` the view
already is. `shown` is how many of `total` are visible at once; nothing is
drawn when everything already is. The file browser's own list view and a
terminal's scrollback (`DT_TERMBAR` in the Terminal pane, off by default) both use
this one widget rather than each drawing their own.

Give it a key as an eighth argument -- `dt_scrollbar ... pos list` -- and
it can be held and dragged: a press on the track puts the view where it
lands, and the window manager carries the drag to the release, whether or
not the window has a `_mouse` of its own, telling the app each step with
`name_scroll id key top`. The app keeps the offset, as it always does:

    fn hello_scroll(int id, key, int top) { HELLO[$id]["top"]=$top; }

A ninth argument of `1` makes only the thumb live, for a bar drawn on a
split divider whose other cells still drag the divider. `dt_hscrollbar`
takes a key the same way. A list with a selection brings its view to the
selection with `dt_scrollview top total vis sel seen` -- only when the
selection has moved since it last did, so the wheel, the page keys and a
held scrollbar are not pulled back on the next frame.

`dt_hit id row col` answers which widget, if any, is at a click -- an app's
`_click` asks it first, and falls back to its own per-row logic when it
comes back empty:

<!-- not run: an app's callback; the desktop calls it -->
```sh
fn hello_click(id, r, c, btn) {
	local tag

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

## Menus for an app

An app declares them by providing `<app>_menus`, which calls `dt_menu`,
`dt_item` and `dt_sep` — the same shape `opt` uses to declare a command line,
and the same prefix contract as `_draw` and `_key`. An app with no `_menus`
simply has none, and the desktop's show instead.

<!-- not run: an app's callback; the desktop calls it -->
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

Every app's bar reads the same: the hibr menu, File, Edit, the app's own
menus, Window -- System 7's order. An app opens its menus with `dt_menu
"File"` and calls `dt_editmenu` before its second menu, which puts Edit
second rather than last; `tests/540-examples.t` fails an app that does not.
An app that defines no `_menus` at all -- Clock, Control Panel, About This
Computer, Screenshot, and every dialog -- is given `File > Close` and Edit
by the window manager, so there is no window whose bar is a different
shape.

<!-- not run: an app's callback; the desktop calls it -->
```sh
notes_menus() {
	dt_menu "File"
	dt_item "Save"   s  notes_save
	dt_editmenu
	dt_menu "Format"
	dt_item "Wrap"   w  notes_wrap
}
```

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

<!-- not run: a line from an app's file, read when the desktop loads it -->
```sh
command -v dt_app > /dev/null && dt_app calc "Calculator" 16 24 once "±" fixed
```

`dt_app <name> <title> <height> <width> [once|many] [icon] [fixed] [hidden] [bare]`. `bare`
is a window with no frame, title bar or shadow: the window's own colour,
which the app sets in `DT[$id]["bg"]` and `["fg"]`, a strip across the top
a shade darker with a close box on it -- drag the strip to move it -- and a
grow mark in the corner. Stickies are bare. A window can also lose its
chrome while it is open: `DT[$id]["chrome"]=none` hands the app every cell,
its `_draw` given the whole window's size and drawing from row and column
0; any press on it moves it, a double click reaches its `_click` with
`double` as the button, and a right click its `_context`. The YouTube mini
player is one. An app's own `_click` can ask `dt_isdbl "$id"` whether a
press is the second of a double click. `once`
means one window at most: launching it again brings that window forward,
shown if it was hidden. The calculator, Control Panel, the clock and the games
are `once`; the terminal and the file browser are `many`. The icon is what
the desktop shows for it. `hidden` keeps an app off the hibr menu and out
of App Shortcuts while `dt_launch` can still open it by name -- Rename and Get
Info, which only ever open on a particular file. The `command -v` guard is
what lets the same file run on its own, where there is no desktop to
register with.

There are two window types, and the seventh argument picks between them.
Leave it off and a window is fully manipulable: it can be moved, resized (by
dragging any of its four corners, the Window menu, or the keyboard's Resize
grab) and zoomed to fill the screen. Say `fixed` and none of that exists for
it -- no maximise button, no grow box drawn at any corner, a drag on one does
an ordinary body click instead of resizing, and Resize and Zoom are both
dimmed on its Window menu, whether reached from the menu bar or a right-click
on its own title bar -- for a board or a grid with one sensible size; the
games and About This Computer use it. There is no half-fixed window: resizability
follows `fixed` everywhere at once, so a window is never left with a working
drag-resize but a dimmed menu item, or the reverse.

A session names the folders and loads them:

<!-- not run: part of a session, which needs the desktop -->
```sh
DT_APPDIRS+=("$d/apps")     # after ~/.config/hibr/apps, which is always first
dt_apps
```

A script dropped in `~/.config/hibr/apps` is on the menu at the next Refresh
Desktop or the next start (`dt_rescan`, which the catalogue also triggers
through the control socket's `rescan` after every install),
in a subfolder of your own if you like -- `dt_apps` looks at every depth, not
just the top -- and one there with the same file name as a bundled app
replaces it, wherever in the tree either one sits. An app file is sourced
from inside a function, so its tables must be declared
`declare -gA`, or they vanish when the loading function returns, exactly as
they would in bash.

## Managing other windows

Most apps mind their own business. One that manages *other* windows — a
task switcher, a window list — uses these, and does not read the window
manager's own table:

| call | gives |
|---|---|
| `dt_ids` | every window id, hidden ones included, in the order they were opened |
| `dt_title <id>` | its title |
| `dt_hidden <id>` | status: is it minimised |
| `dt_raise <id>` | put it on top and give it the keyboard; un-minimises first |
| `dt_close_focused`, `dt_hide_focused`, `dt_zoom_focused` | act on whatever has focus, for menu items |
| `dt_note <text>` | a corner-stacked toast, gone on its own after a timeout |
| `dt_notify <text> <cmd> [args...]` | the same, but clicking it runs `cmd` |
| `dt_notep <prio> <text>`, `dt_notifyp <prio> <text> <cmd>...` | either at a priority: `low`, `normal` (what the two above give), `high`, `urgent` |
| `dt_move <id> <row> <col>` | put a window somewhere, clamped to the screen |
| `dt_resize <id> <h> <w>` | give it a size, clamped to what is usable and what fits |
| `dt_min <id>` | minimise it, or restore it if it already is |
| `dt_del <id>` | close it |
| `dt_new <title> <h> <w> <row> <col> [app]` | open one; the id lands in `$RET` |
| `dt_dlgat <h> <w> [margin]` | where a dialog opens: over the focused window, on its display, made to fit it -- in `DT_DLGR`, `DT_DLGC`, `DT_DLGH`, `DT_DLGW` |

A dialog is placed with `dt_dlgat`, never centred on `DT_ROWS` and
`DT_COLS`: with several displays the middle of the whole screen is between
them, and a dialog opened from a window on one landed on another
(Gitea #293).

```text
dt_dlgat 10 50
id := dt_new "Rename" "$DT_DLGH" "$DT_DLGW" "$DT_DLGR" "$DT_DLGC" rn
```

The first four exist so that reaching into `DT` is never the answer. An app
that reads it is depending on how the window manager happens to be written
today, and the one that tried it walked straight into a subscript trap for
its trouble.

**A key handler returns non-zero for a key it does not want.** That is how
`alt-tab` still cycles windows while your window has focus:

<!-- not run: an app's callback; the desktop calls it -->
```sh
fn hello_key(id, key) {
	case $key in
	up)   N=$((N - 1)) ;;
	down) N=$((N + 1)) ;;
	*)    return 1 ;;
	esac
}
```

An app that never writes a `_key` function cannot swallow a key at all, which
is the reason the app name is a prefix rather than one function answering a
verb. See [decision 0020](../../docs/adr/0020-windows-are-drawn-not-composited.md).

## Control Panel panes

Each pane is its own file, found in `CP_PANEDIRS` the same way apps are
found in `DT_APPDIRS`: a session adds `programs/desktop/control-panel` (and its own
`~/.config/hibr/control-panel`, the default) and calls `cp_panes`, which
sources every file and sorts the result by title -- the same trick
`dt_appnames` uses for the hibr menu, so load order and file names never
decide what the picker shows first.

A pane calls `cp_pane name title icon` to register, then is one of two
shapes. Most are a **row list**: `name_rows id` fills `CP[$id]` the same way
an app fills its own state and returns the count with `ret`; a `set` row
gets `name_do id key dir` to change it and, for a dropdown rather than a
checkbox, `name_drop id row col key` to open one with `dt_droplist`. A
`key`-kind row (a shortcut) needs no callback at all -- the host handles it
generically, the same rows Shortcuts already uses.

A pane that needs a body of its own -- a world map does not fit two columns
of text -- defines `name_draw id h w bx` and `name_click id row col`
instead, in the same `w$id` coordinates every other `_draw`/`_click` pair
already uses; `bx` is where its own body starts, since it shares the window
with the pane list to its left. Either shape may also define `name_key` for
keys the host's own row/pane navigation does not already handle, and the
body shape `name_wheel`. `datetime.hibr` is the one bundled example of this
shape. It holds the menu bar clock's format (a dropdown of examples, or
Custom… for any strftime format up to 24 columns wide), a time zone finder,
and Set Date & Time where the machine owns its clock -- in a container it
says "set by the host" instead. Changing the zone or the clock runs sudo in
a terminal window of its own, so the password prompt and the result stay on
screen. The small world map under
them, and the zone1970.tab lookup that places a mark on it, are copied
verbatim from `examples/traceroute.hibr` rather than redone -- CLAUDE.md's
own trap about that map is not to adjust one by eye, and the same holds for
drawing a second one from scratch.

The bundled panes -- Date & Time, Displays, Keyboard and Mouse under
Hardware; Appearance, Control Strip, Desktop, File Types, Notifications,
Shortcuts and Windows under Desktop; and About This Computer, Files, Task Manager and
Terminal under Apps -- are ordinary files under
`programs/desktop/control-panel` themselves, not special-cased in
`panel.hibr`: a file of your own with the same pane name replaces one, the
same rule `DT_APPDIRS` already has for apps. `cp_pane name "Title" icon`
registers one under Desktop; a fourth word, `hardware` or `app`, lists it
under Hardware or Apps. An
app's pane should say so when its app is not loaded -- `cp_absent w "Title"`
gives it the one row that does -- rather than show nothing.

## Control Strip modules

A module calls `cs_module name title [width]` to register -- `title` is
what is drawn in its own brackets, and (once there is one) what a tooltip
would say; `width` defaults to the title's own length, since the title is
what is drawn unless a module wants something narrower. It then defines
`name_draw row col` and `name_click row col`, in the same absolute screen
coordinates -- `col` is where its own content starts, one column inside
its brackets, however the strip is currently docked, sized or scrolled;
neither takes a window id, since there is never more than one instance of
a strip module. `CS_MODDIRS` (default `~/.config/hibr/control-strip`) and
`cs_modules` find and load them, the same shape `DT_APPDIRS`/`dt_apps`,
`CP_PANEDIRS`/`cp_panes` and `DT_SYSDIRS`/`dt_sysapps` already are.

### A strip module or a bar item?

The menu bar takes items too, since 0.99.129: `dt_baritem name text
[cmd...]`, drawn left of the notification icon, and clicked it runs the
words it was given; `dt_bariwheel name fn` has the wheel over it call `fn
up` or `fn down`, which is how the speaker turns the volume (Gitea #306);
and `dt_bariright name cmd...` gives a right click words of its own, so the
speaker's left click can be the slider and its right click the menu
(Gitea #310). An item's menu opens with `dt_barmenu name who`, built by
`<who>_context`; a small panel of its own -- the slider -- with
`dt_baripop name who h w`, placed the same way and drawn over the frame
like a menu, through `<who>_popdraw row col h w`, `<who>_popmouse act row
col` (press, drag, release, or up and down for the wheel, counted from the panel's
own top left; a press held inside it gets the drag wherever it goes) and,
for keys, `<who>_popkey key`, which answers 1 for a key it has no use for.
It closes on escape, a press outside it, or any key it turns down, which
then goes where it would have. The two look similar and are for different
things.

A **strip module** is a glyph and a click, and **the person decides where it
sits** -- they dock the strip, size it, scroll it, drag it about. It is a
place to put a control.

A **bar item is a state that changes**, laid out by the bar beside the clock,
where the eye already goes for "what is going on": messages waiting, a sync
in flight, a vault left unlocked. The desktop's own notification count and
its "a newer hibr is on disk" mark have always lived there, and this is the
same shelf opened to apps.

So: a button is a strip module. A number or a light is a bar item.

Its text is a value the app **sets when it changes**, never a callback the
bar asks for per frame -- that is what keeps a bar of ten items free on a
frame where none of them moved, and it is why there is no `name_draw` here
to match the strip's. An empty text takes the item off, which is one call
rather than two. `wm/vault.hibr` is the first user: a key on the bar for as
long as the vault is unlocked, and nothing while it is shut.

The dropdown reuses `dt_droplist`'s own widget/`dt_dropcontext` machinery
-- the same one a Control Panel row's own dropdown opens -- through
`cs_dropopen row col cb values...`, which reaches `dt_context_open`'s
absolute screen coordinates directly rather than translating a widget's
row/col through a window the strip does not have. It opens below the
module's own row if the choices fit on the screen there, above it
otherwise -- there is no reason to prefer one over the other beyond what
actually fits, and the strip's own default position, most of the way down
the screen, means most of its dropdowns open upward in practice.

## Controlling a running desktop

A program can ask a held desktop about its displays and windows, and move
them, through its control socket -- what Blit, a terminal server that draws
hibr straight to the screens over KMS with no X11 or Wayland, uses instead
of typing keys at it. `desktop ctl` is the client:

<!-- not run: needs a running held desktop -->
```text
$ desktop ctl displays
{"ok":true,"console":"kitty 8 16","displays":[{"name":"left","row":1,"col":0,"rows":47,"cols":160,"primary":true,"gfx":"kitty","wallpaper":"pixels"},{"name":"right","row":1,"col":160,"rows":47,"cols":120,"primary":false,"gfx":"none","wallpaper":"cells"}]}
$ desktop ctl windows
{"ok":true,"windows":[{"id":3,"title":"Files [~]","app":"files","row":4,"col":10,"h":20,"w":60,"workspace":1,"sticky":false,"hidden":false,"focused":true}]}
$ desktop ctl move 3 right
{"ok":true,"id":3,"display":"right","row":1,"col":160,"h":20,"w":60}
$ desktop ctl move 3 nowhere
{"ok":false,"error":"no-display","detail":"no display called nowhere"}
```

`displays` says, for each display, what the desktop has recorded its
terminal draws (`gfx`: `kitty`, `sixel`, `none`, `unknown` for one that
did not answer, `unasked` for one from before displays were asked, `self`
for a desktop that is not held) and whether the wallpaper goes to it as
`pixels` or `cells`; `console` is the session's own console's answer, the
protocol and the pixel size of a cell. Those are the three things that
decide a picture, so a wallpaper in half blocks says here which one is the
reason.

`state` is what the desktop is waiting on, read-only: its `pid`, whether
the saver (`saving`) or the lock (`locked`) is up, each watcher it runs for
sound, the network or Bluetooth (`fd`, the `pty` it runs on, `cmd`), and
`console`, the descriptors the console wakes for -- one marked `:muted` was
found ready with nothing reading it and is no longer woken for, which the
log names too.

`stats` is what the desktop has done since it was last reset (`desktop ctl
stats reset`), counted the same way on every platform, so two machines can
be compared by asking each, which their load cannot do, since both run
everything else besides (Gitea #343). It answers:
- `secs` since the reset;
- `cpu`: the desktop's own milliseconds (`self_ms`), those of the programs it
  ran and has waited for (`children_ms`), and the two as a percentage of a
  core (`pct`), all from the `times` builtin;
- `wakes` of the loop, `frames` drawn and `events` read;
- `rereads`: how often each of the sound, network and Bluetooth layers asked
  its server for everything again;
- while `timing` is on, from a reset until `desktop ctl stats stop`: each
  ticker's `runs` and `ms`, and `draw_ms`;
- `console_bytes`: what the display's flushes have written since it opened,
  and `displays`: for a held desktop, each attached terminal's `name`, the
  `bytes` hold has sent it since it attached and in how many `frames` --
  what crosses the wire, and what a leaner encoding is judged by (Gitea
  #353). Neither is reset by `reset`.

A re-read's program runs are inside its ticker's time. What it cannot see
it says, in `unseen`: hold's server, which is the emulator and renderer a
held desktop draws through, and the terminal or Blit are other processes.
The same procedure on each machine is: reset, leave the desktop idle a
minute, read.

The socket is `${TMPDIR:-/tmp}/hibr-ctl-<uid>/<session>.ctl`, beside hold's
own and outlasting a logout the same way, in a folder only its owner can
enter; a connection from another user is refused. One request per
connection, a line of JSON -- `{"op":"displays"}`, `{"op":"windows"}`,
`{"op":"session"}`, `{"op":"move","id":3,"display":"right"}`,
`{"op":"resize","id":3,"h":20,"w":60}`, `{"op":"focus","id":3}`,
`{"op":"rescan"}`, `{"op":"state"}` -- and one
line back, `{"ok":true,...}` or `{"ok":false,"error":...,"detail":...}`
with the errors `bad-request`, `unknown-op`, `no-window`, `no-display`,
`detached`, `bad-geometry` and `unauthorized`. `desktop ctl -j '{...}'`
sends one as it is, and the status is 0 for an answer that is ok, 1 for one
that is not, 2 when no desktop answered. Requests are served between
frames, through the same operations the menus use: the desktop's own state
is the only state. A held desktop always has a control socket; an unheld
one only with `DT_CTL=1` (and `DT_CTLPATH` to put it somewhere). Windows
are numbered by the desktop; displays are named as `desktop --join --name`
named them.

`{"op":"signedin"}` (`desktop ctl signedin`) says the person has just
signed in at the login screen. The login screen resumes the desktop already
held, and the password given there is the one its lock would ask for, so
the lock goes, the saver stops and the idle clock starts again. The answer
says what it found: `"was":"locked"`, `"saving"` or `"idle"`. The login
sends it itself (`login/start.hibr` sets `HIBR_SIGNEDIN`, which
`dt_autohold` turns into this request before it attaches). Anything running
as the desktop's own user can send it, which is the same reach as every
other request here; the lock is for someone at a display who has not
signed in.
