#!/usr/bin/env python3
"""A stand-in for blit-compositor, Blit's Wayland compositor for programs
(Gitea #494, Blit ADR 0228), as the desktop starts it.

    natserve.py LOG [compositor arguments...]

Connects to BLIT_FRAME_SOCKET with BLIT_FRAME_TOKEN as the real compositor
does, says hello at version 5 and wants window=1 back, names its display,
opens one window (7) with its hints and asks for an input route by socket
of its own (`INPUT sock 7 <path>`). Every WINDOW size it is told, it draws
at: a dmabuf frame of exactly that size -- a memfd as the plane, since this
box has no GPU -- and again every 100 ms. A WINDOW close is answered with
WINDOW gone, as a program that closes when asked; WINDOW quit 0 ends it.
LOG.full makes it ask for full screen once; LOG.child opens a second window,
8, whose parent is 7; LOG.popup opens a popup, 9, of 80 by 60 at 12,63 of
7, which a WINDOW close dismisses; LOG.csd says 7 draws its own frame;
LOG.lock asks for the pointer held, confined, over 7, and LOG.unlock no
longer wants it; LOG.sub gives 7 two
subsurfaces, 11 below it hanging past its top left and 10 above it.
LOG.geom draws 7 with a shadow of ten pixels round it and says its WINDOW
geometry; LOG.move, LOG.resize (holding an edge) and LOG.menu (holding X Y)
each ask once, as its own title bar would; LOG.copy (a line of text, and
`secret` on the next) offers that as its clipboard. The desktop's own
clipboard offers are read at once, as a program pasting would read them.
LOG.shm, there from the start, draws 7 in shared memory (FRM1), its subs
by dmabuf, as Firefox does. When the desktop goes it dials again for sixty seconds and announces every
window again under the same ids, as blit-compositor does across a Restart
Desktop. Everything it is told is a JSON line in LOG.
"""
import fcntl, json, os, signal, socket, struct, sys, time

LOG = sys.argv[1]


def note(**kw):
    with open(LOG, "a") as f:
        f.write(json.dumps(kw) + "\n")


signal.signal(signal.SIGHUP, lambda *a: (note(kind="hangup"), os._exit(0)))
note(kind="start", argv=sys.argv[2:], pid=os.getpid(),
     sock=os.environ.get("BLIT_FRAME_SOCKET", ""),
     tok=bool(os.environ.get("BLIT_FRAME_TOKEN")))
s = None
size = {}
seq = {}
fds = {}
isock = os.path.join(os.path.dirname(LOG), "input-%d.sock" % os.getpid())
ls = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
ls.bind(isock)


def say(line):
    s.send(line.encode())


def dial():
    """Hello at version 5; the answer, or None where nothing listens."""
    global s
    c = socket.socket(socket.AF_UNIX, socket.SOCK_SEQPACKET)
    try:
        c.connect("\0" + os.environ["BLIT_FRAME_SOCKET"].lstrip("@"))
    except OSError:
        c.close()
        return None
    t = os.environ["BLIT_FRAME_TOKEN"].encode()
    c.send(b"BLITFRM1" + struct.pack("<II", 5, len(t)) + t)
    c.settimeout(3)
    try:
        ans = c.recv(1024).decode().strip()
    except OSError:
        c.close()
        return None
    s = c
    return ans


def announce():
    """Every window, as on a first connection and again on each reconnect
    (Blit's api.md, "Programs survive a desktop restart"): the display,
    each top-level with its last state, then each popup; ids unchanged."""
    say("WINDOW display stand-%d" % os.getpid())
    for l in ("WINDOW new 7 0", "WINDOW title 7 Stand-in", "WINDOW appid 7 hibr.natserve",
              "WINDOW limits 7 64 48 0 0", "WINDOW decor 7 %s" % ("client" if csd else "server")):
        say(l)
    if full and fs7:
        say("WINDOW fullscreen 7 on")
    if geom and 7 in size:
        say("WINDOW geometry 7 10 10 %d %d" % size[7])
    if child and 8 in size:
        for l in ("WINDOW new 8 7", "WINDOW title 8 Child", "WINDOW decor 8 server"):
            say(l)
    say("INPUT sock 7 %s" % isock)
    if popup and 9 in size:
        say("WINDOW popup 9 7 12 63")
    if subs and 7 in size:
        for l in SUBS:
            say(l)


# Two subsurfaces of 7 (Gitea #496), bottom to top: 11 under the window's
# picture, hanging 8 pixels past its body's top left so it has to be cut,
# and 10 over it, inside it.
SUBS = ("WINDOW sub 11 7 -8 -8 64 32 below", "WINDOW sub 10 7 24 40 96 48 above")


full = child = popup = csd = lock = subs = geom = False
fs7 = False
ans = dial()
note(kind="hello", answer=ans)
if not ans or "window=1" not in ans.split():
    note(kind="refused")
    sys.exit(0)
announce()
s.setblocking(False)


def reconnect():
    """The desktop has gone (a Restart Desktop execs it): dial again, 50 ms
    then doubling to a second, for at most sixty seconds -- Blit's own
    rule -- and announce every window again; or end."""
    global s
    note(kind="lost")
    s.close()
    end = time.time() + 60
    gap = 0.05
    while time.time() < end:
        time.sleep(gap)
        gap = min(gap * 2, 1.0)
        a = dial()
        if a and "window=1" in a.split():
            note(kind="hello", answer=a, again=True)
            announce()
            s.setblocking(False)
            return
    note(kind="closed")
    sys.exit(0)


SHM = os.path.exists(LOG + ".shm")


def frame(wid):
    w, h = size[wid]
    if wid == 7 and geom:
        # A shadow of ten pixels round the window, as a program drawing
        # its own frame has: its geometry says where the window is.
        w, h = w + 20, h + 20
    if w <= 0 or h <= 0:
        return
    if wid == 7 and SHM:
        # Drawn in shared memory, as GTK draws Firefox's own frame round
        # its page: an FRM1 copy, a sealed memfd a frame (Gitea #499).
        seq[wid] = seq.get(wid, 0) + 1
        fd = os.memfd_create("shm", os.MFD_ALLOW_SEALING)
        os.ftruncate(fd, w * h * 4)
        fcntl.fcntl(fd, 1033, 0x2 | 0x4 | 0x8)
        hd = b"FRM1" + struct.pack("<IQIIIIIIII", wid, seq[wid], w, h, 32, w * 4, 0, 0, 0, 0)
        try:
            socket.send_fds(s, [hd], [fd])
        except OSError:
            pass
        os.close(fd)
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
    except OSError:
        pass


CLIPS = {}
clipn = 100


def clipline(w, fds):
    """The clipboard as blit-compositor speaks it (Blit's api.md, "The
    clipboard"): the desktop's offer is read at once, with a pipe, as a
    program pasting it would; a read of the stand-in's own offer is
    answered down the pipe it came with; a clear is noted."""
    if w[1] == "offer" and len(w) >= 4:
        r, wr = os.pipe()
        socket.send_fds(s, [("CLIP read %s text/plain;charset=utf-8" % w[2]).encode()], [wr])
        os.close(wr)
        got = b""
        end = time.time() + 3
        while time.time() < end:
            b = os.read(r, 65536)
            if not b:
                break
            got += b
        os.close(r)
        note(kind="pasted", sel=w[3], text=got.decode(errors="replace"),
             secret="x-kde-passwordManagerHint" in w[4:])
    elif w[1] == "read" and fds:
        t = CLIPS.get(w[2])
        if t is not None:
            os.write(fds[0], t.encode())
        os.close(fds[0])
        fds = fds[1:]
        note(kind="clipread", seq=w[2], served=t is not None)
    elif w[1] == "clear":
        note(kind="clipclear", seq=w[2])
    for fd in fds:
        os.close(fd)


while True:
    try:
        while True:
            d, rfds, _, _ = socket.recv_fds(s, 4096, 4)
            if not d:
                reconnect()
                break
            for l in d.decode().split("\n"):
                if not l:
                    continue
                note(kind="line", line=l)
                w = l.split()
                if w[:1] == ["CLIP"] and len(w) >= 3:
                    clipline(w, rfds)
                    rfds = []
                elif w[:2] == ["WINDOW", "size"] and len(w) >= 5:
                    size[int(w[2])] = (int(w[3]), int(w[4]))
                    if w[2] == "7" and geom:
                        say("WINDOW geometry 7 10 10 %s %s" % (w[3], w[4]))
                    if w[2] == "7" and full:
                        # The states it was given are its own, and what a
                        # reconnect announces again.
                        fs7 = "fullscreen" in w[5:]
                    frame(int(w[2]))
                elif w[:2] == ["WINDOW", "close"] and len(w) >= 3:
                    if w[2] == "7" and subs:
                        for sid in (10, 11):
                            say("WINDOW gone %d" % sid)
                            size.pop(sid, None)
                    say("WINDOW gone %s" % w[2])
                    size.pop(int(w[2]), None)
                elif w[:3] == ["WINDOW", "quit", "0"]:
                    note(kind="quit")
                    sys.exit(0)
            for fd in rfds:
                os.close(fd)
    except BlockingIOError:
        pass
    except OSError:
        reconnect()
    if not full and os.path.exists(LOG + ".full"):
        full = fs7 = True
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
    if lock and os.path.exists(LOG + ".unlock"):
        os.unlink(LOG + ".unlock")
        say("WINDOW lock 7 off")
    if not subs and os.path.exists(LOG + ".sub"):
        subs = True
        for l in SUBS:
            say(l)
        size[11] = (64, 32)
        size[10] = (96, 48)
    if not geom and os.path.exists(LOG + ".geom") and 7 in size:
        geom = True
        say("WINDOW geometry 7 10 10 %d %d" % size[7])
    for verb in ("move", "resize", "menu"):
        if os.path.exists(LOG + "." + verb):
            arg = open(LOG + "." + verb).read().strip()
            os.unlink(LOG + "." + verb)
            say(("WINDOW %s 7 %s" % (verb, arg)).strip())
    if os.path.exists(LOG + ".copy"):
        arg = open(LOG + ".copy").read().split("\n")
        os.unlink(LOG + ".copy")
        clipn += 1
        CLIPS[str(clipn)] = arg[0]
        say("CLIP offer %d clipboard text/plain;charset=utf-8%s"
            % (clipn, " x-kde-passwordManagerHint" if arg[1:2] == ["secret"] else ""))
    if not csd and os.path.exists(LOG + ".csd"):
        csd = True
        say("WINDOW decor 7 client")
    for wid in list(size):
        frame(wid)
    time.sleep(0.1)
