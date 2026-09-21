# 0007 — Brace expansion is literal-only

Status: accepted

## Context

Brace expansion happens before variable expansion, which means `{$a,$b}` in
bash produces the two literal strings `$a` and `$b` and then expands them —
except the ordering interacts with quoting and word splitting in ways that
surprise people regularly. Supporting variables inside braces properly means
running expansion twice, with all the re-splitting hazards that implies.

## Decision

Brace expansion operates on literal text only. `{a,b}`, `{1..9}`, `{01..12}`,
`{a..e}` and `{1..9..2}` work, nested and multiplied. `{$a,$b}` is left exactly
as written.

## Consequences

The common uses all work and the expansion order has no surprises in it. No
input is expanded twice, so nothing can be re-split or re-globbed by accident.

The cost is that a script doing `cp file.{$old,$new}` gets a literal, and has
to be written as two words or built with a variable. This is a real divergence
and it is the one most likely to be noticed when porting.

---

[← decisions](README.md)
