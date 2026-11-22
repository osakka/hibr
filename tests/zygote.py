#!/usr/bin/env python3
"""App processes forked from a zygote, and a terminal's program the desktop
owns (ADR 0050, route A): a hosted Terminal whose app process is replaced --
by Restart Desktop, the update route -- while its program keeps running, its
output carried on, its screen kept, and its exit status still the window's;
the master handed to the fresh process by descriptor. And a program whose
app process has gone with no process to take it back is hung up, as a
window's closing would.

    python3 tests/zygote.py

The Terminal is the real one (apps/term.hibr), never sourced in the
desktop; its program is a writer of numbered lines (TW_CMD, from the
environment every app process inherits), so what reaches the screen can be
counted and the program found by its pid.
"""
import os, sys, time, re, signal
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import screen
from screen import Term, check, report, load, tree, scratch, expect

MOD = tree("build/mods/console.so")
WM = tree("programs/desktop/desktop.hibr")
# The Terminal from a copy of its file, so a check can make it newer than
# its process -- an update to the app -- without touching the tree.
TERM = scratch("zygote-term.hibr")
open(TERM, "w").write(open(tree("programs/desktop/apps/term.hibr")).read())
SAID = scratch("zygote-said")
PIDF = scratch("zygote-prog")
WRITER = scratch("zygote-writer.sh")
open(WRITER, "w").write("""#!/bin/sh
echo "$$ $(pwd) ${ZY_MARK-none}" > %s
i=0
while :; do
	echo "line $i"
	i=$((i + 1))
	sleep 0.1
done
""" % PIDF)
os.chmod(WRITER, 0o755)
HERE = scratch("zygote-cwd")
os.makedirs(HERE, exist_ok=True)

# The Terminal is not yet converted to run hosted (tools/hostaudit.py: it
# waits on its menus and on names the desktop does not share), and says so
# as it opens; what this suite asks of it is its program, its screen and its
# end, which do not touch those.
expect(r"DT_OPEN(SELF|SHELL) is not declared")

# The Terminal registered before the desktop opens, as a session file's apps
# are, so a restart that restores its window knows it.
SESSION = r'''%s. %s
DT_LOADING=%s dt_app term "Terminal" 14 52 many ">_"
DT_LOADING=
dt_open
DT_NOTEMS=60000
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
MARK = scratch("zygote-do")
# An app that keeps something only its own process has -- a value made when
# its window opens -- and says each time it opens one, so what a restart
# kept and what it opened again can be told apart.
KEEPER = scratch("zygote-keeper.hibr")
OPENS = scratch("zygote-keeper.opens")
open(KEEPER, "w").write(r"""command -v dt_app > /dev/null && dt_app keeper "Keeper" 6 30
declare -gA KP
fn keeper_open(int id, arg = "") {
	KP[$id]=k$RANDOM$RANDOM
	echo "$$ ${KP[$id]}" >> %s
	return 0
}
fn keeper_draw(int id, int h, int w, int row, int col) {
	console put -p "w$id" 1 1 "kept ${KP[$id]-none}"
}
""" % OPENS)
if os.path.exists(OPENS):
    os.unlink(OPENS)
p = scratch("zygote.hibr")
open(p, "w").write(SESSION.replace(
    'DT_LOADING=\ndt_open', 'DT_LOADING=%s dt_app keeper "Keeper" 6 30\nDT_LOADING=\ndt_open' % KEEPER)
    % (load(MOD), WM, TERM, MARK, MARK, MARK))
os.chdir(HERE)
t = Term(p, rows=30, cols=110, settle=0.6,
         env={"DT_BARTIME": "12:00", "TW_CMD": WRITER, "ZY_MARK": "carried",
              "DT_RTENDSECS": "2"})


def do(cmd):
    """Run cmd in the desktop, from its ticker, and wait until it is taken."""
    open(MARK, "w").write(cmd + "\n")
    deadline = time.time() + 5
    while os.path.exists(MARK) and time.time() < deadline:
        t.send(b"\x1b[I", settle=0.05, collect=0.05)


def result(cmd):
    """Run cmd, which writes SAID.pend, and answer what it wrote."""
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


def log():
    return open(t.log, errors="replace").read() if os.path.exists(t.log) else ""


def stat(pid):
    """A process's state, parent and session, or None once it has gone."""
    try:
        f = open("/proc/%s/stat" % pid).read().rsplit(")", 1)[1].split()
        return {"state": f[0], "ppid": int(f[1]), "sid": int(f[3]), "tty": int(f[4])}
    except (OSError, IndexError, ValueError):
        return None


def gone(pid):
    s = stat(pid)
    return s is None or s["state"] == "Z"


def lines():
    """The numbers of the writer's lines on the screen."""
    return [int(m) for m in re.findall(r"line (\d+)", t.screen().text())]


def masters(pid):
    """The pty indices of the masters a process holds."""
    out = set()
    try:
        fds = os.listdir("/proc/%s/fd" % pid)
    except OSError:
        return out
    for f in fds:
        try:
            if os.readlink("/proc/%s/fd/%s" % (pid, f)) != "/dev/ptmx":
                continue
            for l in open("/proc/%s/fdinfo/%s" % (pid, f)):
                if l.startswith("tty-index:"):
                    out.add(int(l.split()[1]))
        except OSError:
            pass
    return out


def program():
    deadline = time.time() + 15
    while time.time() < deadline:
        if os.path.exists(PIDF):
            w = open(PIDF).read().split()
            if len(w) == 3:
                return w
        t.collect(0.1)
    return ["", "", ""]


def ptyof(pid):
    try:
        return int(os.readlink("/proc/%s/fd/0" % pid).rsplit("/", 1)[1])
    except (OSError, ValueError):
        return -1


for f in (PIDF,):
    if os.path.exists(f):
        os.unlink(f)
dpid = t.pid
do("dt_rthost term")
check("the Terminal's process says hello and opens its cells channel",
      waitfor('[ -n "${DT_RTHOST[term]-}" ]'), log()[-800:])
zpid = result('echo "$DT_RTZPID"')
apid = result('echo "${DT_RTCPID[${DT_RTHOST[term]}]-}"')
za, aa = stat(zpid), stat(apid)
check("app processes are forked from one zygote, the desktop's child",
      za is not None and za["ppid"] == dpid and aa is not None and aa["ppid"] == int(zpid),
      (dpid, zpid, za, apid, aa))
check("and each is a process of its own: a session of its own, its own $$",
      aa is not None and aa["sid"] == int(apid) and
      "app runtime: term read" in log(), (aa, apid, log()[-400:]))

do("T := dt_new Terminal 14 52 3 3 term")
tid = result('echo "$T"')
ppid, pcwd, pmark = program()
ps = stat(ppid) if ppid else None
check("a hosted Terminal's program is the desktop's child, not its app process's",
      ps is not None and ps["ppid"] == dpid, (ppid, ps, dpid, apid))
check("started where the app process is, with its environment",
      pcwd == HERE and pmark == "carried", (pcwd, HERE, pmark))
deadline = time.time() + 10
while time.time() < deadline and len(lines()) < 3:
    t.collect(0.1)
before = lines()
check("its output is drawn in the window by the app's own process",
      len(before) >= 3, t.screen().text())
check("the app process reads the program's master itself: it holds it",
      ptyof(ppid) in masters(apid) and ptyof(ppid) in masters(dpid),
      (ptyof(ppid), masters(apid), masters(dpid)))

# The update route (the owner, 2026-11-22): Restart Desktop keeps every app
# process whose code is what is on disk, and replaces the rest; a terminal's
# program lives on either way.
def waitlog(text, n0, secs=20):
    deadline = time.time() + secs
    while time.time() < deadline:
        t.collect(0.2)
        if text in log()[n0:]:
            return True
    return False


def goes_on(last):
    """The writer's lines after a restart: on the screen, consecutive, and
    going on from what was shown, or a full screen of them -- 11 lines in
    the window's 12 rows, the last the cursor's."""
    deadline = time.time() + 10
    after = []
    while time.time() < deadline:
        t.collect(0.2)
        after = lines()
        if after and max(after) > last + 5:
            break
    return (bool(after) and max(after) > last + 5 and (min(after) <= last + 1 or len(after) >= 11)
            and sorted(after) == list(range(min(after), max(after) + 1))), after


def hostpid():
    return result('echo "${DT_RTCPID[${DT_RTHOST[term]-x}]-}"')


def opens():
    try:
        return [l.split() for l in open(OPENS).read().splitlines() if l.strip()]
    except OSError:
        return []


def keeperpid():
    return result('echo "${DT_RTCPID[${DT_RTHOST[keeper]-x}]-}"')


do("dt_rthost keeper")
waitfor('[ -n "${DT_RTHOST[keeper]-}" ]')
do("K := dt_new Keeper 6 30 3 60 keeper")
kid = result('echo "$K"')
deadline = time.time() + 10
while time.time() < deadline and not opens():
    t.collect(0.1)
kpid = keeperpid()
kval = opens()[0][1] if opens() else "?"
t.until("kept " + kval, tries=50, step=0.1)

# Nothing changed on disk: the Terminal's process and its zygote are kept,
# the process joining the restarted desktop.
last = max(before) if before else -1
n0 = len(log())
do("dt_restart")
joined = waitlog("joined the restarted desktop", n0)
deadline = time.time() + 10
while time.time() < deadline and hostpid() != apid:
    t.collect(0.2)
check("Restart Desktop with hibr's code unchanged keeps the Terminal's process and its zygote",
      joined and hostpid() == apid and result('echo "$DT_RTZPID"') == zpid
      and stat(apid) is not None and stat(apid)["state"] != "Z", (apid, hostpid(), zpid, log()[n0:][-1200:]))
ps = stat(ppid)
check("which still holds the program's master; the program runs on, the desktop's child",
      ps is not None and ps["state"] != "Z" and ps["ppid"] == dpid and ptyof(ppid) in masters(apid),
      (ppid, ps, ptyof(ppid), masters(apid)))
ok, after = goes_on(last)
check("its output goes on in the window, nothing between lost from the screen",
      ok, (last, after, t.screen().text()))
sc = t.until("kept " + kval, tries=50, step=0.1)
check("a window kept with its process is not opened again: what only its process held is there",
      keeperpid() == kpid and len(opens()) == 1 and sc.find("kept " + kval) is not None,
      (kpid, keeperpid(), opens(), sc.text()))

# The app's own file newer than its process: that process is replaced by a
# fresh one from the same zygote, the window carried to it.
last = max(after) if after else last
fut = time.time() + 5
os.utime(TERM, (fut, fut))
n0 = len(log())
do("dt_restart")
deadline = time.time() + 20
napid = ""
while time.time() < deadline:
    t.collect(0.2)
    napid = hostpid()
    if napid and napid != apid and "taken up" in log()[n0:]:
        break
check("with the app's file changed, its process is replaced by a fresh one from the same zygote",
      napid and napid != apid and stat(napid) is not None and stat(napid)["ppid"] == int(zpid)
      and result('echo "$DT_RTZPID"') == zpid, (apid, napid, zpid, log()[n0:][-1200:]))
deadline = time.time() + 5
while time.time() < deadline and not gone(apid):
    t.collect(0.2)
ps = stat(ppid)
check("the old process has gone, the program runs on and the fresh one holds its master",
      gone(apid) and ps is not None and ps["state"] != "Z" and ps["ppid"] == dpid
      and ptyof(ppid) in masters(napid), (stat(apid), ps, ptyof(ppid), masters(napid)))
ok, after = goes_on(last)
check("and its output goes on in the window",
      ok, (last, after, t.screen().text()))
check("decided per process: the other app's process, its files unchanged, is kept",
      keeperpid() == kpid and len(opens()) == 1, (kpid, keeperpid(), opens()))

# The desktop's own files newer than the zygote -- an upgrade: the zygote
# and every app process are replaced.
last = max(after) if after else last
stamp = result('echo "$DT_RTZSTAMP"')
if stamp and os.path.exists(stamp):
    os.utime(stamp, (1, 1))
n0 = len(log())
do("dt_restart")
deadline = time.time() + 20
nzpid = napid2 = ""
while time.time() < deadline:
    t.collect(0.2)
    nzpid = result('echo "$DT_RTZPID"')
    napid2 = hostpid()
    if nzpid and nzpid != zpid and napid2 and napid2 != napid and "taken up" in log()[n0:]:
        break
check("with hibr's code changed, a new zygote and a fresh process for the Terminal",
      stamp and nzpid and nzpid != zpid and napid2 and napid2 != napid and stat(napid2) is not None
      and stat(napid2)["ppid"] == int(nzpid), (stamp, zpid, nzpid, napid, napid2, log()[n0:][-1200:]))
deadline = time.time() + 8
while time.time() < deadline and not (gone(napid) and gone(zpid)):
    t.collect(0.2)
ps = stat(ppid)
check("the old process and zygote have gone, the program runs on in the fresh one",
      gone(napid) and gone(zpid) and ps is not None and ps["state"] != "Z" and ps["ppid"] == dpid
      and ptyof(ppid) in masters(napid2), (stat(napid), stat(zpid), ps, masters(napid2)))
ok, after = goes_on(last)
check("and its output goes on in the window, through all three restarts",
      ok, (last, after, t.screen().text()))
deadline = time.time() + 10
while time.time() < deadline and len(opens()) < 2:
    t.collect(0.2)
check("and the other app's process is replaced too, its window opened afresh there",
      keeperpid() not in ("", kpid) and len(opens()) == 2 and opens()[1][0] == keeperpid(),
      (kpid, keeperpid(), opens()))
do("dt_del %s" % kid)
apid, zpid = napid2, nzpid

# The program's end is still the window's: ended by a signal, its status
# is the one its parent -- the desktop -- read.
try:
    os.kill(int(ppid), signal.SIGTERM)
except (ValueError, OSError):
    pass
sc = t.until("exited 143", tries=100, step=0.1)
check("a program ended by TERM shows status 143 in its window",
      sc.find("exited 143") is not None, sc.text())
check("and the desktop, its parent, said so",
      re.search(r"ended \d+ %s 143" % ppid, log()) is not None, log()[-600:])

# An app process that crashes leaves its windows kept and its terminals'
# programs running (the owner, 2026-11-22): Reopen takes the same program
# back into the window it opens, and closing that window hangs it up.
do("dt_del %s" % tid)
waitfor('[ -z "${DT_RTHOST[term]-}" ]')
for f in (PIDF,):
    if os.path.exists(f):
        os.unlink(f)
do("dt_rthost term")
waitfor('[ -n "${DT_RTHOST[term]-}" ]')
do("T2 := dt_new Terminal 14 52 3 3 term")
tid2 = result('echo "$T2"')
ppid2 = program()[0]
apid2 = hostpid()
check("a second window's program starts", ppid2 and stat(ppid2) is not None, ppid2)
waitfor('[ "${DT[%s]["prog"]-}" = %s ]' % (tid2, ppid2 or "x"), 5)
try:
    os.kill(int(apid2), signal.SIGKILL)
except (ValueError, OSError):
    pass
deadline = time.time() + 4
while time.time() < deadline:
    t.collect(0.2)
kept = result('echo "${DT_RVC[%s]-none}"' % tid2)
check("its app process killed, its window is kept and the program runs on, for Reopen",
      gone(apid2) and kept == "-" and ppid2 and not gone(ppid2),
      (stat(apid2), kept, stat(ppid2), log()[-600:]))
n0 = len(log())
do("dt_rvreopen term")
deadline = time.time() + 15
tid3 = apid3 = ""
while time.time() < deadline:
    t.collect(0.2)
    tid3 = result('for i in "${!DT_RVC[@]}"; do [ "${DT[$i]["app"]-}" = term ] && echo "$i"; done')
    apid3 = hostpid()
    if tid3 and apid3 and ptyof(ppid2) in masters(apid3):
        break
check("Reopen takes the same program back into the window it opens, in a fresh process",
      tid3 and tid3 != tid2 and apid3 and apid3 != apid2 and ptyof(ppid2) in masters(apid3)
      and not gone(ppid2), (tid2, tid3, apid2, apid3, ptyof(ppid2), masters(apid3), log()[n0:][-800:]))
seen0 = lines()
deadline = time.time() + 8
while time.time() < deadline and not (lines() and seen0 is not None and max(lines() or [0]) > max(seen0 or [0]) + 3):
    t.collect(0.2)
check("and the program's output is drawn in it again",
      bool(lines()) and max(lines()) > max(seen0 or [0]) + 3, t.screen().text())
# Crashed again: closing the window it kept, rather than reopening it, hangs
# the program up, as closing a live one does.
try:
    os.kill(int(apid3), signal.SIGKILL)
except (ValueError, OSError):
    pass
deadline = time.time() + 4
while time.time() < deadline and result('echo "${DT_RVC[%s]-none}"' % tid3) != "-":
    t.collect(0.2)
alive2 = ppid2 and not gone(ppid2)
do("dt_del %s" % tid3)
deadline = time.time() + 6
while time.time() < deadline and not gone(ppid2):
    t.collect(0.2)
check("closing a window kept after a crash hangs its program up",
      alive2 and gone(ppid2) and "named by no window, hung up" in log(), (alive2, stat(ppid2), log()[-500:]))
check("and no app process or zygote is left a zombie",
      not any(stat(x) and stat(x)["state"] == "Z" for x in (apid, zpid, apid2, apid3)),
      [(x, stat(x)) for x in (apid, zpid, apid2, apid3)])

# An app process with no window left, at work of its own when its time to
# end came, is looked at again; found idle then, its work has only just
# ended and its own end may be a moment away -- which the desktop learns of
# from the zygote, not at once -- so it is given its time again rather than
# ended mid-way (tests/hostedit.py's job app ended this way one run in two).
# A stand-in process, its jobs folder filled and then emptied, each look
# made due at once.
w = result(r'sleep 60 & p=$!; d := dt_jobsof "$p"; mkdir -p "$d"; : > "$d/1.st"; '
           r'n=$((${EPOCHREALTIME/./} / 1000)); DT_RTENDPID[$p]=$((n - 1)); dt_rvtick; '
           r'command rm -f "$d/1.st"; DT_RTENDPID[$p]=$((${EPOCHREALTIME/./} / 1000 - 1)); dt_rvtick; '
           r'x="$p ${DT_RTENDHIT[$p]:-0} $((DT_RTENDPID[$p] - n))"; kill "$p"; command rmdir "$d"; '
           r'echo "$x"').split()
check("one whose work ended as its time came is given its time again, not ended",
      len(w) == 3 and w[1] == "0" and int(w[2]) >= 1000
      and "process %s had no window left and had not gone" % w[0] not in log(), (w, log()[-500:]))

t.close()

# --- the hold: an app's process draws as often as the desktop shows -------
# A window dirty only by what it watches -- a program writing -- is drawn at
# most DT_TERMMS apart in its app's own process, as the desktop holds a
# frame such output asks for; the answer to the person's input (a terminal's
# echo) is never held. Two apps of the suite's own, each counting in its own
# process: one whose program writes every 4 ms, and one that echoes each key
# it is given through a program, as a terminal does, and times each echo
# from its key to the draw that shows it.
PULSE = scratch("zygote-pulse.hibr")
COUNT = scratch("zygote-pulse.n")
ECHO = scratch("zygote-echo.hibr")
ELAT = scratch("zygote-echo.ms")
FIFO = scratch("zygote-echo.fifo")
def pulseapp(pfx, title, count, live=False):
    """An app whose window a program writes to every 4 ms, counting its
    draws in its own process; live, it says its window plays video."""
    return r"""command -v dt_app > /dev/null && dt_app %(p)s "%(t)s" 6 30
declare -gA PU_FD PU_D
PU_N=0
fn %(p)s_open(int id, arg = "") {
	local fd
	exec {fd}< <(exec perl -e '$|=1; while (1) { print "x\n"; select(undef, undef, undef, 0.004) }')
	PU_FD[$id]=$fd
	console watch "$fd"
	%(live)s
	return 0
}
fn %(p)s_idle(int id) {
	local l fd=${PU_FD[$id]-}
	[ -n "$fd" ] || return 0
	while read -t 0 -u "$fd"; do
		read -r -u "$fd" l || break
		PU_D[$id]=1
	done
	return 0
}
fn %(p)s_dirty(int id) {
	[ -n "${PU_D[$id]-}" ]
}
fn %(p)s_draw(int id, int h, int w, int row, int col) {
	PU_D[$id]=
	PU_N=$((PU_N + 1))
	echo "$PU_N" > %(c)s
	console put -p "w$id" 1 1 "draws $PU_N"
}
fn %(p)s_close(int id) {
	local fd=${PU_FD[$id]-}
	PU_FD[$id]=
	[ -z "$fd" ] || { console unwatch "$fd"; exec {fd}<&-; }
	return 0
}
""" % {"p": pfx, "t": title, "c": count, "live": 'dt_live "$id" on' if live else ":"}


open(PULSE, "w").write(pulseapp("pulse", "Pulse", COUNT))
LIVE = scratch("zygote-live.hibr")
LCOUNT = scratch("zygote-live.n")
open(LIVE, "w").write(pulseapp("livep", "Live", LCOUNT, live=True))
open(ECHO, "w").write(r"""command -v dt_app > /dev/null && dt_app echoer "Echoer" 6 30
declare -gA EC_R EC_W EC_T
fn echoer_open(int id, arg = "") {
	local r w
	command rm -f %(f)s
	mkfifo %(f)s
	exec {r}< <(exec perl -e '$|=1; open(F, "<", $ARGV[0]) or die; while (<F>) { print }' %(f)s)
	exec {w}> %(f)s
	EC_R[$id]=$r
	EC_W[$id]=$w
	console watch "$r"
	return 0
}
fn echoer_key(int id, key) {
	printf '%%s\n' "${EPOCHREALTIME/./}" >&"${EC_W[$id]}"
	return 0
}
fn echoer_idle(int id) {
	local l fd=${EC_R[$id]-}
	[ -n "$fd" ] || return 0
	while read -t 0 -u "$fd"; do
		read -r -u "$fd" l || break
		[ -n "${EC_T[$id]-}" ] || EC_T[$id]=$l
	done
	return 0
}
fn echoer_dirty(int id) {
	[ -n "${EC_T[$id]-}" ]
}
fn echoer_draw(int id, int h, int w, int row, int col) {
	if [ -n "${EC_T[$id]-}" ]; then
		echo "$(((${EPOCHREALTIME/./} - EC_T[$id]) / 1000))" >> %(l)s
	fi
	EC_T[$id]=
	console put -p "w$id" 1 1 "echo"
}
fn echoer_close(int id) {
	local fd=${EC_R[$id]-}
	EC_R[$id]=
	[ -z "$fd" ] || { console unwatch "$fd"; exec {fd}<&-; }
	fd=${EC_W[$id]-}
	EC_W[$id]=
	[ -z "$fd" ] || exec {fd}>&-
	return 0
}
""" % {"f": FIFO, "l": ELAT})
for f in (COUNT, ELAT):
    if os.path.exists(f):
        os.unlink(f)
p2 = scratch("zygote-hold.hibr")
open(p2, "w").write(SESSION.replace(
    'DT_LOADING=\ndt_open',
    'DT_LOADING=%s dt_app pulse "Pulse" 6 30\nDT_LOADING=%s dt_app echoer "Echoer" 6 30\n'
    'DT_LOADING=%s dt_app livep "Live" 6 30\nDT_LOADING=\ndt_open' % (PULSE, ECHO, LIVE))
    % (load(MOD), WM, tree("programs/desktop/apps/term.hibr"), MARK, MARK, MARK))
# DT_TERMMS as shipped: a longer one holds the desktop's own frames, which
# its tickers -- this suite's hands -- wait behind.
t = Term(p2, rows=30, cols=110, settle=0.6, env={"DT_BARTIME": "12:00"})


def drawn(f=None):
    try:
        return int(open(f or COUNT).read().split()[0])
    except (OSError, ValueError, IndexError):
        return -1


def drawrate(f, secs=3.0):
    n0, f0 = drawn(f), t.frames() or 0
    t.collect(secs)
    return (drawn(f) - n0) / secs, ((t.frames() or 0) - f0) / secs


do("dt_rthost pulse; dt_rthost echoer; dt_rthost livep")
waitfor('[ -n "${DT_RTHOST[pulse]-}" ] && [ -n "${DT_RTHOST[echoer]-}" ] && [ -n "${DT_RTHOST[livep]-}" ]')
do("dt_new Pulse 6 30 3 70 pulse")
deadline = time.time() + 10
while time.time() < deadline and drawn() < 1:
    t.collect(0.1)
t.collect(1.0)
n0 = drawn()
t.collect(4.0)
n1 = drawn()
rate = (n1 - n0) / 4.0
print("a window written to every 4 ms: drawn %.1f times a second in its process" % rate)
check("a window its program writes to constantly is drawn in its own process "
      "at most once each DT_TERMMS",
      n0 > 0 and 1.0 <= rate <= 1000.0 / 70 + 3, (n0, n1, rate, log()[-600:]))

# Frames For Output is the person's: changed in the desktop, it reaches the
# app's process, which holds its draws to it (mirrors.hibr sends what the
# runtime reads to every process).
do("DT_TERMMS=30")
t.collect(0.5)
r30, _ = drawrate(COUNT)
do("DT_TERMMS=70")
print("the same window, DT_TERMMS 30 in the desktop: drawn %.1f times a second in its process" % r30)
check("Frames For Output changed in the desktop holds the app's process's draws to it",
      r30 >= 22, (r30, log()[-400:]))
# And it is sent to every app process, not only to a file the inventory saw
# read it: the Terminal's, as shipped, is sent both frame holds.
do("dt_rthost term")
waitfor('[ -n "${DT_RTHOST[term]-}" ]')
tw = result('c=${DT_RTHOST[term]}; for n in DT_TERMMS DT_LIVEMS; do dt_rtwants "$c" "$n" && echo "$n"; done')
check("the Terminal's process, as shipped, is sent Frames For Output and Frames For Video",
      tw.split() == ["DT_TERMMS", "DT_LIVEMS"], (tw, log()[-400:]))

# A window playing video (dt_live) is held to Frames For Video, DT_LIVEMS,
# in its own process and by the desktop alike.
t.collect(0.5)
_, fbase = drawrate(COUNT)
do("dt_new Live 6 30 12 70 livep")
deadline = time.time() + 10
while time.time() < deadline and drawn(LCOUNT) < 1:
    t.collect(0.1)
t.collect(1.0)
rl, fl = drawrate(LCOUNT)
print("a live window written to every 4 ms: drawn %.1f times a second in its process; "
      "the desktop %.1f frames a second (%.1f before it opened)" % (rl, fl, fbase))
check("a window its app says plays video is drawn at Frames For Video in its own process",
      rl >= 22, (rl, log()[-400:]))
check("and the desktop, told, holds its frames to Frames For Video too",
      fl >= 22 and result('echo "${!DT_LIVE[@]}"') != "", (fl, fbase, result('echo "${!DT_LIVE[@]}"')))
do('for i in "${!DT[@]}"; do [ "${DT[$i]["app"]-}" = livep ] && dt_del "$i"; done')

do("dt_new Echoer 6 30 12 3 echoer")
deadline = time.time() + 10
while time.time() < deadline and result('echo "$DT_FOCUS ${DT[$DT_FOCUS]["app"]-}"').split()[-1:] != ["echoer"]:
    t.collect(0.1)
t.collect(1.0)
for ch in "abcdefghijklmnopqrstuvwxyz":
    os.write(t.fd, ch.encode())
    t.sent += 1
    t.collect(0.03)
t.collect(1.0)
try:
    lat = sorted(int(x) for x in open(ELAT).read().split())
except (OSError, ValueError):
    lat = []
med = lat[len(lat) // 2] if lat else None
print("an echo, keys 30 ms apart, from the key to its draw in the app's process: "
      "%d drawn, median %s ms, worst %s ms" % (len(lat), med, lat[-1] if lat else None))
check("the echo of a key, the answer to it, is drawn at once, not held to DT_TERMMS",
      len(lat) >= 18 and med is not None and med < 15, (lat, log()[-600:]))
t.close()
for f in (p, p2, MARK, SAID, SAID + ".pend", WRITER, PIDF, PULSE, COUNT, ECHO, ELAT, FIFO, LIVE, LCOUNT, TERM, KEEPER, OPENS):
    if os.path.exists(f):
        os.unlink(f)
report(35)
