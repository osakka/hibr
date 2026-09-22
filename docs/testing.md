# Tests

```
make check                                 # tests/run.sh: 43 test scripts
./build/hibr tests/self.hibr                       # 84 assertions written in hibr
HIBR=./build/hibr REF=dash tests/run.sh            # compare with another shell
python3 tests/fuzz.py ./build/hibr.asan 500       # mutation fuzzing of the parser
```

Each `tests/*.t` is a script. When a matching `.expected` file exists, the test
is compared with that recording — exit status on the first line, then the
output — because its behaviour is intentionally hibr's own. Otherwise it is run
under bash as well and the two are compared on standard output and exit status.

`tests/self.hibr` is the suite hibr runs on itself, with its own assertion
helpers written as typed functions. It finishes with a limits section: 40
levels of map nesting, a 2000-element array sorted, a 20,000-character string,
200 levels of recursion through `:=`, and a 300-element JSON document.

The whole suite runs clean under AddressSanitizer and UndefinedBehaviorSanitizer:

```
gcc -Iinclude -DHIBR_TLS -g -O1 -fsanitize=address,undefined -w -rdynamic \
    -o build/hibr.asan src/*.c -ldl
ASAN_OPTIONS=detect_leaks=0 HIBR=./build/hibr.asan tests/run.sh
```

`tests/fuzz.py` mutates the test scripts — byte flips, deletions, inserted
shell tokens — and runs `hibr -n` on each result, reporting crashes, sanitizer
errors and hangs. Parser nesting (`HIBR_DEPTH`), arithmetic nesting
(`HIBR_AXDEPTH`) and `[[ ]]` grouping are bounded, so deeply nested input
produces an error rather than a crash.

## Differential fuzzing

`tests/fuzz.py` asks whether the parser survives odd input. `tests/diff.py`
asks the harder question: whether the answer is the same one bash gives. It
generates snippets from a grammar — expansions, arithmetic, conditionals,
loops, `case`, functions, arrays, subscripts, `IFS` changes — runs each under
both shells and compares the output and the status.

```
python3 tests/diff.py 250              # 250 snippets against bash
SEED=7 python3 tests/diff.py 500       # a different stream
python3 tests/diff.py --shell /bin/dash   # or point it somewhere else
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

## The line editor

`tests/run.sh` cannot reach the editor at all, because it only runs when stdin
is a terminal. `python3 tests/editor.py` starts interactive shells on a pseudo
terminal, sends keystrokes and checks what was drawn — that the right-hand
prompt lands on the right column, that it gives way to a long line, that the
transient prompt replaces an accepted one, and that ordinary editing still
works underneath. It takes the shell to test as its first argument.

---

[← documentation index](README.md) · [← project README](../README.md)
