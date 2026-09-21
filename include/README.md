# include — headers

| file | audience | contents |
|---|---|---|
| `hibr.h` | **public** | Types, macros and the module ABI. This is what a module compiles against, and the only header installed by `make install` |
| `pri.h` | internal | Declarations shared across `src/`, the token set, and the `lex` struct |
| `re.h` | internal | Regex declarations. glibc's `regex.h` cannot be parsed by tcc, so the pieces actually used are declared here |

## The ABI

`HIBR_ABI` in `hibr.h` is checked when a module loads; a mismatch is refused
rather than risked. It is currently **5**.

Changing any public type, the meaning of any public function, or the name of
any exported symbol means bumping it. The last bump added `at` to `var`, the
attributes `declare` sets — integer, nameref, and a declared type. The one
before added `amask` to `sh` —
[0006](../docs/adr/0006-arrays-are-sparse-maps.md). Appending to `sh` leaves
every existing offset alone, so an old module would still run, but a module
built against the new header and loaded by an old shell would read past the
struct; the check is what makes that impossible.

A running shell reports the same number in `$HIBR_ABI`, so a script can check
before it loads a module rather than after:

```
[ "${HIBR_ABI:-0}" = 5 ] || { echo "this module wants ABI 5" >&2; exit 1; }
mod load mine
```

Public macros are guarded with `#ifndef` so a module can override one without
having to undefine it first.

See [the module documentation](../docs/modules.md) for what a module can reach.
