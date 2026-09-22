#!/usr/bin/env python3
"""Drive the desktop's apps through a pty: the calculator and the browser.

tests/desktop.py checks the window manager with apps small enough to fit in
the test file. This checks the real ones in examples/apps/, which are the two
that decision 0020 calls for at this stage: the calculator proves keys and
clicks reaching a focused window, and the browser proves scrolling *inside*
one, with the wheel and the selection moving independently.
Run it directly:  python3 tests/apps.py [path-to-hibr]
"""
import os, subprocess, sys, tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import screen as sx
from screen import Term, check, report, press, wheel, load, tree

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


def run(app, win, feed=(), pre="", wait=1.0):
    """Open one app in a window at a known place and drive it."""
    p = os.path.join(S, "session.hibr")
    open(p, "w").write(
        "%s. %s\n. %s/%s.hibr\n%s\ndt_open\ndt_new \"%s\" %s %s\n"
        "dt_run\ndt_close\n"
        % (load("console"), WM, APPS, app, pre, app.title(), win, app))
    t = Term(p, env={"DT_TICK": "60"}, settle=0.6)
    t.keys(feed)
    t.quit(b"q", wait)
    return t.screen()


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

for f in os.listdir(D):
    p = os.path.join(D, f)
    if os.path.isdir(p):
        os.rmdir(p)
    else:
        os.unlink(p)
os.rmdir(D)
os.unlink(os.path.join(S, "session.hibr"))
os.rmdir(S)
report(31)
