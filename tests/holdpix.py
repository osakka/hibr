#!/usr/bin/env python3
"""Pictures through hold: a held program's bitmaps reaching a real terminal.

    python3 tests/holdpix.py [path-to-hibr]

Measured rather than assumed, which is how this was found: `console gfx`
answered `none 0 0` inside anything `hold` started -- hold's own pty reports
no cell size -- and `dt_autohold` holds every desktop, so pixels had never
once reached a desktop on any terminal (Gitea #103). Both halves are checked
here:

  - the cell size an attaching client's own terminal reports reaches the
    program's pty, so `console gfx` inside a held program names a protocol;
  - a picture the program places reaches the client, whole, at its own
    corner -- hold draws cells from its own emulator, which used to consume
    every sixel and every kitty escape and draw nothing for either, the OSC
    52 trap again;
  - a client that joins afterwards gets the pictures with its first frame;
  - a client at an offset in the session's own space gets them translated;
  - text drawn through a picture takes it away again;
  - a client whose terminal says nothing about pixels gets blocks, and no
    bitmap is sent to it at all.

The control, as with OSC 52, is the unheld run of the same script: the
sixel suite and the kitty suite are that control, and they pass.
"""
import os
import shutil
import struct
import sys
import tempfile
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import screen as sx
from screen import Term, check, report, load

if len(sys.argv) > 1:
    sx.HIBR = os.path.abspath(sys.argv[1])
D = tempfile.mkdtemp(prefix="hibr-holdpix-")
LOG = os.path.join(D, "inner.log")
CELL = (8, 16)
# Everything here is several times slower under the sanitizers -- two
# sanitizer-built hibrs and a sanitizer-built emulator between them -- so the
# held program has to stay alive several times longer, or it closes its own
# console while the checks are still watching and the screen model is reset
# by the repaint that follows.
SLOW = 4 if os.environ.get("ASAN_OPTIONS") else 1


def png(name, w, h, f):
    """A PNG of w by h, each pixel from f(x, y) -- no library either side."""
    raw = b"".join(b"\x00" + bytes(v for x in range(w) for v in f(x, y))
                   for y in range(h))

    def chunk(tag, data):
        body = tag + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body))

    path = os.path.join(D, name)
    open(path, "wb").write(
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
        + chunk(b"IDAT", zlib.compress(raw))
        + chunk(b"IEND", b""))
    return path


GRAD = png("grad.png", 48, 48, lambda x, y: (x * 5 % 256, y * 5 % 256, 90))


NAMED = [0]


def name():
    """A session name nobody else in this suite will use. A name reused
    across runs races its own clean-up: `hold kill` returns before the
    program has gone, and the next `hold new` is told the name is taken --
    which then reads as an attach finding no session at all."""
    NAMED[0] += 1
    return "p%d" % NAMED[0]


def kill(nm):
    """End the session, in this suite's own TMPDIR -- which is where the
    sessions live, so a kill that forgets it ends nothing."""
    os.system("TMPDIR=%s %s -c 'need hold; hold kill %s' > /dev/null 2>&1"
              % (D, sx.HIBR, nm))


def inner(body, settle=12000, nm="p"):
    """A held program: open the display, wait until a client has attached and
    said what its cells measure, then do as it is told.

    Waiting for the answer rather than for the clock: a program `hold` starts
    has no client at all for its first moment, and under a full parallel run
    an attach that takes a quarter of a second alone takes several. A fixed
    wait here passed alone and failed three checks in all.py, which is what a
    load-dependent timing assumption looks like. settle bounds the wait; it
    is not spent."""
    p = os.path.join(D, "pic%s.hibr" % nm)
    open(p, "w").write(
        load("console", "img")
        + "console open\n"
        + "i=0\n"
        + "while [ $i -lt %d ]; do\n" % max(1, settle // 100)
        + "  g := console gfx\n"
        + "  case $g in none*) ;; *) break ;; esac\n"
        + "  console key 100 > /dev/null\n"
        + "  i=$((i + 1))\n"
        + "done\n"
        + "g := console gfx\n"
        + 'echo "gfx $g" >> ' + LOG + "\n"
        + "console pen '#ffffff' '#000000'\n"
        + body
        # Waiting in a loop, not in one long call: `console key` returns on a
        # resize as well as on a key, and a second display joining resizes
        # the session -- a single wait would end here and the program would
        # exit the moment anyone else attached, which reads as hold dropping
        # the session.
        + "i=0\nwhile [ $i -lt %d ]; do console key 1000 > /dev/null; " % (12 * SLOW)
        + "i=$((i + 1)); done\n"
        + "console close\n")
    return p


def run(body, attach="hold attach %s\n", cell=CELL, settle=12000, wait=40,
        gfx="kitty", until="HELD"):
    """Hold that program, attach to it from a pty that says what a cell
    measures, and give back the client's own screen. HIBR_GFX stands in for
    the terminal's own name, which the harness scrubs on purpose: the held
    program inherits it through `hold new`, the same as it inherits every
    other variable."""
    nm = name()
    open(LOG, "w").close()
    outer = os.path.join(D, "s%s.hibr" % nm)
    open(outer, "w").write(
        load("hold")
        + 'hold new -d %s "$HIBR" %s\n' % (nm, inner(body, settle, nm))
        + "sleep 0.6\n"
        + (attach % nm if "%s" in attach else attach))
    t = Term(outer, rows=24, cols=60, settle=1.0, cellw=cell[0],
             cellh=cell[1], env={"HIBR_GFX": gfx, "TMPDIR": D})
    for _ in range(wait):
        t.collect(0.25)
        if until in t.text:
            break
    t.collect(1.0)
    sc = t.screen()
    t.quit(None, 0.5)
    kill(nm)
    log = open(LOG).read()
    return sc, log


PIC = ("console put 0 0 'HELD PICTURE HERE'\n"
       "img draw %s 2 2 6 12 -m pixels\n"
       "console flush\n" % GRAD)

sc, log = run(PIC)
check("a held program is told what a cell measures, so it has a protocol",
      "gfx kitty 8 16" in log or "gfx sixel 8 16" in log, log)
check("and the picture reaches the attached client, at its own corner",
      len(sc.images) == 1 and sc.images[0][:2] == (2, 2), (sc.images, log))
check("whole, every chunk of it",
      len(sc.images) == 1 and sc.images[0][2] == (96 * 96 * 3 + 2) // 3 * 4,
      sc.images)
check("with the text around it still text",
      sc.row(0).startswith("HELD PICTURE HERE"), sc.row(0))

sc, log = run(PIC, gfx="sixel")
check("a sixel goes through the same way",
      len(sc.images) == 1 and sc.images[0][:2] == (2, 2) and not sc.apc,
      (sc.images, sc.apc, log))

# A picture the text is then drawn through must never be left behind on a
# client's screen. Which of the two a client sees depends on timing that is
# not ours: hold renders snapshots of its emulator rather than the program's
# byte stream, so two flushes a fraction of a second apart can arrive in one
# read -- under the sanitizers they regularly do -- and the picture is then
# dropped before any client ever saw it. So the invariant is the one that
# matters and holds either way: the text is there, and nothing is holding a
# picture that has gone. kitgfx.py checks the targeted delete itself, unheld,
# where the sequence is ours to control.
sc, log = run(PIC + "console put 3 3 'THROUGH'\nconsole flush\n",
              until="THROUGH")
check("text drawn through it leaves no picture behind",
      "THROUGH" in sc.row(3) and
      (not sc.images or any("a=d" in a for a in sc.apc)),
      (sc.row(3), sc.images, [a[:40] for a in sc.apc]))

# A window that moves takes its picture with it: the console drops the region
# at the old corner and places one at the new. A kitty picture stays on a
# terminal until something deletes it, and hold's clients never saw the
# console's own delete -- which left a real desktop covered in the pictures
# of windows that had moved, reported on kitty against 0.99.77. Whether a
# client sees one placement or two is hold's own timing; what must hold
# either way is that it is never left holding the one that has gone.
sc, log = run(PIC + "console key 1200\n"
              # A window moving is the picture placed somewhere else *and*
              # the cells it used to cover drawn through, which is what makes
              # the console drop the old region. Drawing a second picture
              # without disturbing the first is two pictures, and both
              # rightly stay.
              "img draw %s 12 20 6 12 -m pixels\n" % GRAD
              + "console put 3 3 'GONE'\n"
              + "console put 1 0 'MOVED'\nconsole flush\n", until="MOVED")
check("a picture that moves leaves nothing where it was",
      "MOVED" in sc.row(1) and
      (len(sc.images) < 2 or any("a=d" in a for a in sc.apc)),
      (sc.row(1), sc.images, [a[:40] for a in sc.apc]))

sc, log = run(PIC + "console key 700\nconsole flush\n"
              "console key 700\nconsole flush\n"
              "console put 1 0 'SETTLED'\nconsole flush\n", until="SETTLED")
check("a settled frame sends it again no more than the console does",
      len(sc.images) == 1 and "SETTLED" in sc.row(1), (sc.images, sc.row(1)))

# A second display joining has missed the escape that carried the picture:
# it gets every one of them with its own first full frame, which is what the
# generation kept per client is for. Two ptys, because two clients on one
# terminal would be writing over each other.
open(LOG, "w").close()
NM = name()
one = os.path.join(D, "one.hibr")
two = os.path.join(D, "two.hibr")
open(one, "w").write(load("hold")
                     + 'hold new -d %s "$HIBR" %s\n' % (NM, inner(PIC, 12000, NM))
                     + "sleep 0.6\n"
                     + "hold attach -m -n one %s 0 0\n" % NM)
open(two, "w").write(load("hold") + "hold attach -m -n two %s 0 0\n" % NM)
t1 = Term(one, rows=24, cols=60, settle=1.0, cellw=8, cellh=16,
          env={"HIBR_GFX": "kitty", "TMPDIR": D})
for _ in range(24):
    t1.collect(0.25)
    if "HELD" in t1.text:
        break
t2 = Term(two, rows=24, cols=60, settle=1.0, cellw=8, cellh=16,
          env={"HIBR_GFX": "kitty", "TMPDIR": D})
for _ in range(16):
    t2.collect(0.25)
    if "HELD" in t2.text:
        break
t2.collect(1.0)
sc = t2.screen()
t2.quit(None, 0.5)
t1.quit(None, 0.5)
kill(NM)
check("a display that joins afterwards gets the pictures it never saw",
      len(sc.images) == 1 and sc.images[0][:2] == (2, 2) and
      sc.row(0).startswith("HELD PICTURE HERE"), (sc.images, sc.row(0)))

# No cell size will ever arrive here, so the poll above is bounded short
# rather than waiting out its whole budget for an answer that cannot come.
sc, log = run(PIC, cell=(0, 0), settle=800)
check("a client that says nothing about pixels gets blocks, not a bitmap",
      not sc.images and "gfx none 0 0" in log, (sc.images, log))
check("and the blocks are really drawn", "▀" in sc.row(3), repr(sc.row(3)))

# Mouse reporting is another thing the emulator draws nothing for, and so
# another thing hold has to carry: the program writes CSI ? 1002 h into
# hold's own pty, the emulator takes it as a mode change, and unless
# hd_modes sends it on, the client's real terminal never enables reporting
# and every click is discarded -- "the mouse moves but nothing is
# clickable", which reads as the client's bug and is not one. The whole
# chain is four components long and nothing pinned it until Gitea #164
# reported the symptom from a display of its own; measured, hibr's own side
# carries it end to end, so this is the guard for that.
inner2 = os.path.join(D, "mouse.hibr")
open(inner2, "w").write(
    load("console")
    + "console open\nconsole mouse drag\n"
    + "console put 1 1 'HELD MOUSE'\nconsole flush\n"
    + "i=0\nwhile [ $i -lt %d ]; do console key 1000 > /dev/null; "
      "i=$((i + 1)); done\n" % (12 * SLOW)
    + "console close\n")
NM2 = name()
outer2 = os.path.join(D, "mouseout.hibr")
open(outer2, "w").write(
    load("hold")
    + 'hold new -d %s "$HIBR" %s\n' % (NM2, inner2)
    + "sleep 0.8\n"
    + "hold attach %s\n" % NM2)
tm = Term(outer2, rows=24, cols=70, settle=1.0, env={"TMPDIR": D})
for _ in range(30 * SLOW):
    tm.collect(0.25)
    if b"\x1b[?1002h" in tm.out and "HELD MOUSE" in tm.text:
        break
rawm = tm.out
tm.quit(None, 0.6)
kill(NM2)
check("a held program asking for the mouse makes its client enable reporting",
      b"\x1b[?1002h" in rawm and b"\x1b[?1006h" in rawm and "HELD MOUSE" in tm.text,
      (rawm[-200:], tm.text[-120:]))

shutil.rmtree(D, True)
report(13)
