#!/usr/bin/env python3
"""Pictures as pixels: the console's own image regions, through a pty.

    python3 tests/sixel.py [path-to-hibr]

Sixel is not cells -- it is a bitmap the terminal paints over a rectangle --
so the console keeps each picture as a region and the ordinary cell diff is
what keeps it honest. What that has to mean, and what is checked here:

  - a terminal that does not say what a cell measures gets half blocks, and
    no bitmap is ever sent to it;
  - one that does gets the picture at the right corner, as one DCS string;
  - the text around it is untouched, and the cells under it are left alone
    rather than painted over;
  - a still picture costs nothing on the flushes after the first;
  - text drawn through it brings the text back -- nothing has to tell the
    console the picture has gone;
  - inside a window it goes at the pane's own offset, and is refused rather
    than drawn outside it;
  - mono and ascii stay cells, whatever the terminal can do.

The harness keeps DCS payloads out of its screen model and records them
instead (Screen.images), so a check can ask what was drawn where.
"""
import os
import struct
import sys
import tempfile
import shutil
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import screen as sx
from screen import Term, check, report, load, tree

if len(sys.argv) > 1:
    sx.HIBR = os.path.abspath(sys.argv[1])
D = tempfile.mkdtemp(prefix="hibr-sixel-")
CELL = (8, 16)


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
FLAT = png("flat.png", 16, 16, lambda x, y: (10, 200, 10))


def run(body, cell=CELL, gfx="sixel", rows=24, cols=60, settle=1.0):
    """Run a console script on a pty that says what a cell measures."""
    p = os.path.join(D, "s.hibr")
    open(p, "w").write("%sconsole open\n%sconsole close\n"
                       % (load("console", "img"), body))
    env = {} if gfx is None else {"HIBR_GFX": gfx}
    t = Term(p, rows=rows, cols=cols, settle=settle,
             cellw=cell[0], cellh=cell[1], env=env)
    t.collect(0.8)
    sc = t.screen()
    t.quit(None, 0.5)
    return sc


TEXT = ("console pen '#ffffff' '#000000'\n"
        "console put 0 0 'over the picture'\n"
        "console put 11 0 'under the picture'\n")
PIC = "img draw %s 2 2 6 12 %s\n" % (GRAD, "%s")

sc = run(TEXT + PIC % "-m sixel" + "console flush\nconsole key 400\n")
check("a picture goes where it was put, as one bitmap",
      len(sc.images) == 1 and sc.images[0][:2] == (2, 2) and sc.images[0][2] > 200,
      sc.images)
check("and the text around it is drawn as text",
      sc.row(0).startswith("over the picture") and
      sc.row(11).startswith("under the picture"), (sc.row(0), sc.row(11)))
check("the cells under it are left alone, not painted over",
      sc.row(3).strip() == "", repr(sc.row(3)))

sc = run(TEXT + PIC % "-m sixel" + "console flush\n" + PIC % "-m sixel" +
         "console flush\n" + PIC % "-m sixel" + "console flush\nconsole key 400\n")
check("the same picture again costs nothing: one bitmap for three flushes",
      len(sc.images) == 1, sc.images)

sc = run(TEXT + PIC % "-m sixel" + "console flush\n"
         "console put 3 3 'THROUGH'\nconsole flush\nconsole key 400\n")
check("text drawn through it brings the text back",
      len(sc.images) == 1 and "THROUGH" in sc.row(3), (sc.images, sc.row(3)))

sc = run(TEXT + PIC % "-m sixel" + "console flush\n"
         "img draw %s 2 2 6 12 -m sixel\nconsole flush\nconsole key 400\n" % FLAT)
check("a different picture in the same place replaces it",
      len(sc.images) == 2, sc.images)

sc = run(TEXT + PIC % "-m sixel" + "console flush\nconsole key 400\n", cell=(0, 0))
check("a terminal that does not say what a cell measures gets cells instead",
      not sc.images and "▀" in sc.row(3), (sc.images, repr(sc.row(3))))

sc = run(TEXT + PIC % "-m sixel" + "console flush\nconsole key 400\n", gfx="off")
check("and one told not to draw pictures gets cells too",
      not sc.images and "▀" in sc.row(3), (sc.images, repr(sc.row(3))))

sc = run(TEXT + PIC % "-m mono" + "console flush\nconsole key 400\n")
check("mono stays cells, whatever the terminal can do", not sc.images, sc.images)
sc = run(TEXT + PIC % "-m ascii" + "console flush\nconsole key 400\n")
check("so does ascii", not sc.images, sc.images)

sc = run("console pane w1 5 10 8 20\n"
         "img draw %s 1 1 4 8 -m sixel -p w1\n"
         "console flush\nconsole key 400\n" % GRAD)
check("in a pane it goes at the pane's own corner",
      len(sc.images) == 1 and sc.images[0][:2] == (6, 11), sc.images)
sc = run("console pane w1 5 10 8 20\n"
         "img draw %s 1 1 40 80 -m sixel -p w1\n"
         "console flush\nconsole key 400\n" % GRAD)
check("and past the pane's edge it is refused, not drawn outside it",
      not sc.images, sc.images)

sc = run("g := console gfx\nconsole put 0 0 \"[$g]\"\nconsole flush\n"
         "console key 400\n")
check("console gfx says what can be drawn and what a cell measures",
      "[sixel 8 16]" in sc.row(0), sc.row(0))
sc = run("g := console gfx\nconsole put 0 0 \"[$g]\"\nconsole flush\n"
         "console key 400\n", cell=(0, 0))
check("and says none where the terminal does not measure one",
      "[none 0 0]" in sc.row(0), sc.row(0))

# A film's frames go out with the fixed palette, so the terminal keeps its
# colour registers rather than being handed 256 new ones a frame; the media
# module hands over the frame it decoded and the console scales it.
WAV = os.path.join(D, "tone.wav")
import math
fr = 8000
pcm = b"".join(struct.pack("<h", int(8000 * math.sin(i * 0.05))) for i in range(fr))
open(WAV, "wb").write(b"RIFF" + struct.pack("<I", 36 + len(pcm)) + b"WAVEfmt "
                      + struct.pack("<IHHIIHH", 16, 1, 1, fr, fr * 2, 2, 16)
                      + b"data" + struct.pack("<I", len(pcm)) + pcm)
sc = run("p := media open %s -o none 2> /dev/null || p=\n"
         "[ -n \"$p\" ] && media mode \"$p\" sixel\n"
         "console put 0 0 'a sound file has no picture'\n"
         "console flush\nconsole key 400\n" % WAV)
check("a file with no picture draws none, and says nothing to the terminal",
      not sc.images, sc.images)

# A picture under the text (img draw -u, which the wallpaper uses): text in
# the same frame would otherwise drop it before it was ever sent. Sixel is
# paint, so a cell written over it has destroyed that much of it and the
# bitmap goes again -- which is what "repaint when anything moves" costs, and
# the honest difference from the kitty protocol, where the terminal
# composites the picture below the text and nothing is re-sent
# (tests/kitgfx.py has that side).
UPIC = "img draw %s 2 2 6 12 -m pixels -u\n" % GRAD
sc = run(TEXT + UPIC + "console put 3 3 'OVER'\nconsole flush\n"
         "console key 400\n")
check("a picture under the text survives text drawn over it in one frame",
      len(sc.images) == 1 and sc.images[0][:2] == (2, 2) and
      "OVER" in sc.row(3), (sc.images, sc.row(3)))
sc = run(TEXT + UPIC + "console flush\n" + UPIC + "console flush\n"
         + UPIC + "console flush\nconsole key 400\n")
check("nothing above it moving means nothing is painted again",
      len(sc.images) == 1, sc.images)
sc = run(TEXT + UPIC + "console flush\n"
         + UPIC + "console put 3 3 'OVER'\nconsole flush\n"
         "console key 400\n")
check("a cell written over it means the bitmap goes again",
      len(sc.images) == 2 and "OVER" in sc.row(3), (sc.images, sc.row(3)))

shutil.rmtree(D, True)
report(19)
