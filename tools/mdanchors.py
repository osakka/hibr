#!/usr/bin/env python3
"""Every link to a markdown file's #anchor names a heading that file has.

    python3 tools/mdanchors.py [ROOT]

tests/530-docs.t checks that a linked page exists; this checks the part
after the #, which nothing did. Splitting the desktop's README into a
guide and WRITING.md moved eight sections, and a link into any of them
would still have found its file and landed at the top of the wrong page.
Headings are slugged the way GitHub does it -- lower case, punctuation
dropped, spaces to dashes, a repeat numbered -- and only tracked pages are
read, so a worktree or a build folder is never mistaken for the tree.
Prints each broken anchor, and exits 1 if there was one.
"""
import os, re, subprocess, sys
root = sys.argv[1] if len(sys.argv) > 1 else "."
files = subprocess.run(["git", "-C", root, "ls-files", "*.md"], capture_output=True, text=True).stdout.split()
def slug(h):
    h = re.sub(r"[`*_]", "", h.strip().lower())
    h = re.sub(r"[^\w\- ]", "", h)
    return h.replace(" ", "-")
heads = {}
def anchors(p):
    if p not in heads:
        s, seen, fence = set(), {}, False
        try: lines = open(p).read().split("\n")
        except OSError: heads[p] = None; return None
        for l in lines:
            if l.startswith("```"): fence = not fence
            if fence: continue
            m = re.match(r"#{1,6}\s+(.*)", l)
            if m:
                a = slug(m.group(1)); n = seen.get(a, 0); seen[a] = n + 1
                s.add(a if n == 0 else "%s-%d" % (a, n))
        heads[p] = s
    return heads[p]
bad = 0
LINK = re.compile(r"\]\(([^)\s]*?)#([^)\s]+)\)")
for f in files:
    fp = os.path.join(root, f)
    for n, l in enumerate(open(fp), 1):
        for tgt, a in LINK.findall(re.sub(r"`[^`]*`", "", l)):
            if tgt.startswith("http"): continue
            p = os.path.normpath(os.path.join(os.path.dirname(fp), tgt)) if tgt else fp
            if not p.endswith(".md"): continue
            hs = anchors(p)
            if hs is None: print("%s:%d: %s: no such file" % (f, n, tgt)); bad += 1
            elif a not in hs: print("%s:%d: %s#%s: no such heading" % (f, n, tgt or "(self)", a)); bad += 1
print("%d broken" % bad); sys.exit(1 if bad else 0)
