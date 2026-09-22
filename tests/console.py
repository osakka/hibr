#!/usr/bin/env python3
"""Drive the console display through a pseudo terminal and read what it emits.

The .t files cannot reach any of this, for the same reason tests/editor.py
exists: full-screen drawing only happens when there is a terminal to draw on.
Run it directly:  python3 tests/console.py [path-to-hibr]
"""
import fcntl, os, pty, re, select, signal, struct, sys, termios, time

HIBR = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else "./build/hibr")
MOD = os.path.abspath("./build/mods/console.so")
FAIL = []


def run(script, feed=(), after=None, wait=2.5, rows=24, cols=80):
    """Run a script under a pty, optionally feeding keys, and return its output."""
    path = "/tmp/hibr-screen-%d.hibr" % os.getpid()
    open(path, "w").write("mod load %s\n%s" % (MOD, script))
    pid, fd = pty.fork()
    if pid == 0:
        os.environ["TERM"] = "xterm-256color"
        os.execv(HIBR, ["hibr", path])
    fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack("HHHH", rows, cols, 0, 0))
    time.sleep(0.4)
    for k in feed:
        os.write(fd, k)
        time.sleep(0.12)
    if after:
        after(pid, fd)
    out, end = b"", time.time() + wait
    while time.time() < end:
        if select.select([fd], [], [], 0.1)[0]:
            try:
                d = os.read(fd, 65536)
            except OSError:
                break
            if not d:
                break
            out += d
    try:
        os.close(fd)
    except OSError:
        pass
    try:
        st = os.waitpid(pid, 0)[1]
    except ChildProcessError:
        st = 0
    os.unlink(path)
    return out, st


def check(name, ok):
    print(("ok   " if ok else "FAIL ") + name)
    if not ok:
        FAIL.append(name)


def counts(out):
    """The numbers the R line reports, one per flush the script measured."""
    m = re.search(rb"R ([-0-9 ]+)", out)
    return [int(x) for x in m.group(1).split()] if m else []


def drawn(out):
    """Everything emitted after the opening full paint."""
    i = out.rfind(b"\x1b[24;1H")
    return out[i:] if i >= 0 else out


o, _ = run('console open\nconsole flush\n'
           'console put 2 4 "hello screen"\nconsole flush\nconsole close\n')
check("the alternate screen is entered and left",
      b"\x1b[?1049h" in o and b"\x1b[?1049l" in o)
check("the cursor is hidden and given back",
      b"\x1b[?25l" in o and b"\x1b[?25h" in o)
check("text reaches the terminal in one piece", b"hello screen" in o)
check("it lands where it was put", b"\x1b[3;5Hhello screen" in drawn(o))

o, _ = run('console open\na := console flush\n'
           'console put 5 10 "0000"\nb := console flush\n'
           'console put 5 10 "0001"\nc := console flush\n'
           'd := console flush\nconsole close\necho "R $a $b $c $d"\n')
n = counts(o)
check("the first flush paints the whole screen", n and n[0] > 1900)
check("only the changed run is sent afterwards", len(n) > 1 and n[1] < 20)
check("one changed cell costs about one escape", len(n) > 2 and n[2] < 12)
check("an unchanged screen sends nothing at all", len(n) > 3 and n[3] == 0)

o, _ = run('console open\nconsole flush\n'
           'console put 1 0 "漢字ab"\nb := console flush\n'
           'console put 1 1 "X"\nc := console flush\nconsole close\necho "R $b $c"\n')
d = drawn(o)
check("a wide glyph is written whole", "漢字ab".encode() in d)
check("overwriting half of one clears the other half",
      re.search(rb"\x1b\[2;1H X", d) is not None)

o, _ = run('console open\nconsole flush\nconsole put 3 0 "éf"\n'
           'console flush\nconsole close\n')
d = drawn(o)
check("a combining mark rides with its base rather than taking a cell",
      "éf".encode() in d and b"\x1b[4;1H" in d)

o, _ = run('console open\nconsole flush\nconsole pane box 5 10 3 6\n'
           'console put -p box 0 0 "abcdefghij"\n'
           'console put -p box 1 4 "zzzz"\n'
           'console put -p box 9 0 "offpane"\n'
           'console flush\nconsole close\n')
d = drawn(o)
check("a pane clips what is too wide for it", b"\x1b[6;11Habcdef" in d)
check("it clips from the pane's own origin", b"\x1b[7;15Hzz" in d)
check("a row outside the pane draws nothing", b"offpane" not in d)

o, _ = run('console open\nconsole flush\nconsole pen red blue bold\n'
           'console put 1 1 "styled"\nconsole flush\nconsole close\n')
check("the pen becomes one SGR before the run",
      b"\x1b[0;1;38;5;1;48;5;4mstyled" in drawn(o))

o, _ = run('console open\nconsole flush\nconsole pen "#ff8800"\n'
           'console put 1 1 "rgb"\nconsole flush\nconsole close\n')
check("a hex colour becomes a true-colour SGR",
      b"38;2;255;136;0" in drawn(o))

SEQ = [b"\x1b[A", b"\x1b[1;5C", b"\x1b[1;2B", b"\x01", b"\x1b[15~", b"\x1b[H",
       b"\x1b[5~", b"\x1b[Z", b"\x1b[<0;12;3M", b"\x1bx", b"\xc3\xa9", b"\r",
       b"\x1b[200~pasted\x1b[201~", b"\x1b"]
WANT = ["up", "ctrl-right", "shift-down", "ctrl-a", "f5", "home", "pageup",
        "shift-tab", "mouse left 3 12", "alt-x", "é", "enter",
        "paste pasted", "escape"]
o, _ = run('console open\ni=0\nwhile [ $i -lt %d ]; do\n'
           '  k := console key 2000\n  if [ -z "$k" ]; then break; fi\n'
           '  echo "KEY[$k]"\n  i=$((i+1))\ndone\nconsole close\n' % len(SEQ),
           feed=SEQ, wait=3)
got = [m.decode("utf8", "replace") for m in re.findall(rb"KEY\[([^\]]*)\]", o)]
for i, w in enumerate(WANT):
    check("key %-16s decodes" % repr(w), i < len(got) and got[i] == w)

o, st = run('console open\nconsole flush\nconsole key 5000\n',
            after=lambda pid, fd: os.kill(pid, signal.SIGINT), wait=1.5)
check("an interrupt still leaves the alternate screen", b"\x1b[?1049l" in o)
check("an interrupt still gives the cursor back", b"\x1b[?25h" in o)
check("and the signal is not swallowed",
      os.WIFSIGNALED(st) and os.WTERMSIG(st) == signal.SIGINT)


def bigger(pid, fd):
    fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack("HHHH", 40, 100, 0, 0))


o, _ = run('console open\nconsole flush\nconsole key 3000\nconsole close\n'
           'if console resized; then s := console size; echo "R $s"; fi\n',
           after=bigger, wait=3)
check("a resize interrupts the wait and is reported", counts(o) == [40, 100])

print()
print("%d passed, %d failed" % (len(WANT) + 21 - len(FAIL), len(FAIL)))
sys.exit(1 if FAIL else 0)
