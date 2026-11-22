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
TERM = tree("programs/desktop/apps/term.hibr")
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
p = scratch("zygote.hibr")
open(p, "w").write(SESSION % (load(MOD), WM, TERM, MARK, MARK, MARK))
os.chdir(HERE)
t = Term(p, rows=30, cols=110, settle=0.6,
         env={"DT_BARTIME": "12:00", "TW_CMD": WRITER, "ZY_MARK": "carried",
              "DT_RTPTYSECS": "2", "DT_RTENDSECS": "2"})


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

# The update route: Restart Desktop replaces the zygote and every app
# process, the window carried to a fresh process that takes its program back.
last = max(before) if before else -1
do("dt_restart")
deadline = time.time() + 20
napid = ""
while time.time() < deadline:
    t.collect(0.2)
    nz = result('echo "$DT_RTZPID"')
    napid = result('echo "${DT_RTCPID[${DT_RTHOST[term]-x}]-}"')
    if nz and nz != zpid and napid and "taken up" in log():
        break
nzpid = result('echo "$DT_RTZPID"')
check("Restart Desktop starts a new zygote and a fresh process for the Terminal",
      nzpid and nzpid != zpid and napid and napid != apid and stat(napid) is not None
      and stat(napid)["ppid"] == int(nzpid), (zpid, nzpid, apid, napid, log()[-1200:]))
check("the old app process and its zygote have gone",
      gone(apid) and gone(zpid), (stat(apid), stat(zpid)))
ps = stat(ppid)
check("the program kept running through it, the desktop's child still",
      ps is not None and ps["state"] != "Z" and ps["ppid"] == dpid, (ppid, ps))
check("its master was handed to the fresh process by descriptor",
      ptyof(ppid) in masters(napid), (ptyof(ppid), masters(napid)))
deadline = time.time() + 10
after = []
while time.time() < deadline:
    t.collect(0.2)
    after = lines()
    if after and max(after) > last + 5:
        break
# Consecutive: what the program wrote while its window changed processes is
# on the screen, none of it read by the old process and lost with it; and
# the screen goes on from what the old one showed, or scrolled past it.
check("its output goes on in the window, nothing between lost from the screen",
      after and max(after) > last + 5 and (min(after) <= last + 1 or len(after) >= 12) and
      sorted(after) == list(range(min(after), max(after) + 1)),
      (last, after, t.screen().text()))

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

# A program whose app process has gone, with no process to take it back,
# is hung up -- as the window's own closing would have ended it.
# Its last window gone, the app's process ends; the app is started again for
# a second window.
do("dt_del %s" % tid)
waitfor('[ -z "${DT_RTHOST[term]-}" ]')
for f in (PIDF,):
    if os.path.exists(f):
        os.unlink(f)
do("dt_rthost term")
waitfor('[ -n "${DT_RTHOST[term]-}" ]')
do("T2 := dt_new Terminal 14 52 3 3 term")
ppid2 = program()[0]
apid2 = result('echo "${DT_RTCPID[${DT_RTHOST[term]-x}]-}"')
check("a second window's program starts", ppid2 and stat(ppid2) is not None, ppid2)
try:
    os.kill(int(apid2), signal.SIGKILL)
except (ValueError, OSError):
    pass
deadline = time.time() + 2
while time.time() < deadline and not gone(apid2):
    t.collect(0.1)
alive = ppid2 and not gone(ppid2)
deadline = time.time() + 10
while time.time() < deadline and ppid2 and not gone(ppid2):
    t.collect(0.2)
check("its app process killed, the program outlives it a while, then is hung up",
      alive and gone(ppid2) and "terminals' programs:" in log(), (alive, stat(ppid2), log()[-600:]))
check("and no app process or zygote is left a zombie",
      not any(stat(x) and stat(x)["state"] == "Z" for x in (apid, zpid, apid2)),
      [(x, stat(x)) for x in (apid, zpid, apid2)])

t.close()
for f in (p, MARK, SAID, SAID + ".pend", WRITER, PIDF):
    if os.path.exists(f):
        os.unlink(f)
report(18)
