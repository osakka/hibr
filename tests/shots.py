#!/usr/bin/env python3
"""tools/screenshot.py: the grid it reconstructs, and the pictures it draws.

    python3 tests/shots.py [path-to-hibr]

The tool's claim is a strong one -- every picture in the documentation is a
real render of real output -- so what it must never do is put something on
the screen that the terminal never showed. Until 0.99.148 it did: its escape
skip advanced two bytes whatever the escape was, so an APC's base64 and the
harness's own idle marker were fed to the grid as text (Gitea #181).

These feed it streams with those escapes in and read the grid back, and
drive one real session with a picture wallpaper to see the bitmap arrive.
Needs Pillow, which the tool needs too; skips, saying so, without it.
"""
import base64
import importlib.util
import os
import sys
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import screen as sx
from screen import check, report, tree, Term, load

try:
    from PIL import Image  # noqa: F401
except ImportError:
    print("shots: no Pillow here, the screenshot tool cannot run")
    print("0 passed, 0 failed")
    sys.exit(0)

if len(sys.argv) > 1:
    sx.HIBR = os.path.abspath(sys.argv[1])

spec = importlib.util.spec_from_file_location("shot", tree("tools/screenshot.py"))
shot = importlib.util.module_from_spec(spec)
spec.loader.exec_module(shot)


def grid(stream, rows=4, cols=40):
    cs = shot.ColourScreen(rows, cols)
    cs.feed(stream)
    return cs, ["".join(c.ch for c in cs.g[r]).rstrip() for r in range(rows)]


# The harness's own idle marker is an OSC, and the desktop sends one per
# frame under HIBR_TESTIDLE. Skipping two bytes leaves `7777;idle;0;4` in
# the grid -- measured, before this was fixed, as a Clock window reading
# `03:29:537777;idle;0;4` where the time should be.
cs, rows = grid("\x1b[1;1Hhello\x1b]7777;idle;0;4\x07\x1b[2;1Hworld")
check("the idle marker is not text, and does not reach the grid",
      rows[0] == "hello" and rows[1] == "world" and "7777" not in "".join(rows), rows)

# An OSC may end with ST rather than BEL, and OSC 52 -- a copy -- carries
# base64 that would read as text just as happily.
cs, rows = grid("\x1b[1;1Hone\x1b]52;c;aGVsbG8=\x1b\\\x1b[2;1Htwo")
check("an OSC ending in ST is skipped whole, payload and all",
      rows[0] == "one" and rows[1] == "two" and "aGVsbG8" not in "".join(rows), rows)

# A kitty transmission: the control, then base64 of a 2x1 picture, then the
# text that follows it. The grid must hold only the text, and the picture
# must come back as a picture.
raw = bytes([255, 0, 0, 0, 0, 255])
pay = base64.b64encode(raw).decode()
cs, rows = grid("\x1b[1;1H\x1b_Ga=T,f=24,q=2,i=9,s=2,v=1,c=2,r=1;%s\x1b\\\x1b[2;1Hafter" % pay)
pics = cs.pictures()
check("a kitty picture stays out of the grid and comes back as one",
      rows[0] == "" and rows[1] == "after" and len(pics) == 1
      and pics[0][2].size == (2, 1) and pics[0][2].getpixel((0, 0)) == (255, 0, 0)
      and pics[0][4] == (2, 1), (rows, pics))

# A picture larger than 4096 bytes arrives in chunks and only the first
# carries the control; a renderer that takes each chunk for its own picture
# draws the first few rows and throws the rest away.
half = pay[:4], pay[4:]
cs, rows = grid("\x1b[1;1H\x1b_Ga=T,f=24,q=2,i=9,s=2,v=1,c=2,r=1,m=1;%s\x1b\\"
                "\x1b_Gm=0;%s\x1b\\\x1b[2;1Hafter" % half)
pics = cs.pictures()
check("a chunked picture is one picture, joined, not several",
      len(pics) == 1 and pics[0][2].size == (2, 1)
      and pics[0][2].getpixel((1, 0)) == (0, 0, 255), (rows, pics))

# Compressed, which is what the wallpaper is sent as by default.
z = base64.b64encode(zlib.compress(raw)).decode()
cs, _ = grid("\x1b_Ga=T,f=24,q=2,o=z,i=9,s=2,v=1,c=2,r=1;%s\x1b\\" % z)
pics = cs.pictures()
check("a zlib payload is inflated rather than taken for pixels",
      len(pics) == 1 and pics[0][2].getpixel((0, 0)) == (255, 0, 0), pics)

# A sixel is a DCS, and is skipped rather than painted: it has no placement
# to read a rectangle from. What must not happen is its bytes landing in
# the grid.
cs, rows = grid("\x1b[1;1Hbefore\x1bP0;0;0q#0;2;100;0;0#0~~@@vv@@\x1b\\\x1b[2;1Hafter")
check("a sixel's own bytes do not reach the grid either",
      rows[0] == "before" and rows[1] == "after" and "~~@@" not in "".join(rows), rows)

# And end to end: a real desktop with a picture wallpaper, through the tool
# itself. Without --cell the terminal reports no cell size and hibr draws
# half blocks, so this also checks the flag is what makes pixels happen.
D = sx.tree("build")
out = os.path.join(D, "shots-wall.png")
wall = os.path.join(D, "shots-wall-src.png")
Image.new("RGB", (64, 40), (200, 40, 90)).save(wall)
sess = os.path.join(D, "shots-sess.hibr")
open(sess, "w").write("%s. %s\nDT_WALLIMG=%s\nDT_WALLMODE=stretch\nDT_IMGMODE=pixels\n"
                      "dt_open\ndt_new \"Hello\" 6 20 4 4\ndt_run\ndt_close\n"
                      % (load("console", "img"), tree("examples/desktop/desktop.hibr"), wall))
t = Term(sess, env={"DT_TICK": "60", "HIBR_GFX": "kitty"}, rows=20, cols=60,
         cellw=9, cellh=18, settle=1.2)
cs = shot.ColourScreen(20, 60)
cs.feed(t.raw.decode("utf-8", "replace"))
t.quit(b"qy", 1.0)
pics = cs.pictures()
check("a session with a picture wallpaper hands the tool a bitmap to draw",
      len(pics) == 1 and pics[0][4] == (60, 20), [(p[0], p[1], p[2].size, p[3], p[4]) for p in pics])
shot.render(cs, out)
im = Image.open(out).convert("RGB")
# A cell the wallpaper shows through: outside the window, below the bar.
px = im.getpixel((2 * 9 + 4, 15 * 18 + 9))
check("and the picture is what the screenshot shows where no window covers it",
      im.size == (540, 360) and abs(px[0] - 200) < 60 and px[0] > px[2], (im.size, px))
rows = ["".join(c.ch for c in cs.g[r]).rstrip() for r in range(20)]
check("with none of its payload anywhere in the text",
      not any("a=T" in r or "7777" in r for r in rows), rows[:3])

report(10)
