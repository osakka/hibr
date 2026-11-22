#!/usr/bin/env python3
"""Pictures an app in a process of its own draws reach every display without
passing through the desktop (ADR 0050, "Pictures, by hold").

An app's process is a child inside the held session, so it hands its frames
to hold's server itself, under slot numbers of its own; the desktop places
each picture on its own terminal under an image id it takes fresh and tells
hold which slot that id is written from. Two parts, each against two stacked
displays with stand-in Blits (frameserve.py):

  A. Hold's own wire, driven by stand-in apps and a stand-in desktop: a
     granted slot reaches both displays under its id; an app cannot reach an
     id it was not granted, nor be granted one another sender writes, nor
     write over another's, nor mix kinds on one; releases go back to the
     sender, by its own slot; a placement waits for its first frame; a flood
     is capped and counted while the desktop's frames still flow; a display
     whose Blit drops it gives every app's frames back; revoking ends it.
  B. The console end to end: a held desktop's cells channel and an app's own
     console drawing `img draw -m pixels`, the picture placed by the desktop
     under an id of its own band, its frames from the app straight to hold,
     a film one id, the desktop not woken by it, hidden and shown without
     deleting, gone for good with a fresh id after, and the app's end
     revoking it -- and outside hold, the app draws cells.
"""
import array
import fcntl
import json
import os
import re
import select
import socket
import struct
import subprocess
import sys
import time

T_FRAME, T_FRM3, T_FRMOPEN, T_FRMANS = b"I", b"N", b"O", b"Q"
T_APPOPEN, T_APPCTL, T_APPLIST = b"1", b"2", b"3"


def hsend(s, t, payload=b"", fds=()):
    """One message of hold's wire: a type byte, carrying any descriptors,
    then a length and the payload."""
    if fds:
        socket.send_fds(s, [t], list(fds))
    else:
        s.sendall(t)
    s.sendall(struct.pack("=I", len(payload)) + payload)


def recvn(s, n):
    b = b""
    while len(b) < n:
        d = s.recv(n - len(b))
        if not d:
            raise EOFError
        b += d
    return b


def hrecv(s):
    t = recvn(s, 1)
    n = struct.unpack("=I", recvn(s, 4))[0]
    return t, recvn(s, n) if n else b""


def memfd(w, h, rgb, size=None):
    fd = os.memfd_create("f", os.MFD_ALLOW_SEALING)
    if size is None:
        os.write(fd, bytes(rgb) * (w * h))
    else:
        os.ftruncate(fd, size)
    fcntl.fcntl(fd, 1033, 0x2 | 0x4 | 0x8)
    return fd


def hdr1(iid, seq, w, h):
    return b"FRM1" + struct.pack("<IQIIIIIIII", iid, seq, w, h, 24, w * 3,
                                 0, 0, 0, 0)


def hdr3(iid, seq):
    return (b"FRM3" + struct.pack("<IQII", iid, seq, 64, 16) + b"XR24" +
            struct.pack("<IQ", 1, 0) + struct.pack("<II", 0, 256) + bytes(24))


def producer(path, kind, out):
    """A stand-in app (HD_APPOPEN) or desktop (HD_FRMOPEN) on the server at
    path: commands a line on standard input, what comes back a line in out."""
    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    s.connect(path)
    hsend(s, T_APPOPEN if kind == "app" else T_FRMOPEN)
    plane = os.memfd_create("plane")
    os.ftruncate(plane, 64 * 16 * 4)
    log = open(out, "a", buffering=1)
    log.write("pid %d\n" % os.getpid())
    # Standard input is read a block at a time and split here: a line
    # left in a file object's own buffer is one select never reports.
    pend = b""
    while True:
        r, _, _ = select.select([0, s], [], [], 0.2)
        if s in r:
            try:
                t, p = hrecv(s)
            except (EOFError, OSError):
                log.write("closed\n")
                break
            log.write("ans %s\n" % p.decode(errors="replace"))
        if 0 not in r:
            continue
        got = os.read(0, 65536)
        if not got:
            break
        pend += got
        stop = False
        while b"\n" in pend:
            ln, pend = pend.split(b"\n", 1)
            w = ln.decode().split()
            if not w:
                continue
            if w[0] == "quit":
                stop = True
                break
            try:
                if w[0] == "mem":
                    slot, seq, sz = int(w[1]), int(w[2]), int(w[3])
                    rgb = [int(x) for x in w[4:7]]
                    big = int(w[7]) if len(w) > 7 else None
                    fd = memfd(sz, sz, rgb, big)
                    hsend(s, T_FRAME, hdr1(slot, seq, sz, sz), [fd])
                    os.close(fd)
                elif w[0] == "dma":
                    hsend(s, T_FRM3, hdr3(int(w[1]), int(w[2])), [plane])
                elif w[0] == "ctl":
                    hsend(s, T_APPCTL, " ".join(w[1:]).encode())
                log.write("sent %s\n" % " ".join(w))
            except OSError as e:
                log.write("error %s\n" % e)
        if stop:
            break


if len(sys.argv) > 3 and sys.argv[1] in ("app", "desk"):
    producer(sys.argv[2], sys.argv[1], sys.argv[3])
    sys.exit(0)

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import screen
from screen import Term, check, report, load, tree

HERE = os.path.dirname(os.path.abspath(__file__))
src = open(os.path.join(HERE, "frames.py")).read()
exec(compile(src[:src.index("# Nothing between")], "frames.py", "exec"))


def pngfile(name, w, h, f):
    import zlib
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


def memframes(lg, iid=None):
    return [x for x in lines(lg) if x["kind"] == "frame"
            and (iid is None or x["id"] == iid)]


def dmaframes(lg, iid=None):
    return [x for x in lines(lg) if x["kind"] == "frame3"
            and (iid is None or x["id"] == iid)]


def refused(lg, why=None):
    return [x for x in lines(lg) if x["kind"] == "refused"
            and (why is None or x.get("why") == why)]


def ctls(t, since=0):
    t.collect(0.3)
    raw = t.out[since:].decode("latin-1")
    out = []
    for m in re.finditer(r"\x1b_G([^;\x1b]*)", raw):
        out.append(dict(kv.split("=", 1) for kv in m.group(1).split(",")
                        if "=" in kv))
    return out


def placed(cs, iid):
    return [c for c in cs if c.get("a") == "p" and c.get("i") == str(iid)]


def deleted(cs, iid, how):
    return [c for c in cs if c.get("a") == "d" and c.get("i") == str(iid)
            and c.get("d") == how]


ENV = {"HIBR_GFX": "kitty", "TMPDIR": D}


def apps(nm):
    """What hold apps says: pid -> [slots, frames, refused, bytes, early]."""
    r = subprocess.run([screen.HIBR, "-c", load("hold") + "hold apps %s" % nm],
                       env=dict(os.environ, **ENV), capture_output=True, text=True)
    out = {}
    for ln in r.stdout.splitlines():
        w = ln.split()
        if len(w) == 6:
            out[int(w[0])] = [int(x) for x in w[1:]]
    return out


class Prod:
    """A stand-in producer in a process of its own, so hold knows it by a
    pid of its own."""

    def __init__(self, path, kind, tag):
        self.out = os.path.join(D, "prod-%s.log" % tag)
        open(self.out, "w").close()
        self.p = subprocess.Popen([sys.executable, os.path.abspath(__file__), kind,
                                   path, self.out], stdin=subprocess.PIPE,
                                  text=True)
        self.pid = self.p.pid

    def say(self, *ws):
        try:
            self.p.stdin.write(" ".join(str(w) for w in ws) + "\n")
            self.p.stdin.flush()
        except (BrokenPipeError, ValueError):
            pass

    def log(self):
        return open(self.out).read().splitlines()

    def answers(self):
        return [l[4:] for l in self.log() if l.startswith("ans ")]

    def released(self):
        out = []
        for a in self.answers():
            w = a.split()
            if len(w) == 4 and w[:2] == ["FRAME", "release"]:
                out.append((int(w[2]), int(w[3])))
        return out

    def quit(self):
        self.say("quit")
        try:
            self.p.stdin.close()
        except (BrokenPipeError, ValueError):
            pass
        try:
            self.p.wait(3)
        except subprocess.TimeoutExpired:
            self.p.kill()


def session(tag, prog):
    """A held program on two stacked displays, each with a stand-in Blit
    that imports dmabufs: the displays' terminals and their Blits' logs."""
    pa, la, ea = blit(tag + "t")
    pb, lb, eb = blit(tag + "b")
    nm = "hpx%s-%d" % (tag, os.getpid())
    a0 = os.path.join(D, "%s-a0.hibr" % tag)
    open(a0, "w").write(load("hold") + 'hold new -d %s "$HIBR" %s\nsleep 0.6\n'
                        'hold attach -m -n top %s 0 0\n' % (nm, prog, nm))
    t0 = Term(a0, rows=12, cols=60, settle=1.0, cellw=8, cellh=16,
              env=dict(ENV, **ea), caps=["kitty"])
    t0.collect(1.0 * SLOW)
    a1 = os.path.join(D, "%s-a1.hibr" % tag)
    open(a1, "w").write(load("hold") + 'hold attach -m -n bottom %s 12 0\n' % nm)
    t1 = Term(a1, rows=12, cols=60, settle=1.0, cellw=8, cellh=16,
              env=dict(ENV, **eb), caps=["kitty"])
    t1.collect(1.0 * SLOW)
    return nm, t0, t1, la, lb, (pa, pb)


def finish(nm, t0, t1, ps):
    t1.quit(None, 0.3)
    t0.quit(None, 0.3)
    subprocess.run([screen.HIBR, "-c", load("hold") + "hold kill %s" % nm],
                   env=dict(os.environ, **ENV), capture_output=True)
    for p in ps:
        p.kill()


# A. Hold's own wire. The held program places what it is told to, as the
# desktop's console would: an a=p for an id, or that id deleted.
oa = os.path.join(D, "a")
proga = os.path.join(D, "progA-%d.hibr" % os.getpid())
open(proga, "w").write(
    "echo \"$HIBR_HOLD\" > %(o)s.hold\n"
    "last=\n"
    "while [ ! -f %(o)s.stop ]; do\n"
    "  c=; [ -f %(o)s.cmd ] && read -r c < %(o)s.cmd\n"
    "  if [ -n \"$c\" ] && [ \"$c\" != \"$last\" ]; then\n"
    "    last=$c\n"
    "    read -r n v id r col <<< \"$c\"\n"
    "    case $v in\n"
    "    place) printf '\\e[%%d;%%dH\\e_Ga=p,q=2,C=1,i=%%d,p=1,c=8,r=4\\e\\\\' \"$r\" \"$col\" \"$id\" ;;\n"
    "    del) printf '\\e_Ga=d,d=I,q=2,i=%%d\\e\\\\' \"$id\" ;;\n"
    "    esac\n"
    "    echo \"$n\" > %(o)s.done\n"
    "  fi\n"
    "  sleep 0.1\n"
    "done\n" % {"o": oa})
nma, ta0, ta1, laa, lab, psa = session("a", proga)
hp = wait(lambda: open(oa + ".hold").read().strip() if os.path.exists(oa + ".hold") else None, 5)
NCMD = [0]


def held(verb, iid=0, r=0, c=0):
    NCMD[0] += 1
    open(oa + ".cmd.tmp", "w").write("%d %s %d %d %d\n" % (NCMD[0], verb, iid, r, c))
    os.rename(oa + ".cmd.tmp", oa + ".cmd")
    n = NCMD[0]
    wait(lambda: os.path.exists(oa + ".done") and
         open(oa + ".done").read().strip() == str(n), 3)
    time.sleep(0.3 * SLOW)


def oneshot(iid, seq, rgb):
    """A frame as the desktop's console sends one of its own: a connection
    of its own, the newest frame, gone."""
    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    s.connect(hp)
    fd = memfd(4, 4, rgb)
    hsend(s, T_FRAME, hdr1(iid, seq, 4, 4), [fd])
    os.close(fd)
    s.close()


IA, IB, IE, DESK = 0x7A000001, 0x7A000002, 0x7A000005, 0x48000077
desk = Prod(hp, "desk", "desk")
A = Prod(hp, "app", "A")
B = Prod(hp, "app", "B")
time.sleep(0.5 * SLOW)
check("an app's connection is told whether pictures may go, and what a cell "
      "measures: every display takes frames and draws kitty pictures",
      "PIC 8 16" in A.answers() and "PIC 8 16" in B.answers(), (A.log(), B.log()))

# The desktop's own picture at the top, its frames sent as its console
# sends them; the apps' pictures across the seam.
held("place", DESK, 2, 2)
oneshot(DESK, 1, (10, 20, 30))
desk.say("ctl", "grant", A.pid, 1, IA)
desk.say("ctl", "grant", B.pid, 1, IB)
held("place", IA, 10, 2)
held("place", IB, 10, 20)
for s in range(1, 6):
    A.say("dma", 1, s)
    B.say("dma", 1, 100 + s)
    time.sleep(0.1)
time.sleep(1.0 * SLOW)
fa = [(x["id"], x["seq"]) for x in dmaframes(laa)]
fb = [(x["id"], x["seq"]) for x in dmaframes(lab)]
check("each app's frames reach both displays' Blits under the id its slot was "
      "granted, never under the slot",
      {s for i, s in fa if i == IA} == set(range(1, 6)) and
      {s for i, s in fb if i == IA} == set(range(1, 6)) and
      {s for i, s in fa if i == IB} == set(range(101, 106)) and
      not [i for i, s in fa + fb if i == 1], (fa, fb))
ra, rb, ab = A.released(), B.released(), apps(nma)
check("releases go back to the app that sent the frame, by its own slot: "
      "the same slot and the sequences apart, nothing crosses, and none "
      "of them given back unshown", ra and rb and
      all(sl == 1 and 1 <= q <= 5 for sl, q in ra) and
      all(sl == 1 and 101 <= q <= 105 for sl, q in rb) and
      ab.get(A.pid, [0] * 5)[4] == 0 and ab.get(B.pid, [0] * 5)[4] == 0,
      (ra, rb, ab))

# An app cannot reach an id it was not granted, nor take one over.
A.say("mem", DESK, 1, 4, 200, 0, 0)
time.sleep(0.6 * SLOW)
desk.say("ctl", "grant", A.pid, DESK, DESK)
time.sleep(0.3)
A.say("mem", DESK, 2, 4, 200, 0, 0)
time.sleep(0.6 * SLOW)
oneshot(DESK, 3, (10, 20, 30))
time.sleep(0.6 * SLOW)
dk = memframes(laa, DESK)
check("an app sending under the desktop's own id as its slot never reaches "
      "it, and the id cannot be granted to it either: the desktop's frames "
      "alone are there", dk and all(x["first"] == [10, 20, 30] for x in dk)
      and {x["seq"] for x in dk} >= {1, 3}, dk)
desk.say("dma", IA, 999)
oneshot(IB, 999, (1, 2, 3))
time.sleep(0.6 * SLOW)
check("and nobody else writes an app's id: the desktop's own frames for it, "
      "by dmabuf or by memfd, are refused before any display",
      not [x for x in dmaframes(laa) + dmaframes(lab) if x["seq"] == 999]
      and not memframes(laa, IB) and not memframes(lab, IB),
      (dmaframes(laa), memframes(laa)))
n0 = apps(nma).get(A.pid, [0, 0, 0])[2]
A.say("mem", 1, 50, 4, 9, 9, 9)
time.sleep(0.5 * SLOW)
check("an app cannot mix kinds on one image: a memfd for a slot it sends "
      "dmabufs on is refused, and counted",
      not memframes(laa, IA) and apps(nma).get(A.pid, [0, 0, 0])[2] > n0,
      apps(nma))

# A placement waits for its image's first frame.
E = Prod(hp, "app", "E")
time.sleep(0.4)
c0, c1 = len(ta0.out), len(ta1.out)
desk.say("ctl", "grant", E.pid, 3, IE)
held("place", IE, 2, 30)
time.sleep(0.5 * SLOW)
before = placed(ctls(ta0, c0), IE)
E.say("mem", 3, 1, 4, 0, 200, 0)
time.sleep(1.0 * SLOW)
after = placed(ctls(ta0, c0), IE)
check("a granted id's placement is not handed to a display before its first "
      "frame, which Blit would count against the eight a session may have "
      "waiting; the frame brings it", not before and after and
      memframes(laa, IE), (before, after))

# Caps: a flood is refused past HD_APPFPS, counted, never cut off, while the
# desktop's own frames flow.
d0 = len(memframes(laa, DESK))
end = time.time() + 1.2
seq = 1
while time.time() < end:
    seq += 1
    E.say("mem", 3, seq, 4, 0, 0, (seq * 7) % 256)
    if seq % 10 == 0:
        oneshot(DESK, 10 + seq, (10, 20, 30))
    time.sleep(0.004)
time.sleep(1.0 * SLOW)
ae = apps(nma).get(E.pid)
check("an app flooding frames is capped at its rate and the rest refused "
      "and counted, while it stays connected",
      ae and ae[2] > 0 and ae[1] <= seq and "closed" not in E.log(), (ae, seq))
check("and the desktop's own frames go on reaching the display meanwhile",
      len(memframes(laa, DESK)) - d0 >= 5, len(memframes(laa, DESK)) - d0)
time.sleep(1.1)
n0 = apps(nma)[E.pid][2]
E.say("mem", 9, 1, 4, 0, 0, 0, 40 << 20)
time.sleep(0.4)
for k in range(20):
    E.say("mem", 20 + k, 1, 4, 0, 0, 0)
time.sleep(0.8 * SLOW)
ae = apps(nma)[E.pid]
check("a frame larger than an app may send, and more pictures than it may "
      "keep, are refused too", ae[2] - n0 >= 5 and ae[0] <= 16, ae)

# A display whose Blit drops it gives back every app's frames it held.
open(lab + ".drop", "w").close()
time.sleep(1.0 * SLOW)
r0 = set(A.released())
A.say("dma", 1, 6)
time.sleep(1.0 * SLOW)
check("a display whose Blit closed on it holds no app's frames any more: "
      "the one it showed comes back at once when a newer one replaces it",
      (1, 5) in set(A.released()) - r0, (A.released(), r0))

# Revoking an app ends its connection and its pictures.
desk.say("ctl", "revoke", B.pid)
time.sleep(0.6 * SLOW)
check("revoking an app closes its connection and forgets its pictures",
      "closed" in B.log() and B.pid not in apps(nma), (B.log()[-3:], apps(nma)))

for p in (A, B, E, desk):
    p.quit()
open(oa + ".stop", "w").close()
finish(nma, ta0, ta1, psa)

# B. The console end to end.
ob = os.path.join(D, "b")
G = [pngfile("g%d.png" % k, 32, 32, lambda x, y, k=k: (x * 7 % 256, y * 7 % 256, 60 + 90 * k))
     for k in range(3)]
P = [pngfile("p%d.png" % k, 32, 32, lambda x, y, k=k: (200, (x + 40 * k) % 256, y * 7 % 256))
     for k in range(3)]
SOCK = os.path.join(D, "cw.sock")
appb = os.path.join(D, "appB-%d.hibr" % os.getpid())
open(appb, "w").write(
    "need console; need img\n"
    "connect -U %(s)s FD || exit 1\n"
    "console cells open \"$FD\" || exit 1\n"
    "console cells view v1 1 8 26\n"
    "echo $$ > %(o)s.apppid\n"
    "fn draw(p) {\n"
    "  console cells clear v1\n"
    "  console pen default default\n"
    "  console put -p v1 0 0 hello\n"
    "  [ -z \"$p\" ] || img draw \"$p\" 1 1 6 12 -m pixels -p v1\n"
    "  [ -z \"${fsn-}\" ] || frames place \"$fsn\" v1 1 14 6 10\n"
    "  console flush > /dev/null\n"
    "}\n"
    "fsn=\n"
    "last=\n"
    "while :; do\n"
    "  c=; [ -f %(o)s.cmd ] && read -r c < %(o)s.cmd\n"
    "  if [ -n \"$c\" ] && [ \"$c\" != \"$last\" ]; then\n"
    "    last=$c\n"
    "    read -r n v a b <<< \"$c\"\n"
    "    case $v in\n"
    "    pic) draw \"$a\" ;;\n"
    "    stream) need frames; x := frames serve; read -r fsn fso fst <<< \"$x\"\n"
    "      printf '%%s %%s\\n' \"$fso\" \"$fst\" > %(o)s.fsock; draw \"$a\" ;;\n"
    "    none) draw \"\" ;;\n"
    "    regen) console cells view v1 2 8 28; draw \"\" ;;\n"
    "    film) for ((i = 0; i < a; i++)); do draw \"%(p)s$((i %% 2)).png\"; sleep 0.05; done ;;\n"
    "    flood) for ((i = 0; i < a; i++)); do draw \"%(p)s$((i %% 3)).png\"; done ;;\n"
    "    quit) exit 0 ;;\n"
    "    esac\n"
    "    echo \"$n\" > %(o)s.done\n"
    "  fi\n"
    "  console key 100 > /dev/null\n"
    "done\n" % {"o": ob, "s": SOCK, "p": os.path.join(D, "p")})
deskb = os.path.join(D, "deskB-%d.hibr" % os.getpid())
open(deskb, "w").write(
    "need console; need img\n"
    "console open\n"
    "console imgfd hold\n"
    "rm -f %(s)s\n"
    "listen -b %(s)s LFD || exit 1\n"
    "\"$HIBR\" %(a)s 2>> %(o)s.apperr &\n"
    "accept -t 20 \"$LFD\" CFD || exit 1\n"
    "console watch \"$CFD\"\n"
    "console cells own \"$CFD\" v1 1 w1 1 1 8 26 || exit 1\n"
    "n=0\n"
    "while [ ! -f %(o)s.stop ]; do\n"
    "  r=8; [ -f %(o)s.at ] && read -r r < %(o)s.at\n"
    "  console pane w1 $r 2 10 30\n"
    "  console pane w2 2 34 8 16\n"
    "  console pen default default\n"
    "  console put -p w1 0 0 WIN\n"
    "  if [ -n \"$CFD\" ] && [ -f %(o)s.regen ] && [ -z \"${rg-}\" ]; then\n"
    "    console cells own \"$CFD\" v1 2 w1 1 1 8 28; rg=1; echo x > %(o)s.regened\n"
    "  fi\n"
    "  if [ -n \"$CFD\" ] && [ -f %(o)s.close ]; then\n"
    "    console unwatch \"$CFD\"; console cells drop \"$CFD\"; exec {CFD}>&-; CFD=; echo x > %(o)s.dropped\n"
    "  fi\n"
    "  if [ -n \"$CFD\" ]; then\n"
    "    st=0\n"
    "    [ -f %(o)s.gone ] || { x := console cells take \"$CFD\" || st=$?; }\n"
    "    if [ \"$st\" != 0 ]; then\n"
    "      console unwatch \"$CFD\"; echo gone > %(o)s.gone\n"
    "    fi\n"
    "    [ -f %(o)s.hide ] || console cells show v1 -a\n"
    "  fi\n"
    "  k=0; [ -f %(o)s.anim ] && k=$((n %% 2))\n"
    "  img draw %(g)s$k.png 1 1 6 12 -m pixels -p w2\n"
    "  console flush\n"
    "  n=$((n + 1)); echo $n > %(o)s.passes\n"
    "  t=1000; [ -f %(o)s.anim ] && t=50\n"
    "  console key $t > /dev/null\n"
    "done\n" % {"o": ob, "s": SOCK, "a": appb, "g": os.path.join(D, "g")})
nmb, tb0, tb1, lba, lbb, psb = session("b", deskb)
apid = wait(lambda: open(ob + ".apppid").read().strip() if os.path.exists(ob + ".apppid") else None, 5)
apid = int(apid) if apid else 0
NB = [0]


def appdo(*w):
    NB[0] += 1
    n = NB[0]
    open(ob + ".cmd.tmp", "w").write("%d %s\n" % (n, " ".join(str(x) for x in w)))
    os.rename(ob + ".cmd.tmp", ob + ".cmd")
    wait(lambda: os.path.exists(ob + ".done") and
         open(ob + ".done").read().strip() == str(n), 10)
    time.sleep(1.5 * SLOW)


def passes():
    try:
        return int(open(ob + ".passes").read())
    except (OSError, ValueError):
        return 0


c0, c1 = len(tb0.out), len(tb1.out)
appdo("pic", P[0])
dsk = [x["id"] for x in memframes(lba) if x["first"][:3] == [0, 0, 60]]
app = [x["id"] for x in memframes(lba) if x["first"][:3] == [200, 0, 0]]
appb_ = [x["id"] for x in memframes(lbb) if x["first"][:3] == [200, 0, 0]]
I = app[0] if app else 0
check("an app's picture, drawn with img draw in its own process, reaches both "
      "displays' Blits as frames, across the seam",
      I and I in appb_, (memframes(lba), memframes(lbb)))
check("under an id the desktop took from its own band, apart from its own "
      "picture's", I and dsk and I != dsk[0] and I >> 14 == dsk[0] >> 14,
      (I, dsk))
s0, s1 = ctls(tb0, c0), ctls(tb1, c1)
check("and the desktop places it on each display's terminal under that id",
      placed(s0, I) and placed(s1, I), (s0, s1))
ap = apps(nmb).get(apid)
check("hold knows the app by its process, one picture, its frame taken",
      ap and ap[0] == 1 and ap[1] >= 1 and ap[2] == 0, apps(nmb))

# A program's frames by dmabuf through the app's own process (the frames
# module there, as Blit's compositor windows are in the desktop's): its
# stream placed beside the picture, its frames to hold on the app's own
# connection, and what the displays say of them back to the program in its
# own numbers.
appdo("stream", P[0])
fs = open(ob + ".fsock").read().split() if os.path.exists(ob + ".fsock") else None
pb = Bridge(*fs) if fs else None
# A stream is placed once its first frame has come, as a window places it
# from its next drawing: the app draws again after it.
if pb:
    pb.send(42)
time.sleep(0.5 * SLOW)
appdo("pic", P[0])
f3a, f3b = len(dmaframes(lba)), len(dmaframes(lbb))
for _ in range(6):
    if pb:
        pb.send(42)
    time.sleep(0.15 * SLOW)
time.sleep(1.0 * SLOW)
sa = [x["id"] for x in dmaframes(lba)[f3a:]]
sb = [x["id"] for x in dmaframes(lbb)[f3b:]]
check("a program's dmabuf frames, sent to the frames module in an app's own "
      "process, reach both displays' Blits under an id of the desktop's band",
      sa and sb and set(sa) == set(sb) and len(set(sa)) == 1 and sa[0] != I
      and sa[0] >> 14 == I >> 14, (sa, sb, I))
rel = [a for a in (pb.answers(1.0) if pb else []) if a.startswith("FRAME release")]
check("and the displays' releases come back to the program as its own id",
      rel and all(a.split()[2] == "42" for a in rel), rel)
ap = apps(nmb).get(apid)
check("hold keeps the app's two pictures apart, each its own slot",
      ap and ap[0] == 2 and ap[2] == 0, apps(nmb))

p0 = passes()
f0 = len(memframes(lba, I))
appdo("film", 20)
fr = memframes(lba, I)[f0:]
check("a film of the same size is one id, its frames rising: no new image a "
      "frame", len(fr) >= 15 and [x["seq"] for x in fr] == sorted(x["seq"] for x in fr)
      and not [x for x in memframes(lba)[len(memframes(lba)) - len(fr):]
               if x["first"][0] == 200 and x["id"] != I], (len(fr), memframes(lba)[-5:]))
check("and its frames never wake the desktop: they go from the app to hold",
      passes() - p0 <= 6 * SLOW, passes() - p0)

c0, c1 = len(tb0.out), len(tb1.out)
open(ob + ".hide", "w").close()
time.sleep(2.0 * SLOW)
s0 = ctls(tb0, c0)
check("a window not shown takes its picture's placement away and keeps the "
      "image, so it can come back", deleted(s0, I, "i") and not deleted(s0, I, "I"),
      s0)
c0 = len(tb0.out)
os.unlink(ob + ".hide")
time.sleep(2.0 * SLOW)
check("and shown again it is placed under the same id",
      placed(ctls(tb0, c0), I), ctls(tb0, c0))

c0, c1 = len(tb0.out), len(tb1.out)
sl0 = apps(nmb).get(apid, [0])[0]
appdo("none")
s0 = ctls(tb0, c0)
check("a picture the app no longer draws is deleted for good, and hold lets "
      "its slot go", deleted(s0, I, "I") and sl0 >= 1 and
      apps(nmb).get(apid, [9])[0] == sl0 - 1,
      (s0, apps(nmb)))
for lg in (lba, lbb):
    open(lg + ".del", "w").write("%d\n" % I)
time.sleep(0.5)
appdo("pic", P[1])
app2 = [x["id"] for x in memframes(lba) if x["first"][:2] == [200, 40]]
check("a picture after it has a fresh id, and no frame anywhere is refused "
      "as one for a deleted image",
      app2 and app2[-1] != I and not refused(lba, "deleted") and
      not refused(lbb, "deleted"), (app2, refused(lba)))

# A picture let go in a frame the desktop skips -- drawn for a size the
# window had already left -- still goes: the next frame starts the view
# again and says every picture it has, and this one is not among them.
K = app2[-1] if app2 else 0
open(ob + ".regen", "w").close()
wait(lambda: os.path.exists(ob + ".regened"), 5)
time.sleep(0.5 * SLOW)
c0, c1 = len(tb0.out), len(tb1.out)
appdo("none")
mid = deleted(ctls(tb0, c0), K, "I")
appdo("regen")
s0 = ctls(tb0, c0)
check("a picture whose going was said in a frame the desktop skipped, for a "
      "size the window had left, is deleted all the same when the view "
      "starts again without it", not mid and deleted(s0, K, "I"), (mid, s0))

def deskframes():
    return [x for x in memframes(lba) if x["first"][:2] == [0, 0]]


d0 = len(deskframes())
open(ob + ".anim", "w").close()
appdo("flood", 150)
os.unlink(ob + ".anim")
ap = apps(nmb).get(apid)
dn = deskframes()[d0:]
check("an app drawing faster than it may is refused past its rate and "
      "counted in hold, and the desktop's own picture keeps changing on the "
      "display meanwhile", ap and ap[2] > 0 and len(dn) >= 5,
      (ap, len(dn), dn[-4:]))

J = app2[-1] if app2 else 0
fl = [x["id"] for x in memframes(lba) if x["first"][0] == 200]
J = fl[-1] if fl else J
appdo("quit")
wait(lambda: os.path.exists(ob + ".gone"), 5)
open(ob + ".hide", "w").close()
time.sleep(2.0 * SLOW)
c0, c1 = len(tb0.out), len(tb1.out)
os.unlink(ob + ".hide")
time.sleep(2.0 * SLOW)
s0, s1 = ctls(tb0, c0), ctls(tb1, c1)
ap = apps(nmb).get(apid)
check("an app that has ended leaves its window its last picture, as Control "
      "Panel's default keeps it: hidden and shown again, it is placed on both "
      "displays, hold keeping its frame", os.path.exists(ob + ".gone") and
      placed(s0, J) and placed(s1, J) and ap and ap[0] >= 1, (J, s0, s1, ap))
c0, c1 = len(tb0.out), len(tb1.out)
open(ob + ".close", "w").close()
wait(lambda: os.path.exists(ob + ".dropped"), 5)
time.sleep(2.0 * SLOW)
s0, s1 = ctls(tb0, c0), ctls(tb1, c1)
check("and when the window lets it go, the picture is deleted for good on "
      "every display and hold forgets the app, its frame with it",
      deleted(s0, J, "I") and deleted(s1, J, "I") and apid not in apps(nmb),
      (s0, s1, apps(nmb)))
open(ob + ".stop", "w").close()
time.sleep(0.5)
finish(nmb, tb0, tb1, psb)

# C. The real path: a held test desktop, an installed app started in its
# own process by the desktop (dt_rthost, runtime.hibr), its _draw drawing a
# picture with img draw, the window shown from its kept cells (dt_rvshow),
# minimised and back, its app killed with its window kept (Control Panel's
# default), and the window closed at last (dt_rvletgo).
oc = os.path.join(D, "c")
PICAPP = os.path.join(D, "picapp-%d.hibr" % os.getpid())
open(PICAPP, "w").write(
    "need img\n"
    "command -v dt_app > /dev/null && dt_app picapp \"Pic\" 10 30\n"
    "fn picapp_draw(int id, int h, int w, int row, int col) {\n"
    "\tconsole put -p \"w$id\" 1 1 \"picture\"\n"
    "\timg draw %s 2 1 6 12 -m pixels -p \"w$id\"\n"
    "}\n" % P[2])
sessc = os.path.join(D, "sessC-%d.hibr" % os.getpid())
open(sessc, "w").write(
    "%s. %s\n"
    "dt_open\n"
    "DT_NOTEMS=60000\n"
    "DT_LOADING=%s dt_app picapp \"Pic\" 10 30\n"
    "DT_LOADING=\n"
    "tst_go() {\n"
    "\t[ -f %s.do ] || { dt_want 100; return 0; }\n"
    "\tlocal c\n"
    "\tread -r c < %s.do\n"
    "\tcommand rm -f %s.do\n"
    "\teval \"$c\"\n"
    "\tdt_want 30\n"
    "}\n"
    "dt_ticker tst_go\n"
    "dt_run\n"
    "dt_close\n" % (load(tree("build/mods/console.so")),
                    tree("programs/desktop/desktop.hibr"), PICAPP, oc, oc, oc))
nmc, tc0, tc1, lca, lcb, psc = session("c", sessc)


def cdo(cmd):
    open(oc + ".do.tmp", "w").write(cmd + "\n")
    os.rename(oc + ".do.tmp", oc + ".do")
    wait(lambda: not os.path.exists(oc + ".do"), 10)


def cres(cmd):
    if os.path.exists(oc + ".pend"):
        os.unlink(oc + ".pend")
    cdo(cmd + " > %s.pend" % oc)
    wait(lambda: os.path.exists(oc + ".pend"), 10)
    time.sleep(0.05)
    return open(oc + ".pend").read().strip() if os.path.exists(oc + ".pend") else ""


def apppic(lg):
    return [x["id"] for x in memframes(lg) if x["first"][:2] == [200, 80]]


cdo("dt_rthost picapp")
wait(lambda: cres('echo "${DT_RTHOST[picapp]-}"'), 10 * SLOW)
c0, c1 = len(tc0.out), len(tc1.out)
cdo("W := dt_new Pic 10 30 7 4 picapp")
wait(lambda: apppic(lca) and apppic(lcb), 10 * SLOW)
time.sleep(1.0 * SLOW)
pa, pb_ = apppic(lca), apppic(lcb)
Ic = pa[-1] if pa else 0
cpid = cres('echo "${DT_RTCPID[${DT_RVC[$W]}]-}"')
check("through the real runtime: an installed app started in its own process "
      "draws a picture with img draw, and its frames reach both displays' "
      "Blits from that process", Ic and Ic in pb_ and cpid.isdigit()
      and int(cpid) in apps(nmc), (pa, pb_, cpid, apps(nmc)))
s0, s1 = ctls(tc0, c0), ctls(tc1, c1)
check("and the desktop places it on both displays' terminals where the "
      "window shows it", placed(s0, Ic) and placed(s1, Ic), (s0[-6:], s1[-6:]))
c0 = len(tc0.out)
cdo("dt_min $W")
time.sleep(1.5 * SLOW)
s0 = ctls(tc0, c0)
c0 = len(tc0.out)
cdo("dt_min $W")
time.sleep(1.5 * SLOW)
s0b = ctls(tc0, c0)
check("minimised, the window's picture loses its placement and keeps its "
      "image; shown again, it is placed under the same id",
      deleted(s0, Ic, "i") and not deleted(s0, Ic, "I") and placed(s0b, Ic),
      (s0, s0b))
c0, c1 = len(tc0.out), len(tc1.out)
cres("kill -9 %s; echo" % cpid)
wait(lambda: cres('echo "${DT_RVC[$W]-}"') == "-", 10 * SLOW)
time.sleep(1.0 * SLOW)
cdo("dt_min $W")
time.sleep(1.0 * SLOW)
c0, c1 = len(tc0.out), len(tc1.out)
cdo("dt_min $W")
time.sleep(1.5 * SLOW)
s0, s1 = ctls(tc0, c0), ctls(tc1, c1)
ac = apps(nmc).get(int(cpid) if cpid.isdigit() else 0)
check("its app killed, the window keeps its last picture as the Control "
      "Panel's default says: minimised and shown again, it is placed on "
      "both displays, hold keeping its frame", cres('echo "${DT_RVC[$W]-}"') == "-"
      and placed(s0, Ic) and placed(s1, Ic) and ac and ac[0] == 1, (s0, s1, ac))
c0, c1 = len(tc0.out), len(tc1.out)
cdo("dt_del $W")
time.sleep(2.0 * SLOW)
s0, s1 = ctls(tc0, c0), ctls(tc1, c1)
check("closed, the kept window lets its picture go for good on every "
      "display, and hold forgets the app", deleted(s0, Ic, "I")
      and deleted(s1, Ic, "I") and int(cpid or 0) not in apps(nmc),
      (s0, s1, apps(nmc)))
finish(nmc, tc0, tc1, psc)

# Outside hold, an app's pictures are cells.
lsn = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
sock2 = os.path.join(D, "cw2.sock")
lsn.bind(sock2)
lsn.listen(1)
app2f = os.path.join(D, "app2-%d.hibr" % os.getpid())
open(app2f, "w").write(
    "need console; need img\n"
    "connect -U %s FD || exit 1\n"
    "console cells open \"$FD\"\n"
    "console cells view v1 1 8 26\n"
    "console cells clear v1\n"
    "img draw %s 1 1 6 12 -m pixels -p v1\n"
    "console flush > /dev/null\n"
    "sleep 1\n" % (sock2, P[0]))
env = dict(os.environ)
env.pop("HIBR_HOLD", None)
pr = subprocess.Popen([screen.HIBR, app2f], env=env, stderr=subprocess.DEVNULL)
lsn.settimeout(5 * SLOW)
got = b""
try:
    cs, _ = lsn.accept()
    cs.settimeout(3 * SLOW)
    while True:
        d = cs.recv(65536)
        if not d:
            break
        got += d
except OSError:
    pass
pr.wait(10)
recs = [l for l in got.split(b"\n") if l]
check("outside a held session an app's picture is drawn as cells, and no "
      "picture is said to the desktop",
      any(l.startswith(b"r ") for l in recs) and not any(l.startswith(b"p ") for l in recs),
      recs[:8])
report(36)
