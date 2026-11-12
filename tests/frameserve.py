#!/usr/bin/env python3
"""A stand-in for Blit's frame socket (Blit ADR 0202), at wire version 5,
the only one since Blit 0.3.74: every line about a picture is
`<WORD> <verb> <id> [arguments]`, the id the third word.

    python3 tests/frameserve.py NAME TOKEN LOG [old|v3|nodma]

With old it is a Blit older than 0.3.74, refusing a version 5 hello with
EINVAL as that one does. v3 imports dmabufs (ADR 0223); without it, or with
nodma, the hello says dmabuf=0 and only memfd frames are taken.

The performance panel (Blit ADR 0203): while a file LOG.hud exists the
panel is shown, and each client is sent "HUD1 on\n" -- right after its OK
when it already applies -- and "HUD1 off\n" when the file goes. A STA1
message is checked as Blit's frame_status checks it and logged with its
lines, or refused with "FRAME error 0 EINVAL\n".

Listens on the abstract Unix socket NAME (SOCK_SEQPACKET), as Blit does,
takes a hello carrying TOKEN, and then frames: a 48-byte header and one
memfd. Everything it accepts or refuses is a JSON line in LOG.

It refuses what Blit refuses, since a stand-in that accepts anything cannot
catch a client that sends the wrong thing: a wrong token, a frame before the
hello, a memfd not sealed against shrinking, growing and writing, one too
short for its header, a first frame for an id that is not whole, a sequence
not newer than the last for that id, and a format that is not 24 or 32.
Each refusal is also answered as Blit answers, "FRAME error <id> <code>\\n".
"""
import fcntl
import json
import os
import select
import socket
import struct
import sys
import time

NAME, TOKEN, LOG = sys.argv[1], sys.argv[2].encode(), sys.argv[3]
OLD = len(sys.argv) > 4 and sys.argv[4] in ("old", "v1")
# v3 is a Blit that imports dmabuf frames (ADR 0223); otherwise the hello
# says dmabuf=0 and an FRM3 is refused ENOTSUP. Planes are checked as Blit
# checks them, by size, and never mapped; a frame's descriptors are held
# until a newer one of the image comes, then released. LOG.hold makes it
# hold on to everything. A fifth word, once `hold` for Blit 0.3.70, is
# taken and ignored: every version 5 Blit takes INPUT hold and member.
NODMA = not (len(sys.argv) > 4 and sys.argv[4] == "v3")
F_GET_SEALS = 1034
SEALS = 0x0002 | 0x0004 | 0x0008


KEEPNAMED = {"up", "down", "left", "right", "home", "end", "pageup", "pagedown",
             "insert", "delete", "tab", "enter", "space", "escape", "backspace",
             "print", "menu"} | {"f%d" % i for i in range(1, 25)}


def keepok(c):
    """A chord as Blit's INPUT keep spells it: ctrl-, alt-, shift-, super-
    in that order, then a key -- a letter, a digit, US unshifted punctuation
    or a named key; super alone is the tap."""
    if c == "super":
        return True
    for m in ("ctrl-", "alt-", "shift-", "super-"):
        if c.startswith(m) and len(c) > len(m):
            c = c[len(m):]
    return (len(c) == 1 and (c.isalnum() or c in "`-=[]\;',./")) or c in KEEPNAMED


def note(**kw):
    with open(LOG, "a") as f:
        f.write(json.dumps(kw) + "\n")


def frame(conn, st, data, fds):
    if len(data) != 48 or data[:4] != b"FRM1" or len(fds) != 1:
        for fd in fds:
            os.close(fd)
        note(kind="refused", why="frame-header")
        conn.send(b"FRAME error 0 EINVAL\n")
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
            conn.send(("FRAME error %d EINVAL\n" % iid).encode())
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
        conn.send(b"FRAME error %d ENOTSUP\n" % iid)
        return
    ok = len(data) == 72 and data[:4] == b"FRM3"
    if ok:
        iid, seq, w, h, cc, planes, mod = struct.unpack("<IQII4sIQ", data[4:40])
        ok = iid and 1 <= planes <= 4 and len(fds) in (planes, planes + 1)
    if not ok:
        for fd in fds:
            os.close(fd)
        note(kind="refused", why="frame3", fds=len(fds))
        conn.send(b"FRAME error 0 EINVAL\n")
        return
    pitch = struct.unpack("<I", data[44:48])[0]
    size = os.lseek(fds[0], 0, os.SEEK_END)
    if pitch < 4 * w or size < pitch * h:
        for fd in fds:
            os.close(fd)
        note(kind="refused", why="plane", size=size)
        conn.send(b"FRAME error %d EINVAL\n" % iid)
        return
    note(kind="frame3", id=iid, seq=seq, fds=len(fds), fourcc=cc.decode())
    q = held.setdefault(iid, [])
    q.append((seq, fds))
    while len(q) > 1 and not os.path.exists(LOG + ".hold"):
        old = q.pop(0)
        for fd in old[1]:
            os.close(fd)
        conn.send(b"FRAME release %d %d\n" % (iid, old[0]))
        note(kind="released", id=iid, seq=old[0])


def inputline(conn, held, routes, data, fds):
    """An input route's request, as Blit takes it at version 5 (docs/api.md):
    the id third, the socket must be there and the connection's own uid's,
    the picture need not have a frame yet, and an id has one route.
    Answered and logged."""
    for fd in fds:
        os.close(fd)
    w = data.decode(errors="replace").strip().split(" ")
    iid = int(w[2]) if len(w) >= 3 and w[2].isdigit() else 0
    why = None
    # `INPUT eis` went with Blit's gamescope bridge in 0.3.75: an unknown
    # line now, refused as one.
    if len(w) == 4 and w[1] == "sock":
        # As Blit's api.md has it: the image need not have a frame yet;
        # ENOENT is no such socket, EACCES one that is not the client's.
        try:
            st = os.stat(w[3])
            here = (st.st_mode & 0o170000) == 0o140000
            own = here and st.st_uid == os.getuid()
        except OSError:
            here = own = False
        if not iid:
            why = "EINVAL"
        elif not here:
            why = "ENOENT"
        elif not own:
            why = "EACCES"
        elif iid in routes.setdefault(conn, {}):
            why = "EBUSY"
        else:
            routes[conn][iid] = "off"
            if w[1] == "sock":
                SOCKS[(conn, iid)] = w[3]
    elif len(w) == 4 and w[1] == "hold" and iid and \
            w[3] in ("confine", "relative"):
        if iid not in routes.get(conn, {}):
            why = "ENOENT"
        else:
            note(kind="hold", id=iid, mode=w[3])
    elif len(w) == 6 and w[1] == "member" and all(x.lstrip("-").isdigit() for x in w[2:]):
        # As Blit's api.md has it: the popup need not have a frame; the
        # top-level must have a route on this connection.
        if int(w[3]) not in routes.get(conn, {}) or iid == int(w[3]):
            why = "EINVAL"
        else:
            note(kind="member", id=iid, top=int(w[3]), x=int(w[4]), y=int(w[5]))
    elif len(w) >= 2 and w[1] == "keep":
        # Blit 0.3.95 (Gitea #530): the chords the desktop keeps, the whole
        # set each time, not answered; a chord out of its one spelling
        # refuses the whole line with INPUT error 0 EINVAL.
        if all(keepok(c) for c in w[2:]):
            note(kind="keep", chords=w[2:])
            return
        why = "EINVAL"
    elif len(w) == 3 and w[1] in ("on", "off"):
        if iid not in routes.get(conn, {}):
            why = "ENOENT"
        else:
            routes[conn][iid] = w[1]
    else:
        why = "EINVAL"
    if why:
        conn.send(b"INPUT error %d %s\n" % (iid, why.encode()))
        note(kind="input", verb=w[1] if len(w) > 1 else "", id=iid, error=why)
    else:
        conn.send(b"INPUT ok %d\n" % iid)
        note(kind="input", verb=w[1], id=iid, ok=True)


SOCKS = {}


def sendkeys(routes, at="10.00 10.00"):
    """LOG.keys: as Blit does down a live `INPUT sock` route (docs/api.md,
    "Input straight from Blit"), connect to the compositor's socket and
    write a pointer move -- to 10,10, or to the "X Y" the file starts with,
    in the top-level's pixels -- and then, by the file's next words, a key
    pressed and let go (`key CODE`, Space by default), a button pressed
    (`press CODE`) or let go (`release CODE`, the left by default), each a
    batch ended by its frame, under Blit's own picture id."""
    w = at.split()
    xy = " ".join(w[:2]) if len(w) >= 2 else "10.00 10.00"
    act = w[2] if len(w) > 2 else "key"
    code = int(w[3]) if len(w) > 3 else (57 if act == "key" else 272)
    for (c, iid), path in list(SOCKS.items()):
        if routes.get(c, {}).get(iid) != "on":
            continue
        try:
            k = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            k.connect(path)
            us = int(time.monotonic() * 1e6)
            out = "INPUT ptr %d %s\nINPUT frame %d %d\n" % (iid, xy, iid, us)
            if act == "key":
                out += ("INPUT key %d %d press\nINPUT frame %d %d\n"
                        "INPUT key %d %d release\nINPUT frame %d %d\n"
                        % (iid, code, iid, us + 1, iid, code, iid, us + 2))
            else:
                out += ("INPUT btn %d %d %s\nINPUT frame %d %d\n"
                        % (iid, code, act, iid, us + 1))
            k.sendall(out.encode())
            KEEP.append(k)
            note(kind="sentkeys", id=iid, path=path, act=act, code=code)
        except OSError as e:
            note(kind="sentkeys", id=iid, path=path, error=str(e))


KEEP = []


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
                        if ok:
                            c.send(b"FRAME release %d %d\n" % (iid, seq))
                    note(kind="released", id=iid, seq=seq, deleted=True)
        if os.path.exists(LOG + ".keys"):
            at = open(LOG + ".keys").read().strip()
            os.unlink(LOG + ".keys")
            sendkeys(routes, at or "10.00 10.00")
        now = os.path.exists(LOG + ".hud")
        if now != hud:
            hud = now
            for c, ok in conns.items():
                if ok:
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
                # Blit forgets a connection's routes with it.
                routes.pop(s, None)
                for key in [k for k in SOCKS if k[0] is s]:
                    del SOCKS[key]
                s.close()
                note(kind="closed")
                continue
            if not conns[s]:
                for fd in fds:
                    os.close(fd)
                ver = struct.unpack("<I", data[8:12])[0] if len(data) >= 12 else 0
                if data[:8] != b"BLITFRM1" or OLD or ver != 5:
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
                s.send(b"OK dmabuf=0 window=0\n" if NODMA else
                       b"OK dmabuf=1 fence=0 formats=XR24/0,AR24/0 window=0\n")
                if hud:
                    s.send(b"HUD1 on\n")
                    note(kind="hud", on=True)
                continue
            if data[:4] == b"STA1":
                lines = status(data, fds)
                for fd in fds:
                    os.close(fd)
                if lines is None:
                    note(kind="refused", why="status")
                    s.send(b"FRAME error 0 EINVAL\n")
                else:
                    note(kind="status", lines=lines)
                continue
            if data[:4] == b"FRM3":
                frame3(s, held, data, fds)
                continue
            if data[:6] == b"INPUT ":
                inputline(s, held, routes, data, fds)
                continue
            frame(s, images, data, fds)


main()
