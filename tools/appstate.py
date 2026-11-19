#!/usr/bin/env python3
"""The platform's own variables installables read or write (ADR 0050): every
global the platform declares -- wm/, the widgets, the `# platform:` files --
and, for each, how many installables read it and how many write it. What a
process of its own may still see is decided from this: a mirror the desktop
pushes, the app's own copy, or refused.

  python3 tools/appstate.py [--write] [--back]
      # --write puts docs/adr/0050-state.tsv and programs/desktop/mirrors.hibr;
      # --back lists what an installable reads back after changing it

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
SENT = ("save", "hello", "hot", "window", "registry")
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
# The desktop's language follows the system's locale (wm/locale, Gitea
# #526): worked out by the desktop, not kept, and an app must follow it.
BYNAME["DT_LANG"] = "mirror"


# How each mirror reaches an app's own process (ADR 0050, "mirrors"): each
# door is a place the desktop already changes the value through, so nothing
# new hooks the writers. A setting's door is `save` (dt_save walks every kept
# setting on each change). A mirror with no door fails the survey.
#   hot       one string compared each pass while an app is connected
#   registry  sent when it is registered, unregistered or rescanned
#   hello     a constant of this desktop, sent once at the hello
#   call      moves with input or a frame: travels in the call that uses it,
#             never a mirror (the pointer, a menu's place, a drag, a key's
#             phase, a cell's pixels)
#   answer    comes back as the answer of the asked request that set it
#   gone      the prototype's, removed with it
#   window    the window table: an app's own windows' records, sent when one
#             is written
#   kept      an app's own kept settings and the settings file: the
#             kept-settings slice
DOOR = {}
for _n in """DT_FOCUS DT_ROWS DT_COLS DT_WS DT_PRIMARY_COL DT_PRIMARY_H DT_PRIMARY_W
        DT_DISPNAMES DT_UPNEW DT_UPSESSION DT_TRASHN DT_KBCUR DT_IME DT_RESTORED DT_LANG""".split():
    DOOR[_n] = "hot"
for _n in """DT_APPS DT_BARI DT_HANDLERS DT_OPENERS DT_OPENERORDER DT_OPENVERBS DT_OVLIST
        DT_BUNDLEEXT SV_LIST CS_MOD_ALL DT_SBLIST DT_KB DT_KBLOC DT_KBN DT_KBRES DT_AKEY
        DT_AKEYORDER DT_CH DT_CHORDER DT_NOTEHIST DT_NOTEHIST_ORDER DT_VWA DT_VWJOB DT_VWERR
        DT_VWITEMS DT_VWSYNCED VW_SESSION DT_PKRES DRW""".split():
    DOOR[_n] = "registry"
# Written only where they are declared, so constants of this desktop: sent
# once, at the hello.
for _n in """DT_HERE DT_VER DT_BELLICONS DT_KEYORDER DT_NOTECOLOURS DT_NOTELEVELS DT_PLACES
        DT_TRASH""".split():
    DOOR[_n] = "hello"
for _n in """DT_PX DT_PY MB_CX MB_CY MB_CTXID MB_CTXWHO DT_DND DT_KEYBASE DT_KEYCODE DT_KEYMODS
        DT_KEYPHASE DT_PHASEWIN DT_NATFOCUSES DT_NATCH DT_NATCW CS_CLICKC DT_BARWCOL""".split():
    DOOR[_n] = "call"
# The next note's number, bumped per note and read only by the pane that
# writes the note queue: part of that request, not a mirror.
BYNAME["DT_NOTE_NEXT"] = "request"

# A registry whose entries are records: each field an installable reads, and
# its door -- `registry`, sent with the table, or `call`, moving with a frame
# or input and so travelling in the call that uses it, left out of what is
# sent. A field read and not named here fails the survey.
FIELDDOOR = {
    "DT_APPS": {"hidden": "registry", "src": "registry", "title": "registry"},
    "DT_BARI": {"label": "registry", "text": "registry", "c": "call", "w": "call"},
    "DT_CH": {"at": "registry", "pin": "registry", "text": "registry"},
    "DT_HANDLERS": {"n": "registry", "orig": "registry", "term": "registry", "$i": "registry"},
    "DT_KB": {"label": "registry", "layout": "registry", "name": "registry", "variant": "registry"},
    "DT_NOTEHIST": {"at": "registry", "from": "registry", "prio": "registry", "text": "registry"},
    "DT_OPENERS": {"types": "registry"},
    "DT_OPENVERBS": {"label": "registry"},
    "DRW": {"stamp": "registry"},
}
# Registries an app the survey has not read is never sent: one that is not
# known gets every other registry, so it is never silently empty, but not a
# key or what it opens.
SECRET = {"VW_SESSION", "DT_VWITEMS"}
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


def regwriters():
    """Every platform function that writes a registry, with the fields it
    writes: (file, function, name, fields, marks). A write at top level is
    the declaration, made before any app can connect, and is not one."""
    regs = [n for n, d in DOOR.items() if d == "registry"]
    out = []
    for f in platfiles():
        fn, body, start = None, [], 0
        for line in open(f, errors="replace").read().split("\n") + ["}"]:
            m = re.match(r"^(?:fn\s+([A-Za-z_][A-Za-z0-9_]*)\(|([A-Za-z_][A-Za-z0-9_]*)\(\)\s*\{)", line)
            if m:
                fn, body = m.group(1) or m.group(2), []
                continue
            if fn is None:
                continue
            if line.startswith("}"):
                text = "\n".join(body)
                marks = set()
                for mm in re.finditer(r"\bdt_rtreg((?:\s+[A-Z][A-Z0-9_]*)+)", text):
                    marks.update(mm.group(1).split())
                marks.update(re.findall(r"\bdt_rtregk\s+([A-Z][A-Z0-9_]*)", text))
                for n in regs:
                    if n not in text:
                        continue
                    flds = set()
                    hit = False
                    for b in body:
                        if re.search(WRITE % ((n,) * 5), b):
                            hit = True
                            flds.update(re.findall(r'\b%s\[[^\]]*\]\["?([a-z0-9_]+)"?\]\+?=' % n, b))
                            if not re.search(r'\b%s\[[^\]]*\]\["?[a-z0-9_]+"?\]\+?=' % n, b):
                                flds.add("*")
                    if hit:
                        out.append((os.path.relpath(f, DESK), fn, n, flds, n in marks))
                fn = None
                continue
            body.append(line)
    return out
# What a function an installable calls passes back through a global: the
# openers for a path (dt_openers), a selection's verbs (dt_openverbs), the
# strip's modules in order (cs_ordered), the title bar's buttons
# (dt_btnlist), the bar's items as they read (dt_barreading).
for _n in """DT_DLGR DT_DLGC DT_DLGH DT_DLGW DT_OWLIST DT_OVLIST CS_MOD_ORD DT_BTNL
        DT_BARREAD""".split():
    DOOR[_n] = "answer"
for _n in "DT_HOSTMARK".split():
    DOOR[_n] = "gone"
# Names the survey calls settings, since settings.hibr declares them, that
# are not one: the window table, whose door is the window records (an
# app's own windows, sent when a record is written); and the settings
# file's parse, which an app reads only to ask whether a setting of its own
# is stored -- the kept-settings slice's question, answered there.
SETDOOR = {"DT": "window", "DT_CONFJ": "kept"}


def maps():
    """The platform's globals declared as maps (declare -A at top level)."""
    out = set()
    for f in platfiles():
        for line in open(f, errors="replace"):
            m = re.match(r"^(?:declare|typeset)\s+-\w*A\w*\s+(.*)", line)
            if m:
                out.update(w.split("=")[0] for w in m.group(1).split()
                           if re.match(r"^[A-Z][A-Z0-9_]*", w))
    return out


def lists():
    """The platform's globals that are lists: assigned NAME=( at top level,
    or declared -a. Like a map, a list crosses whole, as JSON."""
    out = set()
    for f in platfiles():
        for line in open(f, errors="replace"):
            m = re.match(r"^(?:declare\s+-g\w*\s+)?([A-Z][A-Z0-9_]*)=\(", line)
            if m:
                out.add(m.group(1))
            m = re.match(r"^(?:declare|typeset)\s+-\w*a\w*\s+(.*)", line)
            if m:
                out.update(w.split("=")[0] for w in m.group(1).split()
                           if re.match(r"^[A-Z][A-Z0-9_]*", w))
    return out - maps()


def winfields():
    """The fields of a window's record (DT[$id]["x"]) any installable reads:
    what an app's own process is sent of its own windows."""
    out = set()
    for f in installables():
        out.update(re.findall(r'\bDT\[\$?\{?[A-Za-z_]+\}?\]\["([a-z]+)"\]', open(f, errors="replace").read()))
    return sorted(out)


def mirrors(rows, reads):
    """programs/desktop/mirrors.hibr: every mirror's door, which the desktop
    reads to know what to send and the app to know what it may be sent; and
    the declarations of what is sent today, in a function only the app's
    runtime calls -- in the desktop they would come before its settings'
    defaults, which are written ${X-default} and would find X set empty."""
    mp = maps()
    ls = lists()
    doors = sorted((r[0], door(r[0], r[4])) for r in rows if r[4] in ("mirror", "setting"))
    sent = [(n, d) for n, d in doors if d in SENT]
    out = ["# mirrors -- the desktop state an app's own process is sent, and how (ADR",
           "# 0050): DT_RTDOOR is each mirror's door (tools/appstate.py says what each",
           "# means), and dt_rtdeclare declares, in an app's own process, every name",
           "# it is sent today, so a strict file can be given them. Generated by",
           "# tools/appstate.py --write from the state survey; never edited by hand.",
           "# interface: shared",
           "",
           "declare -gA DT_RTDOOR=(" + " ".join("[%s]=%s" % (n, d) for n, d in doors) + ")",
           "# The fields of a window's record installables read: what an app's own",
           "# process is sent of its own windows (the window door).",
           "DT_RTWINF=(" + " ".join(winfields()) + ")",
           "# Who reads each registry, as the kind folder sees the file (\"*\": the",
           "# app interface does, so every process); every installable the survey",
           "# read, so one it did not is sent every registry but a secret; the",
           "# secrets; and a record's fields that move with input or a frame, left",
           "# out of what is sent.",
           "declare -gA DT_RTAUD=(" + " ".join('[%s]="%s"' % (n, aud(n, reads)) for n, d in doors if d == "registry") + ")",
           "declare -gA DT_RTKNOWN=(" + " ".join("[%s]=1" % k for k in sorted(set(kindrel(f) for f in installables()))) + ")",
           "declare -gA DT_RTSECRET=(" + " ".join("[%s]=1" % n for n in sorted(SECRET)) + ")",
           "declare -gA DT_RTSKIP=(" + " ".join('[%s]="%s"' % (n, " ".join(sorted(k for k, v in fd.items() if v != "registry")))
                                               for n, fd in sorted(FIELDDOOR.items()) if any(v != "registry" for v in fd.values())) + ")",
           "# Which of them are maps or lists, each sent whole, as JSON.",
           "declare -gA DT_RTMAP=(" + " ".join("[%s]=1" % n for n, d in doors if n in mp or n in ls) + ")",
           "",
           "# The names sent today, declared in an app's own process: called by the",
           "# runtime alone, before its hello.",
           "dt_rtdeclare() {"]
    sc = [n for n, d in sent if n not in mp and n not in ls]
    mm = [n for n, d in sent if n in mp]
    ll = [n for n, d in sent if n in ls]
    for i in range(0, len(sc), 8):
        out.append("\tdeclare -g " + " ".join(sc[i:i + 8]))
    for i in range(0, len(mm), 8):
        out.append("\tdeclare -gA " + " ".join(mm[i:i + 8]))
    for i in range(0, len(ll), 8):
        out.append("\tdeclare -ga " + " ".join(ll[i:i + 8]))
    out += ["\treturn 0", "}", ""]
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


def door(name, k):
    if k == "setting":
        return SETDOOR.get(name, "save")
    if k == "mirror":
        return DOOR.get(name, "?")
    return "-"


DIRECT = []


def klass(name, wr, where):
    if name in BYNAME:
        return BYNAME[name]
    # A registry an installable also writes directly is still sent; the
    # direct write is a request to convert, listed, and marked meanwhile.
    if DOOR.get(name) == "registry":
        if wr:
            DIRECT.append((name, " ".join(sorted(set(kindrel(f) for f in wr)))))
        return "mirror"
    # A kept setting is the desktop's wherever it is declared: the glyph set,
    # the language and the mirroring are declared in shared files, and taken
    # for each process's own they would never have been sent.
    if name in KEPT:
        return "setting"
    if where.startswith(LOCAL) or name in MODVARS:
        return "local"
    if where.startswith(SETTING):
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
            if re.search(r"(^|[\s;(&|])%s(\[[^\]]*\])*\+?=|\b(unset|read\b[^\n]*)\s+[\"']?%s\b" % (n, n), text, re.M):
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
            o.write("# name\tread in\twritten in\tdeclared in\tclass\tdoor -- tools/appstate.py --write (a word match, not a parse)\n")
            for row in rows:
                o.write("%s\t%d\t%d\t%s\t%s\t%s\n" % (row + (door(row[0], row[4]),)))
        print("wrote", os.path.relpath(OUT, ROOT))
    mtext = mirrors(rows, reads)
    if "--write" in sys.argv:
        open(MIRRORS, "w").write(mtext)
        print("wrote", os.path.relpath(MIRRORS, ROOT))
    elif os.path.isdir(APPS) and (not os.path.exists(MIRRORS) or open(MIRRORS).read() != mtext):
        print("FAIL programs/desktop/mirrors.hibr is not what the survey makes: tools/appstate.py --write")
        sys.exit(1)
    undoored = [r[0] for r in rows if door(r[0], r[4]) == "?"]
    # A door no installable needs is stale only where every installable was
    # read: run over this tree alone (no catalogue checkout), a name only the
    # catalogue's apps read is simply not seen.
    stale = [n for n in DOOR if not any(r[0] == n and r[4] == "mirror" for r in rows)] if os.path.isdir(APPS) else []
    for n in undoored:
        print("FAIL %s is a mirror an installable reads, with no door: say in DOOR how it reaches an app's process" % n)
    for n in stale:
        print("FAIL %s has a door and is no mirror any installable reads: take it out of DOOR" % n)
    doors = collections.Counter(door(r[0], r[4]) for r in rows if r[4] in ("mirror", "setting"))
    print("  doors: " + ", ".join("%s %d" % kv for kv in sorted(doors.items())))
    # Every function that writes a registry marks it (dt_rtreg NAME), or the
    # processes that read it never see the change; a write of a field whose
    # door is the call is not one.
    unmarked = []
    for f, fn, n, flds, marked in regwriters():
        fd = FIELDDOOR.get(n, {})
        if flds and "*" not in flds and all(fd.get(x, "registry") != "registry" for x in flds):
            continue
        if not marked:
            unmarked.append((f, fn, n))
    for f, fn, n in unmarked:
        print("FAIL %s: %s writes the registry %s and never marks it: dt_rtreg %s" % (f, fn, n, n))
    # An installable that changes a registry through the desktop and reads it
    # back: in a process of its own that call is an asked request, answered
    # only after the table has been sent, or the read sees the old one. The
    # asked-requests slice is built from this list.
    wr = collections.defaultdict(set)
    for f, fn, n, flds, marked in regwriters():
        wr[n].add(fn)
    back = set()
    for f in files:
        t = open(f, errors="replace").read()
        for n, fns in wr.items():
            if n not in reads or f not in reads[n]:
                continue
            for fn in fns:
                if re.search(r"(^|[\s;&|(])%s\b" % fn, t, re.M):
                    back.add((kindrel(f), n, fn))
    print("  read back after a change through the desktop: %d (an installable, a registry, the call)" % len(back))
    if "--back" in sys.argv:
        for b in sorted(back):
            print("    %-34s %-14s %s" % b)
    for n, who in sorted(set(DIRECT)):
        print("  written directly by an installable, a request to convert: %s (%s)" % (n, who))
    # A field of a record read by an installable has a door.
    undf = []
    for n, fd in FIELDDOOR.items():
        for f in reads[n]:
            for x in re.findall(r'\b%s\[[^\]]*\]\["?([a-z0-9_$]+)"?\]' % n, open(f, errors="replace").read()):
                if x not in fd:
                    undf.append((n, x, kindrel(f)))
    for n, x, f in sorted(set(undf)):
        print("FAIL %s reads the field %s of %s, which has no door in FIELDDOOR" % (f, x, n))
    if undoored or stale or unmarked or undf:
        sys.exit(1)


if __name__ == "__main__":
    main()
