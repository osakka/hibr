#!/usr/bin/env python3
"""Drive the window manager through a pty and read the screen it draws.

examples/desktop/desktop.hibr is a hibr script, so none of this can be reached from
a .t file: it needs a terminal for the console to open and a mouse to click
with.  Run it directly:  python3 tests/desktop.py [path-to-hibr]

The pty and the terminal model live in tests/screen.py, which every
full-screen suite shares.
"""
import json, os, re, shutil, subprocess, sys, tempfile, time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import screen
from screen import (Term, check, report, press, release, drag, wheel, load,
                    tree, expect, scratch)

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

# alt-w, not ctrl-w: a terminal never yields ctrl with a letter, so the old
# default silently did nothing whenever one had focus (0.99.90).
sc, _ = run(ONE, [b"\x1bw"])
check("alt-w closes the focused window", sc.find("Hello") is None, sc)

# alt-q is Quit Application: every window of the focused app, not only the one
# in front, which is what Quit means where one app may have several. Each goes
# through dt_closereq, so an app that asks before closing still asks -- once
# per window.
QA = ('qa_open() { return 0; }\nqa_draw() { console put -p "w$1" 2 2 "qa"; return 0; }\n'
      'qb_open() { return 0; }\nqb_draw() { console put -p "w$1" 2 2 "qb"; return 0; }\n'
      'dt_app qa "Qa" 7 24 "" "" "" "" ""\ndt_app qb "Qb" 7 24 "" "" "" "" ""\n'
      'dt_new "Qa One" 7 24 2 4 qa\ndt_new "Qb Only" 7 24 2 40 qb\n'
      'dt_new "Qa Two" 7 24 12 4 qa\n')
sc, _ = run(QA, [press(12, 8), release(12, 8), 0.3, b"\x1bq"])
check("alt-q closes every window of the focused app",
      sc.find("┤ Qa One ├") is None and sc.find("┤ Qa Two ├") is None, sc)
check("and leaves another app's window alone",
      sc.find("┤ Qb Only ├") is not None, sc)

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

# The default wallpaper (#16324a on #0d1b2a) darkened 55%, rounded rather
# than truncated: fg (22,50,74) -> (12,28,41), bg (13,27,42) -> (7,15,23)
# -- the shadow's own colour, wherever it peeks out from under the window
# it belongs to.
SHADOW_RGB = b"38;2;12;28;41;48;2;7;15;23"


def shadow_run(env=None, settle=0.6):
    path = scratch("desktop-shadow.hibr")
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
    path = scratch("desktop-mshadow.hibr")
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
    path = scratch("desktop-barshadow.hibr")
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

# Drawing only what changed, and the proof that it changes nothing: inside
# one session, with the screen settled, turning DT_FORCEDRAW on redraws every
# window and repaints the wallpaper -- and not one cell may come out
# different, the pens as well as the glyphs, because a shadow darkened twice
# differs only in colour. Two windows that overlap, and a drag that moves one
# of them first, because a move is what leaves cells where a pane used to be
# and a shadow cast on a window is what gets darkened twice.
#
# One session, not two compared with each other: two pty sessions settle at
# their own pace, so the same correct desktop can end them on different
# frames, and an oracle that fails one run in three tells you nothing. Here
# both screens come from the same desktop, moments apart, and the only thing
# that changed between them is whether the skipping was allowed.
ORCFN = (
    'declare -gA OC OD\n'
    'orc_open() { OC[$1]=0; return 0; }\n'
    'orc_draw() { console put -p "w$1" 2 3 "n ${OC[$1]}"; return 0; }\n'
    # F turns the skipping off from inside the session, through the one
    # callback the window manager already hands keys to, so the comparison is
    # between two frames of one desktop rather than between two desktops.
    # F turns the skipping off; z is a key it draws nothing new for, so a
    # frame can be asked for without changing what is on the screen. Any key
    # at all costs a frame, which is the point of sending them.
    'orc_key()  { case $2 in F) DT_FORCEDRAW=1; return 0 ;; z) return 0 ;;\n'
    '                       N) dt_note "a note over the screen"; return 0 ;; esac\n'
    '             OC[$1]=$(( ${OC[$1]} + 1 )); OD[$1]=1; return 0; }\n'
    'orc_dirty() { [ -n "${OD[$1]}" ] && { OD[$1]=; return 0; }; return 1; }\n')
ORACLE = ORCFN + ('dt_new "Under" 9 34 6 10 orc\n'
                  'dt_new "Over" 9 34 10 22 orc\n')
# Settled means the desktop has stopped drawing, not that a moment has
# passed: t.keys waits for the idle marker of the bytes it sent, but a frame
# a *timer* asked for -- the wallpaper's once-a-second ceiling, the icons'
# five-second rescan -- can land after that marker and before the screenshot.
# Both snapshots have to be taken with nothing in flight, or the comparison
# is between two different moments in the desktop's life. That is what made
# this check fail about one run in three inside the suite and never once in
# eight runs on its own: under load the timer frame falls in a different
# place.
def orsettled(t, quiet=0.35, limit=4.0):
    was, spent = t.frames(), 0.0
    while spent < limit:
        t.collect(quiet)
        spent += quiet
        now = t.frames()
        if now == was:
            return t.screen()
        was = now
    return t.screen()


orpath = scratch("desktop-oracle.hibr")
open(orpath, "w").write("%s. %s\ndt_open\n%s\ndt_run\ndt_close\n"
                        % (load(MOD), WM, ORACLE))
# The clock is pinned here for the same reason it is in dragsteps: these two
# snapshots are two or three seconds apart, so a minute can turn between them
# and `15:36` meets `15:37` -- one glyph, no pen. That failed a release gate,
# and it reproduces on demand by starting the run two seconds before a minute
# boundary. A format with no field in it is the same text for ever.
ort = Term(orpath, env={"DT_TICK": "60", "DT_BARTIME": "hibr"}, rows=ROWS,
           cols=COLS, settle=0.6)
ort.keys([b"x", b"x", press(10, 30), 0.2, drag(12, 34), 0.2,
          drag(14, 38), 0.2, release(14, 38), 0.4, b"x", 0.5])
before = orsettled(ort)
beforeg = [r[:] for r in before.g]
beforep = [r[:] for r in before.p]
moved = before.find("┤ Over ├")
# Everything redrawn, twice over, so a single forced frame cannot be what
# makes them agree.
ort.keys([b"F", 0.4, b"z", 0.4, b"z", 0.5])
after = orsettled(ort)
check("the drag moved the window, so there are cells where a pane used to be",
      moved is not None and moved != (10, 24), moved)


# And the same through every frame of a drag, not only once it has settled: a
# shadow that flickers while a window moves is invisible to a comparison of
# the end state, which is how one shipped. Each step is compared at the point
# the desktop says it has consumed that much input -- the idle marker -- so
# the two runs are at the same logical frame rather than the same instant,
# which is what made comparing whole sessions flaky.
def dragsteps(force):
    p = scratch("desktop-drag.hibr")
    open(p, "w").write("%s. %s\ndt_open\n%s\ndt_run\ndt_close\n"
                       % (load(MOD), WM, ORACLE))
    # The one thing two sessions legitimately disagree about is the clock in
    # the bar: these two runs are about eight seconds apart, so one of them
    # crosses a minute and `12:38` meets `12:39` -- one glyph, no pen, in
    # every step at once, which is what this failed a release gate with. A
    # format with no field in it is the same text for ever. The clock is the
    # settled oracle's business (it asks for its own frames); here it is only
    # noise, and masking row 0 would hide the bar's shadow with it.
    t = Term(p, env={"DT_TICK": "60", "DT_FORCEDRAW": force,
                     "DT_BARTIME": "hibr"}, rows=ROWS, cols=COLS, settle=0.6)
    out = []
    t.keys([press(10, 30)])
    for i in range(1, 7):
        t.keys([drag(10 + i, 30 + i * 2)])
        sc = orsettled(t)
        out.append(([r[:] for r in sc.g], [r[:] for r in sc.p]))
    t.keys([release(16, 42)])
    sc = orsettled(t)
    out.append(([r[:] for r in sc.g], [r[:] for r in sc.p]))
    t.quit(b"qy", 1.0)
    os.unlink(p)
    return out


d0 = dragsteps("0")
d1 = dragsteps("1")
bad = [i for i, (a, b) in enumerate(zip(d0, d1)) if a != b]
check("every frame of a drag is what a full redraw would have drawn",
      len(d0) == len(d1) and not bad,
      "steps differing: %s of %d" % (bad, len(d0)))
check("including the shadows, which is what flickered while a window moved",
      all(a[1] == b[1] for a, b in zip(d0, d1)),
      [i for i, (a, b) in enumerate(zip(d0, d1)) if a[1] != b[1]])
difg = [(r, c, beforeg[r][c], after.g[r][c])
        for r in range(len(beforeg)) for c in range(len(beforeg[r]))
        if beforeg[r][c] != after.g[r][c]]
check("redrawing everything changes no glyph that drawing only what changed left",
      not difg, "cells (row, col, left alone, redrawn): %s" % difg[:12])
check("and no colour, which a shadow cast twice on the same cells would",
      beforep == after.p, after)
ort.quit(b"qy", 1.2)
os.unlink(orpath)

# Everything drawn over the finished frame -- a note, an open menu, a dialog,
# a drag's ghost, the standby dim -- writes at absolute coordinates on top of
# the lot, and nothing repaints what it covered once it has gone: the
# wallpaper may be left alone and a window nothing changed draws nothing. A
# note that expired left its whole box on the screen until the next key
# (0.99.86), reported as "if something overwrites them like the notification
# panel, they are not redrawn unless I click on them". The frame after one has
# drawn is a full one now, and the console counts the writes (`console drawn`)
# rather than each overlay declaring itself, so one added later cannot forget.
#
# Two windows that overlap nothing, on purpose: where any two overlap the
# wallpaper is not gated at all, it repaints the note's cells for free, and
# the check would pass with the bug still there.
overpath = scratch("desktop-over.hibr")
open(overpath, "w").write(
    "%s. %s\ndt_open\n%s\ndt_run\ndt_close\n"
    % (load(MOD), WM, ORCFN + 'dt_new "Alone" 8 24 3 50 orc\n'
                              'dt_new "Apart" 8 24 13 4 orc\n'))
ovt = Term(overpath, env={"DT_TICK": "60", "DT_NOTEMS": "700"}, rows=ROWS,
           cols=COLS, settle=0.6)
ovt.keys([b"x", b"x", 0.5, b"N", 1.6])
gone = orsettled(ovt)
goneg = [r[:] for r in gone.g]
gonep = [r[:] for r in gone.p]
ovt.keys([b"F", 0.4, b"z", 0.4, b"z", 0.5])
full = orsettled(ovt)
ovt.quit(b"qy", 1.0)
os.unlink(overpath)
difn = [(r, c, goneg[r][c], full.g[r][c]) for r in range(len(goneg))
        for c in range(len(goneg[r])) if goneg[r][c] != full.g[r][c]]
check("a note drawn over the screen leaves nothing of itself when it goes",
      not difn, "cells (row, col, left, redrawn): %s" % difn[:12])
check("and no colour of its own either", gonep == full.p, full)

# A bare window -- a sticky note -- draws every cell of itself, and dt_win
# returned for it before the line that cast the shadow, while the path for a
# window it leaves alone cast one: so it had a shadow on the frames it was
# skipped and none on the frames it was drawn. A sticky is the only bare
# window with a dirty flag of its own, which is why it was the only thing that
# did it -- "the stickies' shadow blink when I move any window, only them",
# and shadows differing from one workspace to the next, which is the same
# thing seen in a snapshot. The two frames are this file's usual pair: settled
# with the skipping on, then the same frame with it off.
barepath = scratch("desktop-bare.hibr")
open(barepath, "w").write(
    "%s. %s\ndt_open\n%s\ndt_run\ndt_close\n"
    % (load(MOD), WM, ORCFN.replace("orc_", "orb_")
       + 'dt_app orb "Bare" 8 24 "" "" "" "" bare\n'
         'dt_new "Bare" 8 24 3 50 orb\ndt_new "Away" 8 24 14 4 orb\n'))
bt = Term(barepath, env={"DT_TICK": "60"}, rows=ROWS, cols=COLS, settle=0.6)
bt.keys([b"x", b"x", 0.6])
left = orsettled(bt)
leftp = [r[:] for r in left.p]
bt.keys([b"F", 0.4, b"z", 0.4, b"z", 0.5])
drew = orsettled(bt)
bt.quit(b"qy", 1.0)
os.unlink(barepath)
check("a bare window's shadow is the same on a frame that drew it as on one "
      "that left it alone", leftp == drew.p,
      [(r, c) for r in range(len(leftp)) for c in range(len(leftp[r]))
       if leftp[r][c] != drew.p[r][c]][:10])
check("and it is cast at all: the column beside it is dimmed",
      drew.p[4][74].startswith("0;2;") and leftp[4][74].startswith("0;2;"),
      (leftp[4][74], drew.p[4][74]))

# A key sent right after a resize must not be lost while the debounce is
# waiting to see whether more of them are coming (DT_RSTILL is 150ms).
path = scratch("desktop-rsz.hibr")
open(path, "w").write("%s. %s\ndt_open\n%s\ndt_run\ndt_close\n"
                      % (load(MOD), WM, ONE))
t = Term(path, env={"DT_TICK": "60"}, rows=ROWS, cols=COLS, settle=0.6)
t.resize(ROWS, COLS + 1)
t.send(b"\x1b[21~", settle=0.5)
sc = t.screen()
check("a key right after a resize is not dropped by the debounce",
      sc.find("Screen Saver") is not None, sc)
t.quit(b"qy", 1.0)
os.unlink(path)

# A burst of drag reports is drawn once it has caught up, not once per
# report: a frame slower than the reports arrive left the window crawling
# after the mouse had stopped. One write, a hundred moves.
path = scratch("desktop-burst.hibr")
open(path, "w").write("%s. %s\ndt_open\n%s\ndt_run\ndt_close\n"
                      % (load(MOD), WM, ONE))
t = Term(path, rows=ROWS, cols=COLS, settle=0.6)
f0 = t.frames()
burst = press(6, 20)
for i in range(1, 101):
    burst += drag(6 + i % 8, 20 + i % 40)
burst += release(6, 60)
t.send(burst)
f1 = t.frames()
sc = t.screen()
check("a burst of a hundred drag reports draws a handful of frames",
      0 < f1 - f0 <= 5, "%d frames" % (f1 - f0))
check("...and leaves the window where the last report put it",
      sc.find("┤ Hello ├") is not None and
      sc.find("┤ Hello ├")[0] == 6 + 100 % 8, sc)
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

sc, _ = run(ONE, [press(6, 37), release(6, 37)])
check("clicking the close button closes the window",
      sc.find("Hello") is None, sc)
check("and nothing is left where it was", sc.g[6][10] == "·", sc)

# Modifier drag -- #47 -- moves a window from a click anywhere in its
# body, not just its title bar; on by default since 0.73 with alt, and
# only for the configured modifier. ALT is bit 8 of the SGR mouse button
# byte.
ALT = 8
DRAG_BODY = [press(8, 15, ALT), drag(11, 27, ALT), release(11, 27, ALT)]

sc, _ = run(ONE, DRAG_BODY)
check("on by default: an alt-drag in the body moves the window",
      sc.find("┤ Hello ├") == (9, 24), sc)
sc, _ = run(ONE, DRAG_BODY, pre="DT_DRAGMOD=0")
check("and switched off, an alt-click in the body does not move it",
      sc.find("┤ Hello ├") == (6, 12), sc)

sc, _ = run(ONE, DRAG_BODY, pre="DT_DRAGMOD=1\nDT_DRAGKEY=alt")
check("enabled, with alt configured: it moves the same as the title bar",
      sc.find("┤ Hello ├") == (9, 24), sc)

sc, _ = run(ONE, DRAG_BODY, pre="DT_DRAGMOD=1\nDT_DRAGKEY=ctrl")
check("enabled, but configured for ctrl: alt alone still does nothing",
      sc.find("┤ Hello ├") == (6, 12), sc)

sc, _ = run(ONE, [press(8, 15), drag(11, 27), release(11, 27)],
            pre="DT_DRAGMOD=1\nDT_DRAGKEY=alt")
check("enabled, but the click itself carries no modifier: nothing moves",
      sc.find("┤ Hello ├") == (6, 12), sc)

# A title-bar button presses then releases, the same as any other
# clickable thing in a real GUI, and is only acted on if the release lands
# back on the same button. While held, only the glyph changes colour -- no
# block behind it: close's red goes lighter (checked in the raw bytes,
# since the screen model tracks characters, not colour), and a grey button
# takes the theme's accent.
CLOSE_BG_RGB = b"48;2;245;101;101"
CLOSE_PRESSED = b"38;2;249;170;170"
sc, raw = run(ONE, [press(6, 37)])
check("the close button's glyph turns lighter red while held, with no "
      "block behind it", CLOSE_PRESSED in raw and CLOSE_BG_RGB not in raw,
      raw)

sc, _ = run(ONE, [press(6, 37), drag(15, 15), release(15, 15)])
check("dragging off the close button before releasing cancels it",
      sc.find("Hello") is not None, sc)

sc, _ = run(ONE, [press(6, 33), release(6, 33)])
check("minimising takes the window off the screen",
      sc.g[6][10] == "·" and sc.g[10][20] == "·", sc)
sc, _ = run(ONE, [press(6, 33), release(6, 33), press(0, 70)])
check("and it is still listed, marked hidden, in the application menu",
      sc.find("· Hello") is not None, sc)

sc, _ = run(ONE, [press(6, 33), release(6, 33), press(0, 70), b"\r"])
check("choosing it there brings it back",
      sc.g[6][10] == "┌" and sc.find("┤ Hello ├") == (6, 12), sc)

sc, _ = run(ONE, [press(6, 35), release(6, 35)])
check("zooming fills the screen below the bar",
      sc.g[1][0] == "┌" and sc.g[23][79] == "◢", sc)
sc, _ = run(ONE, [press(6, 35), release(6, 35), press(1, 75), release(1, 75)])
check("and zooming again puts it back where it was",
      sc.g[6][10] == "┌" and sc.g[13][39] == "◢", sc)


# Two presses close enough together to count as one double click -- run()'s
# own default settle/collect between keys (0.25s + 0.2s) is longer than
# DT_DBLMS, so this needs its own tight timing rather than run()'s.
def dblclick_run(session, r, c, env=None):
    # This process's own name, the same as run()'s: a fixed path under /tmp
    # is shared with every other run of this suite, and tests/all.py and
    # tests/asan.py run side by side -- one unlinked the file the other was
    # about to be started on, which fails as "no such file" in whichever got
    # there second and reads as nothing to do with double clicking.
    path = "/tmp/hibr-desktop-dblclick-%d.hibr" % os.getpid()
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

# --- window chrome: frame, button side, title alignment, icon style -------
#
# DT_FRAME, DT_BTNSIDE, DT_TITLEALIGN and DT_BTNSTYLE are the Windows pane's
# pane's own four settings (examples/desktop/control-panel/window-style.hibr);
# tests/apps.py checks the pane itself, this checks what each value actually
# draws. Every button style is 5 (fixed) or 7 (movable) characters wide, the
# same as the original brackets, so a style change alone never moves where a
# button is clicked -- only what is drawn there.

sc, _ = run(ONE, env={"DT_FRAME": "double"})
check("DT_FRAME=double draws double-line corners and sides",
      sc.g[6][10] == "╔" and sc.g[6][39] == "╗" and
      sc.g[13][10] == "╚" and sc.g[9][10] == "║", sc)
check("and the grow box still overwrites its own corner, same as single",
      sc.g[13][39] == "◢", sc)
check("and the title and buttons are unaffected by the frame style",
      sc.find("┤ Hello ├") == (6, 12) and sc.g[6][37] == "x", sc)

sc, _ = run(ONE, env={"DT_FRAME": "rounded"})
check("DT_FRAME=rounded draws arcs at the corners, single lines between",
      sc.g[6][10] == "╭" and sc.g[6][39] == "╮" and sc.g[13][10] == "╰" and
      sc.g[9][10] == "│" and sc.g[13][39] == "◢", sc)

sc, raw = run(ONE, env={"DT_FRAME": "none"})
check("DT_FRAME=none is a blank frame -- no border ring at all",
      sc.find("┌") is None and sc.find("└") is None and
      sc.g[6][10] == " " and sc.g[13][39] == " ", sc)
check("and no tick marks either -- those are box-drawing glyphs too",
      sc.find("┤") is None and sc.find("├") is None, sc)
check("but the title and its buttons are still there, same as ever",
      sc.find("Hello") == (6, 14) and sc.g[6][37] == "x", sc)
sc, _ = run(ONE, [press(6, 20), drag(9, 24), release(9, 24)],
            env={"DT_FRAME": "none"})
check("and the (undrawn) title bar can still be dragged",
      sc.find("Hello") == (9, 18), sc)
sc, _ = run(ONE, [press(6, 37), release(6, 37)], env={"DT_FRAME": "none"})
check("and its close button still closes it",
      sc.find("Hello") is None, sc)

sc, _ = run(ONE, env={"DT_BTNSIDE": "left"})
check("DT_BTNSIDE=left docks the cluster left, mirrored -- close, max, "
      "min -- so close stays nearest the window's own corner either way",
      sc.g[6][12] == "x" and sc.g[6][14] == "□" and sc.g[6][16] == "_" and
      sc.find("┤ Hello ├") == (6, 18), sc)
sc, _ = run(ONE, [press(6, 12), release(6, 12)], env={"DT_BTNSIDE": "left"})
check("and the close button, now leftmost, still closes it",
      sc.find("Hello") is None, sc)
sc, _ = run(ONE, [press(6, 16), release(6, 16), press(0, 70)],
            env={"DT_BTNSIDE": "left"})
check("while the rightmost button there is min, not close",
      sc.find("· Hello") is not None, sc)

sc, _ = run(ONE, env={"DT_TITLEALIGN": "center"})
check("DT_TITLEALIGN=center centres the title on the bar's own full width",
      sc.find("┤ Hello ├") == (6, 20), sc)
sc, _ = run(ONE, env={"DT_TITLEALIGN": "right"})
check("and right pins it against the button cluster",
      sc.find("┤ Hello ├") == (6, 23), sc)
sc, _ = run(ONE, env={"DT_TITLEALIGN": "center", "DT_BTNSIDE": "left"})
check("and centring lands in the same place regardless of which side the "
      "buttons are docked on -- centring in the room they leave instead "
      "pushed the title away from them by about half their own width, in "
      "opposite directions depending on the side, which is what read as "
      "off-centre",
      sc.find("┤ Hello ├") == (6, 20), sc)

FIXEDONE = ('dt_app fx "Fixed" 6 20 once "◆" fixed\n'
            'dt_new "Fixed" 6 20 6 10 fx\n')
sc, _ = run(FIXEDONE, env={"DT_TITLEALIGN": "center"})
check("and a fixed window's own narrower cluster still centres correctly",
      sc.find("┤ Fixed ├") == (6, 15), sc)

NARROWFIXED = ('dt_app fx "Fixedish" 6 20 once "◆" fixed\n'
               'dt_new "Fixedish" 6 20 6 10 fx\n')
sc, _ = run(NARROWFIXED, env={"DT_TITLEALIGN": "center"})
check("and on a bar too narrow for true centre, it clamps short of the "
      "buttons rather than running into them",
      sc.find("┤ Fixedish ├") == (6, 12), sc)

AMBER_FG = b"38;2;246;173;85"
GREEN_FG = b"38;2;104;211;145"
RED_FG = b"38;2;245;101;101"
sc, raw = run(ONE, env={"DT_BTNSTYLE": "circles"})
check("DT_BTNSTYLE=circles colours all three buttons, traffic-light style",
      AMBER_FG in raw and GREEN_FG in raw and RED_FG in raw and
      sc.g[6][33] == "●" and sc.g[6][35] == "●" and sc.g[6][37] == "●", sc)
sc, raw = run(ONE, env={"DT_BTNSTYLE": "squares"})
check("DT_BTNSTYLE=squares does the same with a different glyph",
      AMBER_FG in raw and GREEN_FG in raw and RED_FG in raw and
      sc.g[6][37] == "■", sc)
sc, raw = run(ONE, env={"DT_BTNSTYLE": "diamonds"})
check("DT_BTNSTYLE=diamonds and dashes colour them too, each its own glyph",
      AMBER_FG in raw and GREEN_FG in raw and RED_FG in raw and
      sc.g[6][33] == "◆" and sc.g[6][37] == "◆", sc)
sc, raw = run(ONE, env={"DT_BTNSTYLE": "dashes"})
check("and dashes are three dashes, in the same three colours",
      AMBER_FG in raw and GREEN_FG in raw and RED_FG in raw and
      sc.g[6][33] == "━" and sc.g[6][35] == "━" and sc.g[6][37] == "━", sc)
sc, _ = run(ONE, [press(6, 37), release(6, 37)], env={"DT_BTNSTYLE": "circles"})
check("a style change never moves where a button is clicked, only its glyph",
      sc.find("Hello") is None, sc)

# An app declared fixed has no maximise button at all -- not dimmed, not
# there -- so a game whose board is one size does not offer to stretch it.
# These sessions open their window with dt_launch and then click where it
# lands, so they keep the old cascade rather than follow smart placement.
FIXED = ('dt_app fx "Fixed" 6 20 once "◆" fixed\n'
         'fx_draw() { console put -p "w$1" 1 1 "hi"; }\n'
         'DT_PLACE=cascade\ndt_launch fx\n')
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
# Three rights, not two: every bar reads File, Edit, the app's own menus,
# Window since 0.99.93, and this app declares none, so Window is third
# (Gitea #120).
sc, _ = run(FIXED, [b"\x1b[21~", b"\x1b[C", b"\x1b[C", b"\x1b[C"])
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
NOMENU = 'dt_app nm "NoMenu" 6 20\nnm_draw() { :; }\nDT_PLACE=cascade\ndt_launch nm\n'
sc, _ = run(NOMENU)
check("an app with no menus of its own still names itself in the bar",
      sc.find("NoMenu ▾") is not None, sc)
# And the bar it gets is the same shape as every other app's. An app that
# declares no menus had no File at all until 0.99.93 -- Clock, Control
# Panel, About, Screenshot and every dialog -- so the two menus a person
# reaches for most moved about from one window to the next (Gitea #120).
# The window manager declares File > Close for them in one place, rather
# than each of twenty-four files remembering to.
bar = sc.text().splitlines()[0]
bf, be = bar.find("File"), bar.find("Edit")
check("and the File, Edit bar every other app has",
      bf >= 0 and bf < be, sc)

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
           'DT_PLACE=cascade\ndt_launch wg\n')
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
path = scratch("desktop-widgets.hibr")
open(path, "w").write("%s. %s\n%s\ndt_open\n%s\ndt_run\ndt_close\n"
                      % (load(MOD), WM, "", WIDGETS))
t = Term(path, env={"DT_TICK": "60"}, rows=ROWS, cols=COLS, settle=0.6)
t.keys([press(7, 9), press(9, 10)])
sc = t.screen()
check("choosing one runs the callback with the id and the value",
      sc.find("picked two") is not None and sc.find("three") is None, sc)
t.quit(b"qy", 1.2)
os.unlink(path)

# A note used to be three plain rows with no border at all -- applying a
# wallpaper from the Wallpaper pane, a window that fills most of the
# screen, landed its own centred note on top of that same window and
# read as the screen having come out wrong rather than as a
# notification, until the next click or key (dismissing it) was
# mistaken for whatever "fixed" it. It has a real border now, and clears
# itself on a timeout, not only on the next key -- checked with nothing
# sent at all, so only the timeout could have cleared it.
NOTET = tempfile.mkdtemp(prefix="hibr-notet-")
p = os.path.join(NOTET, "session.hibr")
open(p, "w").write(
    "%s. %s\ndt_open\ndt_note \"Hi there\"\ndt_run\ndt_close\n" % (load(MOD), WM)
)
t = Term(p, env={"DT_TICK": "300"}, rows=ROWS, cols=COLS, settle=0.5)
sc = t.screen()
check("a note shows in a real bordered box, not plain floating text",
      sc.find("┌") is not None and sc.find("Hi there") is not None, sc)
t.collect(3.0)
sc = t.screen()
check("and clears itself after its own timeout, with no key or click at all",
      sc.find("Hi there") is None, sc)
t.quit(None, 1.0)
shutil.rmtree(NOTET, True)

# Growl-style stacking: several notes fired together all show at once,
# stacked, not one replacing another the way a single DT_NOTE slot used
# to.
NOTEQ = tempfile.mkdtemp(prefix="hibr-noteq-")
p = os.path.join(NOTEQ, "session.hibr")
open(p, "w").write(
    "%s. %s\ndt_open\ndt_note \"First\"\ndt_note \"Second\"\ndt_run\ndt_close\n"
    % (load(MOD), WM)
)
t = Term(p, env={"DT_TICK": "300"}, rows=ROWS, cols=COLS, settle=0.5)
sc = t.screen()
f1, f2 = sc.find("First"), sc.find("Second")
check("two notes fired together both show, stacked rather than one "
      "replacing the other",
      f1 is not None and f2 is not None and f1[0] != f2[0], sc)
t.quit(None, 1.0)
shutil.rmtree(NOTEQ, True)

# dt_notify's own clickable form: an action after the message runs when
# that note, specifically, is clicked, and the note is dismissed either
# way.
NOTIFY = tempfile.mkdtemp(prefix="hibr-notify-")
mark = os.path.join(NOTIFY, "clicked")
p = os.path.join(NOTIFY, "session.hibr")
open(p, "w").write(
    "%s. %s\nnf_mark() { touch %s; }\ndt_open\n"
    "dt_notify \"Click me\" nf_mark\ndt_run\ndt_close\n"
    % (load(MOD), WM, mark)
)
t = Term(p, env={"DT_TICK": "300"}, rows=ROWS, cols=COLS, settle=0.5)
sc = t.screen()
hit = sc.find("Click me")
check("a dt_notify note is on screen before it is clicked",
      hit is not None, sc)
t.keys([press(hit[0], hit[1] + 1)])
sc = t.screen()
check("clicking it runs its own action", os.path.exists(mark), sc)
check("and dismisses the note", sc.find("Click me") is None, sc)
t.quit(None, 1.0)
shutil.rmtree(NOTIFY, True)

# A plain dt_note has no action -- clicking it only dismisses it.
DISMISS = tempfile.mkdtemp(prefix="hibr-notedismiss-")
p = os.path.join(DISMISS, "session.hibr")
open(p, "w").write(
    "%s. %s\ndt_open\ndt_note \"Dismiss me\"\ndt_run\ndt_close\n"
    % (load(MOD), WM)
)
t = Term(p, env={"DT_TICK": "300"}, rows=ROWS, cols=COLS, settle=0.5)
sc = t.screen()
hit = sc.find("Dismiss me")
check("a plain dt_note is on screen before it is clicked",
      hit is not None, sc)
t.keys([press(hit[0], hit[1] + 1)])
sc = t.screen()
check("clicking a plain note with no action just dismisses it",
      sc.find("Dismiss me") is None, sc)
t.quit(None, 1.0)
shutil.rmtree(DISMISS, True)

# DT_NOTEPOS moves which corner new notes stack from.
CORNER = tempfile.mkdtemp(prefix="hibr-notecorner-")
p = os.path.join(CORNER, "session.hibr")
open(p, "w").write(
    "%s. %s\ndt_open\ndt_note \"Corner\"\ndt_run\ndt_close\n" % (load(MOD), WM)
)
t = Term(p, env={"DT_TICK": "300", "DT_NOTEPOS": "bottom-left"},
         rows=ROWS, cols=COLS, settle=0.5)
sc = t.screen()
hit = sc.find("Corner")
check("DT_NOTEPOS=bottom-left stacks from the bottom-left instead",
      hit is not None and hit[0] >= ROWS - 4 and hit[1] <= 4, sc)
t.quit(None, 1.0)
shutil.rmtree(CORNER, True)

# The menu-bar bell opens a read-only history of notes already shown,
# even ones already gone -- the "center" a notification center needs.
BELL = tempfile.mkdtemp(prefix="hibr-notebell-")
p = os.path.join(BELL, "session.hibr")
bellapps = 'DT_APPDIRS+=("%s")\ndt_apps\n' % tree("examples/desktop/apps")
open(p, "w").write(
    "%s. %s\n%sdt_open\ndt_note \"Remembered\"\ndt_run\ndt_close\n"
    % (load(MOD), WM, bellapps)
)
t = Term(p, env={"DT_TICK": "300"}, rows=ROWS, cols=COLS, settle=0.5)
t.keys([press(0, COLS - 23)])
sc = t.screen()
check("the menu bar's own bell opens the notification history",
      sc.find("┤ Notifications ├") is not None, sc)
check("and it lists a note already shown, timestamped",
      re.search(r"\d\d:\d\d  normal  Desktop\s+Remembered", sc.text()) is not None, sc)
t.quit(None, 1.0)
shutil.rmtree(BELL, True)

# Idle means asleep. Every other run here uses a 60ms DT_TICK, which no
# longer exists, and could never have caught a desktop waking on its own:
# this one opens a real terminal window, with a blinking cursor off and
# on, and counts how often the process actually wakes with nothing
# happening. The bar clock asks once a minute and the desktop icons' own
# mount rescan every 5s, so a quiet desktop is well under one wake a
# second; a blinking cursor is two draws a second by design.
def idle_wakes(env, session, act=None):
    d = tempfile.mkdtemp(prefix="hibr-idle-")
    p = os.path.join(d, "session.hibr")
    apps = 'DT_APPDIRS+=("%s")\ndt_apps\n' % tree("examples/desktop/apps")
    open(p, "w").write("%s. %s\n%sdt_open\n%s\ndt_run\ndt_close\n"
                       % (load(MOD), WM, apps, session))
    t = Term(p, env=env, rows=ROWS, cols=COLS, settle=1.5)
    t.collect(1.5)
    if act:
        act(t)
        t.collect(1.0)

    def vol():
        for l in open("/proc/%d/status" % t.pid):
            if l.startswith("voluntary_ctxt_switches"):
                return int(l.split()[1])

    def cpu():
        f = open("/proc/%d/stat" % t.pid).read().rsplit(")", 1)[1].split()
        return (int(f[11]) + int(f[12])) / os.sysconf("SC_CLK_TCK")
    a, c, t0 = vol(), cpu(), time.time()
    t.collect(6.0)
    took = time.time() - t0
    rate = (vol() - a) / took
    sc = t.screen()
    sc.cpu = (cpu() - c) / took
    t.quit(None, 0.5)
    shutil.rmtree(d, True)
    return rate, sc

rate, sc = idle_wakes({"DT_CURSOR_BLINK": "0"}, "dt_launch term")
check("an idle desktop with a terminal open sleeps, not redraws on a tick "
      "(%.1f wakes/s, %.0f%% of a core)" % (rate, sc.cpu * 100),
      rate < 1.0 and sc.cpu < 0.05, sc)
rate, sc = idle_wakes({"DT_CURSOR_BLINK": "1"}, "dt_launch term")
check("a blinking cursor asks for its own two frames a second, and no more "
      "(%.1f wakes/s)" % rate, 1.0 < rate < 4.0, sc)
# A pty whose program has exited reads as ready for ever; a window left
# showing the exit status must stop watching it, or the desktop spins. A
# spin never blocks, so it makes *no* voluntary wakes and would pass the
# checks above: CPU time is what catches it.
rate, sc = idle_wakes({"DT_CURSOR_BLINK": "1"},
                      "TW_CMD=(false)\ndt_launch term")
check("a terminal whose program has exited leaves the desktop idle "
      "(%.0f%% of a core)" % (sc.cpu * 100),
      sc.cpu < 0.05 and sc.find("[exited 1") is not None, sc)

# A terminal that is not drawn -- minimised, or on another workspace --
# while its program keeps writing: its output must still be read, or the
# pty stays readable and the desktop spins on it.
CHATTY = ("TW_CMD=(sh -c 'while :; do echo tick; sleep 0.2; done')\n"
          "dt_launch term")


def minimise(t):
    sc = t.screen()
    b = sc.find("_ □ x")
    if b:
        t.send(press(b[0], b[1]))
        t.send(release(b[0], b[1]))


def away(t):
    t.send(b"\x1b2")


rate, sc = idle_wakes({"DT_CURSOR_BLINK": "0"}, CHATTY, minimise)
check("a minimised terminal still writing leaves the desktop idle "
      "(%.0f%% of a core)" % (sc.cpu * 100),
      sc.cpu < 0.05 and sc.find("tick") is None, sc)
rate, sc = idle_wakes({"DT_CURSOR_BLINK": "0"}, CHATTY, away)
check("so does one on another workspace (%.0f%% of a core)" % (sc.cpu * 100),
      sc.cpu < 0.05 and sc.find("tick") is None, sc)
rate, sc = idle_wakes({"DT_CURSOR_BLINK": "0"},
                      "TW_CMD=(sh -c 'sleep 0.5; exit 3')\ndt_launch term",
                      away)
check("and one whose program ends while it is away (%.0f%% of a core)"
      % (sc.cpu * 100), sc.cpu < 0.05, sc)

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
# The session ends with q, which now ends move mode on its way to quitting,
# so whether the note was drawn is read from everything sent, not from the
# last frame.
sc, raw = run(ONE, [press(6, 20, 2), press(6, 22)])
check("choosing Move from it starts moving, the same as from the menu bar",
      b"moving" in raw, sc)

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

sc, _ = run(TWO, [b"\x1b\t"])
check("alt-tab raises the window at the bottom of the stack",
      sc.g[9][39] == "│" and sc.g[13][25] == "─", sc)
sc, _ = run(TWO, [b"\x1b\t", b"\x1b\t"])
check("and alt-tab again brings the other one back",
      sc.g[9][20] == "┌" and sc.g[9][39] == "─", sc)

sc, _ = run(TWO, [press(6, 12), press(6, 37), release(6, 37)])
check("closing the raised window leaves the other",
      sc.find("Under") is None and sc.find("┤ Over ├") == (9, 22), sc)

CLOSED = scratch("dt-closed.mark")
APP = ('counter_open()  { CN=0; }\n'
       'counter_draw()  { console put -p "w$1" 2 3 "count $CN"; }\n'
       'counter_key()   { [ "$2" = + ] && CN=$((CN+1)) && return 0; return 1; }\n'
       'counter_click() { CN=$(($2 * 100 + $3)); }\n'
       'counter_close() { echo "CLOSED" > ' + CLOSED + '; }\n'
       'dt_new "App" 8 30 6 10 counter\n')

sc, _ = run(APP)
check("an app draws inside its own window", sc.find("count 0") == (8, 13), sc)

sc, _ = run(APP, [b"\x1b[15~"])
check("a key the app refuses does not reach it", sc.find("count 0"), sc)

sc, _ = run(APP, [press(9, 16)])
check("a click reaches the app in the coordinates it draws in",
      sc.find("count 306") == (8, 13), sc)

try:
    os.unlink(CLOSED)
except OSError:
    pass
sc, _ = run(APP, [press(6, 37), release(6, 37)])
check("closing an app's window closes the app",
      sc.find("count") is None and os.path.exists(CLOSED), sc)

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

sc, raw = run(MENUS, [press(0, 6)])
check("clicking a title drops the menu under it",
      sc.find("Bump") == (1, 6) and sc.find("Reset") == (3, 6), sc)
check("a separator is drawn between the groups", sc.at(2, 5) == "─", sc)
check("and a blank row closes it, not a line -- Close is the last item",
      sc.find("Close") == (4, 6) and sc.at(5, 6) == " ", sc)
# #55: an accelerator that is a real letter of the label is underlined
# in place, not repeated after it -- Bump and Reset are both picked by
# their own first letter, so column 16 (the old trailing column) is
# blank for them now, and the underline SGR (4) brackets that letter
# in the raw stream instead. Close's own accelerator, w, is not a
# letter anywhere in "Close" -- an author's own pick, not a spelled-out
# one -- so it still falls back to showing up there, same as always.
check("the old trailing column is blank for an on-label accelerator",
      sc.at(1, 16) == " " and sc.at(3, 16) == " ", sc)
check("but still shows an off-label one, Close's own w",
      sc.at(4, 16) == "w", sc)
check("and the on-label accelerator is underlined in the label itself",
      re.search(rb"\x1b\[[0-9;]*\b4\b[0-9;]*mB", raw) is not None and
      re.search(rb"\x1b\[[0-9;]*\b4\b[0-9;]*mR", raw) is not None, raw)

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
      sc.find("Screen Saver") is not None, sc)
sc, _ = run(MENUS, [b"\x1b"])
check("and so does escape", sc.find("Screen Saver") is not None, sc)

sc, _ = run(MENUS, [b"\x1b[21~"])
check("an ordinary registered app is listed", sc.find("Noted") is not None, sc)

HIDDEN = MENUS + 'dt_app hushed "Hushed" 6 24 "" "" "" hidden\n'
sc, _ = run(HIDDEN, [b"\x1b[21~"])
check("one registered hidden is not, though it is still a real app",
      sc.find("Hushed") is None, sc)

# Rename and Get Info register themselves only so dt_win/dt_btn treat
# their windows as fixed -- they open on a specific file's own entry,
# never generically, and used to leak onto this menu regardless, since
# dt_appmenu only ever excluded "about" by a hardcoded name and nothing
# excluded them. Loading the real apps proves the hidden flag actually
# reaches them, not just a synthetic one built for the check above.
REALAPPS = 'DT_APPDIRS+=("%s")\ndt_apps\n' % tree("examples/desktop/apps")
sc, _ = run(REALAPPS, [b"\x1b[21~"])
check("Rename and Get Info do not leak onto the real hibr menu",
      sc.find("Rename") is None and sc.find("Get Info") is None, sc)
check("but the apps that belong there still do",
      sc.find("Files") is not None and sc.find("Task Manager") is not None,
      sc)

# A dropdown is a rectangle, which is a thing only the pens can say: a dimmed
# row changes the foreground and keeps the background, so the glyphs of a row
# that was padded a column short look exactly like one that was not. Measured
# on each row's run of menu-background cells -- every row must end at the same
# column. dt_dim passes an empty key and the no-underline branch interpolated
# it into a three-column field as " $k ", which is two columns when the key is
# empty, so every dimmed item in every menu was a column short (Gitea #128).
MBG = re.compile(r"48;2;\d+;\d+;\d+")


def menuedges(sc, anchor):
    """The rightmost menu-background cell of each row of the open menu."""
    def bg(p):
        m = MBG.search(p or "")
        return m.group(0) if m else ""
    a = None
    for r in range(1, ROWS):
        if anchor in sc.row(r):
            a = r
            break
    if a is None:
        return []
    face = bg(sc.p[a][sc.row(a).index(anchor)])
    out = []
    for r in range(1, ROWS):
        cells = [c for c in range(COLS) if bg(sc.p[r][c]) == face]
        if len(cells) >= 6:
            out.append((r, max(cells)))
    return out


sc, _ = run('dt_new "Win" 8 30 6 10', feed=[b"\x1b[21~"] + [b"\x1b[C"] * 3)
EDGES = menuedges(sc, "Resize")
check("an open menu is a rectangle: every row ends at the same column",
      len(EDGES) > 6 and len({e for _, e in EDGES}) == 1, EDGES)

sc, _ = run(MENUS, [b"\x1b[21~", b"\x1b[C"])
check("right walks to the next menu along",
      sc.find("Bump") is not None and sc.find("Screen Saver") is None, sc)
sc, _ = run(MENUS, [b"\x1b[21~", b"\x1b[C", b"\x1b[C"])
check("and on to the one after that", sc.find("Bump Twice") is not None, sc)
sc, _ = run(MENUS, [b"\x1b[21~", b"\x1b[D"])
check("left from the first wraps round to the application menu",
      sc.find("Noted") is not None and sc.find("Screen Saver") is None, sc)

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
path = scratch("desktop-about.hibr")
open(path, "w").write("%s. %s\n%s\ndt_open\n%s\ndt_run\ndt_close\n"
                      % (load(MOD), WM, "", MENUS))
t = Term(path, env={"DT_TICK": "60"}, rows=ROWS, cols=COLS, settle=0.6)
t.keys([press(0, 2), b"a"])
sc = t.screen()
check("about says what this is",
      sc.find("a desktop written in the shell") is not None, sc)
t.quit(b"qy", 1.2)
os.unlink(path)

sc, _ = run(MENUS, [press(6, 37), release(6, 37)])
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

# Any of the other three corners resizes too, the opposite one anchored --
# captured once at the press, since the window's own row/col start
# changing the moment a top or left edge does.
sc, _ = run(ONE, [press(6, 10), drag(4, 8), release(4, 8)])
check("the top-left corner resizes too, growing up and to the left",
      sc.at(4, 8) == "┌" and sc.at(13, 39) == "◢", sc)

sc, _ = run(ONE, [press(6, 39), drag(4, 45), release(4, 45)])
check("so does the top-right, growing up and to the right",
      sc.at(4, 45) == "┐" and sc.at(13, 10) == "└", sc)

sc, _ = run(ONE, [press(13, 10), drag(15, 6), release(15, 6)])
check("and the bottom-left, growing down and to the left",
      sc.at(15, 6) == "└" and sc.at(6, 39) == "┐", sc)

sc, _ = run(ONE, [press(6, 10), drag(12, 30), release(12, 30)])
check("dragging the top-left past the minimum size stops there, "
      "the opposite corner still anchored",
      sc.at(13, 39) == "◢" and sc.at(10, 24) == "┌", sc)

FIXED_TL = ('dt_app fxc "Fixed" 6 20 once "◆" fixed\n'
            'dt_new "Fixed" 6 20 6 10 fxc\n')
sc, _ = run(FIXED_TL, [press(6, 10), drag(4, 8), release(4, 8)])
check("a fixed window's top-left corner moves it instead -- no drag can "
      "resize it, from any corner",
      sc.find("┤ Fixed ├") == (4, 10), sc)

# A plain side, not just a corner, resizes too -- one dimension only, the
# opposite edge anchored. There is no top edge of its own: row wr is the
# title bar over its whole width, so the two top corners stay the only
# way to resize from above a window.
sc, _ = run(ONE, [press(9, 39), drag(9, 50), release(9, 50)])
check("the right edge resizes width alone, the left edge and height "
      "anchored",
      sc.at(6, 50) == "┐" and sc.at(6, 10) == "┌" and
      sc.at(13, 10) == "└", sc)

sc, _ = run(ONE, [press(9, 10), drag(9, 2), release(9, 2)])
check("the left edge resizes width from that side instead, the right "
      "edge anchored",
      sc.at(9, 2) == "│" and sc.at(6, 39) == "┐" and
      sc.at(13, 39) == "◢", sc)

sc, _ = run(ONE, [press(13, 20), drag(18, 20), release(18, 20)])
check("the bottom edge resizes height alone, the top and width anchored",
      sc.at(18, 10) == "└" and sc.at(6, 10) == "┌" and
      sc.at(6, 39) == "┐", sc)

sc, _ = run(ONE, [press(9, 39), drag(9, 50), release(9, 50)],
            env={"DT_EDGERESIZE": "0"})
check("DT_EDGERESIZE=0 turns edge resizing off, corners still work",
      sc.at(6, 39) == "┐", sc)

sc, _ = run(FIXED_TL, [press(8, 29), drag(8, 40), release(8, 40)])
check("a fixed window's edges do not resize it either",
      sc.g[6][10] == "┌" and sc.g[6][29] == "┐", sc)

# --- the window menu, and items that cannot be chosen ---------------------

# Where Window sits on the bar: after the app's two menus and Edit when the
# app has focus, after the Finder's four when nothing does.
#   "  ✎  Count  More  Edit  Window"      "  ✎  File  Edit  View  Special  Window"
WIN, WIN0 = 23, 32

sc, _ = run(MENUS, [press(0, WIN)])
check("a window menu is there even for an app with its own menus",
      sc.find("Move") is not None and sc.find("Cycle") is not None, sc)

sc, _ = run(MENUS, [press(6, 37), release(6, 37), press(0, WIN0)])
check("with nothing focused its items lose their letters",
      sc.find("Move") is not None and sc.at(1, 37) != "m", sc)

sc, _ = run(MENUS, [press(6, 37), release(6, 37), press(0, WIN0), b"m"])
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

sc, raw = run(MENUS, [press(0, WIN), b"m"])
check("move says what it is doing", b"moving" in raw, sc)

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

# Any other key ends the mode and goes where it would have, so a mode nobody
# noticed does not swallow what is typed; the arrows after it are ordinary
# again and the window stays put.
sc, _ = run(MENUS, [press(0, WIN), b"m", b"\x1b[A", b"z", b"\x1b[A",
                    b"\x1b[A"])
check("a key the mode has no use for ends it, and the arrows are free again",
      sc.find("┤ Noted ├") == (5, 12), sc)
sc, _ = run(MENUS, [press(0, WIN), b"m", press(20, 70), b"\x1b[A", b"\x1b[A"])
check("and so does a click anywhere",
      sc.find("┤ Noted ├") == (6, 12), sc)

# --- where a new window goes ------------------------------------------------
#
# A launched window goes to the first spot on the display that overlaps
# nothing -- scanning from the top left, below the bar, room left for each
# window's shadow -- and where there is none, to the spot of least overlap.
DA = tree("examples/desktop/desk-accessories")
PLACEAPPS = (". %s/calc.hibr\n. %s/clock.hibr\n. %s/imgview.hibr\n"
             ". %s/puzzle.hibr\nDT_ICONS=0" % (DA, DA, DA, DA))
sc, _ = run("dt_launch calc\ndt_launch clock\ndt_launch imgview", pre=PLACEAPPS)
check("three launched windows land side by side, overlapping nothing",
      sc.find("┤ Calculator ├") == (1, 2) and sc.find("┤ Clock ├") == (1, 28)
      and sc.find("┤ Image Viewer ├") == (8, 42), sc)
sc, _ = run("dt_launch calc\ndt_launch clock\ndt_launch imgview\n"
            "dt_launch puzzle", pre=PLACEAPPS)
p4 = sc.find("┤ Puzzle ├")
check("a fourth with no free spot left still opens whole, where it covers "
      "least", p4 is not None and p4[0] + 12 <= 23, sc)
sc, _ = run("dt_launch clock", pre=PLACEAPPS + "\nDT_PLACE=center")
check("Placement center puts it in the middle of the display",
      sc.find("┤ Clock ├") == (9, 29), sc)
sc, _ = run("dt_launch clock\ndt_launch clock", pre=PLACEAPPS)
check("and a once-app launched again is brought forward, not placed twice",
      len([r for r in range(24) if "┤ Clock ├" in sc.row(r)]) == 1, sc)
TERMLOAD = ("mod load %s\nmod load %s\n. %s/term.hibr\nDT_ICONS=0"
            % (tree("build/mods/pty.so"), tree("build/mods/term.so"),
               tree("examples/desktop/apps")))
sc, _ = run("dt_launch term", pre=TERMLOAD)
check("a terminal opens at 24 by 80 by default, shrunk to fit a smaller "
      "screen", sc.at(1, 0) == "┌" and sc.at(23, 79) == "◢", sc)
sc, _ = run("dt_launch term", pre=TERMLOAD + "\nDT_TERMROWS=10\nDT_TERMCOLS=40"
            "\nDT_TERMBAR=0")
check("and at the size the Terminal pane sets, which fits",
      sc.at(1, 0) == "┌" and sc.at(12, 41) == "◢", sc)

# --- snapping ----------------------------------------------------------------
#
# alt and an arrow puts the focused window on that half of the display, below
# the bar; the same again puts it back. ONE is 8 by 30 at row 6, column 10.
# All four directions are on alt since 0.99.90, where left and right were on
# ctrl-alt from 0.99.26 -- which left the four split across two modifiers, with
# no rule to explain which was which. ctrl-alt and an arrow is the workspaces'.
AL, AR, AU, AD = b"\x1b[1;3D", b"\x1b[1;3C", b"\x1b[1;3A", b"\x1b[1;3B"
sc, _ = run(ONE, [AL])
check("alt-left snaps the window to the left half",
      sc.at(1, 0) == "┌" and sc.at(23, 39) == "◢", sc)
sc, _ = run(ONE, [AR])
check("alt-right to the right half",
      sc.at(1, 40) == "┌" and sc.at(23, 79) == "◢", sc)
sc, _ = run(ONE, [AU])
check("alt-up to the top half",
      sc.at(1, 0) == "┌" and sc.at(11, 79) == "◢", sc)
sc, _ = run(ONE, [AD])
check("alt-down to the bottom half",
      sc.at(12, 0) == "┌" and sc.at(23, 79) == "◢", sc)
sc, _ = run(ONE, [AL, AL])
check("and the same snap again puts it back where it was",
      sc.find("┤ Hello ├") == (6, 12) and sc.at(13, 39) == "◢", sc)
sc, _ = run(ONE, [AL, AR, AR])
check("back where it was before the first snap, not the one before",
      sc.find("┤ Hello ├") == (6, 12), sc)
sc, raw = run(FIXED, [AL])
check("a fixed window keeps its size", sc.find("┤ Fixed ├") is not None and
      b"keeps its size" in raw, sc)

# The desktop draws with the theme's colour roles, not with midnight's
# values written into the code: a close button is the theme's "bad".
sc, _ = run(ONE, pre='DT_BAD="#c53030"')
xc = sc.row(6).find("x├")
check("the close button takes the theme's colour for bad",
      xc > 0 and sc.style(6, xc)["fg"] == "#c53030", sc)

# --- workspaces --------------------------------------------------------------
#
# Three by default, for the whole desktop. Switching hides one workspace's
# windows the way minimising does and shows the other's as they were
# stacked; the bar shows the numbers, a click switches, a window dragged by
# its title onto a number goes there.
WS2 = 'dt_new "Under" 8 30 6 10\ndt_new "Over" 8 30 9 25\n'
sc, _ = run(WS2, [b"\x1b2"])
check("alt-2 switches to an empty workspace: no window is drawn",
      sc.find("Under") is None and sc.find("Over") is None, sc)
w2 = sc.row(0).find("1 2 3")
check("and the bar shows the three workspaces", w2 > 0, sc)
sc, _ = run(WS2)
w2 = sc.row(0).find("1 2 3")
sc, _ = run(WS2, [b"\x1b2", b"\x1b1"])
check("alt-1 comes back to both, stacked as they were",
      sc.find("┤ Under ├") == (6, 12) and sc.at(9, 25) == "┌", sc)
sc, _ = run(WS2, [b"\x1b[1;7C"])
check("ctrl-alt-right goes to the next workspace",
      sc.find("Under") is None and sc.find("Over") is None, sc)
sc, _ = run(WS2, [b"\x1b[1;7C", b"\x1b[1;7D"])
check("and ctrl-alt-left back to the one before",
      sc.find("┤ Under ├") == (6, 12) and sc.at(9, 25) == "┌", sc)
sc, _ = run(WS2, [press(0, w2 + 2)] if w2 > 0 else [])
check("clicking a number on the bar switches to it",
      sc.find("Under") is None, sc)
sc, raw = run(WS2 + "dt_wsmove 2 3\n", [])
check("a window sent to another workspace leaves this one",
      sc.find("Over") is None and sc.find("┤ Under ├") == (6, 12), sc)
sc, _ = run(WS2 + "dt_wsmove 2 3\n", [b"\x1b3"])
check("and is there on its own", sc.find("┤ Over ├") == (9, 27) and
      sc.find("Under") is None, sc)
sc, _ = run(WS2, [press(9, 35), drag(0, w2 + 4), release(0, w2 + 4)]
            if w2 > 0 else [])
check("a window dragged by its title onto a number goes to that workspace",
      sc.find("Over") is None and sc.find("┤ Under ├") == (6, 12), sc)
sc, _ = run(WS2, [press(9, 35), drag(0, w2 + 4), release(0, w2 + 4),
                  b"\x1b3"] if w2 > 0 else [])
check("where it is, as it was before the drag",
      sc.find("┤ Over ├") == (9, 27), sc)
sc, _ = run("dt_launch calc\ndt_wsgo 2\ndt_launch calc", pre=PLACEAPPS)
check("launching a once-app open on another workspace goes there, not a "
      "second copy", sc.find("┤ Calculator ├") is not None and
      sc.row(0).find("1 2 3") > 0, sc)
sc, _ = run("dt_launch calc\ndt_wsgo 2\ndt_launch clock", pre=PLACEAPPS)
check("a new window is placed as if the other workspaces' were not there",
      sc.find("┤ Clock ├") == (1, 2), sc)
sc, _ = run(WS2 + "dt_wsmove 2 3\ndt_wsset 2\n", [b"\x1b2"])
check("fewer workspaces: windows on one that went move to the last left",
      sc.find("┤ Over ├") == (9, 27) and "1 2 3" not in sc.row(0), sc)

# A window with no chrome: no border or title, moved by dragging its body;
# a double click and a right click are its app's.
TINY = ('tinyc_draw() { console put -p "w$1" 0 0 TINY; }\n'
        'tinyc_click() { [ "$4" = double ] && dt_notep low doubled; return 0; }\n'
        'tinyc_context() { dt_menu Tiny; dt_item Ping p dt_notep low pinged; }\n'
        'tid := dt_new Tiny 6 20 6 10 tinyc\nDT[$tid]["chrome"]=none\n')
sc, _ = run(TINY)
check("a window with no chrome is only what its app draws", sc.find("Tiny") is None
      and sc.find("TINY") == (6, 10), sc)
sc, _ = run(TINY, [press(7, 12), drag(10, 20), release(10, 20)])
check("and a drag anywhere on it moves it", sc.find("TINY") == (9, 18), sc)
sc, raw = run(TINY, [press(7, 12), release(7, 12), press(7, 12), release(7, 12)])
check("a double click is told to its app", b"doubled" in raw, sc)
sc, _ = run(TINY, [press(7, 12, 2)])
check("and a right click opens its app's own menu", sc.find("Ping") is not None, sc)

# The control socket of an unheld desktop, asked for with DT_CTL: it
# answers, it is back after Restart Desktop, and it is gone after Quit.
CTD = tempfile.mkdtemp(prefix="hibr-ctl-")
CTS = os.path.join(CTD, "d.ctl")
cpath = os.path.join(CTD, "s.hibr")
open(cpath, "w").write("%s. %s\nDT_CTL=1\nDT_CTLPATH=%s\ndt_open\n"
                       "[ \"$DT_RESTORED\" = 1 ] || dt_new One 8 30 4 10\ndt_run\ndt_close\n"
                       % (load(MOD), WM, CTS))
tc = Term(cpath, rows=ROWS, cols=COLS, settle=1.0)


def uctl(*a):
    r = subprocess.run([screen.HIBR, tree("examples/desktop/lib/ctl.hibr"), "--socket", CTS] + list(a),
                       capture_output=True, text=True, timeout=10)
    try:
        return r.returncode, json.loads(r.stdout)
    except ValueError:
        return r.returncode, {"raw": r.stdout + r.stderr}


rc, j = uctl("windows")
check("an unheld desktop with DT_CTL answers on its control socket",
      rc == 0 and [w["title"] for w in j.get("windows", [])] == ["One"], j)
tc.keys([b"\x1b[21~", b"r"])
tc.collect(2.5)
rc, j = uctl("windows")
check("and answers again after Restart Desktop, its windows kept",
      rc == 0 and [w["title"] for w in j.get("windows", [])] == ["One"], j)
tc.quit(b"qy", 1.2)
check("and its socket is gone after Quit", not os.path.exists(CTS), os.listdir(CTD))
shutil.rmtree(CTD, True)

# A sticky window is on every workspace; taken off them, it stays on the
# one it is seen on; sent to another, it is no longer on every one.
sc, _ = run(WS2 + "dt_sticky 2 1\n", [b"\x1b2"])
check("a window on every workspace is still there on another",
      sc.find("┤ Over ├") == (9, 27) and sc.find("Under") is None, sc)
sc, _ = run(WS2 + "dt_sticky 2 1\n", [b"\x1b2", b"\x1b3", b"\x1b1"])
check("and back on the first, with the rest", sc.find("┤ Over ├") == (9, 27)
      and sc.find("┤ Under ├") == (6, 12), sc)
sc, _ = run(WS2 + "dt_sticky 2 1\ndt_wsgo 2\ndt_sticky 2 0\ndt_wsgo 1\n")
check("taken off every workspace, it stays on the one it was seen on",
      sc.find("Over") is None and sc.find("┤ Under ├") == (6, 12), sc)
sc, _ = run(WS2 + "dt_sticky 2 1\ndt_wsmove 2 3\n", [b"\x1b2"])
check("and sent to one workspace, it is on that one only", sc.find("Over") is None, sc)
sc, _ = run(WS2, [b"\x1b[21~", b"\x1b[C", b"\x1b[C", b"\x1b[C", b"e", b"\x1b2"])
check("the Window menu's On Every Workspace makes it so",
      sc.find("┤ Over ├") == (9, 27), sc)

sc, _ = run(WS2, [wheel(20, 5, up=False)])
check("the wheel over the bare desktop goes to the next workspace",
      sc.find("Under") is None and sc.find("Over") is None, sc)
sc, _ = run(WS2, [wheel(20, 5, up=False), 0.4, wheel(20, 5, up=True)])
check("and back up to the previous one",
      sc.find("┤ Under ├") == (6, 12), sc)
sc, _ = run(WS2, [wheel(20, 5, up=False)] * 3)
check("a burst of wheel reports is one step, not one per report",
      sc.find("Under") is None and sc.row(0).find("1 2 3") > 0, sc)
sc, _ = run(WS2, [wheel(20, 5, up=True)])
check("up from the first wraps round to the last", sc.find("Under") is None,
      sc)
sc, _ = run(WS2, [press(9, 35, 2)])
check("a title bar's menu can send the window to another workspace",
      sc.find("Move to Workspace") is not None, sc)
check("and offers no other display when there is only this one",
      sc.find("Move to Display") is None, sc)
sc, _ = run(WS2, [press(9, 35, 2)], pre="DT_WSN=1")
check("with one workspace there is nothing to move it to",
      sc.find("Move to Workspace") is None, sc)
sc, _ = run(WS2, [wheel(20, 5, up=False)], pre="DT_WSN=1")
check("and the bar shows no numbers, and the wheel stays put",
      not re.search(r"\b1 2\b", sc.row(0)) and
      sc.find("┤ Under ├") == (6, 12), sc)

# Cycle Windows On: this workspace by default, every one when asked.
# "Under" stays on 1, "Over" goes to 3; focus is on Under.
CYC = WS2 + "dt_wsmove 2 3\ndt_raise 1\n"
sc, _ = run(CYC, [b"\x1b\t"])
check("by default alt-tab stays on this workspace",
      sc.find("┤ Under ├") == (6, 12) and sc.find("Over") is None, sc)
sc, _ = run(CYC, [b"\x1b\t"], pre="DT_CYCLE=all")
check("set to all, it goes to the next window on another workspace",
      sc.find("┤ Over ├") == (9, 27) and sc.find("Under") is None, sc)
sc, _ = run(CYC, [b"\x1b\t", b"\x1b\t"], pre="DT_CYCLE=all")
check("and round again to where it started",
      sc.find("┤ Under ├") == (6, 12) and sc.find("Over") is None, sc)
sc, _ = run(CYC + "dt_min 1\n", [b"\x1b\t"], pre="DT_CYCLE=all")
check("a minimised window is passed over, as on one workspace",
      sc.find("┤ Over ├") == (9, 27) and sc.find("Under") is None, sc)

# Screenshots: ctrl-alt-g asks Screen, Window or Area, the last one taken
# already chosen; what is written is the screen's cells, ANSI by default.
SHOTD = tempfile.mkdtemp(prefix="hibr-shots-")
SHOTPRE = "DT_SHOTDIR=%s\n" % SHOTD
SHOTK = b"\x1b\x07"


def shots():
    return sorted(os.listdir(SHOTD))


def shotclear():
    for f in os.listdir(SHOTD):
        os.unlink(os.path.join(SHOTD, f))


sc, _ = run(WS2, [SHOTK], pre=SHOTPRE)
check("the Screenshot shortcut asks what to take",
      sc.find("Screenshot") is not None and sc.find("Screen") is not None
      and sc.find("Window") is not None and sc.find("Area") is not None, sc)
sc, _ = run(WS2, [SHOTK, b"\x1b"], pre=SHOTPRE)
check("escape cancels it and writes nothing",
      sc.find("enter takes it") is None and shots() == [], sc)
sc, raw = run(WS2, [SHOTK, b"\r"], pre=SHOTPRE)
f = shots()
txt = open(os.path.join(SHOTD, f[0]), encoding="utf-8").read() if f else ""
check("enter takes the whole screen, as ANSI text with its colours",
      len(f) == 1 and f[0].endswith(".ans") and "┤ Over ├" in txt and
      "\x1b[0;" in txt and txt.count("\n") == ROWS and
      "enter takes it" not in txt, txt[:300])
check("and a note says where it went", b"Screenshot saved" in raw, sc)
shotclear()
sc, _ = run(WS2, [SHOTK, b"w"], pre=SHOTPRE)
f = shots()
txt = open(os.path.join(SHOTD, f[0]), encoding="utf-8").read() if f else ""
check("w takes the focused window, its own size",
      len(f) == 1 and txt.count("\n") == 8 and "Over" in txt and
      "Under" not in txt, txt)
shotclear()
sc, _ = run(WS2, [SHOTK, b"a", press(7, 13), drag(9, 20), release(9, 20)],
            pre=SHOTPRE + "DT_SHOTFMT=text\n")
f = shots()
txt = open(os.path.join(SHOTD, f[0]), encoding="utf-8").read() if f else ""
check("an area dragged out is taken, corners included, as plain text",
      len(f) == 1 and f[0].endswith(".txt") and txt.count("\n") == 3 and
      "\x1b" not in txt, repr(txt))
shotclear()
sc, _ = run(WS2, [SHOTK, b"\r"], pre=SHOTPRE + "DT_SHOTFMT=html\n"
            "DT_SHOTLAST=window\n")
f = shots()
txt = open(os.path.join(SHOTD, f[0]), encoding="utf-8").read() if f else ""
check("the chooser starts on the last one taken, and HTML is a page",
      len(f) == 1 and f[0].endswith(".html") and "<span style=" in txt and
      txt.count("\n") < ROWS, txt[:200])
shutil.rmtree(SHOTD, True)

# --- tiling ------------------------------------------------------------------
#
# A tiled workspace lays its windows out itself: the main one on the left,
# the rest stacked down the right, redone as windows come and go.
W3 = ('dt_new "One" 8 30 6 10\ndt_new "Two" 8 30 9 25\n'
      'dt_new "Three" 6 20 3 40\ndt_tiletoggle\n')
sc, _ = run(W3)
check("tiling puts the first window on the left and stacks the rest",
      sc.find("┤ One ├") == (1, 2) and sc.find("┤ Two ├") == (1, 42) and
      sc.find("┤ Three ├") == (12, 42) and sc.at(23, 39) == "◢", sc)
sc, _ = run(W3 + 'dt_new "Four" 5 20 2 2\n')
check("a window opened on a tiled workspace joins the stack",
      sc.find("┤ Four ├") is not None and sc.find("┤ Four ├")[1] == 42 and
      sc.find("┤ Two ├") == (1, 42), sc)
sc, _ = run(W3 + "dt_del 2\n")
check("closing one lays out the rest again",
      sc.find("┤ Two ├") is None and sc.find("┤ Three ├") == (1, 42) and
      sc.at(23, 79) == "◢", sc)
sc, _ = run(W3 + "dt_raise 3\ndt_tilemain\n")
check("Make Main puts the focused window on the left",
      sc.find("┤ Three ├") == (1, 2) and sc.find("┤ One ├") == (1, 42), sc)
sc, _ = run(W3, [press(1, 20), drag(15, 60), release(15, 60)])
check("a tiled window dragged onto another swaps places with it",
      sc.find("┤ Three ├") == (1, 2) and sc.find("┤ One ├") == (12, 42), sc)
sc, _ = run(W3, [press(1, 20), drag(5, 30), release(5, 30)])
check("and let go anywhere else goes back to its place",
      sc.find("┤ One ├") == (1, 2), sc)
sc, raw = run(W3, [press(23, 39), drag(18, 30), release(18, 30)])
check("resizing a tiled window by hand is refused, and says why",
      sc.at(23, 39) == "◢" and b"the layout sizes this window" in raw, sc)
sc, _ = run(W3 + "dt_tiletoggle\n")
check("turned off, the windows stay where the layout put them",
      sc.find("┤ One ├") == (1, 2) and sc.find("┤ Three ├") == (12, 42), sc)
sc, _ = run(W3 + 'dt_wsgo 2\ndt_new "Free" 8 30 6 10\n')
check("tiling is a workspace's own: another one's windows float",
      sc.find("┤ Free ├") == (6, 12), sc)
sc, _ = run(W3, [b"\x1b[21~", b"\x1b[C", b"\x1b[C", b"\x1b[C"])
check("the Window menu shows Tile Workspace ticked, and Make Main",
      sc.find("Tile Workspace") is not None and
      sc.find("Make Main") is not None, sc)

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

# A Mac has no /proc/mounts; its volumes are directories in /Volumes, the
# boot disk a link to /. Faked the same way: DT_MOUNTS somewhere unreadable,
# DT_VOLUMES a directory of our own.
vols = os.path.join(d, "Volumes")
os.makedirs(os.path.join(vols, "USB"))
os.symlink("/", os.path.join(vols, "Macintosh HD"))
menv = dict(env, DT_MOUNTS=os.path.join(d, "none"), DT_VOLUMES=vols)
sc, raw = run("", env=menv, pre=pre)
check("without /proc/mounts, the disks are the volumes in /Volumes",
      sc.find("Macintosh") is not None and sc.find("USB") is not None, sc)
mac = sc.find("Macintosh")
sc, raw = run("", feed=[press(mac[0] - 1, mac[1]), press(mac[0] - 1, mac[1])],
              env=menv, pre=pre)
check("and the boot disk, a link to /, opens / itself",
      sc.find("┤ Files [/] ├") is not None, sc)

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
path = scratch("resize-icon.hibr")
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
check("the menu bar follows the terminal's width: its right end is at the new edge",
      0 <= sc.row(0).find("▾") < 40, sc.row(0))
t.resize(ROWS, COLS)
t.send(b"", settle=0.6)
sc = t.screen()
check("and when it widens again, out to the far edge -- a reattach is a resize too",
      sc.row(0).rfind("▾") >= COLS - 5, sc.row(0))
t.quit(b"qy", 1.0)
t.close()
os.unlink(path)
shutil.rmtree(d, True)

d, env, pre, home, backup, usb, src = desk()
sc, raw = run("", feed=[press(3, 70), drag(10, 74), drag(14, 68),
                        release(14, 68), b"\x1b[21~"] + [b"\x1b[C"] * 4 + [b"u"], env=env,
              pre=pre)
check("Clean Up Desktop on the Special menu puts them back at their defaults",
      sc.find("Home") == (3, 71), sc)
shutil.rmtree(d, True)

# With nothing focused the desktop is the Finder: File, Edit, View and
# Special, as on System 7. Special > Empty Trash asks, then deletes for good.
d, env, pre, home, backup, usb, src = desk()
os.makedirs(os.path.join(d, "trash", "files"))
os.makedirs(os.path.join(d, "trash", "info"))
open(os.path.join(d, "trash", "files", "old.txt"), "w").write("x\n")
open(os.path.join(d, "trash", "info", "old.txt.trashinfo"), "w").write("[Trash Info]\n")
sc, raw = run("", env=env, pre=pre)
check("with nothing focused the menu bar reads File, Edit, View, Special, Window",
      "\u270e  File  Edit  View  Special  Window" in sc.row(0), sc)
epath = "/tmp/hibr-desktop-et-%d.hibr" % os.getpid()
open(epath, "w").write("%s. %s\n%s\ndt_open\ndt_run\ndt_close\n" % (load(MOD), WM, pre))
t = Term(epath, env=dict({"DT_TICK": "60"}, **env), rows=ROWS, cols=COLS, settle=0.5)
t.keys([b"\x1b[21~"] + [b"\x1b[C"] * 4 + [b"t"])
sc = t.screen()
t.quit(b"\x1bqy", 1.2)
os.unlink(epath)
check("Empty Trash asks first, saying how much goes, and no leaves it be",
      sc.find("Empty the Trash?") is not None and sc.find("one item") is not None
      and os.listdir(os.path.join(d, "trash", "files")) == ["old.txt"], sc)
sc, raw = run("", feed=[b"\x1b[21~"] + [b"\x1b[C"] * 4 + [b"t", b"y"], env=env, pre=pre)
check("and yes empties it", os.listdir(os.path.join(d, "trash", "files")) == []
      and os.listdir(os.path.join(d, "trash", "info")) == [], os.listdir(os.path.join(d, "trash", "files")))
shutil.rmtree(d, True)

# The hibr menu carries one About and it belongs to whatever is in front: the
# app's own with an app focused, the computer's with nothing. Both at once is
# what it used to show, and no other menu works that way -- on a Mac the first
# item becomes the application's (Gitea #119). With no _about of its own an app
# gets the desktop's card, made from what it declares.
sc, raw = run('dt_new Files 12 40 3 4 files', feed=[b"\x1b[21~"], pre=APPS)
check("with an app in front the hibr menu's About is that app's, and only that",
      sc.find("About Files") is not None
      and sc.find("About This Computer") is None, sc)
sc, raw = run("", feed=[b"\x1b[21~"], pre=APPS)
check("and with nothing in front it is the computer's",
      sc.find("About This Computer") is not None
      and sc.find("About Files") is None, sc)
# The About window is itself an app, and the one whose About *is* the
# computer's: a template applied to its own title read "About About This
# Computer…" until it was routed to the second branch.
sc, raw = run('dt_new "About This Computer" 14 44 8 20 about',
              feed=[b"\x1b[21~"], pre=APPS)
check("and the About window names itself once, not twice",
      sc.find("About About") is None
      and sc.find("About This Computer…") is not None, sc)

# tests/540-examples.t holds the File, Edit rule for all twenty-three apps
# that declare menus, by reading their source; this is the one check that
# the window manager really draws them in that order (Gitea #120).
sc, raw = run('dt_new "Task Manager" 14 60 4 8 tasks', pre=APPS)
bar = sc.text().splitlines()[0]
bf, be, bt = bar.find("File"), bar.find("Edit"), bar.find("Task")
check("an app's own menus come after File and Edit, not before",
      bf >= 0 and bf < be < bt, sc)
sc, raw = run('dt_new Files 12 40 3 4 files', feed=[b"\x1b[21~", b"a"], pre=APPS)
check("and it opens a card of the app's name, icon, description and the shell's version",
      sc.find("About Files") is not None and sc.find("A file browser") is not None
      and sc.find("hibr ") is not None and sc.find("files.hibr") is not None, sc)

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
      sc.find("Screen Saver") is not None and sc.status == 0, sc)
check("and Detach is on the hibr menu, dimmed when nothing holds it",
      sc.find("Detach") is not None, sc)

# Quit asks first: q used to end it on the spot, and one key too many
# landed there more than once.
path = scratch("desktop-quit.hibr")
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

# Yes and No on the confirm box are buttons: clickable, not just typeable.
path = scratch("desktop-confirmclick.hibr")
open(path, "w").write("%s. %s\ndt_open\n%s\ndt_run\ndt_close\n"
                      % (load(MOD), WM, ONE))
t = Term(path, env={"DT_TICK": "60"}, rows=ROWS, cols=COLS, settle=0.5)
t.send(b"q", settle=0.4)
sc = t.screen()
yes_pos = sc.find(" Yes ")
check("the confirm box's answers are buttons, each with a shadow under it",
      yes_pos is not None and sc.find(" No ") is not None and
      sc.at(yes_pos[0] + 1, yes_pos[1] + 2) == "▀", sc)
no_pos = sc.find(" No ")
t.send(press(no_pos[0], no_pos[1] + 1), settle=0.4)
sc = t.screen()
check("clicking No cancels the confirm box",
      sc.find("Quit hibr?") is None and not t.exited, sc)
t.send(b"q", settle=0.4)
sc = t.screen()
yes_pos = sc.find(" Yes ")
t.send(press(yes_pos[0], yes_pos[1] + 1), settle=0.4)
t.quit(None, 1.0)
check("and clicking Yes quits", t.exited, t.raw)
os.unlink(path)

# The keyboard moves between them: focus starts on Yes, so enter still
# means yes; tab or an arrow moves it to No, where enter cancels.
path = scratch("desktop-confirmkeys.hibr")
open(path, "w").write("%s. %s\ndt_open\n%s\ndt_run\ndt_close\n"
                      % (load(MOD), WM, ONE))
t = Term(path, env={"DT_TICK": "60"}, rows=ROWS, cols=COLS, settle=0.5)
t.send(b"q", settle=0.3)
t.send(b"\t", settle=0.3)
t.send(b"\r", settle=0.4)
sc = t.screen()
check("tab moves focus to No, and enter there cancels",
      sc.find("Quit hibr?") is None and not t.exited, sc)
t.send(b"q", settle=0.3)
t.send(b"\x1b[C", settle=0.3)
t.send(b"\x1b[D", settle=0.3)
t.quit(b"\r", 1.0)
check("right then left comes back to Yes, and enter quits", t.exited, t.raw)
os.unlink(path)

# The shadow is a setting of its own, on by default.
path = scratch("desktop-confirmflat.hibr")
open(path, "w").write("%s. %s\nDT_BTNSHADOW=0\ndt_open\n%s\ndt_run\n"
                      "dt_close\n" % (load(MOD), WM, ONE))
t = Term(path, env={"DT_TICK": "60"}, rows=ROWS, cols=COLS, settle=0.5)
t.send(b"q", settle=0.4)
sc = t.screen()
yes_pos = sc.find(" Yes ")
check("DT_BTNSHADOW=0 draws the buttons flat",
      yes_pos is not None and "▀" not in sc.row(yes_pos[0] + 1) and
      "▄" not in sc.row(yes_pos[0]), sc)
t.quit(b"y", 1.0)
os.unlink(path)

# Brackets draw each button as [label] on the face, the focused one in the
# accent, no fill; the width is the same, so nothing else moves.
path = scratch("desktop-confirmbrackets.hibr")
open(path, "w").write("%s. %s\nDT_DLGBTN=brackets\nDT_BTNSHADOW=0\n"
                      "dt_open\n%s\ndt_run\ndt_close\n" % (load(MOD), WM, ONE))
t = Term(path, env={"DT_TICK": "60"}, rows=ROWS, cols=COLS, settle=0.5)
t.send(b"q", settle=0.4)
sc = t.screen()
check("DT_DLGBTN=brackets draws [Yes] and [No]",
      sc.find("[Yes]") is not None and sc.find("[No]") is not None, sc)
t.quit(b"y", 1.0)
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
# Task Manager, not one of the games, as the third fixed point: the games
# moved into their own Games subfolder (a submenu, not a flat entry) once
# there were three of them worth grouping, so a bundled app that is still
# flat is what a sort-order check needs to stay meaningful.
menu = [sc.row(r)[:20] for r in range(2, 16)]
rows = [m for m in menu
        if "Hello" in m or "Files" in m or "Task Manager" in m]
check("an app in your own folder is on the menu, in its sorted place",
      len(rows) == 3 and "Files" in rows[0] and "Hello" in rows[1] and
      "Task Manager" in rows[2], sc)
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
# them -- "games" belongs between "Files" and "Task Manager", not at the
# end. A synthetic "games" folder here, not the bundled Games one -- this
# checks the general sort, not that specific folder's own existence.
GCONF = tempfile.mkdtemp(prefix="hibr-apps-games-")
os.makedirs(os.path.join(GCONF, "hibr", "apps", "games"))
open(os.path.join(GCONF, "hibr", "apps", "games", "pong.hibr"), "w").write(
    'dt_app pong "Pong" 6 20\n')
sc, _ = run("", feed=[b"\x1b[21~"], env={"XDG_CONFIG_HOME": GCONF}, pre=APPS)
menu = [sc.row(r)[:20] for r in range(2, 17)]
rows = [m for m in menu
        if "Files" in m or "games" in m or "Task Manager" in m]
check("a folder is interleaved by name, not appended after every app",
      len(rows) == 3 and "Files" in rows[0] and "games" in rows[1] and
      "Task Manager" in rows[2], sc)
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
check("which lists Stickies and Puzzle rather than folding them in flat",
      sc2.find("Stickies") is not None and sc2.find("Puzzle") is not None,
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
STRIPROW = row

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

# The strip draws at absolute coordinates, after every window, so it is drawn
# over them -- and a window whose app offers no _dirty is redrawn every frame,
# which wipes the strip's cells. Gated, it put them back at most once a second:
# with one window under it the strip was there in 5 samples of 70, flashing
# back about every 620 ms, reported as "it blinks, and does not refresh
# properly" (Gitea #125). It draws every frame now.
#
# What this needs that a settled screenshot cannot give: frames happening
# continuously. An idle desktop barely draws, and the few frames it does are
# the wallpaper's own, which the old gate redrew on -- so the first probe for
# this, with the window there but nothing asking for frames, showed one state
# across every sample and looked perfectly healthy. The app asks for the next
# frame the way a focused terminal with a blinking cursor does.
CSWIN = ('spin_open() { return 0; }\n'
         'spin_draw() { console put -p "w$1" 1 1 "SPIN"; dt_want 30; return 0; }\n'
         'dt_app spin "Spin" 10 60 "" "" "" "" ""\n')
d = tempfile.mkdtemp(prefix="hibr-stripover-")
p = scratch("strip-over")
open(p, "w").write("%s. %s\n%s%s\ndt_open\ndt_new \"Spin\" 10 60 %d 2 spin\n"
                   "dt_run\ndt_close\n"
                   % (load(MOD), WM, CSSRC, CSWIN, STRIPROW - 5))
t = Term(p, env={"DT_TICK": "60", "XDG_CONFIG_HOME": d}, rows=ROWS, cols=COLS,
         settle=1.0)
rows, pens = [], []
for _ in range(30):
    t.collect(0.04)
    s = t.screen()
    rows.append(s.row(STRIPROW)[0:len(LABELS)])
    pens.append(s.p[STRIPROW + 1][2])
t.quit(b"qy", 1.0)
os.unlink(p)
shutil.rmtree(d, True)
bad = [i for i, r in enumerate(rows) if r != LABELS]
check("a window over the strip does not take it off the screen between frames",
      not bad, "%d of %d samples missing it: %r" % (len(bad), len(rows),
                                                   rows[bad[0]] if bad else ""))
# Its shadow goes with it: console darken -s marks each cell it shades and any
# ordinary write clears that mark, so a strip that skipped a frame lost the
# shadow too. Drawn every frame, it is re-cast every frame -- and -s is what
# stops a shadow cast over a window's own face from darkening twice.
check("and its shadow is cast on every one of them, never twice over",
      len(set(pens)) == 1, set(pens))

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

sc, raw = run("", env={"DT_WALLIMG": tree("tests/img-quad.jpg")}, pre=IMGMOD)
check("and so can a JPEG",
      sc.style(4, 10)["bg"] and sc.style(4, 10)["bg"].startswith("#f") and
      sc.style(20, 60)["bg"] and sc.style(20, 60)["bg"].startswith("#f"),
      (sc.style(4, 10), sc.style(20, 60), sc.dump()))

expect(r"^cannot open /does/not/exist\.png$")
sc, raw = run("", env={"DT_WALLIMG": "/does/not/exist.png"}, pre=IMGMOD)
check("an unusable wallpaper image falls back to the glyph instead",
      sc.row(1)[0:1] == "·", sc)

# DT_WALLMODE: stretch (the default, checked above already) ignores the
# image's own shape; scale fits it in keeping that shape, letterboxed;
# zoom fills the screen keeping it, cropped; center is its own native
# size, unscaled. img-wide.png is 40x4 -- very wide and short, so the
# three differ obviously against a 24x80 screen: scale letterboxes top
# and bottom, zoom covers every cell with no glyph left showing anywhere,
# and center sits at exactly 40 columns by 2 rows (sh/2, img draw's own
# half-block doubling), centred.
WIDEFIX = tree("tests/img-wide.png")

sc, raw = run("", env={"DT_WALLIMG": WIDEFIX, "DT_WALLMODE": "scale"},
              pre=IMGMOD)
check("scale fits the image within the screen, letterboxed",
      "·" in sc.row(1) and "·" not in sc.row(11) and
      sc.at(11, 0) != "·" and sc.at(11, 79) != "·", sc)

sc, raw = run("", env={"DT_WALLIMG": WIDEFIX, "DT_WALLMODE": "zoom"},
              pre=IMGMOD)
check("zoom fills the screen, cropped, with no glyph left showing",
      sc.text().count("·") == 0, sc)

sc, raw = run("", env={"DT_WALLIMG": WIDEFIX, "DT_WALLMODE": "center"},
              pre=IMGMOD)
check("center sits at the image's own native size, unscaled, centred",
      sc.at(11, 19) == "·" and sc.at(11, 20) != "·" and
      sc.at(11, 59) != "·" and sc.at(11, 60) == "·" and
      sc.at(10, 20) == "·" and sc.at(13, 20) == "·", sc)

sc, raw = run('dt_launch imgview "%s"' % IMGFIX, pre=IMGMOD + DASRC)
check("the image viewer opens with a picture and draws it",
      sc.find("Image Viewer") is not None and
      b"38;2;255;0;0" in raw and b"48;2;0;0;255" in raw, sc)

sc, raw = run("dt_launch imgview", pre=IMGMOD + DASRC)
check("opened with no picture, it says so instead of showing nothing",
      sc.find("Drop a picture here") is not None, sc)

# By the keyboard rather than by a column worked out from the bar: the order
# is File, Edit, the app's own, Window in every app and tests/540-examples.t
# holds it, so three rights is the app's own menu wherever its title falls
# (Gitea #120).
WCONF = tempfile.mkdtemp(prefix="hibr-imgview-")
sc, raw = run('dt_launch imgview "%s"' % IMGFIX,
              feed=[b"\x1b[21~", b"\x1b[C", b"\x1b[C", b"\x1b[C", b"w"],
              env={"XDG_CONFIG_HOME": WCONF}, pre=IMGMOD + DASRC)
saved = os.path.join(WCONF, "hibr", "desktop.hibr")
text = open(saved).read() if os.path.exists(saved) else ""
check("Set as Wallpaper on its own Image menu sets and saves DT_WALLIMG",
      ("DT_WALLIMG=%s" % IMGFIX) in text, text)
shutil.rmtree(WCONF, True)

LAUNCH = MENU + [b"c"]
sc, raw = run("", feed=LAUNCH + LAUNCH, pre=APPS)
check("an app declared hidden is not offered on the menu -- Mail's New Message",
      sc.text().count("┤ Control Panel ├") == 1 and "┤ New Message ├" not in sc.text(), sc)
# 'c' launches Control Panel, the first app in examples/desktop/apps whose
# name starts with it now that Calendar and Contacts are desk accessories;
# it is declared `once`. This check was never about which app it launches,
# only that a `once` one opens no more than a single window.
check("an app declared once opens one window, however often launched",
      sc.text().count("┤ Control Panel ├") == 1, sc)
LAUNCH = MENU + [b"f"]
sc, raw = run("", feed=LAUNCH + LAUNCH, pre=APPS)
check("and one that is not opens another window each time",
      sc.text().count("┤ Files [") == 2, sc)

sc, _ = run("", feed=[press(0, 2), b"a"], pre=APPS)
check("About This Computer opens a window with the machine's own numbers",
      sc.find("┤ About This Computer ├") is not None and
      sc.find("CPU") is not None and sc.find("MEM") is not None and
      sc.find("%") is not None, sc)
check("and it has no maximise button, being a fixed size",
      sc.find("┤_ x├") is not None, sc)
dpos = sc.find("hibr desktop v")
check("and shows the desktop's own version above hibr's, not just hibr's",
      dpos is not None and "hibr v" in sc.row(dpos[0] + 1), sc)
# And a section for what it is drawing on, which is the other half of what
# decides what the desktop can do (Gitea #121). Each line is something the
# desktop can ask rather than guess -- the harness's terminal says nothing
# about a cell, so there are no pixels to report and it says so.
check("and a This Terminal section: size, pictures, colour, mouse and hold",
      sc.find("This Terminal: ") is not None and
      sc.find("Size: 24 x 80 cells") is not None and
      sc.find("Pictures: blocks only") is not None and
      sc.find("Colour: 24-bit sent") is not None and
      sc.find("Mouse: clicks and drags") is not None and
      sc.find("Held: no") is not None, sc)
check("and invents no pixels when the terminal has not said what a cell is",
      "0 x 0" not in sc.text() and "0 by 0" not in sc.text(), sc)

# Clock is a desk accessory now, not in examples/desktop/apps -- the bar's own
# click handler only asks dt_has clock_draw, so it works regardless of
# which loader found it, but the test has to load it from where it is.
sc, _ = run("", feed=[press(0, 63)],
            pre=APPS + 'DA_DIRS+=("%s")\nda_apps\n' % tree("examples/desktop/desk-accessories"))
check("clicking the clock in the bar opens the Clock app",
      sc.find("┤ Clock ├") is not None, sc)

# The Window menu's own Zoom, Hide and Close, from a title bar's right-click:
# the same items the menu bar's Window menu holds, and letters choose them
# once it is open.
sc, _ = run(ONE, [press(6, 20, 2), b"z"])
top = sc.find("┤ Hello ├")
check("Zoom from the Window menu fills the screen with the window",
      top is not None and top[0] < 6 and sc.g[6][10] != "┌", sc)
sc, _ = run(ONE, [press(6, 20, 2), b"h"])
check("Hide from it hides the window",
      sc.find("┤ Hello ├") is None, sc)
sc, _ = run(ONE, [press(6, 20, 2), b"w"])
check("Close from it closes the window",
      sc.find("┤ Hello ├") is None, sc)

# The strip's Theme and Wallpaper modules each open a list of choices and
# apply the one taken, saved like any other setting. Theme needs the
# Appearance pane loaded, since the themes are its.
def stripdrop(col):
    d = tempfile.mkdtemp(prefix="hibr-strip-")
    sc, _ = run("", feed=[press(STRIPROW, col), b"\x1b[B", b"\r"],
                env={"XDG_CONFIG_HOME": d},
                pre=CSSRC + "\n. %s\n" % tree(
                    "examples/desktop/control-panel/appearance.hibr"))
    saved = os.path.join(d, "hibr", "desktop.hibr")
    text = open(saved).read() if os.path.exists(saved) else ""
    shutil.rmtree(d, True)
    return sc, text


sc, saved = stripdrop(17)
check("the strip's Theme module applies the theme chosen from its list",
      "CP_THEME=construction" in saved, sc)
sc, saved = stripdrop(25)
check("its Wallpaper module applies the glyph chosen from its list",
      re.search(r"DT_GLYPH=.?░", saved) is not None, sc)

# The same three from the menu bar's own Window menu, the last one: F10,
# then left past the application menu to reach it.
WMENU = [b"\x1b[21~", b"\x1b[D", b"\x1b[D"]
sc, _ = run(ONE, WMENU + [b"z"])
top = sc.find("┤ Hello ├")
check("the menu bar's Window menu zooms the focused window",
      top is not None and top[0] < 6, sc)
sc, _ = run(ONE, WMENU + [b"h"])
check("hides it", sc.find("┤ Hello ├") is None, sc)
sc, _ = run(ONE, WMENU + [b"w"])
check("and closes it", sc.find("┤ Hello ├") is None, sc)

# Next Wallpaper on the desktop's own right-click menu steps to the next
# glyph, so an empty cell of the wallpaper shows something else; Change
# Wallpaper… is the Control Panel's, dimmed without it.
before, _ = run(ONE)
sc, _ = run(ONE, [press(15, 50, 2), b"n"])
check("Next Wallpaper steps the wallpaper to its next glyph",
      sc.at(20, 70) != before.at(20, 70), sc)
sc, raw = run(ONE, [press(15, 50, 2), b"r"])
check("Refresh Desktop draws everything again, and says so, uncounted",
      b"Refreshed" in raw and sc.find("┤ Hello ├") is not None and
      sc.find("⚑1") is None, sc)

# A shift-click on a desktop icon takes the run from the last one clicked,
# which alt-c then copies as their paths.
d, env, pre, home, backup, usb, src = desk()
sc0, _ = run("", env=env, pre=pre)
hp, bp = sc0.find("Home"), sc0.find(os.path.basename(backup))
sc, raw = run("", feed=[press(hp[0] - 1, hp[1] + 1), release(hp[0] - 1, hp[1] + 1),
                        press(bp[0] - 1, bp[1] + 1, 4),
                        release(bp[0] - 1, bp[1] + 1, 4), b"\x1bc"],
              env=env, pre=pre)
check("shift-click selects the run of icons from the last one clicked",
      copied(raw) == home + "\n" + backup, sc)
shutil.rmtree(d, True)

# Control Panel is a pane picker, panes loaded from examples/desktop/control-panel:
# the desktop's own first, then one per app, each group sorted by title.
# tests/apps.py verifies this order directly against cp_panes; ORDER here
# just names it, so a real change to it breaks an assertion instead of a
# silent miscount.
PANEL = ('. %s/panel.hibr\nCP_PANEDIRS+=("%s")\ncp_panes'
         % (tree("examples/desktop/apps"), tree("examples/desktop/control-panel")))
ORDER = ["datetime", "displays", "keyboard", "mouse", "aboutme", "appearance",
         "cliphist", "control_strip", "desktop", "filetypes", "language", "network", "notify",
         "vaultset", "pictures", "prayerset", "screensaver", "shortcuts",
         "windows", "abouthibr", "filesview", "notes", "taskmgr", "terminal", "tube"]
DOWN_APP = [b"\x1b[B"] * ORDER.index("appearance")
DOWN_KB = [b"\x1b[B"] * ORDER.index("shortcuts")

# The bar's notification dot: hollow with nothing unread, filled once a
# note arrives, hollow again once the history has been opened.
def dotrun(session, env=None, pre="", keys=(), cols=COLS):
    d = tempfile.mkdtemp(prefix="hibr-dot-")
    p = os.path.join(d, "session.hibr")
    apps = 'DT_APPDIRS+=("%s")\ndt_apps\n' % tree("examples/desktop/apps")
    cp = 'CP_PANEDIRS+=("%s")\ncp_panes\n' % tree("examples/desktop/control-panel")
    open(p, "w").write("%s. %s\n%s%s%sdt_open\n%s\ndt_run\ndt_close\n"
                       % (load(MOD), WM, apps, cp, pre, session))
    e = {"XDG_CONFIG_HOME": os.path.join(d, "config")}
    e.update(env or {})
    t = Term(p, env=e, rows=ROWS, cols=cols, settle=0.8)
    t.keys(list(keys), settle=0.4)
    return t, d

t, d = dotrun("")
sc = t.screen()
# The icon is ⚑ unless DT_BELLICON says otherwise: one cell, in any font.
check("with nothing unread the bar shows just the icon, left of the clock",
      sc.find("⚑") == (0, COLS - 23), sc)
check("and the bell, the clock and the application menu sit two cells apart, "
      "as the menu titles on the left do",
      re.search(r"⚑  \d\d:\d\d  Desktop ▾", sc.row(0)) is not None and
      re.search(r"✎  File  Edit  View  Special  Window", sc.row(0)) is not None, sc)
t.quit(None, 0.5); shutil.rmtree(d, True)

t, d = dotrun('dt_note "Unread"')
sc = t.screen()
check("a note puts its count beside the bell",
      sc.find("⚑") == (0, COLS - 24) and
      re.search(r"⚑1  \d\d:\d\d", sc.row(0)) is not None, sc)
t.keys([press(0, COLS - 24)], settle=0.6)
sc = t.screen()
check("and opening the history clears the count again",
      sc.find("┤ Notifications ├") is not None and
      re.search(r"⚑  \d\d:\d\d", sc.row(0)) is not None, sc)
t.quit(None, 0.5); shutil.rmtree(d, True)

# Priorities: the desktop's own chatter is low and does not count, a
# program's or an app's note is normal and does; DT_NOTECOUNT moves the line.
t, d = dotrun('dt_notep low "Workspace 9"\ndt_notep low "Copied"')
sc = t.screen()
check("low notes still pop up but leave the count alone",
      sc.find("Workspace 9") is not None and
      re.search(r"⚑  \d\d:\d\d", sc.row(0)) is not None, sc)
t.quit(None, 0.5); shutil.rmtree(d, True)
t, d = dotrun('dt_notep low "Workspace 9"', pre="DT_NOTECOUNT=low\n")
sc = t.screen()
check("counting from low counts them",
      re.search(r"⚑1  \d\d:\d\d", sc.row(0)) is not None, sc)
t.quit(None, 0.5); shutil.rmtree(d, True)
t, d = dotrun('dt_notep high "Disk full"\ndt_notep low "Workspace 2"\n'
              'dt_note "Build done"', keys=[press(0, COLS - 24)])
sc = t.screen()
hist = sc.text()
check("the history shows each note's priority beside it, and who sent it",
      re.search(r"\d\d:\d\d  normal  Desktop\s+Build done", hist) and
      re.search(r"\d\d:\d\d  low     Desktop\s+Workspace 2", hist) and
      re.search(r"\d\d:\d\d  high    Desktop\s+Disk full", hist), sc)
t.keys([b"\x1b[B", b"\x1b[3~"], settle=0.4)
sc = t.screen()
hist = "\n".join(sc.row(r)[:54] for r in range(2, 8))
check("delete clears the selected note and leaves the rest",
      "Workspace 2" not in hist and "Build done" in hist and
      "Disk full" in hist, sc)
t.quit(None, 0.5); shutil.rmtree(d, True)
# A note says who it is from: the app whose window the desktop was
# handling when it was made, in the note's top border and in the history.
NOTER = ('noter_key() { dt_note "Made by the app"; }\n'
         'dt_app noter "Noter" 6 24\ndt_launch noter\n')
t, d = dotrun(NOTER, keys=[b"x"])
sc = t.screen()
r = sc.find("Made by the app")
check("a note made in an app's key handler is from that app, on its border",
      r is not None and "Noter" in sc.row(r[0] - 1), sc)
t.keys([press(0, COLS - 24)], settle=0.6)
check("and the history says so too",
      re.search(r"normal  Noter\s+Made by the app", t.screen().text()),
      t.screen())
t.quit(None, 0.5); shutil.rmtree(d, True)
t, d = dotrun('dt_note "On its own"')
sc = t.screen()
r = sc.find("On its own")
check("one the desktop makes on its own is from the Desktop",
      r is not None and "Desktop" in sc.row(r[0] - 1), sc)
t.quit(None, 0.5); shutil.rmtree(d, True)
t, d = dotrun('dt_notep high "Disk full"\ndt_notep low "Workspace 2"\n'
              'dt_notep low "Copied"\ndt_note "Build done"',
              keys=[press(0, COLS - 24), b"\x1b[21~", b"\x1b[C", b"\x1b[C",
                    b"\x1b[C", b"l"])
sc = t.screen()
hist = "\n".join(sc.row(r)[:54] for r in range(2, 8))
check("History > Clear Low clears every low note and nothing else",
      "Workspace 2" not in hist and "Copied" not in hist and
      "Build done" in hist and "Disk full" in hist, sc)
t.quit(None, 0.5); shutil.rmtree(d, True)

# The bell is a choice, not the default: it is an emoji, two cells wide,
# and a terminal without an emoji font draws it as an empty box. Chosen,
# the layout measures it and its click still lands. (A wide character's
# second cell reads as a space in the screen model, so it is "🔔 ".)
t, d = dotrun('dt_note "Unread"', env={"DT_BELLICON": "bell"})
sc = t.screen()
bell = sc.find("🔔")
check("DT_BELLICON=bell puts the bell there, two cells wide, count beside it",
      bell == (0, COLS - 25) and
      re.search(r"🔔 1  \d\d:\d\d", sc.row(0)) is not None, sc)
t.keys([press(0, bell[1] + 1 if bell else 0)], settle=0.6)
sc = t.screen()
check("and a click on either of its cells opens the history",
      sc.find("┤ Notifications ├") is not None, sc)
t.quit(None, 0.5); shutil.rmtree(d, True)

# DT_BARTIME: the clock's own strftime format. A wider one moves the clock
# and the dot left, and their clicks follow them.
t, d = dotrun("", env={"DT_BARTIME": "%a %H:%M:%S"})
sc = t.screen()
m = re.search(r"⚑\S*  (\w\w\w \d\d:\d\d:\d\d)  ", sc.row(0))
check("a format with seconds and a weekday is what the bar shows", m, sc)
dot = sc.row(0).find("⚑")
t.keys([press(0, dot)], settle=0.6)
sc = t.screen()
check("and the bell, moved left to make room, still opens the history",
      sc.find("┤ Notifications ├") is not None, sc)
t.quit(None, 0.5); shutil.rmtree(d, True)

# Date & Time in Control Panel: the clock format is a dropdown of examples
# plus Custom…; the zone has a finder; setting the clock is offered only
# where the machine owns it.
DTPANEL = 'dt_new "Control Panel" 22 70 1 2 panel'
DOWN_DT = [b"\x1b[B"] * ORDER.index("datetime")
t, d = dotrun(DTPANEL, keys=DOWN_DT)
sc = t.screen()
row = next((r for r in range(ROWS) if "Menu bar" in sc.row(r)), None)
check("Date & Time has a Menu bar clock dropdown and a Time zone button",
      row is not None and "▾" in sc.row(row) and sc.find("[ Change… ]"), sc)
host = os.path.exists("/run/systemd/container") or \
    os.path.exists("/.dockerenv") or os.path.exists("/run/.containerenv")
check("setting the clock is offered only where this machine owns it",
      (sc.find("set by the host") is not None) == host and
      (sc.find("[ Set… ]") is not None) == (not host), sc)
t.keys([press(row, sc.row(row).index("▾") - 2)], settle=0.6)
sc = t.screen()
item = sc.find("2:05:09 PM")
check("its list shows examples, not strftime codes",
      item is not None and sc.find("Custom…") is not None and
      sc.find("%H") is None, sc)
t.keys([press(item[0], item[1] + 1)], settle=0.8)
sc = t.screen()
saved = open(os.path.join(d, "config", "hibr", "desktop.hibr")).read() \
    if os.path.exists(os.path.join(d, "config", "hibr", "desktop.hibr")) else ""
check("choosing one changes the bar at once, and is saved",
      re.search(r"\d?\d:\d\d:\d\d [AP]M", sc.row(0)) and
      "DT_BARTIME=%l:%M:%S\\ %p" in saved, saved or sc)
t.quit(None, 0.5); shutil.rmtree(d, True)

t, d = dotrun(DTPANEL, keys=DOWN_DT)
sc = t.screen()
row = next(r for r in range(ROWS) if "Menu bar" in sc.row(r))
t.keys([press(row, sc.row(row).index("▾") - 2)], settle=0.6)
sc = t.screen()
cu = sc.find("Custom…")
t.keys([press(cu[0], cu[1] + 1)], settle=0.6)
t.keys([b"\x7f"] * 8 + [b"%H)"], settle=0.2)
sc = t.screen()
check("a custom format is checked as it is typed",
      sc.find("┤ Clock Format ├") is not None and
      sc.find("cannot contain )") is not None, sc)
t.keys([b"\x7f", b"h"], settle=0.3)
t.keys([b"\r"], settle=0.6)
sc = t.screen()
check("and a valid one is taken on enter",
      sc.find("┤ Clock Format ├") is None and
      re.search(r"⚑\S*  \d\dh  ", sc.row(0)) is not None, sc)
t.quit(None, 0.5); shutil.rmtree(d, True)

t, d = dotrun(DTPANEL, keys=DOWN_DT)
sc = t.screen()
ch = sc.find("[ Change… ]")
t.keys([press(ch[0], ch[1] + 2)], settle=0.8)
t.keys([b"u", b"t", b"c"], settle=0.3)
sc = t.screen()
check("the zone finder narrows the list as you type",
      sc.find("┤ Time Zone ├") is not None and sc.find(" UTC") is not None
      and sc.find("Europe/") is None, sc)
t.keys([b"\x1b"], settle=0.5)
sc = t.screen()
check("and escape closes it without running anything",
      sc.find("┤ Time Zone ├") is None and sc.find("┤ Terminal") is None, sc)
t.quit(None, 0.5); shutil.rmtree(d, True)


CONF = tempfile.mkdtemp(prefix="hibr-conf-")
sc, raw = run('dt_new "Control Panel" 22 58 2 2 panel',
              feed=DOWN_APP + [b"\x1b[C", b"\x1b[C"],
              env={"XDG_CONFIG_HOME": CONF}, pre=PANEL)
saved = os.path.join(CONF, "hibr", "desktop.hibr")
text = open(saved).read() if os.path.exists(saved) else ""
check("a changed setting is written at once, as a script",
      "CP_THEME=construction" in text and "DT_WALL=\\#0d0d0d" in text and
      "DT_ICONS=" in text, text or sc)
sc, raw = run('dt_new "Control Panel" 22 58 2 2 panel', feed=DOWN_APP,
              env={"XDG_CONFIG_HOME": CONF}, pre=PANEL)
check("and the next desktop starts with it", sc.find("construction") is not None,
      sc)
shutil.rmtree(CONF, True)

# ORDER.index("shortcuts") downs on the picker reaches Shortcuts; entering it
# lands on its first row that is not a heading, Menu Bar. The rows follow
# DT_KEYORDER, read from wm/keys.hibr rather than counted here.
KEYORDER = re.search(r"DT_KEYORDER=\(([^)]*)\)",
                     open(tree("examples/desktop/wm/keys.hibr")).read()
                     ).group(1).split()
NKEYS = len(KEYORDER)
CONF2 = tempfile.mkdtemp(prefix="hibr-conf2-")
sc, raw = run('dt_new "Control Panel" 22 58 2 2 panel',
              feed=DOWN_KB + [b"\r"] + [b"\x1b[B"] * KEYORDER.index("close")
              + [b"\r", b"x"],
              env={"XDG_CONFIG_HOME": CONF2}, pre=PANEL)
check("a shortcut row can be rebound to a new key",
      sc.find("alt-w") is None, sc)
saved2 = os.path.join(CONF2, "hibr", "desktop.hibr")
text2 = open(saved2).read() if os.path.exists(saved2) else ""
check("and the new binding is saved", 'DT_KEYS["close"]=x' in text2, text2)
shutil.rmtree(CONF2, True)

# Any registered app gets its own row in Shortcuts, under Apps, not just
# Terminal and Task Manager -- empty by default, assignable the same way the
# desktop's own are. Past the desktop's own rows -- as many as DT_KEYORDER
# names, read from wm/keys.hibr rather than counted here -- Calculator is the
# first app, since it sorts before Control Panel.

CALCSRC = '. %s/calc.hibr' % tree("examples/desktop/desk-accessories")
CONF3 = tempfile.mkdtemp(prefix="hibr-conf3-")
sc, _ = run('dt_new "Control Panel" 22 58 2 2 panel',
            feed=DOWN_KB + [b"\r"] + [b"\x1b[B"] * NKEYS,
            env={"XDG_CONFIG_HOME": CONF3}, pre=PANEL + "\n" + CALCSRC)
check("a registered app is listed with no shortcut by default",
      sc.find("Calculator") is not None, sc)
sc, _ = run('dt_new "Control Panel" 22 58 2 2 panel',
            feed=DOWN_KB + [b"\r"] + [b"\x1b[B"] * NKEYS + [b"\r", b"g"],
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

# #56: ctrl-alt-t and alt-ctrl-t name the same physical chord -- which
# one a real keypress produces depends on whether the terminal encoded
# it as an ESC-prefixed alt combination (always "alt-" first) or a
# CSI-encoded one (always "ctrl-" first), not on anything the user did
# differently. dt_keynorm is the direct, deterministic proof; the pty
# check after it is the real thing end to end, term registered the
# other order round from what an actual keypress sends.
import subprocess
KEYNORM = (
    '. %s\n'
    'for pair in "alt-ctrl-t:ctrl-alt-t" "ctrl-alt-t:alt-ctrl-t" '
    '"alt-s:alt-s" "alt-f4:alt-f4"; do\n'
    '  a=${pair%%:*}; b=${pair#*:}\n'
    '  an := dt_keynorm "$a"; bn := dt_keynorm "$b"\n'
    '  [ "$an" = "$bn" ] && echo "match $a $b" || echo "nomatch $a $b"\n'
    'done\n'
    % WM
)
out = subprocess.run([screen.HIBR, "-c", KEYNORM], capture_output=True,
                     text=True, env=dict(os.environ, DT_ROWS="1")).stdout
check("dt_keynorm treats either modifier order as the same shortcut",
      out.count("match ") == 4 and "nomatch" not in out, out)

TERMKEY = tempfile.mkdtemp(prefix="hibr-termkey-")
p = os.path.join(TERMKEY, "session.hibr")
open(p, "w").write(
    "%s. %s\n. %s/term.hibr\n"
    'DT_APPKEY[term]="ctrl-alt-t"\n'
    "dt_open\ndt_run\ndt_close\n"
    % (load(MOD, "build/mods/pty.so", "build/mods/term.so"), WM,
       tree("examples/desktop/apps"))
)
t = Term(p, env={"DT_TICK": "60"}, rows=ROWS, cols=COLS, settle=0.6)
t.send(b"\x1b\x14", settle=0.4, collect=0.4)  # ESC ctrl-T: alt-ctrl-t
sc = t.screen()
t.quit(b"qy", 1.0)
check("registered as ctrl-alt-t, a real alt-ctrl-t keypress still opens it",
      sc.find("Terminal") is not None, sc)
shutil.rmtree(TERMKEY, True)

HOLD = tempfile.mkdtemp(prefix="hibr-hold-")
held = os.path.join(HOLD, "session.hibr")
# The apps are loaded here so About can be opened in a held desktop: its
# Held line is the one fact about the terminal that only a real hold
# session can answer, and nothing in apps/ opens a window at startup, so
# the screen these checks read is unchanged by loading them.
open(held, "w").write("%s. %s\nDT_APPDIRS+=(\"%s\")\ndt_apps\n"
                      "dt_open\ndt_new \"Held\" 8 30 6 10\n"
                      "dt_run\ndt_close\n"
                      % (load(MOD, "build/mods/pty.so", "build/mods/term.so",
                              "build/mods/hold.so"), WM,
                         tree("examples/desktop/apps")))
HENV = {"TMPDIR": HOLD, "DT_TICK": "60"}
HOLDC = load("build/mods/pty.so", "build/mods/term.so", "build/mods/hold.so")


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

# A session comes up on another machine with a smaller screen: the whole
# point of hold is that the desktop outlives the terminal it was started on,
# so the one that attaches next is routinely a different size. Nothing
# covered that until 0.99.98 -- every `hold attach` in these suites used the
# same size as the `hold new` before it -- which is why a live report of the
# bar not resizing, the wallpaper not adjusting and the Control Strip being
# off the screen could not be answered from the suites either way.
RZ = tempfile.mkdtemp(prefix="hibr-rz-")
rzs = os.path.join(RZ, "session.hibr")
open(rzs, "w").write("%s. %s\n%s%s\nDT_WALLMODE=stretch\n"
                     "dt_open\ndt_new \"Rz\" 6 20 2 2\ndt_run\ndt_close\n"
                     % (load(MOD, "build/mods/pty.so", "build/mods/term.so",
                             "build/mods/hold.so"), WM, CSSRC,
                        'DT_SAVERDIRS+=("%s")\ndt_savers\n'
                        % tree("examples/desktop/savers")))
RZENV = {"TMPDIR": RZ, "DT_TICK": "60", "XDG_CONFIG_HOME": RZ}
RZC = load("build/mods/pty.so", "build/mods/term.so", "build/mods/hold.so")
atexit.register(lambda: shutil.rmtree(RZ, True))
atexit.register(lambda: subprocess.run(
    [screen.HIBR, "-c", RZC + "hold kill rz"],
    env=dict(os.environ, **RZENV), capture_output=True))


def rzstrip(sc, rows):
    """Which row the strip is on, or None when it is nowhere to be seen."""
    for r in range(rows - 1, 0, -1):
        if "[Cursor]" in sc.row(r) or sc.row(r).lstrip().startswith("▸"):
            return r
    return None


t = Term("-c", RZC + "hold new rz %s %s" % (screen.HIBR, rzs), env=RZENV,
         rows=30, cols=100, settle=2.0)
t.collect(1.0)
sc = t.screen(rows=30, cols=100)
check("a held desktop draws to the size of the terminal it was started on",
      len(sc.row(0).rstrip()) > 90 and rzstrip(sc, 30) is not None, sc)
t.send(b"\x1c", settle=0.8)
t.close()
t = Term("-c", RZC + "hold attach rz", env=RZENV, rows=18, cols=60,
         settle=2.0)
t.collect(2.0)
sc = t.screen(rows=18, cols=60)
check("reattached on a smaller screen, the menu bar is composed for that one",
      len(sc.row(0).rstrip()) <= 60, repr(sc.row(0)))
check("and the Control Strip is pulled back onto it",
      rzstrip(sc, 18) is not None, sc)
check("and the wallpaper is repainted out to the new bottom corner",
      sc.at(17, 59) not in ("", " "), repr(sc.at(17, 59)))
t.quit(b"qy", 1.0)

# And the same with the screen saver up while it happens, which is the
# ordinary way of it: you detach from one machine, it locks or savers while
# you are away, and you attach from another with a different screen. dt_run's
# own saver branch continued before the resize check at the foot of the loop
# ever ran, and dt_saverinput throws a `resize` key away on purpose so the
# saver is not dismissed by one -- so nothing recorded that the screen had
# changed size, and the desktop came back still drawing for the old one
# (Gitea #126). Reported twice from a live session before it was reproduced,
# because every probe for it had no saver and no lock.
t = Term("-c", RZC + "hold new rz %s %s" % (screen.HIBR, rzs), env=RZENV,
         rows=30, cols=100, settle=2.0)
t.collect(1.0)
t.send(b"\x1b[21~", settle=0.4)
t.send(b"s", settle=1.0)
sc = t.screen(rows=30, cols=100)
check("the hibr menu's Screen Saver covers the desktop",
      sc.find("┤ Rz ├") is None, sc)
t.send(b"\x1c", settle=0.8)
t.close()
t = Term("-c", RZC + "hold attach rz", env=RZENV, rows=18, cols=60,
         settle=2.0)
t.collect(1.5)
t.send(b" ", settle=1.2)
t.collect(1.2)
sc = t.screen(rows=18, cols=60)
check("a resize that arrived while the saver was up is acted on when it ends",
      len(sc.row(0).rstrip()) <= 60 and rzstrip(sc, 18) is not None,
      (len(sc.row(0).rstrip()), rzstrip(sc, 18)))
t.quit(b"qy", 1.0)
shutil.rmtree(RZ, True)

# About's own Held line, which only a real held session can answer: one
# client is attached, this one (Gitea #121).
t = Term("-c", HOLDC + "hold attach desk", env=HENV, settle=1.5)
t.send(b"\x1b[21~", settle=0.4)
t.send(b"a", settle=0.8)
sc = t.screen()
check("About in a held desktop says it is held, and by how many displays",
      sc.find("Held: yes, 1 display attached") is not None, sc)
t.send(b"\x1b[21~", settle=0.4)
t.send(b"d", settle=0.8)
t.close()

t = Term("-c", HOLDC + "hold attach desk; echo \"back $?\"", env=HENV,
         settle=1.5)
t.send(b"\x1b[21~")
t.send(b"q", settle=1.0)
t.collect(0.5)
check("quitting a held desktop ends the session",
      b"[desk ended, status 0]" in t.out and b"back 0" in t.out, t.out.decode(errors="replace"))
t.close()

# hold redraws each client from an emulator of its own, so anything a
# program sends for the terminal rather than the screen used to die there:
# a bell, a notification, the window title, a link. Each is passed on now,
# and a link is drawn as one.
EMIT = os.path.join(HOLD, "emit.sh")
open(EMIT, "w").write(
    "#!/bin/sh\nsleep 0.5\nprintf 'BEFORE\\a'\n"
    "printf '\\033]9;built\\007'\n"
    "printf '\\033]777;notify;make;all green\\007'\n"
    "printf '\\033]2;WinTitle\\007'\n"
    "printf '\\033]8;;https://example.com\\007LINK\\033]8;;\\007 after\\n'\n"
    "sleep 3\n")
os.chmod(EMIT, 0o755)
t = Term("-c", HOLDC + "hold new signals %s" % EMIT, env=HENV, settle=2.0)
t.collect(0.5)
check("a bell in a held session reaches the terminal attached to it",
      b"BEFORE" in t.out and
      b"\x07" in re.sub(rb"\x1b\][^\x07]*\x07", b"", t.out), t.out)
check("and so do its notifications, as it sent them",
      b"\x1b]9;built\x07" in t.out and
      b"\x1b]777;notify;make;all green\x07" in t.out, t.out)
check("and the window title it sets",
      b"\x1b]2;WinTitle\x07" in t.out, t.out)
inside = b"".join(re.findall(
    rb"\x1b\]8;;https://example\.com\x07(.*?)\x1b\]8;;\x07", t.out, re.S))
inside = re.sub(rb"\x1b\[[0-9;?]*[A-Za-z]", b"", inside)
check("and a link stays a link, closed where it ends",
      b"LINK" in inside and b"after" not in inside, t.out)
t.close()
subprocess.run([screen.HIBR, "-c", HOLDC + "hold kill signals"],
               env=dict(os.environ, **HENV), capture_output=True)

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
      sc.find("┤ Files [") is not None, sc)
t.send(b"\x1b[21~", settle=0.6)
t.send(b"\x1b", settle=1.0)
sc = t.screen()
check("a lone escape reaches a held desktop on its own, not with the next "
      "key", sc.find("Screen Saver") is None, sc)
# hold keeps its own emulator of the session's screen, which drew nothing
# for OSC 52 and so dropped every copy made in a held desktop: Copy reached
# no machine's clipboard. It is passed on to every attached terminal now.
hm = sc.find("Home")
if hm:
    t.send(press(hm[0], hm[1] + 1) + release(hm[0], hm[1] + 1), settle=0.6)
t.send(b"\x1bc", settle=1.0)
check("Copy in a held desktop reaches the terminal attached to it",
      hm is not None and b"\x1b]52;c;" in t.out, t.screen())
t.send(b"\x1b[21~")
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
      sc.find("┤ Files [") is not None, sc)
t.send(b"\x1b[21~")
t.send(b"q", settle=1.0)
t.close()
r = subprocess.run([screen.HIBR, "-c", HOLDC + "hold list"],
                   env=dict(os.environ, **S2ENV), capture_output=True,
                   text=True)
check("ending it leaves the other one alone",
      "personal" in r.stdout and "work" not in r.stdout, r.stdout)

# HIBR_HOLD is an ordinary environment variable, inherited by anything a
# held terminal starts -- so a shell already attached to some other
# session, starting this one from inside it, inherits that other
# session's own HIBR_HOLD. dt_autohold must not mistake "HIBR_HOLD is
# set" for "I am already the held program": only a value naming *this*
# session means that.
INHERITENV = dict(S2ENV, HIBR_HOLD="/tmp/hibr-hold-nonexistent/elsewhere")
t = Term(SESSION, "--session", "third", env=INHERITENV, settle=2.0)
t.send(b"\x1c", settle=0.5)
t.close()
r = subprocess.run([screen.HIBR, "-c", HOLDC + "hold list"],
                   env=dict(os.environ, **S2ENV), capture_output=True,
                   text=True)
check("an unrelated inherited HIBR_HOLD does not stop it holding for real",
      "third" in r.stdout, r.stdout)
subprocess.run([screen.HIBR, "-c", HOLDC + "hold kill third"],
               env=dict(os.environ, **S2ENV), capture_output=True)
unsession()

# --join is a second terminal's own request to attach beside the first
# rather than start or resume a session of its own -- one more monitor on
# the same desktop. Content-level assertions about what the joined
# terminal actually draws (the menu bar staying on the first screen, not
# spanning both) belong to the primary-viewport work that follows this;
# what belongs here is that it attaches at all, at the right combined
# width, and that detaching it leaves the first alone.

JOIN = tempfile.mkdtemp(prefix="hibr-join-")
JENV = {"HOME": JOIN, "TMPDIR": JOIN,
        "XDG_CONFIG_HOME": os.path.join(JOIN, "config"),
        "XDG_STATE_HOME": os.path.join(JOIN, "state"),
        "XDG_DATA_HOME": os.path.join(JOIN, "data"),
        # session.hibr's own `need hold`/`need term` etc. otherwise fall
        # through to whatever hold.so happens to be installed system-wide,
        # which -m and HD_INFO's own size field need not be new enough to
        # have -- this suite must not depend on that, only on this build.
        "HIBR_MODPATH": tree("build/mods")}


def unjoin():
    subprocess.run([screen.HIBR, "-c", HOLDC + "hold kill desktop"],
                   env=dict(os.environ, **JENV), capture_output=True)
    shutil.rmtree(JOIN, True)


atexit.register(unjoin)

t1 = Term(SESSION, env=JENV, cols=80, settle=2.0)
sc = t1.screen()
check("a plain desktop starts, ready to be joined",
      sc.find("Home") is not None, sc)

t2 = Term(SESSION, "--join", env=JENV, cols=60, settle=1.5)
out2 = t2.out.decode(errors="replace")
r = subprocess.run([screen.HIBR, "-c", HOLDC + "hold list"],
                   env=dict(os.environ, **JENV), capture_output=True,
                   text=True)
check("--join attaches beside it, growing the union to the combined width",
      "x140" in r.stdout and "is not running" not in out2,
      r.stdout + "\n" + out2)

sc2 = t2.screen()
check("the bar and icons stay on the first screen, not spilling into this one",
      sc2.find("Home") is None and sc2.find("Desktop") is None, sc2)

r3 = subprocess.run([screen.HIBR, "-c", HOLDC + "hold clients desktop"],
                    env=dict(os.environ, **JENV), capture_output=True,
                    text=True)
rows = [ln.split() for ln in r3.stdout.splitlines() if ln.split()]
check("hold clients lists both attached displays", len(rows) == 2, r3.stdout)
primary = next((row[0] for row in rows if row[-1] == "1"), None)
joined = next((row[0] for row in rows if row[-1] == "0"), None)
check("exactly one is primary, the other is not",
      primary is not None and joined is not None, r3.stdout)

# A window's own titlebar context menu gets a "Move to" submenu (Slice C):
# open Files from the desktop's own Home icon, right-click its titlebar,
# and hand it to the joined display by name.
hp = sc.find("Home")
t1.send(press(hp[0], hp[1]))
t1.send(press(hp[0], hp[1]))
sc = t1.screen()
tb = sc.find("┤ Files [~] ├")
check("double-clicking home opens Files, with a titlebar to right-click",
      tb is not None, sc)
t1.send(press(tb[0], tb[1], 2))
sc = t1.screen()
check("the titlebar's own context menu gets a Move to Display submenu",
      sc.find("Move to Display") is not None, sc)
for _ in range(6):
    t1.send(b"\x1b[B")
t1.send(b"\x1b[C")
sc = t1.screen()
check("it lists every attached display by name",
      sc.find(primary) is not None and sc.find(joined) is not None, sc)
t1.send(b"\x1b[B")
t1.send(b"\r")
t2.collect(1.0)
sc1c = t1.screen()
sc2c = t2.screen()
check("choosing one moves the window there",
      sc1c.find("┤ Files [~] ├") is None and
      sc2c.find("┤ Files [~] ├") is not None,
      str(sc1c) + "\n" + str(sc2c))

# The control socket (#91): what Blit, the terminal server that draws a
# held desktop on its screens, uses instead of keystrokes -- the displays
# and windows as JSON, and a window moved with the same dt_movewin the
# menu uses, the joined screen drawing it.
def ctl(*a):
    r = subprocess.run([screen.HIBR, tree("examples/desktop/lib/ctl.hibr"), "--session", "desktop"] + list(a),
                       env=dict(os.environ, **JENV), capture_output=True, text=True, timeout=20)
    try:
        return r.returncode, json.loads(r.stdout)
    except ValueError:
        return r.returncode, {"raw": r.stdout + r.stderr}


rc, j = ctl("displays")
names = sorted(d["name"] for d in j.get("displays", []))
check("desktop ctl displays lists both attached displays, one primary",
      rc == 0 and names == sorted([primary, joined]) and
      [d["primary"] for d in j["displays"]].count(True) == 1, j)
rc, j = ctl("windows")
fw = [w for w in j.get("windows", []) if w["title"].startswith("Files")]
check("desktop ctl windows lists the Files window with its place and size",
      rc == 0 and len(fw) == 1 and all(k in fw[0] for k in ("id", "row", "col", "h", "w", "workspace", "focused")), j)
fid = str(fw[0]["id"]) if fw else "0"
rc, j = ctl("move", fid, primary)
t1.collect(1.0)
check("a move to the primary display puts it there, and its screen draws it",
      rc == 0 and j.get("ok") and t1.screen().find("┤ Files [~] ├") is not None, (j, str(t1.screen())))
rc, j = ctl("move", fid, joined)
t2.collect(1.0)
check("and to the joined display, which draws it, not the background",
      rc == 0 and j.get("ok") and t2.screen().find("┤ Files [~] ├") is not None, (j, str(t2.screen())))
rc, j = ctl("move", "999", joined)
check("a window that is not there is refused, saying so", rc == 1 and j.get("error") == "no-window", j)
rc, j = ctl("move", fid, "nosuch")
check("and so is a display that is not", rc == 1 and j.get("error") == "no-display", j)

# The Control Panel gets a "Displays" pane (Slice D): every attached
# display drawn to scale from hold clients, dragged to reposition it
# (hold move on release); a "Primary:" dropdown to choose which one is
# primary (hold primary); right-click a rectangle for Detach (hold drop)
# or Identify (flash its own name on its own screen). There is no
# separate hibr-menu equivalent -- this pane is the one place for all
# of it.
t1.send(press(0, 1))
sc = t1.screen()
cp = sc.find("Control Panel")
t1.send(press(cp[0], cp[1]))
sc = t1.screen()
dpy = sc.find("Displays")
t1.send(press(dpy[0], dpy[1]))
sc = t1.screen()
check("the Displays pane draws both attached displays by name",
      sc.find(primary) is not None and sc.find(joined) is not None, sc)
prim = sc.find("Primary:")
check("the Primary: dropdown sits on the same row as its label",
      prim is not None and sc.find_from("▾", prim[0]) is not None and
      sc.find_from("▾", prim[0])[0] == prim[0], sc)

# The pane list stays clickable while Displays is showing: the pane's own
# draw used to clear every hit region in the window, the list's included,
# so only the keys could leave it.
ap = sc.find("Appearance")
t1.send(press(ap[0], ap[1]))
t1.send(release(ap[0], ap[1]))
sc = t1.screen()
check("a click on the pane list leaves Displays for another pane",
      sc.find("Primary:") is None and sc.find("Theme") is not None, sc)
dpy = sc.find("Displays")
t1.send(press(dpy[0], dpy[1]))
t1.send(release(dpy[0], dpy[1]))
sc = t1.screen()

# The dropdown's own displayed value also carries the primary's name, so
# every check below that cares about *where* a name is drawn (not just
# whether it appears at all) looks only below it, or it would just be
# reading the dropdown back to itself.
canvas0 = sc.find("Primary:")[0] + 3

# The primary display always draws at this pane's own fixed anchor,
# whatever its real row/col is -- dragging a *different* display must
# never move it, only the rectangle actually being dragged.
ppos = sc.find_from(primary, canvas0)
lbl = sc.find_from(joined, canvas0)
t1.send(press(lbl[0], lbl[1]))
t1.send(drag(lbl[0] + 1, lbl[1] + 3))
t1.send(release(lbl[0] + 1, lbl[1] + 3))
r4 = subprocess.run([screen.HIBR, "-c", HOLDC + "hold clients desktop"],
                    env=dict(os.environ, **JENV), capture_output=True,
                    text=True)
row4 = next((ln.split() for ln in r4.stdout.splitlines()
             if ln.split() and ln.split()[0] == joined), None)
check("dragging a display's own rectangle repositions it",
      row4 is not None and row4[2] != "80", r4.stdout)
t1.collect(0.5)
sc = t1.screen()
check("...and leaves the primary display drawn exactly where it was",
      sc.find_from(primary, canvas0) == ppos, sc)

# The primary display's own rectangle is the picture's fixed anchor, so
# it cannot be dragged at all -- attempting to is a no-op, not a move.
r4b = subprocess.run([screen.HIBR, "-c", HOLDC + "hold clients desktop"],
                     env=dict(os.environ, **JENV), capture_output=True,
                     text=True)
rowp_before = next((ln.split() for ln in r4b.stdout.splitlines()
                    if ln.split() and ln.split()[0] == primary), None)
ppos2 = sc.find_from(primary, canvas0)
t1.send(press(ppos2[0], ppos2[1]))
t1.send(drag(ppos2[0] + 2, ppos2[1] + 5))
t1.send(release(ppos2[0] + 2, ppos2[1] + 5))
r4c = subprocess.run([screen.HIBR, "-c", HOLDC + "hold clients desktop"],
                     env=dict(os.environ, **JENV), capture_output=True,
                     text=True)
rowp_after = next((ln.split() for ln in r4c.stdout.splitlines()
                   if ln.split() and ln.split()[0] == primary), None)
check("dragging the primary display's own rectangle is a no-op",
      rowp_before == rowp_after, r4b.stdout + "\n" + r4c.stdout)

# The dropdown is the one reliable way to change which display is
# primary: open it, choose the other one. The list picker's own row for
# "Displays" must still read correctly afterward -- the display that
# used to be primary can now have a negative offset from the new one,
# which is exactly what put a rectangle on top of the category list
# before this was fixed.
t1.collect(0.5)
sc = t1.screen()
prim = sc.find("Primary:")
t1.send(press(prim[0], prim[1] + len("Primary: ")))
sc = t1.screen()
check("the dropdown opens with a choice for each attached display",
      sc.find_from(joined, prim[0]) is not None, sc)
item = sc.find_from(joined, prim[0] + 1)
t1.send(press(item[0], item[1]))
r5 = subprocess.run([screen.HIBR, "-c", HOLDC + "hold clients desktop"],
                    env=dict(os.environ, **JENV), capture_output=True,
                    text=True)
row5 = next((ln.split() for ln in r5.stdout.splitlines()
             if ln.split() and ln.split()[0] == joined), None)
check("choosing a display from the dropdown makes it primary",
      row5 is not None and row5[-1] == "1", r5.stdout)
t1.collect(1.0)
t2.collect(1.0)
sc = t1.screen()
check("the pane list's own row is still intact, not overlapped",
      sc.find("Displays") is not None, sc)
check("...and the bar follows the new primary onto the other terminal",
      t2.screen().find("Home") is not None, sc)

# Right-click a rectangle for Detach and Identify -- joined's, not
# primary's: Identify flashes its own name in the middle of its own
# screen, and checking it on the terminal without the Control Panel
# open (joined is t2's own display) avoids the flash landing under the
# pane list or the dropdown this same window also draws.
lbl3 = sc.find_from(joined, canvas0)
t1.send(press(lbl3[0], lbl3[1], 2))
sc = t1.screen()
check("right-clicking a rectangle offers Detach and Identify",
      sc.find("Detach") is not None and sc.find("Identify") is not None, sc)
ident = sc.find("Identify")
t1.send(press(ident[0], ident[1]))
t2.collect(0.3)
check("choosing Identify flashes the display's own name on its own screen",
      t2.screen().find("│ %s │" % joined) is not None, sc)

# Detach, on a third display joined just for this, so the two the checks
# below still use stay attached: right-click its rectangle, choose Detach,
# and that terminal is let go, told why: it was switched off.
t3 = Term(SESSION, "--join", env=JENV, cols=40, settle=1.5)
r6 = subprocess.run([screen.HIBR, "-c", HOLDC + "hold clients desktop"],
                    env=dict(os.environ, **JENV), capture_output=True,
                    text=True)
third = next((ln.split()[0] for ln in r6.stdout.splitlines()
              if ln.split() and ln.split()[0] not in (primary, joined)), None)
t1.collect(1.0)
sc = t1.screen()
# A display's rectangle is drawn to scale, and a narrow one shows only as
# much of its name as fits -- a 40-column terminal's reads "clie" -- so it
# is found as the label that starts its name without being another's.
lbl6 = None
jrow = sc.find_from(joined, canvas0)
if third and jrow:
    line = sc.row(jrow[0])
    for m in re.finditer(r"│([^│┌┐└┘ ]+)", line):
        txt = m.group(1)
        if third.startswith(txt) and txt not in (primary, joined):
            lbl6 = (jrow[0], m.start(1))
if lbl6:
    t1.send(press(lbl6[0], lbl6[1], 2))
    sc = t1.screen()
    det = sc.find("Detach")
    if det:
        t1.send(press(det[0], det[1]))
t3.collect(1.0)
r7 = subprocess.run([screen.HIBR, "-c", HOLDC + "hold clients desktop"],
                    env=dict(os.environ, **JENV), capture_output=True,
                    text=True)
check("Detach on a display's rectangle switches that terminal off",
      third is not None and lbl6 is not None and
      b"[desktop: switched off" in t3.out and third not in r7.stdout,
      r6.stdout + "\n" + r7.stdout + "\n" + t3.out.decode(errors="replace"))
t3.close()

t1.collect(0.5)
sc = t1.screen()
close = sc.find("x├")
t1.send(press(close[0], close[1]))

# The panel can change which display is primary, live: hold primary
# nudges the program to notice (hd_poke, the same SIGWINCH a fresh attach
# already gets), and dt_size re-reads hold clients rather than a
# heuristic -- the bar and icons should move to follow whichever display
# picks it up.
subprocess.run([screen.HIBR, "-c",
                HOLDC + "hold primary desktop %s" % joined],
               env=dict(os.environ, **JENV), capture_output=True)
t1.collect(1.0)
t2.collect(1.0)
sc1b = t1.screen()
sc2b = t2.screen()
check("making the joined display primary moves the bar to follow it",
      sc2b.find("Home") is not None, sc2b)
check("...and it leaves the display that used to be primary",
      sc1b.find("Home") is None, sc1b)

t2.send(b"\x1c", settle=0.5)
check("ctrl-\\ detaches just the joined terminal",
      b"[desktop: detached" in t2.out, t2.out.decode(errors="replace"))
t2.close()
r2 = subprocess.run([screen.HIBR, "-c", HOLDC + "hold list"],
                    env=dict(os.environ, **JENV), capture_output=True,
                    text=True)
check("the first one is still there, unaffected",
      "desktop" in r2.stdout, r2.stdout)
rc, j = ctl("move", fid, joined)
check("a move to the display that detached says it is detached", rc == 1 and j.get("error") == "detached", j)

t1.send(b"\x1b[21~")
t1.send(b"q", settle=1.0)
t1.collect(0.5)
check("quitting from the first ends the whole session",
      b"[desktop ended" in t1.out, t1.out.decode(errors="replace"))
t1.close()
check("and its control socket is gone with it",
      not os.path.exists(os.path.join(JOIN, "hibr-ctl-%d" % os.getuid(), "desktop.ctl")),
      os.listdir(JOIN))
unjoin()

# Blit's own launch (#98): the primary held as --session blit with a
# display name, the supervisor on as it is outside the harness, an
# auxiliary --join with its own name, and desktop ctl reached the way Blit
# reaches it, through session.hibr ctl. The socket is there once both are
# attached, lists both displays by name, and moves and resizes a window;
# the desktop log says where it listens. PATH holds hibr and nothing else,
# as in Blit's guest, which has busybox and no applet links: the socket's
# folder was made by /bin/mkdir, so there it never was, and neither was the
# log's (0.99.43; mkdir and rm are builtins now). Since 0.99.44 mv is too,
# and About Me is read with no head (Gitea #99): no session here may print
# a command not found.
BL = tempfile.mkdtemp(prefix="hibr-blit-")
os.makedirs(os.path.join(BL, "bin"))
os.symlink(screen.HIBR, os.path.join(BL, "bin", "hibr"))
BENV = {"HOME": BL, "TMPDIR": BL, "XDG_CONFIG_HOME": os.path.join(BL, "config"),
        "XDG_STATE_HOME": os.path.join(BL, "state"), "XDG_DATA_HOME": os.path.join(BL, "data"),
        "HIBR_MODPATH": tree("build/mods"), "DT_SUPERVISE": "on",
        "PATH": os.path.join(BL, "bin")}


def bctl(*a):
    r = subprocess.run([screen.HIBR, SESSION, "ctl", "--session", "blit"] + list(a),
                       env=dict(os.environ, **BENV), capture_output=True, text=True, timeout=20)
    try:
        return r.returncode, json.loads(r.stdout)
    except ValueError:
        return r.returncode, {"raw": r.stdout + r.stderr}


b1 = Term(SESSION, "--session", "blit", "--name", "display-1", env=BENV, cols=80, settle=2.5)
b2 = Term(SESSION, "--session", "blit", "--join", "--name", "display-2", env=BENV, cols=60, settle=2.0)
bsock = os.path.join(BL, "hibr-ctl-%d" % os.getuid(), "blit.ctl")
deadline = time.time() + 10
while not os.path.exists(bsock) and time.time() < deadline:
    time.sleep(0.2)
check("a supervised held desktop launched as Blit launches it has its control socket",
      os.path.exists(bsock), sorted(os.listdir(BL)))
rc, j = bctl("displays")
check("and session.hibr ctl displays names both displays",
      rc == 0 and sorted(d["name"] for d in j.get("displays", [])) == ["display-1", "display-2"], j)
sc = b1.screen()
hp = sc.find("Home")
if hp:
    b1.send(press(hp[0], hp[1]))
    b1.send(press(hp[0], hp[1]))
b1.collect(1.0)
rc, j = bctl("windows")
bw = [w for w in j.get("windows", []) if w["title"].startswith("Files")]
check("a window opened there is listed", rc == 0 and len(bw) == 1, j)
bid = str(bw[0]["id"]) if bw else "0"
rc, j = bctl("move", bid, "display-2")
b2.collect(1.0)
check("moved to the auxiliary display, which draws it",
      rc == 0 and j.get("ok") and b2.screen().find("Files") is not None, (j, str(b2.screen())))
rc, j = bctl("resize", bid, "12", "40")
check("and resized", rc == 0 and j.get("h") == 12 and j.get("w") == 40, j)
rc, j = bctl("resize", bid, "12", "200")
rc2, j2 = bctl("windows")
bw = [w for w in j2.get("windows", []) if str(w["id"]) == bid]
check("a size the joined display cannot hold is refused, the window as it was",
      j.get("error") == "bad-geometry" and bw and bw[0]["h"] == 12 and bw[0]["w"] == 40, (j, j2))
blog = os.path.join(BL, "state", "hibr", "desktop.log")
check("the desktop log says where its control socket listens, made with no mkdir on PATH",
      os.path.exists(blog) and "control socket: listening at" in open(blog).read(),
      open(blog).read()[-800:] if os.path.exists(blog) else "no log")
b2.close()
b1.send(b"\x1b[21~")
b1.send(b"q", settle=1.0)
b1.close()
subprocess.run([screen.HIBR, "-c", HOLDC + "hold kill blit"], env=dict(os.environ, **BENV), capture_output=True)
shutil.rmtree(BL, True)

# A ctl resize is measured against the display the window is on (#100):
# under Blit the desktop's own screen is only the primary's surface, so a
# window moved to a joined display at column 128 was clamped to nothing by
# the 128 columns and every resize there refused. Here the display list is
# given, so a horizontal pair as Blit has, a vertical one and two panes
# can each be tried without hold.
GEO = """declare -gA DT
DT_MINH=4 DT_MINW=12 DT_FOCUS= DT_WS=1 DT_WSN=1 HIBR_HOLD=
dt_ticker() { :; }; dt_want() { :; }; dt_rslog() { :; }; dt_hidden() { return 1; }
console() { :; }
dt_movewin() { DT[$1]["row"]=$2; DT[$1]["col"]=$3; }
. %s
DT[1]["title"]=Files DT[1]["app"]=files DT[1]["row"]=1 DT[1]["col"]=2 DT[1]["h"]=10 DT[1]["w"]=34
DT_ROWS=48 DT_COLS=128
dt_ctldlist() { ret "$LAYOUT"; }
LAYOUT=$'display-47 0 0 48 128 1\\ndisplay-48 0 128 48 128 0'
x := dt_ctlmove 1 display-48; echo "$x"
x := dt_ctlresize 1 20 60; echo "$x"
x := dt_ctlresize 1 20 200; echo "$x"
x := dt_ctlwindows; echo "$x"
LAYOUT=$'top 0 0 24 80 1\\nbottom 24 0 24 80 0'
DT[1]["row"]=30 DT[1]["col"]=5
x := dt_ctlresize 1 18 70; echo "$x"
x := dt_ctlresize 1 19 70; echo "$x"
LAYOUT=$'left 0 0 40 60 1\\nright 0 60 40 60 0'
DT[1]["row"]=2 DT[1]["col"]=61
x := dt_ctlresize 1 38 59; echo "$x"
x := dt_ctlresize 1 38 60; echo "$x"
""" % tree("examples/desktop/wm/ctl.hibr")
geo = [json.loads(l) for l in subprocess.run([screen.HIBR, "-c", GEO], capture_output=True,
                                              text=True, timeout=20).stdout.splitlines()]
check("a window moved to a display joined beside a 128-column primary resizes there",
      len(geo) == 8 and geo[0]["col"] == 128 and geo[1].get("ok") and geo[1]["w"] == 60, geo)
check("a size that does not fit is refused whole, and windows still says the last good one",
      len(geo) == 8 and geo[2].get("error") == "bad-geometry"
      and geo[3]["windows"][0]["h"] == 20 and geo[3]["windows"][0]["w"] == 60, geo)
check("on a display below another, as far as its bottom edge and no further",
      len(geo) == 8 and geo[4].get("ok") and geo[5].get("error") == "bad-geometry", geo)
check("and in the right of two panes, as far as its right edge",
      len(geo) == 8 and geo[6].get("ok") and geo[7].get("error") == "bad-geometry", geo)

# A language (#68): every string the desktop draws through its widgets is
# looked up in the catalogue. The pseudo-language xx marks each one, so a
# menu, an item or an icon still in plain English is one that bypassed the
# lookup. English itself is every other test in this file.
sc, raw = run('dt_new Files 12 40 3 4 files', feed=[b"\x1b[21~"], pre=APPS,
              env={"DT_LANG": "xx"})
check("in a language the menu bar is translated, item by item",
      sc.find("⟦File⟧") is not None and sc.find("⟦Window⟧") is not None, sc)
# Screen Saver rather than the About item: that one follows the front app
# now, and this run has one focused, so its text comes from a template
# rather than being a literal catalogue key (Gitea #119).
check("and the menus and the desktop's icons",
      sc.find("⟦Screen Saver⟧") is not None
      and sc.find("⟦Home⟧") is not None, sc)

# Text an app draws itself goes through dt_tput: in xx it is marked too.
sc, raw = run('dt_new "About This Computer" 16 56 2 2 about', pre=APPS,
              env={"DT_LANG": "xx"})
check("text an app draws itself is translated: About's own labels",
      sc.find("\u27e6Hostname:") is not None, sc)

# Mirrored layout (#68, ADR 0033): with a right-to-left language the bar
# runs from the right edge, the application menu and the clock at the left;
# a window's buttons and title swap sides. xy is a right-to-left pseudo-
# language; English unchanged unless DT_MIRROR=on.
XY = {"DT_LANG": "xy"}
sc, raw = run(ONE, env=XY)
bar = sc.row(0).rstrip()
check("mirrored, the hibr menu is at the right edge and the app menu at the left",
      bar.endswith("\u270e") and bar.lstrip().startswith("Hello") is False
      and "\u25be" in bar[:16], bar)
check("and a window's buttons are on the left, its title on the right",
      re.search(r"\u250c.?\u2524x", sc.row(6)) is not None and
      sc.row(6).find("Hello") > sc.row(6).find("x"), sc.row(6))
sc, raw = run(ONE, feed=[press(6, 12), release(6, 12)], env=XY)
check("the close button is where it is drawn: a click on it closes the window",
      sc.find("Hello") is None, sc)
sc, raw = run(ONE, feed=[press(0, COLS - 2)], env=XY)
check("a click on the rightmost title opens the hibr menu under it",
      sc.find("\u27eaAbout This Computer") is not None, sc)
sc, raw = run(ONE, env={"DT_MIRROR": "on"})
check("DT_MIRROR=on mirrors English too",
      sc.row(0).rstrip().endswith("\u270e"), sc.row(0))
sc, raw = run(ONE)
check("and English is not mirrored by default",
      sc.row(0).lstrip().startswith("\u270e"), sc.row(0))

# The one shipped translation (#68, ADR 0032): Arabic is a real catalogue,
# not a pseudo-language, and it is right to left, so choosing it both
# translates the desktop and mirrors it with no other setting.
sc, raw = run(ONE, env={"DT_LANG": "ar"})
bar = sc.row(0).rstrip()
# The cells hold the shaped forms the uni module chose (U+FExx), not the
# letters the catalogue is written in, so the check is "Arabic, and no
# English left on the bar" rather than one spelling of one word.
check("the bundled Arabic catalogue translates the desktop and mirrors it",
      re.search(r"[\u0600-\u06ff\ufb50-\ufeff]", bar) is not None and
      "File" not in bar and "Edit" not in bar and bar.endswith("\u270e"), bar)
# Who puts that text in display order depends on the terminal (ADR 0031):
# with nothing saying otherwise hibr does it, and the cells hold the shaped
# forms; where the terminal does its own -- kitty shapes with HarfBuzz,
# which orders the run with it -- hibr must not, or the two reversals cancel
# and Arabic reads left to right. The harness says nothing about the
# terminal, so this is the one place that does.
check("by default hibr orders it: the cells hold shaped forms",
      re.search(r"[\ufb50-\ufeff]", bar) is not None, bar)
sc, raw = run(ONE, env={"DT_LANG": "ar", "KITTY_WINDOW_ID": "1"})
kbar = sc.row(0).rstrip()
check("in kitty it leaves the ordering to the terminal: the letters as written",
      re.search(r"[\u0600-\u06ff]", kbar) is not None and
      re.search(r"[\ufb50-\ufeff]", kbar) is None, kbar)
sc, raw = run(ONE, env={"DT_LANG": "ar", "KITTY_WINDOW_ID": "1", "DT_BIDI": "on"})
check("and saying hibr outright still orders it there",
      re.search(r"[\ufb50-\ufeff]", sc.row(0)) is not None, sc.row(0))

# The rest of the desktop turns round as well: the icons start from the
# left edge, the Control Strip docks right, notes stack from the top-left
# corner, a tiled workspace's main window is on the right, and Control
# Panel's list of panes is on the right of its divider.
d, env, pre, home, backup, usb, src = desk()
sc, raw = run("", env=dict(env, **XY), pre=pre)
check("mirrored, the desktop's icons start from the left edge",
      sc.find("Home") is not None and sc.find("Home")[1] < 10 and
      sc.find("Trash") is not None and sc.find("Trash")[1] < 10, sc)
shutil.rmtree(d, True)
d = tempfile.mkdtemp(prefix="hibr-strip-")
sc, _ = run("", env=dict(XY, XDG_CONFIG_HOME=d), pre=CSSRC)
strip = [r for r in range(ROWS) if sc.row(r).rstrip().endswith("]")]
check("the Control Strip docks at the right", len(strip) == 1 and
      sc.row(strip[0]).find("\u25c2") > 0, sc)
shutil.rmtree(d, True)
p = "/tmp/hibr-desktop-%d.hibr" % os.getpid()
open(p, "w").write("%s. %s\ndt_open\ndt_note \"Corner\"\ndt_run\ndt_close\n"
                   % (load(MOD), WM))
t = Term(p, env=dict(XY, DT_TICK="300"), rows=ROWS, cols=COLS, settle=0.5)
sc = t.screen()
t.quit(None, 1.0)
os.unlink(p)
hit = sc.find("Corner")
check("notes stack from the top-left corner", hit is not None and
      hit[0] <= 4 and hit[1] <= 6, sc)
sc, _ = run(W3, env=XY)
one, two = sc.find("One"), sc.find("Two")
check("a tiled workspace's main window is on the right, the stack on the left",
      one is not None and two is not None and one[0] == 1 and
      one[1] > COLS // 2 and two[1] < COLS // 2, sc)
CPAN = (APPS + 'CP_PANEDIRS+=("%s")\ncp_panes\n'
        % tree("examples/desktop/control-panel"))
XYPANEL = 'dt_new Panel 22 70 1 4 panel'
sc, _ = run(XYPANEL, env=XY, pre=CPAN)
srch = sc.find("Search")
bar = [c for c in range(COLS) if sc.at(5, c) == "\u2502"][1]
check("Control Panel's list of panes is on the right, its body on the left",
      srch is not None and srch[1] > 50 and
      re.search(r"\u2502 \d\d:\d\d:\d\d", sc.row(2)) is not None, sc)
disp = sc.find("Displays")
sc, _ = run(XYPANEL, feed=[press(disp[0], disp[1]), release(disp[0], disp[1])],
            env=XY, pre=CPAN)
check("and a click on a pane in it opens that pane",
      sc.find("Displays") is not None and
      re.search(r"\d\d:\d\d:\d\d", sc.row(2)) is None, sc)
sc, _ = run(XYPANEL + " language", env=XY, pre=CPAN)
row = sc.row(2)
check("a pane's rows put the value at the left and the label at the right",
      re.match(r"\S*\u2502 xy +\u25be +\S", row) is not None and
      row.rstrip().find("Language") > row.find("\u25be"), row)
drop = row.index("\u25be")
sc, _ = run(XYPANEL + " language", env=XY, pre=CPAN,
            feed=[press(2, drop), release(2, drop)])
check("and its dropdown opens from where it is drawn",
      sc.find("English") is not None, sc)
sc, _ = run(XYPANEL, feed=[press(5, bar), drag(5, bar - 8), release(5, bar - 8)],
            env=XY, pre=CPAN)
check("dragging its divider left widens the list, as it is on the right",
      sc.find("Search") is not None and sc.find("Search")[1] < srch[1] - 4,
      sc)

# Files mirrored: names at the right in each view, the icon grid from the
# right, and a click on a tile or a column heading reaching what is drawn.
FBD = tempfile.mkdtemp(prefix="hibr-fbm-")
for nm in ("alpha.txt", "beta.md", "c"):
    open(os.path.join(FBD, nm), "w").write("x" * (1000 * len(nm)))
os.mkdir(os.path.join(FBD, "sub"))
FBM = 'dt_new Files 14 60 2 4 files "%s"\nFB[1][view]=' % FBD
sc, _ = run(FBM + "list", env=XY, pre=APPS)
row = [sc.row(r) for r in range(ROWS) if "alpha.txt" in sc.row(r)][0]
check("mirrored, Files' list puts each name at the right",
      row.index("alpha.txt") > 40 and row.index("alpha.txt") + 9 + 1 ==
      row.index("\u2502", 10), row)
sc, _ = run(FBM + "icons", env=XY, pre=APPS)
a = sc.find("alpha.txt")
sc, _ = run(FBM + "icons", env=XY, pre=APPS,
            feed=[press(a[0] - 1, a[1]), release(a[0] - 1, a[1])])
check("and a click on an icon selects the one drawn there",
      sc.find("3 of 5") is not None, sc)
sc, _ = run(FBM + "details", env=XY, pre=APPS)
z = sc.find("Size")
sc, _ = run(FBM + "details", env=XY, pre=APPS,
            feed=[press(z[0], z[1]), release(z[0], z[1])])
check("and a click on a heading sorts by the column under it",
      re.search(r"Size[\u25b2\u25bc]", sc.text()) is not None, sc)
sc, _ = run('dt_filepick save "Save As" "All files:*" "%s" new.txt : 0' % FBD,
            env=XY)
nm, ty, sub = sc.find("Name:"), sc.find("Type:"), sc.find("sub/")
check("the file dialog mirrors: its list and Name at the right, Type at the left",
      nm is not None and ty is not None and sub is not None and
      nm[1] > ty[1] and sub[1] > COLS // 2, sc)
shutil.rmtree(FBD, True)

# Dates as a region writes them (#69, ADR 0034): the Hijri date beside the
# clock when it is on, Arabic-Indic digits when chosen, nothing otherwise.
HMON = r"(Muharram|Safar|Rabi' al-Awwal|Rabi' al-Thani|Jumada al-Ula|Jumada al-Akhirah|Rajab|Sha'ban|Ramadan|Shawwal|Dhu al-Qa'dah|Dhu al-Hijjah)"
sc, _ = run(ONE, env={"DT_HCAL": "umalqura", "DT_BARTIME": "%ie %iB %iY %H:%M"})
check("a Hijri code in the bar's format puts the Hijri date beside the clock",
      re.search(r"\d+ " + HMON + r" 14\d\d +\d\d:\d\d", sc.row(0)) is not None, sc.row(0))
sc, _ = run(ONE, env={"DT_HCAL": "none", "DT_BARTIME": "%ie %iB %iY %H:%M"})
check("and with no Hijri calendar the same format leaves it out",
      re.search(HMON, sc.row(0)) is None and re.search(r"\d\d:\d\d", sc.row(0)), sc.row(0))
MIG = tempfile.mkdtemp(prefix="hibr-mig-")
os.makedirs(os.path.join(MIG, "hibr"))
open(os.path.join(MIG, "hibr", "desktop.hibr"), "w").write(
    "DT_SETVER=4\nDT_HIJRI=on\nDT_HCAL=umalqura\nDT_BARTIME='%H:%M'\n")
sc, _ = run(ONE, env={"XDG_CONFIG_HOME": MIG})
check("settings saved with 0.99.66's Show Hijri Dates on keep the Hijri date in the bar",
      re.search(r"\d+ (Muh|Saf|Rab I|Rab II|Jum I|Jum II|Raj|Sha|Ram|Shaw|Dhu Q|Dhu H) +\d\d:\d\d",
                sc.row(0)) is not None, sc.row(0))
shutil.rmtree(MIG, True)
sc, _ = run(ONE, env={"DT_DIGITS": "arabic"})
check("and Arabic-Indic digits write the clock in them",
      re.search("[\u0660-\u0669]{2}:[\u0660-\u0669]{2}", sc.row(0)) is not None and
      re.search(r"[0-9][0-9]:[0-9][0-9]", sc.row(0)) is None, sc.row(0))
sc, _ = run(ONE)
check("neither is there unless asked for",
      re.search(HMON, sc.row(0)) is None and re.search(r"\d\d:\d\d", sc.row(0)), sc.row(0))

# Prayer times (#70, ADR 0035): the window lists the day's six in order,
# the next lit with a countdown; the bar shows the next when asked; and a
# moment passed since the last look gives a note.
DA = 'DT_APPDIRS+=("%s")\ndt_apps\n' % tree("examples/desktop/desk-accessories")
LDN = {"DT_PLAT": "51.5074", "DT_PLON": "-0.1278"}
sc, _ = run('dt_new "Prayer Times" 15 40 2 4 prayer', env=LDN, pre=DA)
rows = [sc.row(r) for r in range(ROWS)]
order = [next((i for i, l in enumerate(rows) if re.search(r"\b%s\b +\d\d:\d\d" % n, l)), -1)
         for n in ("Fajr", "Sunrise", "Dhuhr", "Asr", "Maghrib", "Isha")]
check("Prayer Times lists the day's six times in order, the next with how long until it",
      -1 not in order and order == sorted(order) and
      re.search(r"(Fajr|Dhuhr|Asr|Maghrib|Isha) in \d", sc.text()) is not None, sc)
sc, _ = run(ONE, env=dict(LDN, DT_PBAR="on"))
check("Next Prayer in the Menu Bar puts the next one beside the clock",
      re.search(r"(Fajr|Dhuhr|Asr|Maghrib|Isha) \d\d:\d\d .*\d\d:\d\d", sc.row(0)) is not None, sc.row(0))
# The notes are the ticker's own doing, so the times are given rather than
# computed: with the real ones this passed between Fajr and Isha and failed
# through the night, since no prayer of today's has happened yet at 00:18.
# What the sun does is tests/salat_adhan.py's, to the minute, all year.
PAST = ('\nfn dt_ptimes(int when = -1) { ret "$((EPOCHSECONDS - 50)) '
        '$((EPOCHSECONDS - 45)) $((EPOCHSECONDS - 40)) $((EPOCHSECONDS - 30)) '
        '$((EPOCHSECONDS - 20)) $((EPOCHSECONDS - 10)) $((EPOCHSECONDS - 5))"; }'
        '\nDT_PNOTED=$((EPOCHSECONDS - 60))')
sc, _ = run(ONE + PAST, env=dict(LDN, DT_PNOTE="on"))
check("and A Note at Each Prayer gives one for each moment since the last look",
      re.search("\u2691[1-5] ", sc.row(0)) is not None, sc.row(0))

# --standby: a terminal that waits to be joined, joins, and when it is let
# go waits again. Blank keeps a joined display joined but dark.
SB = tempfile.mkdtemp(prefix="hibr-standby-")
SBENV = {"HOME": SB, "TMPDIR": SB,
         "XDG_CONFIG_HOME": os.path.join(SB, "config"),
         "XDG_STATE_HOME": os.path.join(SB, "state"),
         "XDG_DATA_HOME": os.path.join(SB, "data"),
         "HIBR_MODPATH": tree("build/mods")}
SBDIR = os.path.join(SB, "state", "hibr", "standby")


def unstandby():
    subprocess.run([screen.HIBR, "-c", HOLDC + "hold kill desktop"],
                   env=dict(os.environ, **SBENV), capture_output=True)
    shutil.rmtree(SB, True)


def sbclients():
    r = subprocess.run([screen.HIBR, "-c", HOLDC + "hold clients desktop"],
                       env=dict(os.environ, **SBENV), capture_output=True,
                       text=True)
    return [ln.split()[0] for ln in r.stdout.splitlines() if ln.split()]


def sbstate(nm):
    try:
        return open(os.path.join(SBDIR, nm)).read().split()[1]
    except (OSError, IndexError):
        return None


atexit.register(unstandby)

ts = Term(SESSION, "--standby", "--name", "spare", env=SBENV, cols=60,
          settle=1.5)
sc = ts.screen()
check("--standby with nothing running waits, saying so with its name",
      sc.find("spare") is not None and
      sc.find("waiting for the desktop") is not None and
      sbstate("spare") == "waiting", sc)
t1 = Term(SESSION, env=SBENV, cols=80, settle=2.0)
ts.collect(3.0)
check("when the desktop starts, a waiting display joins it on its own",
      "spare" in sbclients(), sbclients())
subprocess.run([screen.HIBR, "-c", HOLDC + "hold drop desktop spare"],
               env=dict(os.environ, **SBENV), capture_output=True)
ts.collect(2.0)
sc = ts.screen()
check("let go from the desktop, it waits again instead of ending",
      sc.find("let go") is not None and sbstate("spare") == "released", sc)
t1.collect(6.0)
check("and a display the desktop let go is not joined again on its own",
      "spare" not in sbclients(), sbclients())
open(os.path.join(SBDIR, "spare.join"), "w").close()
ts.collect(2.5)
check("asked by the desktop (Displays > Standby), it joins again",
      "spare" in sbclients(), sbclients())
t1.send(b"\x1b[21~")
t1.send(b"q", settle=1.0)
ts.collect(2.5)
sc = ts.screen()
check("when the desktop quits, it goes back to waiting for the next",
      sc.find("waiting for the desktop") is not None and
      sbstate("spare") == "waiting", sc)
t1.close()
os.makedirs(os.path.join(SB, "state", "hibr"), exist_ok=True)
open(os.path.join(SB, "state", "hibr", "blanked"), "w").write("spare\n")
t1 = Term(SESSION, env=SBENV, cols=80, settle=2.0)
ts.collect(3.5)
sc = ts.screen()
st = sc.style(10, 30)
check("a blank display stays joined and goes dark",
      "spare" in sbclients() and st.get("bg") == "#000000" and
      st.get("fg") == "#000000", (sbclients(), st, sc))
t1.send(b"\x1b[21~")
t1.send(b"q", settle=1.0)
ts.collect(2.0)
ts.send(b"q", settle=1.0)
ts.close()
check("q on the waiting screen stops waiting, and leaves no trace",
      not os.path.exists(os.path.join(SBDIR, "spare")) and ts.exited,
      os.listdir(SBDIR) if os.path.isdir(SBDIR) else "gone")
t1.close()
unstandby()

# --- the screen saver --------------------------------------------------------
#
# After the idle time a saver takes the screen; a key ends it and goes
# nowhere else -- here q, the suite's quit key, which must not quit. The
# clock saver is used for its big block digits, easy to tell from a desktop.

def saverrun(feed, env):
    path = "/tmp/hibr-saver-%d.hibr" % os.getpid()
    open(path, "w").write("%s. %s\ndt_open\n%s\ndt_run\ndt_close\n"
                          % (load(MOD), WM, ONE))
    t = Term(path, env=dict({"DT_SAVER": "clock"}, **env), rows=ROWS, cols=COLS,
             settle=0.5)
    shots = []
    for f in feed:
        if isinstance(f, float):
            t.collect(f)
        else:
            t.send(f)
            t.collect(0.8)
        shots.append(t.screen())
    t.quit(b"qy", 1.2)
    os.unlink(path)
    return shots, t.exited


s, _ = saverrun([3.5, b"q"], {"DT_SAVERSECS": "2"})
check("after the idle time the screen saver takes the whole screen",
      s[0].find("Hello") is None and "\u2588" in s[0].text(), s[0])
check("and a key ends it -- q, which goes nowhere else and does not quit",
      s[1].find("Hello") is not None and "\u2588" not in s[1].text(), s[1])
s, _ = saverrun([b"\x1b[21~", b"s"], {"DT_SAVERIDLE": "0"})
check("the hibr menu's Screen Saver starts one at once",
      s[1].find("Hello") is None and "\u2588" in s[1].text(), s[1])
s, _ = saverrun([3.0], {"DT_SAVERIDLE": "0"})
check("with Start After at never, nothing starts however long it waits",
      s[0].find("Hello") is not None, s[0])

# A saver of one's own is a file in ~/.config/hibr/savers, listed beside
# the bundled ones; saver.hibr also runs one by its path, to try it out.
MINE = tempfile.mkdtemp(prefix="hibr-mysaver-")
os.makedirs(os.path.join(MINE, "hibr", "savers"))
MYSAVER = os.path.join(MINE, "hibr", "savers", "mine.hibr")
open(MYSAVER, "w").write(
    'command -v sv_saver > /dev/null && sv_saver mine "Mine" 200\n'
    'fn mine_start(int rows, int cols) { :; }\n'
    'fn mine_frame(int rows, int cols) { console pen white black; '
    'console put 3 3 "MY OWN SAVER"; }\n')
s, _ = saverrun([3.5], {"DT_SAVERSECS": "2", "DT_SAVER": "mine",
                        "XDG_CONFIG_HOME": MINE})
check("a saver of one's own, in the config folder, runs as any other",
      s[0].find("MY OWN SAVER") is not None and s[0].find("Hello") is None, s[0])
t = Term(tree("examples/desktop/savers/saver.hibr"), MYSAVER, rows=ROWS, cols=COLS,
         settle=0.8, env={"HIBR_MODPATH": tree("build/mods")})
sc = t.screen()
t.quit(b"x", 1.0)
check("and saver.hibr runs one by its path, on its own",
      sc.find("MY OWN SAVER") is not None, sc)
shutil.rmtree(MINE, True)

# --- the screen lock ---------------------------------------------------------
#
# Locked, the saver runs and a key shows a box asking for the password,
# checked through PAM by the auth module. The suite gives PAM a folder of
# its own (DT_LOCKCONF, `auth check -c`): one service whose only password
# is "right horse", checked by a pam_exec script, so nothing here touches
# the account's real one.
PAMDIR = tempfile.mkdtemp(prefix="hibr-pam-")
open(os.path.join(PAMDIR, "chk.sh"), "w").write(
    '#!/bin/sh\ntr -d "\\000" | grep -qx "right horse"\n')
os.chmod(os.path.join(PAMDIR, "chk.sh"), 0o755)
open(os.path.join(PAMDIR, "pw"), "w").write(
    "auth required pam_exec.so expose_authtok quiet %s/chk.sh\n"
    "account required pam_permit.so\n" % PAMDIR)
LOCKENV = {"DT_SAVERIDLE": "0", "DT_LOCKSERVICE": "pw", "DT_LOCKCONF": PAMDIR}

s, ex = saverrun([b"\x1b[21~", b"l", b"q", b"\x15wrong\r", 2.5,
                  b"right horse\r"], LOCKENV)
check("Lock Screen on the hibr menu locks: the saver takes the screen",
      s[1].find("Hello") is None and "\u2588" in s[1].text(), s[1])
check("a key shows the box, and a letter typed at the saver is the "
      "password's first",
      s[2].find("Enter your password") is not None and
      s[2].find("Locked -- ") is not None and
      s[2].find("\u2022") is not None and s[2].find("Hello") is None, s[2])
check("a wrong password is refused, and the desktop stays locked",
      s[3].find("Not the password") is not None and s[3].find("Hello") is None,
      s[3])
check("the right one unlocks, and the desktop is back as it was",
      s[5].find("Hello") is not None and "\u2588" not in s[5].text() and ex,
      s[5])

s, ex = saverrun([b"\x1b\x0c", b"\x1c", b"q", b"\x1b", b"\x1b[21~", 1.0,
                  b"right horse\r"], LOCKENV)
check("the Lock Screen shortcut locks too",
      s[0].find("Hello") is None and "\u2588" in s[0].text(), s[0])
check("locked, neither Detach nor quit reaches the desktop",
      s[2].find("Hello") is None and s[2].find("Enter your password")
      is not None, s[2])
check("escape puts the box away, and the next key brings it back",
      s[3].find("Enter your password") is None and
      s[4].find("Enter your password") is not None, s[4])
check("and the password is still what opens it", s[6].find("Hello")
      is not None and ex, s[6])

s, ex = saverrun([3.5, b"x", b"\x15right horse\r"],
                 dict(LOCKENV, DT_SAVERSECS="2", DT_LOCKSECS="1"))
check("Lock After locks a saver that has run that long: a key asks for the "
      "password rather than ending it",
      s[1].find("Enter your password") is not None and s[1].find("Hello")
      is None, s[1])
check("and it opens as any lock does", s[2].find("Hello") is not None, s[2])
s, _ = saverrun([3.5, b"x"], dict(LOCKENV, DT_SAVERSECS="2"))
check("with Lock After at never, a key just ends the saver",
      s[1].find("Hello") is not None, s[1])
shutil.rmtree(PAMDIR, True)

# A terminal's program goes on writing behind the saver, and its pty,
# watched, ends every wait at once: unread, the saver redrew as fast as it
# could and took a whole core (0.99.20). CPU time is what is measured, not
# wakes -- a spin makes no voluntary switches.
path = "/tmp/hibr-saverpty-%d.hibr" % os.getpid()
open(path, "w").write(
    "%s. %s\n. %s\nTW_CMD=(/bin/sh -c 'while :; do echo x; sleep 0.02; done')"
    "\ndt_open\ndt_new Term 14 44 2 2 term\ndt_run\ndt_close\n"
    % (load(MOD, tree("build/mods/pty.so"), tree("build/mods/term.so")), WM,
       tree("examples/desktop/apps/term.hibr")))
t = Term(path, env={"DT_SAVERSECS": "1", "DT_SAVER": "clock"}, rows=ROWS,
         cols=COLS, settle=0.6)
t.collect(3.0)
def ticks(pid):
    f = open("/proc/%d/stat" % pid).read().rsplit(")", 1)[1].split()
    return int(f[11]) + int(f[12])
c0 = ticks(t.pid)
t.collect(4.0)
used = (ticks(t.pid) - c0) / os.sysconf("SC_CLK_TCK") / 4.0
sc = t.screen()
t.quit(b"xqy", 1.2)
os.unlink(path)
check("the saver over a terminal that keeps writing costs little, not a "
      "core (%.0f%%)" % (used * 100), "\u2588" in sc.text() and used < 0.3, sc)

# --- the login screen --------------------------------------------------------
#
# login/login.hibr, run as the suite's own user -- a trial run, which can
# only log in as itself -- against a PAM folder of its own: "lg" takes only
# "right horse" (pam_exec decides, pam_deny refuses, as a real stack's
# "Authentication failure") and opens a session that sets one variable. The session it
# starts is a stub that writes down what it was asked for, as whom, and
# that variable; LG_ONCE ends the screen after one.
LGDIR = tempfile.mkdtemp(prefix="hibr-login-")
open(os.path.join(LGDIR, "chk.sh"), "w").write(
    '#!/bin/sh\ntr -d "\\000" | grep -qx "right horse"\n')
os.chmod(os.path.join(LGDIR, "chk.sh"), 0o755)
open(os.path.join(LGDIR, "env.conf"), "w").write("LG_SUITE DEFAULT=from-pam\n")
open(os.path.join(LGDIR, "lg"), "w").write(
    "auth [success=1 default=ignore] pam_exec.so expose_authtok quiet "
    "%s/chk.sh\nauth requisite pam_deny.so\nauth required pam_permit.so\n"
    "account required pam_permit.so\nsession required pam_permit.so\n"
    "session optional pam_env.so readenv=0 conffile=%s/env.conf\n"
    % (LGDIR, LGDIR))
LGSTUB = os.path.join(LGDIR, "stub.sh")
LGRAN = os.path.join(LGDIR, "ran")
open(LGSTUB, "w").write('#!/bin/sh\necho "$1 $(id -un) $LG_SUITE" > %s\n'
                        % LGRAN)
os.chmod(LGSTUB, 0o755)
LGCFG = os.path.join(LGDIR, "cfg")
os.makedirs(os.path.join(LGCFG, "hibr"))
shutil.copy(tree("tests/img-quad.jpg"), os.path.join(LGCFG, "me.jpg"))
open(os.path.join(LGCFG, "hibr", "me.json"), "w").write(
    '{"name": "Test Person", "line": "Out to lunch", "picture": "%s",'
    ' "session": "shell"}' % os.path.join(LGCFG, "me.jpg"))
ME = os.environ.get("USER") or subprocess.check_output(["id", "-un"]).decode().strip()


def loginrun(feed, env=None):
    """Drive the login screen; screens after each step, and what ran."""
    if os.path.exists(LGRAN):
        os.unlink(LGRAN)
    e = {"LG_PAMDIR": LGDIR, "LG_SERVICE": "lg", "LG_SAVER": "clock",
         "LG_ONCE": "1", "LG_SESSIONCMD": LGSTUB, "XDG_CONFIG_HOME": LGCFG,
         "LG_TITLE": "testbox", "LG_CONFFILE": "/nonexistent"}
    e.update(env or {})
    t = Term(tree("examples/desktop/login/login.hibr"), env=e, rows=ROWS,
             cols=COLS, settle=0.8)
    shots = []
    for f in feed:
        if isinstance(f, float):
            t.collect(f)
        else:
            t.send(f)
            t.collect(0.8)
        shots.append(t.screen())
    t.quit(b"", 1.5)
    ran = open(LGRAN).read().strip() if os.path.exists(LGRAN) else None
    return shots, ran


s, ran = loginrun([0.3, ME[:1].encode(), ME[1:].encode() + b"\r", b"wrong\r",
                   2.5, b"right horse", b"\t", b"\r"])
check("the login screen starts on the saver, with no box",
      "\u2588" in s[0].text() and s[0].find("testbox") is None, s[0])
check("a key brings the box, and a letter typed is the user name's first",
      s[1].find("testbox") is not None and s[1].find("User name") is not None
      and s[1].find(ME[:1]) is not None, s[1])
check("the user name brings that person's own About Me: name and line, read "
      "as them", s[2].find("Test Person") is not None and
      s[2].find("Out to lunch") is not None and s[2].find("Password")
      is not None, s[2])
check("and their picture, from their own file, drawn from the PPM a child "
      "decoded as them", "\u2580" in s[2].text(), s[2])
check("a wrong password is refused there, and nothing starts",
      s[3].find("Not the password") is not None, s[3])
check("the right one logs in as the person, with PAM's session around it -- "
      "and About Me's own session first, Shell, then Tab moved it to Desktop",
      ran == "desktop %s from-pam" % ME, s[-1])

s, ran = loginrun([0.3, b"\x1b[B", ME.encode() + b"\r", b"right horse\r"],
                  {"XDG_CONFIG_HOME": os.path.join(LGDIR, "none")})
check("an arrow brings the box without typing anything, and with no About "
      "Me it is the account's own name, a letter for the picture, the desktop",
      s[1].find("User name") is not None and s[2].find("Password") is not None
      and "\u2580" not in s[2].text() and ran == "desktop %s from-pam" % ME, s[2])

s, ran = loginrun([0.3, b"r", b"oot\r", b"anything\r", 2.2, b"\x1b", b"\x1b"])
check("root is refused at the box", s[3].find("root cannot log in here")
      is not None and ran is None, s[3])
check("escape goes back to the user name, and again to the saver",
      s[5].find("User name") is not None and s[6].find("testbox") is None, s[6])

s, ran = loginrun([0.3, b"n", b"obody-here\r", b"x\r"])
check("a name with no account gets a password box like any other, and a "
      "refusal", s[2].find("nobody-here") is not None and
      s[3].find("Not the password") is not None and ran is None, s[3])
# Reading About Me as the person gets five seconds: one made a pipe that
# nobody writes to must not hang the screen for everyone else.
FIFOCFG = os.path.join(LGDIR, "fifo")
os.makedirs(os.path.join(FIFOCFG, "hibr"))
os.mkfifo(os.path.join(FIFOCFG, "hibr", "me.json"))
s, ran = loginrun([0.3, ME.encode() + b"\r", 6.0], {"XDG_CONFIG_HOME": FIFOCFG})
check("an About Me that is a pipe nobody writes to does not hang the screen",
      s[2].find("Password") is not None, s[2])

# start.hibr is what a login runs, as the person: it keeps the session
# chosen in their About Me -- keeping everything else there -- and then
# becomes it; a shell starts as a login shell, its name with a dash.
open(os.path.join(LGCFG, "hibr", "me.json"), "w").write(
    '{"name": "Test Person", "session": "desktop"}')
r = subprocess.run([tree("build/hibr"), tree("examples/desktop/login/start.hibr"),
                    "shell"], input=b'echo "name=$0"\nexit\n', capture_output=True,
                   env=dict(os.environ, XDG_CONFIG_HOME=LGCFG, SHELL="/bin/sh",
                            ENV="", HOME=LGDIR), timeout=20)
me = open(os.path.join(LGCFG, "hibr", "me.json")).read()
check("start.hibr keeps the session chosen in About Me, and keeps the rest",
      '"session": "shell"' in me and "Test Person" in me, me)
check("and Shell is the person's own shell, started as a login shell",
      b"name=-sh" in r.stdout, r.stdout + r.stderr)
shutil.rmtree(LGDIR, True)

# --- recovering ------------------------------------------------------------
#
# A held desktop runs under a supervisor (DT_SUPERVISE, on when held, off
# under the harness unless asked). Here it is asked, and the desktop is
# killed with SIGSEGV twelve seconds in -- old enough to be started again
# -- once only, a file saying it already happened. The new one puts the
# window back from the snapshot, says why, and quits cleanly.
RD = tempfile.mkdtemp(prefix="hibr-recover-")
mark = os.path.join(RD, "crashed")
path = os.path.join(RD, "crash.hibr")
open(path, "w").write("%s. %s\ndt_open\n[ \"$DT_RESTORED\" = 1 ] || dt_new \"Hello\" 8 30 6 10\n"
                      "[ -e %s ] || { : > %s; ( sleep 12; kill -SEGV $$ ) & }\ndt_run\ndt_close\n"
                      % (load(MOD), WM, mark, mark))
t = Term(path, env={"DT_SUPERVISE": "on"}, rows=ROWS, cols=COLS, settle=1.5)
t.collect(2.0)
before = t.screen().find("┤ Hello ├")
t.collect(16.0)
sc = t.screen()
log = open(t.log).read() if os.path.exists(t.log) else ""
check("a desktop that dies on a signal is started again, its windows back",
      before == (6, 12) and sc.find("┤ Hello ├") == (6, 12)
      and "died on signal 11" in log and "restored 1 windows" in log, log)
t.quit(b"qy", 3)
check("and quits as usual afterwards", t.exited and t.status == 0, t.status)

# A SIGTERM is a request to stop: the windows are written down, the desktop
# exits 143, and the next start reopens them once; after a Quit, nothing.
ST = os.path.join(RD, "state")
def recsess(name, extra):
    p = os.path.join(RD, name)
    open(p, "w").write("%s. %s\ndt_open\n%s\ndt_run\ndt_close\n" % (load(MOD), WM, extra))
    return p
t = Term(recsess("a.hibr", '[ "$DT_RESTORED" = 1 ] || dt_new "Kept" 8 30 6 10\n( sleep 2; kill -TERM $$ ) &'),
         env={"XDG_STATE_HOME": ST}, rows=ROWS, cols=COLS, settle=1.0)
t.collect(4.0)
t.wait(3)
check("SIGTERM writes the windows down and exits 143",
      t.exited and t.status == 143 * 256, t.status)
t = Term(recsess("b.hibr", ""), env={"XDG_STATE_HOME": ST}, rows=ROWS, cols=COLS, settle=1.5)
t.collect(2.0)
sc = t.screen()
check("the next start reopens them, and says so",
      sc.find("┤ Kept ├") == (6, 12) and sc.find("Reopened") is not None, sc)
t.quit(b"qy", 3)
t = Term(recsess("c.hibr", ""), env={"XDG_STATE_HOME": ST}, rows=ROWS, cols=COLS, settle=1.5)
t.collect(2.0)
sc = t.screen()
check("a Quit leaves nothing to reopen", sc.find("┤ Kept ├") is None, sc)
t.quit(b"qy", 3)

# Restart Desktop carries a terminal's program across the exec as this
# process's child, so a restart that carries one -- here from a desktop
# that had no supervisor into one that would -- stays unsupervised: the
# program is still the desktop's own child, and the window comes back.
path = os.path.join(RD, "rs.hibr")
open(path, "w").write("%s. %s\n%s\n[ -n \"$DT_RESTORE\" ] && export DT_SUPERVISE=on\ndt_open\n"
                      "[ \"$DT_RESTORED\" = 1 ] || dt_launch term\ndt_run\ndt_close\n" % (load(MOD), WM, TERMLOAD))
t = Term(path, rows=ROWS, cols=COLS, settle=1.5)
t.collect(2.0)
t.keys([b"\x1b[21~", 0.3, b"r", 4.0])
kids = open("/proc/%d/task/%d/children" % (t.pid, t.pid)).read().split()
log = open(t.log).read() if os.path.exists(t.log) else ""
check("a restart carrying a terminal's program stays unsupervised, the program its child",
      "no supervisor until the next start" in log and "restored 1 windows" in log
      and len(kids) == 1 and "[desktop]" not in open("/proc/%s/cmdline" % kids[0]).read(), log)
t.quit(b"qy", 3)

# An app's arithmetic slip fails the command, not the desktop: the shell's
# keepgoing option, which dt_open turns on.
expect(r"arithmetic: syntax error")
path = os.path.join(RD, "slip.hibr")
open(path, "w").write("%s. %s\ndt_open\nslip_key() { [ \"$2\" = x ] || return 1; echo $(( 1 + )); return 0; }\n"
                      "dt_app slip Slip 8 30\ndt_new Slip 8 30 6 10 slip\ndt_run\ndt_close\n" % (load(MOD), WM))
t = Term(path, rows=ROWS, cols=COLS, settle=1.5)
t.collect(1.5)
t.keys([b"x", 0.5])
sc = t.screen()
t.quit(b"qy", 3)
log = open(t.log).read() if os.path.exists(t.log) else ""
check("an arithmetic error in an app is logged and the desktop carries on",
      sc.find("┤ Slip ├") is not None and "arithmetic" in log and t.exited and t.status == 0,
      (log, t.status))

# --- the wallpaper as pixels -------------------------------------------------
#
# The wallpaper is the one picture with text drawn over it in the same frame --
# the bar, every window, the icons -- so it is placed under the text
# (`img draw -u`) and the cells it covers are blanked with no colours of their
# own first: a bitmap writes no cells, and a cell with a background colour
# paints over a picture the terminal is compositing below the glyphs. It was
# never pixels at all before 0.99.79 (Gitea #104), on sixel either.
import struct as _struct, zlib as _zlib

WPD = tempfile.mkdtemp(prefix="hibr-wall-")


def wallpng(name, w, h):
    raw = b"".join(b"\x00" + bytes(v for x in range(w)
                                   for v in (x % 256, y * 2 % 256, 160))
                   for y in range(h))

    def chunk(tag, data):
        body = tag + data
        return (_struct.pack(">I", len(data)) + body
                + _struct.pack(">I", _zlib.crc32(body)))

    path = os.path.join(WPD, name)
    open(path, "wb").write(
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", _struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
        + chunk(b"IDAT", _zlib.compress(raw)) + chunk(b"IEND", b""))
    return path


WALL = wallpng("wall.png", 160, 100)


# Wider than the screen in pixels at 80 cells of 8, which is what makes
# center's own column negative.
WIDE = wallpng("wide.png", 1600, 200)


def wallrun(mode, gfx="kitty", feed=(), wmode="stretch", wall=None):
    """A desktop with a wallpaper, on a pty that says what a cell measures."""
    path = os.path.join(WPD, "s.hibr")
    open(path, "w").write(
        "%s. %s\nDT_IMGMODE=%s\nDT_WALLIMG=%s\nDT_WALLMODE=%s\n"
        "dt_open\ndt_new \"Hello\" 8 30 6 10\ndt_run\ndt_close\n"
        % (load(MOD, "build/mods/img.so"), WM, mode, wall or WALL, wmode))
    t = Term(path, env={"DT_TICK": "60", "HIBR_GFX": gfx}, rows=ROWS,
             cols=COLS, settle=1.0, cellw=8, cellh=16)
    t.collect(1.0)
    t.keys(feed)
    sc = t.screen()
    t.quit(b"qy", 1.0)
    return sc


sc = wallrun("pixels")
check("the wallpaper is one picture at the top left corner",
      len(sc.images) == 1 and sc.images[0][:2] == (0, 0), sc.images)
check("and the bar and a window are still text over it",
      sc.find("Window") is not None and sc.find("┤ Hello ├") is not None, sc)
check("placed under the text, so a terminal that keeps it is sent it once",
      len([a for a in sc.apc if "a=T" in a]) == 1 and
      any("z=-1" in a for a in sc.apc), [a[:40] for a in sc.apc][:2])
sc = wallrun("half")
check("asked for blocks it is cells, with nothing sent as a bitmap",
      not sc.images and sc.find("┤ Hello ├") is not None, (sc.images, sc))


# A picture under the text is kept only while its caller places it again, so
# the frame that stops placing it is the frame it goes. dt_wall is allowed to
# paint nothing when nothing it paints has changed, and from 0.99.83 that
# frame took the whole wallpaper off the screen until the next key -- reported
# as shadows blinking and tearing while a window moved, since what a shadow
# falls on is cells blanked for the picture one moment and painted the next.
# A gated frame places it too now, which costs nothing: img keeps a record of
# what it last placed and asks the display to keep what it has, where handing
# the pixels over again makes the display hash all 6.3 MB of them -- 19.6 ms
# for a screenful, on every frame that painted the wallpaper, which is what
# made a desktop in Pixels cost what it did (a frame went 23.8 ms to 2.2).
def wallsent(sc):
    """What the terminal was told about pictures: placements and deletes. A
    live *set* is the wrong assertion -- the old bug dropped the picture and
    placed a new one with a fresh id, so the set is size 1 again whenever the
    last frame happened to be one that painted, which is pure timing. One
    placement and no delete at all is the thing that must hold."""
    return ([a for a in sc.apc if "a=T" in a], [a for a in sc.apc if "a=d" in a])


put, gone = wallsent(wallrun("pixels", feed=[press(6, 12), 0.3, drag(9, 30),
                                             0.3, release(9, 30), 1.4]))
check("a wallpaper of real pixels is placed once and never deleted, though a "
      "window moved over it", len(put) == 1 and not gone,
      "%d placed, %d deleted" % (len(put), len(gone)))
put, gone = wallsent(wallrun("pixels", feed=[2.4]))
check("nor by two seconds of being left alone, which once took it away",
      len(put) == 1 and not gone,
      "%d placed, %d deleted" % (len(put), len(gone)))
# zoom and center mean the picture overflows and is cropped, so dt_wallfit
# hands over a rectangle taller or wider than the screen, with a negative row
# or column. Until 0.99.95 the display refused exactly that and img fell back
# to half blocks, so neither mode had ever been pixels on any terminal, and
# DT_IMGMODE had no effect for anyone using either (Gitea #117). Measured
# against 0.99.94's own image.c: zero placements for both, one for stretch.
sc = wallrun("pixels", feed=[2.4], wmode="zoom")
put, gone = wallsent(sc)
check("a zoom wallpaper is pixels, cropped to the screen rather than refused",
      len(put) == 1 and not gone and sc.images[0][:2] == (0, 0),
      "%d placed, %d deleted, %s" % (len(put), len(gone), sc.images))
check("and it is the whole screen's worth of cells, not the fitted rectangle",
      len(put) == 1 and "c=%d" % COLS in put[0] and "r=%d" % ROWS in put[0],
      put)
check("the window is still drawn as text over it", sc.find("┤ Hello ├") is not None, sc)
# center uses the source's own pixel width as a column count, so a picture
# wider than the screen in pixels gives a negative column -- the other half
# of the same bug.
sc = wallrun("pixels", feed=[2.4], wmode="center", wall=WIDE)
put, gone = wallsent(sc)
check("so is a center wallpaper wider than the screen",
      len(put) == 1 and not gone, "%d placed, %d deleted" % (len(put), len(gone)))
shutil.rmtree(WPD, True)

report(556)
