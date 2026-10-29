#!/usr/bin/env python3
"""Run some of a long suite's sections on their own (Gitea #101).

    python3 tests/apps_panel.py [path-to-hibr]     # and apps_reach, apps_core, apps_more
    python3 tests/desktop_basics.py [path-to-hibr] # and the other desktop_ parts

What is said below of tests/apps.py is true of tests/desktop.py too: each is
cut the same way, by SUITES, so a part is always one suite's own sections.

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
# Each suite cut into parts: its file, the sections each part runs (None for
# every section no other part names), each part's plan, and what the file
# would have cleaned up at its end, for a part that does not end it.
SUITES = {
    "apps": {
        "src": "apps.py",
        "parts": {
            "panel": ["the control panel"],
            "reach": ["what nothing reached before 0.56"],
            "office": ["Sheet"],
            "write": ["Write and the file dialog"],
            "core": ["the calculator", "the file browser", "the desk accessories",
                     "the terminal window", "glyphs", "what a frame redraws"],
            "more": None,
        },
        "plan": {"panel": 329, "reach": 75, "office": 30, "write": 39, "core": 128, "more": 172},
        "tail": "shutil.rmtree(D, True)\nshutil.rmtree(S, True)\n",
    },
    "desktop": {
        "src": "desktop.py",
        "parts": {
            "basics": ["the window manager's basics",
                       "window chrome: frame, button side, title alignment, icon style",
                       "widgets", "right-click context menus", "the menu bar"],
            "windows": ["resizing", "the window menu, and items that cannot be chosen",
                        "submenus", "moving and resizing from the keyboard",
                        "where a new window goes", "snapping", "workspaces", "tiling",
                        "icons on the desktop",
                        "breaks, saved settings, and a session that outlives its terminal",
                        "the hibr menu comes from folders of apps", "the control strip",
                        "the screen saver", "the screen lock", "the login screen",
                        "recovering"],
            "pictures": ["the wallpaper and the image viewer", "the wallpaper as pixels",
                         "a session older than the desktop inside it (Gitea #174)"],
            "session": None,
        },
        "plan": {"basics": 220, "windows": 245, "pictures": 199, "session": 222},
        "tail": "",
    },
}
SRC = os.path.join(HERE, "apps.py")
PARTS = SUITES["apps"]["parts"]
PLAN = SUITES["apps"]["plan"]
SLOW = {"run", "cprun", "wrrun", "shrun", "check", "Term", "report", "run_img"}


def uses(node):
    """The names a statement reads from outside itself: a function's own
    parameters and locals, and a comprehension's own variable, are not --
    taken for free names, a helper's local `face` fetched the login
    section's top-level `face` and every statement that set it up."""
    loads = {n.id for n in ast.walk(node) if isinstance(n, ast.Name) and isinstance(n.ctx, ast.Load)}
    local = set()
    for n in ast.walk(node):
        if isinstance(n, (ast.FunctionDef, ast.AsyncFunctionDef, ast.Lambda)):
            a = n.args
            local |= {x.arg for x in a.args + a.kwonlyargs + getattr(a, "posonlyargs", [])}
            if a.vararg:
                local.add(a.vararg.arg)
            if a.kwarg:
                local.add(a.kwarg.arg)
            body = n.body if isinstance(n.body, list) else [n.body]
            for b in body:
                local |= {m.id for m in ast.walk(b) if isinstance(m, ast.Name) and isinstance(m.ctx, ast.Store)}
        elif isinstance(n, ast.comprehension):
            local |= {m.id for m in ast.walk(n.target) if isinstance(m, ast.Name)}
    return loads - local


def defs(node):
    """The names a statement binds at the top level."""
    if isinstance(node, (ast.FunctionDef, ast.ClassDef)):
        return {node.name}
    if isinstance(node, (ast.Import, ast.ImportFrom)):
        return {(a.asname or a.name).split(".")[0] for a in node.names}
    return {n.id for n in ast.walk(node) if isinstance(n, ast.Name) and isinstance(n.ctx, ast.Store)}


def slow(node, names=None):
    """Whether a statement starts a session or checks something: calls one
    of SLOW, or a function of the suite's own that does (names)."""
    if isinstance(node, (ast.FunctionDef, ast.ClassDef)):
        return False
    names = SLOW if names is None else names
    return any(isinstance(n, ast.Call) and isinstance(n.func, ast.Name) and n.func.id in names
               for n in ast.walk(node))



def slownames(body):
    """SLOW and every function of the suite's own that reaches one of them,
    however indirectly -- desktop.py's swopen, desk and shots start sessions
    as surely as run does, and a statement calling them must not be copied
    into another part."""
    names = set(SLOW)
    fns = [st for st in body if isinstance(st, ast.FunctionDef)]
    grew = True
    while grew:
        grew = False
        for f in fns:
            if f.name not in names and any(
                    isinstance(n, ast.Call) and isinstance(n.func, ast.Name) and n.func.id in names
                    for n in ast.walk(f)):
                names.add(f.name)
                grew = True
    return names


def sections(lines):
    """Each section's title and its line range, 1-based and inclusive."""
    at = [(i + 1, re.sub(r"^# --- (.*?) -*$", r"\1", l)) for i, l in enumerate(lines) if l.startswith("# --- ")]
    return [(t, s, at[k + 1][0] - 1 if k + 1 < len(at) else len(lines)) for k, (s, t) in enumerate(at)]


def plan(src):
    """The N of apps.py's last report(N)."""
    tail = ast.parse(src).body[-1]
    assert isinstance(tail, ast.Expr) and isinstance(tail.value, ast.Call) and tail.value.func.id == "report"
    return tail.value.args[0].value


def pick(src, part, parts=None):
    """The statements a part runs, in the suite's order, and whether it ends it."""
    PARTS = parts if parts is not None else SUITES["apps"]["parts"]
    lines = src.split("\n")
    secs = sections(lines)
    titles = [t for t, _, _ in secs]
    named = [t for p in PARTS.values() if p for t in p]
    unknown = [t for t in named if t not in titles]
    if unknown:
        sys.exit("appslice: no section in the suite is called %s" % ", ".join(unknown))
    want = PARTS[part] if PARTS[part] is not None else [t for t in titles if t not in named]
    own = {titles.index(t) for t in want}
    body = ast.parse(src).body[:-1]

    def sec(st):
        return next((k for k, (_, s, e) in enumerate(secs) if s < st.lineno <= e), -1)

    tagged = [(st, sec(st)) for st in body]
    head = set().union(*[defs(st) for st, k in tagged if k < 0])
    sn = slownames(body)
    made = set()
    for st, k in tagged:
        if k >= 0 and not isinstance(st, (ast.FunctionDef, ast.ClassDef)) and (slow(st, sn) or uses(st) & made):
            made |= defs(st)
    mine = [st for st, k in tagged if k in own]
    # Each name a statement needs is the definition Python itself would have
    # used: the nearest one before that statement. Taking every statement
    # that assigns a common name such as `out` pulled in whatever each of
    # them read, from all over the file.
    skip = head | set(dir(builtins))
    todo = [(nm, i) for i, (st, k) in enumerate(tagged) if k in own
            for nm in uses(st) - skip]
    seen, take = set(), set()
    while todo:
        nm, at = todo.pop()
        if (nm, at) in seen:
            continue
        seen.add((nm, at))
        for i in range(at - 1, -1, -1):
            st, k = tagged[i]
            if nm not in defs(st):
                continue
            if (k >= 0 and k not in own and not slow(st, sn)
                    and isinstance(st, (ast.FunctionDef, ast.ClassDef, ast.Assign, ast.Import, ast.ImportFrom))
                    and (isinstance(st, (ast.FunctionDef, ast.ClassDef)) or not uses(st) & made)
                    and i not in take):
                take.add(i)
                todo += [(n2, i) for n2 in uses(st) - skip]
            break
        else:
            # A function body is run later than it is written: a name it reads
            # may be defined after it, so look forward as well.
            for i in range(at + 1, len(tagged)):
                st, k = tagged[i]
                if nm in defs(st):
                    if (k >= 0 and k not in own and not slow(st, sn)
                            and isinstance(st, (ast.FunctionDef, ast.ClassDef, ast.Assign, ast.Import, ast.ImportFrom))
                            and (isinstance(st, (ast.FunctionDef, ast.ClassDef)) or not uses(st) & made)
                            and i not in take):
                        take.add(i)
                        todo += [(n2, i) for n2 in uses(st) - skip]
                    break
    # A name taken from another section must be whole once its assignment
    # has run. A loop that fills a folder the assignment made, or a call that
    # writes into it, cannot be copied with it, and the part would run with
    # the folder empty -- which reads as the thing under test being broken, a
    # long way from the cause. So a section that sets a taken name up that
    # way is refused here, by name: make it in one function instead.
    for i in take:
        st, k = tagged[i]
        if isinstance(st, (ast.FunctionDef, ast.ClassDef, ast.Import, ast.ImportFrom)):
            continue
        dn = defs(st)
        # Setup is what follows the assignment before the section's first
        # session: a use of the name after a session has run is a use.
        for j in range(i + 1, len(tagged)):
            o, ok = tagged[j]
            if ok != k or slow(o, sn):
                break
            if (j not in take and
                    isinstance(o, (ast.Expr, ast.For, ast.While, ast.With, ast.If, ast.AugAssign))
                    and uses(o) & dn):
                sys.exit("appslice: %s line %d sets up %s, which a part takes from that section, with"
                         " a statement a part cannot copy (line %d): make it in one function"
                         % (secs[k][0], st.lineno, ", ".join(sorted(uses(o) & dn)), o.lineno))
    keep = [st for i, (st, k) in enumerate(tagged) if k < 0 or k in own or i in take]
    return keep, len(secs) - 1 in own


def run(part, suite="apps"):
    """Run one part of a suite, as __main__, with its own plan."""
    cf = SUITES[suite]
    path = os.path.join(HERE, cf["src"])
    plans = cf["plan"]
    src = open(path).read()
    total = sum(plans.values()) - (len(plans) - 1)
    if total != plan(src):
        sys.exit("appslice: %s's parts add up to %d checks and %s plans %d -- a check was added"
                 " to one and not the other" % (suite, total, cf["src"], plan(src)))
    keep, last = pick(src, part, cf["parts"])
    tail = "report(%d)\n" % plans[part]
    if not last:
        tail = cf["tail"] + tail
    end = ast.parse(tail).body
    for st in end:
        ast.increment_lineno(st, len(src.split("\n")))
    code = compile(ast.Module(body=keep + end, type_ignores=[]), path, "exec")
    sys.path.insert(0, HERE)
    exec(code, {"__name__": "__main__", "__file__": SRC, "__builtins__": builtins})
