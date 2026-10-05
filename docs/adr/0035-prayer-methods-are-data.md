# 0035 — Prayer methods are data, in a folder

Status: accepted

## Context

There is no one way to compute the prayer times. Authorities differ on
the sun's depression for Fajr and Isha, on whether Isha is an angle or a
fixed interval after Maghrib, on Asr's shadow, on what to do where the
sun never goes deep enough, and on small offsets of their own; a mosque
may adjust any of them by a minute or two. A desktop meant for anywhere
cannot hard-code a list (Gitea #70).

## Decision

As with calendars (ADR 0034), a method is a JSON file in a folder --
angles, intervals, offsets, rounding -- read by the `salat` module, which
computes the sun's position itself, as Meeus gives it, and takes
everything a method decides from the file. Twelve methods are bundled
with the parameters adhan-js uses; anyone can add another as a file. The
Asr school, the high-latitude rule and a person's own minutes are
settings on top of the method, not part of it.

## Consequences

- Every bundled method is checked against adhan-js to the minute, at
  places from the equator to inside the Arctic circle, through the year
  (`tests/salat_adhan.py`).
- The athan is a sound file the person chooses; hibr ships no recording.
- A method that is not angles and offsets -- one published only as a
  timetable -- would need a table kind, as calendars have.
