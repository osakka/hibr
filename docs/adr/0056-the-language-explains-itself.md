# ADR 0056: the language explains itself -- an `inspect` module

Status: accepted (0.99.507, the owner, 2026-11-12). Built by hui-assistant
(itdlabs/hui-assistant#1) as a branch against this tree; hibr reviews,
gates and releases it (Gitea #532).

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

### What it returns

Records, never nodes -- typed maps through the result slot (`r := inspect
...`), the same shapes as JSON (`inspect ... -j`), quiet when bound. Every
record carries:

- **`source`**: the path as given, the size in bytes and a 64-bit content
  identity (FNV-1a, no new dependency), so a record is never attached to
  text that has since changed;
- **`span`**: byte offsets, start inclusive and end exclusive, and line and
  column (1-based; columns count bytes, said so), of the text it is about;
- **`known`**: how the fact is known, one of
  - `syntax` -- seen in the text (a definition, a call written out);
  - `declared` -- the author said so (a signature, a type, a default, the
    one-line comment above a function, which says what it is *meant* to
    do and proves nothing about what it does);
  - `inferred` -- by a named rule (below), the rule's id given;
  - `observed` -- at runtime, with where and when; never produced by
    reading a file, reserved for a later, opt-in request.

  These are never merged and never upgraded silently.

A result also has a **`state`**: `ok`, `empty`, `incomplete` (the text runs
out inside a construct), `invalid` (a syntax error), or `unreadable` (the
file could not be opened, or was refused). Diagnostics are records with a
message and a span, and the records that did parse are returned beside
them, each top-level command being parsed on its own. Nothing is written to
stderr.

### The requests (version 1)

- `inspect functions FILE` -- each function: name, span, the declared
  parameters (name, type, default, rest), the comment above it, and its
  body's span.
- `inspect calls FILE [FUNCTION]` -- each command a function (or the top
  level) runs, in order: the name as written and how it resolves --
  `function` (one this file defines), `builtin`, `module` (a builtin of a
  module already loaded), or `unresolved`; with `:=` binding marked. A call
  whose name is an expansion (`"${ap}_draw"`) is reported as exactly that,
  `dynamic`, with its expression, and stays unresolved.
- `inspect refs FILE` -- each literal argument that names a function the
  file defines (`dt_ticker dt_nattick`, `dt_item "Move" m dt_grab move`,
  `dt_baripress app dt_appmpress`): syntax, and most of the desktop's
  callback edges with no convention at all.
- `inspect check FILE` -- the parse's own diagnostics, and lint's findings
  in the same schema, each with a stable rule id and the ADR or CLAUDE.md
  section it rests on, so a tool cites the rule rather than paraphrasing it.

Version 1 says nothing about what a variable holds; variable references,
cross-file relationships and runtime observation are extension points
(below), not promises.

### Conventions are data

The desktop's callback contract (`<app>_draw`, `_key`, `_click`, ... the
table in `programs/desktop/ARCHITECTURE.md`) lives today in
`tests/540-examples.t`'s code. It becomes one JSON rule file,
`programs/desktop/conventions.json`, which that test and `inspect calls
--rules FILE` both read; an edge derived from it is `inferred`, naming the
rule. The module itself stays about the language; the desktop's rules
stay the desktop's.

### Safe by construction

- **Nothing runs.** No command, substitution, `source`, alias, trap or
  module init. A command name is resolved against builtins, modules
  already loaded and the file's own functions -- never the autoload index,
  which dlopens every candidate. `unresolved` never means "does not
  happen".
- **The caller's shell is untouched**: the parse is silent and in an arena
  of its own, freed before returning, so `$?`, `RET`, `LINENO`, `s->ar`,
  `keep` and `src` are as they were, error or not.
- **Files are opened the way `source` opens them**: a local scheme, so a
  catalogue bundle can be inspected inside `/dev/archive/...` before it is
  installed; a scheme that fetches (`/dev/http/...`) is refused by name.
- **Privacy by default**: names, spans, signatures and comments. A string
  literal or a body comes back only when asked for by span -- these
  records may be sent to a model.
- **Bounded**: a size cap and the parser's own depth limit; a refused file
  is `unreadable`, never a crash.
- **No public structs**: the C table is a handful of functions returning
  typed maps; version 1 grows by new requests, not by changed fields.

### Example

`inspect functions programs/desktop/wm/place.hibr -j`, worked out from the
file as it is at 0.99.505 (the module's own output will be held to this):

```text
{
  "inspect": 1,
  "state": "ok",
  "source": {"path": "programs/desktop/wm/place.hibr", "size": 5767,
             "id": "fnv1a64:c59ee660265ec3e5"},
  "functions": [
    {
      "name": "dt_place",
      "known": "syntax",
      "span": {"start": 1249, "end": 5766, "line": 28, "col": 1,
               "endline": 185},
      "params": [
        {"name": "h", "type": "int", "known": "declared"},
        {"name": "w", "type": "int", "known": "declared"}
      ],
      "summary": {"text": "Where a window of h rows and w columns goes, as \"row col h w\" -- h and w shrunk to fit the display if they must.",
                  "known": "declared", "line": 26}
    }
  ],
  "diagnostics": []
}
```

and `inspect calls programs/desktop/wm/place.hibr dt_place`, the first few:

```text
line 32  dt_ptop     unresolved   bound (top :=)
line 40  read        builtin
line 64  ret         builtin
line 85  dt_hidden   unresolved
line 86  dt_away     unresolved
line 94  cs_y        unresolved   bound (sr :=)
line 95  cs_width    unresolved   bound (sw :=)
```

`dt_ptop` and the rest are `unresolved` because this file does not define
them and reading it loads nothing; asked about the whole desktop (an
extension below) they resolve to `wm/state.hibr` (`dt_ptop`),
`wm/windows.hibr` (`dt_hidden`), `wm/workspaces.hibr` (`dt_away`) and
`wm/strip.hibr` (`cs_y`, `cs_width`).

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
