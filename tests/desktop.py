#!/usr/bin/env python3
"""Drive the window manager through a pty and read the screen it draws.

examples/desktop.hibr is a hibr script, so none of this can be reached from
a .t file: it needs a terminal for the console to open and a mouse to click
with.  Run it directly:  python3 tests/desktop.py [path-to-hibr]

The pty and the terminal model live in tests/screen.py, which every
full-screen suite shares.
"""
import os, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import screen
from screen import Term, check, report, press, release, drag, wheel, load, tree

if len(sys.argv) > 1:
    screen.HIBR = os.path.abspath(sys.argv[1])
MOD = tree("build/mods/console.so")
WM = tree("examples/desktop.hibr")
ROWS, COLS = 24, 80


def run(session, feed=(), wait=1.2):
    """Run a session on top of the window manager and return the last screen."""
    path = "/tmp/hibr-desktop-%d.hibr" % os.getpid()
    open(path, "w").write("%s. %s\ndt_open\n%s\ndt_run\ndt_close\n"
                          % (load(MOD), WM, session))
    t = Term(path, env={"DT_TICK": "60"}, rows=ROWS, cols=COLS, settle=0.5)
    t.keys(feed)
    t.quit(b"q", wait)
    os.unlink(path)
    sc = t.screen()
    sc.quit = t.exited
    return sc, t.raw


TWO_DEF = ('dt_new "Under" 8 30 6 10\n'
           'dt_new "Over" 8 30 9 20\n')
ONE = 'dt_new "Hello" 8 30 6 10'

sc, raw = run(ONE)
check("a window has a top-left corner where it was put",
      sc.g[6][10] == "┌", sc)
check("and a bottom-right corner at its far end",
      sc.g[13][39] == "┘", sc)
check("its title is in the title bar", sc.find("┤ Hello ├") == (6, 12), sc)
check("the close button is at the right of the bar", sc.g[6][37] == "x", sc)
check("the minimise and zoom buttons sit beside it",
      sc.g[6][32] == "┤" and sc.g[6][33] == "_" and
      sc.g[6][35] == "□" and sc.g[6][38] == "├", sc)
check("the wallpaper is drawn behind it", sc.g[12][2] == "·", sc)
check("the bar across the top counts the windows",
      "1 open" in sc.row(0) and "hidden" not in sc.row(0), sc)
check("the alternate screen is left on the way out", b"\x1b[?1049l" in raw)
check("and the mouse is turned off again",
      b"\x1b[?1002l" in raw or b"\x1b[?1000l" in raw)

sc, _ = run(ONE, [press(6, 20), drag(9, 24), release(9, 24)])
check("dragging the title bar moves the window",
      sc.find("┤ Hello ├") == (9, 16), sc)
check("the window is drawn whole at its new place",
      sc.g[9][14] == "┌" and sc.g[16][43] == "┘", sc)
check("and nothing of it is left behind",
      sc.g[6][10] == "·" and sc.g[8][39] == "·" and sc.g[13][12] == "·", sc)

sc, _ = run(ONE, [press(6, 20), drag(0, 0), release(0, 0)])
check("a window cannot be dragged up over the bar",
      sc.find("┤ Hello ├") == (1, 2), sc)

sc, _ = run(ONE, [press(6, 20), drag(23, 79), release(23, 79)])
check("nor off the bottom right", sc.g[16][50] == "┌", sc)

sc, _ = run(ONE, [press(6, 37)])
check("clicking the close button closes the window",
      sc.find("Hello") is None, sc)
check("and the count on the bar goes down", "0 open" in sc.row(0), sc)

sc, _ = run(ONE, [press(6, 33)])
check("minimising takes the window off the screen",
      sc.g[6][10] == "·" and sc.g[10][20] == "·", sc)
check("but leaves a label for it on the bar",
      sc.find("[Hello]") == (0, 7) and "0 open, 1 hidden" in sc.row(0), sc)

sc, _ = run(ONE, [press(6, 33), press(0, 9)])
check("clicking the label brings the window back",
      sc.g[6][10] == "┌" and sc.find("┤ Hello ├") == (6, 12) and
      "1 open" in sc.row(0) and "hidden" not in sc.row(0), sc)

sc, _ = run(ONE, [press(6, 33), b"\x1b"])
check("and escape restores a minimised window too",
      sc.g[6][10] == "┌", sc)

sc, _ = run(TWO_DEF, [press(6, 33), press(9, 43)])
check("two minimised windows each get their own label",
      sc.find("[Under]") == (0, 7) and sc.find("[Over]") == (0, 15) and
      "0 open, 2 hidden" in sc.row(0), sc)
sc, _ = run(TWO_DEF, [press(6, 33), press(9, 43), press(0, 17)])
check("and clicking the second label restores the right one",
      sc.find("┤ Over ├") == (9, 22) and sc.find("[Under]") == (0, 7) and
      "1 open, 1 hidden" in sc.row(0), sc)

sc, _ = run(ONE, [press(6, 35)])
check("zooming fills the screen below the bar",
      sc.g[1][0] == "┌" and sc.g[23][79] == "┘", sc)
sc, _ = run(ONE, [press(6, 35), press(1, 75)])
check("and zooming again puts it back where it was",
      sc.g[6][10] == "┌" and sc.g[13][39] == "┘", sc)

TWO = TWO_DEF

sc, _ = run(TWO)
check("two windows overlap, the newer one on top",
      sc.find("┤ Over ├") == (9, 22) and sc.g[9][20] == "┌", sc)
check("so the one below loses its right edge where they meet",
      sc.g[8][39] == "│" and sc.g[9][39] == "─", sc)
check("but keeps the edges the top one does not cover",
      sc.g[10][10] == "│" and sc.g[10][19] == " " and
      sc.g[10][20] == "│", sc)
check("the bar counts both", "2 open" in sc.row(0), sc)

sc, _ = run(TWO, [press(6, 12)])
check("clicking the lower window raises it",
      sc.g[9][39] == "│" and sc.g[13][25] == "─", sc)
check("and the one that was on top is now clipped",
      sc.g[9][20] == " " and sc.g[9][40] == "─", sc)
check("the raised window is whole again", sc.find("┤ Under ├") == (6, 12) and
      sc.g[6][10] == "┌" and sc.g[13][39] == "┘", sc)

sc, _ = run(TWO, [b"\t"])
check("tab raises the window at the bottom of the stack",
      sc.g[9][39] == "│" and sc.g[13][25] == "─", sc)
sc, _ = run(TWO, [b"\t", b"\t"])
check("and tab again brings the other one back",
      sc.g[9][20] == "┌" and sc.g[9][39] == "─", sc)

sc, _ = run(TWO, [press(6, 12), press(6, 37)])
check("closing the raised window leaves the other",
      sc.find("Under") is None and sc.find("┤ Over ├") == (9, 22), sc)

APP = ('counter_open()  { CN=0; }\n'
       'counter_draw()  { console put -p "w$1" 2 3 "count $CN"; }\n'
       'counter_key()   { [ "$2" = + ] && CN=$((CN+1)) && return 0; return 1; }\n'
       'counter_click() { CN=$(($2 * 100 + $3)); }\n'
       'counter_close() { echo "CLOSED" > /tmp/hibr-dt-closed; }\n'
       'dt_new "App" 8 30 6 10 counter\n')

sc, _ = run(APP)
check("an app draws inside its own window", sc.find("count 0") == (8, 13), sc)

sc, _ = run(APP, [b"\x1b[15~"])
check("a key the app refuses does not reach it", sc.find("count 0"), sc)

sc, _ = run(APP, [press(9, 16)])
check("a click reaches the app in the coordinates it draws in",
      sc.find("count 306") == (8, 13), sc)

try:
    os.unlink("/tmp/hibr-dt-closed")
except OSError:
    pass
sc, _ = run(APP, [press(6, 37)])
check("closing an app's window closes the app",
      sc.find("count") is None and os.path.exists("/tmp/hibr-dt-closed"), sc)

sc, _ = run(APP, [b"+", b"+"])
check("a key the app wants reaches it", sc.find("count 2") == (8, 13), sc)

QUIET = ('quiet_draw() { console put -p "w$1" 1 2 "no keys here"; }\n'
         'dt_new "Quiet" 6 24 4 6 quiet\n')
sc, raw = run(QUIET)
check("an app with no key handler cannot swallow one",
      sc.quit and b"\x1b[?1049l" in raw and
      sc.find("no keys here") == (5, 8), sc)

report(39)
