# inspect

Hibr's in-tree source inspection producer. The [version 1 contract](../../docs/inspect.md) is the single reference for requests, record fields, options, limits and runnable examples. [ADR 0056](../../docs/adr/0056-the-language-explains-itself.md) records the design rationale. Review and the provider release gate precede the interface freeze.

The private parser observer captures provenance without exposing an AST. Only inspection installs the observer and allocates its source-position table. Public ABI 17 is unchanged.

Run `make check-inspect` for behavioral checks, symbol bounds, whole-tree inspection and extraction round trips. `tests/531-doc-examples.t` runs the contract examples. `tests/inspect_records.py`, `inspect_symbols.py` and `inspect_spans.py` support sanitizer builds through HIBR/HIBR_TESTMODS. The inspection fuzzer preflights module loading and exercises symbol boundaries.

[PARITY.md](PARITY.md) records structural-checker differences and the formatting oracle. Dependency, census and strict-function discovery consume producer records; variable analysis remains outside version 1.
