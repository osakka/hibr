# Tests

```
make check                                 # tests/run.sh: 43 test scripts
./hibr tests/self.hibr                       # 84 assertions written in hibr
NSH=./hibr REF=dash tests/run.sh            # compare with another shell
python3 tests/fuzz.py ./hibr.asan 500       # mutation fuzzing of the parser
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
    -o hibr.asan src/*.c -ldl
ASAN_OPTIONS=detect_leaks=0 NSH=./hibr.asan tests/run.sh
```

`tests/fuzz.py` mutates the test scripts — byte flips, deletions, inserted
shell tokens — and runs `hibr -n` on each result, reporting crashes, sanitizer
errors and hangs. Parser nesting (`HIBR_DEPTH`), arithmetic nesting
(`HIBR_AXDEPTH`) and `[[ ]]` grouping are bounded, so deeply nested input
produces an error rather than a crash.

---

[← documentation index](README.md) · [← project README](../README.md)
