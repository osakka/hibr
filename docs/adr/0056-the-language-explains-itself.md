# ADR 0056: the language explains itself -- an `inspect` module

Status: proposed (Gitea #532), for the owner's yes on this page before any
code.

## Context

hui-assistant (and any tool that answers questions about hibr code, a person
or a model) needs facts about scripts: which functions there are, what they
take, what they call, what is wrong with them. Its prototype reached for the
parser directly, `hibr_parse` and the node structures, and #532 asked for
them to be made public.

They should not be, for four reasons found in the code:

- `HIBR_ABI` covers struct layout, not meaning. The tree's meaning lives in
  bits the core changes for speed: flags ORed into `p->op` (`V_INDF`),
  type bits in a parameter node's `f` (`TY_*`), arithmetic parts rewritten
  on first evaluation (`AX_PLAIN`, `AX_QKEY`). The node kinds and ops are in
  `pri.h`. Publishing nodes would freeze all of it.
- Using the tree safely means marking and releasing `s->ar` and putting back
  `st`, `keep` and `src` -- a contract where one slip by an outside module
  corrupts the running shell.
- Syntax errors go to `lg`, which in a desktop is `desktop.log`: an
  assistant reading a half-typed file would log an error a keystroke, and
  the suites fail on any.
- A compile-time `HIBR_PARSE_API` beside a minimum release is a second
  compatibility axis, where an add-on is meant to depend on the ABI alone.

Meanwhile hibr's own tree already answers these questions four times over,
each with its own regular expressions -- `tests/strictvars.py`,
`tests/deps.py`, `tests/census.py`, and the arity and duplicate-name checks
in `tests/540-examples.t` -- and this file's sister, CLAUDE.md, records what
re-parsing a language with regexes has cost.

And the parser turned out to allow something better than exposing it
(checked 2026-11-12):

- It reads no session state that changes what it finds: aliases are never
  expanded at parse time, and `s->it` decides only how much text is kept.
  The same bytes give the same facts in any session.
- It can parse silently: `sk_cmd` already does, with `hibr_lv = -1`.
- It can parse into an arena of its own: `sk_cmd` gives its lexer one.
- It can take one top-level command at a time and resume after a bad one,
  as `hibr_run` does with `p_line`, so a half-typed file still yields what
  parses before and after the damage, and every span comes from the
  lexer's position with no new field on `node`.

## Decision

hibr ships an in-tree module, **`inspect`**, that is the one authority on
what a hibr script says. It offers the interface `"inspect"` (through
`hibr_provide`, so a consumer asks with `hibr_require` and a version
mismatch is refused by the machinery that refuses every other) and a
builtin of the same name. Both are one implementation producing records;
anything human-readable renders those records. No consumer parses hibr
itself.

The authoritative request, record, option and limit contract is [docs/inspect.md](../inspect.md), with examples exercised by the documentation harness. It is written there once; this ADR records the rationale and acceptance process.

## How it is proven before it is frozen

Version 1 is not released until all of these pass in the gate:

- **The whole tree**: every `.hibr` in hibr inspects `ok` with nothing on
  stderr, and the largest is measured against the desktop's 50 ms callback
  budget (ADR 0049). A whole-tree index is a job, cached by content
  identity, never a frame's work.
- **Round trip**: a function's span, inspected on its own, gives the same
  record.
- **Parity, one check at a time**: `540-examples.t`'s duplicate-name and
  arity checks move onto `inspect` first, then `strictvars.py`, `deps.py`
  and `census.py`, each with a test that the old and new answers agree
  before the old one is removed.
- **Hostile input**: into the parser fuzzer and the sanitizer quick list
  with its leak pass; deep nesting, NUL, a file at the size cap.
- **Non-execution**: a file whose top level writes a marker, runs `$( )`,
  `source`s another, defines and uses an alias and names an unloaded
  module's builtin -- inspected, the marker is absent and nothing was
  dlopened (`/proc/self/maps` unchanged).
- **The shell untouched**: `$?`, `RET` and `LINENO` the same after a call,
  including one made inside a function under `:=` and one on invalid text.

## Extension points, named and not built

- **Cross-file**: an index over a set of files, reporting its scope -- the
  files read, the names left unresolved -- so "nothing calls this" is
  always qualified by where it looked.
- **What would this do**: a function run under `--plan` (ADR 0027) in a
  child process, returning the refused actions as `observed` records.
- **Which tests reach this**: `tests/coverage.tsv` joined by function name.
- **Live state**: the running desktop's own answers through `desktop ctl`,
  as `observed`.
- **Variables**: reads, writes and declarations as syntax, kept apart from
  any runtime value.

## Consequences

- hibr becomes the one place that knows its language; the assistant, lint,
  the catalogue and hibr's own checks read the same facts.
- A module, so a shell that never inspects anything pays nothing (the
  "tools are modules" rule).
- The parser gains one thing: an error message and position stored in the
  lexer as well as logged, so a silent parse can return it. No `node`
  field, no ABI change.
- The contract is records and requests, so the tree can go on changing for
  speed underneath it.
- hui-assistant waits for a release with `inspect` 1 rather than shipping
  against a private declaration now.

## Alternatives

- **Promote `hibr_parse` and the nodes** (#532 as first asked): refused for
  the reasons in Context.
- **An opaque handle with accessor functions over the tree**: safer than raw
  nodes, but every accessor freezes a piece of the tree's meaning, and the
  caller-state contract remains. Records cost one copy and free the tree.
- **Leave it to each tool**: four regex parsers in this tree already, each
  wrong in its own way at some point.

---

[← documentation index](../README.md) · [← project README](../../README.md)
