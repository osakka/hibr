#!/usr/bin/env python3
"""Every place the platform runs code whose name it does not write -- a
window's callback, a callback an app handed it, another installable's
hook, or the desktop's own -- classed by what it becomes when apps run in
processes of their own (ADR 0050). Read by the inspect module, never run.

    python3 tools/callsites.py [--write|--check]

  window   a call into an app's own code: dt_appfn, the one place, which
           sends it to the app's process when the app is hosted; every other
           call into an app goes through it
  handed   a callback an app handed the platform (a dialog's answer, a
           job's end, a menu command, a note's action), called by a name
           here: none may be -- each is stored with its owner (DT_CALLER,
           which dt_appfn sets) and called back through dt_appfn with it
  hook     another installable's registered hook (a bar item, a strip
           module, a location, a bus handler) called by a name here: none
           may be -- each records its owner when registered and is called
           through dt_hookfn, dt_prefixfn or dt_appfn with it
  desktop  the desktop's own: a program it starts, its own tickers, its own
           dispatch
  local    shared code (the app interface) calling a name in its own
           process: a widget calling the app that drew it; and the saver
           framework run on its own, with no desktop, calling its saver

--write puts programs/desktop/callsites.tsv; --check fails on a site no rule
classes, so a new one cannot go unsorted.
"""
import collections, glob, os, sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "tests"))
from inspectlib import records

OUT = os.path.join(ROOT, "programs", "desktop", "callsites.tsv")

# By the function the site is in.
BYOWNER = {
    "window": """dt_appfn""",
    "handed": """""",
    "hook": """dt_copylocated dt_openverbs dt_openverb dt_modehook dt_modetick dt_runatstart dt_unload dt_reinstate dt_barrescan
               dt_baritoggle dt_barikey dt_popmouse dt_popdraw dt_barido dt_barilay dt_bar
               dt_busgive dt_loccan cs_relist dt_stripdraw cs_press""",
    "local": """bd_put dt_dropcontext dt_omniask dt_sbto sv_call""",
    "desktop": """dt_tickers dt_job dt_jobmerge dt_jobvarsrun dt_runhandler dt_autohold
                  dt_isozygote dt_natver dt_pwlocal dt_supervise dt_updcheck dt_rtmsg dt_rtanswer dt_vw
                  dt_vwget dt_vwmatch""",
}
# One site at a time, where the function holds sites of two kinds:
# file:line of the call -> class. dt_mouse dispatches a bar item's click
# and wheel (hooks) and a window's title menu (window); dt_event a bar
# popup's key (hook) and a window's save (window).
BYSITE = {}
SPLIT = {
    "dt_mouse": {"_titlemenu": "window", "": "hook"},
    "dt_event": {"savefile": "window", "": "hook"},
}


def owners():
    out = {}
    for k, v in BYOWNER.items():
        for n in v.split():
            out[n] = k
    return out


def platform():
    """The installable kinds' files that say `# platform:` of themselves
    (ADR 0046): the Control Panel's window, the saver framework, the bar
    items the package ships -- the desktop's own, calling panes, savers and
    the like, so their calls into those are the desktop's too."""
    out = []
    for d in ("apps", "system", "control-panel", "control-strip", "menubar", "savers"):
        for f in glob.glob(os.path.join(ROOT, "programs/desktop", d, "**", "*.hibr"), recursive=True):
            if any(l.startswith("# platform:") for l in open(f, errors="replace")):
                out.append(f)
    return sorted(out)


def sites():
    """Every dynamic call in the platform's files: (file, line, owner, text)."""
    out = []
    files = (glob.glob(os.path.join(ROOT, "programs/desktop/wm/*.hibr")) +
             glob.glob(os.path.join(ROOT, "programs/desktop/widgets/*.hibr")) +
             platform())
    for f in sorted(files):
        lines = open(f, errors="replace").read().split("\n")
        rel = os.path.relpath(f, os.path.join(ROOT, "programs/desktop"))
        for c in records(f, "calls").get("calls", []):
            if c.get("name"):
                continue
            ln = c["span"]["line"]
            out.append((rel, ln, c.get("owner", ""), lines[ln - 1].strip()))
    return out


SHAREDF = {}


def shared(rel):
    """Whether a platform file is part of the app interface (its header says
    `# interface: shared`): code there runs in whichever process called it."""
    if rel not in SHAREDF:
        SHAREDF[rel] = any(l.startswith("# interface: shared") for l in
                           open(os.path.join(ROOT, "programs/desktop", rel), errors="replace"))
    return SHAREDF[rel]


def direct():
    """Calls of dt_appcall or dt_appget by name: an input handler reached
    around the seam, which a dynamic-call census cannot see -- the dynamic
    call is inside the shared budget.hibr. Only the seam's own dt_wincall and
    dt_winget, and wm/rtserve's, may make them; shared code included, since
    the desktop runs it too (a text field typing into its window)."""
    out = []
    files = (glob.glob(os.path.join(ROOT, "programs/desktop/wm/*.hibr")) +
             glob.glob(os.path.join(ROOT, "programs/desktop/widgets/*.hibr")))
    for f in sorted(files):
        rel = os.path.relpath(f, os.path.join(ROOT, "programs/desktop"))
        if rel in ("wm/rtserve.hibr", "wm/seam.hibr", "wm/budget.hibr"):
            continue
        for c in records(f, "calls").get("calls", []):
            if c.get("name") in ("dt_appcall", "dt_appget"):
                out.append((rel, c["span"]["line"], c.get("owner", "")))
    return out


def classify(site, own):
    rel, ln, owner, text = site
    if owner == "dt_appfn":
        return "window"
    if shared(rel):
        return "local"
    key = "%s:%d" % (rel, ln)
    if key in BYSITE:
        return BYSITE[key]
    if owner in SPLIT:
        for frag, k in SPLIT[owner].items():
            if frag and frag in text:
                return k
        return SPLIT[owner][""]
    return own.get(owner, "")


# Shared code runs in whichever process called it, so a call it makes by a
# name is local only while no code of the desktop's own reaches it: the
# desktop running a scrollbar's press, or a dropdown's namer, would be
# calling an app's code in its own process. Reached anyway, by reason:
REACHOK = {
    "bd_put": "the pen is a function of the code drawing the text",
    "dt_job": "runs a program",
    "dt_jobmerge": "runs a program",
    "dt_jobvarsrun": "runs the job's own body in the job's child",
    "dt_pwlocal": "runs a program",
    "sv_call": "goes through dt_prefixfn when the desktop runs it",
    "dt_tickers": "calls only a ticker with no owner -- the desktop's own in the desktop, the app's own in an app; an installable's goes through dt_appfn",
    "dt_appcall": "the seam's own dispatch, reached through dt_wincall",
    "dt_appget": "the seam's own dispatch, reached through dt_winget",
}


def reached():
    """For each shared function, a chain from desktop code that reaches it,
    following callers through shared code."""
    D = os.path.join(ROOT, "programs/desktop")
    fs = sorted(f for f in glob.glob(os.path.join(D, "**", "*.hibr"), recursive=True) if "/tests/" not in f)
    callers = collections.defaultdict(set)
    for f in fs:
        for c in records(f, "calls").get("calls", []):
            if c.get("name"):
                callers[c["name"]].add((os.path.relpath(f, D), c.get("owner", "")))
    desk = lambda rel: not shared(rel) and (rel.startswith("wm/") or os.path.join(D, rel) in platform())
    out = {}

    def up(fn):
        seen, todo = {fn}, [(fn, [fn])]
        while todo:
            name, path = todo.pop()
            for rel, owner in sorted(callers.get(name, ())):
                if desk(rel):
                    return " <- ".join(path + ["%s (%s)" % (owner or "top", rel)])
                if shared(rel) and owner and owner not in seen:
                    seen.add(owner)
                    todo.append((owner, path + [owner]))
        return ""
    return up


def main():
    own = owners()
    rows = [(classify(s, own),) + s for s in sites()]
    kinds = collections.Counter(r[0] or "unclassed" for r in rows)
    print("dynamic call sites: %d -- %s" % (len(rows), ", ".join(
        "%s %d" % (k, kinds[k]) for k in ("window", "handed", "hook", "desktop", "local", "unclassed"))))
    bad = [r for r in rows if not r[0]]
    # The seam is the one place a window's or an app's own code is called:
    # anything else that calls one has to go through it, and shared code --
    # which runs in an app's process as well -- never calls into another.
    cross = [r for r in rows if r[0] == "window" and r[3] != "dt_appfn"]
    cross += [r for r in rows if r[0] in ("handed", "hook")]
    cross += [r for r in rows if r[0] == "local" and shared(r[1]) and
              own.get(r[3]) in ("window", "hook") and r[3] not in ("dt_appcall", "dt_appget")]
    up = reached()
    reach = [(r, up(r[3])) for r in rows if r[0] == "local" and r[3] not in REACHOK]
    reach = [(r, why) for r, why in reach if why]
    for r in cross:
        print("  across  %s:%d in %s calls into an app by a name; it goes through dt_appfn, "
              "from a file of the desktop's own" % (r[1], r[2], r[3]))
    for r, why in reach:
        print("  reached  %s:%d in %s, shared code the desktop runs: %s" % (r[1], r[2], r[3], why))
    for r in bad:
        print("  unclassed  %s:%d in %s: %s" % (r[1], r[2], r[3], r[4][:80]))
    if "--write" in sys.argv:
        with open(OUT, "w") as o:
            o.write("# class\tfile\tline\tfunction\tthe call -- tools/callsites.py --write (ADR 0050)\n")
            for r in rows:
                o.write("%s\t%s\t%d\t%s\t%s\n" % r)
        print("wrote", os.path.relpath(OUT, ROOT))
    if "--check" in sys.argv:
        for r in bad:
            print("FAIL %s:%d in %s runs code by a name it does not write, and no rule says "
                  "what it becomes in an app's own process: class it in tools/callsites.py" %
                  (r[1], r[2], r[3]))
        # Against the committed table by class, file, function and the call's
        # own text -- not by line, which moves with every edit -- so a new
        # site inside a function already listed is a difference too, and is
        # classed on purpose rather than by the function it happens to be in.
        key = lambda r: (r[0], r[1], r[3], r[4])
        have = collections.Counter(key(r) for r in rows)
        kept = collections.Counter()
        if os.path.exists(OUT):
            for l in open(OUT):
                if l.startswith("#") or not l.strip():
                    continue
                c = l.rstrip("\n").split("\t")
                kept[(c[0], c[1], c[3], c[4])] += 1
        diff = list((have - kept).elements()), list((kept - have).elements())
        for k in diff[0]:
            print("FAIL new or changed: %s in %s (%s) %s -- tools/callsites.py --write after "
                  "checking its class" % (k[0], k[2], k[1], k[3][:70]))
        for k in diff[1]:
            print("FAIL gone or changed: %s in %s (%s) %s -- tools/callsites.py --write" %
                  (k[0], k[2], k[1], k[3][:70]))
        for r in cross:
            print("FAIL %s:%d in %s calls into an app by a name, not through dt_appfn "
                  "(ADR 0050)" % (r[1], r[2], r[3]))
        around = direct()
        for r in around:
            print("FAIL %s:%d in %s calls a window's handler with dt_appcall or dt_appget "
                  "directly: dt_wincall or dt_winget, the seam's (ADR 0050)" % r)
        for r, why in reach:
            print("FAIL %s:%d in %s calls by a name in shared code the desktop runs (%s): "
                  "through the seam, or in REACHOK with the reason it is not an app's code" %
                  (r[1], r[2], r[3], why))
        cross = cross + around
        fails = len(bad) + len(diff[0]) + len(diff[1]) + len(cross) + len(reach)
        print("\n%d passed, %d failed" % (len(rows) - len(bad), fails))
        sys.exit(1 if fails else 0)


main()
