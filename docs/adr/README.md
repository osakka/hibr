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
| [0004](0004-regex-captures-go-to-M.md) | Regex captures land in `M` (and, since 0.65, `BASH_REMATCH`) | accepted, amended |
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
| [0015](0015-the-name-is-hibr.md) | The name is hibr | superseded |
| [0016](0016-privileges-are-dropped-never-gained.md) | Privileges are given up, never taken | accepted |
| [0017](0017-one-namespace-for-options.md) | One namespace for options, and extglob is always on | accepted |
| [0018](0018-a-coprocess-is-an-endpoint.md) | A coprocess is an endpoint like any other | accepted |
| [0019](0019-the-console-display-assumes-xterm.md) | The console display assumes an xterm and skips terminfo | accepted |
| [0020](0020-windows-are-drawn-not-composited.md) | Windows are painted back to front; an app is a hibr file | accepted |
| [0021](0021-lengths-and-slices-count-characters.md) | `${#s}` and `${s:i:n}` count characters, not bytes | accepted |
| [0022](0022-the-reading-is-now-highly-intuitive-bash-like-runtime.md) | The reading is now Highly Intuitive Bash-like Runtime | accepted |
| [0023](0023-strict-is-per-file.md) | Strict checks are asked for per file | accepted |
| [0024](0024-a-type-can-allow-empty.md) | A type ending in `?` also accepts empty | accepted |
| [0025](0025-agent-mode.md) | Agent mode: JSON errors, `set -u`, strict expansion, no terminal, timeouts | accepted |
| [0026](0026-a-script-runs-as-it-is-read.md) | A script runs as it is read; `checkfirst` parses it whole first | accepted |
| [0027](0027-a-dry-run-refuses-what-it-cannot-show-is-harmless.md) | A dry run refuses what it cannot show is harmless | accepted |
| [0028](0028-a-theme-is-data.md) | A theme is data, kept as JSON | accepted |
| [0029](0029-mkdir-and-rm-are-builtins.md) | mkdir, rm and mv are builtins | accepted |
| [0030](0030-text-direction-and-shaping-come-from-one-unicode-version.md) | Text direction and shaping come from one Unicode version, in a module | accepted |
| [0031](0031-the-console-draws-right-to-left-text-in-display-order.md) | The console draws right-to-left text in display order | accepted |
| [0032](0032-the-desktop-is-translated-at-its-widgets.md) | The desktop is translated at its widgets, English as the key | accepted |
| [0033](0033-the-layout-mirrors-as-a-layer-over-the-settings.md) | The layout mirrors as a layer over the settings | accepted |
| [0034](0034-calendar-systems-are-data.md) | Calendar systems are data, in a folder | accepted |

---

[← documentation index](../README.md) · [← project README](../../README.md)
