# tests/gen — fixture generators

`410-object.t` and `420-status.t` need real git data: zlib streams, a packfile
with delta chains, index files in several versions, commits with parents. The
suite cannot depend on `git` being installed, and a recorded test has to
produce identical bytes on every machine.

So the fixtures are generated once, here, and committed as `printf` statements
with octal escapes inside the test scripts themselves. Running a test rebuilds
its own repository from those bytes. No `git` binary is involved.

```
python3 tests/gen/410-object.py     # rewrites tests/410-object.t
python3 tests/gen/420-status.py     # rewrites tests/420-status.t
```

These are the only part of the test suite that needs Python, and only when a
fixture changes. After regenerating, run the test, read the diff, and
re-record the `.expected` file only if the new behaviour is right.
