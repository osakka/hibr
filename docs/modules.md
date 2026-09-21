# Modules

Modules are shared objects exporting one `hibr_module` symbol. The ABI version
(currently 7) is checked when a module loads.

```c
#include "hibr.h"

int m_hello(sh *s, int ac, char **av) { hibr_ret(s, "hello"); return HIBR_OK; }

const hibr_bi hello_bi[] = { { "hello", m_hello, "say hello" }, HIBR_BI_END };
HIBR_MODULE("hello", "1.0", "greeting builtins", hello_bi, 0, 0);
```

```
tcc -Iinclude -shared -o build/mods/hello.so mods/hello.c
mod load ./build/mods/hello.so;  mod list;  mod drop hello
```

A module can reach everything the language can:

| | |
|---|---|
| `hibr_get`, `hibr_set` | scalars |
| `hibr_getp`, `hibr_setp`, `hibr_count`, `hibr_list` | maps, by subscript path |
| `hibr_ret`, `hibr_retn` | the result slot, one value or several |
| `hibr_fail` | `$ERRMSG`, as `fail` sets it |
| `hibr_run` | run shell source |
| `hibr_dial` | open a TCP or UDP connection |
| `hibr_scheme`, `hibr_unscheme` | register a `/dev/<name>/` protocol |
| `lg`, arenas, `str`, `vec` | the shell's own utilities |

**Schemes** let a module add a protocol rather than a command: once registered,
`/dev/<name>/…` works anywhere a file does — `<`, `exec 3<`, `while … done <`,
pipelines. The built-in `/dev/tcp/` and `/dev/tls/` use the same mechanism.
Module builtins take precedence over the built-in ones; lookup order is alias,
function, module, builtin, `PATH`.

Reference modules in `mods/`:
- **`http`** registers `/dev/http/host/port/path`: the request is made on open
  and the descriptor is positioned at the body.
  `while read l; do …; done </dev/http/127.0.0.1/8080/status`
- **`prompt`** builds the prompt out of segments; see
  [The prompt](prompt.md).
- **`ls`** is an in-process `ls` with columns, `-l -a -A -h -t -S -r -d -1 -F`
  and colour, whose listing also lands in `$RET` — `files := ls -q src`.
- **`sys`** is a minimal example.

`examples/ls-report.hibr` loads a module, uses it through the result slot,
declared arguments, maps, regex and JSON, and unloads it.

---

[← documentation index](README.md) · [← project README](../README.md)
