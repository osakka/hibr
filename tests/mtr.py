#!/usr/bin/env python3
"""Drive the live traceroute through a pseudo terminal.

Everything here traces to the loopback address, which is one hop, always
reachable and always fast -- a test that depends on the internet being up and
on which routers answer is a test that fails for reasons that are not about
this code.
Run it directly:  python3 tests/mtr.py [path-to-hibr]
"""
import fcntl, os, pty, select, struct, sys, termios, time

HIBR = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else "./build/hibr")
CONSOLE = os.path.abspath("./build/mods/console.so")
TRACE = os.path.abspath("./build/mods/trace.so")
FAIL = []
ROWS, COLS = 12, 100


def run(cmd, keys=(), settle=3.0):
    pid, fd = pty.fork()
    if pid == 0:
        os.environ["TERM"] = "xterm-256color"
        os.execv(HIBR, ["hibr", "-c", "mod load %s; mod load %s; %s"
                        % (CONSOLE, TRACE, cmd)])
    fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack("HHHH", ROWS, COLS, 0, 0))
    out = b""

    def grab(t):
        nonlocal out
        end = time.time() + t
        while time.time() < end:
            if select.select([fd], [], [], 0.2)[0]:
                try:
                    d = os.read(fd, 65536)
                except OSError:
                    return
                if not d:
                    return
                out += d

    time.sleep(settle)
    grab(0.6)
    for k in keys:
        os.write(fd, k)
        time.sleep(0.5)
        grab(0.4)
    os.write(fd, b"q")
    time.sleep(0.4)
    grab(0.4)
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

print()
print("%d passed, %d failed" % (11 - len(FAIL), len(FAIL)))
sys.exit(1 if FAIL else 0)
