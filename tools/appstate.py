#!/usr/bin/env python3
"""The platform's own variables installables read or write (ADR 0050): every
global the platform declares -- wm/, the widgets, the `# platform:` files --
and, for each, how many installables read it and how many write it. What a
process of its own may still see is decided from this: a mirror the desktop
pushes, the app's own copy, or refused.

  python3 tools/appstate.py [--write]   # --write puts docs/adr/0050-state.tsv

Read from the text, not run: a name counts as written where it is assigned
(NAME=, NAME[...]=, NAME+=, declare/read/unset of it) and as read where it is
expanded (${NAME, $NAME). Approximate by design -- a word, not a parse -- and
said so in the table's header.
"""
import collections, glob, os, re, sys
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
APPS = os.environ.get("HIBR_APPS", os.path.expanduser("~/hibr-apps"))
OUT = os.path.join(ROOT, "docs", "adr", "0050-state.tsv")
DESK = os.path.join(ROOT, "programs/desktop")


def plat(f):
    try:
        return any(l.startswith("# platform:") for l in open(f, errors="replace"))
    except OSError:
        return False


def platfiles():
    fs = glob.glob(os.path.join(DESK, "wm/*.hibr")) + glob.glob(os.path.join(DESK, "widgets/*.hibr"))
    for d in ("apps", "system", "control-panel", "control-strip", "menubar", "savers"):
        fs += [f for f in glob.glob(os.path.join(DESK, d, "**", "*.hibr"), recursive=True) if plat(f)]
    fs.append(os.path.join(DESK, "desktop.hibr"))
    return fs


def installables():
    fs = set(glob.glob(os.path.join(APPS, "bundles", "**", "*.hibr"), recursive=True))
    for d in ("apps", "system", "control-panel", "control-strip", "menubar", "savers"):
        fs |= set(glob.glob(os.path.join(DESK, d, "**", "*.hibr"), recursive=True))
    return sorted(f for f in fs if not plat(f))


DECL = re.compile(r"^\s*(?:declare\s+-g\w*\s+|export\s+)?([A-Z][A-Z0-9_]*)(?:\[[^\]]*\])?\+?=")
DECLG = re.compile(r"^\s*declare\s+-g\w*\s+(.*)")


def globals_():
    names = set()
    for f in platfiles():
        for line in open(f, errors="replace"):
            if line.startswith("\t") or line.startswith("  "):
                continue
            m = DECLG.match(line)
            if m:
                for w in m.group(1).split():
                    w = w.split("=")[0]
                    if re.match(r"^[A-Z][A-Z0-9_]*$", w):
                        names.add(w)
                continue
            m = DECL.match(line)
            if m:
                names.add(m.group(1))
    return names


def main():
    names = globals_()
    reads, writes = collections.defaultdict(set), collections.defaultdict(set)
    files = installables()
    for f in files:
        text = open(f, errors="replace").read()
        own = set(re.findall(r"^\s*declare\s+-g\w*\s+([A-Z][A-Z0-9_]*)", text, re.M))
        for n in names:
            if n in own or n not in text:
                continue
            if re.search(r"\$\{?%s\b" % n, text):
                reads[n].add(f)
            if re.search(r"(^|[\s;(&|])%s(\[[^\]]*\])*\+?=|\b(unset|read\b[^\n]*)\s+%s\b" % (n, n), text, re.M):
                writes[n].add(f)
    rows = sorted(((n, len(reads[n]), len(writes[n])) for n in names if reads[n] or writes[n]),
                  key=lambda r: (-(r[1] + r[2]), r[0]))
    print("installables: %d; platform globals: %d; touched by installables: %d (written by %d)" % (
        len(files), len(names), len(rows), sum(1 for r in rows if r[2])))
    for n, r, w in rows[:40]:
        print("  %-24s read in %3d, written in %3d" % (n, r, w))
    if "--write" in sys.argv:
        with open(OUT, "w") as o:
            o.write("# name\tread in\twritten in -- tools/appstate.py --write (a word match, not a parse)\n")
            for row in rows:
                o.write("%s\t%d\t%d\n" % row)
        print("wrote", os.path.relpath(OUT, ROOT))


main()
