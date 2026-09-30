# 2026-10-01: what docs/llm.md is worth

Thirteen tasks (`measure.py`): three `plain`, four `trap`, six `feature`.
Each model wrote all thirteen scripts in one reply, from the prompt alone,
with tools off and nothing to run; `measure.py score` ran them under hibr
0.62. Counts are passes over runs.

## Clean: `claude -p`, no project context

Run from an empty directory under `/tmp`, so no `CLAUDE.md` reached the
model, with `--tools ""` and a one-line system prompt. Asked beforehand,
both models said they knew nothing of hibr.

| model | page | runs | plain | trap | feature |
|---|---|---|---|---|---|
| Haiku 4.5 | none | 3 | 9/9 | 7/12 | 10/18 |
| Haiku 4.5 | before | 3 | 9/9 | 11/12 | 16/18 |
| Haiku 4.5 | after | 3 | 9/9 | 12/12 | 18/18 |
| Sonnet 5.5 | none | 3 | 9/9 | 9/12 | 5/18 |
| Sonnet 5.5 | before | 3 | 9/9 | 12/12 | 18/18 |
| Sonnet 5.5 | after | 3 | 9/9 | 12/12 | 18/18 |

"Before" is `docs/llm.md` as 0.62 shipped it (`prompt-page-before.txt`);
"after" is the page with the three gaps these runs found filled
(`prompt-page-after.txt`). Two intermediate versions were tried on Haiku as
the gaps were filled one at a time: `haiku-page2-*` and `haiku-page3-*`.

What failed without the page, per task over six runs (both models):

- T1, a regex capture: 0/6. Every script read `BASH_REMATCH`, which hibr
  leaves empty; its captures go to `M` (ADR 0004).
- F1, JSON without jq: 0/6. F3, declared arguments: 0/6.
- T2, removing a key held in a variable: 5/6; T4, a dashed key: 5/6.
- Plain shell work: 18/18. hibr broke none of the ordinary bash written.

What failed with the page before it was fixed, and what it was missing:

- F1 (Haiku, 2 of 3): `json get` on an array gives its JSON text, which the
  page did not say; the scripts joined `["admin","dev"]` as if it were a
  list. The page now says so and shows walking the map instead.
- T1 (Haiku, 1 of 3): a regex with a blank after `=~`, a syntax error in
  bash as well. The page now says to put such a pattern in a variable.
- F6 (Haiku, found on the second version): `arr sort` without `-n` sorts as
  text; the page listed `sort` and never said so.
- F4 (Haiku, found on the third version): `fn fetch { ... }` with no
  parameter list; the page never said the list is always written.

## With this repository's CLAUDE.md in context

The first eight replies (`with-project-context/`) came from subagents of a
session working in this repository, so hibr's `CLAUDE.md` -- which teaches
`M`, `unset m["$k"]`, `fn`, `ret` and `:=` in passing -- was in their
context. Without the page they already passed every trap task (8/8 runs 4/4)
and 14 of 24 feature tasks; with it, 23 of 24. That is a measure of
`CLAUDE.md`, not of a model that has never seen hibr, and is kept as such.

## What this does and does not show

It shows the page turning a model that has never seen hibr from about half
the hibr-specific tasks to all of them, on both a small and a mid-sized
model, and that ordinary bash written for hibr ran unchanged. It does not
show much beyond these thirteen tasks: the last three page fixes were made
*from* failures on them, so the "after" rows are partly fitted to the tasks
that found the gaps. A fresh set of tasks is the honest check of the fixed
page. Three runs per cell on two models is a signal, not a benchmark.
