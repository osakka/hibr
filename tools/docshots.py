#!/usr/bin/env python3
"""Make every screenshot the documentation shows, from one place.

    python3 tools/docshots.py [--out DIR] [NAME ...]

Until this existed the eight pictures under `programs/desktop/img/` had no
recipe anywhere in the tree: each was made by hand, once, and never again.
So when `tools/screenshot.py` turned out to be corrupting its own output
(Gitea #181, fixed in 0.99.148) there was nothing to re-run, and the broken
images stayed shipped for sixty-nine releases -- `desktop-strip.png` lost
three of the menu bar's five menus, the workspace switcher and the
notification flag, and nobody could tell because nobody could reproduce it.

A picture with no recipe is a claim nobody can check. The recipes live
here now, next to the tool that renders them, and `tests/shots.py` asserts
what each must contain.

Every one is a real hibr session on a real pty: no mockups, nothing drawn
by hand. `--cell` is deliberately left off -- the documentation shows the
desktop as it looks on a terminal that places no pixels, which is the
honest default and the one a visitor is most likely to have. A page that
wants to show pixels says so and passes `--cell` itself.
"""
import argparse, os, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
OUT = os.path.join(ROOT, "programs", "desktop", "img")

# name -> (session, extra flags). The caption each one answers is quoted
# above it: a caption is a claim, and this is where the claim is kept
# honest.
SHOTS = {
    # "An empty desktop: the menu bar, the wallpaper, and the three fixed
    # icons"
    "desktop-empty": ("", []),

    # "The hibr menu open, showing About hibr, Clean Up Icons, Detach and
    # Quit"
    "desktop-menu": ("", ["--keys", "f10"]),

    # "A right-click on the desktop background showing Arrange Icons and
    # Change Wallpaper"
    "desktop-context": ("", ["--keys", "rclick:12:30", "--rows", "24", "--cols", "80"]),

    # "Three windows open: a calculator, the file browser, and a real
    # terminal running a nested hibr shell"
    # calc declares 16x24 and term 14x52 on their own `dt_app` lines; a
    # window smaller than an app's own size draws nothing at all, which
    # is what made the first render of this show two empty boxes. And a
    # terminal's program starts at its first *draw*, not at dt_new, so
    # TW_CMD has to be set before the frame and the wait has to be long
    # enough for a shell to have printed its prompt.
    "desktop-apps": (
        'TW_CMD=(/bin/sh -c \'PS1="hibr$ "; export PS1; exec /bin/sh\')\n'
        'dt_new "Calculator" 16 24 2 2 calc\n'
        'dt_new "Files" 16 36 2 28 files\n'
        'dt_new "Terminal" 14 52 19 2 term\n',
        ["--apps", "calc,files,term", "--modules", "console,pty,term",
         "--rows", "35", "--cols", "96", "--until", "hibr$"]),

    # "Control Panel: every pane in two groups down the left, Appearance
    # open on its theme and colour rows"
    # The panel app lists panes, and the panes are a scan root of their
    # own: `--apps panel` alone draws "Nothing found", which is a true
    # picture of a desktop nobody would ever run.
    "desktop-controlpanel": (
        'CP_PANEDIRS+=("%(cp)s")\ncp_panes\n'
        'dt_new "Control Panel" 24 76 2 2 panel\n',
        ["--apps", "panel", "--rows", "30", "--cols", "88",
         "--until", "Appearance", "--keys", "click:11:12"]),

    # "Date & Time, the pane the Control Panel opens on: a live clock,
    # the date, and a small world map with Europe/London marked"
    "desktop-datetime": (
        'CP_PANEDIRS+=("%(cp)s")\ncp_panes\n'
        'dt_new "Control Panel" 24 76 2 2 panel\n',
        ["--apps", "panel", "--rows", "30", "--cols", "88",
         "--until", "Europe/London"]),

    # "The task manager, sorted by CPU, showing real processes from this
    # machine"
    "desktop-tasks": (
        'dt_new "Task Manager" 20 72 2 2 tasks\n',
        ["--apps", "tasks", "--rows", "26", "--cols", "84", "--wait", "1.5"]),

    # "The Control Strip docked left, four named modules in brackets
    # ending in its own arrow"
    # The strip's modules are a scan root of their own, exactly as the
    # Control Panel's panes are: CS_ON=1 with nothing loaded turns on an
    # empty strip, which draws nothing and photographs as a desktop that
    # simply has no strip.
    "desktop-strip": (
        'CS_MODDIRS+=("%(cs)s")\ncs_modules\nCS_ON=1\n',
        ["--rows", "24", "--cols", "80", "--until", "Theme"]),
}


CP = os.path.join(ROOT, "programs", "desktop", "control-panel")
CS = os.path.join(ROOT, "programs", "desktop", "control-strip")


def make(name, outdir):
    session, flags = SHOTS[name]
    if "%(" in session:
        session = session % {"cp": CP, "cs": CS}
    out = os.path.join(outdir, name + ".png")
    cmd = [sys.executable, os.path.join(HERE, "screenshot.py"), out]
    if session:
        cmd.append(session)
    cmd += flags
    r = subprocess.run(cmd, capture_output=True, text=True, timeout=180)
    ok = r.returncode == 0 and os.path.exists(out)
    print("  %-24s %s" % (name, "ok" if ok else "FAILED: " + (r.stderr or r.stdout)[:120]))
    return ok


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=OUT)
    ap.add_argument("names", nargs="*")
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    names = a.names or list(SHOTS)
    bad = [n for n in names if not make(n, a.out)]
    sys.exit(1 if bad else 0)
