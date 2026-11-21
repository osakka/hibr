#!/usr/bin/env python3
"""A picture across the seam between two displays shows on both, each its
own part of it (Gitea #492).

The owner's tall screen is two Blit sessions, one above the other, so a
held desktop has two displays at different rows -- and hold skipped any
picture that did not fit one display whole, so a window over the seam
showed its picture on neither. Blit honours a kitty source rectangle on a
placement (its ADR 0060), so hold now gives each display the slice of the
picture on it. Here: two stacked displays with stand-in Blits, and a held
program showing two pictures side by side in one row -- a stream placed by
descriptor (the Steam window's kind) and an ordinary transmitted one (a
PNG through img) -- moved from the top display, across the seam, wholly
into the bottom one and back. Every check reads what crossed a socket or a
terminal. Then a program window's subsurfaces drawn through their views
across the same seam (Gitea #600), and three displays whose grids grow and
shrink as a font changes, which must keep their arrangement (Gitea #409).
"""
import os, re, shutil, struct, subprocess, sys, time, zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import screen
from screen import Term, check, report, load

HERE = os.path.dirname(os.path.abspath(__file__))
src = open(os.path.join(HERE, "frames.py")).read()
exec(compile(src[:src.index("# Nothing between")], "frames.py", "exec"))


def png(name, w, h, f):
    """A PNG of w by h, each pixel from f(x, y)."""
    raw = b"".join(b"\x00" + bytes(v for x in range(w) for v in f(x, y))
                   for y in range(h))

    def chunk(tag, data):
        body = tag + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body))

    path = os.path.join(D, name)
    open(path, "wb").write(b"\x89PNG\r\n\x1a\n"
                           + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
                           + chunk(b"IDAT", zlib.compress(raw))
                           + chunk(b"IEND", b""))
    return path


GRAD = png("grad.png", 48, 48, lambda x, y: (x * 5 % 256, y * 5 % 256, 90))
out = os.path.join(D, "s")
prog = os.path.join(D, "seam-%d.hibr" % os.getpid())
open(prog, "w").write(
    "need console; need frames; need img\n"
    "console open\n"
    "[ -n \"${HIBR_HOLD-}\" ] && console imgfd hold\n"
    "x := frames serve\n"
    "read -r n sock tok <<< \"$x\"\n"
    "printf '%%s %%s\\n' \"$sock\" \"$tok\" > %(o)s.sock\n"
    "while [ ! -f %(o)s.stop ]; do\n"
    "  r=2; [ -f %(o)s.at ] && read -r r < %(o)s.at\n"
    "  console pane w1 $r 2 10 26\n"
    "  console put -p w1 0 0 STREAM\n"
    "  frames place $n w1 1 1 6 20\n"
    "  console pane w2 $r 30 10 20\n"
    "  img draw %(g)s 1 1 6 12 -m pixels -p w2\n"
    "  console flush\n"
    "  console key 300 > /dev/null\n"
    "done\n"
    "frames stop $n\n"
    "console flush\n" % {"o": out, "g": GRAD})

ph0, lg0, e0 = blit("t")
ph1, lg1, e1 = blit("b")
nm = "hsm-%d" % os.getpid()
env = {"HIBR_GFX": "kitty", "TMPDIR": D}
a0 = os.path.join(D, "a0.hibr")
open(a0, "w").write(load("hold") + 'hold new -d %s "$HIBR" %s\nsleep 0.6\n'
                    'hold attach -m -n top %s 0 0\n' % (nm, prog, nm))
t0 = Term(a0, rows=12, cols=60, settle=1.0, cellw=8, cellh=16,
          env=dict(env, **e0), caps=["kitty"])
t0.collect(1.0 * SLOW)
a1 = os.path.join(D, "a1.hibr")
open(a1, "w").write(load("hold") + 'hold attach -m -n bottom %s 12 0\n' % nm)
t1 = Term(a1, rows=12, cols=60, settle=1.0, cellw=8, cellh=16,
          env=dict(env, **e1), caps=["kitty"])
t1.collect(1.0 * SLOW)
so = wait(lambda: sockof(out), 5)
br = Bridge(*so) if so else None


def ctl(t, since):
    """Every kitty control string t was sent after byte since, without its
    payload: what was placed, cut or deleted."""
    t.collect(0.3)
    raw = t.out[since:].decode("latin-1")
    return [m.group(1) for m in re.finditer(r"\x1b_G([^;\x1b]*)", raw)]


def keys(c):
    return dict(kv.split("=", 1) for kv in c.split(",") if "=" in kv)


def placed(cs, which):
    """The placements of one of the two pictures: the stream, whose
    placement is held (p=), or the PNG, whose is not."""
    return [keys(c) for c in cs if keys(c).get("a") in ("p", "T")
            and ("p" in keys(c)) == (which == "stream")]


def at(r):
    open(out + ".at", "w").write("%d\n" % r)


def step(r):
    """Move the row to r, send frames while it settles, and say what each
    display was sent meanwhile."""
    c0, c1 = len(t0.out), len(t1.out)
    f0, f1 = len(frames(lg0)), len(frames(lg1))
    at(r)
    for _ in range(8):
        if br:
            br.send()
        time.sleep(0.15 * SLOW)
    time.sleep(0.6 * SLOW)
    return (ctl(t0, c0), ctl(t1, c1), len(frames(lg0)) - f0,
            len(frames(lg1)) - f1)


s0, s1, _, _ = step(2)
check("wholly on the top display, both pictures go there whole and the "
      "bottom display is sent neither",
      placed(s0, "stream") and "y" not in placed(s0, "stream")[-1]
      and placed(s0, "png") and "y" not in placed(s0, "png")[-1]
      and not placed(s1, "stream") and not placed(s1, "png"), (s0, s1))

s0, s1, f0, f1 = step(8)
p0, p1 = (placed(s0, "stream") or [{}])[-1], (placed(s1, "stream") or [{}])[-1]
check("across the seam, the stream is placed on both displays, each with "
      "its own rows of it", p0.get("y") == "0" and p1.get("y") == p0.get("h")
      and int(p0.get("r", 0)) + int(p1.get("r", 0)) == 6
      and p0.get("c") == "20" and p1.get("c") == "20", (s0, s1))
check("and its slices are cut in its own pixels, the two adding up to the "
      "whole of it", p0.get("w") == "64" and p1.get("w") == "64"
      and int(p0.get("h", 0)) + int(p1.get("h", 0)) == 16, (p0, p1))
check("and its frames reach both displays' Blits", f0 > 0 and f1 > 0, (f0, f1))
t0p, t1p = (placed(s0, "png") or [{}])[-1], (placed(s1, "png") or [{}])[-1]
check("an ordinary picture across the seam, sent through hold's frame "
      "socket, goes to both as well, each cut to its own part", t0p.get("y") == "0" and t1p.get("y")
      and t1p.get("y") == t0p.get("h")
      and int(t0p.get("r", 0)) + int(t1p.get("r", 0)) == 6, (s0, s1))

s0, s1, f0, f1 = step(14)
gone = [keys(c) for c in s0 if keys(c).get("a") == "d"]
check("wholly on the bottom display, it goes there whole, and the top one "
      "takes its placement away but keeps the image, so it can come back",
      placed(s1, "stream") and "y" not in placed(s1, "stream")[-1]
      and any(g.get("d") == "i" and g.get("i") == p0.get("i") for g in gone)
      and not any(g.get("d") == "I" and g.get("i") == p0.get("i") for g in gone),
      (s0, s1))

s0, s1, f0, f1 = step(2)
check("and back on the top display, it is placed there again and its frames "
      "go there again", placed(s0, "stream") and f0 > 0, (s0, f0))

open(out + ".stop", "w").close()
time.sleep(0.5)
if br:
    br.close()
t1.quit(None, 0.3)
t0.quit(None, 0.3)
subprocess.run([screen.HIBR, "-c", load("hold") + "hold kill %s" % nm],
               env=dict(os.environ, **env), capture_output=True)

# The same picture sent in the escape itself, chunked, as a plain kitty
# terminal is sent one: no frame socket, so the program places nothing by
# descriptor and the transmission is what hold has to cut -- its first
# chunk carries the keys, and every chunk after it goes as it came.
out2 = os.path.join(D, "s2")
prog2 = os.path.join(D, "inband-%d.hibr" % os.getpid())
open(prog2, "w").write(
    "need console; need img\n"
    "console open\n"
    "while [ ! -f %(o)s.stop ]; do\n"
    "  r=2; [ -f %(o)s.at ] && read -r r < %(o)s.at\n"
    "  console pane w2 $r 30 10 20\n"
    "  img draw %(g)s 1 1 6 12 -m pixels -p w2\n"
    "  console flush\n"
    "  console key 300 > /dev/null\n"
    "done\n" % {"o": out2, "g": GRAD})
nm2 = "hsn-%d" % os.getpid()
b0 = os.path.join(D, "b0.hibr")
open(b0, "w").write(load("hold") + 'hold new -d %s "$HIBR" %s\nsleep 0.6\n'
                    'hold attach -m -n top %s 0 0\n' % (nm2, prog2, nm2))
u0 = Term(b0, rows=12, cols=60, settle=1.0, cellw=8, cellh=16, env=env,
          caps=["kitty"])
u0.collect(1.0 * SLOW)
b1 = os.path.join(D, "b1.hibr")
open(b1, "w").write(load("hold") + 'hold attach -m -n bottom %s 12 0\n' % nm2)
u1 = Term(b1, rows=12, cols=60, settle=1.0, cellw=8, cellh=16, env=env,
          caps=["kitty"])
u1.collect(1.0 * SLOW)
c0, c1 = len(u0.out), len(u1.out)
open(out2 + ".at", "w").write("8\n")
time.sleep(1.5 * SLOW)


def chunks(t, since):
    """The escapes of each in-band transmission t was sent after since: the
    first one's keys and how many chunks came with it."""
    t.collect(0.3)
    raw = t.out[since:].decode("latin-1")
    found = []
    for m in re.finditer(r"\x1b_G([^;\x1b]*)", raw):
        k = keys(m.group(1))
        if k.get("a") == "T":
            found.append([k, 1])
        elif found and set(k) <= {"m", "q"}:
            found[-1][1] += 1
    return found


n0, n1 = chunks(u0, c0), chunks(u1, c1)
k0, k1 = (n0 or [[{}, 0]])[-1], (n1 or [[{}, 0]])[-1]
check("sent in the escape itself, a picture across the seam reaches both "
      "displays cut, its first chunk carrying the slice and its later "
      "chunks passed on as they came",
      k0[0].get("y") == "0" and k1[0].get("y") == k0[0].get("h")
      and int(k0[0].get("r", 0)) + int(k1[0].get("r", 0)) == 6
      and k0[0].get("m") == "1" and k0[1] > 1 and k0[1] == k1[1], (n0, n1))
open(out2 + ".stop", "w").close()
time.sleep(0.5)
u1.quit(None, 0.3)
u0.quit(None, 0.3)
subprocess.run([screen.HIBR, "-c", load("hold") + "hold kill %s" % nm2],
               env=dict(os.environ, **env), capture_output=True)
for p in (ph0, ph1):
    p.terminate()
    p.wait()

# A subsurface drawn through its view, across the seam (Gitea #600): mpv's
# video at half its buffer's size, and a cropped part of another buffer at
# its own scale -- natserve.py's two, 17 and 18 -- in a window whose body
# straddles the two displays. Each display's Blit (0.3.127, which takes a
# placement's size in pixels) is sent the part of the sub on it: hold cuts
# what is shown at the display's edge and then maps that part through the
# view, so the two slices are adjacent rectangles of the buffer, each drawn
# at the height it shows. Worked out on paper, with 8 by 16 cells: the body
# starts at row 11, column 3 (176, 24 in pixels), and the seam is pixel row
# 192. 17 is shown at 64,184 for 64 by 32 -- 8 rows of it above the seam,
# 24 below -- and its buffer is twice that each way; 18 hangs past the body,
# so 40 by 20 of its 48 by 24 shows, from 8,4 of what it shows, at 24,176 --
# 16 rows above, 4 below -- out of a buffer whose shown part starts at 20,10.
NATSRV = os.path.join(HERE, "natserve.py")
vo = os.path.join(D, "v")
vprog = os.path.join(D, "vsub-%d.hibr" % os.getpid())
open(vprog, "w").write(
    "need console; need frames\n"
    "console open\n"
    "[ -n \"${HIBR_HOLD-}\" ] && console imgfd hold\n"
    "x := frames serve -w\n"
    "read -r n sock tok <<< \"$x\"\n"
    "printf '%%s %%s\\n' \"$sock\" \"$tok\" > %(o)s.sock\n"
    "top= subs=()\n"
    "while [ ! -f %(o)s.stop ]; do\n"
    "  e := frames events $n 2> /dev/null\n"
    "  while read -r verb m rest; do\n"
    "    case $verb in\n"
    "    new) top=$m; frames window $m size 160 96 ;;\n"
    "    sub) read -r p sx sy sw sh ab <<< \"$rest\"; subs+=($m)\n"
    "         frames input $m member $p $sx $sy ;;\n"
    "    esac\n"
    "  done <<< \"$e\"\n"
    "  r=2; [ -f %(o)s.at ] && read -r r < %(o)s.at\n"
    "  console pane w1 $r 2 8 22\n"
    "  if [ -n \"$top\" ]; then\n"
    "    frames place $top w1 1 1 6 20\n"
    "    for m in \"${subs[@]}\"; do frames place $m w1 1 1 6 20; done\n"
    "  fi\n"
    "  console flush\n"
    "  console key 100 > /dev/null\n"
    "done\n" % {"o": vo})
vb0, vl0, ve0 = blit("vt")
vb1, vl1, ve1 = blit("vb")
vnm = "hsv-%d" % os.getpid()
venv = {"HIBR_GFX": "kitty", "TMPDIR": D, "BLIT_VERSION": "0.3.127"}
v0 = os.path.join(D, "v0.hibr")
open(v0, "w").write(load("hold") + 'hold new -d %s "$HIBR" %s\nsleep 0.6\n'
                    'hold attach -m -n top %s 0 0\n' % (vnm, vprog, vnm))
w0 = Term(v0, rows=12, cols=60, settle=1.0, cellw=8, cellh=16,
          env=dict(venv, **ve0), caps=["kitty"])
w0.collect(1.0 * SLOW)
v1 = os.path.join(D, "v1.hibr")
open(v1, "w").write(load("hold") + 'hold attach -m -n bottom %s 12 0\n' % vnm)
w1 = Term(v1, rows=12, cols=60, settle=1.0, cellw=8, cellh=16,
          env=dict(venv, **ve1), caps=["kitty"])
w1.collect(1.0 * SLOW)
vso = wait(lambda: sockof(vo), 5)
nlog = os.path.join(D, "nat.log")
open(nlog + ".vsub", "w").close()
nat = subprocess.Popen([sys.executable, NATSRV, nlog],
                       env=dict(os.environ, BLIT_FRAME_SOCKET=vso[0],
                                BLIT_FRAME_TOKEN=vso[1])) if vso else None


def subid(lg, x, y):
    """The picture a display's Blit was told is a member at x, y of the
    window's route: a subsurface, under the console's id for it."""
    m = [e for e in lines(lg) if e["kind"] == "member" and (e["x"], e["y"]) == (x, y)]
    return m[-1]["id"] if m else None


def lastnat(t, iid):
    """The keys of the last placement of picture iid t was sent."""
    t.collect(0.2)
    ms = [m.group(1) for m in re.finditer(r"\x1b_G(a=p[^;\x1b]*)", t.out.decode("latin-1"))
          if iid and ",i=%d," % iid in "," + m.group(1) + ","]
    return keys(ms[-1]) if ms else {}


def nums(k, names):
    return [int(k[n]) if n in k else None for n in names]


i17 = wait(lambda: subid(vl0, 40, 8), 8)
i18 = wait(lambda: subid(vl0, -8, -4), 4)
XYWH = ["x", "y", "w", "h", "W", "T", "X", "Y"]
whole = wait(lambda: i17 and "W" in lastnat(w0, i17) and lastnat(w0, i17), 6)
check("wholly on the top display, a subsurface is drawn through its view "
      "there: its whole buffer at the size it is shown",
      whole and nums(whole, XYWH) == [0, 0, 128, 64, 64, 32, None, 8], (i17, whole))
c0, c1 = len(w0.out), len(w1.out)
open(vo + ".at", "w").write("10\n")


def seamed(iid, top, bottom):
    return lambda: (nums(lastnat(w0, iid), XYWH) == top and
                    nums(lastnat(w1, iid), XYWH) == bottom)


s17 = wait(seamed(i17, [0, 0, 128, 16, 64, 8, None, 8],
                  [0, 16, 128, 48, 64, 24, None, None]), 6)
check("across the seam, a subsurface at half its buffer's size is cut where "
      "it is shown and then mapped through its view: the top display draws "
      "its buffer's first 16 rows in the 8 it shows, the bottom one the 48 "
      "after them in its 24", s17,
      (lastnat(w0, i17), lastnat(w1, i17)))
s18 = wait(seamed(i18, [28, 14, 40, 16, 40, 16, None, None],
                  [28, 30, 40, 4, 40, 4, None, None]), 4)
check("and one showing a cropped part of its buffer, cut by the window's "
      "body too: each display draws its own rows of the crop, from the "
      "view's own origin", s18, (lastnat(w0, i18), lastnat(w1, i18)))
# Blit maps the pointer, not hibr (Blit #159): through the placement to the
# buffer, and through the view to the surface. Each display's Blit is told
# the view and the membership under the sub's id, and the point either side
# of the seam then reads the same row of the surface: the last pixel row on
# the top display and the first on the bottom one map to surface rows 7 and
# 8 of the 32 it shows.
vw0 = wait(lambda: [e["view"] for e in lines(vl0) if e["kind"] == "view" and e["id"] == i17], 4)
vw1 = wait(lambda: [e["view"] for e in lines(vl1) if e["kind"] == "view" and e["id"] == i17], 4)
mb1 = subid(vl1, 40, 8)


def surf(k, v, p):
    """Surface row of pixel row p into placement k, as Blit maps it: to the
    buffer through the placement's source rows, then through the view."""
    by = int(k["y"]) + p * int(k["h"]) / int(k["T"])
    return int((by - v[1]) * v[5] / v[3])


k0, k1 = lastnat(w0, i17), lastnat(w1, i17)
check("and both displays' Blits are told the subsurface's view and its "
      "membership, so a click either side of the seam reaches the surface "
      "row it shows",
      vw0 and vw1 and vw0[-1] == vw1[-1] == [0, 0, 128, 64, 64, 32] and mb1 == i17
      and "T" in k0 and "T" in k1
      and surf(k0, vw0[-1], int(k0["T"]) - 1) == 7 and surf(k1, vw1[-1], 0) == 8,
      (vw0, vw1, mb1, k0, k1))
open(vo + ".stop", "w").close()
time.sleep(0.4)
if nat:
    nat.terminate()
    nat.wait()
w1.quit(None, 0.3)
w0.quit(None, 0.3)
subprocess.run([screen.HIBR, "-c", load("hold") + "hold kill %s" % vnm],
               env=dict(os.environ, **venv), capture_output=True)
for p in (vb0, vb1):
    p.terminate()
    p.wait()

# A display's grid that changes size keeps the arrangement (Gitea #409):
# on Blit a smaller font is a bigger grid, and the display it grew into
# went on being drawn where it was, so the top half of a tall screen showed
# a second menu bar and the other display's windows. Three displays as the
# owner's were -- two stacked, one beside both -- grown one after another,
# and shrunk back in another order, must end where they began.
gnm = "hsg-%d" % os.getpid()
genv = {"TMPDIR": D}
g0 = os.path.join(D, "g0.hibr")
open(g0, "w").write(load("hold") + 'hold new -d %s "$HIBR" -c "sleep 120"\nsleep 0.5\n'
                    'hold attach -m -n top %s 0 0\n' % (gnm, gnm))
gt = Term(g0, rows=12, cols=40, settle=1.0, cellw=8, cellh=16, env=genv, caps=[])
gt.collect(1.0 * SLOW)
g1 = os.path.join(D, "g1.hibr")
open(g1, "w").write(load("hold") + 'hold attach -m -n low %s 12 0\n' % gnm)
gp = Term(g1, rows=12, cols=40, settle=1.0, cellw=8, cellh=16, env=genv, caps=[])
g2 = os.path.join(D, "g2.hibr")
open(g2, "w").write(load("hold") + 'hold attach -m -n side %s 0 40\n' % gnm)
gr = Term(g2, rows=30, cols=50, settle=1.0, cellw=8, cellh=16, env=genv, caps=[])


def where():
    """Each display's row, column, rows and columns, by name."""
    r = subprocess.run([screen.HIBR, "-c", load("hold") + "hold clients %s" % gnm],
                       env=dict(os.environ, **genv), capture_output=True, text=True)
    return {w[0]: [int(x) for x in w[1:5]] for w in
            (l.split() for l in r.stdout.splitlines()) if len(w) >= 5}


def sized(t, rows, cols, want):
    t.resize(rows, cols, 8, 16)
    return wait(lambda: (gt.collect(0.05), gp.collect(0.05), gr.collect(0.05),
                         where())[3] == want and where(), 4)


start = wait(lambda: (gt.collect(0.05), gp.collect(0.05), gr.collect(0.05),
                      len(where()))[3] == 3 and where(), 6)
check("three displays, two stacked and one beside both",
      start == {"top": [0, 0, 12, 40], "low": [12, 0, 12, 40], "side": [0, 40, 30, 50]},
      start)
sized(gt, 16, 50, {"top": [0, 0, 16, 50], "low": [16, 0, 12, 40], "side": [0, 50, 30, 50]})
sized(gp, 16, 50, {"top": [0, 0, 16, 50], "low": [16, 0, 16, 50], "side": [0, 50, 30, 50]})
grown = sized(gr, 36, 60, {"top": [0, 0, 16, 50], "low": [16, 0, 16, 50], "side": [0, 50, 36, 60]})
check("a grid that grows moves the display below it down and the one beside "
      "it across by as much, and nothing overlaps", grown, where())
b1 = sized(gp, 12, 40, {"top": [0, 0, 16, 50], "low": [16, 0, 12, 40], "side": [0, 40, 36, 60]})
b2 = sized(gr, 30, 50, {"top": [0, 0, 16, 50], "low": [16, 0, 12, 40], "side": [0, 40, 30, 50]})
back = sized(gt, 12, 40, start)
check("and shrunk back, in another order, each display follows at every "
      "step and ends where it began", b1 and b2 and back == start, (b1, b2, where()))
for t in (gr, gp, gt):
    t.quit(None, 0.3)
subprocess.run([screen.HIBR, "-c", load("hold") + "hold kill %s" % gnm],
               env=dict(os.environ, **genv), capture_output=True)
shutil.rmtree(D, True)
report(16)
