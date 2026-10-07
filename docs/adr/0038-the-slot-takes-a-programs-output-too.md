# 0038 — `:=` takes a command's result, whatever kind of command it is

Status: accepted

Extends [0005](0005-results-travel-in-a-slot.md).

## Context

0005 gave hibr a result slot: `ret` fills it, `x := cmd` binds it, and nothing
forks. Builtins and module commands are told when they are being bound and fill
the slot silently.

A **program** cannot do that. There is no slot in a child process to fill, so
`x := curl …` ran curl, bound nothing, and reported success:

```text
v := /bin/echo hello      v=[]       status=0   and "hello" went to the terminal
v := /bin/false           v=[]       status=1
v := str upper hello      v=[HELLO]  status=0
```

An empty value with a status of 0 is the worst of the three possible answers,
because nothing downstream can tell it from a command that legitimately
produced nothing. It cost this project real time twice: it is what "the About
window is writing outside its own box" turned out to be, and the same bug made
a platform check (`[ "$AB_OS" = Linux ]`) never match on Linux, so two apps ran
their macOS branch there for releases. It was written down as a trap, and being
written down was not enough — the owner hit it again from a cold start, which
is the argument for changing the behaviour rather than the documentation.

## Decision

`x := prog` captures the program's standard output, the way `$( )` does, with
all trailing newlines removed. `:=` therefore means one thing for all three
kinds of command: take this command's result.

It applies to a plain foreground command only. A pipeline stage and a
background job already run in a child whose binding dies with it, and they are
left exactly as they were — binding nothing in the parent. A redirection the
command carries of its own still wins, so `x := cmd > file` binds nothing and
writes the file, as `v=$(cmd > file)` does.

A **function** is unchanged: it fills the slot with `ret`. Capturing a
function's output would mean forking for an in-process call, which is the cost
0005 exists to avoid, and `ret` is the documented contract.

## Consequences

The construct now reads as it behaves, and the failure mode that cost two
afternoons is gone by construction: a program bound this way writes nothing to
the terminal, so it cannot draw underneath a full-screen program's own display.

A program captured this way forks — the same fork `$( )` costs. That is not a
new cost in any loop, because the slot's no-fork path is the builtin one and
that is untouched: a 40,000-iteration `while` loop measures 530.2M instructions
against 533.1M before, a difference inside the noise that code layout alone
produces.

What is given up is the ability to write `x := prog` and have the program's
output go to the terminal. No script in this tree did that — a census of all
295 distinct `:=` command words in `examples/desktop/`, classified against the
builtin table with every module loaded and against the desktop's own 1,715
functions, found **no** `:=` on a program anywhere, and the two comments that
mention one are warnings about the old behaviour at the call sites where it
bit.

The remaining asymmetry is deliberate and named: a function that prints instead
of calling `ret` still binds nothing. `hibr --explain` is where that belongs,
since the lint module walks the parsed tree and can see a `:=` whose word is
neither a builtin nor a function the file defines.

---

[← decisions](README.md)
