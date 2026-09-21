# 0017 — One namespace for options, and extglob is always on

Status: accepted

## Context

bash keeps shell options in two places that do not know about each other.
`set -o` holds `errexit`, `nounset`, `xtrace` and their kin; `shopt` holds
`nullglob`, `extglob`, `globstar`, `expand_aliases`. Asking the wrong one is an
error rather than an answer: `shopt errexit` and `set -o nullglob` both fail,
and which name lives where has to be memorised.

`extglob` is worse than misfiled. It changes how the parser reads `@(`, so it
has to be switched on *before* the line that uses it is parsed. A script with
`shopt -s extglob` and an extended pattern on the same line is a syntax error,
and so is one that turns it on in a function that is parsed as a whole. It is
an option whose value has already been used by the time it is set.

## Decision

There is one namespace. Every option is reachable by either spelling: `set -o
nullglob` and `shopt -s errexit` both work, and `shopt` with no arguments lists
all of them, whichever half of bash they came from.

Extended patterns — `?(…) *(…) +(…) @(…) !(…)` — are always available, in
`case`, in `[[ ]]`, and in filename globbing. There is nothing to switch on.

Options that cannot move say so. `shopt -u extglob` fails with a message rather
than appearing to succeed; so does `shopt -s pipefail`, which
[0002](0002-errexit-is-scoped.md) rules out.

## Consequences

The parse-time trap cannot happen, because there is no state to get the order
wrong with. A script written for bash that says `shopt -s extglob` first still
works: setting an option that is already on is not an error.

A script that relies on turning one of these *off* will fail, loudly. That is
the cost, and it is the same one [0008](0008-globstar-is-always-on.md) already
accepted for `**`: an option that is always on is one fewer thing for a reader
to check, and the behaviour it selects is the one worth having.

`nullglob`, `nocaseglob`, `dotglob`, `failglob` and `nocasematch` are real
switches and stay switchable, since each genuinely changes what a correct
script wants.

The remaining divergence is that hibr reports `extglob` as on when bash would
report it off, so a script testing `shopt -q extglob` to decide whether it may
use extended patterns gets a different, and correct, answer.

---

[← decisions](README.md)
