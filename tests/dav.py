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
from screen import Term, check, report, tree, load, press, release

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

    # --- the desktop: Files, the server dialog, the Control Panel pane ---
    shutil.rmtree(ROOT)
    os.makedirs(os.path.join(ROOT, "sub"))
    open(os.path.join(ROOT, "a.txt"), "w").write("hello\n")
    open(os.path.join(ROOT, "sub", "b c.txt"), "w").write("x y\n")
    if os.path.exists(CONF):
        os.unlink(CONF)
    p = serve("--auth", "digest")
    hb("dav server set t http://127.0.0.1:%d/ -u u -p p" % p)
    LOCAL = os.path.join(D, "local")
    os.makedirs(LOCAL)
    open(os.path.join(LOCAL, "up.txt"), "w").write("from here\n")
    CACHE = os.path.join(D, "cache")
    OPENED = os.path.join(D, "opened")

    def drun(body, phases, rows=30, cols=100):
        """A desktop with Files and the Control Panel, the session's body,
        then each phase: keys to send (a callable gets the screen and
        gives them), and what to wait for -- text on the screen or a
        callable that is true once it has happened."""
        sess = os.path.join(D, "session.hibr")
        open(sess, "w").write(
            "%s. %s\n. %s\n. %s\n"
            "dt_openfile() { printf '%%s\\n' \"$1\" > %s; }\n"
            "dt_open\n%s\ndt_run\ndt_close\n"
            % (load("console", "dav"), tree("examples/desktop/desktop.hibr"),
               tree("examples/desktop/apps/files.hibr"),
               tree("examples/desktop/apps/panel.hibr"), OPENED, body))
        t = Term(sess, env={"HIBR_DAV_CONF": CONF, "XDG_CACHE_HOME": CACHE},
                 settle=1.0, rows=rows, cols=cols)
        shots = []
        for feed, until in phases:
            if callable(feed):
                feed = feed(t.screen())
            if feed:
                t.keys(list(feed), settle=0.4)
            for _ in range(40):
                sc = t.screen()
                if until is None or (until(sc) if callable(until) else
                                     sc.find(until) is not None):
                    break
                t.collect(0.25)
            shots.append(t.screen())
        t.quit(None, 0.5)
        return shots

    def click(text, dr=0, dc=1):
        def f(sc):
            at = sc.find(text)
            return [press(at[0] + dr, at[1] + dc), release(at[0] + dr, at[1] + dc)] if at else []
        return f

    sc = drun('dt_new "Files" 20 60 2 2 files "dav://t"', [([], "a.txt")])[0]
    check("Files opens a server: its listing, its name in the title, no .. at its root",
          sc.find("sub/") is not None and sc.find("a.txt") is not None and
          sc.find("Files [t:/]") is not None and sc.find("../") is None, sc)
    sc = drun('dt_new "Files" 20 60 2 2 files "dav://t"',
              [([], "a.txt"), ([b"\r"], "b c.txt")])[-1]
    check("enter goes into a folder on the server, with .. to come back",
          sc.find("b c.txt") is not None and sc.find("../") is not None and
          sc.find("Files [t:/sub]") is not None, sc)

    def server(path):
        f = os.path.join(ROOT, path)
        return open(f).read() if os.path.exists(f) else None

    def opened():
        return open(OPENED).read().strip() if os.path.exists(OPENED) else ""

    def edit(text):
        def f(sc):
            with open(opened(), "a") as fh:
                fh.write(text)
            return []
        return f

    def server_edit(sc):
        time.sleep(1.1)
        open(os.path.join(ROOT, "a.txt"), "w").write("their change\n")
        return []

    SAW = {}
    shots = drun('dt_new "Files" 20 60 2 2 files "dav://t"', [
        ([], "a.txt"),
        ([b"\x1b[B", b"\r"], lambda sc: opened() != ""),
        (edit("mine\n"), lambda sc: sc.find("Saved a.txt to t") is not None and
         SAW.update(a=server("a.txt")) is None),
        (server_edit, None),
        (edit("mine again\n"), "changed on the server"),
    ])
    cf = [n for n in os.listdir(ROOT) if "conflict" in n]
    check("opening a remote file fetches a copy and opens that",
          opened().startswith(os.path.join(CACHE, "hibr", "dav", "t")) and
          opened().endswith("/a.txt"), opened())
    check("saving the copy puts it back on the server, and says so",
          shots[2].find("Saved a.txt to t") is not None and
          SAW.get("a") == "hello\nmine\n", repr(SAW) + shots[2].dump())
    check("a copy saved after the server's changed goes beside it, not over it",
          server("a.txt") == "their change\n" and len(cf) == 1 and
          cf[0].startswith("a (conflict ") and cf[0].endswith(").txt") and
          server(cf[0]) == "hello\nmine\nmine again\n" and
          shots[4].find("changed on the server") is not None, shots[4])
    for n in cf:
        os.unlink(os.path.join(ROOT, n))

    shots = drun('dt_new "Files" 12 44 2 2 files "%s"\n'
                 'dt_new "Files" 12 44 2 50 files "dav://t/sub"' % LOCAL, [
        ([], "b c.txt"),
        (click("up.txt"), None),
        ([b"\x1bc"], None),
        (click("b c.txt"), None),
        ([b"\x1bv"], lambda sc: server("sub/up.txt") is not None and
         sc.find("up.txt", ) is not None),
    ])
    check("a local file copied and pasted into a server's folder goes up",
          server("sub/up.txt") == "from here\n" and
          os.path.exists(os.path.join(LOCAL, "up.txt")), shots[-1])
    shots = drun('dt_new "Files" 12 44 2 50 files "dav://t/sub"\n'
                 'dt_new "Files" 12 44 2 2 files "%s"' % LOCAL, [
        ([], "b c.txt"),
        (click("b c.txt"), None),
        ([b"\x1bc"], None),
        (click("up.txt"), None),
        ([b"\x1bv"], lambda sc: os.path.exists(os.path.join(LOCAL, "b c.txt"))),
    ])
    check("and a remote one pasted into a local folder comes down",
          open(os.path.join(LOCAL, "b c.txt")).read() == "x y\n"
          if os.path.exists(os.path.join(LOCAL, "b c.txt")) else False, shots[-1])

    shots = drun('dt_new "Files" 16 50 2 2 files "dav://t/sub"', [
        ([], "up.txt"),
        (click("up.txt"), None),
        ([b"n", b"\x15"] + [c.encode() for c in "moved.txt"] + [b"\r"],
         lambda sc: server("sub/moved.txt") is not None),
        (click("moved.txt"), None),
        ([b"\x1b[3~"], "cannot be undone"),
        ([b"y"], lambda sc: server("sub/moved.txt") is None),
    ])
    check("rename on a server renames it there", shots[2].find("moved.txt") is not None
          and server("sub/up.txt") is None, shots[2])
    check("delete asks first, then removes it from the server",
          shots[4].find("Delete moved.txt from the server?") is not None and
          server("sub/moved.txt") is None, shots[4])

    shots = drun("dt_davserver", [
        ([c.encode() for c in "nc"] + [b"\t"] +
         [c.encode() for c in "http://127.0.0.1:%d/" % p] + [b"\t", b"u", b"\t", b"p",
                                                             b"\t", b"\t"], None),
        ([b"\r"], "Connected"),
        ([b"\t", b"\r"], lambda sc: "nc\t" in open(CONF).read()),
    ])
    check("Connect to Server tests what is typed before it is saved",
          shots[1].find("Connected") is not None, shots[1])
    line = [l for l in open(CONF).read().split("\n") if l.startswith("nc\t")]
    check("and saves it to the private list",
          line == ["nc\thttp://127.0.0.1:%d/\tu\tp\t" % p] and
          stat.S_IMODE(os.stat(CONF).st_mode) == 0o600, open(CONF).read())
    shots = drun("dt_davserver nc", [
        ([b"\t", b"\t", b"\t", b" ", b"\t", b"\t", b"\r"],
         lambda sc: "nc\t" in open(CONF).read() and "\tk\n" in open(CONF).read()),
    ])
    check("editing a server keeps its password and changes what was changed",
          [l for l in open(CONF).read().split("\n") if l.startswith("nc\t")] ==
          ["nc\thttp://127.0.0.1:%d/\tu\tp\tk" % p], open(CONF).read())
    sc = drun('CP_PANEDIRS+=("%s")\ncp_panes\n'
              'dt_new "Control Panel" 26 76 1 1 panel network'
              % tree("examples/desktop/control-panel"),
              [([], "Upload Edits on Save")])[0]
    check("Control Panel lists the servers and the settings for files on them",
          sc.find("Network Serve") is not None and sc.find("nc") is not None and
          sc.find("Add Server") is not None and sc.find("Timeout (seconds)") is not None
          and sc.find("Clear Cache") is not None, sc)
    out, err = hb("mod load %s; . %s; dav server set far http://localhost:%d/ -u u -p p; dt_davok;"
                  " dt_davbatch copy dav://far/sub dav://t/a.txt; dt_davbatch copy dav://t dav://far/sub"
                  % (tree("build/mods/console.so"), tree("examples/desktop/desktop.hibr"), p),
                  env={"XDG_CACHE_HOME": CACHE})
    check("between two servers a file and a folder go through a copy here, and the "
          "batch says only how it went",
          out == "1\n0\na.txt\n\n0\n1\n\nt already has a sub\n" and
          server("sub/a.txt") == "their change\n", out + err)

finally:
    for s in SERVERS:
        s.kill()
        s.wait()
    shutil.rmtree(D, True)

report(30)
