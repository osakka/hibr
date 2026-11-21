#!/usr/bin/env python3
"""The screen saver started again and again in one desktop (Gitea #556).

A desktop reads its savers the first time one is wanted, and a rescan --
the catalogue's after every change, Refresh Desktop -- asks for them again.
Asking again read savers/saver.hibr and every saver a second time: strict
refused each function as already defined, a line apiece in desktop.log, and
the second reading emptied the list, so the saver after the next rescan was
a blank screen. Here the saver is started, a saver of one's own is put in
the config folder and the desktop told to rescan, it runs, the desktop is
told again and it runs again, and then it is what the lock shows, twice --
with nothing in the log.

    python3 tests/saverload.py [path/to/hibr]
"""
import os, shutil, subprocess, sys, tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import screen
from screen import Term, check, report, load, tree, scratch

if len(sys.argv) > 1:
    screen.HIBR = os.path.abspath(sys.argv[1])
MOD = tree("build/mods/console.so")
WM = tree("programs/desktop/desktop.hibr")
ROWS, COLS = 24, 80
MENU = b"\x1b[21~"
LOCK = b"\x1b\x0c"

# The lock's password, checked through PAM in a folder of the suite's own:
# one service whose only password is "right horse".
PAMDIR = tempfile.mkdtemp(prefix="hibr-saverload-pam-")
open(os.path.join(PAMDIR, "chk.sh"), "w").write(
    '#!/bin/sh\ntr -d "\\000" | grep -qx "right horse"\n')
os.chmod(os.path.join(PAMDIR, "chk.sh"), 0o755)
open(os.path.join(PAMDIR, "pw"), "w").write(
    "auth required pam_exec.so expose_authtok quiet %s/chk.sh\n"
    "account required pam_permit.so\n" % PAMDIR)

CONF = tempfile.mkdtemp(prefix="hibr-saverload-conf-")
SAVERS = os.path.join(CONF, "hibr", "savers")
os.makedirs(SAVERS)
CTL = os.path.join(CONF, "d.ctl")
path = scratch("saverload.hibr")
open(path, "w").write("%s. %s\nDT_CTL=1\nDT_CTLPATH=%s\ndt_open\n"
                      'dt_new "Hello" 8 30 6 10\ndt_run\ndt_close\n'
                      % (load(MOD), WM, CTL))


def rescan():
    """What the catalogue does after it installs something."""
    r = subprocess.run([screen.HIBR, tree("programs/desktop/lib/ctl.hibr"),
                        "--socket", CTL, "rescan"], capture_output=True,
                       text=True, timeout=10)
    return r.returncode == 0, r.stdout + r.stderr


def mine(sc):
    return sc.find("MY OWN SAVER") is not None and sc.find("Hello") is None


def seen(t, cond):
    """Wait for the screen to show it; whether it came. `until` answers the
    last screen either way."""
    return cond(t.until(cond, tries=80))


def deskup(sc):
    return sc.find("Hello") is not None and sc.find("MY OWN SAVER") is None


def asked(sc):
    return sc.find("Enter your password") is not None


def desk(t):
    return seen(t, deskup)


t = Term(path, env={"XDG_CONFIG_HOME": CONF, "DT_SAVER": "mine",
                    "DT_SAVERIDLE": "0", "DT_LOCKSERVICE": "pw",
                    "DT_LOCKCONF": PAMDIR},
         rows=ROWS, cols=COLS, settle=1.0)
seen(t, lambda sc: sc.find("Hello") is not None)

t.keys([MENU, b"s"])
first = seen(t, lambda sc: sc.find("Hello") is None)
t.keys([b"x"])
back = desk(t)
check("the saver starts, with none of one's own yet a bundled one, and a key "
      "ends it", first and back, t.screen())

open(os.path.join(SAVERS, "mine.hibr"), "w").write(
    'command -v sv_saver > /dev/null && sv_saver mine "Mine" 200\n'
    'fn mine_start(int rows, int cols) { :; }\n'
    'fn mine_frame(int rows, int cols) { console pen white black; '
    'console put 3 3 "MY OWN SAVER"; }\n')
ok1, out1 = rescan()
t.keys([MENU, b"s"])
ran = seen(t, mine)
t.keys([b"x"])
desk(t)
check("a saver installed and a rescan told: the next start reads and runs it",
      ok1 and ran, (out1, t.screen()))

ok2, out2 = rescan()
t.keys([MENU, b"s"])
again = seen(t, mine)
t.keys([b"x"])
desk(t)
check("told again, the next start still has it -- nothing read twice, the "
      "list not emptied", ok2 and again, (out2, t.screen()))

t.keys([LOCK])
locked = seen(t, mine)
t.keys([b"x"])
seen(t, asked)
t.keys([b"\x15right horse\r"])
open1 = desk(t)
check("locked, the saver is the same one, and the password opens it",
      locked and open1, t.screen())

rescan()
t.keys([LOCK])
relocked = seen(t, mine)
t.keys([b"x"])
seen(t, asked)
t.keys([b"\x15right horse\r"])
open2 = desk(t)
check("unlocked, rescanned and locked again, it starts once more",
      relocked and open2, t.screen())

t.quit(b"qy", 1.2)
t.close()
try:
    log = open(t.log, "rb").read().decode("utf8", "replace")
except OSError:
    log = ""
check("and the log says nothing was defined twice",
      "already defined" not in log,
      "\n".join(l for l in log.splitlines() if "already defined" in l))

shutil.rmtree(CONF, True)
shutil.rmtree(PAMDIR, True)
os.unlink(path)
report(7)
