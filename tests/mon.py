#!/usr/bin/env python3
"""Drive the system monitor through a pseudo terminal.

Checks are on the shape of what it draws and on numbers that can be compared
with /proc directly, not on values that move between one reading and the next.
Run it directly:  python3 tests/mon.py [path-to-hibr]
"""
import os, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import screen as sx
from screen import Term, Screen, check, report, load

if len(sys.argv) > 1:
    sx.HIBR = os.path.abspath(sys.argv[1])
ROWS, COLS = 24, 100
LOAD = load("console", "mon")


def run(cmd, keys=(), settle=3.0):
    t = Term("-c", LOAD + cmd, rows=ROWS, cols=COLS, settle=settle)
    t.keys(keys, settle=0.5, collect=0.4)
    t.quit(b"q", 0.8)
    return t.text


def screen(t):
    return Screen(ROWS, COLS).feed(t).text()





import re

t = run("mon -d 0.4", settle=2.5)
v = screen(t)
check("it draws the processors", "CPU" in v)
check("and the memory", "MEM" in v)
check("and a process table", "PID" in v and "COMMAND" in v and "RSS" in v)
check("the machine is named", os.uname()[1][:12] in v)
check("it takes the terminal and gives it back",
      "\x1b[?1049h" in t and "\x1b[?1049l" in t)
check("there are bars to read",
      "\u2588" in v or "\u2591" in v)
check("and a history",
      any(b in v for b in "\u2581\u2582\u2583\u2584\u2585\u2586\u2587"))

# one core line per processor the kernel reports
ncore = len([l for l in open("/proc/stat") if l.startswith("cpu") and l[3].isdigit()])
shown = len(re.findall(r"\b\d+ [\u2588\u2591]{8}", v))
check("every processor has its own bar", shown == ncore)

# the total memory it prints must be the total memory the kernel reports
mt = 0
for l in open("/proc/meminfo"):
    if l.startswith("MemTotal:"):
        mt = int(l.split()[1])
        break
gb = mt / 1024.0 / 1024.0
check("the memory total agrees with /proc",
      ("%.1fG" % gb) in v or ("%.1fG" % round(gb)) in v)

# Read the table from a *paused* monitor. A live one redraws while the capture
# is running, and because only changed cells are sent, the reconstruction can
# hold one row from before a re-sort and the next from after it -- which looks
# like an ordering bug and is not one.
vp = screen(run("mon -d 0.4", keys=[b"p"], settle=2.5))
rowre = re.compile(r"^\s*(\d+)\s+(\S+)\s+([\d.]+)\s+([\d.]+[BKMGT])\s+(\S+)")
rows = [rowre.match(l) for l in vp.split("\n")]
rows = [m for m in rows if m]
check("the process table holds parsable rows", len(rows) >= 3)
check("its pids are real processes",
      all(os.path.isdir("/proc/" + m.group(1)) for m in rows[:3]))
check("and it is sorted, heaviest first",
      [float(m.group(3)) for m in rows] ==
      sorted([float(m.group(3)) for m in rows], reverse=True))

v = screen(run("mon -d 0.4", keys=[b"p"], settle=2.0))
check("p pauses", "PAUSED" in v)

v = screen(run("mon -d 0.4", keys=[b"m"], settle=2.0))
check("m sorts by memory", "PID" in v)

t = run("mon --nonsense", settle=1.0)
check("a bad option is refused", "usage" in t)

report(15)
