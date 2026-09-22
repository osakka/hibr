# `term` — a terminal emulator (in progress, not built)

**Nothing here is built or loaded yet.** There is no rule for it in the
Makefile and no test covers it. It is the second half of
[decision 0020](../../docs/adr/0020-windows-are-drawn-not-composited.md)'s
last step, started and then set aside when four smaller gaps in the desktop
turned out to be worth closing first.

What is here is the screen model: `tm.h` and `grid.c`, a cell grid with a
cursor, a scroll region, wrapping, insert and delete of lines and characters,
and resizing. It is written and it is not exercised, which is the same as
saying it is unverified.

What is not here yet:

- `vt.c`, the escape parser — CSI, OSC, SGR, the alternate screen
- `draw.c`, blitting the grid into a window through the `display` interface
- `key.c`, turning a decoded key name back into the bytes a program expects
- the builtin, the Makefile rule, and the tests

The half below it *is* finished: [`mods/pty/`](../pty/README.md) opens a
pseudo terminal and runs a program on it, and `mods/pty.h` offers that to
this module as a C interface so it does not carry its own copy of forkpty.
