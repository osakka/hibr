#!/usr/bin/env python3
"""The uni module's Arabic shaping and display order against GNU FriBidi,
case by case: every Arabic letter alone and joined on either side, lam-alef
in each context, harakat and tatweel inside words, mixed Arabic, Latin and
numbers, and real sentences. `uni vis` must give what fribidi_log2vis gives
with shaping and mirroring on, its ligature fillers (U+FEFF) left out.
Skipped, and said so, when libfribidi is not installed.

    python3 tests/uni_shape.py [path-to-hibr]
"""
import ctypes, ctypes.util, os, subprocess, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import screen
from screen import check, report, tree

if len(sys.argv) > 1:
    screen.HIBR = os.path.abspath(sys.argv[1])
MODS = os.environ.get("HIBR_TESTMODS") or tree("build/mods")

try:
    FB = ctypes.CDLL(ctypes.util.find_library("fribidi") or "libfribidi.so.0")
except OSError:
    print("skip: libfribidi is not installed")
    sys.exit(0)

PAR_ON = 0x00000040


def fribidi(s):
    n = len(s)
    U = ctypes.c_uint32 * n
    src = U(*[ord(c) for c in s])
    vis = U()
    base = ctypes.c_uint32(PAR_ON)
    FB.fribidi_log2vis(src, n, ctypes.byref(base), vis, None, None, None)
    return "".join(chr(c) for c in vis if c != 0xFEFF)


letters = [chr(c) for c in range(0x0621, 0x064B)] + \
    [chr(c) for c in (0x0671, 0x067E, 0x0686, 0x0698, 0x06A4, 0x06A9, 0x06AF, 0x06CC, 0x06D2)]
beh = "ب"
cases = []
for x in letters:
    cases += [x, beh + x, x + beh, beh + x + beh]
for alef in "آأإا":
    cases += ["ل" + alef, beh + "ل" + alef, "ل" + alef + beh,
              beh + "ل" + alef + beh, "لَ" + alef]
cases += [
    "كَتَبَ",
    "مـــحمد",
    "سلام عليكم",
    "hello مرحبا world",
    "العدد 123 هنا",
    "النسبة (50%) من الكل",
    "بسم الله الرحمن الرحيم",
    "قال: \"نعم\" ثم ذهب.",
    "file.txt ملف",
    "שלום עולם",
    "الساعة ١٢:٣٠",
    "[عربي] text {نص}",
]
r = subprocess.run([screen.HIBR, "-c",
                    "mod load %s/uni.so; while IFS= read -r l; do uni vis \"$l\"; done" % MODS],
                   input="\n".join(cases) + "\n", capture_output=True, text=True, timeout=120)
got = r.stdout.split("\n")
bad = [(c, got[i] if i < len(got) else None, fribidi(c)) for i, c in enumerate(cases)
       if (got[i] if i < len(got) else None) != fribidi(c)]
check("every case shapes and orders as FriBidi does (%d cases)" % len(cases), not bad,
      "%d differ: %s" % (len(bad), [(c, g, w, [hex(ord(x)) for x in (g or "")],
                                    [hex(ord(x)) for x in w]) for c, g, w in bad[:4]]))
report(1)
