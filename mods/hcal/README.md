# hcal — calendar systems as data

The Hijri date of a day, and the day of a Hijri date, in whichever
reckoning a person follows: Umm al-Qura, the tabular (arithmetic)
calendars, or one of their own added as a file (Gitea #69, ADR 0034).

```text
hcal list                               name TAB title, one a line
hcal format [-c cal] [-a days] [-n names] [-s shorts] FORMAT [time]
                                        a strftime format, local time, with
                                        Hijri codes: %id %ie %im %iY %iB %ib
hcal date [-c cal] [-a days] [when]      y m d length-of-month; when is a
                                         time in seconds or yyyy-mm-dd,
                                         today (local time) if left out
hcal greg [-c cal] [-a days] y m d       the Gregorian day, yyyy-mm-dd
hcal month [-c cal] y m                  how many days month m has
hcal name [-c cal] m                     the month's name, as the file gives it
hcal info [-c cal]                       name TAB kind TAB title TAB file
```

`format` is strftime's, with six codes of its own for the day in the
calendar chosen: `%id` the day (01-30), `%ie` the day space-padded, `%im`
the month (01-12), `%iY` the year, `%iB` the month's name and `%ib` its
short name -- from the file, or from `-n` and `-s` (twelve names joined
with `|`, for a translation). `%%` stays a percent sign; every other code
is strftime's. The desktop's clock and every date it draws go through it
(`dt_clocktext`), so a custom format can mix both calendars.

Every form prints, or fills the slot under `:=`. The calendar is
`umalqura` unless `-c` says otherwise. `-a` moves the Hijri date by whole
days -- `+1` when the month was seen to start a day earlier where you are
than the calendar says -- and `greg` moves back by the same amount, so the
two stay inverses.

## Calendars are files

A calendar is a JSON file in a folder: `$HIBR_CALENDARS` (colon-separated)
first, then `~/.config/hibr/calendars`, then the installed
`$(SHAREDIR)/calendars`. The first file to give a name wins, so a person's
own `umalqura.json` replaces the bundled one. A file that does not parse,
or lacks what its kind needs, is left out and said in the log (`hibr -d`).

Two kinds:

- `table` -- month lengths from a published table: `first_year`,
  `first_day` (the Julian day of 1 Muharram of that year) and `years`, one
  number a year whose bit m is set when month m+1 has 30 days. Outside the
  table the calendar named by `outside` (a tabular one) takes over.
- `tabular` -- the 30-year arithmetic cycle: `epoch` (the Julian day of
  1 Muharram 1 AH; 1948440 civil, 1948439 astronomical) and `leap`, the
  years of the cycle that have 355 days.

Both may give `months`, twelve names, `short`, their short forms (else
the first three letters), and a `title`.

Bundled: `umalqura` (ICU's Umm al-Qura table, 1300-1600 AH, written by
`tools/hcalgen.py`), `civil` and `astronomical` (the 2, 5, 7, 10, 13, 16,
18, 21, 24, 26, 29 cycle on the two epochs). `civil` is also built in, so
the module works with no folder at all.

A calendar of twelve months is assumed: a lunisolar one with a leap month
(the Hebrew calendar) would need the month arrays widened first.

## Checked

`tests/hcal_icu.py` compares every day from 1870 to 2200 in each bundled
calendar with ICU's (islamic-umalqura, islamic-civil, islamic-tbla), both
ways; `tests/999-hcal.t` records the command itself, a day's adjustment, a
calendar added from a folder and a broken one left out.
