#!/usr/bin/env python3
"""Drive the Vault desk accessory and the desktop's vault through a pty.

    python3 tests/vault.py [path-to-hibr]

Against tests/bwserve.py, the stand-in server: log in from the Vault
window, a wrong password first; find an item; copy its password and its
username, which reach the terminal's clipboard but never the clipboard
history, and leave it again when their time is up; sync through the
desktop's own exported session; lock; and in a new desktop unlock again,
offline, from the vault kept here. Every run has config, data, state and
runtime folders of its own. Needs Python's cryptography package for the
stand-in server; skips, saying so, without it.
"""
import base64, os, re, shutil, subprocess, sys, tempfile, time, urllib.request, json

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import screen as sx
from screen import Term, check, report, load, tree

try:
    import cryptography  # noqa: F401
except ImportError:
    print("vault: no cryptography package here for the stand-in server, nothing driven")
    print("0 passed, 0 failed")
    sys.exit(0)

if len(sys.argv) > 1:
    sx.HIBR = os.path.abspath(sys.argv[1])
HERE = os.path.dirname(os.path.abspath(__file__))
SLOW = 4 if os.environ.get("ASAN_OPTIONS") else 1
D = tempfile.mkdtemp(prefix="hibr-vault-")
ENV = {"XDG_CONFIG_HOME": os.path.join(D, "c"), "XDG_DATA_HOME": os.path.join(D, "d"),
       "XDG_RUNTIME_DIR": os.path.join(D, "r"), "XDG_STATE_HOME": os.path.join(D, "s")}
PW = "correct horse battery staple"
CLIPFILE = os.path.join(D, "s", "hibr", "clipboard.json")


def session(tag, pre=""):
    sess = os.path.join(D, tag + ".hibr")
    open(sess, "w").write("%s%s\n. %s\n. %s\n. %s\ndt_open\ndt_new Vault 20 64 1 2 vault\ndt_run\ndt_close\n" % (
        load("console", "vw", "dav"), pre, tree("examples/desktop/desktop.hibr"),
        tree("examples/desktop/desk-accessories/vault.hibr"),
        tree("examples/desktop/control-panel/vaultset.hibr")))
    return Term(sess, rows=30, cols=100, settle=1.5, env=ENV)


def look(t, secs=0.5):
    t.collect(secs * SLOW)
    return t.screen()


def waitfor(t, text, secs=8.0):
    end = time.time() + secs * SLOW
    while True:
        sc = look(t, 0.3)
        if sc.find(text) or time.time() > end:
            return sc


def clips(t):
    """What the desktop has put on the terminal's clipboard, in order."""
    return [base64.b64decode(m.group(1)).decode("utf8", "replace")
            for m in re.finditer(rb"\x1b\]52;c;([A-Za-z0-9+/=]*)(?:\x07|\x1b\\)", t.out)]


def stats(port):
    return json.load(urllib.request.urlopen("http://127.0.0.1:%d/stats" % port))


srv = subprocess.Popen([sys.executable, os.path.join(HERE, "bwserve.py")], stdout=subprocess.PIPE, text=True)
port = int(srv.stdout.readline().split()[1])
URL = "http://127.0.0.1:%d" % port
try:
    t = session("first", "DT_VWCLEAR=2")
    try:
        sc = look(t)
        check("with no account the Vault offers to log in", sc.find("No vault yet") and sc.find("Log In"), sc)
        t.keys([b"\r"])
        sc = waitfor(t, "Log In to a Vault")
        check("enter opens the login dialog, its server field first",
              sc.find("Server:") and sc.find("Email:") and sc.find("Password:"), sc)
        t.keys([URL.encode(), b"\t", b"pat@example.com", b"\t", b"wrong one", b"\r"])
        sc = waitfor(t, "incorrect")
        check("a wrong password is refused in the dialog, with the server's reason, and nothing typed shown",
              sc.find("incorrect") and sc.find("Log In to a Vault") and not sc.find("wrong one"), sc)
        t.keys([PW.encode(), b"\r"])
        sc = waitfor(t, "unlocked until")
        rows = [r for r in range(30) if any(n in sc.row(r) for n in ("Bank", "Example Mail", "Wifi"))]
        names = [next(n for n in ("Bank", "Example Mail", "Wifi") if n in sc.row(r)) for r in rows]
        check("the right one logs in: the items, sorted by name, with their users",
              names == ["Bank", "Example Mail", "Wifi"] and "pat.doe" in sc.row(rows[0]), (names, sc))
        check("and the password typed never reached the screen", PW not in t.text, "")
        t.keys([b"mail"])
        sc = look(t)
        check("typing finds by name", sc.find("Example Mail") and not sc.find("Bank"), sc)
        t.keys([b"\r"])
        sc = waitfor(t, "Copied")
        check("enter copies the password to the terminal's clipboard",
              "s3crét-mail" in clips(t), clips(t))
        t.keys([b"\t", b"\t", b"\r"])
        waitfor(t, "Copied")
        check("the Username button copies the username", clips(t)[-1:] == ["pat"], clips(t))
        n = len(clips(t))
        time.sleep(3)
        look(t, 0.8)
        check("a copied secret leaves the clipboard when its time is up",
              len(clips(t)) > n and clips(t)[-1] == "", clips(t))
        hist = open(CLIPFILE).read() if os.path.exists(CLIPFILE) else ""
        check("and never reaches the clipboard history", "s3cr" not in hist and "pat" not in hist, hist)
        # F10 opens the menu bar on the hibr menu; one right arrow is File.
        before = stats(port)["sync"]
        t.keys([b"\x1b[21~", b"\x1b[C", b"s"])
        ok = False
        end = time.time() + 8 * SLOW
        while time.time() < end and not ok:
            look(t, 0.3)
            ok = stats(port)["sync"] > before
        check("File > Sync Now syncs through the session the desktop exported", ok, stats(port))
        t.keys([b"\x1b[21~", b"\x1b[C", b"l"])
        sc = waitfor(t, "The vault is locked.")
        sess = os.path.join(D, "r", "hibr-vw-%d" % os.getuid(), "session")
        check("File > Lock locks it, for every shell: the session is gone",
              sc.find("The vault is locked.") and not os.path.exists(sess), sc)
    finally:
        t.quit()
finally:
    srv.terminate()
    srv.wait()

# A new desktop, the server gone: unlock from the vault kept here.
t = session("second")
try:
    sc = look(t)
    check("a new desktop starts locked, the account remembered",
          sc.find("The vault is locked.") and sc.find("pat@example.com"), sc)
    t.keys([b"\r"])
    sc = waitfor(t, "Unlock the Vault")
    t.keys([b"nope", b"\r"])
    sc = waitfor(t, "wrong master password")
    check("offline, a wrong password is refused", sc.find("wrong master password"), sc)
    t.keys([PW.encode(), b"\r"])
    sc = waitfor(t, "unlocked until")
    check("and the right one opens the vault kept here", sc.find("Bank") and sc.find("Wifi"), sc)
finally:
    t.quit()

leak = [os.path.join(r, f) for r, _, fs in os.walk(D) for f in fs
        if any(s in open(os.path.join(r, f), "rb").read() for s in (b"s3cr", b"b4nk-p4ss", b"h0me-w1fi", PW.encode()))]
check("nothing decrypted, nor the password, was written anywhere", not leak, leak)
shutil.rmtree(D, True)
report(17)
