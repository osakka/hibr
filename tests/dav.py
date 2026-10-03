#!/usr/bin/env python3
"""The dav module against tests/davserve.py: a local WebDAV server, run
in each of the shapes a real one comes in -- Digest or Basic, plain or
TLS, chunked replies, a default namespace, absolute hrefs under a base
path, idle connections dropped, nonces gone stale, no ETag on a PUT, and
a hostile server whose names try to step outside a download.
Nothing here reaches the network.
"""
import os, shutil, stat, subprocess, sys, tempfile, time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import screen as sx
from screen import check, report, tree

if len(sys.argv) > 1:
    sx.HIBR = os.path.abspath(sys.argv[1])

D = tempfile.mkdtemp(prefix="hibr-dav-")
ROOT = os.path.join(D, "root")
CONF = os.path.join(D, "cfg", "dav")
WORK = os.path.join(D, "work")
os.makedirs(os.path.join(ROOT, "sub"))
os.makedirs(WORK)
open(os.path.join(ROOT, "a.txt"), "w").write("hello\n")
open(os.path.join(ROOT, "sub", "b c.txt"), "w").write("x y\n")
open(os.path.join(ROOT, "ünï.txt"), "w").write("u\n")
SERVERS = []


def serve(*args):
    p = subprocess.Popen([sys.executable, tree("tests/davserve.py"), "--root", ROOT]
                         + list(args), stdout=subprocess.PIPE,
                         stderr=subprocess.DEVNULL, text=True)
    SERVERS.append(p)
    line = p.stdout.readline()
    return int(line.split()[1])


def hb(script, env=None):
    e = dict(os.environ, HIBR_DAV_CONF=CONF, HIBR_DAV_TIMEOUT="5")
    e.update(env or {})
    r = subprocess.run([sx.HIBR, "-c", "mod load %s\n%s" %
                        (tree("build/mods/dav.so"), script)],
                       env=e, capture_output=True, text=True, timeout=120, cwd=WORK)
    return r.stdout, r.stderr


try:
    p = serve("--auth", "digest", "--chunked", "--nsdefault")
    out, err = hb("dav server set t http://127.0.0.1:%d/ -u u -p p; dav servers" % p)
    mode = stat.S_IMODE(os.stat(CONF).st_mode) if os.path.exists(CONF) else 0
    check("dav server set writes a list only its owner can read, no password shown",
          mode == 0o600 and out == "t\thttp://127.0.0.1:%d/\tu\tchecked\n" % p
          and "\tp\t" in open(CONF).read(), out + err)
    out, err = hb("dav ls dav://t/; echo st $? $DAV_CODE; dav ls dav://t/sub")
    check("ls lists a folder over Digest, chunked, with a default namespace",
          out.split("\n")[0].endswith("\ta.txt") and "\tsub\n" in out and
          out.split("\n")[1].startswith("d\t0\t") and "st 0 207" in out and
          out.rstrip().endswith("\tb c.txt") and "ünï.txt" in out, out + err)
    out, err = hb('r := dav ls dav://t/; echo "${r[0]["name"]} ${r[0]["dir"]} ${r[0]["size"]}";'
                  ' s := dav stat dav://t/sub; echo "${s["name"]} ${s["dir"]}";'
                  ' echo "${#r[@]} ${r[0]["etag"]:0:1}"')
    check("with := ls gives a map per entry and stat one map",
          out.split("\n")[:3] == ["a.txt 0 6", "sub 1", '3 "'], out + err)
    out, err = hb("dav get dav://t/a.txt; cat a.txt; dav get 'dav://t/sub/b c.txt' -;"
                  " dav put a.txt dav://t/sub/ >/dev/null; dav ls dav://t/sub | cut -f4")
    check("get names the file after the remote one, - is standard output; put "
          "into a folder keeps the name",
          out == "hello\nx y\na.txt\nb c.txt\n" and not os.path.exists(
              os.path.join(WORK, "a.txt.part")), out + err)
    out, err = hb("dav mkdir dav://t/n; dav mv dav://t/sub/a.txt dav://t/n/m.txt;"
                  " dav cp dav://t/n dav://t/n2; dav cp dav://t/n dav://t/n2; echo st $? $DAV_CODE;"
                  " dav mkdir dav://t/n; echo mk $? $DAV_CODE; dav rm dav://t/n;"
                  " dav ls dav://t/n2 | cut -f4; dav ls dav://t/ | cut -f4")
    check("mkdir, mv, cp and rm; cp onto something there refuses; mkdir twice says so",
          "st 1 412" in out and "mk 1 405" in out and
          out.split("\n")[2:] == ["m.txt", "a.txt", "n2", "sub", "ünï.txt", ""] and
          "already there" in err, out + err)
    out, err = hb("dav get -r dav://t/sub got; find got -type f | sort;"
                  " dav put -r got dav://t/up; dav ls dav://t/up | cut -f4")
    check("get -r and put -r carry a whole folder",
          out == "got/b c.txt\nb c.txt\n", out + err)
    out, err = hb("s := dav stat dav://t/a.txt;"
                  ' t=${s["etag"]};'
                  " echo changed > work-x; n := dav put -m \"$t\" work-x dav://t/a.txt; echo ok $?;"
                  ' dav put -m "$t" work-x dav://t/a.txt; echo stale $? $DAV_CODE;'
                  " dav put -n work-x dav://t/a.txt; echo none $? $DAV_CODE")
    check("put -m uploads only over the ETag it was given; -n only where nothing is",
          "ok 0" in out and "stale 1 412" in out and "none 1 412" in out and
          "changed on the server" in err and "already there" in err, out + err)
    out, err = hb("dav get dav://t/nope x; echo st $? $DAV_CODE; dav ls dav://zz/; echo z $?")
    check("a missing file and an unknown server each say so",
          "st 1 404" in out and "z 1" in out and "not there (404)" in err and
          "no server called zz" in err and not os.path.exists(os.path.join(WORK, "x")),
          out + err)

    subprocess.run(["openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes",
                    "-keyout", os.path.join(D, "k.pem"), "-out", os.path.join(D, "c.pem"),
                    "-days", "2", "-subj", "/CN=localhost"], capture_output=True)
    p = serve("--auth", "basic", "--tls", os.path.join(D, "c.pem"), os.path.join(D, "k.pem"),
              "--prefix", "/remote.php/dav/files/u", "--absolute", "--idle", "1", "--noetag")
    out, err = hb("dav server set s https://localhost:%d/remote.php/dav/files/u -u u -p p;"
                  " dav ls dav://s/; echo st $?" % p)
    check("an untrusted certificate is refused", "st 1" in out and
          "certificate" in err, out + err)
    open(os.path.join(WORK, "a-local"), "w").write("q\n")
    out, err = hb("dav server set s https://localhost:%d/remote.php/dav/files/u -k;"
                  " dav ls dav://s/ | cut -f4; sleep 1.5; dav ls dav://s/sub | cut -f4;"
                  " e := dav put a-local 'dav://s/sp ace.txt'; echo \"${e:0:1}\"" % p)
    check("-k trusts that server; Basic over TLS under a base path with absolute "
          "hrefs; a dropped idle connection is redialled; an ETag a PUT did "
          "not give is asked for",
          out.split("\n")[0] == "a.txt" and "b c.txt\n" in out and
          out.rstrip().endswith('"') and os.path.exists(os.path.join(ROOT, "sp ace.txt")),
          out + err)
    out, err = hb("dav server set s https://localhost:%d/remote.php/dav/files/u -u u -p no;"
                  " dav ls dav://s/; echo st $? $DAV_CODE; dav test s; echo t $?" % p)
    check("a wrong password is a refused login, and test says so",
          "st 1 401" in out and "t 1" in out and "refused the login" in err, out + err)

    p = serve("--auth", "digest", "--stale", "2", "--close")
    out, err = hb("dav server set d http://127.0.0.1:%d/ -u u -p p;"
                  " for i in 1 2 3 4 5 6; do dav stat dav://d/a.txt >/dev/null || echo fail; done;"
                  " dav test d" % p)
    check("a stale nonce is answered again, and a server that closes every "
          "connection is redialled", out == "ok\n", out + err)

    p = serve("--evil")
    out, err = hb("dav server set e http://127.0.0.1:%d/; dav ls dav://e/sub | cut -f4;"
                  " dav get -r dav://e/sub ev; find ev | sort" % p)
    check("names that would step outside a folder are never listed or written",
          out == "b c.txt\nev\nev/b c.txt\n" and
          not os.path.exists(os.path.join(D, "escaped.txt")) and
          not os.path.exists(os.path.join(WORK, "escaped.txt")), out + err)

    os.chmod(CONF, 0o644)
    out, err = hb("dav ls dav://e/; echo st $?")
    check("a server list others can read is refused", "st 1" in out and
          "chmod 600" in err, out + err)
    os.chmod(CONF, 0o600)
    out, err = hb("dav server rename e evil; dav server rm d; dav servers | cut -f1;"
                  " dav server set x ftp://h/; echo st $?")
    check("servers are renamed and removed; an address that is not http is refused",
          out == "t\ns\nevil\nst 1\n" and "not an http" in err, out + err)
finally:
    for s in SERVERS:
        s.kill()
        s.wait()
    shutil.rmtree(D, True)

report(15)
