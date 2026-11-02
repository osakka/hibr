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

# Ctrl-U and Ctrl-K at an empty prompt, before anything is typed (Gitea #429):
# the line had no storage yet, and both kills wrote its terminator through a
# NULL pointer -- the interactive shell died on signal 11.
t = session('PS1="> "\n', ["\x15", "\x0b", "echo killed-ok\n", "exit\n"])
check("Ctrl-U and Ctrl-K at an empty prompt leave the shell running",
      "killed-ok\r\n" in t or "killed-ok\n" in t)

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

# A variable's name completes after $ or ${, written as it is -- never
# escaped into \$ -- and ${ is closed with its brace, as bash does (#422).
VARS = [
    ("echo $HIBRZ\t", "echo $HIBRZQ "),
    ("echo ${HIBRZ\t", "echo ${HIBRZQ} "),
    ('echo "$HIBRZ\t', 'echo "$HIBRZQ" '),
]
for typed, want in VARS:
    last, t = shell('PS1="> "\nHIBRZQ=1\n', [typed])
    check("completing %r gives %r" % (typed, want), plain(last)[0] == "> " + want,
          repr(last))

# Home, End, Delete and the word moves arrive in more than one spelling:
# the ~ forms (1 4 7 8 3), a modifier on an arrow (;5 ctrl, ;3 alt), ESC
# ESC before one, and alt-b, alt-f. Each moves or deletes and leaves no
# text of its own behind (#422). X marks where the cursor was.
KEYS = [
    ("abc def\x1b[1~X", "Xabc def", "Home as ESC [ 1 ~"),
    ("abc\x01\x1b[4~X", "abcX", "End as ESC [ 4 ~"),
    ("abc def\x1b[7~X", "Xabc def", "Home as ESC [ 7 ~ (rxvt)"),
    ("abc\x01\x1b[8~X", "abcX", "End as ESC [ 8 ~ (rxvt)"),
    ("abc def ghi\x1b[1;5DX", "abc def Xghi", "ctrl-left moves back a word"),
    ("abc def ghi\x01\x1b[1;5CX", "abcX def ghi", "ctrl-right moves on a word"),
    ("abc def ghi\x1b[1;3DX", "abc def Xghi", "alt-left moves back a word"),
    ("abc def ghi\x01\x1b[1;3CX", "abcX def ghi", "alt-right moves on a word"),
    ("abc def ghi\x1b\x1b[DX", "abc def Xghi", "ESC ESC left moves back a word"),
    ("abc def\x01\x1b[3;5~X", "X def", "ctrl-delete deletes the word ahead"),
    ("abc def\x01\x1b[3~X", "Xbc def", "delete still deletes one character"),
    ("abc def\x1bbX", "abc Xdef", "alt-b moves back a word"),
    ("abc def\x01\x1bfX", "abcX def", "alt-f moves on a word"),
]
for typed, want, what in KEYS:
    last, t = shell('PS1="> "\n', [typed])
    check(what, plain(last)[0] == "> " + want, repr(last))

print()
# Agent mode is for a script a program runs, with nobody at the terminal, so
# a terminal on standard input is swapped for /dev/null: a read gets the end
# of input at once rather than waiting for a person.
t = Term("--agent", "-c", 'read -r x; echo "read [$x] status $?"', settle=0,
         size=False)
t.wait(3.0)
check("in agent mode a read on a terminal ends at once instead of waiting",
      t.exited and "read [] status 1" in t.text, t.text)

# A trapped signal while the prompt waits (Gitea #448): it ran the trap
# and then ended the shell, or, with a line half typed, ran that line as
# if Enter had been pressed. The read is interrupted on purpose -- traps
# have no SA_RESTART so a `read` builtin can be broken out of -- and the
# editor took the interruption for end of input.
SIG = 'PS1="> "\ntrap "echo got-sig" USR1\n(sleep 0.6; kill -USR1 $$) &\n'
t = session(SIG, [1.4, "echo alive\n", "exit\n"])
check("a trap at an empty prompt runs and the shell stays", "got-sig" in t and "alive\r\n" in t)
t = session(SIG, ["echo par", 1.4, "tial\n", "exit\n"])
check("a trap mid-line runs and the line goes on being typed",
      "got-sig" in t and "partial\r\n" in t and "\npar\r\n" not in t)

# hibr -i -c on a terminal (Gitea #447): job control was never set up on
# that path, so every pipeline's child took the terminal from a background
# group and was stopped.
t = Term("-i", "-c", "ls /etc/hostname | cat; echo end-ok", rows=24, cols=80,
         env={"HIBR_RC": "/dev/null", "TERM": "xterm"}, settle=0.6)
t.close()
check("hibr -i -c runs a pipeline on a terminal",
      "/etc/hostname" in t.text and "end-ok" in t.text and "Stopped" not in t.text)

# An interactive shell's own ignore of QUIT was taken for the parent's,
# so trap refused to trap it (Gitea #446).
t = Term("-i", "-c", 'trap "echo q" QUIT; trap -p QUIT', rows=24, cols=80,
         env={"HIBR_RC": "/dev/null", "TERM": "xterm"}, settle=0.6)
t.close()
check("an interactive shell can trap QUIT", "echo q" in t.text)

# Interrupt at the prompt with an INT trap (review of #448): the trap
# runs and, as in bash, the half-typed line is dropped rather than drawn
# again for Enter to run.
t = session('PS1="> "\ntrap "echo INTTRAP" INT\n',
            ["echo partial", "\x03", 0.6, "\n", "echo alive\n", "exit\n"])
check("interrupt with an INT trap runs it and drops the line",
      "INTTRAP" in t and "alive\r\n" in t and "\r\npartial\r\n" not in t, repr(t))

# wait on a stopped job (review of #446): bash keeps the job and answers
# 128 and the stop signal; dropping it orphaned the process.
expect(r"^wait: job 1 is stopped")
t = session('PS1="> "\n', ["sleep 30 &\n", 0.3, "kill -STOP %1\n", 0.4,
            "wait %1; echo rc=$?\n", 0.3, "jobs\n", 0.3,
            "kill -9 %1; echo killed=$?\n", 0.3, "exit\n"])
check("wait on a stopped job keeps it and answers 147",
      "rc=147" in t and "Stopped" in t and "killed=0" in t, repr(t))

# A hangup (Gitea #468): the terminal closing used to kill the shell before
# it saved its history or told its jobs. History is written readable by its
# owner alone, a background job is sent HUP, and the shell ends by the signal.
import signal as _sig, tempfile as _tf, stat as _st, subprocess as _sp
hhome = _tf.mkdtemp()
t = Term("-i", rows=24, cols=80, env={"HIBR_RC": "/dev/null", "TERM": "xterm",
                                       "HOME": hhome}, settle=0.6)
t.keys([b"echo hangup-marker\n", 0.3, b"sleep 3.23 &\n", 0.4])
os.kill(t.pid, _sig.SIGHUP)
time.sleep(0.8)
hf = os.path.join(hhome, ".hibr_history")
check("a hangup saves history, readable by its owner alone",
      os.path.exists(hf) and "hangup-marker" in open(hf).read() and
      (os.stat(hf).st_mode & 0o777) == 0o600,
      repr(os.path.exists(hf) and oct(os.stat(hf).st_mode)))
check("a hangup is passed to background jobs",
      "sleep 3.23" not in _sp.run(["ps", "-eo", "args"], capture_output=True,
                                  text=True).stdout.splitlines())
t.close()

# The line editor's audit (Gitea #469). An OSC in the prompt -- a window
# title -- was counted as columns up to its first letter, so the cursor sat
# far right of the text.
last, t = shell('PS1=$\'\\e]0;title here\\a$ \'\n', ["abc"])
check("an OSC in the prompt takes no columns", plain(last)[1] == 5, repr(last))
# After ; | && a word is a command again, and COMPLETE is asked by the
# command the word belongs to, not by the line's first word.
d = tempfile.mkdtemp()
last, t = shell('PS1="> "\n', ["echo a; ech\t"], cwd=d)
check("a command completes after ;", "echo a; echo " in plain(last)[0], repr(last))
rc = ('PS1="> "\nfn gc(cmd, w) { ret checkout status; }\n'
      'declare -A COMPLETE; COMPLETE[git]=gc\n')
last, t = shell(rc, ["ls; git che\t"], cwd=d)
check("COMPLETE is asked by the command being typed",
      "ls; git checkout " in plain(last)[0], repr(last))
# The completion hook keeps the shell's state: $? and an exit inside it.
rc = ('PS1="> "\nfn bad(cmd, w) { false; exit 3; }\n'
      'declare -A COMPLETE; COMPLETE[zz]=bad\n')
t = session(rc, ["true\n", "zz q\t", 0.3, "\x15echo status=$? alive\n", "exit\n"])
check("a completion hook keeps $? and cannot end the shell",
      "status=0 alive" in t, repr(t[-300:]))
shutil.rmtree(d, ignore_errors=True)

report(62)
