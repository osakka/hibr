# hibr documentation

A shell that runs a useful subset of bash in about 22,900 lines of C, and adds
nested maps, JSON, regex capture, native sockets and TLS, typed functions,
results without forking, declared command-line arguments, and modules that can
add whole protocols.

Start with the [project README](../README.md) for what hibr is and why it
exists. These pages are the detail.

## Learn it

| | |
|---|---|
| [From bash to hibr, in ten minutes](tutorial.md) | **Start here.** Install, then each thing hibr does that bash cannot, one short example each |
| [hibr, for a language model](llm.md) | The whole language on one page: every difference from bash, and each addition with a run example -- written to be handed to a model |
| [The language](language.md) | Syntax, expansion, arithmetic, conditionals, maps and arrays, typed functions, declared arguments, errors, strict expansion |
| [Text, regex and JSON](data.md) | `str`, `arr`, `match`, `rsub`, and JSON over the map model — every operation, with examples |
| [Cookbook](cookbook.md) | Whole tasks solved: logs, INI files, JSON, coprocesses, daemons, CLI programs |
| [Networking](networking.md) | Sockets, TLS, `/dev/tcp` and friends, `listen`, coprocesses, and HTTP with `fetch` |
| [Interactive use](interactive.md) | Prompts including right-hand and transient, line editing, history, completion, jobs |
| [The prompt](prompt.md) | The prompt hook, the segment module, and its git support |
| [Full-screen programs](display.md) | The console display: cells, panes, colour, decoded keys |
| [Your first hour with the desktop](desktop.md) | **The desktop, start here.** A tour in five pictures: starting it, the Catalogue, windows, making it yours |
| [Windows on a console](../programs/desktop/README.md) | The desktop's full guide: windows, menus, mail, calendars, servers, more than one screen |
| [Writing for the desktop](../programs/desktop/WRITING.md) | Applications, Control Panel panes, strip modules and sessions, each a hibr script |
| [The desktop's architecture](../programs/desktop/ARCHITECTURE.md) | The three layers, the event loop, damage-based redraw, and the app contract, with real screenshots |
| [Installing desktop applications](catalogue.md) | `catalogue`: an index over HTTPS, what it trusts, what it refuses, and how to publish one |
| [Running a command as root](please.md) | `please`: sudo's job through polkit, governed by Control Panel > Administrative Access |

## Look it up

| | |
|---|---|
| [The grammar](grammar.md) | Lexical structure, EBNF, operator precedence, expansion order, patterns |
| [Builtins](builtins.md) | All 77, what each takes and what it gives back |
| `man hibr` | Its flags (`--agent`, `--explain`, `--plan`, `--checkfirst`, `--modules=`, `--login`), environment variables, files and exit status |
| `man desktop` | The `desktop` command: its options, keys, and `desktop ctl`, the control socket |
| [Modules](modules.md) | The module ABI, and writing one |
| [Sheet](sheet.md) | The spreadsheet whose formula language is hibr: formulas, cells as variables, ranges as arrays, the containment they run in, and full access for a sheet that should act |

## Build on it

| | |
|---|---|
| [Deployment](deployment.md) | `deploy.sh`: install, verify, update, roll back; and the apt, dnf, pacman and Homebrew packages |
| [Testing](testing.md) | The harness, the recording discipline, sanitizers and fuzzing |
| [Decisions](adr/README.md) | Why hibr behaves the way it does, one record per decision |
| [Backlog](backlog.md) | What should exist next, and what each piece actually needs |

## Where hibr differs from bash on purpose

Every divergence has a record. The short version:

| | |
|---|---|
| [Arrays are sparse maps](adr/0006-arrays-are-sparse-maps.md) | One container, nested, and a quoted subscript is a literal key |
| [Results travel in a slot](adr/0005-results-travel-in-a-slot.md) | `x := f` returns a value without forking |
| [Captures land in `M`](adr/0004-regex-captures-go-to-M.md) | and, for `=~`, in `BASH_REMATCH` too |
| [`set -e` is scoped](adr/0002-errexit-is-scoped.md) | and there is no `pipefail` |
| [`((expr))` is a value](adr/0003-arithmetic-status-is-a-value.md) | so it never trips `set -e` |
| [Brace expansion is literal](adr/0007-brace-expansion-is-literal.md) | `{$a,$b}` is not expanded |
| [`**` is always on](adr/0008-globstar-is-always-on.md) | and never follows a symlink |
| [One namespace for options](adr/0017-one-namespace-for-options.md) | `set -o` and `shopt` are the same table, and extended patterns need no switch |
| [A coprocess is an endpoint](adr/0018-a-coprocess-is-an-endpoint.md) | spoken to with the same verbs as a socket |
| [The display assumes an xterm](adr/0019-the-console-display-assumes-xterm.md) | no terminfo, no ncurses, and what that costs |
| [Windows are painted, not composited](adr/0020-windows-are-drawn-not-composited.md) | and an app is a hibr file that says what it needs |
| [Privileges only go one way](adr/0016-privileges-are-dropped-never-gained.md) | `drop` gives up root; hibr is never setuid |
| [Lengths count characters](adr/0021-lengths-and-slices-count-characters.md) | `${#s}` and `${s:i:n}` count characters, never bytes |
| [Strict checks are per file](adr/0023-strict-is-per-file.md) | `strict` turns on checks for the file that says it |
| [A type can allow empty](adr/0024-a-type-can-allow-empty.md) | `int?` takes a number or nothing |
| [Agent mode](adr/0025-agent-mode.md) | `--agent`: JSON errors, `set -u`, strict expansion, a timeout per process |
| [A script runs as it is read](adr/0026-a-script-runs-as-it-is-read.md) | as in bash; `--checkfirst` parses the whole of it first |
| [A dry run](adr/0027-a-dry-run-refuses-what-it-cannot-show-is-harmless.md) | `--plan` refuses what it cannot show is harmless, and lists it |
| [`mkdir`, `rm` and `mv` are builtins](adr/0029-mkdir-and-rm-are-builtins.md) | answering as GNU's do for what they implement, and handing the rest to the program |
| [`:=` takes any command's result](adr/0038-the-slot-takes-a-programs-output-too.md) | a program's output too, the way `$( )` does; a function's only through `ret` |
| [A login shell reads both](adr/0039-an-interactive-login-shell-reads-both.md) | an interactive login shell reads the profile and then `~/.hibrc` |
| [A module may answer a command](adr/0040-a-module-may-answer-a-command.md) | `HIBR_MODULES` says whether a module is loaded for a command, and before or after `PATH` |
| [`source` honours a local scheme](adr/0043-source-honours-a-local-scheme.md) | `. /dev/archive/…` works; a scheme that fetches, like `/dev/http`, is refused |
| [`read` takes a datagram whole](adr/0051-read-takes-a-datagram-whole.md) | on `/dev/udp` a line or `-n` gets the line, where bash keeps its first byte |

## Reading these pages in a browser

hibr serves them itself, with no other software involved:

<!-- not run: a server, which answers until it is stopped -->
```sh
hibr examples/httpd.hibr --root ./docs --port 8080
```

That server parses its requests with regex into maps, reads files through a
redirection, and answers without forking once — which is most of the argument
for the shell, in one example.

Conventions for working on the code — naming, memory, what not to reintroduce —
live in [CLAUDE.md](../CLAUDE.md).
