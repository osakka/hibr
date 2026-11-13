# Checker migration observations

Baseline: Hibr commit `7bfde28` (0.99.506), before this branch's changes. The existing checker answers were captured before editing and compared with inspection-backed function discovery and signatures.

- `deps.py`: definition sets are identical across the desktop; its suite passes with 32,118 checked lines and no failures.
- `census.py`: six former definitions (`sum`, `avg`, `min`, `max`, `count`, `__eval`) disappear from the desktop definition set. They are text in Sheet's quoted `SS_PRELUDE` heredoc, not definitions executed when that source file is read. Existing definitions retain their original file mapping.
- `census.py` signatures: `dt_putruns` is added. Its multiline declaration was missed by the old one-line regex.
- `strictvars.py`: 3,130 strict functions are checked instead of 2,972. The extra 159 real definitions include inline helpers and `dt_putruns`; the heredoc's `__eval` is no longer treated as a desktop definition. The suite still reports no failures. Removing missed-function bodies from top-level scanning also removes false global allowances; legacy variable scanning still sees some heredoc text and is not claimed as authoritative variable analysis.
- `540-examples.t`: duplicate names and callback arity consume producer records, and arities/registrations come from conventions.json. The record-based version passes the recorded examples suite.

These are reviewed differences in structural coverage, not silent attempts to force a parser to reproduce regex mistakes. Private parser provenance now passes the exact-span regressions and extraction/reinspection for all 3,289 function records. The interface and extra private instrumentation still require upstream review.
