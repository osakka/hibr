# `hold` — sessions that outlive their terminal

Start a program on a terminal the shell keeps for it, walk away, and come
back to it later from another terminal, another login or another machine
over ssh, with everything as it was. It is what `dtach` does, and the part of
`tmux` and `screen` that matters for a desktop: the program never stops, so
nothing has to be saved and restored.

    hold new desk hibr examples/desktop/session.hibr   # start it, attached
    ...ctrl-\ ...                                       # detach
    hold attach desk                                    # later, from anywhere

| | |
|---|---|
| `hold new [-d] name cmd args...` | start `cmd` in a session called `name`, and attach unless `-d` |
| `hold attach [-m] name [row col]` | put this terminal on it; `-m` joins alongside whoever is already there instead of taking over, at the given offset (0,0 if not given); returns 0 after a detach, the program's status when it ends |
| `hold detach [name]` | detach whoever is attached; with no name, the session this shell is running in |
| `hold list` | each session, attached or detached, the program's pid, and the union's own size (rows x cols) |
| `hold kill name` | end the program and the session |

While attached, **ctrl-\\** detaches. Everything else, ctrl-c included, goes
to the program. A second plain `hold attach` takes the session over and
detaches the first, with a message saying so; `hold attach -m` joins
alongside it instead -- both terminals see the same bytes and either can
type, and each detaches on its own without disturbing the other. This is the
foundation a multi-monitor arrangement attaches through, one terminal per
monitor, all onto the same session.

Each attached client has a place in the session's own virtual space: its own
row,col offset (0,0 by default) and its own rows,cols size. The program on
the pty is always sized to the union of every attached client's own
rectangle, so it sees one screen big enough for everyone at their own place
-- three clients placed side by side, each 40 columns wide, give the program
120 columns to draw on, not 40. Every client is sent only its own rectangle
of that union, translated into its own coordinates -- a client at column 80
whose neighbour's line runs into that column sees it starting at its own
column 0, not 80. The same translation runs the other way: a client's own
mouse report, in SGR form, is rewritten by its own offset before it reaches
the pty, so a click at that client's own local column 5 lands on the
program's own column 85, not 5.

There is no diffing yet, though: `hold` keeps a terminal emulator of its own
(the `term` module's `"terminal"` interface, fed every byte the pty writes)
and, once nothing more is waiting from it right now, repaints each client's
own rectangle from scratch -- a full screen every time, not just what
changed since the last one. A quiet session costs nothing extra; a busy one
resends unchanged cells along with the changed ones. That is the next thing
to add here, not a correctness gap: what a client sees is right, only more
of it than it strictly needs to be.

## How it works

`hold new` forks a server that leaves the shell's session with `setsid`,
starts the program on a pty through the `pty` module, and listens on a Unix
socket at `$TMPDIR/hibr-hold-$UID/name` (`/tmp` when `TMPDIR` is unset). The
directory is created mode 0700 and refused if anyone else owns it or can
read it. A client is `hold attach`: it puts its terminal in raw mode and
passes bytes both ways, with the window size and a detach as messages of
their own.

- **Logging off detaches.** A closed terminal or a dropped ssh connection
  hangs up the client, which dies. The server was never in that session, so
  it carries on, detached. It ignores `SIGHUP` itself, but the program it
  starts gets the default back: an ignored signal survives `exec`, and a
  held program that could not be hung up would outlive its own terminal.
- **Nothing is lost while detached.** What the program writes while nobody
  is attached is read and dropped, or the pty would fill and stop it.
- **Reattaching repaints.** A newly attached client is rendered at once,
  brought to the session's actual mode state and sent its own rectangle's
  current content, whether or not the program does anything at all -- a
  program that never reacts to `SIGWINCH` (`cat`, unlike a shell redrawing
  its prompt) would otherwise leave it seeing nothing. The server still
  sends `SIGWINCH` to the program's foreground on every attach, even when
  the size has not changed, for whatever a full-screen program does with it
  beyond what hold already sent.
- **Detaching gives the terminal back clean.** The alternate screen, a hidden
  cursor, every mouse mode and bracketed paste are all turned off in the
  terminal being left, because the program still thinks they are on.
- **A program knows it is held.** `HIBR_HOLD` holds the socket's path, so
  `hold detach` with no name works from inside, which is what the desktop's
  Detach item does.

The sockets are not in `$XDG_RUNTIME_DIR` on purpose: systemd removes that
at logout, which is the moment a held session exists to outlive. For the same
reason, a system with `KillUserProcesses=yes` in `logind.conf` kills
everything a user started when they log out, this included. Allow lingering
with `loginctl enable-linger` there.

## Files

| file | role |
|---|---|
| `hd.h` | the message types and every function the files share |
| `wire.c` | the socket directory, names, and framing: a type byte, a length, the bytes |
| `srv.c` | the server: the program's pty, the listening socket, any number of attached clients, their union size |
| `render.c` | one client's own rectangle of the emulator's grid, as a full repaint of escape sequences |
| `mouse.c` | a client's own SGR mouse report, rewritten by its own offset and re-encoded to legacy if that is what the program asked for |
| `cli.c` | the client: raw mode, the relay, ctrl-\\, and giving the terminal back |
| `hold.c` | the builtin, and starting a server |

## Tests

`tests/790-hold.t` starts sessions under a `TMPDIR` of its own and plays
the attaching terminals with the `pty` module: detaching with ctrl-\\,
closing the terminal as a logout would, attaching again and finding the
shell's variables still set, the program's status coming back, a second
attach taking over, `-m` joining alongside instead, the pty sized to the
union of every attached client, one client's own rectangle showing content
the other's does not, the alternate screen reaching an attaching client on
entry but not again once nothing has changed, a just-attached client seeing
its own mode state and content at once even when the program never reacts
to `SIGWINCH`, and a client's own mouse click at its own local column
reaching the program at that column plus its offset -- rewritten to
whichever encoding the program actually asked for, legacy included.
`tests/desktop.py` holds a whole desktop, detaches it, reattaches from a
new terminal and checks the full frame is drawn again. Both are clean
under ASan and UBSan.

## What it does not do

Every frame is a full repaint -- there is no diffing between one and the
next, so a busy program's session resends every cell of a client's own
rectangle on every settled redraw, not just what changed. No copy of the
screen is kept across a full detach with nobody attached, so a program
that does not redraw on `SIGWINCH` comes back blank until it next writes
on its own. No splitting, no windows, no scrollback: that is the desktop's
job, and the terminal module's.
