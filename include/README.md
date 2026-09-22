# include — headers

| file | audience | contents |
|---|---|---|
| `hibr.h` | **public** | Types, macros and the module ABI. This is what a module compiles against, and the only header installed by `make install` |
| `pri.h` | internal | Declarations shared across `src/`, the token set, and the `lex` struct |
| `re.h` | internal | Regex declarations. glibc's `regex.h` cannot be parsed by tcc, so the pieces actually used are declared here |

## The ABI

`HIBR_ABI` in `hibr.h` is checked when a module loads; a mismatch is refused
rather than risked. It is currently **9**.

Changing any public type, the meaning of any public function, or the name of
any exported symbol means bumping it. The last bump added `rtrap` to `sh`, for
`trap … RETURN`. Before it `IFS` was cached there, since four of every six
variable lookups in a loop were asking for it. Before
that `arena` gained a short free list, so a released block is reused by the
next command instead of going back to `malloc`. Before that `cmds` joined `sh`, where the shell remembers what it
found on `PATH`. Before that `at` joined `var`, for
the attributes `declare` sets, and `amask` joined `sh` —
[0006](../docs/adr/0006-arrays-are-sparse-maps.md). Appending to `sh` leaves
every existing offset alone, so an old module would still run, but a module
built against the new header and loaded by an old shell would read past the
struct; the check is what makes that impossible.

A running shell reports the same number in `$HIBR_ABI`, so a script can check
before it loads a module rather than after:

```
[ "${HIBR_ABI:-0}" = 9 ] || { echo "this module wants ABI 9" >&2; exit 1; }
mod load mine
```

Public macros are guarded with `#ifndef` so a module can override one without
having to undefine it first.

See [the module documentation](../docs/modules.md) for what a module can reach.
