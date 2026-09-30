#!/usr/bin/env python3
"""Which of the desktop's functions no suite ever calls.

    python3 tests/census.py [suite...]     # default: desktop apps uifuzz run.sh
    python3 tests/census.py --log FILE     # report on a log already taken
    python3 tests/census.py --expansion [PREFIX...]
                        # and where strict expansion would change behaviour

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

With --expansion it also lists, file and line, every place an expansion split
or a value globbed or matched as a pattern -- the places `strict expansion`
would change -- under the desktop-relative prefixes given, or by default in
the files that already say `strict`. A place no suite reaches is not listed,
so what it prints is where to start, not a proof of what is left.
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
        if len(f) != 5 or f[0] == "X":
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


def expansion(log, prefixes):
    """Print each place under the prefixes where strict expansion would change
    what happens: an expansion that split, or a value that globbed or matched
    as a pattern. With no prefixes, the desktop files that already say
    `strict`, since turning expansion on is those files' next step."""
    if not prefixes:
        prefixes = sorted(os.path.relpath(p, DESK) for p in strict_files())
    sites = defaultdict(set)
    for line in open(log, errors="replace"):
        f = line.rstrip("\n").split("\t")
        if len(f) != 5 or f[0] != "X" or not f[2].startswith(DESK + "/"):
            continue
        rel = os.path.relpath(f[2], DESK)
        if any(rel == p or rel.startswith(p.rstrip("/") + "/")
               for p in prefixes):
            sites[(rel, int(f[3]), f[1])].add(f[4])
    for (rel, ln, kind), vals in sorted(sites.items()):
        v = sorted(vals)
        print("%s:%d: %s %r%s" % (rel, ln, kind, v[0],
                                  " (+%d more)" % (len(v) - 1) if len(v) > 1
                                  else ""))
    print("\n%d place%s where strict expansion would change what happens"
          % (len(sites), "" if len(sites) == 1 else "s"))
    return len(sites)


SIG = re.compile(r"^\s*fn\s+([A-Za-z_][A-Za-z0-9_]*)\s*\(([^)]*)\)")


def signatures():
    """name -> (file, [(param, type, optional)]) for every declared function."""
    out = {}
    for d, _, fs in os.walk(DESK):
        for f in sorted(fs):
            if not f.endswith(".hibr"):
                continue
            p = os.path.join(d, f)
            for line in open(p, errors="replace"):
                m = SIG.match(line)
                if not m:
                    continue
                ps = []
                for a in m.group(2).split(","):
                    a = a.strip()
                    if not a or a.startswith("..."):
                        continue
                    opt = "=" in a
                    words = a.split("=")[0].split()
                    ty = words[0] if len(words) > 1 else ""
                    ps.append((words[-1], ty, opt))
                out.setdefault(m.group(1), (os.path.relpath(p, DESK), ps))
    return out


def types(log):
    """Print, for every declared parameter, what the suites passed it: an
    integer every time, an integer or nothing, or something else."""
    sigs = signatures()
    seen = defaultdict(Counter)
    calls = Counter()
    for line in open(log, errors="replace"):
        f = line.rstrip("\n").split("\t")
        if len(f) != 5 or f[0] == "X" or f[0] not in sigs or f[3] != "0":
            continue
        calls[f[0]] += 1
        for k, c in enumerate(f[4]):
            seen[(f[0], k)][c] += 1
    groups = defaultdict(list)
    for name, (rel, ps) in sorted(sigs.items()):
        for k, (pn, ty, opt) in enumerate(ps):
            c = seen[(name, k)]
            if ty:
                g = "typed already"
            elif not calls[name]:
                g = "never called"
            elif not c:
                g = "never passed"
            elif set(c) == {"i"}:
                g = "always an integer"
            elif set(c) == {"i", "e"}:
                g = "an integer or empty"
            else:
                g = "anything else"
            groups[g].append("%s(%s%s) %s" % (name, pn, " =" if opt else "",
                                              dict(c) if c else ""))
    for g in ("always an integer", "an integer or empty", "anything else",
              "never passed", "never called", "typed already"):
        print("== %s: %d" % (g, len(groups[g])))
        if g in ("always an integer", "an integer or empty"):
            for x in groups[g]:
                print("   " + x)


def strict_files():
    """Desktop files that ask for strict checks of any kind."""
    out = []
    for d, _, fs in os.walk(DESK):
        for f in fs:
            p = os.path.join(d, f)
            if f.endswith(".hibr") and re.search(
                    r"^strict\b", open(p, errors="replace").read(), re.M):
                out.append(p)
    return out


def main():
    args = sys.argv[1:]
    if "--types" in args:
        args.remove("--types")
        if "--log" in args:
            types(args[args.index("--log") + 1])
            return
        log = os.path.join(ROOT, "build", "census.log")
        st = take(args or ["desktop", "apps", "uifuzz", "run.sh"], log)
        types(log)
        sys.exit(st)
    exp = None
    if "--expansion" in args:
        i = args.index("--expansion")
        exp = [a for a in args[i + 1:] if not a.startswith("-")]
        args = args[:i]
    if "--log" in args:
        log = args[args.index("--log") + 1]
        expansion(log, exp) if exp is not None else report(log)
        return
    log = os.path.join(ROOT, "build", "census.log")
    st = take(args or ["desktop", "apps", "uifuzz", "run.sh"], log)
    report(log)
    if exp is not None:
        print()
        expansion(log, exp)
    if st:
        print("census: a suite failed under the census build; "
              "logs in %s" % LOGS)
    sys.exit(st)


if __name__ == "__main__":
    main()
