# Checker migration observations

Baseline: Hibr commit `7bfde28` (0.99.506), before this branch's changes. The existing checker answers were captured before editing and compared with inspection-backed function discovery and signatures.

- `deps.py`: definition sets are identical across the desktop; its suite passes with 32,118 checked lines and no failures.
- `census.py`: six former definitions (`sum`, `avg`, `min`, `max`, `count`, `__eval`) disappear from the desktop definition set. They are text in Sheet's quoted `SS_PRELUDE` heredoc, not definitions executed when that source file is read. Existing definitions retain their original file mapping.
- `census.py` signatures: `dt_putruns` is added. Its multiline declaration was missed by the old one-line regex.
- `strictvars.py`: 3,130 strict functions are checked instead of 2,972. The extra 159 real definitions include inline helpers and `dt_putruns`; the heredoc's `__eval` is no longer treated as a desktop definition. The suite still reports no failures. Removing missed-function bodies from top-level scanning also removes false global allowances; legacy variable scanning still sees some heredoc text and is not claimed as authoritative variable analysis.
- `540-examples.t`: duplicate names and callback arity consume producer records, and arities/registrations come from conventions.json. The record-based version passes the recorded examples suite.

These are reviewed differences in structural coverage, not silent attempts to force a parser to reproduce regex mistakes. Private parser provenance now passes the exact-span regressions and extraction/reinspection for all 3,289 function records. The interface and extra private instrumentation still require upstream review.

## Required release review: dependency calls and references

Compared `ef6e412`'s token-based dependency checker with producer `calls` and `refs` on the same 98 installable files. `python3 tests/inspect_deps_parity.py ef6e412` reports **99 old, 99 new, 99 unchanged cross-entry candidate locations**: no removed or added edges to explain. Both complete checkers report zero dependency failures. The production checker has no `TOK` pattern; only the explicit audit loads the historical Python checker from Git.

The new checker supplies the known installable function names to `refs` as an explicit `symbols` whitelist. This requests literal candidate names, not cross-file resolution: records remain syntax, say `lookup: supplied-symbol`, and remain unresolved. Without that option, refs retains its inspected-file-only scope and privacy. The module does not read other source files. It never reveals unrelated argument literals. Calls and refs supply the line and enclosing function, including inline definitions and nested substitutions.

The line counter changes from 32,118 to 34,099 because it now includes 1,981 declaration lines formerly skipped wholesale. This is a reporting difference; the candidate-location comparison above is unchanged. Guard-argument, resource-path and catalogue-header recognition remain explicit text checks: version 1 does not expose those argument/path facts. Variable analysis and dt_app metadata are separate follow-ups, not duplicate parsers introduced here.

## Formatting oracle

After the required semantic changes and before the one-statement-per-line rewrite, `python3 tests/inspect_oracle.py capture build/inspect-format-oracle` captured raw `-j` bytes for all four requests over all 197 tracked Hibr files (788 outputs). The same script's `compare` mode checks every byte and the source corpus hashes after the rewrite. `gcc -fsyntax-only -Wall -Wextra -Iinclude mods/inspect/inspect.c` also passes. These are formatting checks; the endline/default/binding changes are intentionally covered by separate behavioral regressions.

The inspection fuzzer now preflights loading the selected producer and requires an inspection JSON result for every non-crashing case. `HIBR_TESTMODS` is explicitly mapped to the shell's module path. A missing module can no longer produce a false clean fuzz result. The required-change verification reruns 300 cases with the actual ASan/UBSan producer and leak detection.
