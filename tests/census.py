#!/usr/bin/env python3
"""Which of the desktop's functions no suite ever calls.

    python3 tests/census.py [suite...]     # default: desktop apps uifuzz run.sh
    python3 tests/census.py --log FILE     # report on a log already taken

Builds build/hibr.census (`make census`: the shell, logging every function
call to $HIBR_CENSUS), runs the suites under it side by side through
tests/all.py, and lists every function defined under examples/desktop that
no run called, file by file, with the count. At 0.44 that count was 71, and
it was where 0.44.1's thirteen bugs lived: a function no test reaches is
one nothing has checked. Watch the count fall; the last one is kept in
build/census.last so each report says which way it moved.

The log also records whether each call's arguments bound to the function's
declared parameters; calls that did not are listed too, since a callback
handed the wrong number of arguments is exactly what 0.44.1 was.
"""
import os
import re
import subprocess
import sys
from collections import Counter, defaultdict

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
DESK = os.path.join(ROOT, "examples", "desktop")
BIN = os.path.join(ROOT, "build", "hibr.census")
LAST = os.path.join(ROOT, "build", "census.last")
LOGS = os.path.join(ROOT, "build", "census-logs")
DEFS = [re.compile(r"^\s*([A-Za-z_][A-Za-z0-9_]*)\(\)\s*\{"),
        re.compile(r"^\s*fn\s+([A-Za-z_][A-Za-z0-9_]*)\s*\(")]


def defined():
    """name -> the desktop file defining it, relative to examples/desktop."""
    out = {}
    for d, _, fs in os.walk(DESK):
        for f in sorted(fs):
            if not f.endswith(".hibr"):
                continue
            p = os.path.join(d, f)
            for line in open(p, errors="replace"):
                for r in DEFS:
                    m = r.match(line)
                    if m:
                        out.setdefault(m.group(1), os.path.relpath(p, DESK))
    return out


def take(suites, log):
    """Run the suites under the census build, appending every call to log."""
    subprocess.run(["make", "-s", "census"], cwd=ROOT, check=True)
    if os.path.exists(log):
        os.unlink(log)
    env = dict(os.environ, HIBR=BIN, HIBR_CENSUS=log, HIBR_TESTLOGS=LOGS)
    return subprocess.run([sys.executable, os.path.join(HERE, "all.py")]
                          + suites, cwd=ROOT, env=env).returncode


def report(log):
    """Print what was never called and what failed to bind; the count."""
    defs = defined()
    called = set()
    bad = Counter()
    for line in open(log, errors="replace"):
        f = line.rstrip("\n").split("\t")
        if len(f) != 4:
            continue
        called.add(f[0])
        if f[3] != "0" and f[0] in defs:
            bad[(f[0], f[2])] += 1
    never = defaultdict(list)
    for n, p in defs.items():
        if n not in called:
            never[p].append(n)
    count = sum(len(v) for v in never.values())
    for p in sorted(never):
        print("%s: %s" % (p, " ".join(sorted(never[p]))))
    if bad:
        print("\ncalls whose arguments did not bind:")
        for (n, a), k in sorted(bad.items()):
            print("  %s with %s argument%s, %d time%s" % (
                n, a, "" if a == "1" else "s", k, "" if k == 1 else "s"))
    was = None
    try:
        was = int(open(LAST).read())
    except (OSError, ValueError):
        pass
    move = "" if was is None else " (was %d)" % was
    print("\n%d of %d desktop functions never called%s" % (count, len(defs),
                                                          move))
    open(LAST, "w").write("%d\n" % count)
    return count


def main():
    args = sys.argv[1:]
    if "--log" in args:
        report(args[args.index("--log") + 1])
        return
    log = os.path.join(ROOT, "build", "census.log")
    st = take(args or ["desktop", "apps", "uifuzz", "run.sh"], log)
    report(log)
    if st:
        print("census: a suite failed under the census build; "
              "logs in %s" % LOGS)
    sys.exit(st)


if __name__ == "__main__":
    main()
