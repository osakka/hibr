#!/usr/bin/env python3
"""The suites a change reaches, and optionally run just those.

    python3 tests/affected.py                 # what the working tree changed
    python3 tests/affected.py --since v0.49   # what changed since a ref
    python3 tests/affected.py PATH...         # what these paths reach
    python3 tests/affected.py --run [...]     # and run them with tests/all.py
    python3 tests/affected.py --why [...]     # say which path chose each

For iterating. The full run, tests/all.py, stays the release gate: this reads
dependencies from the tree, and a dependency it cannot see -- a script
reaching a module through a string it builds -- it does not know about.

What a suite reaches is worked out, not listed. Each suite names the modules
it loads directly (SUITE_MODS below; the desktop suites also take every
`need`/`mod load` in examples/desktop). Each module's own `hibr_require`
calls are read from its source and resolved through the `hibr_provide`
calls of the others, so a change to mods/console reaches hvi.py because hvi
requires "display" and console provides it. The shell itself, include/,
the Makefile and tests/screen.py reach everything. A path this does not
recognise reaches everything too: guessing small is how a skipped suite
reports success.
"""
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
from all import SUITES  # noqa: E402

SUITE_MODS = {
    "console": ["console"],
    "cat": ["cat"],
    "most": ["console", "most"],
    "hvi": ["console", "hvi"],
    "mon": ["console", "mon"],
    "mtr": ["console", "trace"],
    "editor": [],
    "term_diff": ["term"],
    "desktop": [],
    "apps": [],
    "uifuzz": [],
}
DESKTOP = ("desktop", "apps", "uifuzz")
EVERYTHING = ("src/", "include/", "Makefile", "tests/screen.py",
              "tests/all.py", "deploy.sh")


def module_of(path):
    """The module a path under mods/ belongs to, or None for a shared header."""
    rest = path[len("mods/"):]
    if "/" in rest:
        return rest.split("/")[0]
    if rest.endswith(".c"):
        return rest[:-2]
    return None


def sources(mod):
    """The C sources and headers of one module."""
    d = os.path.join(ROOT, "mods", mod)
    if os.path.isdir(d):
        return [os.path.join(d, f) for f in sorted(os.listdir(d))
                if f.endswith((".c", ".h"))]
    f = d + ".c"
    return [f] if os.path.exists(f) else []


def graph():
    """Each module's required interfaces, and the module providing each."""
    req, prov = {}, {}
    names = set()
    for e in os.listdir(os.path.join(ROOT, "mods")):
        if os.path.isdir(os.path.join(ROOT, "mods", e)):
            names.add(e)
        elif e.endswith(".c"):
            names.add(e[:-2])
    for m in names:
        req[m] = set()
        for f in sources(m):
            t = open(f, errors="replace").read()
            req[m] |= set(re.findall(r'hibr_require\(s, "([a-z]+)"', t))
            for i in re.findall(r'hibr_provide\(s, "([a-z]+)"', t):
                prov[i] = m
    return req, prov


def resolve(name, prov):
    """A `need` name is an interface or a module; the module either way."""
    return prov.get(name, name)


def desktop_mods(req, prov):
    """Every module the desktop's scripts ask for by name, anywhere on a line."""
    out = set()
    for d, _, fs in os.walk(os.path.join(ROOT, "examples", "desktop")):
        for f in fs:
            if f.endswith(".hibr"):
                t = open(os.path.join(d, f), errors="replace").read()
                for n in re.findall(r"\b(?:need|mod load) ([a-z]+)\b", t):
                    if resolve(n, prov) in req:
                        out.add(resolve(n, prov))
    return out


def reach(req, prov):
    """suite -> every module it reaches, following requires to providers."""
    out = {}
    extra = desktop_mods(req, prov)
    for s in SUITES:
        if s == "run.sh":
            continue
        todo = list(SUITE_MODS.get(s, []))
        if s in DESKTOP:
            todo += sorted(extra)
        seen = set()
        while todo:
            m = todo.pop()
            if m in seen:
                continue
            seen.add(m)
            todo += [resolve(i, prov) for i in req.get(m, ())]
        out[s] = seen
    return out


def header_mods(path, req, prov):
    """The modules a shared header under mods/ reaches: its users, its provider."""
    base = os.path.basename(path)
    hit = set()
    for m in req:
        for f in sources(m):
            if re.search(r'#include\s+"(\.\./)?%s"' % re.escape(base),
                         open(f, errors="replace").read()):
                hit.add(m)
    return hit


def suites_for(path, req, prov, where):
    """The suites one changed path reaches, and a word on why."""
    if path.startswith(EVERYTHING) or path == "include":
        return set(SUITES), "the shell or the harness"
    if path.endswith(".md"):
        return {"run.sh"}, "documentation (530-docs)"
    if path.startswith("mods/"):
        m = module_of(path)
        mods = {m} if m else header_mods(path, req, prov)
        if not mods:
            return set(SUITES), "a mods/ file nothing includes by name"
        hit = {s for s, r in where.items() if r & mods}
        return hit | {"run.sh"}, "module " + ", ".join(sorted(mods))
    if path.startswith("examples/desktop/"):
        return set(DESKTOP) | {"run.sh"}, "the desktop"
    if path.startswith("tests/"):
        n = os.path.basename(path)
        s = n[:-3] if n.endswith(".py") else None
        if s in SUITES:
            return {s}, "the suite itself"
        if n.endswith((".t", ".expected", ".hibr")) or n == "run.sh":
            return {"run.sh"}, "the C-side harness"
        if n in ("affected.py", "census.py", "asan.py", "diff.py",
                 "corpus.py", "fuzz.py"):
            return set(), "a tool, not a suite"
    if path.startswith(("docs/", "examples/")):
        return {"run.sh"}, "documentation or an example (530/540)"
    if path.startswith(("packaging/", "tools/", ".git")) or path in (
            "LICENSE", ".gitignore"):
        return set(), "outside what the suites run"
    return set(SUITES), "a path this does not recognise"


def changed(since):
    """Paths changed in the working tree, or since a ref, plus untracked ones."""
    cmd = ["git", "diff", "--name-only"] + ([since] if since else ["HEAD"])
    out = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True,
                         check=True).stdout.split()
    out += subprocess.run(["git", "ls-files", "--others", "--exclude-standard"],
                          cwd=ROOT, capture_output=True, text=True,
                          check=True).stdout.split()
    return sorted(set(out))


def main():
    args = sys.argv[1:]
    run = "--run" in args
    why = "--why" in args
    since = None
    if "--since" in args:
        i = args.index("--since")
        since = args[i + 1]
        del args[i:i + 2]
    paths = [a for a in args if not a.startswith("--")] or changed(since)
    req, prov = graph()
    where = reach(req, prov)
    pick = set()
    for p in paths:
        hit, because = suites_for(os.path.normpath(p), req, prov, where)
        pick |= hit
        if why:
            print("%-50s %-28s %s" % (p, because, " ".join(
                s for s in SUITES if s in hit) or "-"))
    order = [s for s in SUITES if s in pick]
    if not run:
        print(" ".join(order) if order else "nothing to run")
        return
    if not order:
        print("nothing to run")
        return
    sys.exit(subprocess.run([sys.executable, os.path.join(HERE, "all.py")]
                            + order, cwd=ROOT).returncode)


if __name__ == "__main__":
    main()
