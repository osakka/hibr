#!/usr/bin/env python3
"""A real app drawing and taking keys in its own process (ADR 0050, the
spine): the desktop starts it (dt_rthost), binds its window's view, the app
draws through its headless console into cell records on a channel of its
own, the desktop shows them where the app's _draw would have run, and a key
or a click on the window reaches the app's own _key or _click, its redraw
coming back drawn, never held. Close asks the app and holds the window, and
the process goes when it has no window left.

    python3 tests/hosted.py

Each comparison is against the same app drawn in-process, cell for cell,
glyphs and pens: the Clock in one session beside its hosted twin, at a
moment both show the same second (nothing can pin `clock_draw`'s now); the
Calculator in two sessions, the hosted one never sourcing calc.hibr, so no
in-process path can stand in for it.
"""
import os, sys, time, re, statistics
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import screen
from screen import Term, check, report, load, tree, scratch, expect

MOD = tree("build/mods/console.so")
# The sanitizer build runs several times slower, so a wait for a process to
# end widens with it, as the other suites' do.
SLOW = 4 if "ASAN_OPTIONS" in os.environ else 1
WM = tree("programs/desktop/desktop.hibr")
CLOCK = tree("programs/desktop/apps/Accessories/clock.hibr")
CALC = tree("programs/desktop/apps/Accessories/calc.hibr")
SAID = scratch("hosted-said")
CLOSER = scratch("hosted-closer.hibr")
HANGER = scratch("hosted-hanger.hibr")

# Two apps of the test's own, in files a process of their own reads: one
# whose _draw sets no pen of its own -- so it shows the pen the desktop
# leaves before a window's draw -- and whose _close writes what it reads of
# its window and which process it ran in; one whose _close never returns.
open(CLOSER, "w").write(r'''command -v dt_app > /dev/null && dt_app closer "Closer" 6 22
fn closer_draw(int id, int h, int w, int row, int col) {
	console put -p "w$id" 1 1 "plain pen"
	console put -p "w$id" 2 1 "h=$h w=$w"
}
fn closer_close(int id) {
	printf '%%s\t%%s\t%%s\n' "$$" "${DT[$id]["title"]-none}" "${DT[$id]["w"]-}" > %s.closed
}
''' % SAID)
open(HANGER, "w").write(r'''command -v dt_app > /dev/null && dt_app hanger "Hanger" 6 22
fn hanger_draw(int id, int h, int w, int row, int col) {
	console put -p "w$id" 1 1 "hangs on close"
	echo "$$" > %s.hpid
}
fn hanger_close(int id) {
	local e=$((SECONDS + 30))
	while [ "$SECONDS" -lt "$e" ]; do :; done
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

# The hosted session: the Clock and the closer sourced in-process too, for
# their twins, each draw of theirs counted by window; the Calculator and the
# hanger only registered, read and never run here.
HOSTED = r'''DT_LOADING=%s . %s
DT_LOADING=%s . %s
DT_LOADING=
eval "$(declare -f clock_draw | sed '1s/clock_draw(/clock_real(/')"
eval "$(declare -f closer_draw | sed '1s/closer_draw(/closer_real(/')"
HB_IDS=
clock_draw() { HB_IDS+=" $1"; clock_real "$@"; }
closer_draw() { HB_IDS+=" $1"; closer_real "$@"; }
DT_LOADING=%s dt_app calc "Calculator" 16 24 once "x" fixed
DT_LOADING=%s dt_app hanger "Hanger" 6 22
DT_LOADING=''' % (CLOCK, CLOCK, CLOSER, CLOSER, CALC, HANGER)
# The in-process session: the Calculator sourced as a desktop sources it.
INPROC = r'''DT_LOADING=%s . %s
DT_LOADING=''' % (CALC, CALC)


def session(name, body):
    p = scratch("hosted-%s.hibr" % name)
    mark = scratch("hosted-do-%s" % name)
    open(p, "w").write(SESSION % (load(MOD), WM, body, mark, mark, mark))
    term = Term(p, rows=30, cols=120, settle=0.6, env={"DT_BARTIME": "12:00"})
    # Each session its own: a command meant for one must never be taken by
    # the other's ticker.
    term.mark = mark
    return term


def do(t, cmd):
    """Run cmd in t's desktop, from its ticker, and wait until it is taken."""
    screen.putcmd(t.mark, cmd + "\n")
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
    """Whether a process has ended: no longer there, or ended and not yet
    reaped by the desktop that started it (a zombie still has /proc)."""
    try:
        return open("/proc/%s/stat" % pid).read().rsplit(")", 1)[1].split()[0] == "Z"
    except (OSError, IndexError):
        return True


def log(t):
    return open(t.log, errors="replace").read() if os.path.exists(t.log) else ""


def rect(sc, r, c, h, w):
    """A window's cells, glyph and pen each, with its frame."""
    return [(sc.g[y][x], sc.p[y][x]) for y in range(r, r + h) for x in range(c, c + w)]


def geom(t, var):
    v = result(t, 'echo "${DT[$%s]["row"]} ${DT[$%s]["col"]} ${DT[$%s]["h"]} ${DT[$%s]["w"]}"'
               % (var, var, var, var))
    return [int(x) for x in v.split()] if len(v.split()) == 4 else [0, 0, 1, 1]


def twins(t, a, b, secs=6):
    """Whether windows a and b show the same cells on one screen, read until
    they do or secs pass: the one thing waited out is a second turning
    between the two draws, which a real difference never survives."""
    ga, gb = geom(t, a), geom(t, b)
    deadline = time.time() + secs
    ra = rb = None
    while time.time() < deadline:
        t.collect(0.1)
        sc = t.screen()
        ra, rb = rect(sc, *ga), rect(sc, *gb)
        if ga[2:] == gb[2:] and ra == rb:
            return True, ""
    diff = [(i, x, y) for i, (x, y) in enumerate(zip(ra or [], rb or [])) if x != y][:6]
    return False, "%s %s first differences %s\n%s" % (ga, gb, diff, t.screen().text())


t = session("hosted", HOSTED)

# The Clock and its twin: in-process first, then the hosted one beside it,
# a third window focused so both frames are drawn alike.
do(t, "A := dt_new Clock 7 26 3 2 clock")
do(t, "dt_rthost clock")
check("the Clock's own process says hello and opens its cells channel",
      waitfor(t, '[ -n "${DT_RTHOST[clock]-}" ]'), log(t)[-800:])
do(t, "B := dt_new Clock 7 26 3 36 clock; O := dt_new Other 5 20 20 2")
ok, why = twins(t, "A", "B")
check("the hosted Clock's window is cell for cell, glyphs and pens, the "
      "in-process one", ok, why)
ids = result(t, 'echo "$HB_IDS"').split()
bid = result(t, 'echo "$B"')
check("and the desktop made no clock_* call for the hosted window",
      bid and bid not in ids and result(t, 'echo "$A"') in ids, (bid, ids[-10:]))
check("the hosted window is bound, at generation 1",
      result(t, 'echo "${DT_RVC[$B]-none} ${DT_RVGEN[$B]-}"').split()[-1:] == ["1"],
      result(t, 'echo "${DT_RVC[$B]-none} ${DT_RVGEN[$B]-}"'))

# A resize: the view bound again at a new generation, and the app's next
# frame at the new size whole -- against the in-process twin resized alike.
do(t, "dt_resize $A 9 30; dt_resize $B 9 30")
ok, why = twins(t, "A", "B")
check("resized, its view is bound again and drawn as in-process", ok, why)
check("at a generation of its own",
      result(t, 'echo "${DT_RVGEN[$B]-}"') == "2", log(t)[-600:])

# The closer's twin: its _draw sets no pen, so what it shows is the pen the
# runtime leaves before a window's draw, as dt_win leaves it in-process.
do(t, "KI := dt_new Closer 6 22 13 62 closer")
do(t, "dt_rthost closer")
waitfor(t, '[ -n "${DT_RTHOST[closer]-}" ]')
do(t, "K := dt_new Closer 6 22 13 36 closer; dt_raise $O")
ok, why = twins(t, "KI", "K")
check("a window whose _draw sets no pen is drawn in the pen in-process leaves", ok, why)
check("and a window opened while its app is hosted is never drawn in-process",
      result(t, 'echo "$K"') not in result(t, 'echo "$HB_IDS"').split(),
      result(t, 'echo "$HB_IDS"'))

# Close: asked of the app, which reads its window's record as it stands --
# retitled the moment before -- in its own process, and the window goes on
# its answer; the process, with no window left, ends by itself.
for f in (SAID + ".closed",):
    if os.path.exists(f):
        os.unlink(f)
kpid = result(t, 'echo "${DT_RTCPID[${DT_RVC[$K]}]-}"')
dpid = str(t.pid)
do(t, 'dt_retitle $K "Renamed Closer"; dt_del $K')
deadline = time.time() + 5
while time.time() < deadline and not os.path.exists(SAID + ".closed"):
    t.collect(0.05)
closed = open(SAID + ".closed").read().strip().split("\t") if os.path.exists(SAID + ".closed") else []
check("close is asked of the app's own process, which reads its window as it stands",
      len(closed) == 3 and closed[0] == kpid and closed[1] == "Renamed Closer" and closed[2] == "22",
      (closed, kpid, dpid))
check("and the window goes on its answer",
      waitfor(t, '! dt_iswin "$K"', 3), log(t)[-600:])
deadline = time.time() + 3
while time.time() < deadline and kpid and not ended(kpid):
    t.collect(0.1)
check("the process, with no window left, ends by itself",
      kpid and ended(kpid) and "process %s had no window" % kpid not in log(t),
      log(t)[-600:])

# One whose _close never returns: the window goes after DT_HOLDMS all the
# same, and the process is ended DT_RTENDSECS later.
do(t, "dt_rthost hanger")
waitfor(t, '[ -n "${DT_RTHOST[hanger]-}" ]')
do(t, "H := dt_new Hanger 6 22 21 36 hanger")
deadline = time.time() + 5
while time.time() < deadline and not os.path.exists(SAID + ".hpid"):
    t.collect(0.05)
hpid = open(SAID + ".hpid").read().strip() if os.path.exists(SAID + ".hpid") else ""
t0 = time.time()
do(t, "dt_del $H")
gone = waitfor(t, '! dt_iswin "$H"', 3 * SLOW)
check("a close the app never answers lets the window go after DT_HOLDMS",
      gone and "did not answer the close" in log(t), log(t)[-600:])
deadline = time.time() + 12 * SLOW
while time.time() < deadline and hpid and not ended(hpid):
    t.collect(0.2)
check("and the process is ended DT_RTENDSECS later",
      hpid and ended(hpid) and "process %s had no window left" % hpid in log(t)
      and time.time() - t0 >= 4.5, (hpid, round(time.time() - t0, 1), log(t)[-400:]))

# The Calculator, hosted, never sourced here.
do(t, "dt_rthost calc")
waitfor(t, '[ -n "${DT_RTHOST[calc]-}" ]')
check("the desktop never defined the Calculator's functions",
      result(t, 'command -v calc_draw || echo none') == "none")
do(t, "C := dt_new Calculator 16 24 3 92 calc")
u = session("inproc", INPROC)
do(u, "C := dt_new Calculator 16 24 3 92 calc")


def same(steps):
    """Send the same input to both and say whether the windows agree."""
    gt, gu = geom(t, "C"), geom(u, "C")
    for s in steps:
        t.send(s, settle=0.1, collect=0.1)
        u.send(s, settle=0.1, collect=0.1)
    deadline = time.time() + 5
    while time.time() < deadline:
        t.collect(0.05)
        u.collect(0.05)
        a, b = rect(t.screen(), *gt), rect(u.screen(), *gu)
        if gt == gu and a == b:
            return True, ""
    return False, "%s %s\n--- hosted\n%s\n--- in-process\n%s" % (
        gt, gu, t.screen().text(), u.screen().text())


ok, why = same([])
check("the hosted Calculator's window is cell for cell the in-process one", ok, why)
ok, why = same([b"1", b"2", b"+", b"3"])
check("keys reach its own _key and its redraw comes back drawn", ok, why)
gr, gc, gh, gw = geom(t, "C")
sc = t.screen()
seven = None
for y in range(gr, gr + gh):
    for x in range(gc, gc + gw):
        if sc.g[y][x] == "7":
            seven = (y, x)
            break
    if seven:
        break
ok, why = same([screen.press(*seven), screen.release(*seven)] if seven else [])
check("a click reaches its own _click", seven is not None and ok, why)
ok, why = same([b"\r"])
check("and enter works it out, as in-process", ok, why)


def latency(term, n=12):
    """Key to screen, ms, median of n: a digit sent, read until the window
    has changed."""
    g = geom(term, "C")
    out = []
    for i in range(n):
        term.collect(0.15)
        before = rect(term.screen(), *g)
        t0 = time.time()
        os.write(term.fd, str(i % 10).encode())
        term.sent += 1
        while time.time() - t0 < 3:
            term.collect(0.002)
            if rect(term.screen(), *g) != before:
                break
        out.append((time.time() - t0) * 1000)
    return statistics.median(out), max(out)


lh, lu = latency(t), latency(u)
print("key to screen, ms (median, worst): hosted %.1f %.1f, in-process %.1f %.1f"
      % (lh[0], lh[1], lu[0], lu[1]))
check("a key's echo is on screen within 70 ms of in-process",
      lh[0] < lu[0] + 70, (lh, lu))
# What one input report costs the desktop, hosted (the pass forced and the
# line said) against the same report run in-process -- a click on no key.
PER = 'C0=${EPOCHREALTIME/./}; for i in {1..200}; do dt_wincall "$C" calc_click 0 0 left; done; echo $(( (${EPOCHREALTIME/./} - C0) / 200 ))'
ph, pu = result(t, PER), result(u, PER)
print("one input report, us: hosted %s, in-process %s" % (ph, pu))

# The keys the desktop also acts on -- every chord of a shortcut or an app
# launcher's key, plain ones included, and the harness's q -- are each
# declined by the in-process Calculator; hosted, one is sent and the
# desktop's own action waits for the answer (wm/rtfail.hibr: the hold).
got = result(u, r'''for k in $(printf '%s\n' "${DT_KEYS[@]}" "${DT_APPKEY[@]}") q; do calc_key "$C" "$k"; echo "$k $?"; done | sort -u | tr '\n' ' ' ''')
pairs = re.findall(r"(\S+) (\d+) ", got)
taken = [x for x in pairs if x[1] != "1"]
print("the desktop's keys, each with in-process calc_key's status: %s" % got)
check("every key the desktop also acts on is declined in-process",
      len(pairs) >= 30 and not taken, (len(pairs), taken, got[:400]))
t.send(b"\x1b", settle=0.3, collect=0.3)
t.send(b"\x1b", settle=0.3, collect=0.3)
check("and one sent to a hosted window is declined by its own process, said "
      "in the log, and the desktop's own action runs",
      "hosted: key escape declined" in log(t), log(t)[-400:])

u.close()
t.close()
for f in (CLOSER, HANGER, t.mark, u.mark, SAID + ".pend", SAID + ".closed", SAID + ".hpid",
          scratch("hosted-hosted.hibr"), scratch("hosted-inproc.hibr")):
    if os.path.exists(f):
        os.unlink(f)
report(22)
