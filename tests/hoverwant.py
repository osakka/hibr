#!/usr/bin/env python3
"""A window asking for the pointer's movement for itself, through a pty
(Gitea #316).

Where Control Panel > Mouse > Hover is Auto and the terminal reports cells
only, the desktop does not ask for every movement. A focused window that
asks with dt_hoverwant gets it: the terminal is asked for movement, the
window's _hover is told where the pointer is in its content, and nowhere
else does movement light, tip or move focus; focus moving on ends it with a
leave. Where Hover is Off nothing a window asks turns it on (the owner,
2026-11-20). Run it directly:  python3 tests/hoverwant.py [path-to-hibr]
"""
import os, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import screen
from screen import Term, check, report, press, release, load, tree, scratch

if len(sys.argv) > 1:
    screen.HIBR = os.path.abspath(sys.argv[1])
MOD = tree("build/mods/console.so")
WM = tree("programs/desktop/desktop.hibr")
ROWS, COLS = 24, 80
# The asking window is made last, so it has the focus; Other sits to its
# right, overlapping nothing.
# The asker keeps every key it is given, the harness's own quit keys after
# them too, so a check looks for the first.
HW = ('declare -gA HW\n'
      'hw_open() { dt_hoverwant "$1" on; }\n'
      'hw_hover() { HW["at"]="$2 $3"; }\n'
      'hw_key() { HW["key"]+=$2; }\n'
      'hw_draw() { console put -p "w$1" 2 3 "at ${HW["at"]:-none} key ${HW["key"]:-none}"; }\n'
      'dt_app hw "Asker" 8 30\n'
      'dt_new "Other" 8 30 6 45\n'
      'dt_new "Asker" 8 30 6 10 hw')


def hov(row, col):
    return b"\x1b[<35;%d;%dM" % (col + 1, row + 1)


def run(feed, env=None):
    """The desktop, a terminal that reports cells only, the feed; its last
    screen and everything it sent."""
    path = scratch("hoverwant.hibr")
    open(path, "w").write("%s. %s\ndt_open\n%s\ndt_run\ndt_close\n"
                          % (load(MOD), WM, HW))
    t = Term(path, env=env, rows=ROWS, cols=COLS, settle=0.5, caps=[])
    if not t.idle:
        t.idle = t.until_idle(8 * (4 if os.environ.get("ASAN_OPTIONS") else 1))
    t.keys(feed)
    t.quit(b"qy", 1.2)
    os.unlink(path)
    return t.screen(), t.raw


sc, raw = run([0.5, hov(9, 16), 0.4])
check("with Hover on Auto, a focused window that asks is given movement: the "
      "terminal is asked for it and the window told where the pointer is",
      b"\x1b[?1003h" in raw and sc.find("at 3 6") is not None, sc)
sc, raw = run([0.5, hov(9, 16), 0.4], env={"DT_HOVER": "off"})
check("with Hover Off, the asking changes nothing", b"\x1b[?1003h" not in raw
      and sc.find("at none") is not None, sc)
sc, raw = run([0.5, hov(9, 16), 0.3, hov(9, 55), 0.4, b"x", 0.3],
              env={"DT_FOCUSHOVER": "1"})
check("movement outside the asking window is the desktop's to ignore: told "
      "it left, and Focus Follows Mouse does not take the keys away",
      sc.find("at -1 -1 key x") is not None, sc)
sc, raw = run([0.5, hov(9, 16), 0.3, press(9, 55), release(9, 55), 0.4])
check("focus moving to another window ends it, and the asker is told the "
      "pointer has left", sc.find("at -1 -1") is not None, sc)
i = raw.find(b"\x1b[?1003h")
check("and the terminal is asked for movement no more",
      i >= 0 and raw.find(b"\x1b[?1002h", i) > i, raw[-300:])
report(6)
