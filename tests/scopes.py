#!/usr/bin/env python3
"""Every installable thing, in the scopes the catalogue actually puts it.

    python3 tests/scopes.py

The catalogue installs a desktop file into a person's own data folder, and
that folder is read *before* the shipped copy (ADR 0045). Nothing had ever
started a desktop that way: tests/appmgr.py installs files and checks they
landed, and every other suite runs the shipped tree, where apps/ and lib/
are siblings and a path built from a file's own location happens to be
right. So Calendar installed from the catalogue replaced a working app with
one that could not load, and the owner's desktop came up on
`source: .../apps/../lib/pim.hibr: No such file or directory` (Gitea #262).

This starts the desktop in each arrangement that can exist, and fails on
any shell error any of them prints -- naming which installable it was:

  * **platform only**: the core and the two frameworks every pane and
    every saver build on, and nothing else at all. The strictest case, and
    the answer to "what if nothing was installed?".
  * **as shipped**: what the package installs -- which since 0.99.194 is
    no applications, so this is every fresh install.
  * **each installable alone**, in an otherwise empty data folder over the
    platform: loaded, and then used -- an application launched, a pane
    opened in the Control Panel, a saver started -- because a missing
    dependency bites in a callback, not while a file is being read. This
    is what catches one installable that only works because another
    happened to be there.
  * **everything over the shipped desktop**: every installable copied into
    the data folder, shadowing the shipped copies, which is the
    arrangement the owner's own desktop was in.

What is installable is read from the tree and the catalogue's own kinds,
so a new application, pane, strip module or saver is covered with no edit
here. An application that uses the web module is loaded but not launched:
launching it starts Chromium and reaches the real network, which no suite
does unasked.
"""
import os
import re
import shutil
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import screen as sx
from screen import Term, check, report, load, tree

DESK = tree("programs/desktop")
MOD = tree("build/mods/console.so")
SRC = open(tree("programs/catalogue.hibr")).read()
KINDS = sorted(k for k, e in re.findall(
    r"\[([a-z-]+)\]=(\S+)",
    re.search(r"declare -gA AP_KEXT=\((.*?)\n\)", SRC, re.S).group(1))
    if e == ".hibr")
APP = re.compile(r"\bdt_app\s+([a-z][a-z0-9_]*)\b")
PANE = re.compile(r"\bcp_pane\s+([a-z][a-z0-9_]*)\b")
SAVER = re.compile(r"\bsv_saver\s+([a-z][a-z0-9_]*)\b")
D = tempfile.mkdtemp(prefix="hibr-scopes-")


def platform(path):
    for l in open(path, errors="replace"):
        if not l.startswith("#"):
            return False
        if l.startswith("# platform:"):
            return True
    return False


def installables():
    out = []
    for k in KINDS:
        base = os.path.join(DESK, k)
        for dp, _, fs in os.walk(base):
            for f in sorted(fs):
                if f.endswith(".hibr"):
                    p = os.path.join(dp, f)
                    out.append((k, os.path.relpath(p, base), p))
    return sorted(out)


ITEMS = installables()
PLAT = [(k, r, p) for k, r, p in ITEMS if platform(p)]
INST = [(k, r, p) for k, r, p in ITEMS if not platform(p)]

# The platform on its own, at the places the scans look: a folder per kind
# holding only the files that say `# platform:` of themselves.
PLATDIR = os.path.join(D, "platform")
for k, r, p in PLAT:
    os.makedirs(os.path.dirname(os.path.join(PLATDIR, k, r)), exist_ok=True)
    shutil.copy(p, os.path.join(PLATDIR, k, r))


def session(dirs, after=""):
    """A session that scans exactly `dirs` -- {kind: [folder, ...]} on top
    of the person's own scopes the window manager already lists -- and
    then runs `after` once the desktop is open."""
    s = [load(MOD), ". %s" % os.path.join(DESK, "desktop.hibr")]
    for k, var, scan in (("apps", "DT_APPDIRS", "dt_apps"),
                         ("system", "DT_SYSDIRS", "dt_sysapps"),
                         ("control-panel", "CP_PANEDIRS",
                          "command -v cp_panes > /dev/null && cp_panes"),
                         ("control-strip", "CS_MODDIRS", "cs_modules")):
        for d in dirs.get(k, []):
            s.append('%s+=("%s")' % (var, d))
        s.append(scan)
    s.append("dt_open || exit 1")
    if after:
        s.append(after)
    s.append("dt_run\ndt_close")
    return "\n".join(s) + "\n"


def start(name, text, data):
    """Run one session over a data folder of its own, and answer what it
    printed that was a shell error, and whether its bar was drawn."""
    home = os.path.join(D, "run-" + name)
    os.makedirs(home, exist_ok=True)
    path = os.path.join(home, "session.hibr")
    open(path, "w").write(text)
    before = len(sx.ERRS)
    t = Term(path, rows=40, cols=140, settle=1.5,
             env={"XDG_DATA_HOME": data,
                  "XDG_CONFIG_HOME": os.path.join(home, "config"),
                  "XDG_STATE_HOME": os.path.join(home, "state")})
    t.collect(0.8)
    sc = t.screen()
    bar = sc.row(0)
    t.close()
    return sx.ERRS[before:], "File" in bar or "Edit" in bar, sc


def place(data, k, r, p):
    """Put one installable where the catalogue would: <data>/hibr/<kind>/."""
    dst = os.path.join(data, "hibr", k, r)
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    shutil.copy(p, dst)


def use(path, k):
    """What to do with an installable once the desktop is open, so its
    callbacks run and not only its top level: launch an application, open
    the Control Panel on a pane, start a saver. A strip module is drawn on
    every frame already."""
    text = open(path, errors="replace").read()
    if k in ("apps", "system"):
        m = APP.search(text)
        if m and not re.search(r"\bneed web\b", text):
            return "dt_launch %s" % m.group(1)
    if k == "control-panel":
        m = PANE.search(text)
        if m:
            return ("id := dt_launch panel && dt_has panel_goto && "
                    "panel_goto \"$id\" %s" % m.group(1))
    if k == "savers":
        m = SAVER.search(text)
        if m:
            return "DT_SAVER=%s\ndt_saverstart preview" % m.group(1)
    return ""


PLATDIRS = {k: [os.path.join(PLATDIR, k)] for k in set(k for k, _, _ in PLAT)}
SHIPPED = {k: [os.path.join(DESK, k)] for k in KINDS if k != "apps"}

empty = os.path.join(D, "empty")
os.makedirs(empty)
errs, bar, sc = start("platform", session(PLATDIRS), empty)
check("a desktop with nothing installed but the platform starts, draws its "
      "bar and prints no error", bar and not errs, (errs[:3], sc))

errs, bar, sc = start("shipped", session(SHIPPED), empty)
check("a desktop as the package ships it -- no applications -- starts and "
      "prints no error", bar and not errs, (errs[:3], sc))

broken = []
for k, r, p in INST:
    data = os.path.join(D, "alone", k, r.replace("/", "_"))
    os.makedirs(data, exist_ok=True)
    place(data, k, r, p)
    errs, bar, sc = start("alone-%s-%s" % (k, r.replace("/", "_")),
                          session(PLATDIRS, use(p, k)), data)
    # A saver that is running owns the whole screen, so there is no bar
    # to find: that it drew something at all is what says it ran.
    if k == "savers":
        bar = any(sc.row(i).strip() for i in range(sc.rows))
    ok = bar and not errs
    if not ok:
        broken.append("%s/%s" % (k, r))
    check("%s/%s installed alone over the platform loads and runs with no "
          "error" % (k, r), ok, (errs[:3], "bar drawn" if bar else sc))

everything = os.path.join(D, "everything")
for k, r, p in INST:
    place(everything, k, r, p)
errs, bar, sc = start("everything", session(SHIPPED), everything)
check("every installable over the shipped desktop, shadowing its copies, "
      "starts and prints no error", bar and not errs, (errs[:3], sc))

print("%d installable files in %s, %d of them platform%s"
      % (len(ITEMS), " ".join(KINDS), len(PLAT),
         "; broken alone: " + " ".join(broken) if broken else ""))
shutil.rmtree(D, True)
report(len(INST) + 4)
