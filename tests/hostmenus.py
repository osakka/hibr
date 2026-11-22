#!/usr/bin/env python3
"""A hosted app's menus, context menu, lists and dialogs (ADR 0050, the
menus track): its _menus and _context run in its own process with the
builders the desktop has, what they build comes back pushed (`menus`,
`context`) and the desktop builds the same rows again; choosing a row runs
the app's own function in the app's process and one of the desktop's in the
desktop; a list of choices goes up built, its namer's words made where the
app is; a dialog's answer goes back to the app, which says whether it is
taken.

    python3 tests/hostmenus.py

The Calculator is compared with itself in-process, cell for cell, glyphs
and pens -- the bar, a menu open, a row chosen, its context menu -- in two
sessions, the hosted one never sourcing calc.hibr, so no in-process path
can stand in for it. A fixture app proves where each thing ran by the pid
its own code writes, and that a row acts on the window it was built for.
"""
import os, sys, time, statistics
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import screen
from screen import Term, check, report, load, tree, scratch

MOD = tree("build/mods/console.so")
WM = tree("programs/desktop/desktop.hibr")
CALC = tree("programs/desktop/apps/Accessories/calc.hibr")
SAID = scratch("hostmenus-said")
MFX = scratch("hostmenus-mfx.hibr")

# The fixture: its menus, a submenu, a tick that follows its window's
# state, a list with a namer, an asked dialog and a confirm, a context menu.
# Every action writes the pid of the process it ran in. It never names the
# request that opens a list, which its dt_droplist reaches for it.
open(MFX, "w").write(r'''command -v dt_app > /dev/null && dt_app mfx "Menus" 6 30
declare -gA MFX
fn mfx_draw(int id, int h, int w, int row, int col) {
	console put -p "w$id" 1 1 "n=${MFX[$id]-0}"
}
mfx_menus() {
	dt_menu "File"
	dt_item "Close" w dt_close_focused
	dt_editmenu
	dt_menu "Tools"
	dt_item "Bump" b mfx_bump "$DT_FOCUS"
	dt_mark "Odd" o $((${MFX[$DT_FOCUS]-0} %% 2)) mfx_bump "$DT_FOCUS"
	dt_sub "More" m
	dt_item "Deep" d mfx_say deep "$DT_FOCUS"
	dt_end
	dt_sep
	dt_item "Pick" p mfx_pick "$DT_FOCUS"
	dt_item "Ask" a mfx_ask "$DT_FOCUS"
	dt_item "Confirm" c mfx_conf "$DT_FOCUS"
	dt_dim "At $EPOCHREALTIME"
}
fn mfx_titlemenu(int id, int r, int c) {
	DT_POPNAMENEXT=mfx_name
	dt_droplist "$id" "$r" "$c" mfx_picked up top
}
fn mfx_context(int? id = "") {
	dt_menu "Mfx"
	dt_item "Here" h mfx_say here "$id"
}
fn mfx_say(what, id = "") {
	printf '%%s\t%%s\t%%s\n' "$$" "$what" "$id" >> %s
}
fn mfx_bump(int id) {
	MFX[$id]=$((${MFX[$id]-0} + 1))
	mfx_say bump "$id"
}
fn mfx_name(v) {
	ret "Word $v $$"
}
fn mfx_pick(int id) {
	DT_POPNAMENEXT=mfx_name
	DT_POPCURNEXT=b
	dt_droplist "$id" 1 1 mfx_picked a b c
}
fn mfx_picked(id, v) {
	mfx_say "picked $v" "$id"
}
fn mfx_ask(int id) {
	dt_ask "Name" "Name:" "" mfx_named
}
fn mfx_named(buf, id) {
	mfx_say "named $buf" "$id"
	[ "$buf" = ok ]
}
fn mfx_conf(int id) {
	dt_confirm "Sure?" mfx_yes
}
mfx_yes() {
	mfx_say yes "$DT_FOCUS"
}
''' % SAID)

SESSION = r'''%s. %s
dt_open
DT_NOTEMS=60000
%s
tst_go() {
	[ -f %s ] || { dt_want 100; return 0; }
	local c
	read -r c < %s
	command rm -f %s
	eval "$c"
	dt_want 30
}
dt_ticker tst_go
dt_run
dt_close
'''

# The hosted session: the Calculator and the fixture only registered, read
# and never run here.
HOSTED = r'''DT_LOADING=%s dt_app calc "Calculator" 16 24 once "x" fixed
DT_LOADING=%s dt_app mfx "Menus" 6 30
DT_LOADING=''' % (CALC, MFX)
INPROC = r'''DT_LOADING=%s . %s
DT_LOADING=''' % (CALC, CALC)


def session(name, body):
    p = scratch("hostmenus-%s.hibr" % name)
    mark = scratch("hostmenus-do-%s" % name)
    open(p, "w").write(SESSION % (load(MOD), WM, body, mark, mark, mark))
    term = Term(p, rows=30, cols=100, settle=0.6, env={"DT_BARTIME": "12:00"})
    term.mark = mark
    return term


def do(t, cmd):
    """Run cmd in t's desktop, from its ticker, and wait until it is taken."""
    open(t.mark, "w").write(cmd + "\n")
    deadline = time.time() + 5
    while os.path.exists(t.mark) and time.time() < deadline:
        t.send(b"\x1b[I", settle=0.05, collect=0.05)


def result(t, cmd):
    """Run cmd, which writes SAID.pend, and answer what it wrote."""
    if os.path.exists(SAID + ".pend"):
        os.unlink(SAID + ".pend")
    do(t, cmd + " > %s.pend" % SAID)
    deadline = time.time() + 20
    while time.time() < deadline and not os.path.exists(SAID + ".pend"):
        t.collect(0.05)
    t.collect(0.05)
    return open(SAID + ".pend").read().strip() if os.path.exists(SAID + ".pend") else ""


def waitfor(t, cmd, secs=10):
    deadline = time.time() + secs
    while time.time() < deadline:
        if result(t, "if %s; then echo yes; else echo no; fi" % cmd) == "yes":
            return True
    return False


def log(t):
    return open(t.log, errors="replace").read() if os.path.exists(t.log) else ""


def said():
    """What the fixture's own code wrote: (pid, what, window) a line."""
    if not os.path.exists(SAID):
        return []
    return [l.split("\t") for l in open(SAID).read().splitlines()]


def saidwait(n, secs=5):
    deadline = time.time() + secs
    while time.time() < deadline and len(said()) < n:
        t.collect(0.05)
    return said()


def cells(sc, rows=None):
    rows = range(sc.rows) if rows is None else rows
    return [(sc.g[y][x], sc.p[y][x]) for y in rows for x in range(sc.cols)]


def until(t, f, secs=5):
    """Read the screen until f(screen) holds or secs pass."""
    deadline = time.time() + secs
    while time.time() < deadline:
        t.collect(0.05)
        sc = t.screen()
        if f(sc):
            return sc
    return None


def click(t, r, c, button=0):
    if r < 0:
        return
    t.send(screen.press(r, c, button), settle=0.05, collect=0.05)
    t.send(screen.release(r, c, button), settle=0.1, collect=0.1)


def at(t, text, minrow=0, secs=5):
    """Where text is on t's screen, waited for; (-1, -1), which click
    passes over, when it never comes."""
    sc = until(t, lambda s: s.find_from(text, minrow) is not None, secs)
    return sc.find_from(text, minrow) if sc else (-1, -1)


def same(rows=None, secs=6):
    """Whether both sessions show the same cells, glyphs and pens."""
    deadline = time.time() + secs
    while time.time() < deadline:
        t.collect(0.05)
        u.collect(0.05)
        a, b = t.screen(), u.screen()
        if cells(a, rows) == cells(b, rows):
            return True, ""
    return False, "--- hosted\n%s\n--- in-process\n%s" % (t.screen().text(), u.screen().text())


def both(data):
    for term in (t, u):
        term.send(data, settle=0.1, collect=0.1)


def bothclick(text, minrow=0, button=0):
    p = at(u, text, minrow)
    if p[0] >= 0:
        for term in (t, u):
            click(term, p[0], p[1] + 1, button)
    return p


t = session("hosted", HOSTED)
u = session("inproc", INPROC)
do(t, "dt_rthost calc")
check("the Calculator's own process says hello and draws",
      waitfor(t, '[ -n "${DT_RTHOST[calc]-}" ]'), log(t)[-800:])
check("the desktop never defined the Calculator's functions",
      result(t, 'command -v calc_menus || echo none') == "none")
for term in (t, u):
    do(term, "C := dt_new Calculator 16 24 4 40 calc")

# The bar: File, Edit, the app's own, Window -- the same cells as in-process.
ok, why = same([0])
check("a hosted Calculator's bar is cell for cell the in-process one's", ok, why)
bar = t.screen().row(0)
pos = [bar.find(" %s " % w) for w in ("File", "Edit", "Calc", "Window")]
check("and reads File, Edit, Calc, Window", all(p >= 0 for p in pos) and pos == sorted(pos), bar)

# Its own menu open, and a row of the app's own chosen from it.
for k in (b"1", b"2", b"+", b"3"):
    both(k)
bothclick(" Calc ")
ok, why = same()
check("its Calc menu opens as in-process, every row", ok, why)
bothclick("Evaluate", 1)
ok, why = same()
check("a row of the app's own runs in its process and its window shows it", ok, why)

# Its context menu, and a row of it.
gr = [int(x) for x in result(t, 'echo "${DT[$C]["row"]} ${DT[$C]["col"]}"').split()]
for term in (t, u):
    click(term, gr[0] + 3, gr[1] + 4, 2)
ok, why = same()
check("its context menu opens at the pointer as in-process", ok, why)
bothclick("Clear", gr[0] + 3)
ok, why = same()
check("and its row runs in the app's process, as in-process", ok, why)

# File > Close: one of the desktop's own rows, run in the desktop.
bothclick(" File ")
bothclick("Close", 1)
check("File > Close, the desktop's own row, closes the hosted window",
      waitfor(t, '! dt_iswin "$C"', 5) and waitfor(u, '! dt_iswin "$C"', 5), log(t)[-600:])

# The fixture, in two windows: a row acts on the window it was built for,
# in the app's own process.
if os.path.exists(SAID):
    os.unlink(SAID)
do(t, "dt_rthost mfx")
waitfor(t, '[ -n "${DT_RTHOST[mfx]-}" ]')
do(t, "M1 := dt_new Menus 6 30 4 2 mfx; M2 := dt_new Menus 6 30 12 2 mfx")
m1, m2 = result(t, 'echo "$M1"'), result(t, 'echo "$M2"')
apid = result(t, 'echo "${DT_RTCPID[${DT_RTHOST[mfx]}]-}"')
dpid = str(t.pid)
at(t, " Tools ")
click(t, *at(t, " Tools "))
click(t, *at(t, "Bump", 1))
s = saidwait(1)
check("a row of the app's own runs in the app's process, for the focused window",
      s[:1] == [[apid, "bump", m2]] and apid and apid != dpid, (s, apid, dpid, m2))
click(t, 6, 5)
click(t, *at(t, " Tools "))
click(t, *at(t, "Bump", 1))
s = saidwait(2)
check("and focused on the other window, the same row acts on that one",
      len(s) == 2 and s[1] == [apid, "bump", m1], s)
click(t, *at(t, " Tools "))
sc = until(t, lambda x: x.find("Odd") is not None and "✓" in x.row(x.find("Odd")[0]))
check("a tick that follows the window's state is drawn as the app built it",
      sc is not None, t.screen().text())
click(t, *at(t, "More", 1))
click(t, *at(t, "Deep", 1))
s = saidwait(3)
check("a submenu's row runs in the app's process",
      len(s) == 3 and s[2] == [apid, "deep", m1], s)

# A list of choices, its words from the app's namer, in the app's process.
click(t, *at(t, " Tools "))
click(t, *at(t, "Pick", 1))
sc = until(t, lambda x: x.find("Word c %s" % apid) is not None)
check("a list of choices opens with the words its namer gave, in the app's process",
      sc is not None and sc.find("Word a %s" % apid) is not None, (apid, t.screen().text()))
row = sc.row(sc.find("Word b")[0]) if sc and sc.find("Word b") else ""
check("and the value chosen now is ticked", "✓" in row, row)
p = sc.find("Word c") if sc else None
if p:
    click(t, p[0], p[1] + 1)
s = saidwait(4)
check("choosing one hands the app its value, in its own process",
      len(s) == 4 and s[3] == [apid, "picked c", m1], (s, log(t)[-500:]))

# An asked dialog: its answer goes back to the app, which refuses one, and
# the dialog stays until it takes one.
click(t, *at(t, " Tools "))
click(t, *at(t, "Ask", 1))
at(t, "Name:")
t.send(b"bad\r", settle=0.2, collect=0.2)
s = saidwait(5)
time.sleep(0.4)
t.collect(0.2)
check("a dialog's answer is handed to the app's process, and one it refuses "
      "leaves the dialog up",
      len(s) == 5 and s[4][:2] == [apid, "named bad"] and t.screen().find("Name:") is not None,
      (s, t.screen().text()))
t.send(b"\x7f\x7f\x7fok\r", settle=0.2, collect=0.2)
s = saidwait(6)
sc = until(t, lambda x: x.find("Name:") is None)
check("and one it takes closes it",
      len(s) == 6 and s[5][:2] == [apid, "named ok"] and sc is not None, (s, t.screen().text()))

# A confirm: Yes runs the app's own command in its process.
click(t, *at(t, " Tools "))
click(t, *at(t, "Confirm", 1))
at(t, "Sure?")
t.send(b"y", settle=0.2, collect=0.2)
s = saidwait(7)
check("a confirm's Yes runs the app's command in the app's process",
      [apid, "yes", m1] in s[6:], s)

# A title menu, on the title clicked with its modifier (alt by default):
# told to the app, whose list comes back built.
t.send(b"\x1b[<8;9;13M", settle=0.05, collect=0.05)
t.send(b"\x1b[<8;9;13m", settle=0.1, collect=0.1)
sc = until(t, lambda x: x.find("Word top %s" % apid) is not None)
check("a title menu is the app's own, its list with its namer's words",
      sc is not None, (apid, t.screen().text(), log(t)[-400:]))
p = sc.find("Word top") if sc else None
if p:
    click(t, p[0], p[1] + 1)
s = saidwait(8)
check("and choosing from it runs in the app's process, for that window",
      [apid, "picked top", m2] in s[6:], s)

# A menu whose label differs on every build (a time in it) settles: what a
# push brings is built and not asked for again, so the idle desktop draws
# no more than it would with any other window.
t.collect(1.0)
f0 = t.frames()
t.collect(2.0)
nf = t.frames() - f0
check("a menu that differs on every build does not keep the desktop drawing",
      nf <= 4, nf)

# Its context menu, for the window clicked.
click(t, 14, 6, 2)
click(t, *at(t, "Here", 12))
s = saidwait(9)
check("a context menu's row runs in the app's process, for the window clicked",
      [apid, "here", m2] in s[6:], (s, t.screen().text()))
check("nothing about menus, lists or dialogs refused, late or unread",
      not any(x in log(t) for x in ("refused", "could not read", "did not build",
                                    "did not answer", "which is pushed", "after its time")),
      log(t)[-1500:])

# What a menu rebuild costs the desktop: hosted (the last push built again,
# the app asked afresh) against in-process (the app's own _menus).
do(t, "dt_rthost calc")
waitfor(t, '[ -n "${DT_RTHOST[calc]-}" ]')
for term in (t, u):
    do(term, "C := dt_new Calculator 16 24 4 40 calc")
waitfor(t, '[ -n "${DT_RVMENU[menus:calc]["n"]-}" ]')
# Twenty at a time: a hundred held the desktop past the stall budget.
PER = 'T0=${EPOCHREALTIME/./}; for i in {1..20}; do dt_menus; done; echo $(( (${EPOCHREALTIME/./} - T0) / 20 ))'
ph = [int(result(t, PER) or 0) for _ in range(7)]
pu = [int(result(u, PER) or 0) for _ in range(7)]
print("a menu rebuild, Calculator focused, us (median of 7 x 20): hosted %d, in-process %d"
      % (statistics.median(ph), statistics.median(pu)))

u.close()
t.close()
for f in (MFX, SAID, t.mark, u.mark, SAID + ".pend",
          scratch("hostmenus-hosted.hibr"), scratch("hostmenus-inproc.hibr")):
    if os.path.exists(f):
        os.unlink(f)
report(25)
