#!/usr/bin/env python3
"""Drive the window manager through a pty and read the screen it draws.

examples/desktop.hibr is a hibr script, so none of this can be reached from
a .t file: it needs a terminal for the console to open and a mouse to click
with.  Run it directly:  python3 tests/desktop.py [path-to-hibr]

The pty and the terminal model live in tests/screen.py, which every
full-screen suite shares.
"""
import os, re, shutil, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import screen
from screen import Term, check, report, press, release, drag, wheel, load, tree

if len(sys.argv) > 1:
    screen.HIBR = os.path.abspath(sys.argv[1])
MOD = tree("build/mods/console.so")
WM = tree("examples/desktop.hibr")
ROWS, COLS = 24, 80


def run(session, feed=(), wait=1.2, env=None, pre=""):
    """Run a session on top of the window manager and return the last screen."""
    path = "/tmp/hibr-desktop-%d.hibr" % os.getpid()
    open(path, "w").write("%s. %s\n%s\ndt_open\n%s\ndt_run\ndt_close\n"
                          % (load(MOD), WM, pre, session))
    t = Term(path, env=dict({"DT_TICK": "60"}, **(env or {})), rows=ROWS,
             cols=COLS, settle=0.5)
    t.keys(feed)
    t.quit(b"q", wait)
    os.unlink(path)
    sc = t.screen()
    sc.quit = t.exited
    sc.status = t.status
    return sc, t.raw


TWO_DEF = ('dt_new "Under" 8 30 6 10\n'
           'dt_new "Over" 8 30 9 20\n')
ONE = 'dt_new "Hello" 8 30 6 10'

sc, raw = run(ONE)
check("a window has a top-left corner where it was put",
      sc.g[6][10] == "┌", sc)
check("and a grow box at its far corner", sc.g[13][39] == "◢", sc)
check("its title is in the title bar", sc.find("┤ Hello ├") == (6, 12), sc)
check("the close button is at the right of the bar", sc.g[6][37] == "x", sc)
check("the minimise and zoom buttons sit beside it",
      sc.g[6][32] == "┤" and sc.g[6][33] == "_" and
      sc.g[6][35] == "□" and sc.g[6][38] == "├", sc)
check("the wallpaper is drawn behind it", sc.g[12][2] == "·", sc)
check("the menu bar names hibr, the time and the active application",
      sc.find("hibr") == (0, 2) and re.search(r"\d\d:\d\d", sc.row(0)) and
      sc.find("Desktop ▾") is not None, sc)
check("the alternate screen is left on the way out", b"\x1b[?1049l" in raw)
check("and the mouse is turned off again",
      b"\x1b[?1002l" in raw or b"\x1b[?1000l" in raw)

sc, _ = run(ONE, [press(6, 20), drag(9, 24), release(9, 24)])
check("dragging the title bar moves the window",
      sc.find("┤ Hello ├") == (9, 16), sc)
check("the window is drawn whole at its new place",
      sc.g[9][14] == "┌" and sc.g[16][43] == "◢", sc)
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
check("and nothing is left where it was", sc.g[6][10] == "·", sc)

sc, _ = run(ONE, [press(6, 33)])
check("minimising takes the window off the screen",
      sc.g[6][10] == "·" and sc.g[10][20] == "·", sc)
sc, _ = run(ONE, [press(6, 33), press(0, 70)])
check("and it is still listed, marked hidden, in the application menu",
      sc.find("· Hello") is not None, sc)

sc, _ = run(ONE, [press(6, 33), press(0, 70), b"\r"])
check("choosing it there brings it back",
      sc.g[6][10] == "┌" and sc.find("┤ Hello ├") == (6, 12), sc)

sc, _ = run(ONE, [press(6, 35)])
check("zooming fills the screen below the bar",
      sc.g[1][0] == "┌" and sc.g[23][79] == "◢", sc)
sc, _ = run(ONE, [press(6, 35), press(1, 75)])
check("and zooming again puts it back where it was",
      sc.g[6][10] == "┌" and sc.g[13][39] == "◢", sc)

TWO = TWO_DEF

sc, _ = run(TWO)
check("two windows overlap, the newer one on top",
      sc.find("┤ Over ├") == (9, 22) and sc.g[9][20] == "┌", sc)
check("so the one below loses its right edge where they meet",
      sc.g[8][39] == "│" and sc.g[9][39] == "─", sc)
check("but keeps the edges the top one does not cover",
      sc.g[10][10] == "│" and sc.g[10][19] == " " and
      sc.g[10][20] == "│", sc)
check("both are listed in the application menu",
      run(TWO, [press(0, 70)])[0].find("Under") is not None, sc)

sc, _ = run(TWO, [press(6, 12)])
check("clicking the lower window raises it",
      sc.g[9][39] == "│" and sc.g[13][25] == "─", sc)
check("and the one that was on top is now clipped",
      sc.g[9][20] == " " and sc.g[9][40] == "─", sc)
check("the raised window is whole again", sc.find("┤ Under ├") == (6, 12) and
      sc.g[6][10] == "┌" and sc.g[13][39] == "◢", sc)

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

# --- the menu bar ---------------------------------------------------------

MENUS = ('noted_draw() { console put -p "w$1" 1 2 "count $NC"; }\n'
         'noted_open() { NC=0; }\n'
         'noted_bump() { NC=$((NC + 1)); }\n'
         'noted_menus() {\n'
         '  dt_menu "Count"\n'
         '  dt_item "Bump" b noted_bump\n'
         '  dt_sep\n'
         '  dt_item "Reset" r noted_open\n'
         '  dt_item "Close" w dt_close_focused\n'
         '  dt_menu "More"\n'
         '  dt_item "Bump Twice" t noted_twice\n'
         '  dt_sub "Set To"\n'
         '  dt_mark "One" o 0 noted_set 1\n'
         '  dt_mark "Two" x 1 noted_set 2\n'
         '  dt_end\n'
         '  dt_dim "Not Now"\n'
         '}\n'
         'noted_set() { NC=$1; }\n'
         'noted_twice() { noted_bump; noted_bump; }\n'
         'dt_app noted "Noted" 6 24\n'
         'dt_new "Noted" 8 30 6 10 noted\n')

sc, _ = run(MENUS)
check("an app's own menus are on the bar when it has focus",
      sc.find("Count") == (0, 8) and sc.find("More") is not None, sc)
check("and the application menu names it",
      sc.find("Noted ▾") is not None, sc)

sc, _ = run(MENUS, [press(0, 9)])
check("clicking a title drops the menu under it",
      sc.find("Bump") == (1, 9) and sc.find("Reset") == (3, 9), sc)
check("a separator is drawn between the groups", sc.at(2, 8) == "─", sc)
check("each item shows the letter that picks it", sc.at(1, 19) == "b" and
      sc.at(3, 19) == "r", sc)

sc, _ = run(MENUS, [press(0, 9), press(0, 9)])
check("clicking it again puts it away", sc.find("Bump") is None, sc)

sc, _ = run(MENUS, [press(0, 9), press(15, 60)])
check("clicking away from an open menu shuts it",
      sc.find("Bump") is None, sc)

sc, _ = run(MENUS, [press(0, 9), b"b"])
check("a letter picks the item beside it", sc.find("count 1") is not None
      and sc.find("Bump") is None, sc)

sc, _ = run(MENUS, [press(0, 9), b"\r"])
check("enter takes the highlighted one", sc.find("count 1") is not None, sc)

sc, _ = run(MENUS, [press(0, 9), b"\x1b[B", b"\r"])
check("down steps over the separator to the next real item",
      sc.find("count 0") is not None and sc.find("Reset") is None, sc)

sc, _ = run(MENUS, [press(0, 9), b"\x1b"])
check("escape shuts the menu and does nothing else",
      sc.find("Bump") is None and sc.find("count 0") is not None, sc)

sc, _ = run(MENUS, [b"\x1b[21~"])
check("f10 opens the bar at the hibr menu",
      sc.find("About hibr") is not None, sc)
sc, _ = run(MENUS, [b"\x1b"])
check("and so does escape", sc.find("About hibr") is not None, sc)

sc, _ = run(MENUS, [b"\x1b[21~", b"\x1b[C"])
check("right walks to the next menu along",
      sc.find("Bump") is not None and sc.find("About hibr") is None, sc)
sc, _ = run(MENUS, [b"\x1b[21~", b"\x1b[C", b"\x1b[C"])
check("and on to the one after that", sc.find("Bump Twice") is not None, sc)
sc, _ = run(MENUS, [b"\x1b[21~", b"\x1b[D"])
check("left from the first wraps round to the application menu",
      sc.find("Noted") is not None and sc.find("About hibr") is None, sc)

sc, _ = run(MENUS, [b"\x1b[21~", b"\x1b[C", b"\x1b[C", b"t"])
check("an item in the second menu runs too",
      sc.find("count 2") is not None, sc)

sc, _ = run(MENUS, [press(0, 2), b"n"])
check("the hibr menu opens a registered app",
      sc.find("┤ Noted ├") is not None and
      sc.find("count 0") is not None, sc)

sc, raw = run(MENUS, [press(0, 2), b"q"])
check("and quit from the hibr menu ends the session",
      sc.quit and b"\x1b[?1049l" in raw, sc)

sc, _ = run(MENUS, [press(0, 2), b"a"])
check("about says what this is", sc.find("a desktop written in the shell")
      is not None, sc)

sc, _ = run(MENUS, [press(6, 37)])
check("with no window left the desktop's own menus show",
      sc.find("Desktop") is not None and sc.find("Count") is None, sc)

# --- resizing -------------------------------------------------------------

sc, _ = run(ONE, [press(13, 39), drag(17, 51), release(17, 51)])
check("dragging the grow box makes the window bigger",
      sc.at(6, 10) == "┌" and sc.at(17, 51) == "◢", sc)
check("and the title bar's buttons move out with the edge",
      sc.at(6, 49) == "x", sc)

sc, _ = run(ONE, [press(13, 39), drag(9, 25), release(9, 25)])
check("and smaller", sc.at(9, 25) == "◢" and sc.at(6, 10) == "┌", sc)

sc, _ = run(ONE, [press(13, 39), drag(6, 10), release(6, 10)])
check("but never smaller than the title bar's buttons need",
      sc.at(9, 25) == "◢" and sc.at(6, 23) == "x" and
      sc.at(6, 10) == "┌", sc)

sc, _ = run(ONE, [press(13, 39), drag(30, 120), release(30, 120)])
check("nor past the edge of the screen", sc.at(23, 79) == "◢", sc)

# --- the window menu, and items that cannot be chosen ---------------------

# Where Window sits on the bar: after the app's two menus and Edit when the
# app has focus, after Edit alone when nothing does.
#   "  hibr  Count  More  Edit  Window"      "  hibr  Edit  Window"
WIN, WIN0 = 26, 14

sc, _ = run(MENUS, [press(0, WIN)])
check("a window menu is there even for an app with its own menus",
      sc.find("Move") is not None and sc.find("Cycle") is not None, sc)

sc, _ = run(MENUS, [press(6, 37), press(0, WIN0)])
check("with nothing focused its items lose their letters",
      sc.find("Move") is not None and sc.at(1, 19) != "m", sc)

sc, _ = run(MENUS, [press(6, 37), press(0, WIN0), b"m"])
check("and a dimmed letter does nothing", sc.find("Move") is not None, sc)

# --- submenus -------------------------------------------------------------

sc, _ = run(MENUS, [press(0, 15)])
check("an item with a submenu shows an arrow, not a letter",
      sc.find("Set To") is not None and sc.at(2, 30) == "▸", sc)

sc, _ = run(MENUS, [press(0, 15), b"\x1b[B", b"\x1b[C"])
check("right opens it beside its parent",
      sc.find("One") is not None and sc.find("Two") is not None and
      sc.find("Bump Twice") is not None, sc)
check("and the current choice carries a tick",
      sc.find("✓ Two") is not None and sc.find("✓ One") is None, sc)

sc, _ = run(MENUS, [press(0, 15), b"\x1b[B", b"\x1b[C", b"\x1b[D"])
check("left comes back out, leaving the parent open",
      sc.find("One") is None and sc.find("Bump Twice") is not None, sc)

sc, _ = run(MENUS, [press(0, 15), b"\x1b[B", b"\x1b[C", b"o"])
check("an item inside a submenu runs",
      sc.find("count 1") is not None and sc.find("One") is None, sc)

sc, _ = run(MENUS, [press(0, 15), b"\x1b[B", b"\x1b[B", b"\r"])
check("a dimmed item is stepped over, so down twice wraps past it",
      sc.find("count 2") is not None, sc)

# --- moving and resizing from the keyboard --------------------------------

sc, _ = run(MENUS, [press(0, WIN), b"m"])
check("move says what it is doing", sc.find("moving") is not None, sc)

sc, _ = run(MENUS, [press(0, WIN), b"m", b"\x1b[A", b"\x1b[A",
                    b"\x1b[D", b"\r"])
check("the arrows move the window while it is held",
      sc.find("┤ Noted ├") == (4, 11) and sc.find("moving") is None, sc)

sc, _ = run(MENUS, [press(0, WIN), b"r", b"\x1b[C", b"\x1b[C", b"\r"])
check("and resize it in the other mode",
      sc.at(6, 10) == "┌" and sc.at(13, 41) == "◢", sc)

sc, _ = run(MENUS, [press(0, WIN), b"m", b"\x1b[A", b"\x1b"])
check("escape ends the mode, keeping what it did",
      sc.find("┤ Noted ├") == (5, 12) and sc.find("moving") is None, sc)

# --- breaks, saved settings, and a session that outlives its terminal ----

sc, raw = run(ONE, feed=[b"\x03", b"\x1c", b"\x1a", b"\x1b[21~"])
check("ctrl-c, ctrl-\\ and ctrl-z are keys, not the end of the desktop",
      sc.find("About hibr") is not None and sc.status == 0, sc)
check("and Detach is on the hibr menu, dimmed when nothing holds it",
      sc.find("Detach") is not None, sc)

import tempfile

# --- the hibr menu comes from folders of apps ---------------------------

UCONF = tempfile.mkdtemp(prefix="hibr-apps-")
os.makedirs(os.path.join(UCONF, "hibr", "apps"))
open(os.path.join(UCONF, "hibr", "apps", "hello.hibr"), "w").write(
    'dt_app hello "Hello" 6 20 once "☺"\n'
    'hello_draw() { console put -p "w$1" 1 1 "hi there"; }\n')
open(os.path.join(UCONF, "hibr", "apps", "calc.hibr"), "w").write(
    'dt_app calc "My Sums" 6 20 once "±"\n')
APPS = 'DT_APPDIRS+=("%s")\ndt_apps\n' % tree("examples/apps")
MENU = [b"\x1b[21~", b"\x1b[B"]
sc, raw = run("", feed=MENU, env={"XDG_CONFIG_HOME": UCONF}, pre=APPS)
rows = [r for r in range(2, 16) if "Hello" in sc.row(r) or
        "Files" in sc.row(r) or "Mines" in sc.row(r)]
check("an app in your own folder is on the menu, in its sorted place",
      len(rows) == 3 and "Files" in sc.row(rows[0]) and
      "Hello" in sc.row(rows[1]) and "Mines" in sc.row(rows[2]), sc)
check("and a file of yours replaces the bundled app of that name",
      sc.find("My Sums") is not None and sc.find("Calculator") is None, sc)
shutil.rmtree(UCONF, True)

LAUNCH = MENU + [b"c"]
sc, raw = run("", feed=LAUNCH + LAUNCH, pre=APPS)
check("an app declared once opens one window, however often launched",
      sc.text().count("┤ Calculator ├") == 1, sc)
LAUNCH = MENU + [b"f"]
sc, raw = run("", feed=LAUNCH + LAUNCH, pre=APPS)
check("and one that is not opens another window each time",
      sc.text().count("┤ Files ├") == 2, sc)
CONF = tempfile.mkdtemp(prefix="hibr-conf-")
PANEL = '. %s/panel.hibr' % tree("examples/apps")
sc, raw = run('dt_new "Settings" 12 34 2 2 panel', feed=[b"\x1b[C"],
              env={"XDG_CONFIG_HOME": CONF}, pre=PANEL)
saved = os.path.join(CONF, "hibr", "desktop.hibr")
text = open(saved).read() if os.path.exists(saved) else ""
check("a changed setting is written at once, as a script",
      "CP_THEME=slate" in text and "DT_WALL=\\#1a202c" in text and
      "DT_TICK=" in text, text or sc)
sc, raw = run('dt_new "Settings" 12 34 2 2 panel',
              env={"XDG_CONFIG_HOME": CONF}, pre=PANEL)
check("and the next desktop starts with it", sc.find("slate") is not None, sc)
shutil.rmtree(CONF, True)

HOLD = tempfile.mkdtemp(prefix="hibr-hold-")
held = os.path.join(HOLD, "session.hibr")
open(held, "w").write("%s. %s\ndt_open\ndt_new \"Held\" 8 30 6 10\n"
                      "dt_run\ndt_close\n"
                      % (load(MOD, "build/mods/pty.so",
                              "build/mods/hold.so"), WM))
HENV = {"TMPDIR": HOLD, "DT_TICK": "60"}
HOLDC = load("build/mods/pty.so", "build/mods/hold.so")


def unhold():
    """Kill the held desktop whatever happened, so a failure leaves nothing
    running in the background for hours."""
    import subprocess
    subprocess.run([screen.HIBR, "-c", HOLDC + "hold kill desk"],
                   env=dict(os.environ, **HENV), capture_output=True)
    shutil.rmtree(HOLD, True)


import atexit
atexit.register(unhold)
t = Term("-c", HOLDC + "hold new desk %s %s" % (screen.HIBR, held),
         env=HENV, settle=1.5)
sc = t.screen()
check("a desktop started with hold new draws as usual",
      sc.g[6][10] == "┌" and sc.find("Held") is not None, sc)
t.send(b"\x1c", settle=0.6)
check("ctrl-\\ detaches, and says how to come back",
      b"[desk: detached -- hold attach desk]" in t.out, t.out.decode(errors="replace"))
t.close()

t = Term("-c", HOLDC + "hold attach desk", env=HENV, settle=1.5)
sc = t.screen()
check("attached from a new terminal, the whole desktop is drawn again",
      sc.g[6][10] == "┌" and sc.find("Held") is not None and
      sc.find("hibr") == (0, 2), sc)
t.send(b"\x1b[21~", settle=0.4)
t.send(b"d", settle=0.8)
check("Detach on the hibr menu detaches too",
      b"[desk: detached -- hold attach desk]" in t.out, t.out.decode(errors="replace"))
t.close()

t = Term("-c", HOLDC + "hold attach desk; echo \"back $?\"", env=HENV,
         settle=1.5)
t.send(b"q", settle=1.0)
t.collect(0.5)
check("quitting a held desktop ends the session",
      b"[desk ended, status 0]" in t.out and b"back 0" in t.out, t.out.decode(errors="replace"))
t.close()

report(86)
