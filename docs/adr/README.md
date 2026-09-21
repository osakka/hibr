# Decisions

hibr is bash-flavoured, not bash-compatible. Where it diverges it does so on
purpose, and this is where those purposes are written down — one record per
decision, in the order they were taken.

A record states what the situation was, what was decided, and what that
decision costs. The cost matters as much as the benefit; a decision with no
downside is usually a decision nobody had to make.

| | | |
|---|---|---|
| [0001](0001-build-with-tcc-and-no-dependencies.md) | Build with tcc, depend on libc and libdl only | accepted |
| [0002](0002-errexit-is-scoped.md) | `set -e` is scoped to the tested pipeline | accepted |
| [0003](0003-arithmetic-status-is-a-value.md) | `((expr))` yields a value, not a failure | accepted |
| [0004](0004-regex-captures-go-to-M.md) | Regex captures land in `M` | accepted |
| [0005](0005-results-travel-in-a-slot.md) | `ret` does not print; results travel in a slot | accepted |
| [0006](0006-arrays-are-sparse-maps.md) | Arrays are sparse maps | accepted |
| [0007](0007-brace-expansion-is-literal.md) | Brace expansion is literal-only | accepted |
| [0008](0008-globstar-is-always-on.md) | `**` is always on and never follows symlinks | accepted |
| [0009](0009-strict-expansion-is-opt-in.md) | Strict expansion is opt-in | accepted |
| [0010](0010-tls-verifies-and-is-dlopened.md) | TLS verifies by default, and libssl is loaded on demand | accepted |
| [0011](0011-args-exits-only-at-top-level.md) | `args` ends the script only at the top level | accepted |
| [0012](0012-prompt-is-an-in-process-hook.md) | The prompt comes from an in-process hook | accepted |
| [0013](0013-prompt-config-uses-the-map-model.md) | Prompt configuration uses the shell's own map model | accepted |
| [0014](0014-read-git-objects-natively.md) | Read git's object store natively rather than forking git | accepted |
| [0015](0015-the-name-is-hibr.md) | The name is hibr, read as Hackable In-process Bash Runtime | accepted |

---

[← documentation index](../README.md) · [← project README](../../README.md)
