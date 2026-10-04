#!/usr/bin/env python3
"""The pim module's recurrence expansion against RFC 5545's own examples.

Every rule in RFC 5545 section 3.8.5.3, with the DTSTART the RFC gives it
and in its zone (America/New_York), expanded by `pim ics expand` over a
window and compared, instance by instance, with what python-dateutil makes
of the same rule. dateutil is the reference, not this file: its answers are
kept in tests/pim/rrule.txt, written by `--regen` where dateutil can be
imported, so the suite runs anywhere and still compares with something
independent of the code it checks.

    python3 tests/pim_rrule.py [path-to-hibr]
    PYTHONPATH=/path/to/dateutil python3 tests/pim_rrule.py --regen
"""
import os, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
FIX = os.path.join(HERE, "pim", "rrule.txt")
HIBR = os.environ.get("HIBR", os.path.join(ROOT, "build", "hibr"))
MOD = os.environ.get("PIMSO", os.path.join(os.environ.get("HIBR_TESTMODS", os.path.join(ROOT, "build", "mods")), "pim.so"))
TZ = "America/New_York"
FROM, TO = "19960101T000000", "20080101T000000"

RULES = [
    ("19970902T090000", "FREQ=DAILY;COUNT=10"),
    ("19970902T090000", "FREQ=DAILY;UNTIL=19971224T000000Z"),
    ("19970902T090000", "FREQ=DAILY;INTERVAL=2;COUNT=40"),
    ("19970902T090000", "FREQ=DAILY;INTERVAL=10;COUNT=5"),
    ("19980101T090000", "FREQ=YEARLY;UNTIL=20000131T140000Z;BYMONTH=1;BYDAY=SU,MO,TU,WE,TH,FR,SA"),
    ("19980101T090000", "FREQ=DAILY;UNTIL=20000131T140000Z;BYMONTH=1"),
    ("19970902T090000", "FREQ=WEEKLY;COUNT=10"),
    ("19970902T090000", "FREQ=WEEKLY;UNTIL=19971224T000000Z"),
    ("19970902T090000", "FREQ=WEEKLY;INTERVAL=2;WKST=SU;COUNT=20"),
    ("19970902T090000", "FREQ=WEEKLY;UNTIL=19971007T000000Z;WKST=SU;BYDAY=TU,TH"),
    ("19970902T090000", "FREQ=WEEKLY;COUNT=10;WKST=SU;BYDAY=TU,TH"),
    ("19970901T090000", "FREQ=WEEKLY;INTERVAL=2;UNTIL=19971224T000000Z;WKST=SU;BYDAY=MO,WE,FR"),
    ("19970902T090000", "FREQ=WEEKLY;INTERVAL=2;COUNT=8;WKST=SU;BYDAY=TU,TH"),
    ("19970905T090000", "FREQ=MONTHLY;COUNT=10;BYDAY=1FR"),
    ("19970905T090000", "FREQ=MONTHLY;UNTIL=19971224T000000Z;BYDAY=1FR"),
    ("19970907T090000", "FREQ=MONTHLY;INTERVAL=2;COUNT=10;BYDAY=1SU,-1SU"),
    ("19970922T090000", "FREQ=MONTHLY;COUNT=6;BYDAY=-2MO"),
    ("19970928T090000", "FREQ=MONTHLY;BYMONTHDAY=-3;COUNT=12"),
    ("19970902T090000", "FREQ=MONTHLY;COUNT=10;BYMONTHDAY=2,15"),
    ("19970930T090000", "FREQ=MONTHLY;COUNT=10;BYMONTHDAY=1,-1"),
    ("19970910T090000", "FREQ=MONTHLY;INTERVAL=18;COUNT=10;BYMONTHDAY=10,11,12,13,14,15"),
    ("19970902T090000", "FREQ=MONTHLY;INTERVAL=2;BYDAY=TU;COUNT=25"),
    ("19970610T090000", "FREQ=YEARLY;COUNT=10;BYMONTH=6,7"),
    ("19970310T090000", "FREQ=YEARLY;INTERVAL=2;COUNT=10;BYMONTH=1,2,3"),
    ("19970101T090000", "FREQ=YEARLY;INTERVAL=3;COUNT=10;BYYEARDAY=1,100,200"),
    ("19970519T090000", "FREQ=YEARLY;BYDAY=20MO;COUNT=5"),
    ("19970512T090000", "FREQ=YEARLY;BYWEEKNO=20;BYDAY=MO;COUNT=5"),
    ("19970313T090000", "FREQ=YEARLY;BYMONTH=3;BYDAY=TH;COUNT=12"),
    ("19970605T090000", "FREQ=YEARLY;BYDAY=TH;BYMONTH=6,7,8;COUNT=30"),
    ("19970902T090000", "FREQ=MONTHLY;BYDAY=FR;BYMONTHDAY=13"),
    ("19970913T090000", "FREQ=MONTHLY;BYDAY=SA;BYMONTHDAY=7,8,9,10,11,12,13;COUNT=10"),
    ("19961105T090000", "FREQ=YEARLY;INTERVAL=4;BYMONTH=11;BYDAY=TU;BYMONTHDAY=2,3,4,5,6,7,8;COUNT=3"),
    ("19970904T090000", "FREQ=MONTHLY;COUNT=3;BYDAY=TU,WE,TH;BYSETPOS=3"),
    ("19970929T090000", "FREQ=MONTHLY;BYDAY=MO,TU,WE,TH,FR;BYSETPOS=-2;COUNT=7"),
    ("19970902T090000", "FREQ=HOURLY;INTERVAL=3;UNTIL=19970902T170000Z"),
    ("19970902T090000", "FREQ=MINUTELY;INTERVAL=15;COUNT=6"),
    ("19970902T090000", "FREQ=MINUTELY;INTERVAL=90;COUNT=4"),
    ("19970902T090000", "FREQ=DAILY;BYHOUR=9,10,11,12,13,14,15,16;BYMINUTE=0,20,40;COUNT=30"),
    ("19970902T090000", "FREQ=MINUTELY;INTERVAL=20;BYHOUR=9,10,11,12,13,14,15,16;COUNT=30"),
    ("19970805T090000", "FREQ=WEEKLY;INTERVAL=2;COUNT=4;BYDAY=TU,SU;WKST=MO"),
    ("19970805T090000", "FREQ=WEEKLY;INTERVAL=2;COUNT=4;BYDAY=TU,SU;WKST=SU"),
    ("20070115T090000", "FREQ=MONTHLY;BYMONTHDAY=15,30;COUNT=5"),
    ("20070131T090000", "FREQ=MONTHLY;COUNT=6"),
    ("20040229T090000", "FREQ=YEARLY;COUNT=3"),
    ("20061029T013000", "FREQ=DAILY;COUNT=3"),
    ("20070310T023000", "FREQ=DAILY;COUNT=3"),
]


# A DTSTART that the rule itself would not produce is the first instance
# by RFC 5545 and is left out by dateutil; the RFC's own example of such a
# rule (Friday the 13th) excludes it with EXDATE, and so does this.
EXDATE = {"FREQ=MONTHLY;BYDAY=FR;BYMONTHDAY=13"}


def ics(dtstart, rule):
    ex = "EXDATE;TZID=%s:%s\r\n" % (TZ, dtstart) if rule in EXDATE else ""
    return ("BEGIN:VCALENDAR\r\nVERSION:2.0\r\nBEGIN:VEVENT\r\nUID:r\r\n"
            "DTSTART;TZID=%s:%s\r\nDURATION:PT1H\r\nRRULE:%s\r\n%sSUMMARY:r\r\n"
            "END:VEVENT\r\nEND:VCALENDAR\r\n" % (TZ, dtstart, rule, ex))


def regen():
    from datetime import datetime
    from zoneinfo import ZoneInfo
    from dateutil.rrule import rrulestr
    z = ZoneInfo(TZ)
    lo = datetime.strptime(FROM, "%Y%m%dT%H%M%S").replace(tzinfo=z)
    hi = datetime.strptime(TO, "%Y%m%dT%H%M%S").replace(tzinfo=z)
    out = []
    for dtstart, rule in RULES:
        st = datetime.strptime(dtstart, "%Y%m%dT%H%M%S").replace(tzinfo=z)
        got = [int(d.timestamp()) for d in rrulestr(rule, dtstart=st).between(lo, hi, inc=True)]
        out.append("%s %s %s" % (dtstart, rule, " ".join(map(str, got))))
    os.makedirs(os.path.dirname(FIX), exist_ok=True)
    with open(FIX, "w") as f:
        f.write("# expected instances of RFC 5545's examples, from python-dateutil\n")
        f.write("\n".join(out) + "\n")
    print("wrote %d rules to %s" % (len(out), FIX))


def main():
    if "--regen" in sys.argv:
        return regen()
    args = [a for a in sys.argv[1:] if not a.startswith("-")]
    hibr = os.path.abspath(args[0]) if args else HIBR
    want = {}
    for line in open(FIX):
        if line.startswith("#") or not line.strip():
            continue
        parts = line.split()
        want[(parts[0], parts[1])] = [int(x) for x in parts[2:]]
    env = dict(os.environ, TZ=TZ)
    lo = subprocess.run(["date", "-d", "%s-%s-%s %s:%s" % (FROM[:4], FROM[4:6], FROM[6:8], FROM[9:11], FROM[11:13]), "+%s"],
                        capture_output=True, text=True, env=env).stdout.strip()
    hi = subprocess.run(["date", "-d", "%s-%s-%s %s:%s" % (TO[:4], TO[4:6], TO[6:8], TO[9:11], TO[11:13]), "+%s"],
                        capture_output=True, text=True, env=env).stdout.strip()
    d = tempfile.mkdtemp(prefix="hibr-rrule-")
    ok = bad = 0
    for dtstart, rule in RULES:
        p = os.path.join(d, "r.ics")
        open(p, "w").write(ics(dtstart, rule))
        r = subprocess.run([hibr, "-c", "mod load %s; pim ics expand %s %s %s" % (MOD, p, lo, hi)],
                           capture_output=True, text=True, env=env)
        got = [int(l.split("\t")[0]) for l in r.stdout.splitlines() if l]
        exp = want.get((dtstart, rule))
        if got == exp:
            ok += 1
            print("ok   %s %s (%d)" % (dtstart, rule, len(got)))
        else:
            bad += 1
            print("FAIL %s %s" % (dtstart, rule))
            if os.environ.get("V"):
                print("  want %s\n  got  %s\n  %s" % (exp, got, r.stderr))
    print()
    print("%d passed, %d failed" % (ok, bad))
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
