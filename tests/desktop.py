#!/usr/bin/env python3
"""Drive the window manager through a pty and read the screen it draws.

examples/desktop.hibr is a hibr script, so none of this can be reached from
a .t file: it needs a terminal for the console to open and a mouse to click
with.  Run it directly:  python3 tests/desktop.py [path-to-hibr]

The Screen class below is just enough of a terminal to answer "what is at row
r, column c" -- absolute cursor moves and printable text.  That is all the
console ever emits.
"""
import fcntl, os, pty, re, select, struct, sys, termios, time

HIBR = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else "./build/hibr")
MOD = os.path.abspath("./build/mods/console.so")
WM = os.path.abspath("./examples/desktop.hibr")
ROWS, COLS = 24, 80
FAIL = []

CSI = re.compile(r"\x1b\[([0-9;?]*)([A-Za-z])")


class Screen:
    """A grid that replays what the console emitted."""

    def __init__(self, rows=ROWS, cols=COLS):
        self.rows, self.cols = rows, cols
        self.g = [[" "] * cols for _ in range(rows)]
        self.r = self.c = 0

    def feed(self, text):
        i = 0
        while i < len(text):
            m = CSI.match(text, i)
            if m:
                a, verb = m.group(1), m.group(2)
                if verb == "H":
                    p = [int(x or 1) for x in a.split(";")] or [1, 1]
                    self.r = (p[0] if p else 1) - 1
                    self.c = (p[1] if len(p) > 1 else 1) - 1
                i = m.end()
                continue
            ch = text[i]
            if ch == "\x1b":
                i += 2
                continue
            if ch in "\r\n":
                i += 1
                continue
            if 0 <= self.r < self.rows and 0 <= self.c < self.cols:
                self.g[self.r][self.c] = ch
            self.c += 1 + (1 if ord(ch) > 0x2E80 else 0)
            i += 1

    def row(self, r):
        return "".join(self.g[r]).rstrip()

    def find(self, s):
        """Where a string starts, as (row, col), or None."""
        for r in range(self.rows):
            c = "".join(self.g[r]).find(s)
            if c >= 0:
                return r, c
        return None

    def dump(self):
        return "\n".join("%2d|%s" % (r, self.row(r)) for r in range(self.rows))


def press(row, col, btn=0):
    return b"\x1b[<%d;%d;%dM" % (btn, col + 1, row + 1)


def drag(row, col):
    return b"\x1b[<32;%d;%dM" % (col + 1, row + 1)


def release(row, col):
    return b"\x1b[<0;%d;%dm" % (col + 1, row + 1)


def run(session, feed=(), wait=1.2):
    """Run a session on top of the window manager and return the last screen."""
    path = "/tmp/hibr-desktop-%d.hibr" % os.getpid()
    open(path, "w").write(
        "mod load %s\n. %s\ndt_open\n%s\ndt_run\ndt_close\n" % (MOD, WM, session))
    pid, fd = pty.fork()
    if pid == 0:
        os.environ["TERM"] = "xterm-256color"
        os.environ["DT_TICK"] = "60"
        os.execv(HIBR, ["hibr", path])
    fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack("HHHH", ROWS, COLS, 0, 0))
    out = b""
    time.sleep(0.5)
    for k in feed:
        os.write(fd, k)
        deadline = time.time() + 0.25
        while time.time() < deadline:
            if select.select([fd], [], [], 0.05)[0]:
                try:
                    out += os.read(fd, 65536)
                except OSError:
                    break
    os.write(fd, b"q")
    end = time.time() + wait
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
        os.waitpid(pid, 0)
    except ChildProcessError:
        pass
    os.unlink(path)
    text = out.decode("utf8", "replace")
    sc = Screen()
    sc.feed(text)
    return sc, out


def check(name, ok, sc=None):
    print(("ok   " if ok else "FAIL ") + name)
    if not ok:
        FAIL.append(name)
        if sc and os.environ.get("V"):
            print(sc.dump())


ONE = 'dt_new "Hello" 8 30 6 10'

sc, raw = run(ONE)
check("a window has a top-left corner where it was put",
      sc.g[6][10] == "┌", sc)
check("and a bottom-right corner at its far end",
      sc.g[13][39] == "┘", sc)
check("its title is in the title bar", sc.find("┤ Hello ├") == (6, 12), sc)
check("the close button is at the right of the bar", sc.g[6][37] == "x", sc)
check("the minimise and zoom buttons sit beside it",
      sc.g[6][33] == "_" and sc.g[6][35] == "□", sc)
check("the wallpaper is drawn behind it", sc.g[12][2] == "·", sc)
check("the bar across the top counts the windows",
      "1 window(s)" in sc.row(0), sc)
check("the alternate screen is left on the way out", b"\x1b[?1049l" in raw)
check("and the mouse is turned off again",
      b"\x1b[?1002l" in raw or b"\x1b[?1000l" in raw)

sc, _ = run(ONE, [press(6, 20), drag(9, 24), release(9, 24)])
check("dragging the title bar moves the window",
      sc.find("┤ Hello ├") == (9, 16), sc)
check("the window is drawn whole at its new place",
      sc.g[9][14] == "┌" and sc.g[16][43] == "┘", sc)
check("and nothing of it is left behind",
      sc.g[6][10] == "·" and sc.g[13][39] == "·", sc)

sc, _ = run(ONE, [press(6, 20), drag(0, 0), release(0, 0)])
check("a window cannot be dragged up over the bar",
      sc.find("┤ Hello ├") == (1, 2), sc)

sc, _ = run(ONE, [press(6, 20), drag(23, 79), release(23, 79)])
check("nor off the bottom right", sc.g[16][50] == "┌", sc)

sc, _ = run(ONE, [press(6, 37)])
check("clicking the close button closes the window",
      sc.find("Hello") is None, sc)
check("and the count on the bar goes down", "0 window(s)" in sc.row(0), sc)

sc, _ = run(ONE, [press(6, 33)])
check("minimising takes the window off the screen",
      sc.find("Hello") is None and "0 window(s)" in sc.row(0), sc)

sc, _ = run(ONE, [press(6, 35)])
check("zooming fills the screen below the bar",
      sc.g[1][0] == "┌" and sc.g[23][79] == "┘", sc)
sc, _ = run(ONE, [press(6, 35), press(1, 35)])
check("and zooming again puts it back where it was",
      sc.g[6][10] == "┌" and sc.g[13][39] == "┘", sc)

TWO = ('dt_new "Under" 8 30 6 10\n'
       'dt_new "Over" 8 30 9 20\n')

sc, _ = run(TWO)
check("two windows overlap, the newer one on top",
      sc.find("┤ Over ├") == (9, 22) and sc.g[9][25] == "─", sc)
check("the one below is clipped by it", sc.g[9][10] == "│" and
      sc.g[9][19] == " " and sc.g[10][20] == " ", sc)
check("the bar counts both", "2 window(s)" in sc.row(0), sc)

sc, _ = run(TWO, [press(6, 12)])
check("clicking the lower window raises it",
      sc.g[9][22] == "│" and sc.g[13][25] == "─", sc)
check("and the title bars show which one has focus",
      sc.find("┤ Under ├") == (6, 12), sc)

sc, _ = run(TWO, [press(6, 12), press(6, 37)])
check("closing the raised window leaves the other",
      sc.find("Under") is None and sc.find("┤ Over ├") == (9, 22), sc)

APP = ('counter() {\n'
       '  case $1 in\n'
       '  draw) console put -p "w$2" 2 3 "count $CN" ;;\n'
       '  key) [ "$3" = plus ] && CN=$((CN+1)) && return 0; return 1 ;;\n'
       '  click) CN=$(($3 * 100 + $4)) ;;\n'
       '  open) CN=0 ;;\n'
       '  esac\n'
       '}\n'
       'dt_new "App" 8 30 6 10 counter\n')

sc, _ = run(APP)
check("an app draws inside its own window", sc.find("count 0") == (8, 14), sc)

sc, _ = run(APP, [b"\x1b[15~"])
check("a key the app refuses does not reach it", sc.find("count 0"), sc)

sc, _ = run(APP, [press(9, 16)])
check("a click in the body reaches the app in its own coordinates",
      sc.find("count 205") == (8, 14), sc)

sc, _ = run(APP, [press(6, 37)])
check("closing an app's window closes the app",
      sc.find("count") is None, sc)

print()
n = 29
print("%d passed, %d failed" % (n - len(FAIL), len(FAIL)))
sys.exit(1 if FAIL else 0)
