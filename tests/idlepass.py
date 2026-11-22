#!/usr/bin/env python3
"""What one idle pass of dt_run costs, counted in instructions, never timed
(ADR 0050, Decision 8: inert prototype code once cost 135 us a pass and
nothing noticed for two releases).

    python3 tests/idlepass.py            # the check
    IP_RUNS=3 python3 tests/idlepass.py  # and say each run's figures

The real dt_run, on a pty, with this tree's installables loaded as a
session loads them (so a ticker an app registers is counted), under
callgrind: a ticker of the test's own counts the passes, asks for the next
one a millisecond on (dt_want 1) and ends the desktop at the Nth. A run of
N1 passes and one of N2 are subtracted, so startup, the first frames and
the load on the box all cancel; what is left, divided by N2 - N1, is one
idle pass -- a wake with nothing to read, a frame with nothing changed
(DT_TERMMS 0, so every wake draws and none is held back by the clock), the
periodic calls and the tickers -- and it is held under BOUND.

BOUND sits between today's pass and today's plus a 100 us slowdown made on
purpose (the busy loop in MUTATE, put in dt_run's own loop), so the code
that cost 135 us would fail it. It needs valgrind: without it the check
fails, it never skips.

Run it alone, never beside other suites: the subtraction cancels startup
and load, but not work the clock sets off, and a slower pass meets more of
it -- with a spinner on each of the four cores a pass counted 12.53 M,
over the bound, where alone it counts 11.86 to 11.99 M. So it is not in
tests/all.py's parallel run; a gate runs it after the others, as the
sanitizer gate runs alone.
"""
import glob, os, re, shutil, statistics, subprocess, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import screen
from screen import Term, check, report, load, tree, scratch

N1, N2 = 20, 60
# Instructions per idle pass. Measured 2026-11-21 at 01933d95 with this
# tree's installables loaded (25 tickers): 11,855,537, 11,952,853 and
# 11,986,282 in three runs; the 100 us slowdown in MUTATE (37 turns, 498 K
# instructions, 102-158 us on the loaded box) adds about half a million, so
# the bound is the median and half of that. A deliberate rise -- a ticker
# added on purpose -- raises it in the commit that adds the cost, said there.
BOUND = int(os.environ.get("IP_BOUND") or 12200000)
MUTATE = 'local ip_mut=0; while [ "$ip_mut" -lt "${IP_SLOW:-0}" ]; do ip_mut=$((ip_mut + 1)); done'

MOD = tree("build/mods/console.so")
WM = tree("programs/desktop/desktop.hibr")
D = tree("programs/desktop")


def session(n):
    p = scratch("idlepass-%d.hibr" % n)
    open(p, "w").write(r'''%s. %s
dt_open
d=%s
DT_APPDIRS=("$d/apps"); dt_apps
DT_SYSDIRS=("$d/system"); dt_sysapps
DT_BARDIRS=("$d/menubar"); dt_menubar
CP_PANEDIRS=("$d/control-panel"); cp_panes
CS_MODDIRS=("$d/control-strip"); cs_modules
DT_TERMMS=0
# Under callgrind /proc/$$/exe is valgrind's own tool, so the update check
# would find a new hibr and put up a note whose life, by the clock, spans a
# load-dependent number of passes. Its once-a-minute look is put an hour off;
# what it costs a pass -- a comparison and a want -- stays in.
DT_UPDT=$EPOCHSECONDS
DT_UPMS=3600000
# Under callgrind everything is fifty times slower: no callback is a stall.
DT_SLOWMS=3600000
DT_HOLDMS=3600000
IP_N=0
ip_tick() {
	IP_N=$((IP_N + 1))
	[ "$IP_N" -lt %d ] || DT_QUIT=1
	dt_want 1
	return 0
}
dt_ticker ip_tick
echo "$HIBR $(readlink /proc/$$/exe)" > %s.exe
dt_run
dt_close
''' % (load(MOD), WM, D, n, p))
    return p


def instructions(n):
    """The instructions one desktop of n passes ran, all of them: the
    desktop's own process, whose profile is named by its pid -- every child
    it forks (a substitution, a job) runs on under callgrind and leaves a
    profile of its own, which is no part of a pass."""
    out = scratch("idlepass-%d.cg" % n)
    logf = scratch("idlepass-%d.vg" % n)
    wrap = scratch("idlepass-wrap")
    for f in glob.glob(out + ".*") + [logf]:
        if os.path.exists(f):
            os.unlink(f)
    open(wrap, "w").write('#!/bin/sh\nexec valgrind --tool=callgrind --log-file=%s --callgrind-out-file=%s.%%p %s "$@"\n'
                          % (logf, out, screen.HIBR))
    os.chmod(wrap, 0o755)
    real, screen.HIBR = screen.HIBR, wrap
    p = session(n)
    try:
        t = Term(p, rows=40, cols=120, settle=1.0, env={"DT_BARTIME": "12:00"})
    finally:
        screen.HIBR = real
    t.wait(900)
    t.close()
    exe = open(p + ".exe").read().strip() if os.path.exists(p + ".exe") else ""
    mine = "%s.%d" % (out, t.pid)
    m = re.search(r"^summary:\s*(\d+)", open(mine).read(), re.M) if os.path.exists(mine) else None
    for f in glob.glob(out + ".*") + [p, p + ".exe", wrap, logf]:
        if os.path.exists(f):
            os.unlink(f)
    return (int(m.group(1)) if m else None), exe


if os.environ.get("ASAN_OPTIONS"):
    # A sanitizer build's instructions describe nothing a desktop runs, and
    # valgrind cannot run it: what is checked is that the passes run.
    p = session(N2)
    t = Term(p, rows=40, cols=120, settle=1.0, env={"DT_BARTIME": "12:00"})
    t.wait(600)
    t.close()
    check("under the sanitizers the desktop starts with the tree loaded", os.path.exists(p + ".exe"))
    check("and runs its %d idle passes to its own end" % N2, t.exited and t.status == 0, t.status)
    report(3)
    sys.exit(0)
have = shutil.which("valgrind")
check("valgrind is here to count the instructions (the check never skips)", have, "no valgrind on PATH")
runs = int(os.environ.get("IP_RUNS", "1"))
per = []
for _ in range(runs if have else 0):
    a, ea = instructions(N1)
    b, eb = instructions(N2)
    if a and b:
        per.append((b - a) // (N2 - N1))
        print("idle pass: %d instructions (%d passes %d, %d passes %d); the desktop ran %s"
              % (per[-1], N1, a, N2, b, eb))
    else:
        print("a run gave no count: %s %s" % (a, b))
got = int(statistics.median(per)) if per else None
print("idle pass, median of %d: %s instructions, bound %d" % (len(per), got, BOUND))
check("an idle pass of dt_run costs at most %d instructions" % BOUND,
      got is not None and got <= BOUND, (got, BOUND, per))
report(3)
