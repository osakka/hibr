#!/usr/bin/env python3
"""A window left undrawn because its app's _dirty says nothing changed must
not show a setting's old value. Since dt_set stopped forcing a whole frame
(ADR 0050, dt_onset), only a setting the desktop registers an effect for,
or one the frame's own signatures read, redraws every window; any other
kept setting a _draw reads has to be in its app's own _dirty.

The names are not listed here: every app with a _dirty and a _draw is read
with `inspect`, its _draw and what that calls in its own file, and in the
desktop's own files one call away, for the kept settings it reads that no
whole frame covers. Each is set with dt_set on a window settled to being
left alone, and the next frame has to draw it. A name read that changes
nothing a settled window shows is exempt, with the reason; a name with
neither a value to try nor an exemption fails, so a new read cannot slip
past.

    python3 tests/stale.py
"""
import collections, glob, json, os, re, subprocess, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import screen
from screen import Term, check, report, load, tree, scratch

HERE = tree("programs/desktop")
WM = os.path.join(HERE, "desktop.hibr")
MODS = screen.MODS or tree("build/mods")
ENV = dict(os.environ, HIBR_MODPATH=MODS)


def hibr(code, *args):
    return subprocess.run([screen.HIBR, "-c", code, "x"] + list(args),
                          capture_output=True, text=True, env=ENV).stdout


def inspect(req, path):
    return json.loads(hibr('need inspect; inspect %s "$1" -j' % req, path))


# The settings the desktop keeps: its own list, and every dt_keep.
KEPT = set(hibr('. "$1"; printf "%s\\n" "${DT_KEEP[@]}"', WM).split())
FILES = sorted(glob.glob(HERE + "/**/*.hibr", recursive=True))
for f in FILES:
    for line in open(f, errors="replace"):
        m = re.search(r"\bdt_keep\s+([A-Z][A-Z0-9_ ]*)", line)
        if m:
            KEPT |= set(m.group(1).split())

# What redraws every window already: a setting whose effect is a whole frame
# (dt_onset ... dt_setwhole), and what dt_alone's look reads.
WHOLE = set()
wmtext = "".join(open(f).read() for f in glob.glob(HERE + "/wm/*.hibr"))
for m in re.finditer(r"^dt_onset ((?:\\\n|[^\n])*)$", wmtext, re.M):
    ws = m.group(1).replace("\\\n", " ").split()
    if ws[-1] == "dt_setwhole":
        WHOLE |= set(ws[:-1])
look = re.search(r'sig="\$DT_FACE.*?look=\$sig', open(HERE + "/wm/draw.hibr").read(), re.S)
WHOLE |= set(re.findall(r"\$(DT_[A-Z0-9_]+)", look.group(0)))

# Where each function of the desktop's own is defined, for the one step a
# _draw takes out of its file (dt_imgm, dt_wallimgmode).
PLATFORM = [f for f in FILES if "/wm/" in f or "/widgets/" in f or "/lib/" in f]
DEF = {}
for f in PLATFORM:
    for line in open(f, errors="replace"):
        m = re.match(r"^(?:fn )?([a-z_][a-z0-9_]*)\s*\(", line)
        if m:
            DEF.setdefault(m.group(1), f)
INSPECTED = {}


def facts(path):
    """A file's functions, and for each the calls it makes and the
    globals it reads."""
    if path not in INSPECTED:
        fns = {d["name"] for d in inspect("functions", path)["functions"]}
        calls, reads = collections.defaultdict(set), collections.defaultdict(set)
        for c in inspect("calls", path)["calls"]:
            if c.get("owner"):
                calls[c["owner"]].add(c["name"])
        for v in inspect("vars", path)["vars"]:
            if v.get("owner") and v["access"] in ("read", "name") and not v["declared"]:
                reads[v["owner"]].add(v["name"])
        INSPECTED[path] = (fns, calls, reads)
    return INSPECTED[path]


def closure(path, start):
    fns, calls, _ = facts(path)
    seen, todo = set(), [start]
    while todo:
        fn = todo.pop()
        if fn in seen or fn not in fns:
            continue
        seen.add(fn)
        todo.extend(calls[fn])
    return seen


def drawreads(path, prefix):
    """The kept settings a window's _draw reads that no whole frame covers."""
    fns, calls, reads = facts(path)
    own = closure(path, prefix + "_draw")
    out = set()
    for fn in own:
        out |= reads[fn]
        for c in calls[fn]:
            if c not in fns and c in DEF:
                g = DEF[c]
                for x in closure(g, c):
                    out |= facts(g)[2][x]
    return sorted(n for n in out if n in KEPT and n not in WHOLE)


# Every window with a _dirty in the desktop.
APPS = {}
for f in FILES:
    if "/login/" in f:
        continue
    src = open(f, errors="replace").read()
    for m in re.finditer(r"^fn ([a-z0-9]+)_dirty\(", src, re.M):
        p = m.group(1)
        if re.search(r"^fn %s_draw\(" % p, src, re.M):
            APPS[p] = f

# How each is opened, beside a window that holds the focus: Files and the
# PDF Reader call themselves dirty while focused, as they should.
DIR = scratch("stale-dir")
os.makedirs(DIR, exist_ok=True)
for n in ("a.txt", "b.txt", "notes"):
    open(os.path.join(DIR, n), "w").write("x\n")
PIC = scratch("stale-pic.ppm")
open(PIC, "wb").write(b"P6\n2 2\n255\n" + bytes([200, 40, 40] * 4))
OPEN = {
    "files": 'dt_new "Files" 10 40 2 2 files "%s"' % DIR,
    "term": 'dt_new "Shell" 8 40 2 46 term',
    "about": 'dt_new "About This Computer" 12 44 2 90 about',
    "tasks": 'dt_new "Task Manager" 12 56 2 138 tasks',
    "pdfr": 'dt_new "PDF Reader" 10 40 16 2 pdfr',
    "diagram": 'dt_new "Diagram" 10 40 16 46 diagram',
}
# A window there is no way to open here, with the reason.
NOWIN = {
    "native": "a program's own window needs Blit's compositor; what its _draw "
              "reads of the settings (DT_NEWON, DT_FULLON, DT_NATREMEMBER) is "
              "where a new window opens, never what one shows",
}
# Two values for each name: whichever the setting does not hold is set.
VALUES = {
    "FB_ICONSIZE": ("single", "double"),
    "FB_ICONCOLOUR": ("classic", "amber"),
    "FB_BGDEF": ("Follow Theme", "Rose"),
    "FB_COVER": ("large", "small"),
    "FB_USECOVER": ("1", "0"),
    "FB_TITLE": ("name", "path"),
    "DT_WSN": ("3", "4"),
    "AB_SLOWMS": ("600000", "1"),
    "TK_BAR": ("1", "0"),
    "TK_FILTER": ("everyone", "mine"),
    "TK_SCANMS": ("600000", "1"),
    "DT_TERMBAR": ("1", "0"),
    "DT_TERMCOLOURS": ("theme", "terminal"),
    "DT_TERMTRANS": ("0", "30"),
    "DT_TERMTITLE": ("1", "0"),
    "DT_CURSOR": ("block", "bar"),
    "DT_WALLIMG": ("", PIC),
    "DT_WALLIMGMODE": ("follow", "half"),
    "DT_IMGMODE": ("auto", "half"),
    "DIA_SPLIT": ("38", "50"),
}
# A name a _draw reads that changes nothing a settled window shows.
EXEMPT = {
    ("term", "DT_TERMEND"): "read only once the program has ended, when "
                            "term_dirty already says drawn",
    ("term", "DT_TERMCLIP"): "what a program copies goes to the clipboard; no cell shows it",
    ("term", "DT_CURSOR_BLINK"): "only the focused terminal draws a cursor, and "
                                 "term_dirty draws a focused one while it blinks",
    ("*", "DT_CLIPKEEP"): "the clipboard's history (dt_clipset), which no window shows",
    ("*", "DT_CLIPMAX"): "the clipboard's history (dt_clipset), which no window shows",
    ("*", "DT_NOTECOUNT"): "a program's notification is the desktop's note (dt_notify), "
                           "not the window's cells",
    ("*", "DT_NOTELIST"): "a program's notification is the desktop's note (dt_notify), "
                          "not the window's cells",
}

CASES, LOST = [], []
for p, f in sorted(APPS.items()):
    if p in NOWIN:
        continue
    names = [n for n in drawreads(f, p) if (p, n) not in EXEMPT and ("*", n) not in EXEMPT]
    if names and p not in OPEN:
        LOST.append("%s (%s): no way to open it here" % (p, os.path.relpath(f, HERE)))
        continue
    for n in names:
        if n not in VALUES:
            LOST.append("%s: %s has no value to try and no exemption" % (p, n))
            continue
        CASES.append((p, n) + VALUES[n])
check("every window with a _dirty is tried, and every kept setting its _draw "
      "reads has a value to try or a reason it shows nothing",
      not LOST and len(CASES) > 0, "\n".join(LOST) or "no cases")

CASEF = scratch("stale-cases")
RES = scratch("stale-res")
MARK = scratch("stale-do")
with open(CASEF, "w") as fh:
    for c in CASES:
        fh.write("|".join(c) + "\n")
opens = "\n".join(OPEN[p] for p in sorted(OPEN))
SESSION = r'''CS_SHOW=0
AB_SLOWMS=600000
TK_SCANMS=600000
TW_CMD=(sleep 600)
%s. %s
DT_STATON=1
DT_APPDIRS+=("$DT_HERE/apps")
dt_apps
DT_SYSDIRS+=("$DT_HERE/system")
dt_sysapps
dt_open
fn st_draw(id, h, w, row, col) { :; }
dt_app st "Focus" 4 20
%s
dt_new "Focus" 4 20 16 90 st
fn st_n(ap, why) { ret "${DT_STAT["y:$ap:$why"]:-0}"; }
# One setting on one app's settled window: how many frames left it alone
# before (1 of 1 settled), and after dt_set, whether it was drawn.
fn st_one(ap, name, a, b) {
	local -n cur=$name
	local was=$cur val=$a l0 l1 l2 i=0
	[ "$was" = "$a" ] && val=$b
	while [ "$i" -lt 12 ]; do
		l0 := st_n "$ap" left
		dt_draw
		l1 := st_n "$ap" left
		[ "$l1" != "$l0" ] && break
		i=$((i + 1))
	done
	dt_set "$name" "$val"
	dt_draw
	l2 := st_n "$ap" left
	echo "$ap $name $((l1 - l0)) $((l2 - l1))" >> %s
	dt_set "$name" "$was"
	dt_draw
	dt_draw
}
# One case a pass, so no pass holds the desktop: the cases, then "done".
ST_CASES=()
st_go() {
	local ap name a b
	if [ -f %s ]; then
		command rm -f %s
		mapfile -t ST_CASES < %s
		ST_CASES+=(done)
	fi
	[ "${#ST_CASES[@]}" -gt 0 ] || { dt_want 100; return 0; }
	if [ "${ST_CASES[0]}" = done ]; then
		echo done >> %s
	else
		IFS='|' read -r ap name a b <<< "${ST_CASES[0]}"
		st_one "$ap" "$name" "$a" "$b"
	fi
	ST_CASES=("${ST_CASES[@]:1}")
	dt_want 30
}
dt_ticker st_go
dt_run
dt_close
''' % (load("build/mods/console.so"), WM, opens, RES, MARK, MARK, CASEF, RES)
p = scratch("stale.hibr")
open(p, "w").write(SESSION)
if os.path.exists(RES):
    os.unlink(RES)
t = Term(p, rows=40, cols=200, settle=1.0, env={"DT_BARTIME": "12:00"})
t.collect(1.0)
open(MARK, "w").write("go\n")
deadline = time.time() + 120
while time.time() < deadline:
    t.send(b"\x1b[I", settle=0.1, collect=0.2)
    if os.path.exists(RES) and open(RES).read().endswith("done\n"):
        break
lines = open(RES).read().split("\n") if os.path.exists(RES) else []
got = [l.split() for l in lines if l and l != "done"]
unsettled = ["%s %s" % (g[0], g[1]) for g in got if g[2] != "1"]
stale = ["%s %s" % (g[0], g[1]) for g in got if g[3] != "0"]
check("each window was left alone on the frame before the setting changed",
      len(got) == len(CASES) and not unsettled,
      "%d of %d run; never left: %s" % (len(got), len(CASES), unsettled))
check("a kept setting a window's _draw reads draws it on the next frame, "
      "though no whole frame is forced",
      len(got) == len(CASES) and not stale, "left showing the old value: %s" % stale)
t.close()
report(4)
