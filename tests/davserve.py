#!/usr/bin/env python3
"""A small WebDAV server for tests/dav.py: class 1 of RFC 4918 over a
folder, with the corners a client has to get right switchable from the
command line -- Basic or Digest logins, TLS, chunked replies, a default
namespace instead of a prefix, absolute hrefs, a base path, redirects for
a folder named without its slash, idle connections dropped, and a nonce
that goes stale. Prints "port N" once it is listening.
"""
import argparse, hashlib, html, os, secrets, shutil, socket, ssl, sys
import threading, time
from email.utils import formatdate
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import quote, unquote, urlsplit

A = None
NONCES = {}
LOCK = threading.Lock()
COUNT = [0]


def etag(st):
    return '"%x-%x"' % (st.st_mtime_ns, st.st_size)


class H(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, fmt, *args):
        if A.log:
            with open(A.log, "a") as f:
                f.write("%s %s %s\n" % (self.command, self.path,
                                         "auth" if self.headers.get("Authorization") else "-"))

    def setup(self):
        super().setup()
        if A.idle:
            self.request.settimeout(A.idle)

    def local(self, path=None):
        p = urlsplit(path if path is not None else self.path).path
        p = unquote(p)
        if not p.startswith(A.prefix):
            return None
        rel = p[len(A.prefix):].lstrip("/")
        full = os.path.normpath(os.path.join(A.root, rel))
        if full != A.root and not full.startswith(A.root + os.sep):
            return None
        return full

    def href(self, full):
        rel = os.path.relpath(full, A.root)
        rel = "" if rel == "." else rel
        h = A.prefix.rstrip("/") + "/" + quote(rel)
        if os.path.isdir(full) and not h.endswith("/"):
            h += "/"
        if A.absolute:
            h = "%s://%s%s" % ("https" if A.tls else "http",
                               self.headers.get("Host", "localhost"), h)
        return h

    def send(self, code, body=b"", headers=None, ctype="text/plain"):
        if isinstance(body, str):
            body = body.encode()
        self.send_response(code)
        for k, v in (headers or {}).items():
            self.send_header(k, v)
        if A.close:
            self.send_header("Connection", "close")
            self.close_connection = True
        if body or code not in (204, 304):
            self.send_header("Content-Type", ctype)
        if A.chunked and body and self.command != "HEAD":
            self.send_header("Transfer-Encoding", "chunked")
            self.end_headers()
            for i in range(0, len(body), 100):
                part = body[i:i + 100]
                self.wfile.write(b"%x;ext=1\r\n" % len(part) + part + b"\r\n")
            self.wfile.write(b"0\r\nX-Trailer: yes\r\n\r\n")
        else:
            if code not in (204, 304):
                self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            if self.command != "HEAD":
                self.wfile.write(body)

    def body(self):
        n = int(self.headers.get("Content-Length") or 0)
        return self.rfile.read(n) if n else b""

    def authed(self):
        COUNT[0] += 1
        if A.auth == "none":
            return True
        h = self.headers.get("Authorization", "")
        if A.auth == "basic" and h.startswith("Basic "):
            import base64
            try:
                u, p = base64.b64decode(h[6:]).decode().split(":", 1)
            except Exception:
                u = p = None
            if (u, p) == (A.user, A.password):
                return True
        if A.auth == "digest" and h.startswith("Digest "):
            f = {}
            for part in h[7:].split(","):
                if "=" in part:
                    k, v = part.strip().split("=", 1)
                    f[k] = v.strip('"')
            ha1 = hashlib.md5(("%s:%s:%s" % (A.user, "hibr", A.password)).encode()).hexdigest()
            ha2 = hashlib.md5(("%s:%s" % (self.command, f.get("uri", ""))).encode()).hexdigest()
            want = hashlib.md5(("%s:%s:%s:%s:%s:%s" % (ha1, f.get("nonce"), f.get("nc"),
                                f.get("cnonce"), f.get("qop"), ha2)).encode()).hexdigest()
            with LOCK:
                age = NONCES.get(f.get("nonce"))
            if f.get("username") == A.user and f.get("response") == want and age is not None:
                with LOCK:
                    NONCES[f["nonce"]] = age + 1
                if A.stale and age >= A.stale:
                    self.challenge(stale=True)
                    return False
                return True
        self.challenge()
        return False

    def challenge(self, stale=False):
        self.body()
        if A.auth == "basic":
            hv = 'Basic realm="hibr"'
            extra = None
        else:
            n = secrets.token_hex(8)
            with LOCK:
                NONCES[n] = 0
            hv = 'Digest realm="hibr", qop="auth", algorithm=MD5, nonce="%s", opaque="x1"%s' % (
                n, ', stale=true' if stale else "")
            extra = 'Basic realm="ignored"'
        self.send_response(401)
        if extra:
            self.send_header("WWW-Authenticate", extra)
        self.send_header("WWW-Authenticate", hv)
        self.send_header("Content-Length", "0")
        if A.close:
            self.send_header("Connection", "close")
            self.close_connection = True
        self.end_headers()

    def slashfix(self, full):
        if os.path.isdir(full) and not urlsplit(self.path).path.endswith("/"):
            self.send(301, "", {"Location": urlsplit(self.path).path + "/"})
            return True
        return False

    def do_OPTIONS(self):
        if not self.authed():
            return
        self.send(200, "", {"DAV": "1, 2", "Allow": "OPTIONS, GET, HEAD, PUT, DELETE, "
                            "MKCOL, MOVE, COPY, PROPFIND"})

    def prop(self, full):
        st = os.stat(full)
        d = "d:" if not A.nsdefault else ""
        isdir = os.path.isdir(full)
        out = ["<%sresponse><%shref>%s</%shref><%spropstat><%sprop>" %
               (d, d, html.escape(self.href(full)), d, d, d)]
        out.append("<%sresourcetype>%s</%sresourcetype>" %
                   (d, "<%scollection/>" % d if isdir else "", d))
        if not isdir:
            out.append("<%sgetcontentlength>%d</%sgetcontentlength>" % (d, st.st_size, d))
            out.append("<%sgetcontenttype>application/octet-stream</%sgetcontenttype>" % (d, d))
        out.append("<%sgetlastmodified>%s</%sgetlastmodified>" %
                   (d, formatdate(st.st_mtime, usegmt=True), d))
        out.append("<%sgetetag>%s</%sgetetag>" % (d, html.escape(etag(st)), d))
        out.append("</%sprop><%sstatus>HTTP/1.1 200 OK</%sstatus></%spropstat>" % (d, d, d, d))
        out.append("<%spropstat><%sprop><x:quota xmlns:x=\"urn:other\"/></%sprop>"
                   "<%sstatus>HTTP/1.1 404 Not Found</%sstatus></%spropstat>" % (d, d, d, d, d, d))
        out.append("</%sresponse>" % d)
        return "".join(out)

    def do_PROPFIND(self):
        if not self.authed():
            return
        self.body()
        full = self.local()
        if full is None or not os.path.exists(full):
            return self.send(404, "not found")
        if self.slashfix(full):
            return
        depth = self.headers.get("Depth", "infinity")
        items = [full]
        if depth == "1" and os.path.isdir(full):
            items += [os.path.join(full, n) for n in sorted(os.listdir(full))]
        if A.nsdefault:
            head = '<?xml version="1.0"?>\n<!-- a comment --><multistatus xmlns="DAV:">'
            tail = "</multistatus>"
        else:
            head = '<?xml version="1.0" encoding="utf-8"?>\n<d:multistatus xmlns:d="DAV:">'
            tail = "</d:multistatus>"
        evil = ""
        if A.evil and depth == "1":
            d = "d:" if not A.nsdefault else ""
            for h in ("..%2F..%2Fescaped.txt", "..", "%2E%2E", "a%2Fb"):
                evil += ("<%sresponse><%shref>%s%s</%shref><%spropstat><%sprop>"
                         "<%sresourcetype/><%sgetcontentlength>3</%sgetcontentlength>"
                         "</%sprop><%sstatus>HTTP/1.1 200 OK</%sstatus></%spropstat>"
                         "</%sresponse>" % (d, d, self.href(full), h, d, d, d, d, d, d,
                                            d, d, d, d, d))
        self.send(207, head + "".join(self.prop(i) for i in items) + evil + tail,
                  ctype='application/xml; charset="utf-8"')

    def do_GET(self):
        if not self.authed():
            return
        full = self.local()
        if full is None or not os.path.exists(full):
            return self.send(404, "not found")
        if self.slashfix(full):
            return
        if os.path.isdir(full):
            return self.send(200, "a folder", ctype="text/html")
        with open(full, "rb") as f:
            data = f.read()
        self.send(200, data, {"ETag": etag(os.stat(full)),
                              "Last-Modified": formatdate(os.stat(full).st_mtime, usegmt=True)},
                  ctype="application/octet-stream")

    def do_HEAD(self):
        self.do_GET()

    def do_PUT(self):
        if not self.authed():
            return
        data = self.body()
        full = self.local()
        if full is None:
            return self.send(403, "no")
        if not os.path.isdir(os.path.dirname(full)):
            return self.send(409, "no parent")
        exists = os.path.exists(full)
        im = self.headers.get("If-Match")
        if im and (not exists or im != etag(os.stat(full))):
            return self.send(412, "changed")
        if self.headers.get("If-None-Match") == "*" and exists:
            return self.send(412, "exists")
        with open(full, "wb") as f:
            f.write(data)
        self.send(204 if exists else 201, "", {"ETag": etag(os.stat(full))} if not A.noetag else {})

    def do_DELETE(self):
        if not self.authed():
            return
        full = self.local()
        if full is None or full == A.root:
            return self.send(403, "no")
        if not os.path.exists(full):
            return self.send(404, "not found")
        if os.path.isdir(full):
            shutil.rmtree(full)
        else:
            os.unlink(full)
        self.send(204)

    def do_MKCOL(self):
        if not self.authed():
            return
        self.body()
        full = self.local()
        if full is None:
            return self.send(403, "no")
        if os.path.exists(full):
            return self.send(405, "exists")
        if not os.path.isdir(os.path.dirname(full.rstrip("/"))):
            return self.send(409, "no parent")
        os.mkdir(full)
        self.send(201)

    def movecopy(self, move):
        if not self.authed():
            return
        self.body()
        src = self.local()
        dst = self.local(self.headers.get("Destination", ""))
        if src is None or dst is None or not os.path.exists(src):
            return self.send(404 if src and not os.path.exists(src) else 403, "no")
        over = self.headers.get("Overwrite", "T") != "F"
        existed = os.path.exists(dst)
        if existed and not over:
            return self.send(412, "exists")
        if not os.path.isdir(os.path.dirname(dst.rstrip("/"))):
            return self.send(409, "no parent")
        if existed:
            shutil.rmtree(dst) if os.path.isdir(dst) else os.unlink(dst)
        if move:
            os.rename(src, dst)
        elif os.path.isdir(src):
            shutil.copytree(src, dst)
        else:
            shutil.copy2(src, dst)
        self.send(204 if existed else 201)

    def do_MOVE(self):
        self.movecopy(True)

    def do_COPY(self):
        self.movecopy(False)


def main():
    global A
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", required=True)
    ap.add_argument("--port", type=int, default=0)
    ap.add_argument("--auth", default="none", choices=["none", "basic", "digest"])
    ap.add_argument("--user", default="u")
    ap.add_argument("--password", default="p")
    ap.add_argument("--prefix", default="/")
    ap.add_argument("--tls", nargs=2, metavar=("CERT", "KEY"))
    ap.add_argument("--chunked", action="store_true")
    ap.add_argument("--close", action="store_true")
    ap.add_argument("--nsdefault", action="store_true")
    ap.add_argument("--absolute", action="store_true")
    ap.add_argument("--noetag", action="store_true")
    ap.add_argument("--idle", type=float, default=0)
    ap.add_argument("--stale", type=int, default=0)
    ap.add_argument("--evil", action="store_true")
    ap.add_argument("--log")
    A = ap.parse_args()
    A.root = os.path.realpath(A.root)
    if not A.prefix.endswith("/"):
        A.prefix += "/"
    srv = ThreadingHTTPServer(("127.0.0.1", A.port), H)
    srv.daemon_threads = True
    if A.tls:
        ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        ctx.load_cert_chain(A.tls[0], A.tls[1])
        srv.socket = ctx.wrap_socket(srv.socket, server_side=True)
    print("port %d" % srv.server_address[1], flush=True)
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
