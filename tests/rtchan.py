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
# An app with a loop of its own (dt_rtrun): it says hello, records its pid,
# runs what the desktop tells it -- a function of its own, which tells the
# desktop a note back and writes down what it was given -- watches a fifo of
# its own, read by its own ticker, and writes how its loop ended.
LOOP = scratch("rtchan-loop.hibr")
FIFO = scratch("rtchan-fifo")
open(LOOP, "w").write(r'''DT_HERE=%s
. "$DT_HERE/runtime.hibr"
dt_rtopen loopdemo || exit 1
env | grep -c '^DT_RTTOKEN=' > %s.tokenv
echo "$$" > %s.pid
fn demo_ping(what) {
	echo "$what" >> %s.pings
	dt_notep low "pinged: %%s" "$what"
}
fn demo_child() { ( sleep 60 ) & }
fn demo_show() { printf '%%s\t%%s\t%%s\t%%s\t%%s\n' "$DT_FACE" "$DT_GLYPHSET" "${GL[vline]}" "$DT_FOCUS" "$DT_RTRECV" > %s.show; }
exec {DEMO_FF}<> %s
console watch "$DEMO_FF"
demo_tick() {
	local w l
	w := console woke "$DEMO_FF"
	[ -n "$w" ] || return 0
	read -r -u "$DEMO_FF" l
	dt_notep low "the fifo said %%s" "$l"
}
dt_ticker demo_tick
dt_rtrun
echo "ended $?" > %s.end
''' % (HERE, SAID, SAID, SAID, SAID, FIFO, SAID))
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
DT_LOADING=%s dt_app loopdemo "Loop Demo" 8 40
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
open(p, "w").write(SESSION % (load(MOD), WM, GOOD, BAD, LOOP, MARK, MARK, MARK))
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

do('dt_rtlisten; dt_rtspawn demo %s' % GOOD)
sc = t.until("second note", tries=80, step=0.1)
said = open(SAID).read() if os.path.exists(SAID) else ""
check("an app process sourcing only the runtime says hello and is answered",
      said.startswith("ok: "), said)
check("a note it tells the desktop to show is shown, its words whole",
      sc.find("from the app: 50%25 off two  blanks [] done") is not None, sc.text())
check("and the next one after it, in order", sc.find("second note") is not None, sc.text())
check("a name that is no told request is refused by name, and the desktop runs on",
      "dt_quit, which is not a told request" in log() and sc.find("second note") is not None, log()[-600:])

do('dt_rtspawn olddemo %s' % BAD)
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
import socket, signal
do('echo "$DT_RTPATH" > %s.path; echo "$HIBR_VERSION" >> %s.path; echo "$DT_RTPROTO" >> %s.path' % (SAID, SAID, SAID))
deadline = time.time() + 5
while time.time() < deadline and not (os.path.exists(SAID + ".path") and
                                      len(open(SAID + ".path").read().split()) == 3):
    time.sleep(0.05)
path, ver, proto = open(SAID + ".path").read().split()


def answer(k, secs=5):
    """The desktop's answer to a hello: what it sends first -- its mirrored
    state, a line each -- and then ok or refused, which is what is given
    back."""
    k.settimeout(secs)
    buf = b""
    deadline = time.time() + secs
    while time.time() < deadline:
        for l in buf.split(b"\n")[:-1]:
            if l.startswith(b"ok") or l.startswith(b"refused"):
                return l
        try:
            b = k.recv(65536)
        except OSError:
            return b""
        if not b:
            return b""
        buf += b
    return b""


def issue(app):
    """A launch's token from the desktop, as dt_rtspawn hands one to a
    process it starts, for a raw client to say hello with."""
    if os.path.exists(SAID + ".tok"):
        os.unlink(SAID + ".tok")
    do('x := dt_rtissue %s; echo "$x" > %s.tok' % (app, SAID))
    deadline = time.time() + 10
    while time.time() < deadline and not (os.path.exists(SAID + ".tok") and open(SAID + ".tok").read().strip()):
        time.sleep(0.05)
    return open(SAID + ".tok").read().strip()


def client(name, tok=None):
    """A raw connection that has said hello and been answered."""
    k = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    k.connect(path)
    k.sendall(("hello %s %s %s %s\n" % (proto, ver, name, tok or issue(name))).encode())
    said = answer(k)
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
    k.sendall(("hello %s %s demo %s\n" % (proto, ver, issue("demo"))).encode())
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
    report(42)

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
k.sendall(("hello %s %s nobody %s\n" % (proto, ver, issue("nobody"))).encode())
said = answer(k)
k.close()
check("a hello naming no installed app is refused", said.startswith(b"refused") and
      b"not%20an%20installed%20app" in said, said)

# Whose a process is comes from the desktop's record of its launch, by the
# token the process was started with -- never from what the hello says.
def hello(line):
    k = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    k.connect(path)
    k.sendall(line.encode())
    return k, answer(k)


k, said = hello("hello %s %s demo\n" % (proto, ver))
k.close()
check("a hello with no launch of this desktop's behind it is refused",
      said.startswith(b"refused") and b"no%20launch" in said, said)
tok = issue("demo")
k1, said1 = hello("hello %s %s demo %s\n" % (proto, ver, tok))
k2, said2 = hello("hello %s %s demo %s\n" % (proto, ver, tok))
check("a launch's token is good for one hello", said1.startswith(b"ok") and said2.startswith(b"refused"),
      (said1, said2))
k2.close()
tok = issue("demo")
k3, said3 = hello("hello %s %s olddemo %s\n" % (proto, ver, tok))
check("a process is the app its launch names, whatever its hello says",
      said3.startswith(b"ok") and "said it was olddemo; it is demo, by its launch" in log(), (said3, log()[-300:]))
k1.close()
k3.close()
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

# The app's own loop: told to run its own function, it does, and tells the
# desktop back; a flood of tells is run whole and in order; what it watches
# and its own ticker wake it; idle, it spends next to nothing; and a name the
# app interface defines is refused rather than run.
for x in (SAID + ".pid", SAID + ".pings", SAID + ".end"):
    if os.path.exists(x):
        os.unlink(x)
if os.path.exists(FIFO):
    os.unlink(FIFO)
os.mkfifo(FIFO)
do('LT := dt_rtspawn loopdemo %s' % LOOP)
deadline = time.time() + 10
while time.time() < deadline and not os.path.exists(SAID + ".pid"):
    time.sleep(0.05)
apid = int(open(SAID + ".pid").read()) if os.path.exists(SAID + ".pid") else 0
time.sleep(0.3)
def noted(text, secs=10):
    """Whether a note holding text is in the desktop's queue: this session
    keeps each note up a minute, so a new one waits behind the last rather
    than showing at once."""
    deadline = time.time() + secs
    got = ""
    while time.time() < deadline:
        got = result('for k in "${!DT_NOTEQ[@]}"; do printf "%%s\\n" "${DT_NOTEQ[$k]["text"]}"; done > %s.pend' % SAID)
        if text in got:
            return True, got
        time.sleep(0.3)
    return False, got


do('dt_rttell "$LT" demo_ping "hello world"')
ok, got = noted("pinged: hello world")
check("an app's own loop runs a function of its own the desktop tells it to, and tells back", ok, got)
do('for i in {1..300}; do dt_rttell "$LT" demo_ping "n$i"; done')
deadline = time.time() + 15
pings = []
while time.time() < deadline:
    pings = open(SAID + ".pings").read().split("\n")[:-1] if os.path.exists(SAID + ".pings") else []
    if len(pings) >= 301:
        break
    time.sleep(0.1)
check("a flood of 300 tells is run whole and in order",
      pings[1:] == ["n%d" % i for i in range(1, 301)], (len(pings), pings[-3:]))
with open(FIFO, "w") as ff:
    ff.write("knock\n")
ok, got = noted("the fifo said knock")
check("what the app watches wakes its loop, and its own ticker reads it", ok, got)


def cpu(pid):
    f = open("/proc/%d/stat" % pid).read().rsplit(")", 1)[1].split()
    return int(f[11]) + int(f[12])


c0 = cpu(apid) if apid else 0
time.sleep(3)
c1 = cpu(apid) if apid else 99
check("idle for three seconds, the app's loop spends next to nothing (%d ticks)" % (c1 - c0),
      apid and c1 - c0 <= 2, c1 - c0)
do('dt_rttell "$LT" dt_notep low "from the desktop, as the app"')
deadline = time.time() + 5
while time.time() < deadline and "not this app's own" not in log():
    time.sleep(0.1)
ok, got = noted("from the desktop, as the app", 1)
check("a name the app interface defines is refused, never run",
      "the desktop told dt_notep, which is not this app's own" in log() and not ok, log()[-400:])
# Started by the desktop, it holds nothing of the desktop's terminal: its
# standard input and output are /dev/null, so it can neither read the
# person's keys nor write over the screen.
tty = os.readlink("/proc/%d/fd/0" % t.pid)
held = [os.readlink("/proc/%d/fd/%s" % (apid, f)) for f in os.listdir("/proc/%d/fd" % apid)] if apid else [tty]
dfds = {}
for f in os.listdir("/proc/%d/fd" % t.pid):
    try:
        tgt = os.readlink("/proc/%d/fd/%s" % (t.pid, f))
        flags = [l for l in open("/proc/%d/fdinfo/%s" % (t.pid, f)) if l.startswith("flags")][0].split()[1]
        if tgt == tty:
            dfds[f] = flags
    except OSError:
        pass
afds = {}
for f in (os.listdir("/proc/%d/fd" % apid) if apid else []):
    try:
        if os.readlink("/proc/%d/fd/%s" % (apid, f)) == tty:
            afds[f] = 1
    except OSError:
        pass
tokenv = open(SAID + ".tokenv").read().strip() if os.path.exists(SAID + ".tokenv") else "?"
check("a program the app starts is not handed its launch's token", tokenv == "0", tokenv)
check("an app process the desktop started holds no descriptor on the desktop's terminal",
      apid and tty not in held, (tty, held, "app fds", afds, "desktop fds/flags", dfds))

# What Quit and Restart Desktop both call (dt_close, dt_restart): the
# socket goes and every app process's connection ends. Called here, since
# a test session's q only opens the Quit question.
# What the desktop's state is, the app has: its settings at the hello; a
# setting changed and saved, as a setter does, before the next thing the
# desktop tells it, with what it derives (the glyph table) rebuilt; the
# focus moving, likewise; and nothing at all while nothing changes.
def show():
    if os.path.exists(SAID + ".show"):
        os.unlink(SAID + ".show")
    do('dt_rttell "$LT" demo_show')
    deadline = time.time() + 10
    while time.time() < deadline and not (os.path.exists(SAID + ".show") and open(SAID + ".show").read().strip()):
        time.sleep(0.05)
    return open(SAID + ".show").read().rstrip("\n").split("\t") if os.path.exists(SAID + ".show") else []


face = result('echo "$DT_FACE" > %s.pend' % SAID)
seen = show()
check("at its hello an app is sent the desktop's settings", seen[:1] == [face] and face != "", (face, seen))
do('DT_GLYPHSET=ascii; dt_glyphs ascii; dt_save')
seen = show()
check("a setting changed and saved reaches the app before the next thing it is told, "
      "and what it derives is rebuilt", seen[1:3] == ["ascii", "|"], seen)
do('DT_GLYPHSET=unicode; dt_glyphs unicode; dt_save')
# The focus changes and the app is told something in the same pass: the
# change must go out ahead of the tell, not at the next pass.
if os.path.exists(SAID + ".show"):
    os.unlink(SAID + ".show")
focus = result('DT_FOCUS=990077; dt_rttell "$LT" demo_show; echo "$DT_FOCUS" > %s.pend' % SAID)
deadline = time.time() + 10
while time.time() < deadline and not (os.path.exists(SAID + ".show") and open(SAID + ".show").read().strip()):
    time.sleep(0.05)
seen = open(SAID + ".show").read().rstrip("\n").split("\t") if os.path.exists(SAID + ".show") else []
do('DT_FOCUS=$OTHER')
check("the focus moving reaches the app before the next thing it is told", seen[3:4] == [focus] and focus != "",
      (focus, seen))
seen = show()
n1 = int(seen[4]) if len(seen) > 4 else -1
time.sleep(3)
seen = show()
n2 = int(seen[4]) if len(seen) > 4 else -1
check("idle for three seconds, the desktop sends an app nothing", n2 - n1 == 1, (n1, n2))

# A child an app starts must not keep the app looking alive once the app
# has gone: killed outright with a child of its own running, its
# connection ends for the desktop, which lets it go.
do('dt_rttell "$LT" demo_child')
time.sleep(0.5)
os.kill(apid, signal.SIGKILL)
deadline = time.time() + 3
left = "?"
while time.time() < deadline:
    left = result('n=0; for c in "${!DT_RTC[@]}"; do [ "${DT_RTC["$c"]}" = loopdemo ] && n=$((n + 1)); done; echo "$n" > %s.pend' % SAID)
    if left == "0":
        break
    time.sleep(0.2)
check("an app killed with a child of its own running is let go: the child holds nothing of it",
      left == "0", left)
for x in (SAID + ".pid", SAID + ".end"):
    if os.path.exists(x):
        os.unlink(x)
do('LT := dt_rtspawn loopdemo %s' % LOOP)
deadline = time.time() + 10
while time.time() < deadline and not os.path.exists(SAID + ".pid"):
    time.sleep(0.05)
time.sleep(0.3)

# A child the desktop starts after an app has connected -- a terminal's
# shell, a job, a watcher -- must not hold the desktop's end of that app's
# socket, or the app never sees its socket end, however the desktop goes.
do('( sleep 60 ) &')
time.sleep(0.3)
do("dt_rtclose")
time.sleep(0.3)
check("closing takes the socket away", not os.path.exists(path), path)
ends = []
for k in idle:
    # What the hello sent is still unread; the end comes after it.
    k.settimeout(2)
    end = False
    try:
        while True:
            b = k.recv(65536)
            if not b:
                end = True
                break
    except OSError:
        end = True
    ends.append(end)
check("and every app process sees its connection end", all(ends), ends)
deadline = time.time() + 2
while time.time() < deadline and not os.path.exists(SAID + ".end"):
    time.sleep(0.05)
end = open(SAID + ".end").read().strip() if os.path.exists(SAID + ".end") else ""
check("an app's loop ends when the desktop closes its socket, on its own, within two seconds",
      end == "ended 0", end)
for k in idle + [half, flood]:
    k.close()
for x in (SAID + ".pid", SAID + ".end"):
    if os.path.exists(x):
        os.unlink(x)
do('dt_rtlisten; LT := dt_rtspawn loopdemo %s' % LOOP)
deadline = time.time() + 10
while time.time() < deadline and not os.path.exists(SAID + ".pid"):
    time.sleep(0.05)
time.sleep(0.5)
os.kill(t.pid, signal.SIGKILL)
deadline = time.time() + 2
while time.time() < deadline and not os.path.exists(SAID + ".end"):
    time.sleep(0.05)
end = open(SAID + ".end").read().strip() if os.path.exists(SAID + ".end") else ""
check("a desktop killed outright ends an app's loop within two seconds, never by a signal",
      end == "ended 0", end)
t.close()
for f in (GOOD, BAD, p, MARK, SAID, SAID + ".bad", SAID + ".path", SAID + ".blame", SAID + ".idle",
          SAID + ".other", SAID + ".alive", SAID + ".before", SAID + ".after", SAID + ".pend",
          SAID + ".pid", SAID + ".pings", SAID + ".end", SAID + ".tok", SAID + ".tokenv", SAID + ".show", LOOP, FIFO):
    if os.path.exists(f):
        os.unlink(f)
report(42)
