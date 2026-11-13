# inspect (development draft)

This in-tree producer implements the record approach in ADR 0056 for hui-assistant issue #1. It is not released or a frozen version-1 contract.

## Implemented

- `inspect functions FILE [-j]`: function names, declared signatures, parameters and unevaluated defaults, author comments and body metadata.
- `inspect calls FILE [FUNCTION] [--rules FILE] [-j]`: literal and dynamic commands, explicit substitution scopes and convention-derived callback relationships.
- `inspect refs FILE [-j]`: literal arguments naming inspected functions.
- `inspect check FILE [-j]`: structured parse diagnostics and findings from the same implementation as `--explain`.
- Typed result maps under `:=`; JSON output when unbound. Plain-text rendering remains to be added.
- An isolated parser arena and variable table, bounded 4 MiB UTF-8 input, registered local schemes only, partial recovery, source/rule identities, and explicit provenance labels.
- C consumers ask for `"inspect"`, version 1, using `inspect.h`. `query` appends JSON to an initialized caller-owned `str`; its options argument is JSON with optional `function` and `rules` fields. It does not transfer parser pointers. The caller owns the output and must free it. The builtin places its result in the usual result slot; the C query itself preserves the caller's status, result and line state.

## Release blocker: exact spans

The current module deliberately labels evidence spans `containing-unit`: those ranges are captured at parser entry/exit and enclose the relevant source unit. They are not exact token, parameter or body spans. Direct parser line metadata is reported where available. Substitution locations are not guessed.

`tests/inspect_spans.py` is a failing release regression. Identical command text can occur first in quoted data and later in executable substitution syntax. Finding retained text in the source is not sufficient to distinguish them. The earlier search-based prototype demonstrated this failure and must not be released with exact-location claims.

Hibr #532 needs a decision on additional private parser/lexer provenance capture before more core changes. No public AST fields or node structures are proposed. Until that is resolved, the exact-span and round-trip requirements in ADR 0056 are not met, even though function discovery, scope facts, parsing, typed results and the app work.

## Verification

`python3 tests/inspect_records.py` covers records, input failures, privacy, convention rules, recovery, non-execution and C caller-state preservation. It supports `HIBR` and `HIBR_TESTMODS` for sanitizers. `tests/inspect_tree.py` checks all tracked Hibr source files and reports process-inclusive timings. `HIBR_FUZZ_INSPECT=1` selects inspection in the existing parser mutation fuzzer. The sanitizer quick list includes the record suite and a separate inspection fuzz pass.

`tests/540-examples.t` consumes inspection records for duplicate definitions and callback arity, with callback data in `programs/desktop/conventions.json`. Dependency and census checks consume function records. Strict-variable checks consume function records and retain legacy variable analysis, which version 1 does not replace. The parity differences are recorded in PARITY.md.
