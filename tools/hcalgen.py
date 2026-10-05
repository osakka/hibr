#!/usr/bin/env python3
"""Write mods/hcal/calendars/umalqura.json from ICU's own Umm al-Qura table.

    python3 tools/hcalgen.py

ICU keeps the Umm al-Qura month lengths for 1300-1600 AH (its own data,
from the Saudi tables); this reads each month's length from it -- the days
between its first and the next month's first -- and writes them as one
12-bit mask a year, bit m set when month m+1 has 30 days, with the Julian
day of the table's first day. Outside the table ICU falls back to the
civil arithmetic calendar, and so does hcal: the file names it. Needs
PyICU (python3-icu); run again only to regenerate, and check the result
with tests/hcal_icu.py.
"""
import json, os, sys
import icu

Y0, Y1 = 1300, 1600
UTC = icu.TimeZone.createTimeZone("UTC")
F = icu.UCalendarDateFields


def jd(cal, y, m, d):
    """The Julian day number of an Umm al-Qura date, through ICU."""
    cal.clear()
    cal.set(F.ERA, 0)
    cal.set(F.YEAR, y)
    cal.set(F.MONTH, m)
    cal.set(F.DATE, d)
    return int(cal.getTime() // 86400) + 2440588


def main():
    cal = icu.Calendar.createInstance(UTC, icu.Locale("en@calendar=islamic-umalqura"))
    masks = []
    for y in range(Y0, Y1 + 1):
        mask = 0
        for m in range(12):
            a = jd(cal, y, m, 1)
            b = jd(cal, y + 1, 0, 1) if m == 11 else jd(cal, y, m + 1, 1)
            n = b - a
            if n not in (29, 30):
                sys.exit("month %d/%d is %d days long" % (m + 1, y, n))
            if n == 30:
                mask |= 1 << m
        masks.append(mask)
    out = {
        "name": "umalqura",
        "title": "Umm al-Qura",
        "kind": "table",
        "first_year": Y0,
        "first_day": jd(cal, Y0, 0, 1),
        "years": masks,
        "outside": "civil",
        "months": ["Muharram", "Safar", "Rabi' al-Awwal", "Rabi' al-Thani", "Jumada al-Ula",
                   "Jumada al-Akhirah", "Rajab", "Sha'ban", "Ramadan", "Shawwal",
                   "Dhu al-Qa'dah", "Dhu al-Hijjah"],
        "source": "ICU %s, islamic-umalqura" % icu.ICU_VERSION,
    }
    here = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    p = os.path.join(here, "mods", "hcal", "calendars", "umalqura.json")
    with open(p, "w") as f:
        json.dump(out, f, indent=None, separators=(",", ":"))
        f.write("\n")
    print("wrote %s: %d years from %d, day %d" % (p, len(masks), Y0, out["first_day"]))


main()
