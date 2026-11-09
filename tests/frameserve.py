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
# v3 is a Blit with ADR 0223's dmabuf frames, nodma one that says version 3
# and imports nothing. Without either it refuses a version 3 hello, as Blit
# before 0.3.60 does. Planes are checked as Blit checks them, by size, and
# never mapped; a frame's descriptors are held until a newer one of the image
# comes, then released. LOG.hold makes it hold on to everything.
V3 = len(sys.argv) > 4 and sys.argv[4] in ("v3", "nodma")
NODMA = len(sys.argv) > 4 and sys.argv[4] == "nodma"
# A fifth word, hold, is Blit 0.3.70, which takes `INPUT hold ID confine|
# relative`; without it the line is refused as an older Blit refuses it,
# ERROR INPUT 0 EINVAL, since that Blit reads the id from its last word.
HOLD = len(sys.argv) > 5 and sys.argv[5] == "hold"
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


def frame3(conn, held, data, fds):
    if NODMA:
        for fd in fds:
            os.close(fd)
        iid = struct.unpack("<I", data[4:8])[0] if len(data) >= 8 else 0
        note(kind="refused", why="notsup")
        conn.send(b"ERROR %d ENOTSUP\n" % iid)
        return
    ok = len(data) == 72 and data[:4] == b"FRM3"
    if ok:
        iid, seq, w, h, cc, planes, mod = struct.unpack("<IQII4sIQ", data[4:40])
        ok = iid and 1 <= planes <= 4 and len(fds) in (planes, planes + 1)
    if not ok:
        for fd in fds:
            os.close(fd)
        note(kind="refused", why="frame3", fds=len(fds))
        conn.send(b"ERROR 0 EINVAL\n")
        return
    pitch = struct.unpack("<I", data[44:48])[0]
    size = os.lseek(fds[0], 0, os.SEEK_END)
    if pitch < 4 * w or size < pitch * h:
        for fd in fds:
            os.close(fd)
        note(kind="refused", why="plane", size=size)
        conn.send(b"ERROR %d EINVAL\n" % iid)
        return
    note(kind="frame3", id=iid, seq=seq, fds=len(fds), fourcc=cc.decode())
    q = held.setdefault(iid, [])
    q.append((seq, fds))
    while len(q) > 1 and not os.path.exists(LOG + ".hold"):
        old = q.pop(0)
        for fd in old[1]:
            os.close(fd)
        conn.send(b"RELEASE %d %d\n" % (iid, old[0]))
        note(kind="released", id=iid, seq=old[0])


def inputline(conn, held, routes, data, fds):
    """An input route's request, as Blit 0.3.64 takes it (docs/api.md): the
    picture must exist (a frame of it has come), the EIS socket must be the
    connection's own uid's, and an id has one route. Answered and logged."""
    for fd in fds:
        os.close(fd)
    w = data.decode(errors="replace").strip().split(" ")
    iid = int(w[-1]) if len(w) >= 3 and w[-1].isdigit() else 0
    why = None
    if len(w) == 4 and w[1] == "eis":
        try:
            st = os.stat(w[2])
            own = st.st_uid == os.getuid() and (st.st_mode & 0o170000) == 0o140000
        except OSError:
            own = False
        if not iid or iid not in held:
            why = "ENOENT"
        elif not own:
            why = "EACCES"
        elif iid in routes.setdefault(conn, {}):
            why = "EBUSY"
        else:
            routes[conn][iid] = "off"
    elif len(w) == 4 and w[1] == "hold" and HOLD and w[2].isdigit() and \
            w[3] in ("confine", "relative"):
        iid = int(w[2])
        if iid not in routes.get(conn, {}):
            why = "ENOENT"
        else:
            note(kind="hold", id=iid, mode=w[3])
    elif len(w) == 3 and w[1] in ("on", "off"):
        if iid not in routes.get(conn, {}):
            why = "ENOENT"
        else:
            routes[conn][iid] = w[1]
    else:
        why = "EINVAL"
    if why:
        conn.send(b"ERROR INPUT %d %s\n" % (iid, why.encode()))
        note(kind="input", verb=w[1] if len(w) > 1 else "", id=iid, error=why)
    else:
        conn.send(b"OK input %d\n" % iid)
        note(kind="input", verb=w[1], id=iid, ok=True)


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
    held = {}
    routes = {}
    hud = False
    while True:
        r, _, _ = select.select([srv] + list(conns), [], [], 0.1)
        # An image deleted on the terminal (a=d,d=I): Blit's
        # frame_dmabuf_free lets go of every frame it still holds of it,
        # each answered. The stand-in cannot see the terminal, so the test
        # hands it the ids it saw deleted, one a line, in LOG.del.
        if os.path.exists(LOG + ".del"):
            gone = [int(x) for x in open(LOG + ".del").read().split()]
            os.unlink(LOG + ".del")
            for iid in gone:
                for r in routes.values():
                    r.pop(iid, None)
                for seq, fds in held.pop(iid, []):
                    for fd in fds:
                        os.close(fd)
                    for c, ok in conns.items():
                        if ok and vers.get(c, 0) >= 3:
                            c.send(b"RELEASE %d %d\n" % (iid, seq))
                    note(kind="released", id=iid, seq=seq, deleted=True)
        now = os.path.exists(LOG + ".hud")
        if now != hud:
            hud = now
            for c, ok in conns.items():
                if ok and vers.get(c, 0) >= 2:
                    c.send(b"HUD1 on\n" if hud else b"HUD1 off\n")
                    note(kind="hud", on=hud)
        for s in r:
            if s is srv:
                c, _ = srv.accept()
                conns[c] = False
                continue
            try:
                data, fds, _, _ = socket.recv_fds(s, 4096, 5)
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
                if data[:8] != b"BLITFRM1" or ver not in ((1,) if OLD else (1, 2, 3) if V3 else (1, 2)):
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
                if ver == 3:
                    s.send(b"OK dmabuf=0\n" if NODMA else
                           b"OK dmabuf=1 fence=0 formats=XR24/0,AR24/0\n")
                else:
                    s.send(b"OK\n")
                if ver >= 2 and hud:
                    s.send(b"HUD1 on\n")
                    note(kind="hud", on=True)
                continue
            if data[:4] == b"STA1":
                lines = status(data, fds) if vers.get(s, 0) >= 2 else None
                for fd in fds:
                    os.close(fd)
                if lines is None:
                    note(kind="refused", why="status")
                    s.send(b"ERROR 0 EINVAL\n")
                else:
                    note(kind="status", lines=lines)
                continue
            if data[:4] == b"FRM3":
                frame3(s, held, data, fds)
                continue
            if data[:6] == b"INPUT " and vers.get(s, 0) >= 3:
                inputline(s, held, routes, data, fds)
                continue
            frame(s, images, data, fds)


main()
