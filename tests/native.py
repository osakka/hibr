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
import json, os, re, shutil, subprocess, sys, tempfile, time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import screen as sx
from screen import Term, check, report, load, tree, press, release

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
    "dt_ticker tst_zoom\ndt_ticker tst_quit\ndt_open\n"
    "sh -c 'printf %%s \"${WAYLAND_DISPLAY-unset}\"' > %s\ndt_run\ndt_close\n"
    % (load("console"), tree("programs/desktop/desktop.hibr"), wrap, MARK, MARK,
       MARK + ".quit", MARK + ".env"))
t = Term(p, rows=26, cols=82, settle=1.0, cellw=8, cellh=16, caps=["kitty", "lock"],
         env={"HIBR_GFX": "kitty", "BLIT_FRAME_SOCKET": "@" + nm,
              "BLIT_FRAME_TOKEN": "tok", "XDG_RUNTIME_DIR": D,
              "WAYLAND_DISPLAY": None})


def scr():
    """The screen now, what the desktop has drawn since read first."""
    t.collect(0.1)
    return t.screen()


def comp(kind=None):
    return [x for x in lines(complog) if kind is None or x["kind"] == kind]


def told(prefix):
    return [x["line"] for x in comp("line") if x["line"].startswith(prefix)]


def blitkind(k, **kw):
    return [x for x in lines(blitlog) if x["kind"] == k and
            all(x.get(a) == b for a, b in kw.items())]


hel = wait(lambda: comp("hello"))
check("the desktop starts the compositor on a socket of its own and answers "
      "its version 4 hello with window=1 and the display's dmabuf formats",
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
      "socket reaches the display's Blit through hold",
      hw and hsock, (bool(hw), bool(hsock), [x for x in lines(blitlog2) if x["kind"] in ("input", "member", "hello")][-8:], hscr().text()[:900]))
open(complog2 + ".popup", "w").close()
ht.keys([b"x"])
hmem = wait(lambda: hk("member"), 6)
check("and a popup's membership of its window's route goes the same way",
      hmem, [x for x in lines(blitlog2) if x["kind"] != "frame3"][-6:])
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
report(24)
