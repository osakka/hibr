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
| `mod drop all` | unload every module, and say how many |
| `mod find <builtin>` | load whichever module on the search path registers that builtin; status 1 if none does |
| `need <name>...` | load whatever offers each interface or module name, or fail saying which is missing; one already loaded is met at once, with no search |

A name containing a `/` is a path and is opened directly. Everything else is
searched for: the current directory, then each entry of `HIBR_MODPATH`, then
`$XDG_DATA_HOME/hibr/mods` (`~/.local/share/hibr/mods` when that is unset),
then the system scope, `$HIBR_SYSDIR/mods`, then the install directory.

The system scope is where `catalogue --system` installs a module for every
user of the machine ([0045](adr/0045-a-thing-has-a-scope.md)), so a person's
own copy still wins over it and the package's is still the floor.
`$HIBR_SYSDIR` is set at startup to the path compiled in -- `/var/lib/hibr`
for a package, `$PREFIX/var/lib/hibr` for any other prefix -- unless the
environment already set it. The compiled path is used only when its `mods`
folder is owned by root or by whoever owns the hibr binary and nobody else
may write it; a path you set yourself is yours.

A shell running as root reads neither the current directory, nor
`HIBR_MODPATH`, nor the data folder, nor `$HIBR_SYSDIR` from the
environment: only the compiled system scope, under that ownership test, and
the install directory — see
[0016](adr/0016-privileges-are-dropped-never-gained.md). A `--plan` run
skips the current directory too, since opening a module found there would
run its code.

`$XDG_DATA_HOME/hibr/mods` is where `catalogue install` puts a module, and it,
the system scope and the current directory are searched **only when something asks for a module by name** — `mod load`,
`mod find`, `need`, `mod list`, `mod avail`. It is deliberately left out of
the index that lets a *command* load the module declaring it, because that
index is built by anything that is not a builtin, not a function and not on
`PATH` — a typo, in a script as much as at a prompt — and building it opens
every candidate to read its descriptor, which runs that object's own
constructors. So a module you installed is one you can ask for, and is never
loaded because you mistyped something. The current directory is left out of
that index for the same reason and more so (Gitea #431, 0.99.425): a typo in
`/tmp`, in a cloned repository or in a folder of libraries ran the
constructors of whatever objects were there. The index also keeps each path
absolute, so a `cd` after it was built cannot change which file a command
loads:

<!-- not run: needs a module installed in the data folder -->
```sh
csv                 # command not found: nothing autoloads from there
need csv; csv       # asked for by name, and it is there
```

The reasoning is [0044](adr/0044-a-module-can-be-installed.md).

`mod avail` walks that same search order and reports what it finds, one line
per module plus its builtins:

```text
$ mod load sys; mod avail
NAME         VERSION  ABI      STATE      PATH
cat          1.0      abi 16   available  /usr/local/lib/hibr/cat.so
             cat   offers highlight
console      1.0      abi 16   available  /usr/local/lib/hibr/console.so
             console   offers display
...
lint         1.0      abi 16   available  /usr/local/lib/hibr/lint.so
             offers lint
ls           1.0      abi 16   available  /usr/local/lib/hibr/ls.so
             ls
...
sys          1.0      abi 16   loaded     /usr/local/lib/hibr/sys.so
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
| `hibr_conntimeo` | how long a connect may wait, in milliseconds: `HIBR_CONNTIMEO`, or the caller's default |
| `hibr_title`, `hibr_title2` | rename the process, as the `title` builtin renames a script: the sentence `ps` shows, and with `hibr_title2` the 15-byte name `top` and `pgrep` read |
| `hibr_provide`, `hibr_require`, `hibr_unprovide` | offer a table of functions to other modules, ask for one, and withdraw the offer |
| `hibr_wrall` | write all of a buffer to a descriptor the module owns, without a peer that has gone killing the process |
| `hibr_scheme`, `hibr_schemef`, `hibr_unscheme` | register a `/dev/<name>/` protocol |
| `hibr_onexec`, `hibr_offexec` | run a function just before `exec` replaces the process, and withdraw it |
| `hibr_slurp`, `hibr_rdfd` | a whole file, or everything left on a descriptor, appended to a `str`, a signal never taken for the end (0.99.433) |
| `s_sq`, `s_sqb`, `u8put` | a string as one single-quoted word, or the inside of one; a code point as UTF-8 (0.99.433) |
| `hibr_tlswrap` | wrap a connected socket in TLS through the shell's relay: the descriptor to use instead, the relay's pid to collect (0.99.434) |
| `hibr_jstr`, `hibr_jws` | a quoted JSON string, every escape resolved, and JSON whitespace passed over -- for a module reading a small JSON file of its own (0.99.434) |
| `lg`, arenas, `str`, `vec` | the shell's own utilities |

**A child the module starts ends before an exec in place.** Restart
Desktop -- and any script's `exec` -- replaces the shell without ending it,
so a module's helper process outlives the image that started it and, once
it exits, is a zombie of one that does not know it: the web engine left one
per restart. `hibr_onexec(s, fn, arg)` in the init registers `fn`, which
ends the module's children and waits for each by pid; `exec` runs every
hook, the last registered first, just before it replaces the process, and
only in the process that registered it, so a subshell's `exec` never ends
its parent's children. The finaliser calls `hibr_offexec` with the same
pair. A child that is meant to cross the exec, such as a terminal's program,
is simply not registered. A module that must still load on a hibr older
than 0.99.428 looks the function up with `dlsym(RTLD_DEFAULT,
"hibr_onexec")`.

**Write with `hibr_wrall`, not `write`**, for a socket or a pipe the module
holds. A plain write to one whose far end has closed raises SIGPIPE, whose
default ends the process -- and for anything inside the desktop the process
is every window at once, so the `EPIPE` handling written around such a write
is correct and never reached. `hibr_wrall` answers the failure instead. An
ordinary redirection a *script* writes through is left alone, deliberately:
`hibr script | head -3` has to end when `head` does.

**Schemes** let a module add a protocol rather than a command: once registered,
`/dev/<name>/…` works in any redirection — `<`, `exec 3<`, `while … done <`,
pipelines -- the same redirection path the built-in `/dev/tcp/`, `/dev/udp/`,
`/dev/tls/` and `/dev/unix/` take, though those four are the shell's own and
are not registered schemes.
Module builtins take precedence over the built-in ones; lookup order is alias,
function, module, builtin, `PATH`.

**`source` asks one thing more of a scheme, and the default answer is no.**
Since 0.99.140 `. /dev/<name>/…` runs a scheme's bytes in the current shell,
which makes a tarball a library — and `source` runs what it reads, so it does
that only where the module has said opening its scheme connects to nothing:
`hibr_schemef(s, nm, fn, HIBR_SCH_LOCAL)`. `hibr_scheme` claims nothing and
means no, so the archive module says it and the http module, which fetches by
being opened, does not. Pick by what your open does, not by what you would
like `source` to allow — see
[0043](adr/0043-source-honours-a-local-scheme.md).

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

Reference modules in `mods/`, a selection -- [`mods/README.md`](../mods/README.md)
lists every one, file by file:
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
- **`http`** registers `/dev/http/host/port/path`: the request is made on open,
  through the `fetch` module, and the descriptor reads the body from its
  start; `HTTP_STATUS` is the reply's status. A redirection on a program is
  opened in the program's own child, so read the status after one on a
  builtin -- `read`, `mapfile`, `exec {fd}<`.
  `while read l; do …; done </dev/http/127.0.0.1/8080/status`
- **`fetch`** is the shell's HTTP/1.1 client (0.99.362): the `fetch`
  builtin for scripts, and the **fetch** interface in `mods/fetch.h` --
  installed beside `hibr.h` as `include/hibr/fetch.h` -- for a module to
  make requests through rather than carry a client of its own: a streamed
  body, a callback for the headers, a cancel flag. Kept connections,
  gzip and deflate, redirects, Basic and Digest, proxies and cookie jars.
  `http` and `dav` are built on it (0.99.363). Call it from a child, never
  a thread, since TLS forks the relay. See
  [`mods/fetch/README.md`](../mods/fetch/README.md).
- **`dav`** is a WebDAV client over `fetch`: servers, listings and
  conditional uploads. See [`mods/dav/README.md`](../mods/dav/README.md).
- **`prompt`** builds the prompt out of segments; see
  [The prompt](prompt.md).
- **`ls`** is an in-process `ls` with columns, `-l -a -A -h -t -S -r -d -1 -F`
  and colour (`-G` for colour where standard output is not a terminal), whose
  listing also lands in `$RET` — `files := ls -q src`, where `-q` prints
  nothing.
- **`pty`** runs a program on a pseudo terminal and drives it: keys in,
  output out, resize, signal, exit status. It offers **pty**.
- **`term`** is a terminal emulator: a program's screen as cells, drawn into a
  window. It offers **terminal**, and is what the desktop's terminal window
  is built on.
- **`hold`** keeps a session alive when its terminal goes: detach, log off,
  attach again, from more than one terminal at once.
- **`img`** decodes an image and draws it as terminal cells, or as pixels
  where the terminal takes them; libpng and libturbojpeg are opened on
  first use. `img size` answers a picture's size; `img save in out.png`
  writes a PNG, a JPEG or a PPM as a PNG (0.99.404); `img keep` asks the
  display to keep the picture it last placed under the text at a rectangle,
  the part of it placed included, drawing nothing either way (0.99.414);
  and `img stats` counts how those keeps went and why one did not
  (0.99.415).
- **`copy`** copies or moves trees in a job (0.99.371): `copy run [-m] [-i
  ms] [-s statusfile] [-k stopfile] dst src...` writes each item under a
  hidden `.hibr-part` name and renames it when whole, keeps a one-line
  status file current, and stops cleanly when the stop file appears or on
  SIGTERM, removing the item in hand; `copy rename` does only the renames.
  Files' copy window is built on it. See [`mods/README.md`](../mods/README.md).
- **`proctab`** prints every process under `/proc` in one call (0.99.399),
  `pid utime stime uid rss name` a line, into `$RET` as well; it fails where
  there is no `/proc`. It shares its reader with `mon`.
- **`contain`** runs a program that may read and compute but not act on
  the world (0.99.427, ADR 0052): `contain exec DIR -- CMD [ARG...]` closes
  every descriptor above 2, lets the program write only under `DIR`, opens
  it no socket, refuses it `chmod`, `chown`, times and extended attributes
  and any signal to another process, then execs `CMD` in that state, which
  every program it starts inherits. On Linux that is Landlock and a seccomp
  filter, on macOS the system sandbox; where neither is there it refuses
  rather than run the command uncontained. `contain probe` says what the
  system offers (`landlock 4`, `sandbox`) and fails where it offers
  nothing. Sheet's formulas run in it.
- **`db`** is a small column store: typed columns in one file, rows
  appended, filters and count/sum/min/max/avg over them, with zone maps
  that let a filter skip whole groups of rows; results print or come back
  as a map. See [`mods/db/README.md`](../mods/db/README.md).
- **`lint`** holds the rules behind `hibr --explain`; it adds no builtin and
  offers **lint**. Among them: `bind-no-ret`, a function bound with `:=` that
  never calls `ret` (0.99.368), and `callback-forks`, a `$( )`, a backquote
  or a `<( )` in a function the desktop calls back (0.99.400) -- see
  [the language](language.md).
- **`darwin`** (macOS only) gives `cpu` and `mem` from the kernel's own
  counters, with no fork.
- **`sys`** is a minimal example. **`http`** also carries two test builtins,
  `msum` and `oops`.

`examples/ls-report.hibr` loads a module, uses it through the result slot,
declared arguments, maps, regex and JSON, and unloads it.

---

[← documentation index](README.md) · [← project README](../README.md)
