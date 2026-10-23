#!/usr/bin/env python3
"""Drive the desktop's apps through a pty: every one in programs/desktop/apps/.

tests/desktop.py checks the window manager with apps small enough to fit in
the test file. This checks the real ones: the calculator proves keys and
clicks reaching a focused window, the browser proves scrolling *inside* one,
the panel proves a multi-pane app with its own picker list, the terminal
proves a real program in a window (two of them, as two sessions), and the
games prove animation on the clock.
Run it directly:  python3 tests/apps.py [path-to-hibr]
"""
import json, os, re, shutil, subprocess, sys, tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import screen as sx
from screen import Term, check, report, press, release, drag, wheel, load, tree, expect

if len(sys.argv) > 1:
    sx.HIBR = os.path.abspath(sys.argv[1])
WM = tree("programs/desktop/desktop.hibr")
APPS = tree("programs/desktop/apps")
SYS = tree("programs/desktop/system")
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
    """Which directory has a.hibr -- APPS searched recursively, the same
    as dt_apps' own **/*.hibr glob (an app in a subfolder is a submenu
    inside Applications); SYS flat, the same as dt_sysapps' own
    deliberately non-recursive scan of the desktop's own apps."""
    if os.path.exists(os.path.join(SYS, a + ".hibr")):
        return SYS
    for root, _dirs, files in os.walk(APPS):
        if a + ".hibr" in files:
            return root
    return APPS


def run(app, win, feed=(), pre="", wait=1.0, also=(), end=b"qy", extra=(),
        env=None, until=None, tries=40, ready=None, rows=None, cell=(0, 0),
        caps=None):
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

    `until` is what to wait for before the quitting keys are sent -- text,
    or a function given the screen. The desktop answers a key before the
    idle marker, but a *program* in a terminal window answers on its own
    time, so without this the `qy` teardown can be typed into a shell whose
    output has not arrived yet: the row then reads `qyhi` where the test
    wanted `hi`, which reads as the terminal losing what it was told rather
    than as the test being early. It failed a release gate that way and
    passed every run alone.
    """
    p = os.path.join(S, "session.hibr")
    src = "".join(". %s/%s.hibr\n" % (appdir(a), a)
                  for a in dict.fromkeys([app] + [x[2] for x in also if x[2]]
                                         + list(extra)))
    more = "".join('dt_new "%s" %s %s\n' % x for x in also)
    # term.hibr's own `need pty`/`need terminal` autoload from the
    # installed module path, not this tree's own build/mods -- the same
    # trap `need` always is. A terminal test loads both explicitly first,
    # so `need` finds them already provided and a rebuilt pty/term is what
    # actually gets exercised, not whatever is separately installed. A
    # terminal opened through `also` (a second window, not the main app)
    # is just as much a terminal test as one opened through `app` or
    # `extra` -- missed here once, which stayed silent only because the
    # installed term.so and this tree's own DP_API_VER happened to still
    # agree; an in-progress version bump is exactly what surfaces it.
    mods = ["console"]
    if app == "term" or "term" in extra or any(x[2] == "term" for x in also):
        mods += ["pty", "term"]
    open(p, "w").write(
        "%s. %s\n%s%s\ndt_open\n"
        "if [ \"$DT_RESTORED\" != 1 ]; then\ndt_new \"%s\" %s %s\n%s:\nfi\n"
        "dt_run\ndt_close\n"
        % (load(*mods), WM, src, pre, app.title(), win, app,
           more + ("dt_raise 1\n" if also else "")))
    t = Term(p, env=dict({"DT_TICK": "60"}, **(env or {})), settle=0.6,
             rows=rows or sx.ROWS, cellw=cell[0], cellh=cell[1], caps=caps)
    # The desktop says when its frame is drawn, but a terminal window's
    # program starts on its own time: a key sent the moment the window is
    # up can reach a shell that has not yet set its trap. Give it the time
    # the old fixed start used to give everything.
    if "term" in mods:
        t.collect(0.8)
    # `ready` is what must be true **before** the keys are sent, where
    # `until` is what must be true after. An app that fetches its own
    # content -- the Applications window runs `apps` as a child -- has
    # nothing for a key to act on until that answer is in, so a key sent
    # on the first frame is a key pressed at an empty window. `until`'s
    # docstring has the other half of this: a key is never early, and
    # what a key *causes* can be.
    if ready is not None:
        t.until(ready, tries)
    t.keys(feed)
    if until is not None:
        # `tries` because `until`'s own four seconds is a budget, not a
        # promise: a window that forks a child and polls for its answer
        # needs longer than one that redraws from what it already has,
        # and a gate runs eighty pty sessions at once. until() gives up
        # **quietly** and returns whatever is on screen, so a budget too
        # small reads as the window never drawing rather than as the
        # test being early.
        t.until(until, tries)
    t.quit(end, wait)
    sc = t.screen()
    sc.out = t.out
    return sc


def stdir():
    """A folder of notes of its own, for a Stickies test."""
    return tempfile.mkdtemp(prefix="hibr-stickies-")


def stnote(d, n=1):
    """What note n holds on disk, or None."""
    f = os.path.join(d, "%d.txt" % n)
    return open(f).read() if os.path.exists(f) else None


def stenv(d):
    return {"ST_DIR": d, "DT_STICKYSTART": "0"}


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
      sc.row(3)[3:6] == " 1 ", sc)

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
check("and the window's own title names it too, truncated the same as any "
      "other long title", sc.find("Files [") is not None, sc)
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

# The path row (row 1 of the pane, absolute row 3 here) is a breadcrumb: a
# click opens a dropdown of every ancestor, root first, D itself among them
# since the window is opened one level below it (in alpha/, already empty
# and already used above) -- and choosing one navigates straight there. The
# row D lands on in the popup depends on how many segments its own path
# has (root, then one row per segment), which varies by machine and by
# $TMPDIR, so it is computed rather than guessed.
DROW = 4 + len([s for s in D.strip("/").split("/") if s])
sc = run("files", FW, [press(3, 5), press(DROW, 6)],
         pre="FB_DIR=%s" % os.path.join(D, "alpha"))
check("clicking the path opens a breadcrumb of every ancestor, and "
      "choosing one navigates there",
      sc.find(D) == (3, 3) and sc.find("1 of %d" % ENTRIES) is not None, sc)

sc = run("files", FW, [wheel(6, 10, up=False), press(4, 10)], pre=PRE)
check("a click after scrolling lands on the row that is there",
      sc.find("4 of %d" % ENTRIES) is not None, sc)

# --- the control panel ----------------------------------------------------
#
# Control Panel is a picker now, not one scrolling list: a pane down the
# left, the selected pane's own rows on the right (System 7's Control
# Panels folder). Panes are loaded from programs/desktop/control-panel and
# listed in two groups under headings -- the desktop's own under System,
# then one pane per app under Apps -- each sorted by title without regard
# to case, the same trick dt_appnames uses for apps. Verified once below,
# not assumed, and ORDER mirrors it exactly.

rc, out, err = cli("panel")
check("with no panes loaded, it says so rather than pretending",
      rc == 0 and out == "no panes registered", out)

CP = tree("programs/desktop/control-panel")
CPLOAD = ('. %s\n. %s/panel.hibr\nCP_PANEDIRS+=("%s")\ncp_panes\n'
          % (WM, SYS, CP))
out = subprocess.run([sx.HIBR, "-c", CPLOAD + 'for p in "${CP_PANE_LIST[@]}"; '
                      'do echo "$p ${CP_PANES[$p]["group"]}"; done'],
                     capture_output=True, text=True).stdout.split("\n")
ORDER = [l.split()[0] for l in out if l.strip()]
GROUP = dict(l.split() for l in out if l.strip())
check("panes register and sort by title within their group, not load order",
      ORDER == ["bluetooth", "datetime", "displays", "keyboard", "mouse", "networks", "sound",
                "aboutme", "abouthibr", "appearance", "cliphist",
                "control_strip", "desktop", "filetypes", "language",
                "notify", "pictures", "screensaver", "network", "shortcuts",
                "taskmgr", "versions", "wallset", "windows",
                "browserset", "pimset", "filesview", "mailset", "prayerset",
                "notes", "terminal", "vaultset", "xscreenset", "tube"], out)
check("Hardware first, then the desktop's own panes, then one per app",
      [GROUP[n] for n in ORDER] ==
      ["hardware"] * 7 + ["system"] * 17 + ["app"] * 10, out)

PW = "22 70 2 2"
# Appearance is the longest pane and got two rows longer in 0.99.193
# (Tree Style and Tree Rules), so in a 22-row window it now scrolls --
# and a scrolling pane spends its last column on a bar, which cut
# `construction` to `constructio` and failed four checks that are
# about themes and shadows rather than about scrolling. They get a
# window tall enough to hold the pane whole, on a terminal tall enough
# to hold the window: 26 rows at row 2 on a 24-row terminal was already
# clipped, and only fitted while nothing was added below Shadows. The
# wallpaper's Picture and Drawn Now rows (0.99.237, Gitea #275) were the
# two that made it not fit.
PWTALL = "30 70 2 2"
TALLROWS = 34
PANEL = ("panel", PW)
TICK = "DT_TICK=200"
CPANES = 'CP_PANEDIRS+=("%s")\ncp_panes' % CP

# Absolute screen coordinates for PW's geometry (row 2, col 2): a pane's
# own first content row is at 3, its dropdown/checkbox column at 58 --
# for a pane whose values fit eleven columns: a wider one moves the column
# left (cp_valw, Gitea #282), and a click at 58 still lands inside it --
# derived once here from CP_DIVCOL and the window's body width, rather than
# copied by eye into every check below. BODYCOL is a body-shape pane's own
# left content column (CP_DIVCOL + 1, plus the window's own left edge) --
# where Wallpaper's own file list starts, not LISTCOL, which is the
# picker's own sidebar column to its left.
R0, VALCOL = 3, 58
LISTCOL = 5
BODYCOL = 22
TITLE = {"aboutme": "About Me", "appearance": "Appearance", "control_strip": "Control Strip",
         "cliphist": "Clipboard", "notes": "Stickies",
         "datetime": "Date & Time", "desktop": "Desktop",
         "displays": "Displays", "filetypes": "File Types", "language": "Language",
         "network": "Servers", "networks": "Network", "bluetooth": "Bluetooth", "prayerset": "Prayer Times", "vaultset": "Vault",
         "pictures": "Pictures",
         "screensaver": "Screen Saver",
         "keyboard": "Keyboard", "mouse": "Mouse", "sound": "Sound",
         "shortcuts": "Shortcuts", "notify": "Notifications",
         "windows": "Windows", "abouthibr": "About This",
         "filesview": "Files", "mailset": "Mail", "pimset": "Calendar &", "taskmgr": "Task Manager",
         "terminal": "Terminal", "tube": "YouTube", "versions": "Versions",
         "browserset": "Browser", "wallset": "Wallpaper",
         "xscreenset": "X Screen"}


def prow(name):
    """Which screen row a pane's own name sits at in the picker list: under
    the Hardware heading, and under Desktop and Apps as well further down."""
    heads = ["hardware", "system", "app"]
    # One row higher since 0.99.193: the list used to begin under a
    # `Search…` box of its own, and the search is the desktop's one search
    # now -- on the window's bottom row, drawn only once `/` asks for it.
    return R0 + 1 + ORDER.index(name) + heads.index(GROUP[name])


def downs(name):
    """How many downs on the picker, from the default pane, reach it."""
    return ORDER.index(name)


def panerows(pane, extra=()):
    """What a pane lists, as (kind, text), asked of its own _rows with the
    same apps loaded that cprun's extra would load."""
    src = "".join(". %s/%s.hibr\n" % (appdir(a), a) for a in extra)
    out = subprocess.run(
        [sx.HIBR, "-c", CPLOAD + src +
         'n := %s_rows 1; i=0; while [ "$i" -lt "$n" ]; do '
         'echo "${CP[1][$i]["kind"]}|${CP[1][$i]["text"]}"; i=$((i + 1)); done'
         % pane], capture_output=True, text=True).stdout
    return [tuple(l.split("|", 1)) for l in out.splitlines() if "|" in l]


def reach(pane, label, extra=()):
    """The keys that select a pane's row by its label: down the picker to
    the pane, right into it, then down past the rows before it -- headings
    are skipped by the arrows, so they are not counted."""
    rows = [t for k, t in panerows(pane, extra) if k != "head"]
    hit = [i for i, t in enumerate(rows) if t == label]
    hit = hit or [i for i, t in enumerate(rows) if t.startswith(label)]
    assert hit, (pane, label, rows)
    return [b"\x1b[B"] * downs(pane) + [b"\x1b[C"] + [b"\x1b[B"] * hit[0]


def browi(sc, label):
    """brow's row number instead of its text, or None."""
    for r in range(sc.rows):
        if re.match(re.escape(label) + r"(\s{2,}|$)", sc.row(r)[BODYCOL:].strip()):
            return r
    return None


def brow(sc, label):
    """The pane's own part of the screen row its row with this label is
    drawn on, or "" -- matched on the label and the gap after it, so Menu
    Bar is not taken for Menu Bar Spacing."""
    r = browi(sc, label)
    return "" if r is None else sc.row(r)[BODYCOL:]


def cprun(feed=(), also=(), extra=(), tz=None, env=None, pre="", end=b"qy",
          post="", geom=None):
    """Run Control Panel from a config directory of its own.

    tests/screen.py gives the whole suite one shared $HOME, so without this
    a theme set by one check would still be in effect for the next one --
    which is exactly how "left cycles it the other way" once passed by
    reading a value "right" had left behind a moment before, rather than by
    actually cycling from the default. Each check gets its own directory
    instead, so its math holds regardless of what ran before it.

    tz, if given, is exported before cp_panes loads the panes, so the Date
    & Time pane's own lookup is deterministic rather than whatever zone the
    machine running the suite happens to be in. env, if given, is real
    process environment, the same as run()'s own -- for a pane like
    Wallpaper that reads $HOME itself, rather than a setting this file's
    own pre line could export. pre, if given, runs before all of that --
    for a `load(...)` prefix a pane needs a freshly built module already
    in place for, rather than autoloaded later by its own `need`, which
    would reach for the installed copy instead of this tree's own build.
    post runs after the panes are loaded, for a stub that must replace one
    of their functions rather than be replaced by it.
    """
    d = tempfile.mkdtemp(prefix="hibr-cp-")
    tzline = "export TZ=%s\n" % tz if tz else ""
    sc = run("panel", geom or PW, feed=feed,
             pre="%sexport XDG_CONFIG_HOME=%s\n%s%s\n%s\n%s"
             % (pre, d, tzline, TICK, CPANES, post), also=also, extra=extra,
             env=env, end=end, rows=TALLROWS if geom == PWTALL else None)
    shutil.rmtree(d, True)
    return sc


sc = cprun()
check("the picker lists every pane it has room for, sorted by title",
      all(TITLE[n] in sc.row(prow(n)) for n in ORDER if prow(n) < R0 + 19),
      sc)
# The list's scrollbar is beside the rows it scrolls and never on the
# window's bottom border: it started a row low, from when the search sat
# above the list, so scrolled to the end its thumb was on the border row,
# under the frame -- gone, and the border a target for it (Gitea #314). At
# the top the two cannot be told apart, since the frame is drawn over the
# bar's last cell either way; the end is where they differ.
psc = cprun(feed=[wheel(R0 + 5, 10, up=False)] * 8 + [0.3])
pdv = next((c for c in range(3, 34) for r in range(R0, R0 + 20)
            if psc.at(r, c) == "\u2588"), None)
pbot = R0 + 20
check("the pane list's scrollbar stops above the window's bottom border",
      pdv is not None and psc.at(pbot - 1, pdv) == "\u2588" and
      psc.row(pbot)[2] == "\u2514", (pdv, psc))
check("under a Hardware heading, then Desktop, then Apps",
      "Hardware" in sc.row(R0) and
      "Desktop" in sc.row(prow("aboutme") - 1) and
      (prow("pimset") - 1 >= R0 + 19 or
       "Apps" in sc.row(prow("pimset") - 1)), sc)
check("a list longer than the window scrolls, with a bar to say so",
      sc.find("Hardware") is not None and "█" in "".join(
          r[2:22] for r in sc.text().split("\n")), sc)


# Files' search box: / starts it, the list narrows as it is typed, escape
# clears it.
sc = run("files", "16 50 2 2", [b"/"] + [c.encode() for c in "FILE1"] + [0.3],
         pre="FB_DIR=%s" % D)
check("Files' search narrows the list as it is typed, whatever the case",
      sc.find("file10.txt") is not None and sc.find("file11.txt") is not None
      and sc.find("file02.txt") is None and sc.find("alpha/") is None and
      sc.find("FILE1") is not None, sc)
sc = run("files", "16 50 2 2", [b"/"] + [c.encode() for c in "file1"] +
         [b"\x1b", 0.6], pre="FB_DIR=%s" % D)
check("and escape clears it, every entry back",
      sc.find("file02.txt") is not None and sc.find("alpha/") is not None, sc)

# The list and the pane beside it scroll on their own, each with its own
# bar: in a window too short for every pane, the arrows bring the last into
# view, and the wheel over the list scrolls the list alone.
def shortcp(feed):
    d = tempfile.mkdtemp(prefix="hibr-cp-")
    sc = run(PANEL[0], "12 70 2 2", feed=feed,
             pre="export XDG_CONFIG_HOME=%s\n%s\n%s\n" % (d, TICK, CPANES))
    shutil.rmtree(d, True)
    return sc


sc = shortcp([])
check("a short window shows the list's first rows, and a bar to say there "
      "are more", sc.find("Hardware") is not None and
      sc.find("Terminal") is None and sc.find("█") is not None, sc)
sc = shortcp([b"\x1b[B"] * (len(ORDER) - 1))
check("the arrows bring the last pane into view",
      sc.find(TITLE["terminal"]) is not None and
      sc.find("Hardware") is None, sc)
sc = shortcp([wheel(8, 5, up=False)] * 12)
check("the wheel over the list scrolls the list, not the pane beside it",
      sc.find("Hardware") is None and sc.find(TITLE["terminal"]) is not None
      and sc.find(TITLE["datetime"]) is None, sc)
sc = cprun()
check("the first pane's own rows show on the right without entering it",
      sc.find(TITLE[ORDER[0]]) is not None and
      sc.find("Bluetooth System") is not None, sc)

# Every dropdown in every pane, walked the same way: open it by clicking its
# value, take the next choice with the keyboard, and see the value change.
# Each choice runs a pane's _drop or cp_set_* callback, and most of those were
# reached by nothing else -- the census listed them. Which rows are
# dropdowns is read from each pane's own _drop, and where each row sits from
# its own _rows. Date & Time is left out: its dialogs run sudo.
PANE_APPS = {"abouthibr": ("about",), "filesview": ("files",),
             "taskmgr": ("tasks",), "terminal": ("term",),
             "browserset": ("browser",), "xscreenset": ("xscreen",)}


PANEHOME = tempfile.mkdtemp(prefix="hibr-pane-conf-")


def panefull(pane, extra=()):
    """A pane's rows as (kind, text, key, value), from its own _rows --
    with a config folder of its own, as every session the suite drives
    has, so a server or a setting of whoever runs it cannot add a row here
    that the session it is compared with does not have."""
    src = "".join(". %s/%s.hibr\n" % (appdir(a), a) for a in extra)
    out = subprocess.run(
        [sx.HIBR, "-c", CPLOAD + src +
         'n := %s_rows 1; i=0; while [ "$i" -lt "$n" ]; do '
         'echo "${CP[1][$i]["kind"]}|${CP[1][$i]["text"]}|'
         '${CP[1][$i]["key"]}|${CP[1][$i]["val"]}"; i=$((i + 1)); done'
         % pane], capture_output=True, text=True,
        env=dict(os.environ, XDG_CONFIG_HOME=PANEHOME,
                 HIBR_DAV_CONF=os.path.join(PANEHOME, "dav"))).stdout
    return [tuple(l.split("|", 3)) for l in out.splitlines() if l.count("|") >= 3]


def dropkeys(pane):
    """The row keys a pane's _drop opens a popup for, read from its source."""
    src = ""
    for f in sorted(os.listdir(CP)):
        s = open(os.path.join(CP, f)).read()
        if re.search(r"cp_pane %s " % pane, s):
            src = s
            break
    m = re.search(r"^fn %s_drop\(.*?^}" % pane, src, re.M | re.S)
    if not m:
        return set()
    keys = set()
    for lab in re.findall(r"^\s*([A-Za-z_][A-Za-z0-9_ |]*)\)", m.group(0),
                          re.M):
        keys.update(k.strip() for k in lab.split("|"))
    return keys


# The Mail pane offers its Test, which is the row a person reaches for when
# mail does not work: it runs lib/mailtest.hibr as a job and draws a rung
# per step for the incoming server and the outgoing one, so "it does not
# work" becomes which of name, port, TLS, greeting or login stopped it
# (Gitea #188). Only its presence is asserted here -- what the ladder
# actually says is tests/mail.py's, where there are servers to say it
# about.
check("the Mail pane offers a connection test",
      any(k == "test" and kind == "action"
          for kind, text, k, val in panefull("mailset")), panefull("mailset"))

# Language is not in this loop: choosing another language translates every
# label the generic check then reads back, including the row's own name.
# tests/desktop.py drives the bundled Arabic catalogue instead.
DROPS = []
for pane in ORDER:
    if pane in ("datetime", "language"):
        continue
    # Wallpaper's Display offers the displays attached and saved, and
    # this session has neither, so it has one value and nothing to step
    # to; its own checks give it a display to choose.
    keys = dropkeys(pane) - {"wdisp"}
    extra = PANE_APPS.get(pane, ())
    for i, (kind, text, key, val) in enumerate(panefull(pane, extra)):
        # Prayer Times' six minute rows are one widget six times: the
        # first stands for them all.
        if pane == "prayerset" and key[:3] == "adj" and key != "adj0":
            continue
        if kind == "set" and key in keys:
            DROPS.append((pane, extra, i, text, val))
for pane, extra, i, text, val in DROPS:
    for step in (1, 2):
        # Tall enough for the longest pane: this drives every dropdown
        # by the row it sits on, so a pane that outgrows the window puts
        # its last rows out of reach and the click lands on nothing.
        sc = cprun([b"\x1b[B"] * downs(pane) +
                   [press(R0 + i, VALCOL)] + [b"\x1b[B"] * step + [b"\r"],
                   extra=extra, geom=PWTALL)
        now = brow(sc, text)
        if now != "" and val not in now:
            break
    check("%s's %s dropdown chooses a new value" % (TITLE[pane], text),
          now != "" and val not in now, sc)


def panerow(pane, key, extra=()):
    """One row of a pane's own _rows, by key: the fields panefull does not
    read -- which widget it is and what range it has -- and where it sits,
    since a row's index is what a click has to be aimed at."""
    src = "".join(". %s/%s.hibr\n" % (appdir(a), a) for a in extra)
    out = subprocess.run(
        [sx.HIBR, "-c", CPLOAD + src +
         'n := %s_rows 1; i=0; while [ "$i" -lt "$n" ]; do '
         'echo "${CP[1][$i]["key"]}|${CP[1][$i]["wtype"]}|${CP[1][$i]["min"]}|'
         '${CP[1][$i]["max"]}|${CP[1][$i]["after"]}|$i"; i=$((i + 1)); done'
         % pane], capture_output=True, text=True,
        env=dict(os.environ, XDG_CONFIG_HOME=PANEHOME,
                 HIBR_DAV_CONF=os.path.join(PANEHOME, "dav"))).stdout
    for l in out.splitlines():
        f = l.split("|")
        if len(f) >= 6 and f[0] == key:
            return dict(zip(("key", "wtype", "min", "max", "after", "i"), f))
    return {}


# A percentage wants a slider rather than five values somebody chose out of
# a hundred (Gitea #193). Two things fall out of it: any of the hundred can
# be reached, and a slider never reaches a pane's _drop at all -- so the
# "it draws, it works on return, and the list never opens" of #186 cannot
# come back for this row. The number is kept beside the label rather than
# inside it, or the label stops being a string any catalogue can hold.
tt = panerow("terminal", "termtrans", ("term",))
check("the terminal shade is a slider over the whole range, its percentage "
      "beside the label",
      tt.get("wtype") == "slider" and tt.get("min") == "0"
      and tt.get("max") == "100" and tt.get("after") == "(0%)", tt)
tti = int(tt.get("i") or 0)
sc = cprun([b"\x1b[B"] * downs("terminal") +
           [press(R0 + tti, VALCOL + 7)], extra=("term",))
# Read off the screen rather than through brow, which matches a label
# followed by two spaces and so cannot see a row whose own number is part
# of what is drawn.
check("a click on its track sets the value that column stands for",
      sc.find("Wallpaper Shows Through (70%)") is not None, sc)
# The other half of clicking one: a value chosen by eye is chosen by
# moving, not by pressing the same cell eleven times.
sc = cprun([b"\x1b[B"] * downs("terminal") +
           [press(R0 + tti, VALCOL + 7), drag(R0 + tti, VALCOL + 2)],
           extra=("term",))
check("and a drag across it goes on setting it",
      sc.find("Wallpaper Shows Through (20%)") is not None, sc)
# The other slider, whose own value is in tens of a 30-70 range rather than
# a plain percentage -- and which runs dt_tile on the way, so this drives
# the one _slide that does something besides store a number.
tm = panerow("windows", "tilemain")
sc = cprun([b"\x1b[B"] * downs("windows") +
           [press(R0 + int(tm.get("i") or 0), VALCOL + 5)])
check("the tiled main pane's share is clickable too, in its own units",
      tm.get("wtype") == "slider"
      and sc.find("Tiled Main (50%)") is not None, sc)

# Shortcuts (with only panel.hibr loaded here, Control Panel is the only app
# it lists) -- rebinding a shortcut, and the
# self-cancelling-click bug reported live as "I tried ctrl-l, alt-ctrl-l
# and l, none of them registered": a row already selected and last (true
# of every attempt after the first) arms capture on the very *first*
# click of the next attempt (panel_click's own "a second click on an
# already-selected row activates it"), so the click right after that --
# the other half of an ordinary double-click, or just a habitual re-click
# -- used to be read as dt_event's own next key and silently cancel the
# capture it had itself just armed, before the intended key was ever
# pressed.
# cprun's own returned screen is always read after its default "qy" quit
# keys have *also* run (run()'s own end=b"qy") -- and "q" is DT_KEYS' own
# default quit shortcut, so a capture still armed when it arrives swallows
# it as the completing key instead of quitting. Every check below sends
# its own real completing key (or escape) before that point, inside its
# own feed, and asserts on the row's own persisted value rather than the
# transient "Press a key…"/"X is now Y" notes, which by the time the
# screen is captured may already have been overwritten by whatever the
# trailing "qy" went on to do.
# Shortcut Key, the pane's first row (Gitea #326): the desktop's own chords
# are stored as mod-..., and every row shows what to press for the choice
# made -- one step along from Alt is Super or Alt.
sc = cprun(reach("shortcuts", "Shortcut Key") + [b"\x1b[C"])
check("Shortcut Key is the Shortcuts pane's first row, and stepping it "
      "chooses Super or Alt", "Super or Alt" in brow(sc, "Shortcut Key"), sc)
check("and every desktop shortcut then shows both keys to press",
      "super-c or alt-c" in brow(sc, "Copy"), sc)

KB_CP = reach("shortcuts", "Control Panel")
sc = cprun(KB_CP + [b"\r", b"z"])
check("activating an app's Shortcuts row starts capture, and the very next "
      "key completes it", "z" in brow(sc, "Control Panel"), sc)

sc = cprun(KB_CP + [b"\r", press(R0, VALCOL), b"z"])
check("a stray click while capturing does not cancel it -- the key sent "
      "right after still completes the rebind",
      "z" in brow(sc, "Control Panel"), sc)

sc = cprun(KB_CP + [b"\r", b"\x1b", b"z"])
check("escape cancels it -- the next key is ordinary again, not captured",
      brow(sc, "Control Panel") != "" and
      "z" not in brow(sc, "Control Panel"), sc)

# Shortcuts is longer than the pane, so the apps' rows are checked with the
# list moved down to them.
sc = cprun(reach("shortcuts", "Control Panel"))
check("Shortcuts lists the desktop's actions, the Control Strip's among "
      "them, then each app's under a heading of its own",
      all(brow(sc, t) != "" for t in ("Close Window", "Detach", "Quit",
                                        "Cycle Windows", "Control Strip",
                                        "Control Panel")) and
      "alt-s" in brow(sc, "Control Strip") and brow(sc, "Apps") != "", sc)
# A shortcut's label was given its indent twice, so it always read as too
# long and every row showed an ellipsis before its key (Gitea #282).
check("a shortcut's row has no ellipsis before its key",
      "…" not in brow(sc, "Close Window") and
      "…" not in brow(sc, "Control Panel"), sc)

# A value wider than the eleven-column slot ran past the window's edge,
# and a dropdown's over its own caret (Gitea #282): Language's
# "Automatic (hibr)" is the one every desktop has. The column widens to
# fit it, and every dropdown in the pane keeps its caret on one column,
# inside the window.
sc = cprun([b"\x1b[B"] * downs("language"))
lgcar = [sc.row(r).find("▾", BODYCOL) for r in range(R0, R0 + 4)]
check("a long value widens the pane's column and stays inside the window",
      "Automatic (hibr) ▾" in sc.row(R0 + 1) and len(set(lgcar)) == 1 and
      0 < lgcar[0] < 71, sc)
sc = cprun(reach("shortcuts", "Control Panel"))

# Clearing one: a ✕ beside every key that is set, and delete or backspace
# on the selected row -- with the list moved down so Quit and the apps'
# rows are on screen.
check("a set shortcut has a ✕ to clear it, and an unset one has none",
      "✕" in brow(sc, "Quit") and "✕" not in brow(sc, "Control Panel"), sc)
qr = browi(sc, "Quit")
sc = cprun(reach("shortcuts", "Control Panel") +
           ([press(qr, sc.row(qr).index("✕"))] if qr else []))
check("clicking it clears the shortcut",
      brow(sc, "Quit") != "" and "q" not in brow(sc, "Quit").split() and
      "✕" not in brow(sc, "Quit"), sc)
sc = cprun(reach("shortcuts", "Close Window") + [b"\x1b[3~"])
check("delete on a selected row clears it",
      brow(sc, "Close Window") != "" and "alt-w" not in brow(sc, "Close Window"),
      sc)
sc = cprun(reach("shortcuts", "Cycle Windows") + [b"\x7f"])
check("and so does backspace",
      brow(sc, "Cycle Windows") != "" and "tab" not in brow(sc, "Cycle Windows"),
      sc)

# No key is reserved: the menu bar's and Copy's are actions like any
# other, so taking one asks the same question taking any held key does.
sc = cprun(KB_CP + [b"\r", b"\x1b[21~"], end=None)
check("f10 is the Menu Bar's, and taking it asks rather than refusing",
      sc.find("f10 is Menu Bar's: give it to Control Panel?") is not None,
      sc)
sc = cprun(KB_CP + [b"\r", b"\x1bc"], end=None)
check("and so is alt-c, which is Copy's -- a key with a dash in its name",
      sc.find("alt-c is Copy's: give it to Control Panel?") is not None, sc)

# A key that moves around the panel cannot become a shortcut: pressing an
# arrow after activating a row is moving on, not choosing the arrow -- 0.77
# took it, and a Menu Bar bound to the right arrow took every arrow from
# every window. The capture ends unchanged and the arrow moves the row.
sc = cprun(reach("shortcuts", "Menu Bar") + [b"\r", b"\x1b[C"], end=None)
check("an arrow after activating a row is not taken as its shortcut",
      "f10" in brow(sc, "Menu Bar") and sc.find("Not changed") is not None, sc)
SAVED = tempfile.mkdtemp(prefix="hibr-keyfix-")
os.makedirs(os.path.join(SAVED, "hibr"))
open(os.path.join(SAVED, "hibr", "desktop.hibr"), "w").write(
    'DT_SETVER=2\nDT_KEYS["menu"]=right\nDT_KEYS["close"]=enter\n'
    'DT_KEYS["terminal"]=alt-ctrl-t\nDT_APPKEY["snake"]=down\n')
out = subprocess.run(
    [sx.HIBR, "-c", '. %s\ndt_load\necho "${DT_KEYS[menu]} ${DT_KEYS[close]} '
     '[${DT_KEYS[terminal]+x}] [${DT_APPKEY[snake]}]"' % WM],
    env=dict(os.environ, XDG_CONFIG_HOME=SAVED),
    capture_output=True, text=True).stdout.strip()
shutil.rmtree(SAVED, True)
check("a saved shortcut on an arrow or enter goes back to its default, and "
      "one for an action that no longer exists is dropped",
      out == "f10 mod-w [] []", out)

# A key another action holds is asked about, and yes moves it: a key never
# has two owners. Detach's ctrl-\\ is the one taken here, not Quit's q,
# since every run ends by pressing q to quit.
sc = cprun(KB_CP + [b"\r", b"\x1c"], end=None)
check("taking a key another action holds asks first, naming both",
      sc.find("ctrl-\\ is Detach's: give it to Control Panel?") is not None,
      sc)
sc = cprun(KB_CP + [b"\r", b"\x1c", b"y"])
check("yes moves it: the other action has none, this one has it",
      "ctrl-\\" not in brow(sc, "Detach") and
      "ctrl-\\" in brow(sc, "Control Panel"), sc)
sc = cprun(KB_CP + [b"\r", b"\x1c", b"n"])
check("no leaves both as they were",
      "ctrl-\\" in brow(sc, "Detach") and
      "ctrl-\\" not in brow(sc, "Control Panel"), sc)

# Two actions sharing a key can only come from a settings file edited by
# hand; both rows say so.
sc = cprun(reach("shortcuts", "Control Panel"), pre="DT_APPKEY[panel]=q\n")
check("a key two actions share is marked on both",
      brow(sc, "Quit ⚠") != "" and brow(sc, "Control Panel ⚠") != "", sc)

# Cleared is kept as cleared: an empty value is saved, not left out, so a
# shortcut that ships with a default does not come back at the next start.
KCONF = tempfile.mkdtemp(prefix="hibr-keyconf-")
KSET = '. %s\ndt_keyset quit ""\ndt_keyset app:term ""\n' % WM
KGET = ('. %s\ndt_load\necho "quit=[${DT_KEYS[quit]}] '
        'term=[${DT_APPKEY[term]}]"\n' % WM)
subprocess.run([sx.HIBR, "-c", KSET], capture_output=True,
               env=dict(os.environ, XDG_CONFIG_HOME=KCONF))
out = subprocess.run([sx.HIBR, "-c", KGET], capture_output=True, text=True,
                     env=dict(os.environ, XDG_CONFIG_HOME=KCONF)).stdout
check("a cleared shortcut is still cleared after a restart",
      "quit=[] term=[]" in out, out)
shutil.rmtree(KCONF, True)

DOWN_APP = [b"\x1b[B"] * downs("appearance")
DOWN_DT = [b"\x1b[B"] * downs("datetime")

sc = cprun(DOWN_APP)
check("down on the picker moves pane by pane, showing each one's rows",
      sc.find("Theme") is not None and sc.find("midnight") is not None, sc)

sc = cprun(DOWN_APP + [b"\x1b[C", b"\x1b[C"], geom=PWTALL)
check("right enters the pane, and a second right cycles its first row",
      "construction" in brow(sc, "Theme") and sc.find("classic") is None, sc)
sc = cprun(DOWN_APP + [b"\t"] + [b"\x1b[D"])
check("tab enters it too, and left cycles the other way, round to the last",
      "retro_car" in brow(sc, "Theme"), sc)
sc = cprun(DOWN_APP + [b"\t", b"\t", b"\x1b[C"])
check("a second tab leaves the pane, back to moving the picker",
      sc.find("construction") is None and
      sc.find(TITLE[ORDER[downs("appearance") + 1]]) is not None, sc)

sc = cprun(DOWN_APP + [press(R0 + 1, VALCOL)])
check("clicking the dropdown's own cell opens a real popup of choices",
      sc.find("construction") is not None and sc.find("meadow") is not None, sc)
sc = cprun(DOWN_APP + [press(R0 + 1, VALCOL), press(R0 + 3, VALCOL + 3)],
           geom=PWTALL)
check("choosing one there applies it, the same as cycling would",
      "construction" in brow(sc, "Theme") and "hazard" in brow(sc, "Colours"), sc)
sc = cprun(DOWN_APP + [press(R0 + 2, VALCOL)])
check("the colour popup offers black, neon, phosphor and amber",
      all(sc.find(t) is not None for t in ("black", "neon", "phosphor",
                                            "amber")), sc)
sc = cprun(DOWN_APP + [press(R0 + 2, VALCOL), press(R0 + 5, VALCOL + 3)])
check("and choosing dracula applies it",
      "dracula" in brow(sc, "Colours") and sc.find("midnight") is None, sc)
sc = cprun(DOWN_APP + [press(R0 + 2, VALCOL), press(R0 + 13, VALCOL + 3)])
check("and phosphor, further down the list",
      "phosphor" in brow(sc, "Colours"), sc)

# The Theme row's own buttons: Save once the look is no longer the theme's,
# Rename and Delete for a theme of the person's own -- d deletes, asked.
MINE = ('mkdir -p "$XDG_CONFIG_HOME/hibr/themes"\n'
        'printf \'{"colours":"paper"}\\n\' > "$XDG_CONFIG_HOME/hibr/themes/mine.json"\n'
        'cp_themes\ncp_theme mine\n')
sc = cprun(DOWN_APP)
check("the Theme row has no Save while the look is the theme's", "Save" not in brow(sc, "Theme"), sc)
sc = cprun(DOWN_APP, post="DT_FRAME=double")
check("and Save, after the name, once a setting has moved it away",
      " Save " in brow(sc, "Theme") and "Rename" not in brow(sc, "Theme"), sc)
sc = cprun(DOWN_APP, post=MINE)
check("a theme of the person's own can be renamed and deleted there",
      " Rename " in brow(sc, "Theme") and " Delete " in brow(sc, "Theme")
      and "mine" in brow(sc, "Theme"), sc)
sc = cprun(DOWN_APP + [b"\x1b[C", b"d", b"y"], post=MINE)
check("d deletes it, after asking", b"Deleted the theme mine" in sc.out
      and "(your own)" in brow(sc, "Theme"), sc)
TD = tempfile.mkdtemp(prefix="hibr-looks-")
os.makedirs(os.path.join(TD, "hibr", "themes"))
open(os.path.join(TD, "hibr", "themes", "mine.json"), "w").write('{"colours":"paper"}')
out = subprocess.run(
    [sx.HIBR, "-c", CPLOAD + 'cp_save() { :; }; dt_notep() { :; }\ncp_theme mine\n'
     'cp_themerenamed "" "%s/hibr/themes/ours"\necho "$CP_THEME ${CP_THEMES[*]}"' % TD],
    env=dict(os.environ, XDG_CONFIG_HOME=TD), capture_output=True, text=True).stdout.strip()
check("Rename moves the file and keeps it chosen",
      out == "ours classic construction meadow ours retro_car" and
      os.path.exists(os.path.join(TD, "hibr", "themes", "ours.json")), out)
shutil.rmtree(TD, True)

# The wallpaper has a pane of its own since 0.99.246 (Gitea #284): Display
# first, then Pattern, so the glyph is one down.
DOWN_WS = [b"\x1b[B"] * downs("wallset")
sc = cprun(DOWN_WS + [b"\x1b[C", b"\x1b[B", b"\x1b[C"])
check("the wallpaper glyph changes, and the desktop follows",
      sc.at(0, 78) != "·" and sc.at(23, 76) == "░", sc)
sc = cprun(reach("appearance", "Menus"))
check("Appearance no longer carries the wallpaper's rows",
      brow(sc, "Pattern") == "" and brow(sc, "Theme") != "", sc)

# A wallpaper of a display's own, by the display's name (Gitea #284). A
# saved one is offered beside All Displays; choosing it hides the pattern,
# which is the whole desktop's, and a change to its Mode is its own while
# All Displays keeps stretch.
WSKIT = "dt_walldisp kitchen '' stretch follow\n"
sc = cprun(DOWN_WS + [b"\x1b[C", b"\x1b[C", b"\x1b[B", b"\x1b[B",
                      b"\x1b[C"], post=WSKIT)
check("a display with a wallpaper of its own is offered by name, and its "
      "Mode is its own",
      "kitchen" in brow(sc, "Display") and brow(sc, "Pattern") == "" and
      "scale" in brow(sc, "Mode"), sc)
sc = cprun(DOWN_WS + [b"\x1b[C", b"\x1b[C", b"\x1b[B", b"\x1b[B",
                      b"\x1b[C", b"\x1b[A", b"\x1b[A", b"\x1b[D"],
           post=WSKIT)
check("while All Displays keeps its own Mode and shows the pattern again",
      "All Displays" in brow(sc, "Display") and brow(sc, "Pattern") != "" and
      "stretch" in brow(sc, "Mode"), sc)
sc = cprun(DOWN_WS + [b"\x1b[C", b"\x1b[C", b"d", b"\x1b[C"], post=WSKIT)
check("and Delete forgets it: it follows All Displays, and is no longer "
      "offered once nothing attached has its name",
      "All Displays" in brow(sc, "Display") and brow(sc, "Pattern") != "" and
      sc.find("kitchen") is None, sc)

# What it is kept as, and the name a display gets when nobody gave one:
# the machine an ssh connection comes from, or the terminal program, with
# nothing in it that would break `hold clients`' space-separated lines.
wsout = subprocess.run(
    [sx.HIBR, "-c", ". %s\n" % WM +
     "SSH_CONNECTION='127.0.0.1 5 10.0.0.1 22'; n := dt_dispname\n"
     "echo \"[$n]\"; unset SSH_CONNECTION; TERM_PROGRAM='My Term'\n"
     "n := dt_dispname; echo \"[$n]\"; DT_CONF=%s\n"
     "dt_walldisp work-laptop '/x y.png' zoom half; dt_save\n"
     "unset DT_WALLDMODE; declare -gA DT_WALLDMODE; . \"$DT_CONF\"\n"
     "echo \"${DT_WALLDIMG[work-laptop]}|${DT_WALLDMODE[work-laptop]}|"
     "${DT_WALLDPIC[work-laptop]}\"\n"
     % os.path.join(PANEHOME, "wsconf.hibr")],
    capture_output=True, text=True).stdout
check("a display with no --name is named for where it connects from, with "
      "no space in it",
      "[localhost]" in wsout and "[My-Term]" in wsout, wsout)
check("and its wallpaper is saved and read back by that name",
      "/x y.png|zoom|half" in wsout, wsout)
# Two terminals on one machine attach as laptop and laptop#2, since hold
# needs the names to differ, and share laptop's wallpaper -- the owner's
# choice -- which the pane offers once, as laptop.
wsout = subprocess.run(
    [sx.HIBR, "-c", CPLOAD +
     "DT_DISPNAMES=(laptop 'laptop#2'); dt_walldisp laptop /x.png zoom half\n"
     "dt_walluse 'laptop#2' '' stretch follow; echo \"[$DT_WALLIMG $DT_WALLMODE]\"\n"
     "cp_wdnames; echo \"<${CP_WDLIST[*]}>\"\n"],
    capture_output=True, text=True).stdout
check("a second display from one machine shares the first one's wallpaper, "
      "offered once by its name",
      "[/x.png zoom]" in wsout and "<laptop>" in wsout, wsout)

# Wallpaper -- #2 -- browses for an image and previews it, in a window of
# its own opened from Control Panel > Wallpaper's "Image…" row. Its folders come
# from the desktop's own Open dialog (fp_go), so none of these sessions
# loads the Files app: until 0.99.228 the picker was built on Files and a
# fresh install could not choose a wallpaper (Gitea #265). Its own $HOME,
# so what it lists is known: a non-image, a subdirectory, and the same 2x2
# PNG fixture other img tests already use.
AWHOME = tempfile.mkdtemp(prefix="hibr-wp-home-")
shutil.copy(os.path.abspath("tests/img-2x2.png"),
            os.path.join(AWHOME, "wall.png"))
open(os.path.join(AWHOME, "notes.txt"), "w").write("hi\n")
os.mkdir(os.path.join(AWHOME, "sub"))
shutil.copy(os.path.abspath("tests/img-2x2.png"),
            os.path.join(AWHOME, "sub", "deep.png"))
DOWN_WP = reach("wallset", "Image")

sc = cprun(DOWN_WP)
check("Control Panel > Wallpaper offers the image picker as a row of its own",
      brow(sc, "Image…") != "", sc)
check("and the way an image fills the screen, stretch by default",
      "stretch" in brow(sc, "Mode"), sc)
sc = cprun(reach("wallset", "Mode") + [b"\x1b[C"])
check("which cycles to scale",
      "scale" in brow(sc, "Mode"), sc)
# The wallpaper's own Picture row, as YouTube's player has one (Gitea #275):
# follow, the Pictures setting, until somebody chooses, with a Drawn Now
# row saying what that comes to; and it cycles through the same five.
sc = cprun(reach("wallset", "Picture"))
check("the wallpaper is drawn by a Picture setting of its own, "
      "following Pictures until chosen, and says what that comes to",
      "follow" in brow(sc, "Picture") and brow(sc, "Drawn Now") != "", sc)
sc = cprun(reach("wallset", "Picture") + [b"\x1b[C"])
check("which steps to half blocks",
      "half" in brow(sc, "Picture") and brow(sc, "Drawn Now") == "", sc)

sc = cprun(DOWN_WP + [b"\r"], env={"HOME": AWHOME})
check("with no Files app installed, it lists the home directory's folders "
      "and images, and leaves out what is not an image",
      sc.find("wall.png") is not None and sc.find("sub/") is not None
      and sc.find("notes.txt") is None, sc)
check("nothing is selected yet, so there is no preview",
      sc.find("Select an image") is not None, sc)
check("it is a window of its own, with Apply and Close buttons",
      sc.find("┤ Wallpaper ├") is not None and
      re.search(r" Apply .* Close ", sc.text()) is not None, sc)

# fp_go lists the folder above, then folders, then the images -- here
# 0 "..", 1 "sub/", 2 "wall.png".
sc = cprun(DOWN_WP + [b"\r", b"\x1b[B"], env={"HOME": AWHOME})
check("selecting a folder leaves the preview cleared",
      sc.find("Select an image") is not None, sc)

sc = cprun(DOWN_WP + [b"\r", b"\x1b[B", b"\x1b[B"],
           env={"HOME": AWHOME})
check("selecting the .png shows a preview instead",
      sc.find("Select an image") is None, sc)

# A JPEG is offered and previewed the same way: the picker lists it, and
# choosing it draws it. Named to sort last, so the rows above keep their
# places.
shutil.copy(os.path.abspath("tests/img-quad.jpg"), os.path.join(AWHOME, "zz.jpg"))
sc = cprun(DOWN_WP + [b"\r", b"\x1b[B", b"\x1b[B", b"\x1b[B"],
           env={"HOME": AWHOME}, pre=load("img"))
check("a JPEG is offered too, and choosing it shows a preview",
      sc.find("zz.jpg") is not None and sc.find("Select an image") is None, sc)

# The preview is ASCII text, drawn through this pane's own buffer like
# every other window's content (console put -p), not a raw draw straight
# at screen coordinates -- tried once for a colour preview, reverted
# after it corrupted the display applying a new wallpaper while this
# pane's own preview was open at the same time, the two most likely
# racing over the same screen region with no pane of its own to track
# damage against. See wp_preview's own comment.
#
# It also keeps the image's own aspect ratio rather than stretching it to
# fill the whole box -- img -g has no notion of the source's own shape
# either, so wallpick_draw works out a smaller, fitted size itself (img
# size, added for this) before asking for it. A very wide, short fixture
# makes a stretch-to-fill bug obvious: stretched, its density-ramp
# characters would fill every row of the preview; fitted, only one or two.
WIDEHOME = tempfile.mkdtemp(prefix="hibr-wp-wide-")
shutil.copy(os.path.abspath("tests/img-wide.png"),
            os.path.join(WIDEHOME, "wide.png"))
sc = cprun(DOWN_WP + [b"\r", b"\x1b[B"], env={"HOME": WIDEHOME},
           pre=load("img"))
filledrows = [r for r in range(sc.rows) if "-" in sc.row(r)]
check("a very wide image is fitted, not stretched to fill the box",
      0 < len(filledrows) < 10, sc)
shutil.rmtree(WIDEHOME, True)

# panel_wheel only falls back to its own generic top/n/vis scrolling for a
# pane with no _draw of its own -- a custom-drawn one, this picker among
# them, has to define its own _wheel the same way it defines its own
# _key. Missing here meant the wheel simply did nothing over the picker
# at all, reported live. Fixture, not a live scroll through the pty: the
# picker only needs enough files to scroll with a directory this suite
# would have to fabricate dozens of just to reach past one screen's worth.
# The widget comes first because the picker's own scrolling is the shared
# dt_scrollmove now, not arithmetic of its own: a pane sourced on its own
# still needs what it draws and scrolls with.
WPWHEEL = (
    '. %s\n'
    '. %s/wallpaper.hibr\n'
    'declare -gA FP\n'
    'wp_refresh() { :; }\n'
    'FP[1]["n"]=20; FP[1]["top"]=0; FP[1]["sel"]=0\n'
    'WPK[1]["vis"]=5\n'
    'wallpick_wheel 1 down; echo "d1=${FP[1]["top"]}"\n'
    'wallpick_wheel 1 down; wallpick_wheel 1 down\n'
    'wallpick_wheel 1 down; wallpick_wheel 1 down\n'
    'wallpick_wheel 1 down; echo "d6=${FP[1]["top"]}"\n'
    'i=0; while [ "$i" -lt 9 ]; do wallpick_wheel 1 up; i=$((i + 1)); done\n'
    'echo "u9=${FP[1]["top"]}"\n'
    % (tree("programs/desktop/widgets/scrollbar.hibr"), CP)
)
out = subprocess.run([sx.HIBR, "-c", WPWHEEL], capture_output=True, text=True,
                     env=dict(os.environ, DT_ROWS="1")).stdout
check("the wheel scrolls the wallpaper picker, one notch at a time",
      "d1=3" in out, out)
check("and keeps scrolling without overrunning the list",
      "d6=15" in out, out)
check("but not above the first entry",
      "u9=0" in out, out)

# A notch of the wheel is DT_SCROLLWHEEL rows in every window, not a 3 each
# app wrote for itself (Gitea #354): Mail's list and dbase's table, called
# directly with the step set to 5. command_not_found stands in for the
# window manager's registrations, which a lone app file makes at load.
WHEEL5 = (
    'command_not_found() { return 0; }\n'
    'DT_HERE=%s\n'
    '. %s/Internet/mail.hibr 2> /dev/null\n'
    '. %s/Office/dbase.hibr 2> /dev/null\n'
    'DT_SCROLLWHEEL=5\n'
    'MW[1]["top"]=0; mail_wheel 1 down; echo "mail=${MW[1]["top"]}"\n'
    'DBA[1]["scroll"]=0; dbase_wheel 1 up 3 3; echo "dbase=${DBA[1]["scroll"]}"\n'
    % (tree("programs/desktop"), tree("programs/desktop/apps"),
       tree("programs/desktop/apps"))
)
out = subprocess.run([sx.HIBR, "-c", WHEEL5], capture_output=True, text=True).stdout
check("a notch of the wheel moves Mail's list by DT_SCROLLWHEEL rows",
      "mail=5" in out, out)
check("and dbase's table by the same",
      "dbase=5" in out, out)

# A click selects a row; a second click on the same, already-selected
# one is what actually navigates or applies. Neither did anything at all
# before this: the column check guarding the whole list gated on the
# list's own *width* rather than on where its body actually starts, so
# every click in it -- not just a repeated one -- was silently rejected,
# which is what "not navigatable by mouse" turned out to mean in full.
#
# Where the picker is comes from the screen: it opens over the Control
# Panel it was asked from (Gitea #293), not in the middle of the screen,
# so its first entry -- the row under `../` -- is read rather than
# assumed.
sc = cprun(DOWN_WP + [b"\r"], env={"HOME": AWHOME})
wpt = sc.find("┤ Wallpaper ├") or (2, 10)
WPROW, WPCOL = wpt[0] + 3, wpt[1]
sc = cprun(DOWN_WP + [b"\r", press(WPROW, WPCOL)],
           env={"HOME": AWHOME})
check("a single click on a directory only selects it, not enters it",
      sc.find("wall.png") is not None, sc)
sc = cprun(DOWN_WP + [b"\r", press(WPROW, WPCOL), press(WPROW, WPCOL)],
           env={"HOME": AWHOME})
check("a second click on the same row enters the directory",
      sc.find("deep.png") is not None and sc.find("wall.png") is None, sc)

# Backspace goes up a folder, and from one just under the root -- /tmp,
# /home -- it reaches the root. fp_go was handed the empty name that
# `${dir%/*}` makes of such a folder, said it could not open it, and
# stayed: in Open and Save As too, which share it.
sc = cprun(DOWN_WP + [b"\r"] + [b"\x7f"] * AWHOME.count("/"),
           env={"HOME": AWHOME})
check("going up a folder at a time reaches the root",
      sc.find("etc/") is not None and sc.find("Cannot open") is None, sc)

# Applying is keyboard-reachable, not only a mouse click on the button --
# enter on an already-previewed file, the same as everywhere else in
# this desktop enter means "confirm this".
# Not asserting on dt_note's own "Wallpaper set" appearing on screen
# here: applying repaints the whole desktop in the same frame Control
# Panel itself redraws in, and the screen model's own simplified escape
# reconstruction ("it understands absolute cursor moves, relative moves
# to the right, and the erase that starts a full-screen program;
# everything else is skipped") cannot keep up with that much of the
# screen changing in one frame. The saved config is the real, reliable
# proof this worked.
AWCONF = tempfile.mkdtemp(prefix="hibr-wp-conf-")
run(*PANEL, feed=DOWN_WP + [b"\r", b"\x1b[B", b"\x1b[B", b"\r"],
    env={"HOME": AWHOME, "XDG_CONFIG_HOME": AWCONF}, pre=CPANES)
saved = os.path.join(AWCONF, "hibr", "desktop.hibr")
text = open(saved).read() if os.path.exists(saved) else ""
check("enter on a previewed file applies it, saved as the wallpaper",
      ("DT_WALLIMG=%s/wall.png" % AWHOME) in text and
      "DT_WALLASCII" not in text, text)
shutil.rmtree(AWCONF, True)

# The picker remembers where it was left, across a restart. With nothing
# recorded yet, it starts at the currently-applied wallpaper's own
# directory rather than $HOME -- a bogus $HOME here proves that, since a
# wrong fallback to it would find nothing.
sc = run(*PANEL, feed=DOWN_WP + [b"\r"],
         env={"HOME": "/nonexistent-for-this-check"},
         pre=CPANES + "\nDT_WALLIMG=%s/wall.png" % AWHOME)
check("with no directory remembered yet, it starts at the wallpaper's own",
      sc.find("wall.png") is not None, sc)
cl = sc.find(" Close ")
sc = run(*PANEL, feed=DOWN_WP + [b"\r"] +
         ([press(cl[0], cl[1] + 1)] if cl else []),
         env={"HOME": "/nonexistent-for-this-check"},
         pre=CPANES + "\nDT_WALLIMG=%s/wall.png" % AWHOME)
check("and Close closes the picker, leaving the Control Panel",
      cl is not None and sc.find("┤ Wallpaper ├") is None and
      sc.find("┤ Panel ├") is not None, sc)

# The search finds panes by their titles and by their rows, so "glyph"
# finds Appearance; escape clears it. Reached with `/` since 0.99.193 and
# drawn on the window's own bottom row: it used to be a box at the top of
# this list that typing went into, which was one of the six shapes the
# desktop's searches had before they were made one.
sc = cprun([b"/"] + [c.encode() for c in "glyph"] + [0.3])
check("the Control Panel's search finds a pane by a row inside it",
      "/glyph" in sc.text() and "Appearance" in sc.row(R0 + 1) and
      sc.find("Date & Time") is None and sc.find("Mouse") is None, sc)
sc = cprun([b"/"] + [c.encode() for c in "zzzx"] + [0.3])
check("and says when nothing matches", sc.find("Nothing found") is not None,
      sc)
sc = cprun([b"/"] + [c.encode() for c in "wall"] + [b"\x1b", 0.6])
check("escape clears it, and every pane is back",
      sc.find("/wall") is None and sc.find("Date & Time") is not None,
      sc)

# Copy, cut and paste in a one-line field whose own window offers no
# clipboard at all (Gitea #214). The three keys are taken by dt_event
# before any app's _key is called and act through the focused window's own
# _copy/_cut/_paste, so a field had a clipboard only if its app had
# written one: eleven of the twenty files that draw a field could paste,
# none could copy or cut, and the Control Panel -- which hosts more fields
# than anything else -- offered none of the three. Its own picker search
# box is one of them, which is why this is driven here: `dsv`, the Network
# Servers dialog, already had all three and would prove nothing.
#
# The field's value belongs to its app, so the write is done by typing:
# every one of those files routes keys through dt_textkey. Since Gitea
# #295 a field has a selection and Copy and Cut take it, so each check
# selects the word first with shift and home; a cut is backspace typed
# over the selection.
# Each check reads the box itself rather than the note beside it: a note
# times out, so asserting on one makes the check a bet on how long the
# teardown takes.
def sbox(sc):
    """What the search bar holds. It is the window's own bottom row and
    opens with `/` (widgets/search.hibr), so the text is what follows --
    read by finding the bar rather than by a row number, since the bar is
    only there while there is a search to show."""
    for row in sc.text().split("\n"):
        m = re.search(r"\u2502/(\S*)\s*\u2502", row)
        if m:
            return m.group(1)
    return ""


SHOME, FEND = b"\x1b[1;2H", b"\x1b[F"
sc = cprun([b"/"] + [c.encode() for c in "mail"] +
           [SHOME, b"\x1bc", 0.4, FEND, b"\x1bv", 0.8])
check("a field whose window has no clipboard of its own can still be "
      "copied, and pasted", sbox(sc) == "mailmail", sc)
sc = cprun([b"/"] + [c.encode() for c in "mail"] + [SHOME, b"\x1bx", 0.6])
check("and cut, which takes what is selected out of it",
      sbox(sc) != "mail", sc)
# Pasting into an **empty** field is the case that needs the *window* to be
# remembered rather than the field: several apps draw a placeholder in
# place of the field while it is empty -- the picker says `Search…` -- so
# dt_textdraw is not called at all on the frame after a cut, and clearing
# the remembered window with the text would make this impossible.
# A typed `x` between the cut and the paste, so the box can only read
# `xmail` if both happened and in that order. Asserting the box is back to
# `mail` would have passed against the old code for the wrong reason --
# the cut did nothing, so nothing had to be put back -- which is this
# file's own rule about a check on an absence, caught here by running the
# new check against the previous release before trusting it.
sc = cprun([b"/"] + [c.encode() for c in "mail"] + [SHOME, b"\x1bx", 0.4, b"x",
                                                     b"\x1bv", 0.8])
check("and pasted back, though an empty field draws a placeholder and not "
      "a field at all", sbox(sc) == "xmail", sc)

# Change Wallpaper… on the desktop's own menu opens the Control Panel at
# Wallpaper, where the wallpaper is chosen (Gitea #284).
sc = cprun(feed=[press(20, 76, 2), b"w", 0.5])
check("Change Wallpaper on the desktop opens the Control Panel at Wallpaper",
      sc.find("Display") is not None and sc.find("Pattern") is not None, sc)

# The divider between the list and the pane is dragged (widgets/split.hibr),
# and where it is left is a setting kept for next time.
sc = run(*PANEL, feed=[press(10, 21), drag(10, 25), drag(10, 29),
                       release(10, 29), 0.3], pre=CPANES)
check("the Control Panel's divider drags: the list wider, the pane narrower",
      sc.at(10, 29) == "│" and sc.at(10, 21) != "│" and
      sc.find("Notifications") is not None, sc)

# Once it has browsed somewhere else, that becomes the new starting
# point -- a directory distinct from both $HOME and the wallpaper's own,
# so this is not just the check above by coincidence.
AWCONF2 = tempfile.mkdtemp(prefix="hibr-wp-conf2-")
run(*PANEL, feed=DOWN_WP + [b"\r", b"\x1b[B", b"\r"],
    env={"HOME": AWHOME, "XDG_CONFIG_HOME": AWCONF2}, pre=CPANES)
sc = run(*PANEL, feed=DOWN_WP + [b"\r"],
         env={"HOME": "/nonexistent-for-this-check",
              "XDG_CONFIG_HOME": AWCONF2},
         pre=CPANES)
check("once it has browsed a directory, that is where it starts next",
      sc.find("deep.png") is not None, sc)
shutil.rmtree(AWHOME, True)
shutil.rmtree(AWCONF2, True)

# Desktop: Icons, Disk Icons, and the redraw-skip slider.
sc = cprun([b"\x1b[B"] * downs("desktop"))
check("Desktop holds Icons, Disk Icons and Redraw Skip",
      brow(sc, "Icons") != "" and brow(sc, "Disk Icons") != "" and
      sc.find("Redraw Skip") is not None, sc)

sc = cprun(reach("desktop", "Icons") + [b"\r"])
check("the icons can be switched off, and the panel shows an unchecked box",
      "[ ]" in brow(sc, "Icons"), sc)

# The shadows, each its own setting, under a heading in Appearance; the
# button shadow is with the rest of the dialog buttons' settings.
sc = cprun(DOWN_APP, geom=PWTALL)
check("Appearance lists three shadows under Shadows and the buttons' under "
      "Dialog Buttons, all on",
      sc.find("Shadows") is not None and sc.find("Dialog Buttons") is not None
      and all("[x]" in brow(sc, t) for t in ("Windows", "Menus", "Bar Shadow",
                                             "Button Shadow")), sc)
check("dialog buttons start filled",
      "filled" in brow(sc, "Dialog Buttons") and brow(sc, "Confirm Starts On") == "",
      sc)
sc = cprun(reach("appearance", "Menus") + [b"\r"])
check("menu shadow is its own setting, separate from window shadow",
      "[ ]" in brow(sc, "Menus") and "[x]" in brow(sc, "Windows"), sc)
sc = cprun(reach("appearance", "Bar Shadow") + [b"\r"])
check("bar shadow is a third, separate setting again",
      "[ ]" in brow(sc, "Bar Shadow") and "[x]" in brow(sc, "Menus"), sc)
sc = cprun(reach("appearance", "Button Shadow") + [b"\r"])
check("button shadow is its own setting, on until switched off here",
      "[ ]" in brow(sc, "Button Shadow") and "[x]" in brow(sc, "Bar Shadow"), sc)
sc = cprun(reach("appearance", "Dialog Buttons") + [b"\x1b[C"])
check("the dialog button style steps to brackets",
      "brackets" in brow(sc, "Dialog Buttons"), sc)

# What a double click on a title bar does belongs with the Mouse.
sc = cprun(reach("mouse", "Titlebar Click"))
check("titlebar double-click defaults to zoom, in Mouse",
      "zoom" in brow(sc, "Titlebar Click"), sc)
sc = cprun(reach("mouse", "Titlebar Click") + [b"\x1b[C"])
check("and it cycles through the other actions",
      "min" in brow(sc, "Titlebar Click"), sc)

# Keyboard is the hardware pane: how keys reach a terminal window. Which
# key does what is all in Shortcuts.
sc = cprun(reach("keyboard", "Shortcuts in Terminals"))
check("Keyboard holds Shortcuts in Terminals and Terminals Keep Ctrl+A-Z, "
      "both on, and nothing to read but settings",
      "[x]" in brow(sc, "Shortcuts in Terminals") and
      "[x]" in brow(sc, "Terminals Keep Ctrl+A-Z") and
      sc.find("menu bar") is None, sc)
sc = cprun(reach("keyboard", "Shortcuts in Terminals") + [b"\r"])
check("and each can be switched off",
      "[ ]" in brow(sc, "Shortcuts in Terminals"), sc)
check("the Terminal pane no longer has it",
      ("set", "Shortcuts in Terminals")
      not in [(k, x.strip()) for k, x in panerows("terminal", ("term",))])

# Every key the desktop acts on is a setting, and a changed one applies
# everywhere at once: in every window, and on the Edit menu.
CB = stdir()
run("stickies", "8 30 14 48", pre='DT_KEYS["copy"]="f5"',
    feed=[b"a", b"b", b"c", b"\x1b[1;2H", b"\x1bc", b"\x1b[15~", b"\x1b[F",
          b"\x1bv"], env=stenv(CB), end=None)
note = stnote(CB) or ""
check("Copy moved to f5 copies on f5, and alt-c no longer does",
      note == "abcabc", note)
shutil.rmtree(CB, True)
sc = run("stickies", "8 30 14 48", pre='DT_KEYS["copy"]="f5"',
         feed=[b"\x1b[21~", b"\x1b[C", b"\x1b[C"], env=stenv(stdir()), end=None)
# The row, not the spacing between the two: the shortcut is a field of its
# own and is right-aligned to the menu's own width now, so a check on the
# exact gap was a check on the padding rather than on what it says
# (Gitea #200).
row = [sc.row(r) for r in range(sc.rows) if "Copy" in sc.row(r)]
check("the Edit menu shows the key Copy has now",
      bool(row) and "f5" in row[0], row or sc)
sc = run("clock", "9 24 3 20", pre='DT_KEYS["menu"]="f9"',
         feed=[b"\x1b[20~"], end=None)
check("the Menu Bar key can move too: f9 opens the menu bar",
      sc.find("Screen Saver") is not None, sc)
sc = run("clock", "9 24 3 20", pre='DT_KEYS["menu"]="f9"\nDT_KEYS["menu2"]=',
         feed=[b"\x1b[21~", b"\x1b"], end=None)
check("and f10 and escape then do nothing of the menu bar's",
      sc.find("Screen Saver") is None, sc)

# A settings file keeps every default as it was when written, so new
# defaults reach it through DT_SETVER, once: a file with no version gets
# Desktop Shortcuts Win (0.72); one older than 2 gets 0.73's -- the
# terminal scrollbar, alt-drag, ctrl-w, alt-tab and no quit key -- and a
# key that was changed from its old default is left alone.
def loadconf(conf):
    d = tempfile.mkdtemp(prefix="hibr-mig-")
    os.makedirs(os.path.join(d, "hibr"))
    open(os.path.join(d, "hibr", "desktop.hibr"), "w").write(conf)
    out = subprocess.run(
        [sx.HIBR, "-c", ". %s\ndt_load\necho \"$DT_SETVER $DT_TERMKEEP "
         "$DT_TERMBAR $DT_DRAGMOD ${DT_KEYS[close]} ${DT_KEYS[cycle]} "
         "[${DT_KEYS[quit]}]\"" % WM],
        env=dict(os.environ, XDG_CONFIG_HOME=d),
        capture_output=True, text=True).stdout.strip()
    shutil.rmtree(d, True)
    return out


OLDKEYS = ('DT_KEYS["close"]=alt-f4\nDT_KEYS["cycle"]=tab\n'
           'DT_KEYS["quit"]=q\nDT_TERMBAR=0\nDT_DRAGMOD=0\n')
out = loadconf("DT_TERMKEEP=0\n" + OLDKEYS)
check("a settings file from before 0.72 is brought up to 0.73's defaults",
      out == "13 1 1 1 mod-w mod-tab []", out)
out = loadconf("DT_SETVER=1\nDT_TERMKEEP=0\n" + OLDKEYS)
check("one from 0.72 keeps its Shortcuts Win and gets the rest",
      out == "13 0 1 1 mod-w mod-tab []", out)
out = loadconf('DT_SETVER=1\nDT_KEYS["close"]=alt-x\nDT_KEYS["cycle"]=f6\n'
               'DT_KEYS["quit"]=ctrl-q\n')
check("a key changed from its old default is not touched",
      out.endswith("alt-x f6 [ctrl-q]"), out)
out = loadconf("DT_SETVER=2\nDT_TERMBAR=0\nDT_DRAGMOD=0\n")
check("and a 0.73 file is read as it is, choices and all",
      out.startswith("13 1 0 0 "), out)


# 0.99.80 holds an output-driven frame 70 ms from the last rather than 33
# (thirty a second, which no text program needs, and twice the CPU of
# fifteen). A file still holding the old default moves; a chosen value stays.
def loadms(conf):
    d = tempfile.mkdtemp(prefix="hibr-mig-")
    os.makedirs(os.path.join(d, "hibr"))
    open(os.path.join(d, "hibr", "desktop.hibr"), "w").write(conf)
    out = subprocess.run(
        [sx.HIBR, "-c", ". %s\ndt_load\necho \"$DT_TERMMS\"" % WM],
        env=dict(os.environ, XDG_CONFIG_HOME=d),
        capture_output=True, text=True).stdout.strip()
    shutil.rmtree(d, True)
    return out


out = loadms("DT_SETVER=7\nDT_TERMMS=33\n")
check("a file holding 0.99.79's 33ms between output frames moves to 70",
      out == "70", out)
out = loadms("DT_SETVER=7\nDT_TERMMS=45\n")
check("and a frame interval somebody chose is left alone", out == "45", out)


# 0.99.26 put the workspaces on alt and an arrow and Snap on ctrl-alt: a
# file still holding the old defaults swaps them, and a key someone chose
# is kept, its partner moving onto the key it gave up.
def loadclose(conf):
    """The same, for the Close Window key, which 9 moves on its own rather
    than by a swap."""
    d = tempfile.mkdtemp(prefix="hibr-mig-")
    os.makedirs(os.path.join(d, "hibr"))
    open(os.path.join(d, "hibr", "desktop.hibr"), "w").write(conf)
    out = subprocess.run(
        [sx.HIBR, "-c", '. %s\ndt_load\necho "${DT_KEYS[close]}"' % WM],
        env=dict(os.environ, XDG_CONFIG_HOME=d),
        capture_output=True, text=True).stdout.strip()
    shutil.rmtree(d, True)
    return out


def loadkeys(conf):
    d = tempfile.mkdtemp(prefix="hibr-mig-")
    os.makedirs(os.path.join(d, "hibr"))
    open(os.path.join(d, "hibr", "desktop.hibr"), "w").write(conf)
    out = subprocess.run(
        [sx.HIBR, "-c", ". %s\ndt_load\necho \"${DT_KEYS[snapleft]} ${DT_KEYS[wsprev]} "
         "${DT_KEYS[snapright]} ${DT_KEYS[wsnext]}\"" % WM],
        env=dict(os.environ, XDG_CONFIG_HOME=d),
        capture_output=True, text=True).stdout.strip()
    shutil.rmtree(d, True)
    return out


OLDWS = ('DT_SETVER=2\nDT_KEYS["snapleft"]=alt-left\nDT_KEYS["snapright"]=alt-right\n'
         'DT_KEYS["wsprev"]=ctrl-alt-left\nDT_KEYS["wsnext"]=ctrl-alt-right\n')
# 0.99.26 swapped Snap and the workspaces; 0.99.90 (settings version 9) swapped
# them back, because the 0.99.26 arrangement left the four tiling directions
# split across two modifiers. So a file from before 0.99.26 goes through both
# and lands where it began -- which is the point rather than a coincidence, and
# is why this reads as "unchanged" now.
out = loadkeys(OLDWS)
check("a file from before 0.99.26 goes through both swaps and ends where it was",
      out == "mod-left ctrl-alt-left mod-right ctrl-alt-right", out)
out = loadkeys(OLDWS + 'DT_KEYS["wsprev"]=f7\n')
check("a workspace key someone chose stays, and Snap still lands on alt",
      out == "mod-left f7 mod-right ctrl-alt-right", out)
out = loadkeys('DT_SETVER=3\nDT_KEYS["snapleft"]=alt-left\nDT_KEYS["wsprev"]=ctrl-alt-left\n')
check("a file written between the two is already where 9 wants it",
      out == "mod-left ctrl-alt-left mod-right ctrl-alt-right", out)
NEWWS = ('DT_SETVER=8\nDT_KEYS["snapleft"]=ctrl-alt-left\nDT_KEYS["snapright"]=ctrl-alt-right\n'
         'DT_KEYS["wsprev"]=alt-left\nDT_KEYS["wsnext"]=alt-right\nDT_KEYS["close"]=ctrl-w\n')
out = loadkeys(NEWWS)
check("a file holding 0.99.89's own defaults moves to alt and ctrl-alt",
      out == "mod-left ctrl-alt-left mod-right ctrl-alt-right", out)
out = loadkeys(NEWWS + 'DT_KEYS["close"]=ctrl-q\n')
check("and a close key someone chose is not moved to alt-w",
      loadclose(NEWWS) == "mod-w" and loadclose(NEWWS + 'DT_KEYS["close"]=ctrl-q\n')
      == "ctrl-q", loadclose(NEWWS + 'DT_KEYS["close"]=ctrl-q\n'))

# Colour schemes are JSON files, read from the person's own folders first
# and then the bundled one; a file of the same name replaces a bundled
# scheme, a new name adds one, and a file that is not valid is left out
# whole. The person's themes folder is read for colours too, since that is
# where every colour file lived before themes and colours came apart.
TD = tempfile.mkdtemp(prefix="hibr-themes-")
os.makedirs(os.path.join(TD, "hibr", "themes"))
TH = os.path.join(TD, "hibr", "themes")
GOOD = ('{"wall":"#010203","dot":"#111111","bar":"#222222","active":"#333333",'
        '"idle":"#444444","face":"#555555","ink":"#666666","shadow":70}')
open(os.path.join(TH, "zinc.json"), "w").write(GOOD)
open(os.path.join(TH, "paper.json"), "w").write(GOOD)
open(os.path.join(TH, "broken.json"), "w").write('{"wall":')
open(os.path.join(TH, "badcolour.json"), "w").write(
    GOOD.replace("#666666", "red"))
open(os.path.join(TH, "badshadow.json"), "w").write(
    GOOD.replace('"shadow":70', '"shadow":500'))
out = subprocess.run(
    [sx.HIBR, "-c", CPLOAD + 'echo "${CP_COLOURLIST[*]}"; cp_colours paper; '
     'echo "$DT_WALL $DT_SHADOW_PCT"; cp_colours midnight; echo "$DT_WALL"; '
     'cp_colours broken || echo refused'],
    env=dict(os.environ, XDG_CONFIG_HOME=TD),
    capture_output=True, text=True).stdout.split("\n")
shutil.rmtree(TD, True)
check("every bundled colour scheme is listed, sorted, with a new one of the "
      "person's own among them",
      out[0] == "amber black dracula ember forest hazard meadow midnight neon paper "
      "phosphor slate zinc", out)
check("a scheme of the person's own replaces the bundled one of that name",
      out[1] == "#010203 70", out)
check("the bundled ones still apply as before", out[2] == "#0d1b2a", out)
check("a broken file, a colour that is not one, and a shadow out of range "
      "are each left out", out[3] == "refused" and
      "broken" not in out[0] and "bad" not in out[0], out)

# A theme may also set colour roles -- dim, selink, good, warn, bad, info,
# well -- and gets the dark themes' values for any it leaves out.
TD = tempfile.mkdtemp(prefix="hibr-themes-")
os.makedirs(os.path.join(TD, "hibr", "themes"))
open(os.path.join(TD, "hibr", "themes", "rosy.json"), "w").write(
    GOOD.replace('"shadow":70', '"bad":"#aa0000","good":"#00aa00","shadow":70'))
open(os.path.join(TD, "hibr", "themes", "badrole.json"), "w").write(
    GOOD.replace('"shadow":70', '"bad":"crimson","shadow":70'))
out = subprocess.run(
    [sx.HIBR, "-c", CPLOAD + 'cp_colours rosy; echo "$DT_BAD $DT_GOOD $DT_WARN"; '
     'cp_colours paper; echo "$DT_BAD $DT_SELINK"; echo "${CP_COLOURLIST[*]}"'],
    env=dict(os.environ, XDG_CONFIG_HOME=TD),
    capture_output=True, text=True).stdout.split("\n")
shutil.rmtree(TD, True)
check("a scheme's roles are its own, and one it leaves out is the default",
      out[0] == "#aa0000 #00aa00 #f6ad55" and out[1] == "#c53030 #f5f2ea", out)
check("and a role that is not a colour leaves the file out",
      "badrole" not in out[2], out)

# A theme is the whole look: a colour scheme by name and the rest of the
# look, each value checked; the person's own folder first, and an old
# colour file there is not taken for a theme.
TD = tempfile.mkdtemp(prefix="hibr-looks-")
TH = os.path.join(TD, "hibr", "themes")
os.makedirs(TH)
open(os.path.join(TH, "zinc.json"), "w").write(GOOD)
open(os.path.join(TH, "dusk.json"), "w").write(
    '{"colours":"dracula","frame":"double","titlebar":"solid","align":"center",'
    '"image":"dusk.png","windowshadow":false}')
open(os.path.join(TH, "badframe.json"), "w").write('{"colours":"paper","frame":"triple"}')
open(os.path.join(TH, "badshadow.json"), "w").write('{"colours":"paper","menushadow":"maybe"}')
open(os.path.join(TH, "nocolours.json"), "w").write('{"frame":"double"}')
out = subprocess.run(
    [sx.HIBR, "-c", CPLOAD + 'echo "${CP_THEMES[*]}"\n'
     'cp_theme construction; echo "$CP_THEME $CP_COLOURS $DT_FRAME $DT_TITLEBAR $DT_BTNSTYLE $DT_CHECKS $DT_FACE ${DT_WALLIMG##*/}"\n'
     'cp_theme dusk; echo "$CP_COLOURS $DT_FRAME $DT_TITLEBAR $DT_TITLEALIGN $DT_SHADOW $DT_WALLIMG"\n'
     'cp_theme classic; echo "$CP_COLOURS $DT_FRAME $DT_TITLEBAR $DT_TITLEALIGN $DT_SHADOW [$DT_WALLIMG]"\n'
     'cp_theme badframe || echo refused\n'
     'cp_save() { :; }; dt_notep() { :; }\n'
     'cp_theme dusk; DT_FRAME=rounded; cp_colours paper; cp_themewrite "%s/Mine Too.json"\n'
     'cp_theme classic; cp_theme "Mine Too"; echo "$CP_THEME $CP_COLOURS $DT_FRAME $DT_TITLEBAR $DT_TITLEALIGN"\n'
     'echo "${CP_THEMES[*]}"' % TH],
    env=dict(os.environ, XDG_CONFIG_HOME=TD),
    capture_output=True, text=True).stdout.split("\n")
check("the bundled themes and the person's own are listed; a colour file, and "
      "a theme with a value its setting cannot take or no colours, are not",
      out[0] == "classic construction dusk meadow retro_car", out)
check("a theme sets its colours and the look with them -- Under Construction "
      "is yellow, double framed, solid barred, with its own picture",
      out[1] == "construction hazard double solid squares block #ffd400 construction.png", out)
check("a theme's picture is found beside the theme, and what a theme leaves "
      "out is the desktop's default, whatever came before",
      out[2] == "dracula double solid center 0 %s/dusk.png" % TH
      and out[3] == "midnight single line left 1 []", out)
check("a theme that fails its checks is refused", out[4] == "refused", out)
check("Save Current Look as Theme writes the look as it is, and it comes back the same",
      out[5] == "Mine Too paper rounded solid center" and "Mine Too" in out[6], out)
shutil.rmtree(TD, True)

# `apps install` places a theme or a colour scheme in
# $XDG_DATA_HOME/hibr/<kind>, exactly as it places an application, and
# until 0.99.187 **neither list looked there at all**: a theme could be
# published, fetched, its sha256 verified and its picture put beside it,
# and the Appearance pane would never show it -- which is half a feature,
# and the half nobody would have noticed from the install's own output
# (Gitea #224). The order is the one every scan list keeps, so the check
# reads both halves: a fetched file is listed, and a file of the person's
# own with that name wins over it.
CD = tempfile.mkdtemp(prefix="hibr-conf-")
DD = tempfile.mkdtemp(prefix="hibr-data-")
for k in ("themes", "colours"):
    os.makedirs(os.path.join(DD, "hibr", k))
    os.makedirs(os.path.join(CD, "hibr", k))
# Fetched: a theme and a colour scheme of names nothing bundles.
open(os.path.join(DD, "hibr", "themes", "fetched.json"), "w").write(
    '{"title":"Fetched","colours":"paper","frame":"double"}')
open(os.path.join(DD, "hibr", "colours", "fetchco.json"), "w").write(GOOD)
# And one of each whose name the person has taken for themselves.
open(os.path.join(DD, "hibr", "themes", "mine.json"), "w").write(
    '{"colours":"paper","frame":"double"}')
open(os.path.join(CD, "hibr", "themes", "mine.json"), "w").write(
    '{"colours":"dracula","frame":"rounded"}')
out = subprocess.run(
    [sx.HIBR, "-c", CPLOAD + 'echo "${CP_THEMES[*]}"\n'
     'echo "${CP_COLOURLIST[*]}"\n'
     'cp_theme mine; echo "$CP_COLOURS $DT_FRAME"'],
    env=dict(os.environ, XDG_CONFIG_HOME=CD, XDG_DATA_HOME=DD),
    capture_output=True, text=True).stdout.split("\n")
check("a theme and a colour scheme installed from the catalogue are listed, "
      "beside the bundled ones",
      "fetched" in out[0].split() and "mine" in out[0].split()
      and "fetchco" in out[1].split() and "classic" in out[0].split(), out)
# This one guards the invariant rather than the change, and says so: with
# no data folder in the list at all it passes against 0.99.186 too, since
# `mine` could only ever have resolved to the person's own copy. It is here
# because the entry that was added is the one that could break it.
check("and a file of the person's own wins over a fetched one of that name, "
      "so an update cannot overwrite their copy",
      out[2] == "dracula rounded", out)
shutil.rmtree(CD, True)
shutil.rmtree(DD, True)

# Before 0.99.33 the colour scheme's name was kept as CP_THEME; a file that
# old has it moved to CP_COLOURS once, and a theme chosen since is kept.
def loadlook(conf):
    d = tempfile.mkdtemp(prefix="hibr-mig-")
    os.makedirs(os.path.join(d, "hibr"))
    open(os.path.join(d, "hibr", "desktop.hibr"), "w").write(conf)
    out = subprocess.run(
        [sx.HIBR, "-c", CPLOAD + 'dt_load\necho "[$CP_THEME] $CP_COLOURS $DT_SETVER"'],
        env=dict(os.environ, XDG_CONFIG_HOME=d),
        capture_output=True, text=True).stdout.strip()
    shutil.rmtree(d, True)
    return out


out = loadlook("DT_SETVER=3\nCP_THEME=slate\n")
check("an old file's CP_THEME, a colour scheme, becomes its colours", out == "[] slate 13", out)
out = loadlook("DT_SETVER=4\nCP_THEME=meadow\nCP_COLOURS=paper\n")
check("and a file written since is read as it is", out == "[meadow] paper 13", out)


# 0.99.77 has the YouTube player follow Control Panel > Pictures, so the one
# setting reaches every picture the desktop draws (ADR 0037). A file still
# holding the old default moves; a picture someone chose for it is kept.
def loadtube(conf):
    d = tempfile.mkdtemp(prefix="hibr-mig-")
    os.makedirs(os.path.join(d, "hibr"))
    open(os.path.join(d, "hibr", "desktop.hibr"), "w").write(conf)
    out = subprocess.run(
        [sx.HIBR, "-c", CPLOAD + ". %s/youtube.hibr\ndt_load\n"
         'echo "$YT_MODE $DT_SETVER"' % appdir("youtube")],
        env=dict(os.environ, XDG_CONFIG_HOME=d),
        capture_output=True, text=True).stdout.strip()
    shutil.rmtree(d, True)
    return out


out = loadtube("DT_SETVER=6\nYT_MODE=half\n")
check("an old file's YouTube picture follows the Pictures setting now",
      out == "follow 13", out)
out = loadtube("DT_SETVER=6\nYT_MODE=ascii\n")
check("and one it was given of its own is kept", out == "ascii 13", out)

# A solid title bar is the frame's colour, the title on it in DT_SELINK,
# with no tee marks around it.
sc = run("clock", "8 24 6 10", pre="DT_TITLEBAR=solid")
pos = sc.find_from("Clock", 1)
check("a solid title bar fills the top row with the accent, the title on it",
      pos is not None and sc.style(pos[0], pos[1])["bg"] == "#63b3ed"
      and sc.style(pos[0], 11)["bg"] == "#63b3ed" and "\u2524 Clock" not in sc.row(pos[0]), sc)

# An app's pane is always listed, and says so when its app is not loaded.
sc = cprun([b"\x1b[B"] * downs("abouthibr"))
check("the About pane says so when About This Computer is not loaded",
      sc.find("About This Computer is not loaded") is not None, sc)
sc = cprun([b"\x1b[B"] * downs("abouthibr"), extra=("about",))
check("and once it is, holds its Refresh",
      brow(sc, "Refresh") != "" and "3000 ms" in brow(sc, "Refresh"), sc)

# The Terminal pane: its own rows only appear once term.hibr is loaded, the
# same as the About pane's Refresh above.
DOWN_TERM = [b"\x1b[B"] * downs("terminal")
sc = cprun(DOWN_TERM, extra=("term",))
check("Scrollbar and Follow Program Title only appear once term itself is "
      "loaded, in the Terminal pane of its own",
      sc.find("Scrollbar") is not None and
      sc.find("Follow Program Title") is not None, sc)

sc = cprun(DOWN_TERM + [b"\x1b[C"], extra=("term",))
check("Scrollbar and Follow Program Title both default on",
      "[x]" in sc.row(sc.find("Scrollbar")[0]) and
      "[x]" in sc.row(sc.find("Follow Program Title")[0]), sc)
check("Colours is a setting too, and terminal windows use the theme's "
      "by default", sc.find("Colours") is not None and
      "theme" in sc.row(sc.find("Colours")[0]), sc)
sc = cprun(reach("terminal", "Colours", ("term",)) + [b"\x1b[C"],
           extra=("term",))
check("and it can be switched to the real terminal's own colours",
      "terminal" in brow(sc, "Colours"), sc)
# About Me: what the login screen shows for you, kept in your own
# me.json. The pane reads the file each time it draws, so what a check
# saved is what the next frame shows.
ABME = ('mkdir -p "$XDG_CONFIG_HOME/hibr"; printf \'{"name": "Old Name", '
        '"line": "Gone fishing"}\' > "$XDG_CONFIG_HOME/hibr/me.json"')
sc = cprun([b"\x1b[B"] * downs("aboutme"), post=ABME)
check("Control Panel > About Me: the name, the line, the picture and the "
      "session the login screen uses, from me.json",
      "Old Name" in brow(sc, "Name") and "Gone fishing" in
      brow(sc, "Line Under It") and "desktop" in brow(sc, "Session at Login")
      and browi(sc, "No Picture") is None, sc)
sc = cprun(reach("aboutme", "Name") + [b"\r", 0.3], post=ABME, end=None)
check("Name opens a dialog to change the name and the line",
      sc.find("┤ About Me ├") is not None and sc.find("Old Name") is not None,
      sc)
sc = cprun(reach("aboutme", "Name") + [b"\r", 0.3] + [b"\x7f"] * 8 +
           [c.encode() for c in "Pat Doe"] + [b"\r", 0.3], post=ABME)
check("and what is typed there is saved, the line kept",
      "Pat Doe" in brow(sc, "Name") and "Gone fishing" in
      brow(sc, "Line Under It") and sc.find("┤ About Me ├") is None, sc)
sc = cprun([b"\x1b[B"] * downs("aboutme"),
           post=ABME + '\nab_picked "" /tmp/pics/me.png Pictures')
check("a picture chosen is kept by its path, and can be taken away again",
      "me.png" in brow(sc, "Picture\u2026") and browi(sc, "No Picture")
      is not None, sc)
sc = cprun([b"\x1b[B"] * downs("aboutme"),
           post='rm -rf "$XDG_CONFIG_HOME/hibr/me.json"')
check("with no me.json, Name is the account's own real name",
      brow(sc, "Name").strip() != "" and "desktop" in
      brow(sc, "Session at Login"), sc)
sc = cprun([b"\x1b[B"] * downs("screensaver"))
check("Control Panel > Screen Saver: which saver, how long idle, a preview",
      "random" in brow(sc, "Screen Saver") and
      "10" in brow(sc, "Start After (minutes)") and brow(sc, "Preview") != "", sc)

# An on/off row as a switch: Appearance > Checkboxes, or what the theme
# suggests. A knob sits right when on and left when off, a block likewise
# between brackets; toggling one moves it.
sc = cprun(DOWN_TERM, extra=("term",), post="DT_CHECKS=knob")
check("with Checkboxes set to knob, on and off are a knob right and left",
      "(  \u25cf)" in brow(sc, "Scrollbar") and "(\u25cf  )" in brow(sc, "Cursor Blink"), sc)
sc = cprun(reach("terminal", "Cursor Blink", ("term",)) + [b"\r"],
           extra=("term",), post="DT_CHECKS=block")
check("and set to block, a block between brackets that moves when toggled",
      "[  \u2588]" in brow(sc, "Scrollbar") and "[  \u2588]" in brow(sc, "Cursor Blink"), sc)
sc = cprun(DOWN_TERM, extra=("term",), post="cp_colours neon")
check("a theme can suggest the style: neon's is the knob",
      "(  \u25cf)" in brow(sc, "Scrollbar"), sc)
sc = cprun(reach("appearance", "Checkboxes") + [b"\x1b[C"])
check("Appearance's Checkboxes follow the theme until told otherwise",
      "box" in brow(sc, "Checkboxes"), sc)

sc = cprun(DOWN_TERM, extra=("term",))
check("the terminal's Cursor and Cursor Blink live here too, block and off",
      "block" in brow(sc, "Cursor") and "[ ]" in brow(sc, "Cursor Blink"), sc)
sc = cprun(reach("terminal", "Cursor", ("term",)) + [b"\x1b[C"],
           extra=("term",))
check("and the cursor cycles to underline",
      "underline" in brow(sc, "Cursor"), sc)

# Appearance's last row: the gap between the items on the bar's right.
sc = cprun(DOWN_APP)
check("Appearance has a Menu Bar Spacing slider, at 2 by default",
      sc.find("Spacing (2)") is not None and
      "●" in sc.row(sc.find("Spacing (2)")[0]), sc)
sc = cprun(reach("appearance", "Spacing") + [b"\x1b[C"])
check("and moving it widens the gap",
      sc.find("Spacing (3)") is not None and
      re.search(r"⚑\S*   \d\d:\d\d   ", sc.row(0)) is not None, sc)
sc = cprun(DOWN_APP)
check("and a Notification Icon choice, the flag by default -- no emoji font "
      "needed", sc.find("Notification Icon") is not None and
      "⚑ flag" in sc.row(sc.find("Notification Icon")[0]), sc)
sc = cprun(reach("appearance", "Notification Icon") + [b"\x1b[C"])
check("which can be the bell instead",
      "bell" in sc.row(sc.find("Notification Icon")[0]) and
      sc.find("🔔") is not None, sc)

# File Types: dt_handler's own table, listed and editable through the UI
# now instead of only through a line in a script of the user's own --
# which is still exactly how it is read back at the next start (dt_save
# writes a dt_handler call for each one), and still exactly how it can be
# set up outside the UI too.
DOWN_FT = [b"\x1b[B"] * downs("filetypes")
FTPRE = "dt_handler xyz feh\n"

sc = cprun(DOWN_FT, pre=FTPRE)
check("File Types lists a registered handler, and offers to add another",
      sc.find("xyz") is not None and sc.find("feh") is not None and
      sc.find("Add File Type") is not None, sc)

sc = cprun(DOWN_FT + [b"\x1b[C", b"\r"], pre=FTPRE)
check("activating a registered extension opens it, pre-filled, for editing",
      sc.find("┤ File Type ├") is not None and
      sc.find("Extension: xyz") is not None and
      sc.find("Command:   feh") is not None, sc)

sc = cprun(DOWN_FT + [b"\x1b[C", b"\r", b"\x1b"], pre=FTPRE)
check("escape cancels the dialog, leaving the entry untouched",
      sc.find("┤ File Type ├") is None and sc.find("xyz") is not None, sc)

sc = cprun(DOWN_FT + [b"\x1b[C", b"\r", b"\x04"], pre=FTPRE)
check("ctrl-d deletes it",
      sc.find("┤ File Type ├") is None and sc.find("xyz") is None, sc)

# Its buttons: Save, Delete -- only for an entry that exists -- and Cancel,
# reached with tab after the two fields, or clicked.
sc = cprun(DOWN_FT + [b"\x1b[C", b"\r"], pre=FTPRE)
check("editing an entry offers Save, Delete and Cancel as buttons",
      re.search(r" Save .* Delete .* Cancel ", sc.text()) is not None, sc)
dl = sc.find(" Delete ")
sc = cprun(DOWN_FT + [b"\x1b[C", b"\r"] +
           ([press(dl[0], dl[1] + 1)] if dl else []), pre=FTPRE)
check("clicking Delete deletes it",
      dl is not None and sc.find("┤ File Type ├") is None and
      sc.find("xyz") is None, sc)
sc = cprun(DOWN_FT + [b"\x1b[C", b"\r"] + [b"\t"] * 4 + [b"\r"],
           pre=FTPRE)
check("tab past the fields, Save and Delete reaches Cancel, and enter on it "
      "closes the dialog with the entry kept",
      sc.find("┤ File Type ├") is None and sc.find("xyz") is not None, sc)
sc = cprun(DOWN_FT + [b"\x1b[C", b"\r"] + [b"\t"] * 3 + [b"\r"],
           pre=FTPRE)
check("and enter on Delete, one before it, deletes",
      sc.find("┤ File Type ├") is None and sc.find("xyz") is None, sc)

sc = cprun(DOWN_FT + [b"\x1b[C", b"\x1b[B", b"\r"], pre=FTPRE)
# cprun's own trailing "qy" (its default end, always sent after this
# feed) lands in the dialog's own Extension field -- still focused,
# nothing here closes it -- so that field is not what this checks;
# the Command field and the checkbox are untouched by it either way.
# "Command:" plus a run of blanks, not the row's own end -- the row also
# holds the Control Panel window's own right border, well past the
# dialog, which .rstrip() (screen.row's own) does not strip since it is
# not whitespace.
check("Add File Type opens a blank dialog",
      sc.find("┤ File Type ├") is not None and
      sc.find("Command:" + " " * 10) is not None and
      sc.find("[ ] Run in a terminal") is not None, sc)

# The command field can hold a space (it is a command line), and
# dt_textkey's own "text cur" return has no other separator to split on
# -- reading the cursor back with the shortest-prefix form (the *first*
# space) mistook the text's own embedded space for that separator,
# scrambling every key typed after it. rn_key and gi_key had the same
# latent bug (Files' own Rename and Get Info fields), fixed alongside
# this one; see the Rename test above for that side of it.
sc = cprun(DOWN_FT + [b"\x1b[C", b"\x1b[B", b"\r", b"p", b"n", b"g", b"\t",
                      b"f", b"e", b"h", b" ", b"-", b"g", b"\r"], pre=FTPRE)
row = sc.find("png")
check("a command with a space in it is not scrambled by what is typed "
      "after it",
      row is not None and "feh -g" in sc.row(row[0]), sc)

# The redraw-skip slider itself: a plain track, min at DT_DRAWSKIP=0 (skip
# none), no popup to open (unlike a dropdown, right/enter on it just cycles
# the value in place, the same as a dropdown's own second right already
# does).
# The row says what its number counts rather than only the number, the
# same as Frames For Output's own `(70ms)` below it: `Redraw Skip (1)`
# can be read as a count of frames or as a mode, and that ambiguity is
# what let its own off-by-one live -- the loop compared against
# `DT_DRAWSKIP - 1`, so the shipped default of 1 skipped none and it was
# the label that was wrong (Gitea #212).
RSKIP = reach("desktop", "Redraw Skip")
sc = cprun(RSKIP)
skiprow = sc.find("Redraw Skip (none)")
check("the redraw-skip row draws as a slider, not a dropdown or checkbox",
      skiprow is not None and "●" in sc.row(skiprow[0]) and
      "▾" not in sc.row(skiprow[0]), sc)
sc = cprun(RSKIP + [b"\x1b[C"])
check("one frame says so in the singular, so the number cannot be read as "
      "a mode", sc.find("Redraw Skip (1 frame)") is not None, sc)
sc = cprun(RSKIP + [b"\x1b[C", b"\x1b[C"])
check("right arrow on it increases the value, the marker moving with it",
      sc.find("Redraw Skip (2 frames)") is not None, sc)
sc = cprun(RSKIP + [b"\x1b[C"] * 20)
check("it stops at the reasonable maximum rather than climbing forever",
      sc.find("Redraw Skip (9 frames)") is not None, sc)

# Task Manager's own pane: Refresh, Scrollbar, and the order a new window
# starts in -- rows that appear only once Task Manager itself is loaded.
DOWN_TM = [b"\x1b[B"] * downs("taskmgr")
sc = cprun(DOWN_TM, extra=("tasks",))
check("Task Manager has a pane of its own: Refresh, Scrollbar, Sort By, "
      "Order and Show", sc.find("Refresh") is not None and
      sc.find("1000 ms") is not None and sc.find("Scrollbar") is not None and
      sc.find("Sort By") is not None and sc.find("Order") is not None and
      sc.find("Show") is not None and
      "cpu" in sc.row(sc.find("Sort By")[0]) and
      "desc" in sc.row(sc.find("Order")[0]) and
      "Everyone" in sc.row(sc.find("Show")[0]), sc)
sc = cprun(DOWN_TM + [b"\x1b[C", b"\x1b[C"], extra=("tasks",))
check("and Refresh cycles through the other intervals",
      "2000 ms" in sc.row(sc.find("Refresh")[0]), sc)
sc = cprun(reach("taskmgr", "Sort By", ("tasks",)) + [b"\x1b[C"],
           extra=("tasks",))
check("and the default sort column can be changed",
      "mem" in brow(sc, "Sort By"), sc)
sc = cprun(DOWN_TM)
check("and without Task Manager loaded, the pane says so",
      sc.find("Task Manager is not loaded") is not None, sc)

# Files' own pane: the view a folder opens in until it has one of its own
# (#50), and forgetting every folder's (#51, a plain button, kind=action).
FVIEW = reach("filesview", "Default View", ("files",))
sc = cprun(FVIEW, extra=("files",))
check("Files' pane has Default View, at list, once Files itself is loaded",
      "list" in brow(sc, "Default View"), sc)
sc = cprun(FVIEW + [b"\x1b[C"], extra=("files",))
check("and cycles through the other views",
      "details" in brow(sc, "Default View"), sc)
check("with Reset All Views below it, as a button",
      brow(sc, "Reset All Views") != "", sc)

# fb_reset_views itself, directly: it clears an already-remembered view
# back to the default, and removes the file it was kept in.
RVSTATE = tempfile.mkdtemp(prefix="hibr-resetviews-state-")
RVSCRIPT = (
    '. %s\n'
    'fb_view_save /some/dir icons\n'
    'before := fb_dirview /some/dir\n'
    'echo "before=$before"\n'
    'fb_reset_views\n'
    'after := fb_dirview /some/dir\n'
    'echo "after=$after"\n'
    '[ -f "$FB_VIEWFILE" ] && echo file=yes || echo file=no\n'
    % (appdir("files") + "/files.hibr")
)
out = subprocess.run([sx.HIBR, "-c", RVSCRIPT], capture_output=True, text=True,
                     env=dict(os.environ, XDG_STATE_HOME=RVSTATE)).stdout
check("it clears a remembered view back to the default",
      "before=icons" in out and "after=list" in out, out)
check("and removes the persisted file", "file=no" in out, out)
shutil.rmtree(RVSTATE, True)

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

# Setting the clock hands the date to a small sh -c script run under sudo,
# and the script reads it as its own $1 -- which 0.44's conversion rewrote to
# $id, inside the single quotes, so the clock was set to nothing. Run the
# real script against a timedatectl that only says what it was asked, with
# sudo taken off the front: nothing on this machine changes.
FAKE = tempfile.mkdtemp(prefix="hibr-fakebin-")
open(os.path.join(FAKE, "timedatectl"), "w").write(
    '#!/bin/sh\necho "timedatectl $*"\n')
os.chmod(os.path.join(FAKE, "timedatectl"), 0o755)
TSET = (
    '. %s/datetime.hibr\n'
    'fn dtp_run(...cmd) { "${cmd[@]:1}"; }\n'
    'dt_del() { :; }; dt_note() { echo "note: $1"; }\n'
    'declare -gA DTS; DTS[7]["dbuf"]=2026-01-02; DTS[7]["tbuf"]=03:04:05\n'
    'dts_apply 7\n' % CP
)
out = subprocess.run([sx.HIBR, "-c", TSET], capture_output=True, text=True,
                     env=dict(os.environ, PATH=FAKE + ":" + os.environ["PATH"])
                     ).stdout
check("Set Date & Time hands the date it was given to the command that sets it",
      "timedatectl set-time 2026-01-02 03:04:05" in out, out)
shutil.rmtree(FAKE, True)

# Windows' own rows start with the chrome: Frame, Buttons, Title, Button Style.
DOWN_WS = [b"\x1b[B"] * downs("windows")
sc = cprun(DOWN_WS)
check("Windows' first row is Frame, defaulting to single",
      sc.find("Frame") is not None and "single" in sc.row(sc.find("Frame")[0]),
      sc)
sc = cprun(DOWN_WS + [b"\x1b[C", b"\x1b[C"])
check("cycling it once reaches double",
      sc.find("Frame") is not None and
      "double" in sc.row(sc.find("Frame")[0]), sc)
sc = cprun(DOWN_WS + [b"\x1b[C", b"\x1b[C", b"\x1b[C"])
check("and again reaches rounded",
      sc.find("Frame") is not None and "rounded" in sc.row(sc.find("Frame")[0]),
      sc)
sc = cprun(DOWN_WS + [b"\x1b[C", b"\x1b[C", b"\x1b[C", b"\x1b[C"])
check("and again reaches none",
      sc.find("Frame") is not None and "none" in sc.row(sc.find("Frame")[0]),
      sc)
sc = cprun(reach("windows", "Button Style") + [b"\x1b[C"])
check("Button Style cycles from brackets to circles",
      sc.find("Button Style") is not None and
      "circles" in sc.row(sc.find("Button Style")[0]), sc)

sc = cprun(reach("mouse", "Edge Resize"))
check("Edge Resize defaults on",
      sc.find("Edge Resize") is not None and
      "[x]" in sc.row(sc.find("Edge Resize")[0]), sc)
sc = cprun(reach("mouse", "Edge Resize") + [b"\r"])
check("and it can be switched off",
      "[ ]" in sc.row(sc.find("Edge Resize")[0]), sc)

sc = cprun(reach("mouse", "Modifier Drag"))
check("Modifier Drag -- #47 -- defaults on",
      sc.find("Modifier Drag") is not None and
      "[x]" in sc.row(sc.find("Modifier Drag")[0]), sc)
sc = cprun(reach("mouse", "Modifier Drag") + [b"\r"])
check("and it can be switched off",
      "[ ]" in sc.row(sc.find("Modifier Drag")[0]), sc)
sc = cprun(reach("mouse", "Drag Modifier"))
check("Drag Modifier defaults to alt",
      sc.find("Drag Modifier") is not None and
      "alt" in sc.row(sc.find("Drag Modifier")[0]), sc)
sc = cprun(reach("mouse", "Drag Modifier") + [b"\x1b[C"])
check("and cycles to the other modifiers",
      "ctrl" in sc.row(sc.find("Drag Modifier")[0]), sc)

sc = cprun([press(prow(ORDER[0]), LISTCOL), press(prow(ORDER[0]), LISTCOL)])
check("clicking the same pane twice in the picker is harmless",
      sc.find(TITLE[ORDER[0]]) is not None, sc)

sc = cprun(DOWN_APP + [press(R0 + 1, VALCOL - 10), press(R0 + 1, VALCOL - 10)],
           geom=PWTALL)
check("a click in the pane's own body selects and a second click acts",
      "construction" in brow(sc, "Theme"), sc)

sc = cprun([press(prow("terminal") + 1, LISTCOL)])
check("clicking below the last pane in the picker does nothing",
      sc.find(TITLE[ORDER[0]]) is not None, sc)
# A pane far enough down the list to have headings above it, but still on
# screen: the picker scrolls (0.94), so a row worked out from ORDER is only
# where it says while the pane is visible, and a pane added to the middle of
# the list pushes the ones below it off the bottom.
sc = cprun([press(prow("language"), LISTCOL)])
check("clicking a pane in the picker shows it, the headings counted",
      brow(sc, "Digits") != "", sc)
sc = cprun([press(R0 - 1, LISTCOL)])
check("and clicking a heading shows nothing new",
      sc.find("Bluetooth System") is not None, sc)

sc2 = run("tasks", "16 50 4 4", feed=[b"\x1b\x14"], extra=("term",))
check("alt-ctrl-t opens a terminal, even with another app focused",
      sc2.find("┤ Terminal ├") is not None, sc2)
sc3 = run("files", FW, feed=[b"\x1b\x10"], pre=PRE, extra=("tasks",))
check("alt-ctrl-p opens the task manager",
      sc3.find("┤ Task Manager ├") is not None, sc3)

# The Versions pane answers the question About This Computer cannot
# (Gitea #42): About draws $HIBR_VERSION, which is the desktop process's
# own, and a restart replaces that process and every module it loads while
# leaving the hold server's image as old as the session. Three states, each
# asked separately here because they move independently.


def vprows(held=""):
    """The Versions pane's rows, with HIBR_HOLD given rather than inherited.
    A helper that runs hibr outside Term inherits the environment of
    whoever started the suite, so a machine inside a held session and one
    outside it would read differently -- the trap this file already carries
    about panefull and the owner's own config."""
    out = subprocess.run(
        [sx.HIBR, "-c", CPLOAD +
         'n := versions_rows 1; i=0; while [ "$i" -lt "$n" ]; do '
         'echo "${CP[1][$i]["kind"]}|${CP[1][$i]["text"]}|${CP[1][$i]["val"]}"; '
         'i=$((i + 1)); done'],
        capture_output=True, text=True,
        env=dict(os.environ, HIBR_HOLD=held)).stdout
    return [tuple(l.split("|", 2)) for l in out.splitlines() if l.count("|") >= 2]


vp = vprows()
check("the Versions pane names each part the desktop is made of",
      [t for k, t, v in vp if k == "head"] ==
      ["This Desktop", "This Session", "Modules"], vp)
check("and gives the running shell's own version with its module ABI",
      any(t == "Shell" and re.match(r"^\d+\.\d+\.\d+ \(abi \d+\)$", v)
          for k, t, v in vp), vp)
check("a session that is not held says so rather than naming one",
      ("info", "Held", "no") in vp, vp)
vph = vprows("/tmp/nowhere/scratch")
check("and a held one is named, its server's image asked on its own",
      ("info", "Name", "scratch") in vph
      and any(t == "Server Image" for k, t, v in vph), vph)
vpm = vp[[t for k, t, v in vp].index("Modules") + 1:]
check("every module loaded is listed with the version mod list gives it",
      len(vpm) >= 1 and all(re.match(r"^\d+\.\d+", v) for k, t, v in vpm), vpm)
# A module replaced on disk is read from this process's own mappings: the
# kernel appends " (deleted)" to a replaced inode, which is what apt and
# make leave behind. vp_scan takes the maps file rather than reading one,
# so this needs no module of the real tree replaced underneath it.
VPMAPS = sx.scratch("versions-maps.txt")
open(VPMAPS, "w").write(
    "7f0000000000-7f0000001000 r-xp 00000000 fc:47 11   /x/mods/term.so (deleted)\n"
    "7f0000002000-7f0000003000 r-xp 00000000 fc:47 12   /x/mods/pty.so\n")
vpg = subprocess.run(
    [sx.HIBR, "-c", CPLOAD + 'g := vp_scan %s; printf "[%%s]\\n" "$g"' % VPMAPS],
    capture_output=True, text=True).stdout.strip()
check("and a mapping the file no longer backs is what marks it replaced",
      "term.so" in vpg and "pty.so" not in vpg, vpg)

# --- the desk accessories --------------------------------------------------
#
# Ordinary apps, kept in programs/desktop/apps/Accessories rather than at the top of apps
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
''' % (WM, os.path.join(APPS, "Accessories", "puzzle.hibr")))
out = subprocess.run([sx.HIBR, WINSCRIPT], capture_output=True,
                     text=True).stdout.strip()
check("sliding the last tile into place is recognised as solved",
      out == "won", out)
os.unlink(WINSCRIPT)

# Stickies. end=None throughout: a note takes every printable character as
# text, q included, so the usual qy quit sequence would type itself into
# the note rather than closing the window -- the same reason a terminal
# test never uses the default end either.
NPD = stdir()
# Control Panel > Stickies > Notes on Every Workspace: a note is on every
# workspace, so it is still there after switching; off, it stays behind.
CB = stdir()
open(os.path.join(CB, "1.txt"), "w").write("pinned note")
open(os.path.join(CB, "notes.json"), "w").write('{"1":{"color":"yellow"}}')
sc = run("stickies", "8 30 4 10", feed=[b"\x1b2"], env=stenv(CB), pre="DT_STICKYALL=1")
check("with Notes on Every Workspace a note follows to another workspace",
      sc.find("pinned note") is not None, sc)
sc = run("stickies", "8 30 4 10", feed=[b"\x1b2"], env=stenv(CB))
check("without it the note stays on its own", sc.find("pinned note") is None, sc)
shutil.rmtree(CB, True)

sc = run("stickies", "12 40 2 2", feed=[b"h", b"i"], env=stenv(NPD), end=None)
check("typing appears in the note", sc.find("hi") is not None, sc)
check("and is saved to disk as it is typed", stnote(NPD) == "hi", stnote(NPD))
check("its first line is its title", sc.find("✕ hi") is not None, sc)
shutil.rmtree(NPD, True)

NPD2 = stdir()
sc = run("stickies", "12 40 2 2", feed=[b"h", b"i", b"\x08"],
         env=stenv(NPD2), end=None)
check("backspace removes the last character typed",
      stnote(NPD2) == "h", stnote(NPD2))
shutil.rmtree(NPD2, True)

NPD3 = stdir()
sc = run("stickies", "12 40 2 2", feed=[b"a", b"\r", b"b"], env=stenv(NPD3),
         end=None)
check("enter starts a new line, and both are saved",
      stnote(NPD3) == "a\nb", stnote(NPD3))
shutil.rmtree(NPD3, True)

NPD4 = stdir()
run("stickies", "12 40 2 2", feed=[b"h", b"e", b"l", b"l", b"o"],
    env=stenv(NPD4), end=None)
sc = run("stickies", "12 40 2 2", env=stenv(NPD4), end=None)
check("reopening it loads the saved note", sc.find("hello") is not None, sc)
shutil.rmtree(NPD4, True)

NPD4 = stdir()
sc = run("stickies", "8 20 2 2", feed=[c.encode() for c in
                                       "one two three four five six"],
         env=stenv(NPD4), end=None)
check("a long line wraps at the note's width, at a space",
      "one two three " in sc.row(3) and "four five six" in sc.row(4)
      and "four" not in sc.row(3), sc)
shutil.rmtree(NPD4, True)
NPD4 = stdir()
sc = run("stickies", "8 20 2 2", feed=[b"a", b"b", b" ", b"\r", b"c",
                                       b"\x1bz", 0.3],
         env=stenv(NPD4), end=None)
check("undo takes back the last change: typing is one, a new line another",
      stnote(NPD4) == "ab \n", repr(stnote(NPD4)))
shutil.rmtree(NPD4, True)
NPD4 = stdir()
sc = run("stickies", "8 20 2 2", feed=[b"x", b"\x1bz", b"\x1by", 0.3],
         env=stenv(NPD4), end=None)
check("and redo puts it back", stnote(NPD4) == "x", stnote(NPD4))
shutil.rmtree(NPD4, True)

NPD4 = stdir()
sc = run("stickies", "10 30 2 4", feed=[c.encode() for c in "Buy milk"],
         env=stenv(NPD4), end=None)
check("a sticky is bare: no frame, a strip with a close box and its title",
      sc.find("┤ Buy milk ├") is None and sc.find("✕ Buy milk") == (2, 5),
      sc)
sc = run("stickies", "10 30 2 4", feed=[press(2, 15), drag(6, 30),
                                        release(6, 30), 0.3],
         env=stenv(NPD4), end=None)
check("dragging the strip moves it", sc.find("✕ Buy milk") == (6, 20), sc)
sc = run("stickies", "10 30 2 4", feed=[press(2, 5), release(2, 5), 0.3],
         env=stenv(NPD4), end=None)
check("its close box puts it away, the note kept",
      sc.find("Buy milk") is None and stnote(NPD4) == "Buy milk", sc)
shutil.rmtree(NPD4, True)

NPD4 = stdir()
open(os.path.join(NPD4, "1.txt"), "w").write("first")
open(os.path.join(NPD4, "2.txt"), "w").write("second")
open(os.path.join(NPD4, "notes.json"), "w").write(
    '{"1":{"color":"blue","h":8,"w":24},"2":{"color":"pink","h":8,"w":24}}')
sc = run("calc", CW, [], pre="dt_launch stickies", extra=("stickies",),
         env=stenv(NPD4), end=None)
check("launching Stickies opens every note",
      sc.find("✕ first") is not None and sc.find("✕ second") is not None,
      sc)
shutil.rmtree(NPD4, True)

NPD4 = stdir()
OLDN = tempfile.mkdtemp(prefix="hibr-oldnote-")
os.makedirs(os.path.join(OLDN, "hibr"))
open(os.path.join(OLDN, "hibr", "notepad.txt"), "w").write("from note pad\n")
sc = run("calc", CW, [], pre="dt_launch stickies", extra=("stickies",),
         env=dict(stenv(NPD4), XDG_CONFIG_HOME=OLDN), end=None)
check("the first time, Note Pad's note becomes the first sticky",
      stnote(NPD4) == "from note pad" and
      sc.find("✕ from note pad") is not None, sc)
shutil.rmtree(NPD4, True)
shutil.rmtree(OLDN, True)

# imgview: draws a decoded picture, or says why not -- #48 found this
# folding two very different failures ("the img module was never
# installed" and "this file could not be decoded") into one identical
# message, with no way to tell which from the screen alone.
IVW = "13 46 2 2"
BUILT_MODS = tree("build/mods")


def run_img(path, moddir, pre=""):
    """Like run(), but opens imgview already showing path -- dt_new's own
    7th argument, which run() has no way to pass through."""
    p = os.path.join(S, "session.hibr")
    open(p, "w").write(
        "%s\n. %s\n. %s\n%s\ndt_open\n"
        'dt_new "Image Viewer" %s imgview "%s"\n'
        "dt_run\ndt_close\n" % (load("console"), WM, appdir("imgview") +
                                "/imgview.hibr", pre, IVW, path)
    )
    t = Term(p, env={"DT_TICK": "60", "HIBR_MODPATH": moddir}, settle=0.8)
    t.quit(b"qy", 1.0)
    return t.screen()


ANSD = tempfile.mkdtemp(prefix="hibr-ans-")
ANSF = os.path.join(ANSD, "shot.ans")
open(ANSF, "w").write("\x1b[0;1;38;2;255;0;0;49mRED\x1b[0;39;49m and"
                      " more\x1b[0m\n\x1b[0;39;49mplain line two\x1b[0m\n")
sc = run_img(ANSF, BUILT_MODS)
check("a screenshot (.ans) opens in the viewer as the cells it was",
      sc.find("RED and more") is not None and
      sc.find("plain line two") is not None and
      sc.find("┤ shot.ans ├") is not None, sc)
shutil.rmtree(ANSD, True)
sc = run_img(os.path.abspath("tests/img-2x2.png"), BUILT_MODS)
check("a decodable picture is drawn, not an error message",
      sc.find("Cannot show") is None and
      sc.find("isn't installed") is None, sc)
sc = run_img(os.path.abspath("tests/img-quad.jpg"), BUILT_MODS)
cols = {sc.style(r, c)["bg"] for r in range(3, 12) for c in range(4, 40, 3)} - {None}
check("and a JPEG the same, in its own colours",
      sc.find("Cannot show") is None and len(cols) >= 4, (cols, sc.dump()))

# HIBR_MODPATH alone cannot force "the module is missing": hibr_require
# falls back to the compiled-in HIBR_MODDIR (the real install) after an
# empty HIBR_MODPATH, so whether this looks missing would otherwise
# depend on what happens to be installed on the machine running the
# tests. dt_has is redefined instead, the same "define it twice" the
# codebase already treats as a deliberate override elsewhere in a test's
# own pre script -- everything but "does img exist" still asks the real
# command -v.
FORCE_NO_IMG = 'dt_has() { [ "$1" = img ] && return 1; command -v "$1" > /dev/null; }'
sc = run_img(os.path.abspath("tests/img-2x2.png"), BUILT_MODS,
             pre=FORCE_NO_IMG)
check("a missing img module says so, not \"cannot show\" the file",
      sc.find("Image support isn't installed") is not None, sc)

expect(r"broken\.png: neither a PNG, a JPEG nor a PPM$")
BADPNG = tempfile.mkdtemp(prefix="hibr-bad-png-")
open(os.path.join(BADPNG, "broken.png"), "wb").write(b"not a real png")
sc = run_img(os.path.join(BADPNG, "broken.png"), BUILT_MODS)
check("a file that fails to decode says so, by name",
      sc.find("Cannot show broken.png") is not None, sc)
shutil.rmtree(BADPNG, True)

# --- the terminal window --------------------------------------------------

TW = "14 44 2 2"
TERM = ("term", TW)
SH = "TW_CMD=(/bin/sh -c 'PS1=\"sh> \"; export PS1; exec /bin/sh')"

sc = run(*TERM, pre=SH,
         wait=1.6)
check("a shell starts in the window", sc.find("sh>") is not None, sc)

sc = run(*TERM, feed=[b"e", b"c", b"h", b"o", b" ", b"h", b"i", b"\r"],
         pre=SH,
         wait=1.6, until=lambda sc: "│hi " in sc.row(4))
check("what is typed reaches it and what it says comes back",
      sc.find("echo hi") is not None and "│hi " in sc.row(4), sc)

# And a paste from the *host* terminal reaches it too (Gitea #250). That
# arrives as one `paste <text>` key, which wm/input.hibr hands to the
# window's own `_paste` with **two** arguments, where dt_edit's Copy/Cut/
# Paste path passes three -- and `term_paste` was the one `_paste` of
# twenty-three declared with a bare `op` rather than `op = ""`, so it
# refused the short call and its body never ran: pasting into a terminal
# window did nothing at all, and said so only in the desktop's own log.
# Three apps were already driven this way here -- Stickies, Files, the
# calculator -- and the terminal, which is what anybody actually pastes
# into, was not.
sc = run(*TERM, feed=[b"\x1b[200~echo pasted\x1b[201~", b"\r"], pre=SH,
         wait=1.6, until=lambda sc: "│pasted " in sc.row(4))
check("and a paste from the host terminal reaches it as well as typing does",
      sc.find("echo pasted") is not None and "│pasted " in sc.row(4), sc)

# A terminal window's own shell says which window it is, in ps: four shells
# in four windows all read a bare `hibr` before 0.99.110, so there was no
# way to tell which process was which window, or to end one by hand without
# guessing (Gitea #111). The desktop gives the child HIBR_PROCTITLE and hibr
# wears it at startup and removes it, so only a hibr child is renamed and a
# window running something else is left alone. TW_CMD names *this* build,
# since the title is read by the shell being started and the installed one
# may be older.
# A job the desktop forks reports its own two names into a file, because
# that is the one way to read them without racing `ps` against a child
# that may exec in place: the subshell writes what the kernel and the argv
# region actually say about itself.
PSJOB = os.path.join(S, "jobnames")
pspath = os.path.join(S, "session.hibr")
open(pspath, "w").write(
    "%s. %s\n. %s\nTW_CMD=(%s)\ndt_open\n"
    "dt_new \"Terminal\" 12 40 2 2 term\ndt_new \"Terminal\" 12 40 2 44 term\n"
    "(\n"
    "\tdt_jobname sync dav.example.org\n"
    "\tpsc=\n\tread -r psc < /proc/self/comm\n"
    "\tpsl=\n\tread -r -d '' psl < /proc/self/cmdline 2> /dev/null\n"
    "\tprintf '%%s\\n%%s\\n' \"$psc\" \"$psl\" > %s\n"
    "\tsleep 3\n"
    ") &\n"
    "dt_run\ndt_close\n"
    % (load("console", "pty", "term"), WM, appdir("term") + "/term.hibr",
       sx.HIBR, PSJOB))
pst = Term(pspath, env={"DT_TICK": "60"}, settle=0.8)
pst.collect(1.0)
psout = subprocess.run(["ps", "-eo", "ppid=,pid=,args="], capture_output=True,
                       text=True).stdout
# The comprehension's own variable is named for this check, not `l`: the
# slicer reads a statement's free names to decide which earlier statements
# a part needs, and a one-letter name that some other section also assigns
# drags that section's statement -- and whatever *it* depends on -- into
# this part, where it fails on a name that was never defined there.
psmine = sorted(psrow.split(None, 2)[2] for psrow in psout.splitlines()
                if len(psrow.split(None, 2)) > 2 and
                psrow.split(None, 2)[0] == str(pst.pid))
pst.quit(b"q", 1.0)
os.unlink(pspath)
# The long name leads with what the process *is* and then says which
# window of which session, where it used to lead with the role of whatever
# started it: `desktop [Terminal 1]` named the desktop (Gitea #213). This
# fixture is not held, so there is no session to name and the form leaves
# it out rather than standing a placeholder in for one.
# Only the terminals: this session also forks the job below, which is a
# child of the desktop too and is checked on its own terms right after.
check("each terminal window's own shell is named for its window in ps, "
      "where every one of them used to read a bare hibr",
      [x for x in psmine if x.startswith("term [")] ==
      ["term [1:Terminal]", "term [2:Terminal]"], psmine)
# And a job says what it is. It wore the desktop's own name before, so a
# dav sync alive for hours was a second process called `desktop` with
# nothing to tell them apart (Gitea #213). The short one leads with what
# the job does and is cut to the kernel's fifteen bytes; the long one is
# the sentence.
psjob = open(PSJOB).read().splitlines() if os.path.exists(PSJOB) else []
# Taken away again: the teardown ends with os.rmdir(S), which refuses a
# directory holding anything, and this file is the only thing besides
# session.hibr ever written there. Left behind, apps.py run **whole**
# died in its own teardown with "Directory not empty" after every check
# had passed -- so it never printed report(), and the four parts hid it
# because no one of them holds both this section and the teardown.
if os.path.exists(PSJOB):
    os.unlink(PSJOB)
check("a job the desktop forks is named for the work it is doing",
      psjob[:1] == ["sync:dav.exampl"] and
      psjob[1:2] == ["job [sync:dav.example.org]"], psjob)

# Closing a terminal that still has something running in it asks first
# (Gitea #209): closing one kills whatever holds the pty, which may be an
# editor with an hour in it. The question is never "is there a process" --
# the shell itself always is one -- it is whether the pty's foreground
# process group is still the shell's own, which an idle prompt leaves it
# and a running command does not.
ASKSH = "TW_CMD=(/bin/sh -c 'PS1=\"sh> \"; export PS1; exec /bin/sh')"
sc = run("term", "10 40 2 2", feed=[b"s", b"l", b"e", b"e", b"p", b" ",
         b"3", b"0", b"\r", 0.6, b"\x1bw"], pre=ASKSH, wait=1.2, end=None,
         until=lambda sc: sc.find("Close it?") is not None)
# The box is drawn over the screen at an absolute position, the window's
# own title bar included, so "the window is still there" cannot be read
# from its border here -- the next check reads it from the window being
# gone once Yes is pressed, which is the same claim from the other side.
check("a terminal with a command still running asks before it closes",
      sc.find("Close it?") is not None and sc.find("Yes") is not None, sc)
sc = run("term", "10 40 2 2", feed=[b"s", b"l", b"e", b"e", b"p", b" ",
         b"3", b"0", b"\r", 0.6, b"\x1bw", 0.4, b"y"], pre=ASKSH, wait=1.2,
         end=None, until=lambda sc: sc.find("┤ Terminal") is None)
check("and Yes is what closes it", sc.find("┤ Terminal") is None, sc)
# Nothing running: the prompt is the shell's own foreground group, so the
# window goes without a word. The wait is for the prompt, since a shell
# that has not drawn one yet has not reached its own foreground either.
sc = run("term", "10 40 2 2", feed=[0.6, b"\x1bw"], pre=ASKSH, wait=1.2,
         end=None, until=lambda sc: sc.find("┤ Terminal") is None)
check("a terminal sitting at its prompt closes without asking",
      sc.find("┤ Terminal") is None and sc.find("Close it?") is None, sc)
# And the setting turns it off, which is the half that was asked for.
sc = run("term", "10 40 2 2", feed=[b"s", b"l", b"e", b"e", b"p", b" ",
         b"3", b"0", b"\r", 0.6, b"\x1bw"], pre=ASKSH + "\nDT_TERMASK=0",
         wait=1.2, end=None, until=lambda sc: sc.find("┤ Terminal") is None)
check("Ask Before Closing off closes it with something running",
      sc.find("┤ Terminal") is None and sc.find("Close it?") is None, sc)

# Two terminals are two sessions: each its own pty and its own shell, and a
# key typed into one never reaches the other.
TWO = "TW_CMD=(/bin/sh -c 'tty; PS1=\"sh> \"; export PS1; exec /bin/sh')"
sc = run("term", "10 36 2 2", feed=[b"e", b"c", b"h", b"o", b" ", b"o",
         b"n", b"e", b"\r"], pre=TWO, wait=1.6,
         also=[("Term", "10 36 2 40", "term")],
         until=lambda sc: sc.row(5).count("one") == 1)
ttys = [r for r in range(24) if "/dev/pts/" in sc.row(r)]
check("two terminals are two sessions, each on its own pty",
      len(ttys) == 1 and sc.row(ttys[0]).count("/dev/pts/") == 2 and
      len(set(x.split()[0] for x in
              sc.row(ttys[0]).split("/dev/pts/")[1:])) == 2, sc)
check("and what is typed in one does not reach the other",
      sc.row(4).count("echo one") == 1 and sc.row(5).count("one") == 1 and
      "│sh>  " in sc.row(4), sc)

# With "Desktop Shortcuts Win" (DT_TERMKEEP) on, a terminal gives back the
# desktop's own chord shortcuts: alt-tab, bound to Cycle Windows, raises the
# next window rather than reaching the program. Off, or with the window's own
# "Pass Every Key", the program gets it. The program prints the bytes it is
# sent, so "033" on its screen means the chord reached it.
OD = ("TW_CMD=(/bin/sh -c 'stty raw -echo; dd bs=2 count=1 2>/dev/null | "
      "od -c | head -1; sleep 5')")
CYC = '\nDT_KEYS[cycle]="alt-tab"'
CLOCKW = [("Clock", "8 30 5 12", "clock")]
sc = run("term", "10 36 2 2", feed=[b"\x1b\t", 0.5], wait=1.6, end=None,
         pre=OD + "\nDT_TERMKEEP=1" + CYC, also=CLOCKW)
check("with Desktop Shortcuts Win, alt-tab cycles windows from a terminal",
      "033" not in sc.text() and sc.find("┤ Clock ├") is not None, sc)
sc = run("term", "10 36 2 2", feed=[b"\x1b\t", 0.5], wait=1.6, end=None,
         pre=OD + "\nDT_TERMKEEP=0" + CYC, also=CLOCKW)
check("without it, the same chord reaches the program",
      "033" in sc.text() and sc.find("┤ Clock ├") is None, sc)
sc = run("term", "10 36 2 2", feed=[b"\x1b\t", 0.5], wait=1.6, end=None,
         pre=OD + "\nDT_TERMKEEP=1" + CYC + '\nTW[1]["passall"]=1',
         also=CLOCKW)
check("and a window told to pass every key passes it too",
      "033" in sc.text(), sc)
# Close Window is on alt-w since 0.99.90, so this binds it to a ctrl+letter of
# its own to go on testing the thing that matters -- whether a terminal yields a
# chord the desktop holds. ctrl-p rather than ctrl-w for two reasons: settings
# version 9 moves a Close still on ctrl-w, and `od -c` prints 0x10 as 020 where
# it prints 0x0b as an escape.
CP = '\nDT_KEYS["close"]=ctrl-p'
sc = run("term", "10 36 2 2", feed=[b"\x10\x10", 0.5], wait=1.6, end=None,
         pre=OD + "\nDT_TERMKEEP=1" + CP)
check("a ctrl+letter that closes a window reaches a terminal's program instead",
      "020" in sc.text() and sc.find("┤ Term") is not None, sc)
sc = run("term", "10 36 2 2", feed=[b"\x10", 0.5], wait=1.6, end=None,
         pre=OD + "\nDT_TERMKEEP=1\nDT_TERMCTRL=0" + CP)
check("unless Terminals Keep Ctrl+A-Z is off: then it closes the window too",
      sc.find("┤ Term") is None, sc)
# And the default, which is the point of putting it on alt: a terminal yields
# every alt chord whatever DT_TERMCTRL says, so alt-w closes a terminal window
# where ctrl-w never could.
sc = run("term", "10 36 2 2", feed=[b"\x1bw", 0.5], wait=1.6, end=None,
         pre=OD + "\nDT_TERMKEEP=1")
check("and alt-w closes a terminal window, which ctrl-w could not",
      sc.find("┤ Term") is None, sc)
sc = run("term", "10 36 2 2", wait=1.6, end=None,
         feed=[b"\x1b[21~", 0.3, b"\x1b[C", b"\x1b[C", b"\x1b[C", b"\x1b[C",
               0.3],
         pre=SH + "\nDT_TERMKEEP=1")
check("the Window menu offers Pass Every Key on a terminal",
      sc.find("Pass Every Key") is not None, sc)

# Shift bypasses a program that has taken the mouse, so shift-right-click
# still opens the terminal's own menu.
GRAB = ("TW_CMD=(/bin/sh -c 'printf \"\\033[?1000h\\033[?1006h\"; "
        "sleep 9')")
sc = run("term", "10 36 2 2", feed=[press(6, 12, 2 + 4), 0.3], wait=1.6,
         end=None, pre=GRAB)
check("shift-right-click opens the terminal's menu over a program's mouse",
      sc.find("Send Interrupt") is not None, sc)

# DT_CURSOR is what a new terminal starts with, and only the focused one
# draws a cursor at all -- end=None, since the default quit key would land
# on the prompt and move it before the screen is read.
sc = run(*TERM, pre=SH + "\nDT_CURSOR=bar", wait=1.6,
         also=[("Term", "14 44 2 50", "term")], end=None)
check("DT_CURSOR sets a new terminal's cursor, drawn only where focused",
      sc.row(3).count("▏") == 1 and
      sum(sc.row(r).count("▏") for r in range(24)) == 1, sc)

# And changing it reaches a terminal that is *already open*. A terminal
# was told its cursor style when it opened and when a restart adopted it,
# nowhere else, so Control Panel > Terminal > Cursor changed only the
# windows opened after it -- Restart Desktop was the only way to see it on
# the rest (Gitea #261). The setter itself is driven, the pane's own
# cp_set_cursor, by a ticker that fires once two seconds in, after the
# terminal is up and drawing its block cursor.
CURSET = (SH + "\n. %s\n. %s\n"
          "CZ_AT=$((EPOCHSECONDS + 2))\n"
          "cz_tick() { [ -n \"$CZ_AT\" ] || return 0\n"
          "  if [ \"$EPOCHSECONDS\" -lt \"$CZ_AT\" ]; then dt_want 200; return 0; fi\n"
          "  CZ_AT=; cp_set_cursor \"\" bar; }\n"
          "dt_ticker cz_tick\n"
          % (tree("programs/desktop/system/panel.hibr"),
             tree("programs/desktop/control-panel/terminal.hibr")))
sc = run(*TERM, pre=CURSET, wait=1.6, feed=[3.5], end=None)
check("changing Cursor reaches a terminal that is already open",
      sum(sc.row(r).count("▏") for r in range(24)) == 1, sc)

sc = run(*TERM, feed=[b"\x1b[21~"],
         pre=SH,
         wait=1.2)
check("f10 still reaches the menu bar, not the program",
      sc.find("Screen Saver") is not None, sc)

sc = run(*TERM, pre="TW_CMD=(/bin/sh -c 'exit 4')", wait=1.6)
check("a program that ends says so in the window",
      sc.find("exited 4") is not None, sc)

sc = run(*TERM, pre="TW_CMD=(/bin/sh -c 'exit 0')", wait=1.6)
check("and one that ends cleanly closes the window instead of asking",
      sc.find("┤ Term ├") is None and sc.find("exited 0") is None and
      "✎" in sc.row(0), sc)

# Control Panel > Terminal > Close When a Program Ends: always closes a
# failed program's window too; never keeps even a successful one open.
sc = run(*TERM, pre="TW_CMD=(/bin/sh -c 'exit 4')\nDT_TERMEND=always", wait=1.6)
check("set to always, a program that failed closes its window too",
      sc.find("┤ Term ├") is None and sc.find("exited") is None, sc)
sc = run(*TERM, pre="TW_CMD=(/bin/sh -c 'exit 0')\nDT_TERMEND=never", wait=1.6,
         end=None)
check("set to never, even one that succeeded stays to say so",
      sc.find("exited 0") is not None, sc)

# A shell's window closes when the shell ends, whatever its last command
# returned: ctrl-d after a command that failed used to leave "exited 127"
# on a window closed on purpose, since a shell exits with its last status.
sc = run(*TERM, feed=[1.2] + [c.encode() for c in "nosuchcommand"] + [b"\r", 0.6, b"\x04", 1.5],
         pre="TW_CMD=(/bin/sh)", end=None)
check("ctrl-d closes a shell's window even after a command that failed",
      sc.find("┤ Term ├") is None and sc.find("exited") is None, sc)

# Scrollback, and the mouse for a program that asks for it.
LONG = "TW_CMD=(/bin/sh -c 'i=1; while [ $i -le 40 ]; do echo \"row $i\"; i=$((i+1)); done; exec cat')"
sc = run(*TERM, feed=[wheel(8, 10)], pre=LONG, wait=1.2, end=None)
check("the wheel scrolls back through what went off the top",
      sc.find("↑ 3 of 29") is not None and sc.find("row 27") == (3, 3) and
      sc.find("row 40") is None, sc)

sc = run(*TERM, feed=[b"\x1b[5;2~"], pre=LONG, wait=1.2, end=None)
check("shift-pageup pages back a screenful less one",
      sc.find("↑ 11 of 29") is not None and sc.find("row 19") == (3, 3), sc)
sc = run(*TERM, feed=[b"\x1b[17~"], pre=LONG + '\nDT_KEYS["scrollup"]="f6"',
         wait=1.2, end=None)
check("Terminal Page Back is a setting: moved to f6, f6 pages back",
      sc.find("↑ 11 of 29") is not None, sc)

sc = run(*TERM, feed=[wheel(8, 10), b"x"], pre=LONG, wait=1.2, end=None)
check("and a key goes back to the live screen",
      sc.find("↑") is None and sc.find("row 40") is not None, sc)

# DT_TERMBAR: on by default since 0.73 -- a real column of the pty, given
# back to the program when it is switched off.
sc = run(*TERM, pre=LONG, wait=1.2, end=None, env={"DT_TERMBAR": "0"})
check("DT_TERMBAR off -- no scrollbar column, full width",
      sc.at(3, 44) != "│" and sc.at(3, 45) == "│", sc)

sc = run(*TERM, feed=[wheel(8, 10)], pre=LONG, wait=1.2, end=None)
check("on, a scrollbar tracks the view -- one column short of the border, "
      "the one it bought back from the program",
      sc.at(3, 44) == "│" and sc.at(3, 45) == "│" and
      sc.at(12, 44) == "█", sc)
check("from the first row of the terminal to its last, not into the title bar",
      sc.at(2, 44) != "│" and sc.at(14, 44) == "│" and
      sc.at(15, 44) != "│", sc)

sc = run(*TERM, feed=[wheel(8, 10)], wait=1.2, end=None,
         pre=LONG + '\nDT_ACTIVE="#2b6cb0"\nDT_IDLE="#8a8578"')
check("the scrollbar is drawn in the theme's colours: accent thumb, idle track",
      sc.at(12, 44) == "█" and sc.style(12, 44)["fg"] == "#2b6cb0" and
      sc.style(4, 44)["fg"] == "#8a8578", sc)


sc = run(*TERM, wait=1.2, end=None)
check("but nothing draws there at all with no scrollback yet to show",
      sc.at(3, 44) != "│" and sc.at(3, 45) == "│", sc)

# DT_TERMTITLE: on by default, a window's title follows the OSC 0 title a
# program inside it reports -- a shell's own PS1 is the "settable based on
# variables" way to choose one (\[\e]0;...\a\], see docs/interactive.md).
OSCTITLE = ("TW_CMD=(/bin/sh -c 'printf \"\\033]0;My Shell\\007\"; "
            "PS1=\"sh> \"; export PS1; exec /bin/sh')")
sc = run(*TERM, pre=OSCTITLE, wait=1.2, end=None)
check("a program's own OSC title becomes the window's title",
      sc.find("My Shell") is not None and sc.find("┤ Term ├") is None, sc)

sc = run(*TERM, pre=OSCTITLE, wait=1.2, end=None,
         env={"DT_TERMTITLE": "0"})
check("and switching that off keeps the window's own title instead",
      sc.find("My Shell") is None and sc.find("┤ Term ├") is not None, sc)

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


def underlined(out):
    """Whether any SGR the desktop sent turned underline on: a parameter 4
    on its own, not one inside 38;5;n or 38;2;r;g;b. Read from every SGR,
    not only the one right before the text -- the program's output can
    arrive in two reads, and then the text is drawn without one."""
    for m in re.findall(rb"\x1b\[([0-9;]*)m", out):
        ps = m.split(b";")
        i = 0
        while i < len(ps):
            if ps[i] in (b"38", b"48"):
                i += 3 if i + 1 < len(ps) and ps[i + 1] == b"5" else 5
                continue
            if ps[i] == b"4":
                return True
            i += 1
    return False


check("SGR 4:0 turns underline off, not on -- the colon is not a digit",
      sc.find("WORD") is not None and not underlined(sc.out), sc)

# Copy and paste: a drag selects, alt-c copies -- to the desktop and, with
# OSC 52, to the clipboard of the terminal the desktop runs on -- and alt-v
# pastes.  "row 30" is the top line once forty rows have gone past.
sc = run(*TERM, feed=[press(3, 3), drag(3, 8), release(3, 8), b"\x1bc",
                      b"\x1bv"], pre=LONG, wait=1.0, end=None)
check("a drag in a terminal selects, and alt-c copies it everywhere",
      b"\x1b]52;c;cm93IDMw\x07" in sc.out, sc)
check("and alt-v pastes it back into the program",
      "row 30" in sc.row(14), sc)

# Copy on Select (Control Panel > Terminal): the release copies, the text
# stays lit, and the next key only puts it out -- the program never sees
# it. Off, a release copies nothing.
sc = run(*TERM, feed=[press(3, 3), drag(3, 8), release(3, 8), 0.3], pre=LONG, wait=1.0, end=None)
check("with Copy on Select off, a release copies nothing", b"\x1b]52;c;" not in sc.out, sc)
sc = run(*TERM, feed=[press(3, 3), drag(3, 8), release(3, 8), 0.3, b"Q", 0.3, b"ZW", 0.3],
         pre=LONG + "\nDT_TERMSELCOPY=1", wait=1.0, end=None)
check("with it on, releasing a selection copies it, no key needed",
      b"\x1b]52;c;cm93IDMw\x07" in sc.out, sc)
check("and the next key only clears the highlight: Q never reaches the program, ZW does",
      sc.find("ZW") is not None and sc.find("QZW") is None, sc)

CLICK = ("TW_CMD=(/bin/sh -c 'stty raw -echo; "
         "printf \"\\033[?1000h\\033[?1006h\"; head -c 18 | cat -v; sleep 5')")
sc = run(*TERM, feed=[press(6, 10), release(6, 10)], pre=CLICK, wait=1.2, end=None)
check("a click reaches a program that asked for the mouse, where it landed",
      sc.find("^[[<0;8;4M^[[<0;8;4m") is not None, sc)
# A program that asked for pixels (1016) is told the pointer's own pixel in
# its grid when the desktop has one (Gitea #370): screen pixel 83,101 is
# 59,53 inside a grid whose first cell is the screen's row 3, column 3.
PCLICK = ("TW_CMD=(/bin/sh -c 'stty raw -echo; "
          "printf \"\\033[?1000h\\033[?1006h\\033[?1016h\"; "
          "head -c 22 | cat -v; sleep 5')")
sc = run(*TERM, feed=[1.0, b"\x1b[<0;84;102M", b"\x1b[<0;84;102m"],
         pre=PCLICK, wait=1.2, end=None, cell=(8, 16), caps=["mpix"])
check("and a program that asked for pixels is told the pointer's own pixel",
      sc.find("^[[<0;60;54M^[[<0;60;54m") is not None, sc)
# The shells a terminal window starts are told the desktop's glyph set, which
# the prompt module's PROMPT[glyphs]=auto follows (Gitea #360).
GLYPHS = "TW_CMD=(/bin/sh -c 'echo \"glyphs=$HIBR_GLYPHS\"; sleep 5')\nDT_GLYPHSET=ascii"
sc = run(*TERM, pre=GLYPHS, wait=1.0, end=None, until="glyphs=")
check("a terminal window's program is told the desktop's glyph set",
      sc.find("glyphs=ascii") is not None, sc)

# --- glyphs ------------------------------------------------------------------
#
# Every glyph the desktop draws is named in wm/glyphs.hibr; the plain-ASCII
# set draws the same desktop for a terminal or a font without them.
sc = run("clock", "8 24 6 10", pre="DT_GLYPHSET=ascii")
check("in the ASCII glyph set a window is drawn in plain ASCII",
      sc.at(6, 10) == "+" and sc.at(7, 10) == "|" and sc.find("[ Clock ]") is not None,
      sc)
out = subprocess.run([sx.HIBR, "-c", ". %s\ndt_glyphs nerd\nprintf \'%%s|%%s|%%s\\n\' \"${GL[home]}\" \"${GL[hline]}\" \"${GL[folder]}\"\n"
                      "dt_glyphs unicode\nprintf \'%%s\\n\' \"${GL[home]}\"" % WM],
                     capture_output=True, text=True).stdout.split("\n")
check("the nerd set takes a Nerd Font's icons for what it has one for, and unicode's for the rest",
      out[0] == "\uf015|\u2500|\uf07b" and out[1] == "\u2302", out)
sc = cprun(reach("appearance", "Glyphs") + [b"\x1b[C"])
check("Appearance chooses the set, and the change shows at once",
      "ascii" in brow(sc, "Glyphs") and sc.find("+") is not None, sc)

# --- what a frame redraws ----------------------------------------------------
#
# A frame nobody's input caused -- the clock's second -- reuses the last
# build of the menus instead of rebuilding them, which was a fifth of every
# idle frame. Counted by wrapping the two functions, not timed: the log
# reads m for a build and b for a frame, and the clock's seconds with
# nothing pressed must show frames in a row with no build between them.
CNT = tempfile.mkdtemp(prefix="hibr-frames-")
COUNTED = ('eval "$(declare -f dt_menus | sed \'1s/^dt_menus/dt_menus0/\')"\n'
           'eval "$(declare -f dt_bar | sed \'1s/^dt_bar/dt_bar0/\')"\n'
           'dt_menus() { echo m >> %s/n; dt_menus0; }\n'
           'dt_bar() { echo b >> %s/n; dt_bar0; }\n' % (CNT, CNT))
run("clock", "9 24 3 20", feed=[3.5], pre=COUNTED)
seen = open(os.path.join(CNT, "n")).read().split() \
    if os.path.exists(os.path.join(CNT, "n")) else []
check("a frame the clock asked for does not rebuild the menus",
      "bbb" in "".join(seen), "".join(seen))
shutil.rmtree(CNT, True)
sc = run("clock", "9 24 3 20",
         feed=[1.5, b"\x1b[21~", 0.5, b"\x1b[C", b"\x1b[C", 0.5])
check("while input still rebuilds them, so the menus open and move as before",
      sc.find("Edit") is not None and sc.find("Copy") is not None, sc)

# --- one clipboard, every app ----------------------------------------------
#
# Copy in one window, paste in another: a terminal and a sticky share the
# desktop's clipboard both ways, a program in a terminal can set it with
# OSC 52, a paste from the machine the desktop runs on sets it too, and
# Files takes text as a new file.
NPW = "8 30 14 48"
CB = stdir()
run("stickies", NPW, also=[("Terminal", TW, "term")], pre=LONG, wait=1.2,
    feed=[press(3, 3), drag(3, 8), release(3, 8), b"\x1bc",
          press(16, 55), release(16, 55), b"\x1bv"],
    env=stenv(CB), end=None)
note = stnote(CB) or ""
check("text copied in a terminal pastes into a sticky", "row 30" in note, note)
shutil.rmtree(CB, True)

CB = stdir()
open(os.path.join(CB, "1.txt"), "w").write("hello note")
open(os.path.join(CB, "notes.json"), "w").write('{"1":{"color":"yellow"}}')
sc = run("stickies", NPW, also=[("Terminal", TW, "term")], pre=SH, wait=1.2,
         feed=[0.5, press(15, 52), release(15, 52), b"\x1b[F", b"\x1b[1;2H",
               b"\x1bc", press(6, 10), release(6, 10), b"\x1bv", 0.5],
         env=stenv(CB), end=None)
check("and text selected in a sticky pastes into a terminal's program",
      sc.find("sh> hello note") is not None, sc)
shutil.rmtree(CB, True)

OSC52 = "TW_CMD=(/bin/sh -c 'printf \"\\033]52;c;aGVsbG8=\\007\"; sleep 5')"
sc = run(*TERM, pre=OSC52, wait=1.2, end=None)
check("a program in a terminal that sets the clipboard sets it everywhere",
      b"\x1b]52;c;aGVsbG8=\x07" in sc.out, sc)
sc = run(*TERM, pre=OSC52 + "\nDT_TERMCLIP=0", wait=1.2, end=None)
check("unless Programs Set Clipboard is off",
      b"\x1b]52;c;aGVsbG8=\x07" not in sc.out, sc)

CB = stdir()
run("stickies", NPW, feed=[b"\x1b[200~hi\x1b[201~", b"\x1bv"],
    env=stenv(CB), end=None)
note = stnote(CB) or ""
check("a paste from the machine the desktop runs on becomes the clipboard",
      note == "hihi", note)
shutil.rmtree(CB, True)

CB = tempfile.mkdtemp(prefix="hibr-clip-")
run("files", FW, feed=[b"\x1b[200~some text\x1b[201~", 0.3],
    pre="FB_DIR=%s" % CB)
f = os.path.join(CB, "Pasted text.txt")
check("text pasted into Files becomes a new file there",
      os.path.exists(f) and open(f).read() == "some text\n",
      os.listdir(CB))
shutil.rmtree(CB, True)

# --- what a program says to the terminal, not the screen ---------------------
#
# A notification from a program in a terminal window is the desktop's own
# and is sent on; a bell is sent on and marks an unfocused window's title
# until it is clicked; a link stays a link.
NOTE9 = "TW_CMD=(/bin/sh -c 'sleep 0.5; printf \"\\033]9;build done\\007\"; sleep 5')"
sc = run(*TERM, pre=NOTE9, wait=1.5, end=None)
check("a program's notification shows on the desktop",
      sc.find("build done") is not None, sc)
check("and is sent on to the real terminal", b"\x1b]9;" in sc.out and
      b"build done\x07" in sc.out, sc)
BELL = "TW_CMD=(/bin/sh -c 'sleep 0.5; printf \"\\007\"; sleep 5')"
sc = run("clock", "8 24 14 50", pre=BELL, wait=1.5, end=None,
         also=[("Term", TW, "term")])
check("a bell from a window without focus rings the real terminal",
      b"\x07" in re.sub(rb"\x1b\][^\x07]*\x07", b"", sc.out), sc)
check("and marks that window's title", sc.find("• Term") is not None, sc)
sc = run("clock", "8 24 14 50", pre=BELL, wait=1.5, end=None,
         also=[("Term", TW, "term")],
         feed=[1.2, press(5, 10), release(5, 10), 0.5])
check("until the window is clicked", sc.find("• Term") is None and
      sc.find("Term") is not None, sc)
LINK8 = ("TW_CMD=(/bin/sh -c 'printf \"\\033]8;;https://example.com\\007"
         "LINK\\033]8;;\\007 after\"; sleep 5')")
sc = run(*TERM, pre=LINK8, wait=1.2, end=None)
inside = b"".join(re.findall(
    rb"\x1b\]8;;https://example\.com\x07(.*?)\x1b\]8;;\x07", sc.out, re.S))
check("a link printed in a terminal window stays a link on the real one",
      b"LINK" in re.sub(rb"\x1b\[[0-9;?]*[A-Za-z]", b"", inside) and
      b"after" not in inside, sc)

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

sc = run("snake", SW, feed=[b"\x1b[B", 0.6, b"p"])
col = [r for r in range(3, 20) if sc.at(r, 23) == "█"]
check("an arrow sets it off, and it goes that way",
      sc.find("paused") is not None and len(col) >= 3, sc)

sc = run("snake", SW, feed=[b"\x1b[A", 3.0])
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

# With 71, one clear cell is left among 72, and it was once, one run in 72,
# the very corner opened first -- which won the game before a mine could be
# hit. RANDOM=1 seeds the layout (assigning RANDOM seeds it, as in bash) so
# the corners hold mines every time.
UL = [b"\x1b[A"] * 4 + [b"\x1b[D"] * 4
sc = run("mines", MW, feed=[b" "] + UL + [b" ", b"\x1b[C", b" "],
         pre="RANDOM=1\nMINES_COUNT=71")
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
      sc.find("♥♥♥") is not None and sc.find_from("▀▀▀▀▀▀▀", 2) == (20, 20), sc)

sc = run("bricks", BW, feed=[b"\x1b[D"])
check("the arrows move the bat, and the ball rides along",
      sc.find_from("▀▀▀▀▀▀▀", 2) == (20, 17) and sc.at(19, 20) == "●", sc)

sc = run("bricks", BW, feed=[press(10, 36)])
check("a click puts the bat under it", sc.find_from("▀▀▀▀▀▀▀", 2) == (20, 33), sc)

sc = run("bricks", BW, feed=[b" ", 2.0])
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

# The details view sorts by a heading: click Size and the largest file is
# first, click again and the smallest is; ".." stays on top and folders
# stay ahead of files either way; Name sorts without regard to case.
HS = tempfile.mkdtemp(prefix="hibr-hsort-")
os.mkdir(os.path.join(HS, "zdir"))
os.mkdir(os.path.join(HS, "Adir"))
open(os.path.join(HS, "big.bin"), "wb").write(b"x" * 5000)
open(os.path.join(HS, "Small.txt"), "wb").write(b"x" * 10)
open(os.path.join(HS, "mid.dat"), "wb").write(b"x" * 300)
HSPRE = "FB_DIR=%s\nFB_DEFAULTVIEW=details\n" % HS
HSW = "14 70 2 2"

def hsorder(sc):
    return [w for r in range(sc.rows) for w in sc.row(r).split()
            if w in ("../", "Adir/", "zdir/", "big.bin", "Small.txt",
                     "mid.dat")]

sc = run("files", HSW, pre=HSPRE)
hr = next((r for r in range(sc.rows) if "Name" in sc.row(r) and
           "Size" in sc.row(r)), None)
size = (hr, sc.row(hr).index("Size")) if hr is not None else (0, 0)
name = (hr, sc.row(hr).index("Name")) if hr is not None else (0, 0)
sc = run("files", HSW, [press(*size)], pre=HSPRE)
check("clicking Size in the details view sorts largest first, and says so",
      "Size▼" in sc.row(hr) and hsorder(sc) ==
      ["../", "Adir/", "zdir/", "big.bin", "mid.dat", "Small.txt"], sc)
sc = run("files", HSW, [press(*size), press(*size)], pre=HSPRE)
check("a second click turns it round, folders still first and still A to Z",
      "Size▲" in sc.row(hr) and hsorder(sc) ==
      ["../", "Adir/", "zdir/", "Small.txt", "mid.dat", "big.bin"], sc)
sc = run("files", HSW, [press(*name), press(*name)], pre=HSPRE)
check("Name sorts without regard to case, and reverses too",
      "Name▼" in sc.row(hr) and hsorder(sc) ==
      ["../", "zdir/", "Adir/", "Small.txt", "mid.dat", "big.bin"], sc)
shutil.rmtree(HS, True)


sc = run("files", "12 60 2 2", [b"v"], pre=PRE)
check("the details view has sizes, times and permissions",
      sc.find("Size") is not None and sc.find("Mode") is not None and
      sc.find("drwxr-xr-x") is not None, sc)
sc = run("files", "12 60 2 2", [b"v", b"v", b"\x1b[C", b"\x1b[B"], pre=PRE)
check("the icon view is a grid, and the arrows move across and down it",
      sc.find("▤▤") is not None and sc.find("6 of %d" % ENTRIES) is not None,
      sc)

# The chosen view is remembered per directory, and survives a fresh
# window entirely -- #49. Its own directory pair and XDG_STATE_HOME, not
# D, since D is what every other files check above and below assumes
# opens in the plain list default.
VA = tempfile.mkdtemp(prefix="hibr-views-a-")
VB = tempfile.mkdtemp(prefix="hibr-views-b-")
open(os.path.join(VA, "f.txt"), "w").close()
open(os.path.join(VB, "f.txt"), "w").close()
VSTATE = tempfile.mkdtemp(prefix="hibr-views-state-")

sc = run("files", "12 40 2 2", [b"v", b"v"], pre="FB_DIR=%s" % VA,
         env={"XDG_STATE_HOME": VSTATE})
check("cycling to icons shows the icons label", "icons" in sc.row(12), sc)

sc = run("files", "12 40 2 2", pre="FB_DIR=%s" % VA,
         env={"XDG_STATE_HOME": VSTATE})
check("reopening the same directory remembers icons, with no keys sent",
      "icons" in sc.row(12), sc)

sc = run("files", "12 40 2 2", pre="FB_DIR=%s" % VB,
         env={"XDG_STATE_HOME": VSTATE})
check("a directory never shown stays the plain default, list",
      "list" in sc.row(12), sc)
shutil.rmtree(VA, True)
shutil.rmtree(VB, True)
shutil.rmtree(VSTATE, True)

# #50: that plain default is Control Panel's own FB_DEFAULTVIEW, not a
# constant -- changing it changes what a directory nobody has shown
# before opens as.
VC = tempfile.mkdtemp(prefix="hibr-views-c-")
open(os.path.join(VC, "f.txt"), "w").close()
sc = run("files", "12 40 2 2", pre="FB_DEFAULTVIEW=icons\nFB_DIR=%s" % VC)
check("changing the default view changes what an untouched directory opens as",
      "icons" in sc.row(12), sc)
shutil.rmtree(VC, True)

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
check("opening a file with no registered handler asks first",
      sc.find("Unregistered extension .txt") is not None, sc)

sc = run("files", FW, [b"\x1b[B"] * 3 + [b"\r", b"y"], pre=PRE + "\n" + EDIT,
         extra=["term"], end=None, wait=1.2)
check("opening a file opens a terminal window called by its name",
      sc.find("┤ Terminal [file00.txt] ├") is not None and
      sc.find("editing") is not None, sc)

sc = run("files", FW, [b"\x1b[B"] * 3 + [b"\r", b"n"], pre=PRE + "\n" + EDIT,
         extra=["term"], end=None, wait=1.2)
check("and declining leaves it closed",
      sc.find("┤ Terminal [file00.txt] ├") is None, sc)

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
HPRE = "FB_DIR=%s\nDT_FBOPENWITH=0" % HD

sc = run("files", FW, [press(5, 10, 2)], pre=HPRE)
check("a right-click opens a File menu at the pointer, on the entry there",
      sc.find("Open") == (5, 12) and sc.find("Rename") is not None and
      sc.find("Cut") is not None and
      sc.find("Copy") is not None and sc.find("Paste") is not None and
      sc.find("Info") is not None and sc.find("Move to Trash") is not None,
      sc)
check("and selects it, as a plain click would",
      sc.find("2 of 3") is not None, sc)

sc = run("files", FW, [press(4, 10, 2)], pre=HPRE)
check("right-clicking .. dims what does not apply to it",
      sc.find("Open") is not None and sc.at(4, 29) != "o", sc)

# Rename and Get Info are real windows of their own, not a one-line note --
# opened on the entry the context menu (or the keyboard: n and i, straight
# from the browser) was aimed at.

RD = tempfile.mkdtemp(prefix="hibr-rename-")
open(os.path.join(RD, "old.txt"), "w").write("hi\n")
RPRE = "FB_DIR=%s" % RD

sc = run("files", FW, [b"\x1b[B", b"n"], pre=RPRE, end=None)
check("n opens Rename on the selected entry, its name already there",
      sc.find("┤ Rename ├") is not None and
      sc.find("New name:") is not None and sc.find("old.txt") is not None,
      sc)

sc = run("files", FW, [b"\x1b[B", b"n", b"\x1b"], pre=RPRE)
check("escape cancels it -- nothing on disk changes",
      os.path.exists(os.path.join(RD, "old.txt")), sc)

sc = run("files", FW,
         [b"\x1b[B", b"n"] + [b"\x7f"] * 7 + [b"new.txt", b"\r"],
         pre=RPRE)
check("enter renames it and closes the window",
      sc.find("new.txt") is not None and
      sc.find("┤ Rename ├") is None, sc)
check("and it is the real file on disk that moved",
      os.path.exists(os.path.join(RD, "new.txt")) and
      not os.path.exists(os.path.join(RD, "old.txt")), sc)
for f in os.listdir(RD):
    os.unlink(os.path.join(RD, f))
open(os.path.join(RD, "old.txt"), "w").write("hi\n")
# Shift and home select the name first: Cut takes what is selected since
# Gitea #295, where it took the whole field.
sc = run("files", FW,
         [b"\x1b[B", b"n", b"\x1b[1;2H", b"\x1bx", b"\x1bv", b"\x1bv", b"\r"],
         pre=RPRE)
check("cut and paste reach the Rename field: cut empties it, paste puts "
      "the name back at the cursor, twice",
      os.path.exists(os.path.join(RD, "old.txtold.txt")), sc)
sc = run("files", FW,
         [b"\x1b[B", b"n", b"\x1b[1;2H", b"\x1bx",
          b"\x1b[200~pasted.txt\x1b[201~", b"\r"], pre=RPRE)
check("and a paste from the real terminal lands in the field the same way",
      os.path.exists(os.path.join(RD, "pasted.txt")), sc)
shutil.rmtree(RD, True)

# Rename and Cancel are buttons: tab reaches them from the field, the
# arrows move between them, enter does whichever has focus, and a click
# does the one it lands on.
RD = tempfile.mkdtemp(prefix="hibr-rename-")
open(os.path.join(RD, "old.txt"), "w").write("hi\n")
RPRE = "FB_DIR=%s" % RD
sc = run("files", FW, [b"\x1b[B", b"n"], pre=RPRE, end=None)
check("Rename has a Rename and a Cancel button under the field",
      re.search(r" Rename .* Cancel ", sc.text()) is not None, sc)
cn = sc.find(" Cancel ")

# With focus in the field, Rename is the default -- what enter does -- and
# says so with its label in the accent; Cancel is plain. Tab puts focus on
# Rename itself, which fills it with the accent instead. Midnight's colours:
# accent #63b3ed, muted #4a5568, face #101820, ink #cbd5e0.
ACCENT, IDLE, FACE, INK = "#63b3ed", "#4a5568", "#101820", "#cbd5e0"
rn = (cn[0], sc.row(cn[0]).find(" Rename ")) if cn else None
st = sc.style(rn[0], rn[1] + 1) if rn else {}
check("with focus in the field, Rename is drawn as the default",
      st.get("fg") == ACCENT and st.get("bold") and st.get("bg") == IDLE, sc)
st = sc.style(cn[0], cn[1] + 1) if cn else {}
check("and Cancel as a plain button", st.get("fg") == INK and
      not st.get("bold"), sc)
sc2 = run("files", FW, [b"\x1b[B", b"n", b"\t"], pre=RPRE, end=None)
st = sc2.style(rn[0], rn[1] + 1) if rn else {}
check("tab puts focus on Rename, filled with the accent",
      st.get("bg") == ACCENT and st.get("fg") == FACE, sc2)
sc2 = run("files", FW, [b"\x1b[B", b"n"], pre=RPRE + "\nDT_DLGBTN=brackets",
          end=None)
bp = sc2.find("[Rename]")
st = sc2.style(bp[0], bp[1] + 1) if bp else {}
check("in brackets, the default is [Rename] in the accent on the face",
      st.get("fg") == ACCENT and st.get("bg") == FACE and st.get("bold"), sc2)
sc = run("files", FW, [b"\x1b[B", b"n", b"\x7f", b"x", b"\t", b"\t", b"\r"],
         pre=RPRE)
check("tab twice reaches Cancel, and enter there renames nothing",
      sc.find("┤ Rename ├") is None and
      os.path.exists(os.path.join(RD, "old.txt")), sc)
sc = run("files", FW, [b"\x1b[B", b"n", b"\x7f", b"x", b"\t", b"\x1b[C",
                       b"\x1b[D", b"\r"], pre=RPRE)
check("right and left move between the buttons, and enter on Rename renames",
      os.path.exists(os.path.join(RD, "old.txx")), sc)
os.rename(os.path.join(RD, "old.txx"), os.path.join(RD, "old.txt"))
sc = run("files", FW, [b"\x1b[B", b"n", b"\x7f", b"z"] +
         ([press(cn[0], cn[1] + 1)] if cn else []), pre=RPRE)
check("clicking Cancel closes it with the file as it was",
      cn is not None and sc.find("┤ Rename ├") is None and
      os.path.exists(os.path.join(RD, "old.txt")), sc)
shutil.rmtree(RD, True)

# A name typed here can have a space in it, and dt_textkey's own "text
# cur" return has no other separator to split on -- ${res#* } (shortest
# prefix, the first space) used to read the *text*'s own embedded space
# as if it were that separator, corrupting the cursor for every key
# typed after it. Renaming to something with a space in the middle, then
# typing more past it, is exactly what would have scrambled.
RD2 = tempfile.mkdtemp(prefix="hibr-rename2-")
open(os.path.join(RD2, "old.txt"), "w").write("hi\n")
sc = run("files", FW,
         [b"\x1b[B", b"n"] + [b"\x7f"] * 7 + [b"new file.txt", b"\r"],
         pre="FB_DIR=%s" % RD2)
check("a space in the new name does not scramble what is typed after it",
      os.path.exists(os.path.join(RD2, "new file.txt")), sc)
shutil.rmtree(RD2, True)

sc = run("files", FW, [press(5, 10, 2), press(12, 12)], pre=HPRE, end=None)
check("Info opens a real Get Info window with the entry's own details",
      sc.find("┤ Get Info ├") is not None and
      sc.find("Name: note.txt") is not None and
      sc.find("Kind: file") is not None and
      sc.find("Size: 6B") is not None and
      sc.find("Owner:") is not None and sc.find("Group:") is not None,
      sc)
# Read from where the box rows are rather than from where a dialog
# centred on the screen would put them: Get Info opens over the Files
# window it was asked from (Gitea #293).
gio = sc.find("Owner [") or (0, 0)
gir, gic = gio[0], gio[1] + 7
check("and the nine permission bits read as checkboxes, matching -rw-r--r--",
      sc.at(gir, gic) == "x" and sc.at(gir, gic + 6) == "x" and
      sc.at(gir, gic + 12) == " " and
      sc.at(gir + 1, gic) == "x" and sc.at(gir + 1, gic + 6) == " ", sc)

NP = os.path.join(HD, "note.txt")
os.chmod(NP, 0o644)
sc = run("files", FW, [press(5, 10, 2), press(12, 12),
                       press(gir + 1, gic + 6)],
         pre=HPRE, end=None)
check("clicking a permission box chmods the real file at once",
      oct(os.stat(NP).st_mode & 0o777) == "0o664", sc)
os.chmod(NP, 0o644)

sc = run("files", FW, [press(5, 10, 2), press(12, 12), b"\x1b"], pre=HPRE)
check("escape closes Get Info without applying a pending name edit",
      os.path.exists(NP) and sc.find("┤ Get Info ├") is None, sc)

sc = run("files", FW, [press(5, 10, 2), press(8, 12)], pre=HPRE)
check("Cut puts its paths on the clipboard, the same as Copy does",
      b"\x1b]52;c;" in sc.out, sc)

# A second window, into a folder of its own, proves Cut moves rather than
# copies: HD2 has a subfolder to paste into, so the file has somewhere to
# actually go.
HD2 = tempfile.mkdtemp(prefix="hibr-hfiles2-")
os.mkdir(os.path.join(HD2, "sub"))
open(os.path.join(HD2, "note.txt"), "w").write("hello\n")
H2PRE = "FB_DIR=%s\nDT_FBOPENWITH=0" % HD2
H2TWO = [("Files", "12 34 2 40", "files")]
H2INTO = [press(5, 44), press(5, 44)]

sc = run("files", FW,
         H2INTO + [press(6, 10, 2), press(9, 12), press(4, 44, 2),
                   press(9, 46)],
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
check("and lists it by its own program name, the default for its type",
      sc.find("touch (default)") is not None, sc)

# Click where the entry actually rendered just above, not a row copied by
# eye -- a hardcoded one is exactly what let the submenu-position bug
# below go unnoticed: it happened to match the (buggy) implementation.
pos = sc.find("touch (default)")
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

# The executable bit, not a second extension, decides run vs edit: a
# double-clicked executable runs directly in a terminal instead of opening
# in hvi. Its own path is not also appended as an argument (it is the
# command, not something being handed to one), and the window stays open
# on a clean exit -- unlike a registered handler's own term:1, closing
# instantly here would be a flash and gone before there was anything to
# read.
EX = tempfile.mkdtemp(prefix="hibr-exec-")
EXOUT = os.path.join(EX, "out.txt")
EXSCRIPT = os.path.join(EX, "run.sh")
open(EXSCRIPT, "w").write('#!/bin/sh\necho "args:$#" > %s\n' % EXOUT)
os.chmod(EXSCRIPT, os.stat(EXSCRIPT).st_mode | 0o111)
EXPRE = "FB_DIR=%s\n" % EX

sc = run("files", FW, [press(5, 10), press(5, 10)], pre=EXPRE,
         extra=("term",), wait=1.5)
check("an executable, double-clicked, runs in a terminal instead of "
      "opening in hvi", sc.find("┤ Terminal [run.sh] ├") is not None, sc)
check("the window stays open on a clean exit, showing the status",
      sc.find("[exited 0") is not None, sc)
check("and it runs with its own path only, not appended a second time "
      "as an argument",
      os.path.exists(EXOUT) and open(EXOUT).read().strip() == "args:0", sc)
shutil.rmtree(EX, True)

# --- the task manager -------------------------------------------------------
#
# The process list is the real machine's, so nothing here asserts on which
# names or numbers appear -- only that a header and at least one real row
# are drawn, and that both sort keys run without error. Nothing here sends
# x or shift-x: killing whatever a live sort put on top would be killing a
# process this suite does not own. Before D and S are cleaned up below, since
# run() still needs S/session.hibr to exist.

# --- Modules: what can be loaded, what is, and changing either (#133) -----
#
# The rows are whatever is on the module path, which under the harness is
# this build's own build/mods -- so the list is read here from the same
# `mod avail` the app reads, and a module is reached by how many rows down
# it is rather than by a hard-coded row. A module added to the tree later
# therefore moves nothing in these checks.
MODMAN = ("modman", "20 74 2 3")


def modnames():
    """Every module on this build's path, in the order mod avail lists it,
    which is the order the app shows. Read through a pipe, so it is the
    field form: name first."""
    out = subprocess.run([sx.HIBR, "-c", "mod avail"], capture_output=True,
                         text=True, env=dict(os.environ,
                                             HIBR_MODPATH=tree("build/mods"))).stdout
    return [l.split("\t")[0] for l in out.splitlines() if "\t" in l]


MODNAMES = modnames()


def moddown(nm):
    """The keys that move the selection from the top row to that module."""
    return [b"\x1b[B"] * MODNAMES.index(nm)


def modrow(sc, nm):
    """The row that module is drawn on, with its state and what it offers."""
    hit = sc.find(" %s " % nm)
    return sc.row(hit[0]) if hit else ""


sc = run(*MODMAN)
check("Modules lists what the module path offers: each one's version, "
      "whether it is loaded, and what it adds",
      sc.find("MODULE") is not None and sc.find("VERSION") is not None and
      sc.find("STATE") is not None and "available" in modrow(sc, "csv") and
      "loaded" in modrow(sc, "console") and
      "display: console" in modrow(sc, "console"), sc)
check("and the selected module's description and path below it, which is "
      "what a list has no room for",
      sc.find("tar and tar.gz read as folders") is not None and
      sc.find("/archive.so") is not None, sc)

sc = run(*MODMAN, feed=moddown("csv") + [b"\r", 0.6])
check("enter loads the selected module, and the row says so",
      "loaded" in modrow(sc, "csv"), (modrow(sc, "csv"), sc))
sc = run(*MODMAN, feed=moddown("csv") + [b"\r", 0.6, b"u", 0.6])
check("and u unloads it again", "available" in modrow(sc, "csv"),
      (modrow(sc, "csv"), sc))

# The two refusals. Both are explained on the window's own bottom line for
# the selected module, before anything is pressed -- which is the durable
# half of this: a note would be gone by the time run()'s own quit keys have
# been read.
sc = run(*MODMAN, feed=moddown("console") + [b"u", 0.6])
check("the console cannot be unloaded, because the desktop is drawn "
      "through it, and the window says so rather than offering it",
      sc.find("The desktop is drawn through console") is not None and
      "loaded" in modrow(sc, "console"), sc)
sc = run(*MODMAN, feed=moddown("cat") + [b"\r", 0.6, b"u", 0.6])
check("nor a loaded module something may be holding the table of: the "
      "registry knows who offers an interface, not who took it",
      sc.find("Something may be using what cat offers") is not None and
      "loaded" in modrow(sc, "cat"), sc)

TASKS = ("tasks", "16 50 4 4")

sc = run(*TASKS)
check("the task manager lists processes under a header",
      sc.find("CPU%") is not None and sc.find("Mem") is not None and
      sc.find("Name") is not None and sc.find("PID") is not None and
      sc.find("Owner") is not None, sc)
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

# #54: PID/Owner columns, and toggling full command lines -- real machine
# data again, so only that toggling runs without error and the header
# survives it, not what changed.
sc = run(*TASKS, feed=[b"n"])
check("n toggles full command lines, without error",
      sc.find("CPU%") is not None and sc.find("PID") is not None, sc)

# Whose processes the list shows (#131). The scan is the thing to check
# rather than a screen: with everyone's it holds the machine's whole
# process table, with only mine every row is this user's own, and with
# only this desktop's it is this process and whatever it has started --
# which for a shell running one command is nothing at all, so the answer
# is one row and that row is itself. The owner is compared through
# tasks_ownername, not against `id -un`: a user who is not in
# /etc/passwd (an LDAP one) shows as their uid, in the app and here
# alike.
TFILT = (
    '. %s\n'
    'tasks_loadusers\n'
    'me := tasks_ownername "$EUID"\n'
    'for f in everyone mine desktop; do\n'
    '  TK_FILTER=$f; tasks_scan 1\n'
    '  n=${TK[1]["n"]}; i=0; ours=0\n'
    '  while [ "$i" -lt "$n" ]; do\n'
    '    [ "${TK[1][$i]["owner"]}" = "$me" ] && ours=$((ours + 1))\n'
    '    i=$((i + 1))\n'
    '  done\n'
    '  echo "$f n=$n ours=$ours"\n'
    'done\n'
    'echo "one=${TK[1][0]["pid"]} self=$$"\n'
    # And with a child of its own, the walk finds it: that is what makes
    # the filter a terminal window's shell and whatever it is running,
    # rather than only the desktop process itself.
    'sleep 30 &\n'
    'kid=$!\n'
    'tasks_scan 1\n'
    'n=${TK[1]["n"]}; i=0; got=\n'
    'while [ "$i" -lt "$n" ]; do\n'
    '  got="$got ${TK[1][$i]["pid"]}"\n'
    '  i=$((i + 1))\n'
    'done\n'
    'echo "withkid n=$n kid=$kid pids=$got"\n'
    'kill "$kid" 2> /dev/null\n'
    % (appdir("tasks") + "/tasks.hibr")
)
out = subprocess.run([sx.HIBR, "-c", TFILT], capture_output=True, text=True,
                     env=dict(os.environ, DT_ROWS="1")).stdout
TF = {}
for line in out.splitlines():
    w = line.split()
    if len(w) == 3 and w[1].startswith("n=") and w[2].startswith("ours="):
        TF[w[0]] = (int(w[1][2:]), int(w[2][5:]))
one = dict(w.split("=") for w in
           next((l for l in out.splitlines() if l.startswith("one=")),
                "one=0 self=1").split())
check("only mine leaves every row this user's own, where everyone's holds "
      "rows that are not",
      len(TF) == 3 and TF["mine"][0] == TF["mine"][1] and
      TF["everyone"][0] > TF["mine"][0] and
      TF["everyone"][1] < TF["everyone"][0], out)
kid = next((l for l in out.splitlines() if l.startswith("withkid ")), "")
kidw = dict(w.split("=", 1) for w in kid.split(" ", 3)[1:]) if kid else {}
check("and only this desktop's is this process and what it has started: "
      "itself alone, then itself and a child it forked",
      TF.get("desktop", (0, 0))[0] == 1 and one["one"] == one["self"] and
      kidw.get("n") == "2" and kidw.get("kid") in kidw.get("pids", "").split(),
      out)
# With no _ in the environment, as a session the login screen starts has
# none: the scan read each process's uid with `while read -r k v _`, which
# strict vars refused there, so it came back empty, read back as uid 0,
# and every row of this desktop said root, with 0K of memory (Gitea #281).
envu = dict(os.environ, DT_ROWS="1")
envu.pop("_", None)
out = subprocess.run(
    [sx.HIBR, "-c", '. %s\ntasks_loadusers\nme := tasks_ownername "$EUID"\n'
     'TK_FILTER=desktop; tasks_scan 1\n'
     'echo "me=$me owner=${TK[1][0]["owner"]} mem=${TK[1][0]["mem"]}"\n'
     % (appdir("tasks") + "/tasks.hibr")],
    capture_output=True, text=True, env=envu).stdout
row = dict(w.split("=", 1) for w in
           next((l for l in out.splitlines() if l.startswith("me=")),
                "me=? owner=- mem=0").split())
check("in a session with no _ in its environment, as the login screen "
      "starts one, this desktop's rows are still this user's, not root's",
      row["owner"] == row["me"] and row["owner"] != "root" and
      row["mem"].isdigit() and int(row["mem"]) > 0, out)

# A uid /etc/passwd does not have (Gitea #138). Reading that file alone is
# right for a machine whose users are all local and wrong for every other
# kind: an LDAP user has no line there and showed as a bare number -- on the
# machine this was reported from, that was the owner's own account, so every
# process they ran was labelled with a uid.
#
# A stub `getent` earlier on PATH rather than this machine's own directory,
# so the check is the same everywhere and says what it means; it counts its
# own calls, because "one fork per uid, ever" is the design constraint and
# not merely the happy path.
NSSD = tempfile.mkdtemp(prefix="hibr-nss-")
NSSCALLS = os.path.join(NSSD, "calls")
open(os.path.join(NSSD, "getent"), "w").write(
    "#!/bin/sh\n"
    "echo \"$2\" >> %s\n"
    "[ \"$2\" = 4242 ] || exit 2\n"
    "echo 'ldapuser:x:4242:4242::/home/ldapuser:/bin/sh'\n" % NSSCALLS)
os.chmod(os.path.join(NSSD, "getent"), 0o755)
NSS = (
    '. %s\n'
    'tasks_loadusers\n'
    'a := tasks_ownername 4242\n'
    'b := tasks_ownername 4242\n'
    'c := tasks_ownername 4243\n'
    'r := tasks_ownername 0\n'
    'echo "a=$a b=$b c=$c r=$r"\n'
    % (appdir("tasks") + "/tasks.hibr"))
out = subprocess.run([sx.HIBR, "-c", NSS], capture_output=True, text=True,
                     env=dict(os.environ, DT_ROWS="1",
                              PATH=NSSD + os.pathsep + os.environ["PATH"])).stdout
# Counted per uid rather than in total: something else on the machine
# may ask getent for its own reasons, and what is being asserted is
# that this uid is asked for once however often it is looked up.
asked = open(NSSCALLS).read().split() if os.path.exists(NSSCALLS) else []
calls = asked.count("4242")
check("a uid /etc/passwd does not have is resolved through NSS",
      "a=ldapuser" in out, out)
check("and a uid nothing answers for is itself, rather than empty",
      "c=4243" in out and "r=root" in out, out)
check("each is asked for once and remembered, so a scan does not fork per "
      "process", calls == 1, (calls, asked, out))
shutil.rmtree(NSSD, ignore_errors=True)

sc = run(*TASKS, feed=[b"w"])
check("w shows only this user's processes, and says so in the window's "
      "title rather than leaving a short list unexplained",
      sc.find("Task Manager [Mine]") is not None, sc)

# A name the kernel cut, recovered from argv[0] (#171). comm lives in
# TASK_COMM_LEN -- 15 usable bytes -- so no column width brings the rest
# back, and this is the scan's job rather than the drawing's so that
# sorting by Name agrees with what Name shows. Both cases are made here
# rather than looked for on the machine: a program with a name too long
# for comm, and a process that has renamed its own argv, which is what
# stops this taking a name from one string to sit under another. The
# second is hibr wearing the very names 0.99.131 gave the desktop.
TFULL = (
    '. %s\n'
    '[ "$(command uname)" = Linux ] || { echo "skip"; exit 0; }\n'
    'd=$(command mktemp -d)\n'
    'command cp "$(command -v sleep)" "$d/averylongprogramname"\n'
    '"$d/averylongprogramname" 30 &\n'
    'cut=$!\n'
    '"$HIBR" -c \'title -s "term:Terminal:104" "desktop [Terminal 104]"\n'
    'sleep 30\' &\n'
    'ren=$!\n'
    # Both children rename themselves as they start, so wait for the
    # kernel to have the new comm rather than guessing at a delay.
    'i=0\n'
    'while [ "$i" -lt 100 ]; do\n'
    '  read -r ca 2> /dev/null < "/proc/$cut/comm"\n'
    '  read -r cr 2> /dev/null < "/proc/$ren/comm"\n'
    '  case $ca$cr in averylong*term:*) break ;; esac\n'
    '  sleep 0.05\n'
    '  i=$((i + 1))\n'
    'done\n'
    'na := tasks_fullname "$cut" "$ca"\n'
    'nr := tasks_fullname "$ren" "$cr"\n'
    'echo "cut comm=$ca full=$na"\n'
    'echo "renamed comm=$cr full=$nr"\n'
    'kill "$cut" "$ren" 2> /dev/null\n'
    'command rm -rf "$d"\n'
    % (appdir("tasks") + "/tasks.hibr")
)
out = subprocess.run([sx.HIBR, "-c", TFULL], capture_output=True, text=True,
                     env=dict(os.environ, HIBR=sx.HIBR,
                              DT_ROWS="1")).stdout
TU = dict((tfl.split()[0], dict(tfw.split("=", 1) for tfw in tfl.split()[1:]))
          for tfl in out.splitlines() if tfl and not tfl.startswith("skip"))
check("a name comm cut short is recovered whole from argv[0]",
      not TU or (len(TU["cut"]["comm"]) == 15 and
                 TU["cut"]["full"] == "averylongprogramname"), out)
check("but a process that renamed its own argv keeps the name it reports, "
      "rather than borrowing an unrelated one",
      not TU or (TU["renamed"]["comm"] == "term:Terminal:1" and
                 TU["renamed"]["full"] == "term:Terminal:1"), out)

# tasks_sort carrying "owner" along with pid/name/cpu/mem is the actual
# correctness question -- found while adding the Owner column: sorting
# moved every other field but left owner behind at its old row index,
# pairing every process with some other one's user. Deterministic
# fixture data, not the real process list, since this is exactly the
# bug a real list's own near-sorted-already rows could hide by chance.
TSORT = (
    '. %s\n'
    'TK[1]["n"]=3\n'
    'TK[1][0]["pid"]=10; TK[1][0]["name"]=a; TK[1][0]["owner"]=alice\n'
    'TK[1][0]["cpu"]=5; TK[1][0]["mem"]=100\n'
    'TK[1][1]["pid"]=20; TK[1][1]["name"]=b; TK[1][1]["owner"]=bob\n'
    'TK[1][1]["cpu"]=50; TK[1][1]["mem"]=50\n'
    'TK[1][2]["pid"]=30; TK[1][2]["name"]=c; TK[1][2]["owner"]=carol\n'
    'TK[1][2]["cpu"]=1; TK[1][2]["mem"]=200\n'
    'tasks_sort 1 cpu\n'
    'echo "${TK[1][0]["pid"]}:${TK[1][0]["owner"]}'
    ' ${TK[1][1]["pid"]}:${TK[1][1]["owner"]}'
    ' ${TK[1][2]["pid"]}:${TK[1][2]["owner"]}"\n'
    % (appdir("tasks") + "/tasks.hibr")
)
out = subprocess.run([sx.HIBR, "-c", TSORT], capture_output=True, text=True,
                     env=dict(os.environ, DT_ROWS="1")).stdout.strip()
check("sorting by CPU keeps each pid's own owner with it, not another's",
      out == "20:bob 10:alice 30:carol", out)

# tasks_click's own row math -- found the same way, while adding #53's
# context menu: row 1 is the header (tasks_draw draws it there), the
# list starts at row 2, and the old "top + r - 1" gave index 1 for row
# 2's own click -- one row below whatever was actually clicked, for
# every left click there has ever been, not just the new right one.
TCLICK = (
    '. %s\n'
    'TK[1]["n"]=3; TK[1]["top"]=0\n'
    'TK[1][0]["pid"]=10; TK[1][1]["pid"]=20; TK[1][2]["pid"]=30\n'
    'tasks_click 1 2; echo "row2=${TK[1]["sel"]}"\n'
    'tasks_click 1 3; echo "row3=${TK[1]["sel"]}"\n'
    'tasks_click 1 1; echo "row1=${TK[1]["sel"]}"\n'
    % (appdir("tasks") + "/tasks.hibr")
)
out = subprocess.run([sx.HIBR, "-c", TCLICK], capture_output=True, text=True,
                     env=dict(os.environ, DT_ROWS="1")).stdout
check("clicking the first visible row selects index 0, not 1",
      "row2=0" in out, out)
check("the second visible row selects index 1",
      "row3=1" in out, out)
check("clicking the header row leaves the selection alone",
      "row1=1" in out, out)

# The context menu -- #53 -- selects the row under the click first, the
# same as a left click already does, before it opens: right-clicking a
# row not currently selected must show End Task and View Details for
# that row, not whatever was selected before.
sc = run(*TASKS, feed=[press(6, 10, 2)])
check("right-clicking an unselected row selects it and opens the menu",
      sc.find("End Task") is not None and
      sc.find("End Task (force)") is not None and
      sc.find("View Details") is not None, sc)

# The row is captured once, on the frame the context menu first opens,
# not re-derived every time it rebuilds -- found the hard way, against
# the real process list: dt_ctxbuild calls tasks_context fresh every
# frame the menu stays open, and re-deriving the clicked pid from the
# click's row every single time meant a background rescan reordering
# the list while the menu just sat there, unclicked, could make View
# Details open on a different process than the one actually
# right-clicked. Fixture data, not the real list, since the real one
# reorders on its own schedule regardless of what a test wants to
# hold still -- one capture, then a rescan simulated by changing the
# fixture between two rebuilds of the same still-open menu.
TCTX = (
    '. %s\n'
    'TK[1]["n"]=2; TK[1]["top"]=0\n'
    'TK[1][0]["pid"]=10; TK[1][0]["name"]=a; TK[1][0]["owner"]=alice\n'
    'TK[1][0]["cpu"]=1; TK[1][0]["mem"]=100\n'
    'TK[1][1]["pid"]=20; TK[1][1]["name"]=b; TK[1][1]["owner"]=bob\n'
    'TK[1][1]["cpu"]=2; TK[1][1]["mem"]=200\n'
    'DT[1]["row"]=4\n'
    'MB_CY=6; MB_CX=10\n'
    'dt_menu() { :; }; dt_item() { :; }; dt_dim() { :; }; dt_sep() { :; }\n'
    'tasks_context 1\n'
    'echo "first=${TK[1]["ctxpid"]}"\n'
    'TK[1][0]["pid"]=999\n'
    'tasks_context 1\n'
    'echo "second=${TK[1]["ctxpid"]}"\n'
    % (appdir("tasks") + "/tasks.hibr")
)
out = subprocess.run([sx.HIBR, "-c", TCTX], capture_output=True, text=True,
                     env=dict(os.environ, DT_ROWS="1")).stdout
check("right-click selects row 0 (index under the click), pid 10",
      "first=10" in out, out)
check("a rescan while the same menu stays open does not change it",
      "second=10" in out, out)

# The list sorts by any heading, and a second click on the same one turns
# it round; a new window starts in whatever order the pane chose; the
# wheel moves the view without moving the selection. Fixture data, the
# same as the sort and click checks above.
THEAD = (
    # The widget first: Task Manager's own scrolling is the shared
    # dt_scrollmove and dt_scrollsel since 0.99.127, so an app sourced on
    # its own still needs what it scrolls with.
    '. %s\n'
    '. %s\n'
    'TK_SORTBY=name; TK_SORTDIR=asc\n'
    'tasks_open 1; echo "open=${TK[1]["by"]}/${TK[1]["dir"]}"\n'
    'TK[1]["hx"]="1 8 30 39 46 53"\n'
    'tasks_click 1 1 10; echo "name2=${TK[1]["by"]}/${TK[1]["dir"]}"\n'
    'tasks_click 1 1 40; echo "cpu=${TK[1]["by"]}/${TK[1]["dir"]}"\n'
    'tasks_click 1 1 42; echo "cpu2=${TK[1]["by"]}/${TK[1]["dir"]}"\n'
    'tasks_click 1 1 2; echo "pid=${TK[1]["by"]}/${TK[1]["dir"]}"\n'
    'TK[1]["n"]=50; TK[1]["vis"]=10; TK[1]["top"]=0; TK[1]["sel"]=4\n'
    'tasks_wheel 1 down; tasks_wheel 1 down; echo "down=${TK[1]["top"]}"\n'
    'tasks_wheel 1 up; echo "up=${TK[1]["top"]} sel=${TK[1]["sel"]}"\n'
    'i=0; while [ $i -lt 20 ]; do tasks_wheel 1 down; i=$((i + 1)); done\n'
    'echo "end=${TK[1]["top"]}"\n'
    'tasks_wheel 1 up 5 10; echo "wm=$? ${TK[1]["top"]}"\n'
    % (tree("programs/desktop/widgets/scrollbar.hibr"),
       appdir("tasks") + "/tasks.hibr")
)
out = subprocess.run([sx.HIBR, "-c", THEAD], capture_output=True, text=True,
                     env=dict(os.environ, DT_ROWS="1")).stdout
check("a new window starts in the order the Task Manager pane chose",
      "open=name/asc" in out, out)
check("clicking the sorted heading again turns the order round",
      "name2=name/desc" in out, out)
check("clicking another heading sorts by it -- a number column largest "
      "first -- and again turns it round",
      "cpu=cpu/desc" in out and "cpu2=cpu/asc" in out and
      "pid=pid/asc" in out, out)
check("the wheel scrolls three rows at a time and leaves the selection",
      "down=6" in out and "up=3 sel=4" in out, out)
check("and stops where the last row is in view", "end=40" in out, out)
check("the window manager's own call -- id, direction, row and col -- "
      "reaches it too", "wm=0 37" in out, out)

sc = run(*TASKS)
hr = sc.find("PID")
check("the sorted heading carries its direction, CPU largest first",
      hr is not None and "▼CPU%" in sc.row(hr[0]), sc)
nm = sc.find("Name")
sc = run(*TASKS, feed=[press(nm[0], nm[1] + 1)] if nm else [])
check("clicking a heading sorts by it, and says so",
      "Name▲" in sc.row(hr[0]) if hr else False, sc)
sc = run(*TASKS, feed=[press(nm[0], nm[1] + 1), press(nm[0], nm[1] + 1)]
         if nm else [])
check("and a second click turns it round",
      "Name▼" in sc.row(hr[0]) if hr else False, sc)

# The scrollbar sits in the list's right margin, between the Mem column
# and the border: a real machine has more processes than rows, so a track
# and its thumb are there.
def tkbar(sc):
    hr = sc.find("PID")
    if not hr:
        return False
    head = sc.row(hr[0])
    lo, hi = head.find("Mem") + 3, head.rfind("│")
    return any(ch in "│█" for r in range(hr[0] + 1, hr[0] + 6)
               for ch in sc.row(r)[lo:hi])

sc = run(*TASKS)
check("the list has a scrollbar in its right margin", tkbar(sc), sc)
sc = run(*TASKS, pre="TK_BAR=0\n")
check("and none when the pane turns it off", not tkbar(sc), sc)

# Two graphs at the bottom -- #52 -- CPU on the left, Mem on the right,
# a percentage label above each and a sparkline below it. Not the
# specific numbers or heights, the real machine's again, only that both
# halves are actually drawn.
sc = run(*TASKS)
# "CPU " (a real space) is the graph's own label, not the header's
# "CPU%" -- the one place with no space between the two.
row = sc.find("CPU ")
check("a CPU% label sits above the left graph", row is not None, sc)
check("and a Mem% label beside it, on the right",
      row is not None and "Mem" in sc.row(row[0]), sc)
SPARK = "▁▂▃▄▅▆▇█"
sparkrow = sc.row(row[0] + 1) if row else ""
check("a sparkline glyph is drawn under each label",
      any(c in SPARK for c in sparkrow), sc)

# --- what nothing reached before 0.56 ---------------------------------------
#
# Each of these drives a function the census found no suite calling. Task
# Manager's End Task is tested in tests/603-tasks-direct.t instead, on a
# process of the test's own: through the window it would end whatever row
# the machine running the suite has selected.

sc = run("calc", CW, [b"6", b"*", b"7", b"=", press(*calc_key(0), 2), b"u"])
check("Use Answer puts the last answer back into the expression",
      sc.find("expression") is None and sc.find("42") is not None, sc)

NPD5 = stdir()
run("stickies", "12 40 2 2", feed=[b"a", b"\r", b"b", b"\x08", b"\x08"],
    env=stenv(NPD5), end=None)
check("backspace at the start of a line joins it to the one above",
      stnote(NPD5) == "a", stnote(NPD5))
shutil.rmtree(NPD5, True)

HID = tempfile.mkdtemp(prefix="hibr-hidden-")
open(os.path.join(HID, "shown.txt"), "w").write("x\n")
open(os.path.join(HID, ".secret"), "w").write("x\n")
sc = run("files", FW, pre="FB_DIR=%s" % HID)
check("Files leaves dot files out by default", sc.find(".secret") is None, sc)
sc = run("files", FW, [b"h"], pre="FB_DIR=%s" % HID)
check("and h shows them", sc.find(".secret") is not None, sc)
HOMED = tempfile.mkdtemp(prefix="hibr-home-")
os.mkdir(os.path.join(HOMED, "homesub"))
sc = run("files", FW, [b"\x1b[21~", b"\x1b[C", b"h"], pre="FB_DIR=%s" % HID,
         env={"HOME": HOMED})
check("File > Home goes to the home directory",
      sc.find("homesub/") is not None, sc)
shutil.rmtree(HOMED, True)
sc = run("files", FW, [press(5, 10, 2), press(12, 12), b"\x7f", b"X", b"\r"],
         pre="FB_DIR=%s\nDT_FBOPENWITH=0" % HID)
check("enter in Get Info's name field applies the new name",
      os.path.exists(os.path.join(HID, "shown.txX")), sc)
shutil.rmtree(HID, True)

sc = run("notifications", "10 40 2 2", [press(2, 10, 2), b"w"])
check("the Notifications window closes from its Window menu",
      sc.find("┤ Notifications ├") is None, sc)

sc = run(*TERM, pre=SH, feed=[1.0, press(8, 10, 2)], wait=1.6, end=None)
check("a right-click in a terminal whose program has not asked for the "
      "mouse opens the terminal's own menu",
      sc.find("Send Interrupt") is not None, sc)

PICD = tempfile.mkdtemp(prefix="hibr-pic-")
shutil.copy(os.path.abspath("tests/img-2x2.png"),
            os.path.join(PICD, "pic.png"))
sc = run("files", FW, [press(5, 10), drag(8, 50), drag(9, 60),
                       release(9, 60)],
         pre="FB_DIR=%s" % PICD, extra=("imgview",),
         also=[("Image Viewer", "13 36 2 40", "imgview")])
check("a picture dragged from Files onto Image Viewer opens there",
      sc.find("┤ Image Viewer ├") is not None and
      sc.find("Drop a picture here") is None, sc)
sc = run("imgview", IVW, [press(2, 10, 2), b"w"])
check("and Image Viewer closes from its Window menu",
      sc.find("┤ Image Viewer ├") is None, sc)
shutil.rmtree(PICD, True)

# View Details only reads: it opens a window about one process. The keys
# sent to Task Manager here are i and escape -- never x or f, End Task's.
sc = run(*TASKS, feed=[press(6, 10, 2), b"i"], end=None)
check("View Details on a row's menu opens a Process Details window",
      sc.find("┤ Process Details ├") is not None and
      sc.find("PID:") is not None, sc)
sc = run(*TASKS, feed=[press(6, 10, 2), b"i", b"\x1b"])
check("and escape closes it", sc.find("┤ Process Details ├") is None, sc)
sc = run(*TASKS, feed=[b"\x1b[21~", b"\x1b[C", b"\x1b[C", b"\x1b[C", b"i"],
         end=None)
check("the Task menu's View Details opens it for the selected row",
      sc.find("┤ Process Details ├") is not None, sc)

sc = cprun(reach("control_strip", "Shadow") + [b"\x1b[C"])
check("the Control Strip's shadow is switched from its pane",
      "[ ]" in brow(sc, "Shadow"), sc)
sc = cprun(reach("notify", "Position") + [b"\x1b[C"])
check("right on Notifications' Position steps it",
      "top-right" not in brow(sc, "Position"), sc)
# A note's box sizes, border included (Gitea #227), each with its unit; and
# the smallest moved past the largest takes the largest with it, so the two
# can never describe a box that cannot be drawn.
sc = cprun(reach("notify", "Largest Width"))
check("Notifications sets a note's largest width, in columns, and its "
      "smallest and largest height, in rows",
      "52 columns" in brow(sc, "Largest Width")
      and "3 rows" in brow(sc, "Smallest Height")
      and "8 rows" in brow(sc, "Largest Height"), sc)
sc = cprun(reach("notify", "Smallest Width") + [b"\x1b[C"] * 6)
check("and a smallest width moved past the largest moves the largest too",
      "100 columns" in brow(sc, "Smallest Width")
      and "100 columns" in brow(sc, "Largest Width"), sc)
sc = cprun(reach("notify", "Clear History") + [b"\r"], end=None)
check("Clear History runs and says so",
      sc.find("History cleared") is not None, sc)
sc = cprun(reach("abouthibr", "Refresh", ("about",)) + [b"\x1b[C"],
           extra=("about",))
check("right on the About pane's Refresh steps it",
      "3000 ms" not in brow(sc, "Refresh"), sc)

# Date & Time's dialogs, all the way to their OK, with nothing on this
# machine able to change: dtp_run -- the one place a pane runs sudo -- only
# records what it was asked, and a sudo first on PATH does the same, in case
# the stub were ever not the one in effect.
FAKESU = tempfile.mkdtemp(prefix="hibr-fakesudo-")
REC = os.path.join(FAKESU, "asked")
open(os.path.join(FAKESU, "sudo"), "w").write(
    '#!/bin/sh\necho "real-sudo-path: $*" >> %s\n' % REC)
os.chmod(os.path.join(FAKESU, "sudo"), 0o755)
DTSTUB = ('fn dtp_run(...cmd) { printf "stub: %%s\\n" "${cmd[*]}" >> %s; }\n'
          'DTP_HOST=' % REC)
DTENV = {"PATH": FAKESU + ":" + os.environ["PATH"]}
sc0 = cprun(DOWN_DT, tz="Europe/London", post=DTSTUB, env=DTENV)
chg, st = sc0.find("Change…"), sc0.find("Set…")
sc = cprun(DOWN_DT + [press(chg[0], chg[1] + 1), b"P", b"a", b"r", b"i",
                      b"s"], tz="Europe/London", post=DTSTUB, env=DTENV,
           end=None)
zp = sc.find("Europe/Paris")
sc = cprun(DOWN_DT + [press(chg[0], chg[1] + 1), b"P", b"a", b"r", b"i", b"s"]
           + ([press(zp[0], zp[1] + 1)] if zp else []) + [b"\r"],
           tz="Europe/London", post=DTSTUB, env=DTENV)
asked = open(REC).read() if os.path.exists(REC) else ""
check("choosing a zone in the Time Zone dialog asks to set that zone",
      zp is not None and "stub: sudo timedatectl set-timezone Europe/Paris"
      in asked, asked)
check("and only ever through the stub: nothing reached a sudo",
      "real-sudo-path" not in asked, asked)
sc = cprun(DOWN_DT + [press(st[0], st[1] + 1)], tz="Europe/London",
           post=DTSTUB, env=DTENV, end=None)
dp = sc.find("Date:")
sc = cprun(DOWN_DT + [press(st[0], st[1] + 1)] +
           ([press(dp[0], dp[1] + 8)] if dp else []) + [b"\x1b"],
           tz="Europe/London", post=DTSTUB, env=DTENV)
check("Set Date & Time opens, takes a click on its date, and escape closes "
      "it without setting anything",
      dp is not None and sc.find("┤ Set Date") is None and
      not re.search(r"set-time\b(?!zone)",
                    open(REC).read() if os.path.exists(REC) else ""), sc)
mb = sc0.find("Menu bar")
OPENFMT = [press(mb[0], mb[1] + 12)] if mb else []
sc = cprun(DOWN_DT + OPENFMT, tz="Europe/London", post=DTSTUB, env=DTENV,
           end=None)
cu = sc.find("Custom…")
PICKCU = OPENFMT + ([press(cu[0], cu[1] + 1)] if cu else [])
sc = cprun(DOWN_DT + PICKCU, tz="Europe/London", post=DTSTUB, env=DTENV,
           end=None)
fm = sc.find("┤ Clock Format ├")
sc = cprun(DOWN_DT + PICKCU + ([press(fm[0] + 2, fm[1] + 4), b"\x1b"]
                               if fm else []),
           tz="Europe/London", post=DTSTUB, env=DTENV)
check("Custom… opens Clock Format, which takes a click and closes on escape",
      fm is not None and sc.find("┤ Clock Format ├") is None, sc)
shutil.rmtree(FAKESU, True)

# dBASE: a dot prompt, CREATE, APPEND, LIST/COUNT/AVERAGE with FOR,
# BROWSE and the Assistant menus, all over the db module built here.
DBD = tempfile.mkdtemp(prefix="hibr-dbase-")
DBPRE = "mod load %s" % tree("build/mods/db.so")
subprocess.run([sx.HIBR, "-c",
                "mod load %s; h := db create %s/big.db n:int name:str:10 "
                "v:float; i=1; while [ $i -le 60 ]; do db insert $h $i "
                "row$i $((i*3)).5; i=$((i+1)); done; db close $h"
                % (tree("build/mods/db.so"), DBD)], check=True)


def typed(s):
    return [c.encode() for c in s] + [b"\r"]


def dbrun(feed):
    return run("dbase", "22 72 1 2", feed=feed, env={"DBASE_DIR": DBD},
               end=None, wait=2.0, pre=DBPRE)


sc = dbrun(typed("CREATE stats") + typed("HOST") + typed("C") + typed("8") +
           typed("LOAD") + typed("N") + typed("6") + typed("2") + typed("") +
           typed("APPEND") + typed("web1") + typed("0.5") + typed("web2") +
           typed("2.75") + [b"\x1b", 0.3] + typed("LIST FOR load > 1") +
           typed("COUNT") + typed("AVERAGE load") +
           typed("DISPLAY STRUCTURE"))
txt = "\n".join(sc.row(r) for r in range(24))
check("CREATE asks for the fields and makes the file",
      "Database STATS created." in txt and
      os.path.exists(os.path.join(DBD, "stats.db")), sc)
check("APPEND's form stores records, and LIST FOR shows only those matching",
      re.search(r"\b2  web2 +2\.75", txt) and
      not re.search(r"\b1  web1 +0\.5", txt), sc)
check("COUNT and AVERAGE answer over the records appended",
      "2 records" in txt and "1.625" in txt, sc)
check("DISPLAY STRUCTURE lists each field with its type and width",
      re.search(r"1  HOST +Character +8", txt) and
      re.search(r"2  LOAD +Numeric", txt), sc)

sc = dbrun(typed("USE big") + typed("LIST FOR v > 20 .OR. n < 3") +
           typed("FROBNICATE") + typed("? 2 + 3 * 4") +
           typed("COUNT FOR v >= 30 .AND. n < 20") + typed("SUM n FOR n <= 4"))
txt = "\n".join(sc.row(r) for r in range(24))
check(".OR. is refused with the reason, an unknown verb is named as one",
      ".OR. is not supported" in txt and
      "Unrecognized command verb" in txt, sc)
check("? evaluates arithmetic, and FOR conditions join with .AND.",
      re.search(r"^..│14 ", txt, re.M) and "10 records" in txt and
      re.search(r"^..│ +10 ", txt, re.M), sc)

sc = dbrun(typed("USE big") + typed("BROWSE FOR v > 20") +
           [b"\x1b[B", b"\x1b[B"])
check("BROWSE shows one matching record a row, from the first",
      sc.find("BROWSE") and re.match(r"..│7 +7 +row7 +21\.5", sc.row(3)),
      sc)
sc = dbrun(typed("USE big") + typed("BROWSE FOR v > 20") +
           [b"\x1b[B", b"\x1b[6~"])
check("page down moves on, keeping the last record of the page in view",
      re.match(r"..│24 +24 +row24", sc.row(3)) and
      re.match(r"..│41 +41 +row41", sc.row(20)), sc)

F10 = [b"\x1b[21~", 0.3]
sc = dbrun(typed("USE big") + F10 + [0.3])
check("the menus are System 7's order: File, Edit, then dBASE's own",
      re.search(r"✎  File  Edit  Records  Query  Help  Window", sc.row(0)),
      sc)
sc = dbrun(typed("USE big") + F10 + [b"\x1b[C", b"\x1b[B", b"\x1b[B",
                                     b"\x1b[C", 0.3])
check("File > Databases lists the databases there are",
      sc.find("Databases") and sc.find(" BIG "), sc)
sc = dbrun(typed("USE big") + F10 + [b"\x1b[C"] * 4 + [b"c", 0.3])
check("Query > Count runs COUNT at the dot prompt",
      sc.find(". COUNT") and sc.find("60 records"), sc)
sc = dbrun(typed("USE big") + typed("COUNT") + [b"\x1b[A", b"\r"])
txt = "\n".join(sc.row(r) for r in range(24))
check("up brings the last command back to the dot prompt",
      txt.count(". COUNT") == 2 and txt.count("60 records") == 2, sc)
shutil.rmtree(DBD, True)

# Copy works in every window that shows something to copy, not only the
# editors: what About says, the time, a dBASE line being typed.
sc = run("about", "16 50 2 2", [b"\x1bc"])
check("About can be copied", b"Copied" in sc.out, sc)
sc = run("clock", "8 24 2 2", [b"\x1bc"])
check("so can the clock's time", b"Copied" in sc.out, sc)
sc = run("dbase", "20 72 1 2", [c.encode() for c in "LIST"] +
         [b"\x1b[1;2H", b"\x1bx", b"\x1bv", b"\x1bv"], pre=DBPRE, end=None)
check("dBASE's prompt cuts and pastes", sc.find(". LISTLIST") is not None, sc)

SHD2 = tempfile.mkdtemp(prefix="hibr-shotfiles-")
sc = run("files", "16 50 2 2", [b"\x1b\x07", b"\r", 0.5],
         pre="FB_DIR=%s\nDT_SHOTDIR=%s" % (SHD2, SHD2), end=None)
check("a screenshot shows up at once in a Files window open on its folder",
      re.search(r"hibr-\d{4}-\d\d-\d\d-\d{6}\.ans", sc.text()) is not None, sc)
shutil.rmtree(SHD2, True)

# Bundles (Gitea #318): a folder whose extension an app registers with
# `dir` is one item -- enter hands its path to that app instead of going in,
# its right-click menu has Show Contents, which still goes in, and the verbs
# the app registered -- and with nothing registered for it a folder of that
# name is a folder like any other.
BND = tempfile.mkdtemp(prefix="hibr-bundle-")
os.makedirs(os.path.join(BND, "Prince.game"))
open(os.path.join(BND, "Prince.game", "inside.txt"), "w").write("x\n")
BMARK = os.path.join(BND, "opened")
BAPP = ("fn bnd_open(int id, arg = \"\") { printf '%s %s\\n' \"${DT[$id][\"title\"]}\" \"$arg\" >> " + BMARK + "; }\n"
        "fn bnd_draw(int id, int h, int w, int row, int col) { console put -p \"w$id\" 1 1 bundle; }\n"
        "dt_app bnd \"Bundle App\" 6 30\n"
        "dt_app bndinfo \"Bundle Info\" 6 30\n"
        "fn bndinfo_open(int id, arg = \"\") { printf 'info %s\\n' \"$arg\" >> " + BMARK + "; }\n"
        "fn bndinfo_draw(int id, int h, int w, int row, int col) { :; }\n")
BREG = ('dt_opener bnd "Bundle App" "game" app bnd dir\n'
        'dt_openerverb game "Get Info..." bndinfo\n')
BPRE = "FB_DIR=%s\n%s" % (BND, BAPP)
def bopened():
    try:
        return open(BMARK).read()
    except OSError:
        return ""
sc = run("files", "16 50 2 2", [b"\x1b[B", b"\r", 1.0], pre=BPRE + BREG, end=None)
check("enter on a bundle hands its path to the app that registered it",
      sc.find("┤ Bundle App ├") is not None and
      os.path.join(BND, "Prince.game") in bopened(), (bopened(), sc))
sc = run("files", "16 50 2 2", [b"\x1b[B", press(5, 6, 2), 0.3, b"\x1b[B",
                                b"\x1b[B", b"\x1b[B", b"\x1b[C", 0.3],
         pre=BPRE + BREG, end=None)
check("its right-click menu has Show Contents, the app's own verbs, and Open "
      "With naming the app as the default",
      sc.find("Show Contents") is not None and sc.find("Get Info...") is not None
      and sc.find("Bundle App (default)") is not None, sc)
sc = run("files", "16 50 2 2", [b"\x1b[B", press(5, 6, 2), 0.3, b"\x1b[B",
                                b"\r", 0.5], pre=BPRE + BREG, end=None)
check("and Show Contents goes into it, since it is still a folder",
      sc.find("inside.txt") is not None, sc)
open(BMARK, "w").close()
sc = run("files", "16 50 2 2", [b"\x1b[B", press(5, 6, 2), 0.3, b"\x1b[B",
                                b"\x1b[B", b"\r", 1.0], pre=BPRE + BREG, end=None)
check("a verb opens its own app with the bundle's path",
      ("info " + os.path.join(BND, "Prince.game")) in bopened(), (bopened(), sc))
sc = run("files", "16 50 2 2", [b"\x1b[B", b"\r", 0.5], pre=BPRE, end=None)
check("with nothing registered for its extension it is a plain folder",
      sc.find("inside.txt") is not None and sc.find("┤ Bundle App ├") is None, sc)
# Covers (Gitea #336): in the icon view, where the terminal draws pixels, a
# bundle shows its .cover.png or cover.png and a plain folder its .cover.png
# only -- a stray cover.png is not taken for one -- and anything else, a
# cover that does not decode among them, keeps its glyph.
def tinypng(path, rgb):
    import struct, zlib
    raw = b"".join(b"\x00" + bytes(rgb) * 8 for _ in range(8))
    def chunk(t, d):
        return (struct.pack(">I", len(d)) + t + d +
                struct.pack(">I", zlib.crc32(t + d) & 0xffffffff))
    open(path, "wb").write(b"\x89PNG\r\n\x1a\n" +
                           chunk(b"IHDR", struct.pack(">IIBBBBB", 8, 8, 8, 2, 0, 0, 0)) +
                           chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b""))
CVD = tempfile.mkdtemp(prefix="hibr-covers-")
for d in ("Aa.game", "Bb", "Cc", "Dd.game", "Ee"):
    os.makedirs(os.path.join(CVD, d))
tinypng(os.path.join(CVD, "Aa.game", "cover.png"), (200, 40, 40))
tinypng(os.path.join(CVD, "Bb", ".cover.png"), (40, 200, 40))
tinypng(os.path.join(CVD, "Ee", "cover.png"), (40, 40, 200))
open(os.path.join(CVD, "Dd.game", "cover.png"), "w").write("not a picture\n")
CPRE = "FB_DIR=%s\nFB_DEFAULTVIEW=icons\n%s%s" % (CVD, BAPP, BREG)
PIX = dict(cell=(8, 16), env={"HIBR_GFX": "kitty"})
sc = run("files", "20 70 2 2", [0.5], pre=CPRE, end=None, **PIX)
pl = [a for a in sc.apc if "a=T" in a]
check("in icons, a bundle's cover and a folder's .cover.png are drawn as pictures",
      len(pl) == 2, (len(pl), sc))
check("the rest keep their glyphs, and tiles grow to fit the pictures",
      sc.find("Cc") is not None and sc.find("Cc")[0] == 9 and
      sc.find("Ee") is not None, sc)
sc = run("files", "20 70 2 2", [0.5], pre=CPRE + "FB_COVER=small\n", end=None, **PIX)
check("Cover Size small keeps the 3-row tile, the picture in the glyph's place",
      len([a for a in sc.apc if "a=T" in a]) == 2 and
      sc.find("Cc") is not None and sc.find("Cc")[0] == 5, sc)
sc = run("files", "20 70 2 2", [0.5], pre=CPRE + "FB_USECOVER=0\n", end=None, **PIX)
check("Use Covers off keeps every glyph and the 3-row tile, pixels or not",
      not [a for a in sc.apc if "a=T" in a] and sc.find("Cc") is not None and
      sc.find("Cc")[0] == 5, sc)
sc = run("files", "20 70 2 2", [0.5], pre=CPRE, end=None)
check("on a terminal that draws no pixels, folders keep their glyphs",
      not [a for a in sc.apc if "a=T" in a] and sc.find("Cc") is not None and
      sc.find("Cc")[0] == 5, sc)
shutil.rmtree(CVD, True)
# Control Panel > File Types asks about a kind of folder as a folder: asked
# as a file, x.game found the editor, which takes any file, and never the
# app that claims the folder -- .boxer read "hvi" on the owner's desktop.
out = subprocess.run(
    [sx.HIBR, "-c", CPLOAD + BAPP + BREG +
     'n := filetypes_rows 1; i=0; while [ "$i" -lt "$n" ]; do '
     'echo "row=${CP[1][$i]["text"]}"; i=$((i + 1)); done; '
     'owt_ask game; echo "ow=${DT_OWLIST[*]}"'],
    capture_output=True, text=True,
    env=dict(os.environ, XDG_CONFIG_HOME=PANEHOME)).stdout.splitlines()
check("File Types lists a kind of folder as a folder, with the app that opens it",
      "row=.game/  Bundle App" in out, out)
check("and its Open With offers that app, not the editor that takes any file",
      "ow=bnd" in out, out)
shutil.rmtree(BND, True)

# Open With, Open Terminal Here: a right-click on a file offers every app
# that can open it, the default first, and Other…; nothing chosen there
# sticks. File Types sets each type's list and default.
OWD = tempfile.mkdtemp(prefix="hibr-ow-")
open(os.path.join(OWD, "a.ans"), "w").write("hello\n")
OWPRE = "FB_DIR=%s" % OWD
OWX = ("term", "imgview")
CTX = [b"\x1b[B", press(5, 6, 2), 0.3]
sc = run("files", "16 50 2 2", CTX + [b"\x1b[B", b"\x1b[C", 0.3], pre=OWPRE,
         extra=OWX, end=None)
check("right-click offers Open With: the apps that open the type, the "
      "default marked, then Other…",
      sc.find("Image Viewer (default)") is not None and
      sc.find("Text Editor (hvi)") is not None and
      sc.find("Other…") is not None, sc)
check("and Open Terminal Here", sc.find("Open Terminal Here") is not None, sc)
sc = run("files", "16 50 2 2", CTX, pre=OWPRE + "\nDT_FBOPENWITH=0\n"
         "DT_FBTERMHERE=0", extra=OWX, end=None)
check("both can be taken off the menu in Control Panel > Files",
      sc.find("Open With") is None and sc.find("Terminal Here") is None, sc)
sc = run("files", "16 50 2 2", CTX + [b"\x1b[B", b"\x1b[C", b"\x1b[B",
                                      b"\r", 1.5],
         pre=OWPRE, extra=OWX, end=None)
check("choosing another opens it there, this once: hvi in a terminal",
      sc.find("┤ Terminal [a.ans] ├") is not None, sc)
sc = run("files", "16 50 2 2", [b"\x1b[B", b"\r", 1.5],
         pre=OWPRE, extra=OWX, end=None)
check("and the double-click is still the default's",
      sc.find("┤ a.ans ├") is not None and sc.find("┤ Terminal") is None, sc)
sc = run("files", "16 50 2 2", [b"\x1b[B", b"\r", 1.5],
         pre=OWPRE + '\nDT_OPENWITH="ans:hvi,imgview"', extra=OWX, end=None)
check("a type's default, set in File Types, is what a double-click does",
      sc.find("┤ Terminal [a.ans] ├") is not None, sc)
sc = run("files", "16 50 2 2", [b"\x1b[B", press(5, 6, 2), 0.3, b"h", 2.0]
         + [c.encode() for c in "pwd"] + [b"\r", 1.0],
         pre=OWPRE, extra=OWX, end=None)
check("Open Terminal Here starts a shell in the folder",
      sc.find(OWD) is not None and sc.find("┤ Terminal") is not None, sc)
sc = run("files", "16 50 2 2", [b"\x1b[B", press(5, 6, 2), 0.3, b"h", 2.0,
                                b"\x04", 1.5], pre=OWPRE, extra=OWX, end=None)
check("and ctrl-d closes its window, as any terminal's, not left showing 0",
      sc.find("┤ Terminal") is None and sc.find("exited 0") is None, sc)
sc = run("files", "16 50 2 2", [b"\x1b[B", press(5, 6, 2), 0.3, b"h", 2.0]
         + [c.encode() for c in "nosuchcommand"] + [b"\r", 0.8, b"\x04", 1.5],
         pre=OWPRE, extra=OWX, end=None)
check("even after a command that failed",
      sc.find("┤ Terminal") is None and sc.find("exited") is None, sc)
shutil.rmtree(OWD, True)
sc = cprun(reach("filetypes", ".ans"), extra=("imgview",))
check("File Types lists each type with its default, and how many others",
      sc.find(".ans  Image Viewer (+1)") is not None, sc)
sc = cprun(reach("filetypes", ".ans") + [b"\r", 0.4, b"\x1b[B", b"d", b"\r",
                                         0.4], extra=("imgview",))
check("its dialog makes another app the default, and the row says so",
      sc.find(".ans  Text Editor (hvi) (+1)") is not None, sc)
sc = run("about", "16 50 2 2", [])
check("About This Computer shows the module ABI, the uptime and who is in",
      re.search(r"module ABI \d+", sc.text()) and
      sc.find("Uptime: ") is not None and sc.find("Users: ") is not None,
      sc)
sc = run("calc", CW, [], pre="dt_launch screenshot", extra=("screenshot",))
check("Screenshot is a desk accessory: launching it asks what to take",
      sc.find("Screen ") is not None and sc.find("Area") is not None, sc)

# The clipboard keeps what was copied: the Clipboard desk accessory lists
# it newest first, makes any of it the clipboard again, pins it past the
# history's size, removes it, and keeps what is pasted into it.
CLD = tempfile.mkdtemp(prefix="hibr-clip-")
CLF = os.path.join(CLD, "clipboard.json")
CLPRE = 'DT_CLIPFILE=%s\ndt_clipset "first one"\ndt_clipset "second one"' % CLF
CBW = "14 50 2 2"
sc = run("clipboard", CBW, [], pre=CLPRE, end=None)
txt = sc.text()
check("what was copied is in the Clipboard, newest first, the current one "
      "marked", txt.find("second one") < txt.find("first one") and
      re.search(r"\d\d:\d\d  • second one", txt) is not None, sc)
sc = run("clipboard", CBW, [], pre=CLPRE, end=None, env={"DT_LANG": "xy"})
check("mirrored, each entry reads from the right: its time, mark, then text",
      re.search(r"second one  \u2022 \d\d:\d\d", sc.text()) is not None, sc)
sc = run("tasks", "16 70 2 2", env={"DT_LANG": "xy"})
head = [sc.row(r) for r in range(24) if "PID" in sc.row(r)]
check("and Task Manager's columns run the other way, PID last",
      head and head[0].index("PID") > head[0].index("Mem"), sc)
sc = run("clipboard", CBW, [b"\x1b[B", b"\r"], pre=CLPRE, end=None)
check("enter makes an older one the clipboard again, and the newest",
      re.search(r"\d\d:\d\d  • first one", sc.row(3)) is not None, sc)
sc = run("clipboard", CBW, [b"\x1b[B", b"p"], pre=CLPRE, end=None)
check("p pins it", re.search(r"\d\d:\d\d ★  first one", sc.text()), sc)
sc = run("clipboard", CBW, [b"\x1b[3~"], pre=CLPRE, end=None)
check("delete removes it", sc.find("second one") is None and
      sc.find("first one") is not None, sc)
sc = run("clipboard", CBW, [b"\x1b[200~kept for later\x1b[201~"], pre=CLPRE,
         end=None)
check("a paste into it is kept, at the top",
      re.search(r"• kept for later", sc.row(3)) is not None, sc)
sc = run("clipboard", CBW, [], pre="DT_CLIPFILE=%s" % CLF, end=None)
check("the history is there again in the next desktop",
      sc.find("kept for later") is not None and
      sc.find("first one") is not None, sc)
check("and its file is readable by its owner alone",
      os.path.exists(CLF) and oct(os.stat(CLF).st_mode & 0o777) == "0o600",
      oct(os.stat(CLF).st_mode) if os.path.exists(CLF) else "missing")
os.unlink(CLF)
sc = run("clipboard", CBW, [], pre=CLPRE + '\ndt_clippin 0\nDT_CLIPMAX=1\n'
         'dt_clipset "third"\ndt_clipset "fourth"', end=None)
check("past its size the oldest go, but never a pinned one",
      sc.find("fourth") is not None and sc.find("third") is None and
      sc.find("second one") is None and sc.find("first one") is not None, sc)
shutil.rmtree(CLD, True)

# Sheet: a spreadsheet whose formulas are hibr, run under --plan unless the
# sheet is trusted, kept in a db file one row per cell.
SHD = tempfile.mkdtemp(prefix="hibr-sheet-")


def shrun(feed, pre="", arg="", wait=0.6, env=None, until=None):
    p = os.path.join(S, "session.hibr")
    open(p, "w").write("%s\n. %s\n. %s\nSS_DIR=%s\n%s\ndt_open\n"
                       'dt_new "Sheet" 22 76 1 2 sheet %s\ndt_run\ndt_close\n'
                       % (load("console"), WM, appdir("sheet") + "/sheet.hibr",
                          SHD, pre, arg))
    tt = Term(p, env=env or {}, settle=1.0)
    tt.keys(list(feed) + [wait], settle=0.4)
    sc = tt.until(until) if until is not None else tt.screen()
    tt.quit(None, 0.5)
    return sc


# Where the window puts things, counted once: the toolbar, the formula bar,
# the column headers, and the screen row sheet row 1 is drawn on. 0.99.120
# added the toolbar and every one of these moved down by one -- which broke
# fourteen checks that each held a screen row of their own, so they hold a
# *sheet* row now and this is the only place that knows the difference.
SH_TBAR = 2
SH_FBAR = 3
SH_HROW = 4
SH_ROW0 = 5


def shcells(sc, n):
    """A sheet's row of cells by its own number, 1-based: after the gutter."""
    return sc.row(SH_ROW0 + n - 1)[8:76]


def shname(sc):
    """What the formula bar says the current cell is. Found from the fx mark
    rather than by column, because mirrored the gutter is at the right."""
    r = sc.row(SH_FBAR)
    i = r.index("\u0192x ")
    return r[i + 3:].split("\u2502")[0].strip()


def shk(t):
    return [c.encode() for c in t]


SHEET = SHD + "/untitled-1.hsheet"
sc = shrun(shk("5") + [b"\r"] + shk("hello") + [b"\r", b"\x1b[A", b"\x1b[D"],
           env={"DT_LANG": "xy"})
head = sc.row(SH_HROW)
check("mirrored, Sheet's column A is at the right and the columns run left",
      head.index(" A ") > head.index(" B ") > head.index(" C "), head)
check("and the left arrow moves to the next column, B",
      shname(sc) == "B2", sc)
os.remove(SHEET)
sc = shrun(shk("5") + [b"\r"] + shk("7") + [b"\r"] +
           shk('=math "A1+A2*1.5"') + [b"\r"])
check("Sheet takes numbers and a formula, and math works it out",
      "15.5" in shcells(sc, 3) and "5" in shcells(sc, 1), sc)
check("its formula bar names the cell", shname(sc) == "A4", sc)

# And with no OLDPWD in the session's environment, which is what a
# desktop started from the login screen has: PAM makes a fresh one, and
# OLDPWD is a thing a shell's own `cd` creates. `ss_recalc` runs every
# formula as `out=$(cd "$tmp" && env -i ... )`, and `strict vars` refused
# PWD and OLDPWD as globals the function was creating -- which does not
# merely log, it *fails* the cd, so the `&&` short-circuited and `out`
# came back empty: every formula in the spreadsheet, always (Gitea #251).
# No suite could see it, because every suite inherits the shell it was
# started from and a developer's shell has OLDPWD.
os.remove(SHEET)
sc = shrun(shk("5") + [b"\r"] + shk("7") + [b"\r"] +
           shk('=math "A1+A2*1.5"') + [b"\r"], env={"OLDPWD": None})
check("and it works a formula out in a session that has no OLDPWD",
      "15.5" in shcells(sc, 3), sc)
os.remove(SHEET)
sc = shrun(shk("5") + [b"\r"] + shk("7") + [b"\r"] +
           shk('=math "A1+A2*1.5"') + [b"\r"])
sc = shrun([], arg=SHEET)
check("what was typed is in the file, there when it is opened again",
      "15.5" in shcells(sc, 3) and "Sheet [Untitled]" in sc.text(), sc)
os.remove(SHEET)
sc = shrun(shk("1") + [b"\r"] + shk("2") + [b"\r"] + shk("3") + [b"\r"] +
           shk('=sum "${A1_A3[@]}"') + [b"\r"] + shk("=$((A4 * 10))") +
           [b"\r"])
check("a range is an array of its cells, and a formula can be an expansion",
      "6" in shcells(sc, 4) and "60" in shcells(sc, 5), sc)
os.remove(SHEET)
sc = shrun(shk("=touch %s/made" % SHD) + [b"\r", b"\x1b[A"])
check("a formula that would write is refused, and says what it would do",
      "#REFUSED" in shcells(sc, 1) and "would run touch" in sc.text() and
      not os.path.exists(SHD + "/made"), sc)
os.remove(SHEET)
sc = shrun(shk("=echo $A2") + [b"\r"] + shk("=echo $A1") + [b"\r"])
check("two formulas that need each other say #CYCLE",
      "#CYCLE" in shcells(sc, 1) and "#CYCLE" in shcells(sc, 2), sc)
os.remove(SHEET)
sc = shrun(shk("=touch %s/made" % SHD) + [b"\r"],
           pre="SS_TRUST=%s" % SHEET)
check("a trusted sheet's formulas run for real",
      os.path.exists(SHD + "/made") and "trusted" in sc.text(), sc)
os.remove(SHEET)
sc = shrun(shk("1") + [b"\r", b"\x1b[21~", b"\x1b[C", b"\x1b[C", b"\x1b[C",
                        b"t", 0.4, b"y", 0.4])
check("Sheet > Trust This Sheet asks first, and then trusts it",
      "trusted" in sc.text(), sc)
os.remove(SHEET)
sc = shrun(shk("2") + [b"\r"] + shk("=$((A1 * 3))") + [b"\r", b"\x1b[A",
                                                        b"\x1b[A", b"\x1b[21~",
                                                        b"\x1b[C", b"\x1b[C",
                                                        b"\x1b[C", b"a", 0.4,
                                                        b"\x1b[B", b"\x1b[B"])
check("a row inserted above moves the cells down, and the formula follows",
      shcells(sc, 1).strip() == "" and "2" in shcells(sc, 2) and
      "6" in shcells(sc, 3) and "=$((A2 * 3))" in sc.row(SH_FBAR), sc)
os.remove(SHEET)
sc = shrun(shk("a") + [b"\r"] + shk("1,5") + [b"\r", b"\x1b[21~", b"\x1b[C",
                                              b"e", 0.5, b"\x15"] +
           shk(SHD + "/out") + [b"\r", 0.5])
out = open(SHD + "/out.csv").read() if os.path.exists(SHD + "/out.csv") else ""
check("File > Export CSV writes what the cells show, quoting a comma",
      out == 'a\n"1,5"\n', repr(out))
os.remove(SHEET)
sc = shrun([press(SH_HROW, 17), drag(SH_HROW, 19), drag(SH_HROW, 21),
            release(SH_HROW, 21), 0.3])
check("a column's edge in the header drags it wider",
      sc.at(SH_HROW, 26) == "B" and sc.at(SH_HROW, 22) != "B", sc)
os.remove(SHEET)
sc = shrun(shk("1234.5") + [b"\r"] + shk("-7") + [b"\r", b"\x1b[A", b"\x1b[A",
                                                   b"\x1b[1;2B", b"\x1b[21~"] +
           [b"\x1b[C"] * 4 + [b"\x1b[B"] * 5 + [b"\x1b[C"] + [b"\x1b[B"] * 3 +
           [b"\r"])
check("Format > Number > 2 Decimals formats the whole selection",
      "1234.50" in shcells(sc, 1) and "-7.00" in shcells(sc, 2), sc)
os.remove(SHEET)
FMT = SHD + "/formats.hsheet"
subprocess.run([sx.HIBR, "-c", 'need db; h := db create "$1" r:int c:int '
                'v:str:4096 f:str:128; db insert $h -1 0 w=16 ""; '
                'db insert $h -2 0 "rows=100 cols=26 frz=1,0" ""; '
                'db insert $h 0 0 1234567.891 "nf=2;th"; '
                'db insert $h 1 0 0.256 "pct;nf=1"; '
                'db insert $h 2 0 -5 "cur"; '
                'db insert $h 3 0 -3 "cond=<0:bad"; '
                'db insert $h 4 0 3 "cond=<0:bad"; '
                'db insert $h 5 0 hi "al=r;b"; db close $h', "x", FMT],
               env=dict(os.environ, HIBR_MODPATH=tree("build/mods")),
               capture_output=True)
sc = shrun([], arg=FMT)
check("thousands, decimals, percent and currency show as asked",
      "1,234,567.89" in shcells(sc, 1) and "25.6%" in shcells(sc, 2) and
      "-$5" in shcells(sc, 3), sc)
check("a rule colours a negative number, and leaves a positive one alone",
      sc.style(SH_ROW0 + 3, 22)["fg"] != sc.style(SH_ROW0 + 4, 22)["fg"], (sc.style(SH_ROW0 + 3, 22),
                                                         sc.style(SH_ROW0 + 4, 22)))
check("bold, and alignment to the right, for text",
      sc.style(SH_ROW0 + 5, 22)["bold"] and shcells(sc, 6).rstrip().endswith("hi") and
      shcells(sc, 6).index("hi") > 10, sc)
if os.path.exists(SHEET):
    os.remove(SHEET)
sc = shrun(shk("150") + [b"\r"] + shk("50") + [b"\r", b"\x1b[A", b"\x1b[A",
                                               b"\x1b[1;2B", b"\x1b[21~"] +
           [b"\x1b[C"] * 4 + [b"\x1b[B"] * 7 + [b"\x1b[C"] + [b"\x1b[B"] * 2 +
           [b"\r", 0.5] + shk("> 100 good") + [b"\r", 0.5, b"\x1b[B",
                                                 b"\x1b[B"])
check("Colour by Value > Rule takes \"> 100 good\" and colours only what meets it",
      sc.style(SH_ROW0 + 0, 12)["fg"] != sc.style(SH_ROW0 + 1, 12)["fg"], (sc.style(SH_ROW0 + 0, 12),
                                                         sc.style(SH_ROW0 + 1, 12)))
sc = shrun([b"\x1b[6~", b"\x1b[6~", b"\x1b[6~"], arg=FMT)
check("a frozen top row stays while the rest scrolls",
      "1,234,567.89" in shcells(sc, 1) and "   1 " in sc.row(SH_ROW0) and
      "25.6%" not in sc.text(), sc)
# What 0.99.120 added, because the app could do all of this and said so
# only in its own source comment: "I have no idea how to use it this way,
# and there is no toolbar even, so I'm not sure how to use hibr inside of
# it?" (Gitea #150).
if os.path.exists(SHEET):
    os.remove(SHEET)
sc = shrun([])
check("an empty cell's formula bar says what a formula looks like",
      "= then hibr" in sc.row(SH_FBAR) and
      '=math "A1 * 1.2"' in sc.row(SH_FBAR), sc)
check("and the bar is marked as one, with the cell it is on",
      "\u0192x" in sc.row(SH_FBAR) and shname(sc) == "A1", sc)
sc = shrun(shk("12") + [b"\r", b"\x1b[A"])
check("a cell with something in it shows that instead of the hint",
      "12" in sc.row(SH_FBAR) and "= then hibr" not in sc.row(SH_FBAR), sc)
check("the toolbar is there, with the Format menu's own actions on it",
      all(b in sc.row(SH_TBAR) for b in ("B", "I", "L", "C", "R", "%")), sc)
# A button and the menu item run the same code, so clicking one is enough.
bcol = sc.row(SH_TBAR).index(" B ") + 1
sc = shrun(shk("12") + [b"\r", b"\x1b[A",
                        press(SH_TBAR, bcol), release(SH_TBAR, bcol), 0.4])
vcol = sc.row(SH_ROW0).index("12")
check("clicking a toolbar button formats the cell",
      sc.style(SH_ROW0, vcol)["bold"] and "12" in shcells(sc, 1), sc)
os.remove(SHEET)
# The fx button and Help > How Formulas Work are the same window.
fcol = sc.row(SH_TBAR).index("\u0192x")
sc = shrun([press(SH_TBAR, fcol), release(SH_TBAR, fcol), 0.5])
check("the fx button opens a window saying what a formula is",
      sc.find("A formula is = and then hibr.") is not None and
      sc.find("#REFUSED") is not None, sc)
check("and it fits, with its button clear of the text",
      sc.find("Close") is not None and
      sc.find("Close")[0] > sc.find("#REFUSED")[0], sc)
os.remove(SHEET)
# The tour: a sheet of worked examples, in a window of its own, with the
# formulas actually worked out -- and one the sandbox refuses, which is the
# only way Trust This Sheet means anything.
# The condition is a *worked-out* cell, not the window's title: the title is
# there as soon as the window is, and every formula in the tour forks a
# sandboxed hibr of its own, so under load the values arrive well after it.
# Waiting for the title would be a condition already true -- which waits for
# nothing, and is the trap a sweep of these pauses has to avoid (Gitea #163).
sc = shrun([b"\x1b[21~"] + [b"\x1b[C"] * 5 + [b"t", 0.4] +
           [b"\x1b[B", b"\r"],
           pre='DT_HERE=%s' % tree("programs/desktop"),
           until=lambda s: "Sheet [Tour]" in s.text() and "2026" in s.text())
check("Help > Open the Tour opens a sheet of worked examples, worked out",
      "Sheet [Tour]" in sc.text() and "42" in sc.text() and
      "2026" in sc.text(), sc)
tourcsv = open(tree("programs/desktop/examples") + "/tour.csv").read()
check("and the example it ships has a cell the sandbox refuses, so the "
      "refusal can be seen rather than only described",
      "=touch " in tourcsv and "#REFUSED" in tourcsv, tourcsv[:80])
shutil.rmtree(SHD, True)

# Write: markdown shown as it reads, the cursor's line raw; the toolbar
# and Format menu put the marks in; files open, save, and ask before an
# unsaved one closes.
WRDOC = """# Shopping list

Things to get **today**, and *maybe* tomorrow.

- milk
- [ ] bread
- [x] eggs

> remember the `coupon`
"""


def wrrun(feed, name="doc.md", text=WRDOC, extra=(), env=None, until=None):
    """A Write session, fed keys. `until` is what to wait for before the
    screen is taken -- text, or a function given the screen. The file
    dialog needs it: a key is never early, but a dialog opening, a folder
    listed and a window retitled all happen after the idle marker the key
    waited for, and a fixed pause instead is what failed under a gate
    running eighty pty sessions (Gitea #163)."""
    # Inside the suite's own folder, so the dialog's `..` is that folder
    # and not /tmp, whose size is whatever every other run left there:
    # 43,760 entries made the preview of `..` slow enough under the
    # sanitizers to fail five checks that had nothing wrong with them.
    d = tempfile.mkdtemp(prefix="hibr-write-", dir=S)
    f = os.path.join(d, name)
    open(f, "w").write(text)
    p = os.path.join(S, "session.hibr")
    open(p, "w").write("%s\n. %s\n. %s\n%s\ndt_open\n"
                       'dt_new "Write" 22 70 1 2 write "%s"\ndt_run\ndt_close\n'
                       % (load("console"), WM, appdir("write") + "/write.hibr",
                          "".join(". %s/%s.hibr\n" % (appdir(x), x) for x in extra),
                          f))
    tt = Term(p, env=env or {}, settle=1.0)
    tt.keys(list(feed), settle=0.3)
    sc = tt.until(until) if until is not None else tt.screen()
    tt.quit(None, 0.5)
    sc.saved = open(f).read() if os.path.exists(f) else None
    sc.dir = d
    return sc


# Right-to-left text in Write (#68): each row drawn in display order, the
# line one paragraph, while typing, clicks and the file stay logical. Plain
# text goes through the textarea widget, markdown through Write's own rows.
AR = "hello world\nسلام عليكم يا صديقي\n"
sc = wrrun([], name="doc.txt", text=AR)
check("Write draws a right-to-left line in display order, joined",
      sc.find("ﻲﻘﻳﺪﺻ ﺎﻳ ﻢﻜﻴﻠﻋ ﻡﻼﺳ") is not None, sc)
shutil.rmtree(sc.dir, True)
sc = wrrun([b"\x1b[B", b"\x1b[C", b"\x1b[C", "ب".encode(), b"\x13"], text=AR)
check("typing in it goes in at the logical position, and saves as typed",
      sc.saved is not None and "سلبام " in sc.saved, sc.saved)
shutil.rmtree(sc.dir, True)
sc = wrrun([b"\x1b[<0;4;5M", b"\x1b[<0;4;5m", b"X", b"\x13"], text=AR)
check("a click on its leftmost letter lands on the line's last character",
      sc.saved is not None and "صديقXي" in sc.saved, sc.saved)
shutil.rmtree(sc.dir, True)
sc = wrrun([], text="top\n**مرحبا** يا *صديق*\n")
check("styled right-to-left text is ordered with its marks gone",
      sc.find("ﻖﻳﺪﺻ ﺎﻳ ﺎﺒﺣﺮﻣ") is not None, sc)
shutil.rmtree(sc.dir, True)

sc = wrrun([])
check("Write shows markdown as it reads: the marks gone, the styles there",
      sc.find("Things to get today, and maybe tomorrow.") is not None and
      sc.find("• milk") is not None and sc.find("☐ bread") is not None and
      sc.find("☑ eggs") is not None and sc.find("remember the coupon")
      is not None, sc)
check("and the line the cursor is on shows its markdown, to edit exactly",
      sc.find("# Shopping list") is not None, sc)
check("its toolbar is there", sc.find(" B  I  S ") is not None, sc)

# The toolbar is mirrored with everything else (#68, ADR 0033): the buttons
# run from the right edge, and each is registered where it is drawn, so a
# click lands on the button a person sees rather than its unmirrored twin.
sc = wrrun([], text="plain\n", env={"DT_MIRROR": "on"})
check("mirrored, Write's toolbar runs from the right edge",
      sc.find(" S  I  B ") is not None and sc.find(" B  I  S ") is None, sc)
row = next((r for r in range(22) if " S  I  B " in sc.row(r)), -1)
col = sc.row(row).index(" B ") + 1
sc = wrrun([press(row, col), release(row, col), b"\x13"], text="plain\n",
           env={"DT_MIRROR": "on"})
check("and a click on a mirrored toolbar button puts its marks in",
      sc.saved is not None and "****" in sc.saved, (row, col, sc.saved, sc))
sc = wrrun([], text="top\n\n- one\n  - two *deep*\n    1. three\n\n"
           "> quoted\n> > twice\n\n| a | b |\n|---|---|\n| 1 | 2 |\n\n"
           "Under\n===\n")
check("the md module lays it out: nested lists keep their indent",
      sc.find("• one") is not None and sc.find("  • two deep") is not None and
      sc.find("    1. three") is not None, sc)
check("quotes in quotes, tables with their rules, setext headings",
      sc.find("│ quoted") is not None and sc.find("│ │ twice") is not None and
      sc.find("│ a │ b │") is not None and sc.find("│ 1 │ 2 │") is not None and
      sc.find("Under") is not None and sc.find("===") is None, sc)
sc = wrrun([b"\x1b[B", b"\x1b[B", b"\x1b[F", b" fresh"])
check("typing marks the document changed, in its title",
      sc.find("┤ Write [doc.md •] ├") is not None and
      sc.saved == WRDOC, sc)
sc = wrrun([b"\x1b[B", b"\x1b[B", b"\x1b[F", b" fresh"] + [b"\x1b[1;2D"] * 5
           + [press(2, 4), 0.3, b"\x13", 0.4])
check("Bold puts the marks round the selection, and ctrl-s saves",
      sc.saved is not None and "tomorrow. **fresh**" in sc.saved and
      sc.find("┤ Write [doc.md] ├") is not None, sc.saved)
sc = wrrun([b"\x1b[B", b"\x1b[B", press(2, 22), 0.3, b"\x13", 0.4])
check("H2 makes the line a heading", sc.saved is not None and
      "\n## Things to get" in sc.saved, sc.saved)
sc = wrrun([b"\x1b[B", b"\x1b[B", b"\x1b[1;2B", b"\x1b[1;2B", press(2, 29),
            0.3, b"\x13", 0.4], text="a\nb\nc\nd\n")
check("a list goes on every line of the selection",
      sc.saved == "a\nb\n- c\n- d\n", repr(sc.saved))
sc = wrrun([press(8, 3), 0.3, b"\x13", 0.4])
check("a click on a task's box ticks it", sc.saved is not None and
      "- [x] bread" in sc.saved, sc.saved)
sc = wrrun([b"x", b"\x1bw", 0.4])
check("closing with unsaved changes asks first",
      sc.find("Close without saving?") is not None, sc)
sc = wrrun([b"x", b"\x1bw", 0.4, b"n", 0.3])
check("and no keeps the window, and the changes",
      sc.find("┤ Write [doc.md •] ├") is not None, sc)
sc = wrrun([], name="plain.txt", text="**not bold** here\n- not a list\n")
check("a .txt is plain text: nothing rendered",
      sc.find("- not a list") is not None and sc.find("Plain text") is not None,
      sc)
sc = wrrun([b"\x1b[21~", b"\x1b[C", b"\x1b[C", b"\x1b[C", b"\x1b[C", b"f",
            0.4] + [c.encode() for c in "BREAD"] + [b"\r", 0.4])
check("Find… finds, whatever its case, and puts the cursor there",
      sc.find("line 6, column 12") is not None, sc)
sc = run("files", "16 50 2 2", [b"\x1b[B", b"\r", 1.0],
         pre="FB_DIR=%s" % sc.dir, extra=("write",), end=None)
check("Files opens a .md in Write", sc.find("┤ Write [doc.md] ├") is not None,
      sc)
# The file dialog: one for Open, Save As and Export everywhere, a folder's
# contents filtered by type, folders first, a preview of what is selected.
def pkdir():
    d = tempfile.mkdtemp(prefix="hibr-pick-")
    os.mkdir(os.path.join(d, "notes"))
    open(os.path.join(d, "readme.txt"), "w").write("plain text\nsecond\n")
    open(os.path.join(d, "data.db"), "w").write("x")
    return d


# Each of these waits for the thing rather than padding with a pause: the
# dialog's own border, the listing holding a name, the window retitled
# after a save. Four of them failed under a release gate running eighty pty
# sessions at once and passed every time alone, which is what a fixed pause
# after an idle marker looks like (Gitea #163).
OPEN = [b"\x1b[21~", b"\x1b[C", b"o"]
sc = wrrun(OPEN + [b"\x1b[B", b"\x1b[B"], until="# Shopping list")
check("Open shows the folder, its folders first, only the type's files",
      sc.find("┤ Open ├") is not None and sc.find("notes/") is None and
      sc.find("doc.md") is not None, sc)
check("and a preview of the file selected",
      sc.find("# Shopping list") is not None, sc)
d = pkdir()
sc = wrrun([b"\x1b[21~", b"\x1b[C", b"o"] +
           [c.encode() for c in d + "/readme.txt"] + [b"\r"],
           until="┤ Write [readme.txt] ├")
check("a path typed in the name opens that file, from anywhere",
      sc.find("┤ Write [readme.txt] ├") is not None, sc)
sc = wrrun([b"\x1b[21~", b"\x1b[C", b"o", b"\t", b"\t", b"\x1b[B",
            b"\t", b"\t", b"\t"] + [c.encode() for c in d] + [b"\r"],
           until="readme.txt")
check("the type chooses what is listed: Text shows the .txt",
      sc.find("readme.txt") is not None and sc.find("data.db") is None, sc)
sc = wrrun([b"x", b"\x1b[21~", b"\x1b[C", b"a", b"\x15"]
           + [c.encode() for c in d + "/fresh"] + [b"\r"],
           until="┤ Write [fresh.md] ├")
check("Save As writes where it is told, the type's extension added",
      os.path.exists(os.path.join(d, "fresh.md")) and
      sc.find("┤ Write [fresh.md] ├") is not None, sc)
sc = wrrun([b"\x1b[21~", b"\x1b[C", b"a", b"\x15"]
           + [c.encode() for c in d + "/fresh.md"] + [b"\r"],
           until="Replace it?")
check("and asks before replacing a file that is there",
      sc.find("fresh.md is there already. Replace it?") is not None, sc)
sc = wrrun([b"\x1b[21~", b"\x1b[C", b"e", b"\x15"]
           + [c.encode() for c in d + "/page"] + [b"\r"],
           until=lambda s: os.path.exists(os.path.join(d, "page.html")))
html = open(os.path.join(d, "page.html")).read() \
    if os.path.exists(os.path.join(d, "page.html")) else ""
check("Export writes the document as an HTML page, through the same dialog",
      "<h1>Shopping list</h1>" in html and "<strong>today</strong>" in html
      and '<input checked="" disabled="" type="checkbox"> eggs' in html, html[:300])
sc = run("dbase", "20 72 1 2", [b"\x1b[21~", b"\x1b[C", b"o",
                                b"\x15"] + [c.encode() for c in d]
         + [b"\r"], pre=DBPRE, end=None, until="data.db")
check("dBASE opens databases with the same dialog, filtered to .db",
      sc.find("┤ Open Database ├") is not None and
      sc.find("data.db") is not None and sc.find("readme.txt") is None, sc)
shutil.rmtree(d, True)
RSW = wrrun([])
shutil.rmtree(RSW.dir, True)
sc = run("write", "18 60 2 2", [c.encode() for c in "unsaved words"]
         + [b"\x1b[21~", 0.3, b"r", 2.5] + [c.encode() for c in " more"],
         end=None)
check("unsaved text in Write comes back after a restart",
      sc.find("unsaved words more") is not None, sc)

# Restart Desktop: the desktop execs the hibr installed now in the same
# process, and every window comes back -- a terminal's program still
# running with its screen, an app's state from the maps it keeps.
RESTART = [b"\x1b[21~", 0.3, b"r", 2.5]
sc = run("term", "14 52 2 4", feed=[1.5] + typed("X=42; seq 1 30; echo was $$")
         + RESTART + typed("echo X is $X now $$") + [0.6], end=None,
         wait=2.0)
txt = "\n".join(sc.row(r) for r in range(24))
was = re.search(r"was (\d+)", txt)
now = re.search(r"X is (\S*) now (\d+)", txt)
check("after a restart the terminal's shell is the same one, still running",
      was and now and was.group(1) == now.group(2) and now.group(1) == "42",
      sc)
check("and its screen came back with it, the old output still there",
      re.search(r"│30 ", txt) is not None, sc)
check("the window is where it was, once -- not opened again beside it",
      sc.find("┤ Term ├") == (2, 6) and txt.count("┤ Term ├") == 1, sc)
IN2 = [press(16, 55), release(16, 55), 0.3]
IN1 = [press(5, 20), release(5, 20), 0.3]
sc = run("term", "9 34 2 4", also=[("Terminal", "10 36 12 40", "term")],
         feed=[1.5] + IN1 + typed("echo one $$") + IN2 + typed("echo two $$")
         + RESTART + IN1 + typed("echo ONE $$") + IN2 + typed("echo TWO $$")
         + [0.6], end=None, wait=2.0)
txt = "\n".join(sc.row(r) for r in range(24))
pids = dict((k, re.search(r"│%s (\d+)" % k, txt))
            for k in ("one", "two", "ONE", "TWO"))
check("two terminals each keep their own shell across a restart",
      all(pids.values()) and
      pids["one"].group(1) == pids["ONE"].group(1) and
      pids["two"].group(1) == pids["TWO"].group(1) and
      pids["one"].group(1) != pids["two"].group(1), sc)
RSD = stdir()
sc = run("stickies", "12 40 3 6", feed=[c.encode() for c in "kept text"]
         + RESTART + [c.encode() for c in " more"], env=stenv(RSD), end=None)
check("a sticky keeps its text and its cursor across a restart",
      sc.find("kept text more") is not None and
      stnote(RSD) == "kept text more", sc)
shutil.rmtree(RSD, True)
sc = run("calc", CW, [b"1", b"2", b"+", b"3"] + RESTART + [b"="])
check("the calculator keeps what was typed, and finishes the sum after",
      sc.find("15") is not None, sc)
sc = run("snake", "18 42 2 4", [b"\x1b[B", 0.3] + RESTART, end=None)
check("a game in play comes back paused, not having run on in the restart",
      sc.find("paused -- p") is not None, sc)
HCOPY = os.path.join(S, "hibr-newer")
shutil.copy(sx.HIBR, HCOPY)
sc = run("calc", CW, [1.0], pre="HIBR=%s\nDT_NOTEMS=60000" % HCOPY,
         end=None)
check("a hibr replaced on disk is noticed, and the restart offered",
      sc.find("is installed") is not None, sc)
os.unlink(HCOPY)

# --- the Applications window -------------------------------------------------
#
# `apps` is the whole implementation and this window is a window around it
# (Gitea #219), so what is tested here is the *window*: a stub `apps` on
# PATH answers the JSON and records what it was asked, which needs no
# network and no catalogue, and makes "did the window ask for the right
# thing" a fact rather than an inference.

ASBIN = tempfile.mkdtemp(prefix="hibr-asbin-")
ASLOG = os.path.join(ASBIN, "asked")
CAT_JSON = json.dumps({"entries": {
    "apps/widget": {"id": "apps/widget", "name": "widget", "kind": "apps",
                    "title": "Widget", "version": "2.0", "installed": "",
                    "update": "0", "about": "A widget\n\nthe second paragraph."},
    # The package's own copy, at the version the catalogue offers. The
    # window drew a tick and a *word where the version goes* for one of
    # these, so a thing you installed showed a number and a shipped one
    # showed a word -- one column meaning two things -- and it offered
    # **Install** for something already present at the same version.
    "themes/slate": {"id": "themes/slate", "name": "slate", "kind": "themes",
                     "title": "Slate", "version": "1.0", "installed": "",
                     "where": "shipped", "have": "1.0",
                     "update": "0", "about": "A shipped theme"},
    # And one the package has at an older version than the catalogue
    # offers: installing over it is a real act, because a copy of your
    # own is read before the package's (ADR 0045), so the button is
    # there and says Update.
    "themes/older": {"id": "themes/older", "name": "older", "kind": "themes",
                     "title": "Older", "version": "2.0", "installed": "",
                     "where": "shipped", "have": "1.0",
                     "update": "0", "about": "A shipped theme, behind"},
    "apps/gadget": {"id": "apps/gadget", "name": "gadget", "kind": "apps",
                    "title": "Gadget", "version": "1.0", "installed": "1.0",
                    "update": "0", "about": "A gadget",
                    "parts": ["control-panel/gadgetset.hibr"]},
    # A copy the machine also has: read second, so it can never run. The
    # command has said so in its own column since 0.99.197 and the window
    # showed a healthy row until 0.99.202 (Gitea #244).
    "apps/doomed": {"id": "apps/doomed", "name": "doomed", "kind": "apps",
                    "title": "Doomed", "version": "1.0", "installed": "1.0",
                    "update": "0", "about": "A doomed one", "defunct": "1"},
    "apps/shady": {"id": "apps/shady", "name": "shady", "kind": "apps",
                   "title": "Shady", "version": "1.0", "installed": "1.0",
                   "update": "0", "about": "A shadowing one", "shadows": "1"},
}})


def asstub(answer=CAT_JSON, rc=0):
    """A stand-in `catalogue` that answers one thing and writes down every call.

    The answer goes in a file the stub cats, rather than into a `printf`
    in the script: JSON needs a literal backslash-n inside a string and
    printf turns one into a real newline, which is invalid JSON -- so the
    first version of this handed the window something that could not
    parse and every check failed for a reason that had nothing to do with
    the window."""
    open(os.path.join(ASBIN, "answer"), "w").write(answer)
    open(os.path.join(ASBIN, "catalogue"), "w").write(
        '#!/bin/sh\n'
        'printf "%%s\\n" "$*" >> %s\n'
        'case "$*" in *-j*) cat %s ;; esac\n'
        'exit %d\n' % (ASLOG, os.path.join(ASBIN, "answer"), rc))
    os.chmod(os.path.join(ASBIN, "catalogue"), 0o755)
    if os.path.exists(ASLOG):
        os.unlink(ASLOG)


def asked():
    return open(ASLOG).read() if os.path.exists(ASLOG) else ""


asstub()
ASENV = {"PATH": ASBIN + ":" + os.environ["PATH"]}
sc = run("catalogue", "21 76 2 2", env=ASENV, tries=150,
         until=lambda s: s.find("apps/widget") is not None)
check("the Applications window lists what `apps -j` answered, under a "
      "heading for its kind, and asks for JSON rather than parsing columns",
      sc.find("Applications") is not None and sc.find("widget") is not None
      and sc.find("gadget") is not None and "list -j" in asked(),
      ("heading=%s widget=%s gadget=%s asked=%r"
       % (sc.find("Applications"), sc.find("widget"), sc.find("gadget"),
          asked()), sc))
check("and the detail pane shows the entry's own prose, paragraphs kept",
      sc.find("A widget") is not None
      and sc.find("the second paragraph.") is not None, sc)
# What `catalogue installed` has said in its own columns since 0.99.197
# and this window showed none of: a copy the machine also has is read
# second and can never run, one that hides the shipped copy is not the
# same as a clean install, and an entry brings files with it that go when
# it does. Two views of one state that disagree about what is true is
# Gitea #237 one level up; this is #244.
def asmark(scr, name):
    """The mark on a row, read rather than spelled: the glyph set is the
    person's choice (DT_GLYPHSET) and a test that writes one in is a test
    of the default."""
    at = scr.find(" " + name + " ")
    if at is None:
        return None
    return scr.row(at[0])[:at[1]].rstrip()[-1:]


check("a copy the machine also has wears a different mark from one that "
      "works, since it is read second and can never run",
      asmark(sc, "doomed") is not None
      and asmark(sc, "gadget") == asmark(sc, "shady")
      and asmark(sc, "doomed") != asmark(sc, "gadget"),
      ("doomed=%r gadget=%r shady=%r" % (asmark(sc, "doomed"),
                                         asmark(sc, "gadget"),
                                         asmark(sc, "shady")), sc))
sc = run("catalogue", "21 76 2 2", env=ASENV, tries=150,
         ready=lambda s: s.find("gadget") is not None,
         feed=[b"\x1b[B"],
         until=lambda s: s.find("files with it") is not None
         or s.find("file with it") is not None)
check("and the detail pane says what an entry brought with it, so what "
      "removing it would take away is readable before you press it",
      sc.find("file with it") is not None, sc)
# A kind folds away, and a folded one still says how many are under it --
# which is the whole reason a shut group is worth reading. The entries go
# by their own name, since the kind is the line above them.
sc = run("catalogue", "21 76 2 2", env=ASENV, tries=150,
         ready=lambda s: s.find("widget") is not None,
         feed=[b"\x1b[D", b"\x1b[D"],
         until=lambda s: s.find("widget") is None)
# The count is read, not merely assumed: this check was named for it from
# the beginning and asserted only that the heading was still there, so a
# fold that lost its number would have passed. It sits in brackets beside
# the name since 0.99.194 -- out at the list's right edge it was a bare
# number in the column the versions below it fill, and read as a version
# (Gitea #220).
# Counted from the fixture rather than written in: this check was a
# literal "(2)" until an entry was added to the stub for another check
# and broke it, which is the same brittleness one level along.
NAPPS = len([e for e in json.loads(CAT_JSON)["entries"].values()
             if e["kind"] == "apps"])
check("a kind folds away, and the fold still says how many it holds",
      sc.find("Applications (%d)" % NAPPS) is not None
      and sc.find("widget") is None,
      ("want (%d), heading=%s" % (NAPPS, sc.find("Applications")), sc))

# The window keeps no list of kinds of its own: `apps list -j` sends them
# in its own declared order and the window groups by those. Both halves
# are read here at once, because one fixture can say both -- the entries
# arrive themes-first and `kinds` says apps-first, so Applications above
# Themes can only have come from the list; and `mods` is in it, which the
# window could not show at all until 0.99.194 (the kind reached `apps` in
# 0.99.191 and a second copy of the list here never heard, Gitea #220).
asstub(json.dumps({
    "entries": {
        "themes/meadow": {"id": "themes/meadow", "name": "meadow",
                          "kind": "themes", "title": "Meadow",
                          "version": "1.1", "installed": "", "update": "0",
                          "about": "A green theme"},
        "apps/widget": {"id": "apps/widget", "name": "widget",
                        "kind": "apps", "title": "Widget", "version": "2.0",
                        "installed": "", "update": "0", "about": "A widget"},
        "mods/hello": {"id": "mods/hello", "name": "hello", "kind": "mods",
                       "title": "Hello", "version": "0.3", "installed": "",
                       "update": "0", "about": "A module"}},
    "kinds": {"apps": 1, "themes": 1, "mods": 1}}))
sc = run("catalogue", "21 76 2 2", env=ASENV, tries=150,
         until=lambda s: s.find("Modules") is not None)
kr = [sc.find(x) for x in ("Applications (1)", "Themes (1)", "Modules (1)")]
check("the kinds and their order come from `apps`, and a module is one of "
      "them",
      all(x is not None for x in kr)
      and kr[0][0] < kr[1][0] < kr[2][0],
      ("rows=%r" % (kr,), sc))
asstub()

# Typing in the search narrows the list; the window does the filtering,
# since asking the command again per keystroke would be a fetch per
# keystroke.
# `/` rather than tab since 0.99.193: the search is the desktop's one
# search and `/` is how every window reaches it (widgets/search.hibr).
# `end=None`: the search bar has the keyboard at this point, so the
# teardown's own `qy` is *typed into it* -- the field read "gadqy" and
# matched nothing, which looks exactly like the search being broken. The
# same trap as a terminal window eating the quitting key, in a text box.
# The log is written down by every session, so it is cleared first: the
# count below is "this window asked once", and without the reset it would
# be counting what the two sessions before it asked as well.
asstub()
sc = run("catalogue", "21 76 2 2", env=ASENV, tries=150, end=None,
         ready=lambda s: s.find("widget") is not None, feed=[b"/", b"gad"],
         until=lambda s: s.find("widget") is None)
check("typing in the search narrows the list without asking again",
      sc.find("gadget") is not None and sc.find("widget") is None
      and asked().count("list -j") == 1, (sc, asked()))

# A row carries the version **and** where it is, in two columns, for
# every kind of row. The first version of the scope column put the word
# in the version's own cell for anything that was not the person's own,
# so a column meant "version" on some rows and "where" on others -- the
# one-value-two-questions trap, walked into while fixing an instance of
# it, and reported as "when it's user, it does not say user, only a
# version number".
# Only the first kind group is open when the window draws, so the
# Themes heading is pressed first: a check that asserted on `slate`
# without opening it was asserting on a row nothing had drawn.
asstub()
sc = run("catalogue", "24 84 2 2", env=ASENV, tries=150,
         ready=lambda s: s.find("Themes") is not None,
         feed=[lambda s: press(*s.find("Themes"))],
         until=lambda s: s.find("slate") is not None)
srow = [sc.row(r) for r in range(sc.rows) if "slate" in sc.row(r)]
check("a shipped row carries its version and the word, not one in place "
      "of the other",
      len(srow) == 1 and "1.0" in srow[0] and "shipped" in srow[0], srow)
check("and the column headings name both",
      sc.find("VERSION") is not None and sc.find("WHERE") is not None, sc)

# Install is offered only when pressing it would do something. What the
# package already provides at the very same version is not yours to
# install again, which is what "why does it say shipped and allow me to
# install?" was about; one the catalogue has *newer* is a real act,
# because a copy of your own is read before the package's (ADR 0045).
sc = run("catalogue", "24 84 2 2", env=ASENV, tries=150,
         ready=lambda s: s.find("Themes") is not None,
         feed=[lambda s: press(*s.find("Themes")),
               lambda s: press(*s.find("slate"))],
         until=lambda s: s.find("Read It") is not None)
check("a shipped thing at the same version offers no Install",
      sc.find("Install") is None and sc.find("Read It") is not None, sc)
sc = run("catalogue", "24 84 2 2", env=ASENV, tries=150,
         ready=lambda s: s.find("Themes") is not None,
         feed=[lambda s: press(*s.find("Themes")),
               lambda s: press(*s.find("older"))],
         until=lambda s: s.find("Update") is not None)
check("and one the catalogue has newer offers Update instead",
      sc.find("Update") is not None and sc.find("Install") is None, sc)

# And the buttons ask the command for the thing the selection is on.
asstub()
sc = run("catalogue", "21 76 2 2", env=ASENV, tries=150,
         ready=lambda s: s.find("widget") is not None, feed=[b"i"],
         until=lambda s: "install" in asked())
check("Install asks the command to install the selected id, by id",
      "install apps/gadget" in asked() or "install apps/widget" in asked(),
      asked())

# And what it installed is on the hibr menu at once, not at the next
# start (Gitea #267). The stand-in places a real app where the catalogue
# would, in the session's own data folder, and the window's own call to
# dt_rescan is the only thing that can find it: this session has no
# control socket, so the command telling the desktop reaches nothing.
# Driven a step at a time, since the menu is opened only once the window
# has listed again -- the reload that follows a finished job.
open(os.path.join(ASBIN, "catalogue"), "w").write(
    '#!/bin/sh\n'
    'printf "%%s\\n" "$*" >> %s\n'
    'case "$*" in *install*)\n'
    '  mkdir -p "$XDG_DATA_HOME/hibr/apps"\n'
    "  echo 'command -v dt_app > /dev/null && dt_app probe \"Widget Probe\" 8 30'"
    ' > "$XDG_DATA_HOME/hibr/apps/probe.hibr" ;;\n'
    'esac\n'
    'case "$*" in *-j*) cat %s ;; esac\n'
    'exit 0\n' % (ASLOG, os.path.join(ASBIN, "answer")))
if os.path.exists(ASLOG):
    os.unlink(ASLOG)
rsp = os.path.join(S, "session.hibr")
open(rsp, "w").write("%s. %s\n. %s\ndt_open\ndt_new \"Catalogue\" 21 76 2 2 catalogue\n"
                   "dt_run\ndt_close\n" % (load("console"), WM, appdir("catalogue")
                                         + "/catalogue.hibr"))
rst = Term(rsp, env=dict({"DT_TICK": "60"}, **ASENV), settle=0.6)
rst.until(lambda s: s.find("widget") is not None, 150)
rst.keys([b"i"])
rst.until(lambda s: asked().count("list -j") >= 2, 150)
rst.keys([b"\x1b[21~", b"\x1b[B", b"\x1b[C"])
rst.until(lambda s: s.find("Widget Probe") is not None, 60)
sc = rst.screen()
rst.quit(b"\x1bqy", 1.0)
check("what Install placed is on the Applications menu without a restart",
      sc.find("Widget Probe") is not None, (asked(), sc))
asstub()

def asrow(sc, name):
    """Where a name sits **in the list**, as a click position.

    Not `sc.find(name)`: the detail pane shows the selected entry's id on
    the same rows the list uses, so a plain search for `widget` finds
    `apps/widget` in the right-hand pane first -- and a click there lands
    on whatever list row shares that line, which for the first attempt at
    this was the heading, folding the branch instead of selecting
    anything. The list is the left half, so the search is too."""
    for r in range(sc.rows):
        c = "".join(sc.g[r])[:44].find(name)
        if c >= 0:
            return r, c
    raise AssertionError("no %r in the list half" % name)


# The mouse. `dt_hit` answered its miss with `ret ""` and no return, and
# `ret` ends a function with status 0 -- so it reported a hit for every
# click ever made, and this window's own `_click`, written
# `tag := dt_hit ... && { case ...; return 0; }`, swallowed the lot.
# Measured as a click on a row doing nothing at all.
asstub()
sc = run("catalogue", "21 76 2 2", env=ASENV, tries=150,
         ready=lambda s: s.find("widget") is not None,
         feed=[lambda s: press(*asrow(s, "widget"))],
         until=lambda s: s.find("A widget") is not None)
check("a click on a row selects it, and the detail pane follows",
      sc.find("A widget") is not None, sc)

# The tree's marks come from widgets/tree.hibr, so every tree in the
# desktop has the same ones and a setting changes all of them at once.
check("a branch wears the open mark and its children the rules below it",
      sc.find("[-]") is not None and sc.find("\u251c\u2500") is not None, sc)

# One button, and it flips: Install when you have not got it, Uninstall
# when you have. The stub's gadget is installed and its widget is not.
check("the button says Install for what is not here and Uninstall for what is",
      sc.find("Install") is not None and sc.find("Uninstall") is None,
      (sc, "selection is widget, which is not installed"))
sc = run("catalogue", "21 76 2 2", env=ASENV, tries=150,
         ready=lambda s: s.find("gadget") is not None,
         feed=[lambda s: press(*asrow(s, "gadget"))],
         until=lambda s: s.find("Uninstall") is not None)
check("and the other way round for one that is installed",
      sc.find("Uninstall") is not None and sc.find("Install") is None, sc)

# The search itself (widgets/search.hibr), driven here because the
# Catalogue is the smallest window that has one. Nothing is drawn until
# `/` is pressed -- which is what the whole sweep is for -- and the Edit
# menu every bar carries is where the feature stays findable with
# nothing on screen saying so.
asstub()
sc = run("catalogue", "21 76 2 2", env=ASENV, tries=150,
         until=lambda s: s.find("widget") is not None)
def barrow(sc):
    """Which row the search bar is on, or None. Anchored on the window's
    own edge: a bare search for `/gad` finds `apps/gadget` in the detail
    pane first, which is the click-at-a-computed-column trap wearing a
    string."""
    for i, row in enumerate(sc.text().split("\n")):
        if re.search(r"\u2502/", row):
            return i
    return None


nobar = barrow(sc) is None
sc = run("catalogue", "21 76 2 2", env=ASENV, tries=150,
         ready=lambda s: s.find("widget") is not None, feed=[b"/", b"gad"],
         end=None, until=lambda s: s.find("widget") is None)
check("no search is drawn until `/` asks for one, and then it is a bar on "
      "the window's own bottom row",
      nobar and barrow(sc) is not None
      and "/gad" in sc.row(barrow(sc))
      and barrow(sc) > sc.find("gadget")[0], (nobar, barrow(sc), sc))
# F10 opens the bar, right moves to Edit, and its last row is Find.
sc = run("catalogue", "21 76 2 2", env=ASENV, tries=150,
         ready=lambda s: s.find("widget") is not None,
         feed=[b"\x1b[21~", b"\x1b[C", b"\x1b[C"],
         until=lambda s: s.find("Find") is not None)
check("and the Edit menu every window has carries Find, naming the key",
      sc.find("Find") is not None and "/" in sc.row(sc.find("Find")[0]), sc)

# The wheel goes past the category the selection is in (Gitea #298). The
# window brought its view back to the selection on every frame, so with
# Applications open the wheel stopped a screen from the selection and the
# Themes heading below forty apps could not be reached without the keys.
BIGCAT = json.dumps({"entries": dict(
    [("apps/a%02d" % i, {"id": "apps/a%02d" % i, "name": "a%02d" % i, "kind": "apps",
                         "title": "App %02d" % i, "version": "1.0", "installed": "",
                         "update": "0", "about": "number %d" % i}) for i in range(40)]
    + [("themes/zz", {"id": "themes/zz", "name": "zz", "kind": "themes",
                      "title": "Last Theme", "version": "1.0", "installed": "",
                      "update": "0", "about": "the last"})])})
asstub(BIGCAT)
sc = run("catalogue", "21 76 2 2", env=ASENV, tries=150,
         ready=lambda s: s.find("a00") is not None,
         feed=[wheel(10, 10, up=False)] * 16,
         until=lambda s: s.find("Themes (1)") is not None)
listhalf = "\n".join("".join(sc.g[r])[:44] for r in range(sc.rows))
check("the wheel scrolls the Catalogue past the category the selection "
      "is in, to the next heading, leaving the selection where it was",
      "Themes (1)" in listhalf and "a39" in listhalf and "a00" not in listhalf
      and sc.find("number 0") is not None, sc)
asstub()

# The split of Gitea #220, from the desktop's own side: `hibr` ships no
# applications at all, so the folder `dt_apps` globs may not exist -- and
# an app you fetched into the data folder, which `apps install` writes
# to, has to be found anyway. That is the sharp edge the ticket named: a
# package that stops shipping a file must not take away the one somebody
# fetched. It falls out of the scan order, and the ticket asked for a
# check rather than a hope.
ADATA = tempfile.mkdtemp(prefix="hibr-bareapps-")
os.makedirs(os.path.join(ADATA, "hibr", "apps"))
open(os.path.join(ADATA, "hibr", "apps", "fetched.hibr"), "w").write(
    'app fetched "An app somebody fetched" 1.0\n'
    'dt_app fetched "Fetched" 6 24 once "F"\n'
    'fn fetched_draw(id, h, w, row, col) {\n'
    '\tconsole put -p "w$id" 1 1 "from the catalogue"\n'
    '}\n')
sc = run("catalogue", "21 76 2 2", env=dict(ASENV, XDG_DATA_HOME=ADATA),
         tries=150,
         pre='DT_APPDIRS=("%s/hibr/apps" "/nonexistent/shipped/apps")\ndt_apps'
             % ADATA,
         feed=[b"\x1b[21~", b"\x1b[B", b"\x1b[C"],
         until=lambda s: s.find("Fetched") is not None)
check("a desktop that ships no applications still finds one you fetched, "
      "with no shipped folder there at all",
      sc.find("Fetched") is not None, sc)
shutil.rmtree(ADATA, True)

# A heading is not a thing to install, and the lint rule found that it
# was being treated as one: `cat_at`'s three misses each ended in
# `ret ""; return 1`, where `ret` ends the function with status 0, so
# every caller's `|| ...` was dead. Measured against the old lines, the
# buttons on a heading read `Install  Read It  Refresh` and `i` asked the
# command `install ` -- an empty id. The window opens on the first entry,
# so `up` is what reaches a heading at all.
asstub()
sc = run("catalogue", "21 76 2 2", env=ASENV, tries=150,
         ready=lambda s: s.find("widget") is not None,
         feed=[b"\x1b[A", b"i"],
         until=lambda s: s.find("Nothing is selected") is not None)
check("a heading offers nothing to install, and says so when asked anyway",
      sc.find("Install") is None and sc.find("Refresh") is not None
      and "install" not in asked(), (sc, asked()))

# An `apps` older than this window has no -j, reads it as the text to
# search for, and succeeds while answering prose. Saying "it did not say
# why" there is true and useless.
asstub(answer="nothing in the index matches -j")
sc = run("catalogue", "21 76 2 2", env=ASENV, tries=150,
         until=lambda s: s.find("did not answer JSON") is not None)
check("an `apps` too old to know -j is named as the likely cause, rather "
      "than an empty list with no reason",
      sc.find("did not answer JSON") is not None, sc)
shutil.rmtree(ASBIN, True)


for f in os.listdir(D):
    p = os.path.join(D, f)
    if os.path.isdir(p):
        os.rmdir(p)
    else:
        os.unlink(p)
os.rmdir(D)
os.unlink(os.path.join(S, "session.hibr"))
os.rmdir(S)

report(697)
