# include — headers

| file | audience | contents |
|---|---|---|
| `hibr.h` | **public** | Types, macros and the module ABI. This is what a module compiles against, and the only header installed by `make install` |
| `pri.h` | internal | Declarations shared across `src/`, the token set, and the `lex` struct |
| `re.h` | internal | Regex declarations. glibc's `regex.h` cannot be parsed by tcc, so the pieces actually used are declared here |

## The ABI

`HIBR_ABI` in `hibr.h` is checked when a module loads; a mismatch is refused
rather than risked. It is currently **3**.

Changing any public type, the meaning of any public function, or the name of
any exported symbol means bumping it. The last bump was the rename from `nsh`
to `hibr`, which changed every exported symbol —
[0015](../docs/adr/0015-the-name-is-hibr.md).

Public macros are guarded with `#ifndef` so a module can override one without
having to undefine it first.

See [the module documentation](../docs/modules.md) for what a module can reach.
