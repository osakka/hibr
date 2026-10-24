#!/usr/bin/env python3
"""Drive the line editor through a pseudo terminal and check what it draws.

The .t files cannot reach any of this: the editor only runs when stdin is a
terminal.  Run it directly:  python3 tests/editor.py [path-to-hibr]
"""
import os, shutil, sys, tempfile

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

# A prompt hook hands the right side over in RET_RIGHT, as final text the
# way RET is (#360): nothing in it is expanded again.
HOOK = 'fn pf() { RET_RIGHT=%s; ret %s; }\nPROMPT_FN=pf\nPS2="... "\n'
t = session(HOOK % ("'[$x]'", "'> '"), RUN)
check("a hook's right side is drawn as it was given", "\033[56G[$x]" in t)
t = session(HOOK % ("'[r]'", "$'top\\n> '"), RUN)
check("and on the row where typing starts, under a two-line prompt",
      "\033[57G[r]" in t)
t = session(HOOK % ("'[r]'", "'> '"), ["if true\n", "then echo hi\n", "fi\n", "exit\n"])
check("a continuation line does not carry it",
      "[r]" not in t.split("... ")[1] if "... " in t else False)
t = session("need prompt\nPROMPT[format]='> '\nPROMPT[right]='right'\n", RUN)
check("the prompt module draws PROMPT[right] there", "\033[55Gright" in t)

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

# A prompt is bash's (Gitea #350): \[...\] marks what takes no room, \033
# and the other octal escapes are bytes, and the rest of bash's escapes
# answer as bash does. The owner's own PS1 first.
import getpass, re, socket, time
VER = re.search(r'HIBR_VER "([^"]+)"', open(sx.tree("include/hibr.h")).read()).group(1)
def shell(rc, keys, cols=80, cwd=None):
    """An interactive shell in cwd: the last redraw of the line, and the session."""
    path = sx.scratch("editor-rc")
    open(path, "w").write(("cd %s\n" % cwd if cwd else "") + rc)
    t = Term(rows=24, cols=cols, env={"HIBR_RC": path, "TERM": "xterm"}, settle=0.35)
    t.keys(keys, settle=0.3, collect=0.05)
    last = t.out.decode("utf-8", "replace").split("\r\033[0K")[-1]
    t.send(b"\x05\x15exit\n")
    t.close()
    os.unlink(path)
    return last, t


def plain(s):
    """What a redraw shows: its escapes taken out, and where it leaves the cursor."""
    m = re.search(r"\r\033\[(\d+)C$", s)
    cur = int(m.group(1)) if m else None
    s = re.sub(r"\r\033\[\d+C$", "", s)
    return re.sub(r"\033\[[0-9;?]*[A-Za-z]", "", s), cur


OWNER = r"PS1='\[\033[1;31m\]\u@\h\[\033[0m\]:\[\033[1;34m\]\w\[\033[0m\]# '" + "\n"
D = os.path.realpath(tempfile.mkdtemp(prefix="hibr-editor-"))
me, host = getpass.getuser(), socket.gethostname().split(".")[0]
last, t = shell(OWNER, ["x", "\x1b[D"], cwd=D)
vis = "%s@%s:%s# " % (me, host, D)
txt, cur = plain(last)
check("PS1's \\033 inside \\[ \\] is an escape, and the prompt reads as bash's",
      txt == vis + "x" and "\033[1;31m%s@%s" % (me, host) in last, repr(last))
check("and the cursor is placed by the visible prompt, not by its escapes",
      cur == len(vis), (cur, len(vis)))
last, t = shell(r"PS1='[\101\102] \V \D{%Y} \A> '" + "\n", ["x"])
txt, _ = plain(last)
check("octal escapes, \\V, \\D{format} and \\A in a prompt",
      re.fullmatch(r"\[AB\] %s %s \d\d:\d\d> x" % (re.escape(VER), time.strftime("%Y")), txt)
      is not None, repr(txt))
last, t = shell(r"PS1='\#> '" + "\n", ["true\n", "true\n", "x"])
check("\\# counts the commands this shell has run", plain(last)[0] == "3> x", repr(last))

# Completion quotes what it completes (Gitea #350): a name with a space is
# escaped, or finished inside the quote the word opened, and a word already
# quoted or escaped is understood before it is completed.
os.makedirs(os.path.join(D, "My Folder"))
open(os.path.join(D, "My Folder", "note one.txt"), "w").close()
for n in ("cost$5", "a&b", "it's"):
    open(os.path.join(D, n), "w").close()
CASES = [
    ("cd My\t", "cd My\\ Folder/"),
    ("ls My\\ Fo\t", "ls My\\ Folder/"),
    ('ls "My Fo\t', 'ls "My Folder/'),
    ("ls 'My Fo\t", "ls 'My Folder/"),
    ('ls "My Fo\t\t', 'ls "My Folder/note one.txt" '),
    ("ls cos\t", "ls cost\\$5 "),
    ("ls a\t", "ls a\\&b "),
    ("ls it\t", "ls it\\'s "),
]
for typed, want in CASES:
    last, t = shell('PS1="> "\n', [typed], cwd=D)
    check("completing %r gives %r" % (typed, want), plain(last)[0] == "> " + want,
          repr(last))
last, t = shell('PS1="> "\nHOME=%s\n' % D, ["ls ~/My\t"])
check("a word beginning ~/ completes and keeps its tilde",
      plain(last)[0] == "> ls ~/My\\ Folder/", repr(last))
shutil.rmtree(D, True)

print()
# Agent mode is for a script a program runs, with nobody at the terminal, so
# a terminal on standard input is swapped for /dev/null: a read gets the end
# of input at once rather than waiting for a person.
t = Term("--agent", "-c", 'read -r x; echo "read [$x] status $?"', settle=0,
         size=False)
t.wait(3.0)
check("in agent mode a read on a terminal ends at once instead of waiting",
      t.exited and "read [] status 1" in t.text, t.text)

report(33)
