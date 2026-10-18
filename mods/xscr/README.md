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
| `xscr draw ID row col h w [-p pane] [-m pixels\|half]` | the screen scaled to h by w cells, as pixels where the display can place them and half blocks where it cannot or `-m half` says so |
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
- **Resizing** a running screen: Xvfb cannot, so the size is chosen when
  the window opens and the screen is scaled into the window.
- **Speed** is the picture path's: each changed frame is a whole picture
  to the terminal, and over ssh the link is what limits motion. OpenGL is
  Mesa's, in software where there is no GPU.
