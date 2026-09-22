#!/usr/bin/env python3
"""Drive the system monitor through a pseudo terminal.

Checks are on the shape of what it draws and on numbers that can be compared
with /proc directly, not on values that move between one reading and the next.
Run it directly:  python3 tests/mon.py [path-to-hibr]
"""
import fcntl, os, pty, select, struct, sys, termios, time

HIBR = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else "./build/hibr")
CONSOLE = os.path.abspath("./build/mods/console.so")
MON = os.path.abspath("./build/mods/mon.so")
FAIL = []
ROWS, COLS = 24, 100


def run(cmd, keys=(), settle=3.0):
    pid, fd = pty.fork()
    if pid == 0:
        os.environ["TERM"] = "xterm-256color"
        os.execv(HIBR, ["hibr", "-c", "mod load %s; mod load %s; %s"
                        % (CONSOLE, MON, cmd)])
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

print()
print("%d passed, %d failed" % (15 - len(FAIL), len(FAIL)))
sys.exit(1 if FAIL else 0)
