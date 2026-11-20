#!/usr/bin/env python3
"""The platform functions installables call, and what each becomes in an app's
own process (ADR 0050): local (the app runtime has it), request (sent to the
desktop, never waited on), declare (built in the app and pushed up), or
decide (not yet classified).

  python3 tools/appcalls.py [--write]   # print the summary; --write the table
  python3 tools/appcalls.py --check     # fail on a function not classed (tests/appcalls.py)

Every call is read by the inspect module (ADR 0056), never run, from every
installable the catalogue checkout holds (~/hibr-apps/bundles, or $HIBR_APPS)
and every one shipped in programs/desktop. A function is classed by the file
that defines it, unless OVERRIDE names it; --write puts the table at
programs/desktop/interface.tsv, which the ADR points at.
"""
import collections, glob, os, re, sys
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "tests"))
from inspectlib import records

APPS = os.environ.get("HIBR_APPS", os.path.expanduser("~/hibr-apps"))
OUT = os.path.join(ROOT, "programs", "desktop", "interface.tsv")

# By the defining file: what the app runtime carries itself (local), what goes
# to the desktop (request), what is built in the app and pushed (declare).
BYFILE = {
    "widgets/": "local", "lib/": "local",
    "wm/lang.hibr": "local", "wm/locale.hibr": "local", "wm/glyphs.hibr": "local",
    "wm/jobs.hibr": "local", "wm/keys.hibr": "local", "wm/remote.hibr": "local",
    "wm/menus.hibr": "declare", "wm/bar.hibr": "declare", "wm/strip.hibr": "declare",
    "wm/windows.hibr": "request", "wm/notify.hibr": "request", "wm/clip.hibr": "request",
    "wm/handlers.hibr": "request", "wm/workspaces.hibr": "request",
    "wm/confirm.hibr": "request", "wm/ask.hibr": "request", "wm/filepick.hibr": "request",
}
# One function at a time, where its file says nothing (a file of mixed roles).
OVERRIDE = {
    "dt_want": "local", "dt_has": "local", "dt_ms": "local", "dt_tput": "local",
    "dt_wreg": "local", "dt_wclear": "local", "dt_hit": "local",
    "dt_keep": "declare", "dt_save": "request", "dt_app": "declare",
    "dt_ticker": "local", "dt_barhook": "declare", "dt_launch": "request",
    "dt_note": "request", "dt_notep": "request",
}
# What is the app's own work, or a fact about the display pushed to it.
for _n in """dt_log dt_cut dt_rep dt_isdbl dt_dblclear dt_shortname dt_procfull
        dt_cellpx dt_imgpix dt_imggfx dt_imgm dt_imgwill dt_wallimgmode dt_sysdir
        dt_holdsess dt_ploc dt_pname dt_pnext dt_ptime dt_ptimes dt_watchstart
        dt_watchstop dt_watchlines dt_buson dt_busoff dt_keepstate dt_needs
        dt_vwstate dt_vwcopy dt_vwlock dt_vwacct dt_vwuntil dt_vwjob dt_vw dt_vwget
        dt_vwpick dt_vwhost dt_vwmatch dt_vwsyncnow dt_signmark dt_pwlocal
        dt_pwitems dt_live dt_size dt_fieldedit dt_edit dt_isblank dt_jobname
        dt_isdark dt_hoveron dt_natcell dt_kblabel dt_keyphases dt_appnames
        dt_stale dt_passmark""".split():
    OVERRIDE[_n] = "local"
# The vault is the desktop's: one lock state, one child running `vw`, so
# unlocking in Vault unlocks the Browser's fill too. Its state is pushed
# to read (dt_vwuntil, dt_vwhost: wm/vaultfacts.hibr); everything else is
# asked of it.
for _n in """dt_vwstate dt_vwcopy dt_vwlock dt_vwacct dt_vwjob dt_vw dt_vwget
        dt_vwpick dt_vwmatch dt_vwsyncnow dt_vwfill dt_blank""".split():
    OVERRIDE[_n] = "request"
# What changes the desktop itself: asked of it, never done in the app.
for _n in """dt_changed dt_fopsave dt_fopundo dt_trash dt_fileop dt_locof dt_loccan
        dt_location dt_context_open dt_panelpane dt_vwdialog dt_chromeoff
        dt_imepre dt_imekey dt_imeflush
        dt_pointer dt_pointerlock dt_ptrlockcan dt_sbauto dt_sbcall dt_tile
        dt_place dt_openlink dt_progload dt_progsave dt_deskscan dt_dnd dt_pwdo
        dt_pwgo dt_pkstart dt_pkstop dt_kbset dt_saverload dt_saverstart
        dt_colours dt_walldisp dt_shot dt_rescan dt_reinstate
        dt_unload dt_onunload dt_scanold dt_appvia dt_appunvia dt_modehook
        dt_rslog dt_rscarry dt_rsclaim dt_onrescan dt_wallretry dt_wallforget
        cs_modwidth dt_progput dt_proghide dt_progdrop dt_statadd dt_pwcrit""".split():
    OVERRIDE[_n] = "request"
for _n in "dt_wintitle dt_ext dt_chromeoff".split():
    OVERRIDE[_n] = "local"
# The desktop laying itself out again, its Copy/Cut/Paste dispatcher, and a
# display's cell size: each traced from the manifest pulling in the window
# list, the bar's pane and the icons (tools/appcalls.py --why).
for _n in "dt_size dt_edit dt_natcell".split():
    OVERRIDE[_n] = "request"
# Asking for a window's key releases and rebinding a shortcut change the
# desktop's key path; an app's own shortcuts are declared to it.
for _n in "dt_keyphases dt_keyswap".split():
    OVERRIDE[_n] = "request"
OVERRIDE["dt_appaction"] = "declare"
# The bus is one
# connection, the desktop's, so an app subscribes to it by asking.
for _n in "dt_buson dt_busoff".split():
    OVERRIDE[_n] = "request"
OVERRIDE["dt_keepstate"] = "declare"
# The input method runs in every app process, its data shared from the
# zygote (owner, 2026-11-18): what a text field asks of it is local. The
# teach box and the chords told to Blit stay the desktop's.
for _n in "dt_imepre dt_imekey dt_imeflush".split():
    OVERRIDE[_n] = "local"
OVERRIDE["dt_atstart"] = "declare"
OVERRIDE["dt_identify"] = "request"
# The one way an installable changes a kept setting (ADR 0050).
OVERRIDE["dt_set"] = "request"
OVERRIDE["dt_sbscan"] = "request"
# The prototype's own plumbing, and what only the platform's own windows
# reach for: no installable calls these once the runtime exists.
OVERRIDE["dt_launched"] = "request"
OVERRIDE["dt_natlayout"] = "request"
for _n in """dt_isosent dt_isomsg dt_isodrop dt_isosend dt_isomenus""".split():
    OVERRIDE[_n] = "gone"


def defs():
    """Every function the platform defines, and its file under programs/desktop."""
    where = {}
    for f in sorted(glob.glob(os.path.join(ROOT, "programs/desktop/wm/*.hibr")) +
                    glob.glob(os.path.join(ROOT, "programs/desktop/widgets/*.hibr")) +
                    glob.glob(os.path.join(ROOT, "programs/desktop/lib/*.hibr"))):
        rel = os.path.relpath(f, os.path.join(ROOT, "programs/desktop"))
        for line in open(f, errors="replace"):
            m = re.match(r"\s*(?:fn\s+)?([A-Za-z_][A-Za-z0-9_]*)\s*\(", line)
            if m:
                where.setdefault(m.group(1), rel)
    return where


def rets(where):
    """The platform functions that answer with a value: their body calls
    `ret`, by the parser. Only those are bound when asked (`x := f` in the
    desktop, `ret -k` in the stub); one that does not is waited for, its
    status the whole answer -- a fact of the function, never of a caller."""
    out = set()
    for rel in sorted(set(where.values())):
        f = os.path.join(ROOT, "programs/desktop", rel)
        for c in records(f, "calls").get("calls", []):
            if c.get("name") == "ret" and c.get("owner"):
                out.add(c["owner"])
    return out


def binders(rows):
    """Each call site binding a request that answers with no value: (file,
    line, request) -- `x := f` of a function that never calls ret binds
    empty, in-process as hosted, so the site is the bug."""
    waited = set(r[1] for r in rows if r[0] == "request" and r[5] == "waited")
    out = []
    for f in installables():
        for c in records(f, "calls").get("calls", []):
            if c.get("name") in waited and (c.get("bound") or c.get("scope") == "substitution"):
                out.append((os.path.basename(f), c.get("line", "?"), c["name"]))
    return out


def platform(f):
    """A file ADR 0046 marks as the platform's own: not an installable here."""
    try:
        return any(l.startswith("# platform:") for l in open(f, errors="replace"))
    except OSError:
        return False


def installables():
    """Every installable script: the catalogue's bundles and the shipped
    tree's, less the platform's own (ADR 0046, kept by the owner 2026-11-18)."""
    fs = set(glob.glob(os.path.join(APPS, "bundles", "**", "*.hibr"), recursive=True))
    for d in ("apps", "system", "control-panel", "control-strip", "menubar", "savers"):
        fs |= set(glob.glob(os.path.join(ROOT, "programs/desktop", d, "**", "*.hibr"), recursive=True))
    return sorted(f for f in fs if not platform(f))


# What a setting does is the desktop's (dt_onset, ADR 0050): an installable
# calling a registered effect right after dt_set of its name does it twice,
# and in an app's own process the second call acts on a stale copy.
def onsets(root):
    """NAME -> the effect functions dt_onset registers for it, read from the
    window manager's own files (a registration is `dt_onset NAME... FUNC`,
    continued with a backslash)."""
    out = {}
    for f in glob.glob(os.path.join(root, "programs/desktop/wm/*.hibr")):
        text = open(f, errors="replace").read().replace("\\\n", " ")
        for line in text.split("\n"):
            m = re.match(r"^\s*dt_onset\s+(.+?)\s*$", line)
            if not m:
                continue
            w = m.group(1).split()
            for n in w[:-1]:
                out.setdefault(n, set()).add(w[-1])
    return out


def doubled(root, files, near=3):
    """Each call of a registered effect within `near` lines after a dt_set of
    one of its names, in the same function (a line at column 0 ends one)."""
    reg = onsets(root)
    sites = []
    for f in files:
        lines = open(f, errors="replace").read().split("\n")
        for i, l in enumerate(lines):
            m = re.search(r"\bdt_set\s+(.*)", l)
            if not m or l.lstrip().startswith("#"):
                continue
            names = [w for w in m.group(1).split() if re.match(r"^[A-Z][A-Z0-9_]*$", w)]
            fx = set().union(*(reg.get(n, set()) for n in names)) if names else set()
            if not fx:
                continue
            for j in range(i, min(i + near + 1, len(lines))):
                if j > i and lines[j][:1] not in ("\t", " ", ""):
                    break
                t = lines[j] if j > i else l[m.end():]
                if t.lstrip().startswith("#"):
                    continue
                for fn in fx:
                    if re.search(r"(^|[^A-Za-z0-9_])%s([^A-Za-z0-9_]|$)" % re.escape(fn), t):
                        sites.append((f, j + 1, " ".join(names), fn))
    return sites


def bodies():
    """Each platform function's body, a closing brace at column 0 ending it."""
    body, cur = collections.defaultdict(str), None
    for f in (glob.glob(os.path.join(ROOT, "programs/desktop/wm/*.hibr")) +
              glob.glob(os.path.join(ROOT, "programs/desktop/widgets/*.hibr")) +
              glob.glob(os.path.join(ROOT, "programs/desktop/lib/*.hibr"))):
        for line in open(f, errors="replace"):
            m = re.match(r"(?:fn\s+)?([A-Za-z_][A-Za-z0-9_]*)\s*\(.*\)\s*\{?\s*$", line)
            if m:
                cur = m.group(1)
            elif line.startswith("}"):
                cur = None
            elif cur and not line.lstrip().startswith("#"):
                body[cur] += line
    return body


def reaches(cls):
    """Every local function that reaches a request through the platform's own
    calls, with one path each: local code that sends one is fine if nothing
    waits for the answer, and the ADR reads the rest."""
    body = bodies()
    calls = {n: set(w for w in re.findall(r"\b([a-z][a-z0-9]*_[a-z0-9_]+)\b", b) if w in body and w != n)
             for n, b in body.items()}
    out = {}
    for n, c in cls.items():
        if c != "local":
            continue
        seen, stack = {n}, [(n, [n])]
        while stack:
            x, path = stack.pop()
            for y in sorted(calls.get(x, ())):
                if y in seen:
                    continue
                seen.add(y)
                if cls.get(y) == "request":
                    out.setdefault(n, " > ".join(path + [y]))
                    continue
                stack.append((y, path + [y]))
    return out


def asked(files, names):
    """The platform functions an installable needs an answer from, by the
    parser: their value taken (`x := f`, `$(f)`), their status tested (`if
    f`, `f ||`, `! f` -- inspect's `tested`), or what they change read back
    after (tools/appstate.py's backs). Told, any of them would answer 0 and
    leave the caller reading its own stale copy; asked, the answer comes after
    the change has been sent."""
    import appstate
    hit = collections.Counter()
    for f in files:
        for c in records(f, "calls").get("calls", []):
            n = c.get("name")
            if n in names and (c.get("bound") or c.get("tested") or c.get("scope") == "substitution"):
                hit[n] += 1
                why = "bound" if c.get("bound") or c.get("scope") == "substitution" else "tested"
                asked.why[n].append((f, c.get("line", "?"), why))
    for f, _, fn in appstate.readbacks():
        if fn in names:
            hit[fn] += 1
            tree = os.path.join(ROOT, "programs", "desktop", f)
            asked.why[fn].append((tree if os.path.exists(tree) else f, "?", "read back"))
    return hit


asked.why = collections.defaultdict(list)


def flips(rows):
    """Each request whose answer differs from the committed table: (name, was,
    now, why), why the sites that make it so. A class changes for every app
    at once -- one installable's tested status made dt_notep a round trip
    for all of them -- so a difference fails until it is taken with --write,
    which puts it in the commit's diff where it is read."""
    if not os.path.exists(OUT):
        return []
    was = {}
    for l in open(OUT):
        c = l.rstrip("\n").split("\t")
        if len(c) > 5 and c[0] == "request":
            was[c[1]] = c[5]
    return [(r[1], was[r[1]], r[5], asked.why.get(r[1], [])) for r in rows
            if r[0] == "request" and r[1] in was and was[r[1]] != r[5]]


# Asked requests reachable from a frame path, held to this many and only ever
# lowered (54, then 41 once Switchboard 0.5.66 tested no note): an ask there
# is a round trip each time the path runs. What is left
# is conditional -- a title changed, a note on an error, a bell -- or waits on
# the openers being declared data (dt_isbundle, dt_locof, dt_loccan).
FRAMEASKS = 39


def kinds(rows):
    """The requests by how they cross: (told, asked or waited)."""
    told = set(r[1] for r in rows if r[0] == "request" and r[5] == "told")
    asked = set(r[1] for r in rows if r[0] == "request" and r[5] in ("asked", "waited"))
    return told, asked


def frameasks(rows):
    """Each (installable, asked request, a path to it) reachable from a
    function a frame runs -- an app's _draw, _dirty and _idle, and a ticker it
    registers -- through the functions of its own file."""
    _, asked = kinds(rows)
    frame = re.compile(r"_(draw|dirty|idle)$")
    out = set()
    for f in installables():
        calls = collections.defaultdict(set)
        roots = set()
        for c in records(f, "calls").get("calls", []):
            o, n = c.get("owner"), c.get("name")
            if o and n:
                calls[o].add(n)
                if frame.search(o):
                    roots.add(o)
        roots |= set(re.findall(r"\bdt_ticker\s+([A-Za-z_][A-Za-z0-9_]*)", open(f, errors="replace").read()))
        rel = os.path.relpath(f, os.path.join(ROOT, "programs", "desktop"))
        for r in roots:
            seen, stack = {r}, [(r, [r])]
            while stack:
                fn, path = stack.pop()
                for g in calls.get(fn, ()):
                    if g in asked:
                        out.add((rel, g, " > ".join(path + [g])))
                    elif g in calls and g not in seen:
                        seen.add(g)
                        stack.append((g, path + [g]))
    best = {}
    for f, g, p in sorted(out):
        best.setdefault((f, g), p)
    return [(f, g, p) for (f, g), p in best.items()]


def statusholes(rows):
    """Status reads `tested` cannot see, of a told request (which answers 0 in
    an app's own process): `$?` read on the next line, and a function ending
    in one whose own status a caller in the file tests or takes."""
    told, _ = kinds(rows)
    qm, tail = [], []
    for f in installables():
        lines = open(f, errors="replace").read().split("\n")
        calls = records(f, "calls").get("calls", [])
        last = {}
        rel = os.path.basename(f)
        for c in calls:
            o, n, ln = c.get("owner"), c.get("name"), c.get("line")
            if o and ln:
                if o not in last or c["span"]["start"] > last[o][1]:
                    last[o] = (n, c["span"]["start"], ln)
            if n in told and ln and ln < len(lines) and re.search(r"\$\?", lines[ln]):
                qm.append((rel, ln + 1, n))
        tested = set(c["name"] for c in calls if c.get("tested") or c.get("bound"))
        for o, (n, _, ln) in last.items():
            if n in told and o in tested:
                tail.append((rel, ln, o, n))
    return qm, tail


def sigs():
    """Every platform function's signature as its definition line writes it."""
    out = {}
    for f in sorted(glob.glob(os.path.join(ROOT, "programs/desktop/wm/*.hibr")) +
                    glob.glob(os.path.join(ROOT, "programs/desktop/widgets/*.hibr")) +
                    glob.glob(os.path.join(ROOT, "programs/desktop/lib/*.hibr"))):
        lines = open(f, errors="replace").read().split("\n")
        for i, line in enumerate(lines):
            m = re.match(r"(?:fn\s+)?([A-Za-z_][A-Za-z0-9_]*)\s*\((.*)", line)
            if not m or m.group(1) in out:
                continue
            text, j = m.group(2), i
            while ")" not in text and j + 1 < len(lines) and j - i < 8:
                j += 1
                text += " " + lines[j]
            if ")" not in text:
                continue
            out[m.group(1)] = "%s(%s)" % (m.group(1), " ".join(text[:text.index(")")].split()))
    return out


def nextver():
    """The release a row added now first ships in: the one after HIBR_VER."""
    v = re.search(r'#define HIBR_VER "([^"]+)"', open(os.path.join(ROOT, "include/hibr.h")).read()).group(1)
    a = v.split(".")
    a[-1] = str(int(a[-1]) + 1)
    return ".".join(a)


# A deliberate break of the app interface, kept only against the one tag it
# is compared with: function -> (that tag, why). It goes at the next release,
# and an entry that names another tag, or a row that did not change, fails
# as stale -- an exception never outlives the break it admits (ADR 0050).
BREAKS = {
    "dt_openerverb": ("v0.99.554", "an opener's verb is declared data: its fifth word is a "
                      "test letter (f, e, d, x), never a function the desktop calls into"),
}


def params(sig):
    """A signature's parameters, in order, as written."""
    if not sig or "(" not in sig:
        return []
    inner = sig[sig.index("(") + 1:sig.rindex(")")].strip()
    return [x.strip() for x in inner.split(",")] if inner else []


def additive(old, new):
    """The parameters new adds after old's, when every one of them is optional
    (a default, or the rest): an old call is still a call of the new one."""
    a, b = params(old), params(new)
    if len(b) <= len(a) or b[:len(a)] != a or any(x.startswith("...") for x in a):
        return None
    extra = b[len(a):]
    return extra if all("=" in x or x.startswith("...") for x in extra) else None


def argsat(field):
    """A row's args field: [(words, release)], a call of that many argument
    words or more needing that release."""
    out = []
    for e in (field or "-").split(";"):
        if "@" in e:
            n, v = e.split("@", 1)
            out.append((int(n), v))
    return out


def callwords(path, call):
    """How many argument words a call passes, read from the source after the
    function's name to the end of its simple command; a redirection is not one."""
    end = (call.get("span") or {}).get("end")
    if end is None:
        return 0
    try:
        text = open(path, "rb").read()[end:].decode("utf-8", "replace")
    except OSError:
        return 0
    words, cur, q, depth, i, inw = [], "", "", 0, 0, False
    while i < len(text):
        ch = text[i]
        if q:
            if ch == "\\" and q == '"':
                cur += text[i:i + 2]; i += 2; continue
            if ch == q:
                q = ""
            cur += ch
        elif ch == "\\":
            if text[i + 1:i + 2] == "\n":
                i += 2; continue
            cur += text[i:i + 2]; inw = True; i += 2; continue
        elif ch in "'\"":
            q = ch; cur += ch; inw = True
        elif ch in "({" and cur.endswith("$"):
            depth += 1; cur += ch
        elif ch in ")}" and depth:
            depth -= 1; cur += ch
        elif depth:
            cur += ch
        elif ch in " \t":
            if inw:
                words.append(cur); cur, inw = "", False
        elif ch in "\n;|&)" or (ch == "#" and not inw):
            break
        else:
            cur += ch; inw = True
        i += 1
    if inw:
        words.append(cur)
    n, skip = 0, False
    for w in words:
        if skip:
            skip = False
            continue
        m = re.match(r"^[0-9]*(?:<>|>>|>&|<&|>|<)(.*)$", w)
        if m:
            skip = not m.group(1)
            continue
        n += 1
    return n


def callneeds(path, call, since, args):
    """The release a call needs: its function's row, or a later parameter it
    passes; "-" when the call is older than the contract."""
    vk = lambda v: tuple(int(x) for x in re.findall(r"\d+", v))
    name = call.get("name")
    top = since.get(name, "-")
    top = "-" if top in ("-", "") else top
    later = argsat(args.get(name))
    if later:
        n = callwords(path, call)
        for w, v in later:
            if n >= w and (top == "-" or vk(v) > vk(top)):
                top = v
    return top


def lasttag():
    """The tag the contract is held to: APPCALLS_AGAINST, or the last one."""
    import subprocess
    return os.environ.get("APPCALLS_AGAINST") or subprocess.run(
        ["git", "describe", "--tags", "--abbrev=0"], cwd=ROOT, capture_output=True, text=True).stdout.strip()


def argsfield(name, sig, nv):
    """A row's args field as --write writes it: what the table already
    records, and each optional parameter added since the last tag, at the
    release it first ships in."""
    import subprocess
    global REFTABLE
    if REFTABLE is None:
        ref = lasttag()
        got = subprocess.run(["git", "show", "%s:programs/desktop/interface.tsv" % ref], cwd=ROOT,
                             capture_output=True, text=True) if ref else None
        REFTABLE = readtable(got.stdout) if got and not got.returncode else {}
        REFTABLE["\0args"] = {r[1]: (r[9] if len(r) > 9 else "-") for r in tablerows()} \
            if os.path.exists(OUT) else {}
    have = dict(argsat(REFTABLE["\0args"].get(name)))
    old = REFTABLE.get(name)
    if old:
        extra = additive(old[1], sig)
        if extra is not None:
            base = len(params(old[1]))
            for k in range(len(extra)):
                have.setdefault(base + 1 + k, nv)
    return ";".join("%d@%s" % (w, have[w]) for w in sorted(have)) or "-"


REFTABLE = None


def readtable(text):
    """A calls table's rows by function: (class, signature, since)."""
    rows = {}
    for l in text.splitlines():
        if l.startswith("#") or not l.strip():
            continue
        c = l.split("\t")
        rows[c[1]] = (c[0], c[7] if len(c) > 7 else "", c[8] if len(c) > 8 else "-")
    return rows


def contract(ref):
    """Every row of the table at ref still a platform function with the same
    signature: a row removed or changed is a breaking change to every app
    that calls it, which no suite here may run (ADR 0050)."""
    import subprocess
    got = subprocess.run(["git", "show", "%s:programs/desktop/interface.tsv" % ref], cwd=ROOT,
                         capture_output=True, text=True)
    if got.returncode:
        return None
    sig, fails = sigs(), []
    args = {r[1]: (r[9] if len(r) > 9 else "-") for r in tablerows()}
    changed = set()
    for n, (c, sg, since) in readtable(got.stdout).items():
        if n not in sig:
            fails.append("%s was in the app interface at %s and is gone" % (n, ref))
            continue
        if not sg or sg == sig[n]:
            continue
        changed.add(n)
        if n in BREAKS and BREAKS[n][0] == ref:
            continue
        extra = additive(sg, sig[n])
        have = {w for w, v in argsat(args.get(n))}
        if extra is not None and all(len(params(sg)) + 1 + k in have for k in range(len(extra))):
            continue
        fails.append("%s changed from %s at %s to %s" % (n, sg, ref, sig[n]))
    for n, (at, why) in BREAKS.items():
        if at != ref or n not in changed:
            fails.append("BREAKS still admits %s against %s, which is not the change against %s: "
                         "remove it" % (n, at, ref))
    return fails


def floors(rows):
    """A catalogue entry's hibr floor below the newest row it uses."""
    import json
    idx = os.path.join(APPS, "index.json")
    if not os.path.exists(idx):
        return []
    ents = json.load(open(idx)).get("entries", {})
    since = {r[1]: r[8] for r in rows}
    args = {r[1]: (r[9] if len(r) > 9 else "-") for r in tablerows()}
    vk = lambda v: tuple(int(x) for x in re.findall(r"\d+", v))
    out = []
    for f in installables():
        if "/bundles/" not in f:
            continue
        key = os.path.relpath(f, os.path.join(APPS, "bundles"))[:-5]
        floor = ents.get(key, {}).get("hibr")
        if not floor:
            continue
        r = records(f, "calls")
        need = [(v, c.get("name")) for c in r.get("calls", [])
                for v in [callneeds(f, c, since, args)] if v != "-"]
        if need:
            top = max(need, key=lambda x: vk(x[0]))
            if vk(top[0]) > vk(floor):
                out.append("%s asks for hibr %s but calls %s, new in %s" % (key, floor, top[1], top[0]))
    return out


def derivedfloors():
    """Each catalogue entry's hibr floor as the contract says it: the newest
    release among the platform functions it and its parts call (the calls
    table's since column). Printed by --floors for the catalogue's mkindex,
    which takes the later of this and a floor written by hand -- for what the
    contract cannot see, a widget's file or a module's verb. Keyed as mkindex
    keys an entry: kind/name."""
    rows = tablerows()
    since = {r[1]: r[8] for r in rows if len(r) > 8}
    args = {r[1]: (r[9] if len(r) > 9 else "-") for r in rows}
    vk = lambda v: tuple(int(x) for x in re.findall(r"\d+", v))
    root = os.path.join(APPS, "bundles")
    out = {}
    for f in installables():
        if not f.startswith(root + os.sep):
            continue
        rel = os.path.relpath(f, root)[:-5]
        kind = rel.split(os.sep)[0]
        files = [f]
        for l in open(f, errors="replace"):
            m = re.match(r"^# part:\s*(\S+)", l)
            if m and m.group(1).endswith(".hibr") and os.path.exists(os.path.join(root, m.group(1))):
                files.append(os.path.join(root, m.group(1)))
        top = None
        for g in files:
            for c in records(g, "calls").get("calls", []):
                v = callneeds(g, c, since, args)
                if v != "-" and (top is None or vk(v) > vk(top)):
                    top = v
        if top:
            out["%s/%s" % (kind, os.path.basename(rel))] = top
    return out


def defsall():
    """Every platform function and its file, all of them (defs keeps the first)."""
    where = {}
    for f in sorted(glob.glob(os.path.join(ROOT, "programs/desktop/wm/*.hibr")) +
                    glob.glob(os.path.join(ROOT, "programs/desktop/widgets/*.hibr")) +
                    glob.glob(os.path.join(ROOT, "programs/desktop/lib/*.hibr"))):
        rel = os.path.relpath(f, os.path.join(ROOT, "programs/desktop"))
        for line in open(f, errors="replace"):
            m = re.match(r"(?:fn\s+)?([A-Za-z_][A-Za-z0-9_]*)\s*\(.*\)\s*\{?\s*$", line)
            if m:
                where.setdefault(m.group(1), rel)
    return where


def manifest(cls):
    """What the app runtime sources and what it stubs (ADR 0050): every file
    whose functions the local ones reach, and, in a file that also holds
    requests or declarations, the functions that have to move out of it so
    the runtime can source the rest whole. A local function that reaches a
    request through the platform's own calls stays: the request is a stub."""
    body = bodies()
    where = defsall()
    calls = {n: set(w for w in re.findall(r"\b([a-z][a-z0-9]*_[a-z0-9_]+)\b", b) if w in body and w != n)
             for n, b in body.items()}
    need, stack, why = set(), [n for n, c in cls.items() if c == "local"], {}
    while stack:
        x = stack.pop()
        if x in need or (cls.get(x) or OVERRIDE.get(x)) in ("request", "declare", "gone"):
            continue
        need.add(x)
        for y in calls.get(x, ()):
            why.setdefault(y, x)
            stack.append(y)
    manifest.why = why
    byfile = collections.defaultdict(set)
    for n in need:
        if n in where:
            byfile[where[n]].add(n)
    files = sorted(byfile)
    stubs = sorted(n for n, c in cls.items() if c in ("request", "declare"))
    mixed = {}
    for f in files:
        other = sorted(n for n, w in where.items() if w == f and cls.get(n) in ("request", "declare"))
        loads = toplevel(f, cls)
        if other or loads:
            mixed[f] = (sorted(byfile[f]), other, loads)
    return files, stubs, mixed, need


REGISTRARS = ("dt_ticker", "dt_keep", "dt_app", "dt_opener", "dt_location", "dt_barhook",
              "dt_atstart", "dt_locload", "dt_glyphs")


def toplevel(rel, cls=None):
    """What a platform file registers when it is sourced: a ticker, a kept
    setting, a dialog as an app, an opener, a location -- or a call to
    anything the desktop alone does. The desktop alone may register; the
    app runtime's own machinery aside, a shared file registers nothing."""
    out, depth = [], 0
    for line in open(os.path.join(ROOT, "programs/desktop", rel), errors="replace"):
        if re.match(r"(?:fn\s+)?[A-Za-z_][A-Za-z0-9_]*\s*\(.*\)\s*\{?\s*$", line):
            depth = 1
            continue
        if line.startswith("}"):
            depth = 0
            continue
        t = line.strip()
        if depth or not t or t.startswith("#") or line[0] in " \t":
            continue
        if re.match(r"(?:fn\s+)?[A-Za-z_][A-Za-z0-9_]*\s*\(.*\)\s*\{.*\}\s*$", t):
            continue
        words = set(re.findall(r"\b[a-z][a-z0-9]*_[a-z0-9_]+\b", t))
        if words & set(REGISTRARS) or any((cls or {}).get(w) in ("request", "declare") for w in words):
            out.append(t)
    return out


SHARED = {}


def shared(rel):
    """Whether a platform file is part of the app interface: it says
    `# interface: shared` in its header (interface.hibr loads it)."""
    if rel not in SHARED:
        try:
            SHARED[rel] = any(l.startswith("# interface: shared")
                              for l in open(os.path.join(ROOT, "programs/desktop", rel), errors="replace"))
        except OSError:
            SHARED[rel] = False
    return SHARED[rel]


def interface():
    """The app interface's files in interface.hibr's own order, and the
    files that say they belong to it: the two must be one list."""
    text = open(os.path.join(ROOT, "programs/desktop/interface.hibr")).read()
    m = re.search(r"for dt_part in (.*?); do", text, re.S)
    listed = ["wm/%s.hibr" % w for w in m.group(1).replace("\\", " ").split()]
    listed += sorted(os.path.relpath(f, os.path.join(ROOT, "programs/desktop"))
                     for f in glob.glob(os.path.join(ROOT, "programs/desktop/widgets/*.hibr")))
    marked = sorted(os.path.relpath(f, os.path.join(ROOT, "programs/desktop"))
                    for f in glob.glob(os.path.join(ROOT, "programs/desktop/wm/*.hibr")) +
                    glob.glob(os.path.join(ROOT, "programs/desktop/widgets/*.hibr"))
                    if shared(os.path.relpath(f, os.path.join(ROOT, "programs/desktop"))))
    return listed, marked


STUBS = os.path.join(ROOT, "programs", "desktop", "requests.hibr")


def stubargs(sig):
    """A signature's parameters as the words a stub forwards them with."""
    inner = sig[sig.index("(") + 1:sig.rindex(")")].strip()
    if not inner:
        return '"$@"'
    out = []
    for p in inner.split(","):
        p = p.split("=")[0].strip()
        words = p.split()
        if not words:
            continue
        nm = words[-1]
        if nm.startswith("..."):
            out.append('"${%s[@]}"' % nm[3:])
        else:
            out.append('"$%s"' % nm)
    return " ".join(out)


def tablerows():
    """The calls table as written, the contract: stubs come from it, never
    from whichever installables one run happens to read."""
    return [l.rstrip("\n").split("\t") for l in open(OUT)
            if l.strip() and not l.startswith("#")]


def stubtext(rows):
    """requests.hibr: every request and declaration in the calls table as a
    function of the same signature that sends itself to the desktop, for
    the app runtime to source after interface.hibr (ADR 0050). Generated,
    and checked against the table: never edited by hand."""
    out = ["#!/usr/bin/env hibr", "#",
           "# requests -- the app interface's other half (ADR 0050): every request and",
           "# declaration an app may make of the desktop, as a function with the",
           "# platform's own signature that sends itself to the desktop over the",
           "# app's socket (dt_rtsend, runtime.hibr). The app runtime sources it after",
           "# interface.hibr; the desktop never does. Generated from",
           "# programs/desktop/interface.tsv by `tools/appcalls.py --stubs`: never edit it by",
           "# hand, and tests/appcalls.py fails when it is not what the table says.",
           "", "strict", ""]
    for r in sorted(rows, key=lambda r: r[1]):
        cls, name, sig, answer = r[0], r[1], r[7], r[5]
        if cls not in ("request", "declare") or not sig or sig == "-":
            continue
        params = sig[sig.index("(") + 1:sig.rindex(")")].strip()
        head = "fn %s(%s)" % (name, params) if params else "%s()" % name
        verb = "declare" if cls == "declare" else ("ask" if answer in ("asked", "waited") else "tell")
        out.append("# %s, %s." % ({"declare": "Declared to the desktop", "ask": "Asked of the desktop",
                                    "tell": "Told to the desktop"}[verb], r[2]))
        out.append("%s {" % head)
        # An asked request hands its answer on to whoever called it; a waited
        # one has none to hand, only its status.
        out.append("\tdt_rtsend %s %s %s%s" % (verb, name, stubargs(sig),
                                                " && ret -k" if answer == "asked" else ""))
        out.append("}")
        out.append("")
    return "\n".join(out)


def classify(name, rel):
    if name in OVERRIDE:
        return OVERRIDE[name]
    if shared(rel):
        return "local"
    for k, v in BYFILE.items():
        if rel == k or (k.endswith("/") and rel.startswith(k)):
            return v
    return "decide"


def main():
    # Floors read the table as written and the catalogue's own files, nothing
    # this run would survey, so they are answered first and alone: mkindex
    # reads every line as a floor.
    if "--floors" in sys.argv:
        for k, v in sorted(derivedfloors().items()):
            print("%s\t%s" % (k, v))
        return
    where = defs()
    use, users, bad = collections.Counter(), collections.defaultdict(set), []
    for f in installables():
        try:
            r = records(f, "calls")
        except Exception as e:
            bad.append(f)
            continue
        facts = r.get("calls", [])
        # A platform function named as an argument -- a menu item's command,
        # a job's callback, a ticker, a hook -- is used as much as one called.
        try:
            facts = facts + records(f, "refs", symbols=sorted(where)).get("refs", [])
        except Exception:
            bad.append(f)
        for c in facts:
            n = c.get("name") or c.get("command") or c.get("word")
            if isinstance(n, dict):
                n = n.get("text")
            if n in where:
                use[n] += 1
                users[n].add(f)
    ask = asked(installables(), set(use))
    ret = rets(where)
    rows = sorted(((classify(n, where[n]), n, where[n], use[n], len(users[n]),
                    ("asked" if n in ret else "waited") if ask[n] else "told") for n in use),
                  key=lambda r: (r[0], -r[3]))
    cls = {r[1]: r[0] for r in rows}
    far = reaches(cls)
    sig = sigs()
    old = readtable(open(OUT).read()) if os.path.exists(OUT) else {}
    nv = nextver()
    rows = [r + (far.get(r[1], "-"), sig.get(r[1], "-"),
                 old[r[1]][2] if r[1] in old else (nv if old else "-")) for r in rows]
    rows = [r + (argsfield(r[1], r[7], nv),) for r in rows]
    kinds = collections.Counter(r[0] for r in rows)
    print("installables read: %d, unreadable: %d" % (len(installables()), len(bad)))
    print("platform functions called: %d -- %s" % (len(rows), ", ".join(
        "%s %d" % (k, kinds[k]) for k in ("local", "request", "declare", "gone", "decide"))))
    req = [r for r in rows if r[0] == "request"]
    print("requests: %d told, %d asked (an answer the app takes): %s" % (
        sum(1 for r in req if r[5] == "told"), sum(1 for r in req if r[5] in ("asked", "waited")),
        ", ".join(r[1] for r in req if r[5] == "asked")))
    print("local functions that reach a request: %d" % len(far))
    for n in sorted(far):
        print("  %-22s %s" % (n, far[n]))
    for r in rows:
        if r[0] == "decide":
            print("  decide  %-26s %-24s %4d calls in %d files" % (r[1], r[2], r[3], r[4]))
    if "--write" in sys.argv:
        with open(OUT, "w") as o:
            o.write("# class\tfunction\tdefined in\tcalls\tfiles\tanswer\treaches\tsignature\tsince\targs"
                    " -- tools/appcalls.py --write; since is the release a row first shipped in, - before"
                    " the contract; args is N@release for each optional parameter added since, a call of"
                    " N words or more needing that release; no field is ever empty, since a tab is IFS"
                    " whitespace and a shell reading the table would lose one (ADR 0050)\n")
            for r in rows:
                o.write("%s\t%s\t%s\t%d\t%d\t%s\t%s\t%s\t%s\t%s\n" % r)
        print("wrote", os.path.relpath(OUT, ROOT))
    for f in bad:
        print("unreadable:", f)
    if "--manifest" in sys.argv:
        files, stubs, mixed, need = manifest(cls)
        print("the runtime reaches %d platform functions in %d files, and stubs %d requests "
              "and declarations" % (len(need), len(files), len(stubs)))
        print("%d whole, %d mixed" % (len(files) - len(mixed), len(mixed)))
        for f in files:
            if f in mixed:
                keep, other, loads = mixed[f]
                print("  mixed  %-24s %3d needed beside %2d requests or declarations, %d run at load: %s" % (
                      f, len(keep), len(other), len(loads), " ".join(keep)[:70]))
                for t in loads:
                    print("           at load: %s" % t[:90])
            else:
                print("  whole  %s" % f)
    if "--stubs" in sys.argv:
        open(STUBS, "w").write(stubtext(tablerows()))
        print("wrote", os.path.relpath(STUBS, ROOT))
        return
    if "--writes" in sys.argv:
        files, stubs, mixed, need = manifest(cls)
        state = {}
        for l in open(os.path.join(ROOT, "docs/adr/0050-state.tsv")):
            if not l.startswith("#"):
                c = l.rstrip("\n").split("\t")
                state[c[0]] = c[4]
        body = bodies()
        bad = 0
        for n in sorted(need):
            hits = set()
            for m in re.finditer(r"(?:^|[\s;(&|])([A-Z][A-Z0-9_]*)(?:\[[^\]]*\])*\+?=", body.get(n, ""), re.M):
                if state.get(m.group(1)) in ("mirror", "setting", "request"):
                    hits.add("%s(%s)" % (m.group(1), state[m.group(1)]))
            if hits:
                bad += 1
                print("  %-24s writes %s" % (n, " ".join(sorted(hits))))
        print("%d needed functions write desktop state" % bad)
        return
    if "--why" in sys.argv:
        files, stubs, mixed, need = manifest(cls)
        for a in sys.argv[sys.argv.index("--why") + 1:]:
            path, x = [a], a
            while x in manifest.why and len(path) < 20:
                x = manifest.why[x]
                path.append(x)
            print("%s: %s" % (a, " < ".join(path) if a in need else "not needed"))
        return
    if "--check" in sys.argv:
        undecided = [r[1] for r in rows if r[0] == "decide"]
        for n in undecided:
            print("FAIL %s (%s) is called by an installable and not classed: "
                  "add it to BYFILE or OVERRIDE in tools/appcalls.py (ADR 0050)" % (n, where[n]))
        for f in bad:
            print("FAIL %s could not be read by the inspect module" % f)
        fails = len(undecided) + len(bad)
        ref = lasttag()
        broken = contract(ref) if ref else None
        if broken is None:
            print("note: no calls table at %s yet, so no contract to hold it to" % (ref or "a tag"))
        else:
            for b in broken:
                print("FAIL %s: a breaking change to the app interface (ADR 0050)" % b)
            fails += len(broken)
        for f, ln, n, fn in doubled(ROOT, [x for x in installables() if x.startswith(ROOT + os.sep)]):
            print("FAIL %s:%d calls %s right after dt_set %s, which the desktop runs itself (dt_onset): "
                  "drop the call" % (os.path.relpath(f, ROOT), ln, fn, n))
            fails += 1
        for b in floors(rows):
            print("FAIL %s: raise its floor" % b)
            fails += 1
        if not os.path.exists(STUBS) or open(STUBS).read() != stubtext(tablerows()):
            print("FAIL programs/desktop/requests.hibr is not what the calls table says: "
                  "run tools/appcalls.py --stubs")
            fails += 1
        listed, marked = interface()
        for f in sorted(set(listed) - set(marked)):
            print("FAIL interface.hibr loads %s, which does not say `# interface: shared`" % f)
            fails += 1
        for f in sorted(set(marked) - set(listed)):
            print("FAIL %s says `# interface: shared` and interface.hibr does not load it" % f)
            fails += 1
        files, stubs, mixed, need = manifest(cls)
        for f in files:
            if f.startswith(("wm/", "widgets/")) and f not in listed:
                print("FAIL the app runtime needs %s from %s, which is not in the app interface: %s" % (
                      ", ".join(sorted(n for n in need if where.get(n) == f))[:120], f,
                      "move it to a shared file (tools/appcalls.py --manifest)"))
                fails += 1
        for f in listed:
            for t in toplevel(f, cls):
                if not re.search(r"\bdt_ticker dt_jobtick\b|^dt_glyphs |^dt_locload$", t):
                    print("FAIL %s, part of the app interface, registers at load: %s" % (f, t[:80]))
                    fails += 1
        if not os.path.isdir(APPS):
            print("note: no catalogue at %s, whose installables decide some requests' classes: "
                  "the classes are not compared with interface.tsv" % APPS)
        for n, old, new, why in (flips(rows) if os.path.isdir(APPS) else []):
            mine = lambda f: f.startswith(ROOT + os.sep)
            sites = ["%s:%s (%s)" % (os.path.relpath(f, ROOT) if mine(f) else f, ln, w) for f, ln, w in why[:4]]
            print("FAIL %s is %s in interface.tsv and %s now: %s -- read why, then take it with "
                  "tools/appcalls.py --write" % (n, old, new, "; ".join(sites) or "no site wants an answer now"))
            fails += 1
        for x in binders(tablerows()):
            print("FAIL %s:%s binds %s, which answers with no value (it never calls ret): "
                  "test its status, or have it ret what it means" % x)
            fails += 1
        qm, tail = statusholes(tablerows())
        for x in qm:
            print("FAIL %s:%s reads $? after %s, a told request whose status is 0 in an app's own "
                  "process: test it where it stands, or make it asked" % x)
        for x in tail:
            print("FAIL %s:%s %s ends with %s, a told request, and a caller tests %s's status: "
                  "end it on something of its own, or make the request asked" % (x[0], x[1], x[2], x[3], x[2]))
        fails += len(qm) + len(tail)
        hot = frameasks(tablerows())
        if len(hot) > FRAMEASKS and os.path.isdir(APPS):
            for f, g, path in sorted(hot)[:20]:
                print("  %s: %s" % (f, path))
            print("FAIL %d asked requests are reachable from a frame path (_draw, _dirty, _idle, a ticker), "
                  "more than the %d held to: an ask there is a round trip a frame" % (len(hot), FRAMEASKS))
            fails += 1
        elif len(hot) < FRAMEASKS:
            print("note: %d asked requests reachable from a frame path, under the %d held to: "
                  "lower FRAMEASKS in tools/appcalls.py" % (len(hot), FRAMEASKS))
        print("\n%d passed, %d failed" % (len(rows) - len(undecided) + 3, fails))
        sys.exit(1 if fails else 0)


main()
