#!/usr/bin/env python3
"""A window's picture sent as Blit's frames (Gitea #486, steps 2 and 3).

A producer speaking Blit's own client wire -- any program drawing into
buffers, the shape Blit's compositor speaks too -- connects to the frames module's socket in a program that has the
console open, and its frames reach a stand-in Blit (frameserve.py) under the
console's id: straight there when nothing is between, and through hold's
server when the program is held. This box has no GPU, so a plane is a memfd:
nothing on the way maps one, and the stand-in checks planes by size."""
import json, os, shutil, socket, struct, subprocess, sys, tempfile, time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import screen
from screen import Term, check, report, load

D = tempfile.mkdtemp(prefix="hibr-frames-")
FRAMESRV = os.path.join(os.path.dirname(os.path.abspath(__file__)), "frameserve.py")
SLOW = 4 if "ASAN_OPTIONS" in os.environ else 1


def blit(tag, mode="v3", *more):
    nm = "hibr-frames-%s-%d" % (tag, os.getpid())
    lg = os.path.join(D, "fs-%s.log" % tag)
    open(lg, "w").close()
    p = subprocess.Popen([sys.executable, FRAMESRV, nm, "tok", lg, mode] + list(more))
    for _ in range(50):
        if "listening" in open(lg).read():
            break
        time.sleep(0.05)
    return p, lg, {"BLIT_FRAME_SOCKET": "@" + nm, "BLIT_FRAME_TOKEN": "tok"}


def lines(lg):
    return [json.loads(l) for l in open(lg) if l.strip()]


def frames(lg):
    return [x for x in lines(lg) if x["kind"] == "frame3"]


def wait(cond, t=3.0):
    end = time.time() + t * SLOW
    while time.time() < end:
        v = cond()
        if v:
            return v
        time.sleep(0.05)
    return cond()


class Bridge:
    """The producer's side of Blit's frame wire."""

    def __init__(self, sock, tok, ver=5):
        self.s = socket.socket(socket.AF_UNIX, socket.SOCK_SEQPACKET)
        self.s.connect("\0" + sock.lstrip("@"))
        t = tok.encode()
        self.s.send(b"BLITFRM1" + struct.pack("<II", ver, len(t)) + t)
        self.s.settimeout(3 * SLOW)
        self.hello = self.s.recv(1024).decode()
        self.seq = 0
        self.fd = os.memfd_create("plane")
        os.ftruncate(self.fd, 64 * 16 * 4)

    def send(self, iid=42, extra=()):
        self.seq += 1
        h = (b"FRM3" + struct.pack("<IQII", iid, self.seq, 64, 16) + b"XR24" +
             struct.pack("<IQ", 1, 0) + struct.pack("<II", 0, 256) + bytes(24))
        socket.send_fds(self.s, [h], [self.fd] + list(extra))
        return self.seq

    def say(self, line):
        self.s.send(line.encode())

    def answers(self, t=1.0):
        out = []
        self.s.settimeout(t * SLOW)
        try:
            while True:
                d = self.s.recv(4096)
                if not d:
                    out.append("closed")
                    break
                out += [l for l in d.decode().split("\n") if l]
                self.s.settimeout(0.3)
        except socket.timeout:
            pass
        return out

    def close(self):
        self.s.close()


def prog(out):
    """A program that serves a socket and keeps a stream's picture in a
    pane, the way a window does, and says how often it woke."""
    p = os.path.join(D, "prog-%d-%s.hibr" % (os.getpid(), os.path.basename(out)))
    open(p, "w").write(
        "need console; need frames\n"
        "console open\n"
        "[ -n \"${HIBR_HOLD-}\" ] && console imgfd hold\n"
        "x := frames serve\n"
        "read -r n sock tok <<< \"$x\"\n"
        "printf '%%s %%s\\n' \"$sock\" \"$tok\" > %(o)s.sock\n"
        "r=2 i=0\n"
        "while [ ! -f %(o)s.stop ]; do\n"
        "  [ -f %(o)s.move ] && r=8\n"
        "  console pane w1 $r 4 10 30\n"
        "  console put -p w1 0 0 WINDOW\n"
        "  [ -f %(o)s.hide ] || frames place $n w1 1 1 6 20\n"
        "  if [ -f %(o)s.on ] && [ \"$on\" != 1 ]; then frames input $n on; on=1; fi\n"
        "  if [ -f %(o)s.hold ] && [ \"$hd\" != 1 ]; then frames input $n hold confine; hd=1; fi\n"
        "  console flush\n"
        "  i=$((i + 1))\n"
        "  y := frames info $n\n"
        "  printf '%%s loops=%%d\\n' \"$y\" $i > %(o)s.tmp && mv %(o)s.tmp %(o)s.info\n"
        "  console key 3000 > /dev/null\n"
        "done\n"
        "frames stop $n\n"
        "console flush\n" % {"o": out})
    return p


def info(out):
    try:
        return dict(kv.split("=", 1) for kv in open(out + ".info").read().split())
    except (OSError, ValueError):
        return {}


def sockof(out):
    try:
        return open(out + ".sock").read().split()
    except OSError:
        return None


def apcs(t):
    return [a for a in t.screen().apcall if "a=p" in a or "a=d" in a]


# Nothing between: the console's own frame socket.
pb, lgb, eb = blit("u")
out = os.path.join(D, "u")
t = Term(prog(out), rows=24, cols=60, settle=1.0, cellw=8, cellh=16,
         env=dict({"HIBR_GFX": "kitty", "TMPDIR": D}, **eb), caps=["kitty"])
so = wait(lambda: sockof(out))
br = Bridge(*so)
check("the hello is answered with what the display imports",
      br.hello.strip() == "OK dmabuf=1 fence=0 formats=XR24/0,AR24/0 window=0", br.hello)
s1 = br.send()
got = wait(lambda: frames(lgb))
t.collect(0.5)
cid = got[0]["id"] if got else None
check("a frame reaches Blit under the console's id, not the producer's",
      got and got[0]["seq"] == s1 and cid != 42 and got[0]["fds"] == 1, got)
pl = [a for a in apcs(t) if "a=p" in a]
check("its picture is placed with an id and a placement of its own",
      any("i=%s," % cid in a and "p=1" in a for a in pl), pl)
before = int(info(out).get("loops", 0))
seqs = [br.send() for _ in range(20)]
wait(lambda: len(frames(lgb)) >= 21)
got = br.answers()
check("each frame Blit lets go comes back as one FRAME release in the producer's id",
      "FRAME release 42 %d" % s1 in got and all(a.startswith("FRAME release 42 ") for a in got)
      and len(got) == len(set(got)), got)
after = int(info(out).get("loops", 0))
check("frames passing do not wake the program", after - before <= 2,
      (before, after))
n3 = len(frames(lgb))
open(out + ".move", "w").close()
os.utime(out + ".info")
br.send()
wait(lambda: len(frames(lgb)) > n3)
t.keys([b"z"])
t.collect(0.5)
pl = [a for a in apcs(t) if "a=p" in a]
dl = [a for a in apcs(t) if "a=d" in a and "i=%s" % cid in a]
check("a move places the same picture again and deletes nothing",
      len([a for a in pl if "i=%s," % cid in a]) >= 2 and not dl, (pl, dl))
check("and sends no frame again", len(frames(lgb)) == n3 + 1, len(frames(lgb)))
last = br.seq
br.answers(0.5)
l0 = int(info(out).get("loops", 0))
open(out + ".hide", "w").close()
t.keys([b"z"])
wait(lambda: int(info(out).get("loops", 0)) > l0)
t.collect(0.5)
dl = [a for a in apcs(t) if "a=d" in a and "i=%s" % cid in a]
open(lgb + ".del", "w").write("%s\n" % cid if dl else "")
os.unlink(out + ".hide")
n4 = len(frames(lgb))
t.keys([b"z"])
wait(lambda: len(frames(lgb)) > n4)
t.collect(0.5)
f4 = frames(lgb)[n4:]
nid = f4[0]["id"] if f4 else None
check("a picture whose pane drew without it goes, and placed again it is a new "
      "id sent the newest frame at once",
      dl and nid and nid != cid and f4[0]["seq"] == last and
      any("i=%s," % nid in a for a in apcs(t) if "a=p" in a), (dl, f4))
early = br.answers(0.5)
br.send()
wait(lambda: len(frames(lgb)) > n4 + 1)
got = early + br.answers()
check("and the producer hears that frame let go once, after both ids have",
      got.count("FRAME release 42 %d" % last) == 1 and
      "FRAME release 42 %d" % last not in early, (early, got))
cid = nid
# Input (Blit 0.3.64): the producer offers its input socket, the window
# says when it has the keys, and every answer comes back in its own id. An
# EIS route went with Blit's gamescope bridge (0.99.483): refused here.
eis = os.path.join(D, "in.sock")
es = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
es.bind(eis)
br.answers(0.3)
br.say("INPUT eis 42 %s" % eis)
got = br.answers()
check("an EIS route, the gamescope bridge's, is refused by the module itself",
      "INPUT error 42 EINVAL" in got and not any(x.get("verb") == "eis" for x in lines(lgb)),
      (got, lines(lgb)[-3:]))
br.say("INPUT sock 42 %s" % eis)
got = br.answers()
check("an input route asked for after a frame is taken, in the producer's id",
      "INPUT ok 42" in got and any(x.get("verb") == "sock" and x.get("id") == cid
                                   and x.get("ok") for x in lines(lgb)), (got, lines(lgb)[-3:]))
open(out + ".on", "w").close()
t.keys([b"z"])
got = br.answers()
check("the window having the keys turns the route on, at Blit and to the producer",
      "INPUT ok 42" in got and any(x.get("verb") == "on" and x.get("ok") for x in lines(lgb)),
      (got, lines(lgb)[-3:]))
notsock = os.path.join(D, "plain")
open(notsock, "w").close()
br2n = len(frames(lgb))
br.say("INPUT sock 42 %s" % notsock)
got = br.answers()
br.send()
wait(lambda: len(frames(lgb)) > br2n)
check("a route Blit refuses (EACCES) is told to the producer and the frames go on",
      any(a.startswith("INPUT error 42 E") for a in got) and len(frames(lgb)) > br2n,
      (got, len(frames(lgb)), br2n))
es.close()
br.close()
t.keys([b"z"])
wait(lambda: any("a=d" in a and "i=%s" % cid in a for a in apcs(t)))
check("the producer hanging up takes the picture away",
      any("a=d" in a and "i=%s" % cid in a for a in apcs(t)), apcs(t))
open(out + ".stop", "w").close()
t.quit(b"z", 0.5)
pb.terminate()
pb.wait()

# A display that imports nothing.
pn, lgn, en = blit("n", "nodma")
out = os.path.join(D, "n")
t = Term(prog(out), rows=24, cols=60, settle=1.0, cellw=8, cellh=16,
         env=dict({"HIBR_GFX": "kitty", "TMPDIR": D}, **en), caps=["kitty"])
so = wait(lambda: sockof(out))
br = Bridge(*so)
check("a display that imports nothing makes the answer no",
      br.hello.strip() == "OK dmabuf=0 window=0", br.hello)
br.send()
got = br.answers()
check("and a dmabuf frame anyway is refused ENOTSUP in the producer's id",
      "FRAME error 42 ENOTSUP" in got, got)
br.close()
time.sleep(0.3)
bad = Bridge(so[0], "x" * 32)
check("a stranger's token is refused", bad.hello.startswith("ERROR 0 EACCES"),
      bad.hello)
bad.close()
open(out + ".stop", "w").close()
t.quit(b"z", 0.5)
pn.terminate()
pn.wait()

# Held: through hold's server to each display's Blit.
ph, lgh, eh = blit("h")
ph2, lgh2, eh2 = blit("h2")
out = os.path.join(D, "h")
nm = "frh-%d" % os.getpid()
f = os.path.join(D, "att-%d.hibr" % os.getpid())
open(f, "w").write(load("hold") + 'hold new -d %s "$HIBR" %s\nsleep 0.6\n'
                   'hold attach -m -n d0 %s 0 0\n' % (nm, prog(out), nm))
t = Term(f, rows=24, cols=60, settle=1.0, cellw=8, cellh=16,
         env=dict({"HIBR_GFX": "kitty", "TMPDIR": D}, **eh), caps=["kitty"])
t.collect(1.0 * SLOW)
f2 = os.path.join(D, "att2-%d.hibr" % os.getpid())
open(f2, "w").write(load("hold") + 'hold attach -m -n d1 %s 0 0\n' % nm)
t2 = Term(f2, rows=24, cols=60, settle=1.0, cellw=8, cellh=16,
          env=dict({"HIBR_GFX": "kitty", "TMPDIR": D}, **eh2), caps=["kitty"])
t2.collect(1.0 * SLOW)
so = wait(lambda: sockof(out), 5)
# The held desktop turns frames by descriptor on once hold says every
# display can take them; this program has no desktop, so it asks itself.
br = Bridge(*so) if so else None
check("held, the hello says what every display imports",
      br and br.hello.startswith("OK dmabuf=1"), br and br.hello)
if br:
    s1 = br.send()
    got = wait(lambda: frames(lgh), 5)
    check("held, a frame reaches the display's Blit through hold",
          got and got[0]["seq"] == s1, lines(lgh))
    br.send()
    br.send()
    got = br.answers()
    check("held, the producer hears one FRAME release per frame in its own id",
          "FRAME release 42 %d" % s1 in got and got.count("FRAME release 42 %d" % s1) == 1, got)
    hid = frames(lgh)[0]["id"]
    n3 = len(frames(lgh))
    t.collect(0.5)
    was = len([a for a in apcs(t) if "a=p" in a and "i=%s," % hid in a])
    l0 = int(info(out).get("loops", 0))
    open(out + ".move", "w").close()
    br.send()
    wait(lambda: len(frames(lgh)) > n3, 5)
    wait(lambda: int(info(out).get("loops", 0)) > l0, 6)
    t.collect(1.0 * SLOW)
    now = [a for a in apcs(t) if "a=p" in a and "i=%s," % hid in a]
    dl = [a for a in apcs(t) if "a=d" in a and "i=%s" % hid in a]
    check("held, a move reaches the display as the same picture placed again",
          was >= 1 and len(now) > was and not dl, (was, now, dl))
    eis = os.path.join(D, "inh.sock")
    es = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    es.bind(eis)
    br.answers(0.3)
    br.say("INPUT sock 42 %s" % eis)
    got = br.answers(1.5)
    asked = [x for lg in (lgh, lgh2) for x in lines(lg) if x["kind"] == "input"]
    check("held, every display's Blit is asked and the producer hears one answer",
          len(asked) == 2 and got.count("INPUT ok 42") == 1, (asked, got))
    es.close()
    br.close()
open(out + ".stop", "w").close()
time.sleep(0.5)
t2.quit(None, 0.3)
t.quit(None, 0.3)
subprocess.run([screen.HIBR, "-c", load("hold") + "hold kill %s" % nm],
               env=dict(os.environ, TMPDIR=D), capture_output=True)
for p in (ph, ph2):
    p.terminate()
    p.wait()
shutil.rmtree(D, True)
report(23)
