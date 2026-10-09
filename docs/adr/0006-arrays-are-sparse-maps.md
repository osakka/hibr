# 0006 — Arrays are sparse maps

Status: accepted

## Context

bash has indexed arrays and associative arrays as separate types, declared
differently, with different subscript rules. Nesting is not possible at all:
there is no array of arrays, no map of maps. Scripts that need structure end up
encoding it into key names, or shelling out to a real language.

## Decision

There is one container. A variable is a scalar or an ordered map of entries,
and each entry is itself a scalar or another map. An array is simply a map
whose keys are numeric. Subscripts chain, so nesting needs no new syntax:
`h[users][omar][role]=admin` creates the intermediate levels as it goes.

Arrays are sparse, matching bash's counts and keys: `a=(one two three); a[7]=x`
has keys `0 1 2 7` and a count of 4.

## Consequences

JSON maps onto the model exactly, which is why `json parse` and subscript
access reach the same data. Nesting costs no new syntax and no new type. The
count and key behaviour matches bash, so array-heavy scripts port cleanly.

The cost is that subscripts need a disambiguation rule, because one syntax now
serves two purposes. A subscript of only digits is a literal key; one
containing an operator is evaluated arithmetically; a bare name is used for its
value when that value is numeric and taken literally otherwise. That rule has
to be learned, and it is the one place the unified model shows its seams.

The seam is sharper than it first looks, and writing the HTTP examples found
it within the hour. A hyphen is an operator, so `head[content-type]` evaluates
as `content - type` — two unset names — and reads or writes the key `0`. It
does so silently: a write followed by a read appears to work, because both
sides mangle the subscript identically, and only `${!head[@]}` reveals the key
is `0`. Quoting does not help, because the subscript is evaluated after
expansion, by which point the quotes are gone.

The consequence worth naming: `json parse` stores literal keys, so a document
with a `content-type` field creates a key that **no subscript can address** —
only `json get` reaches it. bash, which has no arithmetic subscripts, stores
the literal key here.

One difference from bash follows from the word *ordered*. A map here keeps the
order its keys were first assigned in, so `${!h[*]}` answers the same way on
every run and on every machine:

```sh
e[k]=1; e[0]=2; e[zz]=3; e[b]=4
echo "${!e[*]}"        # the order they were set
```

```output
k 0 zz b
```

bash answers `0 k b zz` for the same input: its associative arrays are hash
tables and it iterates buckets, which is stable for a given build and defined
nowhere. A script that wants a particular order has to sort in both shells; a
script that only wants *an* order gets a reproducible one here.

A negative subscript follows from the same place. `${h[-1]}` counts back from
the highest numeric key whatever else the map holds, because there is only one
container; bash has indexed arrays and associative ones, and on the latter it
reads `-1` as the literal key. Both differences were found by `tests/diff.py`,
which is what that harness is for.

## Amendment — a quoted subscript is a literal key

The seam now has the escape hatch quoting already implies everywhere else:
**a quoted subscript is a literal key, never arithmetic.** `head["content-type"]`
and `head["$name"]` address the literal key. `head[content-type]` is unchanged
and still evaluates as arithmetic, so nothing that worked before means
something new — the rule is added to, not replaced.

Reaching the subscript with quoting meant carrying the quote mask further than
expansion used to carry it. A word's per-byte mask already existed for
splitting and globbing; it now survives into the expanded field, so `ex_asg`
and `b_unset` can ask which bytes of a subscript were quoted. For a builtin the
mask arrives as `sh.amask`, parallel to `argv`, and that field is the ABI 4
bump.

Quoting does not survive an alias. `al_run` re-quotes each already-expanded
argument before re-parsing it, so wrapping them in `'…'` would have made every
subscript reached through an alias literal — `alias u=unset; u a[i]` would have
stopped resolving `i`. `al_quote` therefore escapes character by character
instead, which protects the metacharacters without masking the subscript's own
bytes; the cost is that a quoted subscript passed through an alias is
evaluated, not taken literally. Escaping matches what the quotes did for
splitting and globbing, checked against bash.

`unset`, `read` and `[[ -v ]]` all read it, through one helper — `bi_keys` —
which also handles a negative subscript. `export` refuses a subscripted name
outright, as bash does, rather than accepting it and doing nothing. `local`
rejects one too, since it wants a plain name.

It survives an alias as well. An alias re-emits arguments that are then
re-lexed, so `al_quote` is given the old mask and reproduces it: the runs that
were quoted go back inside quotes and the rest is escaped per character. Quote
everything and `u a[i]` stops resolving `i`; escape everything and
`u h["a-b"]` stops reaching the literal key.

The cost is a discipline in `expand.c`: every field is emitted through `xout`
or `xoutq` so the field vector and the mask vector cannot drift apart. A
desync would make `unset` delete the wrong key, so `xargv` compares the two
lengths and drops the mask rather than trust it.

## Amendment — only a subscript *written* as a bare `@` or `*` is the all-form

The rule above has a corner it did not say anything about, and for eighteen
releases the shell got it wrong: `m[*]` and `m[@]` mean every entry, so an
entry whose **key** is `*` or `@` could not be read back. `${m["*"]}`
answered the whole map joined and `${#m["*"]}` answered how many entries
there were rather than how long that one was. bash reads both as the key,
which is the rule this very document states; the assignment and `unset`
were right all along, because those go through `bi_keys`, where the quote
mask was already honoured.

`xkeys` was deciding on the subscript's **expanded text** — if it came out
as `@` or `*`, that was the all-form. It asks the word instead now
(`xallw`): one unquoted run of text that is exactly `@` or `*`. So

- `m[*]` and `m[@]` are the all-form, as before;
- `m["*"]` is the entry named `*`, quoting meaning what it means everywhere
  else;
- **`m[$i]` with `i=*` is also that entry**, because it was not written as
  the all-form — which is what bash does, and the half of this that quoting
  alone would not have fixed.

Deciding on a value where the question is about what was written is the
shape of the bug, and it was in one of the two splitters rather than both:
`bi_keys` had the rule and `xkeys` did not. `tests/196-star-key.t` compares
every form of it against bash. Gitea #148, fixed in 0.99.118.

Masks are built only when the command asks for them. `xargv` expands the first
word, and unless that word names a builtin listed by `bi_mask` it never
allocates a mask at all — so an ordinary command pays for the feature only a
`strcmp`. `command` is on that list because it forwards to a builtin, and it
passes `sh.amask + 1` along with the shifted `argv`; `eval`, an alias and an
expanded command name all re-enter `xargv` with the real name and need no
special case. Measured on `for ((i=0;i<N;i++)); do echo "$i"; done`, the whole
change costs about 2% more instructions, against 7% before the gate.

The keys `json parse` creates are now addressable by subscript, which was the
sharpest consequence of the original rule. The examples no longer need to fold
`-` to `_`.

## An array declared `-A` follows bash

bash evaluates an indexed array's subscript as arithmetic and takes an
associative array's as a literal key. hibr has one kind of array, so until
0.57 every unquoted subscript followed the rules above, and bash's everyday
`declare -A seen; seen[$line]=1` broke on any line holding a dash, a plus or
a space: the key became arithmetic, landed on `0`, or failed to parse. A
variable made with `declare -A`, `local -A` or `typeset -A` now takes every
subscript as its literal key after expansion -- `h[content-type]`, `h[$k]`
with `k=a-b`, `h[-1]` -- exactly as bash does, and every other array keeps
the rules above. It is the `A_ASSOC` attribute bit, so nothing about a map
changes but how its subscripts are read. The check runs only when a
subscript would otherwise be evaluated, which keeps it off the common path:
0.04% on a loop reading a map.

In a compound assignment an element's key is read to its closing `]`, so
`m=([c d]=2)` has the key `c d` and `m=([k]=$v)` keeps a spaced `$v` whole,
as bash does; a `[key]=value` element is expanded like an assignment, and
only plain elements split.

---

[← decisions](README.md)
