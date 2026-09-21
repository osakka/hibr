# 0009 — Strict expansion is opt-in

Status: accepted

## Context

Unquoted expansion splitting and globbing is the single largest source of shell
bugs. `rm -rf $dir/*` becomes `rm -rf /*` when `$dir` is empty. The fix is
obvious — expansions should not split or glob — but it changes the meaning of
almost every script ever written, including the one place where the old
behaviour is load-bearing: an empty expansion currently produces no argument at
all, and under the strict rule it produces one empty argument.

## Decision

`set -S` changes one rule: the result of an expansion is never split and never
globbed. What you write splits; what expands does not. It is off by default.

## Consequences

With `-S`, `count $f` on `my file.txt` passes one argument, and `rm -rf $dir/*`
cannot become `rm -rf /*`.

The cost is the empty case: `count $empty` passes one empty argument rather
than none, which is a genuine behavioural difference and the reason this is not
the default yet. Whether it becomes the default depends on running real scripts
under both, which has not been done at the scale required to decide.

---

[← decisions](README.md)
