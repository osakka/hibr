#!/usr/bin/env python3
"""Every global a strict desktop file's functions would create, found by reading.

    python3 tests/strictvars.py [prefix...]     # default: the whole desktop

`strict vars` refuses, at run time, an assignment in a function that would
create a global -- but only on the paths something actually runs, and a
refusal on one path hides whatever the next line would have been refused
for. This finds them all at once, by reading each function in every file
that says `strict`: whatever it assigns (`x=`, `x+=`, `x[k]=`, `x := f`,
`read x`, `for x in`, `printf -v x`, and inside `(( ))`) must be one of its
parameters, declared local in it, set at the top level of some desktop
file, or one of the shell's own names. Anything else is reported with its
file and line, and fails the suite.

It reads, it does not parse: quotes and comments are stripped a line at a
time and a function ends at the first `}` in column 0, which is how every
function in the desktop is written. A name assigned only as a caller's local
(which strict allows) would be reported here, and none is -- keep it so.
"""
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
DESK = os.path.join(os.path.dirname(HERE), "examples", "desktop")
SHELL = {"RET", "M", "REPLY", "IFS", "OPTIND", "OPTARG", "RANDOM", "SECONDS",
         "PWD", "OLDPWD", "HOME", "PATH", "COLUMNS", "LINES", "LC_ALL", "_"}
NAME = r"[A-Za-z_][A-Za-z0-9_]*"
ASG = re.compile(r"(?:^|[\s;&|({!])(%s)(?:\[[^]]*\])*\+?=(?!=)" % NAME)
BIND = re.compile(r"(?:^|[\s;&|({!])(%s)(?:\[[^]]*\])*\s+:=\s" % NAME)
READ = re.compile(r"\bread\s+((?:-[a-zA-Z]+\s+(?:\S+\s+)?)*)(%s[\w \t]*)"
                  % NAME)
FOR = re.compile(r"\bfor\s+(%s)\s+in\b" % NAME)
PRV = re.compile(r"\bprintf\s+-v\s+(%s)" % NAME)
ARIX = re.compile(r"\(\((.*?)\)\)")
ARI = re.compile(r"(?:\(\(|[,(]|\b)\s*(%s)\s*(?:\+\+|--|[-+*/%%&|^]=|=(?!=))"
                 % NAME)
FN = re.compile(r"^(?:fn\s+(%s)\s*\(([^)]*)\)|(%s)\s*\(\))\s*\{?\s*$"
                % (NAME, NAME))
DECL = re.compile(r"^\s*(local|declare|typeset|readonly|export)\b(.*)$")


def strip(line):
    """The line with quoted text blanked and any comment removed."""
    out, q, i = [], None, 0
    while i < len(line):
        c = line[i]
        if q == "'":
            q = None if c == "'" else q
            i += 1
            continue
        if q == '"':
            if c == "\\":
                i += 2
                continue
            if c == '"':
                q = None
            out.append(" ")
            i += 1
            continue
        if c == "\\":
            i += 2
            continue
        if c in "'\"":
            q = c
            out.append(c)
            i += 1
            continue
        if c == "#" and (i == 0 or line[i - 1] in " \t;"):
            break
        out.append(c)
        i += 1
    return "".join(out)


def assigned(s):
    """Every name a stripped line assigns."""
    names = set()
    for rx in (ASG, BIND, FOR, PRV):
        names.update(m.group(1) for m in rx.finditer(s))
    m = READ.search(s)
    if m:
        for w in re.split(r"[;&|]", m.group(2))[0].split():
            if w in ("do", "then"):
                break
            if re.match(NAME + "$", w):
                names.add(w)
    for ax in ARIX.finditer(s):
        if s[max(0, ax.start() - 1)] != "$":
            names.update(m.group(1) for m in ARI.finditer(ax.group(1)))
    return names


def declared(s):
    """The names a declaration line declares, and whether it is global."""
    d = DECL.match(s)
    if not d:
        return None, set()
    words = d.group(2).split()
    names = {re.split(r"[=\[]", w)[0] for w in words if not w.startswith("-")}
    glob = d.group(1) not in ("local", "declare", "typeset") or "-g" in words
    return glob, names


def read_all():
    """Every desktop file's functions, and every name set at a top level."""
    tops, funcs, strict = set(), [], set()
    for d, _, fs in os.walk(DESK):
        for f in sorted(fs):
            if not f.endswith(".hibr"):
                continue
            p = os.path.join(d, f)
            text = open(p, errors="replace").read()
            if re.search(r"^strict\b", text, re.M):
                strict.add(p)
            cur = None
            for n, raw in enumerate(text.split("\n"), 1):
                m = FN.match(raw.rstrip())
                if cur is None and m:
                    params = set()
                    for a in (m.group(2) or "").split(","):
                        a = a.strip().lstrip(".").split("=")[0].strip()
                        if a:
                            params.add(a.split()[-1])
                    cur = (p, m.group(1) or m.group(3), params, [])
                    continue
                s = strip(raw)
                if cur is not None:
                    if raw.startswith("}"):
                        funcs.append(cur)
                        cur = None
                    else:
                        cur[3].append((n, s))
                    continue
                tops |= assigned(s) | declared(s)[1]
    return tops, funcs, strict


def main():
    want = [os.path.join(DESK, a) for a in sys.argv[1:]] or [DESK]
    tops, funcs, strict = read_all()
    found = []
    checked = 0
    for p, name, params, body in funcs:
        if p not in strict or not any(p.startswith(w) for w in want):
            continue
        checked += 1
        loc = set(params)
        for _, s in body:
            g, names = declared(s)
            if g is False:
                loc |= names
        for n, s in body:
            if DECL.match(s):
                continue
            for v in sorted(assigned(s) - loc - tops - SHELL):
                found.append("%s:%d: %s in %s creates a global" % (
                    os.path.relpath(p, DESK), n, v, name))
    for f in found:
        print("FAIL " + f)
    print("\n%d passed, %d failed" % (checked - len(found), len(found)))
    sys.exit(1 if found else 0)


if __name__ == "__main__":
    main()
