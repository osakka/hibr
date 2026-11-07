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
| a menu bar item | `~/.config/hibr/menubar/` | [A strip module or a bar item?](#a-strip-module-or-a-bar-item) |
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

`dt_new title height width row col [app [arg]]` makes a window and puts it
on top; `arg` is handed to the app's `_open`.
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
| `hello_open` | `id arg` | once, when the window is made; `arg` is what `dt_new` or `dt_launch` was given, often empty |
| `hello_draw` | `id inner_h inner_w row col` | every frame; `row col` is where the window is on screen, for `console fill`, which takes screen coordinates. Under the `none` frame (no border) the window is one row less and its whole width, and the app's row 1, column 1 is still where its content starts -- the pane's origin is moved, not the app |
| `hello_key` | `id key` | a key, while this window has focus |
| `hello_click` | `id row col button` | a click, in the same coordinates the app draws in; `button` is `left`, `middle` or `right` |
| `hello_mouse` | `id press\|drag\|release button row col mods` | instead of `_click`, for an app that wants the whole of a press: after the press, its drags and its release come here wherever the pointer goes. `mods` is what was held, such as `shift` or `ctrl` |
| `hello_copy` | `id` | Edit > Copy or alt-c: hand back the selection with `ret`, or fail when nothing is selected |
| `hello_paste` | `id text op` | Edit > Paste or alt-v, with `op` `cut` or `copy`, and a paste from the terminal the desktop runs on, as a copy |
| `hello_drop` | `id row col move\|copy path...` | files dragged from elsewhere, let go over this one; there may be several |
| `hello_refresh` | `id` | something on disk changed: a move, a copy, the trash |
| `hello_wheel` | `id up\|down\|left\|right row col` | the wheel, over this window whether or not it has focus; left and right are a sideways wheel or a tilt |
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

**What a click would land on lights under the pointer**, wherever hovering
is on (Gitea #370). The desktop works out which region the pointer is over
from what `dt_wreg` registered -- the same regions `dt_hit` answers a click
with -- so every button, checkbox, dropdown and slider lights by itself. A
list lights its rows by registering each one and drawing it with
`dt_hotpen id tag fg bg [attr...]`, which picks the well colour while the
pointer is over that row and `bg` otherwise; `dt_hot id tag` asks the same
question. A list whose clicks are worked out by its own arithmetic registers
its rows with `dt_hotreg id r c w tag` rather than `dt_wreg`, so they light
without `dt_hit` ever answering a click with one. A selected row keeps its
own pen, so do not ask for it. A window
whose `_dirty` says it is clean is drawn again when what is lit in it
changes, and a pointer moving within one region draws nothing.

**A tooltip** says what something does once the pointer has rested on it
(Control Panel > Mouse > Tooltips, on by default). The desktop's own
glyphs speak for themselves: a title bar's buttons, the Control Strip's
modules, the bar's items. An app gives one of its own regions a tooltip with
`dt_tip id tag text`, beside the `dt_wreg` or `dt_hotreg` that registers it.
Give one to a glyph with no word beside it, not to a labelled button or a
menu row, which already say what they do.

`dt_cellpx id r c cw ch` works out a window's own pixel for a click: where
in the cell `r c` the pointer was, as `x y`
in a cell of `cw` by `ch` -- the app's own measure, which need not be the
terminal's -- and the middle of the cell when there is no pixel. The PDF
Reader starts a selection there, and X Screen hands it to the X program.

**A window that draws its own pointer** -- a game -- asks for the terminal's
to be hidden over it with `dt_pointer "$id" none` (Gitea #373), and gives it
back with `dt_pointer "$id"`. The desktop hides it only while the pointer is
over that window's content, the window has focus, no menu is open and
hovering is on, and gives it back anywhere else, on a frame, a menu or the
bar. `dt_ptrcan` says whether any display of this desktop can hide its
pointer at all; most terminals cannot, and a display that cannot simply
keeps its pointer. Call both behind `dt_has`, since a desktop older than
0.99.311 has neither.

**A window that turns with the mouse** -- a game on a relative mouse --
asks to **hold** the pointer with `dt_pointerlock "$id" on` (Gitea #379) and
gives it back with `dt_pointerlock "$id" off`. `dt_ptrlockcan` says whether
any display can (Blit, from 0.2.42; nothing else). On answers whether the
asking was done -- it fails without focus or a display that can, and
unless the person pressed a key or clicked in the last two seconds, so ask
from a click or a key and never from a timer, a job or a restore; a refusal
is logged as `pointer lock: refused ...` -- not whether the display agreed.
A report that arrives after the lock has ended is dropped, never handed to
a window as a key (0.99.416). While it is held every mouse report goes to the
window's `_mouse`, whatever is under the pointer, with `rel` where the
modifiers go: `rel BTN DY DX` for movement, `none` when no button is held,
and the press, release or wheel it was with its own deltas, signed, in the
display's raw counts. The window hears `unlock none 0 0` when the lock ends
for any reason -- the Release Pointer shortcut (ctrl-f10 by default,
Control Panel > Shortcuts), focus moving, a menu or a bar item's
panel opening, the saver, the window closing, the desktop ending, or the
display refusing or taking it back -- so treat `unlock` as the truth, even
straight after an on. Call both behind `dt_has`, since a desktop older than
0.99.365 has neither.

**A window that wants the whole display** -- a game, a film -- calls
`dt_fullscreen "$id" on|off|toggle` (Gitea #405): every cell of the
display the window is on, the bar's row included, with no bar, frame,
shadow or Control Strip over it. A display has one at most, and
`dt_isfull ID` says whether a window is. Its app is told whenever it changes,
by itself or by the desktop, through an optional `<app>_fullscreen ID on|off`.
Every press in it is the app's. It ends when the window closes, hides or
leaves the workspace, when the saver or the lock comes up, on the window
losing the focus or a menu opening where Control Panel > Windows > Full Screen
Ends says `click`, and on **Leave Full Screen** (`DT_KEYS["fsleave"]`, ctrl-f11 by default, Control Panel >
Shortcuts), which is taken before any app sees a key; Restart Desktop puts
the window back first, so the restored state is where it was. While it is
full screen a note of high or urgent priority is one line along the
display's bottom edge, and others are not drawn over it. Call it behind
`dt_has dt_fullscreen`, since a desktop older than 0.99.375 has none --
Boxer falls back to `dt_zoom`.
Since 0.99.403 the desktop does this itself for any window that is not
of a fixed size, on its **Full Screen** key (`DT_KEYS["fullscreen"]`,
alt-enter by default) and the Window menu's row, so an app needs no key
of its own for it; one that answers alt-enter in its `_key` still has it
first.

**A window that is text throughout** -- a terminal -- asks for the I-beam with
`dt_pointer "$id" text`, and a field drawn with `dt_textdraw` gets it with no
call at all (Gitea #394). Text is the one shape shown whether or not the
window has focus.

**A window playing video** says so with `dt_live "$id" on` while it plays,
and `dt_live "$id"` when it stops (Gitea #397). Frames woken by output are
held Control Panel > Desktop > Frames For Output apart (70 ms), which is
right for a terminal and halves a video; while a live window is on show they
are held Frames For Video apart instead, 33 ms by default. Behind `dt_has`:
a desktop older than 0.99.353 has no `dt_live`.

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

The Control Panel, the saver framework and the bar's own four items (the
application menu, the clock, the notification icon and the workspace
numbers) are the exception: everything else is built on them, so they say
`# platform:` in their headers, count as part of the desktop, and are never
installed apart from it.

**Every callback returns inside the frame, and slow work is a job.** Every
app runs in the desktop's one process, between frames: while a `_draw`,
`_key`, `_click`, `_mouse`, `_wheel`, `_idle` or ticker runs, nothing else
does -- not the other windows, not the bar, not a video in another window,
on any display. So a callback does only what it can finish in a few
milliseconds. Anything that may take longer -- scanning `/proc` or a large
folder, starting a program (`$( )` and `:= program` both start one),
reading from the network, decoding a file -- runs as a job, which is a
child of the desktop with every function of yours already loaded:

<!-- not run: needs a running desktop -->
```sh
fn hello_scanned(int n, int st) {
	local id=${DTJ[$n]["tag"]} f
	f := dt_jobfile "$n" out
	[ "$st" = 0 ] && read -r -d '' HELLO["$id"]["list"] < "$f"
	dt_want 0
}
n := dt_job scan "$id" hello_scanned hello_scan "$dir"
```

The callback runs in a later frame, after the job has ended, with its
number and status; what it printed is in `dt_jobfile n out`, the job's
`TAG` -- here the window it is for -- is `${DTJ[$n]["tag"]}`, and
`wm/jobs.hibr` says the rest. `dt_job` is the one way to do this: write no
`( ... ) &` of your own.

When what the work produces is variables -- a pane reading what it shows
for the first time -- `dt_jobvars KIND TAG CALLBACK FUNCTION VAR...` runs
`FUNCTION` as a job and brings the named variables back before `CALLBACK n
status` runs; until then the pane draws "reading". Login Manager and
Administrative Access read their first state this way.

`hibr --explain` holds a file to this before it is written down: a `$( )`,
a backquote or a `<( )` in a callback, or in anything a callback calls in
the same file, is a program started every time it runs, and is reported
unless the line ends with `# fork: reason`, the reason naming how often it
can happen ("once a desktop", "every 30 s while open").
`tests/850-lint.t` lints every file under `programs/`, so a new one fails
the suite.

The desktop times every callback (`wm/budget.hibr`). One that takes longer
than a frame, 50 ms, is a note in the log -- `slow: win:files took 70 ms`
-- for whoever wrote it. One that takes longer than 150 ms has stalled
every display, which a person sees: it is `held: win:files held the
desktop 230 ms`, and the suites fail on that line, so it cannot ship.
Nothing is done to the app itself: a stall is for whoever wrote it to
find, never for the person using it to pay for. `desktop ctl stats` shows each
callback's total and its longest single run, `max_ms`; read the second,
since a callback that is slow once a second is invisible in an average.

**A window that is expensive to draw says when it has changed.** The desktop
asks `<app>_dirty id` before it draws a window: succeed when something in it
has changed, fail when it is exactly as last drawn, and the window is left
alone with the screen keeping its cells. An app without one is drawn every
frame, which is always correct, so offer it once a draw costs more than a
millisecond and only if it can answer truly -- a wrong "clean" leaves a
stale window, which is worse than a slow one. Since 0.99.410 a clean window
is left even when it overlaps another, unless a window drawn under it in the
same frame overlaps it, so the answer is relied on more often than before.
Count what changes your drawing -- a key, a scan, the size, the focus, a
scroll -- in a generation of your own and compare it, as `tasks_dirty` and
`term_dirty` do. A window whose draw repaints every cell inside its frame can
also offer `<app>_paintsall id`, succeeding while it does; the desktop then
draws only its content, not its face and frame again, while nothing else has
drawn over it. The Terminal does while its program runs.

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
from inside `_draw`; since 0.99.393 `dt_del` called from a `_draw`, `_dirty`
or `_idle` does the same, marking the window and closing it after the frame.
A job's callback runs between frames, where `dt_del` closes at once.

## Widgets

A checkbox and a dropdown, for the two things almost every settings screen
needs. Neither keeps state of its own -- they draw what an app tells them to
and hand back a tag when their region is clicked, the same contract as
`_click` itself; the app updates its own state, under its own window id, the
same as anywhere else.

`dt_wclear id` forgets a window's widget regions -- call it at the top of
`_draw`, before drawing any widget, since a scrolled list moves them and
last frame's regions must not answer this frame's click.

`dt_check id row col on label [tag]` draws an on/off choice and its label,
in the style the person chose under Appearance > Checkboxes: a box (`[x]`
and `[ ]`), a knob, a block or a tick. `dt_wdrop id row col width value
[tag]` draws a value padded to `width` columns with a caret after it:
`value ▾`. Both take pane-relative coordinates, the same ones `_draw` and
`_click` already use, and both default `tag` to `label`/`value` if left off.

A single-line editable field is the same shape, split in two rather than
one, since typing has to change what an app's own state holds and neither
of the widgets above ever does that: `dt_textdraw id row col width text
cursor focused [paste]` draws the text, padded or truncated to `width`, and --
while `focused` -- the character at `cursor` shown inverted. `paste` is how
a paste into it folds lines: empty, the default, turns a line break into a
space, so a name pasted over two lines stays two words; `url` removes a
line break and the blanks around it, as a browser's address bar does, for
a field that holds an address a terminal program may have wrapped
(0.99.418). `dt_textkey
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
by typing into the app's own `_key`. A double click selects the word under
it and a triple click the whole field. A press on a field that does not
have the keys is still the app's, to give it them; once the app draws that
field focused, the window manager puts the cursor where the press landed,
unless the app selected the field on giving it the keys, as an address bar
does (0.99.422). The desktop's Copy, Cut, Paste and
Select All reach a field in a window whose app has none of its own; an app
that does keep its own `_copy` and `_cut` for a field takes the selection
with `t := dt_fieldsel` and `t := dt_fieldcut id`, each failing when
nothing is selected.

An address field with suggestions under it, as a browser's address bar
has, is the same field with a list (`widgets/omnibox.hibr`, 0.99.421).
`dt_omnidraw id row col width text cursor focused [paste]` draws the field,
with `dt_textdraw`'s arguments; `dt_omnilist id h`, called **last** in
`_draw` with `_draw`'s own `h`, draws the list under it, so nothing drawn
afterwards covers it. `dt_omnikey id cb text cursor key` takes the place of
`dt_textkey`: whenever the text changes it calls `cb id text`, which answers
the rows to offer through `ret`, one a line as `title<tab>address<tab>kind`
with kind `history`, `bookmark` or `search` -- synchronously, once a key, so
it reads what it already holds rather than starting a program. Up and down
put a row's address in the field, escape puts back what was typed and shuts
the list, and enter shuts it and comes back unhandled for the app to go;
the top row's address, when it begins with what was typed, completes the
field after the cursor, selected, so the next character replaces it.
`a := dt_omnihit id row col` answers the address of a row a press landed
on, and shuts the list on a press anywhere else but the field, so an app
calls it first with every press -- from `_mouse` if it has one, since that
takes the place of `_click`. `dt_omniselect id` selects the whole field on
its next draw, for the moment it is given the keys; `dt_omnisig id` is a
word to put in a `_dirty` key; `dt_omnifree id` belongs in `_close`. The
list is drawn inside the window rather than as a popup, because a popup is
a menu and a menu takes every key, where here typing goes on reaching the
field.

`dt_scrollbar id row0 col h total shown pos` draws a vertical scrollbar:
a track of `│` over `h` rows starting at `row0`, one cell shown as the
thumb `█` at a position proportional to how far through `total` the view
already is. `shown` is how many of `total` are visible at once; nothing is
drawn when everything already is. The file browser's own list view and a
terminal's scrollback (`DT_TERMBAR` in the Terminal pane, on by default) both use
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
is given File > Close and Edit, as below.

<!-- not run: an app's callback; the desktop calls it -->
```sh
fn hello_menus() {
	dt_menu "File"
	dt_item "Close"      w  dt_close_focused
	dt_editmenu
	dt_menu "Hello"
	dt_item "Set Alarm"  a  hello_alarm
	dt_item "12 Hour"    h  hello_ampm
}
```

| declarator | makes |
|---|---|
| `dt_item <label> <key> <cmd…>` | an ordinary item; an empty key means no letter |
| `dt_mark <label> <key> <on> <cmd…>` | the same, with a tick when `on` is not `0` |
| `dt_dim <label>` | an item that is there and cannot be chosen |
| `dt_sep` | a dividing line |
| `dt_sub <label> [key]` … `dt_end` | a submenu; the items between belong to it |

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
fn notes_menus() {
	dt_menu "File"
	dt_item "Save"   s  notes_save
	dt_editmenu
	dt_menu "Format"
	dt_item "Wrap"   w  notes_wrap
}
```

A submenu opens with `→` and closes with `←`, and its parent stays on screen
beside it -- every menu of the chain, outermost first, since a submenu may
hold another: the hibr menu's Applications holds Office, Internet and the
rest the same way.

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
press is the second of a double click, and a list names the row as well --
`dt_isdbl "$id" "$row"` -- so a later click on the row already selected is
only a click; `dt_dblclear` forgets a first click when the list changes
under it. Files and the wallpaper picker open this way. `once`
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

## Settings and words

**A setting an app keeps is one the Control Panel shows.** A variable the
app reads -- with its default set the way the window manager's are,
`HELLO_SIZE="${HELLO_SIZE-12}"` -- is written to the settings file with
everything else once the app names it:

<!-- not run: a line from an app's file, read when the desktop loads it -->
```sh
command -v dt_keep > /dev/null && dt_keep HELLO_SIZE HELLO_WRAP
```

From then on `dt_save` writes it and `dt_load` reads it back, and a change
reaches every window at once. Every kept name has to appear in a Control
Panel pane -- usually the app's own, brought with it as a `# part:` --
or `tests/540-examples.t` fails: nothing a person can choose should be
reachable only by editing the settings file. A pane whose app is installed
and not loaded in this session says so with `cp_absent`. Because the file
keeps every kept setting, defaults included, changing a default in code
does not reach anyone who has saved, and nothing in the desktop migrates a
saved value (ADR 0054).

**Words a person reads go through the language.** The desktop translates
at its widgets: a menu row, a checkbox's label, a button, a pane's row and
a window's title are looked up for you, and `dt_note` and `dt_notep`
translate their text when given format arguments. Text an app draws itself
with `console put` goes through `t := dt_tr "Text"`, or `dt_tr "%s files"
"$n"` with arguments, and `dt_trn n one many` picks a plural. The key is
the English sentence with `%s` where a value goes -- never the value
itself, since a key holding somebody's folder name can have no
translation; `tools/strings.py --check` fails on one. The catalogues are
JSON files under `lang/`, and `lang/strings.txt` is the list of every key
(ADR 0032).

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
| `dt_new <title> <h> <w> <row> <col> [app] [arg]` | open one; the id lands in `$RET` |
| `dt_dlgat <h> <w> [margin]` | where a dialog opens: over the focused window, on its display, made to fit it -- in `DT_DLGR`, `DT_DLGC`, `DT_DLGH`, `DT_DLGW` |
| `dt_dlgfor <id> <h> <w> [margin]` | the same, over window `id` rather than the focused one -- for a question a window asks when it may not have focus, as a confirm does |

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
	up)   HELLO[$id]["n"]=$((HELLO[$id]["n"] - 1)) ;;
	down) HELLO[$id]["n"]=$((HELLO[$id]["n"] + 1)) ;;
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
body shape `name_wheel`. Date & Time, Displays, File Types and About Me
are bundled examples of this shape. `datetime.hibr` holds the menu bar
clock's format (a dropdown of examples, or
Custom… for any strftime format up to 24 columns wide), a time zone finder,
and Set Date & Time where the machine owns its clock -- in a container it
says "set by the host" instead. Changing the zone or the clock runs in a
terminal window of its own -- through polkit, or `please` on macOS -- so
the password prompt and the result stay on screen. The small world map under
them, and the zone1970.tab lookup that places a mark on it, are copied
verbatim from `examples/traceroute.hibr` rather than redone -- CLAUDE.md's
own trap about that map is not to adjust one by eye, and the same holds for
drawing a second one from scratch.

The bundled panes -- Date & Time, Displays, Keyboard and Mouse under
Hardware; Appearance, Control Strip, Desktop, File Types, Menu Bar,
Notifications, Shortcuts, Wallpaper, Windows and the rest under Desktop;
and Files, Terminal and Stickies under Apps -- are ordinary files under
`programs/desktop/control-panel` themselves, not special-cased in
`panel.hibr`: a file of your own with the same pane name replaces one, the
same rule `DT_APPDIRS` already has for apps. `cp_pane name "Title" icon`
registers one under Desktop; a fourth word, `hardware` or `app`, lists it
under Hardware or Apps. An
app's pane should say so when its app is not loaded -- `cp_absent w "Title"`
gives it the one row that does -- rather than show nothing.

## Control Strip modules

A module calls `cs_module name title [width]` to register -- `title` is
what is drawn in its own brackets, and what its tooltip says; `width` defaults to the title's own length, since the title is
what is drawn unless a module wants something narrower. It then defines
`name_draw row col` and `name_click row col`, in the same absolute screen
coordinates -- `col` is where its own content starts, one column inside
its brackets, however the strip is currently docked, sized or scrolled;
neither takes a window id, since there is never more than one instance of
a strip module. `CS_MODDIRS` (default `~/.config/hibr/control-strip`) and
`cs_modules` find and load them, the same shape `DT_APPDIRS`/`dt_apps`,
`CP_PANEDIRS`/`cp_panes` and `DT_SYSDIRS`/`dt_sysapps` already are.

The strip is off until Control Panel > Control Strip > Show Control Strip
turns it on, and that pane's Modules list shows every module loaded, to
tick off or drag into another order (`CS_HIDE`, `CS_ORDER`). A module that
already has a setting of its own for whether it shows -- Prayer Times' In
the Control Strip -- names it with `cs_moduleswitch name VAR [fn]`, and the
list's checkbox is then that setting, with `fn` run after it changes; its
`name_shown`, which leaves it out when it says no, reads the same variable,
so there is one switch and not two. A module with more than one thing to
press -- the workspace numbers -- finds the column the press landed on in
`CS_CLICKC`, a global rather than a third argument every module would have
to declare.

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

An item can live in a file of its own in `menubar/` -- `~/.config/hibr/menubar`
for one of yours -- read the way an app is, and installed from the catalogue
as a kind of its own (Gitea #392). Register it while the file is read, so a
rescan that finds the file gone knows whose it was and takes it off:

<!-- not run: defines a bar item for a desktop to load -->
```sh
app battery "The battery on the menu bar" 1.0
dt_baritem battery ""
dt_barilabel battery "Battery"
dt_baristop battery battery_stop
dt_ticker battery_tick
```

The desktop's own clock, notification icon, workspace numbers and
application menu are written exactly so, in `menubar/`, which makes them
the place to read how `dt_baripin`, `dt_baridraw`, `dt_baripress` and
`dt_barhook` are used.

Control Panel > Menu Bar > Menu Bar Items lists every item by its
`dt_barilabel`, to untick or drag into another place (`DT_BARHIDE`,
`DT_BARORDER`). An item that already has a switch of its own in its pane
names it with `dt_bariswitch name VAR [fn]` -- the speaker's `DT_SNDBAR`,
the next prayer's `DT_PBAR` -- and the list's checkbox is then that
setting, with `fn` run after it changes: one switch for one thing, never a
second beside it. A switch reads the same kept as on/off or 1/0
(`dt_swon`, `dt_swflip`).

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

## Locations: somewhere Files goes that is not a folder here

A server's folder, the inside of an archive: anything Files should list
and open that is not on this machine is a **location** (`wm/locations.hibr`,
ADR 0053). Register a scheme and a prefix, and write the prefix's
functions; Files never names the scheme.

<!-- not run: a desktop file's own line; the desktop sources it -->
```sh
dt_location dav rdav
dt_location archive arc archives
```

A third word names the app that brings the location, for something the
catalogue can take away: the location is there while that app is
registered, so removing it takes the location with it at once.

| function | what it does |
|---|---|
| `_ls DIR` | a map of what DIR holds -- each entry's `name`, `dir` (1 or 0), `size`, `mtime`, and `etag` and `type` where it has them -- or status 1 with `DT_WHY`. **The one that is required** |
| `_ok` | whether it can be reached now; `DT_WHY` if not |
| `_norm DIR` | DIR tidied: no trailing slash, `..` taken up; a path on this machine when `..` leaves the location |
| `_top DIR` | its own root, which has no `..` |
| `_title DIR` | DIR as a window's title shows it |
| `_can OP [PATH]` | `write`, `mkdir`, `rename`, `delete`, `open`: status 0 for yes; `leave`, whether its top has a parent on this machine, which `..` goes to; `browse`, whether PATH can be shown at all, which a browse opener asks |
| `_open PATH [APP]` | open a file: fetch a copy here and open that, since no program reads a scheme |
| `_mkdir PATH`, `_mv OLD NEW`, `_delete PATH...` | `DT_WHY` on failure |
| `_copyplan OP DST SRC...` | run as a job when a copy starts: print `g PATH` for what can go, `c PATH` for what is already at DST, `e PATH`, a tab and why for what cannot. The copy window asks about the `c`s |
| `_copyrun STATUS STOP REPLACE OP DST SRC...` | run as a job to do it, reporting as `copy run` does: the one line in STATUS, `ok SRC`, tab, `DST`, or `err SRC`, tab, why, on standard output, and `stopped` once STOP exists. REPLACE names what may be written over, a name a line. Bytes moved in the file in hand can go in STATUS`.p`, added to the line's own count, so a transfer that blocks can still move the bar |

Return a map from `_ls` the way a function returns anything: let the
builtin that lists fill the slot with `:=` and end with `ret -k`, as
`rdav_ls` in `wm/remote.hibr` does. A location that cannot write says no
to `_can` and leaves those functions out; Files dims what would need them
and turns a move out of it into a copy. `dt_unlocation` takes it away,
and a location whose `_ls` is not loaded is no location at all, so an
extension removed leaves Files as it was.

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
  callback's `runs`, its total `ms` and its longest single run `max_ms`
  (tickers, `win:<app>`, `idle:<app>`, `input:<app>`, `flush`), and
  `draw_ms`;
- while `timing` is on, `woke`: what ended each wait -- `input` (a key or
  a report), `io` (something watched being ready: a program's output, the
  browser's pipe) or `timer:<who>`, named for whoever asked for the soonest
  frame (`win:<app>`, `idle:<app>`, `input:<app>`, a ticker, `held` for a
  frame held back for a program's output, or `desktop`); and `drawn_why`:
  for each app, why its windows were drawn rather than left as they were --
  `left` (they were not), `dirty` (its `_dirty` said so), `nodirty` (it has
  none), `overlap` (a window drawn under it this frame overlaps it; since
  0.99.410 a window that only overlaps another, with nothing under it
  drawn, can be `left`), `hover`, `moved`, `noapp` (a window with no app),
  `forcedraw` (`DT_FORCEDRAW` is on), or
  `full:<cause>` for a frame drawn whole (`arranged`, `overdrew`, `popup`,
  `other`), counted once a window a frame (0.99.405);
- while `timing` is on, the frame's parts as tickers named `part:<name>`
  (`arrange`, `wall`, `windows`, `menus`, `barstrip`, `overlays`), and each
  step of a wallpaper repaint as `part:wall.<step>` (`fit`, `blank`,
  `picture`, `halfblocks`, `pattern`, `icons`; since 0.99.414) -- `part:wall`
  counts every frame, a kept wallpaper included, so the steps are what say
  what one repaint costs;
- `img`: how often a picture under the text -- the wallpaper -- was
  kept rather than placed again, and each reason it was not (`img stats`,
  since 0.99.415); reset with the rest;
- `console_bytes`: what the display's flushes have written since it opened,
  and `displays`: for a held desktop, each attached terminal's `name`, the
  `bytes` hold has sent it since it attached and in how many `frames` --
  what crosses the wire, and what a leaner encoding is judged by (Gitea
  #353). Neither is reset by `reset`.

Blit's performance panel (Blit ADR 0203) shows a section of eight lines
from the desktop on the screen it is shown on, and asks for it on the frame
socket: while it does, `wm/hud.hibr` turns the same timing on (and off
again when the panel goes, unless a `reset` here took it over) and sends,
once a second, `fps=` and `wakes=`, `cpu=`, `slow=` (the longest callback
of the second), `woke=` (its two commonest causes), `drawn=` (windows drawn
/ left, and the commonest reason), `frame=` (average and longest draw),
`held=` (stalls in the last minute) and `flush=` with `live=` while a window
plays. The switch is Blit's: Control Panel > Blit > Performance panel and
Hibr section. A desktop with no frame socket never asks and pays nothing;
one with a socket and the panel off asks every five seconds, a frame each.

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
`{"op":"resize","id":3,"h":20,"w":60}`, `{"op":"focus","id":3}` (or
`raise`), `{"op":"rescan"}`, `{"op":"state"}`, `{"op":"stats"}` (with
`"reset":true` or `"stop":true`), `{"op":"signedin"}` -- and one
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
