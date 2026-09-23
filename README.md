# hibr — Highly Improved Bash Runtime

**hibr** runs a useful subset of bash syntax in about 13,000 lines of C and a
313 KB binary, in a little over half of bash's memory and running tight loops
two and a half times faster than bash. It is built with `tcc` and depends on nothing but libc
and libdl.

The name says what it is. **Highly**: half of bash's memory, tight loops two
and a half times faster, in a 313 KB binary — the numbers above. **Improved**:
bash's own sharp edges, resolved rather than inherited — `test`'s word-splitting
traps, `$BASH_REMATCH`'s awkward capture, a dozen more in
[the decision records](docs/adr/README.md). **Bash**: familiar syntax, not a
rewrite. **Runtime**: the module ABI lets a module add a *protocol*, not just
a command — register a scheme and `/dev/<name>/…` works anywhere a filename
does — and results come back without forking, text and JSON are manipulated
without pipelines, and the prompt reads git's object store with no subprocess
at all. It is also **حِبر**, Arabic for ink, which is what you write with.

It is not a drop-in replacement for bash. Some behaviour differs on purpose,
where bash is error-prone; every one of those divergences is written down in
[the decision records](docs/adr/README.md). What it adds — nested maps, JSON,
regex capture, native networking and TLS, typed functions, results without
forking, declared command-line arguments, protocol-level modules, and a prompt
that reads git's object store without forking anything — is the reason to use
it.

```
make                 # builds ./build/hibr and the modules in mods/
make check           # runs the test suite
./deploy.sh          # build, test, install, verify, keep current
make TLS=0           # build without TLS support

./build/hibr script.sh args...
./build/hibr -c 'echo $((6 * 7))'
./build/hibr -n script.sh   # parse only: report syntax errors, run nothing
./build/hibr -d 3           # log level: 0 error, 1 warn, 2 info, 3 debug, 4 trace
./build/hibr --help | --version
```

## Measurements

Same host; the loop is `while [ $i -lt 50000 ]; do i=$((i+1)); done`. Times are
the best of eleven runs. Memory is the shell's own `VmHWM`, read without
forking, as the median of twenty-five runs — a single reading is worthless
here, because run-to-run spread is about 180 kB either way.

| | hibr | dash | bash |
|---|---|---|---|
| binary, stripped | 313 KB | 122 KB | 1235 KB |
| resident memory at startup | 1792 kB | 1680 kB | 3104 kB |
| resident memory after the loop | 1796 kB | 1712 kB | 3120 kB |
| the loop | 90 ms | 76 ms | 233 ms |
| 5000 function calls, results via `:=` | **23 ms** | — | — |
| the same through `$( )` | 1218 ms | 1159 ms | 2168 ms |
| startup, `-c true` | 1.05 ms | 1.01 ms | 2.82 ms |

Read honestly. Against bash hibr is a little over half the memory and two and a
half times the speed on a tight loop. Against dash it is close on startup and
within 20% on the loop, and still about 110 kB heavier — and dash is a far
smaller language, so being close is the claim, not being ahead. The loop gap
was 1.7x before a round of profiling and is 1.2x now.

Where the memory goes is worth knowing: the heap at startup is only 29 kB of
that 1792, so memory here means the binary, and the binary means how much
language there is. There is no allocator trick left that would move it.

The row that is not a near-miss is the fifth. Returning a value through `$( )`
costs a fork per call in every shell; `:=` costs none, which is where the 53x
comes from. That is the argument for the whole in-process design, in one line.

The row that is not a near-miss is the fifth. Returning a value through `$( )`
costs a fork per call in every shell; `:=` costs none, which is where the 45x
comes from. That is the argument for the whole in-process design, in one line.

A full prompt with git branch, working-tree status and upstream distance costs
35 ms in a 2,600-file repository — against 32 ms for `git status
--porcelain=v2 --branch` alone, with no fork on top. Three milliseconds in a
small repository, 1.4 ms outside one.

## What it looks like

```sh
h[users][omar][role]=admin                  # maps nest, no new syntax
json parse doc "$body"; json get doc .items[0].name

fn add(int a, int b) -> int { ret $((a + b)); }
sum := add 10 32                            # a result, without forking

[[ $line =~ ^([a-z]+)=(.*)$ ]] && echo "${M[1]} is ${M[2]}"

exec 3<>/dev/tls/api.example.com/443        # sockets are descriptors
opt -o --output out path!  "Where to write" # declared arguments
args "$@"
```

## Documentation

| | |
|---|---|
| [The language](docs/language.md) | Syntax, expansion, arithmetic, conditionals, maps and arrays, typed functions, declared arguments, errors |
| [Text, regex and JSON](docs/data.md) | In-process text and array operations, POSIX regex, JSON over the map model |
| [Networking](docs/networking.md) | Sockets, TLS, `/dev/tcp` and friends, `listen` |
| [Interactive use](docs/interactive.md) | `~/.hibrc`, line editing, history, completion, job control |
| [The prompt](docs/prompt.md) | The prompt hook, the segment module, and its native git support |
| [Modules](docs/modules.md) | The module ABI, and writing one |
| [Deployment](docs/deployment.md) | `deploy.sh`: install, verify, update, roll back |
| [Testing](docs/testing.md) | The harness, the recording discipline, sanitizers and fuzzing |
| [Decisions](docs/adr/README.md) | Why hibr behaves the way it does — one record per decision |

## Builtins

`.` `:` `[` `accept` `alias` `args` `arr` `bg` `break` `builtin` `cd` `command`
`connect` `continue` `dirs` `disown` `echo` `eval` `exec` `exit` `export`
`fail` `false` `fg` `getopts` `help` `history` `jobs` `json` `kill` `let`
`listen` `local` `match` `mod` `opt` `popd` `printf` `pushd` `pwd` `read`
`recv` `ret` `return` `rsub` `send` `set` `shift` `source` `str` `test` `time`
`title` `trap` `true` `try` `type` `umask` `unalias` `unset` `wait`

## Deliberate differences from bash

Each of these has a record explaining the reasoning and the cost.

- [`set -e` is scoped](docs/adr/0002-errexit-is-scoped.md) to the tested
  pipeline, not to the bodies of functions it calls. No `pipefail` needed.
- [`((expr))` never trips `set -e`](docs/adr/0003-arithmetic-status-is-a-value.md)
  — its status is a value, not a failure.
- [Regex captures go to `M`](docs/adr/0004-regex-captures-go-to-M.md), for both
  `[[ =~ ]]` and `match`, not `BASH_REMATCH`.
- [`ret` does not print](docs/adr/0005-results-travel-in-a-slot.md); functions
  return through `$RET` and `:=`, without forking.
- [Arrays are sparse maps](docs/adr/0006-arrays-are-sparse-maps.md), and maps
  nest without new syntax.
- [Brace expansion is literal-only](docs/adr/0007-brace-expansion-is-literal.md)
  — `{$a,$b}` is left as written.
- [`**` is always on](docs/adr/0008-globstar-is-always-on.md) and does not
  follow symlinked directories.
- [Strict expansion exists](docs/adr/0009-strict-expansion-is-opt-in.md) as
  `set -S`, and is opt-in.
- [TLS verifies certificates](docs/adr/0010-tls-verifies-and-is-dlopened.md)
  unless told otherwise.

## Not implemented

`declare`/`typeset`, `shopt`, `set -o` by option name, coprocesses, anchored
replacement `${x/#p/r}` and `${x/%p/r}`, extended globs, and `BASH_REMATCH`.
The line editor stores bidirectional text in logical order and leaves
reordering to the terminal, so the cursor moves in logical order through Arabic
text.

## Source layout

| | |
|---|---|
| [`include/`](include/) | Public header and module ABI, internal declarations, regex declarations |
| [`src/`](src/) | The shell itself — lexer, parser, expansion, execution, builtins, editor, networking |
| [`mods/`](mods/) | Reference modules, including the prompt and its git implementation |
| [`tests/`](tests/) | The harness, 43 test scripts, and the suite hibr runs on itself |
| [`examples/`](examples/) | Complete scripts showing the pieces working together |
| [`docs/`](docs/) | Everything above, in detail |

Conventions for working on the code — naming, memory discipline, and the traps
already found — are in [CLAUDE.md](CLAUDE.md). History is in
[CHANGELOG.md](CHANGELOG.md).

## Licence

MIT. See [LICENSE](LICENSE).
