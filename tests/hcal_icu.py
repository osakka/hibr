#!/usr/bin/env python3
"""Check the hcal module against ICU, day by day.

    python3 tests/hcal_icu.py [path-to-hibr]

Every day from 1870 to 2200 -- the whole of the Umm al-Qura table
(1300-1600 AH) and years either side of it, where the table hands over to
the civil arithmetic -- in each of hibr's bundled calendars, against the
ICU calendar it is meant to agree with: umalqura with islamic-umalqura,
civil with islamic-civil, astronomical with islamic-tbla. Both ways: the
Hijri date of each day, and the day each Hijri date goes back to. Needs
PyICU (python3-icu); skips, saying so, without it.
"""
import datetime, os, subprocess, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import screen as sx
from screen import check, report, tree

try:
    import icu
except ImportError:
    print("hcal_icu: no PyICU here, nothing compared")
    print("0 passed, 0 failed")
    sys.exit(0)

if len(sys.argv) > 1:
    sx.HIBR = os.path.abspath(sys.argv[1])
UTC = icu.TimeZone.createTimeZone("UTC")
F = icu.UCalendarDateFields
T0 = int(datetime.datetime(1870, 1, 1, tzinfo=datetime.timezone.utc).timestamp())
T1 = int(datetime.datetime(2200, 1, 1, tzinfo=datetime.timezone.utc).timestamp())
PAIRS = [("umalqura", "islamic-umalqura"), ("civil", "islamic-civil"),
         ("astronomical", "islamic-tbla")]
ENV = dict(os.environ, TZ="UTC", HIBR_CALENDARS=tree("mods/hcal/calendars"),
           HIBR_MODPATH=os.environ.get("HIBR_TESTMODS") or tree("build/mods"))

for ours, theirs in PAIRS:
    cal = icu.Calendar.createInstance(UTC, icu.Locale("en@calendar=" + theirs))
    want = []
    for t in range(T0, T1, 86400):
        cal.setTime(float(t))
        want.append("%d %d %d" % (cal.get(F.YEAR), cal.get(F.MONTH) + 1, cal.get(F.DATE)))
    src = ("need hcal\nfor ((t = %d; t < %d; t += 86400)); do\n"
           "d := hcal date -c %s \"$t\"\nread -r y m n l <<< \"$d\"\n"
           "g := hcal greg -c %s \"$y\" \"$m\" \"$n\"\necho \"$y $m $n|$g\"\ndone\n"
           % (T0, T1, ours, ours))
    out = subprocess.run([sx.HIBR, "-c", src], env=ENV, capture_output=True, text=True)
    got = out.stdout.split("\n")
    bad = []
    back = []
    for i, w in enumerate(want):
        line = got[i] if i < len(got) else ""
        h, _, g = line.partition("|")
        if h != w:
            bad.append((i, w, h))
        iso = datetime.datetime.fromtimestamp(T0 + i * 86400, datetime.timezone.utc).strftime("%Y-%m-%d")
        if g != iso:
            back.append((iso, line))
    check("%s agrees with ICU's %s on every day from 1870 to 2200 (%d days)"
          % (ours, theirs, len(want)), not bad and not out.stderr, (bad[:5], out.stderr[:300]))
    check("and every %s date goes back to the day it came from" % ours, not back, back[:5])

report(6)
