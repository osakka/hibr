# 0003 — `((expr))` yields a value, not a failure

Status: accepted

## Context

In bash, `((i++))` returns status 1 when the expression evaluates to zero. Under
`set -e` that kills the script. So `((i++))` with `i` at 0 is a live grenade in
any careful script, and the usual workaround — `((i++)) || true` — is noise
that obscures the intent.

## Decision

An arithmetic statement's status is the value of its expression, and it never
trips `set -e`.

## Consequences

Counters and increments can be written plainly. Nobody has to remember the
zero-is-failure rule or litter code with `|| true`.

The cost is that `((expr))` can no longer be used as a test under `set -e` in
the way bash allows. Use `[[ ]]` or `if ((expr))` when you want the status,
which reads better anyway.

---

[← decisions](README.md)
