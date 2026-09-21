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

The examples fold `-` to `_` before using a header name as a key. Whether the
rule should change — most plausibly so that a quoted subscript is always
literal, which is how quoting suppresses splitting and globbing everywhere
else — is open, and is a change to the language rather than a bug fix.

---

[← decisions](README.md)
