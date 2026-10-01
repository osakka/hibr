# hibr — Highly Intuitive Bash-like Runtime

**hibr** is a shell that finally behaves the way you already expect a shell
to behave. Familiar bash syntax, its sharpest edges resolved instead of
carried forward, in about 17,500 lines of C and a 398 KB binary — under
two-thirds of bash's memory, and about two and a half times its speed on a
tight loop. Built with `tcc`, depending on nothing but libc and libdl.

The name says what it is. **Highly**: two-thirds of bash's memory, tight
loops two and a half times faster, in a 398 KB binary — the numbers below. **Intuitive**:
the things that make bash surprising the first time you hit them —
`test`'s word-splitting traps, `$BASH_REMATCH`'s awkward capture, a dozen more
in [the decision records](docs/adr/README.md) — fixed, so the shell does what
a reader would already guess it does. **Bash-like**: familiar syntax you
already know, not a rewrite you have to learn, and not a claim to be bash
itself. **Runtime**: the module ABI lets a module add a *protocol*, not just
a command — register a scheme and `/dev/<name>/…` works anywhere a filename
does — and results come back without forking, text and JSON are manipulated
without pipelines, and the prompt reads git's object store with no subprocess
at all.

It is also **حِبر** — Arabic for *ink*. Every shell session, every script,
every automated thing this runs is still just ink on a page in the oldest
sense: it is what you write computing with.

It is not a drop-in replacement for bash. Some behaviour differs on purpose,
where bash is error-prone; every one of those divergences is written down in
[the decision records](docs/adr/README.md). What it adds — nested maps, JSON,
regex capture, native networking and TLS, typed functions, results without
forking, declared command-line arguments, protocol-level modules, and a prompt
that reads git's object store without forking anything — is the reason to use
it.

```text
brew tap osakka/hibr && brew install hibr     # macOS or Linux, or:
make && make install                           # from this tree; PREFIX=... elsewhere

hibr script.sh args...
hibr -c 'echo $((6 * 7))'
hibr --explain script.sh    # name the mistakes in it, run nothing
hibr --plan script.sh       # follow it, change nothing, list what it would do
man hibr                    # every flag, variable and exit status
```

On Debian 12, Ubuntu 22.04 or newer (amd64), from the signed apt
repository at [osakka.github.io/hibr-apt](https://osakka.github.io/hibr-apt):

```text
curl -fsSL https://osakka.github.io/hibr-apt/hibr.gpg \
  | sudo tee /usr/share/keyrings/hibr.gpg > /dev/null
echo "deb [signed-by=/usr/share/keyrings/hibr.gpg] https://osakka.github.io/hibr-apt stable main" \
  | sudo tee /etc/apt/sources.list.d/hibr.list
sudo apt update && sudo apt install hibr
```

New to it? [From bash to hibr, in ten minutes](docs/tutorial.md) is the
place to start.

## Measurements

Measured on 0.68, on one host; the loop is
`while [ $i -lt 50000 ]; do i=$((i+1)); done`. Times are the best of eleven
runs. Memory is the shell's own `VmHWM`, read without forking, as the median
of twenty-five runs — a single reading is worthless here, because run-to-run
spread is about 180 kB either way.

| | hibr | dash | bash |
|---|---|---|---|
| binary, stripped | 398 KB | 122 KB | 1235 KB |
| resident memory at startup | 1852 kB | 1688 kB | 2900 kB |
| resident memory after the loop | 1888 kB | 1656 kB | 2944 kB |
| the loop | 87 ms | 79 ms | 211 ms |
| 5000 function calls, results via `:=` | **19 ms** | — | — |
| the same through `$( )` | 1145 ms | 1018 ms | 1836 ms |
| startup, `-c true` | 1.67 ms | 1.70 ms | 2.30 ms |

Read honestly. Against bash hibr is under two-thirds of the memory and two and
a half times the speed on a tight loop. Against dash it is level on startup
and within 10% on the loop, and about 160 kB heavier — and dash is a far
smaller language, so being close is the claim, not being ahead. The loop gap
was 1.7x before a round of profiling.

Where the memory goes is worth knowing: the heap at startup is 36 kB of the
1852, so memory here means the binary, and the binary means how
much language there is. There is no allocator trick left that would move it.

The row that is not a near-miss is the fifth. Returning a value through `$( )`
costs a fork per call in every shell; `:=` costs none, which is where the 60x
comes from. That is the argument for the whole in-process design, in one line.

A full prompt with git branch, working-tree status and upstream distance cost
35 ms in a 2,600-file repository when it was measured — against 32 ms for
`git status --porcelain=v2 --branch` alone, with no fork on top.

## What it looks like

<!-- not run: a sampler -- it wants a JSON body, a TLS host and arguments -->
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
| [From bash to hibr, in ten minutes](docs/tutorial.md) | **Start here**: install, then each thing hibr adds, one checked example each |
| [The language](docs/language.md) | The whole language, and every place it differs from bash |
| [Builtins](docs/builtins.md) | All 73, what each takes and what it gives back |
| [hibr, for a language model](docs/llm.md) | The whole language on one page, for a model to read; `llms.txt` points to it |
| [Everything else](docs/README.md) | Data, networking, the prompt, interactive use, full-screen programs, the desktop, modules, deployment, testing |
| `man hibr` | Every flag, environment variable, file and exit status |

Every example in these pages is run on every build and its output compared
with what the page prints (`tests/531-doc-examples.t`), so the documentation
cannot quietly drift from the shell.

## Deliberate differences from bash

Each has a record of its own explaining the reasoning and the cost, all
indexed in [the decisions](docs/adr/README.md). The ones met first:

- [`set -e` is scoped](docs/adr/0002-errexit-is-scoped.md): a failure inside
  a function called from a condition still stops the script, and a failing
  stage of a pipeline fails it, so there is no `pipefail`.
- [A quoted subscript is a literal key](docs/adr/0006-arrays-are-sparse-maps.md),
  `h["content-type"]`; arrays are sparse maps, and maps nest.
- [Regex captures go to `M`](docs/adr/0004-regex-captures-go-to-M.md), and
  `=~` fills `BASH_REMATCH` as well.
- [`ret` does not print](docs/adr/0005-results-travel-in-a-slot.md);
  functions hand back a value through `:=` without forking.
- [Brace expansion is literal-only](docs/adr/0007-brace-expansion-is-literal.md)
  — `{$a,$b}` is left as written.

## Not implemented

bash's compound coprocess, `coproc name { …; }`: hibr's `coproc` takes a
command, `coproc name cmd args…` -- see
[0018](docs/adr/0018-a-coprocess-is-an-endpoint.md).
The line editor stores bidirectional text in logical order and leaves
reordering to the terminal, so the cursor moves in logical order through Arabic
text.

## Source layout

| | |
|---|---|
| [`include/`](include/) | Public header and module ABI, internal declarations, regex declarations |
| [`src/`](src/) | The shell itself — lexer, parser, expansion, execution, builtins, editor, networking |
| [`mods/`](mods/) | Reference modules, including the prompt and its git implementation |
| [`tests/`](tests/) | The harness, the scripts compared against bash or recorded, the full-screen suites, and the suite hibr runs on itself |
| [`examples/`](examples/) | Complete scripts showing the pieces working together |
| [`docs/`](docs/) | Everything above, in detail |

Conventions for working on the code — naming, memory discipline, and the traps
already found — are in [CLAUDE.md](CLAUDE.md). History is in
[CHANGELOG.md](CHANGELOG.md).

## Licence

MIT. See [LICENSE](LICENSE).
