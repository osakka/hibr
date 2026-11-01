#!/usr/bin/env python3
"""A stand-in for Blit's frame socket (Blit ADR 0202, wire versions 1 and 2).

    python3 tests/frameserve.py NAME TOKEN LOG [v1]

With v1 it is a Blit older than 0.3.22, refusing a version 2 hello with
EINVAL as that one does.

Version 2 (Blit ADR 0203, the performance panel): while a file LOG.hud
exists the panel is shown, and each version 2 client is sent "HUD1 on\n"
-- right after its OK when it already applies -- and "HUD1 off\n" when the
file goes. A STA1 message is checked as Blit's frame_status checks it and
logged with its lines, or refused with "ERROR 0 EINVAL\n".

Listens on the abstract Unix socket NAME (SOCK_SEQPACKET), as Blit does,
takes a hello carrying TOKEN, and then frames: a 48-byte header and one
memfd. Everything it accepts or refuses is a JSON line in LOG.

It refuses what Blit refuses, since a stand-in that accepts anything cannot
catch a client that sends the wrong thing: a wrong token, a frame before the
hello, a memfd not sealed against shrinking, growing and writing, one too
short for its header, a first frame for an id that is not whole, a sequence
not newer than the last for that id, and a format that is not 24 or 32.
Each refusal is also answered as Blit answers, "ERROR <id> <code>\\n".
"""
import fcntl
import json
import os
import select
import socket
import struct
import sys

NAME, TOKEN, LOG = sys.argv[1], sys.argv[2].encode(), sys.argv[3]
OLD = len(sys.argv) > 4 and sys.argv[4] == "v1"
F_GET_SEALS = 1034
SEALS = 0x0002 | 0x0004 | 0x0008


def note(**kw):
    with open(LOG, "a") as f:
        f.write(json.dumps(kw) + "\n")


def frame(conn, st, data, fds):
    if len(data) != 48 or data[:4] != b"FRM1" or len(fds) != 1:
        for fd in fds:
            os.close(fd)
        note(kind="refused", why="frame-header")
        conn.send(b"ERROR 0 EINVAL\n")
        return
    mfd = fds[0]
    iid, seq, w, h, fmt, stride, x, y, rw, rh = struct.unpack("<IQIIIIIIII", data[4:])
    why = None
    try:
        seals = fcntl.fcntl(mfd, F_GET_SEALS)
    except OSError:
        seals = 0
    rows = rh if rw and rh else h
    size = os.fstat(mfd).st_size
    if seals & SEALS != SEALS:
        why = "frame-not-sealed"
    elif fmt not in (24, 32):
        why = "frame-format"
    elif size < stride * rows:
        why = "frame-short"
    elif iid not in st and (rw or rh):
        why = "frame-edit-unknown"
    elif iid in st and seq <= st[iid]:
        why = "stale"
    if why:
        os.close(mfd)
        note(kind="refused", why=why, id=iid, seq=seq)
        if why != "stale":
            conn.send(("ERROR %d EINVAL\n" % iid).encode())
        return
    st[iid] = seq
    head = os.pread(mfd, min(size, 3), 0)
    os.close(mfd)
    note(kind="frame", id=iid, seq=seq, w=w, h=h, x=x, y=y, rw=rw, rh=rh,
         size=size, first=list(head))


def status(data, fds):
    """The lines of a STA1 message, or None where Blit refuses it."""
    if fds or len(data) < 8:
        return None
    count = struct.unpack("<I", data[4:8])[0]
    if not 1 <= count <= 8 or not data.endswith(b"\n"):
        return None
    lines = data[8:-1].split(b"\n")
    if len(lines) != count:
        return None
    out = []
    for l in lines:
        if len(l) > 64 or any(c < 0x20 or c == 0x7f for c in l):
            return None
        try:
            out.append(l.decode("utf-8"))
        except UnicodeDecodeError:
            return None
    return out


def main():
    srv = socket.socket(socket.AF_UNIX, socket.SOCK_SEQPACKET)
    srv.bind("\0" + NAME.lstrip("@"))
    srv.listen(8)
    note(kind="listening")
    conns = {}
    vers = {}
    images = {}
    hud = False
    while True:
        r, _, _ = select.select([srv] + list(conns), [], [], 0.1)
        now = os.path.exists(LOG + ".hud")
        if now != hud:
            hud = now
            for c, ok in conns.items():
                if ok and vers.get(c) == 2:
                    c.send(b"HUD1 on\n" if hud else b"HUD1 off\n")
                    note(kind="hud", on=hud)
        for s in r:
            if s is srv:
                c, _ = srv.accept()
                conns[c] = False
                continue
            try:
                data, fds, _, _ = socket.recv_fds(s, 4096, 4)
            except OSError:
                data, fds = b"", []
            if not data:
                del conns[s]
                vers.pop(s, None)
                s.close()
                note(kind="closed")
                continue
            if not conns[s]:
                for fd in fds:
                    os.close(fd)
                ver = struct.unpack("<I", data[8:12])[0] if len(data) >= 12 else 0
                if data[:8] != b"BLITFRM1" or ver not in ((1,) if OLD else (1, 2)):
                    note(kind="refused", why="hello-version", version=ver)
                    s.send(b"ERROR 0 EINVAL\n")
                    del conns[s]
                    s.close()
                    continue
                ok = (len(data) >= 16 and
                      data[16:16 + struct.unpack("<I", data[12:16])[0]] == TOKEN)
                if not ok:
                    note(kind="refused", why="hello")
                    s.send(b"ERROR 0 EACCES\n")
                    del conns[s]
                    s.close()
                    continue
                conns[s] = True
                vers[s] = ver
                note(kind="hello", version=ver)
                s.send(b"OK\n")
                if ver == 2 and hud:
                    s.send(b"HUD1 on\n")
                    note(kind="hud", on=True)
                continue
            if data[:4] == b"STA1":
                lines = status(data, fds) if vers.get(s) == 2 else None
                for fd in fds:
                    os.close(fd)
                if lines is None:
                    note(kind="refused", why="status")
                    s.send(b"ERROR 0 EINVAL\n")
                else:
                    note(kind="status", lines=lines)
                continue
            frame(s, images, data, fds)


main()
