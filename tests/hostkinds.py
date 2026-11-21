#!/usr/bin/env python3
"""A Control Panel pane, a Control Strip module and a screen saver drawn by
their own processes (ADR 0050, the view kinds): the desktop starts each by
its file (dt_rthostk), binds a view of its kind -- `pane` in the panel's
window, `strip` in its slot of the strip, `saver` on the screen -- the
process draws through its headless console, and the desktop shows the cells
where the in-process code would have drawn, and never draws them itself.
Input goes as a call by name: a click on the strip module, a press, a key
and the wheel in the pane's body, a press the pane declines becoming the
panel's own click. Each ends: the strip turned off, the panel showing
another pane or closing, the saver stopped, and the process with no view
left goes by itself.

    python3 tests/hostkinds.py

The real ones are the Shadow strip module and the Date & Time pane, each
compared cell for cell, glyphs and pens, with the same installable sourced
in a second session; Classic Mac, whose frames move with time, is found on
the screen; the pane that records its input is the test's own. In the
hosted session the module and the panes are never sourced, only registered
as the desktop reads a registration, so no in-process path can stand in for
them; the savers are read as the desktop reads them, and Classic Mac's own
two functions are counted there and must never run.
"""
import os, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import screen
from screen import Term, check, report, load, tree, scratch

MOD = tree("build/mods/console.so")
WM = tree("programs/desktop/desktop.hibr")
PANEL = tree("programs/desktop/system/panel.hibr")
SHADOW = tree("programs/desktop/control-strip/shadow.hibr")
DATETIME = tree("programs/desktop/control-panel/datetime.hibr")
SAID = scratch("hostkinds-said")
TPANE = scratch("hostkinds-tpane.hibr")

# The test's own pane: a body that says how many clicks it has had, and
# every input it is given written down with the process it ran in. Its
# _mouse declines a press, which the panel then makes its click.
open(TPANE, "w").write(r'''command -v cp_pane > /dev/null && cp_pane tpane "Test Pane" "+"
TP_N=0
fn tpane_draw(int id, int h, int w, int bx) {
	console pen "$DT_INK" "$DT_FACE"
	console put -p "w$id" 1 "$bx" "clicks $TP_N"
	console put -p "w$id" 2 "$bx" "h=$h w=$w"
}
fn tpane_click(int id, int r, int c) {
	TP_N=$((TP_N + 1))
	printf 'click %%s %%s %%s\n' "$$" "$r" "$c" >> %s
}
fn tpane_key(int id, key) {
	printf 'key %%s %%s\n' "$$" "$key" >> %s
	return 0
}
fn tpane_wheel(int id, dir) {
	printf 'wheel %%s %%s\n' "$$" "$dir" >> %s
}
fn tpane_mouse(int id, act, button, int r, int c) {
	printf 'mouse %%s %%s\n' "$$" "$act" >> %s
	[ "$act" != press ]
}
''' % ((SAID + ".tp",) * 4))

SESSION = r'''%s. %s
dt_open
DT_NOTEMS=60000
DT_SAVERSECS=100000
%s
CS_SHOW=1
cs_relist
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

# The hosted session: the panel sourced, as the platform's; the strip
# module, the two panes and Date & Time's own dialogs only registered; the
# savers read as the desktop reads them, Classic Mac's two functions counted
# here so a call of either in this process shows.
HOSTED = r'''DT_LOADING=%s . %s
DT_LOADING=%s cs_module shadow Shadow
DT_LOADING=%s cp_pane datetime "Date & Time" "◷" hardware
DT_LOADING=%s dt_app dtz "Time Zone" 16 46 "" "" fixed hidden
DT_LOADING=%s dt_app dts "Set Date & Time" 10 46 "" "" fixed hidden
DT_LOADING=%s dt_app dtf "Clock Format" 9 50 "" "" fixed hidden
DT_LOADING=%s cp_pane tpane "Test Pane" "+"
DT_LOADING=
DT_SAVER=classicmac
dt_saverload
HK_CALLS=0
eval "$(declare -f classicmac_start | sed '1s/classicmac_start(/hk_start(/')"
eval "$(declare -f classicmac_frame | sed '1s/classicmac_frame(/hk_frame(/')"
classicmac_start() { HK_CALLS=$((HK_CALLS + 1)); hk_start "$@"; }
classicmac_frame() { HK_CALLS=$((HK_CALLS + 1)); hk_frame "$@"; }''' % (
    PANEL, PANEL, SHADOW, DATETIME, DATETIME, DATETIME, DATETIME, TPANE)
# The in-process session: each sourced as a desktop sources it.
INPROC = r'''DT_LOADING=%s . %s
DT_LOADING=%s . %s
DT_LOADING=%s . %s
DT_LOADING=%s . %s
DT_LOADING=''' % (PANEL, PANEL, SHADOW, SHADOW, DATETIME, DATETIME, TPANE, TPANE)


def session(name, body):
    p = scratch("hostkinds-%s.hibr" % name)
    mark = scratch("hostkinds-do-%s" % name)
    open(p, "w").write(SESSION % (load(MOD), WM, body, mark, mark, mark))
    term = Term(p, rows=30, cols=110, settle=0.6, env={"DT_BARTIME": "12:00"})
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
    """Wait until cmd, a test in the desktop, holds."""
    deadline = time.time() + secs
    while time.time() < deadline:
        if result(t, "if %s; then echo yes; else echo no; fi" % cmd) == "yes":
            return True
    return False


def ended(pid):
    """Whether a process has ended, or ended and is not yet reaped."""
    try:
        return open("/proc/%s/stat" % pid).read().rsplit(")", 1)[1].split()[0] == "Z"
    except (OSError, IndexError):
        return True


def waitend(t, pid, secs=4):
    deadline = time.time() + secs
    while time.time() < deadline and pid and not ended(pid):
        t.collect(0.1)
    return bool(pid) and ended(pid)


def log(t):
    return open(t.log, errors="replace").read() if os.path.exists(t.log) else ""


def rect(sc, r, c, h, w):
    return [(sc.g[y][x], sc.p[y][x]) for y in range(r, r + h) for x in range(c, c + w)]


def geom(t, var):
    v = result(t, 'echo "${DT[$%s]["row"]} ${DT[$%s]["col"]} ${DT[$%s]["h"]} ${DT[$%s]["w"]}"'
               % (var, var, var, var))
    return [int(x) for x in v.split()] if len(v.split()) == 4 else [0, 0, 1, 1]


def striprow(t):
    """The row the strip is on, by where its one module draws."""
    sc = t.screen()
    for y in range(sc.rows):
        if "]" in "".join(sc.g[y]) and "[" in "".join(sc.g[y]) and "Shadow" in "".join(sc.g[y]):
            return y
    return None


def agree(t, u, region, secs=6):
    """Whether region (a function of a term answering a rectangle) shows the
    same cells in both sessions, read until it does or secs pass: what is
    waited out is a second turning between the two, never a difference."""
    deadline = time.time() + secs
    a = b = None
    while time.time() < deadline:
        t.collect(0.1)
        u.collect(0.1)
        ga, gb = region(t), region(u)
        if ga and gb and ga == gb:
            a, b = rect(t.screen(), *ga), rect(u.screen(), *gb)
            if a == b:
                return True, ""
    diff = [(i, x, y) for i, (x, y) in enumerate(zip(a or [], b or [])) if x != y][:6]
    return False, "%s %s %s\n--- hosted\n%s\n--- in-process\n%s" % (
        region(t), region(u), diff, t.screen().text(), u.screen().text())


def strip(term):
    r = striprow(term)
    return [r, 0, 1, term.screen().cols] if r is not None else None


def taken(path):
    return open(path).read().split("\n") if os.path.exists(path) else []


for f in (SAID + ".tp",):
    if os.path.exists(f):
        os.unlink(f)
t = session("hosted", HOSTED)
u = session("inproc", INPROC)

# The strip module: its own process draws its slot.
do(t, "dt_rthostk strip shadow")
check("the Shadow module's own process says hello and what it has",
      waitfor(t, '[ -n "${DT_RVPC[shadow]-}" ]'), log(t)[-800:])
ok, why = agree(t, u, strip)
check("the hosted strip module's slot is cell for cell, glyphs and pens, the "
      "in-process one", ok, why)
check("and the desktop never defined its functions",
      result(t, 'command -v shadow_draw || echo none') == "none")
spid = result(t, 'echo "${DT_RTCPID[${DT_RVPC[shadow]}]-}"')
r = striprow(t)
col = "".join(t.screen().g[r]).find("Shadow") if r is not None else -1
was = result(t, 'echo "$DT_SHADOW"')
for term in (t, u):
    term.send(screen.press(r, col + 1), settle=0.1, collect=0.1)
    term.send(screen.release(r, col + 1), settle=0.1, collect=0.1)
check("a click reaches its own _click, whose setting the desktop takes",
      waitfor(t, '[ "$DT_SHADOW" != "%s" ]' % was, 5),
      (was, result(t, 'echo "$DT_SHADOW"'), log(t)[-600:]))
ok, why = agree(t, u, strip)
check("and it redraws in its own process, as the in-process one does", ok, why)

# Date & Time's pane: its own process draws the body of the panel's window,
# against the same pane in-process, at a moment both show the same second.
do(t, "dt_rthostk pane datetime")
check("the Date & Time pane's own process says what it has",
      waitfor(t, '[ -n "${DT_RVPC[datetime]-}" ]'), log(t)[-800:])
do(t, "P := dt_launch panel datetime")
do(u, "P := dt_launch panel datetime")
ok, why = agree(t, u, lambda term: geom(term, "P"), secs=8)
check("the hosted pane's body is cell for cell the in-process one", ok, why)
check("its view is bound in the panel's window, at generation 1",
      result(t, 'echo "${DT_RVXP[p$P]-} ${DT_RVXGEN[p$P]-} ${DT_RVXARGS[p$P]%% *}"')
      .split() == ["datetime", "1", "w" + result(t, 'echo "$P"')],
      result(t, 'echo "${DT_RVXP[p$P]-} ${DT_RVXGEN[p$P]-} ${DT_RVXARGS[p$P]-}"'))
check("and the desktop never defined its functions",
      result(t, 'command -v datetime_draw || echo none') == "none")
dpid = result(t, 'echo "${DT_RTCPID[${DT_RVPC[datetime]}]-}"')
do(t, "dt_resize $P 28 90")
do(u, "dt_resize $P 28 90")
ok, why = agree(t, u, lambda term: geom(term, "P"), secs=8)
check("resized, its view is bound again and drawn as in-process", ok, why)
check("at a generation of its own",
      result(t, 'echo "${DT_RVXGEN[p$P]-}"') == "2", log(t)[-600:])

# The test's own pane: shown in the same window, Date & Time's view goes
# and its process with it.
do(t, "dt_rthostk pane tpane")
waitfor(t, '[ -n "${DT_RVPC[tpane]-}" ]')
do(t, "panel_goto $P tpane")
check("the panel showing another pane lets the first pane's process go",
      waitend(t, dpid) and "process %s had no window" % dpid not in log(t),
      (dpid, log(t)[-600:]))
tpid = result(t, 'echo "${DT_RTCPID[${DT_RVPC[tpane]}]-}"')
pr, pc, ph, pw = geom(t, "P")


def body(text, secs=5):
    deadline = time.time() + secs
    while time.time() < deadline:
        t.collect(0.1)
        if text in t.screen().text():
            return True
    return False


check("the test's pane is drawn by its own process", body("clicks 0"), t.screen().text())
bx = int(result(t, 'echo "${DT_RVXARGS[p$P]}"').split()[2] or 0)
br, bc = pr + 2, pc + bx + 2
t.send(screen.press(br, bc), settle=0.1, collect=0.1)
t.send(screen.release(br, bc), settle=0.1, collect=0.1)
check("a press it declines becomes the panel's click, which reaches its "
      "_click in its own process, and its redraw comes back drawn",
      body("clicks 1"), (taken(SAID + ".tp"), t.screen().text()))
said = [l for l in taken(SAID + ".tp") if l]
check("the press first, then the click, each in the pane's own process",
      said[:1] == ["mouse %s press" % tpid] and
      any(l.startswith("click %s " % tpid) for l in said[1:]), (said, tpid))
t.send(b"x", settle=0.2, collect=0.2)
t.send(screen.wheel(br, bc, up=False), settle=0.2, collect=0.2)
deadline = time.time() + 3
while time.time() < deadline and not any(l.startswith("wheel") for l in taken(SAID + ".tp")):
    t.collect(0.1)
said = taken(SAID + ".tp")
check("a key and the wheel reach its _key and _wheel",
      "key %s x" % tpid in said and "wheel %s down" % tpid in said, said)
t.send(b"\x1b", settle=0.3, collect=0.3)
t.send(b"\x1b", settle=0.3, collect=0.3)
check("and a key the desktop also acts on is withheld from it, said in the log",
      "hosted: key escape withheld" in log(t) and "key %s escape" % tpid not in taken(SAID + ".tp"),
      log(t)[-400:])

# The saver, while the strip module is drawn: it takes the screen, the
# module's slot giving it up, and its frames come from its own process.
do(t, "dt_rthostk saver classicmac")
check("Classic Mac's own process says what it has",
      waitfor(t, '[ -n "${DT_RVPC[classicmac]-}" ]'), log(t)[-800:])
vpid = result(t, 'echo "${DT_RTCPID[${DT_RVPC[classicmac]}]-}"')
do(t, "dt_saverstart")


def mac(secs=5):
    deadline = time.time() + secs
    while time.time() < deadline:
        t.collect(0.1)
        s = t.screen().text()
        if "[====]" in s and "______________________" in s:
            return s
    return None


first = mac()
check("the saver's Mac is on the screen, drawn by its own process", first is not None,
      t.screen().text())
moved = False
deadline = time.time() + 4
while time.time() < deadline and first:
    t.collect(0.2)
    s = t.screen().text()
    if "[====]" in s and s != first:
        moved = True
        break
check("and its frames keep coming, each shown as it changes", moved, t.screen().text())
check("the desktop made no call of the saver's own, which it has read",
      result(t, 'echo "$HK_CALLS"') == "0", result(t, 'echo "$HK_CALLS"'))
t.send(b"z", settle=0.3, collect=0.3)
check("a key ends it, and its process, with no view left, goes by itself",
      waitend(t, vpid) and "[====]" not in t.screen().text(),
      (vpid, log(t)[-600:]))
ok, why = agree(t, u, strip)
check("the strip module's slot, given up to the saver, is bound again and "
      "drawn as in-process", ok and result(t, 'echo "${DT_RVXGEN[sshadow]-}"') == "2",
      (why, result(t, 'echo "${DT_RVXGEN[sshadow]-}"')))

# Each ends: the panel closing, the strip turned off.
do(t, "dt_del $P")
check("the panel closing lets its pane's process go",
      waitend(t, tpid) and "process %s had no window" % tpid not in log(t),
      (tpid, log(t)[-600:]))
do(t, "CS_SHOW=0; cs_relist")
check("the strip turned off lets its module's process go",
      waitend(t, spid), (spid, log(t)[-600:]))
check("and the desktop holds none of their views or prefixes",
      result(t, 'echo "${#DT_RVX[@]} ${#DT_RVP[@]} ${#DT_RVFN[@]}"') == "0 0 0",
      result(t, 'echo "${!DT_RVX[@]} / ${!DT_RVP[@]} / ${!DT_RVFN[@]}"'))

u.close()
t.close()
for f in (TPANE, t.mark, u.mark, SAID + ".pend", SAID + ".tp",
          scratch("hostkinds-hosted.hibr"), scratch("hostkinds-inproc.hibr")):
    if os.path.exists(f):
        os.unlink(f)
report(22)
