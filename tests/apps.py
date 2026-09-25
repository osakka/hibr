#!/usr/bin/env python3
"""Drive the desktop's apps through a pty: every one in examples/desktop/apps/.

tests/desktop.py checks the window manager with apps small enough to fit in
the test file. This checks the real ones: the calculator proves keys and
clicks reaching a focused window, the browser proves scrolling *inside* one,
the panel proves a multi-pane app with its own picker list, the terminal
proves a real program in a window (two of them, as two sessions), and the
games prove animation on the clock.
Run it directly:  python3 tests/apps.py [path-to-hibr]
"""
import os, re, shutil, subprocess, sys, tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import screen as sx
from screen import Term, check, report, press, release, drag, wheel, load, tree

if len(sys.argv) > 1:
    sx.HIBR = os.path.abspath(sys.argv[1])
WM = tree("examples/desktop/desktop.hibr")
APPS = tree("examples/desktop/apps")
DA = tree("examples/desktop/desk-accessories")
D = tempfile.mkdtemp(prefix="hibr-apps-")
S = tempfile.mkdtemp(prefix="hibr-apps-session-")

# A directory with a known listing: two directories and twelve files, so the
# browser has more entries than the window can show and the scrollbar and the
# wheel have something to do.
os.mkdir(os.path.join(D, "beta"))
os.mkdir(os.path.join(D, "alpha"))
for i in range(12):
    open(os.path.join(D, "file%02d.txt" % i), "w").write("x\n")
# alpha/ and beta/ come first, then .. above them, so the list is:
#   0 ..   1 alpha/   2 beta/   3 file00.txt ... 14 file11.txt
ENTRIES = 15


def appdir(a):
    """Which of examples/desktop/apps or examples/desktop/desk-accessories has a.hibr."""
    for d in (APPS, DA):
        if os.path.exists(os.path.join(d, a + ".hibr")):
            return d
    return APPS


def run(app, win, feed=(), pre="", wait=1.0, also=(), end=b"qy", extra=(),
        env=None):
    """Open one app in a window at a known place and drive it.

    `also` adds further windows after it, as (title, geometry, app) triples,
    which is what the control panel needs: it has nothing to show until
    there is something else open. `end` is the keys that finish it -- q
    opens Quit's own confirm box, so the default is qy, not q; a test of a
    terminal passes None, since the program inside would take the q.

    `env` is real process environment, in place at the very first line of
    the script -- unlike a `pre` line, which cannot reach a path an app
    computes once at its own source time, before `pre` ever runs. Note Pad's
    own NP_FILE is exactly that, the same as the desktop's own DT_CONF.
    """
    p = os.path.join(S, "session.hibr")
    src = "".join(". %s/%s.hibr\n" % (appdir(a), a)
                  for a in dict.fromkeys([app] + [x[2] for x in also if x[2]]
                                         + list(extra)))
    more = "".join('dt_new "%s" %s %s\n' % x for x in also)
    open(p, "w").write(
        "%s. %s\n%s%s\ndt_open\ndt_new \"%s\" %s %s\n%s"
        "dt_run\ndt_close\n"
        % (load("console"), WM, src, pre, app.title(), win, app,
           more + ("dt_raise 1\n" if also else "")))
    t = Term(p, env=dict({"DT_TICK": "60"}, **(env or {})), settle=0.6)
    t.keys(feed)
    t.quit(end, wait)
    sc = t.screen()
    sc.out = t.out
    return sc


def calc_key(i):
    """Where key i of the keypad lands on screen, for a window at row 2 col 2."""
    return 6 + (i // 4) * 2, 4 + (i % 4) * 5 + 1


def cli(app, *args):
    out = subprocess.run(
        [sx.HIBR, "%s/%s.hibr" % (appdir(app), app)] + list(args),
        capture_output=True, text=True, cwd=D)
    return out.returncode, out.stdout.strip(), out.stderr.strip()


# --- the calculator -------------------------------------------------------

rc, out, err = cli("calc", "3 * (4 + 5)")
check("it evaluates from the command line", (rc, out) == (0, "27"))
rc, out, err = cli("calc", "2 ** 16")
check("powers work, because the shell has them", out == "65536")
rc, out, err = cli("calc", "2 +")
check("a malformed expression fails with a reason",
      rc == 1 and "not an expression" in err)
rc, out, err = cli("calc", "PATH")
check("a variable name is refused rather than answered",
      rc == 1 and "digits and operators" in err)

CW = "16 24 2 2"
sc = run("calc", CW)
check("the keypad is drawn", sc.find(" 7  ") == (6, 4) and
      sc.find(" =  ") == (14, 19), sc)
check("it starts empty", sc.find("expression") is not None and
      sc.find(" 0") is not None, sc)
check("the keypad has one sensible size, so the window is fixed",
      sc.find("┤_ x├") is not None and sc.find("┤_ □ x├") is None and
      sc.g[17][25] == "┘", sc)

sc = run("calc", CW, [press(*calc_key(0)), press(*calc_key(9)),
                      press(*calc_key(19))])
check("clicking keys builds an expression and = evaluates it",
      sc.find("72") is not None, sc)

sc = run("calc", CW, [b"6", b"*", b"7", b"="])
check("typing does the same", sc.find("42") is not None, sc)

sc = run("calc", CW, [b"9", b"9", b"c"])
check("c clears", sc.find("expression") is not None, sc)

sc = run("calc", CW, [b"1", b"2", b"\x7f"])
# Pinned to the expression's own position, not sc.find(" 12") is None
# anywhere on screen -- that matched the menu bar's clock once an hour, at
# 12 o'clock. " 1 " at this exact spot already rules out "12" being there
# instead: the character right after "1" would be "2", not a space.
check("backspace takes the last character back",
      sc.find(" 1 ") == (3, 3), sc)

sc = run("calc", CW, [b"2", b"+", b"="])
check("an incomplete expression says so rather than answering",
      sc.find("not an expression") is not None, sc)

# --- the file browser -----------------------------------------------------

rc, out, err = cli("files", D)
names = out.split("\n")
check("it lists from the command line", rc == 0 and len(names) == ENTRIES)
check("directories come first, with .. above them",
      names[:3] == ["../", "alpha/", "beta/"], out)
check("files follow, in order", names[3] == "file00.txt" and
      names[-1] == "file11.txt", out)
rc, out, err = cli("files", "/")
check("root has no entry above it", not out.startswith("../"), out)
rc, out, err = cli("files", "/nope")
check("a path that is not a directory fails", rc == 1, err)

FW = "12 34 2 2"
PRE = 'FB_DIR=%s' % D

sc = run("files", FW, pre=PRE)
check("the window shows the path it is in", sc.find(D) == (3, 3), sc)
check("the first entries are drawn", sc.find(" ../") == (4, 3) and
      sc.find(" alpha/") == (5, 3), sc)
check("the selection starts on the first row",
      sc.find("1 of %d" % ENTRIES) is not None, sc)
check("a list longer than the window gets a scrollbar",
      sc.at(4, 34) == "█" and sc.at(11, 34) == "│", sc)

sc = run("files", FW, [b"\x1b[B"] * 3, pre=PRE)
check("down moves the selection", sc.find("4 of %d" % ENTRIES) is not None, sc)
check("and the view has not moved yet", sc.find(" ../") == (4, 3), sc)

sc = run("files", FW, [b"\x1b[B"] * 9, pre=PRE)
check("going past the bottom scrolls the view",
      sc.find(" ../") is None and
      sc.find("10 of %d" % ENTRIES) is not None, sc)

sc = run("files", FW, [b"\x1b[F"], pre=PRE)
check("end goes to the last entry",
      sc.find("%d of %d" % (ENTRIES, ENTRIES)) is not None and
      sc.find("file11.txt") is not None, sc)
check("and the scrollbar thumb is at the bottom", sc.at(11, 34) == "█", sc)

sc = run("files", FW, [b"\x1b[F", b"\x1b[H"], pre=PRE)
check("home comes back", sc.find("1 of %d" % ENTRIES) is not None and
      sc.find(" ../") == (4, 3), sc)

sc = run("files", FW, [wheel(6, 10, up=False)], pre=PRE)
check("the wheel scrolls the view", sc.find(" ../") is None and
      sc.find(" file00.txt") == (4, 3), sc)
check("without moving the selection",
      sc.find("1 of %d" % ENTRIES) is not None, sc)

sc = run("files", FW, [wheel(6, 10, up=False), wheel(6, 10, up=True)], pre=PRE)
check("and scrolls back", sc.find(" ../") == (4, 3), sc)

sc = run("files", FW, [wheel(6, 10, up=True)], pre=PRE)
check("the wheel cannot scroll above the first entry",
      sc.find(" ../") == (4, 3), sc)

sc = run("files", FW, [press(6, 10)], pre=PRE)
check("a click selects the row it landed on",
      sc.find("3 of %d" % ENTRIES) is not None, sc)

sc = run("files", FW, [press(5, 10), press(5, 10)], pre=PRE)
check("a second click on the same row enters the directory",
      sc.find(os.path.join(D, "alpha")) is not None and
      sc.find("1 of 1") is not None, sc)

sc = run("files", FW, [press(5, 10), press(5, 10), b"\x7f"], pre=PRE)
check("backspace goes back up", sc.find(D) == (3, 3) and
      sc.find("1 of %d" % ENTRIES) is not None, sc)

sc = run("files", FW, [wheel(6, 10, up=False), press(4, 10)], pre=PRE)
check("a click after scrolling lands on the row that is there",
      sc.find("4 of %d" % ENTRIES) is not None, sc)

# --- the control panel ----------------------------------------------------
#
# Control Panel is a picker now, not one scrolling list: a pane down the
# left, the selected pane's own rows on the right (System 7's Control
# Panels folder). Panes are loaded from examples/desktop/control-panel, sorted by
# title without regard to case, the same trick dt_appnames uses for apps --
# which is why App Shortcuts sorts ahead of Appearance: a space is less
# than a letter, plain byte order, the same rule already governing the app
# list. Verified once below, not assumed, and ORDER mirrors it exactly
# rather than guessing at ASCII sort trivia a second time.

rc, out, err = cli("panel")
check("with no panes loaded, it says so rather than pretending",
      rc == 0 and out == "no panes registered", out)

CP = tree("examples/desktop/control-panel")
CPLOAD = ('. %s\n. %s/panel.hibr\nCP_PANEDIRS+=("%s")\ncp_panes\n'
          % (WM, APPS, CP))
out = subprocess.run([sx.HIBR, "-c", CPLOAD + "echo ${CP_PANE_LIST[*]}"],
                     capture_output=True, text=True).stdout.strip()
ORDER = out.split()
check("panes register and sort by title, not load order",
      ORDER == ["app_shortcuts", "appearance", "behaviour", "control_strip",
                "datetime", "shortcuts"], out)

PW = "20 58 2 2"
PANEL = ("panel", PW)
TICK = "DT_TICK=200"
CPANES = 'CP_PANEDIRS+=("%s")\ncp_panes' % CP

# Absolute screen coordinates for PW's geometry (row 2, col 2): a pane's
# own first content row is at 3, its dropdown/checkbox column at 47 --
# derived once here from CP_LISTW and the window's body width, rather than
# copied by eye into every check below.
R0, VALCOL = 3, 47
LISTCOL = 5
TITLE = {"app_shortcuts": "App Shortcuts", "appearance": "Appearance",
         "behaviour": "Behaviour", "control_strip": "Control Strip",
         "datetime": "Date & Time", "shortcuts": "Shortcuts"}


def prow(name):
    """Which screen row a pane's own name sits at in the picker list."""
    return R0 + ORDER.index(name)


def downs(name):
    """How many downs on the picker, from the default pane, reach it."""
    return ORDER.index(name)


def cprun(feed=(), also=(), extra=(), tz=None):
    """Run Control Panel from a config directory of its own.

    tests/screen.py gives the whole suite one shared $HOME, so without this
    a theme set by one check would still be in effect for the next one --
    which is exactly how "left cycles it the other way" once passed by
    reading a value "right" had left behind a moment before, rather than by
    actually cycling from the default. Each check gets its own directory
    instead, so its math holds regardless of what ran before it.

    tz, if given, is exported before cp_panes loads the panes, so the Date
    & Time pane's own lookup is deterministic rather than whatever zone the
    machine running the suite happens to be in.
    """
    d = tempfile.mkdtemp(prefix="hibr-cp-")
    tzline = "export TZ=%s\n" % tz if tz else ""
    sc = run(*PANEL, feed=feed, pre="export XDG_CONFIG_HOME=%s\n%s%s\n%s"
             % (d, tzline, TICK, CPANES), also=also, extra=extra)
    shutil.rmtree(d, True)
    return sc


sc = cprun()
check("the picker lists every pane, sorted by title",
      all(TITLE[n] in sc.row(prow(n)) for n in ORDER), sc)
check("the first pane's own rows show on the right without entering it",
      sc.find(TITLE[ORDER[0]]) is not None and
      sc.find("Control Panel") is not None, sc)

DOWN_APP = [b"\x1b[B"] * downs("appearance")
DOWN_BEH = [b"\x1b[B"] * downs("behaviour")
DOWN_DT = [b"\x1b[B"] * downs("datetime")

sc = cprun(DOWN_APP)
check("down on the picker moves pane by pane, showing each one's rows",
      sc.find("Theme") is not None and sc.find("midnight") is not None, sc)

sc = cprun(DOWN_APP + [b"\x1b[C", b"\x1b[C"])
check("right enters the pane, and a second right cycles its first row",
      sc.find("slate") is not None and sc.find("midnight") is None, sc)
sc = cprun(DOWN_APP + [b"\t", b"\x1b[D"])
check("tab enters it too, and left cycles the other way",
      sc.find("dracula") is not None, sc)
sc = cprun(DOWN_APP + [b"\t", b"\t", b"\x1b[C"])
check("a second tab leaves the pane, back to moving the picker",
      sc.find("slate") is None and
      sc.find(TITLE[ORDER[downs("appearance") + 1]]) is not None, sc)

sc = cprun(DOWN_APP + [press(R0, VALCOL)])
check("clicking the dropdown's own cell opens a real popup of choices",
      sc.find("slate") is not None and sc.find("dracula") is not None, sc)
sc = cprun(DOWN_APP + [press(R0, VALCOL), press(R0 + 6, VALCOL + 3)])
check("choosing one there applies it, the same as cycling would",
      sc.find("dracula") is not None and sc.find("midnight") is None, sc)

sc = cprun(DOWN_APP + [b"\x1b[C", b"\x1b[B", b"\x1b[C"])
check("the wallpaper glyph changes, and the desktop follows",
      sc.at(0, 78) != "·" and sc.at(23, 60) == "░", sc)

# Behaviour's own rows, in order: Refresh(0), Icons(1), Disk Icons(2),
# Cursor(3), Cursor Blink(4), Window Shadow(5), Menu Shadow(6), Bar
# Shadow(7), Titlebar Click(8), About Refresh(9, only once about.hibr is
# loaded).
sc = cprun(DOWN_BEH)
check("Behaviour's own second row is Icons, right there with no headings",
      sc.find("Icons") is not None and
      "[x]" in sc.row(sc.find("Icons")[0]), sc)

sc = cprun(DOWN_BEH + [b"\x1b[C", b"\x1b[B", b"\r"])
check("the icons can be switched off, and the panel shows an unchecked box",
      sc.find("Icons") is not None and
      "[ ]" in sc.row(sc.find("Icons")[0]), sc)

sc = cprun(DOWN_BEH + [b"\x1b[C"] + [b"\x1b[B"] * 6 + [b"\r"])
check("menu shadow is its own setting, separate from window shadow",
      sc.find("Menu Shadow") is not None and
      "[ ]" in sc.row(sc.find("Menu Shadow")[0]) and
      "[x]" in sc.row(sc.find("Window Shadow")[0]), sc)

sc = cprun(DOWN_BEH + [b"\x1b[C"] + [b"\x1b[B"] * 7 + [b"\r"])
check("bar shadow is a third, separate setting again",
      sc.find("Bar Shadow") is not None and
      "[ ]" in sc.row(sc.find("Bar Shadow")[0]) and
      "[x]" in sc.row(sc.find("Menu Shadow")[0]), sc)

sc = cprun(DOWN_BEH + [b"\x1b[C"] + [b"\x1b[B"] * 8)
check("titlebar double-click defaults to zoom",
      sc.find("Titlebar Click") is not None and
      "zoom" in sc.row(sc.find("Titlebar Click")[0]), sc)
sc = cprun(DOWN_BEH + [b"\x1b[C"] + [b"\x1b[B"] * 8 + [b"\x1b[C"])
check("and it cycles through the other actions",
      "min" in sc.row(sc.find("Titlebar Click")[0]), sc)

sc = cprun(DOWN_BEH + [b"\x1b[C"] + [b"\x1b[B"] * 9, extra=("about",))
check("About Refresh only appears once About hibr itself is loaded",
      sc.find("About Refresh") is not None and
      sc.find("3000 ms") is not None, sc)

# Date & Time is a custom pane -- a body of its own, not rows -- proving
# that shape rather than the row-list one every other pane above uses.
# TZ is pinned so the coordinate is deterministic regardless of where the
# suite runs.
sc = cprun(DOWN_DT, tz="Europe/London")
check("the Date & Time pane shows the clock, the date and the zone",
      sc.find("Europe/London") is not None and
      sc.find("51N") is not None and sc.find("0W") is not None, sc)
check("and a mark for it on the reused world map",
      sc.find("◉") is not None, sc)

sc = cprun([press(R0, LISTCOL), press(R0, LISTCOL)])
check("clicking the same pane twice in the picker is harmless",
      sc.find(TITLE[ORDER[0]]) is not None, sc)

sc = cprun(DOWN_APP + [press(R0, VALCOL - 10), press(R0, VALCOL - 10)])
check("a click in the pane's own body selects and a second click acts",
      sc.find("slate") is not None, sc)

sc = cprun([press(20, LISTCOL)])
check("clicking below the last pane in the picker does nothing",
      sc.find(TITLE[ORDER[0]]) is not None, sc)

sc2 = run("tasks", "16 50 4 4", feed=[b"\x1b\x14"], extra=("term",))
check("alt-ctrl-t opens a terminal, even with another app focused",
      sc2.find("┤ Terminal ├") is not None, sc2)
sc3 = run("files", FW, feed=[b"\x1b\x10"], pre=PRE, extra=("tasks",))
check("alt-ctrl-p opens the task manager",
      sc3.find("┤ Task Manager ├") is not None, sc3)

# --- the desk accessories --------------------------------------------------
#
# Ordinary apps, kept in examples/desktop/desk-accessories rather than examples/desktop/apps
# only so the hibr menu groups them (see tests/desktop.py for that part);
# nothing about running one is different, which is the point of #32.

PZW = "13 22 2 2"

sc = run("puzzle", PZW)
vals = set()
for r in range(4):
    row = 5 + r
    for c in range(4):
        col0 = 5 + 4 * c
        v = sc.row(row)[col0:col0 + 3].strip()
        vals.add(int(v) if v else 0)
check("the tiles are a full shuffle of 1 to 15 and one blank",
      vals == set(range(16)), sc)

sc = run("puzzle", PZW, feed=[b"\x1b[A", b"\x1b[B", b"\x1b[C", b"\x1b[D"])
check("an arrow key that can move the blank slides a tile and counts it",
      "moves 0" not in sc.row(10), sc)

sc = run("puzzle", PZW, feed=[b"\x1b[A", b"\x1b[B", b"\x1b[C", b"\x1b[D",
         b"n"])
check("n starts a new game, resetting the move count",
      "moves 0" in sc.row(10), sc)

WINSCRIPT = os.path.join(S, "puzzle-win.hibr")
open(WINSCRIPT, "w").write('''. %s
. %s
id=1
i=0
while [ "$i" -lt 14 ]; do
	PZ[$id][$i]=$((i + 1))
	i=$((i + 1))
done
PZ[$id][14]=0
PZ[$id][15]=15
PZ[$id]["blank"]=14
PZ[$id]["moves"]=0
PZ[$id]["state"]=run
pz_slide "$id" right
echo "${PZ[$id]["state"]}"
''' % (WM, os.path.join(DA, "puzzle.hibr")))
out = subprocess.run([sx.HIBR, WINSCRIPT], capture_output=True,
                     text=True).stdout.strip()
check("sliding the last tile into place is recognised as solved",
      out == "won", out)
os.unlink(WINSCRIPT)

# end=None throughout: notepad_key takes every printable character as
# text, q included, so the usual qy quit sequence would type itself into
# the note rather than closing the window -- the same reason a terminal
# test never uses the default end either.
NPD = tempfile.mkdtemp(prefix="hibr-notepad-")
sc = run("notepad", "12 40 2 2", feed=[b"h", b"i"],
         env={"XDG_CONFIG_HOME": NPD}, end=None)
check("typing appears in the window", sc.find("hi") is not None, sc)
NPFILE = os.path.join(NPD, "hibr", "notepad.txt")
text = open(NPFILE).read() if os.path.exists(NPFILE) else ""
check("and is saved to disk as it is typed", text == "hi\n", text)
shutil.rmtree(NPD, True)

NPD2 = tempfile.mkdtemp(prefix="hibr-notepad-")
sc = run("notepad", "12 40 2 2", feed=[b"h", b"i", b"\x08"],
         env={"XDG_CONFIG_HOME": NPD2}, end=None)
check("backspace removes the last character typed",
      sc.find("hi") is None and sc.find("h") is not None, sc)
shutil.rmtree(NPD2, True)

NPD3 = tempfile.mkdtemp(prefix="hibr-notepad-")
sc = run("notepad", "12 40 2 2", feed=[b"a", b"\r", b"b"],
         env={"XDG_CONFIG_HOME": NPD3}, end=None)
posa, posb = sc.find("a"), sc.find("b")
check("enter starts a new line",
      posa is not None and posb is not None and posa[0] != posb[0], sc)
text3 = open(os.path.join(NPD3, "hibr", "notepad.txt")).read()
check("both lines are saved", text3 == "a\nb\n", text3)
shutil.rmtree(NPD3, True)

NPD4 = tempfile.mkdtemp(prefix="hibr-notepad-")
run("notepad", "12 40 2 2", feed=[b"h", b"e", b"l", b"l", b"o"],
    env={"XDG_CONFIG_HOME": NPD4}, end=None)
sc = run("notepad", "12 40 2 2", env={"XDG_CONFIG_HOME": NPD4}, end=None)
check("reopening it loads the saved note", sc.find("hello") is not None, sc)
shutil.rmtree(NPD4, True)

# --- the terminal window --------------------------------------------------

TW = "14 44 2 2"
TERM = ("term", TW)
SH = "TW_CMD=(/bin/sh -c 'PS1=\"sh> \"; export PS1; exec /bin/sh')"

sc = run(*TERM, pre=SH,
         wait=1.6)
check("a shell starts in the window", sc.find("sh>") is not None, sc)

sc = run(*TERM, feed=[b"e", b"c", b"h", b"o", b" ", b"h", b"i", b"\r"],
         pre=SH,
         wait=1.6)
check("what is typed reaches it and what it says comes back",
      sc.find("echo hi") is not None and "│hi " in sc.row(4), sc)

# Two terminals are two sessions: each its own pty and its own shell, and a
# key typed into one never reaches the other.
TWO = "TW_CMD=(/bin/sh -c 'tty; PS1=\"sh> \"; export PS1; exec /bin/sh')"
sc = run("term", "10 36 2 2", feed=[b"e", b"c", b"h", b"o", b" ", b"o",
         b"n", b"e", b"\r"], pre=TWO, wait=1.6,
         also=[("Term", "10 36 2 40", "term")])
ttys = [r for r in range(24) if "/dev/pts/" in sc.row(r)]
check("two terminals are two sessions, each on its own pty",
      len(ttys) == 1 and sc.row(ttys[0]).count("/dev/pts/") == 2 and
      len(set(x.split()[0] for x in
              sc.row(ttys[0]).split("/dev/pts/")[1:])) == 2, sc)
check("and what is typed in one does not reach the other",
      sc.row(4).count("echo one") == 1 and sc.row(5).count("one") == 1 and
      "│sh>  " in sc.row(4), sc)

# DT_CURSOR is what a new terminal starts with, and only the focused one
# draws a cursor at all -- end=None, since the default quit key would land
# on the prompt and move it before the screen is read.
sc = run(*TERM, pre=SH + "\nDT_CURSOR=bar", wait=1.6,
         also=[("Term", "14 44 2 50", "term")], end=None)
check("DT_CURSOR sets a new terminal's cursor, drawn only where focused",
      sc.row(3).count("▏") == 1 and
      sum(sc.row(r).count("▏") for r in range(24)) == 1, sc)

sc = run(*TERM, feed=[b"\x1b[21~"],
         pre=SH,
         wait=1.2)
check("f10 still reaches the menu bar, not the program",
      sc.find("About hibr") is not None, sc)

sc = run(*TERM, pre="TW_CMD=(/bin/sh -c 'exit 4')", wait=1.6)
check("a program that ends says so in the window",
      sc.find("exited 4") is not None, sc)

sc = run(*TERM, pre="TW_CMD=(/bin/sh -c 'exit 0')", wait=1.6)
check("and one that ends cleanly closes the window instead of asking",
      sc.find("┤ Term ├") is None and sc.find("exited 0") is None and
      "✎" in sc.row(0), sc)

# Scrollback, and the mouse for a program that asks for it.
LONG = "TW_CMD=(/bin/sh -c 'i=1; while [ $i -le 40 ]; do echo \"row $i\"; i=$((i+1)); done; exec cat')"
sc = run(*TERM, feed=[wheel(8, 10)], pre=LONG, wait=1.2, end=None)
check("the wheel scrolls back through what went off the top",
      sc.find("↑ 3 of 29") is not None and sc.find("row 27") == (3, 3) and
      sc.find("row 40") is None, sc)

sc = run(*TERM, feed=[b"\x1b[5;2~"], pre=LONG, wait=1.2, end=None)
check("shift-pageup pages back a screenful less one",
      sc.find("↑ 11 of 29") is not None and sc.find("row 19") == (3, 3), sc)

sc = run(*TERM, feed=[wheel(8, 10), b"x"], pre=LONG, wait=1.2, end=None)
check("and a key goes back to the live screen",
      sc.find("↑") is None and sc.find("row 40") is not None, sc)

TRAP = ("TW_CMD=(/bin/sh -c 'trap \"echo caught\" INT; "
        "while :; do sleep 0.1; done')")
sc = run(*TERM, feed=[b"\x03"], pre=TRAP, wait=1.0, end=None)
check("ctrl-c reaches the program in a focused terminal window",
      sc.find("caught") is not None, sc)

# SGR 4's colon sub-parameter picks an underline style, and 0 means none --
# a modern program uses 4:0 to turn underline off, in place of 24, and a
# parser that reads only the digits up to the colon turns it on instead.
UNDER = "TW_CMD=(/bin/sh -c 'printf \"\\033[4:0mWORD\\033[0m\"; sleep 5')"
sc = run(*TERM, pre=UNDER, wait=1.0, end=None)
m = re.search(rb"\x1b\[([0-9;]*)mWORD", sc.out)
check("SGR 4:0 turns underline off, not on -- the colon is not a digit",
      m is not None and b"4" not in m.group(1).split(b";"), sc)

# Copy and paste: a drag selects, alt-c copies -- to the desktop and, with
# OSC 52, to the clipboard of the terminal the desktop runs on -- and alt-v
# pastes.  "row 30" is the top line once forty rows have gone past.
sc = run(*TERM, feed=[press(3, 3), drag(3, 8), release(3, 8), b"\x1bc",
                      b"\x1bv"], pre=LONG, wait=1.0, end=None)
check("a drag in a terminal selects, and alt-c copies it everywhere",
      b"\x1b]52;c;cm93IDMw\x07" in sc.out, sc)
check("and alt-v pastes it back into the program",
      "row 30" in sc.row(14), sc)

CLICK = ("TW_CMD=(/bin/sh -c 'stty raw -echo; "
         "printf \"\\033[?1000h\\033[?1006h\"; head -c 18 | cat -v; sleep 5')")
sc = run(*TERM, feed=[press(6, 10), release(6, 10)], pre=CLICK, wait=1.2, end=None)
check("a click reaches a program that asked for the mouse, where it landed",
      sc.find("^[[<0;8;4M^[[<0;8;4m") is not None, sc)

# --- the games --------------------------------------------------------------
#
# Each one steps on the clock, not on keys, so a key it ignores ("z") is how a
# test lets time pass: the harness waits between keys.  "p" freezes a game,
# so what is asserted is what was on screen when it stopped.

SW = "18 42 2 2"
sc = run("snake", SW)
check("the snake waits in the middle for an arrow",
      sc.find("an arrow to start") is not None and
      sc.find("██████") is not None, sc)

sc = run("snake", SW, feed=[b"\x1b[B", b"p"])
col = [r for r in range(3, 20) if sc.at(r, 23) == "█"]
check("an arrow sets it off, and it goes that way",
      sc.find("paused") is not None and len(col) >= 3, sc)

sc = run("snake", SW, feed=[b"\x1b[A"] + [b"z"] * 5)
check("the wall ends the game", sc.find("bitten") is not None, sc)

MW = "15 31 2 2"
sc = run("mines", MW)
check("a new field is all hidden, ten mines to find",
      sc.text().count("·") >= 81 and sc.find("⚑ 10") is not None, sc)

sc = run("mines", MW, feed=[b" "])
check("the first cell opened is never a mine, and opens a region",
      sc.find("boom") is None and sc.find("[ ]") is not None, sc)

# With 72 mines, only the first cell and its neighbours are clear, so the
# first open is also the last one needed.
sc = run("mines", MW, feed=[b" "], pre="MINES_COUNT=72")
check("opening every clear cell wins", sc.find("cleared!") is not None, sc)

# With 71, one clear cell is left among 72: of the two corner cells at least
# one is a mine, whichever the dice chose.
UL = [b"\x1b[A"] * 4 + [b"\x1b[D"] * 4
sc = run("mines", MW, feed=[b" "] + UL + [b" ", b"\x1b[C", b" "],
         pre="MINES_COUNT=71")
check("a mine ends it and shows where the rest were",
      sc.find("boom") is not None and sc.text().count("✱") >= 2, sc)

sc = run("mines", MW, feed=[b" ", press(5, 4, 2)], pre="MINES_COUNT=71")
check("a right click plants a flag",
      sc.find("⚑ 70") is not None and sc.at(5, 5) == "⚑", sc)

BW = "20 44 2 2"
sc = run("bricks", BW)
check("the wall is up and the ball waits on the bat",
      sc.find("space to serve") is not None and
      all("████" in sc.row(r) for r in range(5, 10)) and
      sc.find("♥♥♥") is not None and sc.find("▀▀▀▀▀▀▀") == (20, 20), sc)

sc = run("bricks", BW, feed=[b"\x1b[D"])
check("the arrows move the bat, and the ball rides along",
      sc.find("▀▀▀▀▀▀▀") == (20, 17) and sc.at(19, 20) == "●", sc)

sc = run("bricks", BW, feed=[press(10, 36)])
check("a click puts the bat under it", sc.find("▀▀▀▀▀▀▀") == (20, 33), sc)

sc = run("bricks", BW, feed=[b" ", b"z", b"z", b"z"])
check("served, the ball knocks a brick out and scores it",
      sc.row(3)[3:6].strip() not in ("0", ""), sc)

# The calculator copies its answer and pastes only what is arithmetic.
CW = "16 24 2 2"
sc = run("calc", CW, feed=[b"6", b"*", b"7", b"=", b"\x1bc", b"\x1bv"])
check("the calculator's copy is its answer",
      b"\x1b]52;c;NDI=\x07" in sc.out, sc)
check("and a paste lands in the expression", sc.find("42") is not None and
      sc.find("expression") is None, sc)
sc = run("calc", CW, feed=[b"\x1b[200~12 apples + 3\x1b[201~"])
check("a paste from the real terminal reaches the app, filtered",
      sc.find("12  + 3") is not None, sc)
check("and Edit is on the menu bar", "Edit" in sc.row(0), sc)

# --- files: views, and dragging between windows -------------------------

sc = run("files", "12 60 2 2", [b"v"], pre=PRE)
check("the details view has sizes, times and permissions",
      sc.find("Size") is not None and sc.find("Mode") is not None and
      sc.find("drwxr-xr-x") is not None, sc)
sc = run("files", "12 60 2 2", [b"v", b"v", b"\x1b[C", b"\x1b[B"], pre=PRE)
check("the icon view is a grid, and the arrows move across and down it",
      sc.find("▤▤") is not None and sc.find("6 of %d" % ENTRIES) is not None,
      sc)

TWO = [("Files", "12 34 2 40", "files")]
# The right-hand window goes into alpha/ with two presses, then file00.txt
# is dragged across from the left-hand one and let go over it.
INTO = [press(5, 44), press(5, 44)]
DRAG = [press(7, 5), drag(7, 9), drag(9, 50)]
sc = run("files", FW, INTO + DRAG + [release(9, 50)], pre=PRE, also=TWO)
moved = os.path.exists(os.path.join(D, "alpha", "file00.txt")) and \
        not os.path.exists(os.path.join(D, "file00.txt"))
check("a file dragged to another window's folder is moved there", moved, sc)
if moved:
    os.rename(os.path.join(D, "alpha", "file00.txt"),
              os.path.join(D, "file00.txt"))
sc = run("files", FW, INTO + DRAG + [release(9, 50, 16)], pre=PRE, also=TWO)
copied = os.path.exists(os.path.join(D, "alpha", "file00.txt")) and \
         os.path.exists(os.path.join(D, "file00.txt"))
check("and with ctrl held it is copied instead", copied, sc)
sc = run("files", FW, INTO + DRAG + [release(9, 50, 16)], pre=PRE, also=TWO,
         end=None)
check("nothing is ever put over a file already there",
      sc.find("already has a file00.txt") is not None, sc)
if copied:
    os.unlink(os.path.join(D, "alpha", "file00.txt"))

TRASH = tempfile.mkdtemp(prefix="hibr-trash-")
sc = run("files", FW, [b"\x1b[B"] * 3 + [b"\x1b[3~"],
         pre=PRE + "\nDT_TRASH=%s" % TRASH)
info = os.path.join(TRASH, "info", "file00.txt.trashinfo")
check("delete moves a file to the trash, with the note of where it was",
      os.path.exists(os.path.join(TRASH, "files", "file00.txt")) and
      os.path.exists(info) and
      ("Path=%s/file00.txt" % D) in open(info).read(), sc)
if os.path.exists(os.path.join(TRASH, "files", "file00.txt")):
    os.rename(os.path.join(TRASH, "files", "file00.txt"),
              os.path.join(D, "file00.txt"))
shutil.rmtree(TRASH, True)

TERMW = [("Term", "10 36 12 40", "term")]
sc = run("files", FW, DRAG[:2] + [drag(15, 50), release(15, 50)],
         pre=PRE + "\n" + SH, also=TERMW, end=None, wait=1.2)
check("a file dropped on a terminal is typed in as its path",
      sc.find("sh> " + D[:28]) is not None, sc)

EDIT = "TW_EDIT=(/bin/sh -c 'echo \"editing $1\"; sleep 5' x)"
sc = run("files", FW, [b"\x1b[B"] * 3 + [b"\r"], pre=PRE + "\n" + EDIT,
         extra=["term"], end=None, wait=1.2)
check("opening a file opens a terminal window called by its name",
      sc.find("┤ file00.txt ├") is not None and
      sc.find("editing") is not None, sc)

# --- files: choosing more than one ---------------------------------------
#
# Rows in the left window: ../ 4, alpha/ 5, beta/ 6, file00.txt 7,
# file01.txt 8.  Button 16 is the left button with ctrl held.

sc = run("files", FW, [press(7, 5), press(8, 5, 16)], pre=PRE)
check("ctrl and a click add to what is selected",
      sc.find("2 selected") is not None, sc)
sc = run("files", FW, [b"\x1b[B"] * 3 + [b"\x1b[1;2B"] * 2, pre=PRE)
check("shift with the arrows carries the selection along",
      sc.find("3 selected") is not None, sc)
sc = run("files", FW, [b"\x1b[B"] * 3 + [b" ", b" "], pre=PRE)
check("space marks an entry and steps on", sc.find("2 selected") is not None,
      sc)
sc = run("files", FW, [b"\x01"], pre=PRE)
check("ctrl-a selects everything but ..",
      sc.find("%d selected" % (ENTRIES - 1)) is not None, sc)

PICK = [press(7, 5), press(8, 5, 16), press(8, 5), drag(8, 9), drag(9, 50),
        release(9, 50)]
sc = run("files", FW, INTO + PICK, pre=PRE, also=TWO)
both = [os.path.exists(os.path.join(D, "alpha", f))
        for f in ("file00.txt", "file01.txt")]
check("a selection dragged to another window moves all of it", all(both), sc)
for f in ("file00.txt", "file01.txt"):
    if os.path.exists(os.path.join(D, "alpha", f)):
        os.rename(os.path.join(D, "alpha", f), os.path.join(D, f))

TRASH = tempfile.mkdtemp(prefix="hibr-trash-")
sc = run("files", FW, [press(7, 5), press(8, 5, 16), b"\x1b[3~"],
         pre=PRE + "\nDT_TRASH=%s" % TRASH)
gone = [os.path.exists(os.path.join(TRASH, "files", f))
        for f in ("file00.txt", "file01.txt")]
check("and delete throws all of it away", all(gone), sc)
for f in ("file00.txt", "file01.txt"):
    if os.path.exists(os.path.join(TRASH, "files", f)):
        os.rename(os.path.join(TRASH, "files", f), os.path.join(D, f))
shutil.rmtree(TRASH, True)

# --- context menu: cut, copy, paste, info, and open with -------------------
#
# HD is its own directory, just two files, so the rows are easy to name:
# ../ is 4, note.txt is 5, pic.jpg is 6.

HD = tempfile.mkdtemp(prefix="hibr-hfiles-")
open(os.path.join(HD, "note.txt"), "w").write("hello\n")
open(os.path.join(HD, "pic.jpg"), "w").write("")
HPRE = "FB_DIR=%s" % HD

sc = run("files", FW, [press(5, 10, 2)], pre=HPRE)
check("a right-click opens a File menu at the pointer, on the entry there",
      sc.find("Open") == (5, 12) and sc.find("Cut") is not None and
      sc.find("Copy") is not None and sc.find("Paste") is not None and
      sc.find("Info") is not None and sc.find("Move to Trash") is not None,
      sc)
check("and selects it, as a plain click would",
      sc.find("2 of 3") is not None, sc)

sc = run("files", FW, [press(4, 10, 2)], pre=HPRE)
check("right-clicking .. dims what does not apply to it",
      sc.find("Open") is not None and sc.at(4, 29) != "o", sc)

sc = run("files", FW, [press(5, 10, 2), press(11, 12)], pre=HPRE, end=None)
check("Info shows the entry's size and permissions",
      sc.find("note.txt — 6B") is not None and
      sc.find("-rw-") is not None, sc)

sc = run("files", FW, [press(5, 10, 2), press(8, 12)], pre=HPRE)
check("Cut puts its paths on the clipboard, the same as Copy does",
      b"\x1b]52;c;" in sc.out, sc)

# A second window, into a folder of its own, proves Cut moves rather than
# copies: HD2 has a subfolder to paste into, so the file has somewhere to
# actually go.
HD2 = tempfile.mkdtemp(prefix="hibr-hfiles2-")
os.mkdir(os.path.join(HD2, "sub"))
open(os.path.join(HD2, "note.txt"), "w").write("hello\n")
H2PRE = "FB_DIR=%s" % HD2
H2TWO = [("Files", "12 34 2 40", "files")]
H2INTO = [press(5, 44), press(5, 44)]

sc = run("files", FW,
         H2INTO + [press(6, 10, 2), press(8, 12), press(4, 44, 2),
                   press(8, 46)],
         pre=H2PRE, also=H2TWO)
check("Cut, then Paste elsewhere, moves the file rather than copying it",
      os.path.exists(os.path.join(HD2, "sub", "note.txt")) and
      not os.path.exists(os.path.join(HD2, "note.txt")), sc)
shutil.rmtree(HD2, True)

# A handler registered with dt_handler, from a script of the user's own.
HW = tempfile.mkdtemp(prefix="hibr-hfiles3-")
open(os.path.join(HW, "note.txt"), "w").write("hello\n")
MARK = os.path.join(HW, "opened")
HWPRE = "FB_DIR=%s\ndt_handler txt touch %s\n" % (HW, MARK)

sc = run("files", FW, [press(5, 10, 2)], pre=HWPRE)
check("a registered handler adds Open With to the menu",
      sc.find("Open With") == (6, 12), sc)

sc = run("files", FW, [press(5, 10, 2), press(6, 12)], pre=HWPRE)
check("and lists it by its own program name and the extension it is for",
      sc.find("touch (.txt)") is not None, sc)

# Click where the entry actually rendered just above, not a row copied by
# eye -- a hardcoded one is exactly what let the submenu-position bug
# below go unnoticed: it happened to match the (buggy) implementation.
pos = sc.find("touch (.txt)")
sc = run("files", FW, [press(5, 10, 2), press(6, 12), press(*pos)],
         pre=HWPRE)
check("choosing it runs that handler on the entry", os.path.exists(MARK), sc)
if os.path.exists(MARK):
    os.unlink(MARK)

sc = run("files", FW, [press(5, 10), press(5, 10)], pre=HWPRE)
check("a plain double-click still opens it through its own registered "
      "handler", os.path.exists(MARK), sc)
shutil.rmtree(HW, True)
shutil.rmtree(HD, True)

# --- the task manager -------------------------------------------------------
#
# The process list is the real machine's, so nothing here asserts on which
# names or numbers appear -- only that a header and at least one real row
# are drawn, and that both sort keys run without error. Nothing here sends
# x or shift-x: killing whatever a live sort put on top would be killing a
# process this suite does not own. Before D and S are cleaned up below, since
# run() still needs S/session.hibr to exist.

TASKS = ("tasks", "16 50 4 4")

sc = run(*TASKS)
check("the task manager lists processes under a header",
      sc.find("CPU%") is not None and sc.find("Mem") is not None and
      sc.find("Name") is not None, sc)
# Not any name in particular: on the very first scan every process ties at
# 0% CPU, so which of a few hundred land in the visible rows is whatever
# order /proc's glob happened to return, not something to name one of. The
# header's own "CPU%" is one "%"; a second one is a real row, not blanks.
check("at least one real row of the process list is drawn",
      sc.text().count("%") > 1, sc)

sc = run(*TASKS, feed=[b"m"])
check("m sorts by memory instead, without error",
      sc.find("CPU%") is not None, sc)

sc = run(*TASKS, feed=[b"\x1b[B", b"\x1b[B", b"\x1b[B"])
check("the arrows move the selection without error",
      sc.find("CPU%") is not None, sc)

for f in os.listdir(D):
    p = os.path.join(D, f)
    if os.path.isdir(p):
        os.rmdir(p)
    else:
        os.unlink(p)
os.rmdir(D)
os.unlink(os.path.join(S, "session.hibr"))
os.rmdir(S)

report(134)
