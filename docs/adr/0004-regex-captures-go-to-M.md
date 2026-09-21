# 0004 — Regex captures land in `M`

Status: accepted

## Context

bash puts the results of `[[ =~ ]]` into `BASH_REMATCH`. The name is long
enough that people assign it to something shorter immediately, and it is tied
to bash by name in a shell that is not bash. hibr also has a `match` builtin
that needs somewhere to put captures, and having two different destinations for
the same idea would be worse than either.

## Decision

Both `[[ =~ ]]` and `match` write their captures to the map `M`, with `M[0]`
the whole match and `M[1]`… the groups. `match` takes an optional name to use a
different map.

## Consequences

One place to look, short enough to use directly: `${M[1]}`. The `match` builtin
and the `=~` operator behave identically, which is one fewer thing to know.

The cost is that scripts written against `BASH_REMATCH` need editing, and
`BASH_REMATCH` is not provided as an alias — providing it would invite scripts
that only work by accident.

---

[← decisions](README.md)
