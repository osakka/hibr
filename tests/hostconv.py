#!/usr/bin/env python3
"""An app in a process of its own, registered by reading its file and never
running it (ADR 0050, Decision 3: dt_regread), and the desktop state it read
that no process of its own was sent (tools/appstate.py's CONVERT), each
carried now:

    python3 tests/hostconv.py

- registered read, not run: its file's dt_app, dt_keep (with the default
  its file writes) and opener made by the desktop from the parser's words,
  nothing of the file run in the desktop; then started and drawing;
- where a dialog opens (DT_DLGR and the rest): worked out in the app's own
  process from its copy, the same place the desktop works out;
- where a context menu was opened, and for whom (MB_CX, MB_CY, MB_CTXWHO):
  in the app's copy before the call that uses it;
- the settings file as it was read (DT_CONFJ): in its copy, read only;
- a window it asked for that is another app's: its record sent to it, and
  its requests on that window taken as its own (rtown).
"""
import os, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from screen import Term, check, report, load, tree, scratch

MOD = tree("build/mods/console.so")
WM = tree("programs/desktop/desktop.hibr")
SAID = scratch("hostconv-said")
APP = scratch("hostconv-app.hibr")
RAN = SAID + ".ran"

open(APP, "w").write(r'''command -v dt_app > /dev/null && dt_app convapp "Conv" 6 30 once "C"
CV_SIZE="${CV_SIZE-7}"
command -v dt_keep > /dev/null && dt_keep CV_SIZE
command -v dt_opener > /dev/null && dt_opener convview "Conv" "cvx" app convapp
echo "$$" >> %(s)s.ran
fn convapp_draw(int id, int h, int w, int row, int col) {
	console put -p "w$id" 1 1 "conv size $CV_SIZE"
}
fn convapp_dlg() {
	dt_dlgat 6 30
	echo "$DT_DLGR $DT_DLGC $DT_DLGH $DT_DLGW" > %(s)s.dlg
}
fn convapp_ctx() {
	echo "$MB_CX $MB_CY $MB_CTXWHO" > %(s)s.ctx
}
fn convapp_conf() {
	echo "${DT_CONFJ["settings"]["CVTEST"]-none}" > %(s)s.conf
}
fn convapp_mine() {
	local d
	d := dt_new "Asked For" 5 20 20 60 cvother
	echo "$d" > %(s)s.mid
}
fn convapp_seen(int d) {
	echo "${DT[$d]["title"]-none}" > %(s)s.seen
	dt_retitle "$d" "Renamed By Its Asker"
}
''' % {"s": SAID})

SESSION = r'''%s. %s
dt_open
DT_NOTEMS=60000
# A tree with no reader registers it by hand, so what follows can be told
# apart from the registration itself when run against such a tree.
if command -v dt_regread > /dev/null; then
	DT_APPDIR_CUR=Applications dt_regread %s
else
	DT_LOADING=%s dt_app convapp "Conv" 6 30 once "C"
fi
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


def clean():
    d = os.path.dirname(SAID)
    for f in os.listdir(d):
        if f.startswith(os.path.basename(SAID) + "."):
            os.unlink(os.path.join(d, f))


clean()
MARK = scratch("hostconv-do")
p = scratch("hostconv-session.hibr")
open(p, "w").write(SESSION % (load(MOD), WM, APP, APP, MARK, MARK, MARK))
t = Term(p, rows=30, cols=120, settle=0.6, env={"DT_BARTIME": "12:00"})


def do(cmd):
    open(MARK, "w").write(cmd + "\n")
    deadline = time.time() + 5
    while os.path.exists(MARK) and time.time() < deadline:
        t.send(b"\x1b[I", settle=0.05, collect=0.05)


def result(cmd):
    if os.path.exists(SAID + ".pend"):
        os.unlink(SAID + ".pend")
    do(cmd + " > %s.pend" % SAID)
    deadline = time.time() + 20
    while time.time() < deadline and not os.path.exists(SAID + ".pend"):
        t.collect(0.05)
    t.collect(0.05)
    return open(SAID + ".pend").read().strip() if os.path.exists(SAID + ".pend") else ""


def waitfor(cmd, secs=10):
    deadline = time.time() + secs
    while time.time() < deadline:
        if result("if %s; then echo yes; else echo no; fi" % cmd) == "yes":
            return True
    return False


def read(f, secs=6):
    deadline = time.time() + secs
    while time.time() < deadline and not os.path.exists(SAID + f):
        t.collect(0.1)
    t.collect(0.2)
    return open(SAID + f).read().strip() if os.path.exists(SAID + f) else ""


def log():
    return open(t.log, errors="replace").read() if os.path.exists(t.log) else ""


def tell(fn, *words):
    """Run one of the app's own functions in its process, the pass forced
    first, as the desktop calls an app's code."""
    do('dt_rttell "${DT_RTCTOK[${DT_RTHOST[convapp]}]}" %s %s' % (fn, " ".join(words)))


reg = result('echo "${DT_APPS[convapp]["title"]-}|${DT_APPS[convapp]["once"]-}|${DT_APPS[convapp]["dir"]-}|'
             '${CV_SIZE-unset}|${DT_OPENERS[convview]["app"]-}"')
unran = not os.path.exists(RAN) and result("command -v convapp_draw || echo none") == "none"
do("dt_rthost convapp")
waitfor('[ -n "${DT_RTHOST[convapp]-}" ]')
do("A := dt_new Conv 6 30 3 4 convapp")
ok = False
deadline = time.time() + 8
while time.time() < deadline and not ok:
    t.collect(0.1)
    ok = "conv size 7" in t.screen().text()
check("its registrations are made from its file read, never run -- the app, its kept setting with "
      "the default its file writes, its opener -- and so registered it starts in a process of its own "
      "and draws", reg == "Conv|1|Applications|7|convapp" and unran and ok and os.path.exists(RAN),
      (reg, unran, t.screen().text() + log()[-400:]))

# Where a dialog opens: the app's own working out, against the desktop's.
tell("convapp_dlg")
mine = read(".dlg")
desk = result('dt_dlgat 6 30; echo "$DT_DLGR $DT_DLGC $DT_DLGH $DT_DLGW"')
check("where a dialog opens is worked out in the app's own process, where the desktop works it out",
      mine and mine == desk and mine.split()[2:] == ["6", "30"], (mine, desk, log()[-300:]))

# Where a context menu was opened, and for whom.
do('MB_CX=17; MB_CY=9; MB_CTXWHO=convapp; dt_rttell "${DT_RTCTOK[${DT_RTHOST[convapp]}]}" convapp_ctx')
check("where a context menu opened, and for whom, is in its copy before the call that uses it",
      read(".ctx") == "17 9 convapp", (read(".ctx"), log()[-300:]))

# The settings file as it was read.
do('DT_CONFJ["settings"]["CVTEST"]=held-in-the-file')
tell("convapp_conf")
check("the settings file as it was read is in its copy", read(".conf") == "held-in-the-file",
      (read(".conf"), log()[-300:]))

# A window it asked for that another app draws.
tell("convapp_mine")
mid = read(".mid")
if mid.isdigit():
    tell("convapp_seen", mid)
read(".seen")
check("a window it asked for that another app draws has its record sent to it",
      read(".seen") == "Asked For", (read(".seen"), log()[-300:]))
check("and its requests on that window are its own",
      waitfor('[ "${DT[%s]["title"]-}" = "Renamed By Its Asker" ]' % (mid or "0"), 5),
      (result('echo "${DT[%s]["title"]-}"' % (mid or "0")), log()[-300:]))

t.close()
clean()
for f in (APP, MARK, p):
    if os.path.exists(f):
        os.unlink(f)
report(7)
