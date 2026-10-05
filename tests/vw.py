#!/usr/bin/env python3
"""Drive the vw command against tests/bwserve.py, a stand-in server.

    python3 tests/vw.py [path-to-hibr]

Log in, sync and read the vault; refuse a wrong password; unlock again
with the server gone, from the vault kept here; a PIN; lock and the
timeout; and that nothing decrypted is left on disk. Every run has a
config, data and runtime folder of its own. Needs Python's cryptography
package for the stand-in server; skips, saying so, without it.
"""
import base64, hashlib, hmac, json, os, shutil, struct, subprocess, sys, tempfile, time, urllib.request

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import screen as sx
from screen import check, report, tree

try:
    import cryptography  # noqa: F401
except ImportError:
    print("vw: no cryptography package here for the stand-in server, nothing driven")
    print("0 passed, 0 failed")
    sys.exit(0)

if len(sys.argv) > 1:
    sx.HIBR = os.path.abspath(sys.argv[1])
HERE = os.path.dirname(os.path.abspath(__file__))
D = tempfile.mkdtemp(prefix="hibr-vw-")
PW = "correct horse battery staple"
ENV = dict(os.environ, XDG_CONFIG_HOME=os.path.join(D, "c"), XDG_DATA_HOME=os.path.join(D, "d"),
           XDG_RUNTIME_DIR=os.path.join(D, "r"), VW_STDIN="1",
           HIBR_MODPATH=os.environ.get("HIBR_TESTMODS") or tree("build/mods"))
VW = tree("examples/vw.hibr")


def vw(*args, stdin="", env=None):
    r = subprocess.run([sx.HIBR, VW] + list(args), input=stdin, capture_output=True, text=True,
                       env=dict(ENV, **(env or {})), timeout=60)
    return r.stdout.strip(), r.returncode, r.stderr.strip()


def server():
    p = subprocess.Popen([sys.executable, os.path.join(HERE, "bwserve.py")], stdout=subprocess.PIPE, text=True)
    return p, int(p.stdout.readline().split()[1])


def stats(port):
    return json.load(urllib.request.urlopen("http://127.0.0.1:%d/stats" % port))


def totp(secret, t):
    k = base64.b32decode(secret)
    m = hmac.new(k, struct.pack(">Q", int(t) // 30), hashlib.sha1).digest()
    o = m[19] & 15
    return "%06d" % ((struct.unpack(">I", m[o:o + 4])[0] & 0x7fffffff) % 1000000)


srv, port = server()
try:
    out, rc, err = vw("server", "http://127.0.0.1:%d" % port)
    check("vw server keeps where the vault is", rc == 0, err)
    out, rc, err = vw("login", "pat@example.com", stdin="wrong password\n")
    check("a wrong master password is refused, saying why",
          rc != 0 and "incorrect" in err and stats(port)["token"] == 0, (out, err))
    out, rc, err = vw("login", "pat@example.com", stdin=PW + "\n")
    sess = out.split("=", 1)[1] if out.startswith("export VW_SESSION=") else ""
    check("vw login logs in, syncs and hands back a session to export",
          rc == 0 and sess and stats(port)["token"] == 1 and stats(port)["sync"] == 1, (out, err))
    S = {"VW_SESSION": sess}
    out, rc, err = vw("list", env=S)
    rows = [(l.split("\t") + [""])[:3] for l in out.split("\n") if l]
    check("vw list gives each item's id, name and user",
          [r[1:] for r in rows] == [["Example Mail", "pat"], ["Bank", "pat.doe"], ["Wifi", ""]], (out, err))
    out, rc, err = vw("get", "password", "mail", env=S)
    check("vw get password reads it, accents and all", out == "s3crét-mail", (out, err))
    out, rc, err = vw("get", "notes", "Bank", env=S)
    check("and an item behind a key of its own opens too", out == "the one with the card", (out, err))
    t = time.time()
    out, rc, err = vw("get", "totp", "Example Mail", env=S)
    check("vw get totp makes the code for now", out in (totp("JBSWY3DPEHPK3PXP", t), totp("JBSWY3DPEHPK3PXP", t + 30)),
          (out, err))
    out, rc, err = vw("get", "password", "a", env=S)
    check("a name that matches several asks for more", rc != 0 and "items have a" in err, (out, err))
    out, rc, err = vw("list")
    check("without the session the vault is locked", rc == 3 and "locked" in err, (out, err))
    out, rc, err = vw("list", env={"VW_SESSION": base64.b64encode(os.urandom(64)).decode()})
    check("and with someone else's it stays locked", rc == 3, (out, err))
    out, rc, err = vw("sync", env=S)
    check("vw sync fetches the vault again with the stored login, no password",
          rc == 0 and stats(port)["refresh"] == 1 and stats(port)["sync"] == 2, (out, err, stats(port)))
finally:
    srv.terminate()
    srv.wait()

# The server is gone: everything here works from the vault kept on disk.
out, rc, err = vw("unlock", stdin=PW + "\n")
sess = out.split("=", 1)[1] if out.startswith("export VW_SESSION=") else ""
check("with the server gone, vw unlock opens the vault kept here", rc == 0 and sess, (out, err))
S = {"VW_SESSION": sess}
out, rc, err = vw("get", "username", "bank", env=S)
check("and reads it offline", out == "pat.doe", (out, err))
out, rc, err = vw("unlock", stdin="nope\n")
check("a wrong password offline opens nothing", rc != 0, (out, err))
out, rc, err = vw("pin", "set", stdin="2468\n", env=S)
check("a PIN can be set while unlocked", rc == 0, (out, err))
vw("lock", env=S)
out, rc, err = vw("list", env=S)
check("vw lock locks it, and the old session no longer opens", rc == 3, (out, err))
out, rc, err = vw("status")
check("a PIN not kept across a restart is forgotten on lock", "PIN set" not in out, out)
out, rc, err = vw("unlock", stdin=PW + "\n")
S = {"VW_SESSION": out.split("=", 1)[1]}
vw("pin", "set", "--keep", stdin="2468\n", env=S)
vw("lock", env=S)
out, rc, err = vw("unlock", "--pin", stdin="1111\n")
check("a kept PIN survives a lock, and a wrong one is refused", rc != 0 and "wrong PIN" in err, (out, err))
out, rc, err = vw("unlock", "--pin", stdin="2468\n")
S = {"VW_SESSION": out.split("=", 1)[1] if "=" in out else ""}
out2, rc2, err2 = vw("get", "password", "wifi", env=S)
check("the right PIN unlocks it", rc == 0 and rc2 != 0 and "no password" in err2, (out, err, out2, err2))
vw("timeout", "1")
run_dir = os.path.join(D, "r", "hibr-vw-%d" % os.getuid())
f = os.path.join(run_dir, "session")
exp, w = open(f).read().rstrip("\n").split("\t")
open(f, "w").write("%d\t%s\n" % (int(time.time()) - 5, w))
out, rc, err = vw("list", env=S)
check("a session unused past the timeout locks itself", rc == 3 and "unused" in err, (out, err))
modes = {os.path.relpath(p, D): oct(os.stat(p).st_mode & 0o777)
         for p in (os.path.join(D, "c", "hibr", "vw", "account.json"), os.path.join(D, "d", "hibr", "vw", "vault.json"))}
check("the account and the vault are kept private", set(modes.values()) == {"0o600"}, modes)
leak = [p for root, _, fs in os.walk(D) for p in [os.path.join(root, x) for x in fs]
        if any(s in open(p, "rb").read() for s in (b"s3cr", b"b4nk-p4ss", b"h0me-w1fi", b"refresh-pat"))]
check("nothing decrypted, and not the login's refresh token, is anywhere on disk", not leak, leak)

shutil.rmtree(D, True)
report(22)
