#!/usr/bin/env python3
"""The uni module's bidi algorithm against Unicode's own conformance suites:
BidiTest (classes, every paragraph direction asked for) and
BidiCharacterTest (code points, with bracket pairs), both from the same
UCD version as mods/uni/tab.c, kept gzipped in tests/uni. Every case runs
through one hibr, `uni levels`, and is compared on its resolved levels,
paragraph level and visual order.

    python3 tests/uni_bidi.py [path-to-hibr]
"""
import gzip, os, subprocess, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import screen
from screen import check, report, tree

if len(sys.argv) > 1:
    screen.HIBR = os.path.abspath(sys.argv[1])
MODS = os.environ.get("HIBR_TESTMODS") or tree("build/mods")
DATA = tree("tests/uni")


def run(lines, cls):
    load = "mod load %s/uni.so; uni levels%s" % (MODS, " -c" if cls else "")
    r = subprocess.run([screen.HIBR, "-c", load], input="\n".join(lines) + "\n",
                       capture_output=True, text=True, timeout=1200)
    return r.stdout.split("\n")


cases, want = [], []
lv = order = None
for l in gzip.open(os.path.join(DATA, "BidiTest.txt.gz"), "rt"):
    l = l.split("#", 1)[0].strip()
    if not l:
        continue
    if l.startswith("@Levels:"):
        lv = l.split(":", 1)[1].split()
        continue
    if l.startswith("@Reorder:"):
        order = l.split(":", 1)[1].split()
        continue
    if l.startswith("@"):
        continue
    inp, bits = l.split(";")
    for bit, d in ((1, 2), (2, 0), (4, 1)):
        if int(bits) & bit:
            cases.append("%d;%s" % (d, inp.strip()))
            want.append((None, lv, order, l, d))
out = run(cases, True)
bad = []
for i, w in enumerate(want):
    f = out[i].split("\t") if i < len(out) else ["", "", ""]
    if f[1].split() != w[1] or f[2].split() != w[2]:
        bad.append((w[3], w[4], w[1], w[2], f))
check("BidiTest: every case's levels and order (%d cases)" % len(want), not bad,
      "%d differ, first: %s" % (len(bad), bad[:3]))

cases, want = [], []
for l in gzip.open(os.path.join(DATA, "BidiCharacterTest.txt.gz"), "rt"):
    l = l.split("#", 1)[0].strip()
    if not l:
        continue
    f = l.split(";")
    cases.append("%s;%s" % (f[1], f[0]))
    want.append((f[2], f[3].split(), f[4].split(), l))
out = run(cases, False)
bad = []
for i, w in enumerate(want):
    f = out[i].split("\t") if i < len(out) else ["", "", ""]
    if f[0] != w[0] or f[1].split() != w[1] or f[2].split() != w[2]:
        bad.append((w[3], f))
check("BidiCharacterTest: every case's paragraph level, levels and order (%d cases)" % len(want),
      not bad, "%d differ, first: %s" % (len(bad), bad[:3]))
report(2)
