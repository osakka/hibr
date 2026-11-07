#!/usr/bin/env python3
"""Frames by dmabuf through hold (Gitea #486, Blit ADR 0223): a stand-in
producer on hold's own socket and stand-ins for each display's Blit. This box
has no GPU, so a plane is a memfd: hold never maps a descriptor, so it cannot
tell, and neither can Blit's checks by size."""
import json, os, shutil, socket, struct, subprocess, sys, tempfile, time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import screen
from screen import Term, check, report, load

D = tempfile.mkdtemp(prefix="hibr-dmabuf-")
FRAMESRV = os.path.join(os.path.dirname(os.path.abspath(__file__)), "frameserve.py")
SLOW = 4 if "ASAN_OPTIONS" in os.environ else 1


def blit(tag, mode="v3"):
    nm = "hibr-dmabuf-%s-%d" % (tag, os.getpid())
    lg = os.path.join(D, "fs-%s.log" % tag)
    open(lg, "w").close()
    p = subprocess.Popen([sys.executable, FRAMESRV, nm, "tok", lg, mode])
    for _ in range(50):
        if "listening" in open(lg).read():
            break
        time.sleep(0.05)
    return p, lg, {"BLIT_FRAME_SOCKET": "@" + nm, "BLIT_FRAME_TOKEN": "tok"}


def lines(lg):
    return [json.loads(l) for l in open(lg) if l.strip()]


def frames(lg):
    return [x for x in lines(lg) if x["kind"] == "frame3"]


class Producer:
    def __init__(self, nm):
        self.s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.s.connect(os.path.join(D, "hibr-hold-%d" % os.getuid(), nm))
        self.s.sendall(b"O" + struct.pack("<I", 0))
        self.seq = 0
        self.fd = os.memfd_create("plane")
        os.ftruncate(self.fd, 64 * 16 * 4)

    def send(self, iid=7, extra=()):
        self.seq += 1
        h = (b"FRM3" + struct.pack("<IQII", iid, self.seq, 64, 16) + b"XR24" +
             struct.pack("<IQ", 1, 0) + struct.pack("<II", 0, 256) + bytes(24))
        socket.send_fds(self.s, [b"K"], [self.fd] + list(extra))
        self.s.sendall(struct.pack("<I", len(h)) + h)
        return self.seq

    def answers(self, t=1.0):
        out, b = [], b""
        self.s.settimeout(t * SLOW)
        try:
            while True:
                d = self.s.recv(4096)
                if not d:
                    out.append("closed")
                    break
                b += d
                while len(b) >= 5:
                    n = struct.unpack("<I", b[1:5])[0]
                    if len(b) < 5 + n:
                        break
                    out.append(b[5:5 + n].decode())
                    b = b[5 + n:]
                self.s.settimeout(0.3)
        except socket.timeout:
            pass
        return out


def ask(nm, typ):
    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    s.connect(os.path.join(D, "hibr-hold-%d" % os.getuid(), nm))
    s.sendall(typ + struct.pack("<I", 0))
    s.settimeout(2)
    b = s.recv(4096)
    s.close()
    return b[5:].decode()


NUM = [0]


def session(envs):
    """A held program with image 7 placed, a display attached for each
    environment, side by side in one view of the session."""
    NUM[0] += 1
    nm = "dma%d-%d" % (NUM[0], os.getpid())
    prog = os.path.join(D, "p%s.hibr" % nm)
    open(prog, "w").write(
        "i=0\nwhile [ $i -lt 120 ]; do printf '\\033[20;1HHELD\\033[H\\033_Ga=p,q=2,C=1,i=7,c=20,r=8\\033\\\\'; "
        "sleep 0.5; i=$((i + 1)); done\n")
    ts = []
    for i, e in enumerate(envs):
        f = os.path.join(D, "a%s-%d.hibr" % (nm, i))
        head = ('hold new -d %s "$HIBR" %s\nsleep 0.6\n' % (nm, prog)) if i == 0 else ""
        open(f, "w").write(load("hold") + head + "hold attach -m -n d%d %s 0 0\n" % (i, nm))
        ts.append(Term(f, rows=24, cols=60, settle=1.0, cellw=8, cellh=16,
                       env=dict({"HIBR_GFX": "kitty", "TMPDIR": D}, **e), caps=["kitty"]))
        ts[-1].collect(1.0 * SLOW)
    return nm, ts


def finish(nm, ts):
    for t in reversed(ts):
        t.quit(None, 0.3)
    subprocess.run([screen.HIBR, "-c", load("hold") + "hold kill %s" % nm],
                   env=dict(os.environ, TMPDIR=D), capture_output=True)


p1, lg1, e1 = blit("a")
p2, lg2, e2 = blit("b")
nm, ts = session([e1, e2])
check("a display says it imports, and hold answers for all of them",
      ask(nm, b"V").startswith("1 XR24/0"), ask(nm, b"V"))
pr = Producer(nm)
s1 = pr.send()
ts[0].collect(0.5)
check("a frame reaches every display with its descriptors",
      [f["seq"] for f in frames(lg1)] == [s1] and [f["seq"] for f in frames(lg2)] == [s1],
      (lines(lg1), lines(lg2)))
open(lg2 + ".hold", "w").close()
s2 = pr.send()
ts[0].collect(0.5)
early = pr.answers(0.5)
check("the producer hears nothing while one display still holds a frame",
      early == [], early)
os.unlink(lg2 + ".hold")
s3 = pr.send()
ts[0].collect(0.5)
got = pr.answers()
check("and hears once per frame when the last has let it go",
      "RELEASE 7 %d" % s1 in got and got.count("RELEASE 7 %d" % s1) == 1, got)
one = os.memfd_create("fence")
s4 = pr.send(extra=[one])
ts[0].collect(0.5)
check("a fence rides along with the planes",
      [f["fds"] for f in frames(lg1)][-1] == 2, frames(lg1)[-1:])
ts[1].quit(None, 0.3)
ts = ts[:1]
ts[0].collect(0.5 * SLOW)
s5 = pr.send()
ts[0].collect(0.5)
got = pr.answers()
check("a display that goes holds nothing back", "RELEASE 7 %d" % s3 in got, got)
finish(nm, ts)
for p in (p1, p2):
    p.terminate()
    p.wait()

p3, lg3, e3 = blit("c", "nodma")
nm, ts = session([e3])
check("a display that imports nothing makes the answer no", ask(nm, b"V") == "0", ask(nm, b"V"))
pr = Producer(nm)
pr.send()
ts[0].collect(0.5)
got = pr.answers()
check("and the producer is told, so it can send memfds",
      "ERROR 7 ENOTSUP" in got, got)
finish(nm, ts)
p3.terminate()
p3.wait()
shutil.rmtree(D, True)
report(9)
