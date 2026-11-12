#!/usr/bin/env python3
"""A function defined only where __linux__ holds and called from code that is
on every platform: on Linux it links, and on a Mac -- where a module is linked
with `-undefined dynamic_lookup` -- the module builds and then will not load
("symbol not found in flat namespace"). console.so did that to every Mac
desktop for bf_idat (Gitea #503). Prints one line per such call, nothing when
there is none; tests/999-linuxonly.t holds the tree to that.

    python3 tools/linuxonly.py
"""
import re, glob, sys
files = glob.glob("src/*.c") + glob.glob("mods/*.c") + glob.glob("mods/*/*.c")
defs = {}      # name -> file
refs = []      # (file, line, name) in code not guarded by __linux__
def walk(path):
    st = []    # stack of (linuxonly_now)
    out = []
    for n, l in enumerate(open(path, errors="replace"), 1):
        s = l.strip()
        m = re.match(r"#\s*(if|ifdef|ifndef|elif|else|endif)\b(.*)", s)
        if m:
            k, cond = m.group(1), m.group(2)
            if k in ("if", "ifdef", "ifndef"):
                lin = ("__linux__" in cond and k != "ifndef" and "!" not in cond)
                notlin = (k == "ifndef" and "__linux__" in cond) or ("!defined(__linux__)" in cond.replace(" ", ""))
                st.append(["lin" if lin else ("not" if notlin else "x")])
            elif k in ("else", "elif"):
                if st:
                    t = st[-1][0]
                    st[-1][0] = "not" if t == "lin" else ("lin" if t == "not" else "x")
            elif k == "endif":
                if st: st.pop()
            continue
        guarded = any(x[0] == "lin" for x in st)
        out.append((n, l, guarded))
    return out
src = {f: walk(f) for f in files}
for f, lines in src.items():
    for i, (n, l, g) in enumerate(lines):
        m = re.match(r"^[a-zA-Z_][\w \*]*?\b([a-z_][a-z0-9_]*)\(([^;]*)$", l)
        if m and not l.startswith(("static", "return", "if", "else")) and i + 1 < len(lines):
            nxt = lines[i + 1][1] if lines[i][1].rstrip().endswith(")") else ""
            if l.rstrip().endswith(")") and nxt.startswith("{"):
                defs.setdefault(m.group(1), []).append((f, g))
linuxonly = {k for k, v in defs.items() if all(g for _, g in v)}
bad = 0
for f, lines in src.items():
    for n, l, g in lines:
        if g:
            continue
        for k in linuxonly:
            if re.search(r"\b%s\s*\(" % re.escape(k), l) and not re.match(r"^[a-zA-Z_].*\b%s\(" % re.escape(k), l):
                print("%s:%d: %s used outside __linux__: %s" % (f, n, k, l.strip()[:90]))
                bad += 1
sys.exit(1 if bad else 0)
