#!/usr/bin/env python3
"""Every global a strict desktop file's functions would create, found by reading.

    python3 tests/strictvars.py [prefix...]     # default: the whole desktop

`strict vars` refuses, at run time, an assignment in a function that would
create a global -- but only on the paths something actually runs, and a
refusal on one path hides whatever the next line would have been refused
for. This finds them all at once, from the parser's own record of every
variable a strict file's functions write (`inspect vars`): whatever one
assigns, binds, reads into, loops over, maps a file into, or writes in
arithmetic must be one of its parameters, declared local in it, written at
the top level of some desktop file, or one of the shell's own names.
Anything else is reported with its file and line, and fails the suite.

A name assigned only as a caller's local (which strict allows) would be
reported here, and none is -- keep it so. What `inspect vars` leaves out --
text another command runs later, a name reached through another variable's
value -- is out of reach here too; docs/inspect.md says what that is.
"""
from concurrent.futures import ThreadPoolExecutor
import os
import sys

from inspectlib import functions, records

HERE = os.path.dirname(os.path.abspath(__file__))
DESK = os.path.join(os.path.dirname(HERE), "programs", "desktop")
SHELL = {"RET", "M", "REPLY", "IFS", "OPTIND", "OPTARG", "RANDOM", "SECONDS",
         "PWD", "OLDPWD", "HOME", "PATH", "COLUMNS", "LINES", "LC_ALL", "_"}
DECL = {"local", "declare", "typeset", "export", "readonly", "nameref", "param"}
KEEP = {"unset", "prefix"}


def facts(p):
    """One file's write records, whether it says `strict`, and how many
    functions it defines."""
    writes = [v for v in records(p, "vars")["vars"]
              if v["access"] == "write" and v["form"] not in KEEP]
    strict = any(c["name"] == "strict" and not c["owner"]
                 for c in records(p, "calls")["calls"])
    return p, writes, strict, len(functions(p))


def read_all():
    """Every desktop file's facts, asked of the producer side by side."""
    paths = sorted(os.path.join(d, f) for d, _, fs in os.walk(DESK)
                   for f in fs if f.endswith(".hibr"))
    files, strict, funcs = {}, set(), {}
    with ThreadPoolExecutor(os.cpu_count() or 4) as pool:
        for p, writes, isstrict, n in pool.map(facts, paths):
            files[p], funcs[p] = writes, n
            if isstrict:
                strict.add(p)
    return files, strict, funcs


def main():
    want = [os.path.join(DESK, a) for a in sys.argv[1:]] or [DESK]
    files, strict, funcs = read_all()
    tops = {v["name"] for vs in files.values() for v in vs if not v["owner"]}
    found, checked = [], 0
    for p, vs in sorted(files.items()):
        if p not in strict or not any(p.startswith(w) for w in want):
            continue
        checked += funcs[p]
        for v in vs:
            if not v["owner"]:
                continue
            if (v["form"] in DECL or v["declared"] or v["name"] in tops
                    or v["name"] in SHELL):
                continue
            hit = "%s:%d: %s in %s creates a global" % (
                os.path.relpath(p, DESK), v["span"]["line"], v["name"], v["owner"])
            if hit not in found:
                found.append(hit)
    for f in found:
        print("FAIL " + f)
    print("\n%d passed, %d failed" % (checked - len(found), len(found)))
    sys.exit(1 if found else 0)


if __name__ == "__main__":
    main()
