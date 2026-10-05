#!/usr/bin/env python3
"""Run every suite at once, one per core, and say how each went.

    python3 tests/all.py [suite...]      # default: all of them

tests/run.sh and every pty suite are independent -- each pty run has its own
terminal, its own XDG directories and its own hold sockets -- and each spends
nearly all its time waiting on the program it drives, so they run side by
side. A suite's full output goes to build/test-logs/<suite>.log; what is
printed here is one line each, and the failures. Exits non-zero if any
suite failed.
"""
import os
import subprocess
import sys
import time
from concurrent.futures import ThreadPoolExecutor

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
LOGS = (os.environ.get("HIBR_TESTLOGS")
        or os.path.join(ROOT, "build", "test-logs"))
SUITES = ["run.sh", "desktop", "apps", "most", "hvi", "console", "cat",
          "mon", "mtr", "editor", "term_diff", "uifuzz", "strictvars",
          "md_spec", "html_tree", "mail", "mailapp", "contacts", "calapp", "pim", "pim_rrule", "web", "dav", "media", "youtube",
          "uni_bidi", "uni_shape", "hcal_icu"]


def one(name):
    """Run one suite; its name, whether it passed, its summary, and time."""
    cmd = ([os.path.join(HERE, "run.sh")] if name == "run.sh"
           else [sys.executable, "-u", os.path.join(HERE, name + ".py")])
    t = time.time()
    p = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
    out = p.stdout + p.stderr
    open(os.path.join(LOGS, name + ".log"), "w").write(out)
    lines = out.splitlines()
    summary = next((l for l in reversed(lines) if "passed" in l), "no summary")
    bad = [l for l in lines if l.startswith("FAIL") or l.startswith("failed:")
           or l.startswith("planned") or l.startswith("  ")
           and "passed" not in l and p.returncode]
    return name, p.returncode == 0, summary, bad, time.time() - t


def main():
    want = sys.argv[1:] or SUITES
    os.makedirs(LOGS, exist_ok=True)
    t = time.time()
    ok = True
    with ThreadPoolExecutor(max_workers=os.cpu_count() or 4) as pool:
        for name, good, summary, bad, took in pool.map(one, want):
            ok = ok and good
            print("%-10s %-4s %-28s %5.0fs" % (name, "ok" if good else "FAIL",
                                             summary, took))
            for l in bad:
                print("    " + l)
    print("all: %s in %.0fs; logs in %s" % ("ok" if ok else "FAILED",
                                            time.time() - t, LOGS))
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
