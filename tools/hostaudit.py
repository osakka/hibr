#!/usr/bin/env python3
"""Whether each installable can run hosted -- in a process of its own (ADR
0050, Decision 8) -- as it stands, and what it waits on or needs if not.

  python3 tools/hostaudit.py [--md FILE] [--tsv FILE]   # every entry: tree and catalogue
  python3 tools/hostaudit.py --root DIR [--md FILE]       # a checkout laid out by kind (huiamp)
  python3 tools/hostaudit.py --detail ENTRY...            # one entry's calls, callbacks and state
  python3 tools/hostaudit.py --check                      # the tree's half, as a gate

An entry is what one process runs: its file and its parts (`# part:`),
a catalogue entry's bundle and the parts its index names. Each is read by
the inspect module, never run, and judged on four things:

  calls     every platform function it calls is in interface.tsv -- a
            request with its stub in requests.hibr, a local the runtime
            carries, or a declaration -- and nothing it calls by a platform
            prefix is gone from the platform (a name no file defines)
  state     every desktop global it reads is shared (dt_share, dt_keep) or
            the app's own, or a conversion named in tools/appstate.py's
            CONVERT; it writes none (in its own process the guard refuses it)
  callbacks every kind of call the desktop makes into it (seamcalls.tsv's
            kinds: a window's _menus, a pane's rows, a bar item's press, a
            handed callback, a hook) is carried for a hosted process --
            derived from this tree, not listed: a window callback through
            dt_wincall unless rtview.hibr's dt_rvhas refuses its kind, one
            through dt_winget unless dt_rvget refuses it, one through
            dt_appfn only where the desktop's calling function routes a
            hosted window (reads DT_RVC); a view of another KIND only once
            runtime.hibr's dt_rtview takes that kind
  more      pictures (img, media, web, xscr, pdf, frames draw at absolute
            coordinates or by descriptor, which the cell record does not
            carry), background work with no window (a ticker in an app,
            whose process ends with its last window), and calls into a
            separate installable (behind dt_has, which is false in another
            process: ADR 0050, Review 6a)

The verdict: `hosted now`, `waits on` the tracks still to carry something
it needs (the entry unchanged), or `needs a change` to the entry itself.
Which track carries what is the one hand table here (TRACK); whether it is
carried yet is read from the tree.

Reads ~/hibr-apps (or $HIBR_APPS) and never writes it.
"""
import collections, glob, json, os, re, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
DESK = os.path.join(ROOT, "programs", "desktop")
sys.path.insert(0, os.path.join(ROOT, "tests"))
sys.path.insert(0, HERE)
from inspectlib import records
import appcalls
import appstate

APPS = os.environ.get("HIBR_APPS", os.path.expanduser("~/hibr-apps"))
CODEKINDS = ("apps", "system", "control-panel", "control-strip", "menubar", "savers", "lib")
VIEWKIND = {"control-panel": "pane", "control-strip": "strip", "menubar": "bar", "savers": "saver"}
# The verbs that put a picture in a window -- by descriptor or at absolute
# coordinates -- which the cell record does not carry (pictures stay on
# `frames`, ADR 0050, Cells measured).
PICTURES = {"img": ("draw", "keep", "pic"), "media": ("draw", "frame"), "web": ("draw", "render"),
            "pdf": ("view",), "frames": ("place",), "xscr": ()}

# Which track carries a kind not carried yet (the owner's tracks after the
# spine, 2026-11-21). Organisational, so by hand; whether a kind IS carried
# is never written here.
TRACK = {"view": "views", "pane": "views", "strip": "views", "bar": "views", "saver": "views",
         "row": "views", "menus": "menus", "context": "menus", "titlemenu": "menus",
         "handed": "menus", "list": "menus", "canclose": "crash"}


def track(need):
    """The track a need waits on, from its kind's first word."""
    if need in ("hook:cs_module", "hook:cp_pane", "hook:dt_barhook"):
        return "views"
    k = re.split(r"[:_ ]", need.lstrip("_"), maxsplit=1)[0]
    if k == "partly":
        return track(need.split(":", 1)[1])
    return TRACK.get(k, "unassigned")


def src(rel):
    return open(os.path.join(DESK, rel), errors="replace").read().split("\n")


def fnspan(lines, at):
    """The text of the function holding line `at` (1-based): from its
    definition back up to its closing brace at column 0."""
    i = at - 1
    while i > 0 and not re.match(r"^(fn\s+)?[A-Za-z_][A-Za-z0-9_]*\s*\(", lines[i]):
        i -= 1
    j = at - 1
    while j < len(lines) and not lines[j].startswith("}") and not (j == i and lines[j].rstrip().endswith("}")):
        j += 1
    return "\n".join(lines[i:j + 1])


def carriage():
    """What the tree carries for a hosted process, read from it: {kind: True
    or False} for every kind seamcalls.tsv names, and the view kinds
    runtime.hibr's dt_rtview takes."""
    rv = "\n".join(src("wm/rtview.hibr"))
    m = re.search(r"fn dt_rvhas\(.*?\n\}", rv, re.S)
    refused = set()
    if m:
        for arm in re.findall(r"^\s*([a-z| ]+)\)\s*$", m.group(0), re.M):
            refused |= set(w.strip() for w in arm.split("|"))
    m = re.search(r"fn dt_rvget\(.*?\n\}", rv, re.S)
    getnot = bool(m and "not carried" in m.group(0))
    rt = "\n".join(open(os.path.join(DESK, "runtime.hibr"), errors="replace").read().split("\n"))
    m = re.search(r"fn dt_rtview\(.*?\n\}", rt, re.S)
    views = set(re.findall(r'"\$kind"\s*!?=\s*([a-z]+)', m.group(0))) if m else set()
    views |= set(w for arm in re.findall(r"^\s*([a-z| ]+)\)", m.group(0) if m else "", re.M)
                 for w in arm.replace(" ", "").split("|") if w)
    runs = set(re.findall(r"\b(dt_[a-z]+)\b", rt))
    sites = collections.defaultdict(list)
    for l in open(os.path.join(DESK, "seamcalls.tsv")):
        if l.startswith("#") or not l.strip():
            continue
        f, ln, fn, seam, cb, kind = l.rstrip("\n").split("\t")[:6]
        if seam == "dt_wincall":
            ok = kind.lstrip("_") not in refused
        elif seam == "dt_winget":
            ok = not getnot
        elif fn in runs:
            ok = True
        else:
            ok = bool(re.search(r"DT_RV[A-Z]*\[|\$hv\b", fnspan(src(f), int(ln))))
        sites[kind].append(ok)
    # A kind dt_rvhas refuses by name is refused however it is called; one
    # whose sites route a hosted window in some places and not others is
    # partly carried, which is a need (the unrouted sites are named).
    out = {}
    for k, v in sites.items():
        out[k] = "yes" if all(v) and k.lstrip("_") not in refused else ("partly" if any(v) else "no")
    for k in refused:
        out["_" + k] = "no"
    return out, views


def platdefs():
    """Every function the platform defines, wm/, widgets/, lib/ and the
    `# platform:` files, by name -> file."""
    where = dict(appcalls.defs())
    for f in glob.glob(os.path.join(DESK, "**", "*.hibr"), recursive=True):
        if appcalls.platform(f) or f.endswith(("/desktop.hibr", "/interface.hibr", "/runtime.hibr")):
            for line in open(f, errors="replace"):
                m = re.match(r"\s*(?:fn\s+)?([A-Za-z_][A-Za-z0-9_]*)\s*\(", line)
                if m:
                    where.setdefault(m.group(1), os.path.relpath(f, DESK))
    return where


def registrars(body):
    """Which platform functions take a callback the desktop calls back later,
    and the kind each is: one that stores the caller as its owner beside a
    callback (a menu row, a note's action, a dialog's answer: `handed`), one
    that records a hook's owner (dt_hookown: a bar hook, a bus handler, a
    location, a strip module), and each function that hands its words
    straight to one of them (dt_notep to dt_noteq). One hop, never the whole
    call graph: almost everything reaches a note somewhere, and a function
    that only might is no registrar."""
    direct = {}
    for n, b in body.items():
        if re.search(r'=\$\{?DT_CALLER\b|="\$DT_CALLER"', b) and n not in ("dt_appfn", "dt_hookown"):
            direct[n] = "handed"
        if re.search(r"\bdt_hookown\b", b) and n not in ("dt_hookown",):
            direct.setdefault(n, "hook:" + n)
    out = dict(direct)
    for n, b in body.items():
        if n in out:
            continue
        for y in re.findall(r"\b([a-z][a-z0-9]*_[a-z0-9_]+)\b", b):
            if y in direct and y != n:
                out[n] = direct[y]
                break
    # A menu row's command is the menus' own (an app's _menus), and a note
    # hands a callback only when one of its words names a function.
    return out


def verof(path):
    """The version an entry says it is, as mkindex reads it."""
    try:
        for l in open(path, errors="replace"):
            m = re.match(r'^app[ \t]+[A-Za-z0-9_-]+[ \t]+"[^"]*"[ \t]+([0-9][0-9A-Za-z.+~-]*)', l)
            if m:
                return m.group(1)
    except OSError:
        pass
    return "-"


def kindof(path):
    parts = os.path.abspath(path).split(os.sep)
    for i in range(len(parts) - 1, -1, -1):
        if parts[i] in CODEKINDS:
            return parts[i]
    return "?"


def partsof(path, base):
    out = []
    for l in open(path, errors="replace"):
        m = re.match(r"^#\s*part:\s*(\S+)", l)
        if m:
            out.append(os.path.join(base, m.group(1)))
    return out


def tree_entries(base=DESK, prefix="tree"):
    """Every installable of a checkout laid out by kind folders, its parts
    folded into the entry that brings them."""
    fs = []
    for k in CODEKINDS:
        fs += glob.glob(os.path.join(base, k, "**", "*.hibr"), recursive=True)
    fs = sorted(f for f in fs if not appcalls.platform(f) and "/tests/" not in f)
    parts = set()
    for f in fs:
        parts |= set(p for p in partsof(f, base) if p.endswith(".hibr"))
    out = []
    for f in fs:
        if f in parts:
            continue
        k = kindof(f)
        name = "%s/%s" % (k, os.path.basename(f)[:-5])
        files = [f] + [p for p in partsof(f, base) if p.endswith(".hibr") and os.path.exists(p)]
        needs = []
        for l in open(f, errors="replace"):
            m = re.match(r"^#\s*needs:\s*(.+)$", l)
            if m:
                needs += m.group(1).split()
        out.append({"entry": name, "where": prefix, "version": verof(f), "kind": k, "files": files, "needs": needs})
    return out


def catalogue_entries():
    idx = json.load(open(os.path.join(APPS, "index.json")))["entries"]
    out = []
    for k, e in sorted(idx.items()):
        kind = e.get("kind")
        b = e.get("bundle")
        if kind not in CODEKINDS or not b or not b.endswith(".hibr"):
            out.append({"entry": k, "where": "catalogue", "version": e.get("version", "-"), "kind": kind,
                        "files": [], "data": True, "hibr": e.get("hibr", "-")})
            continue
        files = [os.path.join(APPS, b)] + [os.path.join(APPS, p["path"]) for p in e.get("parts", [])
                                           if p.get("path", "").endswith(".hibr")]
        out.append({"entry": k, "where": "catalogue", "version": e.get("version", "-"), "kind": kind,
                    "files": [f for f in files if os.path.exists(f)], "hibr": e.get("hibr", "-"),
                    "needs": re.split(r"[,\s]+", e.get("needs", "") or "") if isinstance(e.get("needs", ""), str)
                    else list(e.get("needs", []))})
    return out


class Audit:
    def __init__(self, entries):
        self.entries = entries
        self.where = platdefs()
        self.table = appcalls.readtable(open(appcalls.OUT).read())
        self.stubs = set(re.findall(r"^(?:fn\s+)?([A-Za-z_][A-Za-z0-9_]*)\s*\(",
                                    open(appcalls.STUBS).read(), re.M))
        self.carried, self.views = carriage()
        self.body = appcalls.bodies()
        self.reg = registrars(self.body)
        allfiles = sorted(set(f for e in entries for f in e["files"]))
        self.files = allfiles
        names = appstate.globals_()
        self.reads, self.writes = appstate.inventory(allfiles, names) if allfiles else ({}, {})
        self.rfile = collections.defaultdict(set)
        self.wfile = collections.defaultdict(set)
        for n, fs in self.reads.items():
            for f in fs:
                self.rfile[f].add(n)
        for n, fs in self.writes.items():
            for f in fs:
                self.wfile[f].add(n)
        # Every name any installable or the platform defines, for a call by
        # a platform prefix to a function nobody defines.
        self.defined = set(self.where)
        self.owner = collections.defaultdict(set)
        for f in allfiles:
            for r in records(f).get("functions", []):
                self.defined.add(r["name"])
                self.owner[r["name"]].add(f)
        self.prefixes = set(n.split("_", 1)[0] + "_" for n in self.where if "_" in n
                            and sum(1 for m in self.where if m.startswith(n.split("_", 1)[0] + "_")) >= 5)

    def joined(self, e):
        """The files of the entries this one is one process with: a bundle's
        members, joined by `# needs:` (ADR 0050, Review 6a), never a
        library or a command-line entry."""
        out = set()
        names = set(x["entry"] for x in self.entries if x["files"] and set(x["files"]) & set(e["files"]))
        for x in self.entries:
            if x["kind"] in ("lib", "cli") or not x["files"]:
                continue
            if x.get("where") != e.get("where"):
                continue
            nx, ne = set(x.get("needs", ())), set(e.get("needs", ()))
            if (nx & names) or (ne & {x["entry"]}):
                out |= set(x["files"])
        return out

    def calls(self, f):
        try:
            facts = records(f, "calls", arguments=("dt_app", "dt_note", "dt_notep", "dt_notify", "dt_notifyp")
                            + tuple(PICTURES)).get("calls", [])
        except Exception as e:
            return None
        try:
            facts = facts + records(f, "refs", symbols=sorted(self.where)).get("refs", [])
        except Exception:
            pass
        return facts

    def judge(self, e):
        """An entry's needs: {need: [evidence...]}, each need a kind word, and
        `change:` for one the entry itself must make."""
        need = collections.defaultdict(list)
        if e.get("data"):
            return need, {}
        mine = set(e["files"]) | self.joined(e)
        own = set()
        for f in e["files"]:
            for r in records(f).get("functions", []):
                own.add(r["name"])
        detail = {"calls": collections.Counter(), "state": set(), "callbacks": set()}
        prefixes = set()
        for f in e["files"]:
            k = kindof(f)
            if k in VIEWKIND and VIEWKIND[k] not in self.views:
                need["view:" + VIEWKIND[k]].append(os.path.basename(f))
            facts = self.calls(f)
            if facts is None:
                need["change:unreadable"].append(os.path.basename(f))
                continue
            for c in facts:
                n = c.get("name") or c.get("command") or c.get("word")
                if isinstance(n, dict):
                    n = n.get("text")
                if not n:
                    continue
                if n == "dt_app":
                    args = c.get("arguments") or []
                    if args and args[0].get("text"):
                        prefixes.add(args[0]["text"])
                if n in PICTURES and n not in own:
                    verb = ((c.get("arguments") or [{}])[0].get("text") or "")
                    if not PICTURES[n] or verb in PICTURES[n]:
                        need["pictures:%s %s" % (n, verb)].append("%s:%s" % (os.path.basename(f), c.get("line", "?")))
                if n in self.where and n not in own:
                    detail["calls"][n] += 1
                    row = self.table.get(n)
                    if row is None and not self.where[n].startswith(("wm/", "widgets/", "lib/")):
                        # The Control Panel's or the saver framework's own
                        # functions a pane or a saver calls: the contract its
                        # view kind brings, not the app interface's.
                        need["view:api:%s" % n].append(os.path.basename(f))
                    elif row is None:
                        if not self.where[n].startswith("lib/"):
                            need["change:not in interface.tsv:" + n].append(os.path.basename(f))
                    elif row[0] == "request" and n not in self.stubs:
                        need["change:no stub:" + n].append(os.path.basename(f))
                    elif row[0] == "declare" and self.where[n] == "wm/menus.hibr":
                        pass
                    elif row[0] == "declare" and self.where[n] in ("wm/bar.hibr", "wm/strip.hibr"):
                        need["view:" + ("bar" if "bar" in self.where[n] else "strip")].append(n)
                    if n in self.reg and n not in ("dt_app",) and self.where[n] != "wm/menus.hibr":
                        r = self.reg[n]
                        words = [x.get("text") or "" for x in (c.get("arguments") or [])]
                        if r == "handed" and n.startswith(("dt_note", "dt_notify")):
                            if any(w in self.defined for w in words[1:]):
                                need["handed"].append(n)
                        elif r == "handed":
                            need["handed"].append(n)
                        elif r != "hook:dt_ticker":
                            need[r].append(n)
                elif (n not in self.defined and "_" in n and n.split("_", 1)[0] + "_" in self.prefixes
                      and c.get("known") == "syntax" and re.fullmatch(r"[a-z][a-z0-9]*_[a-z0-9_]+", n)):
                    need["change:calls a platform function no file defines:" + n].append(
                        "%s:%s" % (os.path.basename(f), c.get("line", "?")))
                elif (n in self.owner and not (self.owner[n] & mine) and n not in self.where and n not in own
                      and not all("/lib/" in x for x in self.owner[n])):
                    need["change:calls another installable:" + n].append(
                        "%s:%s -> %s" % (os.path.basename(f), c.get("line", "?"),
                                         ",".join(sorted(os.path.basename(x) for x in self.owner[n]))))
                if n == "dt_ticker" and kindof(f) in ("apps", "system"):
                    need["background"].append("%s:%s" % (os.path.basename(f), c.get("line", "?")))
            # State.
            for n in self.rfile.get(f, ()):
                k = appstate.klass(n, self.writes.get(n), appstate.WHERE.get(n, "?"))
                detail["state"].add((n, k))
                if k == "?":
                    need["change:reads unshared desktop state:" + n].append(os.path.basename(f))
                elif n in appstate.CONVERT:
                    need["convert:%s:%s" % (appstate.CONVERT[n], n)].append(os.path.basename(f))
            for n in self.wfile.get(f, ()):
                k = appstate.klass(n, self.writes.get(n), appstate.WHERE.get(n, "?"))
                if k in ("request", "shared"):
                    need["change:writes desktop state:" + n].append(os.path.basename(f))
        # Callbacks: a window's, by the prefixes its dt_app lines register.
        for f in e["files"]:
            for r in records(f).get("functions", []):
                nm = r["name"]
                for p in prefixes:
                    if nm.startswith(p + "_"):
                        kind = "_" + nm[len(p) + 1:]
                        if kind in self.carried:
                            detail["callbacks"].add(kind)
                            if self.carried[kind] != "yes":
                                need[("partly:" if self.carried[kind] == "partly" else "") + kind.lstrip("_")].append(nm)
        return need, detail

    def verdict(self, need):
        if not need:
            return "hosted now"
        ch = sorted(n for n in need if n.startswith("change:"))
        rest = sorted(n for n in need if not n.startswith("change:"))
        tr = sorted(set(track(n) for n in rest))
        if ch:
            return "needs a change"
        return "waits on " + ", ".join(tr)


def row(a, e):
    need, _ = a.judge(e)
    v = "data, never runs" if e.get("data") else a.verdict(need)
    ch = [n[len("change:"):] for n in sorted(need) if n.startswith("change:")]
    wt = collections.defaultdict(list)
    for n in sorted(need):
        if not n.startswith("change:"):
            wt[track(n)].append(n)
    return v, ch, wt, need


def shortlist(xs, k=6):
    xs = list(xs)
    return ", ".join(xs[:k]) + (" (+%d)" % (len(xs) - k) if len(xs) > k else "")


def main():
    args = sys.argv[1:]
    if "--root" in args:
        base = os.path.abspath(args[args.index("--root") + 1])
        entries = tree_entries(base, os.path.basename(base))
    elif "--check" in args:
        entries = tree_entries()
    else:
        entries = tree_entries() + (catalogue_entries() if os.path.isdir(APPS) else [])
    if "--detail" in args:
        want = args[args.index("--detail") + 1:]
        entries = [e for e in entries if e["entry"] in want or any(w in e["entry"] for w in want)]
    a = Audit(entries)
    rows = [(e, row(a, e)) for e in entries]
    if "--check" in args:
        return check(a, rows)
    if "--detail" in args:
        for e, (v, ch, wt, need) in rows:
            _, d = a.judge(e)
            print("== %s (%s %s): %s" % (e["entry"], e["where"], e["version"], v))
            print("   files: %s" % ", ".join(os.path.relpath(f, os.path.dirname(os.path.dirname(f))) for f in e["files"]))
            for n, c in sorted(d.get("calls", {}).items()):
                r = a.table.get(n, ("?", "", "-"))
                ans = [l.split("\t")[5] for l in open(appcalls.OUT) if l.split("\t")[1:2] == [n]]
                print("   call  %-22s %4d  %-8s %-7s %s" % (n, c, r[0], ans[0] if ans else "-", a.where.get(n, "?")))
            for k in sorted(d.get("callbacks", ())):
                print("   back  %-22s %s" % (k, {"yes": "carried", "partly": "partly carried (%s)" % track(k.lstrip("_"))}.get(a.carried.get(k), "not carried yet (%s)" % track(k.lstrip("_")))))
            for n, k in sorted(d.get("state", ())):
                print("   reads %-22s %s" % (n, k))
            for n in sorted(need):
                print("   need  %s: %s" % (n, shortlist(need[n], 4)))
        return 0
    head = hdr(a, rows)
    print(head)
    out = []
    for e, (v, ch, wt, need) in rows:
        waits = "; ".join("%s: %s" % (t, shortlist(sorted(set(wt[t])), 5)) for t in sorted(wt))
        out.append((e["entry"], e["where"], e["version"], v, "; ".join(shortlist(sorted(set(need["change:" + c])), 2) and
                    "%s (%s)" % (c, shortlist(sorted(set(need["change:" + c])), 2)) for c in ch) or "-", waits or "-"))
    if "--tsv" in args:
        with open(args[args.index("--tsv") + 1], "w") as o:
            o.write("# entry\twhere\tversion\tverdict\tchange it needs\twaits on\n")
            for r in out:
                o.write("\t".join(r) + "\n")
    if "--md" in args:
        with open(args[args.index("--md") + 1], "w") as o:
            o.write(head.replace("\n", "  \n") + "\n\n")
            o.write("| entry | where | version | verdict | change it needs | waits on (track: what) |\n|---|---|---|---|---|---|\n")
            for r in out:
                o.write("| " + " | ".join(x.replace("|", "/") for x in r) + " |\n")
    c = collections.Counter(r[3].split(" on ")[0] if r[3].startswith("waits") else r[3] for r in out)
    print("verdicts: " + ", ".join("%s %d" % kv for kv in sorted(c.items())))
    for r in out:
        print("%-34s %-9s %-9s %-24s %s | %s" % (r[0][:34], r[1][:9], r[2][:9], r[3][:24], r[4][:70], r[5][:90]))
    return 0


def hdr(a, rows):
    def git(d, *cmd):
        return subprocess.run(["git", "-C", d] + list(cmd), capture_output=True, text=True).stdout.strip()
    s = "hostaudit: tree %s" % git(ROOT, "rev-parse", "--short", "HEAD")
    if os.path.isdir(APPS):
        dirty = git(APPS, "status", "--porcelain")
        s += "; catalogue %s at %s%s" % (APPS, git(APPS, "rev-parse", "--short", "HEAD"),
                                         " (dirty: %d paths)" % len(dirty.splitlines()) if dirty else " (clean)")
    s += "\ncarried for a hosted process now: views %s; window callbacks %s" % (
        " ".join(sorted(a.views)) or "none",
        " ".join(sorted(k for k, v in a.carried.items() if v == "yes" and k.startswith("_"))))
    s += "\npartly (some of the desktop's calling sites route a hosted window, some do not): %s" % (
        " ".join(sorted(k for k, v in a.carried.items() if v == "partly")))
    s += "\nnot carried yet: %s" % " ".join(sorted(k for k, v in a.carried.items() if v == "no"))
    s += ("\nglobal, for every entry: registrations are still run, not read (Decision 3: dt_app, cp_pane, "
          "cs_module, dt_baritem, dt_keep read by inspect at scan) -- no track builds that")
    return s


# The tree's half as a gate (tests/appcalls.py): a call by a platform prefix
# to a function no file defines fails outright -- appcalls' table cannot see
# it, since it reads only names the platform defines -- and the calls one
# installable makes into another separate one are held to this many and
# only lowered (each is converted before the flip, ADR 0050 Review 6a).
CROSS = 23


def check(a, rows):
    fails = 0
    cross = 0
    for e, (v, ch, wt, need) in rows:
        for n in sorted(need):
            if n.startswith("change:calls a platform function no file defines:"):
                print("FAIL %s calls %s, a platform-shaped name no file defines: %s" % (
                    e["entry"], n.rsplit(":", 1)[1], shortlist(need[n], 3)))
                fails += 1
            if n.startswith("change:calls another installable:"):
                cross += len(need[n])
                print("  %s: %s" % (e["entry"], shortlist(need[n], 3)))
            if n.startswith("change:unreadable"):
                print("FAIL %s: inspect could not read %s" % (e["entry"], shortlist(need[n])))
                fails += 1
    if cross > CROSS:
        print("FAIL %d calls from one installable into a separate one, more than the %d held to: "
              "in a process of its own the guard is false and the feature goes quiet (ADR 0050, Review 6a)"
              % (cross, CROSS))
        fails += 1
    elif cross < CROSS:
        print("note: %d calls into a separate installable, under the %d held to: lower CROSS in tools/hostaudit.py"
              % (cross, CROSS))
    print("\n%d passed, %d failed" % (len(rows) - fails, fails))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
