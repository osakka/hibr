# 0027 — A dry run refuses what it cannot show is harmless

Status: accepted

## Context

An agent that writes a script, or a person handed one, wants to know what it
will do before it does it. Harnesses build this from wrappers around bash; a
wrapper can be walked around by a subshell. The shell itself sees every
redirection, every program it starts and every builtin that reaches outside,
so it is the place to ask. bash has nothing like it.

A kernel boundary -- Landlock on Linux, a sandbox profile on macOS -- would
contain programs as well as the shell. The machine this was built on has
Landlock switched off, so it could not be built or tested there; a policy
that runs for real stays in the backlog.

## Decision

`hibr --plan` runs the script's own logic and refuses, recording each, every
effect it cannot show is harmless:

- A writing redirection opens `/dev/null` instead; a network endpoint opens
  `/dev/null` and is recorded as a connection. Reading is not refused: a plan
  that pretended not to read would mislead about what it touched.
- A program runs only if it is known to read and nothing more, judged from
  its name and, for `sed`, `sort`, `find`, `git`, `uniq`, `tee`, `date`,
  `file` and `mktemp`, its arguments. Arguments are all it judges: a `w`
  command inside a `sed` script writes a file and is not caught, for the
  same reason `awk` is refused -- what a program's own language does cannot
  be read from its text without guessing, and a guess that fires on prose
  (`s/x/now what/`) refuses honest scripts. Anything else fails with status 1 and no output. A failure
  rather than a pretended success: an empty `$(mktemp -d)` made every later
  path relative to the wrong directory, and a plan listing writes to the
  wrong paths is worse than one that stops.
- A scratch `$TMPDIR` belongs to the plan and is removed when it ends.
  `mktemp`, file tools whose every path is inside it, and redirections into
  it are real, so temporary files work.
- `awk` and every program that runs another (`env` with arguments,
  `timeout`, `xargs`, `nice`, `sudo`, `sh -c`) are refused: whether they
  write cannot be told from their arguments, and allowing one allows all.
- `exec`, `kill` (except `-0`), `listen`, `mod load` and `need` are refused:
  a module's builtins can do anything. The exception (0.99.7) is a module
  that only computes -- `math` and `md`, a list in `pl_pure` -- loaded by
  name, never by path, and found on the module path but never in the
  current folder: finding a module means opening it, and opening a shared
  object runs its constructors, so a folder holding a planned script must
  not be able to supply one. Root's search already skipped the folder for
  the same reason.
- The record goes to a copy of standard error taken at the start, on a high
  descriptor closed on exec, so `2>/dev/null` and `exec 2>&-` cannot hide it.
  With `--agent` each record is a line of JSON keyed `plan`.

## Consequences

A plan is honest about where it stops: the first refused program a script
depends on ends a `set -e` script there. It is not a sandbox -- a program on
the read-only list runs for real, and a mistake in that list is a real
write -- and it says so. Extending the list is a code change, reviewed like
one, not a setting.

---

[← decisions](README.md)
