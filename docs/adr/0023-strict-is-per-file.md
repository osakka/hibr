# 0023 — Strict checks are asked for per file

Status: accepted

## Context

Two mistakes cost the desktop more afternoons than any others, and a shell
lets both through without a word.

A function defined twice replaces the first. The drag-and-drop handler was
once named `dt_drop`, which was already the function that draws the open menu,
so every frame "drew the menu" by dropping whatever was being dragged. Nothing
failed; it stopped working. `tests/540-examples.t` finds a duplicate now, but
only in the files it knows about, and only after the fact.

A variable assigned in a function without `local` becomes a global. Two
windows of one app then share it: two terminals became one shell typing into
both, two calculators held one sum. The fix is always one word; finding where
it was needed is the afternoon.

Perl met both with `use strict`: a program that asks for it is refused the
constructs that are usually mistakes, and one that does not ask is unaffected.
Asking is the point. A script written for bash must keep running as it did.

## Decision

`strict` is a builtin that turns on named checks for **the file that runs
it**:

<!-- not run: three forms of the one command, side by side -->
```sh
strict                        # all three
strict functions vars         # just these
strict off vars               # and off again
```

- **`strict functions`** — defining a function a second time *in the same
  file* is refused. The first definition stays, the command fails with status
  2, and the message names both lines:
  `wm/menus.hibr:313: dt_drop is already defined at line 98 (strict functions)`.
  Defining it again in *another* file is still allowed, because that is how a
  file of your own replaces a bundled one.
- **`strict vars`** — inside a function, an assignment that would *create* a
  global is refused: the name is not local to this function or any function
  that called it, and no variable of that name exists yet. The assignment is
  not made and the command fails with status 1. Assigning a global that
  already exists — one set when the file was loaded, or made with
  `declare -g` — is allowed. `local`, `declare`, `typeset`, `readonly` and
  `export` create variables freely; they are how a name is declared.
- **`strict expansion`** — `set -S` for this file's code only: an expansion
  is never split or globbed. `set -S` itself still means it everywhere.

"The file" is `BASH_SOURCE`: the script, a file being sourced, or, for a
function, the file it was defined in, wherever it is called from. So a strict
library's functions are checked when a lax script calls them, and a lax
library's are not when a strict script does. A command line (`hibr -c`,
interactive input) is a file of its own for this purpose.

Every message names a file and a line, so the parser records a line on each
node. That is also what `$LINENO` needed, and hibr now has one: the line of
the command running, counted in its own file — for a function, in the file it
was defined in; for a `$( )` or an `eval`, from the line it appears on.

## Consequences

Nothing changes for a script that never says `strict`. The costs it pays are
one test on each function call and each `source` — whether any file has
asked for anything — and one on each variable *created*, not each assigned.
They are measured in the release that ships this, with instruction counts.

A refused check fails the command, not the script, unless `set -e` is on; a
strict file that wants to stop at the first one says `set -e` as well, the
same as for any other failure.

`strict vars` catches a forgotten `local` the first time it would create a
global, which is the bug that shipped. It does not catch a function
overwriting a global that already exists: that is sometimes the function's
job, and telling the two apart would mean declaring every global first,
which Perl asks for and a shell script has never had to do.

`struct node` gains a line field, so the module ABI goes from 14 to 15: a
module built for 14 is refused until it is rebuilt. Every bundled module is
rebuilt with the shell.

---

[← decisions](README.md)
