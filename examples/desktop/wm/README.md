# `wm/` — the window manager, one concern to a file

`desktop.hibr` is the window manager's table of contents: it asks for a
display, then sources these files in the order it names them, then the widget
library in [`../widgets/`](../widgets/README.md). Each file here defines
functions and sets state; none of them runs anything while it loads, so each
can be read on its own, and the order is only for reading.

| file | what it holds |
|---|---|
| `state.hibr` | the state every part shares: focus, dragging, sizing, the keyboard grab, drag and drop |
| `settings.hibr` | what the user chooses — keys, theme, wallpaper, cursor, shadows, frames — and `dt_keep`, `dt_load`, `dt_save` |
| `session.hibr` | `dt_open`, `dt_run` (the event loop, asleep until something needs a frame), `dt_close` |
| `windows.hibr` | `dt_new`, `dt_del`, what an app may ask about windows, moving, sizing, zooming, minimising, the grab |
| `frame.hibr` | a window's border, title and title-bar buttons |
| `draw.hibr` | `dt_draw`: one frame of the whole desktop, bottom of the stack upwards |
| `wallpaper.hibr` | the background: a glyph, or an image fitted to the screen |
| `input.hibr` | `dt_event` and `dt_mouse`: where each key and click goes, and dispatching the desktop's own shortcuts |
| `keys.hibr` | shortcuts: what each action is called, capturing a key for one, clearing it, the keys the desktop keeps, and moving a key rather than letting two actions share it |
| `menus.hibr` | menus: `dt_menu`/`dt_item` for an app, drawing them, moving through them |
| `bar.hibr` | the right-hand side of the menu bar: notification icon, clock, application menu |
| `context.hibr` | menus that open at the pointer |
| `apps.hibr` | `dt_app`, finding apps in their folders, the hibr menu, `dt_launch` |
| `files.hibr` | drag and drop between windows, move, copy, the trash, the clipboard |
| `handlers.hibr` | `dt_handler`: what opens a file, by its extension |
| `icons.hibr` | the desktop's own icons: home, disks, trash |
| `hold.hibr` | staying detachable, and the displays attached to a held session |
| `standby.hibr` | displays that wait to be joined (`--standby`) and wait again when let go, and displays blanked: joined but dark and out of use |
| `notify.hibr` | `dt_note`, `dt_notify`: stacked notifications and their history |
| `confirm.hibr` | `dt_confirm`: a question with a yes and a no |
| `strip.hibr` | the Control Strip |

The split was made by moving text, not by rewriting it: every function's
definition is what it was in the single `desktop.hibr` it came from, and once
loaded the two leave exactly the same functions and variables behind. A new
file here must be added to `desktop.hibr`'s own list, by name — a table of
contents that lists itself cannot quietly grow a part nobody meant to load.
