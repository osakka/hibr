# xscr

An X screen in a window (Gitea #290). X11 programs, and the GTK, Qt,
Firefox and Chromium ones that speak X11, run on the hibr desktop through
the same picture path the wallpaper, the Browser and the video player use.

It is built on **Xvfb**, the X server that draws into memory rather than
onto hardware, which comes in the `xvfb` package. Nothing links against X:

- **Pixels** are read from the file Xvfb maps its screen into (`-fbdir`):
  an XWD header in network order, then the screen at 32 bits a pixel, read
  through the colour masks the header gives.
- **What the module says to the server** is written here, over its Unix
  socket: the connection setup, `QueryExtension`, **DAMAGE** (a damage
  object on the root window at the NonEmpty level, so the server sends one
  event per change and an idle screen costs nothing), **XTEST** for keys,
  buttons, motion and the wheel, and `GetKeyboardMapping`, so a key name
  becomes the keycode the server's own map gives it.
- **The server** is started by the module, as a child of this shell in a
  session of its own, so no job's wait can take it, and the module reaps it.
  On Linux the kernel ends it when the shell's process does
  (`PR_SET_PDEATHSIG`), so a desktop that crashes or is killed takes its X
  screens with it. Restart Desktop is an exec of the same process, so the
  server should survive it; that is not yet checked by a suite. Elsewhere
  it is ended when the shell exits.
- **A window manager of its own** (Gitea #291), for a program fitted to
  its window: `xscr manage` takes the root's SubstructureRedirect, which
  only one client may hold, and reads back BadAccess after a round trip, so
  a screen where the person's own window manager runs is known to be
  unmanaged. A normal toplevel is placed at 0,0 at the size `xscr fit`
  gives and placed again when that changes; one with `WM_TRANSIENT_FOR` (a
  dialog) keeps its own size, centred; focus goes to what was mapped last
  and to the window under a click; the main window's title is read from
  `_NET_WM_NAME` or `WM_NAME` as it changes. Menus and tooltips are
  override-redirect and never reach it.
- **DAMAGE is taken off only once the screen has been drawn** (`xscr draw`
  or `xscr seen`), so at the NonEmpty level a window that is hidden or
  minimised hears of one change and then nothing. The app holds frames to
  `DT_TERMMS` apart besides.

## Verbs

| verb | what it does |
|---|---|
| `xscr start N WxH DIR` | start `Xvfb :N` with one screen of W by H at 24 bits, kept in DIR; says the server's pid |
| `xscr stop PID` | end a server `start` began that was never opened |
| `xscr open N FILE [PID]` | open display N, whose screen is FILE (`DIR/Xvfb_screen0`); says a screen number. With PID, closing the screen ends that server -- the one `start` gave, or after a restart the pid the app kept |
| `xscr fd ID` | the connection's descriptor, to `console watch` |
| `xscr poll ID` | read what the server sent: status 0 when the screen changed since it was last drawn, 1 when it has not, 2 when the server has gone |
| `xscr dirty ID` | the same answer without reading |
| `xscr seen ID` | mark the screen drawn and take the damage off, so the next change is reported; `draw` does this itself |
| `xscr size ID` | the screen's size, `W H` |
| `xscr manage ID` | become the screen's window manager: status 0, or 1 when another one already is |
| `xscr fit ID W H` | the region a managed normal window fills, every such window placed again |
| `xscr title ID` | the main window's title, as it is now |
| `xscr draw ID row col h w [-p pane] [-m pixels\|half] [-c]` | the screen scaled to h by w cells, as pixels where the display can place them and half blocks where it cannot or `-m half` says so; with `-c`, only the region `fit` set, which a fitted window shows 1:1 |
| `xscr key ID NAME` | a key by the console's name for it -- a character, or `enter`, `left`, `f5`..., with `ctrl-`, `alt-` and `shift-` in front |
| `xscr type ID TEXT` | type text, a character at a time |
| `xscr move ID X Y` | move the pointer to a pixel of the screen |
| `xscr button ID press\|release N X Y` | a button pressed or let go at a pixel |
| `xscr click ID N X Y` | a button pressed and let go |
| `xscr wheel ID up\|down X Y` | the wheel, as buttons 4 and 5 |
| `xscr close ID` | close the connection, and end its server when it has one |
| `xscr list` | the screens open now |

## What it cannot do yet

- **Hover**: the pointer moves only with a button held, since the desktop
  does not ask the terminal for every movement (CLAUDE.md, the Control
  Strip's hover).
- **Keys outside the server's map**, which is Xvfb's US layout: a character
  no key has is passed over and said in the log. Changing the map for one
  is the way to type them, and is not written yet.
- **The clipboard** between the X screen and the desktop.
- **Focus without a window manager** is wherever the pointer is, so a
  program that does not take the keyboard itself gets keys only once it has
  been clicked. GTK and Qt programs take it. A key the desktop holds, such
  as its quit key, goes to the desktop, and any other goes to the screen,
  as in a terminal window.
- **Resizing** a running screen: Xvfb cannot (RandR's maximum is the
  starting size, and it refuses new modes), so a fitted window's screen
  starts at the Largest Screen size and the program, not the screen, is
  resized; a window larger than that is the screen, scaled.
- **Speed** is the picture path's: each changed frame is a whole picture
  to the terminal, and over ssh the link is what limits motion. OpenGL is
  Mesa's, in software where there is no GPU.
