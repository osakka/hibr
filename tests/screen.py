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

    python3 tests/screen.py examples/desktop/session.hibr
    python3 tests/screen.py -c 'mod load build/mods/mon.so; mon'
"""
import fcntl, os, pty, re, select, signal, struct, sys, termios, time

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


# Where the suites load modules from: this tree's build, or another build
# of the same sources -- tests/asan.py points it at the sanitizer build.
MODS = os.environ.get("HIBR_TESTMODS")


def tree(p):
    """A path in the source tree, whatever directory the suite was run from.

    Not called `path`: it would be shadowed inside any function that takes a
    parameter of that name, which is most of them, and the failure reads as
    "'str' object is not callable" several calls away from the cause.
    """
    if MODS and (p == "build/mods" or p.startswith("build/mods/")):
        p = MODS + p[len("build/mods"):]
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
        self.p = [[""] * self.cols for _ in range(self.rows)]
        self.r = self.c = 0
        self.pen = ""

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
                elif fin == "m":
                    self.pen = seq
                i = j + 1
                continue
            if ch == "\x1b" and i + 1 < n and t[i + 1] == "]":
                j = i + 2
                while j < n and t[j] != "\x07" and t[j:j + 2] != "\x1b\\":
                    j += 1
                i = j + (2 if t[j:j + 2] == "\x1b\\" else 1)
                continue
            if ch == "\x1b":
                i += 2
                continue
            if ch in "\r\n":
                i += 1
                continue
            wide = ord(ch) > 0x2E80
            if 0 <= self.r < self.rows and 0 <= self.c < self.cols:
                self.g[self.r][self.c] = ch
                self.p[self.r][self.c] = self.pen
                # A wide character covers the next cell too, as it does on
                # a real terminal -- whatever was there is gone, and a
                # damage-based renderer never resends it.
                if wide and self.c + 1 < self.cols:
                    self.g[self.r][self.c + 1] = " "
            self.c += 2 if wide else 1
            i += 1
        return self

    def row(self, r):
        return "".join(self.g[r]).rstrip()

    def style(self, r, c):
        """The pen a cell was drawn with: fg and bg as #rrggbb (or None for
        the terminal's own), and whether it was bold. The console sends the
        whole pen on every change, starting from a reset, so the last one
        seen is the whole truth."""
        a = self.p[r][c].split(";")
        out = {"fg": None, "bg": None, "bold": False}
        i = 0
        while i < len(a):
            if a[i] == "1":
                out["bold"] = True
            elif a[i] in ("38", "48") and i + 4 < len(a) and a[i + 1] == "2":
                out["fg" if a[i] == "38" else "bg"] = "#%02x%02x%02x" % tuple(
                    int(x) for x in a[i + 2:i + 5])
                i += 4
            i += 1
        return out

    def at(self, r, c):
        return self.g[r][c]

    def find(self, s):
        """Where a string starts, as (row, col), or None."""
        for r in range(self.rows):
            c = "".join(self.g[r]).find(s)
            if c >= 0:
                return r, c
        return None

    def find_from(self, s, minrow):
        """Where a string starts, at or after minrow -- for a string that
        can also appear earlier, such as a display's own name in a
        dropdown's shown value as well as in the rectangle beneath it."""
        for r in range(minrow, self.rows):
            c = "".join(self.g[r]).find(s)
            if c >= 0:
                return r, c
        return None

    def text(self):
        return "\n".join(self.row(r) for r in range(self.rows))

    def dump(self):
        return "\n".join("%2d|%s" % (r, self.row(r)) for r in range(self.rows))


# `need` loads a module from the module path, which ends at the installed
# copy -- so a suite could be testing yesterday's modules, or none at all
# once the ABI moves and the installed ones are refused. Everything a suite
# runs looks in this tree's own build first.
os.environ["HIBR_MODPATH"] = tree("build/mods") + (
    ":" + os.environ["HIBR_MODPATH"] if os.environ.get("HIBR_MODPATH") else "")

# What the desktop prints, when HIBR_TESTIDLE is set, each time a frame is on
# screen and it is about to wait for input -- an OSC a terminal ignores,
# carrying how many bytes of input it has read by then.
IDLE = re.compile(rb"\x1b\]7777;idle;(\d+)(?:;(\d+))?\x07")

# An error the shell printed: "hibr: " and a message, then a newline. The
# console draws by moving the cursor and never sends one, so a line ending
# in a newline is never something drawn on screen. Every session is scanned
# as it closes, and report() fails a suite in which any printed one -- the
# too-many-arguments or command-not-found that used to scroll past unseen.
ERROR = re.compile(rb"(?<![\w./-])hibr: ([^\r\n\x1b]*)\r?\n")
ERRS = []
TERMS = [0]
EXPECTED = []


def expect(pattern):
    """An error a test provokes on purpose -- a missing file, a bad option --
    which is therefore not one to fail the suite over. Named by what it says,
    not by which session printed it, so an unexpected error beside it still
    counts."""
    EXPECTED.append(re.compile(pattern))


class Term:
    """A pty with hibr running on it.

    A desktop tells the harness when it is idle (IDLE, above), and a Term
    that has seen it once waits for the next one after each key instead of
    sleeping for a fixed time: a key costs what drawing it costs. A key
    sent with an explicit settle still sleeps that long, for a test waiting
    on something outside the frame -- a terminal's child answering, a
    timer. Anything that never says it is idle keeps the fixed timings."""

    def __init__(self, *argv, rows=ROWS, cols=COLS, env=None, settle=0.4,
                 size=True):
        self.rows, self.cols = rows, cols
        self.out = b""
        self.status = None
        self.exited = False
        self.idle = False
        self.sent = 0
        self.mark = 0
        self.pid, self.fd = pty.fork()
        # A desktop sends its own stderr to desktop.log in its state folder
        # while it runs, so an error there never reaches the terminal:
        # close() reads that too.
        state = (env or {}).get("XDG_STATE_HOME") or os.path.join(
            HOME, str(self.pid), "state")
        self.log = os.path.join(state, "hibr", "desktop.log")
        if self.pid == 0:
            # A shell this suite happens to run from can itself be a hold
            # client -- inside a held desktop, in CI under one, anywhere --
            # and HIBR_HOLD is a real environment variable, inherited like
            # any other. A test's own dt_autohold sees it already set and
            # returns immediately, thinking it is already held, when the
            # session it names is nothing to do with this run at all: the
            # desktop still comes up looking entirely normal, and every
            # hold-dependent check fails confusingly far from this cause.
            os.environ.pop("HIBR_HOLD", None)
            os.environ["TERM"] = "xterm-256color"
            os.environ["HIBR_TESTIDLE"] = "1"
            own = os.path.join(HOME, str(os.getpid()))
            os.environ["XDG_CONFIG_HOME"] = os.path.join(own, "config")
            os.environ["XDG_STATE_HOME"] = os.path.join(own, "state")
            os.environ["XDG_DATA_HOME"] = os.path.join(own, "data")
            for k, v in (env or {}).items():
                os.environ[k] = v
            os.execv(HIBR, ["hibr"] + [str(a) for a in argv])
        if size:
            self.resize(rows, cols)
        if settle:
            if self.until_idle(settle + 0.4):
                self.idle = True
            else:
                self.collect(0.05)

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

    def idled(self):
        """Whether the desktop has said it is idle with everything sent so
        far read -- the frame after the last key is on screen."""
        m = None
        for m in IDLE.finditer(self.out, self.mark):
            pass
        if m is None:
            return False
        self.mark = m.start()
        return int(m.group(1)) >= self.sent

    def frames(self):
        """How many frames the desktop has drawn, by its last idle marker."""
        m = None
        for m in IDLE.finditer(self.out):
            pass
        return int(m.group(2)) if m and m.group(2) else 0

    def until_idle(self, timeout=5.0):
        """Collect until idled(), the process exits, or timeout; whether it
        became idle."""
        end = time.time() + timeout
        while time.time() < end:
            if self.idled():
                return True
            if not select.select([self.fd], [], [], 0.02)[0]:
                continue
            try:
                d = os.read(self.fd, 65536)
            except OSError:
                return False
            if not d:
                return False
            self.out += d
        return self.idled()

    def send(self, data, settle=None, collect=None):
        if isinstance(data, str):
            data = data.encode()
        os.write(self.fd, data)
        self.sent += len(data)
        if self.idle and settle is None and collect is None:
            self.until_idle()
            return self
        if settle is None:
            settle = 0.25
        if collect is None:
            collect = 0.2
        if settle:
            time.sleep(settle)
        if collect:
            self.collect(collect)
        return self

    def keys(self, ks, settle=None, collect=None):
        """Send each key; a number among them is a pause of that many
        seconds, output collected meanwhile -- for a game or a clock, which
        moves on time passing rather than on keys."""
        for k in ks:
            if isinstance(k, (int, float)):
                self.collect(k)
            else:
                self.send(k, settle, collect)
        return self

    def signal(self, sig=signal.SIGINT):
        os.kill(self.pid, sig)
        return self

    def quit(self, key=b"q", wait=1.2):
        """Ask it to stop, then wait for it to -- no longer than it takes."""
        if key is not None:
            try:
                os.write(self.fd, key)
            except OSError:
                pass
        return self.wait(wait)

    def close(self):
        """Tear down, recording whether it had already exited on its own,
        and any error the shell printed while it ran."""
        if not getattr(self, "scanned", False):
            self.scanned = True
            TERMS[0] += 1
            text = self.out
            try:
                text += b"\n" + open(self.log, "rb").read()
            except OSError:
                pass
            for m in ERROR.finditer(text):
                e = m.group(1).decode("utf8", "replace")
                if not any(x.search(e) for x in EXPECTED):
                    ERRS.append(e)
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
            self.collect(0.02)
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
    """The `mod load` prefix for a -c command, from module names or paths --
    and `q` as the desktop's quit key, which it has none of by default
    since 0.73, so Term.quit can end a desktop session the way it always
    has. It is set before the window manager is sourced, which keeps any
    key a session has already set."""
    out = ['declare -gA DT_KEYS; DT_KEYS["quit"]=q']
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


RAN = [0]


def check(name, ok, show=None):
    RAN[0] += 1
    print(("ok   " if ok else "FAIL ") + name)
    if not ok:
        FAIL.append(name)
        if show is not None and os.environ.get("V"):
            print(show.dump() if hasattr(show, "dump") else show)
    return ok


def report(total=None):
    """Say how it went. total is how many checks the suite meant to make;
    a suite that made a different number -- one skipped by an exception
    caught somewhere, or added without the plan being raised -- fails
    rather than reporting a count it did not earn."""
    if TERMS[0]:
        check("no session printed a shell error", not ERRS,
              "\n".join(dict.fromkeys(ERRS)))
        if ERRS:
            print("\n".join("  " + e for e in dict.fromkeys(ERRS)))
    print()
    print("%d passed, %d failed" % (RAN[0] - len(FAIL), len(FAIL)))
    if total is not None and RAN[0] != total:
        print("planned %d checks, made %d" % (total, RAN[0]))
        sys.exit(1)
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
