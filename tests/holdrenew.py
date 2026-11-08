#!/usr/bin/env python3
"""`hold upgrade` renews every display's client onto the modules on disk
(Gitea #491).

A held program with a frame socket of its own (the frames suite's), two
displays placed one above the other as the owner's DP-1 is, each with a
stand-in Blit, each attached through the desktop's own dt_holdgo -- the
loop that drops hold, term and pty and loads them again when the client is
told to renew. The modules are loaded from a folder of this suite's own, and
before the upgrade each file there is replaced by a new one with the same
bytes, which is what apt does: a new inode at the same path. Then `hold
upgrade`, and every check reads what crossed a socket, the terminal or
/proc -- never the code's own idea of what it did.
"""
import json, os, re, shutil, socket, subprocess, sys, time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import screen
from screen import Term, check, report, load, tree

HERE = os.path.dirname(os.path.abspath(__file__))
src = open(os.path.join(HERE, "frames.py")).read()
exec(compile(src[:src.index("# Nothing between")], "frames.py", "exec"))

MODS = os.path.join(D, "mods")
os.makedirs(MODS)
for m in ("pty.so", "term.so", "hold.so", "console.so", "frames.so"):
    shutil.copy(os.path.join(os.path.dirname(HERE), "build", "mods", m),
                os.path.join(MODS, m))


def renewmods():
    """The same bytes at the same paths, as new files: an install."""
    for m in ("pty.so", "term.so", "hold.so"):
        p = os.path.join(MODS, m)
        shutil.copy(p, p + ".new")
        os.rename(p + ".new", p)


def maps(pid):
    try:
        return [l.split() for l in open("/proc/%d/maps" % pid)
                if "/hold.so" in l]
    except OSError:
        return []


def ino(path):
    return str(os.stat(path).st_ino)


def held(pid):
    """Whether pid maps pty, term and hold from the files at MODS now, and
    no replaced one: the loop reloads all three, and a stale one of them is
    the same silent failure as a stale hold."""
    try:
        ls = [l.split() for l in open("/proc/%d/maps" % pid)
              if re.search(r"/(pty|term|hold)\.so", l)]
    except OSError:
        return False
    got = {}
    for l in ls:
        if "(deleted)" in " ".join(l) or not l[5].startswith(MODS + "/"):
            return False
        got[os.path.basename(l[5])] = l[4]
    return all(got.get(m) == ino(os.path.join(MODS, m))
               for m in ("pty.so", "term.so", "hold.so"))


ph, lg0, e0 = blit("r0")
ph2, lg1, e1 = blit("r1")
out = os.path.join(D, "r")
nm = "hrn-%d" % os.getpid()
env = {"HIBR_GFX": "kitty", "TMPDIR": D, "HIBR_MODPATH": MODS}
started = os.path.join(D, "start.hibr")
open(started, "w").write(load("hold") + 'hold new -d %s "$HIBR" %s\n'
                         % (nm, prog(out)))
subprocess.run([screen.HIBR, started], env=dict(os.environ, **env),
               capture_output=True, timeout=20)
wait(lambda: sockof(out), 5)


MODSA = os.path.join(D, "modsa")
shutil.copytree(MODS, MODSA)
STATE1 = os.path.join(D, "state1")


def display(tag, row, after, mods=None):
    """A display attached the way the desktop attaches one, saying what
    came back when dt_holdgo returns -- which a standby display goes on
    from, so it must still be running its script. Given mods, it loads its
    modules from there rather than the module path."""
    p = os.path.join(D, "att-%s.hibr" % tag)
    load_ = ("mod load %s/pty.so; mod load %s/term.so; mod load %s/hold.so\n"
             % (mods, mods, mods) if mods else "need hold\n")
    open(p, "w").write(
        load_ + '. %s\n'
        'dt_holdgo -m -n %s %s %d 0\n'
        'printf "%%s %%s\\n" "$?" "$HOLD_WHY" > %s\n'
        'sleep 30\n'
        % (tree("programs/desktop/wm/hold.hibr"), tag, nm, row, after))
    return p


after0 = os.path.join(D, "after0")
after1 = os.path.join(D, "after1")
t0 = Term(display("d0", 0, after0), rows=12, cols=60, settle=1.0, cellw=8,
          cellh=16, env=dict(env, **e0), caps=["kitty"])
t0.collect(1.0 * SLOW)
# The second display's modules are where nothing will be at the upgrade, so
# loading them again in its own shell fails: the fallback, a new shell on
# the same connection, is what renews it.
t1 = Term(display("d1", 12, after1, MODSA), rows=12, cols=60, settle=1.0,
          cellw=8, cellh=16, env=dict(env, XDG_STATE_HOME=STATE1, **e1),
          caps=["kitty"])
t1.collect(1.0 * SLOW)


def clients():
    r = subprocess.run([screen.HIBR, "-c", load("hold") + "hold clients %s" % nm],
                       env=dict(os.environ, **env), capture_output=True,
                       text=True, timeout=10)
    return sorted(l.split()[:3] for l in r.stdout.splitlines() if l.split())


def hellos(lg):
    return len([x for x in lines(lg) if x["kind"] == "hello"])


def routes(lg):
    return [x for x in lines(lg) if x["kind"] == "input"]


so = wait(lambda: sockof(out), 5)
br = Bridge(*so) if so else None
check("both displays are attached through the desktop's own loop",
      len(clients()) == 2 and t0.pid and t1.pid, clients())
was = clients()
pids = (t0.pid, t1.pid)
if br:
    br.send()
    wait(lambda: frames(lg0), 5)
    eis = os.path.join(D, "eis.sock")
    es = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    es.bind(eis)
    br.answers(0.3)
    br.say("INPUT eis %s 42" % eis)
    br.answers(1.0)
    open(out + ".on", "w").close()
    wait(lambda: len([x for x in routes(lg0) if x.get("verb") == "on"]), 5)
r0 = len(routes(lg0))
seq0 = max([x["seq"] for x in frames(lg0)] or [0])
nf0 = len(frames(lg0))
h0 = (hellos(lg0), hellos(lg1))
t0.collect(0.3)
cut0, cut1 = len(t0.out), len(t1.out)
renewmods()
shutil.rmtree(MODSA)
check("before the upgrade both clients map a hold.so that has been replaced",
      not held(pids[0]) and not held(pids[1]) and maps(pids[0]),
      (maps(pids[0]), ino(os.path.join(MODS, "hold.so"))))
up = subprocess.run([screen.HIBR, "-c", load("hold") + "hold upgrade %s" % nm],
                    env=dict(os.environ, **env), capture_output=True,
                    text=True, timeout=20)
ok = wait(lambda: held(pids[0]) and held(pids[1]), 8)
check("after `hold upgrade` each display's client maps the hold.so now on "
      "disk, in the same process", up.returncode == 0 and ok and
      t0.pid == pids[0] and t1.pid == pids[1],
      (up.returncode, up.stderr, maps(pids[0]), maps(pids[1])))
d1log = os.path.join(STATE1, "hibr", "desktop.log")
check("a display whose shell could not load the new modules is renewed in a "
      "new shell on the same connection, and the desktop's log says so",
      held(pids[1]) and os.path.exists(d1log) and
      "renewed in a new shell" in open(d1log).read(),
      (maps(pids[1]), os.path.exists(d1log)))
check("and the session still has the same two displays, where they were",
      wait(lambda: clients() == was, 5), (was, clients()))
check("neither terminal was reset on the way: no alternate screen left, no "
      "keyboard flags taken off",
      b"\x1b[?1049l" not in t0.out[cut0:] and b"\x1b[?1049l" not in t1.out[cut1:]
      and b"\x1b[=0;1u" not in t0.out[cut0:], (t0.out[cut0:][:200],))
t0.collect(0.3)
t1.collect(0.3)
check("and neither display's pictures were deleted by the upgrade: a "
      "resumed display has its placements taken away (d=a), never its "
      "images, which Blit would refuse frames for for good (d=A)",
      b"\x1b_Ga=d,d=A" not in t0.out[cut0:] and b"\x1b_Ga=d,d=A" not in t1.out[cut1:]
      and b"\x1b_Ga=d,d=a" in t0.out[cut0:],
      (t0.out[cut0:].count(b"d=A"), t0.out[cut0:].count(b"d=a")))
check("each display said a fresh hello to its own Blit, which is how it "
      "gains what the new code takes",
      wait(lambda: hellos(lg0) > h0[0] and hellos(lg1) > h0[1], 5),
      (h0, hellos(lg0), hellos(lg1)))
if br:
    # A game goes on sending frames; the route is said again on the
    # first one after the console's connection to hold came back.
    for _ in range(6):
        br.send()
        time.sleep(0.3 * SLOW)
    rep = wait(lambda: [x for x in routes(lg0)[r0:]
                        if x.get("verb") in ("eis", "on")], 5)
    check("the game's input route is set up again at the renewed display's "
          "Blit, on as it was", rep and {x["verb"] for x in rep} >= {"eis", "on"},
          (r0, [x for x in lines(lg0) if x["kind"] != "frame3"][-12:]))
    late = [x for x in frames(lg0)[nf0:]]
    check("frames go on reaching the renewed display's Blit, numbered on "
          "from before -- the console numbers them, not the client -- so "
          "none is dropped as stale", late and min(x["seq"] for x in late) > seq0,
          (seq0, [x["seq"] for x in late][:6]))
    got = br.answers(1.0)
    check("and the producer hears no error about its route",
          not [g for g in got if g.startswith("ERROR")], got)
check("the loop is still waiting on the display, not returned",
      not os.path.exists(after0) and not os.path.exists(after1),
      (os.path.exists(after0), os.path.exists(after1)))
subprocess.run([screen.HIBR, "-c", load("hold") + "hold detach %s" % nm],
               env=dict(os.environ, **env), capture_output=True, timeout=10)
ret = wait(lambda: os.path.exists(after0) and open(after0).read().strip(), 5)
check("and when the display is let go it returns into its own script, as a "
      "standby display needs", ret and ret.split()[0] == "0", ret)
bare = subprocess.run([screen.HIBR, "-c", "need hold; hold attach -R; "
                       'echo "$? $HOLD_WHY"'], env=dict(os.environ, **env),
                      capture_output=True, text=True, timeout=10)
check("a takeover with nothing carried fails and says why, so the loop "
      "cannot spin on it", bare.stdout.strip().startswith("2 ") and
      "renew" not in bare.stdout, (bare.stdout, bare.stderr))
open(out + ".stop", "w").close()
time.sleep(0.5)
t1.quit(None, 0.3)
t0.quit(None, 0.3)
subprocess.run([screen.HIBR, "-c", load("hold") + "hold kill %s" % nm],
               env=dict(os.environ, **env), capture_output=True)
for p in (ph, ph2):
    p.terminate()
    p.wait()
shutil.rmtree(D, True)
report(15)
