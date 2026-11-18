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
                  dt_isozygote dt_natver dt_pwlocal dt_supervise dt_updcheck dt_rtmsg dt_vw
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
    """The desktop's own calls of dt_appcall or dt_appget by name: an input
    handler reached around the seam, which a dynamic-call census cannot see
    -- the dynamic call is inside the shared budget.hibr. Only wm/rtserve's
    dt_wincall and dt_winget may make them."""
    out = []
    for f in sorted(glob.glob(os.path.join(ROOT, "programs/desktop/wm/*.hibr"))):
        rel = os.path.relpath(f, os.path.join(ROOT, "programs/desktop"))
        if shared(rel) or rel == "wm/rtserve.hibr":
            continue
        for c in records(f, "calls").get("calls", []):
            if c.get("name") in ("dt_appcall", "dt_appget"):
                out.append((rel, c["span"]["line"], c.get("owner", "")))
    return out


def classify(site, own):
    rel, ln, owner, text = site
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
    for r in cross:
        print("  across  %s:%d in %s calls into an app by a name; it goes through dt_appfn, "
              "from a file of the desktop's own" % (r[1], r[2], r[3]))
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
        cross = cross + around
        fails = len(bad) + len(diff[0]) + len(diff[1]) + len(cross)
        print("\n%d passed, %d failed" % (len(rows) - len(bad), fails))
        sys.exit(1 if fails else 0)


main()
