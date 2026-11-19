# inspect

Hibr's in-tree source inspection producer. The [version 1 contract](../../docs/inspect.md) is the single reference for requests, record fields, options, limits and runnable examples. [ADR 0056](../../docs/adr/0056-the-language-explains-itself.md) records the design rationale. Review and the provider release gate precede the interface freeze.

The private parser observer captures provenance without exposing an AST. Only inspection installs the observer and allocates its source-position table. Public ABI 17 is unchanged.

The additive `vars FILE [FUNCTION]` request reports syntax-level parameter,
declaration, write, bind, loop target, builtin input target and read records.
Arithmetic records come from Hibr's arithmetic parser in a private empty shell;
inspection does not execute the source or retain arithmetic values. A request
sets `partial` when a construct could not be mapped or parsed completely. The
record contract and field meanings are in `docs/inspect.md`.

Run `make check-inspect` for behavioral checks, symbol bounds, whole-tree
inspection, extraction round trips, variable evidence, and strict-variable
checker parity. `tests/531-doc-examples.t` runs the contract examples.
`tests/inspect_records.py`, `inspect_symbols.py`, `inspect_spans.py` and
`inspect_vars.py` support sanitizer builds through HIBR/HIBR_TESTMODS. The
inspection fuzzer preflights module loading and exercises symbol boundaries.

[PARITY.md](PARITY.md) records structural-checker differences, variable-checker
parity and the formatting oracle. Dependency, census, strict-function
discovery and strict-variable assignment checks consume producer records.
