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
import re
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
        gfx="kitty", until="HELD", bg=None, caps=None, term=None, rows=24,
        cols=60, pics=0):
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
    env = {"TMPDIR": D}
    if gfx is not None:
        env["HIBR_GFX"] = gfx
    if term:
        env["TERM"] = term
    t = Term(outer, rows=rows, cols=cols, settle=1.0, cellw=cell[0],
             cellh=cell[1], env=env, bg=bg, caps=caps)
    for _ in range(wait):
        t.collect(0.25)
        if until in t.text:
            break
    t.collect(1.0)
    # A picture that is megabytes takes longer than the text beside it to
    # come through, so a check that wants pictures waits for them, bounded.
    # `pics` is a count, or a test given the screen: the model counts a
    # picture from its first chunk, so one that is megabytes is there long
    # before all of it is.
    want = pics if callable(pics) else (lambda s: len(s.images) >= pics)
    for _ in range(wait if pics else 0):
        if want(t.screen()):
            break
        t.collect(0.25)
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

# A picture reaches a held display only as hold's emulator keeps it, and it
# kept at most 4 MB of escapes: a wider one was refused and the display got
# nothing, which is a Browser window in pixels wider than about 160 columns
# arriving as an empty area (Gitea #292). This one is 70 by 260 cells, 2080
# by 1120 pixels, which the console sends as well over 4 MB of escapes, and
# it must arrive, one picture, all of it. Against the old bound nothing
# arrived at all.
BIG = ("console put 0 0 'HELD PICTURE HERE'\n"
       "img draw %s 1 0 70 260 -m pixels\n"
       "console flush\n" % GRAD)
sc, log = run(BIG, rows=72, cols=262, wait=160,
              pics=lambda s: s.images and s.images[0][2] > (4 << 20))
check("a picture past 4 MB reaches the held display whole",
      len(sc.images) == 1 and sc.images[0][2] > (4 << 20),
      ([i[:3] for i in sc.images], log))

# A picture drawn *under* the text -- which is what a wallpaper is, and the
# only user of DP_IMG_UNDER -- reaches a client not at all: measured at
# zero APCs and zero images, for a held desktop and for a bare held program
# alike, and identically with `console imgcomp off`, so it is nothing to do
# with the compression of 0.99.128. Gitea #165 has the measurements. The
# check for it was written here and taken out again rather than shipped
# failing, since what it asserts has apparently never worked; it belongs
# here the day that ticket is understood.

sc, log = run(PIC, gfx="sixel")
check("a sixel goes through the same way",
      len(sc.images) == 1 and sc.images[0][:2] == (2, 2)
      and not [a for a in sc.apc if "a=T" in a],
      (sc.images, sc.apc, log))

# A picture drawn *under* the text is the wallpaper's own path, and it is the
# one that never arrived at all (Gitea #165). The console sends it once, with
# the kitty protocol's `z=-1` -- "below the text, for a picture text is drawn
# over" -- and then keeps it: `cn_image` finds the same picture in the same
# rectangle and only marks it still wanted, so there is no second
# transmission to recover from. The emulator stored `r, c, rows, cols, id`
# and the bytes, discarded the `z`, and `tm_imghit` then dropped the region
# on the first cell written inside it -- which for an under-text picture is
# every frame, since text on top of it is the entire point. So `hold` had
# nothing kept to re-emit: zero images at the client, permanently, which is
# what a wallpaper that is a black rectangle actually is.
#
# The cell written inside it is what makes this fail pre-fix: without the
# `console put`, `tm_imghit` never runs and the picture arrives even on the
# broken code.
UNDER = ("console put 0 0 'HELD PICTURE HERE'\n"
         "img draw %s 2 2 6 12 -m pixels -u\n"
         "console put 3 3 'OVER'\n"
         "console flush\n" % GRAD)

sc, log = run(UNDER, until="OVER")
check("a picture under the text reaches the client with text drawn over it",
      len(sc.images) == 1 and sc.images[0][:2] == (2, 2), (sc.images, log))
check("and that text is still text",
      "OVER" in sc.row(3), (sc.row(3), log))

# And the frame after, which is the state a desktop is actually in. A
# settled wallpaper is not re-drawn: `dt_wallkeep` calls `img keep`, which
# reaches `cn_imgkeep` -- it matches an under-text region by its rectangle,
# marks it still wanted and sends nothing. So the picture the client holds
# has to survive another frame of text being written over it with no
# transmission to repair it, which is the case a fix that only worked on the
# placing frame would still get wrong.
KEEP = (UNDER + "img keep %s 2 2 6 12 -m pixels\n"
                "console put 4 4 'SETTLED'\n"
                "console flush\n" % GRAD)

sc, log = run(KEEP, until="SETTLED")
check("and survives a settled frame that only keeps it, text and all",
      len(sc.images) == 1 and sc.images[0][:2] == (2, 2) and
      "SETTLED" in sc.row(4), (sc.images, sc.row(4), log))

# Sixel is paint, not an object: writing over the cells is what removes it,
# and the console re-paints such a region itself when the cells above it
# change (`image.c`'s retire loop, for an id-less region only). So the flag
# above must not reach it -- a kept sixel would be re-emitted *over* the
# text by hold's renderer, and the text would stop reading. That is the
# failure a later tidy-up would introduce by "finishing" the flag for both
# protocols, and this is the check that refuses it.
sc, log = run(UNDER, gfx="sixel", until="OVER")
check("a sixel under the text is paint, so the text over it still reads",
      "OVER" in sc.row(3), (sc.row(3), [a[:40] for a in sc.apc], log))

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

# What the terminal's own background is has to reach a held display too,
# and it is the held case that matters: every desktop is held, and a cell
# painted exactly that colour is the one cell a picture drawn under the
# text is let through (Gitea #215). The emulator in the middle cannot
# answer a colour query -- it has no colours of its own -- so it passes it
# on, hold sends it to every client, and the reply comes back the way a
# keystroke does. The query at open reaches nobody, since a held program
# has no client for its first moment; the one the attach's own resize asks
# again is what is answered here, which is the same path a session
# reattached from another machine takes.
BGQ = ("i=0\n"
       "while [ $i -lt 60 ]; do\n"
       "  console flush\n"
       "  b := console termbg\n"
       "  case $b in *answered*) break ;; esac\n"
       "  k := console key 100\n"
       '  case $k in "") ;; *) echo "key [$k]" >> ' + LOG + " ;; esac\n"
       "  i=$((i + 1))\n"
       "done\n"
       "b := console termbg\n"
       'echo "bg $b" >> ' + LOG + "\n"
       "console put 0 0 'HELD BG'\n"
       "console flush\n")
sc, log = run(BGQ, until="HELD BG", bg="#101418")
check("a held display learns its client terminal's own background",
      "bg #101418 answered" in log, log)
# And it arrives as an answer rather than as input: the program read every
# key while it waited and was handed none, where before this an OSC reply
# was decoded as `alt-]` and the rest of its own text typed in. Only worth
# anything beside the check above -- a release that asked nothing would be
# sent no reply to mistake for keys -- so it reads both halves.
check("and the reply reaches it as an answer rather than as keys",
      "answered" in log and "key [" not in log
      and sc.find("HELD BG") is not None, log)

# And what the client's terminal can *draw* has to reach the session too,
# which until 0.99.181 nothing did (Gitea #216): `cn_gfx` read the picture
# protocol off `$TERM`, and inside `hold` that is the session's own name,
# frozen when it was started -- so a session begun in kitty went on sending
# kitty escapes to whatever it was later attached from, and one begun
# anywhere nameless went on drawing half blocks to a terminal that can place
# pixels. Both directions, driven here as a reattach is: the session's own
# environment says one thing and the client answers another.
#
# `settle=100` rather than the usual wait for a non-`none` gfx: that wait is
# exactly what is in question here, so the body polls instead.
CAPQ = ("i=0\n"
        "while [ $i -lt 60 ]; do\n"
        "  console flush\n"
        "  g := console gfx\n"
        "  case $g in %s) break ;; esac\n"
        "  console key 100 > /dev/null\n"
        "  i=$((i + 1))\n"
        "done\n"
        "g := console gfx\n"
        'echo "gfx $g" >> ' + LOG + "\n"
        "console put 0 0 'HELD CAP'\n"
        "console flush\n")

sc, log = run(CAPQ % "sixel*", until="HELD CAP", gfx=None, settle=100,
              term="xterm-kitty", caps=["sixel"])
check("a held session whose own name says kitty follows a sixel client's "
      "answer", "gfx sixel" in log, log)

sc, log = run(CAPQ % "kitty*", until="HELD CAP", gfx=None, settle=100,
              caps=["kitty"])
check("and one whose name says nothing draws pixels once a client says it "
      "can", "gfx kitty" in log, log)

# The window before that answer arrives is its own bug, and it is the one a
# person actually meets (Gitea #249): `cn_gfx` falls back to the name list
# while nothing has answered, a held session's name is the frozen
# `xterm-kitty` of whatever started it, and so the first frames of a
# `desktop -r` onto a plain xterm place a *kitty bitmap* on a terminal that
# draws none. The console claims those cells and skips them in the diff, the
# terminal drops the payload, and the wallpaper -- which keeps placing the
# same picture, so the region is never retired -- leaves a black rectangle
# that nothing repaints, for the life of the session. Measured on stock
# 0.99.206 as one `a=T` payload sent to a client that answered it draws
# neither, with `console gfx` ending at `none` in the same run.
#
# So a bitmap is committed only on a protocol that was *answered* or forced
# with HIBR_GFX, never on one read off a name: `cn_gfxsure`. A frame or two
# of half blocks at startup costs nothing, and the picture is placed for
# real the moment a client that can draw it says so, which is the pair of
# checks above.
#
# The picture is placed on every pass rather than once, because the window
# is short: a placement that happens only after the answer cannot tell a
# fixed version from a broken one.
GUESS = ("i=0\n"
         "while [ $i -lt 20 ]; do\n"
         "  console put 0 0 'HELD GUESS'\n"
         "  img draw %s 2 2 6 12 -u\n"
         "  console flush\n"
         "  console key 100 > /dev/null\n"
         "  i=$((i + 1))\n"
         "done\n"
         "g := console gfx\n"
         'echo "gfx $g" >> ' + LOG + "\n"
         "console put 1 0 'HELD DONE'\n"
         "console flush\n") % GRAD

sc, log = run(GUESS, until="HELD DONE", gfx=None, settle=100,
              term="xterm-kitty", caps=[])
check("a session named kitty sends no bitmap to a client that answers it "
      "draws neither, not even before the answer arrives",
      not [a for a in sc.apc if "a=T" in a] and not sc.images
      and "gfx none" in log, (sc.apc[:4], sc.images, log))
check("and it drew blocks there instead", "▀" in sc.row(2), repr(sc.row(2)))

# Two displays that disagree about pictures (Gitea #258). A primary whose
# terminal draws none was sent a bitmap the moment one that draws kitty
# joined -- the session switched to pixels for everyone -- and its
# wallpaper went black, since the cells under a picture are left blank for
# it. And with two that both draw pictures, the first display's wallpaper
# was deleted and sent again on every frame that only kept it, because
# one record and one kept rectangle served both displays. Each attaching
# client now asks its own terminal before it joins, hold sends a picture
# only to a terminal that said it draws that kind, and the desktop draws
# each display's wallpaper as pixels or half blocks by what that display
# said. A real held desktop, a picture wallpaper, two ptys.
def payload(raw):
    """The bytes of the last kitty picture a client was sent, every chunk of
    it: two displays the same size sent the same picture are sent the same
    bytes. The last, because a display attached alone is sent its picture
    again when a second joins and the union changes."""
    segs = re.findall(rb"\x1b_G([^\x1b]*)\x1b\\", raw)
    at = [i for i, s in enumerate(segs) if b"a=T" in s.split(b";", 1)[0]]
    if not at:
        return b""
    out = []
    for s in segs[at[-1]:]:
        ctl, _, data = s.partition(b";")
        out.append(data)
        if b"m=1" not in ctl:
            break
    return b"".join(out)


def pair(cap1, cap2, fast=False, extra=""):
    """A held desktop with a picture wallpaper, one display attached and a
    second joining beside it. Each cap is kitty (answers kitty's query and
    has a cell size) or none (answers the device attributes without sixel,
    and has no cell size). What each display was sent, counted from its own
    control strings after the join and again once settled, how many rows of
    half blocks each shows, and what `hold gfx` names each one."""
    nm = name()
    sess = os.path.join(D, "desk%s.hibr" % nm)
    open(sess, "w").write(
        load("console") + ". %s\n" % sx.tree("programs/desktop/desktop.hibr")
        + "DT_WALLIMG=%s\nDT_IMGMODE=auto\nDT_WALLMODE=stretch\n"
        % sx.tree("tests/img-quad.jpg")
        + ("busyf() { dt_want 50; }\ndt_ticker busyf\n" if fast else "")
        + extra + "dt_open || exit 1\ndt_run\ndt_close\n")
    one = os.path.join(D, "one%s.hibr" % nm)
    two = os.path.join(D, "two%s.hibr" % nm)
    open(one, "w").write(load("hold") + 'hold new -d %s "$HIBR" %s\n' % (nm, sess)
                         + "sleep 0.8\nhold attach -m -n one %s 0 0\n" % nm)
    open(two, "w").write(load("hold") + "hold attach -m -n two %s 0 80\n" % nm)

    def term(path, cap):
        cw, ch = (8, 16) if cap == "kitty" else (0, 0)
        return Term(path, rows=24, cols=80, settle=1.0, cellw=cw, cellh=ch,
                    env={"TMPDIR": D}, caps=["kitty"] if cap == "kitty" else [])

    def sent(t, a=0):
        r = t.raw[a:]
        return r.count(b"a=T"), r.count(b"a=d")

    def half(sc):
        return sum(1 for r in range(2, 20) if "▀" in sc.row(r) or "▄" in sc.row(r))

    t1 = term(one, cap1)
    for _ in range(int(32 * SLOW)):
        t1.collect(0.25)
    m1 = len(t1.raw)
    t2 = term(two, cap2)
    for _ in range(int(20 * SLOW)):
        t2.collect(0.25)
        t1.collect(0.05)
    t1.collect(2.0)
    t2.collect(1.0)
    a1, a2 = len(t1.raw), len(t2.raw)
    for _ in range(20):
        t1.collect(0.2)
        t2.collect(0.05)
    r = {"one": sent(t1, m1), "two": sent(t2), "one_settled": sent(t1, a1),
         "two_settled": sent(t2, a2), "one_half": half(t1.screen()),
         "two_half": half(t2.screen()), "one_pl": payload(t1.raw),
         "two_pl": payload(t2.raw)}
    g = os.path.join(D, "g%s.hibr" % nm)
    open(g, "w").write(load("hold") + "hold gfx %s\n" % nm)
    r["gfx"] = os.popen("TMPDIR=%s %s %s 2>&1" % (D, sx.HIBR, g)).read().split("\n")
    t2.quit(None, 0.5)
    t1.quit(None, 0.5)
    kill(nm)
    return r


r = pair("none", "kitty")
check("a display whose terminal draws no pictures is sent none when one that "
      "does joins, and keeps its wallpaper in half blocks",
      r["one"][0] == 0 and r["one_half"] > 10, r)
check("while the one that joined is sent its own picture",
      r["two"][0] == 1 and r["two_half"] == 0, r)
check("and hold gfx names what each display's own terminal said",
      "one none" in r["gfx"] and "two kitty" in r["gfx"], r["gfx"])
r = pair("kitty", "none")
check("a display that draws none joining one that does is sent no picture, "
      "and draws its wallpaper in half blocks",
      r["two"][0] == 0 and r["two_half"] > 10 and r["one_half"] == 0, r)
r = pair("kitty", "kitty", fast=True)
check("two displays that both draw pictures each keep theirs on a busy "
      "desktop, rather than the first being deleted and sent again",
      r["one_settled"] == (0, 0) and r["two_settled"] == (0, 0)
      and r["two"][0] == 1, r)
# The control for the check below: two displays the same size showing the
# one wallpaper are sent the very same bytes.
check("two displays with no wallpaper of their own are sent the same picture",
      r["one_pl"] and r["one_pl"] == r["two_pl"],
      (len(r["one_pl"]), len(r["two_pl"])))

# A wallpaper of a display's own, kept by the display's name (Gitea #284):
# `two` has one, `one` follows the session's. Each is sent its own picture,
# as pixels, and the one with none is unchanged.
r = pair("kitty", "kitty", extra="dt_walldisp two %s stretch follow\n"
         % sx.tree("tests/img-wide.png"))
check("a display with a wallpaper of its own is sent that picture, and the "
      "other display the session's",
      r["one_pl"] and r["two_pl"] and r["one_pl"] != r["two_pl"]
      and r["one_half"] == 0 and r["two_half"] == 0,
      (len(r["one_pl"]), len(r["two_pl"]), r["one_half"], r["two_half"]))

# A new session, attached at once, the way `desktop` starts one: the
# desktop asks its terminal what it draws in its very first output, and
# hold's emulator used to become a relay only at its first drain, which
# comes after that output was fed -- so it answered "no sixel" itself,
# dropped kitty's query, and the console took that for a real no. Every
# new session then drew its wallpaper in half blocks while `hold gfx` said
# kitty, and a restart inside the session fixed it (Gitea #278).
nm = name()
sess = os.path.join(D, "fresh%s.hibr" % nm)
open(sess, "w").write(
    load("console") + ". %s\n" % sx.tree("programs/desktop/desktop.hibr")
    + "DT_WALLIMG=%s\nDT_IMGMODE=auto\nDT_WALLMODE=stretch\n"
    % sx.tree("tests/img-quad.jpg")
    + "dt_open || exit 1\ndt_run\ndt_close\n")
fresh = os.path.join(D, "freshc%s.hibr" % nm)
open(fresh, "w").write(load("hold") + 'hold new %s "$HIBR" %s\n' % (nm, sess))
# And with no _ in the environment, as a session the login screen starts
# has none: the desktop read hold's list of what each display draws with
# `read -r gnm gv _`, strict vars refused it there, every display was taken
# for unknown, and the wallpaper was half blocks while `hold gfx` said
# kitty and YouTube drew pixels -- Blit, every session (Gitea #281).
t = Term(fresh, rows=24, cols=80, settle=1.0, cellw=8, cellh=16,
         env={"TMPDIR": D, "_": None}, caps=["kitty"])
for _ in range(int(32 * SLOW)):
    t.collect(0.25)
    if t.raw.count(b"a=T"):
        break
t.collect(1.0)
sc = t.screen()
fh = sum(1 for r in range(2, 20) if "▀" in sc.row(r) or "▄" in sc.row(r))
check("a new session started on a terminal that draws kitty pictures shows "
      "its wallpaper as one, not half blocks",
      t.raw.count(b"a=T") >= 1 and fh == 0, (t.raw.count(b"a=T"), fh, sc))
t.quit(None, 0.5)
kill(nm)

# A display attached from a hibr older than the asking never sends HD_GFX.
# It started unknown and stayed so for its whole attach -- sent no picture,
# its wallpaper half blocks, where before 0.99.235 it was sent them. It has
# HD_GFXWAITMS to say now, and is unasked after that, the default #274
# gave a carried one (Gitea #279). Played here by a client that speaks the
# old protocol: attach and size, and nothing about pictures.
import socket
import time


def oldclient(path, name, rows, cols, row, col, secs, mid):
    """Attach as a client from before the asking, read what it is sent for
    secs, and call mid once half way, while it is still attached."""
    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    s.connect(path)

    def msg(t, p):
        s.sendall(t + struct.pack("=I", len(p)) + p)
    msg(b"m", struct.pack("=4i", rows, cols, row, col) + name.encode())
    msg(b"w", struct.pack("=4i", rows, cols, cols * 8, rows * 16))
    got, t0 = b"", time.time()
    end, said = t0 + secs, None
    s.settimeout(0.2)
    while time.time() < end:
        if said is None and time.time() > t0 + secs / 2:
            said = mid()
        try:
            b = s.recv(65536)
        except socket.timeout:
            continue
        if not b:
            break
        got += b
    s.close()
    return got, said


nm = name()
sess = os.path.join(D, "old%s.hibr" % nm)
open(sess, "w").write(
    load("console") + ". %s\n" % sx.tree("programs/desktop/desktop.hibr")
    + "DT_WALLIMG=%s\nDT_IMGMODE=auto\nDT_WALLMODE=stretch\n"
    % sx.tree("tests/img-quad.jpg")
    + "dt_open || exit 1\ndt_run\ndt_close\n")
first = os.path.join(D, "oldf%s.hibr" % nm)
open(first, "w").write(load("hold") + 'hold new -d %s "$HIBR" %s\n' % (nm, sess)
                       + "sleep 0.8\nhold attach -m -n one %s 0 0\n" % nm)
t = Term(first, rows=24, cols=80, settle=1.0, cellw=8, cellh=16,
         env={"TMPDIR": D}, caps=["kitty"])
for _ in range(int(32 * SLOW)):
    t.collect(0.25)
    if t.raw.count(b"a=T"):
        break
sock = os.path.join(D, "hibr-hold-%d" % os.getuid(), nm)
g = os.path.join(D, "og%s.hibr" % nm)
open(g, "w").write(load("hold") + "hold gfx %s\n" % nm)
got, gfx = oldclient(sock, "old", 24, 80, 0, 80, 4.0 * SLOW, lambda: os.popen(
    "TMPDIR=%s %s %s 2>&1" % (D, sx.HIBR, g)).read().split("\n"))
t.collect(0.5)
check("a display from a hibr that never says what it draws is unasked, not "
      "unknown, and is sent the session's picture as before 0.99.235",
      "old unasked" in gfx and got.count(b"a=T") >= 1,
      (gfx, got.count(b"a=T"), len(got)))
t.quit(None, 0.5)
kill(nm)

# Control Panel > Notifications > Show On, Every Display (Gitea #280): the
# same note in the corner of each display, and a click on any copy acts on
# it. The default, the primary display alone, is tests/desktop.py's.
nm = name()
sess = os.path.join(D, "notes%s.hibr" % nm)
open(sess, "w").write(
    load("console") + ". %s\n" % sx.tree("programs/desktop/desktop.hibr")
    + "DT_NOTEWHERE=every\nDT_NOTEMS=60000\nNOTED=\n"
    + "nb() { [ -n \"$NOTED\" ] && return 0; [ \"${#DT_DISPLAYS[@]}\" -ge 2 ] "
    + "|| { dt_want 200; return 0; }; NOTED=1; dt_notep normal \"Seen on both\"; "
    + "dt_want 30; }\ndt_ticker nb\n"
    + "dt_open || exit 1\ndt_run\ndt_close\n")
one = os.path.join(D, "none%s.hibr" % nm)
two = os.path.join(D, "ntwo%s.hibr" % nm)
open(one, "w").write(load("hold") + 'hold new -d %s "$HIBR" %s\n' % (nm, sess)
                     + "sleep 0.8\nhold attach -m -n one %s 0 0\n" % nm)
open(two, "w").write(load("hold") + "hold attach -m -n two %s 0 80\n" % nm)
t1 = Term(one, rows=24, cols=80, settle=1.0, env={"TMPDIR": D})
for _ in range(int(16 * SLOW)):
    t1.collect(0.25)
t2 = Term(two, rows=24, cols=80, settle=1.0, env={"TMPDIR": D})
for _ in range(int(40 * SLOW)):
    t2.collect(0.25)
    t1.collect(0.05)
    if t1.screen().find("Seen on both") and t2.screen().find("Seen on both"):
        break
s1, s2 = t1.screen(), t2.screen()
check("with Show On at every display, a note is in the corner of each",
      s1.find("Seen on both") is not None and s2.find("Seen on both") is not None,
      str(s1) + "\n" + str(s2))
p2 = s2.find("Seen on both")
if p2:
    t2.send(sx.press(p2[0], p2[1]))
    t2.send(sx.release(p2[0], p2[1]))
for _ in range(int(12 * SLOW)):
    t1.collect(0.25)
    t2.collect(0.05)
    if t1.screen().find("Seen on both") is None:
        break
check("and a click on the copy on the other display acts on the note, so it "
      "goes from both", p2 is not None and t1.screen().find("Seen on both") is None
      and t2.screen().find("Seen on both") is None,
      str(t1.screen()) + "\n" + str(t2.screen()))
t2.quit(None, 0.5)
t1.quit(None, 0.5)
kill(nm)

# The kitty keyboard protocol through hold (Gitea #326). A held program's
# console asks the question, which the emulator passes on to the client's
# terminal; it pushes the disambiguate flag; hold puts that flag on each
# client whose own terminal answered the question when it attached, and on
# no other; a super chord typed at such a client reaches the program by
# name; and a client leaving takes the flags off its terminal again.
def kbdrun(caps):
    nm = name()
    open(LOG, "w").close()
    p = os.path.join(D, "kbd%s.hibr" % nm)
    open(p, "w").write(
        load("console") + "console open\nconsole flush\n"
        + "i=0\nwhile [ $i -lt %d ]; do\n" % (40 * SLOW)
        + "  k := console key 250\n"
        + '  [ -n "$k" ] && { echo "key $k" >> ' + LOG + "; break; }\n"
        + "  i=$((i + 1))\ndone\nconsole close\n")
    outer = os.path.join(D, "k%s.hibr" % nm)
    open(outer, "w").write(load("hold") + 'hold new -d %s "$HIBR" %s\n'
                           % (nm, p) + "sleep 0.6\nhold attach %s\n" % nm)
    t = Term(outer, rows=24, cols=60, settle=1.0, env={"TMPDIR": D},
             caps=caps)
    for _ in range(int(24 * SLOW)):
        t.collect(0.25)
        if b"\x1b[=1;1u" in t.raw:
            break
    t.collect(0.5)
    os.write(t.fd, b"\x1b[99;9u")
    for _ in range(int(16 * SLOW)):
        t.collect(0.25)
        if "key " in open(LOG).read():
            break
    t.collect(1.0)
    t.quit(None, 0.5)
    kill(nm)
    return t.raw, open(LOG).read()


raw, log = kbdrun(["kbd", "sixel"])
check("a client whose terminal speaks the keyboard protocol is given the "
      "flags the held program asked for", b"\x1b[=1;1u" in raw, raw[-300:])
check("and a super chord typed there reaches the program by name",
      "key super-c" in log, log)
check("and leaving takes the flags off that terminal",
      raw.count(b"\x1b[=0;1u") >= 2, raw[-200:])
raw, log = kbdrun(["sixel"])
check("a client whose terminal does not speak it is given no flags at all",
      b"\x1b[=" not in raw and b"\x1b[>1u" not in raw, raw[-300:])

# A window moved whole, through hold (Gitea #337). The held program's own
# terminal is hold's emulator, which says it copies rectangles itself, so the
# program sends DECCRA; hold applies it to its grid and replays it to a
# client whose own terminal said feature 28 -- and diffs for one that did
# not. Either way the client's screen is the same; only the bytes differ.
MVBODY = ('win() { local r=0 c; while [ $r -lt 6 ]; do c=0; while [ $c -lt 20 ]; do '
          'console pen "#c0c0c8" "#$(printf %02x%02x40 $((r * 30)) $((c * 12)))"; '
          'console put -p w1 $r $c x; c=$((c + 1)); done; r=$((r + 1)); done; '
          'console pen "#ffffff" "#000000"; console put -p w1 1 1 "moved by a copy"; }\n'
          'console pen "#c0c0c8" "#203048"\nconsole fill 0 0 24 60 .\n'
          'console pane w1 2 2 6 20\nwin\nconsole flush\nconsole key 1500 > /dev/null\n'
          'console pane w1 10 30 6 20\nconsole behind on\n'
          'console pen "#c0c0c8" "#203048"\nconsole fill 0 0 24 60 .\n'
          'console behind off\nwin\nc := console copy\n'
          'console pen "#ffffff" "#000000"\nconsole put 0 0 "HELD copy $c"\nconsole flush\n')


def mvheld(caps):
    t = None
    raw = []

    def grab(tt):
        raw.append(tt.raw)
    sc, log = run(MVBODY, gfx=None, caps=caps, settle=1500, until="HELD copy")
    return sc


sc1 = mvheld(["deccra"])
sc2 = mvheld([])
check("inside hold the program copies, since hold's own emulator says it can",
      "HELD copy on" in sc1.row(0) and "HELD copy on" in sc2.row(0),
      (sc1.row(0), sc2.row(0)))
check("and a client whose terminal copies is sent the copy, one that does not "
      "only cells",
      sc1.copies >= 1 and sc2.copies == 0, (sc1.copies, sc2.copies))
check("with the same screen either way",
      sc1.row(11)[31:46] == "moved by a copy" and
      all(sc1.g[r][q] == sc2.g[r][q] and sc1.p[r][q] == sc2.p[r][q]
          for r in range(1, 24) for q in range(60)),
      (sc1.row(11), sc2.row(11)))

# Synchronized output (Gitea #338). hold renders from its emulator's live
# grid, so a program's frame larger than one read used to reach every client
# half drawn; it waits now for the frame to close -- or for the emulator's
# one-second guard, so a program that dies inside one cannot freeze the
# screen -- and brackets every frame it sends a client in the pair itself.
SYNH, SYNL = b"\x1b[?2026h", b"\x1b[?2026l"


def syncheld(body, mid, last, gap=0.4):
    """Hold a program, wait for `mid` on the client's screen, then a little
    longer, and answer that screen, the one once `last` is there, and every
    byte the client was sent."""
    nm = name()
    open(LOG, "w").close()
    outer = os.path.join(D, "s%s.hibr" % nm)
    open(outer, "w").write(
        load("hold")
        + 'hold new -d %s "$HIBR" %s\n' % (nm, inner(body, 1500, nm))
        + "sleep 0.6\nhold attach %s\n" % nm)
    t = Term(outer, rows=24, cols=60, settle=1.0, cellw=CELL[0],
             cellh=CELL[1], env={"TMPDIR": D})
    t.until(lambda s: mid in s.text(), tries=80 * SLOW)
    t.collect(gap * SLOW)
    early = t.screen()
    late = t.until(lambda s: last in s.text(), tries=60 * SLOW)
    t.quit(None, 0.5)
    kill(nm)
    return early, late, t.raw


def paired(raw):
    """Every opening of the pair closed before the next one opens."""
    on, seen = False, 0
    for m in re.finditer(rb"\x1b\[\?2026([hl])", raw):
        if (m.group(1) == b"h") == on:
            return False
        on = not on
        seen += 1
    return not on and seen >= 4


# What decides it is the order on the wire, not a moment to look at: the
# program writes FIRST, waits, then SECOND on a row above it, inside the
# guard's second. Held back, the two arrive in one frame, SECOND first,
# since a frame is sent row by row; not held back, FIRST went out alone,
# earlier. Under the sanitizers a fixed look was once later than the guard.
early, late, raw = syncheld(
    "printf '\\033[3;1HREADY'\nsleep 0.3\n"
    "printf '\\033[?2026h\\033[6;1HFIRST'\nsleep 0.5\n"
    "printf '\\033[4;1HSECOND\\033[?2026l'\n",
    "READY", "SECOND", gap=0)
fi, se = raw.find(b"FIRST"), raw.find(b"SECOND")
fh = raw.rfind(SYNH, 0, fi)
check("a held program's open frame is not shown half drawn",
      0 <= se < fi and fh >= 0 and se > fh
      and raw.find(SYNL, fh) > fi, (fi, se, fh))
check("and is shown whole once the program closes it",
      "FIRST" in late.text() and "SECOND" in late.text(), late.text())
check("every frame a client is sent is one synchronized update",
      paired(raw), raw.count(SYNH))

early, late, raw = syncheld(
    "printf '\\033[3;1HREADY'\nsleep 0.5\n"
    "printf '\\033[?2026h\\033[4;1HLOST'\nsleep %s\n" % (4 * SLOW),
    "READY", "LOST", gap=0)
check("a frame the program never closes is shown after the guard's second",
      "LOST" in late.text(), late.text())

# The mouse in pixels through hold (Gitea #370). The held program asks for
# pixels; hold's emulator says it can give them, whatever the client's
# terminal does. A client whose terminal said it reports pixels is asked
# for them and its pixels reach the program in the session's own; one that
# did not is never asked, and its cell report reaches the program as the
# middle of that cell. Either way the program reads the same event.
def pixheld(caps, report_bytes, env=None):
    nm = name()
    open(LOG, "w").close()
    p = os.path.join(D, "px%s.hibr" % nm)
    open(p, "w").write(
        load("console", "hold") + "console open\nconsole pixels on\n"
        + "console mouse click\nconsole flush\n"
        + "i=0\nwhile [ $i -lt %d ]; do\n" % (40 * SLOW)
        + "  k := console key 250\n"
        + "  q := console pixels\n"
        + '  [ "$q" = on ] && [ ! -e %s.ready ] && : > %s.ready\n' % (LOG, LOG)
        + '  case $k in mouse*) echo "key $k|$q" >> ' + LOG + "\n"
        + '    hp := hold pixels; echo "pixels ${hp##* }" >> ' + LOG + "; break ;; esac\n"
        + "  i=$((i + 1))\ndone\nconsole close\n")
    outer = os.path.join(D, "x%s.hibr" % nm)
    open(outer, "w").write(load("hold") + 'hold new -d %s "$HIBR" %s\n'
                           % (nm, p) + "sleep 0.6\nhold attach %s\n" % nm)
    if os.path.exists(LOG + ".ready"):
        os.unlink(LOG + ".ready")
    e = {"TMPDIR": D}
    e.update(env or {})
    t = Term(outer, rows=24, cols=60, settle=1.0, cellw=8, cellh=16,
             env=e, caps=caps)
    for _ in range(int(24 * SLOW)):
        t.collect(0.25)
        if os.path.exists(LOG + ".ready") and \
                ("mpix" not in caps or b"\x1b[?1016h" in t.raw):
            break
    t.collect(0.5)
    os.write(t.fd, report_bytes)
    for _ in range(int(16 * SLOW)):
        t.collect(0.25)
        if "key " in open(LOG).read():
            break
    t.collect(0.5)
    t.quit(None, 0.5)
    kill(nm)
    if os.path.exists(LOG + ".ready"):
        os.unlink(LOG + ".ready")
    return t.raw, open(LOG).read()


raw, log = pixheld(["mpix", "sixel"], b"\x1b[<0;37;41M")
check("a client whose terminal reports pixels is asked for them",
      b"\x1b[?1016h" in raw, raw[-300:])
check("and its pixel reaches the held program as that pixel and its cell",
      "key mouse press left 2 4 36,40|on" in log, log)
check("and hold pixels says that display reports pixels", "pixels yes" in log,
      log)
raw, log = pixheld(["sixel"], b"\x1b[<0;5;3M")
check("a client whose terminal does not is never asked for pixels",
      b"\x1b[?1016h" not in raw, raw[-300:])
check("and its cell reaches the program as the middle of that cell",
      "key mouse press left 2 4 36,40|on" in log, log)
check("and hold pixels says that one does not", "pixels no" in log, log)
# HIBR_GFX names a picture protocol and nothing else. Blit sets it for every
# session, and the attach used to take it as the whole answer and ask the
# terminal nothing -- so a Blit display, which reports pixels, was a display
# that did not, and Auto never turned hovering on.
raw, log = pixheld(["mpix", "sixel"], b"\x1b[<0;37;41M", {"HIBR_GFX": "kitty"})
check("with HIBR_GFX set, a terminal that reports pixels is still asked",
      b"\x1b[?1016h" in raw, raw[-300:])
check("and hold pixels says it reports them", "pixels yes" in log, log)

# The pointer through hold (Gitea #373). The held program asks for its
# pointer hidden; hold's emulator keeps the shape, and each display is sent
# it by what its own terminal said at attach: `none` to one that can hide,
# its own pointer to one that cannot. A display that can is given its
# pointer back as it leaves.
def ptrheld(caps):
    nm = name()
    open(LOG, "w").close()
    p = os.path.join(D, "pt%s.hibr" % nm)
    open(p, "w").write(
        load("console", "hold") + "console open\nconsole flush\n"
        + "sleep %s\nconsole pointer hide\nconsole flush\n" % (1.5 * SLOW)
        + 'hp := hold pointers; echo "pointers ${hp##* }" >> ' + LOG + "\n"
        + "i=0\nwhile [ $i -lt %d ]; do console key 250; i=$((i + 1)); done\n"
        % (12 * SLOW) + "console close\n")
    outer = os.path.join(D, "y%s.hibr" % nm)
    open(outer, "w").write(load("hold") + 'hold new -d %s "$HIBR" %s\n'
                           % (nm, p) + "sleep 0.6\nhold attach %s\n" % nm)
    t = Term(outer, rows=24, cols=60, settle=1.0, env={"TMPDIR": D},
             caps=caps)
    for _ in range(int(24 * SLOW)):
        t.collect(0.25)
        if "pointers" in open(LOG).read():
            break
    t.collect(0.5)
    os.write(t.fd, b"\x1c")
    t.collect(1.0)
    t.close()
    kill(nm)
    return t.screen().ptr, open(LOG).read()


ptr, log = ptrheld(["ptr"])
check("a display that can hide its pointer is sent none: %r" % ptr,
      "none" in ptr)
check("and given its pointer back as it leaves", ptr[-1:] == [""], ptr)
check("and hold pointers says it can", "pointers yes" in log, log)
ptr, log = ptrheld(["ptrno"])
check("one that cannot is never sent none: %r" % ptr, "none" not in ptr)
check("and hold pointers says it cannot", "pointers no" in log, log)

shutil.rmtree(D, True)
report(59)
