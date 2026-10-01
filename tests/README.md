# tests

```text
tests/run.sh [-v] [prefix]                  # 109 test scripts
./build/hibr tests/self.hibr                # 91 assertions, written in hibr
HIBR=./build/hibr REF=dash tests/run.sh     # compare against another shell
```

Every `tests/*.t` is a script. How it is judged depends on whether a matching
`.expected` file exists:

- **No `.expected`** — the script is run under a reference shell as well
  (bash by default) and the two are compared on standard output and exit
  status. These are the tests where hibr should agree with bash, and the
  reference shell decides what is correct.
- **With `.expected`** — the recording is the answer: exit status on the first
  line, then stdout and stderr together. These are the behaviours that are
  deliberately hibr's own, so there is nothing to compare against.

## Rules for a recorded test

A recording is only useful if it cannot drift.

- **Deterministic output.** No `$$`, no ports, no timings, no hostnames, no
  absolute paths that vary by machine. Using `$$` *in a path* is fine as long
  as the path never reaches the output.
- **Order redirections** so that error text containing paths is suppressed:
  `cmd 2>/dev/null >file`, not `cmd >file 2>/dev/null`.
- **Interleaving is a trap.** stdout is buffered and stderr is not, so a test
  that prints to both gets an order that depends on buffer state. Send stderr
  to a file and `cat` it at a known point.
- **Re-record only after reading the diff** and agreeing the new behaviour is
  correct. A re-record that was not inspected is a test that has been deleted.

## The full-screen suites

Anything that only happens on a terminal -- the console, the line editor, the
pager, the editor, the monitor, the traceroute, the cat, the window manager and
its apps -- cannot be reached from a `.t` file, because `run.sh` gives it a
pipe. Those live in the Python suites, and every one of them drives a real
pseudo terminal:

```text
python3 tests/console.py    the display: cells, panes, damage, decoded keys
python3 tests/cat.py        what cat adds when its output is a terminal
python3 tests/most.py       the pager
python3 tests/hvi.py        the editor
python3 tests/mon.py        the system monitor
python3 tests/mtr.py        the live traceroute
python3 tests/editor.py     the line editor
python3 tests/desktop.py    the window manager
python3 tests/apps.py       the apps, desk accessories and control panel
python3 tests/term_diff.py  the terminal emulator, cell by cell against tmux
python3 tests/uifuzz.py     random input into each app, seeded
```

`term_diff.py` is the odd one out: it drives no pty of its own. It feeds the
same bytes to `mods/term` (`term new` and `term feed`) and to a private tmux
server, and compares the two screens -- short sequences each aimed at one
behaviour, and real programs' output recorded into `term/` from a generated
file (`--record` makes them again; never record anything that shows this
machine's own processes, host or paths). Where xterm and tmux disagree, the
case asserts xterm's result and says why. It skips when tmux is missing.

**`screen.py` is the only pty harness.** It holds the pseudo terminal, the
key and mouse helpers, the assertion tally, and the model that reassembles a
screen from the escapes the console emitted -- which is necessary, because the
display sends only the cells that changed, so grepping the byte stream finds
`78-200/200` where the display reads `178-200/200`. There used to be six
copies of that and five of the model, differing in timings, so fixing one
fixed one. Do not write a seventh; add what is missing to `screen.py`.

**It waits for the program, not for the clock.** A desktop run with
`HIBR_TESTIDLE` set -- every `Term` sets it -- prints an escape a terminal
ignores each time a frame is on screen and it is about to wait, carrying how
many bytes of input the console has read by then. A `Term` that has seen one
waits after each key for the one that has caught up with everything sent, so
a key costs what drawing it costs, and a frame a timer asked for is never
taken for the key's. A key sent with an explicit `settle` still sleeps it,
for a test waiting on something outside the frame; a number among the keys
is a pause, for a game or a clock that moves on time; and `quit` returns the
moment the program has exited. Programs that never say they are idle -- the
pager, the editor -- keep the old fixed timings. A terminal window's own
program starts on its own time, so `tests/apps.py` gives it a moment before
the first key.

**It fails a suite in which any session printed a shell error.** A line
`hibr: ...` ending in a newline is never something the console drew, so
every session is scanned as it closes, and `report` adds the check. An error a
test provokes on purpose is declared beside it with `expect(pattern)`, by
what it says, so an unexpected one in the same session still counts.

**It counts its checks.** `report(N)` fails a suite whose checks made are not
N, so one that stopped being reached cannot read as a pass.

**`tests/all.py` runs everything at once** -- `tests/run.sh` and every pty
suite, one per core -- with one line each and full logs under
`build/test-logs/`; `make check-all` is the same. They are independent: each
pty run has its own terminal, XDG directories and hold sockets.

Four more tools sit around it:

- **`tests/affected.py`** -- the suites a change reaches, from `git diff` or
  from paths given; `--run` runs just those, `--why` says which path chose
  each. Module dependencies are read from each module's `hibr_require` and
  `hibr_provide`, not listed, and an unrecognised path reaches everything.
  For iterating; `all.py` stays the release gate.
- **`tests/census.py`** -- builds `build/hibr.census` (`make census`), which
  logs every function call, runs the desktop suites under it and lists each
  desktop function no suite called, with the count and which way it moved
  since the last run. Calls whose arguments did not bind are listed too.
  `--expansion [prefix...]` also lists every place, file and line, where an
  expansion split or a value globbed or matched as a pattern -- what
  `strict expansion` would change -- so a file can be made strict from
  evidence rather than by reading it. `--types` lists every declared
  parameter by what the suites passed it: an integer every time, an integer
  or nothing, or anything else -- the evidence for typing it.
- **`tests/strictvars.py`** -- reads every function in each desktop file that
  says `strict` and fails on any assignment that would create a global:
  what `strict vars` refuses at run time, found on every path rather than
  only the ones a suite reaches. A suite in `all.py`; it takes under a
  second.
- **`tests/uifuzz.py`** -- random keys, clicks, drags and wheel turns inside
  one app's window, then: still running, still answering, nothing printed.
  A suite in `all.py` at the fixed `SEED=1`; `SEED=random` explores, and a
  failure prints the line that replays it. Task Manager, Files, Terminal
  and the Date & Time pane are not targets: they signal processes, move
  files, run a shell and run sudo on the machine running the tests.

The screen model keeps the pen each cell was drawn with, as well as its
character: `sc.style(r, c)` gives its foreground and background as
`#rrggbb` and whether it was bold, so a test can check what is highlighted,
not only what is written.

It is also runnable, which is what to reach for instead of a throwaway script:

```text
python3 tests/screen.py examples/desktop/session.hibr
python3 tests/screen.py -c 'mod load build/mods/mon.so; mon'
```

## Tests that guard the documentation and the examples

- **`530-docs.t`** fails when a builtin has no entry in `docs/builtins.md`,
  when an internal link stops resolving, or when a decision record is not
  indexed.
- **`531-doc-examples.t`** reads every page with code in it -- all of
  `docs/`, the READMEs, the decision records and the desktop guides. Each
  ```` ```sh ```` block followed by an ```` ```output ```` block is run in an
  empty directory and must print exactly that; one that cannot run says why
  in a `<!-- not run: ... -->` line just before it; a `<!-- setup ... -->`
  comment supplies a file or helper an example assumes; and every other code
  block names what it is (```` ```text ````, ```` ```c ````). A bare fence, or
  an example that neither shows its output nor says why not, fails.
- **`540-examples.t`** runs every script in `examples/`, and fails on a
  function defined twice or a desktop callback that names fewer arguments
  than the window manager hands it.
- **`850-lint.t`** lints every example with `hibr --explain` and fails on
  any finding, beside a firing and a quiet case for each lint rule.

## Comparing against bash

Two tools sit outside `run.sh` because they need bash and a generator, and a
generator written in the shell under test could not be trusted to report that
shell's failure:

```text
python3 tests/diff.py --shell ./build/hibr 250     # random snippets, both shells
python3 tests/corpus.py --shell ./build/hibr --list scripts.txt
```

`diff.py` skips a snippet only when one of its `DELIBERATE` patterns -- each a
decision record -- can fire in it, and each pattern matches only the
construct it names. `corpus.py` runs real scripts with `--help`, `--version`
and no arguments under both shells, each in a sandbox of its own (its own
`HOME`, `TMPDIR` and directory, stdin closed, five seconds), and compares
standard output and status; the sandbox path and a `mktemp` suffix inside it
are normalised first. It executes the scripts it is given, so give it a list.

Each difference these find and fix gets a test of its own, compared against
bash: `855` (an exit inside a condition), `860` (a script runs as it is read),
`865` (a malformed `${…}` after `=~`), `870` (return and break outside a
function or loop), `875` (a function
in a pipeline or the background), `880` (`BASH_REMATCH`), `895` (the four
`corpus.py` found in real system scripts), and the recorded `885` (`--plan`)
and `890` (quoted keys in arithmetic) for what is hibr's own.

## Fixtures without dependencies

`410-object.t` and `420-status.t` need git repositories, but the suite cannot
depend on `git` being installed and must produce identical output everywhere.
Both build their fixtures from `printf` with octal escapes — real zlib streams,
a real packfile with `OFS_DELTA` and `REF_DELTA` entries, real index files in
versions 2 and 4 — generated once and committed as part of the test. No `git`
binary is involved, and the bytes are the same on every machine.

## In `self.hibr`

Assert on `"${a[*]}"`, never `"${a[@]}"`. The latter splits into several
arguments, and the assertion silently never runs.

## Before calling anything done

```text
gcc -Iinclude -DHIBR_TLS -g -O1 -fsanitize=address,undefined \
    -fno-sanitize-recover=undefined -w -rdynamic -o build/hibr.asan src/*.c -ldl
ASAN_OPTIONS=detect_leaks=0 HIBR=./build/hibr.asan tests/run.sh
ASAN_OPTIONS=detect_leaks=1 ./build/hibr.asan tests/<one>.t
SEED=7 python3 tests/fuzz.py ./build/hibr.asan 500
```

`python3 tests/asan.py [suite...]` does the pty suites as well: `make asan`
builds the shell and every module with the sanitizers into `build/asan`, and
every suite runs against them, with reports written to `build/asan-logs`
rather than stderr, so one from a desktop (whose stderr is its log) or a
forked child is still found. Any report fails the run.

On a kernel with high ASLR entropy the sanitizer build loops printing
`AddressSanitizer:DEADLYSIGNAL` instead of running. Wrap it in `setarch -R` and
point `HIBR` at the wrapper.

A module is compiled separately, so a sanitized shell alone does not sanitize
it. `make asan` builds every module, the prompt module included, into
`build/asan/mods`; to cover one by hand, build it with the same flags and load
that copy:

```text
gcc -Iinclude -g -O1 -fsanitize=address,undefined -w -shared -fPIC \
    -o build/prompt-asan.so mods/prompt/*.c
```

More in [the testing documentation](../docs/testing.md).
