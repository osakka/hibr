#!/usr/bin/env python3
"""Super as the drag modifier, through a pty (Gitea #415).

A mouse report has bits for shift, alt and ctrl and none for Super, so a
drag made with Super held arrived as a plain one and Drag Modifier could not
be Super. The console keeps what the last key event's modifiers said -- the
kitty keyboard protocol reports Super's own press and release -- and puts
super- in front of a report made while it is down; the window manager then
matches it as it matches alt. Run it directly:
python3 tests/superdrag.py [path-to-hibr]
"""
import os, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import screen
from screen import Term, check, report, press, release, drag, load, tree, scratch

if len(sys.argv) > 1:
    screen.HIBR = os.path.abspath(sys.argv[1])
MOD = tree("build/mods/console.so")
WM = tree("programs/desktop/desktop.hibr")
ROWS, COLS = 24, 80
ONE = 'dt_new "Hello" 8 30 6 10'
SUPDOWN = b"\x1b[57444;9u"
SUPUP = b"\x1b[57444;1:3u"
BODY = [press(8, 15), drag(11, 27), release(11, 27)]
# Super's own press and release report nothing, so each rides in one write
# with a report beside it: a write the desktop answers nothing to would
# leave the harness waiting for an idle marker that never comes.
# Super's release is left out after the release of the button: the bytes
# would follow the last report the desktop answers, and wait unread.
HELD = [SUPDOWN + press(8, 15), drag(11, 27), release(11, 27)]
# And let go first, as people do: the drag is still the window's.
EARLY = [SUPDOWN + press(8, 15), drag(11, 27), SUPUP + release(11, 27)]


def run(feed, pre):
    """A desktop with one window, the keys and reports fed, its last screen."""
    path = scratch("superdrag.hibr")
    open(path, "w").write("%s. %s\n%s\ndt_open\n%s\ndt_run\ndt_close\n"
                          % (load(MOD), WM, pre, ONE))
    t = Term(path, rows=ROWS, cols=COLS, settle=0.5)
    if not t.idle:
        t.idle = t.until_idle(8 * (4 if os.environ.get("ASAN_OPTIONS") else 1))
    t.keys(feed)
    t.quit(b"qy", 1.2)
    os.unlink(path)
    return t.screen()


sc = run(HELD, "DT_DRAGMOD=1\nDT_DRAGKEY=super")
check("with Super the drag modifier, a drag in the body with Super held "
      "moves the window", sc.find("┤ Hello ├") == (9, 24), sc)
sc = run(EARLY, "DT_DRAGMOD=1\nDT_DRAGKEY=super")
check("and Super let go before the button still puts it down there",
      sc.find("┤ Hello ├") == (9, 24), sc)
sc = run(BODY, "DT_DRAGMOD=1\nDT_DRAGKEY=super")
check("and the same drag with Super not held does not",
      sc.find("┤ Hello ├") == (6, 12), sc)
sc = run(HELD, "DT_DRAGMOD=1\nDT_DRAGKEY=alt")
check("with alt the drag modifier, Super held does not move it",
      sc.find("┤ Hello ├") == (6, 12), sc)
report(5)
