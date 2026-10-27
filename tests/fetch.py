#!/usr/bin/env python3
"""The fetch module (Gitea #399) against tests/fetchserve.py: the script
verb and the C interface, through redirects, codings, cookies, logins,
timeouts and a cancel. Two servers on two ports are two origins, which is
what a cross-origin redirect needs. Nothing here reaches the network.
"""
import json, os, pty, re, select, shutil, signal, stat, subprocess, sys, tempfile
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import screen as sx
from screen import check, report, tree

if len(sys.argv) > 1:
    sx.HIBR = os.path.abspath(sys.argv[1])

D = tempfile.mkdtemp(prefix="hibr-fetch-")
DATA = os.path.join(D, "data")
SERVERS = []
MODS = os.environ.get("HIBR_TESTMODS") or tree("build/mods")
FETCH = os.path.join(MODS, "fetch.so")


def serve(*args):
    p = subprocess.Popen([sys.executable, tree("tests/fetchserve.py")] + list(args),
                         stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True)
    SERVERS.append(p)
    return int(p.stdout.readline().split()[1])


def hb(script, env=None, timeout=60):
    e = dict(os.environ, XDG_DATA_HOME=DATA, HIBR_FETCH_TIMEOUT="10")
    for k in ("http_proxy", "https_proxy", "HTTP_PROXY", "HTTPS_PROXY", "no_proxy"):
        e.pop(k, None)
    e.update(env or {})
    t0 = time.time()
    r = subprocess.run([sx.HIBR, "-c", "mod load %s\n%s" % (FETCH, script)],
                       env=e, capture_output=True, text=True, timeout=timeout, cwd=D)
    return r.stdout, r.stderr, time.time() - t0


try:
    A = serve()
    B = serve()
    a = "http://127.0.0.1:%d" % A
    b = "http://127.0.0.1:%d" % B

    out, err, _ = hb("fetch get %s/text; echo \"st $? $FETCH_STATUS\"" % a)
    check("fetch prints a body and sets FETCH_STATUS", out == "hello\nst 0 200\n", out + err)
    out, err, _ = hb('r := fetch %s/text; echo "${r["status"]}|${r["type"]}|'
                     '${r["headers"]["content-length"]}|${r["body"]}|${r["url"]}"' % a)
    check("with := the reply is a map: status, type, headers by name, body, url",
          out == "200|text/plain|6|hello\n|%s/text\n" % a, out + err)
    out, err, _ = hb("fetch %s/chunked; echo; fetch %s/close" % (a, a))
    check("a chunked body and one ended by closing the connection arrive whole",
          out == "chunk one, chunk two\nuntil the end of the stream", out + err)
    want = "".join("compressed line %d\n" % i for i in range(200))
    out, err, _ = hb("fetch %s/gzip; fetch %s/deflate; fetch %s/rawdeflate" % (a, a, a))
    check("gzip, zlib deflate and raw deflate bodies are offered and decoded",
          out == want * 3, (len(out), err))
    out, err, _ = hb("fetch %s/plain; echo \" $FETCH_STATUS\"; fetch -i %s/plain" % (a, a))
    check("compression is offered by default, and -i asks for the body as it is",
          out.startswith("asked for compression 406") and out.endswith("plain as it came"),
          out + err)
    out, err, _ = hb('c := fetch %s/count; fetch %s/text >/dev/null; '
                     'fetch %s/text >/dev/null; d := fetch %s/count; '
                     'echo "${c["body"]}|${d["body"]}"' % (a, a, a, a))
    c0, c1 = out.strip().split("|")
    check("one process keeps its connection: four requests, one new connection",
          int(c1.split()[1]) - int(c0.split()[1]) == 0 and
          int(c1.split()[0]) - int(c0.split()[0]) == 3, out + err)

    # Redirects: a relative Location with dot segments, 303 after a POST,
    # 307 keeping it, and the redirect handed back with -n.
    out, err, _ = hb('r := fetch "%s/redir?code=302&to=x/../text"; '
                     'echo "${r["body"]}|${r["redirects"]}|${r["url"]}"' % a)
    check("a relative redirect is resolved, dot segments and all",
          out == "hello\n|1|%s/text\n" % a, out + err)
    out, err, _ = hb('r := fetch post "%s/redir?code=303&to=/echo" -d data; echo "${r["body"]}"' % a)
    e = json.loads(out) if out.startswith("{") else {}
    check("a 303 after a POST is a GET with no body",
          e.get("method") == "GET" and e.get("body") == "", out + err)
    out, err, _ = hb('r := fetch post "%s/redir?code=307&to=/echo" -d data; echo "${r["body"]}"' % a)
    e = json.loads(out) if out.startswith("{") else {}
    check("a 307 sends the POST and its body again",
          e.get("method") == "POST" and e.get("body") == "data", out + err)
    out, err, _ = hb('r := fetch put "%s/redir?code=301&to=/echo" -d keep; echo "${r["body"]}"; '
                     'r := fetch post "%s/redir?code=301&to=/echo" -d gone; echo "${r["body"]}"' % (a, a))
    e = [json.loads(x) for x in out.split("\n") if x.startswith("{")]
    check("a 301 keeps any method but POST, and its body; a POST becomes a GET",
          len(e) == 2 and (e[0].get("method"), e[0].get("body")) == ("PUT", "keep") and
          (e[1].get("method"), e[1].get("body")) == ("GET", ""), out + err)
    out, err, _ = hb('r := fetch -n "%s/redir?code=302&to=/text"; '
                     'echo "${r["status"]}|${r["headers"]["location"]}"' % a)
    check("-n hands a redirect back rather than following it", out == "302|/text\n", out + err)
    pf = os.path.join(D, "body.txt")
    open(pf, "w").write("from a file")
    out, err, _ = hb('r := fetch put "%s/redir?code=308&to=/echo" -f %s; echo "${r["body"]}"' % (a, pf))
    e = json.loads(out) if out.startswith("{") else {}
    check("a 308 sends a file's body again, from where it started",
          e.get("method") == "PUT" and e.get("body") == "from a file", out + err)

    # Across origins: the caller's Authorization, the login and the jar's
    # cookies stay behind; within one origin they follow.
    # A jar's cookies go by host, not origin (RFC 6265), and the two servers
    # share a host; what must not cross is what the caller wrote itself.
    hb('fetch -j x "%s/setck?c=sid=7;%%20Path=/" >/dev/null' % a)
    out, err, _ = hb('r := fetch -u u:p -H "Authorization: Bearer t" -H "Cookie: mine=1" '
                     '"%s/redir?code=302&to=%s/echo"; echo "${r["body"]}"' % (a, b))
    e = json.loads(out) if out.startswith("{") else {}
    check("a redirect to another origin carries neither the caller's Authorization "
          "nor its Cookie",
          e.get("path") == "/echo" and "authorization" not in e.get("headers", {})
          and "cookie" not in e.get("headers", {}), out + err)
    out, err, _ = hb('r := fetch -j x -H "Authorization: Bearer t" '
                     '"%s/redir?code=302&to=/echo"; echo "${r["body"]}"' % a)
    e = json.loads(out) if out.startswith("{") else {}
    check("and one within the origin carries both",
          e.get("headers", {}).get("authorization") == "Bearer t" and
          "sid=7" in e.get("headers", {}).get("cookie", ""), out + err)
    out, err, _ = hb('r := fetch -H "Host: evil" -H "Content-Length: 99" -H "User-Agent: mine" '
                     '%s/echo; echo "${r["body"]}"' % a)
    e = json.loads(out) if out.startswith("{") else {}
    h = e.get("headers", {})
    check("Host and Content-Length are fetch's own; a User-Agent given replaces fetch's",
          h.get("host") == "127.0.0.1:%d" % A and "content-length" not in h and
          h.get("user-agent") == "mine", out + err)
    out, err, _ = hb('r := fetch %s/echo; echo "${r["body"]}"' % a)
    e = json.loads(out) if out.startswith("{") else {}
    check("and its own says it is hibr's",
          e.get("headers", {}).get("user-agent", "").startswith("hibr-fetch/"), out + err)

    # Cookies, as RFC 6265 says, in a jar that is a file of its own.
    J = 'fetch -j c "%s/setck?c=%s" >/dev/null'
    out, err, _ = hb(";".join([
        J % (a, "p=1;%20Path=/ck"), J % (a, "bad=1;%20Domain=example.com"),
        J % (a, "sec=1;%20Secure"), J % (a, "gone=1"),
        J % (a, "gone=;%20Expires=Thu,%2001%20Jan%201970%2000:00:00%20GMT"),
        J % (a, "old=1;%20Max-Age=0"), J % (a, "keep=2"),
        'fetch -j c %s/ck; echo; fetch -j c %s/text >/dev/null; fetch -j c %s/echo' % (a, a, a)]))
    first = out.split("\n")[0]
    e = json.loads(out.split("\n", 1)[1]) if "\n{" in out else {}
    check("a cookie goes to its path only; another domain's, a Secure one over "
          "http and an expired one are not kept",
          sorted(first.split("; ")) == ["keep=2", "p=1"] and
          e.get("headers", {}).get("cookie") == "keep=2", out + err)
    jf = os.path.join(DATA, "hibr", "cookies", "c.json")
    mode = stat.S_IMODE(os.stat(jf).st_mode) if os.path.exists(jf) else 0
    dmode = stat.S_IMODE(os.stat(os.path.dirname(jf)).st_mode) if os.path.exists(jf) else 0
    jar = json.load(open(jf)) if os.path.exists(jf) else {}
    check("the jar is JSON its owner alone can read, in a folder only they can enter",
          mode == 0o600 and dmode == 0o700 and
          sorted(c["name"] for c in jar.get("cookies", [])) == ["keep", "p"], (mode, dmode, jar))
    out, err, _ = hb('fetch -j c %s/ck/x; echo; fetch jar clear c; fetch -j c %s/ck' % (a, a))
    check("another run reads the same jar, and fetch jar clear empties it",
          out.split("\n")[0] in ("p=1; keep=2", "keep=2; p=1") and
          out.endswith("(none)") and not os.path.exists(jf), out + err)
    out, err, _ = hb('fetch -j "../x" %s/text; echo "rc $?"' % a)
    check("a jar's name cannot be a path", "rc 1" in out and "not a cookie jar" in err, out + err)

    # Logins: Basic and Digest, refused with the wrong password.
    out, err, _ = hb('fetch -u u:p %s/basic; echo " $?"; fetch -u u:x %s/basic; echo " $?"; '
                     'fetch -u u:p %s/digest; echo " $?"; fetch -u u:x %s/digest; echo " $?"'
                     % (a, a, a, a))
    check("Basic and Digest log in with the right password and not with a wrong one",
          out == "welcome 0\nwho are you 1\nwelcome 0\nwho are you 1\n", out + err)

    # A body to a file, streamed, and a server that stalls.
    big = os.path.join(D, "big.bin")
    out, err, _ = hb('r := fetch %s/big?n=3000000 -o %s; echo "${r["length"]}|${#r["body"]}"' % (a, big))
    check("-o writes the body to a file as it arrives, and keeps it out of the map",
          out == "3000000|0\n" and os.path.getsize(big) == 3000000, out + err)
    out, err, took = hb('fetch -t 1 "%s/stall?s=6"; echo "rc $?"' % a)
    check("a server that sends nothing for the timeout is given up on",
          "rc 1" in out and "sent nothing for 1 seconds" in err and took < 4, (out, err, took))
    out, err, _ = hb("fetch http://127.0.0.1:1/x; echo \"rc $?\"")
    check("an address nothing listens on says so", "rc 1" in out and "cannot reach" in err,
          out + err)

    # HTTPS: refused with a certificate nobody vouched for, taken with -k.
    subprocess.run(["openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes",
                    "-keyout", os.path.join(D, "k.pem"), "-out", os.path.join(D, "c.pem"),
                    "-days", "2", "-subj", "/CN=localhost"], capture_output=True)
    S = serve("--tls", os.path.join(D, "c.pem"), os.path.join(D, "k.pem"))
    out, err, _ = hb('fetch https://localhost:%d/text; echo "rc $?"; '
                     'fetch -k https://localhost:%d/text' % (S, S))
    check("https checks the certificate, and -k takes one nobody vouched for",
          out.startswith("rc 1") and out.endswith("hello\n") and "certificate" in err,
          out + err)

    # The C interface, as a module asks for it: streamed, then cancelled.
    so = os.path.join(D, "fqtest.so")
    cc = os.environ.get("CC", "tcc")
    subprocess.run([cc, "-I" + tree("include"), "-shared", "-fPIC", "-o", so,
                    tree("tests/fetchapi.c")], capture_output=True)
    out, err, _ = hb("mod load %s; fqtest %s/big?n=500000" % (so, a))
    check("a module asks for the interface and is handed the body as it comes",
          out == "200 application/octet-stream 500000 1 -\n", out + err)
    out, err, took = hb('mod load %s; fqtest "%s/stall?s=8" 400' % (so, a))
    check("and a cancel set while it waits stops it within a quarter second or so",
          out.startswith("0 - 0 0 cancelled") and took < 2.5, (out, err, took))

    # An interactive shell's ctrl-c stops a request, not the shell.
    pid, fd = pty.fork()
    if pid == 0:
        os.environ.update(XDG_DATA_HOME=DATA, HIBR_FETCH_TIMEOUT="30", HOME=D,
                          HIBR_MODPATH=MODS)
        os.execv(sx.HIBR, [sx.HIBR, "-i"])
    buf = b""
    # The interrupt is sent once the server has the request, not after a
    # pause: under a loaded gate the line editor may still be echoing what
    # was typed, and a ctrl-c then cancels the line rather than the fetch.
    import urllib.request
    def served():
        return int(urllib.request.urlopen(a + "/count", timeout=5).read().split()[0])
    n0 = served()
    os.write(fd, ("need fetch; fetch '%s/stall?s=15'; echo DONE $?\n" % a).encode())
    t0 = time.time()
    while time.time() - t0 < 20 and served() < n0 + 2:
        r, _, _ = select.select([fd], [], [], 0.1)
        if r:
            buf += os.read(fd, 4096)
    time.sleep(0.3)
    t0 = time.time()
    os.write(fd, b"\x03")
    # The typed line's own echo holds "DONE $?"; the answer is a digit.
    while time.time() - t0 < 5 and not re.search(rb"DONE \d", buf):
        r, _, _ = select.select([fd], [], [], 0.2)
        if r:
            try:
                buf += os.read(fd, 4096)
            except OSError:
                break
    took = time.time() - t0
    os.write(fd, b"exit\n")
    time.sleep(0.2)
    try:
        os.kill(pid, signal.SIGKILL)
    except OSError:
        pass
    os.waitpid(pid, 0)
    check("ctrl-c at the prompt stops a fetch waiting on a slow server",
          b"DONE 1" in buf and b"cancelled" in buf and took < 2, (took, buf[-300:]))
finally:
    for p in SERVERS:
        p.kill()
        p.wait()
    shutil.rmtree(D, True)

report(28)
