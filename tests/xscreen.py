#!/usr/bin/env python3
"""X Screen and the xscr module (Gitea #290): an X screen in a window.

Xvfb draws the screen into a file it maps; the module maps the same file,
talks to the server over its socket for DAMAGE and XTEST, and puts the
screen into a window as pixels or half blocks. Every check here runs a real
Xvfb and a real GTK program, since a stand-in X server would agree with the
module and prove nothing -- so the suite needs Xvfb and Python's GTK
bindings, and says so and skips when either is missing.
"""
import os, shutil, subprocess, sys, tempfile, time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import screen as sx
from screen import Term, check, report, load, tree

D = tempfile.mkdtemp(prefix="hibr-xscreen-")
MOD = tree("build/mods/xscr.so")
ENV = dict(os.environ, HIBR_MODPATH=tree("build/mods"),
           XDG_RUNTIME_DIR=D)


def have():
    if not shutil.which("Xvfb"):
        return False
    r = subprocess.run([sys.executable, "-c",
                        "import gi; gi.require_version('Gtk','3.0');"
                        "from gi.repository import Gtk"],
                       capture_output=True)
    return r.returncode == 0


if not have():
    print("no Xvfb or no GTK bindings here: every check skipped")
    shutil.rmtree(D, True)
    report(0)
    sys.exit(0)

# A GTK window holding one text field, which writes what it holds to a
# file on every change: what was typed at the X screen is read back from
# there, through a real toolkit's own keyboard handling.
ENTRY = os.path.join(D, "entry.py")
open(ENTRY, "w").write(
    "import gi, sys; gi.require_version('Gtk','3.0')\n"
    "from gi.repository import Gtk, GLib\n"
    "out = sys.argv[1]\n"
    "w = Gtk.Window(title='entry'); w.set_default_size(600, 120)\n"
    "e = Gtk.Entry(); w.add(e); w.show_all(); w.present()\n"
    "e.connect('changed', lambda *a: open(out, 'w').write(e.get_text()))\n"
    "GLib.timeout_add(30000, Gtk.main_quit); Gtk.main()\n")


def xvfbs():
    """The Xvfb processes this suite started: their screens are in D."""
    r = subprocess.run(["ps", "-eo", "args"], capture_output=True, text=True)
    return [l for l in r.stdout.splitlines() if l.startswith("Xvfb") and D in l]


def hb(script):
    r = subprocess.run([sx.HIBR, "-c", "mod load %s\n%s" % (MOD, script)],
                       env=ENV, capture_output=True, text=True, timeout=120)
    return r.stdout, r.stderr


# The module alone: start a server, open its screen, type into a program.
# The display number is this process's own, since all.py and asan.py run
# side by side in a gate and a fixed one would be shared; and every wait is
# for the thing itself, bounded, never a fixed sleep (Gitea #283's lesson).
DN = 100 + os.getpid() % 800
typed = os.path.join(D, "typed.txt")
fb = os.path.join(D, "m")
os.mkdir(fb)
out, err = hb(
    "srv := xscr start %d 800x600 %s; n=0\n"
    "while [ ! -s %s/Xvfb_screen0 ] && [ $n -lt 100 ]; do sleep 0.05; n=$((n+1)); done\n"
    "n=0; while ! x := xscr open %d %s/Xvfb_screen0 $srv 2> /dev/null && [ $n -lt 100 ]; do sleep 0.05; n=$((n+1)); done\n"
    "xscr size $x; xscr seen $x\n"
    "DISPLAY=:%d %s %s %s &\n"
    "n=0; c=1; while [ $n -lt 200 ]; do xscr poll $x && { c=0; break; }; sleep 0.05; n=$((n+1)); done\n"
    "echo \"changed $c\"\n"
    "sleep 1; xscr click $x 1 300 60\n"
    "xscr type $x 'Hello X'; xscr key $x shift-1\n"
    "n=0; while [ \"$(cat %s 2> /dev/null)\" != 'Hello X!' ] && [ $n -lt 100 ]; do sleep 0.05; n=$((n+1)); done\n"
    "xscr close $x\n"
    "n=0; while kill -0 $srv 2> /dev/null && [ $n -lt 100 ]; do sleep 0.05; n=$((n+1)); done\n"
    "kill -0 $srv 2> /dev/null && echo alive || echo ended\n"
    % (DN, fb, fb, DN, fb, DN, sys.executable, ENTRY, typed, typed))
check("xscr opens an Xvfb screen and reads its size",
      "800 600" in out, out + err)
# After `seen` the screen is marked drawn, so this answers only to a
# change the server reports: the first version polled the mark a new
# screen starts with, which passed with DAMAGE not working at all.
check("DAMAGE says the screen changed once a program draws on it",
      "changed 0" in out, out + err)
check("keys typed through XTEST reach a GTK program, shifted ones too",
      os.path.exists(typed) and open(typed).read() == "Hello X!",
      (open(typed).read() if os.path.exists(typed) else None, out, err))
check("closing the screen ends the server the module started",
      "ended" in out, out + err)


def session(pix, kill=False):
    """A desktop with an X Screen window running the GTK field: what the
    terminal was sent and shows, after typing hi! at the window."""
    tf = os.path.join(D, "s-typed.txt")
    if os.path.exists(tf):
        os.unlink(tf)
    p = sx.scratch("xscreen.hibr")
    open(p, "w").write(
        "%s. %s\n. %s\nXS_SIZE=640x400\nXS_RUN=%s\n"
        "dt_open\ndt_new \"X Screen\" 24 80 1 0 xscreen\ndt_run\ndt_close\n"
        % (load("console"), tree("programs/desktop/desktop.hibr"),
           tree("programs/desktop/apps/xscreen.hibr"),
           "'%s %s %s'" % (sys.executable, ENTRY, tf)))
    kw = dict(cellw=8, cellh=16, caps=["kitty"]) if pix else {}
    t = Term(p, rows=26, cols=82, settle=1.0,
             env={"XDG_RUNTIME_DIR": D}, **kw)
    for _ in range(60):
        t.collect(0.25)
        sc = t.screen()
        if (pix and b"a=T" in t.raw) or (not pix and "▀" in sc.text()):
            break
    t.collect(2.5)
    for k in (b"h", b"i", b"!"):
        t.send(k)
    for _ in range(40):
        t.collect(0.25)
        if os.path.exists(tf) and open(tf).read() == "hi!":
            break
    sc = t.screen()
    got = open(tf).read() if os.path.exists(tf) else None
    placed = t.raw.count(b"a=T")
    if kill:
        t.signal(9)
        time.sleep(1.0)
    t.close()
    return sc, placed, got


sc, placed, got = session(True)
check("X Screen draws the screen as pixels on a terminal that takes them",
      placed >= 1 and not any("▀" in sc.row(r) for r in range(2, 24)),
      (placed, sc))
check("and what is typed at the window reaches the program on it",
      got == "hi!", got)
sc, placed, got = session(False, kill=True)
check("on one that takes none, it draws half blocks",
      placed == 0 and sum("▀" in sc.row(r) for r in range(2, 24)) > 10,
      (placed, sc))
check("a desktop that is killed takes its X screen with it",
      xvfbs() == [], xvfbs())

# What an animating program costs (Gitea #290): a GTK label changing ten
# times a second is drawn about as often while the window is shown, and not
# at all once it is minimised -- the damage is taken off only when the
# screen has been drawn, so a hidden window hears of one change and then
# nothing. Before that, it woke the desktop for every change, shown or not.
TICK = os.path.join(D, "tick.py")
open(TICK, "w").write(
    "import gi; gi.require_version('Gtk','3.0')\n"
    "from gi.repository import Gtk, GLib\n"
    "w = Gtk.Window(); w.set_default_size(300, 100)\n"
    "l = Gtk.Label(label='0'); w.add(l); w.show_all(); n = [0]\n"
    "def t():\n    n[0] += 1; l.set_text(str(n[0])); return True\n"
    "GLib.timeout_add(100, t); GLib.timeout_add(40000, Gtk.main_quit)\n"
    "Gtk.main()\n")
p = sx.scratch("xscreen-cost.hibr")
open(p, "w").write(
    "%s. %s\n. %s\nXS_SIZE=640x400\nXS_RUN=%s\n"
    "dt_open\nXSID := dt_new \"X Screen\" 24 80 1 0 xscreen\n"
    "T0=$EPOCHSECONDS\n"
    "xsmin() { [ -z \"$MD\" ] && [ $((EPOCHSECONDS - T0)) -ge 8 ] && "
    "{ MD=1; dt_min \"$XSID\"; }; return 0; }\n"
    "dt_ticker xsmin\ndt_run\ndt_close\n"
    % (load("console"), tree("programs/desktop/desktop.hibr"),
       tree("programs/desktop/apps/xscreen.hibr"),
       "'%s %s'" % (sys.executable, TICK)))
t = Term(p, rows=26, cols=82, settle=1.0, env={"XDG_RUNTIME_DIR": D})
t.collect(4.0)
f0, w0 = t.frames(), time.time()
t.collect(3.0)
shown = (t.frames() - f0) / (time.time() - w0)
while time.time() - w0 < 6.5:
    t.collect(0.5)
f0, w1 = t.frames(), time.time()
t.collect(3.0)
hidden = (t.frames() - f0) / (time.time() - w1)
t.signal(9)
time.sleep(0.5)
t.close()
check("a screen that changes ten times a second is drawn about that often",
      shown >= 5, shown)
check("and not at all while its window is minimised", hidden < 1, hidden)

shutil.rmtree(D, True)
report(11)
