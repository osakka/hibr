#!/usr/bin/env python3
"""The html module against html5lib's tree-construction tests.

tests/html/*.dat are html5lib-tests' tree-construction cases (MIT, James
Graham, Geoffrey Sneddon and contributors), taken from the last commit
before they moved to web-platform-tests in June 2026. Each document, or
fragment in its context element, is parsed by `html dump` and its tree
compared line for line with the one the test expects; the few marked
#script-on are parsed with the scripting flag on (`html dump -s`), which
changes only how noscript parses -- hibr itself never runs a script.

    tests/html_tree.py [-v] [--file NAME] [--case N] [--shell PATH]
"""
import argparse, glob, os, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)


def cases(path):
    out, cur, key = [], None, None
    for line in open(path, encoding="utf-8", newline="").read().split("\n"):
        if line == "#data":
            if cur:
                out.append(cur)
            cur, key = {"data": []}, "data"
            continue
        if cur is None:
            continue
        if line.startswith("#") and line[1:] in ("errors", "new-errors", "document",
                                                 "document-fragment", "script-on", "script-off"):
            key = line[1:]
            cur.setdefault(key, [])
            continue
        cur[key].append(line)
    if cur:
        out.append(cur)
    for c in out:
        c["data"] = "\n".join(c["data"])
        doc = c.get("document", [])
        while doc and doc[-1] == "":
            doc.pop()
        c["document"] = "\n".join(doc)
        c["fragment"] = c["document-fragment"][0] if c.get("document-fragment") else None
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("-v", action="store_true")
    ap.add_argument("--file")
    ap.add_argument("--case", type=int)
    ap.add_argument("--shell", default=os.environ.get("HIBR") or os.path.join(ROOT, "build", "hibr"))
    ap.add_argument("--mod", default=os.path.join(os.environ.get("HIBR_TESTMODS") or
                                                  os.path.join(ROOT, "build", "mods"), "html.so"))
    a = ap.parse_args()
    mod = a.mod
    files = sorted(glob.glob(os.path.join(HERE, "html", "*.dat")))
    if a.file:
        files = [f for f in files if os.path.basename(f).startswith(a.file)]
    total = passed = 0
    byfile = {}
    for f in files:
        name = os.path.basename(f)[:-4]
        for i, c in enumerate(cases(f)):
            if a.case is not None and i != a.case:
                continue
            total += 1
            args = [a.shell, "-c", 'mod load "$1"; shift; html dump "$@"', "x", mod]
            if c["fragment"]:
                args += ["-f", c["fragment"]]
            if "script-on" in c:
                args.append("-s")
            args.append("-")
            try:
                r = subprocess.run(args, input=c["data"].encode("utf-8"), capture_output=True,
                                   timeout=10)
                got = r.stdout.decode("utf-8", "replace").rstrip("\n")
            except subprocess.TimeoutExpired:
                got = "(timed out)"
            ok = got == c["document"]
            byfile.setdefault(name, [0, 0])
            byfile[name][1] += 1
            if ok:
                passed += 1
                byfile[name][0] += 1
                if a.v:
                    print("ok   %s %d" % (name, i))
            elif a.v or a.case is not None:
                print("FAIL %s %d\n--- data\n%s\n--- want\n%s\n--- got\n%s\n" %
                      (name, i, c["data"], c["document"], got))
    if not a.v and a.case is None:
        for n, (p, t) in sorted(byfile.items()):
            if p != t:
                print("%-40s %4d/%d" % (n, p, t))
    print("%d passed, %d failed" % (passed, total - passed))
    return 0 if passed == total else 1


sys.exit(main())
