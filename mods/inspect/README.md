# inspect

Hibr's in-tree source inspection producer. The [version 1 contract](../../docs/inspect.md) is the single reference for requests, record fields, options, limits and runnable examples. [ADR 0056](../../docs/adr/0056-the-language-explains-itself.md) records the design rationale. Review and the provider release gate precede the interface freeze.

The private parser observer captures provenance without exposing an AST. Only inspection installs the observer and allocates its source-position table. Besides words, commands and substitutions it is told of each `$name`/`${...}` and `$(( ))` part, each `{var}` redirection name and each `name=(` array clause, which is what gives a variable fact its own bytes. Public ABI 17 is unchanged.

Run `make check-inspect` for behavioral checks, symbol bounds, variable facts, whole-tree inspection and extraction round trips. `tests/531-doc-examples.t` runs the contract examples. `tests/inspect_records.py`, `inspect_symbols.py` and `inspect_spans.py` support sanitizer builds through HIBR/HIBR_TESTMODS. The inspection fuzzer preflights module loading and exercises symbol boundaries.

[PARITY.md](PARITY.md) records structural-checker differences and the formatting oracle. Dependency, census and strict-function discovery consume producer records, and so do `tests/strictvars.py`'s reading of what a function writes and the call-argument checks in `tests/deps.py` and `540-examples.t` (`calls --arguments`). Variable facts are the `vars` request (added within interface 1); `tests/inspect_vars.py` holds every form to a fixture row by row.
