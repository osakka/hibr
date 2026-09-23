#!/usr/bin/env python3
"""Drive the desktop's apps through a pty: every one in examples/apps/.

tests/desktop.py checks the window manager with apps small enough to fit in
the test file. This checks the real ones: the calculator proves keys and
clicks reaching a focused window, the browser proves scrolling *inside* one,
the panel proves an app managing other windows, the terminal proves a real
program in a window (two of them, as two sessions), and the games prove
animation on the clock.
Run it directly:  python3 tests/apps.py [path-to-hibr]
"""
import os, shutil, subprocess, sys, tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import screen as sx
from screen import Term, check, report, press, release, drag, wheel, load, tree

if len(sys.argv) > 1:
    sx.HIBR = os.path.abspath(sys.argv[1])
WM = tree("examples/desktop.hibr")
APPS = tree("examples/apps")
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


def run(app, win, feed=(), pre="", wait=1.0, also=(), end=b"q", extra=()):
    """Open one app in a window at a known place and drive it.

    `also` adds further windows after it, as (title, geometry, app) triples,
    which is what the control panel needs: it has nothing to show until
    there is something else open. `end` is the key that finishes; a test
    of a terminal passes None, since the program inside would take the q.
    """
    p = os.path.join(S, "session.hibr")
    src = "".join(". %s/%s.hibr\n" % (APPS, a)
                  for a in dict.fromkeys([app] + [x[2] for x in also if x[2]]
                                         + list(extra)))
    more = "".join('dt_new "%s" %s %s\n' % x for x in also)
    open(p, "w").write(
        "%s. %s\n%s%s\ndt_open\ndt_new \"%s\" %s %s\n%s"
        "dt_run\ndt_close\n"
        % (load("console"), WM, src, pre, app.title(), win, app,
           more + ("dt_raise 1\n" if also else "")))
    t = Term(p, env={"DT_TICK": "60"}, settle=0.6)
    t.keys(feed)
    t.quit(end, wait)
    sc = t.screen()
    sc.out = t.out
    return sc


def calc_key(i):
    """Where key i of the keypad lands on screen, for a window at row 2 col 2."""
    return 6 + (i // 4) * 2, 4 + (i % 4) * 5 + 1


def cli(app, *args):
    out = subprocess.run([sx.HIBR, "%s/%s.hibr" % (APPS, app)] + list(args),
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

sc = run("calc", CW, [press(*calc_key(0)), press(*calc_key(9)),
                      press(*calc_key(19))])
check("clicking keys builds an expression and = evaluates it",
      sc.find("72") is not None, sc)

sc = run("calc", CW, [b"6", b"*", b"7", b"="])
check("typing does the same", sc.find("42") is not None, sc)

sc = run("calc", CW, [b"9", b"9", b"c"])
check("c clears", sc.find("expression") is not None, sc)

sc = run("calc", CW, [b"1", b"2", b"\x7f"])
check("backspace takes the last character back",
      sc.find(" 1 ") is not None and sc.find(" 12") is None, sc)

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

rc, out, err = cli("panel")
check("it lists what it can change from the command line",
      rc == 0 and "midnight" in out and "refresh:" in out, out)

PW = "14 34 2 2"
PANEL = ("panel", PW)
OTHER = [("Other", "5 20 18 40", "")]
TICK = "DT_TICK=200"

sc = run(*PANEL, pre=TICK, also=OTHER)
check("the sections are drawn", sc.find("Appearance") == (3, 3) and
      sc.find("Behaviour") == (7, 3) and sc.find("Windows") == (11, 3), sc)
check("the settings show their values", sc.find("midnight") is not None and
      sc.find("200 ms") is not None, sc)
check("the window list names what is open",
      sc.find("Panel") is not None and sc.find("Other") is not None, sc)

sc = run(*PANEL, feed=[b"\x1b[C"], pre=TICK, also=OTHER)
check("right cycles the theme", sc.find("slate") is not None and
      sc.find("midnight") is None, sc)
sc = run(*PANEL, feed=[b"\x1b[D"], pre=TICK, also=OTHER)
check("left cycles it the other way", sc.find("paper") is not None, sc)

sc = run(*PANEL, feed=[b"\x1b[B", b"\x1b[C"], pre=TICK, also=OTHER)
check("the wallpaper glyph changes, and the desktop follows",
      sc.at(0, 78) != "·" and sc.at(23, 60) == "░", sc)

sc = run(*PANEL, feed=[b"\x1b[B", b"\x1b[B", b"\x1b[C"], pre=TICK,
         also=OTHER)
check("down skips the blank line and the heading, landing on Refresh",
      sc.find("350 ms") is not None, sc)

# Theme, Wallpaper, Refresh, Icons, Panel, Other -- five downs from Theme
# reaches the second window, because cp_move steps over the headings and the
# blanks.
DOWN4 = [b"\x1b[B"] * 5

sc = run(*PANEL, feed=[b"\x1b[B"] * 3 + [b"\r"], pre=TICK, also=OTHER)
check("the icons can be switched off, and the panel says so",
      sc.find("Icons") is not None and "off" in sc.row(sc.find("Icons")[0]),
      sc)

sc = run(*PANEL, feed=DOWN4 + [b"x"], pre=TICK, also=OTHER)
check("x closes the selected window", sc.find("Other") is None and
      sc.find("Panel") is not None, sc)

sc = run(*PANEL, feed=DOWN4 + [b"-"], pre=TICK, also=OTHER)
check("- hides it, and the panel says so",
      sc.find("Other") is not None and sc.find("hidden") is not None, sc)
check("while the one still showing reads open",
      "open" in sc.row(12) and "hidden" in sc.row(13), sc)

sc = run(*PANEL, feed=DOWN4 + [b"-", b"\r"], pre=TICK, also=OTHER)
check("enter on a hidden window brings it back",
      sc.find("hidden") is None and sc.find("Other") is not None, sc)

sc = run(*PANEL, feed=[press(4, 10), press(4, 10)], pre=TICK, also=OTHER)
check("a click selects and a second click acts",
      sc.find("slate") is not None, sc)

sc = run(*PANEL, feed=[press(3, 10), press(3, 10)], pre=TICK, also=OTHER)
check("clicking a heading does nothing", sc.find("midnight") is not None, sc)

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

sc = run(*TERM, feed=[b"\x1b[21~"],
         pre=SH,
         wait=1.2)
check("f10 still reaches the menu bar, not the program",
      sc.find("About hibr") is not None, sc)

sc = run(*TERM, pre="TW_CMD=(/bin/sh -c 'exit 4')", wait=1.6)
check("a program that ends says so in the window",
      sc.find("exited 4") is not None, sc)

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
sc = run("files", FW, INTO + DRAG + [release(9, 50, 16)], pre=PRE, also=TWO)
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
check("a selection dragged to another window moves all of it",
      all(both) and sc.find("Moved 2 items to alpha") is not None, sc)
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

for f in os.listdir(D):
    p = os.path.join(D, f)
    if os.path.isdir(p):
        os.rmdir(p)
    else:
        os.unlink(p)
os.rmdir(D)
os.unlink(os.path.join(S, "session.hibr"))
os.rmdir(S)
report(89)
