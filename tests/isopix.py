#!/usr/bin/env python3
"""A hosted app's picture as pixels (ADR 0050's prototype): where the desktop
shows pixels, an app in a process of its own draws its picture as pixels too.

Its console is given a frame socket of the desktop's own (frames serve) and
sends the picture's pixels there by descriptor, and only the placement down
its terminal; the desktop's emulator keeps that placement (term images) and
the window places the socket's frames there (frames place) -- the pixels
never pass through the desktop's frame. Checked from both ends: the socket
got frames, the emulator kept the app's placement, and the desktop's own
screen holds a picture placed in the window and no half blocks for it.
"""
import os, re, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import screen
from screen import Term, check, report, load, tree, scratch

MOD = tree("build/mods/console.so")
WM = tree("programs/desktop/desktop.hibr")
MARK = scratch("isopix-do")
OUT = scratch("isopix-out")
PIC = tree("tests/img-wide.png")

APP = r'''fn px_draw(int id, int h, int w, int row, int col) {
	need img
	console put -p "w$id" 1 1 "picture:"
	img draw %s 2 1 4 12 -p "w$id" 2> /dev/null
	return 0
}
dt_app px "Pixels" 8 30
''' % PIC

SESSION = r'''%s. %s
dt_open
tst_do() {
	[ -f %s ] || { dt_want 100; return 0; }
	local c
	read -r c < %s
	rm -f %s
	eval "$c"
	dt_want 30
	return 0
}
dt_ticker tst_do
ISOID := dt_isonew px %s Pixels 8 30 3 3
dt_run
dt_close
'''

app = scratch("isopix-app.hibr")
open(app, "w").write(APP)
p = scratch("isopix.hibr")
open(p, "w").write(SESSION % (load(MOD), WM, MARK, MARK, MARK, app))
t = Term(p, rows=24, cols=80, settle=0.8, cellw=8, cellh=16, caps=["kitty"],
         env={"HIBR_GFX": "kitty"})


def ask(expr, secs=6, want=None):
    got = None
    deadline = time.time() + secs
    while time.time() < deadline:
        if os.path.exists(OUT):
            os.unlink(OUT)
        open(MARK, "w").write('{ %s; } > %s 2>&1\n' % (expr, OUT))
        t.send(b"\x1b[I", settle=0.2, collect=0.2)
        try:
            got = open(OUT).read().strip()
        except OSError:
            got = None
        if got is not None and (want is None or want(got)):
            break
    return got


t.until("picture:", tries=100, step=0.1)
fi = ask('frames info "${TW[$ISOID]["isofn"]}"', want=lambda g: re.search(r"\bseq=[1-9]", g) is not None)
check("the hosted app's picture went to the window's frame socket, by descriptor",
      fi is not None and re.search(r"\bseq=[1-9]", fi or "") is not None and
      re.search(r"\b(frames|rgb)=[1-9]", fi or "") is not None, fi)
im = ask('term images "${TW[$ISOID]["t"]}"', want=lambda g: bool(g))
check("its console placed it on its own terminal, and the emulator kept where",
      im is not None and re.match(r"\d+ 1 0 4 12$", im or "") is not None, im)
t.collect(0.5)
sc = t.screen()
apcs = [a for a in getattr(sc, "apc", [])]
placed = [a for a in apcs if b"a=p" in a or b"a=T" in a] if apcs and isinstance(apcs[0], bytes) else \
         [a for a in apcs if "a=p" in str(a) or "a=T" in str(a)]
check("the desktop placed a picture in the window -- pixels, not half blocks",
      bool(placed) and not any(ch in l[4:32] for l in sc.text().split("\n")[4:10] for ch in (chr(0x2580), chr(0x2584))),
      ([str(a)[:60] for a in apcs], sc))
t.quit(b"q", 1.0)
for f in (app, p, MARK, OUT):
    if os.path.exists(f):
        os.unlink(f)
report(4)
