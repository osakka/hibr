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
import base64, json, os, re, shutil, signal, subprocess, sys, tempfile, time

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
# Run by a copy of the interpreter, so the suite can replace the program a
# running compositor was started from, as an upgrade does.
PYC = os.path.join(D, "pyc")
shutil.copy(os.path.realpath(sys.executable), PYC)
open(wrap, "w").write("#!/bin/sh\nexec %s %s %s \"$@\"\n"
                      % (PYC, os.path.join(HERE, "natserve.py"), complog))
os.chmod(wrap, 0o755)
MARK = os.path.join(D, "zoom")

p = sx.scratch("native.hibr")
open(p, "w").write(
    "%s. %s\nDT_NATBIN=%s\n"
    "tst_zoom() { [ -f %s ] || return 0; rm -f %s; local n; "
    "for n in \"${!NWN[@]}\"; do dt_zoom \"${NWN[$n]}\"; done; dt_want 30; }\n"
    "tst_quit() { [ -f %s ] && dt_quit; return 0; }\n"
    "tst_rs() { [ -f %s ] || return 0; rm -f %s; dt_restart; }\n"
    "tst_x11() { [ -f %s.x11 ] || return 0; local v; read -r v < %s.x11; rm -f %s.x11; DT_NATX11=$v; dt_save; }\n"
    "tst_mn() { local a n; for a in min raise; do [ -f %s.$a ] || continue; rm -f %s.$a; "
    "for n in \"${!NWN[@]}\"; do if [ $a = min ]; then dt_min \"${NWN[$n]}\"; "
    "else dt_raise \"${NWN[$n]}\"; fi; done; dt_want 30; done; return 0; }\n"
    "tst_copy() { [ -f %s ] || return 0; local t k; { read -r t; read -r k; } < %s; rm -f %s; "
    "if [ \"$k\" = clear ]; then dt_natclipclear; else dt_clipset \"$t\" copy \"${k:-text}\"; fi; }\n"
    "tst_nf() { [ -f %s ] || return 0; local v; read -r v < %s; rm -f %s; DT_NATFOCUS=$v; "
    "[ \"$v\" = launch ] && { DT_NATFOCUS=expected; dt_launched; }; return 0; }\n"
    "tst_cf() { [ -f %s ] || return 0; rm -f %s; dt_confirm \"Sure?\" true; dt_want 30; }\n"
    "dt_ticker tst_zoom\ndt_ticker tst_quit\ndt_ticker tst_rs\ndt_ticker tst_copy\ndt_ticker tst_mn\ndt_ticker tst_x11\ndt_ticker tst_nf\ndt_ticker tst_cf\ndt_progload\ndt_open\n"
    "sh -c 'printf %%s \"${WAYLAND_DISPLAY-unset} ${DISPLAY-unset}\"' > %s\ndt_run\ndt_close\n"
    % (load("console"), tree("programs/desktop/desktop.hibr"), wrap, MARK, MARK,
       MARK + ".quit", MARK + ".rs", MARK + ".rs", MARK, MARK, MARK, MARK, MARK, MARK + ".copy", MARK + ".copy",
       MARK + ".copy", MARK + ".nf", MARK + ".nf", MARK + ".nf", MARK + ".cf", MARK + ".cf",
       MARK + ".env"))
# The stand-in's app id has a desktop entry of its own, for the bar's name.
os.makedirs(os.path.join(D, "share", "applications"))
open(os.path.join(D, "share", "applications", "hibr.natserve.desktop"), "w").write(
    "[Desktop Entry]\nType=Application\nName=Natserve App\nExec=true\n")
t = Term(p, rows=26, cols=82, settle=1.0, cellw=8, cellh=16, caps=["kitty", "lock"],
         env={"HIBR_GFX": "kitty", "BLIT_FRAME_SOCKET": "@" + nm,
              "BLIT_FRAME_TOKEN": "tok", "XDG_RUNTIME_DIR": D,
              "WAYLAND_DISPLAY": None, "DISPLAY": None,
              "DT_PROGAUTO": "1", "XDG_DATA_DIRS": os.path.join(D, "share")})


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
check("and the X display it runs Xwayland on, as DISPLAY, for X11 programs "
      "(Blit ADR 0228 phase 2)", env0 and env0.split()[-1:] == [":47"], env0)
sc = wait(lambda: scr().find("Stand-in") and scr())
check("the compositor's window opens as one of the desktop's, titled as the "
      "program titled it", sc and sc.find("Stand-in") is not None,
      sc or scr())
# The bar names the program in front by its own name: its app id's desktop
# entry, the app id itself without one, its title without an app id, and
# "Program" only with none of them.
bar = wait(lambda: "Natserve Ap" in scr().row(0) and scr().row(0))
check("the menu bar names a program window by its desktop entry's name, "
      "not \"Program\"", bar, scr().row(0))
r = subprocess.run([sx.HIBR, "-c", ". %s\ndeclare -gA NW\n"
                    "NW[5][n]=1 NW[5][appid]=XTerm DT[5][title]=t\n"
                    "a := native_appname 5\nNW[5][appid]=\nb := native_appname 5\n"
                    "DT[5][title]=\nc := native_appname 5\necho \"$a|$b|$c|\"\n"
                    % tree("programs/desktop/desktop.hibr")],
                   env=dict(os.environ, DT_PROGAUTO="0"), capture_output=True, text=True,
                   timeout=30)
check("with no entry it is the app id, with no app id the title, then nothing",
      r.stdout.strip() == "XTerm|t||", (r.stdout, r.stderr))
# A key with a program window in front is the program's alone (Blit hands
# it over; the desktop sees it too): Escape in a game opened the desktop's
# menu bar, which is Menu Bar Also.
t.keys([b"\x1b"], settle=0.6)
check("Escape with a program window in front does not open the desktop's menu",
      scr().find("Restart Desktop") is None and scr().find("About This") is None,
      scr().text()[:1200])
# A new window is told no size until the program's first picture says what
# it chose, and is then fitted round it, the program told its own size back
# -- 300 by 200, no whole number of cells -- with the keyboard, since it has
# the focus (Gitea #508). A game sized itself 1920 by 1080 and was resized
# three times before this.
sz = wait(lambda: [l for l in told("WINDOW size 7 ") if l.split()[3:5] == ["300", "200"]]
          and told("WINDOW size 7 "))
w0 = sz[0].split() if sz else []
check("a new program window is told no size until its first picture, then its "
      "own size, with the keyboard, since it has the focus",
      len(sz) >= 2 and w0[3:5] == ["0", "0"] and
      sz[1].split()[3:5] == ["300", "200"] and "activated" in sz[1].split(), sz)
fit = wait(lambda: scr().find("Stand-in") and scr())
check("and its window is fitted round that picture: 38 by 13 cells of body "
      "in a frame", fit and re.search(r"Stand-in", fit.text()) and
      [m for m in re.finditer(r"\u250c[^\n]*Stand-in[^\n]*\u2510", fit.text())
       if len(m.group(0)) == 40], fit or scr())
w0 = ["", "", "", "300", "200"]
out = wait(lambda: told("WINDOW output 7 "))
check("and told the display it is on: its size in pixels, and its rate, "
      "60 Hz where Blit's API does not say",
      out and out[0].split()[3:] == [str(82 * 8), str(26 * 16), "60000"], out)
# The chords the desktop keeps from programs' windows reach Blit (Gitea
# #530): F10, the Shortcut Key's chords spelled with alt, Lock's ctrl-alt-l
# -- and not Escape, a plain key, which a program window keeps.
kp = wait(lambda: blitkind("keep"))
kc = kp[-1]["chords"] if kp else []
check("the desktop tells Blit the chords it keeps from programs, spelled as "
      "Blit reads them, and no plain key among them",
      "f10" in kc and "alt-w" in kc and "alt-tab" in kc and "ctrl-alt-l" in kc
      and "escape" not in kc and not [x for x in lines(blitlog) if x.get("kind") == "input"
                                      and x.get("verb") == "keep"], kc)
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

def framebox(sc, title):
    """A window's frame on screen, found by its title: (top, left, height,
    width), or None."""
    at = sc.find("\u2524 %s \u251c" % title) if sc else None
    if not at:
        return None
    rows = sc.text().split("\n")
    r = at[0]
    c0 = rows[r].rfind("\u250c", 0, at[1])
    c1 = rows[r].find("\u2510", at[1])
    b = next((i for i in range(r + 1, len(rows))
              if len(rows[i]) > c0 and rows[i][c0] == "\u2514"), len(rows) - 1)
    return (r, c0, b - r + 1, c1 - c0 + 1)


pbox = framebox(scr(), "Stand-in")
open(complog + ".child", "w").close()
t.keys([b"x"])
# The whole frame, both corners: a screen read while the dialog's title row
# was still being drawn found no right corner, and a width of -21 failed a
# dialog that was centred (Gitea #527).
ch = wait(lambda: scr().find("Child") and (framebox(scr(), "Child") or (0, 0, 0, 0))[3] > 0
          and scr())
# A window with a parent -- a dialog -- opens centred on it (Gitea #508).
cbox = framebox(ch, "Child")
check("a program's dialog opens centred on its parent window",
      pbox and cbox and
      abs((cbox[0] + cbox[2] / 2) - (pbox[0] + pbox[2] / 2)) <= 1 and
      abs((cbox[1] + cbox[3] / 2) - (pbox[1] + pbox[3] / 2)) <= 1,
      (pbox, cbox, ch.text()[:1600] if ch else scr()))
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

# Programs Take Focus (Gitea #507). Steam took the keys from a running game
# 2.8 s after the game had them. Another program's window that opens while
# this one has the keys, with nothing started by the person just now, opens
# without them and says so; asking for them later is the same; with Always,
# or just after the person started something, it is given them.
def lastsize(w):
    ls = told("WINDOW size %d " % w)
    return ls[-1].split() if ls else []


open(complog + ".other", "w").close()
t.keys([b"x"])
oth = wait(lambda: told("WINDOW size 12 ") and scr().find("wants your attention") and scr())
check("another program's window opening while one has the keys opens without "
      "them, with a note that brings it, and the first keeps them",
      oth and "activated" not in lastsize(12) and "activated" in lastsize(7),
      (told("WINDOW size 12 "), lastsize(7), oth or scr()))
open(complog + ".activate", "w").write("12 x-active\n")
t.keys([b"x"])
t.collect(0.6)
check("and asking for them is refused the same way, X's focus left where it is",
      "activated" not in lastsize(12) and "activated" in lastsize(7),
      (lastsize(12), lastsize(7)))
open(MARK + ".nf", "w").write("launch\n")
t.keys([b"x"])
wait(lambda: not os.path.exists(MARK + ".nf"), 4)
open(complog + ".activate", "w").write("12 x-active\n")
t.keys([b"x"])
got = wait(lambda: "activated" in lastsize(12))
check("just after the person started something it is given them",
      got and "activated" not in lastsize(7), (lastsize(12), lastsize(7)))
open(complog + ".othergone", "w").close()
t.keys([b"x"])
wait(lambda: scr().find("Other") is None)
# A confirm box over a program window answers its keys itself: the window's
# route goes off while it is up and comes back after (Blit's layer 2).
# The stand-in's window is given the keys first: which window has them once
# the other program's has gone depends on timing, and with none there is no
# route to take.
open(MARK + ".raise", "w").close()
t.keys([b"x"])
wait(lambda: not os.path.exists(MARK + ".raise") and "activated" in lastsize(7))
noff = len(blitkind("input", verb="off"))
non = len(blitkind("input", verb="on"))
open(MARK + ".cf", "w").close()
t.keys([b"x"])
cfoff = wait(lambda: scr().find("Sure?") and len(blitkind("input", verb="off")) > noff)
t.keys([b"n"])
cfon = wait(lambda: scr().find("Sure?") is None and len(blitkind("input", verb="on")) > non)
check("a confirm box over a program window takes its keys, and gives them back",
      cfoff and cfon, (bool(cfoff), bool(cfon), scr()))

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
def popat():
    """Where on the screen the popup's picture was last placed, as a cell."""
    w = [(r, c) for r, c, ctrl in scr().kplace if pid9 and ("i=%d," % pid9) in ctrl]
    return w[-1] if w else None


at0 = popat()
open(complog + ".popmove", "w").close()
t.keys([b"x"])
mv = wait(lambda: [x for x in blitkind("member", top=cid) if x["x"] == 52 and x["y"] == 79])
at1 = wait(lambda: popat() != at0 and popat())
check("a popup its program moves is placed again at the new place, and its "
      "route offset follows", mv and at0 and at1 and at1 != at0, (mv, at0, at1))
# And back where it was, for the clicks below.
open(complog + ".popmove", "w").write("12 63\n")
t.keys([b"x"])
wait(lambda: popat() == at0 and popat())
# A popup's own sub -- what Firefox draws a menu's contents into -- and a
# popup of a popup, a submenu (Gitea #528): each is a member of the
# window's route at its offset added down to the window, and each is drawn,
# the sub in its popup's pane and the submenu as a pane of its own.
open(complog + ".popsub", "w").close()
t.keys([b"x"])
m13 = wait(lambda: [x for x in blitkind("member", top=cid) if x["x"] == 16 and x["y"] == 67])
m14 = wait(lambda: [x for x in blitkind("member", top=cid) if x["x"] == 32 and x["y"] == 73])
check("a popup's sub and a popup of a popup join the window's route at their "
      "places added down to the window", m13 and m14,
      [x for x in lines(blitlog) if x["kind"] == "member"][-5:])
i13 = m13[-1]["id"] if m13 else None
i14 = m14[-1]["id"] if m14 else None
drawn = wait(lambda: i13 and i14 and scr() and ("i=%d," % i13).encode() in t.out and
             [1 for r, c, ctrl in scr().kplace if ("i=%d," % i14) in ctrl])
check("and both are drawn: the sub in its popup, the submenu over it",
      drawn, (i13, i14, [m for m in re.findall(rb"\x1b_G([^;\x1b]*)", t.out) if b"a=p" in m][-5:]))
open(complog + ".popsubgone", "w").close()
t.keys([b"x"])
wait(lambda: told("WINDOW size 7 ") and not [1 for r, c, ctrl in scr().kplace
                                             if i14 and ("i=%d," % i14) in ctrl][-1:])
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
# Up and to the left: the window is fitted to the program's own size and may
# sit at the screen's foot, where a move down would be held at the edge.
t.keys([drag(r0 - 2, c0 - 9), release(r0 - 2, c0 - 9)])
mv = wait(lambda: scr().find("Stand-in") == (st[0] - 3, st[1] - 10) if st else None)
on = wait(lambda: len(blitkind("input", verb="on", id=cid)) > non)
check("a program's own title bar moving its window hands the press to the "
      "desktop's drag: the window moves with the pointer, the program's "
      "route off meanwhile and on again after", off and mv and on,
      (bool(off), scr().find("Stand-in"), st, bool(on)))

# Dragging a program window by the desktop's own title bar must cost the
# program nothing (0.99.492, and the owner's "fixed for good"): its size is
# not said again, its route is not turned off and on, and the desktop draws
# about a frame a move, not one for every frame the program sends meanwhile.
t.collect(0.6)
sc = scr()
tt = sc.find("\u2524 Stand-in")
dr0, dc0 = (tt[0], tt[1] + 4) if tt else (2, 10)
ns7 = len(told("WINDOW size 7 "))
nio = len([x for x in blitkind("input") if x.get("verb") in ("on", "off")])
nout = len(told("WINDOW output 7 "))
fr0 = t.frames()
t.keys([press(dr0, dc0)] + [drag(dr0 + (k + 1) // 4, dc0 + k) for k in range(1, 11)]
       + [release(dr0 + 2, dc0 + 10), 0.6])
moved = wait(lambda: scr().find("\u2524 Stand-in") not in (None, tt) and scr())
fr1 = t.frames()
io = [x["verb"] for x in blitkind("input") if x.get("verb") in ("on", "off")][nio:]
check("dragging a program window by the desktop's frame turns its route off "
      "for the drag and on after, so no pointer reaches it meanwhile, and "
      "says nothing else to it: no size, no output",
      moved and len(told("WINDOW size 7 ")) == ns7 and io == ["off", "on"] and
      len(told("WINDOW output 7 ")) == nout and fr1 - fr0 <= 16,
      (bool(moved), told("WINDOW size 7 ")[ns7:], len(told("WINDOW output 7 ")) - nout,
       [x for x in blitkind("input") if x.get("verb") in ("on", "off")][nio:], fr1 - fr0))

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

# A size line is sent when it says something new: the same size and state
# twice in a row -- a decoration said again, a focus change -- before any restart (which tells a reconnected
# compositor again, rightly), was a focus change clearing what had been sent, seen as a
# dozen identical lines on the owner's machine.
for a in ("min", "raise"):
    open(MARK + "." + a, "w").close()
    t.keys([b"x"])
    wait(lambda: not os.path.exists(MARK + "." + a) and scr(), 4)
    t.keys([b"x"])
for _ in range(2):
    open(complog + ".redecor", "w").close()
    wait(lambda: not os.path.exists(complog + ".redecor"), 4)
    t.keys([b"x"])
szs = told("WINDOW size 7 ")
dup = [a for a, b in zip(szs, szs[1:]) if a == b]
check("a program window is never told the same size and state twice running",
      szs and not dup, (len(szs), dup[:3]))

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

# A compositor an upgrade replaced on disk is ended at Restart Desktop and
# the new one started on the same socket, its program's windows closing; and
# one that has gone is started again, where the restart used to serve the
# socket and wait for a compositor that would never come.
def alive(pid):
    try:
        os.kill(pid, 0)
        return True
    except OSError:
        return False


def rsfresh():
    s0, h0 = len(comp("start")), len(comp("hello"))
    old = comp("start")[-1]
    open(MARK + ".rs", "w").close()
    t.keys([b"x"])
    new = wait(lambda: comp("start")[s0:], 8)
    # Its windows are the new compositor's: the old one's, waiting for a
    # program that will not come back, are closed once it has said hello.
    sc = wait(lambda: not alive(old["pid"]) and comp("hello")[h0:] and
              scr().find("Waiting for the program") is None and
              scr().find("│") and scr(), 10)
    return old, new, sc


shutil.copy(PYC, PYC + ".new")
os.rename(PYC + ".new", PYC)
old, new, sc = rsfresh()
check("after an upgrade replaced the compositor's program, Restart Desktop "
      "ends it and starts the new one on the same socket, its windows the new one's",
      new and new[0]["pid"] != old["pid"] and new[0]["sock"] == old["sock"] and
      not alive(old["pid"]) and sc, (old, new, alive(old["pid"]), open("/proc/%d/stat" % old["pid"]).read()[:60] if alive(old["pid"]) else "", (sc or scr()).text()[:1500]))
wait(lambda: told("WINDOW size ")[-1:] and comp("hello")[-1:], 4)
gone = comp("start")[-1]["pid"]
os.kill(gone, signal.SIGKILL)
wait(lambda: not alive(gone), 4)
old, new, sc = rsfresh()
check("and one that has gone is started again", new and sc, (new, sc or scr()))

# X11 Programs (Gitea #504): a compositor that takes --x11 is told the
# setting, and Restart Desktop starts it again when the setting has changed
# since -- and only then.
check("the compositor is started with --x11 on, the setting's default",
      comp("start")[-1]["argv"][:2] == ["--x11", "on"], comp("start")[-1])
open(MARK + ".x11", "w").write("0\n")
t.keys([b"x"])
wait(lambda: not os.path.exists(MARK + ".x11"), 4)
old, new, sc = rsfresh()
check("and X11 Programs turned off is a compositor started again with --x11 off "
      "at Restart Desktop", new and new[0]["argv"][:2] == ["--x11", "off"] and
      not alive(old["pid"]) and sc, (old, new, sc or scr()))
fake = os.path.join(D, "fakecomp")
r = []
for ver in ("0.3.80", "0.3.81", "0.4.0", "garbage"):
    open(fake, "w").write("#!/bin/sh\necho 'blit-compositor %s'\n" % ver)
    os.chmod(fake, 0o755)
    r.append(subprocess.run([sx.HIBR, "-c", ". %s\nDT_NATX11=0\nx := dt_natx11 %s && "
                             "echo \"$x\" || echo none\n"
                             % (tree("programs/desktop/desktop.hibr"), fake)],
                            capture_output=True, text=True, timeout=30).stdout.strip())
check("and only one at 0.3.81 or later is told: an older compositor refuses "
      "an option it does not know", r == ["none", "off", "off", "none"], r)

# Remember Program Windows (Gitea #508): on closing, a move the person made
# is kept as the program's place and a resize as its size, apart; the next
# window of that program opens there -- not its size where the program is of
# one size only, and not at all with the setting off.
pls = os.path.join(D, "plstate")
plr = subprocess.run([sx.HIBR, "-c", """. %s
DT_ROWS=40; DT_COLS=120; DT_DISPLAYS=("0 0 40 120 self"); DT_DISPNAMES=("")
pl5() { DT[5]["row"]=$1; DT[5]["col"]=$2; DT[5]["h"]=$3; DT[5]["w"]=$4
NW[5]["appid"]=org.x; NW[5]["par"]=0; NW[5]["own"]="300 200 13 38"
NW[5]["fit"]="2 4 15 40"; dt_natkeep 5; unset "NW[5]"; }
NW[6]["appid"]=org.x
pl5 10 30 15 40; x := dt_natrecall 6 15 40; echo "$x"
pl5 10 30 20 50; x := dt_natrecall 6 15 40; echo "$x"
NW[6]["limits"]="64 48 64 48"; x := dt_natrecall 6 15 40; echo "$x"
DT_NATREMEMBER=0; dt_natrecall 6 15 40 || echo none
""" % tree("programs/desktop/desktop.hibr")],
    env=dict(os.environ, XDG_STATE_HOME=pls), capture_output=True, text=True, timeout=30)
check("a program window is put back where the person moved it and as large as "
      "they made it, its size only where it may be resized, and not with the "
      "setting off", plr.stdout.split("\n")[:4] ==
      ["10 30 15 40", "10 30 20 50", "10 30 15 40", "none"], (plr.stdout, plr.stderr))

open(MARK + ".quit", "w").close()
t.keys([b"x"])
q = wait(lambda: comp("quit"))
t.quit(None, 0.5)
check("quitting the desktop ends the compositor at once", q,
      (comp()[-3:], [l for l in open(t.log).read().split("\n")
                     if "compositor" in l or "frames" in l][-6:]
       if os.path.exists(t.log) else "no desktop.log"))
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
# A compositor that takes the displays' layout (Blit 0.3.88, Gitea #514).
open(complog2 + ".version", "w").write("0.3.88\n")
wrap2 = os.path.join(D, "blit-compositor2")
open(wrap2, "w").write("#!/bin/sh\nexec %s %s %s \"$@\"\n"
                       % (sys.executable, os.path.join(HERE, "natserve.py"), complog2))
os.chmod(wrap2, 0o755)
MARK2 = os.path.join(D, "held")
p2 = sx.scratch("native-held.hibr")
open(p2, "w").write(
    "%s. %s\nDT_NATBIN=%s\nDT_SUPERVISE=off\n"
    "tst_quit() { [ -f %s ] && dt_quit; return 0; }\n"
    "tst_mn() { local a n; for a in min raise; do [ -f %s.$a ] || continue; rm -f %s.$a; "
    "for n in \"${!NWN[@]}\"; do if [ $a = min ]; then dt_min \"${NWN[$n]}\"; "
    "else dt_raise \"${NWN[$n]}\"; fi; done; dt_want 30; done; return 0; }\n"
    "dt_ticker tst_quit\ndt_ticker tst_mn\ndt_open\ndt_run\ndt_close\n"
    % (load("console"), tree("programs/desktop/desktop.hibr"), wrap2, MARK2 + ".quit",
       MARK2, MARK2))
hn = "nath-%d" % os.getpid()
henv = {"HIBR_GFX": "kitty", "TMPDIR": D, "XDG_RUNTIME_DIR": D}
subprocess.run([sx.HIBR, "-c", load("hold") + 'hold new -d %s "$HIBR" %s' % (hn, p2)],
               env=dict({k: v for k, v in os.environ.items()
                         if k not in ("WAYLAND_DISPLAY", "HIBR_HOLD")}, **henv),
               capture_output=True, timeout=20)
a0 = os.path.join(D, "att.hibr")
open(a0, "w").write(load("hold") + "hold attach -m -n top %s 0 0\n" % hn)
ht = Term(a0, rows=26, cols=82, settle=1.0, cellw=10, cellh=20, caps=["kitty"],
          env=dict(henv, BLIT_FRAME_SOCKET="@" + nm2, BLIT_FRAME_TOKEN="tok2"))


def hscr():
    ht.collect(0.1)
    return ht.screen()


def hk(k, **kw):
    return [x for x in lines(blitlog2) if x["kind"] == k and
            all(x.get(a) == b for a, b in kw.items())]


hw = wait(lambda: hscr().find("Stand-in"), 8)
hkeep = wait(lambda: hk("keep"), 6)
check("held, the chords the desktop keeps reach the display's Blit through "
      "hold (Gitea #530)", hkeep and "f10" in hkeep[-1]["chords"],
      [x for x in lines(blitlog2) if x["kind"] in ("keep", "input")][-4:])
hsock = wait(lambda: hk("input", verb="sock", ok=True), 6)
check("held, the compositor's window reaches the display, and its route by "
      "socket reaches the display's Blit through hold -- the window drawn "
      "in shared memory, as Firefox's own frame is",
      hw and hsock, (bool(hw), bool(hsock), [x for x in lines(blitlog2) if x["kind"] in ("input", "member", "hello")][-8:], hscr().text()[:900]))


def hlay():
    return [x["line"] for x in comp_of(complog2, "line")
            if x["line"].split()[:2] in (["WINDOW", "layout"], ["WINDOW", "primary"])]


lay = wait(lambda: "WINDOW primary top" in hlay() and hlay(), 6)
# The held display's cell is 10 by 20, not the 8 by 16 the desktop assumes
# before it has asked: the layout goes out as the compositor connects, before
# any window has, and was once in the default's pixels.
check("held, the compositor is told each display in pixels, then which one is "
      "primary -- what X's own outputs follow (Gitea #514)",
      lay and lay[0].startswith("WINDOW layout top 0 0 820 ") and
      lay[-2:] == ["WINDOW layout end", "WINDOW primary top"], lay or hlay())
hout = [x["line"] for x in comp_of(complog2, "line") if x["line"].startswith("WINDOW output ")]
check("and a window's output names its display, so two of one mode are told apart",
      hout and hout[-1].split()[-1] == "top", hout)
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
# Minimised, the window's pictures lose their placements on the display and
# keep their images and ids; raised, they are placed again under the same
# ids, so the input route at Blit still names them. A held display was
# never told before, and the window stayed on it unresponsive (#499).
def hapc(c0):
    return [m for m in re.findall(rb"\x1b_G(a=[pd][^;\x1b]*)", ht.out[c0:])]


c0 = len(ht.out)
open(MARK2 + ".min", "w").close()
ht.keys([b"x"])
gone = wait(lambda: hscr() and [m for m in hapc(c0) if m.startswith(b"a=d,d=i")], 6)
c1 = len(ht.out)
open(MARK2 + ".raise", "w").close()
ht.keys([b"x"])
back = wait(lambda: hscr() and [m for m in hapc(c1) if m.startswith(b"a=p")], 6)
ids = lambda ms: {re.search(rb"i=(\d+)", m).group(1) for m in ms}
check("held, a minimised program window leaves the display and keeps its "
      "pictures, and raised it comes back under the same ids",
      gone and back and ids(gone) <= ids(back) and
      not [m for m in hapc(c0) if b"d=I" in m and ids([m]) <= ids(gone)],
      (gone, back, [m for m in hapc(c0) if b"d=I" in m]))
ns, nmb, nkp = len(hk("input", verb="sock")), len(hk("member")), len(hk("keep"))
up = subprocess.run([sx.HIBR, "-c", load("hold") + "hold upgrade %s" % hn],
                    env=dict(os.environ, **henv), capture_output=True, text=True, timeout=30)
again = wait(lambda: len(hk("input", verb="sock")) > ns and len(hk("member")) > nmb
             and len(hk("keep")) > nkp, 10)
# And the chords the desktop keeps (Gitea #530), which the old server held
# in memory and the desktop says only when they change.
check("after `hold upgrade` both are said to the display's Blit again, and "
      "the chords the desktop keeps", up.returncode == 0 and again,
      (up.stderr, len(hk("input", verb="sock")), ns, len(hk("member")), nmb,
       len(hk("keep")), nkp))
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
    "tst_mv() { [ -f %s ] || return 0; local r; read -r r < %s; rm -f %s; local n; "
    "for n in \"${!NWN[@]}\"; do dt_move \"${NWN[$n]}\" \"${r:-19}\" 4; done; dt_want 30; }\n"
    "dt_ticker tst_mv\ndt_open\ndt_run\ndt_close\n"
    % (load("console"), tree("programs/desktop/desktop.hibr"), wrap3, MV, MV, MV))
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
# Moved wholly onto the lower display, its frames go there alone, and the
# last ones the upper display had -- which it keeps, since nothing newer of
# them comes there -- are given back to the program all the same: before,
# hold waited for the upper display to let them go, the compositor waited
# for them, and a program window moved to another display stopped updating
# there until it came back.
def unreleased():
    got = {}
    for x in comp_of(complog3, "line"):
        w = x["line"].split()
        if w[:2] == ["FRAME", "release"] and len(w) == 4:
            got.setdefault(w[2], set()).add(int(w[3]))
    return {k: sorted(set(range(min(v), max(v) - 3)) - v) for k, v in got.items() if v}


open(MV, "w").write("30\n")
# A key wakes the desktop, whose ticker moves the window: frames alone
# pass without waking it.
st1.keys([b"x"])
wait(lambda: False, 2.0 * SLOW)
gap = wait(lambda: not any(unreleased().values()) and unreleased(), 6)
check("moved wholly onto the other display, its frames still come back to "
      "the program, the ones the first display kept included",
      gap, unreleased())
st0.quit(None, 0.3)
st1.quit(None, 0.3)
subprocess.run([sx.HIBR, "-c", load("hold") + "hold kill %s" % hn3],
               env=dict(os.environ, **henv), capture_output=True, timeout=20)
pa.terminate()
pb.terminate()

# A program that says its decoration again while full screen -- an X
# program changing _MOTIF_WM_HINTS -- changes the frame its window gets back
# on leaving full screen, not the none it has meanwhile.
r = subprocess.run([sx.HIBR, "-c", load("console") +
                    ". %s\ndeclare -gA NW\nid := dt_new P 10 30 3 3\n"
                    "dt_fullscreen \"$id\" on\ndt_natdecor \"$id\" client\n"
                    "a=\"${DT[$id][\"chrome\"]}/${DT[$id][\"fschrome\"]}\"\n"
                    "dt_fullscreen \"$id\" off\necho \"$a ${DT[$id][\"chrome\"]}\"\n"
                    % tree("programs/desktop/desktop.hibr")],
                   capture_output=True, text=True, timeout=30)
check("a program window's frameless decoration said while full screen stays "
      "frameless after it", r.stdout.strip() == "none/none none", (r.stdout, r.stderr))

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
report(66)
