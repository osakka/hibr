#!/usr/bin/env python3
"""Check the salat module against adhan-js, an independent implementation.

    python3 tests/salat_adhan.py [path-to-hibr]

Each bundled method, at places from the equator to inside the Arctic
circle, on every third day of a year, with both Asr schools: the six
times salat gives against adhan-js's for the same method, place and day.
adhan-js is found through ADHAN_JS (the folder holding node_modules/adhan),
~/.cache/hibr/adhan -- `npm install --prefix ~/.cache/hibr/adhan adhan@4`
puts it there -- or `npm root -g`; without it, and node, this skips and says so. Times are
whole minutes on both sides (adhan-js keeps a fraction of a second
through its rounding on night-fraction days, so its minute is compared),
so they must agree exactly; a day where
neither can give a time (the sun not reaching the angle, inside the
polar circle) must be one on both.
"""
import datetime, json, os, shutil, subprocess, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import screen as sx
from screen import check, report, tree

if len(sys.argv) > 1:
    sx.HIBR = os.path.abspath(sys.argv[1])
ROOT = os.environ.get("ADHAN_JS", "")
if not ROOT and os.path.exists(os.path.expanduser("~/.cache/hibr/adhan/node_modules/adhan")):
    ROOT = os.path.expanduser("~/.cache/hibr/adhan")
if not ROOT and shutil.which("npm"):
    ROOT = os.path.dirname(subprocess.run(["npm", "root", "-g"], capture_output=True, text=True).stdout.strip())
if not shutil.which("node") or not os.path.exists(os.path.join(ROOT, "node_modules", "adhan")):
    print("salat_adhan: no node or adhan-js here, nothing compared")
    print("0 passed, 0 failed")
    sys.exit(0)

PLACES = [("Makkah", 21.4225, 39.8262), ("Cairo", 30.0444, 31.2357), ("London", 51.5074, -0.1278),
          ("Oslo", 59.9139, 10.7522), ("Reykjavik", 64.1466, -21.9426), ("Tromso", 69.6492, 18.9553),
          ("Jakarta", -6.2088, 106.8456), ("New York", 40.7128, -74.006), ("Sydney", -33.8688, 151.2093),
          ("Tehran", 35.6892, 51.389), ("Istanbul", 41.0082, 28.9784), ("Cape Town", -33.9249, 18.4241),
          ("Karachi", 24.8607, 67.0011), ("Dubai", 25.2048, 55.2708), ("Singapore", 1.3521, 103.8198),
          ("Anchorage", 61.2181, -149.9003)]
METHODS = {"mwl": "MuslimWorldLeague", "isna": "NorthAmerica", "egypt": "Egyptian", "umalqura": "UmmAlQura",
           "karachi": "Karachi", "dubai": "Dubai", "moonsighting": "MoonsightingCommittee", "kuwait": "Kuwait",
           "qatar": "Qatar", "singapore": "Singapore", "tehran": "Tehran", "turkey": "Turkey"}
DAYS = [datetime.date(2026, 1, 1) + datetime.timedelta(days=i) for i in range(0, 365, 3)]
ENV = dict(os.environ, TZ="UTC", HIBR_SALAT=tree("mods/salat/methods"),
           HIBR_MODPATH=os.environ.get("HIBR_TESTMODS") or tree("build/mods"))

JS = r"""
const a = require('adhan');
const cases = JSON.parse(require('fs').readFileSync(0, 'utf8'));
const out = [];
for (const [m, mad, lat, lon, ds] of cases) {
  for (const d of ds) {
    const [y, mo, dd] = d.split('-').map(Number);
    const p = a.CalculationMethod[m]();
    p.madhab = mad === 'hanafi' ? a.Madhab.Hanafi : a.Madhab.Shafi;
    const t = new a.PrayerTimes(new a.Coordinates(lat, lon), new Date(y, mo - 1, dd), p);
    out.push(['fajr', 'sunrise', 'dhuhr', 'asr', 'maghrib', 'isha'].map(k => {
      const v = t[k].getTime(); return isNaN(v) ? -1 : Math.round(v / 60000) * 60; }).join(' '));
  }
}
console.log(out.join('\n'));
"""

for ours, theirs in METHODS.items():
    cases, src = [], ["need salat"]
    for mad in ("shafi", "hanafi"):
        for _, lat, lon in PLACES:
            ds = [d.isoformat() for d in DAYS]
            cases.append([theirs, mad, lat, lon, ds])
            for d in ds:
                src.append("t := salat times -m %s -a %s %s %s %s; echo \"${t%% *}\"" % (ours, mad, lat, lon, d))
    want = subprocess.run(["node", "-e", JS], input=json.dumps(cases), capture_output=True, text=True,
                          env=dict(os.environ, TZ="UTC", NODE_PATH=os.path.join(ROOT, "node_modules")))
    got = subprocess.run([sx.HIBR], input="\n".join(src) + "\n", env=ENV, capture_output=True, text=True)
    w, g = want.stdout.split("\n"), got.stdout.split("\n")
    bad = [(i, w[i], g[i] if i < len(g) else "") for i in range(len(w) - 1)
           if (g[i] if i < len(g) else "") != w[i]]
    n = len(w) - 1
    check("%s agrees with adhan-js's %s at %d places on %d days, both Asr schools (%d days)"
          % (ours, theirs, len(PLACES), len(DAYS), n),
          n > 0 and not bad and not got.stderr and not want.stderr,
          (bad[:4], got.stderr[:200], want.stderr[:200]))

report(len(METHODS))
