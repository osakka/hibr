#!/usr/bin/env python3
"""How long the person's input takes to reach the screen, per app (ADR
0050's sensitivity requirement, wm/latency.hibr): an app whose key and the
draw after it each stay under a stall, but together put the screen past
DT_HOLDMS, is named `late:` in the log -- which the suites fail on, as on
a `held:` line -- and an app that answers at once is not; each app's
inputs and times are in `desktop ctl stats`. Spent on the processor, never
waited for by the clock, so a loaded gate neither trips nor hides it.

    python3 tests/inputlat.py
"""
import json, os, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import screen
from screen import Term, check, report, load, tree, scratch, expect

MOD = tree("build/mods/console.so")
WM = tree("programs/desktop/desktop.hibr")
OUT = scratch("inputlat-out")
MARK = scratch("inputlat-do")
HOLD = int(os.environ["DT_HOLDMS"])

SESSION = r'''%s. %s
dt_open
# Spend ms of this process's own processor time, whatever the load: a wait
# by the clock would spend less of it on a busy machine.
fn lt_spin(int ms) {
	local s n
	read -r s _ _ < "/proc/$$/schedstat"
	n=$s
	while [ $(((n - s) / 1000000)) -lt "$ms" ]; do
		read -r n _ _ < "/proc/$$/schedstat"
	done
}
LT_SLOW=
fn lt_draw(int id, int h, int w, int row, int col) {
	console put -p "w$id" 1 1 "slow app"
	[ -n "$LT_SLOW" ] || return 0
	LT_SLOW=
	lt_spin %d
}
fn lt_key(int id, key) {
	[ "$key" = s ] || return 1
	LT_SLOW=1
	lt_spin %d
	return 0
}
fn fa_draw(int id, int h, int w, int row, int col) { console put -p "w$id" 1 1 "fast app"; }
fn fa_key(int id, key) { [ "$key" = f ]; }
dt_app lt "Slow" 6 30
dt_app fa "Fast" 6 30
LTID := dt_new "Slow" 6 30 2 2 lt
FAID := dt_new "Fast" 6 30 2 40 fa
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
spin = HOLD * 6 // 10
p = scratch("inputlat.hibr")
open(p, "w").write(SESSION % (load(MOD), WM, spin, spin, MARK, MARK, MARK))
for f in (OUT, MARK):
    if os.path.exists(f):
        os.unlink(f)
expect(r"^late: lt's window")
# In one process the key and the draw after it are one pass of the
# desktop's loop, which the pass's own check names as a stall with no app
# -- `late:` is what names the app.
expect(r"^held: pass held the desktop")
t = Term(p, rows=24, cols=90, settle=0.6)
t.until("fast app", tries=60, step=0.1)


def do(cmd):
    """Run cmd in the desktop, from its ticker, and wait until it is taken."""
    open(MARK, "w").write(cmd + "\n")
    deadline = time.time() + 5
    while os.path.exists(MARK) and time.time() < deadline:
        t.send(b"\x1b[I", settle=0.1, collect=0.1)


def log():
    return open(os.path.join(screen.HOME, str(t.pid), "state", "hibr", "desktop.log"),
                errors="replace").read()


def stats():
    if os.path.exists(OUT):
        os.unlink(OUT)
    do("x := dt_latjson; printf '{%%s}' \"$x\" > %s" % OUT)
    deadline = time.time() + 5
    while time.time() < deadline and not os.path.exists(OUT):
        time.sleep(0.05)
    try:
        return json.load(open(OUT))
    except (OSError, ValueError):
        return {}


# The fast window first: typed into, it answers at once.
do('dt_raise "$FAID"')
for i in range(5):
    t.send(b"f", settle=0.2, collect=0.2)
time.sleep(0.5)
st = stats()
fa = st.get("fa", {})
check("input typed into a window that answers at once is counted for its app",
      fa.get("inputs", 0) >= 5 and fa.get("screen_ms") is not None, st)
check("and is not late", fa.get("late", 1) == 0 and "late: fa" not in log(), (st, log()[-300:]))

# The slow one: its key and the draw after it each under a stall, together
# past DT_HOLDMS before the screen shows the answer.
do('dt_raise "$LTID"')
t.send(b"s", settle=0.5, collect=0.5)
deadline = time.time() + 10 * max(1, HOLD // 300)
while time.time() < deadline and "late: lt" not in log():
    time.sleep(0.1)
check("input whose answer reaches the screen past DT_HOLDMS names the app late",
      "late: lt's window reached the screen" in log(), log()[-400:])
check("though no single callback of it was a stall: only the pass, which names no app",
      "held: input:lt" not in log() and "held: win:lt" not in log(), log()[-400:])
st = stats()
lt = st.get("lt", {})
check("and its late input is counted, its time to the screen past the mark",
      lt.get("late", 0) == 1 and lt.get("screen_max_ms", 0) >= HOLD, st)
check("its input to answer is kept too, under its time to the screen",
      0 < lt.get("answer_max_ms", 0) < lt.get("screen_max_ms", 0) + 1, st)

# A key the window declines is the desktop's from there on: what the desktop
# does with it -- Lock, Detach -- is never timed against the app.
t.send(b"z", settle=0.5, collect=0.5)
st2 = stats()
do('echo "[$DT_INPEND][$DT_INWIN]" > %s.pend' % OUT)
pend = open(OUT + ".pend").read().strip() if os.path.exists(OUT + ".pend") else "?"
check("a key the window declines is no input of its app's, and nothing waits on it",
      st2.get("lt", {}).get("inputs") == lt.get("inputs") and pend == "[][]", (lt, st2.get("lt"), pend))

t.quit(b"q", 1.0)

# A window like a terminal's: clean until its program echoes, which here
# is 200 ms after the key -- an ssh session's echo. The program being slow
# is not the window's app being late: judged by the clock, at the mark a
# person's desktop has (150 ms), nothing is named.
ECHO = r'''%s. %s
dt_open
EC_AT=0
EC_SAID=
fn ec_draw(int id, int h, int w, int row, int col) { console put -p "w$id" 1 1 "echo app ${EC_SAID}"; }
EC_NOW=
fn ec_key(int id, key) {
	case $key in
	n) EC_NOW=1; return 0 ;;
	e) EC_AT=${EPOCHREALTIME/./}; dt_want 250; return 0 ;;
	esac
	return 1
}
fn ec_dirty(int id) {
	[ -z "$EC_NOW" ] || { EC_NOW=; return 0; }
	if [ "$EC_AT" -gt 0 ] && [ $((${EPOCHREALTIME/./} - EC_AT)) -lt 200000 ]; then
		dt_want 30
		return 1
	fi
	[ "$EC_AT" -gt 0 ] || return 1
	EC_AT=0
	EC_SAID=echoed
	return 0
}
dt_app ec "Echo" 6 30
ECID := dt_new "Echo" 6 30 2 2 ec
# A resize settles for longer than late is, so a key that comes during one
# waits past the mark before it is acted on -- the desktop's wait.
DT_RSTILL=400
dt_run
dt_close
'''
p2 = scratch("inputlat-echo.hibr")
open(p2, "w").write(ECHO % (load(MOD), WM))
t = Term(p2, rows=24, cols=90, settle=0.6, env={"DT_HOLDBY": "wall", "DT_HOLDMS": "150"})
t.until("echo app", tries=60, step=0.1)
t.send(b"e", settle=0.3, collect=0.3)
sc = t.until("echoed", tries=40, step=0.1)
check("a window whose program echoes 200 ms after the key is drawn with the echo",
      sc is not None and sc.find("echoed") is not None, sc)
check("and its app is not named late for the program's slowness",
      "late:" not in log(), log()[-400:])
# A key that comes while the terminal is being resized is acted on once the
# resize settles; that wait is the desktop's, and the app answers at once.
t.resize(26, 92)
t.send(b"n", settle=1.2, collect=0.3)
check("a key that came during a resize is not charged with the resize's wait",
      "late:" not in log(), log()[-400:])
t.quit(b"q", 1.0)
for f in (p, p2, OUT, MARK, OUT + ".pend"):
    if os.path.exists(f):
        os.unlink(f)
report(11)
