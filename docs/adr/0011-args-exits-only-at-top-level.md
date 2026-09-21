# 0011 — `args` ends the script only at the top level

Status: accepted

## Context

`args` parses a command line against declarations made with `opt`. When the
arguments are wrong, the right thing at the top of a script is to print the
reason and the usage and stop. But the same call inside a function, or inside
`try`, must not take the whole script down — the caller asked for the error to
be catchable.

## Decision

On a parse error, `args` prints the reason and the usage and returns 2. At the
top level of a script that also ends the script. Inside a function, or inside
`try`, it just returns.

## Consequences

The common case — `opt` declarations then `args "$@"` at the top of a script —
behaves like a well-mannered command-line program with no error handling
written by hand. Library functions that parse their own arguments stay
catchable.

The cost is that the same call has two behaviours depending on where it
appears, which has to be documented because it cannot be guessed.

---

[← decisions](README.md)
