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
longer wants it; LOG.teach (LATIN ARABIC) asks the desktop what a word
held in 7 reads as, each time it appears; LOG.sub gives 7 two
subsurfaces, 11 below it hanging past its top left and 10 above it.
LOG.geom draws 7 with a shadow of ten pixels round it and says its WINDOW
geometry; LOG.move, LOG.resize (holding an edge) and LOG.menu (holding X Y)
each ask once, as its own title bar would; LOG.copy (a line of text, and
`secret` on the next) offers that as its clipboard. The desktop's own
clipboard offers are read at once, as a program pasting would read them.
LOG.chatter says a title every 5 ms, so the desktop always has something
unread from it. LOG.shm, there from the start, draws 7 in shared memory (FRM1), its subs
by dmabuf, as Firefox does. LOG.scale draws 7 at twice its size each way and
says so with WINDOW view (wp_viewporter's scale, Blit #159), and its removal
says WINDOW view 7 -; LOG.vpopup opens a popup, 16, of 7 at 12,63 whose
buffer of 160 by 120 is shown at 80 by 60, and LOG.vpopupgone ends it.
LOG.vsub gives 7 two subsurfaces drawn through views (VSUBS): 17 at half
its buffer's size, as mpv's video is, and 18 a cropped part of its buffer
at its own scale hanging past 7's top left; LOG.vsubdrop says 17's view
dropped alone, its buffer then shown at its own pixels (WINDOW view 17 -),
and LOG.vsubgone ends them. LOG.inhibit (on or off) has 7 ask that the screen
stay awake or no longer (WINDOW inhibit, Blit #171) -- said only to a desktop
whose hello answer carries inhibit=1, as blit-compositor does, and an on said
again on every reconnect. When the desktop goes it dials again for sixty seconds and announces every
window again under the same ids, as blit-compositor does across a Restart
Desktop. Everything it is told is a JSON line in LOG.
"""
import fcntl, json, os, re, signal, socket, struct, sys, time

LOG = sys.argv[1]

# --version as blit-compositor answers it, one line and status 0; the
# version a test wants in LOG.version (0.3.81 by default: --x11 known).
# And as an older one would, an option it does not know refused with
# status 2 before anything starts.
if "--version" in sys.argv[2:]:
    v = open(LOG + ".version").read().strip() if os.path.exists(LOG + ".version") else "0.3.81"
    print("blit-compositor %s" % v)
    sys.exit(0)
if "--x11" in sys.argv[2:]:
    v = open(LOG + ".version").read().strip() if os.path.exists(LOG + ".version") else "0.3.81"
    if tuple(int(x) for x in v.split(".")) < (0, 3, 81):
        sys.stderr.write("usage: blit-compositor [--log FILE]\n")
        sys.exit(2)


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
    note(kind="hello", answer=ans)
    return ans


def announce():
    """Every window, as on a first connection and again on each reconnect
    (Blit's api.md, "Programs survive a desktop restart"): the display,
    each top-level with its last state, then each popup; ids unchanged."""
    # Blit ADR 0228 phase 2: the X display Xwayland is on, second.
    say("WINDOW display stand-%d :47" % os.getpid())
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
    # A view is said again on a new channel, before the picture's frame.
    if scale7 and 7 in size:
        say(view7())
    if 16 in size:
        say("WINDOW popup 16 7 12 63")
        say("WINDOW view 16 0 0 160 120 80 60")
    if vsubs and 7 in size:
        for l in VSUBS:
            say(l)
    # A window still asking the screen to stay awake says so again.
    if inh == "on" and "inhibit=1" in (ANS or "").split():
        say("WINDOW inhibit 7 on")


# Two subsurfaces of 7 (Gitea #496), bottom to top: 11 under the window's
# picture, hanging 8 pixels past its body's top left so it has to be cut,
# and 10 over it, inside it.
SUBS = ("WINDOW sub 11 7 -8 -8 64 32 below", "WINDOW sub 10 7 24 40 96 48 above")
# Two subsurfaces of 7 through views (Blit #159, Gitea #600), each its sub
# line (where, and the size it is shown at) then its view: 17's buffer of
# 128 by 64 shown at 64 by 32; 18's buffer of 100 by 60, of which the 48 by
# 24 at 20,10 is shown at its own size, hanging 8 and 4 pixels past 7's
# body so it has to be cut too.
VSUBS = ("WINDOW sub 17 7 40 8 64 32 above", "WINDOW view 17 0 0 128 64 64 32",
         "WINDOW sub 18 7 -8 -4 48 24 above", "WINDOW view 18 20 10 48 24 48 24")


full = child = popup = csd = lock = subs = geom = scale7 = vsubs = False
fs7 = False
inh = None
ans = ANS = dial()
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
    global s, ANS
    note(kind="lost")
    s.close()
    end = time.time() + 60
    gap = 0.05
    while time.time() < end:
        time.sleep(gap)
        gap = min(gap * 2, 1.0)
        a = dial()
        if a and "window=1" in a.split():
            ANS = a
            note(kind="hello", answer=a, again=True)
            announce()
            s.setblocking(False)
            return
    note(kind="closed")
    sys.exit(0)


def view7():
    """7's view while it is drawn at twice its size (LOG.scale): the whole
    buffer, shown at its surface's size -- the shadow round it included."""
    w, h = size[7]
    if geom:
        w, h = w + 20, h + 20
    return "WINDOW view 7 0 0 %d %d %d %d" % (2 * w, 2 * h, w, h)


SHM = os.path.exists(LOG + ".shm")
# The size a window draws at when it is left to choose, from LOG.own as
# "W H", 300 by 200 otherwise.
OWN = tuple(int(x) for x in open(LOG + ".own").read().split()[:2]) \
    if os.path.exists(LOG + ".own") else (300, 200)


def frame(wid):
    w, h = size[wid]
    if wid == 7 and geom:
        # A shadow of ten pixels round the window, as a program drawing
        # its own frame has: its geometry says where the window is.
        w, h = w + 20, h + 20
    if wid == 7 and scale7:
        w, h = 2 * w, 2 * h
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


# A WINDOW size line as blit-compositor reads it: the states in this order,
# each at most once (src/blit_compositor_wire.h).
SIZEOK = re.compile(r"^WINDOW size \d+ \d+ \d+( fullscreen)?( maximized)?( activated)?"
                    r"( tiled-left)?( tiled-right)?( tiled-top)?( tiled-bottom)?$")
CLIPS = {}
DRAGS = set()
ODATA = {}
ON = 0
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
    elif w[1] == "drag" and len(w) >= 6:
        # A drag over a window (Blit 0.3.118): the first of a seq on a
        # window is its enter, answered with what the program would do;
        # the rest are motion, answered only if that changed.
        refuse = os.path.exists(LOG + ".droprefuse")
        first = (w[2], w[3]) not in DRAGS
        DRAGS.add((w[2], w[3]))
        note(kind="drag", seq=w[2], win=w[3], x=w[4], y=w[5], first=first)
        if first:
            say("CLIP hover %s %s" % (w[2], "none" if refuse else "copy"))
    elif w[1] == "leave":
        note(kind="leave", seq=w[2])
    elif w[1] == "drop" and len(w) >= 6:
        # Files let go on a program's window (Blit 0.3.114): read as a
        # real program does -- Firefox reads the list once to decide and
        # again at the drop, and GTK reads text/plain -- then taken, or
        # refused when the suite says so.
        reads = []
        for typ in ("text/uri-list", "text/plain;charset=utf-8"):
            r, wr = os.pipe()
            socket.send_fds(s, [("CLIP read %s %s" % (w[2], typ)).encode()], [wr])
            os.close(wr)
            got = b""
            end = time.time() + 3
            while time.time() < end:
                b = os.read(r, 65536)
                if not b:
                    break
                got += b
            os.close(r)
            reads.append(got.decode(errors="replace"))
        refuse = os.path.exists(LOG + ".droprefuse")
        # Where it landed, as the compositor would judge it: a window it
        # knows, and a point inside that window's own pixels.
        sz = size.get(int(w[3])) if w[3].isdigit() else None
        inside = bool(sz) and 0 <= int(w[4]) < sz[0] and 0 <= int(w[5]) < sz[1]
        note(kind="drop", seq=w[2], win=w[3], x=w[4], y=w[5], types=w[6:], reads=reads,
             taken=not refuse, inside=inside, size=sz)
        say("CLIP dropped %s %s" % (w[2], "refused" if refuse else "taken"))
    elif w[1] == "read" and fds and w[2].startswith("o"):
        # A read of a program's drag out (row 3), answered from what the
        # program offered; a type it does not offer reads empty.
        t = ODATA.get((w[2], w[3]) if len(w) > 3 else None, "")
        os.write(fds[0], t.encode())
        os.close(fds[0])
        fds = fds[1:]
        note(kind="oread", seq=w[2], type=w[3] if len(w) > 3 else "")
    elif w[1] == "dragend":
        note(kind="dragend", seq=w[2], how=w[3] if len(w) > 3 else "")
        if 15 in size:
            del size[15]
            say("WINDOW gone 15")
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
                elif w[:2] == ["WINDOW", "size"] and not SIZEOK.match(l):
                    # Refused whole, as blit-compositor refuses a line
                    # whose states are out of its order.
                    note(kind="unknown", line=l)
                elif w[:2] == ["WINDOW", "size"] and len(w) >= 5:
                    # 0 0 is the program's to choose, as xdg_toplevel's
                    # configure 0,0: it draws at its own size, OWN, which is
                    # no whole number of cells (Gitea #508).
                    if (int(w[3]), int(w[4])) == (0, 0):
                        size[int(w[2])] = OWN
                    else:
                        size[int(w[2])] = (int(w[3]), int(w[4]))
                    if w[2] == "7" and geom:
                        say("WINDOW geometry 7 10 10 %s %s" % (w[3], w[4]))
                    if w[2] == "7" and (scale7 or os.path.exists(LOG + ".scale")):
                        # A new size is a new view, said with the frame --
                        # the first one too, before the program's first
                        # picture, when LOG.scale is there from the start.
                        scale7 = True
                        say(view7())
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
                    if w[2] == "7" and vsubs:
                        for sid in (17, 18):
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
    # Something the desktop has yet to read at every moment, as a busy
    # program's compositor has: a title said every 5 ms (Gitea #560).
    if os.path.exists(LOG + ".chatter"):
        try:
            say("WINDOW title 7 Chatter")
        except OSError:
            pass
    if not full and os.path.exists(LOG + ".full"):
        full = fs7 = True
        say("WINDOW fullscreen 7 on")
    if not child and os.path.exists(LOG + ".child"):
        child = True
        for l in ("WINDOW new 8 7", "WINDOW title 8 Child", "WINDOW decor 8 server"):
            say(l)
    if os.path.exists(LOG + ".other"):
        # Another program's window: an app id of its own (Gitea #507).
        os.unlink(LOG + ".other")
        for l in ("WINDOW new 12 0", "WINDOW title 12 Other", "WINDOW appid 12 other.app",
                  "WINDOW decor 12 server"):
            say(l)
    if os.path.exists(LOG + ".othergone"):
        os.unlink(LOG + ".othergone")
        size.pop(12, None)
        say("WINDOW gone 12")
    if os.path.exists(LOG + ".activate"):
        # A program asking for the focus (Blit 0.3.86): "ID REASON".
        w2 = open(LOG + ".activate").read().split()
        os.unlink(LOG + ".activate")
        say("WINDOW activate %s %s" % (w2[0], w2[1] if len(w2) > 1 else "x-active"))
    if not popup and os.path.exists(LOG + ".popup"):
        popup = True
        say("WINDOW popup 9 7 12 63")
        size[9] = (80, 60)
    if popup and os.path.exists(LOG + ".popsub"):
        # A popup's own sub, as Firefox draws a menu's contents, and a popup
        # of the popup, a submenu (Gitea #528, Blit 0.3.92).
        os.unlink(LOG + ".popsub")
        say("WINDOW sub 13 9 4 4 40 32 above")
        say("WINDOW popup 14 9 20 10")
        size[13] = (40, 32)
        size[14] = (48, 40)
    if os.path.exists(LOG + ".popsubgone"):
        os.unlink(LOG + ".popsubgone")
        for wid in (13, 14):
            size.pop(wid, None)
            say("WINDOW gone %d" % wid)
    if popup and os.path.exists(LOG + ".popmove"):
        # The program moving its popup after it is shown (xdg_popup
        # reposition, or an X menu moved): the same line again, the same
        # id, the new offset (Blit 0.3.82).
        xy = open(LOG + ".popmove").read().split() or ["52", "79"]
        os.unlink(LOG + ".popmove")
        say("WINDOW popup 9 7 %s %s" % (xy[0], xy[1]))
    if os.path.exists(LOG + ".teach"):
        # hibr-ime #1: the input method holding a word in 7 asks.
        w = open(LOG + ".teach").read().split()
        os.unlink(LOG + ".teach")
        say("WINDOW teach 7 %s %s" % (w[0], w[1]))
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
    if not vsubs and os.path.exists(LOG + ".vsub") and 7 in size:
        vsubs = True
        for l in VSUBS:
            say(l)
        size[17] = (128, 64)
        size[18] = (100, 60)
    if os.path.exists(LOG + ".inhibit"):
        v = open(LOG + ".inhibit").read().strip() or "on"
        os.unlink(LOG + ".inhibit")
        inh = v
        told = "inhibit=1" in (ANS or "").split()
        if told:
            say("WINDOW inhibit 7 %s" % v)
        note(kind="inhibit", value=v, said=told)
    if vsubs and os.path.exists(LOG + ".vsubdrop"):
        os.unlink(LOG + ".vsubdrop")
        say("WINDOW view 17 -")
    if vsubs and os.path.exists(LOG + ".vsubgone"):
        os.unlink(LOG + ".vsubgone")
        if os.path.exists(LOG + ".vsub"):
            os.unlink(LOG + ".vsub")
        vsubs = False
        for wid in (17, 18):
            size.pop(wid, None)
            say("WINDOW gone %d" % wid)
    if not geom and os.path.exists(LOG + ".geom") and 7 in size:
        geom = True
        say("WINDOW geometry 7 10 10 %d %d" % size[7])
    for verb in ("move", "resize", "menu"):
        if os.path.exists(LOG + "." + verb):
            arg = open(LOG + "." + verb).read().strip()
            os.unlink(LOG + "." + verb)
            say(("WINDOW %s 7 %s" % (verb, arg)).strip())
    if os.path.exists(LOG + ".dragout"):
        # The program starts dragging a file out of window 7 (row 3).
        arg = open(LOG + ".dragout").read().strip()
        os.unlink(LOG + ".dragout")
        ON += 1
        tok = "o%d" % ON
        ODATA[(tok, "text/uri-list")] = "file://" + arg.replace(" ", "%20") + "\r\n"
        say("CLIP dragout %s 7 copy,move text/uri-list text/plain;charset=utf-8" % tok)
        if os.path.exists(LOG + ".dragicon"):
            # The program's own picture for the drag (Blit 0.3.123): a
            # 32x32 picture, 15, four pixels up and left of the pointer,
            # its frames sent as any picture's while the drag is up.
            size[15] = (32, 32)
            say("WINDOW dragicon 15 %s -4 -4" % tok)
    if os.path.exists(LOG + ".dragclear"):
        os.unlink(LOG + ".dragclear")
        say("CLIP clear o%d" % ON)
    if os.path.exists(LOG + ".copy"):
        arg = open(LOG + ".copy").read().split("\n")
        os.unlink(LOG + ".copy")
        clipn += 1
        CLIPS[str(clipn)] = arg[0]
        say("CLIP offer %d clipboard text/plain;charset=utf-8%s"
            % (clipn, " x-kde-passwordManagerHint" if arg[1:2] == ["secret"] else ""))
    if os.path.exists(LOG + ".redecor"):
        # The decoration said again, unchanged, as a program may on every
        # configure: nothing about the window's size has changed.
        os.unlink(LOG + ".redecor")
        say("WINDOW decor 7 %s" % ("client" if csd else "server"))
    if not csd and os.path.exists(LOG + ".csd"):
        csd = True
        say("WINDOW decor 7 client")
    if not scale7 and os.path.exists(LOG + ".scale") and 7 in size:
        scale7 = True
        say(view7())
    if scale7 and not os.path.exists(LOG + ".scale"):
        scale7 = False
        say("WINDOW view 7 -")
    if 16 not in size and os.path.exists(LOG + ".vpopup"):
        os.unlink(LOG + ".vpopup")
        say("WINDOW popup 16 7 12 63")
        say("WINDOW view 16 0 0 160 120 80 60")
        size[16] = (160, 120)
    if os.path.exists(LOG + ".vpopupgone"):
        os.unlink(LOG + ".vpopupgone")
        if size.pop(16, None):
            say("WINDOW gone 16")
    for wid in list(size):
        frame(wid)
    time.sleep(0.005 if os.path.exists(LOG + ".chatter") else 0.1)
