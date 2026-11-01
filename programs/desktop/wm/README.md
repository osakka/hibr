# `wm/` — the window manager, one concern to a file

`desktop.hibr` is the window manager's table of contents: it asks for a
display, then sources these files in the order it names them, then the widget
library in [`../widgets/`](../widgets/README.md). Each file here defines
functions and sets state; none of them runs anything while it loads, so each
can be read on its own, and the order is only for reading.

| file | what it holds |
|---|---|
| `state.hibr` | the state every part shares: focus, dragging, sizing, the keyboard grab, drag and drop |
| `glyphs.hibr` | every character the desktop draws that is not plain text, by name (`GL[vline]`), from the glyph set chosen in Appearance |
| `settings.hibr` | what the user chooses — keys, theme, wallpaper, cursor, shadows, frames — and `dt_keep`, `dt_load`, `dt_save` |
| `lang.hibr` | the language the desktop speaks and whether it orders right-to-left text itself; `dt_tr` itself is `lib/tr.hibr`, shared with the login screen (ADR 0032) |
| `session.hibr` | `dt_open`, `dt_run` (the event loop, asleep until something needs a frame), `dt_close` |
| `budget.hibr` | every callback timed one way: `slow:` past 50 ms and `held:` past 150 ms in the log, and the totals and longest runs `desktop ctl stats` reads (ADR 0049); nothing is ever done to the app |
| `windows.hibr` | `dt_new`, `dt_del`, what an app may ask about windows, moving, sizing, zooming, minimising, the grab |
| `workspaces.hibr` | the virtual workspaces: which one is shown, which a window is on, sticky windows |
| `tile.hibr` | a workspace the window manager lays out itself, Window > Tile Workspace and Make Main |
| `place.hibr` | where a window the desktop opens lands: smart, cascade or centre |
| `frame.hibr` | a window's border, title and title-bar buttons |
| `draw.hibr` | `dt_draw`: one frame of the whole desktop, bottom of the stack upwards |
| `wallpaper.hibr` | the background: a glyph, or an image fitted to the screen |
| `input.hibr` | `dt_event` and `dt_mouse`: where each key and click goes, and dispatching the desktop's own shortcuts |
| `keys.hibr` | shortcuts: what each action is called, capturing a key for one, clearing it, the keys the desktop keeps, and moving a key rather than letting two actions share it |
| `menus.hibr` | menus: `dt_menu`/`dt_item` for an app, drawing them, moving through them |
| `bar.hibr` | the menu bar's items and how they are laid out: `dt_baritem`, `dt_baripin`, `dt_barilabel` and the rest; the items themselves, the desktop's own four included, are files in `menubar/` |
| `context.hibr` | menus that open at the pointer |
| `apps.hibr` | `dt_app`, finding apps in their folders, the hibr menu, `dt_launch` |
| `files.hibr` | drag and drop between windows, move, copy, the trash, the clipboard |
| `fullscreen.hibr` | a window taking every cell of its display, `dt_fullscreen`: no bar, frame, shadow or strip, the bar's pane off the top, Leave Full Screen, and a high note as one line along the bottom (Gitea #405) |
| `copying.hibr` | moving and copying files: the question about a name already there, renames at once, and a copy in a window of its own over the copy module, with a bar and a Stop |
| `saver.hibr` | the screen saver: the idle timer, and giving the screen to a saver from `savers/` until a key or click, reading the terminals' ptys meanwhile |
| `lock.hibr` | the screen lock: the saver with a password box over it, the password checked by the auth module through PAM, no key reaching the desktop until it opens |
| `restart.hibr` | Restart Desktop: the desktop replaces itself with whatever hibr is installed now, in the same process, windows and running programs kept |
| `recover.hibr` | the supervisor of a held desktop, which starts it again from its last snapshot when it dies |
| `remote.hibr` | folders on WebDAV servers: `dav://` locations moved, copied and deleted in background jobs, remote files opened from a cache and put back when saved, the server dialog |
| `handlers.hibr` | `dt_handler`: what opens a file, by its extension |
| `filepick.hibr` | `dt_filepick`: the Open and Save dialog any app can use, with a preview of what is selected |
| `shot.hibr` | screenshots: the whole screen, the focused window or an area |
| `clip.hibr` | the clipboard and its history, `dt_clipset`; a copied secret is never kept |
| `icons.hibr` | the desktop's own icons: home, disks, trash |
| `hold.hibr` | staying detachable, and the displays attached to a held session |
| `standby.hibr` | displays that wait to be joined (`--standby`) and wait again when let go, and displays blanked: joined but dark and out of use |
| `notify.hibr` | `dt_note`, `dt_notify`: stacked notifications and their history |
| `confirm.hibr` | `dt_confirm`: a question with a yes and a no |
| `ask.hibr` | `dt_ask`: a question with one line of text for an answer -- a label, a field (masked for a secret), a line of help, OK and Cancel; the callback is given the answer and keeps the dialog open by refusing it |
| `jobs.hibr` | `dt_job`, `dt_jobrunning`, `dt_joblast`, `dt_jobfile`: work run in the background with its output kept, and its callback run after a frame once it ends -- marked done first, its files gone after; `wm/remote.hibr`'s transfers run on it |
| `strip.hibr` | the Control Strip |
| `ctl.hibr` | the control socket `desktop ctl` talks to: displays, windows, moves, `rescan`, `state`, `stats` |
| `hud.hibr` | the desktop's section of Blit's performance panel (Blit ADR 0203), sent only while the panel asks |
| `prayer.hibr` | prayer times: today's times, a note at each, the athan (ADR 0035) |
| `vault.hibr` | the Bitwarden or Vaultwarden vault, unlocked once for the whole desktop (ADR 0036) |
| `polkit.hibr` | the password polkit asks for, asked in a dialog of the desktop's own |
| `machine.hibr` | Sleep, Hibernate, Restart and Shut Down through logind, on the Special menu |

The split was made by moving text, not by rewriting it: every function's
definition is what it was in the single `desktop.hibr` it came from, and once
loaded the two leave exactly the same functions and variables behind. A new
file here must be added to `desktop.hibr`'s own list, by name — a table of
contents that lists itself cannot quietly grow a part nobody meant to load.
