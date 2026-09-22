#!/usr/bin/env python3
"""Drive the editor through a pseudo terminal.

Checks are mostly on the file that comes out, which is what an editor is for;
where a message matters the screen is reassembled from the escape stream,
because the display sends only the cells that changed.
Run it directly:  python3 tests/vi.py [path-to-hibr]
"""
import fcntl, os, pty, select, struct, sys, tempfile, termios, threading, time

HIBR = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else "./build/hibr")
CONSOLE = os.path.abspath("./build/mods/console.so")
VI = os.path.abspath("./build/mods/vi.so")
FAIL = []
D = tempfile.mkdtemp(prefix="hibr-vi-")
ROWS, COLS = 10, 60


def vi(path, keys, wait=0.6, step=0.25):
    """Edit a file, send keys, return everything the terminal received."""
    pid, fd = pty.fork()
    if pid == 0:
        os.environ["TERM"] = "xterm-256color"
        os.execv(HIBR, ["hibr", "-c", "mod load %s; mod load %s; vi %s"
                        % (CONSOLE, VI, path)])
    fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack("HHHH", ROWS, COLS, 0, 0))
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

    time.sleep(wait)
    grab(0.4)
    for k in keys:
        os.write(fd, k)
        time.sleep(step)
        grab(0.2)
    time.sleep(0.35)
    grab(0.35)
    try:
        os.close(fd)
    except OSError:
        pass
    try:
        os.waitpid(pid, 0)
    except ChildProcessError:
        pass
    return out.decode("utf8", "replace")


def screen(t):
    """Reassemble the display from the cells that were sent."""
    g = [[" "] * COLS for _ in range(ROWS)]
    r = c = 0
    t = t.split("\x1b[2J", 1)[-1]
    i = 0
    while i < len(t):
        ch = t[i]
        if ch == "\x1b" and i + 1 < len(t) and t[i + 1] == "[":
            j = i + 2
            while j < len(t) and not ("@" <= t[j] <= "~"):
                j += 1
            if j >= len(t):
                break
            seq, fin = t[i + 2:j], t[j]
            if fin == "H":
                a = seq.split(";")
                r = (int(a[0]) - 1) if a[0].isdigit() else 0
                c = (int(a[1]) - 1) if len(a) > 1 and a[1].isdigit() else 0
            elif fin == "C":
                c += int(seq) if seq.isdigit() else 1
            i = j + 1
            continue
        if ch not in "\r\n":
            if 0 <= r < ROWS and 0 <= c < COLS:
                g[r][c] = ch
            c += 1
        i += 1
    return "\n".join("".join(x).rstrip() for x in g)


def check(name, ok):
    print(("ok   " if ok else "FAIL ") + name)
    if not ok:
        FAIL.append(name)


def fresh(nm, text):
    p = os.path.join(D, nm)
    open(p, "w").write(text)
    return p


def read(p):
    return open(p).read()


ESC, CTRLR = b"\x1b", b"\x12"
W, Q = b":w\r", b":q\r"

p = fresh("a.txt", "alpha\nbeta\ngamma\n")
vi(p, [b"A", b" END", ESC, W, Q])
check("A appends to the line", read(p) == "alpha END\nbeta\ngamma\n")

p = fresh("b.txt", "alpha\nbeta\ngamma\n")
vi(p, [b"i", b"XY", ESC, W, Q])
check("i inserts before the cursor", read(p) == "XYalpha\nbeta\ngamma\n")

p = fresh("c.txt", "alpha\nbeta\ngamma\n")
vi(p, [b"j", b"dd", W, Q])
check("dd removes a line", read(p) == "alpha\ngamma\n")

p = fresh("d.txt", "one two three\n")
vi(p, [b"dw", W, Q])
check("dw removes a word", read(p) == "two three\n")

p = fresh("e.txt", "one two three\n")
vi(p, [b"d$", W, Q])
check("d$ removes to the end of the line", read(p) == "\n")

p = fresh("f.txt", "alpha\nbeta\n")
vi(p, [b"yy", b"p", W, Q])
check("yy then p copies a line", read(p) == "alpha\nalpha\nbeta\n")

p = fresh("g.txt", "alpha\n")
vi(p, [b"o", b"new", ESC, W, Q])
check("o opens a line below", read(p) == "alpha\nnew\n")

p = fresh("h.txt", "alpha\n")
vi(p, [b"O", b"new", ESC, W, Q])
check("O opens a line above", read(p) == "new\nalpha\n")

p = fresh("i.txt", "alpha\nbeta\n")
vi(p, [b"J", W, Q])
check("J joins two lines", read(p) == "alpha beta\n")

p = fresh("j.txt", "abc\n")
vi(p, [b"x", b"x", W, Q])
check("x removes characters", read(p) == "c\n")

p = fresh("k.txt", "alpha\nbeta\ngamma\n")
vi(p, [b"i", b"XY", ESC, b"u", W, Q])
check("u takes back a whole insert, not one letter",
      read(p) == "alpha\nbeta\ngamma\n")

p = fresh("l.txt", "alpha\nbeta\ngamma\n")
vi(p, [b"i", b"XY", ESC, b"u", CTRLR, W, Q])
check("ctrl-r puts it back", read(p) == "XYalpha\nbeta\ngamma\n")

p = fresh("m.txt", "one\ntwo\nthree\n")
vi(p, [b"dd", b"dd", b"u", b"u", W, Q])
check("undo goes back more than once", read(p) == "one\ntwo\nthree\n")

p = fresh("n.txt", "alpha\nbeta\ngamma\ndelta\n")
vi(p, [b"V", b"j", b"d", W, Q])
check("visual line covers the lines it moved over",
      read(p) == "gamma\ndelta\n")

p = fresh("o.txt", "abcdef\n")
vi(p, [b"v", b"l", b"l", b"d", W, Q])
check("visual covers the characters it moved over", read(p) == "def\n")

p = fresh("p.txt", "one\ntwo\nthree needle\nfour\n")
vi(p, [b"/needle\r", b"x", W, Q])
check("search moves to the match", read(p) == "one\ntwo\nthree eedle\nfour\n")

p = fresh("q.txt", "héllo\n")
vi(p, [b"x", W, Q])
check("a multi-byte character is one x", read(p) == "éllo\n")

p = fresh("r.txt", "héllo\n")
vi(p, [b"l", b"x", W, Q])
check("and l steps over it whole", read(p) == "hllo\n")

p = fresh("s.txt", "keep me\n")
t = vi(p, [b"x", Q])
check("q refuses to lose unsaved changes",
      read(p) == "keep me\n" and "unsaved changes" in screen(t))
p = fresh("t.txt", "keep me\n")
vi(p, [b"x", b":q!\r"])
check("q! leaves anyway", read(p) == "keep me\n")

p = fresh("u.txt", "original\n")
threading.Thread(target=lambda: (time.sleep(1.0),
                                 open(p, "w").write("THEIRS\n")),
                 daemon=True).start()
t = vi(p, [b"A", b" mine", ESC, W], wait=1.6)
check("w refuses when the file changed underneath",
      read(p) == "THEIRS\n" and "changed on disk" in screen(t))

p = fresh("v.txt", "original\n")
threading.Thread(target=lambda: (time.sleep(1.0),
                                 open(p, "w").write("THEIRS\n")),
                 daemon=True).start()
vi(p, [b"A", b" mine", ESC, b":w!\r", Q], wait=1.6)
check("w! overwrites anyway", read(p) == "original mine\n")

p = fresh("w.txt", "untouched\n")
vi(p, [b"A", b" ok", ESC, W, Q])
check("w writes when nothing moved", read(p) == "untouched ok\n")

p = fresh("x.txt", "".join("line %d\n" % i for i in range(1, 60)))
t = vi(p, [b"G"])
check("G reaches the last line", "line 59" in screen(t))
t = vi(p, [b"G", b"gg"])
check("gg comes back to the first", "line 1" in screen(t))

p = fresh("y.txt", "a\n")
t = vi(p, [b"i", b"x"])
check("insert mode says so", "-- INSERT --" in screen(t))
t = vi(p, [b"v"])
check("visual mode says so", "-- VISUAL --" in screen(t))

print()
print("%d passed, %d failed" % (26 - len(FAIL), len(FAIL)))
for f in os.listdir(D):
    os.unlink(os.path.join(D, f))
os.rmdir(D)
sys.exit(1 if FAIL else 0)
