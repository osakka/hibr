# 0004 — Regex captures land in `M`

Status: accepted, amended in 0.65

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

The cost was that scripts written against `BASH_REMATCH` needed editing.

## Amended in 0.65: `=~` fills `BASH_REMATCH` as well

The original reason for leaving `BASH_REMATCH` out -- that providing it would
invite scripts that only work by accident -- did not survive measurement.
`tools/llm-measure/` asked two models to write a regex capture for hibr
without its reference page, three times each: all six read `BASH_REMATCH`,
got nothing, and printed a bare `|`. Those are not scripts working by
accident; they are bash scripts failing for a name. `=~` now copies its
captures into `BASH_REMATCH` after filling `M`, and clears it on a miss, as
bash does. `M` stays the documented place and `match` fills only `M`, since
bash has no `match`. The copy costs one array assignment per capture, only
on a `=~`.

---

[← decisions](README.md)
