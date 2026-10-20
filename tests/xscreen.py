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


# The programs X Screen keeps (Gitea #291) are a file, one a line: a name,
# a command, how the window behaves and how it is drawn, a tab between.
LIST = os.path.join(D, "xscreen.tsv")


def progs(*rows):
    open(LIST, "w").write("".join("\t".join(r) + "\n" for r in rows))


def desk(body, pix=False, env=None):
    """A desktop session that opens a program through its menu entry, then
    runs body (session lines) besides."""
    p = sx.scratch("xscreen.hibr")
    open(p, "w").write(
        "%s. %s\n. %s\nXS_SIZE=640x400\nXS_MAX=1280x800\n"
        "dt_open\nXSID := dt_launch \"xscreen:0\"\n%s\ndt_run\ndt_close\n"
        % (load("console"), tree("programs/desktop/desktop.hibr"),
           tree("programs/desktop/apps/xscreen.hibr"), body))
    kw = dict(cellw=8, cellh=16, caps=["kitty"]) if pix else {}
    e = {"XDG_RUNTIME_DIR": D, "XS_LIST": LIST}
    e.update(env or {})
    return Term(p, rows=26, cols=82, settle=1.0, env=e, **kw)


def session(pix, kill=False):
    """A desktop with an X Screen window running the GTK field: what the
    terminal was sent and shows, after typing hi! at the window."""
    tf = os.path.join(D, "s-typed.txt")
    if os.path.exists(tf):
        os.unlink(tf)
    progs(("Entry", "%s %s %s" % (sys.executable, ENTRY, tf), "fixed",
           "follow"))
    t = desk("", pix)
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

# The list on the menu: each program an entry under Applications > X
# Screen, the app itself hidden, and a program deleted from the list gone
# from the menu with no restart.
progs(("One", "true", "fit", "follow"), ("Two", "true", "own", "half"))
out, err = hb(
    "XS_LIST=%s\n. %s\n. %s\n"
    "echo \"hidden=${DT_APPS[xscreen][hidden]}\"\n"
    "for k in xscreen:0 xscreen:1; do echo \"$k=${DT_APPS[$k][title]}|${DT_APPS[$k][dir]}|${DT_APPS[$k][via]}|${DT_APPS[$k][arg]}\"; done\n"
    "XSP_NAME=(Two); XSP_CMD=(true); XSP_WIN=(own); XSP_PIC=(half); xs_save; xs_reload\n"
    "echo \"after=${DT_APPS[xscreen:0][title]}|${DT_APPS[xscreen:1][title]}|$(wc -l < %s)\"\n"
    % (LIST, tree("programs/desktop/desktop.hibr"),
       tree("programs/desktop/apps/xscreen.hibr"), LIST))
check("each program is an entry under Applications > X Screen, and the "
      "plain X Screen is not",
      "hidden=1" in out and
      "xscreen:0=One|Applications/X Screen|xscreen|One" in out and
      "xscreen:1=Two|Applications/X Screen|xscreen|Two" in out, out + err)
check("and a program taken off the list is off the menu at once",
      "after=Two||1" in out, out + err)

# The pane is loaded after the app, as a session loads them, and must not
# empty the list the app has read: it did, so a new session's pane showed
# no programs and the next one added saved over the rest (Gitea #300).
# Each program's row carries its own Delete.
progs(("One", "true", "fit", "follow"), ("Two", "true", "own", "half"))
out, err = hb(
    "XS_LIST=%s\n. %s\n. %s\n. %s\n. %s\n"
    "echo \"pane=${#XSP_NAME[@]}\"\n"
    "declare -gA CP; xscreenset_rows 40 > /dev/null\n"
    "echo \"row1=${CP[40][1][text]}|${CP[40][1][btns]}\"\n"
    "xss_remove 0\n"
    "echo \"left=${XSP_NAME[*]}|$(cut -f1 %s)|${DT_APPS[xscreen:0][title]}\"\n"
    % (LIST, tree("programs/desktop/desktop.hibr"),
       tree("programs/desktop/apps/xscreen.hibr"),
       tree("programs/desktop/system/panel.hibr"),
       tree("programs/desktop/control-panel/xscreenset.hibr"), LIST))
check("the pane, loaded after the app, still lists every program",
      "pane=2" in out and "row1=One|del0:Delete" in out, out + err)
check("and a program's own Delete takes it off the list, the file and "
      "the menu", "left=Two|Two|Two" in out, out + err)

# A program fitted to its window (Gitea #291): the module is the screen's
# window manager, so the program's window is the hibr window's body in the
# screen's pixels -- 8 by 16 a cell in half blocks -- and follows a resize;
# the window is titled with the program's name and its own title.
STATE = os.path.join(D, "state.txt")


def winfo(n):
    r = subprocess.run(["xwininfo", "-display", ":%s" % n, "-root",
                        "-children"], capture_output=True, text=True)
    for l in r.stdout.splitlines():
        if '"entry"' in l:
            return l.split(")")[-1].split()[0].split("+")[0]
    return None


def state(n=4):
    """The n fields the desktop's ticker writes, or None -- also for a read
    that met the file half written, which is what a ticker truncating it
    with `>` leaves for a moment: one gate read it with one field and the
    next line indexed the second."""
    try:
        f = open(STATE).read().split("|")
    except OSError:
        return None
    return f if len(f) == n else None


fitted = shutil.which("xwininfo") is not None
progs(("Entry", "%s %s %s" % (sys.executable, ENTRY,
                              os.path.join(D, "f.txt")), "fit", "half"))
t = desk("T0=$EPOCHSECONDS\n"
         "xsl() { printf '%s|%s|%s|%s' \"${XS[$XSID][n]}\" "
         "\"${XS[$XSID][w]}\" \"${XS[$XSID][h]}\" \"${DT[$XSID][title]}\" "
         "> " + STATE + "; [ -z \"$SN\" ] && [ $((EPOCHSECONDS - T0)) -ge 8 ] "
         "&& { SN=1; dt_snap \"$XSID\" left; }; return 0; }\n"
         "dt_ticker xsl")
g1 = g2 = s1 = s2 = None
for _ in range(28):
    t.collect(0.25)
    s1 = state()
    if s1 and s1[0] and fitted and winfo(s1[0]):
        break
t.collect(1.5)
s1 = state()
g1 = winfo(s1[0]) if s1 and s1[0] else None
for _ in range(40):
    t.collect(0.25)
    s2 = state()
    if s2 and s1 and s2[1] != s1[1]:
        break
t.collect(1.0)
s2 = state()
g2 = winfo(s2[0]) if s2 and s2[0] else None
t.signal(9)
time.sleep(0.5)
t.close()
want1 = "%dx%d" % (int(s1[1]) * 8, int(s1[2]) * 16) if s1 and s1[1] else None
want2 = "%dx%d" % (int(s2[1]) * 8, int(s2[2]) * 16) if s2 and s2[1] else None
check("a fitted program's window is the hibr window's body, in pixels",
      fitted and g1 is not None and g1 == want1, (g1, want1, s1))
check("and follows the window when it is resized",
      fitted and g2 is not None and g2 == want2 and want2 != want1,
      (g2, want2, s2))
check("the window is titled with the program's name and its own title",
      s1 is not None and s1[3] == "Entry [entry]", s1)

# A program's own: no window manager of the module's, so the program keeps
# the size it asks for -- GTK's own 600 by 120 here.
progs(("Entry", "%s %s %s" % (sys.executable, ENTRY,
                              os.path.join(D, "o.txt")), "own", "half"))
t = desk("xsl() { printf '%s' \"${XS[$XSID][n]}\" > " + STATE +
         "; return 0; }\ndt_ticker xsl")
g3 = None
for _ in range(40):
    t.collect(0.25)
    s3 = state(1)
    if s3 and s3[0] and fitted:
        g3 = winfo(s3[0])
        if g3:
            break
t.signal(9)
time.sleep(0.5)
t.close()
check("a program left to place itself keeps the size it asks for",
      fitted and g3 == "600x120", g3)

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
progs(("Ticker", "%s %s" % (sys.executable, TICK), "fixed", "half"))
t = desk("T0=$EPOCHSECONDS\n"
         "xsmin() { [ -z \"$MD\" ] && [ $((EPOCHSECONDS - T0)) -ge 8 ] && "
         "{ MD=1; dt_min \"$XSID\"; }; return 0; }\n"
         "dt_ticker xsmin")
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

# When the program ends (Gitea #308): a program's own When It Ends --
# default, close or keep -- and Close When It Ends for the default. Ended
# is every process it started gone and no window of it on the screen, so a
# launcher whose first process forks and leaves keeps its window up until
# that window goes. QUICK shows a window for five seconds, then ends.
QUICK = os.path.join(D, "quick.py")
open(QUICK, "w").write(
    "import gi; gi.require_version('Gtk','3.0')\n"
    "from gi.repository import Gtk, GLib\n"
    "w = Gtk.Window(title='quick'); w.set_default_size(300, 100)\n"
    "w.show_all(); GLib.timeout_add(5000, Gtk.main_quit); Gtk.main()\n")


def ending(cmd, end, closeend, win="fit"):
    """Run QUICK as X Screen's program: whether its window was drawn, was
    still there four seconds after it was, and is there twelve seconds on."""
    progs(("Quick", cmd, win, "half", end))
    t = desk("", env={"XS_CLOSEEND": closeend})

    def up(sc):
        return sc.find("Quick") is not None and sc.find("Quick")[0] <= 2
    seen = t.until(up, 200) is not None and up(t.screen())
    t0 = time.time()
    while time.time() - t0 < 4.0:
        t.collect(0.25)
    mid = up(t.screen())
    while time.time() - t0 < 12.0:
        t.collect(0.25)
        if not up(t.screen()) and not mid:
            break
    last = up(t.screen())
    t.signal(9)
    time.sleep(0.5)
    t.close()
    return seen, mid, last


py = "%s %s" % (sys.executable, QUICK)
s, m, l = ending(py, "close", "0")
check("a program set to close takes its window with it when it ends",
      s and not l, (s, m, l))
s, m, l = ending(py, "default", "1")
check("and one on default does, with Close When It Ends on", s and not l, (s, m, l))
s, m, l = ending(py, "keep", "1")
check("but one set to keep stays, whatever that says", s and l, (s, m, l))
s, m, l = ending(py, "default", "0")
check("and with it off, a window stays when its program ends", s and l, (s, m, l))
s, m, l = ending("sh -c '%s &'" % py, "close", "0")
check("a launcher that forks and leaves keeps its window while the program's is up",
      s and m and not l, (s, m, l))
# Left to place itself, the module manages no window, and the server is
# asked which of the screen's own windows are shown.
s, m, l = ending("sh -c '%s &'" % py, "close", "0", "own")
check("and the same for a program left to place itself, asked of the X server",
      s and m and not l, (s, m, l))

shutil.rmtree(D, True)
report(25)
