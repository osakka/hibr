#!/usr/bin/env python3
"""The stand-in web server for tests/fetch.py: each path answers one
question about a client, and refuses the wrong answer rather than taking
whatever arrives -- a stand-in that accepts everything proves nothing.

  /text            hello, with a Content-Length
  /chunked         a chunked body
  /close           a body ended by closing the connection
  /gzip /deflate /rawdeflate
                   a compressed body, refused (406) to a client that did
                   not offer that coding
  /plain           refused (406) to a client that offered any compression
  /echo            the request as JSON: method, path, headers, body
  /redir?code=N&to=URL
                   a redirect
  /setck?c=...     each c a Set-Cookie line
  /ck              the Cookie header that arrived, or "(none)"
  /stall?s=N       nothing for N seconds, then "late"
  /basic /digest   a login, user u and password p, refused otherwise
  /big?n=N         N bytes of a pattern
  /count           how many requests this server has had, and connections

Prints "port N" once it is listening.
"""
import argparse, gzip, hashlib, json, secrets, socket, ssl, sys, threading
import time, zlib
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlsplit

LOCK = threading.Lock()
NONCES = {}
COUNT = {"requests": 0, "connections": 0}


class H(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, *a):
        pass

    def setup(self):
        with LOCK:
            COUNT["connections"] += 1
        super().setup()

    def reply(self, code, body=b"", ctype="text/plain", extra=()):
        if isinstance(body, str):
            body = body.encode()
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        for k, v in extra:
            self.send_header(k, v)
        self.end_headers()
        if self.command != "HEAD":
            self.wfile.write(body)

    def body(self):
        n = int(self.headers.get("Content-Length") or 0)
        return self.rfile.read(n) if n else b""

    def offered(self, coding):
        ae = self.headers.get("Accept-Encoding", "")
        return any(p.strip().split(";")[0] == coding for p in ae.split(","))

    def authed(self, kind):
        h = self.headers.get("Authorization", "")
        if kind == "basic" and h.startswith("Basic "):
            import base64
            try:
                u, p = base64.b64decode(h[6:]).decode().split(":", 1)
            except Exception:
                u = p = None
            if (u, p) == ("u", "p"):
                return True
        if kind == "digest" and h.startswith("Digest "):
            f = {}
            for part in h[7:].split(","):
                if "=" in part:
                    k, v = part.strip().split("=", 1)
                    f[k] = v.strip('"')
            ha1 = hashlib.md5(b"u:hibr:p").hexdigest()
            ha2 = hashlib.md5(("%s:%s" % (self.command, f.get("uri", ""))).encode()).hexdigest()
            want = hashlib.md5(("%s:%s:%s:%s:%s:%s" % (
                ha1, f.get("nonce"), f.get("nc"), f.get("cnonce"), f.get("qop"),
                ha2)).encode()).hexdigest()
            with LOCK:
                known = f.get("nonce") in NONCES
            if f.get("username") == "u" and f.get("response") == want and known:
                return True
        if kind == "basic":
            hv = 'Basic realm="hibr"'
        else:
            n = secrets.token_hex(8)
            with LOCK:
                NONCES[n] = 0
            hv = 'Digest realm="hibr", qop="auth", algorithm=MD5, nonce="%s", opaque="x1"' % n
        self.reply(401, "who are you", extra=[("WWW-Authenticate", hv)])
        return False

    def handle_one(self):
        with LOCK:
            COUNT["requests"] += 1
        u = urlsplit(self.path)
        q = parse_qs(u.query, keep_blank_values=True)
        p = u.path
        sent = self.body()
        if p == "/text":
            return self.reply(200, "hello\n")
        if p == "/chunked":
            self.send_response(200)
            self.send_header("Content-Type", "text/plain")
            self.send_header("Transfer-Encoding", "chunked")
            self.end_headers()
            for part in (b"chunk one, ", b"chunk two", b""):
                self.wfile.write(b"%x\r\n%s\r\n" % (len(part), part))
            return
        if p == "/close":
            self.send_response(200)
            self.send_header("Content-Type", "text/plain")
            self.send_header("Connection", "close")
            self.end_headers()
            self.wfile.write(b"until the end of the stream")
            self.close_connection = True
            return
        if p in ("/gzip", "/deflate", "/rawdeflate"):
            coding = "gzip" if p == "/gzip" else "deflate"
            if not self.offered(coding):
                return self.reply(406, "offer %s" % coding)
            raw = ("compressed line %d\n" % i for i in range(200))
            raw = "".join(raw).encode()
            if p == "/gzip":
                z = gzip.compress(raw)
            elif p == "/deflate":
                z = zlib.compress(raw)
            else:
                c = zlib.compressobj(wbits=-15)
                z = c.compress(raw) + c.flush()
            return self.reply(200, z, extra=[("Content-Encoding", coding)])
        if p == "/plain":
            ae = self.headers.get("Accept-Encoding", "identity")
            if any(x in ae for x in ("gzip", "deflate", "br")):
                return self.reply(406, "asked for compression")
            return self.reply(200, "plain as it came")
        if p == "/echo":
            return self.reply(200, json.dumps({
                "method": self.command, "path": self.path,
                "headers": {k.lower(): v for k, v in self.headers.items()},
                "body": sent.decode("latin-1")}), "application/json")
        if p == "/redir":
            code = int(q.get("code", ["302"])[0])
            return self.reply(code, "moved", extra=[("Location", q["to"][0])])
        if p == "/setck":
            return self.reply(200, "set", extra=[("Set-Cookie", c) for c in q.get("c", [])])
        if p.startswith("/ck"):
            return self.reply(200, self.headers.get("Cookie", "(none)"))
        if p == "/stall":
            time.sleep(float(q.get("s", ["5"])[0]))
            return self.reply(200, "late")
        if p in ("/basic", "/digest"):
            if self.authed(p[1:]):
                return self.reply(200, "welcome")
            return
        if p == "/big":
            n = int(q.get("n", ["1048576"])[0])
            pat = b"0123456789abcdef"
            return self.reply(200, (pat * (n // 16 + 1))[:n], "application/octet-stream")
        if p == "/count":
            with LOCK:
                return self.reply(200, "%d %d" % (COUNT["requests"], COUNT["connections"]))
        return self.reply(404, "no such thing")

    do_GET = do_POST = do_PUT = do_DELETE = do_HEAD = do_PATCH = handle_one


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--tls", nargs=2, metavar=("CERT", "KEY"))
    A = ap.parse_args()
    srv = ThreadingHTTPServer(("127.0.0.1", 0), H)
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
