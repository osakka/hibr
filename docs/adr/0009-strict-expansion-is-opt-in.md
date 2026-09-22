# 0009 — Strict expansion is opt-in

Status: accepted, measured

## Context

Unquoted expansion splitting and globbing is a large source of shell bugs. A
variable holding `my report.txt` becomes two arguments; one holding `*.txt`
becomes every matching file. Neither is usually what the author meant, and
neither is visible at the call site.

## Decision

`set -S` changes one rule: the result of an expansion is never split and never
globbed. What you write splits; what expands does not. It is off by default.

```sh
f='my report.txt'; p='*.txt'
show $f       # default: two arguments      strict: one
show $p       # default: every .txt file    strict: the literal *.txt
```

## What it does not do

It does not stop a pattern written *in* the word from globbing, because that
pattern is not the result of an expansion:

```sh
dir=
show $dir/*   # every entry in /, with or without -S
```

An earlier version of this record claimed the opposite — that `rm -rf $dir/*`
could not become `rm -rf /*` under `-S`. That was wrong, and it was the whole
stated justification for the option, which is a good argument for running the
example before writing the record. `-S` protects the value that *arrives* in a
word, not the word you typed.

## Should it be the default

Measured, which is what this record previously said had not been done. Realistic
snippets were run under both settings and grouped by what the expanded variable
held:

| what the variable holds | snippets using it | answer changed under `-S` |
|---|---|---|
| a single word | 247 | 1 (0%) |
| several words | 247 | 79 (31%) |
| a glob pattern | 247 | 1 (0%) |
| nothing at all | 247 | 74 (29%) |

For the ordinary case — a variable holding one word — `-S` is invisible. All of
the change is in two idioms, and both are everywhere in real scripts:
`for x in $LIST` relies on splitting, and `cmd $OPTIONAL_FLAGS` relies on an
empty expansion disappearing. In the 169 system scripts on this machine, 78%
contain an unquoted expansion and 22% use one of those two idioms directly.

So it stays opt-in. Roughly a third of either idiom changes meaning, which is
too much to take by default for a benefit that is invisible in the common case.

## One thing worth revisiting

hibr's `-S` makes an empty expansion produce one empty argument. zsh, which has
suppressed splitting by default for decades, makes it produce none:

| | `$empty` | `$multi` | `$glob` |
|---|---|---|---|
| bash, and hibr by default | 0 args | 2 args | 1 |
| zsh | 0 args | 1 arg | 1 |
| hibr with `-S` | **1 arg** | 1 arg | 1 |

The empty rule is the one place `-S` is stricter than zsh, and it is the whole
of the fourth row above — a separate population from the third, not part of it.
It does not buy the protection this record used to claim for it. Aligning with
zsh would remove one of the two ways `-S` changes a working script, leaving it
protecting exactly what it protects today: `for x in $LIST` would still have to
be rewritten, and `cmd $OPTIONAL_FLAGS` would not.

---

[← decisions](README.md)
