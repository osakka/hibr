#!/usr/bin/env python3
"""Random keys, clicks, drags and wheel turns against each app, seeded.

    python3 tests/uifuzz.py                       # every target, SEED=1
    python3 tests/uifuzz.py calc mines            # just these
    python3 tests/uifuzz.py oracle                # only the redraw oracle
    SEED=7 N=400 python3 tests/uifuzz.py puzzle   # replay, or push harder
    SEED=7 ON=100 python3 tests/uifuzz.py oracle  # the oracle, harder
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
import struct
import sys
import tempfile
import zlib

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
    "calc": "apps/Accessories/calc.hibr",
    "clock": "apps/Accessories/clock.hibr",
    "imgview": "apps/Accessories/imgview.hibr",
    "stickies": "apps/Accessories/stickies.hibr",
    "puzzle": "apps/Accessories/puzzle.hibr",
    "about": "system/about.hibr",
    "notifications": "system/notifications.hibr",
    "bricks": "apps/Games/bricks.hibr",
    "mines": "apps/Games/mines.hibr",
    "snake": "apps/Games/snake.hibr",
    "panel": "system/panel.hibr",
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


# --- the forced-redraw oracle (Gitea #115) ---------------------------------
#
# The checks above ask whether a desktop survived; this asks whether what it
# drew is what it *would have* drawn with none of its skipping. Since 0.99.83
# a window nothing changed is left alone and the wallpaper may paint nothing,
# and every fault in that since -- a closed window left on screen, shadows
# pulsing, a note's box surviving its own expiry (#114), the wallpaper deleted
# by a quiet frame (#112), a sticky note's shadow present only on the frames
# it was skipped (#116) -- was found by the owner looking at their screen.
# CLAUDE.md said this oracle was needed before any of that shipped.
#
# One session, one seed, and after every event: settle, snapshot, force a full
# redraw, settle, snapshot, compare glyphs *and* pens, unforce. Pens because a
# shadow is a colour and no glyph comparison can see one.
#
# Three things about it that look like details and are not:
#
# * It deliberately breaks this file's own rule about staying inside a
#   window. Menus, notes, context menus and workspace switches are overlays,
#   and an overlay that stops being drawn is one of the ways the invariant
#   breaks, so they are in the mix on purpose. Nothing here can launch an app:
#   a menu is opened and closed inside one event.
# * Forcing after every event corrects the screen, so a fault cannot
#   accumulate across events here. That is covered by the settled oracle in
#   tests/desktop.py, which compares after a whole drag. This one's job is the
#   breadth of event kinds, and it should not be "fixed" into a two-session
#   design -- two sessions disagree about the clock and settle at their own
#   pace.
# * The arrangement is the point. A window that overlaps another is never
#   left alone, so an oracle whose windows all overlap tests the old code
#   path and reports nothing: that is exactly why three earlier oracles were
#   silent through #116. So the sticky and one oracle window overlap nothing
#   and can be skipped, while another oracle window overlaps the terminal and
#   never can.
OWINS = {
    # h, w, row, col -- a bare window with a dirty flag of its own, which is
    # the shape #116 lived in, overlapping nothing so it can be skipped.
    "sticky": (7, 22, 2, 55),
    # An ordinary window, also skippable, also with a dirty flag.
    "alone": (7, 22, 2, 4),
    # A terminal, for term_dirty: quiet and unable to echo, so the screen
    # settles and a stray key cannot reach a shell.
    "shell": (7, 22, 15, 55),
    # Overlaps the terminal, so it is never left alone: the other gate state.
    "orc": (9, 30, 13, 30),
}
OBARE = (16, 22, 2, 28)
ORACLE_N = int(os.environ.get("ON") or 30)

OAPP = (
    'declare -gA OC OD\n'
    'orc_open() { OC[$1]=0; return 0; }\n'
    'orc_draw() { local i\n'
    '             for i in 1 2 3; do\n'
    '                     console put -p "w$1" $((i + 1)) 2 "row $i n ${OC[$1]}"\n'
    '             done; return 0; }\n'
    # F and G turn the skipping off and on again from inside the session, and
    # F writes a mark: a key only reaches the app that has focus, and a force
    # that never arrived would make the comparison compare a frame with
    # itself. z is a key this app draws nothing new for, so a frame can be
    # asked for without changing what is on the screen.
    'orc_key()  { case $2 in\n'
    '             F) DT_FORCEDRAW=1; printf F >> "$ORC_MARK"; return 0 ;;\n'
    '             G) DT_FORCEDRAW=0; return 0 ;;\n'
    '             z) return 0 ;;\n'
    '             N) dt_note "a note over the screen"; return 0 ;;\n'
    '             esac\n'
    '             OC[$1]=$(( ${OC[$1]} + 1 )); OD[$1]=1; return 0; }\n'
    # C marks a frame this window was left alone on. Without it a run in which
    # nothing was ever skipped -- an arrangement where everything overlaps,
    # say -- would pass while proving nothing.
    'orc_dirty() { [ -n "${OD[$1]}" ] && { OD[$1]=; return 0; }\n'
    '              printf C >> "$ORC_MARK"; return 1; }\n')


def wallpng(where, w, h):
    """A picture to be the wallpaper, so the under-text path is exercised."""
    raw = b"".join(b"\x00" + bytes(v for x in range(w)
                                   for v in (x % 256, y * 3 % 256, 150))
                   for y in range(h))

    def chunk(tag, data):
        body = tag + data
        return (struct.pack(">I", len(data)) + body
                + struct.pack(">I", zlib.crc32(body)))

    p = os.path.join(where, "wall.png")
    open(p, "wb").write(
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
        + chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b""))
    return p


def osession(where, wall):
    """The oracle's own desktop: four windows, a picture wallpaper, and
    everything that would make two frames differ for a reason of its own
    turned off -- the clock (a minute can pass between two snapshots), the
    cursor blink (it would never settle) and a note's own lifetime, kept
    short enough that a note raised by an event has gone before either
    snapshot rather than between them."""
    p = os.path.join(where, "oracle.hibr")
    open(p, "w").write(
        "%s. %s\n"
        ". %s/apps/Accessories/stickies.hibr\n"
        ". %s/apps/term.hibr\n"
        "%s"
        'dt_app orc "Oracle" %d %d "" "" "" "" ""\n'
        "DT_IMGMODE=pixels\nDT_WALLIMG=%s\nDT_WALLMODE=stretch\n"
        "DT_CURSOR_BLINK=0\nDT_NOTEMS=200\nDT_BARTIME=hibr\n"
        "dt_open\n"
        'dt_new "Note" %d %d %d %d stickies\n'
        'dt_new "Alone" %d %d %d %d orc\n'
        "TW_CMD=(/bin/sh -c 'stty -echo raw 2> /dev/null; exec sleep 3600')\n"
        'dt_new "Shell" %d %d %d %d term\n'
        'dt_new "Oracle" %d %d %d %d orc\n'
        "dt_sticky 1 1\n"
        "dt_run\ndt_close\n"
        % (load("console", "term", "img"), WM, DESK, DESK, OAPP,
           OWINS["orc"][0], OWINS["orc"][1], wall,
           *OWINS["sticky"], *OWINS["alone"], *OWINS["shell"], *OWINS["orc"]))
    return p


def osettled(t, quiet=0.35, limit=4.0):
    """The screen once the desktop has stopped drawing, not after a pause: a
    frame a timer asked for lands after the idle marker, and two snapshots
    taken either side of one are of two different moments."""
    was, spent = t.frames(), 0.0
    while spent < limit:
        t.collect(quiet)
        spent += quiet
        if t.frames() == was:
            return t.screen()
        was = t.frames()
    return t.screen()


def oevent(rng, at):
    """One random event, and where the oracle window is afterwards. Every
    press is on that window or on bare desktop, never on the terminal."""
    h, w = OWINS["orc"][0], OWINS["orc"][1]
    tr, br, lc, rc = at[0] + 1, at[0] + h - 2, at[1] + 1, at[1] + w - 2
    tir, tic = at[0], at[1] + 3

    def spot():
        return rng.randint(tr, br), rng.randint(lc, rc)

    def bare():
        return rng.randint(OBARE[0], OBARE[1]), rng.randint(OBARE[2], OBARE[3])

    k = rng.choice(["key"] * 4 + ["click"] * 3 + ["drag"] * 3 + ["wheel"] * 2
                   + ["ws"] * 3 + ["note"] * 2 + ["menu"] * 2 + ["ctx"] * 2
                   + ["pause"] * 4 + ["long"] * 4)
    if k == "key":
        b = rng.choice([b"x", b"a", b"1", b"\x1b[A", b"\x1b[B", b"\t", b"\r"])
        return b, "key %r" % b
    if k == "click":
        y, x = spot()
        return press(y, x) + release(y, x), "click %d,%d" % (y, x)
    if k == "drag":
        out, path = press(tir, tic), [(tir, tic)]
        y, x = tir, tic
        for _ in range(rng.randint(1, 3)):
            y, x = rng.randint(2, 14), rng.randint(4, 48)
            out += drag(y, x)
            path.append((y, x))
        at[0], at[1] = y, x - 3
        return out + release(y, x), "drag " + " ".join("%d,%d" % q for q in path)
    if k == "wheel":
        y, x = bare() if rng.random() < 0.5 else spot()
        up = rng.random() < 0.5
        return wheel(y, x, up), "wheel%s %d,%d" % ("up" if up else "down", y, x)
    if k == "ws":
        n = rng.choice([b"1", b"2", b"3"])
        return b"\x1b" + n, "workspace %s" % n.decode()
    if k == "note":
        return press(tir, tic) + release(tir, tic) + b"N", "note"
    if k == "menu":
        return b"\x1b[21~" + b"\x1b", "menu bar, then escape"
    if k == "ctx":
        y, x = bare()
        return (press(y, x, 2) + release(y, x, 2) + b"\x1b",
                "context menu %d,%d" % (y, x))
    if k == "long":
        return 1.3, "long pause"
    return round(rng.uniform(0.05, 0.4), 2), "pause"


def oracle(seed, n, where):
    """Drive one composed desktop and compare every frame against the same
    frame with the skipping turned off."""
    wall = wallpng(where, 320, 200)
    p = osession(where, wall)
    mark = os.path.join(where, "marks")
    open(mark, "w").close()
    rng = random.Random("oracle:%s" % seed)
    t = Term(p, env={"ST_DIR": os.path.join(where, "stickies"),
                     "DT_STICKYSTART": "0", "DT_TICK": "60",
                     "HIBR_GFX": "kitty", "ORC_MARK": mark},
             settle=1.2, cellw=8, cellh=16)
    t.collect(1.5)
    at = [OWINS["orc"][2], OWINS["orc"][3]]
    sent, bad, lost = [], [], 0
    before = len(ERRS)
    for i in range(n):
        data, label = oevent(rng, at)
        sent.append(label)
        if isinstance(data, float):
            t.collect(data)
        else:
            t.send(data)
        # Focus the oracle window, because the force is a key and a key only
        # reaches the app that has focus -- sent while the sticky note had it,
        # F was typed into the note as text. Then one more frame: the click
        # raised the window, a raise reorders the panes, dt_alone sees a
        # changed signature and sets DT_FORCEONCE, so the frame right after a
        # click is a full one already and comparing it proves nothing.
        t.send(press(at[0], at[1] + 3) + release(at[0], at[1] + 3))
        t.collect(0.25)
        t.send(b"z")
        sc = osettled(t)
        g0 = [r[:] for r in sc.g]
        p0 = [r[:] for r in sc.p]
        t.send(b"F")
        t.collect(0.2)
        t.send(b"z")
        sc = osettled(t)
        dif = [(r, c, g0[r][c], sc.g[r][c], p0[r][c], sc.p[r][c])
               for r in range(len(g0)) for c in range(len(g0[r]))
               if g0[r][c] != sc.g[r][c] or p0[r][c] != sc.p[r][c]]
        if dif:
            bad.append((i, label, dif))
        if open(mark).read().count("F") != i + 1 - lost:
            lost += 1
        t.send(b"G")
        t.collect(0.2)
    marks = open(mark).read()
    t.close()
    errs = ERRS[before:]
    del ERRS[before:]
    how = "replay: SEED=%s ON=%d python3 tests/uifuzz.py oracle" % (seed, n)
    tail = "\n".join("    " + s for s in sent[-8:])
    show = [how, tail]
    for i, label, dif in bad[:3]:
        show.append("  event %d (%s), %d cells:" % (i, label, len(dif)))
        for r, c, ga, gb, pa, pb in dif[:10]:
            show.append("    (%d,%d) left alone %r %s   redrawn %r %s"
                        % (r, c, ga, pa, gb, pb))
    ok = check("oracle: no event left the screen different from a full redraw",
               not bad, "\n".join(show))
    check("oracle: the force reached it on every event", not lost,
          "%s\n%d of %d events compared a frame with itself" % (how, lost, n))
    check("oracle: windows were left alone during the run",
          marks.count("C") > 0,
          "%s\nnothing was ever skipped, so nothing was really compared" % how)
    check("oracle: printed no shell error", not errs,
          "\n".join(dict.fromkeys(errs)))
    if not ok:
        print("\n".join("  " + s for s in show))


def main():
    every = list(TARGETS) + ["oracle"]
    want = [a for a in sys.argv[1:] if not a.startswith("-")] or every
    bad = [a for a in want if a not in every]
    if bad:
        sys.exit("uifuzz: no such target: %s (targets: %s)"
                 % (" ".join(bad), " ".join(every)))
    seed = os.environ.get("SEED") or "1"
    if seed == "random":
        seed = str(random.randrange(1 << 30))
    n = int(os.environ.get("N") or 150)
    apps = [a for a in want if a != "oracle"]
    print("uifuzz: SEED=%s N=%d%s"
          % (seed, n, " ON=%d" % ORACLE_N if "oracle" in want else ""))
    pdir = panes()
    where = tempfile.mkdtemp(prefix="hibr-uifuzz-")
    try:
        for name in apps:
            fuzz(name, seed, n, pdir, where)
        if "oracle" in want:
            oracle(seed, ORACLE_N, where)
    finally:
        shutil.rmtree(pdir, ignore_errors=True)
        shutil.rmtree(where, ignore_errors=True)
    report(3 * len(apps) + (4 if "oracle" in want else 0) + 1)


if __name__ == "__main__":
    main()
