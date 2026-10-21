# `hold` — sessions that outlive their terminal

Start a program on a terminal the shell keeps for it, walk away, and come
back to it later from another terminal, another login or another machine
over ssh, with everything as it was. It is what `dtach` does, and the part of
`tmux` and `screen` that matters for a desktop: the program never stops, so
nothing has to be saved and restored.

    hold new desk hibr programs/desktop/session.hibr   # start it, attached
    ...ctrl-\ ...                                       # detach
    hold attach desk                                    # later, from anywhere

| | |
|---|---|
| `hold new [-d] name cmd args...` | start `cmd` in a session called `name`, and attach unless `-d` |
| `hold attach [-m] [-n display] name [row col]` | put this terminal on it; `-m` joins alongside whoever is already there instead of taking over, at the given offset (0,0 if not given); `-n` names this client for the control panel -- `client-<fd>` if left unnamed; returns 0 after a detach, the program's status when it ends |
| `hold detach [name]` | detach whoever is attached; with no name, the session this shell is running in |
| `hold list` | each session, attached or detached, the program's pid, and the union's own size (rows x cols) |
| `hold clients [name]` | one line per attached client of a session (with no name, the one this shell runs in): `name row col rows cols primary` -- what a control panel draws its rectangles from |
| `hold gfx [name]` | one line per attached client: `name` and what its own terminal draws -- `kitty`, `sixel`, `none`, or `unknown` for a client that never said. Fails, quietly, against a server older than 0.99.235, which a caller reads as every display unknown |
| `hold shift [name]` | how far the session's origin has been normalised since the server started, as `row col` -- what a program drawing on the union translates its own content by |
| `hold move name display row col` | reposition a named client, `row` and `col` counted from where the primary display is (the union's own 0,0 when there is none); they may be negative, and the whole arrangement is then shifted so its top-left corner is the union's own 0,0. Counted from the primary since 0.99.256, so the same move names the same place after any shift, and sent again changes nothing (Gitea #297) |
| `hold slot name display` | where a display of that name was when it last went -- hung up, detached, or dropped -- as `row col rows cols primary` in the union's frame now; nothing and status 1 when none is remembered, when one by that name is attached, or when another display has taken the place since. `desktop --join` and `--standby` ask it, so a display that went without being detached comes back where it was, with its windows; a returning primary takes the role back unless another was chosen meanwhile |
| `hold drop name display` | detach a named client, server-side -- "switch off" a display from the panel |
| `hold primary name display` | make a named client the session's primary; every other client stops being it |
| `hold kill name` | end the program and the session |
| `hold upgrade name` | re-exec the server on the hibr that is on disk now and take the session back: the program, every attached display and what is on screen all survive |

While attached, **ctrl-\\** detaches. Everything else, ctrl-c included, goes
to the program. A second plain `hold attach` takes the session over and
detaches the first, with a message saying so; `hold attach -m` joins
alongside it instead -- both terminals see the same bytes and either can
type, and each detaches on its own without disturbing the other. This is the
foundation a multi-monitor arrangement attaches through, one terminal per
monitor, all onto the same session.

Each attached client also has a name -- given at attach with `-n`, or
`client-<fd>` if not -- and, exactly one of them, is the session's primary,
the first to attach until something else picks another with `hold primary`.
Neither is required for anything above to work; both exist for a control
panel to draw a picture of the arrangement and let it be rearranged: `hold
clients` to list it, `hold move` to reposition one of them, `hold drop` to
switch one off (a server-side detach, indistinguishable to the program from
that client's own `ctrl-\\`), `hold primary` to change which anchors a
desktop's own bar and menu. The primary is a **rectangle**, not a column:
its `row` and `rows` are as real as its `col` and `cols`, so a client
attached at `hold attach name 10 0` is a lower pane in a stacked
arrangement and a desktop anchors its bar to row 10. The desktop read the
column and dropped the row until hibr 0.99.205, which is what made a union
horizontal-only in practice however this described it (Gitea #240). All
three also send the program's foreground
`SIGWINCH`, the same nudge a fresh attach already gets, so a program that
reads `hold clients` on a resize -- the desktop's own bar and icon grid do
-- notices the change live rather than on its own next unrelated redraw.

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

`hold` keeps a terminal emulator of its own (the `term` module's
`"terminal"` interface, fed every byte the pty writes) and, once nothing
more is waiting from it right now, sends each client a diff against what
that client was last sent -- only the cells that actually changed, a
cursor move only when the next one is not already where the last glyph
left it, and the pen only when it differs from the last one actually
emitted. A client's very first frame, and the one right after a resize
(the previous frame no longer means anything at the new size), are still a
full repaint, deliberately: there is nothing sent yet to diff against.
Measured through the real desktop, not guessed: a 15-step window drag
that cost 107KB before this (a full ~6.3KB repaint resent on every
settled move) costs under 1KB after it, matching what the console module
itself would send a directly attached terminal for the same drag -- hold
is no longer paying to re-derive what console had already worked out.

Drawing from an emulator loses anything that is not drawn, and a program
sends several such things on purpose: the clipboard (OSC 52), the bell,
notifications (OSC 9 and 777), the window title (OSC 0 and 2), and links
(OSC 8). The emulator keeps each (`"terminal"` version 3), and hold sends
them on: the clipboard, bells and notifications exactly as the program
wrote them, to every attached client the moment they arrive; the title as
part of each client's mode state, sent when it changes and to a client as
it attaches; and links per cell, drawn the way colours are and closed at
the end of every frame. Before 0.75 all of them were dropped, and before
0.78 all but the clipboard -- a held program could not ring, notify, set a
tab's title or print a clickable link. A request to read the clipboard is
not passed on.

A **picture** is the same lesson with a size: a sixel or a kitty
transmission is a bitmap over a rectangle of cells, and an emulator that
draws cells drops it. Since 0.99.77 the emulator keeps each one as a region
(`"terminal"` version 4) and hold re-emits them, each at its own corner,
after the cells of every frame -- only when the set has changed or a client
is being painted from scratch, so a still picture costs a settled frame
nothing, and only to a client whose own rectangle holds the whole picture,
because a bitmap cannot be clipped. A region goes when something writes a
cell inside it, which is how a window that closed or moved takes its
picture with it.

## Where the origin is

A client's `row` and `col` place it in the session's own space, and a
display above or to the left of whoever is at the origin can only say so
with a **negative** one. The union used to be measured from 0,0 to the
furthest edge any client reached, so such a client contributed nothing to
it at all: the pty and the emulator were sized without it, and its whole
viewport then read rows `tm_at` answers null for. Nothing crashed and
nothing was logged -- the display was simply blank for as long as it was
there, no menu could open on it and no window could be moved onto it, and
the control panel, which shifts its *own* picture to fit, drew a correct
arrangement hold was not in. Measured on a real two-display session as
`client-6 -68 3 64 213 0` beside a union of 64x216 (Gitea #257).

So a negative minimum is **normalised away**: every client is moved by the
same amount, which keeps the arrangement exactly as it was made and puts
its top-left corner at the union's 0,0. A positive minimum is left alone,
which is a decision rather than an oversight -- attaching at an offset with
nothing above or left of it is how space is *reserved* for a display that
has not joined yet, which is how `desktop --join` and `tests/desktop.py`
both stack a union.

The program is told, because it cannot work it out. `hold shift` is the
running total, and the two things a program might infer it from are each
wrong: where the **primary** is moves by a whole display when somebody
merely picks a different primary, with nothing having moved; and how far
**one** client moved cannot be told from that client having been the one
dragged, which is the case that causes a shift in the first place. The
desktop reads it in `dt_size` and translates every window, icon and the
Control Strip by the difference.

That rule means **one picture spanning a union reaches nobody**: at a
stacked arrangement a wallpaper drawn across both panes is held whole by
neither client, so neither is sent it (measured at 0 images on both, hibr
Gitea #238). The answer is not to clip it here -- a sixel is paint and
would have to be re-encoded per client, and the kitty id space would have
to become per-client too. It is for the thing placing the picture to place
**one per display**, each of which is then held whole by its own client by
construction. The desktop does that from 0.99.206: `DT_DISPLAYS` carries
every client's rectangle from `hold clients`, and `dt_wall` fits the
wallpaper to each. Anything else that wants a picture on every display of
a union does the same, and this rule stays as it is.

The other half is that a program can only draw pixels at all if it knows
what a cell measures, and it learns that from `ws_xpixel`/`ws_ypixel` on its
own terminal -- which, for a held program, is a pty hold made and nobody
had ever told. A client reports its own with its size, hold picks the
primary client's (a bitmap is 1:1; there is no second size to draw it at)
and sets it on the pty. Before 0.99.77 `console gfx` answered `none 0 0`
inside every held program, so every picture in a desktop -- and
`dt_autohold` holds them all -- was half blocks whatever the terminal could
do. Gitea #103, ADR 0037.

And a picture goes only to a client whose own terminal draws it (Gitea
#258). Before 0.99.235 one display that could show pictures made the
session draw pixels, and every display whose rectangle held a picture was
sent it -- so a terminal that draws none got a bitmap it dropped, over
cells the program had left blank for it: black. Each attaching client now
asks **its own** terminal, before it joins, the same two questions the
console asks (kitty's `a=q` and the device attributes, from
`mods/gfxask.c`, which both modules build in), against a one-second
deadline, and sends the answer as `HD_GFX`. Asking on the client rather
than reading the replies out of what each client types keeps a parser out
of the relay, and means the answer is in hand before anything is drawn.
The same probe asks the kitty keyboard protocol's own question, and a
client whose terminal answers sends `HD_KBD` (Gitea #326): hold then puts
the flags the program asked for on that terminal, as `CSI = flags ; 1 u`
after the screen switch in each client's mode replay -- such a terminal
keeps a set of flags per screen -- and the client sets them to none on
both screens as it leaves. A client whose terminal did not answer is sent
no flags, and its keys arrive in the old form, which the console still
reads.
The picture loop sends a kitty transmission only to a client that said
kitty and a sixel only to one that said sixel; a client that never said --
one older than this -- is sent none. `hold gfx` reports what each said,
and the desktop draws its wallpaper as pixels on a display that draws the
session's protocol and as half blocks on every other. The answer travels
across `hold upgrade` on a `gfx` line of its own; a client carried from an
older server has none and draws half blocks until it attaches again. A
terminal that answers nothing at all costs the deadline, once, at attach.

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

## Rectangle copies

A program that moves a block of cells whole can send it as one DECCRA
(Gitea #337), and hold's emulator applies it to the session's grid. A client
whose own terminal said so (feature 28 in its device attributes, asked before
it joins and sent as `HD_COPY`) is sent the copy as well, replayed where its
own view holds both ends. Its picture of the screen is changed exactly as the
copy changes the terminal's, and the ordinary diff then runs, so a copy can
only ever save bytes. A client whose terminal does not copy is diffed as
always. So a session can have both kinds of display attached at once.

The program has to be told the server does this, and nothing inside hold
answers its questions: the emulator has no pty to answer on. So the server
puts `HIBR_HOLDCOPY=1` in its program's environment. A program under a
server older than 0.99.283 is never told, and never copies, even when the
real terminal's feature 28 is relayed to it, since that old server's
emulator would ignore the copy.

## Synchronized output

Every frame a client is sent is one synchronized update (mode 2026, Gitea
#338): `CSI ?2026h` before the modes, the copies, the cells, the pictures
and the cursor, and `CSI ?2026l` after them, so the client's terminal
presents it whole however the bytes are split on the way there.

And a program's own pair is honoured. The server renders from its
emulator's live grid, so a frame larger than one read of the pty -- a
desktop's full repaint is hundreds of kilobytes -- used to reach every
client half drawn. A render is now owed until the program closes its frame,
or until the emulator's one-second guard says it is not going to: `tm_api`
version 8's `syncleft` answers how many milliseconds that may still be, and
the loop sleeps exactly that long when nothing else wakes it. Since nothing
reads the emulator mid-frame, a relay keeps no frozen copy of the grid --
which for every frame a console brackets would have been a whole grid,
460 kB at 232x71, made and thrown away.

A client attaching, or resizing, mid-frame is still rendered at once: one
frame of it may be half drawn, and the next, at the frame's close, puts it
right.

## Upgrading a session without ending it

A `Restart Desktop` re-execs the desktop **inside** this server, so it can
never replace this process's own image -- and the emulator every client is
rendered from lives here. A fix in `hold`, `term` or `pty` therefore stayed
out of a session however many times the desktop restarted, and the only
remedy was to quit the session and lose the programs in its terminals,
which is the one thing a restart exists to avoid (Gitea #198).

    hold upgrade NAME

re-execs the server itself on the hibr that is on disk now and takes the
session back. The program on the pty keeps running -- its pid does not
change, and neither does the server's, since an exec keeps it -- every
attached display stays attached, and what was on screen is still on screen.

What is carried is small and all of it already existed: the listening
socket, the pty master and each client's connection are descriptors, and a
process that execs keeps the ones it has not marked to close; the program's
pid and the emulator's size go in a file beside the socket; and the
emulator itself is written out with `term`'s own save, whose fields are
appended to rather than rearranged, so the new build reads what the old one
wrote. `pty`'s own `release` lets the terminal go without ending the
program or closing the master, which is what stops the exec taking the
desktop with it.

Three details that are not details. The modules are re-loaded **by the
paths they were loaded from**, read out of `/proc/self/maps`, rather than
by name through the module path: a session started with `mod load
./build/mods/hold.so` has no module path at all, and a resumed server that
could not find `pty` would lose the session at the exec -- which is exactly
what happened the first time this was run. The ` (deleted)` the kernel
appends to a replaced mapping is dropped, so what is loaded is the new file
at the same name, which is what apt leaves behind. And no client's `front`
is carried: each is sent the whole screen again, which is what an attach
already does, where a stale front is the one thing that would leave it
wrong.

A server **older than this verb** -- anything before 0.99.159 -- has no case
for the request and closes the connection without answering, so the client
asks for a reply and says which refusal it met: `this session's server is
older than hold upgrade` against one of those, `no such session` against a
name nothing answers to. Asking for no reply, as the first version did, made
the command report success and do nothing on exactly the sessions anybody
would first try it on.

What it costs when it goes wrong, measured rather than reasoned about. A
module that is **replaced** is the safe case and the common one. A module
that is **removed** with nothing in its place -- `apt remove`, a `make
clean`, a file moved aside -- loses the session, and there is no way back
once the exec has happened: `mod load <path> || exit 1` fails, the new
image exits, the master descriptor it was keeping closes with it, and the
program on the pty dies. And an exec that **cannot happen** -- a `$HIBR`
that is there but not executable -- leaves the session exactly as it was,
but only since 0.99.165: the pty is released before the exec so the new
image can adopt it, and until then the loop carried on with an id the
module no longer knew, so `hold list` answered a pid of `-1`, no client's
size ever reached the program again and its exit would never have been
noticed, while the screen went on being drawn. It is taken back now
(`tests/936-hold-upgrade.t`, which fails against 0.99.164).

## A session in trouble is not everybody's problem

Every request to a session is bounded: the dial 1000 ms, the reply 5000 ms,
both overridden by `HIBR_CONNTIMEO`. That is a default where the shell's own
knob is opt-in, and deliberately: a `/dev/tcp` script may mean to wait, and
nothing asking a local session a question does. Until 0.99.171 (Gitea #204)
one socket that was **listening with a full accept queue and nobody
accepting** stopped everything that asked after it -- `hold list` dials each
socket in the directory in turn, and a desktop calls it from `dt_autohold`
*before* `dt_open` redirects stderr, so the desktop printed its name and
hung with no error and no `desktop.log`: measured at 0 rows drawn in eight
seconds with one wedged socket present, and 24 with the bound in (Gitea
#170, reported from a Mac and not reproducible until the mechanism was
found).

A timeout is not a refusal, and two places have to tell them apart. `hold
new` refuses a name whose socket is there and not answering, because
`hd_serve` unlinks the path before it binds and would otherwise take a
running server's only name away and leave it alive and unreachable; and
`hold list` removes a socket it got no answer from, which must stay narrowed
to a refusal, or a busy server would be deleted by anybody's listing.
`hd_dial` leaves `errno` saying which it was, `ETIMEDOUT` for the one and
`ECONNREFUSED`/`ENOENT` for the other.

The shape is **not** `net_conn`'s, and that was measured rather than
assumed: a non-blocking AF_UNIX connect against a full backlog answers
`EAGAIN` -- nothing is in progress -- and a poll for writable then returns
at once with `POLLOUT|POLLHUP` and `SO_ERROR` **0**, which by TCP's rules
reads as a successful connect and hands back a descriptor connected to
nothing. So the connect itself is retried against a wall-clock deadline,
since only a connect can learn that the queue has room. It is also kinder
to the wedged server than the blocking call was, which left another
connection on its queue every time anything retried.

## Files

| file | role |
|---|---|
| `hd.h` | the message types and every function the files share |
| `wire.c` | the socket directory, names, and framing: a type byte, a length, the bytes |
| `srv.c` | the server: the program's pty, the listening socket, any number of attached clients, their union size |
| `render.c` | one client's own rectangle of the emulator's grid, diffed against a per-client front to send only what changed |
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
to `SIGWINCH`, a client's own mouse click at its own local column
reaching the program at that column plus its offset -- rewritten to
whichever encoding the program actually asked for, legacy included -- and
a settled frame being a small diff except on the first attach or right
after a resize, where it is a full repaint again. Named clients, the
default name, which one is primary and how it moves on a transfer or a
drop, repositioning with `hold move`, switching one off with `hold drop`
and a bad display name being an error rather than a crash are all checked
against `hold clients`'s own plain text output. `tests/desktop.py` holds
a whole desktop, detaches it, reattaches from a new terminal and checks
the full frame is drawn again. Both are clean under ASan and UBSan.

`tests/937-hold-dial.t` is the bound: a stand-in that fills its own accept
queue from inside itself and then stays alive -- a listener that has exited
is refused in a millisecond and proves nothing -- and against it `hold
list` comes back with nothing set, leaves that socket alone, `hold new`
refuses the name rather than binding over it, and a socket with no listener
at all is still removed as stale. It fails against 0.99.170 three times
over, each one a `status 124`.

## What it does not do

No copy of the screen is kept across a full detach with nobody attached,
so a program that does not redraw on `SIGWINCH` comes back blank until it
next writes on its own. No splitting, no windows, no scrollback: that is
the desktop's job, and the terminal module's.
