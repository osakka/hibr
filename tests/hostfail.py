#!/usr/bin/env python3
"""An app in a process of its own that ends or hangs (ADR 0050, Decision 7
and the not-responding mark): the app's alone, never the desktop's.

    python3 tests/hostfail.py

Ended -- killed, or exiting by itself -- its windows do what Control Panel >
Windows > When an App Quits says: kept showing their last picture with
Reopen offered (keep, the default), its app started again at once (reopen),
or closed (close); one note says it has gone and the log says why, its exit
status or the signal. A reopen that dies again straight away is kept rather
than started for ever.

Hung in its key handler, its window is marked not responding -- never
paused, its input still sent -- while another window answers within half a
second; the stall is a `held:` line, which fails a suite as an in-process
stall does, and the mark goes when the app answers. A key the desktop also
acts on waits for the app's answer at most DT_HOLDMS: then the desktop's
own action runs and the key is revoked, which the app's process drops if it
has not run it and says so if it had.

And a Restart Desktop finds its app processes by their pids across the
exec, and sees each end, rather than leaving it a zombie for the life of
the desktop.
"""
import os, sys, time, signal
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import screen
from screen import Term, check, report, load, tree, scratch, expect, press, release

MOD = tree("build/mods/console.so")
WM = tree("programs/desktop/desktop.hibr")
SAID = scratch("hostfail-said")
VICTIM = scratch("hostfail-victim.hibr")
HANGER = scratch("hostfail-hanger.hibr")
GOOD = scratch("hostfail-good.hibr")
DECLINER = scratch("hostfail-decliner.hibr")

# The victim draws how many keys it was given and its pid; x makes it exit
# with status 3. The hanger spins 3 s on h and 1 s on escape, then writes
# down each key it ran. Good is in the desktop's own process and shows the
# last key it was given.
open(VICTIM, "w").write(r'''command -v dt_app > /dev/null && dt_app victim "Victim" 6 40
declare -gA VC
fn victim_draw(int id, int h, int w, int row, int col) {
	console put -p "w$id" 1 1 "alive ${VC[$id]:-0}"
	echo "$$" > %s.vpid
}
fn victim_key(int id, key) {
	[ "$key" = x ] && exit 3
	VC[$id]=$((${VC[$id]:-0} + 1))
	return 0
}
''' % SAID)
open(HANGER, "w").write(r'''command -v dt_app > /dev/null && dt_app hanger "Hanger" 6 40
fn hanger_draw(int id, int h, int w, int row, int col) {
	console put -p "w$id" 1 1 "hanger"
	echo "$$" > %s.hpid
}
fn hanger_key(int id, key) {
	local t=0
	case $key in
	h) t=3000000 ;;
	escape) t=2000000 ;;
	esac
	t=$((${EPOCHREALTIME/./} + t))
	while [ "${EPOCHREALTIME/./}" -lt "$t" ]; do :; done
	printf '%%s\n' "$key" >> %s.keys
	return 0
}
''' % (SAID, SAID))
# The decliner writes down each key it is given and declines escape after a
# tenth of a second, inside DT_HOLDMS, as vi declining it to Menu Bar does.
open(DECLINER, "w").write(r'''command -v dt_app > /dev/null && dt_app decliner "Decliner" 6 40
fn decliner_draw(int id, int h, int w, int row, int col) {
	console put -p "w$id" 1 1 "decliner"
	echo "$$" > %s.dpid
}
fn decliner_key(int id, key) {
	printf '%%s\n' "$key" >> %s.dkeys
	[ "$key" = escape ] || return 0
	local t=$((${EPOCHREALTIME/./} + 100000))
	while [ "${EPOCHREALTIME/./}" -lt "$t" ]; do :; done
	return 1
}
''' % (SAID, SAID))
open(GOOD, "w").write(r'''declare -gA GD
fn gd_draw(int id, int h, int w, int row, int col) {
	console put -p "w$id" 1 1 "got: ${GD[$id]:-none}"
}
fn gd_key(int id, key) {
	GD[$id]=$key
	return 0
}
dt_app gd "Good" 6 26
''')

SESSION = r'''%s. %s
dt_open
DT_NOTEMS=60000
DT_LOADING=%s dt_app victim "Victim" 6 40
DT_LOADING=%s dt_app hanger "Hanger" 6 40
DT_LOADING=%s dt_app decliner "Decliner" 6 40
DT_LOADING=%s . %s
DT_LOADING=
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

# The hang's stalls are what this suite provokes: by the app's own process
# when its call ends, and by the desktop while it waits.
expect(r"held: input:hanger")


def session():
    p = scratch("hostfail.hibr")
    mark = scratch("hostfail-do")
    open(p, "w").write(SESSION % (load(MOD), WM, VICTIM, HANGER, DECLINER, GOOD, GOOD, mark, mark, mark))
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


def said(t, *words):
    """The log's lines holding any of words, for a failure to show."""
    return [l for l in log(t).splitlines() if any(w in l for w in words)][-12:]


def logwait(t, text, secs=10):
    deadline = time.time() + secs
    while time.time() < deadline:
        if text in log(t):
            return True
        t.collect(0.1)
    return False


def readpid(name, secs=5, old=None):
    f = SAID + name
    deadline = time.time() + secs
    while time.time() < deadline:
        if os.path.exists(f):
            p = open(f).read().strip()
            if p and p != old:
                return p
        time.sleep(0.05)
    return ""


def kill(pid, sig):
    """Signal a process the suite read the pid of; nothing when it read none."""
    if pid:
        try:
            os.kill(int(pid), sig)
        except OSError:
            pass


def gone(pid):
    """Gone and reaped: no /proc entry at all, not even a zombie's."""
    return not os.path.exists("/proc/%s" % pid)


def zombie(pid):
    try:
        return open("/proc/%s/stat" % pid).read().rsplit(")", 1)[1].split()[0] == "Z"
    except (OSError, IndexError):
        return False


def row(t, text):
    f = t.screen().find(text)
    return f[0] if f else None


def notes(t, text):
    return result(t, 'n=0; for i in "${DT_NOTEQ_ORDER[@]}"; do [ "${DT_NOTEQ[$i]["text"]}" = "%s" ] && n=$((n + 1)); done; echo $n' % text)


def hosted(t, app, var, r, c, old=None, pidfile=".vpid", text="alive 0"):
    do(t, "dt_rthost %s" % app)
    waitfor(t, '[ -n "${DT_RTHOST[%s]-}" ]' % app)
    do(t, "%s := dt_new %s 6 40 %d %d %s" % (var, app.capitalize(), r, c, app))
    t.until(text, tries=60, step=0.1)
    return readpid(pidfile, old=old)


for f in (".vpid", ".hpid", ".dpid", ".keys", ".dkeys"):
    if os.path.exists(SAID + f):
        os.unlink(SAID + f)

t = session()

# -- keep, the default: the window stays with its last picture, says its app
# has quit, a key to it is never sent, and one note offers Reopen.
vpid = hosted(t, "victim", "V", 2, 2)
t.send(b"a", settle=0.3, collect=0.3)
t.until("alive 1", tries=40, step=0.1)
vid = result(t, 'echo "$V"')
kill(vpid, signal.SIGKILL)
check("a hosted app killed: the log says it was ended by signal KILL",
      logwait(t, "victim (process %s) was ended by signal KILL (status 137)" % vpid),
      log(t)[-800:])
check("and its window stays, kept, its app's own picture still in it",
      waitfor(t, 'dt_iswin "$V" && [ "${DT_RVC[$V]-}" = - ]', 3)
      and t.until("alive 1", tries=20, step=0.1).find("alive 1") is not None,
      (result(t, 'echo "${DT_RVC[$V]-none}"'), t.screen().text()))
check("its title says the app has quit",
      t.until("Victim (quit)", tries=30, step=0.1).find("Victim (quit)") is not None,
      t.screen().text())
check("one note says so, offering Reopen",
      notes(t, "Victim quit unexpectedly: click to reopen it") == "1",
      result(t, 'for i in "${DT_NOTEQ_ORDER[@]}"; do echo "${DT_NOTEQ[$i]["text"]}"; done'))
t.send(b"a", settle=0.3, collect=0.3)
check("a key to it is sent nowhere, and its picture stays as it was",
      "window %s's app victim has gone; its key is not sent" % vid in log(t)
      and t.screen().find("alive 1") is not None, log(t)[-400:])
f = t.screen().find("click to reopen")
if f:
    t.send(press(f[0], f[1]) + release(f[0], f[1]), settle=0.3, collect=0.3)
v2 = readpid(".vpid", secs=8, old=vpid)
check("Reopen, from the note, starts the app again in a window where the kept one was",
      v2 and waitfor(t, '! dt_iswin %s && id := dt_appwin victim && [ "${DT[$id]["row"]} ${DT[$id]["col"]}" = "2 2" ] && [ -n "${DT_RVC[$id]-}" ] && [ "${DT_RVC[$id]}" != - ]' % vid, 5)
      and t.until("alive 0", tries=40, step=0.1).find("alive 0") is not None,
      (v2, log(t)[-600:], t.screen().text()))
check("and the kept picture's channel is let go",
      result(t, 'echo "${#DT_RVGONE[@]}"') == "0" and gone(vpid),
      (result(t, 'echo "${!DT_RVGONE[@]}"'), vpid))

# -- reopen at once: the app is started again in the same place, said once;
# and one that dies again straight away is kept, not started for ever.
do(t, "DT_APPGONE=reopen")
kill(v2, signal.SIGTERM)
v3 = readpid(".vpid", secs=8, old=v2)
check("reopen: the app ended by TERM is started again at once, in the same place",
      v3 and logwait(t, "victim (process %s) was ended by signal TERM (status 143)" % v2)
      and waitfor(t, 'id := dt_appwin victim && [ "${DT[$id]["row"]} ${DT[$id]["col"]}" = "2 2" ] && [ "${DT_RVC[$id]-}" != - ]', 5),
      (v3, log(t)[-600:]))
check("with one note saying so",
      notes(t, "Victim quit unexpectedly and was opened again") == "1")
kill(v3, signal.SIGKILL)
check("and one that ends again within seconds is kept, not started again",
      logwait(t, "victim ended again within 10 s of being reopened; its windows are kept instead")
      and waitfor(t, 'id := dt_appwin victim && [ "${DT_RVC[$id]-}" = - ]', 3)
      and readpid(".vpid", secs=1.5, old=v3) == "", log(t)[-600:])
do(t, 'id := dt_appwin victim; dt_del "$id"')
check("a kept window closed goes, with its picture",
      waitfor(t, '! dt_appwin victim > /dev/null && [ "${#DT_RVGONE[@]}" = 0 ]', 3))

# -- close: an app exiting by itself, with status 3, takes its windows.
do(t, "DT_APPGONE=close")
v4 = hosted(t, "victim", "V", 2, 2, old=v3)
t.send(b"x", settle=0.3, collect=0.3)
check("close: an app that exits with status 3 is said to have, and its window goes",
      logwait(t, "victim (process %s) exited with status 3; its windows: close" % v4)
      and waitfor(t, '! dt_iswin "$V"', 3), log(t)[-600:])
check("with one note",
      notes(t, "Victim quit unexpectedly") == "1", notes(t, "Victim quit unexpectedly"))
do(t, "DT_APPGONE=keep")

# -- a hang: marked, never paused; another window answers at once; the stall
# is logged; the mark goes when the app answers.
do(t, "G := dt_new Good 6 26 12 60 gd")
hpid = hosted(t, "hanger", "H", 12, 2, pidfile=".hpid", text="hanger")
hid = result(t, 'echo "$H"')
t.send(b"h", settle=0.05, collect=0.05)
t0 = time.time()
g = t.screen().find("got:")
if g:
    t.send(press(g[0], g[1]) + release(g[0], g[1]) + b"x", settle=0, collect=0)
sc = t.until("got: x", tries=50, step=0.02)
took = time.time() - t0
check("a hosted app spinning in its key handler: another window answers a key "
      "within half a second", sc.find("got: x") is not None and took < 0.5, (round(took, 2), sc.text()))
check("its window is marked not responding",
      t.until("Hanger (not responding)", tries=30, step=0.05).find("Hanger (not responding)") is not None,
      t.screen().text())
check("and the stall is a held: line naming it, as in-process",
      logwait(t, "held: input:hanger has not answered the person's key in window %s" % hid, 3),
      log(t)[-600:])
check("the mark goes when it answers",
      logwait(t, "hanger answered; window %s is no longer marked not responding" % hid, 6)
      and t.until(lambda sc: sc.find("(not responding)") is None, tries=30, step=0.1).find("(not responding)") is None,
      t.screen().text())

# -- a key the desktop also acts on: its action waits for the answer at most
# DT_HOLDMS, then runs and the key is revoked. Escape is Menu Bar's.
do(t, 'dt_raise "$H"; DT_FOCUS=$H')
if os.path.exists(SAID + ".keys"):
    os.unlink(SAID + ".keys")
t0 = time.time()
t.send(b"\x1b", settle=0, collect=0)
opened = waitfor(t, '[ "${MB_OPEN:--1}" != -1 ]', 2)
took = time.time() - t0
early = not os.path.exists(SAID + ".keys")
check("escape to an app taking two seconds over it: the menu bar opens anyway, "
      "before the app has answered", opened and early, (round(took, 2), early))
check("and the key is revoked, which the app's process says had run",
      logwait(t, "had run (ADR 0050, a declined key)", 4)
      and os.path.exists(SAID + ".keys") and open(SAID + ".keys").read().split() == ["escape"],
      (said(t, "revoke", "runtime", "hosted:"), open(SAID + ".keys").read() if os.path.exists(SAID + ".keys") else None))
do(t, "dt_mclose")
if os.path.exists(SAID + ".keys"):
    os.unlink(SAID + ".keys")
t.send(b"h", settle=0.05, collect=0.05)
time.sleep(0.2)
t0 = time.time()
t.send(b"\x1b", settle=0, collect=0)
opened = waitfor(t, '[ "${MB_OPEN:--1}" != -1 ]', 2)
took = time.time() - t0
early = not os.path.exists(SAID + ".keys")
check("escape behind a key the app is still spinning on: the menu opens before "
      "the app has answered either", opened and early, (round(took, 2), early))
check("and the revoked key is dropped by the app's process, never run",
      logwait(t, "before it ran; dropped", 6)
      and os.path.exists(SAID + ".keys") and open(SAID + ".keys").read().split() == ["h"],
      (said(t, "revoke", "runtime", "hosted:"), open(SAID + ".keys").read() if os.path.exists(SAID + ".keys") else None))
do(t, "dt_mclose")

# -- Only the one call whose decline runs the desktop's action holds it: a
# desktop key reaching a window's _key from anywhere else -- typed in by a
# paste, an input method -- is plain input, never held, never declined.
if os.path.exists(SAID + ".keys"):
    os.unlink(SAID + ".keys")
got = result(t, 'dt_wincall "$H" hanger_key q; echo "${DT_RVHOLD:-none}"')
time.sleep(0.6)
t.collect(0.2)
check("a desktop key typed into a hosted window from elsewhere is sent, never held",
      got == "none" and "hosted: key q declined" not in log(t)
      and os.path.exists(SAID + ".keys") and open(SAID + ".keys").read().split() == ["q"],
      (got, said(t, "hosted:", "revoke")))

# -- What is typed behind a held key waits for its answer and goes where
# the key's action sends it: escape declined opens the menu bar, and the
# down arrow typed with it moves in the menu, never reaching the app.
hosted(t, "decliner", "D", 21, 2, pidfile=".dpid", text="decliner")
t.collect(0.3)
if os.path.exists(SAID + ".dkeys"):
    os.unlink(SAID + ".dkeys")
t.send(b"\x1b\x1b[B", settle=0.3, collect=0.3)
opened = waitfor(t, '[ "${MB_OPEN:--1}" != -1 ]', 2)
time.sleep(0.4)
t.collect(0.2)
dk = open(SAID + ".dkeys").read().split() if os.path.exists(SAID + ".dkeys") else None
check("input typed behind a held key waits for its answer and goes where the "
      "key's action sends it", opened and dk == ["escape"], (opened, dk, said(t, "hosted:")))
do(t, "dt_mclose")

# -- Restart Desktop: the app's process ends with the socket it spoke on,
# and the desktop after the exec finds it by its pid and sees it end.
before = log(t).count("quit unexpectedly")
hp = readpid(".hpid", secs=1)
do(t, "dt_restart")
check("after Restart Desktop the app's process, carried across the exec, is "
      "seen to end and is not left a zombie",
      (logwait(t, "process %s, carried across the restart, ended with status" % hp, 10)
       or logwait(t, "process %s, carried across the restart, had ended before it" % hp, 1))
      and gone(hp), (hp, zombie(hp), said(t, "carried")))
check("and a restart is no app quitting: no note, nothing reopened",
      log(t).count("quit unexpectedly") == before and "reopened" not in log(t)[log(t).find("restarting into"):],
      log(t)[-600:])

t.quit(b"q", 1.5)
t.close()
for f in (VICTIM, HANGER, DECLINER, GOOD, t.mark, scratch("hostfail.hibr")):
    if os.path.exists(f):
        os.unlink(f)
for f in (".pend", ".vpid", ".hpid", ".dpid", ".keys", ".dkeys"):
    if os.path.exists(SAID + f):
        os.unlink(SAID + f)
report(26)
