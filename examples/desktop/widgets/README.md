# `widgets/` — what an app draws with

The desktop's widget library: the controls an app puts in its own window —
a checkbox, a dropdown, a slider, a text field, a scrollbar — and the hit
regions that tell it which one a click landed on. `desktop.hibr` sources every
file here, in no particular order, before any app is loaded.

| file | what it offers |
|---|---|
| `hit.hibr` | `dt_wclear`, `dt_wreg`, `dt_hit`, `dt_wcol`: the regions widgets register as they draw, and which one a point is in |
| `check.hibr` | `dt_check`: `[x] label` |
| `dropdown.hibr` | `dt_wdrop`, `dt_droplist`, `dt_dropcontext`: a value with a `▾`, and the popup of choices under it |
| `slider.hibr` | `dt_slider`: a track with a marker, changed with the arrows |
| `text.hibr` | `dt_textdraw`, `dt_textkey`: a one-line text field, and one key applied to it |
| `scrollbar.hibr` | `dt_scrollbar`: a vertical track and thumb |

## The contract

A widget **keeps no state of its own**. The app holds the value — under its
own window id, never in a global, since two windows of one app must not share
one — passes it in to draw, and changes it when `dt_hit` names the widget's
tag in its `_click`. So a widget is a function of what it is given, drawn
fresh every frame, and there is nothing to create, destroy or keep in step.

Every widget draws into the window's own pane (`console put -p "w$id"`), in
the coordinates the app itself draws in: row 0 is the title bar, and a click
reaches `_click` in those same coordinates. An app calls `dt_wclear "$id"` at
the top of its `_draw`, before drawing any widget, so last frame's regions
cannot answer this frame's click.

## What makes a file a library

hibr has no separate notion of a library; a library is a script that keeps
four rules, and every file here keeps them:

1. **It only defines.** Functions, and `declare -gA` or plain assignments for
   the state they share. Nothing runs, draws or reads input while it is being
   sourced, so the order files are sourced in never matters.
2. **It is safe to source twice.** A second source redefines the same
   functions and resets nothing that matters.
3. **It owns a prefix.** Everything here is `dt_`, the desktop's own; a
   library of your own should pick one that nothing else uses — see
   `CLAUDE.md` on how a name that collides with the shell's own is silently
   bound to the wrong function.
4. **It says what it offers**, in a one-line comment above each function and
   a row in a table like the one above.

`tests/540-examples.t` parses every file here and fails when a function name
is defined twice anywhere across the desktop and its parts.
