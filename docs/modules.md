# Modules

Modules are shared objects exporting one `hibr_module` symbol. The ABI version
(currently 11) is checked when a module loads.

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

## Finding and loading them

| | |
|---|---|
| `mod load <name>` | search for it; the `.so` suffix is added if missing |
| `mod load <path>` | open that file, no search; the suffix is optional here too |
| `mod list` | the modules loaded in this shell |
| `mod avail`, `mod list -a` | every module that *could* be loaded |
| `mod drop <name>` | unload one |

A name containing a `/` is a path and is opened directly. Everything else is
searched for: the current directory, then each entry of `HIBR_MODPATH`, then
the install directory. A shell running as root skips the first two and consults
only the install directory — see
[0016](adr/0016-privileges-are-dropped-never-gained.md).

`mod avail` walks that same search order and reports what it finds, one line
per module plus its builtins:

```
$ mod avail
NAME         VERSION  ABI      STATE      PATH
ls           0.21     abi 11   available  /usr/local/lib/hibr/ls.so
             ls
sys          0.21     abi 11   loaded     /usr/local/lib/hibr/sys.so
             drop, epoch, sleepms, state, upper
```

It reads each candidate's descriptor and closes it again; nothing is
initialised and no builtin is registered, so listing is not loading. The state
says what would happen to that **file**:

| state | |
|---|---|
| `available` | it would load |
| `loaded` | this exact file is loaded now |
| `elsewhere` | a module of this name is loaded, from a different file |
| `wrong abi` | built against another ABI; `mod load` will refuse it |
| `no descr` | a shared object with no `hibr_module` symbol |
| `unreadable` | it would not open at all |

A file that a directory earlier in the search order already supplied under the
same name is left out, because that is the one a bare `mod load` would get.

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
- **`cat`** is `cat` byte for byte in a pipe, and adds a gutter, visible control
  bytes and lexical colour when standard output is a terminal.
- **`screen`** owns the terminal so other tools do not have to: an alternate
  screen, a cell grid that redraws only what changed, panes, colour and decoded
  keys. See [full-screen programs](screen.md).
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
