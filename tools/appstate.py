#!/usr/bin/env python3
"""The platform's own variables installables read or write (ADR 0050): every
global the platform declares -- wm/, the widgets, the `# platform:` files --
and, for each, how many installables read it and how many write it. What a
process of its own may still see is decided from this: a mirror the desktop
pushes, the app's own copy, or refused.

  python3 tools/appstate.py [--write] [--back] [--writes] [--shared]
      # --write puts docs/adr/0050-state.tsv and programs/desktop/mirrors.hibr;
      # --back lists what an installable reads back after changing it;
      # --writes the desktop names installables write today

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
MIRRORS = os.path.join(ROOT, "programs", "desktop", "mirrors.hibr")
# The doors an app's own process is sent through today; the rest are
# declared as their slices land.
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


# What the desktop shares with app processes is said once, in the file
# that owns the state: `dt_share [-s] [-o FIELD] NAME...` (wm/rtwire.hibr),
# and every kept setting, which dt_keep shares. Read here from those lines,
# never listed again.
def sharedset():
    """What the desktop shares, asked of the desktop itself once it has
    loaded -- DT_SHARED and DT_SHAREOWN, which dt_share and dt_keep fill --
    so there is one list and never a second."""
    import subprocess
    hibr = os.environ.get("HIBR", os.path.join(ROOT, "build", "hibr"))
    env = dict(os.environ, HIBR_MODPATH=os.path.join(ROOT, "build", "mods"))
    # The desktop, then every `# platform:` file a session loads beside it
    # (the saver framework, the Control Panel's own), as a session would.
    more = [f for f in platfiles() if not os.path.relpath(f, DESK).startswith(("wm/", "widgets/"))
            and os.path.basename(f) != "desktop.hibr"]
    out = subprocess.run([hibr, "-c", '. "$1"; shift; for f; do . "$f" 2> /dev/null; done; '
                          'for n in "${!DT_SHARED[@]}"; do '
                          'printf "%s\\t%s\\t%s\\n" "$n" "${DT_SHARED[$n]}" "${DT_SHAREOWN[$n]-}"; done',
                          "x", os.path.join(DESK, "desktop.hibr")] + more,
                         capture_output=True, text=True, env=env).stdout
    names, secret, owner = set(), set(), {}
    for line in out.split("\n"):
        f = line.split("\t")
        if len(f) < 3 or not f[0]:
            continue
        names.add(f[0])
        if f[1]:
            secret.add(f[0])
        if f[2]:
            owner[f[0]] = f[2]
    if not names:
        sys.exit("FAIL the desktop shared nothing (DT_SHARED came back empty): is build/hibr built?")
    return names, secret, owner


SHARED, SECRET, OWNER = sharedset()

# What an installable reads of the desktop and is not shared, each named
# with what it becomes; every one is converted by the slice named, and a
# name read that is neither shared nor here fails the survey.
#   call     moves with input or a frame: travels in the call that uses it
#            (the pointer, a menu's place, a drag, a key's phase)
#   answer   comes back as the answer of the asked request that set it
#   gone     the prototype's, removed with it
#   kept     the settings file's parse: the kept-settings slice
CONVERT = {}
for _n in """DT_PX DT_PY MB_CX MB_CY MB_CTXID MB_CTXWHO DT_DND DT_KEYBASE DT_KEYCODE DT_KEYMODS
        DT_KEYPHASE DT_PHASEWIN DT_NATFOCUSES DT_NATCH DT_NATCW CS_CLICKC DT_BARWCOL
        DT_POPH""".split():
    CONVERT[_n] = "call"
for _n in """DT_DLGR DT_DLGC DT_DLGH DT_DLGW DT_OWLIST DT_OVLIST CS_MOD_ORD DT_BTNL
        DT_BARREAD DTW_R DTW_C DTW_H DTW_W""".split():
    CONVERT[_n] = "answer"
for _n in "DT_HOSTMARK".split():
    CONVERT[_n] = "gone"
CONVERT["DT_CONFJ"] = "kept"
# The next note's number, bumped per note and read only by the pane that
# writes the note queue: part of that request, not shared.
BYNAME["DT_NOTE_NEXT"] = "request"
KINDS = ("apps", "system", "control-panel", "control-strip", "menubar", "savers")


def kindrel(f):
    """A file as the kind folder it is in sees it, apps/files.hibr, wherever
    the scope that holds it is: the same in this tree, the catalogue's
    bundles and an installed copy."""
    parts = os.path.abspath(f).split(os.sep)
    for i in range(len(parts) - 1, -1, -1):
        if parts[i] in KINDS:
            return "/".join(parts[i:])
    return os.path.basename(f)


def shared():
    out = []
    for f in platfiles():
        try:
            if any(l.startswith("# interface: shared") for l in open(f, errors="replace")):
                out.append(f)
        except OSError:
            pass
    return out


WRITE = r"(^|[\s;(&|])%s(\[[^\]]*\])*\+?=|\bunset\s+[\"']?%s\b|\b(json\s+parse|mapfile|readarray)\s+(-\S+\s+)*%s\b|\bread\s+[^\n]*-\w*a\w*\s+%s\b|\b%s(\[[^\]]*\])*\s+:="


def writers():
    """Every platform function that writes a shared name: (file, function,
    name), from the parser. A write at top level is the declaration, not one;
    a name the function declares local is its own."""
    import json, subprocess
    hibr = os.environ.get("HIBR", os.path.join(ROOT, "build", "hibr"))
    env = dict(os.environ, HIBR_MODPATH=os.path.join(ROOT, "build", "mods"))
    files = platfiles()
    out = subprocess.run([hibr, "-c", 'need inspect || exit 1; for f; do inspect vars "$f" -j; printf "\\n"; done',
                          "x"] + files, capture_output=True, text=True, env=env)
    docs = [l for l in out.stdout.split("\n") if l.strip()]
    if out.returncode or len(docs) != len(files):
        sys.exit("FAIL inspect vars answered %d of %d platform files" % (len(docs), len(files)))
    found = []
    for f, d in zip(files, docs):
        for v in json.loads(d).get("vars", []):
            if (v.get("access") == "write" and v.get("owner") and v.get("name") in SHARED
                    and v.get("declared") not in ("local", "param")):
                found.append((os.path.relpath(f, DESK), v["owner"], v["name"]))
    return sorted(set(found))


def mirrors(reads):
    """programs/desktop/mirrors.hibr: who reads each shared name, which is
    all the desktop needs besides the names themselves (dt_share): every
    other fact -- what changed, its shape, whose a window is, what is
    secret -- the variable table and the dt_share lines carry."""
    out = ["# mirrors -- who reads what the desktop shares with app processes (ADR",
           "# 0050): DT_RTAUD names, for each shared name, the files that read it as",
           "# the kind folder sees them (\"*\": the app interface or a library reads",
           "# it, so every process), and DT_RTKNOWN every file the inventory read, so",
           "# one it did not is sent every name but a secret. Generated by",
           "# tools/appstate.py --write; never edited by hand.",
           "# interface: shared",
           "",
           "declare -gA DT_RTAUD=(" + " ".join('[%s]="%s"' % (n, aud(n, reads)) for n in sorted(SHARED)) + ")",
           "declare -gA DT_RTKNOWN=(" + " ".join("[%s]=1" % k for k in sorted(set(kindrel(f) for f in installables()))) + ")",
           ""]
    return "\n".join(out)


def owners():
    """Each part (`# part: control-panel/x.hibr`) and the files that bring
    it: a bundle's members share one process, so the file it is launched
    from is sent what any of them reads."""
    out = collections.defaultdict(set)
    for f in installables():
        for line in open(f, errors="replace"):
            m = re.match(r"^# part:\s*(\S+)", line)
            if m:
                out[m.group(1)].add(kindrel(f))
    return out


OWNERS = None


def aud(n, reads):
    """Who is sent registry n: "*" when the app interface or a library any
    app may source reads it; else the files that read it and the files whose
    parts do."""
    global OWNERS
    libs = glob.glob(os.path.join(DESK, "lib", "*.hibr"))
    for f in shared() + libs:
        if re.search(r"\$\{?[!#]?%s\b|\b%s\[[^\]]*\]" % (n, n), open(f, errors="replace").read()):
            return "*"
    if OWNERS is None:
        OWNERS = owners()
    who = set(kindrel(f) for f in reads[n])
    for r in list(who):
        who |= OWNERS.get(r, set())
    return " ".join(sorted(who))


def klass(name, wr, where):
    """What a platform global an installable touches is for an app's own
    process: local (each process keeps its own), arg, gone, declare,
    request (an installable writes it today), shared (the desktop sends it),
    or a conversion still to come (CONVERT); `?` fails the survey."""
    if name in BYNAME:
        return BYNAME[name]
    if name in SHARED:
        return "shared"
    if where.startswith(LOCAL) or name in MODVARS:
        return "local"
    if where.startswith(DECLARE):
        return "declare"
    if wr:
        return "request"
    return CONVERT.get(name, "?")


def inventory(files, names):
    """Which installable reads and which writes each platform global, from the
    parser (`inspect vars`, ADR 0056): every access the shell itself would
    make, arithmetic included, by name. A name local to, or a parameter of,
    the function it appears in is that function's own, never the platform's;
    a name a file declares at its top level is still the platform's, since in
    the desktop's process the two are one variable. A name only passed by name
    (`dt_keep NAME`) is a registration, not an access."""
    import json, subprocess
    hibr = os.environ.get("HIBR", os.path.join(ROOT, "build", "hibr"))
    env = dict(os.environ, HIBR_MODPATH=os.path.join(ROOT, "build", "mods"))
    out = subprocess.run([hibr, "-c", 'need inspect || exit 1; for f; do inspect vars "$f" -j; printf "\\n"; done',
                          "x"] + files, capture_output=True, text=True, env=env)
    reads, writes = collections.defaultdict(set), collections.defaultdict(set)
    docs = [l for l in out.stdout.split("\n") if l.strip()]
    if out.returncode or len(docs) != len(files):
        sys.exit("FAIL inspect vars answered %d of %d installables (status %d): %s" % (
            len(docs), len(files), out.returncode, out.stderr.strip()[-300:]))
    for f, d in zip(files, docs):
        j = json.loads(d)
        if j.get("state") != "ok":
            sys.exit("FAIL inspect vars: %s: %s" % (f, j.get("state")))
        for v in j.get("vars", []):
            n = v.get("name")
            if n not in names or v.get("declared") in ("local", "param"):
                continue
            if v.get("access") == "read":
                reads[n].add(f)
            elif v.get("access") == "write":
                writes[n].add(f)
    return reads, writes


def main():
    names = globals_()
    files = installables()
    reads, writes = inventory(files, names)
    rows = sorted(((n, len(reads[n]), len(writes[n]), WHERE.get(n, "?"),
                    klass(n, writes[n], WHERE.get(n, "?"))) for n in names if reads[n] or writes[n]),
                  key=lambda r: (-(r[1] + r[2]), r[0]))
    print("installables: %d (read by inspect vars); platform globals: %d; touched by installables: %d (written by %d)" % (
        len(files), len(names), len(rows), sum(1 for r in rows if r[2])))
    kinds = collections.Counter(r[4] for r in rows)
    print("  " + ", ".join("%s %d" % kv for kv in sorted(kinds.items())))
    direct = sorted((n, " ".join(sorted(set(kindrel(f) for f in writes[n])))) for n, r, w, wh, k in rows
                    if w and (k in ("request", "shared")))
    # A desktop name an installable writes is a request to convert: a
    # setting a pane changes goes by the kept-settings slice, the rest by
    # the request conversions; in an app's own process the write is refused.
    print("  written by an installable, each a request to convert: %d (--writes lists them)" % len(direct))
    if "--writes" in sys.argv:
        for n, who in direct:
            print("    %-22s %s" % (n, who[:90]))
    if "--write" in sys.argv:
        with open(OUT, "w") as o:
            o.write("# name\tread in\twritten in\tdeclared in\tclass -- tools/appstate.py --write (the parser's inventory, inspect vars)\n")
            for row in rows:
                o.write("%s\t%d\t%d\t%s\t%s\n" % row)
        print("wrote", os.path.relpath(OUT, ROOT))
    mtext = mirrors(reads)
    if "--write" in sys.argv:
        open(MIRRORS, "w").write(mtext)
        print("wrote", os.path.relpath(MIRRORS, ROOT))
    elif os.path.isdir(APPS) and (not os.path.exists(MIRRORS) or open(MIRRORS).read() != mtext):
        print("FAIL programs/desktop/mirrors.hibr is not what the survey makes: tools/appstate.py --write")
        sys.exit(1)
    fail = 0
    # Every name an installable reads of the desktop is shared, the app's
    # own, or a conversion named in CONVERT.
    for n, r, w, wh, k in rows:
        if k == "?":
            print("FAIL %s is the desktop's (%s) and read by %d installable(s), and neither shared (dt_share) nor in CONVERT" % (n, wh, r))
            fail = 1
    # A shared name is a platform global; a conversion is still read, where
    # every installable was read (a run without the catalogue sees less).
    for n in sorted(SHARED):
        if n not in names:
            print("FAIL %s is shared (dt_share) and the platform declares no such global" % n)
            fail = 1
    if os.path.isdir(APPS):
        for n in sorted(CONVERT):
            if not any(row[0] == n and row[4] == CONVERT[n] for row in rows):
                print("FAIL %s is in CONVERT and no installable reads it: take it out" % n)
                fail = 1
    # What an installable reads back after changing it through the desktop:
    # in a process of its own that call is an asked request, answered only
    # after the change has been sent. The asked-requests slice works from
    # this list.
    wr = collections.defaultdict(set)
    for f, fn, n in writers():
        wr[n].add(fn)
    back = set()
    for f in files:
        t = open(f, errors="replace").read()
        for n, fns in wr.items():
            if f not in reads.get(n, ()):
                continue
            for fn in fns:
                if re.search(r"(^|[\s;&|(])%s\b" % fn, t, re.M):
                    back.add((kindrel(f), n, fn))
    print("  read back after a change through the desktop: %d (an installable, a name, the call)" % len(back))
    # Shared code writes a shared name in whichever process calls it, and in
    # an app's process the guard refuses it: each is a request to convert
    # (the desktop's function, asked for) before it runs there.
    sh = set(os.path.relpath(f, DESK) for f in shared())
    inshared = sorted(set((f, fn, n) for f, fn, n in writers() if f in sh))
    print("  shared code writing a shared name, refused in an app's process: %d (--shared lists them)" % len(inshared))
    if "--shared" in sys.argv:
        for x in inshared:
            print("    %-26s %-22s %s" % x)
    if "--back" in sys.argv:
        for b in sorted(back):
            print("    %-34s %-14s %s" % b)
    sys.exit(fail)


if __name__ == "__main__":
    main()
