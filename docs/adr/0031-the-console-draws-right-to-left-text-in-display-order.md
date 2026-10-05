# 0031 — The console draws right-to-left text in display order

Status: accepted

## Context

A cell grid shows characters left to right in the order they are sent.
Some terminals (VTE, and iTerm2 with its option on) apply the bidirectional
algorithm themselves; Blit, which draws hibr straight to the screens, and
most others do not (Gitea #68). Text sent in logical order is unreadable
on the second kind, and text sent in display order is reversed twice on
the first.

The display interface's `put` carries two different things: what a script
asks the desktop to draw, and what a program has already laid out -- a
terminal window's cells, a pager's line, an editor's buffer. Only the first
is text for the console to order.

## Decision

`console put` (and its pane form) sends text that holds right-to-left
characters through the `uni` module's display order (ADR 0030) before
placing cells. A pane's text is clipped first, then ordered. Plain text
never reaches `uni`: a byte scan looks for the lead bytes of the
right-to-left blocks, the direction marks and the presentation forms, and
never matches box drawing. The display interface's `put` is unchanged, so
a terminal window, `most` and `hvi` draw exactly what their programs
decided.

On by default, since that is right for Blit and most terminals;
`console bidi off` is for a terminal that reorders itself. A setting in
Control Panel follows with the Language pane (phase 3).

The core's width table, written by hand, is now generated from the same
Unicode version (`src/wtab.c`, `tools/unigen.py width`), one table of the
code points that are not one column wide: 6% fewer instructions for
`str width` on a border than the two hand-written tables, and complete. The
test harness's terminal model takes widths from Unicode too, where it had
taken anything above U+2E80 as wide.

## Consequences

- A right-to-left paragraph is ordered within its line but not yet right
  aligned; aligning and mirroring the layout is phase 4.
- `uni` is loaded on the first right-to-left text, through
  `hibr_require`; a session that never draws any never loads it.
- The line editor does the same since 0.99.51 (phase 2b): the line is cut
  into screen rows in logical order and each row drawn in display order,
  levels and shaping worked out over the whole line as one paragraph and
  rules L1 and L2 applied per row -- `uni`'s `vismap`, interface version 2,
  which also says where each character landed so the cursor, still logical,
  stands on the right column. `HIBR_BIDI=off` turns both off, and is the
  console's default until a script says `console bidi`.
- Write and the textarea widget (Stickies, plain text) do the same since
  0.99.52, through `widgets/bidi.hibr` and `uni map`: each row of a line in
  display order with each cell keeping its own style, the cursor, clicks and
  the selection mapped between logical and visual columns. `console put -r`
  draws text already in display order.
- `hvi` does the same since 0.99.53, through `uni`'s `line` (interface 3):
  each character's pen worked out in logical order, then each drawn where
  `uni` put it; the cursor on its position's column. Phase 2 is complete.
