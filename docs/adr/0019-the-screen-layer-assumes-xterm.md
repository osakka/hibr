# 0019 — The screen layer assumes an xterm, and does not read terminfo

Status: accepted

## Context

Four things worth building — a `vi`, a `most`, a system monitor, a file
browser — all need the same piece: an alternate screen, a grid of cells, a
redraw that sends only what changed, and keys decoded into names. Building that
once is obviously right. What it should be built *on* is the question.

The traditional answer is terminfo: a database describing what every terminal
can do and what bytes to send for it, consulted through ncurses. It is the
portable answer and it is forty years old for a reason.

## Decision

The screen module emits escape sequences directly, assuming a terminal that
understands the xterm set: `\e[?1049h` for the alternate screen, `\e[row;colH`
to move, SGR for colour, `\e[?2004h` for bracketed paste, and SGR mouse
reporting. It does not open terminfo, does not link ncurses, and does not
consult `TERM` at all.

## Why

[0001](0001-build-with-tcc-and-no-dependencies.md) is the whole argument. hibr
depends on libc and libdl. Linking ncurses would be the first real dependency
in the project, and reading terminfo without ncurses means writing a parser for
a binary database format — which is possible, but it is a parser for a file
that exists to describe hardware that no longer exists.

What terminfo buys is correctness on terminals that are not xterm-compatible.
In 2026 that set is: hardware serial terminals, some embedded consoles, and the
Linux virtual console, which handles everything above except that it ignores
the sequences it does not know. Every terminal emulator anyone actually uses —
xterm, the VTE family, kitty, alacritty, foot, wezterm, iTerm2, Terminal.app,
Windows Terminal, tmux, screen — implements this set.

## What it costs

On a terminal that is not xterm-compatible, output is wrong rather than
degraded. There is no capability query to fall back on, so a terminal that does
not know `\e[?1049h` will not get an alternate screen and will scroll the
session instead. That is a real regression against ncurses, and it is accepted
because the alternative is a dependency on every platform to serve a set of
terminals that is close enough to empty.

Two capabilities *are* negotiated, because they are the ones that break
visibly rather than cosmetically: bracketed paste and mouse reporting are
enabled on open and explicitly disabled on close, so a terminal that ignores
them is left exactly as it was found.

## What is not given up

The restore path is unconditional. `SIGINT`, `SIGTERM` and `SIGHUP` put the
terminal back and re-raise, and the module finaliser does the same on `mod
drop` and at exit. Leaving a terminal in raw mode is the one failure a
full-screen program must never have, and it does not depend on knowing what
terminal it is.

Width is not guessed either. The same `u8w` tables the line editor uses decide
how many columns a codepoint occupies, so combining marks take none and wide
glyphs take two, on any terminal.

---

[← decisions](README.md)
