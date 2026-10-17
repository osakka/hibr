# 0002 — `set -e` is scoped to the tested pipeline

Status: accepted

## Context

In bash, `set -e` is suspended for any command whose status is being tested —
and that suspension extends into the bodies of functions those commands call.
The result is that a script can pass `set -e` at the top and then quietly stop
checking anything several frames down, which is the opposite of what the author
asked for. This is the most common way `set -e` scripts fail silently.

## Decision

Exemption applies only to the pipeline being tested, not to the bodies of
functions it calls. Any failing stage of a pipeline fails the pipeline, so
there is no separate `pipefail` option to remember. An interactive shell
abandons the current line rather than exiting.

**Forked children keep it** (0.99.231, Gitea #270). A pipeline stage, a
`( )` subshell and a background job run with errexit as the shell that
forked them has it, and a stage or a subshell is exempt exactly when its
status is tested; only a command substitution drops it, unless
`inherit_errexit` is on, as in bash. A stage used to run with errexit off,
so `{ false; echo X; } | cat` printed `X` -- the one place `set -e` quietly
meant nothing, which is the failure this decision exists to prevent. A
child that stops says nothing, and the shell that forked it reports its
status once; each child used to print the errexit line again. And **`!`
never trips errexit**, as POSIX says: an inverted status is a test the
script wrote on purpose.

## Consequences

`set -e` means what it appears to mean, and a function called from inside `if`
still has its own failures caught.

The cost is that some bash scripts that "work" under `set -e` will stop earlier
under hibr — which is usually the bug being surfaced, but it is a behavioural
difference and not always a welcome one. Scripts that rely on the bash
behaviour need `try` around the call, or an explicit test.

---

[← decisions](README.md)
