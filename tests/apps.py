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
from screen import Term, check, report, press, release, drag, wheel, load, tree, expect

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
    """Which directory has a.hibr -- APPS searched recursively, the same
    as dt_apps' own **/*.hibr glob (an app can live in a subfolder of its
    own, which is what makes it a submenu); DA flat, the same as da_apps'
    own deliberately non-recursive scan."""
    if os.path.exists(os.path.join(DA, a + ".hibr")):
        return DA
    for root, _dirs, files in os.walk(APPS):
        if a + ".hibr" in files:
            return root
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
        "%s. %s\n%s%s\ndt_open\ndt_new \"%s\" %s %s\n%s"
        "dt_run\ndt_close\n"
        % (load(*mods), WM, src, pre, app.title(), win, app,
           more + ("dt_raise 1\n" if also else "")))
    t = Term(p, env=dict({"DT_TICK": "60"}, **(env or {})), settle=0.6)
    # The desktop says when its frame is drawn, but a terminal window's
    # program starts on its own time: a key sent the moment the window is
    # up can reach a shell that has not yet set its trap. Give it the time
    # the old fixed start used to give everything.
    if "term" in mods:
        t.collect(0.8)
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
# Panels folder). Panes are loaded from examples/desktop/control-panel and
# listed in two groups under headings -- the desktop's own under System,
# then one pane per app under Apps -- each sorted by title without regard
# to case, the same trick dt_appnames uses for apps. Verified once below,
# not assumed, and ORDER mirrors it exactly.

rc, out, err = cli("panel")
check("with no panes loaded, it says so rather than pretending",
      rc == 0 and out == "no panes registered", out)

CP = tree("examples/desktop/control-panel")
CPLOAD = ('. %s\n. %s/panel.hibr\nCP_PANEDIRS+=("%s")\ncp_panes\n'
          % (WM, APPS, CP))
out = subprocess.run([sx.HIBR, "-c", CPLOAD + 'for p in "${CP_PANE_LIST[@]}"; '
                      'do echo "$p ${CP_PANES[$p]["group"]}"; done'],
                     capture_output=True, text=True).stdout.split("\n")
ORDER = [l.split()[0] for l in out if l.strip()]
GROUP = dict(l.split() for l in out if l.strip())
check("panes register and sort by title within their group, not load order",
      ORDER == ["datetime", "displays", "keyboard", "mouse",
                "appearance", "control_strip", "desktop",
                "filetypes", "notify", "shortcuts", "windows",
                "abouthibr", "filesview", "taskmgr", "terminal"], out)
check("Hardware first, then the desktop's own panes, then one per app",
      [GROUP[n] for n in ORDER] ==
      ["hardware"] * 4 + ["system"] * 7 + ["app"] * 4, out)

PW = "22 58 2 2"
PANEL = ("panel", PW)
TICK = "DT_TICK=200"
CPANES = 'CP_PANEDIRS+=("%s")\ncp_panes' % CP

# Absolute screen coordinates for PW's geometry (row 2, col 2): a pane's
# own first content row is at 3, its dropdown/checkbox column at 47 --
# derived once here from CP_LISTW and the window's body width, rather than
# copied by eye into every check below. BODYCOL is a body-shape pane's own
# left content column (CP_LISTW + 1, plus the window's own left edge) --
# where Wallpaper's own file list starts, not LISTCOL, which is the
# picker's own sidebar column to its left.
R0, VALCOL = 3, 47
LISTCOL = 5
BODYCOL = 20
TITLE = {"appearance": "Appearance", "control_strip": "Control Strip",
         "datetime": "Date & Time", "desktop": "Desktop",
         "displays": "Displays", "filetypes": "File Types",
         "keyboard": "Keyboard", "mouse": "Mouse",
         "shortcuts": "Shortcuts", "notify": "Notifications",
         "windows": "Windows", "abouthibr": "About hibr",
         "filesview": "Files", "taskmgr": "Task Manager",
         "terminal": "Terminal"}


def prow(name):
    """Which screen row a pane's own name sits at in the picker list: under
    the Hardware heading, and under Desktop and Apps as well further down."""
    heads = ["hardware", "system", "app"]
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
          post=""):
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
    sc = run(*PANEL, feed=feed, pre="%sexport XDG_CONFIG_HOME=%s\n%s%s\n%s\n%s"
             % (pre, d, tzline, TICK, CPANES, post), also=also, extra=extra,
             env=env, end=end)
    shutil.rmtree(d, True)
    return sc


sc = cprun()
check("the picker lists every pane, sorted by title",
      all(TITLE[n] in sc.row(prow(n)) for n in ORDER), sc)
check("under a Hardware heading, then Desktop, then Apps",
      "Hardware" in sc.row(R0) and
      "Desktop" in sc.row(prow("appearance") - 1) and
      "Apps" in sc.row(prow("abouthibr") - 1), sc)
check("the last pane fits above the window's bottom border",
      sc.row(prow("terminal")).count("│") >= 3, sc)
check("the first pane's own rows show on the right without entering it",
      sc.find(TITLE[ORDER[0]]) is not None and
      sc.find("Change…") is not None, sc)

# Every dropdown in every pane, walked the same way: open it by clicking its
# value, take the next choice with the keyboard, and see the value change.
# Each choice runs a pane's _drop or cp_set_* callback, and most of those were
# reached by nothing else -- the census listed them. Which rows are
# dropdowns is read from each pane's own _drop, and where each row sits from
# its own _rows. Date & Time is left out: its dialogs run sudo.
PANE_APPS = {"abouthibr": ("about",), "filesview": ("files",),
             "taskmgr": ("tasks",), "terminal": ("term",)}


def panefull(pane, extra=()):
    """A pane's rows as (kind, text, key, value), from its own _rows."""
    src = "".join(". %s/%s.hibr\n" % (appdir(a), a) for a in extra)
    out = subprocess.run(
        [sx.HIBR, "-c", CPLOAD + src +
         'n := %s_rows 1; i=0; while [ "$i" -lt "$n" ]; do '
         'echo "${CP[1][$i]["kind"]}|${CP[1][$i]["text"]}|'
         '${CP[1][$i]["key"]}|${CP[1][$i]["val"]}"; i=$((i + 1)); done'
         % pane], capture_output=True, text=True).stdout
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


DROPS = []
for pane in ORDER:
    if pane == "datetime":
        continue
    extra = PANE_APPS.get(pane, ())
    keys = dropkeys(pane)
    for i, (kind, text, key, val) in enumerate(panefull(pane, extra)):
        if kind == "set" and key in keys:
            DROPS.append((pane, extra, i, text, val))
for pane, extra, i, text, val in DROPS:
    for step in (1, 2):
        sc = cprun([b"\x1b[B"] * downs(pane) +
                   [press(R0 + i, VALCOL)] + [b"\x1b[B"] * step + [b"\r"],
                   extra=extra)
        now = brow(sc, text)
        if now != "" and val not in now:
            break
    check("%s's %s dropdown chooses a new value" % (TITLE[pane], text),
          now != "" and val not in now, sc)

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

sc = cprun([b"\x1b[B"] * downs("shortcuts"))
check("Shortcuts lists the desktop's actions, the Control Strip's among "
      "them, then each app's under a heading of its own",
      all(brow(sc, t) != "" for t in ("Close Window", "Detach", "Quit",
                                        "Cycle Windows", "Control Strip",
                                        "Control Panel")) and
      "alt-s" in brow(sc, "Control Strip") and brow(sc, "Apps") != "", sc)

# Clearing one: a ✕ beside every key that is set, and delete or backspace
# on the selected row.
check("a set shortcut has a ✕ to clear it, and an unset one has none",
      "✕" in brow(sc, "Quit") and "✕" not in brow(sc, "Control Panel"), sc)
qr = browi(sc, "Quit")
sc = cprun([b"\x1b[B"] * downs("shortcuts") +
           ([press(qr, sc.row(qr).index("✕"))] if qr else []))
check("clicking it clears the shortcut",
      brow(sc, "Quit") != "" and "q" not in brow(sc, "Quit").split() and
      "✕" not in brow(sc, "Quit"), sc)
sc = cprun(reach("shortcuts", "Close Window") + [b"\x1b[3~"])
check("delete on a selected row clears it",
      brow(sc, "Close Window") != "" and "ctrl-w" not in brow(sc, "Close Window"),
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
      out == "f10 ctrl-w [] []", out)

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
sc = cprun([b"\x1b[B"] * downs("shortcuts"), pre="DT_APPKEY[panel]=q\n")
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

sc = cprun(DOWN_APP + [b"\x1b[C", b"\x1b[C"])
check("right enters the pane, and a second right cycles its first row",
      "neon" in brow(sc, "Theme") and sc.find("midnight") is None, sc)
sc = cprun(DOWN_APP + [b"\t"] + [b"\x1b[D"] * 6)
check("tab enters it too, and left cycles the other way, round to the last",
      "slate" in brow(sc, "Theme"), sc)
sc = cprun(DOWN_APP + [b"\t", b"\t", b"\x1b[C"])
check("a second tab leaves the pane, back to moving the picker",
      sc.find("slate") is None and
      sc.find(TITLE[ORDER[downs("appearance") + 1]]) is not None, sc)

sc = cprun(DOWN_APP + [press(R0, VALCOL)])
check("clicking the dropdown's own cell opens a real popup of choices",
      sc.find("slate") is not None and sc.find("dracula") is not None, sc)
sc = cprun(DOWN_APP + [press(R0, VALCOL), press(R0 + 3, VALCOL + 3)])
check("choosing one there applies it, the same as cycling would",
      sc.find("dracula") is not None and sc.find("midnight") is None, sc)
sc = cprun(DOWN_APP + [press(R0, VALCOL)])
check("the popup offers black, neon, phosphor and amber too",
      all(sc.find(t) is not None for t in ("black", "neon", "phosphor",
                                            "amber")), sc)
sc = cprun(DOWN_APP + [press(R0, VALCOL), press(R0 + 9, VALCOL + 3)])
check("and choosing phosphor applies it",
      sc.find("phosphor") is not None and sc.find("midnight") is None, sc)

sc = cprun(DOWN_APP + [b"\x1b[C", b"\x1b[B", b"\x1b[C"])
check("the wallpaper glyph changes, and the desktop follows",
      sc.at(0, 78) != "·" and sc.at(23, 60) == "░", sc)

# Wallpaper -- #2 -- browses for an image and previews it, in a window of
# its own opened from Appearance's "Wallpaper Image…" row, reusing files.hibr's
# own fb_scan/fb_go/fb_path for the directory side of that rather than a
# picker built from scratch (the ticket's own explicit constraint). Its
# own $HOME, so what it lists is known: a non-image, a subdirectory, and
# the same 2x2 PNG fixture other img tests already use.
AWHOME = tempfile.mkdtemp(prefix="hibr-wp-home-")
shutil.copy(os.path.abspath("tests/img-2x2.png"),
            os.path.join(AWHOME, "wall.png"))
open(os.path.join(AWHOME, "notes.txt"), "w").write("hi\n")
os.mkdir(os.path.join(AWHOME, "sub"))
shutil.copy(os.path.abspath("tests/img-2x2.png"),
            os.path.join(AWHOME, "sub", "deep.png"))
DOWN_WP = reach("appearance", "Wallpaper Image")
# The picker window sits centred, 20 by 64, so on this 24 by 80 screen its
# border is at row 2, column 8: a list row r is at screen row 2 + r, and
# the list itself starts one column in.
WPCOL = 10

sc = cprun(DOWN_WP)
check("Appearance offers the wallpaper image picker as a row of its own",
      brow(sc, "Wallpaper Image…") != "", sc)
check("and the way an image fills the screen, stretch by default",
      "stretch" in brow(sc, "Wallpaper Mode"), sc)
sc = cprun(reach("appearance", "Wallpaper Mode") + [b"\x1b[C"])
check("which cycles to scale",
      "scale" in brow(sc, "Wallpaper Mode"), sc)

sc = cprun(DOWN_WP + [b"\r"], env={"HOME": AWHOME}, extra=("files",))
check("it lists the home directory, through files.hibr's own scan",
      sc.find("wall.png") is not None and sc.find("notes.txt") is not None,
      sc)
check("nothing is selected yet, so there is no preview",
      sc.find("Select a .png") is not None, sc)
check("it is a window of its own, with Apply and Close buttons",
      sc.find("┤ Wallpaper ├") is not None and
      re.search(r" Apply .* Close ", sc.text()) is not None, sc)

# fb_scan lists directories first, then files sorted -- with one
# directory here, entries are 0 "..", 1 "sub/", 2 "notes.txt", 3 "wall.png".
sc = cprun(DOWN_WP + [b"\r", b"\x1b[B", b"\x1b[B"], env={"HOME": AWHOME},
           extra=("files",))
check("selecting the non-image leaves the preview cleared",
      sc.find("Select a .png") is not None, sc)

sc = cprun(DOWN_WP + [b"\r", b"\x1b[B", b"\x1b[B", b"\x1b[B"],
           env={"HOME": AWHOME}, extra=("files",))
check("selecting the .png shows a preview instead",
      sc.find("Select a .png") is None, sc)

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
           extra=("files",), pre=load("img"))
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
WPWHEEL = (
    '. %s/wallpaper.hibr\n'
    'wp_refresh() { :; }\n'
    'FB[1]["n"]=20; FB[1]["top"]=0; FB[1]["sel"]=0\n'
    'WPK[1]["vis"]=5\n'
    'wallpick_wheel 1 down; echo "d1=${FB[1]["top"]}"\n'
    'wallpick_wheel 1 down; wallpick_wheel 1 down\n'
    'wallpick_wheel 1 down; wallpick_wheel 1 down\n'
    'wallpick_wheel 1 down; echo "d6=${FB[1]["top"]}"\n'
    'i=0; while [ "$i" -lt 9 ]; do wallpick_wheel 1 up; i=$((i + 1)); done\n'
    'echo "u9=${FB[1]["top"]}"\n'
    % CP
)
out = subprocess.run([sx.HIBR, "-c", WPWHEEL], capture_output=True, text=True,
                     env=dict(os.environ, DT_ROWS="1")).stdout
check("the wheel scrolls the wallpaper picker, two rows at a time",
      "d1=2" in out, out)
check("and keeps scrolling without overrunning the list",
      "d6=12" in out, out)
check("but not above the first entry",
      "u9=0" in out, out)

# A click selects a row; a second click on the same, already-selected
# one is what actually navigates or applies. Neither did anything at all
# before this: the column check guarding the whole list gated on the
# list's own *width* rather than on where its body actually starts, so
# every click in it -- not just a repeated one -- was silently rejected,
# which is what "not navigatable by mouse" turned out to mean in full.
sc = cprun(DOWN_WP + [b"\r", press(R0 + 2, WPCOL)],
           env={"HOME": AWHOME}, extra=("files",))
check("a single click on a directory only selects it, not enters it",
      sc.find("wall.png") is not None, sc)
sc = cprun(DOWN_WP + [b"\r", press(R0 + 2, WPCOL), press(R0 + 2, WPCOL)],
           env={"HOME": AWHOME}, extra=("files",))
check("a second click on the same row enters the directory",
      sc.find("deep.png") is not None and sc.find("wall.png") is None, sc)

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
run(*PANEL, feed=DOWN_WP + [b"\r", b"\x1b[B", b"\x1b[B", b"\x1b[B", b"\r"],
    env={"HOME": AWHOME, "XDG_CONFIG_HOME": AWCONF}, pre=CPANES,
    extra=("files",))
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
         pre=CPANES + "\nDT_WALLIMG=%s/wall.png" % AWHOME, extra=("files",))
check("with no directory remembered yet, it starts at the wallpaper's own",
      sc.find("wall.png") is not None, sc)
cl = sc.find(" Close ")
sc = run(*PANEL, feed=DOWN_WP + [b"\r"] +
         ([press(cl[0], cl[1] + 1)] if cl else []),
         env={"HOME": "/nonexistent-for-this-check"},
         pre=CPANES + "\nDT_WALLIMG=%s/wall.png" % AWHOME, extra=("files",))
check("and Close closes the picker, leaving the Control Panel",
      cl is not None and sc.find("┤ Wallpaper ├") is None and
      sc.find("┤ Panel ├") is not None, sc)

# Once it has browsed somewhere else, that becomes the new starting
# point -- a directory distinct from both $HOME and the wallpaper's own,
# so this is not just the check above by coincidence.
AWCONF2 = tempfile.mkdtemp(prefix="hibr-wp-conf2-")
run(*PANEL, feed=DOWN_WP + [b"\r", b"\x1b[B", b"\r"],
    env={"HOME": AWHOME, "XDG_CONFIG_HOME": AWCONF2}, pre=CPANES,
    extra=("files",))
sc = run(*PANEL, feed=DOWN_WP + [b"\r"],
         env={"HOME": "/nonexistent-for-this-check",
              "XDG_CONFIG_HOME": AWCONF2},
         pre=CPANES, extra=("files",))
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
sc = cprun(DOWN_APP)
check("Appearance lists three shadows under Shadows and the buttons' under "
      "Dialog Buttons, all on",
      sc.find("Shadows") is not None and sc.find("Dialog Buttons") is not None
      and all("[x]" in brow(sc, t) for t in ("Windows", "Menus", "Menu Bar",
                                             "Shadow")), sc)
check("dialog buttons start filled",
      "filled" in brow(sc, "Style") and brow(sc, "Confirm Starts On") == "",
      sc)
sc = cprun(reach("appearance", "Menus") + [b"\r"])
check("menu shadow is its own setting, separate from window shadow",
      "[ ]" in brow(sc, "Menus") and "[x]" in brow(sc, "Windows"), sc)
sc = cprun(reach("appearance", "Menu Bar") + [b"\r"])
check("bar shadow is a third, separate setting again",
      "[ ]" in brow(sc, "Menu Bar") and "[x]" in brow(sc, "Menus"), sc)
sc = cprun(reach("appearance", "Shadow") + [b"\r"])
check("button shadow is its own setting, on until switched off here",
      "[ ]" in brow(sc, "Shadow") and "[x]" in brow(sc, "Menu Bar"), sc)
sc = cprun(reach("appearance", "Style") + [b"\x1b[C"])
check("the dialog button style steps to brackets",
      "brackets" in brow(sc, "Style"), sc)

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
CB = tempfile.mkdtemp(prefix="hibr-keys-")
run("notepad", "8 30 14 48", pre='DT_KEYS["copy"]="f5"',
    feed=[b"a", b"b", b"c", b"\x1b[1;2H", b"\x1bc", b"\x1b[15~", b"\x1b[F",
          b"\x1bv"], env={"XDG_CONFIG_HOME": CB}, end=None)
note = open(os.path.join(CB, "hibr", "notepad.txt")).read() \
    if os.path.exists(os.path.join(CB, "hibr", "notepad.txt")) else ""
check("Copy moved to f5 copies on f5, and alt-c no longer does",
      note == "abcabc\n", note)
shutil.rmtree(CB, True)
sc = run("notepad", "8 30 14 48", pre='DT_KEYS["copy"]="f5"',
         feed=[b"\x1b[21~", b"\x1b[C"], end=None)
check("the Edit menu shows the key Copy has now",
      sc.find("Copy       f5") is not None, sc)
sc = run("clock", "9 24 3 20", pre='DT_KEYS["menu"]="f9"',
         feed=[b"\x1b[20~"], end=None)
check("the Menu Bar key can move too: f9 opens the menu bar",
      sc.find("About hibr") is not None, sc)
sc = run("clock", "9 24 3 20", pre='DT_KEYS["menu"]="f9"\nDT_KEYS["menu2"]=',
         feed=[b"\x1b[21~", b"\x1b"], end=None)
check("and f10 and escape then do nothing of the menu bar's",
      sc.find("About hibr") is None, sc)

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
      out == "2 1 1 1 ctrl-w alt-tab []", out)
out = loadconf("DT_SETVER=1\nDT_TERMKEEP=0\n" + OLDKEYS)
check("one from 0.72 keeps its Shortcuts Win and gets the rest",
      out == "2 0 1 1 ctrl-w alt-tab []", out)
out = loadconf('DT_SETVER=1\nDT_KEYS["close"]=alt-x\nDT_KEYS["cycle"]=f6\n'
               'DT_KEYS["quit"]=ctrl-q\n')
check("a key changed from its old default is not touched",
      out.endswith("alt-x f6 [ctrl-q]"), out)
out = loadconf("DT_SETVER=2\nDT_TERMBAR=0\nDT_DRAGMOD=0\n")
check("and a 0.73 file is read as it is, choices and all",
      out.startswith("2 1 0 0 "), out)

# Themes are JSON files, read from the person's own folder first and then
# the bundled one; a file of the same name replaces a bundled theme, a new
# name adds one, and a file that is not a valid theme is left out whole.
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
    [sx.HIBR, "-c", CPLOAD + 'echo "${CP_THEMES[*]}"; cp_theme paper; '
     'echo "$DT_WALL $DT_SHADOW_PCT"; cp_theme midnight; echo "$DT_WALL"; '
     'cp_theme broken || echo refused'],
    env=dict(os.environ, XDG_CONFIG_HOME=TD),
    capture_output=True, text=True).stdout.split("\n")
shutil.rmtree(TD, True)
check("every bundled theme is listed, sorted, with a new one of the "
      "person's own among them",
      out[0] == "amber black dracula ember forest midnight neon paper "
      "phosphor slate zinc", out)
check("a theme of the person's own replaces the bundled one of that name",
      out[1] == "#010203 70", out)
check("the bundled ones still apply as before", out[2] == "#0d1b2a", out)
check("a broken file, a colour that is not one, and a shadow out of range "
      "are each left out", out[3] == "refused" and
      "broken" not in out[0] and "bad" not in out[0], out)

# An app's pane is always listed, and says so when its app is not loaded.
sc = cprun([b"\x1b[B"] * downs("abouthibr"))
check("About hibr's pane says so when About hibr is not loaded",
      sc.find("About hibr is not loaded") is not None, sc)
sc = cprun([b"\x1b[B"] * downs("abouthibr"), extra=("about",))
check("and once it is, holds its Refresh",
      brow(sc, "Refresh") != "" and "3000 ms" in brow(sc, "Refresh"), sc)

# The Terminal pane: its own rows only appear once term.hibr is loaded, the
# same as About hibr's Refresh above.
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
      sc.find("Menu Bar Spacing (2)") is not None and
      "●" in sc.row(sc.find("Menu Bar Spacing (2)")[0]), sc)
sc = cprun(reach("appearance", "Menu Bar Spacing") + [b"\x1b[C"])
check("and moving it widens the gap",
      sc.find("Menu Bar Spacing (3)") is not None and
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
FTPRE = "dt_handler jpg feh\n"

sc = cprun(DOWN_FT, pre=FTPRE)
check("File Types lists a registered handler, and offers to add another",
      sc.find("jpg") is not None and sc.find("feh") is not None and
      sc.find("Add File Type") is not None, sc)

sc = cprun(DOWN_FT + [b"\x1b[C", b"\r"], pre=FTPRE)
check("activating a registered extension opens it, pre-filled, for editing",
      sc.find("┤ File Type ├") is not None and
      sc.find("Extension: jpg") is not None and
      sc.find("Command:   feh") is not None, sc)

sc = cprun(DOWN_FT + [b"\x1b[C", b"\r", b"\x1b"], pre=FTPRE)
check("escape cancels the dialog, leaving the entry untouched",
      sc.find("┤ File Type ├") is None and sc.find("jpg") is not None, sc)

sc = cprun(DOWN_FT + [b"\x1b[C", b"\r", b"\x04"], pre=FTPRE)
check("ctrl-d deletes it",
      sc.find("┤ File Type ├") is None and sc.find("jpg") is None, sc)

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
      sc.find("jpg") is None, sc)
sc = cprun(DOWN_FT + [b"\x1b[C", b"\r"] + [b"\t"] * 4 + [b"\r"],
           pre=FTPRE)
check("tab past the fields, Save and Delete reaches Cancel, and enter on it "
      "closes the dialog with the entry kept",
      sc.find("┤ File Type ├") is None and sc.find("jpg") is not None, sc)
sc = cprun(DOWN_FT + [b"\x1b[C", b"\r"] + [b"\t"] * 3 + [b"\r"],
           pre=FTPRE)
check("and enter on Delete, one before it, deletes",
      sc.find("┤ File Type ├") is None and sc.find("jpg") is None, sc)

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
RSKIP = reach("desktop", "Redraw Skip")
sc = cprun(RSKIP)
skiprow = sc.find("Redraw Skip (0)")
check("the redraw-skip row draws as a slider, not a dropdown or checkbox",
      skiprow is not None and "●" in sc.row(skiprow[0]) and
      "▾" not in sc.row(skiprow[0]), sc)
sc = cprun(RSKIP + [b"\x1b[C", b"\x1b[C"])
check("right arrow on it increases the value, the marker moving with it",
      sc.find("Redraw Skip (2)") is not None, sc)
sc = cprun(RSKIP + [b"\x1b[C"] * 20)
check("it stops at the reasonable maximum rather than climbing forever",
      sc.find("Redraw Skip (9)") is not None, sc)

# Task Manager's own pane: Refresh, Scrollbar, and the order a new window
# starts in -- rows that appear only once Task Manager itself is loaded.
DOWN_TM = [b"\x1b[B"] * downs("taskmgr")
sc = cprun(DOWN_TM, extra=("tasks",))
check("Task Manager has a pane of its own: Refresh, Scrollbar, Sort By and "
      "Order", sc.find("Refresh") is not None and
      sc.find("1000 ms") is not None and sc.find("Scrollbar") is not None and
      sc.find("Sort By") is not None and sc.find("Order") is not None and
      "cpu" in sc.row(sc.find("Sort By")[0]) and
      "desc" in sc.row(sc.find("Order")[0]), sc)
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

sc = cprun(DOWN_APP + [press(R0, VALCOL - 10), press(R0, VALCOL - 10)])
check("a click in the pane's own body selects and a second click acts",
      "neon" in brow(sc, "Theme"), sc)

sc = cprun([press(prow("terminal") + 1, LISTCOL)])
check("clicking below the last pane in the picker does nothing",
      sc.find(TITLE[ORDER[0]]) is not None, sc)
sc = cprun([press(prow("shortcuts"), LISTCOL)])
check("clicking a pane in the picker shows it, the headings counted",
      brow(sc, "Close Window") != "", sc)
sc = cprun([press(R0, LISTCOL)])
check("and clicking a heading shows nothing new",
      sc.find("Change…") is not None, sc)

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


sc = run_img(os.path.abspath("tests/img-2x2.png"), BUILT_MODS)
check("a decodable picture is drawn, not an error message",
      sc.find("Cannot show") is None and
      sc.find("isn't installed") is None, sc)

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

expect(r"broken\.png is not a valid PNG$")
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
sc = run("term", "10 36 2 2", feed=[b"\x17\x17", 0.5], wait=1.6, end=None,
         pre=OD + "\nDT_TERMKEEP=1")
check("ctrl-w, which closes a window, reaches a terminal's program instead",
      "027" in sc.text() and sc.find("┤ Term") is not None, sc)
sc = run("term", "10 36 2 2", feed=[b"\x17", 0.5], wait=1.6, end=None,
         pre=OD + "\nDT_TERMKEEP=1\nDT_TERMCTRL=0")
check("unless Terminals Keep Ctrl+A-Z is off: then ctrl-w closes it too",
      sc.find("┤ Term") is None, sc)
sc = run("term", "10 36 2 2", wait=1.6, end=None,
         feed=[b"\x1b[21~", 0.3, b"\x1b[C", b"\x1b[C", b"\x1b[C", 0.3],
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
      sc.at(11, 44) == "█", sc)

sc = run(*TERM, feed=[wheel(8, 10)], wait=1.2, end=None,
         pre=LONG + '\nDT_ACTIVE="#2b6cb0"\nDT_IDLE="#8a8578"')
check("the scrollbar is drawn in the theme's colours: accent thumb, idle track",
      sc.at(11, 44) == "█" and sc.style(11, 44)["fg"] == "#2b6cb0" and
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

CLICK = ("TW_CMD=(/bin/sh -c 'stty raw -echo; "
         "printf \"\\033[?1000h\\033[?1006h\"; head -c 18 | cat -v; sleep 5')")
sc = run(*TERM, feed=[press(6, 10), release(6, 10)], pre=CLICK, wait=1.2, end=None)
check("a click reaches a program that asked for the mouse, where it landed",
      sc.find("^[[<0;8;4M^[[<0;8;4m") is not None, sc)

# --- glyphs ------------------------------------------------------------------
#
# Every glyph the desktop draws is named in wm/glyphs.hibr; the plain-ASCII
# set draws the same desktop for a terminal or a font without them.
sc = run("clock", "8 24 6 10", pre="DT_GLYPHSET=ascii")
check("in the ASCII glyph set a window is drawn in plain ASCII",
      sc.at(6, 10) == "+" and sc.at(7, 10) == "|" and sc.find("[ Clock ]") is not None,
      sc)
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
sc = run("clock", "9 24 3 20", feed=[1.5, b"\x1b[21~", 0.5, b"\x1b[C", 0.5])
check("while input still rebuilds them, so the menus open and move as before",
      sc.find("Edit") is not None and sc.find("Copy") is not None, sc)

# --- one clipboard, every app ----------------------------------------------
#
# Copy in one window, paste in another: a terminal and Note Pad share the
# desktop's clipboard both ways, a program in a terminal can set it with
# OSC 52, a paste from the machine the desktop runs on sets it too, and
# Files takes text as a new file.
NPW = "8 30 14 48"
CB = tempfile.mkdtemp(prefix="hibr-clip-")
run("notepad", NPW, also=[("Terminal", TW, "term")], pre=LONG, wait=1.2,
    feed=[press(3, 3), drag(3, 8), release(3, 8), b"\x1bc",
          press(16, 55), release(16, 55), b"\x1bv"],
    env={"XDG_CONFIG_HOME": CB}, end=None)
note = open(os.path.join(CB, "hibr", "notepad.txt")).read() \
    if os.path.exists(os.path.join(CB, "hibr", "notepad.txt")) else ""
check("text copied in a terminal pastes into Note Pad", "row 30" in note, note)
shutil.rmtree(CB, True)

CB = tempfile.mkdtemp(prefix="hibr-clip-")
os.makedirs(os.path.join(CB, "hibr"))
open(os.path.join(CB, "hibr", "notepad.txt"), "w").write("hello note\n")
sc = run("notepad", NPW, also=[("Terminal", TW, "term")], pre=SH, wait=1.2,
         feed=[0.5, press(15, 52), release(15, 52), b"\x1b[F", b"\x1b[1;2H",
               b"\x1bc", press(6, 10), release(6, 10), b"\x1bv", 0.5],
         env={"XDG_CONFIG_HOME": CB}, end=None)
check("and text selected in Note Pad pastes into a terminal's program",
      sc.find("sh> hello note") is not None, sc)
shutil.rmtree(CB, True)

OSC52 = "TW_CMD=(/bin/sh -c 'printf \"\\033]52;c;aGVsbG8=\\007\"; sleep 5')"
sc = run(*TERM, pre=OSC52, wait=1.2, end=None)
check("a program in a terminal that sets the clipboard sets it everywhere",
      b"\x1b]52;c;aGVsbG8=\x07" in sc.out, sc)
sc = run(*TERM, pre=OSC52 + "\nDT_TERMCLIP=0", wait=1.2, end=None)
check("unless Programs Set Clipboard is off",
      b"\x1b]52;c;aGVsbG8=\x07" not in sc.out, sc)

CB = tempfile.mkdtemp(prefix="hibr-clip-")
run("notepad", NPW, feed=[b"\x1b[200~hi\x1b[201~", b"\x1bv"],
    env={"XDG_CONFIG_HOME": CB}, end=None)
note = open(os.path.join(CB, "hibr", "notepad.txt")).read() \
    if os.path.exists(os.path.join(CB, "hibr", "notepad.txt")) else ""
check("a paste from the machine the desktop runs on becomes the clipboard",
      note == "hihi\n", note)
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
      sc.find("♥♥♥") is not None and sc.find("▀▀▀▀▀▀▀") == (20, 20), sc)

sc = run("bricks", BW, feed=[b"\x1b[D"])
check("the arrows move the bat, and the ball rides along",
      sc.find("▀▀▀▀▀▀▀") == (20, 17) and sc.at(19, 20) == "●", sc)

sc = run("bricks", BW, feed=[press(10, 36)])
check("a click puts the bat under it", sc.find("▀▀▀▀▀▀▀") == (20, 33), sc)

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
HPRE = "FB_DIR=%s" % HD

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
check("and the nine permission bits read as checkboxes, matching -rw-r--r--",
      sc.at(13, 25) == "x" and sc.at(13, 29) == "x" and
      sc.at(13, 33) == " " and
      sc.at(14, 25) == "x" and sc.at(14, 29) == " ", sc)

NP = os.path.join(HD, "note.txt")
os.chmod(NP, 0o644)
sc = run("files", FW, [press(5, 10, 2), press(12, 12), press(14, 29)],
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
H2PRE = "FB_DIR=%s" % HD2
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
    % (appdir("tasks") + "/tasks.hibr")
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

NPD5 = tempfile.mkdtemp(prefix="hibr-notepad-")
run("notepad", "12 40 2 2", feed=[b"a", b"\r", b"b", b"\x08", b"\x08"],
    env={"XDG_CONFIG_HOME": NPD5}, end=None)
check("backspace at the start of a line joins it to the one above",
      open(os.path.join(NPD5, "hibr", "notepad.txt")).read() == "a\n")
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
         pre="FB_DIR=%s" % HID)
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
sc = run(*TASKS, feed=[b"\x1b[21~", b"\x1b[C", b"i"], end=None)
check("the Task menu's View Details opens it for the selected row",
      sc.find("┤ Process Details ├") is not None, sc)

sc = cprun(reach("control_strip", "Shadow") + [b"\x1b[C"])
check("the Control Strip's shadow is switched from its pane",
      "[ ]" in brow(sc, "Shadow"), sc)
sc = cprun(reach("notify", "Position") + [b"\x1b[C"])
check("right on Notifications' Position steps it",
      "top-right" not in brow(sc, "Position"), sc)
sc = cprun(reach("notify", "Clear History") + [b"\r"], end=None)
check("Clear History runs and says so",
      sc.find("History cleared") is not None, sc)
sc = cprun(reach("abouthibr", "Refresh", ("about",)) + [b"\x1b[C"],
           extra=("about",))
check("right on About hibr's Refresh steps it",
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

for f in os.listdir(D):
    p = os.path.join(D, f)
    if os.path.isdir(p):
        os.rmdir(p)
    else:
        os.unlink(p)
os.rmdir(D)
os.unlink(os.path.join(S, "session.hibr"))
os.rmdir(S)

report(367)
