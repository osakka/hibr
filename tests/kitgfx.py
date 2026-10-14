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
import base64
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


def run(body, cell=CELL, gfx="kitty", rows=24, cols=60, settle=1.0,
        bg=None, pal=None, raw=False, caps=None, term=None):
    """Run a console script on a pty that says what a cell measures -- and,
    given bg, on one that answers a colour query the way a real terminal
    does. raw answers the bytes as well as the screen."""
    p = os.path.join(D, "s.hibr")
    open(p, "w").write("%sconsole open\n%sconsole close\n"
                       % (load("console", "img"), body))
    env = {} if gfx is None else {"HIBR_GFX": gfx}
    if term:
        env["TERM"] = term
    t = Term(p, rows=rows, cols=cols, settle=settle,
             cellw=cell[0], cellh=cell[1], env=env, bg=bg, pal=pal,
             caps=caps)
    t.collect(0.8)
    sc = t.screen()
    out = t.out
    t.quit(None, 0.5)
    return (sc, out) if raw else sc


def key(ctrl, k):
    """One key's value out of an APC control string: key("Ga=T,i=7", "i")."""
    for f in ctrl.lstrip("G").split(","):
        if f.startswith(k + "="):
            return f[len(k) + 1:]
    return None


def bg(sc, r, c):
    """A cell's background as (r, g, b), or None when it has none of its own
    -- which is the whole contract a wallpaper rests on: a cell with no
    background colour is one the terminal composites the picture through."""
    m = re.search(r"48;2;(\d+);(\d+);(\d+)", sc.p[r][c] or "")
    return tuple(int(x) for x in m.groups()) if m else None


def places(sc):
    """The control strings of the placements, in order."""
    return [a for a in sc.apc if "a=T" in a]


def deletes(sc):
    """The control strings of the deletes, in order."""
    return [a for a in sc.apc if "a=d" in a]


def payload(sc, i=0):
    """The bytes of one picture's payload, base64 undone."""
    return base64.b64decode(sc.imgdata[i]) if len(sc.imgdata) > i else b""


def unpng(d):
    """The pixels of a PNG, decoded here rather than by a library: the
    chunks, one zlib stream across every IDAT, then each scanline's filter
    undone. Our own writer is on the other side of this, so a library would
    only be checking that libpng and we agree -- this checks the format."""
    assert d[:8] == b"\x89PNG\r\n\x1a\n", "not a PNG"
    i, idat, w, h = 8, b"", 0, 0
    while i < len(d):
        n = struct.unpack(">I", d[i:i + 4])[0]
        tag, body = d[i + 4:i + 8], d[i + 8:i + 8 + n]
        got = struct.unpack(">I", d[i + 8 + n:i + 12 + n])[0]
        assert got == zlib.crc32(tag + body), "chunk %s has a bad CRC" % tag
        if tag == b"IHDR":
            w, h, depth, kind = struct.unpack(">IIBB", body[:10])
            assert (depth, kind) == (8, 2), (depth, kind)
        elif tag == b"IDAT":
            idat += body
        i += 12 + n
    raw, out, prev, bw = zlib.decompress(idat), bytearray(), bytes(w * 3), w * 3
    assert len(raw) == (bw + 1) * h, (len(raw), bw, h)
    for y in range(h):
        f, line = raw[y * (bw + 1)], bytearray(raw[y * (bw + 1) + 1:(y + 1) * (bw + 1)])
        for x in range(bw):
            a = line[x - 3] if x >= 3 else 0
            b = prev[x]
            c = prev[x - 3] if x >= 3 else 0
            if f == 1:
                line[x] = (line[x] + a) & 0xff
            elif f == 2:
                line[x] = (line[x] + b) & 0xff
            elif f == 3:
                line[x] = (line[x] + ((a + b) >> 1)) & 0xff
            elif f == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[x] = (line[x] + (a if pa <= pb and pa <= pc
                                      else b if pb <= pc else c)) & 0xff
            elif f:
                raise AssertionError("filter %d" % f)
        out += line
        prev = bytes(line)
    return w, h, bytes(out)


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
# A picture that owns its cells is sent as its pixels, whatever `imgcomp`
# says: the browser hands its page over on every frame it draws and the
# viewer re-places on every zoom, so compressing one would cost more than
# its bytes save. Only the wallpaper is compressed, and the checks at the
# foot of this file are its.
check("the payload is every byte of the rectangle, base64, and no o=z",
      sc.images[0][2] == (96 * 96 * 3 + 2) // 3 * 4 and
      "o=z" not in places(sc)[0] and "f=24" in places(sc)[0],
      (sc.images, places(sc)))
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
# A picture, not any APC: the display asks the terminal what it draws with
# one of its own now (Gitea #216), so "nothing was sent" is about
# placements rather than about the control strings in the stream.
check("a terminal that does not say what a cell measures gets cells instead",
      not places(sc) and not sc.images and "▀" in sc.row(3),
      (sc.apc, repr(sc.row(3))))
sc = run(TEXT + PIC % "-m pixels" + "console flush\nconsole key 400\n", gfx="off")
check("and one told not to draw pictures gets cells too",
      not places(sc) and "▀" in sc.row(3), (sc.apc, repr(sc.row(3))))

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
# The z is asserted against the protocol's own threshold rather than a
# literal: below INT32_MIN/2 is "drawn under cells with non-default
# background colors", which is the layer a wallpaper wants and the one
# z=-1 was not (Gitea #175).
check("a picture under the text survives text drawn over it in one frame",
      len(p) == 1 and key(p[0], "z") is not None
      and int(key(p[0], "z")) < -1073741824, (p, sc.row(3)))
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
# But a flush with nothing drawn since the last one is the same frame flushed
# twice, not a frame that stopped wanting the picture -- and `wm/shot.hibr`
# ends with `dt_draw; console flush`, where dt_draw has already flushed. Every
# screenshot was therefore taken with the wallpaper deleted from the terminal
# (Gitea #118). A flush that retires nothing must also keep the picture it
# already sent: no second placement either.
sc = run(TEXT + UPIC + "console flush\nconsole flush\nconsole key 400\n")
check("a second flush with nothing drawn between it keeps the picture",
      len(places(sc)) == 1 and not any("d=I" in a for a in sc.apc), sc.apc)

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

# Whether two pictures are the same is decided by hashing them, and since
# 0.99.96 that reads eight bytes at a time for anything over 64 (Gitea #113).
# A single changed pixel must still make it a different picture: these two are
# 96x96 into 6 by 12 cells of 8x16, so img scales them one to one and exactly
# three bytes of 27,648 differ.
FLATA = png("flata.png", 96, 96, lambda x, y: (10, 200, 10))
FLATB = png("flatb.png", 96, 96,
            lambda x, y: (11, 201, 11) if (x, y) == (50, 50) else (10, 200, 10))
sc = run("img draw %s 0 0 6 12 -m pixels\nconsole flush\n"
         "img draw %s 0 0 6 12 -m pixels\nconsole flush\nconsole key 400\n"
         % (FLATA, FLATB))
check("a picture differing by a single pixel is a different picture",
      len(places(sc)) == 2, places(sc))

# A picture under the text is the wallpaper, and every byte of it goes to
# every client that attaches: at 232x71 with an 8x16 cell, full resolution is
# 8.04 MB of base64, which is free locally and thirteen seconds of an ssh
# session at 5 Mbit/s (Gitea #129). Half the pixels each way is a quarter of
# that, scaled back up by the terminal, which is what a background wants.
# Asserted on the payload's own length, since that is the whole point.
# Both runs with compression off: these count payload bytes, and a zlib
# stream's length is not a number a test can predict.
sc = run("console imgcomp off\nconsole imgdetail half\n" + PIC % "-m pixels -u"
         + "console flush\nconsole key 400\n")
half = sc.images[0][2] if sc.images else 0
sc = run("console imgcomp off\nconsole imgdetail full\n" + PIC % "-m pixels -u"
         + "console flush\nconsole key 400\n")
full = sc.images[0][2] if sc.images else 0
check("a picture under the text is sent at half the pixels each way",
      half == (48 * 48 * 3 + 2) // 3 * 4, half)
check("and at every pixel when asked for full detail, four times the bytes",
      full == (96 * 96 * 3 + 2) // 3 * 4 and full == half * 4, (full, half))

# And a picture that is *looked at* -- a film's frame, a page, a photograph
# -- is the opposite case, which the one flag got wrong for the life of it:
# the condition halved anything whose caller had not set DP_IMG_CHOSEN, and
# that flag means "a palette was taken from this picture", not "this is the
# wallpaper". The media module passes no flags, so every video frame went
# out at a quarter of the area its cells cover and the terminal upscaled it
# -- measured on the owner's own session as 408x209 px into a rectangle of
# 816x418, every frame, which is what "pixels still show blocks" was
# (Gitea #228). `auto` is each kind's own answer now.
sc = run("console imgcomp off\nconsole imgdetail auto\n" + PIC % "-m pixels"
         + "console flush\nconsole key 400\n")
look = sc.images[0][2] if sc.images else 0
check("a picture that is looked at goes out at the cell's own resolution",
      look == (96 * 96 * 3 + 2) // 3 * 4, look)
sc = run("console imgcomp off\nconsole imgdetail auto\n" + PIC % "-m pixels -u"
         + "console flush\nconsole key 400\n")
under = sc.images[0][2] if sc.images else 0
check("while the wallpaper beside it is still halved, which is what auto means",
      under == (48 * 48 * 3 + 2) // 3 * 4 and under * 4 == look, (under, look))
# This one guards the invariant rather than the change and says so: half
# was what every picture got before, so it passes against 0.99.187 too.
# It is here because the knob has to keep a way back for a link that
# cannot carry full frames.
sc = run("console imgcomp off\nconsole imgdetail half\n" + PIC % "-m pixels"
         + "console flush\nconsole key 400\n")
check("and `imgdetail half` reaches it, so a slow link has a way back",
      (sc.images[0][2] if sc.images else 0) == (48 * 48 * 3 + 2) // 3 * 4,
      sc.images)

# And compressed, which is what a reattach over a link costs (Gitea #129).
# Three encodings of one picture, each asserted to describe the same pixels:
# that is the only check worth making here, since q=2 means a terminal that
# could not read a payload says nothing at all, and the lengths of two of
# the three are not numbers a test can predict.
#
# Under the text (`-u`), because that is the only picture compressed: the
# wallpaper. A still that owns its cells is not -- the browser hands its
# page over on every frame and the viewer re-places on every zoom, so
# "a still" is not the same question as "a picture that will not change",
# and the check below asserts that too. At half detail, hence 48 by 48.
PLAIN = ("console imgcomp off\nconsole imgdetail half\n" + PIC % "-m pixels -u"
         + "console flush\nconsole key 400\n")
ZL = ("console imgcomp zlib\nconsole imgdetail half\n" + PIC % "-m pixels -u"
      + "console flush\nconsole key 400\n")
PN = ("console imgcomp png\nconsole imgdetail half\n" + PIC % "-m pixels -u"
      + "console flush\nconsole key 400\n")
praw, pz, pp = run(PLAIN), run(ZL), run(PN)
pixels = payload(praw)
check("a still is deflated by default, as the protocol's own o=z",
      "o=z" in places(pz)[0] and "f=24" in places(pz)[0], places(pz))
check("and inflates back to exactly the pixels sent uncompressed",
      zlib.decompress(payload(pz)) == pixels and len(pixels) == 48 * 48 * 3,
      (len(payload(pz)), len(pixels)))
check("and is smaller than them, which is the whole point",
      0 < len(payload(pz)) < len(pixels), (len(payload(pz)), len(pixels)))
check("asked for a PNG it says f=100, and leaves out the size a PNG carries",
      "f=100" in places(pp)[0] and ",s=" not in places(pp)[0] and
      ",v=" not in places(pp)[0] and "o=z" not in places(pp)[0], places(pp))
w, h, px = unpng(payload(pp))
check("and the PNG is a real one -- its chunks, its CRCs, its filters -- "
      "holding those same pixels",
      (w, h) == (48, 48) and px == pixels, (w, h, len(px), len(pixels)))
check("and is smaller again than the deflated form",
      0 < len(payload(pp)) < len(payload(pz)),
      (len(payload(pp)), len(payload(pz))))
check("console imgcomp with no argument says which is in force",
      run("console imgcomp png\nz := console imgcomp\nconsole put 0 0 \"[$z]\"\n"
          "console flush\nconsole key 300\n").row(0).startswith("[png]"), None)

# A picture under the text goes below the cell backgrounds, not merely
# below the glyphs. The protocol has two layers down there and says so: a
# negative z is "drawn under the text", and only one below INT32_MIN/2
# (-1073741824) is "drawn under cells with non-default background colors".
# The desktop's whole wallpaper design is the second -- a window's face is
# painted in a real colour and only the cells the picture should show
# through are blanked -- so at z=-1 every window, the menu bar and every
# dialog had its fill composited away and the glyphs were left floating on
# the picture, which is what a live desktop showed (Gitea #175). The exact
# number matters, so it is asserted rather than merely "negative".
sc = run(PIC % "-m pixels -u" + "console flush\n")
ctrl = places(sc)[0] if places(sc) else ""
z = key(ctrl, "z")
check("a picture under the text is placed below the cell backgrounds, "
      "not just below the glyphs",
      z is not None and int(z) < -1073741824, (z, ctrl))

# And one that owns its cells carries no z at all: it is drawn over the
# text by rights, and a z would put it under the very cells it owns.
sc = run(PIC % "-m pixels" + "console flush\n")
check("while a picture that owns its cells carries no z",
      places(sc) and key(places(sc)[0], "z") is None, places(sc))

# Two bitmaps cannot be composited, and a picture placed over another is the
# one case the "have my cells been drawn through" hash cannot see (Gitea
# #172). The wallpaper picker clears its preview box to spaces every frame
# and then centres a picture in it, so a region placed over already-blank
# cells is hashed over blank cells and finds them blank again next frame --
# unchanged is indistinguishable from changed-to-the-same-value. Two live
# kitty placements were the result, measured at the picker as
# placements=2 deletes=0, which is one wide and one tall preview on screen
# at once.
#
# First the case that must keep working: two pictures that do **not**
# overlap are both live. That is Mail, which draws an inline image per row
# of a laid-out message into one pane -- and the layout reserves an image
# box's rows, measured through `html lines`: a 5-row image at row 2 leaves
# rows 3-7 blank and the next image starts at row 10, so two of them never
# share a cell.
APART = ("img draw %s 2 2 6 12 -m pixels\n"
         "img draw %s 12 2 6 12 -m pixels\n"
         "console flush\n" % (GRAD, GRAD))
sc = run(APART)
# A region's own delete is d=I; the d=A every one of these ends with is
# cn_close's "take them all with you", which is why it is filtered out here
# exactly as the earlier checks in this file do it.
check("two pictures that do not overlap are both placed, and neither goes",
      len(places(sc)) == 2 and
      not [a for a in deletes(sc) if "d=I" in a], (places(sc), deletes(sc)))

# And the picker's own case: a differently-shaped picture over the first.
# Two flushes, so the first is really sent before the second is placed --
# placed in one frame it would never have reached the terminal and there
# would be nothing to delete.
OVER = ("img draw %s 2 2 6 12 -m pixels\n"
        "console flush\n"
        "img draw %s 3 3 12 6 -m pixels\n"
        "console flush\n" % (GRAD, GRAD))
sc = run(OVER)
check("a picture placed over another takes it away, with a delete for it",
      len(places(sc)) == 2 and
      len([a for a in deletes(sc) if "d=I" in a]) == 1,
      (places(sc), deletes(sc)))

# And the guard that keeps 0.99.134 fixed: a picture under the text is
# *below* every one of these, so a window's picture placed over part of the
# wallpaper must not take the wallpaper with it. It is re-placed on the
# second frame because that is what keeps an under-text region at all --
# without it the retire pass would delete it for having been abandoned, and
# this check would pass for the wrong reason.
UNDERKEEP = ("img draw %s 2 2 20 40 -m pixels -u\n"
             "console flush\n"
             "img draw %s 2 2 20 40 -m pixels -u\n"
             "img draw %s 4 4 6 12 -m pixels\n"
             "console flush\n" % (GRAD, GRAD, GRAD))
sc = run(UNDERKEEP)
check("but a picture over the wallpaper leaves the wallpaper alone",
      len(places(sc)) == 2 and
      not [a for a in deletes(sc) if "d=I" in a], (places(sc), deletes(sc)))

# A shadow over a picture wallpaper darkens the *picture*, not the theme's
# own face (Gitea #184). The desktop tells the console what a cell left at
# the terminal's own default colour should darken toward -- `darkdefault`,
# added so a shadow over a terminal's plain uncoloured output darkens at
# all -- and a cell the wallpaper deliberately left default so the picture
# shows through is exactly such a cell. The two met: the shadow painted
# DT_FACE x pct, an opaque near-black, over the photograph. FLAT is one
# colour throughout, so the answer is arithmetic rather than a guess.
SHADOW = ("console darkdefault \"#cbd5e0\" \"#101820\"\n"
          "img draw %s 2 2 10 20 -m pixels -u\n"
          "console pen default default\n"
          "console fill 2 2 10 20 \" \"\n"
          "console darken -s 5 5 1 4 50\n"
          "console flush\n" % FLAT)
sc = run(SHADOW)
check("a shadow over a picture wallpaper darkens the picture's own colour",
      bg(sc, 5, 5) == (5, 100, 5), bg(sc, 5, 5))
# And the cell beside it is untouched, which is the other half: the shadow
# must take its own cells and no more, or the whole wallpaper goes opaque
# and the picture is never composited anywhere.
check("and a wallpaper cell outside it still has no background of its own",
      bg(sc, 5, 12) is None, bg(sc, 5, 12))

# A window the wallpaper shows through. A cell has no alpha, so this is the
# picture's own colour scaled -- (10,200,10) at 75% is (8,150,8), which is
# arithmetic rather than a guess -- and it happens on the write, so a cell
# nothing wrote keeps showing the picture itself.
TRANS = ("img draw %s 2 2 10 20 -m pixels -u\n"
         "console translucent 75\n"
         "console pen default default\n"
         "console put 5 5 \"hi\"\n"
         "console translucent off\n"
         "console flush\n" % FLAT)
sc = run(TRANS)
check("a translucent write takes the picture's colour, scaled",
      bg(sc, 5, 5) == (8, 150, 8), bg(sc, 5, 5))
# The same drawing with no picture under it has to come out exactly as it
# did before: that is what lets an app ask for translucency unconditionally
# and still be right on a glyph wallpaper, in half blocks, and on a terminal
# that cannot place a pixel at all.
NOPIC = ("console translucent 75\n"
         "console pen default default\n"
         "console put 5 5 \"hi\"\n"
         "console flush\n")
sc = run(NOPIC)
check("and with no picture under it the cell is left at its default",
      bg(sc, 5, 5) is None, bg(sc, 5, 5))
# And the two compose: a shadow falling on a translucent window finds a real
# colour already there and scales that. This is the case that silently looks
# wrong if the substitution is ever moved to flush time, after the darken.
BOTH = ("img draw %s 2 2 10 20 -m pixels -u\n"
        "console translucent 75\n"
        "console pen default default\n"
        "console put 5 5 \"hi\"\n"
        "console translucent off\n"
        "console darken -s 5 5 1 2 50\n"
        "console flush\n" % FLAT)
sc = run(BOTH)
check("a shadow on a translucent cell darkens what is already there",
      bg(sc, 5, 5) == (4, 75, 4), bg(sc, 5, 5))

# And where there is no picture, a fallback colour is what the cell takes --
# which is what lets an app ask for translucency unconditionally and still
# have the desktop's own face on a glyph wallpaper, rather than falling
# through to whatever the real terminal's background happens to be.
FB = ("console translucent 75 \"#101820\"\n"
      "console pen default default\n"
      "console put 5 5 \"hi\"\n"
      "console flush\n")
sc = run(FB)
check("with no picture a translucent write takes the fallback colour",
      bg(sc, 5, 5) == (16, 24, 32), bg(sc, 5, 5))

# What the terminal says its own background is, and the one cell that has to
# be painted a shade off it (Gitea #215). kitty decides whether a cell has a
# background of its own by comparing the cell's *resolved* value with the
# window's own background colour, so a cell a program painted an explicit
# black, on a black terminal, is a cell the picture is let through -- which
# is what made every ncurses program in a window over a picture wallpaper
# see-through while every window in a theme's own RGB stayed solid. The
# display cannot know that colour, so it asks.
ASK = ("console flush\n"
       "console key 400 > /dev/null\n")
sc, out = run(ASK, bg="#101418", raw=True)
check("the display asks the terminal what its background is",
      b"\x1b]11;?" in out)
check("and asks for the palette entries an index can name",
      b"\x1b]4;0;?" in out and b"\x1b]4;15;?" in out)
# And this one only means anything beside the two above: a release that
# never asked would pass it for nothing, since nothing would answer. So it
# reads both halves -- the answer arrived (the display knows the colour)
# and no part of it reached the screen as text.
sc = run("console key 400 > /dev/null\n"
         "b := console termbg\n"
         "console put 1 1 \"$b\"\n"
         "console flush\n", bg="#101418")
check("the answer is taken as an answer, never typed in as a key",
      "answered" in sc.row(1) and "rgb" not in sc.text()
      and "]11" not in sc.text(), sc.row(1))

# The answer is what it then compares a cell's background against.
TBG = ("console key 400 > /dev/null\n"
       "b := console termbg\n"
       "console put 1 1 \"$b\"\n"
       "console flush\n")
sc = run(TBG, bg="#101418")
check("and it keeps what the terminal answered",
      sc.row(1).strip() == "#101418 answered", sc.row(1))
sc = run(TBG)
check("a terminal that answers nothing leaves black assumed",
      sc.row(1).strip() == "#000000 assumed", sc.row(1))

# So over a picture drawn under the text, that one colour is written a shade
# off -- the same colour to look at, and not the one the terminal compares
# against. Everything else is left exactly as it was asked for.
SAME = ("console key 400 > /dev/null\n"
        "img draw %s 2 2 10 20 -m pixels -u\n"
        "console pen \"#ffffff\" \"#101418\"\n"
        "console put 5 5 \"hi\"\n"
        "console pen \"#ffffff\" \"#202428\"\n"
        "console put 6 5 \"hi\"\n"
        "console flush\n" % FLAT)
sc = run(SAME, bg="#101418")
check("a cell painted the terminal's own colour over a picture is a shade off",
      bg(sc, 5, 5) == (16, 20, 25), bg(sc, 5, 5))
check("and any other colour is written exactly as asked",
      bg(sc, 6, 5) == (32, 36, 40), bg(sc, 6, 5))

# Only over a picture: with nothing under it the cell is the colour it was
# given, since there is nothing for the terminal to let through.
NOPIC215 = ("console key 400 > /dev/null\n"
            "console pen \"#ffffff\" \"#101418\"\n"
            "console put 5 5 \"hi\"\n"
            "console flush\n")
sc = run(NOPIC215, bg="#101418")
check("with no picture under it the same colour is left alone",
      bg(sc, 5, 5) == (16, 20, 24), bg(sc, 5, 5))

# And a cell with no background at all is still let through, which is the
# whole reason the wallpaper is drawn with one.
THRU = ("console key 400 > /dev/null\n"
        "img draw %s 2 2 10 20 -m pixels -u\n"
        "console pen default default\n"
        "console put 5 5 \"hi\"\n"
        "console flush\n" % FLAT)
sc = run(THRU, bg="#101418")
check("a cell with no background of its own is still let through",
      bg(sc, 5, 5) is None, bg(sc, 5, 5))

# An index is the case that was reported: an ncurses program paints palette
# 0, not an RGB value, and what that resolves to is the terminal's own
# business -- so the comparison is against what *it* said the index is.
IDX = ("console key 400 > /dev/null\n"
       "img draw %s 2 2 10 20 -m pixels -u\n"
       "console pen white black\n"
       "console put 5 5 \"hi\"\n"
       "console flush\n" % FLAT)
pal = ["#101418"] + ["#800000"] * 15
sc = run(IDX, bg="#101418", pal=pal)
check("an index the terminal resolves to its own background is a shade off",
      bg(sc, 5, 5) == (16, 20, 25), bg(sc, 5, 5))
pal = ["#2b2b2b"] + ["#800000"] * 15
sc = run(IDX, bg="#101418", pal=pal)
check("and one it resolves to anything else is left an index",
      bg(sc, 5, 5) is None and "48;5;0" in (sc.p[5][5] or ""), sc.p[5][5])

# And a translucent write's own colour is the picture's, scaled, so it can
# land on the terminal's background by arithmetic rather than by anybody
# choosing it -- a black patch of a photograph under a window's shadow
# scales to black. FLAT is (10, 200, 10) and 75% of it is (8, 150, 8), so a
# terminal whose background is exactly that is the case, and the cell has to
# come out a shade off rather than let the picture through at full strength.
TRANSSAME = ("console key 400 > /dev/null\n"
             "img draw %s 2 2 10 20 -m pixels -u\n"
             "console translucent 75\n"
             "console pen default default\n"
             "console put 5 5 \"hi\"\n"
             "console translucent off\n"
             "console flush\n" % FLAT)
sc = run(TRANSSAME, bg="#089608")
check("a translucent write that lands on the terminal's colour is a shade off",
      bg(sc, 5, 5) == (8, 150, 9), bg(sc, 5, 5))

# What the terminal can *draw*, asked rather than read off its name (Gitea
# #216). A name list answers what a family usually does, where the question
# is what this terminal does -- and inside `hold` the name is the session's
# own, frozen when it was started, so a client reattaching from another
# machine could never change it.
GFX = ("console key 400 > /dev/null\n"
       "console flush\n"
       "g := console gfx\n"
       "console put 1 1 \"$g\"\n"
       "console flush\n")


def gfxof(**kw):
    """What `console gfx` answers once the terminal has had its say."""
    return run(GFX, gfx=None, **kw).row(1).strip()


sc, out = run(GFX, gfx=None, caps=["kitty"], raw=True)
check("the display asks the terminal what it draws, both ways",
      b"a=q" in out and b"\x1b[c" in out, out[:0])
check("a terminal that answers kitty's own query draws kitty pictures, "
      "whatever its name says", sc.row(1).strip().startswith("kitty"),
      sc.row(1))
# Both halves, or a release that asked nothing would pass this for nothing:
# the answer was taken as an answer (the protocol followed it) and no part
# of it reached the screen as text.
check("and nothing of the answer is typed in as a key",
      sc.row(1).strip().startswith("kitty") and "OK" not in sc.text()
      and "62;" not in sc.text(), sc.row(1))
check("one that answers device attributes with 4 draws sixels",
      gfxof(caps=["sixel"]).startswith("sixel"))
# The negative is the half a name list cannot give, and it is what stops a
# session following the terminal it was started from into a terminal that
# draws nothing: an attributes reply with no 4, and no kitty OK ahead of it,
# is a terminal that draws neither -- said by the terminal rather than
# guessed.
check("one that answers and draws neither is believed over its own name",
      gfxof(caps=[], term="xterm-kitty") == "none 8 16",
      gfxof(caps=[], term="xterm-kitty"))
check("and over a name that claims sixel",
      gfxof(caps=[], term="foot") == "none 8 16",
      gfxof(caps=[], term="foot"))
# A terminal that says nothing at all is still read by name, which is what
# the name list is for now -- and HIBR_GFX is a person's own choice and wins
# over anything a terminal says about itself.
check("a terminal that answers nothing is still read by its name",
      gfxof(caps=None, term="xterm-kitty").startswith("kitty"),
      gfxof(caps=None, term="xterm-kitty"))
check("and an explicit HIBR_GFX wins over what the terminal answered",
      run(GFX, gfx="sixel", caps=["kitty"]).row(1).strip().startswith("sixel"),
      run(GFX, gfx="sixel", caps=["kitty"]).row(1))

shutil.rmtree(D, True)
report(77)
