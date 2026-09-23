#!/usr/bin/env python3
"""One pseudo terminal harness, and one model of what hibr drew on it.

Everything full-screen has to be tested through a pty, because the console,
the line editor and the cat all behave differently when their output is not a
terminal -- which is the point of them. That used to mean every suite carried
its own copy of "fork a pty, send some keys, reassemble the screen", six times
over with slightly different timings, and a fix to one of them fixed one of
them. This is the only copy.

    from screen import Term, check, report, press, wheel

    t = Term("-c", "mod load %s; most file" % MOD)
    t.send(b" ")
    t.quit()
    check("it paged", "200/200" in t.screen().text())
    report(1)

Run it directly to look at something rather than assert on it:

    python3 tests/screen.py examples/desktop-session.hibr
    python3 tests/screen.py -c 'mod load build/mods/mon.so; mon'
"""
import fcntl, os, pty, select, signal, struct, sys, termios, time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HIBR = os.environ.get("HIBR") or os.path.join(ROOT, "build/hibr")
ROWS, COLS = 24, 80

# Somewhere of each run's own for anything a program under test saves --
# the desktop's settings above all.  Without it a test that changes the
# theme changes the owner's theme, and the next test starts from it.  A
# test that wants saved settings to carry over passes the same env itself.
import atexit, shutil, tempfile
HOME = tempfile.mkdtemp(prefix="hibr-screen-")
atexit.register(shutil.rmtree, HOME, True)
FAIL = []


def tree(p):
    """A path in the source tree, whatever directory the suite was run from.

    Not called `path`: it would be shadowed inside any function that takes a
    parameter of that name, which is most of them, and the failure reads as
    "'str' object is not callable" several calls away from the cause.
    """
    return p if os.path.isabs(p) else os.path.join(ROOT, p)


class Screen:
    """Enough of a terminal to answer "what is at row r, column c".

    The console sends only the cells that changed, so grepping the byte
    stream finds fragments -- "78-200/200" where the display reads
    "178-200/200". Applying the moves and the text gives the real thing.

    It understands absolute cursor moves, relative moves to the right, and
    the erase that starts a full-screen program; everything else is skipped,
    because nothing hibr emits needs more than that.
    """

    def __init__(self, rows=ROWS, cols=COLS):
        self.rows, self.cols = rows, cols
        self.clear()

    def clear(self):
        self.g = [[" "] * self.cols for _ in range(self.rows)]
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
                    self.r = int(a[0]) - 1 if a and a[0].isdigit() else 0
                    self.c = (int(a[1]) - 1
                              if len(a) > 1 and a[1].isdigit() else 0)
                elif fin == "C":
                    self.c += int(seq) if seq.isdigit() else 1
                elif fin == "J" and seq in ("2", "3"):
                    self.clear()
                i = j + 1
                continue
            if ch == "\x1b":
                i += 2
                continue
            if ch in "\r\n":
                i += 1
                continue
            if 0 <= self.r < self.rows and 0 <= self.c < self.cols:
                self.g[self.r][self.c] = ch
            self.c += 2 if ord(ch) > 0x2E80 else 1
            i += 1
        return self

    def row(self, r):
        return "".join(self.g[r]).rstrip()

    def at(self, r, c):
        return self.g[r][c]

    def find(self, s):
        """Where a string starts, as (row, col), or None."""
        for r in range(self.rows):
            c = "".join(self.g[r]).find(s)
            if c >= 0:
                return r, c
        return None

    def text(self):
        return "\n".join(self.row(r) for r in range(self.rows))

    def dump(self):
        return "\n".join("%2d|%s" % (r, self.row(r)) for r in range(self.rows))


class Term:
    """A pty with hibr running on it."""

    def __init__(self, *argv, rows=ROWS, cols=COLS, env=None, settle=0.4,
                 size=True):
        self.rows, self.cols = rows, cols
        self.out = b""
        self.status = None
        self.exited = False
        self.pid, self.fd = pty.fork()
        if self.pid == 0:
            os.environ["TERM"] = "xterm-256color"
            own = os.path.join(HOME, str(os.getpid()))
            os.environ["XDG_CONFIG_HOME"] = os.path.join(own, "config")
            os.environ["XDG_STATE_HOME"] = os.path.join(own, "state")
            for k, v in (env or {}).items():
                os.environ[k] = v
            os.execv(HIBR, ["hibr"] + [str(a) for a in argv])
        if size:
            self.resize(rows, cols)
        if settle:
            time.sleep(settle)
            self.collect(0.4)

    def resize(self, rows, cols):
        self.rows, self.cols = rows, cols
        fcntl.ioctl(self.fd, termios.TIOCSWINSZ,
                    struct.pack("HHHH", rows, cols, 0, 0))

    def collect(self, t=0.3):
        end = time.time() + t
        while time.time() < end:
            if not select.select([self.fd], [], [], 0.05)[0]:
                continue
            try:
                d = os.read(self.fd, 65536)
            except OSError:
                return self
            if not d:
                return self
            self.out += d
        return self

    def send(self, data, settle=0.25, collect=0.2):
        if isinstance(data, str):
            data = data.encode()
        os.write(self.fd, data)
        if settle:
            time.sleep(settle)
        if collect:
            self.collect(collect)
        return self

    def keys(self, ks, settle=0.25, collect=0.2):
        for k in ks:
            self.send(k, settle, collect)
        return self

    def signal(self, sig=signal.SIGINT):
        os.kill(self.pid, sig)
        return self

    def quit(self, key=b"q", wait=1.2):
        """Ask it to stop, then wait for it to."""
        if key is not None:
            try:
                os.write(self.fd, key)
            except OSError:
                pass
        self.collect(wait)
        return self.close()

    def close(self):
        """Tear down, recording whether it had already exited on its own."""
        if self.status is None:
            try:
                pid, st = os.waitpid(self.pid, os.WNOHANG)
                self.exited = pid == self.pid
                if self.exited:
                    self.status = st
            except ChildProcessError:
                self.exited = True
                self.status = 0
        if not self.exited:
            try:
                os.kill(self.pid, signal.SIGKILL)
                self.status = os.waitpid(self.pid, 0)[1]
            except (ProcessLookupError, ChildProcessError):
                self.status = 0
        try:
            os.close(self.fd)
        except OSError:
            pass
        return self

    def wait(self, timeout=2.0):
        """Collect until it exits or the time is up."""
        end = time.time() + timeout
        while time.time() < end:
            self.collect(0.1)
            try:
                pid, st = os.waitpid(self.pid, os.WNOHANG)
            except ChildProcessError:
                self.exited, self.status = True, 0
                break
            if pid == self.pid:
                self.exited, self.status = True, st
                break
        return self.close()

    @property
    def raw(self):
        return self.out

    @property
    def text(self):
        return self.out.decode("utf8", "replace")

    def screen(self, rows=None, cols=None):
        return Screen(rows or self.rows, cols or self.cols).feed(self.text)


def load(*mods):
    """The `mod load` prefix for a -c command, from module names or paths."""
    out = []
    for m in mods:
        out.append("mod load %s"
                   % tree(m if "/" in m else "build/mods/%s.so" % m))
    return "; ".join(out) + "; "


def press(row, col, button=0):
    return b"\x1b[<%d;%d;%dM" % (button, col + 1, row + 1)


def release(row, col, button=0):
    return b"\x1b[<%d;%d;%dm" % (button, col + 1, row + 1)


def drag(row, col, button=0):
    return b"\x1b[<%d;%d;%dM" % (32 + button, col + 1, row + 1)


def wheel(row, col, up=True):
    return b"\x1b[<%d;%d;%dM" % (64 if up else 65, col + 1, row + 1)


def check(name, ok, show=None):
    print(("ok   " if ok else "FAIL ") + name)
    if not ok:
        FAIL.append(name)
        if show is not None and os.environ.get("V"):
            print(show.dump() if hasattr(show, "dump") else show)
    return ok


def report(total=None):
    n = total if total is not None else 0
    print()
    print("%d passed, %d failed" % ((n or len(FAIL)) - len(FAIL), len(FAIL)))
    sys.exit(1 if FAIL else 0)


if __name__ == "__main__":
    a = sys.argv[1:]
    if not a:
        print(__doc__)
        sys.exit(0)
    t = Term(*a, settle=0.9)
    t.collect(0.6)
    t.quit()
    print(t.screen().dump())
