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

## An empty expansion still disappears

`-S` used to make an empty expansion produce one empty argument. It no longer
does, which puts it exactly where zsh has been for decades:

| | `$empty` | `$multi` | `$glob` |
|---|---|---|---|
| bash, and hibr by default | 0 args | 2 args | 1 |
| zsh | 0 args | 1 arg | 1 |
| hibr with `-S` | 0 args | 1 arg | 1 |

That rule was the one place `-S` was stricter than either, and it bought none
of the protection this record used to claim for it — an empty `$dir` in
`$dir/*` was never what `-S` guarded against. What it cost was every
`cmd $OPTIONAL_FLAGS` in every script. A quoted `"$empty"` still gives one
empty argument, in both modes, as everywhere else.

## Should it be the default

Measured, which is what this record previously said had not been done. Realistic
snippets were run under both settings, grouped by what the expanded variable
held, before and after the empty rule changed:

| what the variable holds | changed under `-S`, before | after |
|---|---|---|
| a single word | 0% | 0% |
| a glob pattern | 0% | 0% |
| nothing at all | 29% | **0%** |
| several words | 31% | 31% |

`-S` now changes a working script in exactly one way: it stops an expansion
splitting. That is the whole feature, and the remaining 31% is scripts that
meant to split — `for x in $LIST`, `cmd $FLAGS`. In the 169 system scripts on
this machine, 78% contain an unquoted expansion and 22% use one of those idioms
directly.

So it stays opt-in. A third of a very common idiom is still too much to change
by default, and the case for `-S` was never that splitting is rare — it is that
splitting is rarely *meant* at the call site where it bites. Turning it on for
a script you are writing is cheap; turning it on for scripts someone else wrote
is not.

---

[← decisions](README.md)
