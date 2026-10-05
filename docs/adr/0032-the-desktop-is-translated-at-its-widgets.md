# 0032 — The desktop is translated at its widgets, English as the key

Status: accepted

## Context

The desktop draws about a thousand strings for a person: 687 plain
literals (450 distinct), 175 built from variables, and some 160 drawn
straight by an app (`tools/strings.py` counts them). The ticket asks for
real translation (Gitea #68): one lookup for all of it, plural rules, a
language chosen in Control Panel, English as the source.

Changing every call site would be a thousand mechanical edits, each a
chance to break quoting, and would still leave nothing to stop the next
string from bypassing the lookup.

## Decision

- **English is the key.** A string is looked up as written; a catalogue,
  `lang/<code>.json` (the person's own folder first, then the bundled one),
  maps it to the translation. A missing translation shows English.
- **The lookup is at the widgets.** `dt_menu`, `dt_item` (through
  `dt_row`), `dt_sub`, notes, `dt_confirm`, the buttons, `dt_check`,
  desktop icons, window titles and Control Panel's panes and rows translate
  what they are handed. Templates are translated where they are built,
  `dt_tr "Saved %s" "$f"`; a translation may reorder its arguments with
  `%1$s`.
- **The catalogue is a module.** `lang` (C) holds it in a hash and picks a
  plural form by CLDR's integer rules for Arabic, Hebrew, French, Persian,
  Hindi, Russian, Ukrainian, Polish, Japanese, Chinese and Korean, and
  one/other for the rest (`dt_trn`). A hibr map walks a list per lookup.
  It is loaded only when a language is chosen.
- **English pays one variable read.** Each widget asks
  `case $DT_LANG in ?*) ... ;; esac` before calling anything -- a `[ ]`
  test there cost 3,400 instructions a call, `case` nothing measurable;
  the read of `DT_LANG` among the desktop's globals is about 1,700,
  measured at 1.3% of building a menu, which happens on input or a focus
  change, never on an idle frame.
- **The list is kept current.** `tools/strings.py` reads the desktop as
  the shell does and writes `lang/strings.txt` and the pseudo-language
  `lang/xx.json`, which marks every string `⟦like this⟧`;
  `tests/992-strings.t` fails when the code and the list differ, and a
  desktop running in `xx` shows any string that bypassed the lookup in
  plain English.
- **Control Panel > Language** chooses the language and holds the
  right-to-left switch ADR 0031 promised (`DT_BIDI`).

## Consequences

- This release translates what reaches a widget. Templates and text an
  app draws itself are converted app by app; `xx` shows which remain.
- A menu's shortcut letter is chosen from the translated label.
- The login screen is translated later.
- Arabic ships as `lang/ar.json`, all 806 strings, right to left, with
  Arabic's own six plural categories for the counted ones; it is opt-in
  (Control Panel > Language), and `tests/993-lang-ar.t` fails when a string
  is added to the desktop and not translated, or when one left in Latin is
  not named there as deliberate.
