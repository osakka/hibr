#!/usr/bin/env python3
"""Pictures as pixels the terminal keeps: the kitty graphics protocol.

    python3 tests/kitgfx.py [path-to-hibr]

kitty has never drawn a sixel and says so, which is why this exists: on the
one terminal most likely to be in front of a person, the sixel path put a
bitmap nothing rendered and the console then refused to paint the cells
underneath it -- a blank rectangle. The protocol here is an object, not
paint, and that difference is the whole of what is checked:

  - a picture is one placement with an id of its own, whatever its size,
    chunked at 4096 base64 bytes and ending with m=0;
  - the text around it is text, and the cells it covers are left alone;
  - the same picture again costs nothing;
  - a different picture in the same rectangle *replaces* it, keeping the id,
    rather than deleting and placing again;
  - text drawn through it brings the text back -- and because the terminal
    would otherwise keep showing it, a delete goes out in the same frame;
  - closing the display deletes everything it was holding;
  - a picture that owns its cells is not asked to go below the glyphs, which
    is what z=-1 is for and only a page with a text layer wants;
  - a pane's own corner, and a refusal past its edge;
  - `console gfx` names the protocol, and nothing is sent to a terminal
    that does not say what a cell measures.

The harness keeps APC payloads out of its screen model (Screen.apc,
Screen.images), so a check asks what was asked for rather than grepping the
raw stream.
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
D = tempfile.mkdtemp(prefix="hibr-kitgfx-")
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


def run(body, cell=CELL, gfx="kitty", rows=24, cols=60, settle=1.0):
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


def key(ctrl, k):
    """One key's value out of an APC control string: key("Ga=T,i=7", "i")."""
    for f in ctrl.lstrip("G").split(","):
        if f.startswith(k + "="):
            return f[len(k) + 1:]
    return None


def places(sc):
    """The control strings of the placements, in order."""
    return [a for a in sc.apc if "a=T" in a]


def deletes(sc):
    """The control strings of the deletes, in order."""
    return [a for a in sc.apc if "a=d" in a]


TEXT = ("console pen '#ffffff' '#000000'\n"
        "console put 0 0 'over the picture'\n"
        "console put 11 0 'under the picture'\n")
PIC = "img draw %s 2 2 6 12 %s\n" % (GRAD, "%s")

sc = run(TEXT + PIC % "-m pixels" + "console flush\nconsole key 400\n")
p = places(sc)
check("a picture is one placement, at the corner it was put",
      len(p) == 1 and len(sc.images) == 1 and sc.images[0][:2] == (2, 2),
      (p, sc.images))
check("with the pixels it holds, the cells it fills, and an id of its own",
      len(p) == 1 and key(p[0], "s") == "96" and key(p[0], "v") == "96" and
      key(p[0], "c") == "12" and key(p[0], "r") == "6" and
      (key(p[0], "i") or "0").isdigit() and int(key(p[0], "i")) > 0, p)
check("asking for no reply and leaving the cursor where it was",
      len(p) == 1 and key(p[0], "q") == "2" and key(p[0], "C") == "1" and
      key(p[0], "f") == "24", p)
check("a large picture is chunked, and only the last chunk says m=0",
      len([a for a in sc.apc if "m=1" in a]) > 1 and
      len([a for a in sc.apc if "m=0" in a]) == 1 and sc.apc[-2].endswith("m=0"),
      sc.apc[:3] + sc.apc[-2:])
check("the payload is every byte of the rectangle, base64",
      sc.images[0][2] == (96 * 96 * 3 + 2) // 3 * 4, sc.images)
check("and the text around it is drawn as text",
      sc.row(0).startswith("over the picture") and
      sc.row(11).startswith("under the picture"), (sc.row(0), sc.row(11)))
check("the cells under it are left alone, not painted over",
      sc.row(3).strip() == "", repr(sc.row(3)))
check("nothing is deleted while it is still there",
      len(deletes(sc)) == 1 and "d=A" in deletes(sc)[0], deletes(sc))

sc = run(TEXT + PIC % "-m pixels" + "console flush\n" + PIC % "-m pixels" +
         "console flush\n" + PIC % "-m pixels" + "console flush\nconsole key 400\n")
check("the same picture again costs nothing: one placement for three flushes",
      len(places(sc)) == 1, places(sc))

sc = run(TEXT + PIC % "-m pixels" + "console flush\n"
         "img draw %s 2 2 6 12 -m pixels\nconsole flush\nconsole key 400\n" % FLAT)
p = places(sc)
check("a different picture in the same rectangle replaces it, keeping the id",
      len(p) == 2 and key(p[0], "i") == key(p[1], "i"), p)
check("so nothing is deleted for the replacement",
      len(deletes(sc)) == 1 and "d=A" in deletes(sc)[0], deletes(sc))

sc = run(TEXT + PIC % "-m pixels" + "console flush\n"
         "console put 3 3 'THROUGH'\nconsole flush\nconsole key 400\n")
d = [a for a in deletes(sc) if "d=I" in a]
check("text drawn through it brings the text back",
      "THROUGH" in sc.row(3), sc.row(3))
check("and the picture is deleted, or the terminal would still show it",
      len(d) == 1 and key(d[0], "i") == key(places(sc)[0], "i"),
      (d, places(sc)))

sc = run(TEXT + PIC % "-m pixels" + "console flush\nconsole key 400\n")
check("closing the display deletes everything it was holding",
      any("d=A" in a for a in deletes(sc)), sc.apc)

sc = run(TEXT + PIC % "-m pixels" + "console flush\nconsole key 400\n", cell=(0, 0))
check("a terminal that does not say what a cell measures gets cells instead",
      not sc.apc and not sc.images and "▀" in sc.row(3),
      (sc.apc, repr(sc.row(3))))
sc = run(TEXT + PIC % "-m pixels" + "console flush\nconsole key 400\n", gfx="off")
check("and one told not to draw pictures gets cells too",
      not sc.apc and "▀" in sc.row(3), (sc.apc, repr(sc.row(3))))

# Only the delete-all on the way out is left, which every session sends.
sc = run(TEXT + PIC % "-m mono" + "console flush\nconsole key 400\n")
check("mono stays cells, whatever the terminal can do",
      not places(sc) and "▀" in sc.row(3), (sc.apc, repr(sc.row(3))))

sc = run("console pane w1 5 10 8 20\n"
         "img draw %s 1 1 4 8 -m pixels -p w1\n"
         "console flush\nconsole key 400\n" % GRAD)
check("in a pane it goes at the pane's own corner",
      len(sc.images) == 1 and sc.images[0][:2] == (6, 11), sc.images)
sc = run("console pane w1 5 10 8 20\n"
         "img draw %s 1 1 40 80 -m pixels -p w1\n"
         "console flush\nconsole key 400\n" % GRAD)
check("and past the pane's edge it is refused, not drawn outside it",
      not sc.images, sc.images)

sc = run("g := console gfx\nconsole put 0 0 \"[$g]\"\nconsole flush\n"
         "console key 400\n")
check("console gfx names the protocol and what a cell measures",
      "[kitty 8 16]" in sc.row(0), sc.row(0))
sc = run("g := console gfx\nconsole put 0 0 \"[$g]\"\nconsole flush\n"
         "console key 400\n", gfx="sixel")
check("and says sixel where that is what is in force",
      "[sixel 8 16]" in sc.row(0), sc.row(0))

sc = run(TEXT + PIC % "-m pixels" + "console flush\nconsole key 400\n")
check("a picture that owns its cells is not asked to go under the text",
      len(places(sc)) == 1 and key(places(sc)[0], "z") is None, places(sc))

# A picture under the text (img draw -u, which the wallpaper uses): text in
# the *same* frame would otherwise drop it before it was ever sent, since a
# region that owns its cells is gone the moment they change. Under the text
# it owns none, goes out first, and asks to be drawn below the glyphs -- and
# because the terminal composites it there, nothing re-sends it when the text
# above it moves. That is the whole difference from sixel, which has to paint
# again (tests/sixel.py has that side).
UPIC = "img draw %s 2 2 6 12 -m pixels -u\n" % GRAD
sc = run(TEXT + UPIC + "console put 3 3 'OVER'\nconsole flush\n"
         "console key 400\n")
p = places(sc)
check("a picture under the text survives text drawn over it in one frame",
      len(p) == 1 and key(p[0], "z") == "-1", (p, sc.row(3)))
check("and the text is there, on top of it", "OVER" in sc.row(3), sc.row(3))
sc = run(TEXT + UPIC + "console flush\n"
         + UPIC + "console put 3 3 'OVER'\nconsole flush\n"
         + UPIC + "console put 1 0 'AGAIN'\nconsole flush\nconsole key 400\n")
check("placed again each frame it is sent once, whatever moves above it",
      len(places(sc)) == 1 and "AGAIN" in sc.row(1), (places(sc), sc.row(1)))
sc = run(TEXT + UPIC + "console flush\n"
         "console put 1 0 'GONE'\nconsole flush\nconsole key 400\n")
check("and the frame that stops placing it is what takes it away",
      len(places(sc)) == 1 and any("a=d" in a and "d=I" in a
                                   for a in sc.apc), sc.apc)

# The scaled pixels are kept (Gitea #108). The cell path has had a cache for
# releases and the pixel path had none at all, so a wallpaper -- which must
# place its picture again every frame, since a region under the text is kept
# only while its caller keeps placing it -- decoded and resampled the whole
# picture every time: 330 ms a call for a 3840x2160 photograph, and a desktop
# in Pixels cost 93% of a core to draw 2.7 frames a second. A picture big
# enough for the decode to show, drawn twice: the second must be a fraction
# of the first. The margin measured is about twentyfold, so a third is a
# threshold that survives a full parallel run and the sanitizers.
BIG = png("big.png", 900, 900, lambda x, y: ((x * 7) % 256, (y * 3) % 256, 40))
TIMES = os.path.join(D, "times")
sc = run("a=${EPOCHREALTIME/./}\n"
         "img draw %s 0 0 20 40 -m pixels -u\n"
         "b=${EPOCHREALTIME/./}\n"
         "img draw %s 0 0 20 40 -m pixels -u\n"
         "c=${EPOCHREALTIME/./}\n"
         "echo \"$((b - a)) $((c - b))\" > %s\n" % (BIG, BIG, TIMES))
first, again = (int(x) for x in open(TIMES).read().split())
check("the scaled pixels are kept, so placing the same picture again is cheap",
      again * 3 < first, "%d us then %d us" % (first, again))

# A rectangle that hangs off the screen is cropped to it, not refused: a zoom
# or a center wallpaper is meant to overflow, and until 0.99.95 the display
# returned 0 for one, so img fell back to half-block cells and neither mode
# had ever been pixels (Gitea #117). Cropped, not squashed -- the placement
# must be the visible rectangle at the visible corner, and the pixels it
# carries must be that rectangle's, so the part off the left edge is gone
# rather than scaled into view.
sc = run("img draw %s 0 -4 6 12 -m pixels\nconsole flush\nconsole key 400\n"
         % GRAD)
p = places(sc)
check("a picture hanging off the left edge is cropped to the screen, not refused",
      len(p) == 1 and len(sc.images) == 1 and sc.images[0][:2] == (0, 0), (p, sc.images))
check("and carries only the cells and pixels that are on screen",
      len(p) == 1 and key(p[0], "c") == "8" and key(p[0], "r") == "6" and
      key(p[0], "s") == "64" and key(p[0], "v") == "96", p)
# Off the bottom as well, which is the shape dt_wallfit's zoom produces: the
# fitted height is raised past DT_ROWS on purpose.
sc = run("img draw %s 20 0 8 12 -m pixels\nconsole flush\nconsole key 400\n"
         % GRAD)
p = places(sc)
check("one hanging off the bottom is cropped the same way",
      len(p) == 1 and key(p[0], "r") == "4" and key(p[0], "v") == "64" and
      sc.images[0][:2] == (20, 0), (p, sc.images))
# And it is still one region from one frame to the next: the rectangle the
# caller asked for is what names it, so a cropped picture placed again is the
# one already there rather than a new id with a delete owed for the old.
sc = run("img draw %s 0 -4 6 12 -m pixels\nconsole flush\n"
         "img draw %s 0 -4 6 12 -m pixels\nconsole flush\nconsole key 400\n"
         % (GRAD, GRAD))
check("a cropped picture placed again is the same region, not a new one",
      len(places(sc)) == 1 and
      len([a for a in deletes(sc) if "d=I" in a]) == 0,
      (places(sc), deletes(sc)))

shutil.rmtree(D, True)
report(32)
