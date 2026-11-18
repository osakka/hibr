#!/usr/bin/env python3
"""The app runtime's socket (ADR 0050): an app's own process, sourcing
runtime.hibr and nothing else of the desktop, connects, says hello, and
what it tells the desktop happens there; one speaking another protocol is
refused and the person told hibr was updated, with Restart Desktop a click
away; a name interface.tsv does not carry as a told request is refused by
name; and the words cross whole, blanks, percents and empty ones included.

    python3 tests/rtchan.py
"""
import os, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import screen
from screen import Term, check, report, load, tree, scratch

MOD = tree("build/mods/console.so")
WM = tree("programs/desktop/desktop.hibr")
HERE = tree("programs/desktop")
SAID = scratch("rtchan-said")
GOOD = scratch("rtchan-good.hibr")
BAD = scratch("rtchan-bad.hibr")

# An app process: the runtime alone, a hello, then three requests -- a note
# whose words hold a blank, a percent and an empty word between them, a
# name that is no told request, and a second note to say the first two
# were read in order.
open(GOOD, "w").write(r'''DT_HERE=%s
. "$DT_HERE/runtime.hibr"
if ! dt_rtopen demo; then
	printf 'refused: %%s\n' "$DT_RTSAID" > %s
	exit 1
fi
printf 'ok: %%s\n' "$DT_RTSAID" > %s
dt_notep low "from the app: %%s %%s [%%s] done" "50%%25 off" "two  blanks" ""
dt_rtsend tell dt_quit
dt_notep low "second note"
''' % (HERE, SAID, SAID))
# One that speaks another protocol.
open(BAD, "w").write(r'''DT_HERE=%s
. "$DT_HERE/runtime.hibr"
DT_RTPROTO=99
dt_rtopen olddemo || printf 'refused: %%s\n' "$DT_RTSAID" > %s.bad
''' % (HERE, SAID))

SESSION = r'''%s. %s
dt_open
DT_NOTEMS=60000
tst_go() {
	[ -f %s ] || { dt_want 100; return 0; }
	local c
	read -r c < %s
	command rm -f %s
	eval "$c"
	dt_want 30
}
dt_ticker tst_go
dt_run
dt_close
'''
MARK = scratch("rtchan-do")
p = scratch("rtchan.hibr")
open(p, "w").write(SESSION % (load(MOD), WM, MARK, MARK, MARK))
t = Term(p, rows=30, cols=110, settle=0.6)


def do(cmd):
    """Run cmd in the desktop, from its ticker, and wait until it is taken."""
    open(MARK, "w").write(cmd + "\n")
    deadline = time.time() + 5
    while os.path.exists(MARK) and time.time() < deadline:
        t.send(b"\x1b[I", settle=0.1, collect=0.1)


def log():
    return open(os.path.join(screen.HOME, str(t.pid), "state", "hibr", "desktop.log"),
                errors="replace").read()


before = t.screen()
check("an idle desktop is not listening for app processes until one is started",
      "app runtime: listening" not in log(), log()[-300:])

do('dt_rtlisten; "$HIBR" %s &' % GOOD)
sc = t.until("second note", tries=80, step=0.1)
said = open(SAID).read() if os.path.exists(SAID) else ""
check("an app process sourcing only the runtime says hello and is answered",
      said.startswith("ok: "), said)
check("a note it tells the desktop to show is shown, its words whole",
      sc.find("from the app: 50%25 off two  blanks [] done") is not None, sc.text())
check("and the next one after it, in order", sc.find("second note") is not None, sc.text())
check("a name that is no told request is refused by name, and the desktop runs on",
      "dt_quit, which is not a told request" in log() and sc.find("second note") is not None, log()[-600:])

do('"$HIBR" %s &' % BAD)
sc = t.until("hibr was updated", tries=80, step=0.1)
bad = ""
deadline = time.time() + 5
while time.time() < deadline and not bad:
    bad = open(SAID + ".bad").read() if os.path.exists(SAID + ".bad") else ""
    time.sleep(0.1)
check("one speaking another protocol is refused, and told why",
      bad.startswith("refused: ") and "protocol 99" in bad, bad)
check("and the person is told hibr was updated, with the restart a click away",
      sc.find("hibr was updated") is not None, sc.text())
check("the refusal is in the log with both versions",
      "refused olddemo" in log() and "protocol 99" in log(), log()[-600:])

if os.environ.get("RTDEBUG"):
    print("\n".join(l for l in log().split("\n") if "runtime" in l or "rtt" in l or "hibr:" in l))
t.quit(b"q", 1.0)
for f in (GOOD, BAD, p, MARK, SAID, SAID + ".bad"):
    if os.path.exists(f):
        os.unlink(f)
report(9)
