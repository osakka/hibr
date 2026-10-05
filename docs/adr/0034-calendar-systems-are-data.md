# 0034 — Calendar systems are data, in a folder

Status: accepted

## Context

The Hijri date of a day is not one answer. Saudi Arabia's Umm al-Qura
calendar is a published table; the tabular calendars are arithmetic, with
several leap-year patterns and two epochs; and many people follow a local
sighting of the moon, a day either side of any of them. On 5 October 2026
Umm al-Qura says 24 Rabi' al-Thani 1448, the civil tabular calendar 22,
the astronomical 23. A desktop meant to work anywhere cannot pick one
(Gitea #69).

## Decision

A calendar system is a JSON file, like a theme (ADR 0028): a `table` of
month lengths or a `tabular` cycle, read by the `hcal` module from
`$HIBR_CALENDARS`, `~/.config/hibr/calendars` and the installed folder, the
first of a name winning. hibr ships Umm al-Qura (generated from ICU's
table), and the civil and astronomical tabular calendars; anyone can add a
region's own as a file, with no code. A whole-day adjustment, applied to
the Hijri date and undone on the way back, covers local sighting.

Gregorian is always shown; Hijri is a switch, shown beside it.

## Consequences

- Each bundled calendar is checked against ICU for every day from 1870 to
  2200 (`tests/hcal_icu.py`); a table hands over to its named tabular
  calendar outside its years, as ICU does.
- `civil` is built into the module, so it answers with no folder.
- Twelve months are assumed; a lunisolar calendar would need the module
  widened first.
- Prayer times (#70) can follow the same shape: methods as files.
