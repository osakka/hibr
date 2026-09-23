#!/usr/bin/env python3
"""Drive the window manager through a pty and read the screen it draws.

examples/desktop.hibr is a hibr script, so none of this can be reached from
a .t file: it needs a terminal for the console to open and a mouse to click
with.  Run it directly:  python3 tests/desktop.py [path-to-hibr]

The pty and the terminal model live in tests/screen.py, which every
full-screen suite shares.
"""
import os, re, shutil, sys, tempfile

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
check("the menu bar shows the hibr menu, the time and the active application",
      sc.find("✎") == (0, 2) and re.search(r"\d\d:\d\d", sc.row(0)) and
      sc.find("Desktop ▾") is not None, sc)
check("the alternate screen is left on the way out", b"\x1b[?1049l" in raw)
check("and the mouse is turned off again",
      b"\x1b[?1002l" in raw or b"\x1b[?1000l" in raw)
check("the real cursor is never shown while the desktop runs, only on exit",
      raw.count(b"\x1b[?25h") == 1, raw)

# The default wallpaper (#16324a on #0d1b2a) darkened 55%: fg (22,50,74) ->
# (12,27,40), bg (13,27,42) -> (7,14,23) -- the shadow's own colour, wherever
# it peeks out from under the window it belongs to.
SHADOW_RGB = b"38;2;12;27;40;48;2;7;14;23"


def shadow_run(env=None, settle=0.6):
    path = "/tmp/hibr-desktop-shadow.hibr"
    open(path, "w").write("%s. %s\ndt_open\n%s\ndt_run\ndt_close\n"
                          % (load(MOD), WM, ONE))
    t = Term(path, env=dict({"DT_TICK": "60"}, **(env or {})), rows=ROWS,
             cols=COLS, settle=settle)
    t.quit(b"q", 1.0)
    os.unlink(path)
    return t.raw


raw_shadow = shadow_run()
check("a window casts a shadow on the wallpaper under it",
      SHADOW_RGB in raw_shadow, raw_shadow)
raw_noshadow = shadow_run(env={"DT_SHADOW": "0"})
check("DT_SHADOW=0 casts none", SHADOW_RGB not in raw_noshadow, raw_noshadow)
raw_long = shadow_run(settle=2.5)
check("more frames before quitting does not darken it further -- damage "
      "sends an unchanged cell once",
      raw_long.count(SHADOW_RGB) == raw_shadow.count(SHADOW_RGB), raw_long)

# A key sent right after a resize must not be lost while the debounce is
# waiting to see whether more of them are coming (DT_RSTILL is 150ms).
path = "/tmp/hibr-desktop-rsz.hibr"
open(path, "w").write("%s. %s\ndt_open\n%s\ndt_run\ndt_close\n"
                      % (load(MOD), WM, ONE))
t = Term(path, env={"DT_TICK": "60"}, rows=ROWS, cols=COLS, settle=0.6)
t.resize(ROWS, COLS + 1)
t.quit(b"q", 1.0)
os.unlink(path)
check("a key right after a resize is not dropped by the debounce",
      t.exited, t.raw)

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

# An app declared fixed has no maximise button at all -- not dimmed, not
# there -- so a game whose board is one size does not offer to stretch it.
FIXED = ('dt_app fx "Fixed" 6 20 once "◆" fixed\n'
         'fx_draw() { console put -p "w$1" 1 1 "hi"; }\n'
         'dt_launch fx\n')
sc, _ = run(FIXED)
check("a fixed app has no maximise button",
      sc.find("┤_ x├") is not None and sc.find("┤_ □ x├") is None, sc)
sc, _ = run(FIXED, [press(4, 21)])
check("clicking where it would be does not zoom",
      sc.find("┤ Fixed ├") is not None and sc.find("┤_ x├") is not None, sc)
sc, _ = run(FIXED, [b"\x1b[21~", b"\x1b[C", b"\x1b[C"])
check("Zoom is dimmed on the Window menu for it",
      sc.find("Zoom") is not None and sc.at(3, 22) != "z", sc)

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
      sc.find("Count") == (0, 5) and sc.find("More") is not None, sc)
check("and the application menu names it",
      sc.find("Noted ▾") is not None, sc)

sc, _ = run(MENUS, [press(0, 6)])
check("clicking a title drops the menu under it",
      sc.find("Bump") == (1, 6) and sc.find("Reset") == (3, 6), sc)
check("a separator is drawn between the groups", sc.at(2, 5) == "─", sc)
check("each item shows the letter that picks it", sc.at(1, 16) == "b" and
      sc.at(3, 16) == "r", sc)

sc, _ = run(MENUS, [press(0, 6), press(0, 6)])
check("clicking it again puts it away", sc.find("Bump") is None, sc)

sc, _ = run(MENUS, [press(0, 6), press(15, 60)])
check("clicking away from an open menu shuts it",
      sc.find("Bump") is None, sc)

sc, _ = run(MENUS, [press(0, 45)])
check("a click on empty bar space, nothing open, opens nothing",
      sc.find("Bump") is None and sc.find("Move") is None and
      sc.at(1, 45) == "·" and sc.row(0).startswith("  ✎  Count  More"), sc)

sc, _ = run(MENUS, [press(0, 6), b"b"])
check("a letter picks the item beside it", sc.find("count 1") is not None
      and sc.find("Bump") is None, sc)

sc, _ = run(MENUS, [press(0, 6), b"\r"])
check("enter takes the highlighted one", sc.find("count 1") is not None, sc)

sc, _ = run(MENUS, [press(0, 6), b"\x1b[B", b"\r"])
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
#   "  ✎  Count  More  Edit  Window"      "  ✎  Edit  Window"
WIN, WIN0 = 23, 11

sc, _ = run(MENUS, [press(0, WIN)])
check("a window menu is there even for an app with its own menus",
      sc.find("Move") is not None and sc.find("Cycle") is not None, sc)

sc, _ = run(MENUS, [press(6, 37), press(0, WIN0)])
check("with nothing focused its items lose their letters",
      sc.find("Move") is not None and sc.at(1, 16) != "m", sc)

sc, _ = run(MENUS, [press(6, 37), press(0, WIN0), b"m"])
check("and a dimmed letter does nothing", sc.find("Move") is not None, sc)

# --- submenus -------------------------------------------------------------

sc, _ = run(MENUS, [press(0, 12)])
check("an item with a submenu shows an arrow, not a letter",
      sc.find("Set To") is not None and sc.at(2, 27) == "▸", sc)

sc, _ = run(MENUS, [press(0, 12), b"\x1b[B", b"\x1b[C"])
check("right opens it beside its parent",
      sc.find("One") is not None and sc.find("Two") is not None and
      sc.find("Bump Twice") is not None, sc)
check("and the current choice carries a tick",
      sc.find("✓ Two") is not None and sc.find("✓ One") is None, sc)

sc, _ = run(MENUS, [press(0, 12), b"\x1b[B", b"\x1b[C", b"\x1b[D"])
check("left comes back out, leaving the parent open",
      sc.find("One") is None and sc.find("Bump Twice") is not None, sc)

sc, _ = run(MENUS, [press(0, 12), b"\x1b[B", b"\x1b[C", b"o"])
check("an item inside a submenu runs",
      sc.find("count 1") is not None and sc.find("One") is None, sc)

sc, _ = run(MENUS, [press(0, 12), b"\x1b[B", b"\x1b[B", b"\r"])
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

# --- icons on the desktop -------------------------------------------------
#
# A clean desktop: Home, the disks (faked here through DT_MOUNTS, so the
# suite does not report on whatever is really mounted where it runs), and
# the trash.  One column at column 67: Home at row 2, the first disk at 5,
# the second at 8, the trash at 11 -- or at 5, with disks off.

APPS = 'DT_APPDIRS+=("%s")\ndt_apps\n' % tree("examples/apps")

import base64


def copied(raw):
    i = raw.rfind(b"\x1b]52;c;")
    if i < 0:
        return ""
    return base64.b64decode(raw[i + 7:raw.index(b"\x07", i)]).decode()


def desk():
    d = tempfile.mkdtemp(prefix="hibr-desk-")
    home, backup, usb, src = (os.path.join(d, n)
                              for n in ("h", "backup", "usb", "src"))
    for sub in (home, backup, usb, src, os.path.join(d, "conf"),
                os.path.join(d, "trash")):
        os.makedirs(sub)
    open(os.path.join(src, "moveme.txt"), "w").write("x\n")
    mounts = os.path.join(d, "mounts")
    open(mounts, "w").write("/dev/sda1 %s ext4 rw 0 0\n"
                            "/dev/sdb1 %s btrfs rw 0 0\n" % (backup, usb))
    env = {"HOME": home, "DT_MOUNTS": mounts,
           "XDG_CONFIG_HOME": os.path.join(d, "conf")}
    pre = APPS + "DT_TRASH=%s\n" % os.path.join(d, "trash")
    return d, env, pre, home, backup, usb, src


d, env, pre, home, backup, usb, src = desk()
sc, raw = run("", env=env, pre=pre)
check("home, the disks and the trash are the desktop's icons",
      sc.find("Home") == (3, 71) and sc.find("backup") == (6, 70) and
      sc.find("usb") == (9, 71) and sc.find("Trash") == (12, 70), sc)

sc, raw = run("", env=env, pre=pre + "DT_DISKS=0\n")
check("and disks off leaves just home and the trash, moved up to meet it",
      sc.find("Home") == (3, 71) and sc.find("Trash") == (6, 70) and
      sc.find("backup") is None, sc)

sc, raw = run("", feed=[press(3, 70), press(3, 70)], env=env, pre=pre)
check("a double click on home opens it in Files", sc.find(home) is not None,
      sc)
sc, raw = run("", feed=[press(6, 70), press(6, 70)], env=env, pre=pre)
check("and on a disk opens it there", sc.find(backup) is not None, sc)

sc, raw = run('dt_new "Files" 10 34 2 2 files', env=env,
              pre=pre + "FB_DIR=%s\n" % src,
              feed=[press(5, 5), drag(5, 9), drag(12, 70), release(12, 70)])
check("a file dragged from a window onto the trash icon is thrown away",
      os.path.exists(os.path.join(d, "trash", "files", "moveme.txt")) and
      sc.find("Moved moveme.txt to the trash") is not None, sc)
shutil.rmtree(d, True)

d, env, pre, home, backup, usb, src = desk()
sc, raw = run("", feed=[b"\x1b[3~"], env=env, pre=pre)
check("delete on home selected does nothing -- it is not a file to lose",
      os.path.isdir(home) and sc.find("Home") is not None, sc)
sc, raw = run("", feed=[press(3, 70), drag(3, 74), drag(12, 70),
                        release(12, 70)], env=env, pre=pre)
check("nor does dragging its icon onto the trash",
      os.path.isdir(home) and sc.find("Home") is not None, sc)
shutil.rmtree(d, True)

d, env, pre, home, backup, usb, src = desk()
sc, raw = run("", feed=[press(3, 70), drag(3, 74), drag(15, 20),
                        release(15, 20)], env=env, pre=pre)
conf = open(os.path.join(d, "conf", "hibr", "desktop.hibr")).read()
# It keeps where it was held: grabbed at (3, 70) and let go at (15, 20), it
# has moved twelve rows down and fifty columns left.
check("an icon dragged somewhere empty stays where it was put",
      sc.find("Home") == (15, 21) and "dt_iconpos home" in conf, sc)
sc, raw = run("", env=env, pre=pre)
check("and is there again at the next start", sc.find("Home") == (15, 21),
      sc)
shutil.rmtree(d, True)

# Choosing more than one, over the two disks: a band from (4, 60) to
# (10, 79) touches both and neither Home nor the trash.  With no window
# focused, alt-c copies the paths of what is selected, which says exactly
# what that is.

d, env, pre, home, backup, usb, src = desk()
BAND = [press(4, 60), drag(7, 65), drag(10, 79), release(10, 79)]
sc, raw = run("", feed=BAND + [b"\x1bc"], env=env, pre=pre)
check("a band drawn across the desktop selects what it touches",
      copied(raw) == backup + "\n" + usb, sc)
sc, raw = run("", feed=[press(6, 70), press(9, 70, 16), b"\x1bc"], env=env,
              pre=pre)
check("and ctrl with a click adds an icon", copied(raw) == backup + "\n" + usb,
      sc)

sc, raw = run("", feed=BAND + [press(6, 70), drag(6, 74), drag(15, 20),
                               release(15, 20)], env=env, pre=pre)
check("a selection dragged somewhere empty keeps its arrangement",
      sc.find("backup") == (15, 20) and sc.find("usb") == (18, 21), sc)
shutil.rmtree(d, True)

# A saved position is a preference, not a promise: dt_iconlay clamps it to
# whatever screen it is laid out on, every time, not just when it is first
# dropped -- or an icon dragged out to the edge of a wide screen is off the
# side, or under the bar, once the terminal narrows.  Home is dragged to a
# row and column no other icon defaults to, at column 68, the furthest
# right an icon fits on an 80-column screen; row and column are given
# directly, since drag() encodes a fixed offset from press() rather than an
# absolute position, and this is not a click near the icon's own start.
d, env, pre, home, backup, usb, src = desk()
path = "/tmp/hibr-resize-icon.hibr"
open(path, "w").write("%s. %s\n%sdt_open\ndt_run\ndt_close\n"
                      % (load(MOD), WM, pre))
t = Term(path, env=dict({"DT_TICK": "60"}, **env), rows=ROWS, cols=COLS,
         settle=0.6)
t.keys([press(3, 70), drag(10, 74), drag(14, 68), release(14, 68)])
sc = t.screen()
check("an icon dragged to the edge of a wide screen is visible there",
      sc.find("Home") == (14, 69), sc)
t.resize(ROWS, 40)
t.send(b"", settle=0.6)
sc = t.screen()
check("and stays visible once the terminal narrows under it",
      sc.find("Home") is not None and sc.find("Home")[1] < 40, sc)
t.quit(b"q", 1.0)
t.close()
os.unlink(path)
shutil.rmtree(d, True)

d, env, pre, home, backup, usb, src = desk()
sc, raw = run("", feed=[press(3, 70), drag(10, 74), drag(14, 68),
                        release(14, 68), b"\x1b[21~", b"u"], env=env,
              pre=pre)
check("Clean Up Icons on the hibr menu puts them back at their defaults",
      sc.find("Home") == (3, 71), sc)
shutil.rmtree(d, True)

# The first arrow press with nothing yet selected only picks Home, where the
# cursor already conceptually was; it is the second press that actually
# moves, from Home to the first disk.
d, env, pre, home, backup, usb, src = desk()
sc, raw = run("", feed=[b"\x1b[B", b"\x1b[B", b"\r"], env=env, pre=pre)
check("with no window focused the arrows go from icon to icon, enter opens",
      sc.find(backup) is not None, sc)

# Home and a disk are both place icons, so neither is ever a file delete can
# lose; shift extends the selection across them regardless, provable by
# copying rather than deleting.
sc, raw = run("", feed=[b"\x1b[B", b"\x1b[1;2B", b"\x1bc"], env=env, pre=pre)
check("shift with them extends the selection across kinds too",
      copied(raw) == home + "\n" + backup, sc)
sc, raw = run("", feed=[b"\x1b[B", b"\x1b[1;2B", b"\x1b[3~"], env=env,
              pre=pre)
check("and delete leaves both alone -- neither is a file to lose",
      os.path.isdir(home) and os.path.isdir(backup) and
      sc.find("Home") is not None, sc)
shutil.rmtree(d, True)

# The same for a drag: a selection of place icons dropped on the trash is
# refused whole, not just its first, unsafe icon.
d, env, pre, home, backup, usb, src = desk()
BAND = [press(4, 60), drag(7, 65), drag(10, 79), release(10, 79)]
sc, raw = run("", feed=BAND + [press(6, 70), drag(6, 74), drag(11, 68),
                               release(11, 68)], env=env, pre=pre)
check("dragging a selection of them onto the trash loses neither",
      os.path.isdir(backup) and os.path.isdir(usb) and
      sc.find("backup") is not None and sc.find("usb") is not None, sc)
shutil.rmtree(d, True)

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
# The menu is on the left; the icons on the right carry the same names.
menu = [sc.row(r)[:20] for r in range(2, 16)]
rows = [m for m in menu if "Hello" in m or "Files" in m or "Mines" in m]
check("an app in your own folder is on the menu, in its sorted place",
      len(rows) == 3 and "Files" in rows[0] and "Hello" in rows[1] and
      "Mines" in rows[2], sc)
check("and a file of yours replaces the bundled app of that name",
      sc.find("My Sums") is not None and sc.find("Calculator") is None, sc)
shutil.rmtree(UCONF, True)

NCONF = tempfile.mkdtemp(prefix="hibr-apps-nested-")
os.makedirs(os.path.join(NCONF, "hibr", "apps", "sub"))
open(os.path.join(NCONF, "hibr", "apps", "sub", "greet.hibr"), "w").write(
    'dt_app greet "Greetings" 6 20 once "☺"\n')
sc, _ = run("", feed=[b"\x1b[21~"], env={"XDG_CONFIG_HOME": NCONF},
            pre="dt_apps\n")
check("an app in a subfolder of your own is found too",
      sc.find("Greetings") is not None, sc)
shutil.rmtree(NCONF, True)

LAUNCH = MENU + [b"c"]
sc, raw = run("", feed=LAUNCH + LAUNCH, pre=APPS)
check("an app declared once opens one window, however often launched",
      sc.text().count("┤ Calculator ├") == 1, sc)
LAUNCH = MENU + [b"f"]
sc, raw = run("", feed=LAUNCH + LAUNCH, pre=APPS)
check("and one that is not opens another window each time",
      sc.text().count("┤ Files ├") == 2, sc)

sc, _ = run("", feed=[press(0, 2), b"a"], pre=APPS)
check("About hibr opens a window with the machine's own numbers",
      sc.find("┤ About hibr ├") is not None and
      sc.find("CPU") is not None and sc.find("MEM") is not None and
      sc.find("%") is not None, sc)
check("and it has no maximise button, being a fixed size",
      sc.find("┤_ x├") is not None, sc)
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
      sc.find("✎") == (0, 2), sc)
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

# --- the shipped session holds itself, and --resume comes back to it -----
#
# examples/desktop-session.hibr calls dt_autohold on its own, so running it
# plainly makes it detachable without anyone asking hold for that by hand;
# running it again without --resume must not start a second, independent
# one under the same name, and --resume is how you get back to it.

RESUME = tempfile.mkdtemp(prefix="hibr-resume-")
RENV = {"HOME": RESUME, "TMPDIR": RESUME,
        "XDG_CONFIG_HOME": os.path.join(RESUME, "config"),
        "XDG_STATE_HOME": os.path.join(RESUME, "state"),
        "XDG_DATA_HOME": os.path.join(RESUME, "data")}
SESSION = tree("examples/desktop-session.hibr")


def unresume():
    import subprocess
    for n in ("desktop", "work", "personal"):
        subprocess.run([screen.HIBR, "-c", HOLDC + "hold kill %s" % n],
                       env=dict(os.environ, **RENV), capture_output=True)
    shutil.rmtree(RESUME, True)


atexit.register(unresume)

t = Term(SESSION, env=RENV, settle=2.0)
sc = t.screen()
check("run plainly, the shipped session is already detachable",
      sc.find("Home") is not None and sc.find("Trash") is not None, sc)
# A window open when it detaches is what proves --resume brings back more
# than a bare desktop: F10 then the app's own letter opens it, the same way
# a person would from the hibr menu.
t.send(b"\x1b[21~", settle=0.3)
t.send(b"f", settle=0.6)
t.send(b"\x1c", settle=0.6)
check("and ctrl-\\ detaches it", b"[desktop: detached" in t.out,
      t.out.decode(errors="replace"))
t.close()

t = Term(SESSION, env=RENV, settle=1.5)
out = t.out.decode(errors="replace")
check("run again without --resume, it refuses rather than starting a second",
      "desktop is already running" in out and "--resume" in out, out)
t.close()

t = Term(SESSION, "--resume", env=RENV, settle=2.0)
sc = t.screen()
check("--resume comes back to the same desktop, windows and all",
      sc.find("┤ Files ├") is not None, sc)
t.send(b"q", settle=1.0)
t.collect(0.5)
check("and quitting it from there ends the whole session",
      b"[desktop ended, status 0]" in t.out, t.out.decode(errors="replace"))
t.close()
unresume()

# --session names which one, for more than one side by side.  A directory
# of its own, not RESUME -- unresume() has already removed that one, TMPDIR
# included, and hold's own directory needs TMPDIR to still exist.

import subprocess

SESS2 = tempfile.mkdtemp(prefix="hibr-session-")
S2ENV = {"HOME": SESS2, "TMPDIR": SESS2,
         "XDG_CONFIG_HOME": os.path.join(SESS2, "config"),
         "XDG_STATE_HOME": os.path.join(SESS2, "state"),
         "XDG_DATA_HOME": os.path.join(SESS2, "data")}


def unsession():
    for n in ("work", "personal"):
        subprocess.run([screen.HIBR, "-c", HOLDC + "hold kill %s" % n],
                       env=dict(os.environ, **S2ENV), capture_output=True)
    shutil.rmtree(SESS2, True)


atexit.register(unsession)

t = Term(SESSION, "--session", "work", env=S2ENV, settle=2.0)
t.send(b"\x1b[21~", settle=0.3)
t.send(b"f", settle=0.6)
t.send(b"\x1c", settle=0.5)
t.close()
t = Term(SESSION, "--session", "personal", env=S2ENV, settle=2.0)
t.send(b"\x1c", settle=0.5)
t.close()
r = subprocess.run([screen.HIBR, "-c", HOLDC + "hold list"],
                   env=dict(os.environ, **S2ENV), capture_output=True,
                   text=True)
running = set(l.split()[0] for l in r.stdout.splitlines())
check("--session starts an independently named desktop, more than one at once",
      running == {"work", "personal"}, r.stdout)

t = Term(SESSION, "--session", "work", "--resume", env=S2ENV, settle=2.0)
sc = t.screen()
check("--session with --resume comes back to that one specifically",
      sc.find("┤ Files ├") is not None, sc)
t.send(b"q", settle=1.0)
t.close()
r = subprocess.run([screen.HIBR, "-c", HOLDC + "hold list"],
                   env=dict(os.environ, **S2ENV), capture_output=True,
                   text=True)
check("ending it leaves the other one alone",
      "personal" in r.stdout and "work" not in r.stdout, r.stdout)
unsession()

report(119)
