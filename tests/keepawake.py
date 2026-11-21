#!/usr/bin/env python3
"""A program keeping the screen awake (Blit #171): the desktop's saver holds
off its idle start while a program asks, by either of the two routes, which
feed one rule (dt_saverawake, wm/saver.hibr).

The window's route: a stand-in compositor (natserve.py) says WINDOW inhibit
7 on|off, which it says only to a desktop whose hello answer carries
inhibit=1. The bus's: a program on a dbus-daemon of the suite's own calls
org.freedesktop.ScreenSaver's Inhibit, which the desktop answers once it has
the name. Held, the saver does not start on idle; a window minimised, a
request let go, a window gone, a program that leaves the bus, or Programs
May Keep It Awake turned off, and it does. A saver asked for starts however
many ask.

Whether the saver is up is read from the desktop itself -- a ticker writes
DT_SAVING whenever it changes -- and from the screen: the clock saver's
block digits. Nothing is ever sent as a key while the idle time runs, since
a key is someone being here; a ticker asking for a frame every 200 ms keeps
the desktop reading the suite's marks.
"""
import json, os, shutil, signal, subprocess, sys, tempfile, time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import screen as sx
from screen import Term, check, report, load, tree

HERE = os.path.dirname(os.path.abspath(__file__))
SLOW = 4 if "ASAN_OPTIONS" in os.environ else 1
D = tempfile.mkdtemp(prefix="hibr-awake-")
IDLE = 2


def lines(lg):
    try:
        return [json.loads(l) for l in open(lg) if l.strip()]
    except (OSError, ValueError):
        return []


def wait(cond, t=4.0):
    end = time.time() + t * SLOW
    while time.time() < end:
        v = cond()
        if v:
            return v
        time.sleep(0.05)
    return cond()


have_bus = bool(shutil.which("dbus-daemon"))
try:
    import dbus  # noqa: F401
except ImportError:
    have_bus = False
BUS = os.path.join(D, "bus")
daemon = None
if have_bus:
    daemon = subprocess.Popen(["dbus-daemon", "--session", "--nofork",
                               "--address=unix:path=" + BUS],
                              stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    import atexit
    atexit.register(daemon.kill)
    wait(lambda: os.path.exists(BUS), 2)

nm = "hibr-awake-%d" % os.getpid()
blitlog = os.path.join(D, "blit.log")
open(blitlog, "w").close()
blit = subprocess.Popen([sys.executable, os.path.join(HERE, "frameserve.py"),
                         nm, "tok", blitlog, "v3", "hold"])
wait(lambda: "listening" in open(blitlog).read())
complog = os.path.join(D, "comp.log")
wrap = os.path.join(D, "blit-compositor")
open(wrap, "w").write("#!/bin/sh\nexec %s %s %s \"$@\"\n"
                      % (sys.executable, os.path.join(HERE, "natserve.py"), complog))
os.chmod(wrap, 0o755)
MARK = os.path.join(D, "mark")
STATE = os.path.join(D, "state")

p = sx.scratch("keepawake.hibr")
open(p, "w").write(
    "%s. %s\nDT_NATBIN=%s\nDT_SUPERVISE=off\n"
    # What the saver is doing, a line each time it changes.
    "TST_ST=\n"
    "tst_st() { local s=\"saving=${DT_SAVING:-0}\"; [ \"$s\" = \"$TST_ST\" ] && return 0; "
    "TST_ST=$s; echo \"$s\" >> %s; }\n"
    # The suite's marks: minimise or raise the program's window, close it,
    # ask for the saver, set Programs May Keep It Awake.
    "tst_do() { local a n v; for a in min raise close start; do [ -f %s.$a ] || continue; "
    "rm -f %s.$a; for n in \"${!NWN[@]}\"; do case $a in min) dt_min \"${NWN[$n]}\" ;; "
    "raise) dt_raise \"${NWN[$n]}\" ;; close) native_canclose \"${NWN[$n]}\" ;; esac; done; "
    "[ $a = start ] && dt_saverstart; done; "
    "if [ -f %s.hon ]; then read -r v < %s.hon; rm -f %s.hon; dt_set DT_SAVERHONOUR \"$v\"; fi; "
    "dt_want 200; return 0; }\n"
    "dt_ticker tst_st\ndt_ticker tst_do\n"
    "dt_open\ndt_run\ndt_close\n"
    % (load("console"), tree("programs/desktop/desktop.hibr"), wrap, STATE,
       MARK, MARK, MARK, MARK, MARK))
env = {"HIBR_GFX": "kitty", "BLIT_FRAME_SOCKET": "@" + nm, "BLIT_FRAME_TOKEN": "tok",
       "XDG_RUNTIME_DIR": D, "WAYLAND_DISPLAY": None, "DISPLAY": None,
       "BLIT_VERSION": "0.3.127", "DT_SAVER": "clock", "DT_SAVERSECS": str(IDLE * SLOW)}
if have_bus:
    env["DBUS_SESSION_BUS_ADDRESS"] = "unix:path=" + BUS
t = Term(p, rows=26, cols=82, settle=1.0, cellw=8, cellh=16, caps=["kitty"], env=env)


def scr():
    t.collect(0.1)
    return t.screen()


def states():
    try:
        return [l.strip() for l in open(STATE) if l.strip()]
    except OSError:
        return []


def comp(kind):
    return [x for x in lines(complog) if x["kind"] == kind]


def log():
    try:
        return open(t.log).read()
    except OSError:
        return ""


def saving():
    s = states()
    return bool(s) and s[-1] == "saving=1"


def mark(name, text=""):
    open(MARK + "." + name, "w").write(text)


def endsaver():
    """A key, which is someone being here: the saver ends, the idle time
    starts again, and the key goes nowhere else."""
    if not wait(saving, 3):
        return True
    t.send(b"q")
    t.collect(0.5)
    return wait(lambda: not saving(), 4)


def held(secs=2.5 * IDLE):
    """Whether the saver stayed away for well past the idle time, read as
    every state written meanwhile."""
    if saving():
        return False
    n = len(states())
    time.sleep(secs * SLOW)
    t.collect(0.2)
    return "saving=1" not in states()[n:]


hel = wait(lambda: comp("hello"), 8)
check("the desktop tells the compositor it honours a program's request to "
      "keep the screen awake (inhibit=1)",
      hel and "inhibit=1" in hel[0]["answer"].split(), hel)
sc = wait(lambda: scr().find("Stand-in") and scr(), 8)
check("the program's window opens", sc, scr())

# Nobody asking: the saver comes on its own, which is what makes every held
# check below mean something.
st = wait(saving, 3 * IDLE)
check("with nothing asking, the saver starts once the idle time is up",
      st and "█" in scr().text(), (states(), scr()))
endsaver()

# The window asks: held for well past the idle time, and the log says why.
mark_n = len(log())
open(complog + ".inhibit", "w").write("on\n")
said = wait(lambda: comp("inhibit"), 4)
check("the compositor says the window's request, to a desktop that takes it",
      said and said[-1]["said"] is True, said)
check("a program's window asking the screen to stay awake holds the saver off",
      held(), states())
check("and the desktop's log says what held it",
      "saver: held off, Stand-in asks the screen to stay awake" in log()[mark_n:],
      log()[mark_n:][-600:])

# A saver asked for comes however many ask; only the idle start is held.
mark("start")
st = wait(saving, 4)
check("a saver asked for starts while the window asks", st, states())
endsaver()

# Minimised, the window is not shown, so its request no longer holds it.
mark("min")
st = wait(saving, 3 * IDLE)
check("minimised, the window's request no longer holds the saver off", st, states())
endsaver()
mark("raise")
check("raised again, it holds it off again", held(), states())

# Programs May Keep It Awake, off: nothing a program asks holds it.
mark("hon", "0\n")
st = wait(saving, 3 * IDLE)
check("with Programs May Keep It Awake off, the request is not honoured",
      st, (states(), log()[-400:]))
endsaver()
mark("hon", "1\n")
# Off let the bus go; on asks for it again, and the desktop opens it no
# sooner than thirty seconds after it last did (dt_busopen), which is the
# wait the name check below allows.
hon_n = len(log())
check("and on again, it is", held(), states())

# The program lets go.
open(complog + ".inhibit", "w").write("off\n")
st = wait(saving, 3 * IDLE)
check("once the program lets go, the saver starts on idle again", st, states())
endsaver()

# Asked again, then the window goes: its request goes with it.
open(complog + ".inhibit", "w").write("on\n")
check("asked again, it is held again", held(), states())
mark("close")
gone = wait(lambda: scr().find("Stand-in") is None, 6)
st = wait(saving, 3 * IDLE)
check("the window gone, its request went with it", gone and st, (states(), scr()))
endsaver()

# The bus: the desktop is the session's org.freedesktop.ScreenSaver, and a
# program's Inhibit holds the saver until it leaves the bus.
if have_bus:
    tookname = wait(lambda: "saver: programs on the session bus may keep the screen awake"
                    in log()[hon_n:], 45)
    check("the desktop takes the session bus's screen saver name again once "
          "Programs May Keep It Awake is on again", tookname, log()[hon_n:][-600:])
    # Nothing asked meanwhile, so the saver may well be up: ended first.
    endsaver()
    cl = os.path.join(D, "client.py")
    open(cl, "w").write(
        "import sys, time, dbus\n"
        "b = dbus.SessionBus()\n"
        "s = dbus.Interface(b.get_object('org.freedesktop.ScreenSaver', "
        "'/org/freedesktop/ScreenSaver'), 'org.freedesktop.ScreenSaver')\n"
        "c = s.Inhibit('film', 'playing')\n"
        "open(sys.argv[1], 'w').write('%d\\n' % c)\n"
        "while True:\n    time.sleep(1)\n")
    ck = os.path.join(D, "cookie")
    mark_n = len(log())
    cp = subprocess.Popen([sys.executable, cl, ck],
                          env=dict(os.environ, DBUS_SESSION_BUS_ADDRESS="unix:path=" + BUS),
                          stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    got = wait(lambda: os.path.exists(ck) and open(ck).read().strip(), 6)
    check("a program on the session bus asks the screen to stay awake and is "
          "given a cookie", got and got.isdigit(), got)
    check("which holds the saver off", held(), states())
    check("and the log says a program on the bus held it, and which",
          "saver: held off, 1 asking on the session bus (" in log()[mark_n:] and
          "film: playing)" in log()[mark_n:], log()[mark_n:][-600:])
    cp.kill()
    cp.wait()
    st = wait(saving, 3 * IDLE)
    check("the program leaving the bus lets go of it, and the saver starts", st, states())
    endsaver()
else:
    for n in ("the desktop takes the session bus's screen saver name again once "
              "Programs May Keep It Awake is on again",
              "a program on the session bus asks the screen to stay awake and is given a cookie",
              "which holds the saver off", "and the log says a program on the bus held it, and which",
              "the program leaving the bus lets go of it, and the saver starts"):
        print("skip " + n + " (no dbus-daemon or python3 dbus)")
        sx.RAN[0] += 1

t.quit(b"q", 2.0)
t.close()
for pr in (blit,):
    pr.kill()
if daemon:
    daemon.kill()
shutil.rmtree(D, True)
report(20)
