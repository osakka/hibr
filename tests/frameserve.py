#!/usr/bin/env python3
"""A stand-in for Blit's frame socket (Blit ADR 0202, wire version 1).

    python3 tests/frameserve.py NAME TOKEN LOG

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


def main():
    srv = socket.socket(socket.AF_UNIX, socket.SOCK_SEQPACKET)
    srv.bind("\0" + NAME.lstrip("@"))
    srv.listen(8)
    note(kind="listening")
    conns = {}
    images = {}
    while True:
        r, _, _ = select.select([srv] + list(conns), [], [], 1.0)
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
                s.close()
                note(kind="closed")
                continue
            if not conns[s]:
                for fd in fds:
                    os.close(fd)
                ok = (data[:8] == b"BLITFRM1" and len(data) >= 16 and
                      data[16:16 + struct.unpack("<I", data[12:16])[0]] == TOKEN)
                if not ok:
                    note(kind="refused", why="hello")
                    s.send(b"ERROR EACCES\n")
                    del conns[s]
                    s.close()
                    continue
                conns[s] = True
                note(kind="hello")
                s.send(b"OK\n")
                continue
            frame(s, images, data, fds)


main()
