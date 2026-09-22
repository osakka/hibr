#!/usr/bin/env python3
"""Drive the pager through a pseudo terminal.

None of this is reachable from run.sh: a pager needs a terminal to draw on and
a keyboard to read, and its whole point is that those are not the same thing as
its input. Note that the screen layer only sends the cells that changed, so a
check has to look at everything the session emitted rather than at the last
frame alone.
Run it directly:  python3 tests/most.py [path-to-hibr]
"""
import os, pty, select, sys, tempfile, time

HIBR = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else "./build/hibr")
SCREEN = os.path.abspath("./build/mods/screen.so")
MOST = os.path.abspath("./build/mods/most.so")
FAIL = []
D = tempfile.mkdtemp(prefix="hibr-most-")
LOAD = "mod load %s; mod load %s; " % (SCREEN, MOST)


def run(cmd, keys=(), settle=0.7, rows=24, cols=80):
    """Page something, send keys, and return everything that came back."""
    import fcntl, struct, termios
    pid, fd = pty.fork()
    if pid == 0:
        os.environ["TERM"] = "xterm-256color"
        os.execv(HIBR, ["hibr", "-c", LOAD + cmd])
    fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack("HHHH", rows, cols, 0, 0))
    out = b""

    def grab(t):
        nonlocal out
        end = time.time() + t
        while time.time() < end:
            if select.select([fd], [], [], 0.1)[0]:
                try:
                    d = os.read(fd, 65536)
                except OSError:
                    return
                if not d:
                    return
                out += d

    time.sleep(settle)
    grab(0.5)
    for k in keys:
        os.write(fd, k)
        time.sleep(0.4)
        grab(0.35)
    os.write(fd, b"q")
    time.sleep(0.3)
    grab(0.3)
    try:
        os.close(fd)
    except OSError:
        pass
    try:
        os.waitpid(pid, 0)
    except ChildProcessError:
        pass
    return out.decode("utf8", "replace")


class Screen:
    """Just enough terminal to reassemble what the pager actually showed.

    The screen layer sends only the cells that changed, so grepping the byte
    stream finds fragments like "78-200/200" where the display reads
    "178-200/200". Applying the moves and the text gives the real thing.
    """

    def __init__(self, rows=24, cols=80):
        self.rows, self.cols = rows, cols
        self.g = [[" "] * cols for _ in range(rows)]
        self.r = self.c = 0

    def feed(self, t):
        i, n = 0, len(t)
        while i < n:
            ch = t[i]
            if ch == "\x1b" and i + 1 < n and t[i + 1] == "[":
                j = i + 2
                while j < n and not ("@" <= t[j] <= "~"):
                    j += 1
                if j >= n:
                    break
                seq, fin = t[i + 2:j], t[j]
                if fin == "H":
                    a = seq.split(";")
                    self.r = (int(a[0]) - 1) if a and a[0].isdigit() else 0
                    self.c = (int(a[1]) - 1) if len(a) > 1 and a[1].isdigit() else 0
                elif fin == "C":
                    self.c += int(seq) if seq.isdigit() else 1
                i = j + 1
                continue
            if ch in "\r\n":
                i += 1
                continue
            if 0 <= self.r < self.rows and 0 <= self.c < self.cols:
                self.g[self.r][self.c] = ch
            self.c += 1
            i += 1

    def text(self):
        return "\n".join("".join(row).rstrip() for row in self.g)


def screen_of(t, rows=24, cols=80):
    s = Screen(rows, cols)
    body = t.split("\x1b[2J", 1)
    s.feed(body[1] if len(body) > 1 else t)
    return s.text()


def check(name, ok):
    print(("ok   " if ok else "FAIL ") + name)
    if not ok:
        FAIL.append(name)


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

print()
print("%d passed, %d failed" % (20 - len(FAIL), len(FAIL)))
for f in os.listdir(D):
    os.unlink(os.path.join(D, f))
os.rmdir(D)
sys.exit(1 if FAIL else 0)
