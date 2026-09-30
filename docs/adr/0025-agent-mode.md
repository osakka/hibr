# 0025 — Agent mode

Status: accepted

## Context

A script that a program runs -- a language model's, a build's, a harness's
-- has no person watching it. What a person reads easily is what that
program parses badly: an error as prose, a prompt it cannot answer, a
command that hangs with nobody to press ctrl-c. Harnesses work around this
from outside, and the shell can do it from inside.

## Decision

`hibr --agent`, or `set -o agent` from inside a script, turns on four things:

- **Errors are one line of JSON each** on stderr, keyed by level, with what
  hibr knows exactly and nothing else:
  `{"error":"missing: unbound variable","file":"run.sh","line":2,"source":"echo \"$missing\""}`.
  A syntax error adds the column. There are no error codes or fix hints:
  hibr has none to give honestly, and inventing them per error site would be
  a larger change than the mode itself.
- **`set -u`**: an unset variable is an error. Not `set -e`: hibr scopes
  errexit differently from bash (ADR 0002), and a model trained on bash's
  rules would be surprised whichever way the mode chose, so errexit stays the
  script's own decision.
- **Strict expansion**, `set -S`: an expansion never splits or globs, so an
  unquoted `$f` handed to `rm` is one argument.
- **Nothing waits on a terminal**: when standard input is a terminal it is
  replaced by `/dev/null`, so `read`, a pager or an editor gets the end of
  input at once. Piped input is untouched.

And `HIBR_TIMEOUT=seconds` bounds each foreground process: when it runs
out, the process and everything it started get TERM, then KILL two seconds
later, and its status is 124, as timeout(1) reports. It is off unless set,
so a long build is never cut short by surprise.

## Consequences

The timeout reaches processes, not the shell itself: a builtin, a function
or a `while` loop running in the shell is not a process that can be ended,
and is not bounded. To make "everything it started" true, a non-interactive
shell under a timeout puts each foreground child in a process group of its
own, which it would otherwise share with the shell.

Writing it exposed three places hibr differed from bash for everyone, and
they are fixed for everyone: a syntax error now exits with status 2, not 0;
an empty `then`, `do`, `{ }` or `( )` is a syntax error naming the token, not
accepted (an empty `while` body looped for ever); and an error is written
after whatever the script printed before it, not ahead of it.

The one position that was not exact: hibr parsed a whole script before
running it, where bash runs each command as it reads it. Since 0.60 hibr
reads as bash does, and agent mode keeps the whole-script check by turning
on `checkfirst` -- see [0026](0026-a-script-runs-as-it-is-read.md).

---

[← decisions](README.md)
