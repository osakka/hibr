# 0015 — The name is hibr

Status: superseded by [0022](0022-the-reading-is-now-highly-intuitive-bash-like-runtime.md)

The name itself, and the reasoning below for choosing **hibr** over every
other candidate, both stand. Only the four words behind the letters changed;
0022 says why.

## Context

The project was called `nsh`, which says nothing about it and sits in the most
crowded namespace in Unix — `ash dash zsh ksh csh tcsh mksh yash oksh posh
bosh lsh rsh sash hush` and more. The three-letter `?sh` space is exhausted.

The first instinct was `osh`. That is the binary name of
[Oil Shell](https://www.oilshell.org/), a long-running bash-compatible shell
whose pitch — run real bash scripts, fix the parts that are broken — is nearly
identical to this one. Taking it would mean competing for search results with
the one project most likely to be confused with this.

Three criteria were set: memorable, easy to say and spell, and unlikely to
collide. A fourth emerged during the search: the name should mean something in
English on its own, without a translation footnote.

## Decision

The name is **hibr**, read as **H**ighly **I**mproved **B**ash **R**untime.

*Highly* names the numbers: half of bash's memory, tight loops two and a half
times faster, in a 313 KB binary. *Improved* names the intent: bash's own
sharp edges — `test`'s word-splitting traps, `$BASH_REMATCH`'s awkward
capture, and the rest recorded across this directory — resolved rather than
inherited. *Bash* says what it stays close to: familiar syntax, not a
rewrite. *Runtime* carries what the original expansion called *Hackable* and
*In-process*: the module ABI lets a module register a protocol, so
`/dev/<name>/…` works anywhere a filename does, and results return without
forking ([0005](0005-results-travel-in-a-slot.md)), text and JSON are
manipulated without pipelines, and the prompt reads git's object store with
no subprocess ([0014](0014-read-git-objects-natively.md)) — a runtime that
extends the language, not just the command set, and stays in-process doing
it.

The original reading was **H**ackable **I**n-process **B**ash **R**untime,
both halves load-bearing claims rather than decoration. It described the same
two facts this one does; this expansion reads better and still says nothing
untrue.

It is also **حِبر**, Arabic for ink. That second reading is a bonus for those
who have it, not a prerequisite for anyone else.

## Consequences

Sixteen candidates were checked individually — local binaries, installed
packages, and a targeted search per name. Batched `OR` searches were tried
first and proved actively misleading: they under-report badly, and two names
cleared that way turned out to be taken when searched alone.

Most were already claimed, several by projects in this exact space: `nacre` is
[an "Intuitive Shell"](https://github.com/Ninroot/nacre), `whelk` is [a Unix
shell in Rust](https://github.com/ferris007/whelk), `qalam` is [an interpreted
language](https://github.com/ammar-ahmed22/qalam), `bask` is [a Bash
framework](https://github.com/xwmx/bask), `yash` is [a POSIX shell in
Debian](https://packages.debian.org/stable/shells/yash), `cowrie` is [an SSH
honeypot that emulates a shell](https://github.com/cowrie/cowrie), and `mysh`
is the name every student gives their first shell. A pattern held throughout:
the well-loved words are the ones other developers have already taken, in
English and in Arabic alike.

`midad` (مِداد, the classical word for ink) was adopted first and then dropped,
because to an English eye it reads as "my dad" — a flaw no amount of etymology
survives. `hibr` returned nothing anywhere.

The remaining cost is pronunciation: `hibr` ends in a consonant cluster with no
vowel, so English speakers will land on "HIB-ber" or spell it out. Spelling it
out is fine; `zsh` has been read aloud that way for thirty years.

The mechanical cost was paid at the right time. The rename touched roughly 650
macro occurrences, 21 exported symbols, five filenames and seven user-visible
names, and it broke the module ABI, which moved to 3. Doing it before any third
party had written a module made it an afternoon instead of a breaking change.
It will never be this cheap again.

---

[← decisions](README.md)
