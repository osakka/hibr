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


def server(*extra):
    p = subprocess.Popen([sys.executable, os.path.join(HERE, "bwserve.py")] + list(extra),
                         stdout=subprocess.PIPE, text=True)
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

# --- two-factor (Gitea #156) ------------------------------------------------
#
# The protocol here is read from Vaultwarden's source rather than remembered,
# and the stand-in answers with its exact strings and status codes: a 400
# carrying TwoFactorProviders means "ask", a 401 means "that code was wrong",
# and a 400 *without* that key is a wrong password. A client that tells those
# apart by the status alone locks somebody out the first time their password
# is mistyped.
srv2, p2 = server("--2fa", "0")
try:
    D2 = tempfile.mkdtemp(prefix="hibr-vw2-")
    E2 = {"XDG_CONFIG_HOME": os.path.join(D2, "c"), "XDG_DATA_HOME": os.path.join(D2, "d"),
          "XDG_RUNTIME_DIR": os.path.join(D2, "r")}
    vw("server", "http://127.0.0.1:%d" % p2, env=E2)

    out, rc, err = vw("login", "pat@example.com", stdin=PW + "\n", env=E2)
    check("a login with two-factor on, and no code, says which provider it wants",
          rc != 0 and "authenticator" in err.lower(), (out, err))

    out, rc, err = vw("login", "pat@example.com", stdin=PW + "\n000000\n", env=E2)
    check("a wrong code fails with the server's own words",
          rc != 0 and "Two-step token is invalid" in err, (out, err))

    out, rc, err = vw("login", "pat@example.com", stdin=PW + "\n123456\n", env=E2)
    check("the code on the second line logs in", rc == 0 and "unlocked" in err, (out, err))

    acct = json.load(open(os.path.join(D2, "c", "hibr", "vw", "account.json")))
    check("the server's remember token is kept, and not in the clear",
          acct.get("remember", "").startswith("2.") and "remember-pat" not in open(
              os.path.join(D2, "c", "hibr", "vw", "account.json")).read(), acct.get("remember", "none"))

    before = stats(p2)["twofa"]
    out, rc, err = vw("login", "pat@example.com", stdin=PW + "\n", env=E2)
    check("the next login is not asked again, because the device is remembered",
          rc == 0 and stats(p2)["twofa"] == before, (out, err, before, stats(p2)["twofa"]))

    out, rc, err = vw("login", "pat@example.com", stdin="wrong password\n", env=E2)
    kept = json.load(open(os.path.join(D2, "c", "hibr", "vw", "account.json"))).get("remember", "")
    check("a wrong password is not mistaken for a dead token, and keeps it",
          rc != 0 and "incorrect" in err and kept, (err, kept))

    # A remember token the server stops honouring: it answers with the
    # providers again rather than 401, and the client has to ask rather than
    # die -- Vaultwarden drops them on a password change, so this is the
    # path that otherwise locks somebody out a week later.
    a = json.load(open(os.path.join(D2, "c", "hibr", "vw", "account.json")))
    a["remember"] = a["refresh"]
    json.dump(a, open(os.path.join(D2, "c", "hibr", "vw", "account.json"), "w"))
    out, rc, err = vw("login", "pat@example.com", stdin=PW + "\n123456\n", env=E2)
    check("a remember token the server has forgotten falls back to asking",
          rc == 0 and "unlocked" in err, (out, err))
finally:
    srv2.kill()

# A provider this client cannot do is named rather than half-tried.
# Provider 1 is the emailed code, and it is the only one that makes a
# request of its own before the code is typed: the server mails it when
# asked, and that ask carries the master password hash. Nothing drove this
# provider until Gitea #185, which is why the client could send a
# form-encoded hash in a JSON body for releases -- the stand-in replied 200
# to anything, and no check ever reached the endpoint at all.
srv4, p4 = server("--2fa", "1")
try:
    D4 = tempfile.mkdtemp(prefix="hibr-vw4-")
    E4 = {"XDG_CONFIG_HOME": os.path.join(D4, "c"), "XDG_DATA_HOME": os.path.join(D4, "d"),
          "XDG_RUNTIME_DIR": os.path.join(D4, "r")}
    vw("server", "http://127.0.0.1:%d" % p4, env=E4)

    before = stats(p4)["mailed"]
    out, rc, err = vw("login", "pat@example.com", stdin=PW + "\n", env=E4)
    check("an emailed code is asked for by name, and the server is told to send it",
          rc != 0 and "email" in err.lower() and stats(p4)["mailed"] == before + 1,
          (err, before, stats(p4)["mailed"]))

    # The code the server actually mailed, read the way a person reads their
    # inbox -- not a constant. A constant is what let the client mail a
    # *second* code and then submit the first for as long as provider 1 has
    # existed: the real server had already replaced it, so every attempt was
    # a 401, the dialog asked again, each attempt mailed again, and
    # Vaultwarden ended it with too many failures (Gitea #206).
    code = stats(p4)["code"]
    mailed = stats(p4)["mailed"]
    out, rc, err = vw("login", "pat@example.com", stdin=PW + "\n%s\n" % code, env=E4)
    check("and the code the server mailed logs in", rc == 0 and "unlocked" in err,
          (out, err))
    check("without mailing another one, which would have killed the code in hand",
          stats(p4)["mailed"] == mailed, (mailed, stats(p4)["mailed"]))

    # The terminal path -- mail first, then prompt -- is deliberately not
    # checked here and cannot be from this harness: without VW_STDIN the
    # *password* is read from a tty too, so a run with it unset dies at
    # "the password could not be read" long before two factor. What pins
    # the behaviour that matters is the pair above: a mail when there is no
    # code to use, and no mail when there is one. The terminal branch is
    # the order that was already there.
finally:
    srv4.kill()

srv3, p3 = server("--2fa", "7")
try:
    D3 = tempfile.mkdtemp(prefix="hibr-vw3-")
    E3 = {"XDG_CONFIG_HOME": os.path.join(D3, "c"), "XDG_DATA_HOME": os.path.join(D3, "d"),
          "XDG_RUNTIME_DIR": os.path.join(D3, "r")}
    vw("server", "http://127.0.0.1:%d" % p3, env=E3)
    out, rc, err = vw("login", "pat@example.com", stdin=PW + "\n123456\n", env=E3)
    check("a WebAuthn-only account is refused by name, not half-tried",
          rc != 0 and "WebAuthn" in err, (out, err))
finally:
    srv3.kill()

shutil.rmtree(D, True)
report(33)
