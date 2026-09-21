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

---

[← decisions](README.md)
