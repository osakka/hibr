# `pty` — pseudo terminals

Runs a program on a terminal of its own and lets a script drive it: write
keystrokes in, read what was drawn out, resize it, signal it, collect its
exit status.

```sh
need pty                     # or: mod load pty

id := pty spawn -r 24 -c 80 sh -c 'read line; echo "got: $line"; exit 3'
pty write $id 'hello
'
out := pty drain $id 500
st := pty wait $id 1000
pty close $id
echo "status $st"
printf '%s' "$out" | tr -d '\r'
```

```output
status 3
hello
got: hello
```

What came back is what a terminal shows: the line as the terminal echoed it,
then the program's answer. A terminal ends its lines with `\r\n`, hence the
`tr`. Spawning `hibr` itself works the same way, and is how the shell is
driven through a terminal of its own.

| form | does |
|---|---|
| `pty spawn [-r rows] [-c cols] [-x] cmd [args…]` | start it; the id lands in `$RET`. The size is on the terminal **before** the program is forked, so what it reads of its own width on its first line is already right (Gitea #203). `-x` makes the master close-on-exec: no program started afterwards inherits it, so this process alone holds it and the program is hung up the moment this process goes, however it goes -- which is how the desktop's watchers end with it (Gitea #339). A pty that must survive an `exec`, as a terminal window's does across Restart Desktop, must not have it |
| `pty read <id> [ms]` | what is there now, waiting up to `ms` for the first byte |
| `pty drain <id> [ms]` | keep reading until it stops, or `ms` passes with nothing |
| `pty write <id> text…` | send bytes, as if typed |
| `pty resize <id> rows cols [xpixel ypixel]` | change the size; the program gets `SIGWINCH`. The pixels are the whole terminal's, as `ws_xpixel`/`ws_ypixel`, and are the only way a program on this terminal can learn what a cell measures -- and so whether it can put pixels on screen at all (ADR 0037). Left alone when not given, kept across a later resize that does not mention them |
| `pty size <id>` | the size it has |
| `pty alive <id>` | status: is it still running |
| `pty wait <id> [ms]` | wait for it; the exit status lands in `$RET` |
| `pty signal <id> n` | send it a signal |
| `pty fd <id>` | the master's descriptor, for `console watch` -- read it through `pty read`, which knows the descriptor is non-blocking |
| `pty pid <id>` | its process id |
| `pty close <id>` | kill it if it is still going, and release the pty |
| `pty adopt <fd> <pid> [rows cols]` | take over a pty this process already has open -- after an `exec`, which keeps the master descriptor (it is not close-on-exec) and the program as this process's child; the id lands in `$RET` |
| `pty release <id>` | forget it without ending the program or closing the descriptor |
| `pty list` | every open id |
| `pty via <fd>` | from now on a program this process starts is started by the process at the far end of channel `fd`, as **its** child, and only its master handed over (below); `-` lets the channel go, no word says which it is |
| `pty pump` | take what that process has said -- a program ended, and with what -- never waiting; fails once it has gone |
| `pty serve <fd> <owner>` | the other end: do what the process on channel `fd` asks, for the app named `owner`, never waiting; what was done comes back a line each (`spawned`, `adopted`, `closed`), and status 1 once the channel has ended |
| `pty reap [ms]` | every program served that has ended is waited for and its process told; with `ms`, one no process has held for that long is hung up; fails when none is left to serve |
| `pty owned` | every program served and still running, `fd pid owner` a line: what a restart carries across its exec |
| `pty adopt -o <owner> <fd> <pid> [rows cols]` | take one of those back after the exec, held by no process until one of `owner`'s asks for it |

## A program another process owns

An app in a process of its own (ADR 0050) shows a terminal window, but the
program in it is the **desktop's** child: the app's process is replaced at
every Restart Desktop and may crash, and a program whose parent has gone is
reparented to init -- `waitpid` then answers `ECHILD`, which reads as the
program having ended while it runs, and its status is lost. So in a process
with a channel (`pty via`) `spawn` and `adopt` are asked of the process at
the far end, which starts the program -- every rule above kept: the size on
the master before the fork, a session and a controlling terminal, signals
reset -- in the asking process's directory and with its environment, closes
every other descriptor in the child, and hands the master over by
descriptor (`mods/fdmsg.c`). The asking process reads and writes the master
itself; no byte of the program's goes through the owner. `alive` and
`status` are what the owner last said, `signal` and `close` are asked of
it, and `-x` stays this process's own child, since it means "ends with me".
A program the asking process lets go of, or loses when it ends, is held by
nobody until a process of the same app asks for it by its pid -- the fresh
one a restart starts -- or `pty reap`'s time passes and it is hung up, as
closing its window would. `tests/999-farpty.t` drives both ends.

## What it is for

Two things, and the second is why it exists at all.

**A terminal in a window.** `mods/term/` reads a program's output through
one of these and turns the escape sequences into cells, which is how a hibr
runs inside a hibr window — the last step of
[decision 0020](../../docs/adr/0020-windows-are-drawn-not-composited.md).
`mods/hold/` keeps its sessions' programs on them too.

Neither goes through the builtin: the module offers `"pty"` (`mods/pty.h`,
`PY_API_VER` 4) and both ask for it with `hibr_require`. Version 2 added the
master's descriptor, 3 the size in pixels (`resizepx`), 4 `adopt` and
`release`, for hold's server taking its own session back across an `exec`
(Gitea #198).

**Testing what only happens on a terminal.** The line editor, the console,
the pager, the window manager: all of them behave differently when their
output is not a terminal, which is the point of them and the reason their
test suites are in Python — a harness has to be the controlling process of a
pty and hibr could not open one. It can now, and `tests/750-pty.t` is the
first *shell* test of terminal handling in the tree.

## Things that are easy to get wrong

**The child needs a session and a controlling terminal**, not just the slave
on its descriptors. Without `setsid` and `TIOCSCTTY` a shell started here has
no terminal, runs no line editor, and reports itself non-interactive — which
looks like the program being broken rather than the pty being half-made.

**A master whose child has gone reads `EIO`, not zero.** That is the one
place a pty differs from a pipe, and `tt_read` treats it as end of file.

**The shell must not reap a module's children.** `waitpid(-1, …)` collects
anything, including a child this module forked, and then discards the status
because it does not recognise the pid — so every program run here reported
exit 0. `src/job.c` now waits on each job's own process group. Whatever
forked a child is the thing entitled to its status.

**Output comes back as a string**, so a NUL byte would end it early.
Terminal output is text and escape sequences, so this has not mattered; if it
ever does, the fix is a variable holding bytes, not a smarter reader.
