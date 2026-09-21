# 0005 — `ret` does not print; results travel in a slot

Status: accepted

## Context

A shell function returns a value by printing it, and the caller collects it
with `$( )`. That costs a fork, a pipe and a wait on every single call. In a
loop it dominates everything else: 5,000 calls through `$( )` take around
670 ms, against 33 ms for the same work without forking.

It also conflates two different things — a function's output, which the user
may want to see, and its result, which the caller wants to consume.

## Decision

`ret` places a value in a result slot, readable as `$RET`. The binding
operator `x := cmd` clears the slot, runs the command, and binds whatever it
holds. Nothing forks. `ret` prints nothing; a function that should print uses
`echo`.

Builtins and module commands are told when they are being bound, so they fill
the slot silently instead of writing to stdout — `files := ls -q src` gives an
array rather than text to be re-parsed.

## Consequences

Function calls become cheap enough to use freely, and results keep their shape:
`ret left right` binds an array, not a string that has to be split. Output and
result are separate concerns.

`$( )` still works and still forks, so nothing is taken away. The cost is a
second calling convention to learn, and a function written for hibr does not
return values usefully to a bash caller.

---

[← decisions](README.md)
