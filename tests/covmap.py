#!/usr/bin/env python3
"""Which suites call each of the desktop's functions, measured.

    python3 tests/covmap.py --take        # run every suite under the census
                                          # build and write tests/coverage.tsv
    python3 tests/covmap.py FUNCTION...   # the suites the map says call each

tests/affected.py asks this for a change under programs/desktop: a change
inside a function's body reaches the suites that called that function, and
nothing else of the desktop's. The map is measured, not read: `--take`
builds build/hibr.census (`make census`, the shell logging every function
call), runs tests/all.py under it with a census log per suite, and keeps,
for every function defined under programs/desktop, the suites that called
it. It is committed, with the commit it was taken at on its first line, so
a release gate in a fresh worktree has it.

It answers only what it measured, and refuses the rest -- the caller then
runs every desktop suite, as it did before there was a map:

  - a changed line outside every function body is code that runs when the
    file is sourced, in every desktop suite;
  - a function the map has no entry for -- new, renamed, or one no suite
    calls -- is unknown;
  - a map not taken in this history is no map.

What it cannot see is a test or a function changed *after* it was taken
that now calls the changed function. tests/affected.py closes that by
diffing from the map's commit rather than from the release before whenever
the map is the older of the two, so every desktop and suite change since
the map is part of the change; the window is what changed since the map,
which the daily run keeps to a day.
"""
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
MAP = os.path.join(HERE, "coverage.tsv")
BIN = os.path.join(ROOT, "build", "hibr.census")
LOGS = os.path.join(ROOT, "build", "census-suites")
DESK = "programs/desktop/"
DEFS = [re.compile(r"^\s*([A-Za-z_][A-Za-z0-9_]*)\(\)\s*\{"),
        re.compile(r"^\s*fn\s+([A-Za-z_][A-Za-z0-9_]*)\s*\(")]
# Suites that read every desktop file rather than run it: whatever changes
# under programs/desktop, they are cheap and they always run.
READERS = ("run.sh", "strictvars", "deps", "scopes")


def git(*a, check=True):
    """A git command's output in ROOT, or None when it fails and check is off."""
    p = subprocess.run(["git"] + list(a), cwd=ROOT, capture_output=True,
                       text=True)
    if p.returncode and check:
        raise SystemExit("git %s: %s" % (" ".join(a), p.stderr.strip()))
    return p.stdout if p.returncode == 0 else None


def spans(lines):
    """(name, first, last) for each function, 0-based and inclusive.

    A body ends at the next line that is `}` alone, before any other
    definition; one that does not is given no name, which reads as code
    outside a function -- the side that runs more, never less."""
    out, i = [], 0
    while i < len(lines):
        m = None
        for r in DEFS:
            m = m or r.match(lines[i])
        if not m:
            i += 1
            continue
        if lines[i].rstrip().endswith("}") and \
                lines[i].count("{") == lines[i].count("}"):
            out.append((m.group(1), i, i))
            i += 1
            continue
        j = i + 1
        while j < len(lines) and lines[j] != "}" and \
                not any(r.match(lines[j]) for r in DEFS):
            j += 1
        if j < len(lines) and lines[j] == "}":
            out.append((m.group(1), i, j))
            i = j + 1
        else:
            out.append((None, i, j - 1))
            i = j
    return out


def defined():
    """Every function defined under programs/desktop."""
    out = set()
    for d, _, fs in os.walk(os.path.join(ROOT, DESK)):
        for f in fs:
            if f.endswith(".hibr"):
                text = open(os.path.join(d, f), errors="replace").read()
                out |= {n for n, _, _ in spans(text.split("\n")) if n}
    return out


def load():
    """(commit, {function: set of suites}), or (None, {}) with no map."""
    try:
        f = open(MAP)
    except OSError:
        return None, {}
    head = f.readline().split()
    commit = head[3].rstrip(";") if len(head) > 3 else None
    out = {}
    for line in f:
        n, _, s = line.rstrip("\n").partition("\t")
        out[n] = set(s.split()) if s else set()
    return commit, out


def callers():
    """Every suite the map saw call a desktop function: what a change the
    map cannot narrow has to run."""
    _, m = load()
    out = set()
    for v in m.values():
        out |= v
    return out


def take():
    """Run every suite under the census build; write the map."""
    sys.path.insert(0, HERE)
    from all import SUITES
    if git("status", "--porcelain", "--", "programs", "tests", "src",
           "mods").strip():
        raise SystemExit("covmap: the tree has changes; a map is taken at a "
                         "commit, so commit them or take it in a worktree")
    commit = git("rev-parse", "HEAD").strip()
    subprocess.run(["make", "-s", "census"], cwd=ROOT, check=True)
    os.makedirs(LOGS, exist_ok=True)
    for f in os.listdir(LOGS):
        os.unlink(os.path.join(LOGS, f))
    env = dict(os.environ, HIBR=BIN, HIBR_CENSUSDIR=LOGS,
               HIBR_TESTLOGS=os.path.join(ROOT, "build", "census-testlogs"))
    rc = subprocess.run([sys.executable, os.path.join(HERE, "all.py")],
                        cwd=ROOT, env=env).returncode
    if rc:
        raise SystemExit("covmap: a suite failed under the census build; a "
                         "map from a run that did not finish would say too "
                         "little, so none was written")
    names = defined()
    calls = {n: set() for n in names}
    for s in SUITES:
        p = os.path.join(LOGS, s + ".log")
        if not os.path.exists(p):
            continue
        for line in open(p, errors="replace"):
            f = line.split("\t", 1)
            if f[0] in calls:
                calls[f[0]].add(s)
    with open(MAP, "w") as out:
        out.write("# taken at %s; python3 tests/covmap.py --take\n" % commit)
        for n in sorted(calls):
            if calls[n]:
                out.write("%s\t%s\n" % (n, " ".join(
                    s for s in SUITES if s in calls[n])))
    hit = sum(1 for n in calls if calls[n])
    print("covmap: %d of %d desktop functions called by some suite; %s"
          % (hit, len(names), os.path.relpath(MAP, ROOT)))


def hunks(base, path):
    """(old lines, new lines) a diff from base touches, 0-based, or None
    when the file is new or gone on either side."""
    d = git("diff", "-U0", base, "--", path)
    old, new = set(), set()
    for m in re.finditer(r"^@@ -(\d+)(?:,(\d+))? \+(\d+)(?:,(\d+))? @@", d,
                         re.M):
        a, b = int(m.group(1)), int(m.group(2) or 1)
        c, e = int(m.group(3)), int(m.group(4) or 1)
        old |= set(range(a - 1, a - 1 + b))
        new |= set(range(c - 1, c - 1 + e))
    return old, new


def owners(lines, touched):
    """The function holding each touched line; None for one outside any."""
    sp = spans(lines)
    out = set()
    for ln in touched:
        name = None
        for n, a, b in sp:
            if a <= ln <= b:
                name = n
                break
        out.add(name)
    return out


def reach(path, base, suites):
    """(suites, why) a change to one desktop file reaches, or (None, why)
    when the map cannot say and every desktop suite has to run."""
    commit, m = load()
    if not commit:
        return None, "no coverage map"
    if not path.endswith(".hibr"):
        return None, "not a script"
    was = git("show", "%s:%s" % (base, path), check=False)
    full = os.path.join(ROOT, path)
    if was is None or not os.path.exists(full):
        return None, "a file added or removed"
    old, new = hunks(base, path)
    fns = owners(was.split("\n"), old) | owners(
        open(full, errors="replace").read().split("\n"), new)
    if None in fns:
        return None, "a line outside any function"
    hit = set(READERS)
    for f in sorted(fns):
        if f not in m:
            return None, "%s: no suite in the map calls it" % f
        if not m[f] <= set(suites):
            return None, "%s: the map names a suite that is gone" % f
        hit |= m[f]
    return hit, "function%s %s" % ("" if len(fns) == 1 else "s",
                                    " ".join(sorted(fns)))


def main():
    a = sys.argv[1:]
    if a == ["--take"]:
        take()
        return
    commit, m = load()
    if not commit:
        raise SystemExit("covmap: no map; python3 tests/covmap.py --take")
    print("map taken at %s" % commit[:12])
    for n in a:
        print("%-24s %s" % (n, " ".join(sorted(m[n])) if n in m
                            else "(not in the map)"))


if __name__ == "__main__":
    main()
