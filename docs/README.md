# hibr documentation

A shell that runs a useful subset of bash in ~13,000 lines of C, and adds
nested maps, JSON, regex capture, native sockets and TLS, typed functions,
results without forking, declared command-line arguments, and modules that can
add whole protocols.

Start with the [project README](../README.md) for what hibr is and why it
exists. These pages are the detail.

## Learn it

| | |
|---|---|
| [The language](language.md) | Syntax, expansion, arithmetic, conditionals, maps and arrays, typed functions, declared arguments, errors, strict expansion |
| [Text, regex and JSON](data.md) | `str`, `arr`, `match`, `rsub`, and JSON over the map model |
| [Networking](networking.md) | Sockets, TLS, `/dev/tcp` and friends, `listen`, coprocesses |
| [Interactive use](interactive.md) | `~/.hibrc`, line editing, history, completion, job control |
| [The prompt](prompt.md) | The prompt hook, the segment module, and its git support |

## Look it up

| | |
|---|---|
| [The grammar](grammar.md) | Lexical structure, EBNF, operator precedence, expansion order, patterns |
| [Builtins](builtins.md) | All seventy, what each takes and what it gives back |
| [Modules](modules.md) | The module ABI, and writing one |

## Build on it

| | |
|---|---|
| [Deployment](deployment.md) | `deploy.sh`: install, verify, update, roll back |
| [Testing](testing.md) | The harness, the recording discipline, sanitizers and fuzzing |
| [Decisions](adr/README.md) | Why hibr behaves the way it does, one record per decision |

## Where hibr differs from bash on purpose

Every divergence has a record. The short version:

| | |
|---|---|
| [Arrays are sparse maps](adr/0006-arrays-are-sparse-maps.md) | One container, nested, and a quoted subscript is a literal key |
| [Results travel in a slot](adr/0005-results-travel-in-a-slot.md) | `x := f` returns a value without forking |
| [Captures land in `M`](adr/0004-regex-captures-go-to-M.md) | not `BASH_REMATCH` |
| [`set -e` is scoped](adr/0002-errexit-is-scoped.md) | and there is no `pipefail` |
| [`((expr))` is a value](adr/0003-arithmetic-status-is-a-value.md) | so it never trips `set -e` |
| [Brace expansion is literal](adr/0007-brace-expansion-is-literal.md) | `{$a,$b}` is not expanded |
| [`**` is always on](adr/0008-globstar-is-always-on.md) | and never follows a symlink |
| [One namespace for options](adr/0017-one-namespace-for-options.md) | `set -o` and `shopt` are the same table, and extended patterns need no switch |
| [A coprocess is an endpoint](adr/0018-a-coprocess-is-an-endpoint.md) | spoken to with the same verbs as a socket |
| [Privileges only go one way](adr/0016-privileges-are-dropped-never-gained.md) | `drop` gives up root; hibr is never setuid |

## Reading these pages in a browser

hibr serves them itself, with no other software involved:

```sh
hibr examples/httpd.hibr --root ./docs --port 8080
```

That server parses its requests with regex into maps, reads files through a
redirection, and answers without forking once — which is most of the argument
for the shell, in one example.

Conventions for working on the code — naming, memory, what not to reintroduce —
live in [CLAUDE.md](../CLAUDE.md).
