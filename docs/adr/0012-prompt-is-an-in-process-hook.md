# 0012 — The prompt comes from an in-process hook

Status: accepted

## Context

A rich prompt is normally built by a separate program invoked from `PS1`
through `$( )`. That costs a fork and an exec before every prompt. Measured
here, the fork and exec floor alone is about 1.5 ms — nearly twice hibr's
entire startup — before the prompt program has done any work at all. For a
shell whose stated priorities begin with resident memory and startup time,
paying that on every keystroke-to-prompt is the wrong trade.

`PS1` with backslash escapes was the only mechanism available, and it cannot
express a prompt that has to compute anything.

## Decision

If `PROMPT_FN` names a function, a builtin, or a module builtin, an interactive
shell calls it in process and uses whatever it leaves in the result slot as the
prompt. `PS1` still works and is still the default when `PROMPT_FN` is unset.

The hook's output is final text. It does not go back through `\`-escape
processing or word expansion, so a `$` or a backslash in a git branch name is
safe. Two variables are set for it: `$STATUS`, the status of the last command,
and `$DURATION`, how long that command took in milliseconds.

## Consequences

A prompt that reads files, parses a git index and walks commit history costs no
processes at all. It also means a prompt can be written in hibr itself —
`fn myprompt() { ret "…" }` — which was not possible before.

Two costs. The hook runs a command between two of the user's commands, so it
has to save and restore everything a command can disturb: the last status, the
stop flag, the `try` depth, the binding flag, the expansion-error flag, and the
break, continue and return flags. Getting that list wrong means a `fail` inside
a prompt function silently swallows the user's next command. And because the
hook is called in the interactive loop, it cannot be exercised by the normal
test harness; the module's own render path is tested instead, with the loop
integration checked by hand under a pty.

---

[← decisions](README.md)
