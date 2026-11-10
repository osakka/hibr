#!/usr/bin/env python3
"""Programs' own windows through Blit's compositor (Gitea #494, Blit ADR
0228): wm/native.hibr and the frames module's windows.

A stand-in compositor (natserve.py) is started as the desktop would start
blit-compositor, and a stand-in Blit (frameserve.py) is the display's: the
compositor's windows open as the desktop's, are told their size in pixels
from their cells, their frames reach Blit under the console's ids, their
input route is asked for by socket and follows the keys, a zoom and a full
screen are said as states, a second window takes the keyboard from the
first, closing a window asks the program and the window goes when it has,
and quitting the desktop ends the compositor. Every check reads what
crossed a socket or the terminal.
"""
import base64, json, os, re, shutil, subprocess, sys, tempfile, time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import screen as sx
from screen import Term, check, report, load, tree, press, release, drag

D = tempfile.mkdtemp(prefix="hibr-native-")
HERE = os.path.dirname(os.path.abspath(__file__))
SLOW = 4 if "ASAN_OPTIONS" in os.environ else 1


def lines(lg):
    try:
        return [json.loads(l) for l in open(lg) if l.strip()]
    except OSError:
        return []


def wait(cond, t=4.0):
    end = time.time() + t * SLOW
    while time.time() < end:
        v = cond()
        if v:
            return v
        time.sleep(0.05)
    return cond()


nm = "hibr-nat-%d" % os.getpid()
blitlog = os.path.join(D, "blit.log")
open(blitlog, "w").close()
blit = subprocess.Popen([sys.executable, os.path.join(HERE, "frameserve.py"),
                         nm, "tok", blitlog, "v3", "hold"])
wait(lambda: "listening" in open(blitlog).read())
complog = os.path.join(D, "comp.log")
wrap = os.path.join(D, "blit-compositor")
open(wrap, "w").write("#!/bin/sh\nexec %s %s %s \"$@\"\n"
                      % (sys.executable, os.path.join(HERE, "natserve.py"), complog))
os.chmod(wrap, 0o755)
MARK = os.path.join(D, "zoom")

p = sx.scratch("native.hibr")
open(p, "w").write(
    "%s. %s\nDT_NATBIN=%s\n"
    "tst_zoom() { [ -f %s ] || return 0; rm -f %s; local n; "
    "for n in \"${!NWN[@]}\"; do dt_zoom \"${NWN[$n]}\"; done; dt_want 30; }\n"
    "tst_quit() { [ -f %s ] && dt_quit; return 0; }\n"
    "tst_rs() { [ -f %s ] || return 0; rm -f %s; dt_restart; }\n"
    "tst_copy() { [ -f %s ] || return 0; local t k; { read -r t; read -r k; } < %s; rm -f %s; "
    "if [ \"$k\" = clear ]; then dt_natclipclear; else dt_clipset \"$t\" copy \"${k:-text}\"; fi; }\n"
    "dt_ticker tst_zoom\ndt_ticker tst_quit\ndt_ticker tst_rs\ndt_ticker tst_copy\ndt_open\n"
    "sh -c 'printf %%s \"${WAYLAND_DISPLAY-unset}\"' > %s\ndt_run\ndt_close\n"
    % (load("console"), tree("programs/desktop/desktop.hibr"), wrap, MARK, MARK,
       MARK + ".quit", MARK + ".rs", MARK + ".rs", MARK + ".copy", MARK + ".copy",
       MARK + ".copy", MARK + ".env"))
t = Term(p, rows=26, cols=82, settle=1.0, cellw=8, cellh=16, caps=["kitty", "lock"],
         env={"HIBR_GFX": "kitty", "BLIT_FRAME_SOCKET": "@" + nm,
              "BLIT_FRAME_TOKEN": "tok", "XDG_RUNTIME_DIR": D,
              "WAYLAND_DISPLAY": None})


def scr():
    """The screen now, what the desktop has drawn since read first."""
    t.collect(0.1)
    return t.screen()


def comp_of(lg, kind=None):
    return [x for x in lines(lg) if kind is None or x["kind"] == kind]


def comp(kind=None):
    return [x for x in lines(complog) if kind is None or x["kind"] == kind]


def told(prefix):
    return [x["line"] for x in comp("line") if x["line"].startswith(prefix)]


def blitkind(k, **kw):
    return [x for x in lines(blitlog) if x["kind"] == k and
            all(x.get(a) == b for a, b in kw.items())]


hel = wait(lambda: comp("hello"))
check("the desktop starts the compositor on a socket of its own and answers "
      "its version 5 hello with window=1 and the display's dmabuf formats",
      hel and "window=1" in hel[0]["answer"].split() and
      "dmabuf=1" in hel[0]["answer"].split(), hel)
env0 = wait(lambda: os.path.exists(MARK + ".env") and open(MARK + ".env").read())
check("its display is named before the desktop's first frame, so a program "
      "the desktop starts as it opens is given it", env0 and env0.startswith("stand-"), env0)
sc = wait(lambda: scr().find("Stand-in") and scr())
check("the compositor's window opens as one of the desktop's, titled as the "
      "program titled it", sc and sc.find("Stand-in") is not None,
      sc or scr())
sz = wait(lambda: told("WINDOW size 7 "))
w0 = sz[0].split() if sz else []
check("and is told to draw at its body's size in pixels -- whole cells of 8 "
      "by 16 -- with the keyboard, since it has the focus",
      len(w0) >= 6 and int(w0[3]) % 8 == 0 and int(w0[4]) % 16 == 0 and
      int(w0[3]) > 64 and "activated" in w0, sz)
out = wait(lambda: told("WINDOW output 7 "))
check("and told the display it is on: its size in pixels, and its rate, "
      "60 Hz where Blit's API does not say",
      out and out[0].split()[3:] == [str(82 * 8), str(26 * 16), "60000"], out)
f3 = wait(lambda: blitkind("frame3"))
cid = f3[0]["id"] if f3 else None
check("its frames reach the display's Blit under the console's id, not the "
      "compositor's", cid and cid != 7, lines(blitlog)[-4:])
ok = wait(lambda: blitkind("input", verb="sock", id=cid, ok=True) and
          blitkind("input", verb="on", id=cid, ok=True))
check("its input route is asked for by socket and is on while its window "
      "has the keys", ok, [x for x in lines(blitlog) if x["kind"] == "input"])

n0 = len(told("WINDOW size 7 "))
open(MARK, "w").close()
t.keys([b"x"])
zs = wait(lambda: [l for l in told("WINDOW size 7 ")[n0:] if "maximized" in l.split()])
check("zoomed, it is told a larger size and that it is maximized",
      zs and int(zs[-1].split()[3]) > int(w0[3]), told("WINDOW size 7 "))

n1 = len(told("WINDOW size 7 "))
open(complog + ".full", "w").close()
t.keys([b"x"])
fs = wait(lambda: [l for l in told("WINDOW size 7 ")[n1:] if "fullscreen" in l.split()])
check("a program that asks for full screen is given it, and told so",
      fs and scr().find("File") is None, (told("WINDOW size 7 ")[n1:], scr()))
check("and every size line, zoomed or full screen, is one the compositor "
      "reads: its states in its order, or it refuses the line whole",
      not comp("unknown"), comp("unknown")[:3])
t.keys([b"\x1b[23;5~"])
t.collect(0.6)

open(complog + ".child", "w").close()
t.keys([b"x"])
ch = wait(lambda: scr().find("Child") and scr())
n2 = len(told("WINDOW size 7 "))
lost = wait(lambda: [l for l in told("WINDOW size 7 ")[n2 - 1:] if "activated" not in l.split()])
check("a second window of the program opens titled as it says, takes the "
      "keyboard, and the first is told it has lost it",
      ch and told("WINDOW size 8 ") and "activated" in told("WINDOW size 8 ")[-1].split()
      and lost, (told("WINDOW size "), ch or scr()))

t.keys([b"\x1bw"])
cl = wait(lambda: told("WINDOW close 8"))
gone = wait(lambda: scr().find("Child") is None)
check("closing it asks the program, and the window goes when the program "
      "says it has", cl and gone and scr().find("Stand-in") is not None,
      (cl, scr()))

c0 = len(t.out)
open(complog + ".lock", "w").close()
t.keys([b"x"])
lk = wait(lambda: told("WINDOW lock 7 on"))
check("a program asking to hold the pointer, just after a key in its window, "
      "is given the desktop's hold, confined as it asked, and told so",
      lk and wait(lambda: blitkind("hold", id=cid, mode="confine")) and
      (scr() and b"\x1b[?7701h" in t.out[c0:]), (told("WINDOW lock"), t.out[c0:][-200:]))
t.keys([b"\x1b[21;5~"])
lo = wait(lambda: told("WINDOW lock 7 off"))
check("and Release Pointer gives it back and tells the program", lo, told("WINDOW lock"))

# Zoomed back, so there is desktop beside it to click on.
open(MARK, "w").close()
t.keys([b"x"])
wait(lambda: [l for l in told("WINDOW size 7 ") if "maximized" not in l.split()][-1:]
     and "maximized" not in told("WINDOW size 7 ")[-1].split())
open(complog + ".popup", "w").close()
t.keys([b"x"])
mem = wait(lambda: blitkind("member", top=cid))
check("a popup's picture joins its window's input route, at its place in "
      "the window's pixels", mem and mem[-1]["x"] == 12 and mem[-1]["y"] == 63,
      [x for x in lines(blitlog) if x["kind"] in ("member", "input")][-4:])
pid9 = mem[-1]["id"] if mem else None
placed = wait(lambda: pid9 and scr() and ("i=%d," % pid9).encode() in t.out and
              b"c=10,r=4" in t.out)
check("and is placed as a pane of its own above the window, the size of its "
      "frames in whole cells", placed,
      (pid9, [m for m in re.findall(rb"\x1b_G([^;\x1b]*)", t.out) if b"a=p" in m][-4:]))
sc = scr()
st = sc.find("Stand-in")
wr = st[0] if st else 0
wc = sc.text().splitlines()[wr].index("┌") if st else 0
nc = len(told("WINDOW close 9"))
n4 = len(told("WINDOW size 7 "))
t.keys([press(wr + 1 + 63 // 16, wc + 1 + 12 // 8 + 2), release(wr + 1 + 63 // 16, wc + 1 + 12 // 8 + 2)])
t.collect(0.6)
check("a click on the popup itself is the program's: the popup stays, and "
      "its window keeps the keyboard",
      len(told("WINDOW close 9")) == nc and
      all("activated" in l.split() for l in told("WINDOW size 7 ")[n4:]),
      (told("WINDOW close"), told("WINDOW size 7 ")[n4:]))
t.keys([press(24, 1), release(24, 1)])
dis = wait(lambda: told("WINDOW close 9"))
check("a click elsewhere dismisses it: the program is told, and it goes",
      dis, told("WINDOW close"))
# Subsurfaces (Gitea #496: Firefox draws its page into one): each its own
# picture in its window's pane, at its own pixel size -- no c= or r=, so
# nothing scales -- offset into its first cell, cut to the window's body,
# and at a layer of its own under the window's picture or over it.
def aps():
    return [m.decode() for m in re.findall(rb"\x1b_G(a=p[^;\x1b]*)", t.out)]


def akey(a, k):
    m = re.search(r"(?:^|,)%s=(-?\d+)" % k, a)
    return int(m.group(1)) if m else None


open(complog + ".sub", "w").close()
t.keys([b"x"])
sb = wait(lambda: scr() and [a for a in aps() if "w=96,h=48" in a] and
          [a for a in aps() if "x=8,y=8,w=56,h=24" in a])
above = [a for a in aps() if "w=96,h=48" in a][-1:]
below = [a for a in aps() if "x=8,y=8,w=56,h=24" in a][-1:]
top = [a for a in aps() if "i=%s," % cid in a and akey(a, "c")][-1:]
check("a program window's subsurfaces are pictures of their own at their "
      "own size, offset into a cell, the one past the window's corner cut "
      "to the body",
      above and below and akey(above[0], "c") is None and akey(above[0], "r") is None and
      akey(above[0], "Y") == 40 % 16 and akey(above[0], "X") is None and
      akey(below[0], "c") is None,
      (above, below))
check("and layered: the one below under the window's picture, the one above "
      "over it", above and below and top and
      akey(below[0], "z") < akey(top[0], "z") < akey(above[0], "z"),
      (below, top, above))
mb = wait(lambda: [x for x in blitkind("member", top=cid) if (x["x"], x["y"]) in ((24, 40), (-8, -8))])
check("and each joins its window's input route at its place, so the pointer "
      "over it reaches the program",
      mb and {(x["x"], x["y"]) for x in mb} == {(24, 40), (-8, -8)},
      [x for x in lines(blitlog) if x["kind"] == "member"][-4:])
nsub = len([a for a in aps() if "w=96,h=48" in a])

# A program that draws its own shadow says where its window is in its
# buffer (Wayland's window geometry): its picture is cut to that, the
# shadow never shown, and still scaled into the window's body.
open(complog + ".geom", "w").close()
t.keys([b"x"])
gp = wait(lambda: scr() and [a for a in aps() if "i=%s," % cid in a and "x=10,y=10," in a and ",c=" in a])
check("a window drawing its own shadow is cut to its geometry, the shadow "
      "never shown", gp, [a for a in aps() if "i=%s," % cid in a][-2:])

# Its own title bar asking to move it, a button down: the desktop's own
# drag takes over from that press, the program's route off until the
# button comes up and on again after.
# The program no longer wants the pointer held, or the press below would
# take the hold again rather than start a drag.
nlo = len(told("WINDOW lock 7 off"))
open(complog + ".unlock", "w").close()
t.keys([b"x"])
wait(lambda: len(told("WINDOW lock 7 off")) > nlo)
sc = scr()
st = sc.find("Stand-in")
r0, c0 = (st[0] + 3, st[1]) if st else (6, 10)
noff = len(blitkind("input", verb="off", id=cid))
non = len(blitkind("input", verb="on", id=cid))
t.keys([press(r0 + 1, c0 + 1)])
open(complog + ".move", "w").close()
off = wait(lambda: len(blitkind("input", verb="off", id=cid)) > noff)
t.keys([drag(r0 + 4, c0 + 11), release(r0 + 4, c0 + 11)])
mv = wait(lambda: scr().find("Stand-in") == (st[0] + 3, st[1] + 10) if st else None)
on = wait(lambda: len(blitkind("input", verb="on", id=cid)) > non)
check("a program's own title bar moving its window hands the press to the "
      "desktop's drag: the window moves with the pointer, the program's "
      "route off meanwhile and on again after", off and mv and on,
      (bool(off), scr().find("Stand-in"), st, bool(on)))

# The clipboard, both ways (Blit ADR 0229).
def osc52():
    return [base64.b64decode(m).decode(errors="replace")
            for m in re.findall(rb"\x1b\]52;c;([A-Za-z0-9+/=]*)", t.out)]


npaste = len(comp("pasted"))
open(complog + ".copy", "w").write("copied in a program\n")
cp = wait(lambda: scr() and "copied in a program" in osc52())
check("a program's copy is the desktop's clipboard -- and the terminal's -- "
      "and is not offered back to the programs, which have it",
      cp and not [x for x in comp("pasted")[npaste:] if x["text"] == "copied in a program"],
      (osc52()[-2:], comp("pasted")[npaste:]))
open(MARK + ".copy", "w").write("copied on the desktop\n")
t.keys([b"x"])
pd = wait(lambda: [x for x in comp("pasted") if x["text"] == "copied on the desktop"])
check("the desktop's copy is offered to the programs, and pastes as its text",
      pd and pd[0]["sel"] == "clipboard" and not pd[0]["secret"], comp("pasted")[-2:])
open(MARK + ".copy", "w").write("hunter2\nsecret\n")
t.keys([b"x"])
ps = wait(lambda: [x for x in comp("pasted") if x["text"] == "hunter2"])
open(MARK + ".copy", "w").write("\nclear\n")
t.keys([b"x"])
pc = wait(lambda: comp("clipclear"))
check("a secret goes marked as one, and its clear takes it back from the "
      "programs too", ps and ps[0]["secret"] and pc, (comp("pasted")[-2:], comp("clipclear")))

# Restart Desktop: the compositor is in a session of its own and outlives
# the exec, the restarted desktop serves the same socket and token, and the
# compositor dials it again and announces its window under the same id --
# which the window that came back with the restart takes, rather than a
# second one opening beside it.
h0 = len(comp("hello"))
nr = len(told("WINDOW size 7 "))
nf0 = len(blitkind("frame3"))
ni0 = len(blitkind("input", verb="sock", ok=True))
no0 = len(blitkind("input", verb="on", ok=True))
open(MARK + ".rs", "w").close()
t.keys([b"x"])
again = wait(lambda: len(comp("hello")) > h0 and told("WINDOW size 7 ")[nr:], 8)
sc = wait(lambda: scr().find("Stand-in") and
          scr().find("Waiting for the program") is None and scr(), 4)
check("after Restart Desktop the compositor is the same process, says hello "
      "again, and its window is the one that came back, told its size again",
      again and len(set(x["pid"] for x in comp("start"))) == 1 and sc and
      len(re.findall("Stand-in", sc.text())) == 1,
      (comp("hello")[h0:], told("WINDOW size 7 ")[nr:], sc or scr()))
f3b = wait(lambda: blitkind("frame3")[nf0:])
cid = f3b[-1]["id"] if f3b else cid
okb = wait(lambda: blitkind("input", verb="sock", ok=True)[ni0:] and
           blitkind("input", verb="on", ok=True)[no0:])
sb2 = wait(lambda: scr() and len([a for a in aps() if "w=96,h=48" in a]) > nsub)
check("and its subsurfaces come back with it, placed again",
      sb2, [a for a in aps() if "w=" in a][-3:])
check("and its frames and its input route reach the display's Blit again, "
      "the route on, since the window still has the keys",
      f3b and okb, [x for x in lines(blitlog) if x["kind"] in ("input", "hello")][-6:])

n3 = len(told("WINDOW size 7 "))
open(complog + ".csd", "w").close()
t.keys([b"x"])
nf = wait(lambda: scr().find("Stand-in") is None and told("WINDOW size 7 ")[n3:])
check("a program that draws its own frame gets a window with none, and is "
      "told the whole of it", nf and max(int(l.split()[3]) for l in told("WINDOW size 7 ")[n3:]) >
      int(told("WINDOW size 7 ")[n3 - 1].split()[3]), (told("WINDOW size 7 ")[n3 - 1:], scr()))

open(MARK + ".quit", "w").close()
t.keys([b"x"])
q = wait(lambda: comp("quit"))
t.quit(None, 0.5)
check("quitting the desktop ends the compositor at once", q, comp()[-3:])
blit.terminate()
blit.wait()

# Held, as every desktop of the owner's is: what the compositor asks of
# Blit -- its route by socket, a popup's membership -- goes through hold to
# the display's Blit, and comes back after `hold upgrade`.
nm2 = "hibr-nath-%d" % os.getpid()
blitlog2 = os.path.join(D, "blit2.log")
open(blitlog2, "w").close()
blit2 = subprocess.Popen([sys.executable, os.path.join(HERE, "frameserve.py"),
                          nm2, "tok2", blitlog2, "v3", "hold"])
wait(lambda: "listening" in open(blitlog2).read())
complog2 = os.path.join(D, "comp2.log")
# Its window drawn in shared memory (FRM1), its subsurfaces by dmabuf, as
# Firefox draws its own frame round its page: held, a memfd frame once went
# without being recorded and the window's route was never asked (#499).
open(complog2 + ".shm", "w").close()
wrap2 = os.path.join(D, "blit-compositor2")
open(wrap2, "w").write("#!/bin/sh\nexec %s %s %s \"$@\"\n"
                       % (sys.executable, os.path.join(HERE, "natserve.py"), complog2))
os.chmod(wrap2, 0o755)
MARK2 = os.path.join(D, "held")
p2 = sx.scratch("native-held.hibr")
open(p2, "w").write(
    "%s. %s\nDT_NATBIN=%s\nDT_SUPERVISE=off\n"
    "tst_quit() { [ -f %s ] && dt_quit; return 0; }\n"
    "dt_ticker tst_quit\ndt_open\ndt_run\ndt_close\n"
    % (load("console"), tree("programs/desktop/desktop.hibr"), wrap2, MARK2 + ".quit"))
hn = "nath-%d" % os.getpid()
henv = {"HIBR_GFX": "kitty", "TMPDIR": D, "XDG_RUNTIME_DIR": D}
subprocess.run([sx.HIBR, "-c", load("hold") + 'hold new -d %s "$HIBR" %s' % (hn, p2)],
               env=dict({k: v for k, v in os.environ.items()
                         if k not in ("WAYLAND_DISPLAY", "HIBR_HOLD")}, **henv),
               capture_output=True, timeout=20)
a0 = os.path.join(D, "att.hibr")
open(a0, "w").write(load("hold") + "hold attach -m -n top %s 0 0\n" % hn)
ht = Term(a0, rows=26, cols=82, settle=1.0, cellw=8, cellh=16, caps=["kitty"],
          env=dict(henv, BLIT_FRAME_SOCKET="@" + nm2, BLIT_FRAME_TOKEN="tok2"))


def hscr():
    ht.collect(0.1)
    return ht.screen()


def hk(k, **kw):
    return [x for x in lines(blitlog2) if x["kind"] == k and
            all(x.get(a) == b for a, b in kw.items())]


hw = wait(lambda: hscr().find("Stand-in"), 8)
hsock = wait(lambda: hk("input", verb="sock", ok=True), 6)
check("held, the compositor's window reaches the display, and its route by "
      "socket reaches the display's Blit through hold -- the window drawn "
      "in shared memory, as Firefox's own frame is",
      hw and hsock, (bool(hw), bool(hsock), [x for x in lines(blitlog2) if x["kind"] in ("input", "member", "hello")][-8:], hscr().text()[:900]))
open(complog2 + ".popup", "w").close()
ht.keys([b"x"])
hmem = wait(lambda: hk("member"), 6)
check("and a popup's membership of its window's route goes the same way",
      hmem, [x for x in lines(blitlog2) if x["kind"] != "frame3"][-6:])
open(complog2 + ".sub", "w").close()
ht.keys([b"x"])
hsb = wait(lambda: hscr() and [m for m in re.findall(rb"\x1b_G(a=p[^;\x1b]*)", ht.out)
                               if b"w=96,h=48" in m and b",c=" not in m], 8)
check("and a subsurface drawn at its own size reaches the display through "
      "hold, which keeps it by the cells its pixels cover",
      hsb, [m[:90] for m in re.findall(rb"\x1b_G(a=p[^;\x1b]*)", ht.out)][-5:])
ns, nmb = len(hk("input", verb="sock")), len(hk("member"))
up = subprocess.run([sx.HIBR, "-c", load("hold") + "hold upgrade %s" % hn],
                    env=dict(os.environ, **henv), capture_output=True, text=True, timeout=30)
again = wait(lambda: len(hk("input", verb="sock")) > ns and len(hk("member")) > nmb, 10)
check("after `hold upgrade` both are said to the display's Blit again",
      up.returncode == 0 and again, (up.stderr, len(hk("input", verb="sock")), ns,
                                     len(hk("member")), nmb))
open(MARK2 + ".quit", "w").close()
ht.keys([b"x"])
wait(lambda: [x for x in lines(complog2) if x["kind"] == "quit"], 6)
ht.quit(None, 0.5)
subprocess.run([sx.HIBR, "-c", load("hold") + "hold kill %s" % hn],
               env=dict(os.environ, **henv), capture_output=True, timeout=20)
blit2.terminate()
blit2.wait()

# Two stacked displays, as the owner's tall screen is: a window over their
# seam, with a subsurface across it (Firefox's page), shows on both -- the
# window's own picture cut by its cells, the subsurface by its pixels, each
# display sent its own slice; it went whole to the top-left one's display
# and the other showed nothing of it.
def blitd(tag):
    nmx = "hibr-nat2%s-%d" % (tag, os.getpid())
    lg = os.path.join(D, "blit2%s.log" % tag)
    open(lg, "w").close()
    pr = subprocess.Popen([sys.executable, os.path.join(HERE, "frameserve.py"),
                           nmx, "tok" + tag, lg, "v3"])
    wait(lambda: "listening" in open(lg).read())
    return pr, {"BLIT_FRAME_SOCKET": "@" + nmx, "BLIT_FRAME_TOKEN": "tok" + tag}


pa, ea = blitd("a")
pb, eb = blitd("b")
complog3 = os.path.join(D, "comp3.log")
open(complog3 + ".sub", "w").close()
wrap3 = os.path.join(D, "blit-compositor3")
open(wrap3, "w").write("#!/bin/sh\nexec %s %s %s \"$@\"\n"
                       % (sys.executable, os.path.join(HERE, "natserve.py"), complog3))
os.chmod(wrap3, 0o755)
MV = os.path.join(D, "mv")
p3 = sx.scratch("native-seam.hibr")
open(p3, "w").write(
    "%s. %s\nDT_NATBIN=%s\nDT_SUPERVISE=off\n"
    "tst_mv() { [ -f %s ] || return 0; rm -f %s; local n; "
    "for n in \"${!NWN[@]}\"; do dt_move \"${NWN[$n]}\" 19 4; done; dt_want 30; }\n"
    "dt_ticker tst_mv\ndt_open\ndt_run\ndt_close\n"
    % (load("console"), tree("programs/desktop/desktop.hibr"), wrap3, MV, MV))
hn3 = "nat3-%d" % os.getpid()
subprocess.run([sx.HIBR, "-c", load("hold") + 'hold new -d %s "$HIBR" %s' % (hn3, p3)],
               env=dict({k: v for k, v in os.environ.items()
                         if k not in ("WAYLAND_DISPLAY", "HIBR_HOLD")}, **henv),
               capture_output=True, timeout=20)
at = os.path.join(D, "at3.hibr")
open(at, "w").write(load("hold") + "hold attach -m -n top %s 0 0\n" % hn3)
st0 = Term(at, rows=24, cols=82, settle=1.0, cellw=8, cellh=16, caps=["kitty"],
           env=dict(henv, **ea))
st0.collect(2.0 * SLOW)
ab = os.path.join(D, "ab3.hibr")
open(ab, "w").write(load("hold") + "hold attach -m -n bottom %s 24 0\n" % hn3)
st1 = Term(ab, rows=24, cols=82, settle=1.0, cellw=8, cellh=16, caps=["kitty"],
           env=dict(henv, **eb))
wait(lambda: comp_of(complog3, "hello"), 6)
open(MV, "w").close()


def subcut(tt):
    tt.collect(0.2)
    return [m for m in re.findall(rb"\x1b_G(a=p[^;\x1b]*)", tt.out)
            if b"w=96,h=24" in m and b",c=" not in m]


top = wait(lambda: subcut(st0) and subcut(st0), 8)
bot = wait(lambda: subcut(st1) and subcut(st1), 8)
check("a window over the seam of two stacked displays shows its subsurface on "
      "both, each its own slice of it by its pixels",
      top and bot and b"Y=8" in top[-1] and b"y=24" in bot[-1], (top[-2:], bot[-2:]))
st0.quit(None, 0.3)
st1.quit(None, 0.3)
subprocess.run([sx.HIBR, "-c", load("hold") + "hold kill %s" % hn3],
               env=dict(os.environ, **henv), capture_output=True, timeout=20)
pa.terminate()
pb.terminate()

# With no compositor, nothing about Wayland is put in what the desktop
# starts: an empty WAYLAND_DISPLAY sends Wayland programs to a display
# called "" and makes GTK and Qt give up on X11.
r = subprocess.run([sx.HIBR, "-c", ". %s\nenv | grep -c '^WAYLAND_DISPLAY=' || true\n"
                    % tree("programs/desktop/desktop.hibr")],
                   env=dict((k, v) for k, v in os.environ.items() if k != "WAYLAND_DISPLAY"),
                   capture_output=True, text=True, timeout=30)
check("a desktop with no compositor exports no WAYLAND_DISPLAY at all",
      r.stdout.strip() == "0", (r.stdout, r.stderr))
shutil.rmtree(D, True)
report(38)
