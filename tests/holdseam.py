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
terminal.
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
shutil.rmtree(D, True)
report(9)
