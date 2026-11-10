#!/usr/bin/env python3
"""A stand-in for blit-compositor, Blit's Wayland compositor for programs
(Gitea #494, Blit ADR 0228), as the desktop starts it.

    natserve.py LOG [compositor arguments...]

Connects to BLIT_FRAME_SOCKET with BLIT_FRAME_TOKEN as the real compositor
does, says hello at version 4 and wants window=1 back, names its display,
opens one window (7) with its hints and asks for an input route by socket
of its own (`INPUT sock <path> 7`). Every WINDOW size it is told, it draws
at: a dmabuf frame of exactly that size -- a memfd as the plane, since this
box has no GPU -- and again every 100 ms. A WINDOW close is answered with
WINDOW gone, as a program that closes when asked; WINDOW quit 0 ends it.
LOG.full makes it ask for full screen once; LOG.child opens a second window,
8, whose parent is 7; LOG.popup opens a popup, 9, of 80 by 60 at 12,63 of
7, which a WINDOW close dismisses; LOG.csd says 7 draws its own frame;
LOG.lock asks for the pointer held, confined, over 7.
Everything it is told is a JSON line in LOG.
"""
import json, os, signal, socket, struct, sys, time

LOG = sys.argv[1]


def note(**kw):
    with open(LOG, "a") as f:
        f.write(json.dumps(kw) + "\n")


signal.signal(signal.SIGHUP, lambda *a: (note(kind="hangup"), os._exit(0)))
note(kind="start", argv=sys.argv[2:], pid=os.getpid(),
     sock=os.environ.get("BLIT_FRAME_SOCKET", ""),
     tok=bool(os.environ.get("BLIT_FRAME_TOKEN")))
s = socket.socket(socket.AF_UNIX, socket.SOCK_SEQPACKET)
s.connect("\0" + os.environ["BLIT_FRAME_SOCKET"].lstrip("@"))
t = os.environ["BLIT_FRAME_TOKEN"].encode()
s.send(b"BLITFRM1" + struct.pack("<II", 4, len(t)) + t)
ans = s.recv(1024).decode().strip()
note(kind="hello", answer=ans)
if "window=1" not in ans.split():
    note(kind="refused")
    sys.exit(0)


def say(line):
    s.send(line.encode())


say("WINDOW display stand-%d" % os.getpid())
for l in ("WINDOW new 7 0", "WINDOW title 7 Stand-in", "WINDOW appid 7 hibr.natserve",
          "WINDOW limits 7 64 48 0 0", "WINDOW decor 7 server"):
    say(l)
isock = os.path.join(os.path.dirname(LOG), "input-%d.sock" % os.getpid())
ls = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
ls.bind(isock)
say("INPUT sock %s 7" % isock)
s.setblocking(False)
size = {}
seq = {}
fds = {}
full = child = popup = csd = lock = False


def frame(wid):
    w, h = size[wid]
    if w <= 0 or h <= 0:
        return
    if fds.get(wid, (0, 0, -1))[:2] != (w, h):
        fd = os.memfd_create("plane")
        os.ftruncate(fd, w * h * 4)
        fds[wid] = (w, h, fd)
    seq[wid] = seq.get(wid, 0) + 1
    hd = (b"FRM3" + struct.pack("<IQII", wid, seq[wid], w, h) + b"XR24" +
          struct.pack("<IQ", 1, 0) + struct.pack("<II", 0, w * 4) + bytes(24))
    try:
        socket.send_fds(s, [hd], [fds[wid][2]])
    except BlockingIOError:
        pass


while True:
    try:
        while True:
            d = s.recv(4096)
            if not d:
                note(kind="closed")
                sys.exit(0)
            for l in d.decode().split("\n"):
                if not l:
                    continue
                note(kind="line", line=l)
                w = l.split()
                if w[:2] == ["WINDOW", "size"] and len(w) >= 5:
                    size[int(w[2])] = (int(w[3]), int(w[4]))
                    frame(int(w[2]))
                elif w[:2] == ["WINDOW", "close"] and len(w) >= 3:
                    say("WINDOW gone %s" % w[2])
                    size.pop(int(w[2]), None)
                elif w[:3] == ["WINDOW", "quit", "0"]:
                    note(kind="quit")
                    sys.exit(0)
    except BlockingIOError:
        pass
    except OSError:
        note(kind="closed")
        sys.exit(0)
    if not full and os.path.exists(LOG + ".full"):
        full = True
        say("WINDOW fullscreen 7 on")
    if not child and os.path.exists(LOG + ".child"):
        child = True
        for l in ("WINDOW new 8 7", "WINDOW title 8 Child", "WINDOW decor 8 server"):
            say(l)
    if not popup and os.path.exists(LOG + ".popup"):
        popup = True
        say("WINDOW popup 9 7 12 63")
        size[9] = (80, 60)
    if not lock and os.path.exists(LOG + ".lock"):
        lock = True
        say("WINDOW lock 7 confine")
    if not csd and os.path.exists(LOG + ".csd"):
        csd = True
        say("WINDOW decor 7 client")
    for wid in list(size):
        frame(wid)
    time.sleep(0.1)
