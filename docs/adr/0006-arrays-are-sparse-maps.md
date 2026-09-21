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

`unset` is the only builtin reading it today. `local` rejects a subscripted
name outright, and `export`, `read` and `[[ -v ]]` do not parse subscripts at
all — they are unaffected by this change and remain a separate item.

The cost is a discipline in `expand.c`: every field is emitted through `xout`
or `xoutq` so the field vector and the mask vector cannot drift apart. A
desync would make `unset` delete the wrong key, so `xargv` compares the two
lengths and drops the mask rather than trust it.

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

---

[← decisions](README.md)
