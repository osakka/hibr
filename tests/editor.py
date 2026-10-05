#!/usr/bin/env python3
"""Drive the line editor through a pseudo terminal and check what it draws.

The .t files cannot reach any of this: the editor only runs when stdin is a
terminal.  Run it directly:  python3 tests/editor.py [path-to-hibr]
"""
import os, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import screen as sx
from screen import Term, check, report, expect

if len(sys.argv) > 1:
    sx.HIBR = os.path.abspath(sys.argv[1])


def session(rc, keys, cols=60):
    """Start an interactive shell with that rc, send keys, return what came back."""
    path = "/tmp/hibr-editor-rc-%d" % os.getpid()
    open(path, "w").write(rc)
    t = Term(rows=24, cols=cols, env={"HIBR_RC": path, "TERM": "xterm"},
             settle=0.35)
    t.keys(keys, settle=0.22, collect=0.05)
    t.close()
    os.unlink(path)
    return t.text


RUN = ["echo hi\n", "exit\n"]

t = session('PS1="> "\nRPS1="[right]"\n', RUN)
check("right prompt is drawn at the right edge", "\033[53G[right]" in t)
check("the cursor comes back after it", "[right]\r\033[" in t)

t = session('PS1="> "\nRPS1="[right]"\n', ["echo " + "a" * 48])
check("right prompt gives way to a long line", "[right]" not in t.split("echo a")[-1])

t = session('PS1="full> "\nTPS1="$ "\n', RUN)
check("transient prompt replaces an accepted line", "\033[0K$ echo hi" in t)
check("it replaces every accepted line", "\033[0K$ exit" in t)

t = session('PS1="> "\nRPS1="R"\nTPS1="; "\n', RUN)
check("both together: right prompt while editing", "\033[59GR" in t)
check("both together: gone from the transient line",
      "R" not in t.split("\033[0K; echo hi")[-1].split("\r\n")[0])

t = session('PS1="> "\n', RUN)
check("neither set changes nothing", "\033[59G" not in t and "0K$ " not in t)

t = session('PS1="> "\nRPS1="[\\t]"\n', ["x"])
check("the right prompt takes prompt escapes",
      any(c.isdigit() for c in t.split("G[")[-1][:8]) if "G[" in t else False)

expect(r"^axy: command not found")
t = session('PS1="> "\nRPS1="[r]"\n', ["abc", "\x7f\x7f", "xy\n", "exit\n"])
check("editing still works with a right prompt", "\r\nhibr: axy: command not found" in t
      or "axy" in t)

# A line holding right-to-left text is drawn in display order, a row at a
# time, while the buffer and every edit stay in logical order (#68): the
# cursor sits on the column its logical position landed in.
def bidi(keys, env=None):
    """The last redraw of the line, and everything the session printed."""
    path = "/tmp/hibr-editor-rc-%d" % os.getpid()
    open(path, "w").write('PS1="> "\n')
    t = Term(rows=24, cols=60, env=dict({"HIBR_RC": path, "TERM": "xterm"}, **(env or {})),
             settle=0.35)
    t.keys(keys, settle=0.22, collect=0.05)
    last = t.out.decode("utf-8", "replace").split("\r\033[0K")[-1]
    t.send(b"\nexit\n")
    t.close()
    os.unlink(path)
    return t, last


AR = "\u0633\u0644\u0627\u0645"
t, last = bidi(["echo " + AR])
check("a typed right-to-left word is drawn in display order, joined",
      last.startswith("> echo \ufee1\ufefc\ufeb3"), repr(last))
t, last = bidi(["echo " + AR, "\x1b[D"])
check("the cursor stands on the column of its logical position",
      last.endswith("\r\033[7C"), repr(last))
t, last = bidi(["echo " + AR, "\x1b[D", "\x1b[D", "X\n"])
check("editing is in logical order: X lands two characters from the end",
      "\u0633\u0644X\u0627\u0645" in t.text, t.text[-200:])
t, last = bidi(["echo " + AR], env={"HIBR_BIDI": "off"})
check("HIBR_BIDI=off draws the line as typed", last.startswith("> echo " + AR), repr(last))

print()
# Agent mode is for a script a program runs, with nobody at the terminal, so
# a terminal on standard input is swapped for /dev/null: a read gets the end
# of input at once rather than waiting for a person.
t = Term("--agent", "-c", 'read -r x; echo "read [$x] status $?"', settle=0,
         size=False)
t.wait(3.0)
check("in agent mode a read on a terminal ends at once instead of waiting",
      t.exited and "read [] status 1" in t.text, t.text)

report(16)
