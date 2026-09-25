#!/usr/bin/env python3
"""Drive the window manager through a pty and read the screen it draws.

examples/desktop/desktop.hibr is a hibr script, so none of this can be reached from
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
WM = tree("examples/desktop/desktop.hibr")
ROWS, COLS = 24, 80


def run(session, feed=(), wait=1.2, env=None, pre=""):
    """Run a session on top of the window manager and return the last screen."""
    path = "/tmp/hibr-desktop-%d.hibr" % os.getpid()
    open(path, "w").write("%s. %s\n%s\ndt_open\n%s\ndt_run\ndt_close\n"
                          % (load(MOD), WM, pre, session))
    t = Term(path, env=dict({"DT_TICK": "60"}, **(env or {})), rows=ROWS,
             cols=COLS, settle=0.5)
    t.keys(feed)
    t.quit(b"qy", wait)
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
    t.quit(b"qy", 1.0)
    os.unlink(path)
    return t.raw


raw_shadow = shadow_run()
check("a window casts a shadow on the wallpaper under it",
      SHADOW_RGB in raw_shadow, raw_shadow)
# DT_BARSHADOW defaults on and would otherwise darken the same wallpaper
# colours at row 1, the same exact bytes this checks for -- off here so a
# second, unrelated shadow source cannot make "none" look like "some".
raw_noshadow = shadow_run(env={"DT_SHADOW": "0", "DT_BARSHADOW": "0"})
check("DT_SHADOW=0 casts none", SHADOW_RGB not in raw_noshadow, raw_noshadow)
raw_long = shadow_run(settle=2.5)
check("more frames before quitting does not darken it further -- damage "
      "sends an unchanged cell once",
      raw_long.count(SHADOW_RGB) == raw_shadow.count(SHADOW_RGB), raw_long)


def mshadow_run(env=None):
    path = "/tmp/hibr-desktop-mshadow.hibr"
    open(path, "w").write("%s. %s\ndt_open\n\ndt_run\ndt_close\n"
                          % (load(MOD), WM))
    t = Term(path, env=dict({"DT_TICK": "60"}, **(env or {})), rows=ROWS,
             cols=COLS, settle=0.6)
    t.send(b"\x1b[21~", settle=0.4)
    t.quit(b"qy", 1.0)
    os.unlink(path)
    return t.raw


raw_mshadow = mshadow_run()
check("an open menu casts a shadow too", SHADOW_RGB in raw_mshadow, raw_mshadow)
raw_nomshadow = mshadow_run(env={"DT_MSHADOW": "0", "DT_BARSHADOW": "0"})
check("DT_MSHADOW=0 casts none, independently of DT_SHADOW",
      SHADOW_RGB not in raw_nomshadow, raw_nomshadow)


def barshadow_run(env=None):
    path = "/tmp/hibr-desktop-barshadow.hibr"
    open(path, "w").write("%s. %s\ndt_open\ndt_run\ndt_close\n" % (load(MOD), WM))
    t = Term(path, env=dict({"DT_TICK": "60"}, **(env or {})), rows=ROWS,
             cols=COLS, settle=0.6)
    t.quit(b"qy", 1.0)
    os.unlink(path)
    return t.raw


raw_barshadow = barshadow_run()
check("the menu bar casts a shadow too, with nothing else open at all",
      SHADOW_RGB in raw_barshadow, raw_barshadow)
raw_nobarshadow = barshadow_run(env={"DT_BARSHADOW": "0"})
check("DT_BARSHADOW=0 casts none, independently of the other two",
      SHADOW_RGB not in raw_nobarshadow, raw_nobarshadow)

# A key sent right after a resize must not be lost while the debounce is
# waiting to see whether more of them are coming (DT_RSTILL is 150ms).
path = "/tmp/hibr-desktop-rsz.hibr"
open(path, "w").write("%s. %s\ndt_open\n%s\ndt_run\ndt_close\n"
                      % (load(MOD), WM, ONE))
t = Term(path, env={"DT_TICK": "60"}, rows=ROWS, cols=COLS, settle=0.6)
t.resize(ROWS, COLS + 1)
t.send(b"\x1b[21~", settle=0.5)
sc = t.screen()
check("a key right after a resize is not dropped by the debounce",
      sc.find("About hibr") is not None, sc)
t.quit(b"qy", 1.0)
os.unlink(path)

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


# Two presses close enough together to count as one double click -- run()'s
# own default settle/collect between keys (0.25s + 0.2s) is longer than
# DT_DBLMS, so this needs its own tight timing rather than run()'s.
def dblclick_run(session, r, c, env=None):
    path = "/tmp/hibr-desktop-dblclick.hibr"
    open(path, "w").write("%s. %s\ndt_open\n%s\ndt_run\ndt_close\n"
                          % (load(MOD), WM, session))
    t = Term(path, env=dict({"DT_TICK": "60"}, **(env or {})), rows=ROWS,
             cols=COLS, settle=0.5)
    t.send(press(r, c), settle=0.1, collect=0.05)
    t.send(press(r, c), settle=0.3, collect=0.2)
    sc = t.screen()
    t.quit(b"qy", 1.0)
    os.unlink(path)
    return sc


sc = dblclick_run(ONE, 6, 20)
check("double-clicking the title bar zooms it too, by default",
      sc.g[1][0] == "┌" and sc.g[23][79] == "◢", sc)
sc = dblclick_run(ONE, 6, 20, env={"DT_DBLACTION": "none"})
check("DT_DBLACTION=none turns that off",
      sc.g[6][10] == "┌" and sc.g[1][0] != "┌", sc)

# An app declared fixed has no maximise button at all -- not dimmed, not
# there -- so a game whose board is one size does not offer to stretch it.
FIXED = ('dt_app fx "Fixed" 6 20 once "◆" fixed\n'
         'fx_draw() { console put -p "w$1" 1 1 "hi"; }\n'
         'dt_launch fx\n')
sc, _ = run(FIXED)
check("a fixed app has no maximise button",
      sc.find("┤_ x├") is not None and sc.find("┤_ □ x├") is None, sc)
check("and no grow box is drawn at its corner", sc.g[9][27] == "┘", sc)
sc, _ = run(FIXED, [press(4, 21)])
check("clicking where it would be does not zoom",
      sc.find("┤ Fixed ├") is not None and sc.find("┤_ x├") is not None, sc)
sc, _ = run(FIXED, [press(9, 27), drag(15, 40), release(15, 40)])
check("dragging its corner does not resize it",
      sc.g[9][27] == "┘" and sc.find("┤ Fixed ├") == (4, 10), sc)
sc, _ = run(FIXED, [b"\x1b[21~", b"\x1b[C", b"\x1b[C"])
check("Zoom is dimmed on the Window menu for it",
      sc.find("Zoom") is not None and sc.at(3, 22) != "z", sc)
check("and so is Resize",
      sc.find("Resize") is not None and sc.at(2, 22) != "r", sc)
sc = dblclick_run(FIXED, 4, 15)
check("a fixed window's double-click does not zoom it either",
      sc.g[9][27] == "┘", sc)

# The bar's app name follows focus even for an app with no menus of its own
# to merge in -- Clock and About are exactly this shape, and used to show
# Desktop while focused, since MB_APP was only set as a side effect of
# finding a _menus function to call.
NOMENU = 'dt_app nm "NoMenu" 6 20\nnm_draw() { :; }\ndt_launch nm\n'
sc, _ = run(NOMENU)
check("an app with no menus of its own still names itself in the bar",
      sc.find("NoMenu ▾") is not None, sc)

# --- widgets -----------------------------------------------------------
#
# dt_check and dt_wdrop are draw helpers plus a per-window hit registry
# (WG); an app's own _click asks dt_hit which one, if any, was clicked and
# updates its own state, the same contract as everywhere else in the
# desktop. dt_droplist opens a popup of choices anchored under a dropdown,
# reusing the same context-menu machinery a right-click uses.
WIDGETS = ('dt_app wg "Widgets" 10 30 once "▢"\n'
           'declare -A WGS\n'
           'wg_draw() {\n'
           '\tdt_wclear "$1"\n'
           '\tdt_check "$1" 1 1 "${WGS[$1]:-0}" "Beep" chk\n'
           '\tdt_wdrop "$1" 3 1 10 "pick" drp\n'
           '}\n'
           'wg_pick() { dt_note "picked $2"; }\n'
           'wg_click() {\n'
           '\tlocal id=$1 r=$2 c=$3 tag wc\n'
           '\ttag := dt_hit "$id" "$r" "$c"\n'
           '\tcase $tag in\n'
           '\tchk) WGS[$id]=$((1 - ${WGS[$id]:-0})) ;;\n'
           '\tdrp) wc := dt_wcol "$id" "$r" "$tag"\n'
           '\t     dt_droplist "$id" "$r" "$wc" wg_pick one two three ;;\n'
           '\tesac\n'
           '}\n'
           'dt_launch wg\n')
sc, _ = run(WIDGETS)
check("dt_check draws an unchecked box and its label",
      sc.find("[ ] Beep") == (5, 9), sc)
check("dt_wdrop draws a value with a caret", sc.find("pick     ▾") == (7, 9), sc)
sc, _ = run(WIDGETS, [press(5, 9)])
check("clicking the checkbox's own cell checks it, through dt_hit",
      sc.find("[x] Beep") == (5, 9), sc)
sc, _ = run(WIDGETS, [press(5, 20)])
check("but clicking past its region does nothing",
      sc.find("[ ] Beep") == (5, 9), sc)
sc, _ = run(WIDGETS, [press(7, 9)])
check("clicking a dropdown opens a popup of its choices beneath it",
      sc.find("one") == (8, 11) and sc.find("two") == (9, 11) and
      sc.find("three") == (10, 11), sc)
sc, _ = run(WIDGETS, [press(7, 17)])
check("and it aligns to the dropdown itself, not wherever inside it "
      "was clicked",
      sc.find("one") == (8, 11), sc)

# A note lasts only until the next key, and run()'s own qy teardown is a
# key -- checked before it, the same way the about-note test is.
path = "/tmp/hibr-desktop-widgets.hibr"
open(path, "w").write("%s. %s\n%s\ndt_open\n%s\ndt_run\ndt_close\n"
                      % (load(MOD), WM, "", WIDGETS))
t = Term(path, env={"DT_TICK": "60"}, rows=ROWS, cols=COLS, settle=0.6)
t.keys([press(7, 9), press(9, 10)])
sc = t.screen()
check("choosing one runs the callback with the id and the value",
      sc.find("picked two") is not None and sc.find("three") is None, sc)
t.quit(b"qy", 1.2)
os.unlink(path)

# --- right-click context menus ---------------------------------------------
#
# ctx has both _click and _context: right-click must reach _context, not
# _click, and choosing an item there must not also count as a click. plain
# has _click alone, to prove an app without _context still gets its right
# clicks the way mines always has.

CTX = ('declare -gA CTX_N\n'
       'ctx_draw() { console put -p "w$1" 1 2 "hits=${CTX_N[$1]:-0}"; }\n'
       'ctx_click() { CTX_N[$1]=$((${CTX_N[$1]:-0} + 1)); }\n'
       'ctx_context() { dt_menu "Act"; dt_item "Bump" b ctx_bump "$1"; }\n'
       'ctx_bump() { CTX_N[$1]=$((${CTX_N[$1]:-0} + 10)); }\n'
       'dt_new "Ctx" 8 30 6 10 ctx\n')

sc, _ = run(CTX, [press(9, 15, 2)])
check("a right-click on an app with _context opens it there, not at the bar",
      sc.find("Bump") == (9, 17) and sc.row(0).find("Act") == -1, sc)
check("and does not also count as a click",
      sc.find("hits=0") is not None, sc)

sc, _ = run(CTX, [press(9, 15, 2), press(9, 17)])
check("choosing its item runs the item's own command",
      sc.find("hits=10") is not None, sc)

sc, _ = run(CTX, [press(9, 15, 2), press(0, 40)])
check("clicking away from it closes it, same as any other menu",
      sc.find("Bump") is None, sc)

PLAIN = ('plain_draw() { console put -p "w$1" 1 2 "hits=${CTX_N[$1]}"; }\n'
         'plain_click() { CTX_N[$1]=$((${CTX_N[$1]:-0} + 1)); }\n'
         'dt_new "Plain" 8 30 6 10 plain\n')
sc, _ = run(PLAIN, [press(9, 15, 2)])
check("without _context, a right-click still reaches _click as it always did",
      sc.find("Bump") is None and sc.find("hits=1") is not None, sc)

sc, _ = run(ONE, [press(15, 40, 2)])
check("a right-click on the bare desktop opens its own menu at the pointer",
      sc.find("Arrange Icons") == (15, 42) and
      sc.find("Change Wallpaper") is not None, sc)

sc, _ = run(ONE, [press(6, 20, 2)])
check("a right-click on a title bar opens the Window menu's own items there",
      sc.find("Move") == (6, 22) and sc.find("Resize") is not None and
      sc.find("Close") is not None, sc)
sc, _ = run(ONE, [press(6, 20, 2), press(6, 22)])
check("choosing Move from it starts moving, the same as from the menu bar",
      sc.find("moving") is not None, sc)

sc, _ = run(ONE, [press(0, 40, 2)])
check("a right-click on empty menu-bar space offers the quick launchers",
      sc.find("New Terminal") is not None and
      sc.find("Task Manager") is not None, sc)
check("but not Control Panel, reachable from the hibr menu instead",
      sc.find("Control Panel") is None, sc)

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
check("and a blank row closes it, not a line -- Close is the last item",
      sc.find("Close") == (4, 6) and sc.at(5, 6) == " ", sc)
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

# A note lasts only until the next key, and the quit sequence's own q is a
# key -- checked before it, rather than through run()'s own qy teardown.
path = "/tmp/hibr-desktop-about.hibr"
open(path, "w").write("%s. %s\n%s\ndt_open\n%s\ndt_run\ndt_close\n"
                      % (load(MOD), WM, "", MENUS))
t = Term(path, env={"DT_TICK": "60"}, rows=ROWS, cols=COLS, settle=0.6)
t.keys([press(0, 2), b"a"])
sc = t.screen()
check("about says what this is",
      sc.find("a desktop written in the shell") is not None, sc)
t.quit(b"qy", 1.2)
os.unlink(path)

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

APPS = 'DT_APPDIRS+=("%s")\ndt_apps\n' % tree("examples/desktop/apps")

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
      os.path.exists(os.path.join(d, "trash", "files", "moveme.txt")), sc)
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
t.quit(b"qy", 1.0)
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

# Quit asks first: q used to end it on the spot, and one key too many
# landed there more than once.
path = "/tmp/hibr-desktop-quit.hibr"
open(path, "w").write("%s. %s\ndt_open\n%s\ndt_run\ndt_close\n"
                      % (load(MOD), WM, ONE))
t = Term(path, env={"DT_TICK": "60"}, rows=ROWS, cols=COLS, settle=0.6)
t.send(b"q", settle=0.4)
sc = t.screen()
check("q asks before quitting, rather than quitting on the spot",
      sc.find("Quit hibr?") is not None and not t.exited, sc)
t.send(b"n", settle=0.4)
sc = t.screen()
check("n cancels it, and the desktop is still there",
      sc.find("Quit hibr?") is None and
      sc.find("┤ Hello ├") is not None and not t.exited, sc)
t.quit(b"qy", 1.0)
check("and q then y still quits", t.exited, t.raw)
os.unlink(path)

# [y]es and [n]o on the confirm box are clickable, not just typeable.
path = "/tmp/hibr-desktop-confirmclick.hibr"
open(path, "w").write("%s. %s\ndt_open\n%s\ndt_run\ndt_close\n"
                      % (load(MOD), WM, ONE))
t = Term(path, env={"DT_TICK": "60"}, rows=ROWS, cols=COLS, settle=0.5)
t.send(b"q", settle=0.4)
sc = t.screen()
no_pos = sc.find("[n]o")
t.send(press(*no_pos), settle=0.4)
sc = t.screen()
check("clicking [n]o cancels the confirm box",
      sc.find("Quit hibr?") is None and not t.exited, sc)
t.send(b"q", settle=0.4)
sc = t.screen()
yes_pos = sc.find("[y]es")
t.send(press(*yes_pos), settle=0.4)
t.quit(None, 1.0)
check("and clicking [y]es quits", t.exited, t.raw)
os.unlink(path)

import tempfile

# --- the hibr menu comes from folders of apps ---------------------------

UCONF = tempfile.mkdtemp(prefix="hibr-apps-")
os.makedirs(os.path.join(UCONF, "hibr", "apps"))
open(os.path.join(UCONF, "hibr", "apps", "hello.hibr"), "w").write(
    'dt_app hello "Hello" 6 20 once "☺"\n'
    'hello_draw() { console put -p "w$1" 1 1 "hi there"; }\n')
open(os.path.join(UCONF, "hibr", "apps", "calc.hibr"), "w").write(
    'dt_app calc "My Sums" 6 20 once "±"\n')
APPS = 'DT_APPDIRS+=("%s")\ndt_apps\n' % tree("examples/desktop/apps")
# Calculator is a desk accessory now, found by da_apps rather than dt_apps
# -- but DT_SRC is one shared registry either way, so a user's own file,
# loaded first by dt_apps from the default DT_APPDIRS entry, still blocks
# the bundled one da_apps would otherwise find, the same as it always did.
DAAPPS = 'DA_DIRS+=("%s")\nda_apps\n' % tree("examples/desktop/desk-accessories")
MENU = [b"\x1b[21~", b"\x1b[B"]
sc, raw = run("", feed=MENU, env={"XDG_CONFIG_HOME": UCONF},
              pre=APPS + DAAPPS)
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
check("an app in a subfolder becomes a submenu named after the folder",
      sc.find("sub") is not None and sc.find("Greetings") is None, sc)
sc, _ = run("", feed=[b"\x1b[21~", b"\x1b[B", b"\x1b[C"],
            env={"XDG_CONFIG_HOME": NCONF}, pre="dt_apps\n")
check("and descending into it finds the app",
      sc.find("Greetings") is not None, sc)
shutil.rmtree(NCONF, True)

# A folder sorts by its own name among the flat apps, not after all of
# them -- "games" belongs between "Files" and "Mines", not at the end.
GCONF = tempfile.mkdtemp(prefix="hibr-apps-games-")
os.makedirs(os.path.join(GCONF, "hibr", "apps", "games"))
open(os.path.join(GCONF, "hibr", "apps", "games", "pong.hibr"), "w").write(
    'dt_app pong "Pong" 6 20\n')
sc, _ = run("", feed=[b"\x1b[21~"], env={"XDG_CONFIG_HOME": GCONF}, pre=APPS)
menu = [sc.row(r)[:20] for r in range(2, 17)]
rows = [m for m in menu if "Files" in m or "games" in m or "Mines" in m]
check("a folder is interleaved by name, not appended after every app",
      len(rows) == 3 and "Files" in rows[0] and "games" in rows[1] and
      "Mines" in rows[2], sc)
shutil.rmtree(GCONF, True)

# Desk accessories are ordinary apps, grouped under one submenu name
# regardless of where DA_DIRS points -- unlike the folder-submenu above,
# an accessory need not live inside DT_APPDIRS at all.
DACONF = tempfile.mkdtemp(prefix="hibr-apps-da-")
DASRC = APPS + 'DA_DIRS+=("%s")\nda_apps\n' % tree("examples/desktop/desk-accessories")
sc, _ = run("", feed=[b"\x1b[21~"], env={"XDG_CONFIG_HOME": DACONF}, pre=DASRC)
pos = sc.find("Desk Accessories")
check("desk accessories are grouped under their own submenu",
      pos is not None, sc)
sc2, _ = run("", feed=[b"\x1b[21~", press(pos[0], pos[1] + 2)],
             env={"XDG_CONFIG_HOME": DACONF}, pre=DASRC)
check("which lists Note Pad and Puzzle rather than folding them in flat",
      sc2.find("Note Pad") is not None and sc2.find("Puzzle") is not None,
      sc2)
shutil.rmtree(DACONF, True)

# --- the control strip ---------------------------------------------------
#
# One line of quick-toggle modules, each in its own brackets, drawn
# straight over everything else like the bar and the confirm box, not a
# DT[] window -- docked to a side and dragged up and down, hibr's own take
# rather than the bottom-only strip the real one was.

CSSRC = 'CS_MODDIRS+=("%s")\ncs_modules\n' % tree("examples/desktop/control-strip")


def csrun(feed=()):
    """Run the bare desktop with the strip loaded, its own config dir each
    time -- one check's collapse or drag must not be the next check's
    starting point, the same reason Control Panel's own tests each get one.
    """
    d = tempfile.mkdtemp(prefix="hibr-strip-")
    sc, _ = run("", feed=feed, env={"XDG_CONFIG_HOME": d}, pre=CSSRC)
    sc.conf = d
    return sc


def cssaved(sc):
    saved = os.path.join(sc.conf, "hibr", "desktop.hibr")
    return open(saved).read() if os.path.exists(saved) else ""


sc = csrun()
arrow = sc.find("▸")
check("the strip docks left by default, roughly 80% down the screen",
      arrow is not None and arrow[0] > ROWS * 3 // 4, sc)
row = arrow[0]

# A saved CS_Y is a preference, not a promise, the same as an icon's own
# saved spot -- dt_size clamps it on open (and on every resize) rather
# than baking the clamp into the saved value, so it is pulled back onto
# the screen here without losing what was actually saved.
sc2, _ = run("", env={"CS_Y": "500"}, pre=CSSRC)
arrow2 = sc2.find("▸")
check("a saved position past the edge of the screen is pulled back onto it",
      arrow2 is not None and arrow2[0] == ROWS - 1, sc2)
# cursor, shadow, theme, wallpaper: alphabetical file order, its own full
# name in each bracket rather than a single glyph nobody could read
# without already knowing what it meant -- verified as one exact string
# before any position derived from it is trusted for a click.
LABELS = "[Cursor][Shadow][Theme][Wallpaper]▸"
check("its four modules draw as one bracketed row, in file order, named",
      sc.row(row)[0:len(LABELS)] == LABELS, sc)
SHADOW_COL = sc.find("Shadow")[1]
CURSOR_COL = sc.find("Cursor")[1]
ARROW_COL = arrow[1]
shutil.rmtree(sc.conf, True)

sc = csrun([press(row, SHADOW_COL), release(row, SHADOW_COL)])
# Shadow is a plain toggle, drawn bold in the active colour when on and
# dimmed when off -- its own colour changes, not its text, and the screen
# model tracks characters, not colour, so this checks the saved setting.
check("a click toggles Shadow", "DT_SHADOW=0" in cssaved(sc), sc)
shutil.rmtree(sc.conf, True)

sc = csrun([press(row, CURSOR_COL), release(row, CURSOR_COL)])
check("clicking Cursor instead opens a dropdown of its three styles",
      sc.find("block") is not None and sc.find("underline") is not None and
      sc.find("bar") is not None, sc)
pick = sc.find("bar")
shutil.rmtree(sc.conf, True)

sc = csrun([press(row, CURSOR_COL), release(row, CURSOR_COL),
            press(*pick), release(*pick)])
check("picking a value from it sets and saves the value",
      "DT_CURSOR=bar" in cssaved(sc), sc)
shutil.rmtree(sc.conf, True)

sc = csrun([press(row, ARROW_COL), release(row, ARROW_COL)])
check("a click on the arrow collapses the strip to its own tab",
      sc.row(row).startswith("▸") and "[" not in sc.row(row), sc)
shutil.rmtree(sc.conf, True)

# The kind (move vs resize) latches on the first drag event once away from
# the press, off whichever of dr/dc is bigger -- a small vertical step
# first, same as a real drag's first few pixels, keeps it "move" before
# the big horizontal jump that actually reaches the other side; jumping
# straight there reads as a sideways resize instead, since dc dwarfs dr in
# one single (row, col) jump the way it never does pixel by pixel.
sc = csrun([press(row, ARROW_COL), drag(row - 1, ARROW_COL),
            drag(15, 70), release(15, 70)])
# Right-docked, the arrow leads instead of trailing, flush against the
# screen's own right edge.
TAIL = "◂[Cursor][Shadow][Theme][Wallpaper]"
check("dragging the arrow across the screen re-docks it to the other side",
      sc.row(15)[-len(TAIL):] == TAIL, sc)
saved = os.path.join(sc.conf, "hibr", "desktop.hibr")
text = open(saved).read() if os.path.exists(saved) else ""
check("the new side and position are saved",
      "CS_SIDE=right" in text and "CS_Y=15" in text, text)
shutil.rmtree(sc.conf, True)

# Dragging the arrow sideways instead of up/down resizes it -- how many
# modules fit between the docked edge and the pointer, recomputed live
# from where the pointer actually is, not accumulated one drag event at a
# time. Shrunk to one module, the other three are still there to scroll to.
sc = csrun([press(row, ARROW_COL), drag(row, 5), release(row, 5)])
check("dragging the arrow sideways instead resizes it",
      sc.row(row)[0:9] == "[Cursor]▸", sc)
shutil.rmtree(sc.conf, True)

SHRINK = [press(row, ARROW_COL), drag(row, 5), release(row, 5)]

sc = csrun(SHRINK + [wheel(row, 1, up=False)])
check("the wheel over the strip scrolls to the next module",
      sc.row(row)[0:9] == "[Shadow]▸", sc)
shutil.rmtree(sc.conf, True)

# `[ "$act" = wheelup ] && cs_scroll -1 || cs_scroll 1` looked like an
# if/else but is not one: cs_scroll's own clamping ends in a test that is
# often false, so the `-1` call's own exit status re-triggered the `|| cs_scroll
# 1` right after it, leaving a wheel-up stuck whenever it actually had
# somewhere to go. Scroll to the far end, then back past every module.
sc = csrun(SHRINK + [wheel(row, 1, up=False)] * 4 + [wheel(row, 1, up=True)])
check("the wheel scrolls back too, not just forward",
      sc.row(row)[0:8] == "[Theme]▸", sc)
shutil.rmtree(sc.conf, True)

sc = csrun(SHRINK + [wheel(row, 1, up=False)] * 4 + [wheel(row, 1, up=True)] * 3)
check("scrolling all the way back reaches the first module again",
      sc.row(row)[0:9] == "[Cursor]▸", sc)
shutil.rmtree(sc.conf, True)

# With no mouse at all (or simply preferred), its own shortcut gives it
# attention instead --
# right still scrolls it, escape or the same shortcut again releases it.
sc = csrun(SHRINK + [b"\x1bs", b"\x1b[C"])
check("the strip's own shortcut scrolls it with no mouse involved",
      sc.row(row)[0:9] == "[Shadow]▸", sc)
shutil.rmtree(sc.conf, True)

sc = csrun(SHRINK + [b"\x1bs", b"\x1b", b"\x1b[C"])
check("escape releases it, so the same arrow goes back to being unhandled",
      sc.row(row)[0:9] == "[Cursor]▸", sc)
shutil.rmtree(sc.conf, True)

# --- the wallpaper and the image viewer -----------------------------------
#
# img (mods/img) decodes a PNG and draws it as coloured half-blocks -- the
# desktop reads DT_WALLIMG the same way it already reads DT_GLYPH, and the
# desk accessory imgview is the only way to set one short of hand-editing
# the saved config. tests/img-2x2.png (red, green / blue, yellow, one pixel
# each) is the same fixture 830-img.t itself is recorded against.

IMGMOD = 'mod load %s\n' % tree("build/mods/img.so")
IMGFIX = tree("tests/img-2x2.png")
DASRC = 'DA_DIRS+=("%s")\nda_apps\n' % tree("examples/desktop/desk-accessories")

sc, raw = run("", env={"DT_WALLIMG": IMGFIX}, pre=IMGMOD)
check("a real image can be the desktop's own wallpaper",
      b"38;2;255;0;0" in raw and b"48;2;0;0;255" in raw, raw)

sc, raw = run("", env={"DT_WALLIMG": "/does/not/exist.png"}, pre=IMGMOD)
check("an unusable wallpaper image falls back to the glyph instead",
      sc.row(1)[0:1] == "·", sc)

sc, raw = run('dt_launch imgview "%s"' % IMGFIX, pre=IMGMOD + DASRC)
check("the image viewer opens with a picture and draws it",
      sc.find("Image Viewer") is not None and
      b"38;2;255;0;0" in raw and b"48;2;0;0;255" in raw, sc)

sc, raw = run("dt_launch imgview", pre=IMGMOD + DASRC)
check("opened with no picture, it says so instead of showing nothing",
      sc.find("Drop a picture here") is not None, sc)

WCONF = tempfile.mkdtemp(prefix="hibr-imgview-")
menurow = sc.find("Image")[0]
sc, raw = run('dt_launch imgview "%s"' % IMGFIX,
              feed=[press(menurow, 4), release(menurow, 4),
                    press(menurow + 1, 4), release(menurow + 1, 4)],
              env={"XDG_CONFIG_HOME": WCONF}, pre=IMGMOD + DASRC)
saved = os.path.join(WCONF, "hibr", "desktop.hibr")
text = open(saved).read() if os.path.exists(saved) else ""
check("Set as Wallpaper on its own Image menu sets and saves DT_WALLIMG",
      ("DT_WALLIMG=%s" % IMGFIX) in text, text)
shutil.rmtree(WCONF, True)

LAUNCH = MENU + [b"c"]
sc, raw = run("", feed=LAUNCH + LAUNCH, pre=APPS)
# 'c' launches Control Panel, not Clock -- Clock moved to Desk Accessories
# and is not registered at all under a bare APPS, so it no longer holds
# 'c' here (Control Panel does, being the sole 'c'-starting app left in
# examples/desktop/apps); this check was never about which app it launches, only
# that a `once` one opens no more than a single window.
check("an app declared once opens one window, however often launched",
      sc.text().count("┤ Control Panel ├") == 1, sc)
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

# Clock is a desk accessory now, not in examples/desktop/apps -- the bar's own
# click handler only asks dt_has clock_draw, so it works regardless of
# which loader found it, but the test has to load it from where it is.
sc, _ = run("", feed=[press(0, 63)],
            pre=APPS + 'DA_DIRS+=("%s")\nda_apps\n' % tree("examples/desktop/desk-accessories"))
check("clicking the clock in the bar opens the Clock app",
      sc.find("┤ Clock ├") is not None, sc)

# Control Panel is a pane picker, panes loaded from examples/desktop/control-panel
# and sorted by title without regard to case, the same trick dt_appnames
# uses for apps -- which puts App Shortcuts first, ahead of Appearance: a
# space sorts before a letter, plain byte order. tests/apps.py verifies
# this order directly against cp_panes; ORDER here just names it, so a
# real change to it breaks an assertion instead of a silent miscount.
PANEL = ('. %s/panel.hibr\nCP_PANEDIRS+=("%s")\ncp_panes'
         % (tree("examples/desktop/apps"), tree("examples/desktop/control-panel")))
ORDER = ["app_shortcuts", "appearance", "behaviour", "control_strip",
         "datetime", "shortcuts", "windows"]
DOWN_APP = [b"\x1b[B"] * ORDER.index("appearance")
DOWN_SHORT = [b"\x1b[B"] * ORDER.index("shortcuts")

CONF = tempfile.mkdtemp(prefix="hibr-conf-")
sc, raw = run('dt_new "Control Panel" 20 58 2 2 panel',
              feed=DOWN_APP + [b"\x1b[C", b"\x1b[C"],
              env={"XDG_CONFIG_HOME": CONF}, pre=PANEL)
saved = os.path.join(CONF, "hibr", "desktop.hibr")
text = open(saved).read() if os.path.exists(saved) else ""
check("a changed setting is written at once, as a script",
      "CP_THEME=slate" in text and "DT_WALL=\\#1a202c" in text and
      "DT_TICK=" in text, text or sc)
sc, raw = run('dt_new "Control Panel" 20 58 2 2 panel', feed=DOWN_APP,
              env={"XDG_CONFIG_HOME": CONF}, pre=PANEL)
check("and the next desktop starts with it", sc.find("slate") is not None,
      sc)
shutil.rmtree(CONF, True)

# ORDER.index("shortcuts") downs on the picker reaches Shortcuts; entering
# it lands on its first row, Close Window.
CONF2 = tempfile.mkdtemp(prefix="hibr-conf2-")
sc, raw = run('dt_new "Control Panel" 20 58 2 2 panel',
              feed=DOWN_SHORT + [b"\r", b"\r", b"x"],
              env={"XDG_CONFIG_HOME": CONF2}, pre=PANEL)
check("a shortcut row can be rebound to a new key",
      sc.find("alt-f4") is None, sc)
saved2 = os.path.join(CONF2, "hibr", "desktop.hibr")
text2 = open(saved2).read() if os.path.exists(saved2) else ""
check("and the new binding is saved", 'DT_KEYS["close"]=x' in text2, text2)
shutil.rmtree(CONF2, True)

# Any registered app gets its own row in App Shortcuts, not just Terminal
# and Task Manager -- empty by default, assignable the same way DT_KEYS'
# fixed four are. App Shortcuts sorts first of all the panes (see ORDER
# above), so it is the default pane -- no downs on the picker at all;
# entering it lands on Calculator, since it sorts before Control Panel.
CALCSRC = '. %s/calc.hibr' % tree("examples/desktop/desk-accessories")
CONF3 = tempfile.mkdtemp(prefix="hibr-conf3-")
sc, _ = run('dt_new "Control Panel" 20 58 2 2 panel', feed=[b"\r"],
            env={"XDG_CONFIG_HOME": CONF3}, pre=PANEL + "\n" + CALCSRC)
check("a registered app is listed with no shortcut by default",
      sc.find("Calculator") is not None, sc)
sc, _ = run('dt_new "Control Panel" 20 58 2 2 panel',
            feed=[b"\r", b"\r", b"g"],
            env={"XDG_CONFIG_HOME": CONF3}, pre=PANEL + "\n" + CALCSRC)
check("a shortcut can be assigned to any app, not only the two defaults",
      sc.find("Calculator") is not None and
      "g" in sc.row(sc.find("Calculator")[0]), sc)
saved3 = os.path.join(CONF3, "hibr", "desktop.hibr")
text3 = open(saved3).read() if os.path.exists(saved3) else ""
check("and it is saved as DT_APPKEY, not DT_KEYS",
      'DT_APPKEY["calc"]=g' in text3, text3)
sc, _ = run('', env={"XDG_CONFIG_HOME": CONF3}, pre=CALCSRC, feed=[b"g"])
check("and takes effect: g now opens the calculator",
      sc.find("Calculator") is not None, sc)
shutil.rmtree(CONF3, True)

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
t.send(b"qy", settle=1.0)
t.collect(0.5)
check("quitting a held desktop ends the session",
      b"[desk ended, status 0]" in t.out and b"back 0" in t.out, t.out.decode(errors="replace"))
t.close()

# --- the shipped session holds itself, and --resume comes back to it -----
#
# examples/desktop/session.hibr calls dt_autohold on its own, so running it
# plainly makes it detachable without anyone asking hold for that by hand;
# running it again without --resume must not start a second, independent
# one under the same name, and --resume is how you get back to it.

RESUME = tempfile.mkdtemp(prefix="hibr-resume-")
RENV = {"HOME": RESUME, "TMPDIR": RESUME,
        "XDG_CONFIG_HOME": os.path.join(RESUME, "config"),
        "XDG_STATE_HOME": os.path.join(RESUME, "state"),
        "XDG_DATA_HOME": os.path.join(RESUME, "data")}
SESSION = tree("examples/desktop/session.hibr")


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
t.send(b"qy", settle=1.0)
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
t.send(b"qy", settle=1.0)
t.close()
r = subprocess.run([screen.HIBR, "-c", HOLDC + "hold list"],
                   env=dict(os.environ, **S2ENV), capture_output=True,
                   text=True)
check("ending it leaves the other one alone",
      "personal" in r.stdout and "work" not in r.stdout, r.stdout)
unsession()

report(193)
