#!/usr/bin/env python3
"""Drive the live traceroute through a pseudo terminal.

Everything here traces to the loopback address, which is one hop, always
reachable and always fast -- a test that depends on the internet being up and
on which routers answer is a test that fails for reasons that are not about
this code.
Run it directly:  python3 tests/mtr.py [path-to-hibr]
"""
import os, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import screen as sx
from screen import Term, Screen, check, report, load

if len(sys.argv) > 1:
    sx.HIBR = os.path.abspath(sys.argv[1])
ROWS, COLS = 12, 100
LOAD = load("console", "trace")


def run(cmd, keys=(), settle=3.0):
    t = Term("-c", LOAD + cmd, rows=ROWS, cols=COLS, settle=settle)
    t.keys(keys, settle=0.5, collect=0.4)
    t.quit(b"q", 0.8)
    return t.text


def screen(t):
    return Screen(ROWS, COLS).feed(t).text()




t = run("trace -l -m 3 -w 300 127.0.0.1")
v = screen(t)
check("it draws a table", "Hop" in v and "Loss" in v and "Jttr" in v)
check("the target is named", "127.0.0.1" in v)
check("it keeps going, round after round", "rounds" in v)
check("the first hop is the loopback", "localhost" in v or "127.0.0.1" in v)
check("loss is reported", "0.0%" in v)
check("there is a history to look at",
      any(b in v for b in "▁▂▃▄▅▆▇█"))
check("it takes the terminal and gives it back",
      "\x1b[?1049h" in t and "\x1b[?1049l" in t)

v = screen(run("trace -l -m 3 -w 300 127.0.0.1", keys=[b"p"]))
check("p pauses", "PAUSED" in v)

v = screen(run("trace -l -m 3 -w 300 127.0.0.1", keys=[b"p", b"p"]))
check("and p starts it again", "PAUSED" not in v)

t1 = run("trace -l -m 3 -w 300 127.0.0.1", settle=4.0)
t2 = run("trace -l -m 3 -w 300 127.0.0.1", keys=[b"r"], settle=4.0)


def rounds(v):
    import re
    m = re.search(r"(\d+) rounds?", v)
    return int(m.group(1)) if m else -1


check("r starts the counting again",
      0 < rounds(screen(t2)) < rounds(screen(t1)))

v = screen(run("trace -l -m 3 -w 300 -n 127.0.0.1"))
check("-n leaves the address unresolved",
      "127.0.0.1" in v and "localhost" not in v.split("\n", 1)[1])

report(11)
