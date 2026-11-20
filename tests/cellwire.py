#!/usr/bin/env python3
"""An app's window drawn from its own process: the cell record (ADR 0050).

    python3 tests/cellwire.py [path-to-hibr]

The desktop's end (cellwire.desk.hibr) draws the same app twice, side by
side on one terminal: at the left in-process, its _draw run in the desktop
as today, and at the right from the app's own process (cellwire.app.hibr),
whose console has no terminal and sends the cells that changed as records
the desktop's console applies (`console cells`). Each side is shot with
`console shot -f ansi` -- glyph, colours, attributes, framing and the
underline's colour of every cell -- and the two must be the same file:

  - the Clock, the Calculator and a 24x80 window changing every cell, after
    one frame, after 21, and after a repaint from what the desktop keeps;
  - one cell changed is one cell carried, which a version that sends every
    cell cannot pass;
  - a window over the hosted one, its shadow falling on it;
  - translucency over a picture wallpaper, the window's and the app's own;
  - a window holding a picture, which drawing it from records must not
    take away;
  - a resize and a close while frames are in flight, and an app killed in
    the middle of one: the frame is let go, the connection is not;
  - each of the socket's guards, against a hand-written stream.
"""
import os
import re
import struct
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import screen as sx
from screen import Term, check, report, tree

if len(sys.argv) > 1:
    sx.HIBR = os.path.abspath(sys.argv[1])
HERE = os.path.dirname(os.path.abspath(__file__))
D = tempfile.mkdtemp(prefix="hibr-cellwire-")
SOCK = os.path.join(os.environ.get("TMPDIR", "/tmp"), "cw%d.sock" % os.getpid())


def ppm(path, w, h, px):
    with open(path, "wb") as f:
        f.write(b"P6\n%d %d\n255\n" % (w, h))
        for y in range(h):
            for x in range(w):
                f.write(bytes(px(x, y)))


RUNS = [0]


def run(app, kind, n=21, case="same", env=None, cell=(0, 0), raws=(),
        owns=None):
    """One session: the shot files' prefix, the take log and the screen.
    Each its own prefix, so no session reads another's shots or log."""
    RUNS[0] += 1
    out = os.path.join(D, "%02d-%s-%s-%s" % (RUNS[0], case, app, kind))
    ppm(out + ".wall.ppm", 16, 16, lambda x, y: (200, 120, 40))
    ppm(out + ".pic.ppm", 12, 6, lambda x, y: (x * 20, y * 40, 90))
    for i, r in enumerate(raws):
        open("%s.raw%02d" % (out, i), "wb").write(r)
        if owns and owns.get(i):
            open("%s.raw%02d.own" % (out, i), "w").write(owns[i])
    e = dict(CW_APP=app, CW_KIND=kind, CW_N=str(n), CW_CASE=case,
             CW_DIR=HERE, CW_TREE=tree(""), CW_HIBR=sx.HIBR, CW_OUT=out,
             CW_SOCK=SOCK)
    if cell[0]:
        e["HIBR_GFX"] = "kitty"
    e.update(env or {})
    t = Term(os.path.join(HERE, "cellwire.desk.hibr"), rows=40, cols=200,
             env=e, cellw=cell[0], cellh=cell[1])
    t.wait(120)
    log = open(out + ".log").read() if os.path.exists(out + ".log") else ""
    return out, log, t


def same(out, tag):
    try:
        a = open("%s.A%s" % (out, tag), "rb").read()
        b = open("%s.B%s" % (out, tag), "rb").read()
    except OSError:
        return False
    return len(a) > 100 and a == b


def takes(log, frame=None):
    """Each take's frames, cells, bytes and its rc, in order."""
    out = []
    for m in re.finditer(r"^take (\S+) rc=(\d+) (\d+) (\d+) (\d+)\|(.*)$",
                         log, re.M):
        if frame is None or m.group(1) == str(frame):
            out.append((int(m.group(3)), int(m.group(4)), int(m.group(5)),
                        int(m.group(2)), m.group(6)))
    return out


# Cell for cell, as in-process draws it.
for app in ("clock", "calc", "big"):
    for kind in ("F", "O"):
        out, log, t = run(app, kind)
        check("%s %s: the first frame is cell for cell what in-process draws"
              % (app, kind), same(out, 1), log)
        check("%s %s: so is the 21st" % (app, kind), same(out, 21), log)
        check("%s %s: so is a repaint from what the desktop keeps"
              % (app, kind), same(out, "R"), log)
        if kind == "O":
            ones = [tk[1] for i in range(2, 22) for tk in takes(log, i)
                    if tk[0]]
            check("%s O: a frame that changed one cell carried one cell"
                  % app, len(ones) == 20 and set(ones) == {1}, log)

# The app's own translucency and a link, wide glyphs and a combining mark.
out, log, t = run("big", "F", env={"CW_TS": "1", "CW_LINK": "1"})
check("a link, a wide glyph and a combining mark arrive as drawn",
      same(out, 1) and same(out, 21) and same(out, "R"), log)
opens = [t.raw.count(b"\x1b]8;;https://example.org/%d\x07" % k)
         for k in range(3)]
check("the link reaches the terminal from both sides, frame by frame",
      sum(opens) >= 42 and all(n % 2 == 0 for n in opens), str(opens))

# A window over the hosted one, its shadow on it.
out, log, t = run("big", "F", case="overlap")
check("a window over a hosted one, and its shadow, are as in-process",
      same(out, 1) and same(out, 21) and same(out, "R"), log)

# Translucency over a picture wallpaper: the window's own, and the app's.
out, log, t = run("big", "F", case="trans", cell=(8, 16),
                  env={"CW_TS": "1"})
# And it is translucency: the wallpaper was sent, and backgrounds that are
# neither the face nor the well -- the picture's colour, scaled -- are in
# the window. (Face is there too, rightly: the app's own `console translucent
# off` after its first row makes the rest of its writes opaque.)
sc = t.screen()
shot = open(out + ".B21", "rb").read() if os.path.exists(out + ".B21") else b""
bgs = set(re.findall(rb"48;2;\d+;\d+;\d+", shot)) - {b"48;2;16;24;32",
                                                     b"48;2;45;55;72"}
check("translucency over a picture wallpaper is as in-process",
      same(out, 1) and same(out, 21) and same(out, "R") and
      any("a=T" in c for c in sc.apcall) and len(bgs) >= 2,
      "%s\npictures sent: %d, picture backgrounds: %s" %
      (log, sum("a=T" in c for c in sc.apcall), bgs))

# A window holding a picture: placed once and kept, as in-process.
out, log, t = run("calc", "O", case="pic", cell=(8, 16))
sc = t.screen()
sent = [c for c in sc.apcall if "a=T" in c]
check("a hosted window's picture is sent once and kept over 21 frames",
      same(out, 21) and len(sent) == 2, "%d transmissions" % len(sent))

# A view at a place inside a pane the desktop draws in too, and one of the
# screen whose app drew a picture into its cells through dp->put.
out, log, t = run("clock", "O", case="place")
check("a view drawn into part of a pane the desktop also draws in is as "
      "in-process", same(out, "P"), log)
check("a view of the screen, a picture in its cells through dp->put, is as "
      "in-process", same(out, "S"), log)
check("a second connection is refused another's cells and another's name",
      "own-overlap refused" in log and "own-name refused" in log, log)

# A resize while a frame is in flight: that frame is let go, the next one at
# the new size is whole, and the connection goes on.
out, log, t = run("clock", "F", n=6, case="resize")
st = takes(log, "stale4")
check("a frame drawn before a resize is let go and the connection kept",
      st and all(x[0] == 0 and x[3] == 0 for x in st) and
      sum(x[2] for x in st) > 0, log)
check("the frame after the resize is whole and as in-process",
      same(out, 5) and same(out, 6), log)

# A close while frames are in flight: the closed view's frames are let go,
# the other's go on.
out, log, t = run("clock", "F", n=6, case="close")
check("a closed view's frames are let go, the app's other view goes on",
      same(out, 6) and all(x[3] == 0 for x in takes(log)), log)

# An app killed in the middle of a frame.
out, log, t = run("clock", "F", n=6, case="die")
dies = takes(log, "die")
check("an app killed mid-frame leaves its window as its last whole frame",
      dies and dies[-1][3] == 1 and same(out, "D"), log)


# A held frame, which dt_run waits out with `console key -q 70`: what is
# watched as input ends it, and what a key told to the app draws in answer
# is drawn when it comes -- each against the same wait without it, which
# must sit the hold out, or the check proves nothing.
# The hold is 2 s, the first wait is let settle (the harness's own resize at
# the start ends a wait too), and each side is judged against the hold.
HOLD = 2000


def lats(log):
    v = sorted(int(m.group(2)) for m in re.finditer(r"^lat (\d+) (\d+)", log,
                                                     re.M) if m.group(1) != "1"
               and m.group(1) != "2")
    return v[len(v) // 2] if v else None


out, log, t = run("clock", "O", case="inwatch")
m = lats(log)
woke = re.findall(r"^lat (\d+) \d+ woke=(.*)$", log, re.M)
check("a descriptor watched as input ends a quiet wait, and woke names it",
      m is not None and m < HOLD // 2 and woke and
      all(w for i, w in woke if i != "1"), log)
out, log, t = run("clock", "O", case="inwatch", env={"CW_PLAIN": "1"})
m = lats(log)
check("one watched plainly does not, as before",
      m is not None and m >= HOLD - 100, log)
out, log, t = run("clock", "O", case="echo")
m = lats(log)
check("a key's echo is drawn when it comes, with no hold between",
      m is not None and m < HOLD // 2 and same(out, "E"), log)
out, log, t = run("clock", "O", case="echo", env={"CW_PLAIN": "1"})
m = lats(log)
check("without `console cells input` the echo sits the hold out",
      m is not None and m >= HOLD - 100, log)

# The guards, against a stream written by hand into a 24x80 view.
def guard(raws, owns=None):
    out, log, t = run("big", "F", case="raw", raws=raws, owns=owns)
    return takes(log), log


S = b"w w1 1\ns 24 80\n"
GOOD = S + b"r 0 0 0 0 0 0 0 2 ok\ne\n"
for name, data in (
        ("an unknown letter", b"x 1\n"),
        ("a field that is not a number", b"w w1 1\nr 0 z 0 0 0 0 0 1 a\ne\n"),
        ("a line past CN_CWLINE", b"w w1 1\nr 0 0 0 0 0 0 0 70000 "
         + b"a" * 70000 + b"\ne\n"),
        ("a header past CN_CWLINE", b"w " + b"a" * 70000),
        ("a link it never named", S + b"r 0 0 0 0 0 0 5 1 a\ne\n"),
        ("a translucency it never named",
         S + b"r 0 0 0 0 200000 0 0 1 a\ne\n"),
        ("links past CN_CWLKB", b"".join(
            b"l %d 2000 %s\n" % (i, b"u" * 2000) for i in range(1, 40)))):
    tk, log = guard([data])
    check("%s drops the connection" % name, tk and tk[-1][3] == 2, log)
for name, data in (
        ("a frame past its bound", S + b"r 0 0 0 0 0 0 0 1 a\n" * 120000
         + b"e\n"),
        ("a size that is not the view's", b"w w1 1\ns 23 80\ne\n"),
        ("a change to a grid it does not hold", b"w w1 1\nr 0 0 0 0 0 0 0 1 "
         b"a\ne\n"),
        ("a stale generation", b"w w1 7\ns 24 80\ne\n")):
    tk, log = guard([data, GOOD])
    rcs = [x[3] for x in tk]
    again = name != "a stale generation"
    check("%s is let go, %s, and the next frame applied" %
          (name, "asked for again" if again else "unasked"),
          tk and set(rcs) == {0} and sum(x[0] for x in tk) == 1 and
          (("again w1" in log) == again), log)
# A view bound again at another size keeps what it held until a frame with
# its new size comes: one with no size of its own is let go and asked for
# whole, never applied to a grid of the old size -- past its end, growing.
for name, size in (("larger", "2 30 100"), ("smaller", "2 10 40")):
    tk, log = guard([GOOD, b"w w1 2\nr 9 0 0 0 0 0 0 1 a\nr 28 99 0 0 0 0 0 "
                     b"1 a\ne\n", b"w w1 2\ns %s\nr 0 0 0 0 0 0 0 2 ok\ne\n"
                     % size.split(" ", 1)[1].encode()], {1: size})
    check("a view bound again %s has its next frame with no size let go and "
          "asked for again, then a whole one applied" % name,
          tk and set(x[3] for x in tk) == {0} and
          sum(x[0] for x in tk) == 2 and "again w1" in log, log)
tk, log = guard([GOOD * 1, (b"w w1 1\n" + b"r 0 0 0 0 0 0 0 78 " + b"b" * 78
                            + b"\ne\n") * 8000])
by = [x[2] for x in tk if x[2]]
check("a pass reads at most CN_CWPASS and leaves the rest for the next",
      len(by) >= 3 and max(by) <= 262144 + 4096 and
      sum(x[0] for x in tk) == 8001, log)

report(52)
