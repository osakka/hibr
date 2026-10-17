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

# The session the package runs, minus the one thing that would make it
# unphotographable: `programs/desktop/session.hibr` ends in
# `dt_autohold`, which re-execs into a held session, and the tool's pty
# then watches the parent exit and renders nothing at all. Everything
# else is here in its order -- the scan roots and their loaders. The
# shipped apps/ root is left out because the package has none; `dt_apps`
# still reads the person's own and the catalogue's folders, which is
# where the tour's apps are.
BARE = (
    'dt_apps\n'
    'DT_SYSDIRS+=("%(sys)s")\ndt_sysapps\n'
    'CP_PANEDIRS+=("%(cp)s")\ncp_panes\n'
    'CS_MODDIRS+=("%(cs)s")\ncs_modules\n'
    'FB_DIR=$HOME\n')


HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
OUT = os.path.join(ROOT, "programs", "desktop", "img")

# name -> (session, extra flags). The caption each one answers is quoted
# above it: a caption is a claim, and this is where the claim is kept
# honest.
SHOTS = {
    # --- the tour, docs/desktop.md ------------------------------------
    #
    # One first hour a visitor could have had, in order, on the desktop
    # the package installs -- which has no applications (Gitea #220).
    # Calculator and Files come from the catalogue: `prepare()` serves a
    # copy of the real one on this machine and installs them with the
    # real `catalogue` command into a data folder of the tour's own, and
    # the shots after that read that folder. Nothing here loads the
    # tree's apps/, which no install has.

    # "The desktop as it starts: a menu bar, a wallpaper and its icons"
    "tour-1-start": (BARE, ["--rows", "26", "--cols", "86"]),

    # "The hibr menu: the desktop's own programs, and Applications"
    "tour-2-menu": (BARE, ["--rows", "26", "--cols", "86",
                           "--keys", "f10"], "empty"),

    # "The Catalogue: what there is to install, and what you have"
    # Seven rows down is calc, the app the tour installs next; the list
    # opens on its first entry, whose description is the vault's.
    "tour-3-catalogue": (
        BARE + 'dt_new "Catalogue" 17 84 2 1 catalogue\n',
        ["--rows", "26", "--cols", "86", "--until", "in the catalogue"] +
        ["--keys", "down"] * 7, "empty"),

    # "Calculator and Files, installed a moment ago, side by side"
    "tour-4-windows": (
        BARE + 'dt_new "Calculator" 16 24 3 3 calc\n'
               'dt_new "Files" 16 44 3 30 files\n',
        ["--rows", "26", "--cols", "86", "--until", "integers only"],
        "installed"),

    # "Appearance, where the theme and the colours are chosen"
    "tour-5-appearance": (
        BARE + 'dt_new "Control Panel" 24 76 2 2 panel\n',
        ["--rows", "34", "--cols", "88", "--until", "Appearance",
         "--keys", "click:11:12"], "installed"),

    # --- the eight the reference pages show -----------------------------

    # "An empty desktop: the menu bar, the wallpaper, and the three fixed
    # icons"
    "desktop-empty": ("", []),

    # "The hibr menu open: the desktop's own programs above Screen Saver,
    # Lock Screen, Restart Desktop, Detach and Quit"
    # The same scan roots a real session has: with none, the menu draws an
    # empty group between two rules, which no install ever shows.
    "desktop-menu": (BARE, ["--keys", "f10"]),

    # "A right-click on the desktop background: Arrange Icons, Change
    # Wallpaper, Next Wallpaper and Refresh Desktop"
    # BARE, because Change Wallpaper opens the Control Panel and is dimmed
    # on a desktop that has none -- true, and not a desktop anyone runs.
    "desktop-context": (BARE, ["--keys", "rclick:12:30", "--rows", "24", "--cols", "80"]),

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
        'FB_DIR=$HOME\n'
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


APPS = os.path.join(ROOT, "programs", "desktop", "apps")
SYS = os.path.join(ROOT, "programs", "desktop", "system")
CP = os.path.join(ROOT, "programs", "desktop", "control-panel")
CS = os.path.join(ROOT, "programs", "desktop", "control-strip")


# The tour's catalogue and the two data folders it is photographed over:
# `empty`, as the package leaves a person, and `installed`, after
# `catalogue install calc files`. Set by prepare(), once.
TOUR = {}


def prepare():
    """Serve a copy of the real catalogue here and install from it.

    The copy is of ~/hibr-apps as it stands, or of CATALOGUE_DIR, so the
    Catalogue window lists what visitors would really find; it is served
    by the suites' own stand-in server, on this machine, so making the
    pictures fetches nothing from anywhere and changes nothing published.
    """
    import shutil, tempfile
    src = os.environ.get("CATALOGUE_DIR", os.path.expanduser("~/hibr-apps"))
    if not os.path.exists(os.path.join(src, "index.json")):
        sys.exit("docshots: no catalogue at %s (set CATALOGUE_DIR)" % src)
    d = tempfile.mkdtemp(prefix="hibr-docshots-")
    www = os.path.join(d, "www")
    os.makedirs(www)
    for f in ("index.json", "index.json.asc"):
        if os.path.exists(os.path.join(src, f)):
            shutil.copy(os.path.join(src, f), www)
    shutil.copytree(os.path.join(src, "bundles"), os.path.join(www, "bundles"))
    srv = subprocess.Popen([sys.executable, os.path.join(ROOT, "tests", "davserve.py"),
                            "--root", www, "--auth", "none"],
                           stdout=subprocess.PIPE, text=True)
    port = int(srv.stdout.readline().split()[1])
    url = "http://127.0.0.1:%d/index.json" % port
    for which in ("empty", "installed"):
        os.makedirs(os.path.join(d, which, "data"))
    # A home that is nobody's, so a published picture of Files shows a
    # visitor's folders rather than whoever made the pictures.
    # Its path is drawn in the window, so it is a fixed one: a random
    # temporary name in a picture reads as a mistake.
    home = "/tmp/visitor"
    if os.path.exists(home):
        sys.exit("docshots: %s is in the way; it is made fresh each run" % home)
    TOUR["home"] = home
    for f in ("Documents", "Downloads", "Music", "Pictures", "Projects"):
        os.makedirs(os.path.join(home, f))
    for f, body in (("notes.txt", "things to try\n"), ("todo.md", "- tea\n")):
        open(os.path.join(home, f), "w").write(body)
    env = dict(os.environ, HIBR_APPS_URL=url,
               HIBR_MODPATH=os.path.join(ROOT, "build", "mods"),
               XDG_DATA_HOME=os.path.join(d, "installed", "data"),
               XDG_CONFIG_HOME=os.path.join(d, "installed", "config"),
               XDG_CACHE_HOME=os.path.join(d, "installed", "cache"),
               TMPDIR=d)
    env.pop("HIBR_HOLD", None)
    r = subprocess.run([os.path.join(ROOT, "build", "hibr"),
                        os.path.join(ROOT, "programs", "catalogue.hibr"),
                        "install", "calc", "files"],
                       env=env, capture_output=True, text=True, timeout=120)
    if r.returncode:
        srv.kill()
        shutil.rmtree(home, ignore_errors=True)
        sys.exit("docshots: catalogue install failed:\n" + r.stdout + r.stderr)
    TOUR.update(srv=srv, url=url, dir=d)


def make(name, outdir):
    session, flags, *data = SHOTS[name]
    if "%(" in session:
        session = session % {"apps": APPS, "sys": SYS, "cp": CP, "cs": CS}
    # Every shot gets the visitor's home and an empty data folder, not only
    # the tour's: a picture of Files anywhere must not show the home of
    # whoever ran this.
    if not TOUR:
        prepare()
    which = data[0] if data else "empty"
    flags = flags + ["--env", "HIBR_APPS_URL=" + TOUR["url"],
                         "--env", "HOME=" + TOUR["home"],
                         "--env", "XDG_DATA_HOME=" +
                         os.path.join(TOUR["dir"], which, "data")]
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
    try:
        bad = [n for n in names if not make(n, a.out)]
    finally:
        if TOUR:
            TOUR["srv"].kill()
            __import__("shutil").rmtree(TOUR["dir"], ignore_errors=True)
            __import__("shutil").rmtree(TOUR["home"], ignore_errors=True)
    sys.exit(1 if bad else 0)
