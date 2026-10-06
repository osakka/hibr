# Modules

Modules are shared objects exporting one `hibr_module` symbol. The ABI version
-- `HIBR_ABI` in `include/hibr.h`, and `$HIBR_ABI` in the shell -- is checked
when a module loads.

```c
#include "hibr.h"

int m_hello(sh *s, int ac, char **av) { hibr_ret(s, "hello"); return HIBR_OK; }

const hibr_bi hello_bi[] = { { "hello", m_hello, "say hello" }, HIBR_BI_END };
HIBR_MODULE("hello", "1.0", "greeting builtins", hello_bi, 0, 0);
```

```text
tcc -Iinclude -shared -o build/mods/hello.so mods/hello.c
hibr -c 'mod load ./build/mods/hello.so; mod list; mod drop hello'
```

## Finding and loading them

| | |
|---|---|
| `mod load <name>` | search for it; the `.so` suffix is added if missing |
| `mod load <path>` | open that file, no search; the suffix is optional here too |
| `mod list` | the modules loaded in this shell |
| `mod avail`, `mod list -a` | every module that *could* be loaded |
| `mod drop <name>` | unload one |
| `mod find <builtin>` | which module would provide that builtin, without loading it |
| `need <name>...` | load whatever offers each interface or module name, or fail saying which is missing |

A name containing a `/` is a path and is opened directly. Everything else is
searched for: the current directory, then each entry of `HIBR_MODPATH`, then
the install directory. A shell running as root skips the first two and consults
only the install directory — see
[0016](adr/0016-privileges-are-dropped-never-gained.md).

`mod avail` walks that same search order and reports what it finds, one line
per module plus its builtins:

```text
$ mod load sys; mod avail
NAME         VERSION  ABI      STATE      PATH
cat          0.21     abi 16   available  /usr/local/lib/hibr/cat.so
             cat   offers highlight
console      0.21     abi 16   available  /usr/local/lib/hibr/console.so
             console   offers display
...
lint         1.0      abi 16   available  /usr/local/lib/hibr/lint.so
             offers lint
ls           0.68     abi 16   available  /usr/local/lib/hibr/ls.so
             ls
...
sys          0.68     abi 16   loaded     /usr/local/lib/hibr/sys.so
             drop, epoch, sleepms, state, upper
```

The second line of each entry is its builtins, then the interface it offers,
if any.

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

**A module names the interface it offers** in `HIBR_MODULE_P`, and the shell
uses that to find it: `hibr_require(s, "display", 1)` with nothing loaded walks
the module path, reads each descriptor without initialising it, and loads the
first that says it offers `display`. So a tool asks for what it needs and never
has to name the module that has it.

**Modules can offer tables of functions to each other.** They are opened
`RTLD_LOCAL`, so a symbol in one is invisible to the rest on purpose; the way
across is `hibr_provide(s, name, version, table)` in the provider's init and
`hibr_require(s, name, version)` in the user's. The version must match exactly,
and a provider withdraws its offer in its finaliser so that dropping it makes
its users refuse rather than call into an unloaded object. `most` uses the
`console` module this way, asking for "display" rather than for a backend.

Reference modules in `mods/`:
- **`most`** pages files or a pipe, built on the display interface.
- **`hvi`** hibr's vi, on the same interface: gap buffer, linear undo, and
  the cat's colourer asked for through the registry.
- **`mon`** a system monitor over `/proc`, on the display interface.
- **`sysinfo`** what this machine is, printed once, with a picture.
- **`trace`** traces a route with no privileges, using `IP_RECVERR` rather than
  a raw socket, and puts the hops in a map. `trace -l` keeps probing and shows
  loss, jitter and a round-trip history per hop on the display.
  `examples/traceroute.hibr` draws a route on a world map.
- **`cat`** is `cat` byte for byte in a pipe, and adds a gutter, visible control
  bytes and lexical colour when standard output is a terminal.
- **`console`** owns the terminal so other tools do not have to: an alternate
  screen, a cell grid that redraws only what changed, panes, colour and decoded
  keys. It offers the **display** interface in `mods/display.h`, so a
  framebuffer or SDL backend could replace it without the tools changing. See
  [full-screen programs](display.md).
- **`http`** registers `/dev/http/host/port/path`: the request is made on open
  and the descriptor is positioned at the body.
  `while read l; do …; done </dev/http/127.0.0.1/8080/status`
- **`prompt`** builds the prompt out of segments; see
  [The prompt](prompt.md).
- **`ls`** is an in-process `ls` with columns, `-l -a -A -h -t -S -r -d -1 -F`
  and colour, whose listing also lands in `$RET` — `files := ls -q src`.
- **`pty`** runs a program on a pseudo terminal and drives it: keys in,
  output out, resize, signal, exit status. It offers **pty**.
- **`term`** is a terminal emulator: a program's screen as cells, drawn into a
  window. It offers **terminal**, and is what the desktop's terminal window
  is built on.
- **`hold`** keeps a session alive when its terminal goes: detach, log off,
  attach again, from more than one terminal at once.
- **`img`** decodes an image and draws it as terminal cells; libpng is opened
  on first use.
- **`db`** is a small column store: typed columns in one file, rows
  appended, filters and count/sum/min/max/avg over them, with zone maps
  that let a filter skip whole groups of rows; results print or come back
  as a map. See [`mods/db/README.md`](../mods/db/README.md).
- **`lint`** holds the rules behind `hibr --explain`; it adds no builtin and
  offers **lint**.
- **`darwin`** (macOS only) gives `cpu` and `mem` from the kernel's own
  counters, with no fork.
- **`sys`** is a minimal example. **`http`** also carries two test builtins,
  `msum` and `oops`.

`examples/ls-report.hibr` loads a module, uses it through the result slot,
declared arguments, maps, regex and JSON, and unloads it.

---

[← documentation index](README.md) · [← project README](../README.md)
