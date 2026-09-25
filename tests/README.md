# tests

```
tests/run.sh [-v] [prefix]              # 43 test scripts
./build/hibr tests/self.hibr                # 84 assertions, written in hibr
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

```
python3 tests/console.py    the display: cells, panes, damage, decoded keys
python3 tests/cat.py        what cat adds when its output is a terminal
python3 tests/most.py       the pager
python3 tests/hvi.py        the editor
python3 tests/mon.py        the system monitor
python3 tests/mtr.py        the live traceroute
python3 tests/editor.py     the line editor
python3 tests/desktop.py    the window manager
python3 tests/apps.py       the calculator and the file browser
```

**`screen.py` is the only pty harness.** It holds the pseudo terminal, the
key and mouse helpers, the assertion tally, and the model that reassembles a
screen from the escapes the console emitted -- which is necessary, because the
display sends only the cells that changed, so grepping the byte stream finds
`78-200/200` where the display reads `178-200/200`. There used to be six
copies of that and five of the model, differing in timings, so fixing one
fixed one. Do not write a seventh; add what is missing to `screen.py`.

It is also runnable, which is what to reach for instead of a throwaway script:

```
python3 tests/screen.py examples/desktop/desktop-session.hibr
python3 tests/screen.py -c 'mod load build/mods/mon.so; mon'
```

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

```
gcc -Iinclude -DHIBR_TLS -g -O1 -fsanitize=address,undefined \
    -fno-sanitize-recover=undefined -w -rdynamic -o build/hibr.asan src/*.c -ldl
ASAN_OPTIONS=detect_leaks=0 HIBR=./build/hibr.asan tests/run.sh
ASAN_OPTIONS=detect_leaks=1 ./build/hibr.asan tests/<one>.t
SEED=7 python3 fuzz.py ./build/hibr.asan 500
```

On a kernel with high ASLR entropy the sanitizer build loops printing
`AddressSanitizer:DEADLYSIGNAL` instead of running. Wrap it in `setarch -R` and
point `HIBR` at the wrapper.

The prompt module is compiled separately, so sanitizing the shell does not
sanitize it. To cover it, build it with the same flags and load that copy:

```
gcc -Iinclude -g -O1 -fsanitize=address,undefined -w -shared -fPIC \
    -o build/prompt-asan.so mods/prompt/*.c
```

More in [the testing documentation](../docs/testing.md).
