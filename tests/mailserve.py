#!/usr/bin/env python3
"""A stand-in mail server for tests/email.py: IMAP4rev1 with the parts the
email module uses -- LOGIN and AUTHENTICATE PLAIN, STARTTLS, LIST with
special-use flags, SELECT, UID FETCH/STORE/SEARCH/MOVE/COPY, APPEND, IDLE,
and with gmail=True Gmail's X-GM-EXT-1 (labels, thread and message ids) --
plus POP3 (USER/PASS, STLS, LIST, UIDL, RETR, DELE) and SMTP (EHLO,
STARTTLS, AUTH PLAIN and LOGIN, MAIL, RCPT, DATA). Everything lives in a
Box the test can change while a client is connected; a change wakes any
client in IDLE. Nothing touches the network beyond 127.0.0.1.
"""
import base64, re, socket, ssl, threading, time


class Msg:
    def __init__(self, uid, raw, flags=(), labels=(), thrid=0, gmid=0, idate=None):
        self.uid, self.raw = uid, raw if isinstance(raw, bytes) else raw.encode()
        self.flags, self.labels = set(flags), set(labels)
        self.thrid, self.gmid = thrid or uid * 1000, gmid or uid * 1000 + 1
        self.idate = idate or "01-Oct-2026 09:30:00 +0000"


class Box:
    """Folders of messages, and what changed for IDLE to report."""

    def __init__(self, user="pat", password="right horse", gmail=False):
        self.user, self.password, self.gmail = user, password, gmail
        self.folders = {"INBOX": [], "Sent": [], "Trash": [], "Archive": []}
        self.special = {"Sent": "\\Sent", "Trash": "\\Trash", "Archive": "\\Archive"}
        if gmail:
            self.folders = {"INBOX": [], "[Gmail]/All Mail": [], "[Gmail]/Sent Mail": [],
                            "[Gmail]/Trash": [], "Work/Projets dété": []}
            self.special = {"[Gmail]/All Mail": "\\All", "[Gmail]/Sent Mail": "\\Sent",
                            "[Gmail]/Trash": "\\Trash"}
        self.next = {f: 1 for f in self.folders}
        self.cv = threading.Condition()
        self.gen = 0
        self.sent = []
        self.pop = []
        self.drop_stores = 0

    def add(self, folder, raw, **kw):
        with self.cv:
            m = Msg(self.next[folder], raw, **kw)
            self.next[folder] += 1
            self.folders[folder].append(m)
            self.gen += 1
            self.cv.notify_all()
            return m


def mutf7(s):
    out, buf = "", ""

    def flush():
        nonlocal out, buf
        if buf:
            b = base64.b64encode(buf.encode("utf-16-be")).decode().rstrip("=").replace("/", ",")
            out += "&" + b + "-"
            buf = ""
    for ch in s:
        if 0x20 <= ord(ch) < 0x7F:
            flush()
            out += "&-" if ch == "&" else ch
        else:
            buf += ch
    flush()
    return out


def unmutf7(s):
    def dec(m):
        b = m.group(1)
        if not b:
            return "&"
        b = b.replace(",", "/")
        b += "=" * (-len(b) % 4)
        return base64.b64decode(b).decode("utf-16-be")
    return re.sub(r"&([^-]*)-", dec, s)


def quote(s):
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'


class Conn:
    """One client: its socket wrapped in a buffered reader, TLS when asked."""

    def __init__(self, sock, ctx):
        self.s, self.ctx, self.buf = sock, ctx, b""
        sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)

    def send(self, data):
        self.s.sendall(data if isinstance(data, bytes) else data.encode())

    def line(self):
        while b"\n" not in self.buf:
            d = self.s.recv(65536)
            if not d:
                return None
            self.buf += d
        ln, self.buf = self.buf.split(b"\n", 1)
        return ln.rstrip(b"\r").decode("utf-8", "replace")

    def exact(self, n):
        while len(self.buf) < n:
            d = self.s.recv(65536)
            if not d:
                return None
            self.buf += d
        d, self.buf = self.buf[:n], self.buf[n:]
        return d

    def starttls(self):
        self.s = self.ctx.wrap_socket(self.s, server_side=True)


def args(s):
    """Split an IMAP command's arguments: atoms, quoted strings, lists."""
    out, i = [], 0
    while i < len(s):
        c = s[i]
        if c == " ":
            i += 1
        elif c == '"':
            j, v = i + 1, ""
            while j < len(s) and s[j] != '"':
                if s[j] == "\\":
                    j += 1
                v += s[j]
                j += 1
            out.append(v)
            i = j + 1
        elif c == "(":
            d, j = 1, i + 1
            while j < len(s) and d:
                d += {"(": 1, ")": -1}.get(s[j], 0)
                j += 1
            out.append(s[i:j])
            i = j
        else:
            j = i
            while j < len(s) and s[j] != " ":
                if s[j] == "[":
                    j = s.index("]", j)
                j += 1
            out.append(s[i:j])
            i = j
    return out


def uidset(spec, msgs):
    uids = [m.uid for m in msgs]
    top = max(uids) if uids else 0
    want = set()
    for part in spec.split(","):
        a, _, b = part.partition(":")
        a = top if a == "*" else int(a)
        b = a if not b else (top if b == "*" else int(b))
        lo, hi = min(a, b), max(a, b)
        want.update(u for u in uids if lo <= u <= hi)
    return [m for m in msgs if m.uid in want]


def headers(raw, names):
    head = raw.split(b"\r\n\r\n", 1)[0] + b"\r\n"
    out, keep = b"", False
    for ln in head.split(b"\r\n"):
        if ln[:1] in (b" ", b"\t"):
            if keep:
                out += ln + b"\r\n"
            continue
        k = ln.split(b":", 1)[0].strip().upper().decode("latin-1")
        keep = k in names
        if keep:
            out += ln + b"\r\n"
    return out + b"\r\n"


def imap(conn, box, starttls_ok):
    authed, folder, tagged = False, None, None
    conn.send("* OK [CAPABILITY IMAP4rev1] stand-in ready\r\n")

    def caps():
        c = "IMAP4rev1 IDLE UIDPLUS MOVE SPECIAL-USE SASL-IR AUTH=PLAIN"
        if starttls_ok:
            c += " STARTTLS"
        if box.gmail:
            c += " X-GM-EXT-1"
        return c
    while True:
        ln = conn.line()
        if ln is None:
            return
        tag, _, rest = ln.partition(" ")
        cmd, _, rest = rest.partition(" ")
        cmd = cmd.upper()
        if cmd == "UID":
            cmd2, _, rest = rest.partition(" ")
            cmd = "UID " + cmd2.upper()
        if cmd == "UID STORE" and box.drop_stores:
            box.drop_stores -= 1
            if not box.drop_stores:
                return
        if cmd == "CAPABILITY":
            conn.send("* CAPABILITY %s\r\n%s OK done\r\n" % (caps(), tag))
        elif cmd == "STARTTLS" and starttls_ok:
            conn.send("%s OK begin TLS\r\n" % tag)
            conn.starttls()
        elif cmd == "LOGIN":
            a = args(rest)
            if a[0] == box.user and a[1] == box.password:
                authed = True
                conn.send("%s OK logged in\r\n" % tag)
            else:
                conn.send("%s NO [AUTHENTICATIONFAILED] Invalid credentials\r\n" % tag)
        elif cmd == "AUTHENTICATE":
            a = rest.split()
            parts = base64.b64decode(a[1]).split(b"\0") if len(a) > 1 else []
            if len(parts) == 3 and parts[1].decode() == box.user and parts[2].decode() == box.password:
                authed = True
                conn.send("%s OK authenticated\r\n" % tag)
            else:
                conn.send("%s NO [AUTHENTICATIONFAILED] Invalid credentials\r\n" % tag)
        elif cmd == "LOGOUT":
            conn.send("* BYE\r\n%s OK bye\r\n" % tag)
            return
        elif cmd == "NOOP":
            conn.send("%s OK noop\r\n" % tag)
        elif not authed:
            conn.send("%s NO log in first\r\n" % tag)
        elif cmd == "LIST":
            for f in box.folders:
                fl = box.special.get(f, "")
                kids = "\\HasChildren" if any(g.startswith(f + "/") for g in box.folders) else "\\HasNoChildren"
                conn.send("* LIST (%s) \"/\" %s\r\n" % ((kids + " " + fl).strip(), quote(mutf7(f))))
            conn.send("%s OK listed\r\n" % tag)
        elif cmd in ("SELECT", "EXAMINE"):
            f = unmutf7(args(rest)[0])
            if f.upper() == "INBOX":
                f = "INBOX"
            if f not in box.folders:
                conn.send("%s NO no such folder\r\n" % tag)
                continue
            folder = f
            ms = box.folders[f]
            conn.send("* %d EXISTS\r\n* OK [UIDVALIDITY 7] ok\r\n* OK [UIDNEXT %d] ok\r\n%s OK [READ-WRITE] selected\r\n"
                      % (len(ms), box.next[f], tag))
        elif cmd == "UID SEARCH":
            ms = box.folders[folder]
            conn.send("* SEARCH %s\r\n%s OK searched\r\n" % (" ".join(str(m.uid) for m in ms), tag))
        elif cmd == "UID FETCH":
            spec, _, items = rest.partition(" ")
            want = set(id(m) for m in uidset(spec, box.folders[folder]))
            for seq, m in enumerate(box.folders[folder], 1):
                if id(m) not in want:
                    continue
                parts = [b"UID %d" % m.uid]
                if "FLAGS" in items:
                    parts.append(("FLAGS (%s)" % " ".join(sorted(m.flags))).encode())
                if "INTERNALDATE" in items:
                    parts.append(('INTERNALDATE "%s"' % m.idate).encode())
                if "RFC822.SIZE" in items:
                    parts.append(b"RFC822.SIZE %d" % len(m.raw))
                if box.gmail and "X-GM-LABELS" in items:
                    parts.append(("X-GM-LABELS (%s)" % " ".join(
                        lb if lb.startswith("\\") else quote(mutf7(lb)) for lb in sorted(m.labels))).encode())
                if box.gmail and "X-GM-THRID" in items:
                    parts.append(b"X-GM-THRID %d" % m.thrid)
                if box.gmail and "X-GM-MSGID" in items:
                    parts.append(b"X-GM-MSGID %d" % m.gmid)
                hm = re.search(r"BODY\.PEEK\[HEADER\.FIELDS \(([^)]*)\)\]", items)
                if hm:
                    h = headers(m.raw, set(hm.group(1).upper().split()))
                    parts.append(b"BODY[HEADER.FIELDS (" + hm.group(1).encode() + b")] {%d}\r\n" % len(h) + h)
                elif "BODY.PEEK[]" in items:
                    parts.append(b"BODY[] {%d}\r\n" % len(m.raw) + m.raw)
                conn.send(b"* %d FETCH (" % seq + b" ".join(parts) + b")\r\n")
            conn.send("%s OK fetched\r\n" % tag)
        elif cmd == "UID STORE":
            spec, op, vals = rest.split(" ", 2)
            words = args(vals.strip("()")) if vals.strip("()") else []
            for m in uidset(spec, box.folders[folder]):
                tgt = m.labels if "X-GM-LABELS" in op else m.flags
                words2 = [unmutf7(w) for w in words] if "X-GM-LABELS" in op else words
                if op.startswith("+"):
                    tgt.update(words2)
                elif op.startswith("-"):
                    tgt.difference_update(words2)
            conn.send("%s OK stored\r\n" % tag)
        elif cmd in ("UID MOVE", "UID COPY"):
            spec, dest = rest.split(" ", 1)
            dest = unmutf7(args(dest)[0])
            if dest not in box.folders:
                conn.send("%s NO [TRYCREATE] no such folder\r\n" % tag)
                continue
            for m in uidset(spec, box.folders[folder]):
                box.add(dest, m.raw, flags=m.flags, labels=m.labels)
                if cmd == "UID MOVE":
                    box.folders[folder].remove(m)
            conn.send("%s OK done\r\n" % tag)
        elif cmd == "EXPUNGE" or cmd == "UID EXPUNGE":
            box.folders[folder] = [m for m in box.folders[folder] if "\\Deleted" not in m.flags]
            conn.send("%s OK expunged\r\n" % tag)
        elif cmd == "APPEND":
            m = re.match(r'(\S+|"[^"]*") \(([^)]*)\) \{(\d+)\}$', rest)
            dest = unmutf7(args(m.group(1))[0])
            conn.send("+ go ahead\r\n")
            data = conn.exact(int(m.group(3)))
            conn.line()
            box.add(dest, data, flags=m.group(2).split())
            conn.send("%s OK appended\r\n" % tag)
        elif cmd == "IDLE":
            conn.send("+ idling\r\n")
            with box.cv:
                gen = box.gen
            done = threading.Event()

            def watch():
                with box.cv:
                    while box.gen == gen and not done.is_set():
                        box.cv.wait(0.2)
                    if box.gen != gen and folder in box.folders:
                        try:
                            conn.send("* %d EXISTS\r\n" % len(box.folders[folder]))
                        except OSError:
                            pass
            th = threading.Thread(target=watch, daemon=True)
            th.start()
            ln = conn.line()
            done.set()
            th.join(1)
            conn.send("%s OK idle done\r\n" % tag)
            if ln is None:
                return
        else:
            conn.send("%s BAD unknown command\r\n" % tag)


def pop3(conn, box, starttls_ok):
    user, authed = None, False
    conn.send("+OK stand-in POP3\r\n")
    while True:
        ln = conn.line()
        if ln is None:
            return
        cmd, _, arg = ln.partition(" ")
        cmd = cmd.upper()
        live = [m for m in box.pop if not m.get("deleted")]
        if cmd == "STLS" and starttls_ok:
            conn.send("+OK begin TLS\r\n")
            conn.starttls()
        elif cmd == "USER":
            user = arg
            conn.send("+OK\r\n")
        elif cmd == "PASS":
            authed = user == box.user and arg == box.password
            conn.send("+OK logged in\r\n" if authed else "-ERR [AUTH] invalid password\r\n")
        elif cmd == "QUIT":
            box.pop = [m for m in box.pop if not m.get("deleted")]
            conn.send("+OK bye\r\n")
            return
        elif not authed:
            conn.send("-ERR log in first\r\n")
        elif cmd == "LIST":
            conn.send("+OK\r\n" + "".join("%d %d\r\n" % (i + 1, len(m["raw"])) for i, m in enumerate(box.pop)
                                          if not m.get("deleted")) + ".\r\n")
        elif cmd == "UIDL":
            conn.send("+OK\r\n" + "".join("%d %s\r\n" % (i + 1, m["uid"]) for i, m in enumerate(box.pop)
                                          if not m.get("deleted")) + ".\r\n")
        elif cmd == "RETR":
            m = box.pop[int(arg) - 1]
            body = b"\r\n".join((b"." + l if l.startswith(b".") else l) for l in m["raw"].split(b"\r\n"))
            conn.send(b"+OK\r\n" + body + (b"" if body.endswith(b"\r\n") else b"\r\n") + b".\r\n")
        elif cmd == "DELE":
            box.pop[int(arg) - 1]["deleted"] = True
            conn.send("+OK deleted\r\n")
        else:
            conn.send("-ERR unknown\r\n")


def smtp(conn, box, starttls_ok):
    authed, frm, rcpt = False, None, []
    conn.send("220 stand-in ESMTP\r\n")
    while True:
        ln = conn.line()
        if ln is None:
            return
        cmd = ln[:4].upper()
        if cmd == "EHLO":
            ext = ["250-stand-in", "250-AUTH PLAIN LOGIN", "250-8BITMIME"]
            if starttls_ok:
                ext.append("250-STARTTLS")
            conn.send("\r\n".join(ext) + "\r\n250 SIZE 10000000\r\n")
        elif ln.upper() == "STARTTLS" and starttls_ok:
            conn.send("220 go ahead\r\n")
            conn.starttls()
        elif ln.upper().startswith("AUTH PLAIN "):
            p = base64.b64decode(ln.split()[2]).split(b"\0")
            authed = len(p) == 3 and p[1].decode() == box.user and p[2].decode() == box.password
            conn.send("235 ok\r\n" if authed else "535 5.7.8 bad credentials\r\n")
        elif ln.upper() == "AUTH LOGIN":
            conn.send("334 VXNlcm5hbWU6\r\n")
            u = base64.b64decode(conn.line()).decode()
            conn.send("334 UGFzc3dvcmQ6\r\n")
            pw = base64.b64decode(conn.line()).decode()
            authed = u == box.user and pw == box.password
            conn.send("235 ok\r\n" if authed else "535 5.7.8 bad credentials\r\n")
        elif cmd == "MAIL":
            if not authed:
                conn.send("530 authenticate first\r\n")
                continue
            frm, rcpt = ln[10:].strip("<> "), []
            conn.send("250 ok\r\n")
        elif cmd == "RCPT":
            rcpt.append(ln[8:].strip("<> "))
            conn.send("250 ok\r\n")
        elif cmd == "DATA":
            conn.send("354 go\r\n")
            lines = []
            while True:
                l = conn.line()
                if l is None or l == ".":
                    break
                lines.append(l[1:] if l.startswith(".") else l)
            box.sent.append({"from": frm, "to": rcpt, "raw": "\r\n".join(lines) + "\r\n"})
            conn.send("250 queued\r\n")
        elif cmd == "QUIT":
            conn.send("221 bye\r\n")
            return
        else:
            conn.send("502 unknown\r\n")


def serve(proto, box, tls=None, starttls=None):
    """Listen on a free port on 127.0.0.1; give the port. tls is a
    (cert, key) pair for TLS from the start; starttls the same for
    STARTTLS/STLS."""
    ls = socket.socket()
    ls.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    ls.bind(("127.0.0.1", 0))
    ls.listen(8)
    pair = tls or starttls
    ctx = None
    if pair:
        ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        ctx.load_cert_chain(pair[0], pair[1])
    fn = {"imap": imap, "pop": pop3, "smtp": smtp}[proto]

    def one(s):
        conn = Conn(s, ctx)
        try:
            if tls:
                conn.starttls()
            fn(conn, box, bool(starttls))
        except (OSError, ssl.SSLError, ValueError, IndexError):
            pass
        finally:
            try:
                conn.s.close()
            except OSError:
                pass

    def loop():
        while True:
            s, _ = ls.accept()
            threading.Thread(target=one, args=(s,), daemon=True).start()
    threading.Thread(target=loop, daemon=True).start()
    return ls.getsockname()[1]
