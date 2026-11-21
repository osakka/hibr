#!/usr/bin/env python3
"""Every platform function an installable in this tree calls is classed for
an app's own process (ADR 0050): local, a request, a declaration or gone --
tools/appcalls.py --check -- every call into one is of a decided kind
(tools/seamcalls.py --check), and the desktop's own list of saved settings,
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
# Every call into an installable is of a kind that is decided -- asked,
# pushed or told -- and no told kind's answer is read (tools/seamcalls.py).
seam = subprocess.run([sys.executable, os.path.join(ROOT, "tools", "seamcalls.py"), "--check"],
                      cwd=ROOT, env=env, capture_output=True, text=True)
if seam.returncode:
    said = [l for l in (seam.stdout + seam.stderr).splitlines() if l.startswith("seamcalls:")]
    fails += ["FAIL " + l for l in said] or ["FAIL tools/seamcalls.py --check exited %d" % seam.returncode]
if state.returncode:
    fails.append((state.stdout + state.stderr).strip().splitlines()[-1])
# Whether each installable can run in a process of its own (ADR 0050,
# Decision 8): the tree's half -- a call by a platform prefix to a function
# no file defines, and calls into a separate installable held to a count
# that only lowers. The catalogue's half is report-only (tools/hostaudit.py
# with no flag), since it is other sessions' tree.
host = subprocess.run([sys.executable, os.path.join(ROOT, "tools", "hostaudit.py"), "--check"],
                      cwd=ROOT, env=env, capture_output=True, text=True)
fails += [l for l in (host.stdout + host.stderr).splitlines() if l.startswith("FAIL")]
if host.returncode and not [l for l in host.stdout.splitlines() if l.startswith("FAIL")]:
    fails.append("FAIL tools/hostaudit.py --check exited %d: %s" % (host.returncode,
                 (host.stdout + host.stderr).strip()[-300:]))
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
