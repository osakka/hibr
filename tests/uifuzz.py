#!/usr/bin/env python3
"""Random keys, clicks, drags and wheel turns against each app, seeded.

    python3 tests/uifuzz.py                       # every target, SEED=1
    python3 tests/uifuzz.py calc mines            # just these
    SEED=7 N=400 python3 tests/uifuzz.py puzzle   # replay, or push harder
    SEED=random python3 tests/uifuzz.py           # somewhere new

The default seed is fixed, so as one of tests/all.py's suites it asks the
same questions every run and a failure there is a regression; a new seed is
how new ground is covered, and the seed a run used is printed first.

Each target gets a desktop of its own with one window open, and N events
aimed inside that window's body. Afterwards three things must hold: the
desktop is still running, it still answers -- it has read every byte it was
sent and said it was idle -- and nothing printed `hibr: ...`, on the
terminal or in desktop.log. A failure names the seed and the last events
sent, and the same SEED replays the same events.

What it can reach is chosen for this machine as much as for the apps. The
events stay inside the window's body and never include escape, a function
key or alt, so the menu bar never opens and nothing outside the window is
clicked; the session sources only the target, so there is no other app to
launch. Task Manager (it sends signals to real processes), Files (it moves
and trashes real files), Terminal (it runs a shell) and the Date & Time pane
(it runs sudo) are not targets, and must not become targets without a
sandbox that makes what they do harmless. `q` is left out too, since an app
that does not take it asks whether to quit, and a `y` after it would end
the run early, which is not a finding.
"""
import os
import random
import shutil
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import screen as sx  # noqa: E402
from screen import (Term, check, report, press, release, drag, wheel,  # noqa
                    load, tree, ERRS)

WM = tree("examples/desktop/desktop.hibr")
DESK = tree("examples/desktop")
CP = tree("examples/desktop/control-panel")
UNSAFE_PANES = ("datetime.hibr",)
GEOM = (18, 60, 2, 2)
PANEL_GEOM = (20, 58, 2, 2)

TARGETS = {
    "calc": "desk-accessories/calc.hibr",
    "clock": "desk-accessories/clock.hibr",
    "imgview": "desk-accessories/imgview.hibr",
    "stickies": "desk-accessories/stickies.hibr",
    "puzzle": "desk-accessories/puzzle.hibr",
    "about": "apps/about.hibr",
    "notifications": "apps/notifications.hibr",
    "bricks": "apps/Games/bricks.hibr",
    "mines": "apps/Games/mines.hibr",
    "snake": "apps/Games/snake.hibr",
    "panel": "apps/panel.hibr",
}

KEYS = ([bytes([c]) for c in range(0x20, 0x7f) if chr(c) not in "qQ"]
        + [b"\x1b[A", b"\x1b[B", b"\x1b[C", b"\x1b[D", b"\r", b"\t",
           b"\x1b[Z", b"\x7f", b"\x1b[3~", b"\x1b[H", b"\x1b[F",
           b"\x1b[5~", b"\x1b[6~", b"\x01", b"\x05", b"\x0b", b"\x15",
           b"\x17", b"\x04"])
KINDS = ["key"] * 5 + ["arrow"] * 3 + ["click"] * 4 + ["right"] + \
        ["drag"] * 2 + ["wheel"] * 2 + ["pause"]
ARROWS = KEYS[KEYS.index(b"\x1b[A"):KEYS.index(b"\x1b[A") + 4]


def panes():
    """A folder holding every bundled pane but the unsafe ones, as links."""
    d = tempfile.mkdtemp(prefix="hibr-uifuzz-panes-")
    for f in sorted(os.listdir(CP)):
        if f.endswith(".hibr") and f not in UNSAFE_PANES:
            os.symlink(os.path.join(CP, f), os.path.join(d, f))
    return d


def session(name, rel, geom, pdir, where):
    """Write the session file for one target: its desktop, its one window."""
    src = ". %s/%s\n" % (DESK, rel)
    if name == "panel":
        src += 'CP_PANEDIRS+=("%s")\ncp_panes\n' % pdir
    p = os.path.join(where, name + ".hibr")
    open(p, "w").write(
        "%s. %s\n%s"
        "sudo() { echo 'uifuzz: sudo refused' >&2; return 1; }\n"
        "dt_open\ndt_new \"%s\" %d %d %d %d %s\ndt_run\ndt_close\n"
        % (load("console"), WM, src, name.title(), *geom, name))
    return p


def event(rng, geom):
    """One random event inside the window body: its bytes and a label."""
    h, w, r, c = geom
    top, bot, left, right = r + 1, r + h - 2, c + 1, c + w - 2

    def spot():
        return rng.randint(top, bot), rng.randint(left, right)

    k = rng.choice(KINDS)
    if k == "key":
        b = rng.choice(KEYS)
        return b, "key %r" % b
    if k == "arrow":
        b = rng.choice(ARROWS)
        return b, "key %r" % b
    if k in ("click", "right"):
        y, x = spot()
        btn = 2 if k == "right" else 0
        return press(y, x, btn) + release(y, x, btn), "%s %d,%d" % (k, y, x)
    if k == "drag":
        y, x = spot()
        out, path = press(y, x), [(y, x)]
        for _ in range(rng.randint(1, 4)):
            y, x = spot()
            out += drag(y, x)
            path.append((y, x))
        return out + release(y, x), "drag " + " ".join(
            "%d,%d" % p for p in path)
    if k == "wheel":
        y, x = spot()
        up = rng.random() < 0.5
        return wheel(y, x, up), "wheel%s %d,%d" % ("up" if up else "down",
                                                   y, x)
    return round(rng.uniform(0.05, 0.4), 2), "pause"


def fuzz(name, seed, n, pdir, where):
    """Drive one target with n events; whether it lived, answered, was quiet."""
    geom = PANEL_GEOM if name == "panel" else GEOM
    p = session(name, TARGETS[name], geom, pdir, where)
    rng = random.Random("%s:%s" % (seed, name))
    env = {"ST_DIR": os.path.join(where, "stickies"), "DT_STICKYSTART": "0"}
    t = Term(p, env=env, settle=0.6)
    sent, answered = [], t.idle
    for _ in range(n):
        data, label = event(rng, geom)
        sent.append(label)
        if isinstance(data, float):
            t.collect(data)
            continue
        try:
            t.send(data)
        except OSError:
            break
        if t.idle and not t.idled():
            answered = False
            break
    if answered:
        answered = t.until_idle(5.0)
    before = len(ERRS)
    t.collect(0.1)
    try:
        pid, _ = os.waitpid(t.pid, os.WNOHANG)
        alive = pid == 0
    except ChildProcessError:
        alive = False
    t.close()
    errs = ERRS[before:]
    del ERRS[before:]
    tail = "\n".join("    " + s for s in sent[-8:])
    how = "replay: SEED=%s N=%d python3 tests/uifuzz.py %s" % (seed, n, name)
    ok1 = check("%s: still running after %d events" % (name, len(sent)),
                alive, "%s\n%s" % (how, tail))
    ok2 = check("%s: still answering" % name, answered,
                "%s\n%s" % (how, tail))
    ok3 = check("%s: printed no shell error" % name, not errs,
                "%s\n%s" % (how, "\n".join(dict.fromkeys(errs))))
    if not (ok1 and ok2 and ok3):
        print("  " + how)
        for e in dict.fromkeys(errs):
            print("  " + e)
        print("  last events:\n" + tail)


def main():
    want = [a for a in sys.argv[1:] if not a.startswith("-")] or list(TARGETS)
    bad = [a for a in want if a not in TARGETS]
    if bad:
        sys.exit("uifuzz: no such target: %s (targets: %s)"
                 % (" ".join(bad), " ".join(TARGETS)))
    seed = os.environ.get("SEED") or "1"
    if seed == "random":
        seed = str(random.randrange(1 << 30))
    n = int(os.environ.get("N") or 150)
    print("uifuzz: SEED=%s N=%d" % (seed, n))
    pdir = panes()
    where = tempfile.mkdtemp(prefix="hibr-uifuzz-")
    try:
        for name in want:
            fuzz(name, seed, n, pdir, where)
    finally:
        shutil.rmtree(pdir, ignore_errors=True)
        shutil.rmtree(where, ignore_errors=True)
    report(3 * len(want) + 1)


if __name__ == "__main__":
    main()
