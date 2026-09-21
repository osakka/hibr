# 0008 — `**` is always on and never follows symlinks

Status: accepted

## Context

bash hides recursive globbing behind `shopt -s globstar`, off by default, so
`**` silently means `*` until someone remembers to enable it. A pattern that
quietly matches the wrong thing is worse than one that fails. Separately,
bash's globstar follows symlinked directories, which can loop forever on a
self-referential link.

## Decision

`**` always crosses directories, with no option to enable. It does not descend
into symlinked directories.

## Consequences

`**/*.c` means what it looks like, in every hibr script, without a preamble.
It cannot loop.

The cost is that a bash script relying on `**` meaning `*` — which is to say,
relying on a bug — behaves differently. And there is no way to ask for a
symlink-following recursive glob; that needs `find`.

---

[← decisions](README.md)
