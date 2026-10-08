#!/usr/bin/env python3
"""Run some of tests/apps.py's sections on their own (Gitea #101).

    python3 tests/apps_panel.py [path-to-hibr]     # and apps_reach, apps_core, apps_more

tests/apps.py is one suite of about 550 checks that took twelve minutes,
mostly waiting on the desktop it drives -- the longest pole in tests/all.py.
Its sections are independent once the helpers and the values they share
are made, so this reads apps.py, keeps the statements before its first
section, the sections a part names and, from the other sections, only the
definitions those need, and runs them in apps.py's own order with apps.py's
own line numbers. apps.py stays the one place a check is written, and
still runs whole.

A statement taken from another section is a function, a class, an import
or an assignment that runs nothing: an assignment that starts a session,
or reads what one produced, belongs to its own section and is never copied.
A section no part names runs in "more", so a new one cannot go missing.
PLAN holds each part's checks; together they are apps.py's own plan, less
the shell-error check every part adds once, and that is checked every run.
"""
import ast
import builtins
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "apps.py")
PARTS = {
    "panel": ["the control panel"],
    "reach": ["what nothing reached before 0.56"],
    "core": ["the calculator", "the file browser", "the desk accessories",
             "the terminal window", "glyphs", "what a frame redraws"],
    "more": None,
}
PLAN = {"panel": 230, "reach": 118, "core": 106, "more": 118}
SLOW = {"run", "cprun", "wrrun", "shrun", "check", "Term", "report", "run_img"}


def uses(node):
    """The names a statement reads."""
    return {n.id for n in ast.walk(node) if isinstance(n, ast.Name) and isinstance(n.ctx, ast.Load)}


def defs(node):
    """The names a statement binds at the top level."""
    if isinstance(node, (ast.FunctionDef, ast.ClassDef)):
        return {node.name}
    if isinstance(node, (ast.Import, ast.ImportFrom)):
        return {(a.asname or a.name).split(".")[0] for a in node.names}
    return {n.id for n in ast.walk(node) if isinstance(n, ast.Name) and isinstance(n.ctx, ast.Store)}


def slow(node):
    """Whether a statement starts a session or checks something."""
    if isinstance(node, (ast.FunctionDef, ast.ClassDef)):
        return False
    return any(isinstance(n, ast.Call) and isinstance(n.func, ast.Name) and n.func.id in SLOW
               for n in ast.walk(node))


def sections(lines):
    """Each section's title and its line range, 1-based and inclusive."""
    at = [(i + 1, re.sub(r"^# --- (.*?) -*$", r"\1", l)) for i, l in enumerate(lines) if l.startswith("# --- ")]
    return [(t, s, at[k + 1][0] - 1 if k + 1 < len(at) else len(lines)) for k, (s, t) in enumerate(at)]


def plan(src):
    """The N of apps.py's last report(N)."""
    tail = ast.parse(src).body[-1]
    assert isinstance(tail, ast.Expr) and isinstance(tail.value, ast.Call) and tail.value.func.id == "report"
    return tail.value.args[0].value


def pick(src, part):
    """The statements a part runs, in apps.py's order, and whether it ends apps.py."""
    lines = src.split("\n")
    secs = sections(lines)
    titles = [t for t, _, _ in secs]
    named = [t for p in PARTS.values() if p for t in p]
    unknown = [t for t in named if t not in titles]
    if unknown:
        sys.exit("appslice: no section in apps.py is called %s" % ", ".join(unknown))
    want = PARTS[part] if PARTS[part] is not None else [t for t in titles if t not in named]
    own = {titles.index(t) for t in want}
    body = ast.parse(src).body[:-1]

    def sec(st):
        return next((k for k, (_, s, e) in enumerate(secs) if s < st.lineno <= e), -1)

    tagged = [(st, sec(st)) for st in body]
    head = set().union(*[defs(st) for st, k in tagged if k < 0])
    made = set()
    for st, k in tagged:
        if k >= 0 and not isinstance(st, (ast.FunctionDef, ast.ClassDef)) and (slow(st) or uses(st) & made):
            made |= defs(st)
    mine = [st for st, k in tagged if k in own]
    todo = list(set().union(*[uses(st) for st in mine]) - head - set(dir(builtins)))
    seen, take = set(), set()
    while todo:
        nm = todo.pop()
        if nm in seen:
            continue
        seen.add(nm)
        for i, (st, k) in enumerate(tagged):
            if (k >= 0 and k not in own and nm in defs(st) and not slow(st)
                    and isinstance(st, (ast.FunctionDef, ast.ClassDef, ast.Assign, ast.Import, ast.ImportFrom))
                    and (isinstance(st, (ast.FunctionDef, ast.ClassDef)) or not uses(st) & made)
                    and i not in take):
                take.add(i)
                todo += list(uses(st) - head - set(dir(builtins)))
    keep = [st for i, (st, k) in enumerate(tagged) if k < 0 or k in own or i in take]
    return keep, len(secs) - 1 in own


def run(part):
    """Run one part of apps.py, as __main__, with its own plan."""
    src = open(SRC).read()
    total = sum(PLAN.values()) - (len(PLAN) - 1)
    if total != plan(src):
        sys.exit("appslice: PLAN adds up to %d checks and apps.py plans %d -- a check was added to one"
                 " and not the other" % (total, plan(src)))
    keep, last = pick(src, part)
    tail = "report(%d)\n" % PLAN[part]
    if not last:
        tail = "shutil.rmtree(D, True)\nshutil.rmtree(S, True)\n" + tail
    end = ast.parse(tail).body
    for st in end:
        ast.increment_lineno(st, len(src.split("\n")))
    code = compile(ast.Module(body=keep + end, type_ignores=[]), SRC, "exec")
    sys.path.insert(0, HERE)
    exec(code, {"__name__": "__main__", "__file__": SRC, "__builtins__": builtins})
