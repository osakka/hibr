#!/usr/bin/env python3
"""What a setting does beyond its value, on the desktop's side (ADR 0050,
dt_onset): dt_set runs each effect registered for a name it sets, once a
call and on every set; a setting with none redraws only what is drawn every
frame, so a window that says it is clean is left alone, where a colour is a
whole frame; and Partial Updates set from an app's own process reaches the
console through the desktop's own effect, not the pane's.

    python3 tests/onset.py
"""
import os, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import screen
from screen import Term, check, report, load, tree, scratch

MOD = tree("build/mods/console.so")
WM = tree("programs/desktop/desktop.hibr")
HERE = tree("programs/desktop")
SAID = scratch("onset-said")
MARK = scratch("onset-do")
APP = scratch("onset-app.hibr")

# An app's own process: the runtime, a hello, and one function the desktop
# tells it to run, which sets Partial Updates the one way an app may.
open(APP, "w").write(r'''DT_HERE=%s
. "$DT_HERE/runtime.hibr"
dt_rtopen onapp || exit 1
echo "$$" > %s.pid
fn onapp_off() { dt_set DT_IMGEDITS off; }
dt_rtrun
''' % (HERE, SAID))

# The desktop: a window whose app says it is clean once drawn, counting its
# draws; an effect of the session's own on two settings, counting its runs.
SESSION = r'''%s. %s
dt_open
TST_DRAWN=0
TST_FX=0
fn tst_draw(id, h, w, row, col) { TST_DRAWN=$((TST_DRAWN + 1)); }
fn tst_dirty(id) { [ "$TST_DRAWN" = 0 ]; }
tst_fx() { TST_FX=$((TST_FX + 1)); }
dt_onset DT_TIPMS DT_TIPS tst_fx
dt_app tst "Clean" 6 24
DT_LOADING=%s dt_app onapp "On App" 6 24
TW1 := dt_new "Clean" 6 24 4 10 tst
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
''' % (load(MOD), WM, APP, MARK, MARK, MARK)
p = scratch("onset.hibr")
open(p, "w").write(SESSION)
t = Term(p, rows=30, cols=100, settle=0.6, env={"DT_BARTIME": "12:00"})


def do(cmd):
    """Run cmd in the desktop, from its ticker, and wait until it is taken."""
    open(MARK, "w").write(cmd + "\n")
    deadline = time.time() + 5
    while os.path.exists(MARK) and time.time() < deadline:
        t.send(b"\x1b[I", settle=0.1, collect=0.1)


def result(cmd):
    """Run cmd, which writes its answer to SAID.pend, and read it."""
    if os.path.exists(SAID + ".pend"):
        os.unlink(SAID + ".pend")
    do(cmd)
    deadline = time.time() + 20
    while time.time() < deadline and not os.path.exists(SAID + ".pend"):
        t.collect(0.05)
    return open(SAID + ".pend").read().strip() if os.path.exists(SAID + ".pend") else "?"


def drawn(setting):
    """How often the clean window is drawn by the frame after `dt_set
    setting`, two frames having settled it first."""
    return result('dt_draw; dt_draw; n=$TST_DRAWN; dt_set %s; dt_draw; '
                  'echo $((TST_DRAWN - n)) > %s.pend' % (setting, SAID))


left = drawn("DT_TIPMS 700")
whole = drawn("DT_FACE '#112233'")
check("a setting with no effect leaves a window that says it is clean alone, and a colour draws it",
      left == "0" and whole.isdigit() and int(whole) > 0, "%s %s" % (left, whole))

fx = result('TST_FX=0; dt_set DT_TIPMS 650 DT_TIPS 0; echo "$TST_FX" > %s.pend' % SAID)
check("an effect runs once for a dt_set that sets two of its names", fx == "1", fx)
fx = result('dt_set DT_TIPMS 650; echo "$TST_FX" > %s.pend' % SAID)
check("and again when the same value is set again: on set, not on change", fx == "2", fx)

menus = result('dt_draw; dt_set DT_TIPMS 600; a=${DT_MENUIN:-built}; dt_set DT_MENUMAX 50; '
               'echo "$a ${DT_MENUIN:-rebuild}" > %s.pend' % SAID)
check("a menu's width rebuilds the menus and a tooltip delay does not", menus == "1 rebuild", menus)

ed = result('dt_set DT_IMGEDITS off; a=$DT_IMGEDIT; dt_set DT_IMGEDITS auto; '
            'echo "$a ${DT_IMGEDIT:-auto}" > %s.pend' % SAID)
check("Partial Updates through dt_set reaches the console, off and back (dt_imgapply)",
      ed == "off auto", ed)

do('dt_rtlisten; OT := dt_rtspawn onapp %s' % APP)
deadline = time.time() + 10
while time.time() < deadline and not os.path.exists(SAID + ".pid"):
    t.collect(0.05)
time.sleep(0.3)
do('dt_rttell "$OT" onapp_off')
hosted = "?"
deadline = time.time() + 10
while time.time() < deadline:
    hosted = result('echo "${DT_IMGEDITS} ${DT_IMGEDIT:-auto}" > %s.pend' % SAID)
    if hosted == "off off":
        break
    time.sleep(0.2)
check("set from an app's own process, the desktop's effect turns the console's editing off",
      hosted == "off off", hosted)

apid = int(open(SAID + ".pid").read()) if os.path.exists(SAID + ".pid") else 0
t.close()
if apid:
    try:
        os.kill(apid, 15)
    except OSError:
        pass
report(7)
