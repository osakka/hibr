# `widgets/` — what an app draws with

The desktop's widget library: the controls an app puts in its own window —
a checkbox, a dropdown, a slider, a text field, a scrollbar, a button — and the hit
regions that tell it which one a click landed on. `desktop.hibr` sources every
file here, in no particular order, before any app is loaded.

| file | what it offers |
|---|---|
| `hit.hibr` | `dt_wclear`, `dt_wreg`, `dt_hit`, `dt_wcol`: the regions widgets register as they draw, and which one a point is in |
| `check.hibr` | `dt_check`: `[x] label` |
| `dropdown.hibr` | `dt_wdrop`, `dt_droplist`, `dt_dropcontext`: a value with a `▾`, and the popup of choices under it |
| `slider.hibr` | `dt_slider`: a track with a marker, changed with the arrows |
| `text.hibr` | `dt_textdraw`, `dt_textkey`, `dt_textpaste`: a one-line text field, one key applied to it, and a paste into it |
| `textarea.hibr` | `tb_*`: a multi-line editor -- lines, cursor, selection, undo and redo, soft wrap -- keeping its buffer in `TB` under the window id, the one widget that keeps state, freed by the app's `_close` with `tb_free`. Stickies uses it whole; the markdown editor uses its editing and draws its own way |
| `scrollbar.hibr` | `dt_scrollbar` and `dt_hscrollbar` draw a track and thumb; `dt_scrollfit`, `dt_scrollsel` and `dt_scrollmove` are the behaviour behind them, and `DT_SCROLLWHEEL` is how far one wheel notch moves anything |
| `bidi.hibr` | `bd_row`, `bd_col`, `bd_at`, `bd_glyph`, `bd_put`: one row of a line holding right-to-left text in display order, through `uni map` -- the line one paragraph, each row reordered on its own -- with where each character went, so a cursor or a click stays logical; fails at once for plain text or with bidi off, so a caller keeps its plain path. The textarea and Write draw through it |
| `split.hibr` | `dt_split`, `dt_splitbar`, `dt_splitmove`: a window cut in two at a divider the mouse drags, its column kept in a variable the app names so it can be kept as a setting |
| `button.hibr` | `dt_button`, `dt_buttons`, `dt_dlgbtns`: filled push buttons with a Turbo Vision shadow, one or a row of them, or a dialog's centred along its bottom; `dt_focus` and `dt_focuskey`: moving focus through fields and buttons with tab, shift-tab and the arrows |

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
5. **It declares its parameters**: `fn dt_check(id, r, c, on, label, tag)`,
   not `local id=$1 r=$2 ...`. The signature is the documentation a caller
   reads, a wrong number of arguments fails the call rather than drawing
   something plausible in the wrong place, and binding costs less than the
   `local` line it replaces.

A library of your own can live anywhere on `PATH` and be loaded by name --
`. mylib` -- since `.` looks a bare name up there before the current
directory, the way bash does.

`tests/540-examples.t` parses every file here and fails when a function name
is defined twice anywhere across the desktop and its parts.
