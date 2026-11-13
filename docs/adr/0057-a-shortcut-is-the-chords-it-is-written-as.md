# ADR 0057: a shortcut is the chords it is written as

Status: accepted (0.99.507, the owner, 2026-11-12); built in 0.99.509.

## Context

Control Panel > Shortcuts opens with a row called Shortcut Key, offering
Alt, Super or Alt, and Super (`DT_MOD`, Gitea #326). It is a placeholder:
23 of the desktop's default shortcuts are stored as `mod-c`, `mod-w`,
`mod-left`, and `dt_keynorm` turns a key pressed into `mod-` whenever its
modifier is the one chosen. The other 16 defaults are written outright --
F10, Ctrl+Alt+arrows for workspaces and Snap Top/Bottom, Ctrl+Alt+L,
Ctrl+S, Shift+F10.

The owner, reading the pane: *why are we restricted between ctrl and super
and alt? ... I want one source of truth, and no parallels whatsoever.* What
the code holds, measured on 0.99.505:

- **One chord, two spellings.** `mod-c` and `alt-c` are the same chord
  under one choice and different chords under another. A key captured
  with Alt as the Shortcut Key is stored as `mod-k`, the same key captured
  under Super is stored as `alt-k`, and a saved settings file holds
  placeholders beside chords written outright. Files registers Show Hidden Files as `alt-.` beside
  `mod-o`, `mod-i` and `mod-n`.
- **Which shortcuts follow the switch is arbitrary.** Snap Left and Right
  do and Snap Top and Bottom do not; Undo does and Save does not.
- **The placeholder is translated in three places**, `dt_keynorm`,
  `dt_keyshow` and the chord list sent to Blit (`dt_keepchord`), where
  CLAUDE.md says one.
- **"Two keys for one action" exists twice, built two ways**: Super or
  Alt, and a second action, Menu Bar Also, that exists only to give Menu
  Bar Escape as well as F10.
- **The restriction is the mechanism's.** `mod` can only become Alt or
  Super. Any row can already capture any chord.
- **Nothing stops a chord Blit keeps for itself.** Blit takes Ctrl+Alt+F1
  to F12 and its stop chord (Ctrl+Alt+Backspace by default) before any
  program sees them, and says which (`KEYBOARD reserved`, Gitea #529);
  bound in the desktop, such a key silently never arrives.
- **It collides with work in flight.** Next Keyboard (#529) was planned on
  Super+Space, and Window Menu is `mod-space`: with Super in the Shortcut
  Key the two are one key.

And the one thing the switch does that nothing else does: it moves every
desktop chord from Alt to Super at once, so Alt reaches programs in windows
-- Firefox's menus, an editor's Meta. And it serves a held desktop attached
from displays that differ: one whose terminal sends Super (Blit, kitty) and
one that cannot.

Blit answered for its side (2026-11-12): which physical key is Super, Alt
or Control is Blit's, per keyboard, as a role map (`modifiers`, shown in
its Keyboard pane as Caps Lock Key Is and Super Key Is), applied before the
keymap so it reaches every program; xkb options are deliberately not
offered. A role changes what a key *is* everywhere; a shortcut says what a
chord *does* on the desktop. Those are two owners, not two copies.

## Decision

**A shortcut is a list of chords, each written as it is pressed, and
nothing else decides what it means.**

- Each action -- the desktop's (`DT_KEYS`), an app's launch
  (`DT_APPKEY`), an app's own action (`DT_AKEY`) -- holds one or more
  chords, space-separated in the value it already has: `"alt-c"`, or
  `"alt-c super-c"`. Any modifiers: Ctrl, Alt, Shift, Super, in any
  combination. A value with one chord is already in this form, so
  `desktop.json`'s shape does not change.
- **`mod` goes.** No placeholder, no `DT_MOD`, no Shortcut Key row.
  `dt_keynorm` only puts a chord's modifiers in one order.
- **Menu Bar Also goes**: Menu Bar is `f10 escape`. A plain key in a
  shortcut's list is looked at only once the focused window has declined
  it, a chord or function key before any window -- which is what Menu Bar
  Also was for: F10 is the way back from a window that takes everything
  else, and vi keeps Escape (found while building it, 0.99.509).
  **Context Menu Also goes** the same way: Context Menu is `shift-f10
  menukey`.
- **Defaults are today's keys under today's default**, Alt: Copy is
  `alt-c`, Close Window `alt-w`, Window Menu `alt-space`. Nothing a person
  presses changes on the day this ships. Next Keyboard (#529) is
  `super-space`, which no longer collides with anything.
- **In the pane**, a row shows its chords as they are pressed (Alt+C,
  Super+C). Activating a row and pressing a key makes that the row's only
  chord; `+` beside it adds the next key pressed as another; the ✕ clears
  the row. The conflict question it asks today asks about each chord.
- **One action, not a setting, for the move the switch was for**: a row
  "Use Super Instead of Alt" that rewrites every chord whose modifiers are
  Alt alone to Super, once, and says how many it changed. Its opposite
  undoes it. What a person bound by hand afterwards is theirs.
- **Blit's own chords are shown and refused.** The pane lists them under a
  heading of their own, read from `KEYBOARD reserved`, and a capture of one
  is refused with a note naming what Blit does with it. Where there is no
  Blit, there is nothing to show.
- **Everything else is worked out from the table, never stored again**:
  the chords Blit keeps away from programs (`INPUT keep`), the menus'
  shortcut labels (the first chord), the terminal rules (Shortcuts in
  Terminals, Terminals Keep Ctrl+A-Z), the conflict marks.
- **Which physical key is Super or Alt is Blit's**: the pane's head points
  at Blit's Keyboard pane, as it does now for layouts.

### What becomes of `mod-` already written

ADR 0054: no migrations, the product carries no legacy unless the owner
asks. So:

- **Saved settings**: converted once, outside the repository, by a
  throwaway script run on each machine the owner keeps a desktop on --
  each `mod-x` becomes the chord or chords the saved `DT_MOD` meant
  (`super alt` gives both), as ADR 0054's own move was done. The desktop
  itself never reads `mod-`.
- **Apps**: Files and the Browser register `mod-` defaults today. Both
  change in the same release and in the catalogue, with that release as
  their minimum. A chord naming a modifier the desktop does not know is
  logged for the developer and left unbound, never guessed.

## Consequences

- One spelling per chord, one table, and one question a person asks of a
  row: what do I press.
- The 23 and the 16 stop being two kinds of shortcut.
- Moving to Super is one action, and the result can be read in the list
  afterwards rather than inferred from a switch.
- The cost, named: an Alt chord in the table stays taken from programs in
  windows (Blit keeps it for the desktop, #530), as today. A second chord
  per action is a second key kept; the defaults add none.
- The cost to compatibility: an older installed Files or Browser on a new
  desktop registers `mod-` chords that do nothing until updated, which the
  catalogue minimum prevents for anyone who updates through it.

## Alternatives

- **Keep the switch and label it better.** Fixes the reading, keeps the two
  spellings, the arbitrary split and the restriction.
- **One chord per action.** Simpler rows, but Menu Bar needs Escape as well
  as F10, and a desktop attached from a terminal without Super needs an Alt
  chord beside a Super one: two mechanisms again.
- **Read `mod-` on load for ever.** A second spelling in the code for as
  long as it lives, which is the parallel this record removes.
- **Swap Alt and Super in Blit's role map instead.** Changes the key for
  every program too, which is a different thing from the desktop's
  shortcuts; it stays available, in Blit, for anyone who wants it.

---

[← documentation index](../README.md) · [← project README](../../README.md)
