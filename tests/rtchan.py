#!/usr/bin/env python3
"""The app runtime's socket (ADR 0050): an app's own process, sourcing
runtime.hibr and nothing else of the desktop, connects, says hello, and
what it tells the desktop happens there; one speaking another protocol is
refused and the person told hibr was updated, with Restart Desktop a click
away; a name interface.tsv does not carry as a told request is refused by
name; and the words cross whole, blanks, percents and empty ones included.

    python3 tests/rtchan.py
"""
import os, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import screen
from screen import Term, check, report, load, tree, scratch

MOD = tree("build/mods/console.so")
WM = tree("programs/desktop/desktop.hibr")
HERE = tree("programs/desktop")
SAID = scratch("rtchan-said")
GOOD = scratch("rtchan-good.hibr")
BAD = scratch("rtchan-bad.hibr")

# An app process: the runtime alone, a hello, then three requests -- a note
# whose words hold a blank, a percent and an empty word between them, a
# name that is no told request, and a second note to say the first two
# were read in order.
open(GOOD, "w").write(r'''DT_HERE=%s
. "$DT_HERE/runtime.hibr"
if ! dt_rtopen demo; then
	printf 'refused: %%s\n' "$DT_RTSAID" > %s
	exit 1
fi
printf 'ok: %%s\n' "$DT_RTSAID" > %s
demo_bye() { dt_del "$1"; }
dt_notep low "from the app: %%s %%s [%%s] done" "50%%25 off" "two  blanks" ""
dt_rtsend tell dt_quit
dt_notep low "second note"
''' % (HERE, SAID, SAID))
# One that speaks another protocol.
open(BAD, "w").write(r'''DT_HERE=%s
. "$DT_HERE/runtime.hibr"
DT_RTPROTO=99
dt_rtopen olddemo || printf 'refused: %%s\n' "$DT_RTSAID" > %s.bad
''' % (HERE, SAID))

SESSION = r'''%s. %s
dt_open
DT_NOTEMS=60000
DT_LOADING=%s dt_app demo "Demo" 8 40
DT_LOADING=%s dt_app olddemo "Old Demo" 8 40
OTHER := dt_new Other 6 30 3 3
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
MARK = scratch("rtchan-do")
p = scratch("rtchan.hibr")
open(p, "w").write(SESSION % (load(MOD), WM, GOOD, BAD, MARK, MARK, MARK))
t = Term(p, rows=30, cols=110, settle=0.6)


def do(cmd):
    """Run cmd in the desktop, from its ticker, and wait until it is taken."""
    open(MARK, "w").write(cmd + "\n")
    deadline = time.time() + 5
    while os.path.exists(MARK) and time.time() < deadline:
        t.send(b"\x1b[I", settle=0.1, collect=0.1)


def log():
    return open(os.path.join(screen.HOME, str(t.pid), "state", "hibr", "desktop.log"),
                errors="replace").read()


before = t.screen()
check("an idle desktop is not listening for app processes until one is started",
      "app runtime: listening" not in log(), log()[-300:])

do('dt_rtlisten; "$HIBR" %s &' % GOOD)
sc = t.until("second note", tries=80, step=0.1)
said = open(SAID).read() if os.path.exists(SAID) else ""
check("an app process sourcing only the runtime says hello and is answered",
      said.startswith("ok: "), said)
check("a note it tells the desktop to show is shown, its words whole",
      sc.find("from the app: 50%25 off two  blanks [] done") is not None, sc.text())
check("and the next one after it, in order", sc.find("second note") is not None, sc.text())
check("a name that is no told request is refused by name, and the desktop runs on",
      "dt_quit, which is not a told request" in log() and sc.find("second note") is not None, log()[-600:])

do('"$HIBR" %s &' % BAD)
sc = t.until("hibr was updated", tries=80, step=0.1)
bad = ""
deadline = time.time() + 5
while time.time() < deadline and not bad:
    bad = open(SAID + ".bad").read() if os.path.exists(SAID + ".bad") else ""
    time.sleep(0.1)
check("one speaking another protocol is refused, and told why",
      bad.startswith("refused: ") and "protocol 99" in bad, bad)
check("and the person is told hibr was updated, with the restart a click away",
      sc.find("hibr was updated") is not None, sc.text())
check("the refusal is in the log with both versions",
      "refused olddemo" in log() and "protocol 99" in log(), log()[-600:])

if os.environ.get("RTDEBUG"):
    print("\n".join(l for l in log().split("\n") if "runtime" in l or "rtt" in l or "hibr:" in l))
# The holes a socket has that the polite client above never finds, each
# from a client that misbehaves on the wire itself.
import socket
do('echo "$DT_RTPATH" > %s.path; echo "$HIBR_VERSION" >> %s.path' % (SAID, SAID))
deadline = time.time() + 5
while time.time() < deadline and not (os.path.exists(SAID + ".path") and
                                      len(open(SAID + ".path").read().split()) == 2):
    time.sleep(0.05)
path, ver = open(SAID + ".path").read().split()


def client(name):
    """A raw connection that has said hello and been answered."""
    k = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    k.connect(path)
    k.sendall(("hello 1 %s %s\n" % (ver, name)).encode())
    k.settimeout(5)
    said = k.recv(256)
    if not said.startswith(b"ok"):
        raise OSError("refused: %r" % said)
    return k


def answered(secs):
    """How long the desktop takes to run one command from its ticker."""
    t0 = time.time()
    do("dt_want 0")
    return time.time() - t0


# Said hello and went at once, before reading the answer: the answer goes
# to a peer that has gone, which must not be a SIGPIPE.
for i in range(5):
    k = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    k.connect(path)
    k.sendall(("hello 1 %s demo\n" % ver).encode())
    k.close()
time.sleep(0.5)
try:
    k = client("demo")
    k.sendall("tell dt_notep low still%20here\n".encode())
    sc = t.until("still here", tries=60, step=0.1)
    alive = sc.find("still here") is not None
except OSError as e:
    alive, sc = False, None
check("a client that says hello and goes before the answer does not end the desktop",
      alive and "signal 13" not in log(), log()[-400:])
if not alive:
    report(25)

# Half a line, then nothing for three seconds: the desktop must not wait.
half = client("demo")
half.sendall("tell dt_notep low half%20a".encode())
time.sleep(0.3)
took = max(answered(1) for _ in range(3))
check("half a line and a pause hold the desktop up not at all", took < 0.6, round(took, 3))
half.sendall("%20line\n".encode())
sc = t.until("half a line", tries=60, step=0.1)
check("and the line is carried whole once its end arrives",
      sc.find("half a line") is not None, sc.text())

# A flood: thousands of lines at once. The desktop answers between them,
# and every one is carried in the end, in order.
flood = client("demo")
flood.sendall(("tell dt_log flood\n" * 3000 + "tell dt_notep low flood%20done\n").encode())
time.sleep(0.2)
took = max(answered(1) for _ in range(3))
check("a flood of lines from one app holds the desktop up not at all", took < 0.6, round(took, 3))
sc = t.until("flood done", tries=200, step=0.1)
check("and all of it is carried, the last line last", sc.find("flood done") is not None,
      (log().count("not a told request"), sc.text()))

# The guards (against an app's bugs and blaming the wrong app, not a
# sandbox): a hello naming no installed app is refused; a request the
# app's own file never makes is refused; a window named must be its own.
k = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
k.connect(path)
k.sendall(("hello 1 %s nobody\n" % ver).encode())
k.settimeout(5)
said = k.recv(256)
k.close()
check("a hello naming no installed app is refused", said.startswith(b"refused") and
      b"not%20an%20installed%20app" in said, said)
do('echo "$OTHER" > %s.other' % SAID)
deadline = time.time() + 5
while time.time() < deadline and not (os.path.exists(SAID + ".other") and open(SAID + ".other").read().strip()):
    time.sleep(0.05)
other = open(SAID + ".other").read().strip()
g = client("demo")
g.sendall(("tell dt_rslog from%%20demo\ntell dt_del %s\ntell dt_notep low guarded\n" % other).encode())
sc = t.until("guarded", tries=60, step=0.1)
check("a request its own file never makes is refused by name",
      "demo asked for dt_rslog, which its own file never asks for" in log(), log()[-500:])
do('dt_iswin "$OTHER" && echo yes > %s.alive || echo no > %s.alive' % (SAID, SAID))
deadline = time.time() + 5
while time.time() < deadline and not os.path.exists(SAID + ".alive"):
    time.sleep(0.05)
alive = open(SAID + ".alive").read().strip() if os.path.exists(SAID + ".alive") else ""
check("a request naming another app's window is refused, and the window stays",
      "which is not its own" in log() and alive == "yes", (alive, log()[-500:]))
g.close()

# Blame: what an app's request cost is timed under the request's name and
# the app's, the way a callback is (wm/budget.hibr).
do('echo "${DT_MAX["dt_notep"]-none}" > %s.blame' % SAID)
deadline = time.time() + 5
while time.time() < deadline and not os.path.exists(SAID + ".blame"):
    time.sleep(0.05)
blame = open(SAID + ".blame").read().strip() if os.path.exists(SAID + ".blame") else ""
check("a carried request is timed under its own name", blame.isdigit(), blame)

# An app that stops reading: what the desktop says to it waits for it, the
# desktop answering everything else meanwhile, and arrives whole once it
# reads again; past DT_RTQMAX waiting, the app is taken to have stopped and
# let go, by name. Its connection is found as the one added between two
# listings of the table.
def newconn(name):
    do('echo "${!DT_RTC[@]}" > %s.before' % SAID)
    k = client(name)
    do('echo "${!DT_RTC[@]}" > %s.after' % SAID)
    before = set(open(SAID + ".before").read().split())
    added = [c for c in open(SAID + ".after").read().split() if c not in before]
    return k, (added[0] if len(added) == 1 else "")


def readall(k, want, secs):
    got, deadline = b"", time.time() + secs
    k.settimeout(0.2)
    while len(got) < want and time.time() < deadline:
        try:
            b = k.recv(65536)
        except socket.timeout:
            continue
        if not b:
            return got, True
        got += b
    return got, False


LINE = 100000
def result(cmd):
    """Run cmd in the desktop and wait for it to write its .pend file:
    do() returns once the command is taken, which a slow build may finish
    well after."""
    if os.path.exists(SAID + ".pend"):
        os.unlink(SAID + ".pend")
    do(cmd)
    deadline = time.time() + 20
    while time.time() < deadline and not os.path.exists(SAID + ".pend"):
        time.sleep(0.05)
    time.sleep(0.1)
    return open(SAID + ".pend").read().strip() if os.path.exists(SAID + ".pend") else ""


stuck, sfd = newconn("demo")
result('big=$(printf "%%0%dd" 0); for i in 1 2 3 4 5 6; do dt_rtsay %s "$i$big"; done; '
   'echo "${DT_RTPEND[%s]-none} $RET" > %s.pend' % (LINE - 1, sfd, sfd, SAID))
pend = open(SAID + ".pend").read().split() if os.path.exists(SAID + ".pend") else []
took = answered(5)
check("what the desktop says to an app that has stopped reading waits for it, and the desktop answers at once",
      sfd != "" and pend[:1] == ["1"] and int(pend[1]) > 0 and took < 0.6, (sfd, pend, round(took, 3)))
got, _ = readall(stuck, 6 * (LINE + 1), 10)
result('echo "${DT_RTPEND[%s]-none}" > %s.pend' % (sfd, SAID))
lines = got.split(b"\n")
check("once it reads, all of it arrives in order and nothing is left waiting",
      len(got) == 6 * (LINE + 1) and [l[:1] for l in lines[:6]] == [b"1", b"2", b"3", b"4", b"5", b"6"]
      and open(SAID + ".pend").read().strip() == "none", (len(got), open(SAID + ".pend").read()))
hung, hfd = newconn("demo")
result('big=$(printf "%%0%dd" 0); for i in {1..20}; do dt_rtsay %s "$big" || break; done; '
   'echo "${DT_RTC[%s]-gone}" > %s.pend' % (LINE - 1, hfd, hfd, SAID))
check("past DT_RTQMAX waiting, the app is let go, and the log says which and why",
      open(SAID + ".pend").read().strip() == "gone" and "demo stopped reading" in log(), log()[-300:])
ended, eof = readall(hung, 50 * (LINE + 1), 10)
check("and its connection ends, before all that was said to it", eof and len(ended) < 20 * (LINE + 1),
      (eof, len(ended)))
for k in (stuck, hung):
    k.close()
# Idle cost: ten connections that say nothing cost a pass next to nothing.
idle = [client("demo") for i in range(10)]
time.sleep(0.3)
do('t0=${EPOCHREALTIME/./}; for i in {1..200}; do dt_rttick; done; '
   'echo $(( (${EPOCHREALTIME/./} - t0) / 200 )) > %s.idle' % SAID)
deadline = time.time() + 10
while time.time() < deadline and not os.path.exists(SAID + ".idle"):
    time.sleep(0.05)
us = open(SAID + ".idle").read().strip() if os.path.exists(SAID + ".idle") else ""
check("a pass with ten idle app connections costs under 100 us", us.isdigit() and int(us) < 100, us)

# What Quit and Restart Desktop both call (dt_close, dt_restart): the
# socket goes and every app process's connection ends. Called here, since
# a test session's q only opens the Quit question.
do("dt_rtclose")
time.sleep(0.3)
check("closing takes the socket away", not os.path.exists(path), path)
ends = []
for k in idle:
    k.settimeout(2)
    try:
        ends.append(k.recv(16) == b"")
    except OSError:
        ends.append(True)
check("and every app process sees its connection end", all(ends), ends)
for k in idle + [half, flood]:
    k.close()
t.quit(b"q", 1.0)
for f in (GOOD, BAD, p, MARK, SAID, SAID + ".bad", SAID + ".path", SAID + ".blame", SAID + ".idle",
          SAID + ".other", SAID + ".alive", SAID + ".before", SAID + ".after", SAID + ".pend"):
    if os.path.exists(f):
        os.unlink(f)
report(25)
