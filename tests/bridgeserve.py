#!/usr/bin/env python3
"""A stand-in for Blit's gamescope bridge (blit-gamescope), as the Steam
window starts it (Gitea #486).

    bridgeserve.py LOG [bridge arguments...]

Records how it was started, connects to BLIT_FRAME_SOCKET with
BLIT_FRAME_TOKEN as the real bridge does, says hello at version 5, offers an
EIS socket of its own as soon as it is connected (`INPUT eis 7 <path>`, as
the bridge does when gamescope's libei is up), and sends a dmabuf frame of
image 7 every 50 ms -- a memfd as the plane, since this box has no GPU. It
never sends `INPUT on`/`off`: under --no-place that is the window's.
Everything it is told comes back as a JSON line in LOG. It ends when the
socket closes or its terminal is hung up.
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
s.send(b"BLITFRM1" + struct.pack("<II", 5, len(t)) + t)
note(kind="hello", answer=s.recv(1024).decode().strip())
eis = os.path.join(os.path.dirname(LOG), "eis-%d.sock" % os.getpid())
es = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
es.bind(eis)
s.send(("INPUT eis 7 %s" % eis).encode())
fd = os.memfd_create("plane")
os.ftruncate(fd, 64 * 16 * 4)
s.setblocking(False)
seq = 0
while True:
    seq += 1
    h = (b"FRM3" + struct.pack("<IQII", 7, seq, 64, 16) + b"XR24" +
         struct.pack("<IQ", 1, 0) + struct.pack("<II", 0, 256) + bytes(24))
    # The real bridge's log goes to its stdout, a line or more a frame: a
    # pty nobody drains fills, and the bridge then blocks writing it.
    sys.stdout.write(("frame %d " % seq) + "." * 1000 + "\n")
    sys.stdout.flush()
    try:
        socket.send_fds(s, [h], [fd])
    except BlockingIOError:
        pass
    except OSError:
        note(kind="closed")
        break
    try:
        while True:
            d = s.recv(4096)
            if not d:
                note(kind="closed")
                sys.exit(0)
            for l in d.decode().split("\n"):
                if l:
                    note(kind="answer", line=l)
    except BlockingIOError:
        pass
    time.sleep(0.05)
