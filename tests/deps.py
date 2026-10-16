#!/usr/bin/env python3
"""What an installable file may reach outside itself, checked by reading.

    python3 tests/deps.py

The catalogue installs a desktop file into any of four scopes (ADR 0045),
and the user scope is read before the shipped copy -- so a file that only
works where the shipped tree put it breaks the moment somebody installs it.
That is how Calendar from the catalogue replaced a working app with one
that could not load, and the owner's desktop came up on
`source: .../apps/../lib/pim.hibr: No such file or directory` (Gitea #262).

So everything an installable file reaches outside itself has to be
something that is there in every scope. Three rules, each a check here:

1. **No path from its own location.** `${BASH_SOURCE%/*}/..` is right in
   the shipped tree, where apps/ and lib/ are siblings, and wrong in every
   other scope. What the desktop ships is reached through `$DT_HERE`;
   a file's own resources through `${DT_SRC%.hibr}/`.

2. **A library it sources is one the desktop loads.** A library marks
   itself with a `*_LIBDONE=1` line and `dt_libs` loads every such file
   from the shipped lib/ before any app, so an app's own guarded source is
   never reached. A sourced file without the mark would be loaded by
   nobody but the app.

3. **Another installable only behind a guard in the same function** --
   `dt_has`, `dt_needs` or `command -v` -- unless the callee is part of the
   platform, which a file says of itself with a `# platform:` line in its
   header (the Control Panel, every pane's framework; the saver
   framework). That is what keeps a link between two apps an optional
   integration rather than a dependency nothing resolves: a person can
   install or remove either, and the other does less rather than breaking.

Which kinds are installable is read from the catalogue's own CAT_KEXT --
every kind whose files end in .hibr -- so a kind added later is covered
without an edit here. It reads, it does not parse: a function begins at a
`name(` in column 0 and a guard covers the calls after it in that
function, which is how every file in the desktop is written. A guard that
names any function of a file covers calls to every function of that file,
since it has asked whether that file is loaded.
"""
import glob
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
DESK = os.path.join(ROOT, "programs", "desktop")

DEF = re.compile(r"^(?:fn\s+)?([a-z][a-z0-9]*_[a-z0-9_]+)\s*\(")
TOK = re.compile(r"\b([a-z][a-z0-9]*_[a-z0-9_]+)\b")
GUARD = re.compile(r'(?:command -v|dt_has|dt_needs [a-z]+)\s+"?\$?\{?([a-z][a-z0-9_]*)')
OWNPATH = re.compile(r"\$\{BASH_SOURCE%/\*\}/\.\.|\$\{0%/\*\}")
LIBSRC = re.compile(r'(?:^|[\s;&|])\.\s+"\$DT_HERE/lib/([a-z][a-z0-9]*)\.hibr"')
MARK = re.compile(r"^[A-Z][A-Z0-9]*_LIBDONE=1$")


def kinds():
    """The script kinds, from the catalogue's own table of extensions."""
    src = open(os.path.join(ROOT, "programs", "catalogue.hibr")).read()
    m = re.search(r"declare -gA CAT_KEXT=\((.*?)\n\)", src, re.S)
    return sorted(k for k, e in re.findall(r"\[([a-z-]+)\]=(\S+)", m.group(1))
                  if e == ".hibr")


def code(line):
    """A line with its comment taken off, or empty for a comment line."""
    if line.lstrip().startswith("#"):
        return ""
    return line.split(" #", 1)[0]


def defs(path):
    return {m.group(1) for l in open(path, errors="replace")
            for m in [DEF.match(l)] if m}


def platform(path):
    """Whether a file says in its header that it is part of the desktop."""
    for l in open(path, errors="replace"):
        if not l.startswith("#"):
            return False
        if l.startswith("# platform:"):
            return True
    return False


def main():
    K = kinds()
    inst = sorted(f for k in K
                  for f in glob.glob(os.path.join(DESK, k, "**", "*.hibr"),
                                     recursive=True))
    core = [f for f in glob.glob(os.path.join(DESK, "wm", "*.hibr")) +
            glob.glob(os.path.join(DESK, "widgets", "*.hibr")) +
            glob.glob(os.path.join(DESK, "lib", "*.hibr")) +
            [os.path.join(DESK, "desktop.hibr"),
             os.path.join(DESK, "session.hibr")]]
    plat = [f for f in inst if platform(f)]
    given = set().union(*[defs(f) for f in core + plat])
    owner = {}
    for f in inst:
        if f in plat:
            continue
        for d in defs(f) - given:
            owner.setdefault(d, set()).add(f)
    libs = {os.path.basename(f)[:-5] for f in glob.glob(os.path.join(DESK, "lib", "*.hibr"))
            if any(MARK.match(l.rstrip("\n")) for l in open(f, errors="replace"))}
    rel = lambda p: os.path.relpath(p, DESK)
    fails = []
    checked = 0
    for g in inst:
        guards = set()
        for n, line in enumerate(open(g, errors="replace"), 1):
            if DEF.match(line):
                guards = set()
                continue
            s = code(line)
            if not s.strip():
                continue
            checked += 1
            if OWNPATH.search(s):
                fails.append("%s:%d: builds a path from its own location, which "
                             "is only right in the shipped tree -- use $DT_HERE"
                             % (rel(g), n))
            for lib in LIBSRC.findall(s):
                if lib not in libs:
                    fails.append("%s:%d: sources lib/%s.hibr, which has no "
                                 "*_LIBDONE=1 line, so the desktop does not load it"
                                 % (rel(g), n, lib))
            for gm in GUARD.finditer(s):
                guards.add(gm.group(1))
            covered = set().union(*[owner.get(x, set()) for x in guards]) if guards else set()
            inline = ("command -v" in s or "dt_has" in s or "dt_needs" in s)
            for tok in sorted(set(TOK.findall(s))):
                fs = owner.get(tok)
                if not fs or g in fs or inline or fs & covered:
                    continue
                fails.append("%s:%d: calls %s, from %s, with no guard in this "
                             "function -- dt_has, dt_needs or command -v"
                             % (rel(g), n, tok, ", ".join(sorted(rel(x) for x in fs))))
    for f in fails:
        print("FAIL " + f)
    print("%d installable files in %s, %d of them platform; %d lines read"
          % (len(inst), " ".join(K), len(plat), checked))
    print("\n%d passed, %d failed" % (checked - len(fails), len(fails)))
    sys.exit(1 if fails else 0)


if __name__ == "__main__":
    main()
