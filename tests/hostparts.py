#!/usr/bin/env python3
"""A bundle as one process, a rows pane, a bar hook and a bar pop-up drawn
by their own processes (ADR 0050, the view kinds): starting a bundle's bar
item starts its app's process, which reads the app's file and each part it
declares (`# part:`), so the item, its pane and its hook are that
process's; the bar hook runs there and no longer here, waking for a
setting it reads; the item's pop-up is a view of its own on the screen,
its key and its press sent there; a Control Panel pane of rows -- the
test's own, a part of the bundle, and the real Mouse pane -- is drawn and
worked by the panel's own rows (widgets/rows.hibr) in its process, cell
for cell what the panel draws in-process, a checkbox, an action, a list
and a step reaching the pane's own functions there. Another bundle's item
or pane named by a bundle's process is refused, and a pane that holds its
process past a stall is named held, as one in the desktop is.

    python3 tests/hostparts.py

In the hosted session nothing of the bundle is sourced: its app, its pane
and its item are registered as the desktop reads registrations, so no
in-process path can stand in for them; the in-process session sources the
same files, and the two are compared.
"""
import os, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import screen
from screen import Term, check, report, load, tree, scratch, expect

SLOW = 4 if os.environ.get("ASAN_OPTIONS") else 1
MOD = tree("build/mods/console.so")
WM = tree("programs/desktop/desktop.hibr")
PANEL = tree("programs/desktop/system/panel.hibr")
MOUSE = tree("programs/desktop/control-panel/mouse.hibr")
DATETIME = tree("programs/desktop/control-panel/datetime.hibr")
SAID = scratch("hostparts-said")
ROOT = scratch("hostparts-tree")
for d in ("apps", "control-panel", "menubar"):
    os.makedirs(os.path.join(ROOT, d), exist_ok=True)
APP = os.path.join(ROOT, "apps", "tbun.hibr")
BPANE = os.path.join(ROOT, "control-panel", "tbunp.hibr")
BBAR = os.path.join(ROOT, "menubar", "tbunb.hibr")
OPANE = os.path.join(ROOT, "control-panel", "opane.hibr")
OBAR = os.path.join(ROOT, "menubar", "obar.hibr")
SPANE = os.path.join(ROOT, "control-panel", "slowp.hibr")
# A terminal of the test's own, in the desktop in both sessions: what a
# window of it was made to run, written down as it opens.
TERM = scratch("hostparts-term.hibr")
open(TERM, "w").write(r'''fn term_draw(int id, int h, int w, int row, int col) {
	:
}
fn term_open(int id, arg = "") {
	printf 'term %%s %%s\n' "${DT[$id]["cmd0"]-} ${DT[$id]["cmd1"]-}" "${DT[$id]["openself"]-}" >> %s
}
dt_app term "Terminal" 10 40
''' % SAID)

# The bundle: an app with no window, whose parts are a rows pane and a bar
# item with a hook and a pop-up; two functions it can be told to run, each
# naming what is another's.
open(APP, "w").write(r'''# A bundle of the test's own.
# part: control-panel/tbunp.hibr
# part: menubar/tbunb.hibr
tbun_steal() {
	dt_baritem obar "STOLEN"
}
tbun_claim() {
	local l
	l := dt_rtline has opane draw key
	send "$DT_RTFD" "$l"
}
''')
open(BPANE, "w").write(r'''command -v cp_pane > /dev/null && cp_pane tbunp "Bundle Pane" "+"
TBP_ON=0
TBP_N=0
TBP_V=a
fn tbunp_rows(int w) {
	CP[$w][0]["kind"]=head; CP[$w][0]["text"]="Bundle"
	CP[$w][1]["kind"]=set; CP[$w][1]["text"]="Switch"; CP[$w][1]["key"]=sw
	CP[$w][1]["wtype"]=bool; CP[$w][1]["val"]=off
	[ "$TBP_ON" = 1 ] && CP[$w][1]["val"]=on
	CP[$w][2]["kind"]=set; CP[$w][2]["text"]="Letter"; CP[$w][2]["key"]=lt
	cp_choice "$w" 2 "$TBP_V" tbunp_setv a b c
	CP[$w][3]["kind"]=action; CP[$w][3]["text"]="Count $TBP_N"; CP[$w][3]["key"]=go
	CP[$w][3]["val"]=""
	ret 4
}
fn tbunp_do(int w, key, int dir) {
	case $key in
	sw) TBP_ON=$((1 - TBP_ON)) ;;
	go) TBP_N=$((TBP_N + 1)) ;;
	esac
	printf 'do %%s %%s\n' "$$" "$key" >> %s
}
fn tbunp_setv(int w, v) {
	TBP_V=$v
	printf 'set %%s %%s\n' "$$" "$v" >> %s
}
''' % (SAID, SAID))
open(BBAR, "w").write(r'''dt_baritem tbunb "B:-"
dt_barilabel tbunb "Bundle Item"
dt_baripress tbunb tbunb_press
dt_barhook tbunb_hook
TBB_K=
TBB_P=0
tbunb_hook() {
	[ "$DT_NOTEMS" = "$TBB_K" ] && return 0
	TBB_K=$DT_NOTEMS
	dt_baritem tbunb "B:$TBB_K"
	printf 'hook %%s %%s\n' "$$" "$TBB_K" >> %s
	return 0
}
fn tbunb_press(btn, int r, int c) {
	dt_baripop tbunb tbunb 4 20
	return 0
}
fn tbunb_popdraw(int r, int c, int h, int w) {
	console put "$((r + 1))" "$((c + 2))" "POP $TBB_P"
}
fn tbunb_popkey(key) {
	[ "$key" = x ] || return 1
	TBB_P=$((TBB_P + 1))
	printf 'popkey %%s\n' "$$" >> %s
	return 0
}
fn tbunb_popmouse(act, int r, int c) {
	printf 'popmouse %%s %%s\n' "$$" "$act" >> %s
}
''' % (SAID, SAID, SAID))
# Another's: a pane and an item no bundle declares, and a pane that holds
# its process a quarter of a second each time it draws.
open(OPANE, "w").write(r'''command -v cp_pane > /dev/null && cp_pane opane "Other Pane" "+"
fn opane_draw(int id, int h, int w, int bx) {
	console put -p "w$id" 1 "$bx" "other"
}
''')
open(OBAR, "w").write('dt_baritem obar "O:1"\n')
open(SPANE, "w").write(r'''command -v cp_pane > /dev/null && cp_pane slowp "Slow Pane" "+"
fn slowp_draw(int id, int h, int w, int bx) {
	local t=$((${EPOCHREALTIME/./} + 250000))
	while [ "${EPOCHREALTIME/./}" -lt "$t" ]; do :; done
	console put -p "w$id" 1 "$bx" "slow"
}
fn slowp_click(int id, int r, int c) {
	:
}
''')

SESSION = r'''%s. %s
dt_open
DT_NOTEMS=60000
DT_SAVERSECS=100000
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
HOSTED = r'''DT_LOADING=%s . %s
DT_LOADING=%s cp_pane mouse "Mouse" "◎" hardware
DT_LOADING=%s dt_app tbun "Test Bundle" 10 30
DT_LOADING=%s cp_pane tbunp "Bundle Pane" "+"
DT_LOADING=%s dt_baritem tbunb ""
DT_LOADING=%s cp_pane opane "Other Pane" "+"
DT_LOADING=%s dt_baritem obar "O:1"
DT_LOADING=%s cp_pane slowp "Slow Pane" "+"
DT_LOADING=%s cp_pane datetime "Date & Time" "◷" hardware
DT_LOADING=%s dt_app dtz "Time Zone" 16 46 "" "" fixed hidden
DT_LOADING=%s dt_app dts "Set Date & Time" 10 46 "" "" fixed hidden
DT_LOADING=%s dt_app dtf "Clock Format" 9 50 "" "" fixed hidden
DT_LOADING=%s . %s
DT_LOADING=''' % (PANEL, PANEL, MOUSE, APP, BPANE, BBAR, OPANE, OBAR, SPANE,
                  DATETIME, DATETIME, DATETIME, DATETIME, TERM, TERM)
INPROC = r'''DT_LOADING=%s . %s
DT_LOADING=%s . %s
DT_LOADING=%s . %s
DT_LOADING=%s . %s
DT_LOADING=%s . %s
DT_LOADING=%s . %s
DT_LOADING=''' % (PANEL, PANEL, MOUSE, MOUSE, BPANE, BPANE, BBAR, BBAR, DATETIME, DATETIME, TERM, TERM)


def session(name, body):
    p = scratch("hostparts-%s.hibr" % name)
    mark = scratch("hostparts-do-%s" % name)
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
    deadline = time.time() + secs * SLOW
    while time.time() < deadline:
        if result(t, "if %s; then echo yes; else echo no; fi" % cmd) == "yes":
            return True
    return False


def log(t):
    return open(t.log, errors="replace").read() if os.path.exists(t.log) else ""


def said():
    return [l for l in open(SAID).read().split("\n") if l] if os.path.exists(SAID) else []


def saidwait(line, secs=5):
    deadline = time.time() + secs * SLOW
    while time.time() < deadline and line not in said():
        t.collect(0.1)
    return line in said()


def rect(sc, r, c, h, w):
    return [(sc.g[y][x], sc.p[y][x]) for y in range(r, r + h) for x in range(c, c + w)]


def geom(t, var):
    v = result(t, 'echo "${DT[$%s]["row"]} ${DT[$%s]["col"]} ${DT[$%s]["h"]} ${DT[$%s]["w"]}"'
               % (var, var, var, var))
    return [int(x) for x in v.split()] if len(v.split()) == 4 else [0, 0, 1, 1]


def body(term):
    """The pane's body in the panel's window: right of its list, inside its
    frame -- the list differs, the hosted session having more panes."""
    r, c, h, w = geom(term, "P")
    lw = result(term, 'echo "${PW[$P]["lw"]-}"')
    lw = int(lw) if lw.isdigit() else 19
    return [r + 1, c + lw + 2, h - 2, w - lw - 3]


def agree(t, u, region, secs=6):
    """Whether region shows the same cells, glyphs and pens, in both
    sessions, read until it does or secs pass."""
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


def onscreen(term, text, secs=6):
    deadline = time.time() + secs * SLOW
    while time.time() < deadline:
        term.collect(0.1)
        if text in term.screen().text():
            return True
    return False


def find(term, text):
    sc = term.screen()
    for y in range(sc.rows):
        x = "".join(sc.g[y]).find(text)
        if x >= 0:
            return y, x
    return None, None


def click(term, r, c):
    term.send(screen.press(r, c), settle=0.1, collect=0.1)
    term.send(screen.release(r, c), settle=0.1, collect=0.1)


if os.path.exists(SAID):
    os.unlink(SAID)
t = session("hosted", HOSTED)
u = session("inproc", INPROC)
for name, term in (("hosted", t), ("in-process", u)):
    if result(term, "echo up") != "up":
        print("the %s session did not start:\n%s\n%s" % (name, term.screen().text(), log(term)[-3000:]))
        sys.exit(1)

# The bundle's bar item, with its hook registered as the desktop reads it:
# starting the item starts its app's process, which reads the app's file
# and each part, so the item, its pane and its hook are that process's.
do(t, "DT_LOADING=%s dt_barhook tbunb_hook; DT_LOADING=; dt_rthostb tbunb" % BBAR)
check("starting a bundle's bar item starts its app's process, which says "
      "hello as the app and has the bundle's pane and item both",
      waitfor(t, '[ -n "${DT_RVPC[tbunp]-}" ] && [ "${DT_RVB[tbunb]-}" = "${DT_RVPC[tbunp]}" ] && '
              '[ "${DT_RTC[${DT_RVPC[tbunp]}]-}" = tbun ]'),
      (result(t, 'echo "${DT_RVPC[tbunp]-} ${DT_RVB[tbunb]-} ${DT_RTC[${DT_RVPC[tbunp]-0}]-}"'), log(t)[-1500:]))
bpid = result(t, 'echo "${DT_RTCPID[${DT_RVPC[tbunp]}]-}"')
check("its hook runs in that process, from its first pass, and puts its "
      "text on the bar", onscreen(t, "B:60000") and "hook %s 60000" % bpid in said(),
      (said(), bpid, t.screen().text()[:220]))
check("and the desktop runs it no more, nor ever defined it",
      result(t, '{ case " ${DT_BARHOOKS[*]-} " in *" tbunb_hook "*) printf "on ";; *) printf "off ";; esac; '
             'echo "${DT_RVHOOKS[tbunb_hook]-} $(command -v tbunb_hook || echo none)"; }')
      == "off %s none" % BBAR,
      result(t, 'echo "${DT_BARHOOKS[*]-} / ${!DT_RVHOOKS[*]-}"'))
do(t, "DT_NOTEMS=61000")
check("a setting the hook reads, changed in the desktop, reaches its process, "
      "whose hook runs again and tells the new text",
      onscreen(t, "B:61000") and "hook %s 61000" % bpid in said(), (said(), log(t)[-800:]))

# Its pop-up: a press on the item opens it in its own process, drawn in a
# view of its own; its key and press go there, and Escape closes it.
do(u, "DT_NOTEMS=61000")
onscreen(u, "B:61000")
r, c = find(t, "B:61000")
for term in (t, u):
    rr, cc = find(term, "B:61000")
    if rr is not None:
        click(term, rr, cc + 1)


def pop(term):
    rr, cc = find(term, "POP ")
    return [rr - 1, cc - 2, 4, 20] if rr is not None else None


ok, why = agree(t, u, pop) if onscreen(t, "POP 0") else (False, (log(t)[-1500:], t.screen().text()))
check("a press on the item opens its pop-up, drawn by its process in a view "
      "on the screen, cell for cell the in-process one",
      ok and result(t, 'echo "${!DT_RVX[*]}"').split().count("otbunb") == 1, why)
for term in (t, u):
    term.send(b"x", settle=0.2, collect=0.2)
check("a key in the pop-up reaches its _popkey in that process, and its "
      "redraw comes back", saidwait("popkey %s" % bpid) and onscreen(t, "POP 1"),
      (said(), t.screen().text()[:400]))
pr, pc = find(t, "POP 1")
if pr is not None:
    click(t, pr, pc)
check("a press in it reaches its _popmouse there",
      saidwait("popmouse %s press" % bpid), said())
t.send(b"\x1b", settle=0.3, collect=0.3)
check("Escape closes it, and its view is let go",
      waitfor(t, '[ -z "$DT_POP" ] && [ -z "${DT_RVX[otbunb]+x}" ]', 4) and "POP 1" not in t.screen().text(),
      (result(t, 'echo "$DT_POP / ${!DT_RVX[*]}"'), t.screen().text()[:400]))
u.send(b"\x1b", settle=0.3, collect=0.3)

# The bundle's pane: rows, drawn and worked in its process by the panel's
# own rows, against the same pane in-process.
do(t, "P := dt_launch panel tbunp")
do(u, "P := dt_launch panel tbunp")
ok, why = agree(t, u, body, secs=8)
check("the bundle's rows pane is cell for cell the in-process one, drawn in "
      "the bundle's process, the desktop never defining its functions",
      ok and result(t, 'command -v tbunp_rows tbunp_draw cp_rowsdraw | tr "\\n" " "').split() == ["cp_rowsdraw"]
      and result(t, 'echo "${DT_RVXP[p$P]-}"') == "tbunp", why)
pr0, pc0, ph, pw = geom(t, "P")
sr, sc_ = find(t, "Switch")
cr = None
if sr is not None:
    line = "".join(t.screen().g[sr])
    cr = max(line.rfind("["), line.rfind("☐"), line.rfind("☑"))
for term in (t, u):
    rr, _ = find(term, "Switch")
    if rr is not None and cr is not None and cr > 0:
        click(term, rr, cr)
check("a click on its checkbox reaches the pane's _do in the bundle's process",
      saidwait("do %s sw" % bpid), (said(), cr, t.screen().text()))
ok, why = agree(t, u, body, secs=6)
check("and its rows are drawn again, switched, as in-process", ok, why)
for term in (t, u):
    term.send(b"\x1b[B\x1b[B", settle=0.2, collect=0.2)
    term.send(b"\r", settle=0.3, collect=0.3)
check("the keys move its selection and Enter acts on an action row, there",
      saidwait("do %s go" % bpid) and onscreen(t, "Count 1"), (said(), t.screen().text()))
ok, why = agree(t, u, body, secs=6)
check("and the selection and the count are drawn as in-process", ok, why)
for term in (t, u):
    term.send(b"\x1b[A", settle=0.2, collect=0.2)
    term.send(b"\x1b[C", settle=0.3, collect=0.3)
check("the right arrow steps a list row through the pane's setter, there",
      saidwait("set %s b" % bpid), said())
ok, why = agree(t, u, body, secs=6)
check("and the stepped value is drawn as in-process", ok, why)

# The Mouse pane, a real one that is no bundle's part: started by its file,
# drawn and stepped in its own process, its setting the desktop's.
do(t, "dt_rthostk pane mouse")
waitfor(t, '[ -n "${DT_RVPC[mouse]-}" ]')
do(t, "panel_goto $P mouse")
do(u, "panel_goto $P mouse")
ok, why = agree(t, u, body, secs=8)
check("the Mouse pane drawn by its own process is cell for cell the "
      "in-process one", ok and result(t, 'command -v mouse_rows || echo none') == "none", why)
mpid = result(t, 'echo "${DT_RTCPID[${DT_RVPC[mouse]}]-}"')
was = result(t, 'echo "$DT_SCROLLWHEEL"')
# A click on the row's label selects it, in the pane's process here.
for term in (t, u):
    rr, cc = find(term, "Wheel Scrolls")
    if rr is not None:
        click(term, rr, cc + 2)
for term in (t, u):
    term.send(b"\x1b[C", settle=0.3, collect=0.3)
check("a step on its Wheel Scrolls row sets the desktop's own setting, "
      "through dt_set from the pane's process",
      waitfor(t, '[ "$DT_SCROLLWHEEL" != "%s" ]' % was, 4) and
      waitfor(u, '[ "$DT_SCROLLWHEEL" != "%s" ]' % was, 4),
      (was, result(t, 'echo "$DT_SCROLLWHEEL"'), log(t)[-800:]))
ok, why = agree(t, u, body, secs=6)
check("and the pane is drawn with it as in-process", ok, why)

# Date & Time's pane in its own process: a dialog it opens is placed over
# the panel's window, which that process is sent (DT's rtpane), and a
# command it runs in a terminal is the request's own words.
do(t, "dt_rthostk pane datetime")
waitfor(t, '[ -n "${DT_RVPC[datetime]-}" ]')
for term in (t, u):
    do(term, "dt_move $P 2 1; panel_goto $P datetime")
waitfor(t, '[ -n "${DT_RVX[p$P]+x}" ] && [ "${DT_RVXP[p$P]-}" = datetime ]', 6)


def dialog(term):
    return result(term, 'for i in "${!DT[@]}"; do [ "${DT[$i]["title"]-}" = "Clock Format" ] && '
                  'echo "${DT[$i]["row"]} ${DT[$i]["col"]}"; done')


do(t, 'dt_rttell "${DT_RTCTOK[${DT_RVPC[datetime]}]}" dtf_dialog')
do(u, "dtf_dialog")
deadline = time.time() + 5 * SLOW
while time.time() < deadline and not dialog(t):
    t.collect(0.1)
check("a dialog the hosted pane opens is placed over the panel's window, "
      "as in-process, its process sent the window it is drawn in",
      dialog(t) != "" and dialog(t) == dialog(u)
      and result(t, 'echo "${DT[$P]["rtpane"]-}"') == DATETIME,
      (dialog(t), dialog(u), geom(t, "P"), result(t, 'echo "${DT[$P]["rtpane"]-}"')))
do(t, 'dt_rttell "${DT_RTCTOK[${DT_RVPC[datetime]}]}" dtp_run echo hi')
check("a command the hosted pane runs in a terminal reaches the new "
      "window's own record, from the request's own words",
      saidwait("term echo hi 1"), (said(), log(t)[-800:]))
do(t, "panel_goto $P tbunp")

# What is another's is refused: an item, and a pane said to be had.
expect(r"refused: dt_baritem: .* named the bar item obar, which is not its own")
do(t, 'dt_rttell "${DT_RTCTOK[${DT_RVPC[tbunp]}]}" tbun_steal')
deadline = time.time() + 3 * SLOW
while time.time() < deadline and "named the bar item obar" not in log(t):
    t.collect(0.1)
check("the bundle's process naming another's bar item is refused, and the "
      "item stays", "named the bar item obar, which is not its own" in log(t)
      and onscreen(t, "O:1", 1) and "STOLEN" not in t.screen().text(), log(t)[-1500:])
do(t, 'dt_rttell "${DT_RTCTOK[${DT_RVPC[tbunp]}]}" tbun_claim')
deadline = time.time() + 3 * SLOW
while time.time() < deadline and "what opane has" not in log(t):
    t.collect(0.1)
check("and saying it has another's pane is refused: the pane stays the "
      "desktop's", "said what opane has, which its file does not register; refused" in log(t)
      and result(t, 'echo "${DT_RVP[opane]-none} ${DT_RVFN[opane_draw]-none}"') == "none none",
      log(t)[-1500:])

# A pane that holds its process past a stall: the person's input to it is
# named held, judged from outside by a window's rule, the same budget.
expect(r"held: input:\S*slowp\.hibr has not answered the person's input to pane:slowp:click")
do(t, "dt_rthostk pane slowp")
waitfor(t, '[ -n "${DT_RVPC[slowp]-}" ]')
do(t, "panel_goto $P slowp")
onscreen(t, "slow", 6)
r, c = find(t, "slow")
if r is not None:
    click(t, r, c)
deadline = time.time() + 4 * SLOW
while time.time() < deadline and "has not answered the person's input to pane:slowp:click" not in log(t):
    t.collect(0.2)
check("a click on a pane whose process is held past a stall is named held in "
      "the log, its process and the input, as a window's is",
      "has not answered the person's input to pane:slowp:click" in log(t)
      and onscreen(t, "slow", 1), "\n".join(l for l in log(t).split("\n") if "slowp" in l or "held" in l)[-2500:])

# Each ends: the panel closing leaves the bundle's process, which has an
# item on the bar; killed, its item, its pop-up's prefix and its hook go.
do(t, "dt_del $P")
time.sleep(0.5 * SLOW)
check("the panel closing does not end the bundle's process, whose item is "
      "on the bar for the session", bpid and os.path.exists("/proc/%s" % bpid)
      and onscreen(t, "B:61000", 1), log(t)[-800:])
os.kill(int(bpid), 15) if bpid else None
check("its process gone, its item goes off the bar and its hook is the "
      "desktop's list's again, with nothing of it left hosted",
      waitfor(t, '[ -z "${DT_RVB[tbunb]+x}" ] && [ -z "${DT_RVHOOKS[tbunb_hook]+x}" ] && '
              '[ -z "${DT_RVPOP[tbunb]+x}" ] && [ -z "${DT_RVFN[tbunb_popdraw]+x}" ]', 4)
      and not onscreen(t, "B:61000", 1),
      (result(t, 'echo "${!DT_RVB[*]} / ${!DT_RVHOOKS[*]} / ${!DT_RVPOP[*]}"'), log(t)[-1000:]))
expect(r"tbunb_hook: command not found|tbunb_hook: not found")

u.close()
t.close()
for f in (APP, BPANE, BBAR, OPANE, OBAR, SPANE, TERM, SAID, t.mark, u.mark, SAID + ".pend",
          scratch("hostparts-hosted.hibr"), scratch("hostparts-inproc.hibr")):
    if os.path.exists(f):
        os.unlink(f)
for d in ("apps", "control-panel", "menubar", ""):
    try:
        os.rmdir(os.path.join(ROOT, d))
    except OSError:
        pass
report(24)
