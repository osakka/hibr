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
`need`/`mod load` in programs/desktop). Each module's own `hibr_require`
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
import covmap  # noqa: E402

SUITE_MODS = {
    "console": ["console"],
    "cat": ["cat"],
    "most": ["console", "most"],
    "hvi": ["console", "hvi"],
    "mon": ["console", "mon"],
    "mtr": ["console", "trace"],
    "editor": [],
    "term_diff": ["term"],
    "desktop_basics": [],
    "desktop_windows": [],
    "desktop_pictures": [],
    "desktop_session": [],
    "apps_panel": [],
    "apps_reach": [],
    "apps_office": [],
    "apps_core": [],
    "apps_more": [],
    "uifuzz": [],
    "strictvars": [],
    "html_tree": ["html"],
    "mail": ["email"],
    "mailapp": ["email", "html", "db"],
    "md_spec": ["md"],
    "dav": ["dav"],
    "fetch": ["fetch", "inflate"],
    "web": ["web"],
    "media": ["media"],
    "youtube": ["web", "media"],
    "sound": ["media"],
    "xscreen": ["xscr"],
    "pim": ["pim", "dav"],
    "contacts": ["pim", "dav", "email", "db"],
    "calapp": ["pim", "dav", "email", "db"],
    "pim_rrule": ["pim"],
    "uni_bidi": ["uni"],
    "uni_shape": ["uni"],
    "hcal_icu": ["hcal"],
    "salat_adhan": ["salat"],
    "vw_crypto": ["vw"],
    "vw": ["vw", "dav"],
    "vault": ["vw", "dav"],
    "sixel": ["console", "img", "media"],
    "kitgfx": ["console", "img"],
    "pdfapp": ["pdf", "img", "console"],
    "holdpix": ["console", "img", "hold", "term", "pty"],
    # inflate.c is not a module of its own: it is shared source, the prompt
    # module's own, which the archive module compiles in as well. Naming it
    # here is what makes a change to it reach this suite.
    "archive": ["archive", "inflate"],
    "appmgr": ["dav", "archive", "inflate"],
}
APPS = ("apps_panel", "apps_reach", "apps_office", "apps_core", "apps_more")
DESK = ("desktop_basics", "desktop_windows", "desktop_pictures", "desktop_session")
DESKTOP = DESK + ("vault",) + APPS + ("uifuzz", "strictvars", "mailapp", "contacts", "calapp",
                                         "sound", "net", "bt")
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
    for d, _, fs in os.walk(os.path.join(ROOT, "programs", "desktop")):
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


def suites_for(path, req, prov, where, desk=None):
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
    if path.startswith("programs/desktop/"):
        if desk:
            hit, because = covmap.reach(path, desk, SUITES)
            if hit is not None:
                return hit, because
            return set(DESKTOP) | set(covmap.READERS), "the desktop: " + because
        return set(DESKTOP) | set(covmap.READERS), "the desktop"
    if path.startswith("tests/"):
        n = os.path.basename(path)
        s = n[:-3] if n.endswith(".py") else None
        if s in SUITES:
            return {s}, "the suite itself"
        if n == "apps.py":
            return set(APPS), "the apps suite, run as its parts"
        if n == "desktop.py":
            return set(DESK), "the desktop suite, run as its parts"
        if n == "appslice.py":
            return set(APPS) | set(DESK), "the cutter both suites run their parts through"
        if n.endswith((".t", ".expected", ".hibr")) or n == "run.sh":
            return {"run.sh"}, "the C-side harness"
        if n == "coverage.tsv":
            return set(), "the coverage map, which only narrows"
        if n in ("affected.py", "census.py", "asan.py", "diff.py",
                 "corpus.py", "fuzz.py", "covmap.py"):
            return set(), "a tool, not a suite"
    if path == "programs/vw.hibr":
        return {"vw", "vault", "run.sh"}, "the vw command"
    if path == "programs/catalogue.hibr":
        return {"appmgr", "run.sh"}, "the application manager"
    if path.startswith(("docs/", "examples/", "programs/")):
        return {"run.sh"}, "documentation, an example or a program (530/540)"
    if path.startswith(("packaging/", "tools/", ".git")) or path in (
            "LICENSE", "COPYING", "TRADEMARKS", ".gitignore"):
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


def ancestor(a, b):
    """Whether commit a is b or one of b's ancestors."""
    return subprocess.run(["git", "merge-base", "--is-ancestor", a, b],
                          cwd=ROOT, capture_output=True).returncode == 0


def deskbase(since):
    """What a desktop change is measured from, and the paths that adds.

    The coverage map knows the calls as they were when it was taken, so a
    desktop file or a suite changed since then is part of the change even
    when it is older than the ref asked about: from the map's commit, never
    from anything later. A map not taken in this history is none."""
    commit, _ = covmap.load()
    if not commit or not ancestor(commit, "HEAD"):
        return None, []
    base = since or "HEAD"
    if ancestor(base, commit):
        return base, []
    more = subprocess.run(["git", "diff", "--name-only", commit, base, "--",
                           "programs/desktop", "tests"], cwd=ROOT,
                          capture_output=True, text=True,
                          check=True).stdout.split()
    return commit, more


def main():
    args = sys.argv[1:]
    run = "--run" in args
    why = "--why" in args
    since = None
    if "--since" in args:
        i = args.index("--since")
        since = args[i + 1]
        del args[i:i + 2]
    given = [a for a in args if not a.startswith("--")]
    paths = given or changed(since)
    desk, wider = deskbase(since)
    if not given:
        paths = sorted(set(paths) | set(wider))
    req, prov = graph()
    where = reach(req, prov)
    pick = set()
    for p in paths:
        hit, because = suites_for(os.path.normpath(p), req, prov, where, desk)
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
