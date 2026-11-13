# inspect (development draft)

This in-tree producer implements the record approach in ADR 0056 for hui-assistant issue #1. It is not released or a frozen version-1 contract.

## Implemented

- `inspect functions FILE [-j]`: function names, declared signatures, parameters and unevaluated defaults, author comments and body metadata.
- `inspect calls FILE [FUNCTION] [--rules FILE] [-j]`: literal and dynamic commands, explicit substitution scopes and convention-derived callback relationships.
- `inspect refs FILE [-j]`: literal arguments naming inspected functions.
- `inspect check FILE [-j]`: structured parse diagnostics and findings from the same implementation as `--explain`.
- Typed result maps under `:=`; JSON output with `-j`, otherwise a plain-text rendering of those same records.
- An isolated parser arena and variable table, bounded 4 MiB UTF-8 input, registered local schemes only, partial recovery, source/rule identities, and explicit provenance labels.
- C consumers ask for `"inspect"`, version 1, using `inspect.h`. `query` appends JSON to an initialized caller-owned `str`; its options argument is JSON with optional `function`, `rules` and `symbols` fields. `symbols` is an explicit whitelist of literal-reference candidates; names supplied by the caller remain unresolved and are labelled `lookup: supplied-symbol`, never resolved across files. The builtin accepts the same list as `--symbols JSON_ARRAY`. It does not transfer parser pointers. The caller owns the output and must free it. The builtin places its result in the usual result slot; the C query itself preserves the caller's status, result and line state.

## Source positions and review

The private lexer observer captures node, word and substitution ranges before text is copied or unescaped. Inspection stores those positions in its own side table; normal parsing has no observer, no source table and no public AST changes. Calls and references point to their original words. Function ranges pass extraction/reinspection across all 3,289 definitions in the tracked tree.

`tests/inspect_spans.py` covers identical data/code text, repeated arguments, nested and process substitutions, references, parameter defaults and function bodies. Backtick substitutions whose unescaping changes the bytes are explicitly unresolved and mark analysis partial: they are never assigned guessed locations. Public ABI 17 is unchanged. This is a review proposal in PR #535, not a released contract.

## Coordinates and rendering

`span.start` is inclusive and `span.end` exclusive. Lines and byte columns are 1-based. `endline` is always the last line covered by the span, using the start line for an empty span. Each declared parameter has a `default`: either null, or source evidence containing unevaluated text, identity, span and declared provenance. The plain form renders a compact summary plus all typed evidence fields, so resolution, binding, source locations and future fields cannot silently disappear. A static binding target is named; computed targets expose their source span instead of leaking arbitrary expression code.

## Verification

`python3 tests/inspect_records.py` covers records, input failures, privacy, convention rules, recovery, non-execution and C caller-state preservation. It supports `HIBR` and `HIBR_TESTMODS` for sanitizers. `tests/inspect_tree.py` checks all tracked Hibr source files and reports process-inclusive timings. `HIBR_FUZZ_INSPECT=1` selects inspection in the existing parser mutation fuzzer. The sanitizer quick list includes the record suite and a separate inspection fuzz pass.

`tests/540-examples.t` consumes inspection records for duplicate definitions and callback arity, with callback data in `programs/desktop/conventions.json`. Dependency and census checks consume function records. Strict-variable checks consume function records and retain legacy variable analysis, which version 1 does not replace. The parity differences are recorded in PARITY.md.
