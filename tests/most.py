#!/usr/bin/env python3
"""Drive the pager through a pseudo terminal.

None of this is reachable from run.sh: a pager needs a terminal to draw on and
a keyboard to read, and its whole point is that those are not the same thing as
its input. Note that the screen layer only sends the cells that changed, so a
check has to look at everything the session emitted rather than at the last
frame alone -- which is what tests/screen.py's model is for.
Run it directly:  python3 tests/most.py [path-to-hibr]
"""
import os, sys, tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import screen
from screen import Term, Screen, check, report, load

if len(sys.argv) > 1:
    screen.HIBR = os.path.abspath(sys.argv[1])
D = tempfile.mkdtemp(prefix="hibr-most-")
LOAD = load("console", "most")


def run(cmd, keys=(), settle=0.7, rows=24, cols=80):
    """Page something, send keys, and return everything that came back."""
    t = Term("-c", LOAD + cmd, rows=rows, cols=cols, settle=settle)
    t.keys(keys, settle=0.4, collect=0.35)
    t.quit(b"q", 0.6)
    return t.text


def screen_of(t, rows=24, cols=80):
    return Screen(rows, cols).feed(t).text()


def write(nm, data):
    p = os.path.join(D, nm)
    open(p, "wb").write(data if isinstance(data, bytes) else data.encode())
    return p


plain = write("p.txt", "".join("line %d\n" % i for i in range(1, 201)))
col = write("c.txt", "plain\n\x1b[31mred\x1b[0m\n\x1b[1;32mbold green\x1b[0m\n"
                     "needle one\nmiddle\nneedle two\n")
wide = write("w.txt", "aaa " + "b" * 84 + " ZZZEND\nshort\n")
two = write("t.txt", "second file here\n")

raw = run("most %s" % plain)
v = screen_of(raw)
check("it pages a file", "line 1" in v and "line 23" in v)
check("the status line counts", "1-23/200" in v)
t = raw
check("the alternate screen is used and given back",
      "\x1b[?1049h" in t and "\x1b[?1049l" in t)

v = screen_of(run("seq 1 200 | most"))
check("its input can be a pipe while its keys are not",
      "1-23/200" in v and "line" not in v and "1" in v)
check("a pipe is named as such", "standard input" in v)

v = screen_of(run("most %s" % plain, keys=[b"G"]))
check("G reaches the end", "178-200/200" in v and "line 200" in v)
v = screen_of(run("most %s" % plain, keys=[b"G", b"g"]))
check("g comes back to the top", "1-23/200" in v and "line 1" in v)
v = screen_of(run("most %s" % plain, keys=[b" "]))
check("space pages down", "24-46/200" in v and "line 24" in v)
v = screen_of(run("most %s" % plain, keys=[b" ", b"\x02"]))
check("ctrl-b pages back", "1-23/200" in v)
v = screen_of(run("most %s" % plain, keys=[b"\x1b[B", b"\x1b[B"]))
check("the arrow keys move a line at a time", "3-25/200" in v)

t = run("most %s" % col)
check("colour in the input becomes a pen, not escapes on screen",
      "38;5;1" in t and "38;5;2" in t)

t = run("most %s" % col, keys=[b"/needle\r"])
check("search highlights a match", "48;5;227" in t)
check("every match is highlighted, not just the next one",
      t.count("48;5;227") >= 2)

v = screen_of(run("most %s" % wide, keys=[b"l", b"l", b"l"]))
check("it scrolls sideways", "ZZZEND" in v)
check("and says which column it is at", "col 25" in v)
v = screen_of(run("most %s" % wide, keys=[b"l", b"l", b"l", b"h", b"h", b"h"]))
check("and comes back", v.startswith("aaa "))

v = screen_of(run("most %s %s" % (plain, two), keys=[b"s"]))
check("s splits the view over two files", "p.txt" in v and "t.txt" in v)
v = screen_of(run("most %s %s" % (plain, two), keys=[b"n"]))
check("n moves to the next file",
      "t.txt" in v and "second file here" in v)

v = screen_of(run("most %s" % plain, keys=[b"F"]))
check("F says it is following", "FOLLOWING" in v)

t = run("most %s" % plain, keys=[b"?"])
check("? lists the keys", "q" in t and "tab" in t)

t = run("most %s/nosuchfile; echo rc=$?" % D, settle=0.5)
check("a missing file is refused", "rc=" in t)

tabs = write("tab.txt", "col\tA\nlonger\tB\n")
v = screen_of(run("most %s" % tabs))
check("tabs reach their stop rather than showing as ^I",
      "col     A" in v and "longer  B" in v and "^I" not in v)

print()
for f in os.listdir(D):
    os.unlink(os.path.join(D, f))
os.rmdir(D)
report(21)
