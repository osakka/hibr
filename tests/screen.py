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
import atexit, shutil, tempfile, unicodedata
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


def scratch(nm):
    """A scratch path under /tmp that no other process shares.

    tests/all.py and tests/asan.py run side by side in the release gate, so
    two copies of the same suite are live at once: a fixed name meant one
    unlinked the file the other was about to be started on, and the second
    died with "no such file" in a check about something else entirely.
    """
    r, e = os.path.splitext(nm)
    return "/tmp/hibr-%s-%d%s" % (r, os.getpid(), e)


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
        # Pictures sent as DCS (sixel) or APC (the kitty graphics protocol):
        # where each landed and how many payload bytes it was.
        self.images = []
        # Every APC control string, in order, whole: "a=T,f=24,...,m=0" or
        # "a=d,d=I,q=2,i=1234". What a test asks about a kitty picture.
        self.apc = []

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
            # A DCS payload -- a sixel image is one (ESC P ... ESC \\) -- is
            # not text: fed to the grid as characters it would scribble the
            # bitmap's own bytes across the screen and break every check on
            # a window that happens to show a picture. The images are kept
            # instead, so a test can ask what was drawn rather than grep raw
            # output: each is (row, col, bytes) at the cursor it arrived on.
            if ch == "\x1b" and i + 1 < n and t[i + 1] == "P":
                j = i + 2
                while j < n and t[j:j + 2] != "\x1b\\" and t[j] != "\x9c":
                    j += 1
                self.images.append((self.r, self.c, j - (i + 2)))
                i = j + (2 if t[j:j + 2] == "\x1b\\" else 1)
                continue
            # An APC string is not text either: the kitty graphics protocol
            # sends ESC _ G <control> ; <base64> ESC \, in chunks of 4096 for
            # a large picture, and a delete with no payload at all. The
            # control part of each is kept whole, and a picture counts once
            # however many chunks carried it -- a test asking "how many
            # pictures" means placements, not escapes.
            if ch == "\x1b" and i + 1 < n and t[i + 1] == "_":
                j = i + 2
                while j < n and t[j:j + 2] != "\x1b\\" and t[j] != "\x9c":
                    j += 1
                ctrl, _, data = t[i + 2:j].partition(";")
                self.apc.append(ctrl)
                if "a=T" in ctrl or "a=p" in ctrl:
                    self.images.append((self.r, self.c, len(data)))
                elif data and self.images:
                    r, c, had = self.images[-1]
                    self.images[-1] = (r, c, had + len(data))
                i = j + (2 if t[j:j + 2] == "\x1b\\" else 1)
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
            wide = unicodedata.east_asian_width(ch) in ("W", "F")
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
# The same for calendar systems (ADR 0034): this tree's, not the installed ones.
os.environ["HIBR_CALENDARS"] = tree("mods/hcal/calendars")
os.environ["HIBR_SALAT"] = tree("mods/salat/methods")

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
                 size=True, cellw=0, cellh=0):
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
            # And the same for what a *desktop* exports. A suite is often
            # run from a terminal window inside a running desktop -- that is
            # how this was found, three supervisor checks failing on code
            # whose own gate had passed them hours earlier, because the
            # shell they ran from had DT_SUPERVISED=1 from the desktop it
            # was a window of. Every desktop the harness then started
            # believed it was already supervised, never started a
            # supervisor, and the check that kills one and waits for it to
            # come back waited for ever. DT_RESTORE is the same shape: a
            # restart's own snapshot path, which would make a fresh session
            # restore somebody else's windows.
            for v in ("DT_SUPERVISED", "DT_RESTORE", "DT_T0"):
                os.environ.pop(v, None)
            # Who orders right-to-left text is decided from what the
            # terminal says it is (DT_BIDI=auto), so a suite run from kitty
            # would draw Arabic differently from one run anywhere else. The
            # harness says nothing, and a test that wants a terminal of its
            # own sets these itself.
            for v in ("KITTY_WINDOW_ID", "VTE_VERSION", "TERM_PROGRAM",
                      "TERM_PROGRAM_VERSION"):
                os.environ.pop(v, None)
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
            self.resize(rows, cols, cellw, cellh)
        if settle:
            if self.until_idle(settle + 0.4):
                self.idle = True
            else:
                self.collect(0.05)

    def resize(self, rows, cols, cellw=0, cellh=0):
        """Set the pty's size, and with cellw/cellh the pixel size a cell
        has -- what a terminal fills in ws_xpixel/ws_ypixel and what the
        console needs before it will draw a picture as pixels rather than as
        half blocks. Zero says nothing, as an ordinary pty does."""
        self.rows, self.cols = rows, cols
        fcntl.ioctl(self.fd, termios.TIOCSWINSZ,
                    struct.pack("HHHH", rows, cols, cols * cellw, rows * cellh))

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

    def until(self, cond, tries=40, step=0.1):
        """Collect until the screen satisfies cond, and answer that screen.

        `keys` waits for the idle marker of the bytes it sent, so a key is
        never early -- but what a key *causes* can be: a dialog opening, a
        folder listed, a preview drawn and a window retitled all happen
        after that marker. Padding with a fixed pause instead is what made
        four of Write's file-dialog checks fail under a release gate that
        ran eighty pty sessions at once and pass every time alone (Gitea
        #163). Wait for the thing: text, or a function given the screen.
        """
        sc = self.screen()
        for _ in range(tries):
            if (cond(sc) if callable(cond) else sc.find(cond) is not None):
                return sc
            self.collect(step)
            sc = self.screen()
        return sc

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


def wheel(row, col, up=True, side="", shift=False):
    """A wheel report. 64 is up and 65 down; 66 and 67 are a wheel that
    tilts or a trackpad's horizontal swipe, and bit 2 is shift held, which
    the desktop reads as sideways as well."""
    b = 64 if up else 65
    if side:
        b = 66 if side == "left" else 67
    if shift:
        b |= 4
    return b"\x1b[<%d;%d;%dM" % (b, col + 1, row + 1)


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
