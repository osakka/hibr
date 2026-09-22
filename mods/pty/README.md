# `pty` — pseudo terminals

Runs a program on a terminal of its own and lets a script drive it: write
keystrokes in, read what was drawn out, resize it, signal it, collect its
exit status.

```sh
mod load pty                 # or: need pty

id := pty spawn -r 24 -c 80 hibr
pty write $id 'echo hello
'
out := pty drain $id 500
pty write $id 'exit
'
st := pty wait $id 1000
pty close $id
```

| form | does |
|---|---|
| `pty spawn [-r rows] [-c cols] cmd [args…]` | start it; the id lands in `$RET` |
| `pty read <id> [ms]` | what is there now, waiting up to `ms` for the first byte |
| `pty drain <id> [ms]` | keep reading until it stops, or `ms` passes with nothing |
| `pty write <id> text…` | send bytes, as if typed |
| `pty resize <id> rows cols` | change the size; the program gets `SIGWINCH` |
| `pty size <id>` | the size it has |
| `pty alive <id>` | status: is it still running |
| `pty wait <id> [ms]` | wait for it; the exit status lands in `$RET` |
| `pty signal <id> n` | send it a signal |
| `pty pid <id>` | its process id |
| `pty close <id>` | kill it if it is still going, and release the pty |
| `pty list` | every open id |

## What it is for

Two things, and the second is why it exists at all.

**A terminal in a window.** `mods/term/` will read a program's output through
one of these and turn the escape sequences into cells, which is how a hibr
runs inside a hibr window — the last step of
[decision 0020](../../docs/adr/0020-windows-are-drawn-not-composited.md).

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
