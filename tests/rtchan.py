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
fn demo_win(id) { printf '%%s\t%%s\t%%s\n' "${DT[$id]["title"]-none}" "${DT[$id]["w"]-}" "${DT[$id]["col"]-}" > %s.win; }
fn demo_mark() { GL["wifi1"]=kept; }
fn demo_app(a) { printf '%%s\t%%s\n' "${DT_APPS[$a]["title"]-none}" "${DT_APPS[$a]["src"]-}" > %s.app; }
fn demo_attr() { printf '%%s\n' "${DT_KEYS@a} ${DT_BTNS@a}" > %s.attr; }
fn demo_write() { local was=$DT_FOCUS; DT_FOCUS=hack 2> /dev/null; printf '%%s %%s\n' "$?" "$was" > %s.wrote; }
fn demo_list() { printf '%%s\n' "${#DT_BTNS[@]} ${DT_BTNS[*]}" > %s.list; }
fn demo_gl() { printf '%%s\n' "${GL["wifi1"]-}" > %s.gl; }
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
''' % (HERE, SAID, SAID, SAID, SAID, SAID, SAID, SAID, SAID, SAID, SAID, FIFO, SAID))
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
# The bar's clock is pinned: a minute turning is a real change the bar's
# table carries to every app that reads it, which checks counting what an
# app was sent would take for their own.
t = Term(p, rows=30, cols=110, settle=0.6, env={"DT_BARTIME": "12:00"})


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
    report(65)

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
early = open(SAID + ".pend").read().strip()
deadline = time.time() + 10
gone = ""
while time.time() < deadline:
    gone = result('echo "${DT_RTC[%s]-gone}" > %s.pend' % (hfd, SAID))
    if gone == "gone":
        break
    time.sleep(0.3)
check("past DT_RTQMAX waiting and not shrinking for DT_RTQSECS, the app is let go, and the log says which and why",
      early != "gone" and gone == "gone" and "demo stopped reading" in log(), (early, gone, log()[-300:]))
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
# The sanitizer build runs several times slower; the bound scales with it,
# as every timing check in the tree does.
PASSUS = 100 * (4 if os.environ.get("ASAN_OPTIONS") else 1)
check("a pass with ten idle app connections costs under %d us" % PASSUS,
      us.isdigit() and int(us) < PASSUS, us)

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
# What a setting derives is rebuilt only when its own setting moved: a
# focus change left every app refilling its glyph table.
do('dt_rttell "$LT" demo_mark')
# The focus moves in one pass and is read in a later one, so whatever the
# batch that carried it rebuilt has been rebuilt by then.
result('DT_FOCUS=990078; echo x > %s.pend' % SAID)
time.sleep(0.3)
result('dt_rttell "$LT" demo_gl; echo x > %s.pend' % SAID)
deadline = time.time() + 10
while time.time() < deadline and not os.path.exists(SAID + ".gl"):
    time.sleep(0.05)
gl = open(SAID + ".gl").read().strip() if os.path.exists(SAID + ".gl") else "?"
do('DT_FOCUS=$OTHER')
check("a focus change leaves what the settings derive alone", gl == "kept", gl)
# A list crosses whole, every element in order, where a line per key once
# carried its first element alone.
want = result('echo "${#DT_BTNS[@]} ${DT_BTNS[*]}" > %s.pend' % SAID)
do('dt_rttell "$LT" demo_list')
deadline = time.time() + 10
while time.time() < deadline and not os.path.exists(SAID + ".list"):
    time.sleep(0.05)
got = open(SAID + ".list").read().strip() if os.path.exists(SAID + ".list") else "?"
check("a list the desktop sends arrives whole and in order", got == want and int(want.split()[0]) > 1, (got, want))


# The registry door. A raw connection whose launch names a file, and every
# line its hello brings before ok: what it is sent, by name.
def dec(w):
    if w == "%":
        return ""
    for a, b in (("%20", " "), ("%09", "\t"), ("%0A", "\n"), ("%0D", "\r"), ("%25", "%")):
        w = w.replace(a, b)
    return w


def hello_from(f):
    if os.path.exists(SAID + ".tok"):
        os.unlink(SAID + ".tok")
    do('x := dt_rtissue demo %s; echo "$x" > %s.tok' % (f, SAID))
    deadline = time.time() + 10
    while time.time() < deadline and not (os.path.exists(SAID + ".tok") and open(SAID + ".tok").read().strip()):
        time.sleep(0.05)
    tok = open(SAID + ".tok").read().strip()
    os.unlink(SAID + ".tok")
    k = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    k.connect(path)
    k.sendall(("hello %s %s demo %s\n" % (proto, ver, tok)).encode())
    k.settimeout(5)
    buf = b""
    deadline = time.time() + 10
    while time.time() < deadline and b"\nok " not in b"\n" + buf:
        try:
            b = k.recv(65536)
        except socket.timeout:
            continue
        if not b:
            break
        buf += b
    got = {}
    for l in buf.decode("utf8", "replace").split("\n"):
        w = l.split(" ")
        if len(w) >= 4 and w[0] == "json":
            got[dec(w[1])] = dec(w[3])
    if b"\nok " not in b"\n" + buf:
        got["?"] = buf[:300].decode("utf8", "replace")
    return k, got


def lines_in(k, secs):
    got, _ = readall(k, 1 << 20, secs)
    out = {}
    for l in got.decode("utf8", "replace").split("\n"):
        w = l.split(" ")
        if len(w) >= 4 and w[0] == "json":
            out[dec(w[1])] = dec(w[3])
        elif len(w) >= 4 and w[0] == "jsonk":
            out[dec(w[1])] = out.get(dec(w[1]), "") + " %s %s" % (dec(w[2]), dec(w[3]))
    return out


def rawlines(k, secs, until=None):
    """What arrives on k for up to secs, split into lines of words, and
    whether the desktop ended the connection."""
    got, deadline, eof = b"", time.time() + secs, False
    k.settimeout(0.2)
    while time.time() < deadline:
        try:
            b = k.recv(1 << 20)
        except socket.timeout:
            if until and until in got:
                break
            continue
        if not b:
            eof = True
            break
        got += b
    return [l.split(" ") for l in got.decode("utf8", "replace").split("\n") if l], eof


import json as pyjson
kf, files = hello_from("/elsewhere/apps/files.hibr")
check("a file is sent the registries it reads and the interface reads, and no other",
      "DT_OPENVERBS" in files and "DT_APPS" in files and "DT_CH" not in files and "DT_NOTEHIST" not in files,
      (files.get("?"), sorted(n for n in files if n.isupper())))
ku, unknown = hello_from("/elsewhere/apps/nobody-read-this.hibr")
check("a file the survey never read is sent every registry but a secret",
      "DT_CH" in unknown and "DT_NOTEHIST" in unknown and "VW_SESSION" not in unknown and "DT_VWITEMS" not in unknown,
      sorted(n for n in unknown if n.startswith(("DT_CH", "DT_NOTE", "VW", "DT_VW"))))
bari = result('echo "${DT_BARI["app"]["c"]-}" > %s.pend' % SAID)
kt, tray = hello_from("/elsewhere/menubar/tray.hibr")
bj = pyjson.loads(tray.get("DT_BARI", "{}") or "{}")
# The bar lays its items out every frame, writing each one's column again:
# a write that changes nothing is not reported, so a reader is sent nothing.
idle_bar = lines_in(kt, 1.5)
check("a value the desktop writes again unchanged every frame is never sent",
      bari != "" and bj and any("c" in v for v in bj.values() if isinstance(v, dict)) and
      "DT_BARI" not in idle_bar, (bari, sorted(idle_bar)))
apps = pyjson.loads(unknown.get("DT_APPS", "{}"))
check("a table of records crosses whole: every entry with its fields",
      apps and all(isinstance(v, dict) and "title" in v for v in apps.values()), list(apps.items())[:1])
# A value written again unchanged sends nothing; a registration sends it.
do('t=${DT_APPS[demo]["title"]}; DT_APPS[demo]["title"]=$t')
quiet = lines_in(ku, 1.0)
do('dt_app rtfresh "Fresh Title" 10 30')
moved = lines_in(ku, 2.0)
check("a value written again unchanged is not sent, and a registration is",
      "DT_APPS" not in quiet and "rtfresh" in str(moved.get("DT_APPS", "")), (sorted(quiet), sorted(moved)))
do('dt_rttell "$LT" demo_app rtfresh')
deadline = time.time() + 10
while time.time() < deadline and not os.path.exists(SAID + ".app"):
    time.sleep(0.05)
got = open(SAID + ".app").read().strip() if os.path.exists(SAID + ".app") else "?"
check("an app's own process sees an app registered after its hello", got.startswith("Fresh Title"), got)
# A registry changed with nobody connected that reads it, and changed back,
# reaches the next reader: a secret, so the unknown connections do not count.
def items(d):
    try:
        return " ".join(pyjson.loads(d.get("DT_VWITEMS", "[]") or "[]"))
    except ValueError:
        return "?"


k1, v1 = hello_from("/elsewhere/apps/vault.hibr")
do('DT_VWITEMS=(first)')
s1 = lines_in(k1, 1.5)
k1.close()
time.sleep(0.5)
do('DT_VWITEMS=(second)')
time.sleep(0.3)
do('DT_VWITEMS=(first)')
k2, v2 = hello_from("/elsewhere/apps/vault.hibr")
do('DT_VWITEMS=(second)')
s2 = lines_in(k2, 1.5)
check("a registry changed with no reader connected, and back, reaches the next reader",
      items(s1) == "first" and items(v2) == "first" and items(s2) == "second",
      (s1, v2.get("DT_VWITEMS"), s2))
for k in (kf, ku, kt, k2):
    k.close()
do('DT_VWITEMS=()')
# A launch is good for DT_RTLAUNCHSECS: a token never spent goes void, and
# one spent after its time is refused.
do('DT_RTLAUNCHSECS=1')
late = issue("demo")
time.sleep(2.5)
k = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
k.connect(path)
k.sendall(("hello %s %s demo %s\n" % (proto, ver, late)).encode())
said = answer(k)
k.close()
check("a launch not spent within its time is void, and the log says so",
      said.startswith(b"refused") and "did not say hello within 1 seconds; its launch is void" in log(),
      (said, log()[-300:]))
do('DT_RTLAUNCHSECS=15')
# An app that exits before its hello is noticed at once and told to the
# person, never left as a window that silently never came.
QUIT = scratch("rtchan-quitter.hibr")
open(QUIT, "w").write("exit 3\n")
do('dt_rtspawn quitter %s' % QUIT)
deadline = time.time() + 10
while time.time() < deadline and "quitter started from" not in log():
    time.sleep(0.1)
noted = result('o=; for k in "${!DT_NOTEHIST[@]}"; do o+="${DT_NOTEHIST[$k]["text"]}|"; done; echo "$o" > %s.pend' % SAID)
check("an app whose process exits before its hello is told to the person, and logged",
      "quitter did not start" in noted and
      "quitter started from %s exited before it said hello" % QUIT in log() and
      "olddemo did not start" not in noted,
      (noted[-200:], [l for l in log().split("\n") if "quitter" in l][-2:]))
# A write in a form no word match sees reaches the readers just the same:
# the variable table tells, whatever wrote it.
ks, _ = hello_from("/elsewhere/apps/nobody-read-this-at-all.hibr")
do('''json parse DT_CH["990"] '{"text":"sneaky"}' ''')
sneak = rawlines(ks, 3, b"sneaky")[0]
# A file is sent what its parts read: a bundle's members share its process.
kv, vlt = hello_from("/elsewhere/apps/vault.hibr")
check("a file is sent the registries its parts read", "DT_VWERR" in vlt and "VW_SESSION" in vlt,
      (vlt.get("?"), sorted(n for n in vlt if n.startswith(("DT_VW", "VW_")))))
kv.close()


# The clipboard's history crosses by entry: one large copy is one line, to
# each reader, and nobody is let go for it.
kc, _ = hello_from("/elsewhere/system/clipboard.hibr")
kn, _ = hello_from("/elsewhere/apps/nobody-read-this-either.hibr")
do('dt_clipkeep "$(printf "%0200000d" 0)Z"')
lc, ec = rawlines(kc, 6, b"Z")
ln, en = rawlines(kn, 6, b"Z")


def bigentry(ls):
    for w in ls:
        if len(w) >= 4 and w[0] == "jsonk" and w[1] == "DT_CH":
            v = pyjson.loads(dec(w[3]))
            if isinstance(v, dict) and v.get("text", "").endswith("Z") and len(v["text"]) == 200001:
                return True
    return False


check("a copy of 200 kB reaches each reader of the history as one entry, never the whole table",
      bigentry(lc) and bigentry(ln) and not any(w[0] == "json" and w[1] == "DT_CH" for w in lc + ln),
      ([(w[:3], w[3][:60] if len(w) > 3 else "") for w in lc][:6], [w[:3] for w in ln][:6]))
do('dt_clipkeep "after the large one"')
lc2, ec2 = rawlines(kc, 3, b"after%20the%20large%20one")
check("and no reader is let go for it: the next copy reaches it too",
      not ec and not en and not ec2 and any(w[0] == "jsonk" and w[1] == "DT_CH" for w in lc2),
      (ec, en, ec2, [w[:2] for w in lc2]))
kc.close()
kn.close()
# A map arrives declared -A and a list as a list, so a key with a dash in
# it reads back literally in the app.
do('dt_rttell "$LT" demo_attr')
deadline = time.time() + 10
while time.time() < deadline and not os.path.exists(SAID + ".attr"):
    time.sleep(0.05)
attr = open(SAID + ".attr").read().strip() if os.path.exists(SAID + ".attr") else "?"
check("a map arrives declared as the desktop declares it, and a list as a list", attr == "A a", attr)
check("a write in a form no word match sees reaches the readers, with nothing marked",
      any(w[0] == "jsonk" and w[1] == "DT_CH" and w[2] == "990" for w in sneak), [w[:3] for w in sneak][:6])
ks.close()
do('unset "DT_CH[990]"')
# An app's own process holds a copy it cannot write: the variable table
# refuses, and says which.
screen.expect(r"DT_FOCUS: the desktop's, read only in an app's own process")
do('dt_rttell "$LT" demo_write')
deadline = time.time() + 10
while time.time() < deadline and "read only in an app's own process" not in log():
    time.sleep(0.1)
w = open(SAID + ".wrote").read().strip() if os.path.exists(SAID + ".wrote") else "?"
check("an app's own process cannot write the desktop's state: refused, and named",
      "DT_FOCUS: the desktop's, read only in an app's own process" in log() and w.startswith("1 "), (w, log()[-200:]))
seen = show()
n1 = int(seen[4]) if len(seen) > 4 else -1
time.sleep(3)
seen = show()
n2 = int(seen[4]) if len(seen) > 4 else -1
check("idle for three seconds, the desktop sends an app nothing", n2 - n1 == 1, (n1, n2))

# The window door: an app's own windows' records are sent -- what
# installables read of them -- when one moves, and when one goes; another
# app's are never sent.
def win(wid):
    if os.path.exists(SAID + ".win"):
        os.unlink(SAID + ".win")
    do('dt_rttell "$LT" demo_win %s' % wid)
    deadline = time.time() + 10
    while time.time() < deadline and not (os.path.exists(SAID + ".win") and open(SAID + ".win").read().strip()):
        time.sleep(0.05)
    return open(SAID + ".win").read().rstrip("\n").split("\t") if os.path.exists(SAID + ".win") else []


lw = result('LW := dt_new "Loop Window" 6 30 3 40 loopdemo; echo "$LW" > %s.pend' % SAID)
own = win(lw)
check("an app's own window's record is sent to its process", own[:2] == ["Loop Window", "30"], own)
# A window being dragged -- its place changed on pass after pass -- is held
# until it settles: its app is sent the drag's start and its end, never a
# line a step; a tell sends what is held first, so a call never reads a
# place the app has not been sent.
n1 = int(show()[4])
result('for c in 41 42 43 44 45; do DT[$LW]["col"]=$c; dt_rtpass; done; echo x > %s.pend' % SAID)
time.sleep(0.8)
n2 = int(show()[4])
moved = win(lw)
check("a window dragged sends its app its start and its end, not a line a step",
      n2 - n1 == 3 and moved[2:3] == ["45"], (n1, n2, moved))
result('dt_retitle "$LW" "Renamed"; dt_rttell "$LT" demo_win "$LW"; echo x > %s.pend' % SAID)
deadline = time.time() + 10
while time.time() < deadline and not (os.path.exists(SAID + ".win") and "Renamed" in open(SAID + ".win").read()):
    time.sleep(0.05)
got = open(SAID + ".win").read().rstrip("\n").split("\t") if os.path.exists(SAID + ".win") else []
check("a window retitled reaches its app before the next thing it is told", got[:1] == ["Renamed"], got)
other = result('echo "$OTHER" > %s.pend' % SAID)
check("another app's window is never sent, where its own is", own[:2] == ["Loop Window", "30"] and
      win(other)[:1] == ["none"], (own, win(other)))
result('dt_del "$LW"; echo x > %s.pend' % SAID)
time.sleep(0.3)
check("a window that has gone is gone from its app too, having been there", got[:1] == ["Renamed"] and
      win(lw)[:1] == ["none"], (got, win(lw)))

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
# A setting changed with no app connected, sent to the next app at its
# hello, and changed back, reaches that app: the record of what every app
# was sent is kept on every save while the desktop listens.
do('dt_rtlisten')
result('DT_GLYPHSET=ascii; dt_glyphs ascii; dt_save; echo x > %s.pend' % SAID)
do('LT := dt_rtspawn loopdemo %s' % LOOP)
deadline = time.time() + 10
while time.time() < deadline and not os.path.exists(SAID + ".pid"):
    time.sleep(0.05)
time.sleep(0.5)
first = show()
do('DT_GLYPHSET=unicode; dt_glyphs unicode; dt_save')
back = show()
check("a setting changed with no app connected, and back, reaches the app it was sent to",
      first[1:2] == ["ascii"] and back[1:2] == ["unicode"], (first, back))
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
          SAID + ".pid", SAID + ".pings", SAID + ".end", SAID + ".tok", SAID + ".tokenv", SAID + ".show", SAID + ".win", SAID + ".gl", SAID + ".list", SAID + ".app", SAID + ".attr", SAID + ".wrote", LOOP, FIFO, scratch("rtchan-quitter.hibr")):
    if os.path.exists(f):
        os.unlink(f)
report(65)
