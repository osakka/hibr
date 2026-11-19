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
MIRRORS = os.path.join(ROOT, "programs", "desktop", "mirrors.hibr")
# The doors an app's own process is sent through today; the rest are
# declared as their slices land.
SENT = ("save", "hello", "hot", "window")
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
        DT_OWLIST DT_BUNDLEEXT DT_PLACES SV_LIST CS_MOD_ALL CS_MOD_ORD DT_SBLIST DT_KB DT_KBLOC
        DT_KBN DT_KBRES DT_AKEY DT_AKEYORDER DT_KEYORDER DT_BELLICONS DT_NOTELEVELS DT_NOTECOLOURS
        DT_CH DT_CHORDER DT_NOTEHIST DT_NOTE_NEXT DT_VWA DT_VWJOB DT_VWERR DT_VWITEMS DT_VWSYNCED
        VW_SESSION DT_TRASH DT_PKRES DT_BTNL DT_BARREAD DT_BARWCOL DRW""".split():
    DOOR[_n] = "registry"
for _n in "DT_HERE DT_VER".split():
    DOOR[_n] = "hello"
for _n in """DT_PX DT_PY MB_CX MB_CY MB_CTXID MB_CTXWHO DT_DND DT_KEYBASE DT_KEYCODE DT_KEYMODS
        DT_KEYPHASE DT_PHASEWIN DT_NATFOCUSES DT_NATCH DT_NATCW CS_CLICKC""".split():
    DOOR[_n] = "call"
for _n in "DT_DLGR DT_DLGC DT_DLGH DT_DLGW".split():
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


def winfields():
    """The fields of a window's record (DT[$id]["x"]) any installable reads:
    what an app's own process is sent of its own windows."""
    out = set()
    for f in installables():
        out.update(re.findall(r'\bDT\[\$?\{?[A-Za-z_]+\}?\]\["([a-z]+)"\]', open(f, errors="replace").read()))
    return sorted(out)


def mirrors(rows):
    """programs/desktop/mirrors.hibr: every mirror's door, which the desktop
    reads to know what to send and the app to know what it may be sent; and
    the declarations of what is sent today, in a function only the app's
    runtime calls -- in the desktop they would come before its settings'
    defaults, which are written ${X-default} and would find X set empty."""
    mp = maps()
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
           "# Which of them are maps, sent entry by entry.",
           "declare -gA DT_RTMAP=(" + " ".join("[%s]=1" % n for n, d in doors if n in mp) + ")",
           "",
           "# The names sent today, declared in an app's own process: called by the",
           "# runtime alone, before its hello.",
           "dt_rtdeclare() {"]
    sc = [n for n, d in sent if n not in mp]
    mm = [n for n, d in sent if n in mp]
    for i in range(0, len(sc), 8):
        out.append("\tdeclare -g " + " ".join(sc[i:i + 8]))
    for i in range(0, len(mm), 8):
        out.append("\tdeclare -gA " + " ".join(mm[i:i + 8]))
    out += ["\treturn 0", "}", ""]
    return "\n".join(out)


def door(name, k):
    if k == "setting":
        return SETDOOR.get(name, "save")
    if k == "mirror":
        return DOOR.get(name, "?")
    return "-"


def klass(name, wr, where):
    if name in BYNAME:
        return BYNAME[name]
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
            o.write("# name\tread in\twritten in\tdeclared in\tclass\tdoor -- tools/appstate.py --write (a word match, not a parse)\n")
            for row in rows:
                o.write("%s\t%d\t%d\t%s\t%s\t%s\n" % (row + (door(row[0], row[4]),)))
        print("wrote", os.path.relpath(OUT, ROOT))
    mtext = mirrors(rows)
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
    if undoored or stale:
        sys.exit(1)


main()
