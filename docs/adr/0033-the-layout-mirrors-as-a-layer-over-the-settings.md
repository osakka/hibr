# 0033 — The layout mirrors as a layer over the settings

Status: accepted

## Context

A right-to-left language reads its screen from the right: the menu bar
starts at the right edge, a window's buttons sit at its left, a row of
dialog buttons runs the other way (Gitea #68, phase 4). The desktop
already has preferences that look like they decide this -- `DT_BTNSIDE`,
`DT_TITLEALIGN` -- but every saved settings file has frozen their current
values (the `DT_SETVER` trap in CLAUDE.md), so changing a default when
the language is Arabic would reach nobody who has ever saved anything,
and changing the saved value would lose the person's own choice.

## Decision

Mirroring is a layer over the settings, never a change to them.
`DT_MIRROR` (auto, on, off; Control Panel > Language > Mirror Layout)
decides it, auto following the catalogue's `"dir"`; `DT_MIRRORON` is what
is in effect, and layout code asks `case $DT_MIRRORON in 1)` -- nothing
for an unmirrored desktop to pay. A person's own `DT_BTNSIDE` and
`DT_TITLEALIGN` keep their meaning inside the mirrored frame: "right" is
the trailing side.

Mirrored in this release (4a): the menu bar (menus from the right edge,
the application menu, clock, notifications and workspaces from the left,
each hit-tested where it is drawn), menus (opening under their title's
right edge, submenus to the left, a row's shortcut on the left and its
label right-aligned by display width), a window's buttons and title, and
a row of dialog buttons (reversed, the default still the dialog's action).

Mirrored in 0.99.58 (4b): the desktop icons' default column (from the left
edge; an icon a person has placed stays where they put it), the Control
Strip's side and the notifications' corner -- drawn from `CS_SIDEX` and
`DT_NOTEPOSX`, set each frame from the person's own `CS_SIDE` and
`DT_NOTEPOS`, so the saved value is still theirs, and a strip dragged to a
side is stored as the side it would be unmirrored -- tiling's main window
(on the right, the stack on the left), and Control Panel's split (the list
of panes right of the divider, dragged the other way).

A pseudo-language, `xy`, is `xx` with `"dir": "rtl"`: the suites drive the
mirrored layout with it, and `tests/uifuzz.py` runs clean under it.

## Consequences

- Mirrored in 0.99.60 (4c, first part): the rows of every Control Panel
  pane (value or widget left, label right, the scrollbar on the pane's
  left edge) and Files (path and names at the right, the details columns
  reversed, the icon grid from the right). A pane that draws its own
  body, and every other app's layout -- Mail's panes, Sheet, the
  scrollbars at a window's right edge -- follow. A terminal window's
  content is never mirrored: it is a program's own screen.
- Mirrored in 0.99.62: Mail -- the sidebar, the bar, the conversation
  list and an open conversation's headings. A message body keeps the
  layout `html lines` gives it, left-aligned; aligning a right-to-left
  message needs a direction in that layout, not in the app.
- Mirrored in 0.99.63: Sheet (column A at the right, the gutter outside
  it, left and right arrows following what is seen) and the file dialog.
- `DT_MIRROR=on` mirrors an English desktop too, for whoever wants it and
  for testing.
