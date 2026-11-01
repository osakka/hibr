# Tests

```text
make check                                    # tests/run.sh: 212 test scripts
./build/hibr tests/self.hibr                  # 92 assertions written in hibr
HIBR=./build/hibr REF=dash tests/run.sh       # compare with another shell
python3 tests/fuzz.py ./build/hibr.asan 500   # mutation fuzzing of the parser
make check-all                                # tests/all.py: every suite at once
```

The full-screen suites, the sanitizer runner and the other harnesses are
listed in [tests/README.md](../tests/README.md).

## The release gate, and the daily run

A release does not run every suite. It runs the ones its change can reach,
and then, when the change touched C, the quick sanitizer pass:

```text
python3 tests/affected.py --since <last tag> --run   # the suites the change reaches
python3 tests/asan.py --quick                        # only when C changed
```

`affected.py` works out what reaches what rather than reading a list: a
module's suites from who loads and requires it, and a change under
`programs/desktop` from `tests/coverage.tsv`, which records the desktop
functions each suite was seen calling. A change to `src/`, `include/`, the
Makefile or `tests/screen.py` still reaches everything, and so does a path
it does not recognise. The map is measured by `python3 tests/covmap.py
--take`, which runs every suite under a build that logs each function call,
and is committed with the commit it was taken at. It refuses what it did
not measure -- a changed line outside every function, a function no suite
was seen calling, a map from another history -- and every desktop suite
runs then instead.

`tests/all.py` is the daily run: every suite side by side, about 21
minutes on four cores, after which the map is taken again. The full
`tests/asan.py` is daily too, never beside a gate, whose time it doubles.

A desktop callback that stalls the desktop writes a `held:` line to its
log, and a pty suite in which one was written fails, the same as one in
which the shell printed an error. `tests/850-lint.t` lints every script
under `examples/` and `programs/`, so a rule `--explain` learns -- a fork
in a desktop callback is one -- holds the shipped scripts to it.

Each `tests/*.t` is a script. When a matching `.expected` file exists, the test
is compared with that recording — exit status on the first line, then the
output — because its behaviour is intentionally hibr's own. Otherwise it is run
under bash as well and the two are compared on standard output and exit status.

`tests/self.hibr` is the suite hibr runs on itself, with its own assertion
helpers written as typed functions. It finishes with a limits section: 40
levels of map nesting, a 2000-element array sorted, a 20,000-character string,
200 levels of recursion through `:=`, and a 300-element JSON document.

The whole suite runs clean under AddressSanitizer and UndefinedBehaviorSanitizer:

```text
gcc -Iinclude -DHIBR_TLS -g -O1 -fsanitize=address,undefined \
    -fno-sanitize-recover=undefined -w -rdynamic \
    -o build/hibr.asan src/*.c -ldl
ASAN_OPTIONS=detect_leaks=0 HIBR=./build/hibr.asan tests/run.sh
python3 tests/asan.py        # the same for every suite, the pty ones too
python3 tests/asan.py --quick    # core, C modules and what changed: the gate's half
```

`-fno-sanitize-recover=undefined` makes an undefined-behaviour report end the
run rather than print and go on; `make asan` builds the shell and every module
the same way, into `build/asan`.

Both forms of `tests/asan.py` run the recorded tests a third time with
`detect_leaks=1`, and fail on any leak report (since 0.99.309, when every
recorded test was made to pass it); the other passes run with leaks off.

`tests/fuzz.py` mutates the test scripts — byte flips, deletions, inserted
shell tokens — and runs `hibr -n` on each result, reporting crashes, sanitizer
errors and hangs. Parser nesting (`HIBR_DEPTH`), arithmetic nesting
(`HIBR_AXDEPTH`) and `[[ ]]` grouping are bounded, so deeply nested input
produces an error rather than a crash.

`tests/self.hibr` opens with `plan N`. A suite that runs fewer assertions than
it planned has skipped something — an `is` inside a branch that stopped being
taken, a section that failed early — and a count nobody checks cannot say so.
The run fails on a mismatch, not only on a failed assertion.

## Differential fuzzing

`tests/fuzz.py` asks whether the parser survives odd input. `tests/diff.py`
asks the harder question: whether the answer is the same one bash gives. It
generates snippets from a grammar — expansions, arithmetic, conditionals,
loops, `case`, functions, arrays, subscripts, `IFS` changes — runs each under
both shells and compares the output and the status.

```text
python3 tests/diff.py 250                     # 250 snippets against bash
SEED=7 python3 tests/diff.py 500              # a different stream
python3 tests/diff.py --ref /bin/dash 250     # against dash instead
python3 tests/diff.py --shell ./build/hibr.asan 250   # a different hibr
```

Everything it generates is deterministic and touches nothing: no `$RANDOM`, no
`$$`, no clock, no file system, no process but the shell. A difference is
printed with the snippet that caused it and saved as `tests/diff-NNN.sh`.

Divergences hibr makes on purpose are listed in `DELIBERATE`, each naming the
decision record that argues for it. The patterns match only the construct they
name — an early version filtered on `{`, which also caught `${x}` and every
function body, and hid three quarters of the runs behind a true statement. A
filter that is too broad is worse than no filter, because it reports success.

It found two differences worth recording and one worth fixing: that a map keeps
insertion order where bash's associative arrays do not, that a negative
subscript counts back from the highest key even on a map, and that an
arithmetic expansion error was not failing the command at all.

## Real scripts, and the documentation

`tests/corpus.py` runs real scripts under both shells, each in a sandbox of
its own, and compares their output and status; every difference it has
found was read and fixed one at a time, each with a test of its own. It
executes what it is given, so it takes a list. `tests/531-doc-examples.t` runs every example on every page of
the documentation and fails when one stops printing what the page says --
see [tests/README.md](../tests/README.md) for both, and for the rest of the
tests that guard the docs and the examples.

## The line editor

`tests/run.sh` cannot reach the editor at all, because it only runs when stdin
is a terminal. `python3 tests/editor.py` starts interactive shells on a pseudo
terminal, sends keystrokes and checks what was drawn — that the right-hand
prompt lands on the right column, that it gives way to a long line, that the
transient prompt replaces an accepted one, and that ordinary editing still
works underneath. It takes the shell to test as an optional first argument.

---

[← documentation index](README.md) · [← project README](../README.md)
