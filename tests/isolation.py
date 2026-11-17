#!/usr/bin/env python3
"""Nothing an app does can stop the desktop: ADR 0050's definition of done.

The owner, 2026-11-17: "nothing should be able to ever affect the stability
of the desktop". Every installable runs inside the desktop's one process
today, so an app that waits stops every window -- the Video Player held the
owner's desktop 8 s on an engine call, the Browser 22 s. This suite is the
target that isolation is built against, and it FAILS until then, by design:
it is not in tests/all.py's list, and goes there in the release that makes
it pass.

Each case opens a good app beside a bad one. The good one shows the last key
it was given; the case focuses it and types, and the key must be on screen
within half a second -- a frame or two, never the bad app's time -- and the
desktop's log must still name the bad app. Three ways to be bad, each a
real one:

  sleep    its _draw sleeps 3 s, every time it is drawn
  spin     its _key burns the processor for 3 s
  block    its _draw waits 3 s in a builtin, as an engine call that is
           never answered does (`read -t` on a pipe nobody writes)
"""
import os, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from screen import Term, check, report, load, tree, scratch, expect, press, release

MOD = tree("build/mods/console.so")
WM = tree("programs/desktop/desktop.hibr")

GOOD = ('declare -gA GD\n'
        'fn gd_draw(int id, int h, int w, int row, int col) {\n'
        '    console put -p "w$id" 1 1 "got: ${GD[$id]["k"]:-none}"\n'
        '}\n'
        'fn gd_key(int id, key) {\n'
        '    GD[$id]["k"]=$key\n'
        '    return 0\n'
        '}\n'
        'dt_app gd "Good" 6 30\n')

BAD = {
    "sleep": ('fn bd_draw(int id, int h, int w, int row, int col) {\n'
              '    console put -p "w$id" 1 1 "bad"\n'
              '    sleep 3\n'
              '}\n'),
    "spin": ('fn bd_draw(int id, int h, int w, int row, int col) {\n'
             '    console put -p "w$id" 1 1 "bad"\n'
             '}\n'
             'fn bd_key(int id, key) {\n'
             '    local t=$((${EPOCHREALTIME/./} + 3000000))\n'
             '    while [ "${EPOCHREALTIME/./}" -lt "$t" ]; do :; done\n'
             '    return 0\n'
             '}\n'),
    "block": ('declare -g BD_FIFO=%s\n'
              'fn bd_draw(int id, int h, int w, int row, int col) {\n'
              '    console put -p "w$id" 1 1 "bad"\n'
              '    local l\n'
              '    read -t 3 l <> "$BD_FIFO"\n'
              '}\n'),
}


def case(name):
    fifo = scratch("isolation-fifo")
    if name == "block":
        if os.path.exists(fifo):
            os.unlink(fifo)
        os.mkfifo(fifo)
    body = BAD[name] % fifo if name == "block" else BAD[name]
    p = scratch("isolation-%s.hibr" % name)
    open(p, "w").write("%s. %s\ndt_open\n%s%sdt_app bd \"Bad\" 6 30\n"
                       "dt_new \"Bad\" 6 30 2 2 bd\ndt_new \"Good\" 6 30 2 40 gd\n"
                       "dt_run\ndt_close\n" % (load(MOD), WM, GOOD, body))
    expect(r"(held|slow): (win|input):bd")
    t = Term(p, env={"DT_HOLDMS": "150", "DT_HOLDBY": "wall"}, rows=24, cols=80, settle=1.0)
    if name == "spin":
        # The bad app has the focus first and is handed a key, which is
        # what starts its spin; the good one is clicked straight after.
        bad = t.screen().find("bad")
        if bad:
            t.send(press(bad[0], bad[1]) + release(bad[0], bad[1]), settle=0, collect=0)
        t.send(b"s", settle=0, collect=0)
    good = t.screen().find("got:")
    sent = time.time()
    if good:
        t.send(press(good[0], good[1]) + release(good[0], good[1]) + b"x",
               settle=0, collect=0)
    sc = t.until("got: x", tries=50, step=0.05)
    took = time.time() - sent
    shown = sc.find("got: x") is not None
    time.sleep(3.5)
    t.collect(0.3)
    named = [l for l in open(t.log, errors="replace")
             if ":bd" in l and ("held:" in l or "slow:" in l)]
    t.quit(b"q", 1.0)
    for f in (p, fifo):
        if os.path.exists(f):
            os.unlink(f)
    check("%s: beside an app that %s, another window answers a key within "
          "half a second" % (name, {"sleep": "sleeps in its draw",
                                    "spin": "spins in its key handler",
                                    "block": "blocks in a builtin"}[name]),
          shown and took < 0.5, (round(took, 2), sc))
    check("%s: and the log still names the bad app" % name, len(named) >= 1, named)


for n in ("sleep", "spin", "block"):
    case(n)
report(7)
