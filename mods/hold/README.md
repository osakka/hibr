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
| `hold attach name` | put this terminal on it; returns 0 after a detach, the program's status when it ends |
| `hold detach [name]` | detach whoever is attached; with no name, the session this shell is running in |
| `hold list` | each session, attached or detached, and the program's pid |
| `hold kill name` | end the program and the session |

While attached, **ctrl-\\** detaches. Everything else, ctrl-c included, goes
to the program. A second `hold attach` takes the session over and detaches
the first, with a message saying so.

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
- **Reattaching repaints.** The server sends `SIGWINCH` to the program's
  foreground on every attach, even when the size has not changed. A
  full-screen program redraws on that; the console re-asserts the alternate
  screen and mouse mode first, because the new terminal has never seen them.
  A plain shell only redraws its prompt, since nothing keeps a copy of the
  screen.
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
| `srv.c` | the server: the program's pty, the listening socket, one client |
| `cli.c` | the client: raw mode, the relay, ctrl-\\, and giving the terminal back |
| `hold.c` | the builtin, and starting a server |

## Tests

`tests/790-hold.t` starts sessions under a `TMPDIR` of its own and plays
the attaching terminals with the `pty` module: detaching with ctrl-\\,
closing the terminal as a logout would, attaching again and finding the
shell's variables still set, the program's status coming back, a second
attach taking over, `hold detach` from outside, `HIBR_HOLD`, and `kill`.
`tests/desktop.py` holds a whole desktop, detaches it, reattaches from a new
terminal and checks the full frame is drawn again. Both are clean under ASan
and UBSan.

## What it does not do

One client at a time; a second takes over rather than sharing. No copy of
the screen is kept, so something that does not redraw on `SIGWINCH` comes
back blank until it next writes. No splitting, no windows, no scrollback:
that is the desktop's job, and the terminal module's.
