#!/usr/bin/env python3
"""A Control Panel pane, a Control Strip module, a screen saver and menu bar
items drawn by their own processes (ADR 0050, the view kinds): the desktop
starts each by its file (dt_rthostk, dt_rthostb), binds a view of its kind
-- `pane` in the panel's window, `strip` in its slot of the strip, `saver`
on the screen, `bar` in the bar's pane for an item that draws itself; a
text item needs none, its text and command told as requests -- the
process draws through its headless console, and the desktop shows the cells
where the in-process code would have drawn, and never draws them itself.
Input goes as a call by name: a click on the strip module, a press, a key
and the wheel in the pane's body, a press the pane declines becoming the
panel's own click, a click on a bar item running its command in its
process and a press on a drawn one reaching its handler. Each ends: the
strip turned off, the panel showing another pane or closing, the saver
stopped, and the process with no view left goes by itself; a bar item whose
process ends goes off the bar. An item's process naming another's item is
refused.

    python3 tests/hostkinds.py

The bar items are the test's own: the tree's text items need a bar hook
(prayer) or two keyboards (keyboard), and its one drawn installable, the
tray, draws pictures, which do not cross yet. The real ones are the Shadow
strip module and the Date & Time pane, each
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
from screen import Term, check, report, load, tree, scratch, expect

# Every wait is longer under the sanitizers, whose build is several times slower.
SLOW = 4 if os.environ.get("ASAN_OPTIONS") else 1

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
	[ "$key" != escape ]
}
fn tpane_wheel(int id, dir) {
	printf 'wheel %%s %%s\n' "$$" "$dir" >> %s
}
fn tpane_mouse(int id, act, button, int r, int c) {
	printf 'mouse %%s %%s\n' "$$" "$act" >> %s
	[ "$act" != press ]
}
''' % ((SAID + ".tp",) * 4))

# Two menu bar items of the test's own: one that is its text and a command,
# one that draws itself and takes a press; and a function the first can be
# told to run, naming the second's item, which is not its own.
TBAR = scratch("hostkinds-tbar.hibr")
DBAR = scratch("hostkinds-dbar.hibr")
open(TBAR, "w").write(r'''TB_N=0
dt_baritem tbar "T:0" tbar_do
dt_barilabel tbar "Test Text"
tbar_do() {
	TB_N=$((TB_N + 1))
	dt_baritem tbar "T:$TB_N" tbar_do
	printf 'do %%s\n' "$$" >> %s
}
tbar_steal() {
	dt_baritem dbar "STOLEN"
}
''' % (SAID + ".bar"))
open(DBAR, "w").write(r'''DB_N=0
dt_baritem dbar "D:0   "
dt_baridraw dbar dbar_draw
dt_baripress dbar dbar_press
fn dbar_draw(int c, int hover) {
	console pen "$DT_ACTIVE" "$DT_BAR" bold
	dt_barput "$c" "D:$DB_N"
}
fn dbar_press(btn, int r, int c) {
	DB_N=$((DB_N + 1))
	dt_baritem dbar "D:$DB_N   "
	printf 'press %%s %%s\n' "$$" "$btn" >> %s
	return 0
}
''' % (SAID + ".bar"))

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
DT_LOADING=%s dt_baritem tbar ""
DT_LOADING=%s dt_baritem dbar ""
DT_LOADING=
DT_SAVER=classicmac
dt_saverload
HK_CALLS=0
eval "$(declare -f classicmac_start | sed '1s/classicmac_start(/hk_start(/')"
eval "$(declare -f classicmac_frame | sed '1s/classicmac_frame(/hk_frame(/')"
classicmac_start() { HK_CALLS=$((HK_CALLS + 1)); hk_start "$@"; }
classicmac_frame() { HK_CALLS=$((HK_CALLS + 1)); hk_frame "$@"; }''' % (
    PANEL, PANEL, SHADOW, DATETIME, DATETIME, DATETIME, DATETIME, TPANE, TBAR, DBAR)
# The in-process session: each sourced as a desktop sources it.
INPROC = r'''DT_LOADING=%s . %s
DT_LOADING=%s . %s
DT_LOADING=%s . %s
DT_LOADING=%s . %s
DT_LOADING=%s . %s
DT_LOADING=%s . %s
DT_LOADING=''' % (PANEL, PANEL, SHADOW, SHADOW, DATETIME, DATETIME, TPANE, TPANE, TBAR, TBAR, DBAR, DBAR)


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
    deadline = time.time() + 5 * SLOW
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
    deadline = time.time() + secs * SLOW
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
    deadline = time.time() + secs * SLOW
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
    deadline = time.time() + secs * SLOW
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
    """The strip's own cells: its arrow, the brackets and the module, never
    the rest of its row, which a window may be under."""
    r = striprow(term)
    if r is None:
        return None
    c = "".join(term.screen().g[r]).find("Shadow")
    return [r, max(0, c - 2), 1, 10]


def taken(path):
    return open(path).read().split("\n") if os.path.exists(path) else []


for f in (SAID + ".tp", SAID + ".bar"):
    if os.path.exists(f):
        os.unlink(f)
t = session("hosted", HOSTED)
u = session("inproc", INPROC)
for name, term in (("hosted", t), ("in-process", u)):
    if result(term, "echo up") != "up":
        st = "gone"
        try:
            st = open("/proc/%d/stat" % term.pid).read().split()[2] + " " + \
                open("/proc/%d/wchan" % term.pid).read()
        except OSError:
            pass
        print("the %s session did not start (%s), its file %s:\n%s\n%s\n%s" % (
            name, st, scratch("hostkinds-%s.hibr" % ("hosted" if term is t else "inproc")),
            term.screen().text(), log(term)[-3000:], term.text()[-2000:] if hasattr(term, "text") else ""))
        sys.exit(1)

# The strip module: its own process draws its slot.
do(t, "dt_rthostk strip shadow")
check("the Shadow module's own process says hello and what it has",
      waitfor(t, '[ -n "${DT_RVPC[shadow]-}" ]'), log(t)[-800:])
ok, why = agree(t, u, strip)
check("the hosted strip module's slot is cell for cell, glyphs and pens, the "
      "in-process one, and the desktop never defined its functions",
      ok and result(t, 'command -v shadow_draw || echo none') == "none", why)
spid = result(t, 'echo "${DT_RTCPID[${DT_RVPC[shadow]}]-}"')
r = striprow(t)
col = "".join(t.screen().g[r]).find("Shadow") if r is not None else -1
was = result(t, 'echo "$DT_SHADOW"')
for term in (t, u):
    if r is None:
        break
    term.send(screen.press(r, col + 1), settle=0.1, collect=0.1)
    term.send(screen.release(r, col + 1), settle=0.1, collect=0.1)
check("a click reaches its own _click, whose setting the desktop takes",
      waitfor(t, '[ "$DT_SHADOW" != "%s" ]' % was, 5),
      (was, result(t, 'echo "$DT_SHADOW"'), log(t)[-600:]))
ok, why = agree(t, u, strip)
check("and it redraws in its own process, as the in-process one does", ok, why)

# The menu bar items: a text one, all of it told by its own process, and a
# drawn one, whose cells come from its own process into the bar's pane.
do(t, "dt_rthostb tbar; dt_rthostb dbar")


def bar(term):
    return [0, 0, 1, term.screen().cols]


def onbar(term, text, secs=6):
    deadline = time.time() + secs * SLOW
    while time.time() < deadline:
        term.collect(0.1)
        if text in "".join(term.screen().g[0]):
            return True
    return False


ok, why = agree(t, u, bar) if onbar(t, "D:0") and onbar(t, "T:0") else (False, t.screen().text())
check("a text item's own process puts it on the bar, and a drawn one's draws "
      "its slot: the bar is cell for cell the in-process one, and the desktop "
      "never defined their functions",
      ok and result(t, 'command -v tbar_do dbar_draw || echo none') == "none", why)
row = "".join(t.screen().g[0])
tc, dc = row.find("T:0"), row.find("D:0")
for term in (t, u):
    term.send(screen.press(0, tc), settle=0.1, collect=0.1)
    term.send(screen.release(0, tc), settle=0.1, collect=0.1)
ok, why = agree(t, u, bar) if onbar(t, "T:1") else (False, t.screen().text())
said = [l for l in taken(SAID + ".bar") if l]
bpid = result(t, 'echo "${DT_RTCPID[${DT_RVB[tbar]}]-}"')
check("a click on the text item runs its command in its own process, whose new "
      "text it tells the desktop, as in-process",
      ok and "do %s" % bpid in said, (said, bpid, why))
for term in (t, u):
    term.send(screen.press(0, dc), settle=0.1, collect=0.1)
    term.send(screen.release(0, dc), settle=0.1, collect=0.1)
ok, why = agree(t, u, bar) if onbar(t, "D:1") else (False, t.screen().text())
dpid = result(t, 'echo "${DT_RTCPID[${DT_RVB[dbar]}]-}"')
check("a press on the drawn item reaches its own handler, and its redraw "
      "comes back drawn, as in-process",
      ok and "press %s left" % dpid in taken(SAID + ".bar"), (taken(SAID + ".bar"), why))
expect(r"refused: dt_baritem: .* named the bar item dbar, which is not its own")
do(t, 'dt_rttell "${DT_RTCTOK[${DT_RVB[tbar]}]}" tbar_steal')
deadline = time.time() + 3 * SLOW
while time.time() < deadline and "named the bar item dbar" not in log(t):
    t.collect(0.1)
check("an item's process naming another's item is refused, and the item stays",
      "named the bar item dbar, which is not its own" in log(t) and onbar(t, "D:1", 1)
      and "STOLEN" not in t.screen().text(), (taken(SAID + ".bar"), log(t)[-2500:]))
for pid in (bpid, dpid):
    if pid:
        os.kill(int(pid), 15)
check("an item whose process ends goes off the bar, and its view with it",
      waitend(t, bpid) and waitend(t, dpid) and
      waitfor(t, '[ -z "${DT_RVB[*]-}${DT_RVX[bdbar]-}" ]', 3) and
      not onbar(t, "T:1", 1) and not onbar(t, "D:1", 0.5),
      (result(t, 'echo "${!DT_RVB[@]} / ${!DT_RVX[@]}"'), t.screen().text()[:300]))

# Date & Time's pane: its own process draws the body of the panel's window,
# against the same pane in-process, at a moment both show the same second.
do(t, "dt_rthostk pane datetime")
check("the Date & Time pane's own process says what it has",
      waitfor(t, '[ -n "${DT_RVPC[datetime]-}" ]'), log(t)[-800:])
do(t, "P := dt_launch panel datetime")
do(u, "P := dt_launch panel datetime")
ok, why = agree(t, u, lambda term: geom(term, "P"), secs=8)
check("the hosted pane's body is cell for cell the in-process one, and the "
      "desktop never defined its functions",
      ok and result(t, 'command -v datetime_draw || echo none') == "none", why)
check("its view is bound in the panel's window, at generation 1",
      result(t, 'echo "${DT_RVXP[p$P]-} ${DT_RVXGEN[p$P]-} ${DT_RVXARGS[p$P]%% *}"')
      .split() == ["datetime", "1", "w" + result(t, 'echo "$P"')],
      result(t, 'echo "${DT_RVXP[p$P]-} ${DT_RVXGEN[p$P]-} ${DT_RVXARGS[p$P]-}"'))
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
    deadline = time.time() + secs * SLOW
    while time.time() < deadline:
        t.collect(0.1)
        if text in t.screen().text():
            return True
    return False


check("the test's pane is drawn by its own process", body("clicks 0"), t.screen().text())
bx = (result(t, 'echo "${DT_RVXARGS[p$P]-}"').split() + ["", "", "0"])[2]
bx = int(bx) if bx.isdigit() else 0
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
deadline = time.time() + 3 * SLOW
while time.time() < deadline and not any(l.startswith("wheel") for l in taken(SAID + ".tp")):
    t.collect(0.1)
said = taken(SAID + ".tp")
check("a key and the wheel reach its _key and _wheel",
      "key %s x" % tpid in said and "wheel %s down" % tpid in said, said)
t.send(b"\x1b", settle=0.3, collect=0.3)
t.send(b"\x1b", settle=0.3, collect=0.3)
check("and a key the desktop also acts on is held for its answer: given to "
      "its _key, declined there, and the desktop's own action runs",
      "hosted: key escape declined; the desktop's own action runs" in log(t)
      and "key %s escape" % tpid in taken(SAID + ".tp"),
      (taken(SAID + ".tp"), log(t)[-1500:]))

# The saver, while the strip module is drawn: it takes the screen, the
# module's slot giving it up, and its frames come from its own process.
do(t, "dt_rthostk saver classicmac")
check("Classic Mac's own process says what it has",
      waitfor(t, '[ -n "${DT_RVPC[classicmac]-}" ]'), log(t)[-800:])
vpid = result(t, 'echo "${DT_RTCPID[${DT_RVPC[classicmac]}]-}"')
g0 = int(result(t, 'echo "${DT_RVXGEN[sshadow]-0}"') or 0)
do(t, "dt_saverstart")


def mac(secs=5):
    deadline = time.time() + secs * SLOW
    while time.time() < deadline:
        t.collect(0.1)
        s = t.screen().text()
        if "[====]" in s and "______________________" in s:
            return s
    return None


first = mac()
calls = result(t, 'echo "$HK_CALLS"')
check("the saver's Mac is on the screen, drawn by its own process: the "
      "desktop made no call of the saver's own, which it has read",
      first is not None and calls == "0", (calls, t.screen().text()))
moved = False
deadline = time.time() + 4 * SLOW
while time.time() < deadline and first:
    t.collect(0.2)
    s = t.screen().text()
    if "[====]" in s and s != first:
        moved = True
        break
calls = result(t, 'echo "$HK_CALLS"')
check("and its frames keep coming, each shown as it changes, still none of "
      "them drawn here", moved and calls == "0", (calls, t.screen().text()))
t.send(b"z", settle=0.3, collect=0.3)
check("a key ends it, and its process, with no view left, goes by itself",
      waitend(t, vpid) and "[====]" not in t.screen().text(),
      (vpid, log(t)[-600:]))
ok, why = agree(t, u, strip)
check("the strip module's slot, given up to the saver, is bound again and "
      "drawn as in-process", ok and int(result(t, 'echo "${DT_RVXGEN[sshadow]-0}"') or 0) > g0,
      (why, result(t, 'echo "${DT_RVXGEN[sshadow]-}"')))

# Each ends: the panel closing, the strip turned off.
do(t, "dt_del $P")
check("the panel closing lets its pane's process go",
      waitend(t, tpid) and "process %s had no window" % tpid not in log(t),
      (tpid, log(t)[-600:]))
do(t, "CS_SHOW=0; cs_relist")
gone = waitend(t, spid)
check("the strip turned off lets its module's process go, and the desktop "
      "holds none of their views or prefixes",
      gone and waitfor(t, '[ "${#DT_RVX[@]} ${#DT_RVP[@]} ${#DT_RVFN[@]}" = "0 0 0" ]', 3),
      (spid, result(t, 'echo "${!DT_RVX[@]} / ${!DT_RVP[@]} / ${!DT_RVFN[@]}"'), log(t)[-600:]))

u.close()
t.close()
for f in (TPANE, TBAR, DBAR, SAID + ".bar", t.mark, u.mark, SAID + ".pend", SAID + ".tp",
          scratch("hostkinds-hosted.hibr"), scratch("hostkinds-inproc.hibr")):
    if os.path.exists(f):
        os.unlink(f)
report(28)
