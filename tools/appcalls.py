#!/usr/bin/env python3
"""The platform functions installables call, and what each becomes in an app's
own process (ADR 0050): local (the app runtime has it), request (sent to the
desktop, never waited on), declare (built in the app and pushed up), or
decide (not yet classified).

  python3 tools/appcalls.py [--write]   # print the summary; --write the table

Every call is read by the inspect module (ADR 0056), never run, from every
installable the catalogue checkout holds (~/hibr-apps/bundles, or $HIBR_APPS)
and every one shipped in programs/desktop. A function is classed by the file
that defines it, unless OVERRIDE names it; --write puts the table at
docs/adr/0050-calls.tsv, which the ADR points at.
"""
import collections, glob, os, re, sys
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "tests"))
from inspectlib import records

APPS = os.environ.get("HIBR_APPS", os.path.expanduser("~/hibr-apps"))
OUT = os.path.join(ROOT, "docs", "adr", "0050-calls.tsv")

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
# What changes the desktop itself: asked of it, never done in the app.
for _n in """dt_changed dt_fopsave dt_fopundo dt_trash dt_fileop dt_locof dt_loccan
        dt_location dt_context_open dt_panelpane dt_vwdialog dt_chromeoff
        dt_btnhidden dt_btnlist dt_capped dt_imepre dt_imekey dt_imeflush
        dt_pointer dt_pointerlock dt_ptrlockcan dt_sbauto dt_sbcall dt_tile
        dt_place dt_openlink dt_progload dt_progsave dt_deskscan dt_dnd dt_pwdo
        dt_pwgo dt_pkstart dt_pkstop dt_kbset dt_saverload dt_saverstart
        dt_colours dt_walldisp dt_walldrawn dt_shot dt_rescan dt_reinstate
        dt_unload dt_onunload dt_scanold dt_appvia dt_appunvia dt_modehook
        dt_pbar dt_rslog dt_rscarry dt_rsclaim""".split():
    OVERRIDE[_n] = "request"
OVERRIDE["dt_atstart"] = "declare"
OVERRIDE["dt_identify"] = "request"
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


def classify(name, rel):
    if name in OVERRIDE:
        return OVERRIDE[name]
    for k, v in BYFILE.items():
        if rel == k or (k.endswith("/") and rel.startswith(k)):
            return v
    return "decide"


def main():
    where = defs()
    use, users, bad = collections.Counter(), collections.defaultdict(set), []
    for f in installables():
        try:
            r = records(f, "calls")
        except Exception as e:
            bad.append(f)
            continue
        for c in r.get("calls", []):
            n = c.get("name") or c.get("command") or c.get("word")
            if isinstance(n, dict):
                n = n.get("text")
            if n in where:
                use[n] += 1
                users[n].add(f)
    rows = sorted(((classify(n, where[n]), n, where[n], use[n], len(users[n])) for n in use),
                  key=lambda r: (r[0], -r[3]))
    kinds = collections.Counter(r[0] for r in rows)
    print("installables read: %d, unreadable: %d" % (len(installables()), len(bad)))
    print("platform functions called: %d -- %s" % (len(rows), ", ".join(
        "%s %d" % (k, kinds[k]) for k in ("local", "request", "declare", "gone", "decide"))))
    for r in rows:
        if r[0] == "decide":
            print("  decide  %-26s %-24s %4d calls in %d files" % (r[1], r[2], r[3], r[4]))
    if "--write" in sys.argv:
        with open(OUT, "w") as o:
            o.write("# class\tfunction\tdefined in\tcalls\tfiles -- tools/appcalls.py --write\n")
            for r in rows:
                o.write("%s\t%s\t%s\t%d\t%d\n" % r)
        print("wrote", os.path.relpath(OUT, ROOT))
    for f in bad:
        print("unreadable:", f)


main()
