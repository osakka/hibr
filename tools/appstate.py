#!/usr/bin/env python3
"""The platform's own variables installables read or write (ADR 0050): every
global the platform declares -- wm/, the widgets, the `# platform:` files --
and, for each, how many installables read it and how many write it. What a
process of its own may still see is decided from this: a mirror the desktop
pushes, the app's own copy, or refused.

  python3 tools/appstate.py [--write]   # --write puts docs/adr/0050-state.tsv

Read from the text, not run: a name counts as written where it is assigned
(NAME=, NAME[...]=, NAME+=, declare/read/unset of it) and as read where it is
expanded (${NAME, $NAME, ${!NAME, ${#NAME, or NAME[...] read in arithmetic);
a top-level `declare -A` counts as the platform's, `-g` or not. Approximate by design -- a word, not a parse -- and
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
DECLG = re.compile(r"^(?:declare|typeset)\s+-\w+\s+(.*)")


WHERE = {}


def globals_():
    names = set()
    for f in platfiles():
        rel = os.path.relpath(f, DESK)
        for line in open(f, errors="replace"):
            if line.startswith("\t") or line.startswith("  "):
                continue
            m = DECLG.match(line)
            if m:
                for w in m.group(1).split():
                    w = w.split("=")[0]
                    if re.match(r"^[A-Z][A-Z0-9_]*$", w):
                        names.add(w)
                        WHERE.setdefault(w, rel)
                continue
            m = DECL.match(line)
            if m:
                names.add(m.group(1))
                WHERE.setdefault(m.group(1), rel)
    return names


# What a global becomes in an app's own process, by where it is declared:
# local (the app runtime keeps its own: widgets, translation, glyphs, jobs),
# setting (a desktop setting: pushed to read, changed by a request the
# desktop accepts only against a pane's declared rows), declare (built in
# the app and pushed up), mirror (desktop state, pushed, read only) or
# request (desktop state an app writes today: a request, or refused).
LOCAL = ("widgets/", "wm/lang.hibr", "wm/locale.hibr", "wm/glyphs.hibr",
         "wm/jobs.hibr", "wm/remote.hibr")
SETTING = ("wm/settings.hibr",)
DECLARE = ("system/panel.hibr",)


def kept():
    """The desktop's saved settings, as the desktop itself lists them."""
    import subprocess
    hibr = os.environ.get("HIBR", os.path.join(ROOT, "build", "hibr"))
    env = dict(os.environ, HIBR_MODPATH=os.path.join(ROOT, "build", "mods"))
    out = subprocess.run([hibr, "-c", '. "$1"; printf "%s\\n" "${DT_KEEP[@]}"', "x",
                          os.path.join(DESK, "desktop.hibr")],
                         capture_output=True, text=True, env=env).stdout
    return set(out.split())


KEPT = kept()
if not KEPT:
    sys.exit("FAIL the desktop's own list of saved settings (DT_KEEP) came back empty, "
             "so every setting would be classed as desktop state: is build/hibr built?")
# A module's own status variable, set in whichever process called it.
MODVARS = ("DAV_CODE",)


# Desktop state an app writes today, one name at a time: a value handed to
# a request through a global becomes the request's argument (arg); a hint
# to redraw goes, since a declaration or setting arriving is the redraw
# (gone); the input method's own state is the app's (local).
BYNAME = {}
for _n in """DT_OPENCMD DT_OPENSELF DT_OPENSHELL DT_CLIPKIND DT_CLIP DT_CLIPOP
        DT_NOTEFROM DT_NOTEWIN DT_NOTEAPP""".split():
    BYNAME[_n] = "arg"
for _n in "DT_FORCEONCE DT_MENUIN DT_BARWAS DT_ISON".split():
    BYNAME[_n] = "gone"
# The input method's engine runs in every app process (owner, 2026-11-18):
# what it holds is each process's; the teach box and the chords told to
# Blit (DT_IMET*, DT_IMEKSENT) stay the desktop's.
for _n in "DT_IMEPRE DT_IMEBY DT_IMEC DT_IMEP DT_IMEAT DT_IMEWIN DT_IMEH DT_IMEMOD DT_IMEBAD DT_IMEM".split():
    BYNAME[_n] = "local"
# A helper's answer handed back through a global (a watcher's descriptors
# and lines, a reason, an image's arguments), and a process's own cache or
# readings: each process keeps its own.
for _n in "DT_APPNAMEV DT_WHY DT_IMGARG DT_WLINES DT_WFD DT_WPTY DT_BUSFD DT_PW DT_PKEY".split():
    BYNAME[_n] = "local"


def klass(name, wr, where):
    if name in BYNAME:
        return BYNAME[name]
    if where.startswith(LOCAL) or name in MODVARS:
        return "local"
    if where.startswith(SETTING) or name in KEPT:
        return "setting"
    if where.startswith(DECLARE):
        return "declare"
    return "request" if wr else "mirror"


def main():
    names = globals_()
    reads, writes = collections.defaultdict(set), collections.defaultdict(set)
    files = installables()
    for f in files:
        text = open(f, errors="replace").read()
        own = set(re.findall(r"^(?:declare|typeset)\s+-\w+\s+([A-Z][A-Z0-9_]*)", text, re.M))
        for n in names:
            if n in own or n not in text:
                continue
            if re.search(r"\$\{?[!#]?%s\b|\b%s\[[^\]]*\](?!\S*\+?=)" % (n, n), text):
                reads[n].add(f)
            if re.search(r"(^|[\s;(&|])%s(\[[^\]]*\])*\+?=|\b(unset|read\b[^\n]*)\s+%s\b" % (n, n), text, re.M):
                writes[n].add(f)
    rows = sorted(((n, len(reads[n]), len(writes[n]), WHERE.get(n, "?"),
                    klass(n, writes[n], WHERE.get(n, "?"))) for n in names if reads[n] or writes[n]),
                  key=lambda r: (-(r[1] + r[2]), r[0]))
    print("installables: %d; platform globals: %d; touched by installables: %d (written by %d)" % (
        len(files), len(names), len(rows), sum(1 for r in rows if r[2])))
    kinds = collections.Counter(r[4] for r in rows)
    print("  " + ", ".join("%s %d" % (k, kinds[k]) for k in ("local", "setting", "declare", "mirror", "arg", "gone", "request")))
    for n, r, w, wh, k in rows:
        if k == "request":
            print("  request  %-22s written in %3d  %-22s %s" % (n, w, wh,
                  " ".join(sorted(os.path.basename(f) for f in writes[n]))[:60]))
    if "--write" in sys.argv:
        with open(OUT, "w") as o:
            o.write("# name\tread in\twritten in\tdeclared in\tclass -- tools/appstate.py --write (a word match, not a parse)\n")
            for row in rows:
                o.write("%s\t%d\t%d\t%s\t%s\n" % row)
        print("wrote", os.path.relpath(OUT, ROOT))


main()
