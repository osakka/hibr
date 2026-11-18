#!/usr/bin/env python3
"""Every platform function an installable in this tree calls is classed for
an app's own process (ADR 0050): local, a request, a declaration or gone --
tools/appcalls.py --check -- and the desktop's own list of saved settings,
which the state survey classes by, can be read (tools/appstate.py).

    python3 tests/appcalls.py

Reads, never runs, the tree's installables only: the catalogue checkout is
other sessions' working tree, and its half is checked by ./stale there.
"""
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
env = dict(os.environ, HIBR_APPS="/nonexistent-catalogue",
           HIBR=os.environ.get("HIBR", os.path.join(ROOT, "build", "hibr")),
           HIBR_TESTMODS=os.environ.get("HIBR_TESTMODS", os.path.join(ROOT, "build", "mods")))
calls = subprocess.run([sys.executable, os.path.join(ROOT, "tools", "appcalls.py"), "--check"],
                       cwd=ROOT, env=env, capture_output=True, text=True)
out = calls.stdout + calls.stderr
fails = [l for l in out.splitlines() if l.startswith("FAIL")]
state = subprocess.run([sys.executable, os.path.join(ROOT, "tools", "appstate.py")],
                       cwd=ROOT, env=env, capture_output=True, text=True)
sites = subprocess.run([sys.executable, os.path.join(ROOT, "tools", "callsites.py"), "--check"],
                       cwd=ROOT, env=env, capture_output=True, text=True)
fails += [l for l in (sites.stdout + sites.stderr).splitlines() if l.startswith("FAIL")]
if sites.returncode and not [l for l in sites.stdout.splitlines() if l.startswith("FAIL")]:
    fails.append("FAIL tools/callsites.py --check exited %d: %s" % (sites.returncode,
                 (sites.stdout + sites.stderr).strip()[-300:]))
if state.returncode:
    fails.append((state.stdout + state.stderr).strip().splitlines()[-1])
passed = 0
for l in out.splitlines():
    if l.endswith("failed") and " passed, " in l:
        passed = int(l.split()[0])
if calls.returncode and not fails:
    fails.append("FAIL tools/appcalls.py --check exited %d: %s" % (calls.returncode, out.strip()[-300:]))
for l in fails:
    print(l if l.startswith("FAIL") else "FAIL " + l)
print("\n%d passed, %d failed" % (passed + (state.returncode == 0), len(fails)))
sys.exit(1 if fails else 0)
