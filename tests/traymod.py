#!/usr/bin/env python3
"""The dbus module: hibr as the session bus's tray watcher and host (Gitea
#505), against a dbus-daemon of the suite's own and tests/trayitem.py,
a stand-in tray item that registers as Qt does or as libappindicator does,
answers late when told, offers only a picture when told, changes its menu
and vanishes without a word.

Each check runs a hibr script that opens the bus and pumps it -- never
waiting on it -- writing every event it sees to a file, while the suite
starts the stand-in and reads what it was sent. Without dbus-daemon, or
without libdbus, every check is skipped and the suite says so.
"""
import json, os, shutil, signal, subprocess, sys, tempfile, time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import screen as sx
from screen import check, report, tree

if len(sys.argv) > 1:
    sx.HIBR = os.path.abspath(sys.argv[1])

SLOW = 4.0 if os.environ.get("ASAN_OPTIONS") else 1.0
D = tempfile.mkdtemp(prefix="hibr-tray-")
BUS = os.path.join(D, "bus")
ITEM = os.path.join(os.path.dirname(os.path.abspath(__file__)), "trayitem.py")
MODS = os.environ.get("HIBR_TESTMODS") or tree("build/mods")

if not shutil.which("dbus-daemon"):
    print("no dbus-daemon: every check skipped")
    report(0)
    sys.exit(0)
daemon = subprocess.Popen(["dbus-daemon", "--session", "--nofork",
                           "--address=unix:path=" + BUS],
                          stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
for _ in range(100):
    if os.path.exists(BUS):
        break
    time.sleep(0.02)
ENV = dict(os.environ, DBUS_SESSION_BUS_ADDRESS="unix:path=" + BUS)
procs = []
n = [0]

# Every script: open, pump until a role is settled, say so in READY, then
# what the check asks. `pu PATTERN` pumps until an event matches, writing
# each to EV; `quiet` pumps for a while, so replies go out.
PRE = r'''mod load %s/dbus.so
fn pu(want, int n = 200) {
	local e k i
	for ((i = 0; i < n; i++)); do
		e := dbus pump
		for k in "${e[@]}"; do
			echo "$k" >> "$EV"
			[[ $k == $want ]] && return 0
		done
		sleep 0.05
	done
	return 1
}
fn quiet(int n = 10) {
	local e i
	for ((i = 0; i < n; i++)); do e := dbus pump; sleep 0.05; done
}
dbus open > /dev/null || { echo nobus; exit 1; }
pu "role *" || { echo norole; exit 1; }
r := dbus role
echo "role $r"
: > "$READY"
''' % MODS


def scratch():
    n[0] += 1
    d = os.path.join(D, "c%d" % n[0])
    os.makedirs(d)
    return d


def hb(body, d, wait=False):
    """Start a script in d; its output is read when it ends."""
    e = dict(ENV, EV=os.path.join(d, "ev"), READY=os.path.join(d, "ready"))
    p = subprocess.Popen([sx.HIBR, "-c", PRE + body], env=e, cwd=d,
                         stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    procs.append(p)
    if wait:
        until(lambda: os.path.exists(os.path.join(d, "ready")), 10)
    return p


def item(d, *args, name="item"):
    """Start the stand-in, logging to d/name, once it has registered."""
    log = os.path.join(d, name)
    p = subprocess.Popen([sys.executable, ITEM, log] + list(args), env=ENV,
                         stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
    procs.append(p)
    until(lambda: said(log, "registered") or said(log, "noregister"), 10)
    return p, log


def said(log, kind):
    try:
        return [json.loads(l) for l in open(log) if json.loads(l)["kind"] == kind]
    except FileNotFoundError:
        return []


def until(cond, secs):
    end = time.time() + secs * SLOW
    while time.time() < end:
        v = cond()
        if v:
            return v
        time.sleep(0.05)
    return cond()


def done(p, secs=30):
    try:
        out, err = p.communicate(timeout=secs * SLOW)
    except subprocess.TimeoutExpired:
        p.kill()
        out, err = p.communicate()
    return out, err


lib = subprocess.run([sx.HIBR, "-c", "mod load %s/dbus.so; DBUS_SESSION_BUS_ADDRESS=unix:path=%s "
                      "dbus open > /dev/null; echo $?" % (MODS, BUS)],
                     env=ENV, capture_output=True, text=True)
if lib.stdout.strip() != "0":
    print("no libdbus-1, or the bus would not take a connection: every check skipped",
          lib.stderr[-300:])
    daemon.kill()
    shutil.rmtree(D, True)
    report(0)
    sys.exit(0)

# --- the watcher, an item by name, its fields, menu and clicks -------------
d = scratch()
p = hb(r'''
pu "add *" || { echo noadd; exit 1; }
i := dbus item 1
echo "item ${i["title"]}|${i["icon"]}|${i["menu"]}|${i["tooltip"]}|${i["status"]}|${i["ismenu"]}"
dbus menu 1 fetch || echo nofetch
pu "menu 1" || { echo nomenu; exit 1; }
m := dbus menu 1
for ((k = 0; k < ${m["n"]}; k++)); do
	echo "row ${m[$k]["depth"]} ${m[$k]["id"]} ${m[$k]["sep"]}${m[$k]["enabled"]}${m[$k]["sub"]} ${m[$k]["toggle"]}:${m[$k]["on"]} [${m[$k]["label"]}]"
done
dbus click 1 5
dbus activate 1 10 20
dbus scroll 1 -3 horizontal
quiet 10
: > "$READY.2"
pu "change 1" || echo nochange
i := dbus item 1
echo "title ${i["title"]}"
: > "$READY.3"
pu "menustale 1" || echo nostale
dbus menu 1 fetch
pu "menu 1" || echo nomenu2
m := dbus menu 1
echo "rows ${m["n"]} last [${m[$((m["n"] - 1))]["label"]}]"
: > "$READY.4"
pu "gone 1" || echo nogone
x := dbus items
echo "after [${x[*]}]"
''', d, wait=True)
ip, log = item(d)
until(lambda: os.path.exists(os.path.join(d, "ready.2")), 15)
open(log + ".title", "w").write("Renamed")
until(lambda: os.path.exists(os.path.join(d, "ready.3")), 15)
open(log + ".menu", "w").close()
until(lambda: os.path.exists(os.path.join(d, "ready.4")), 15)
open(log + ".quit", "w").close()
out, err = done(p)
lines = out.split("\n")
check("the desktop is the session's tray watcher when nobody else is",
      "role watcher" in lines, (out, err))
check("an item registered by its bus name is read: its title, icon, menu and tooltip",
      "item Stand-in|standin-icon|/MenuBar|Stand-in tip|Active|0" in lines, (out, err))
rows = [l for l in lines if l.startswith("row ")]
check("its menu is the rows shown, mnemonics gone, a separator, a checkmark, a submenu "
      "and the row under it one deeper, and nothing that is not visible",
      rows == ["row 0 1 010 :0 [Open]", "row 0 2 110 :0 []", "row 0 3 010 check:0 [Show Hidden]",
               "row 0 4 011 :0 [More]", "row 1 5 010 :0 [Quit]"], (rows, err))
ev = said(log, "Event")
check("AboutToShow goes before the layout is read, and a click is the row's Event",
      said(log, "AboutToShow") and ev and ev[-1]["id"] == 5 and ev[-1]["event"] == "clicked",
      (said(log, "AboutToShow"), ev))
act, sc = said(log, "Activate"), said(log, "Scroll")
check("a click on the item activates it where it was, and the wheel scrolls it",
      act and (act[-1]["x"], act[-1]["y"]) == (10, 20) and sc and
      (sc[-1]["delta"], sc[-1]["orientation"]) == (-3, "horizontal"), (act, sc))
check("a new title is read again when the item says so",
      "title Renamed" in lines, (out, err))
check("a changed menu is said, and read again has its new row",
      "rows 6 last [Added Later]" in lines, (out, err))
check("an item whose program ends without unregistering goes with it",
      "after []" in lines, (out, err))

# --- libappindicator's way: an object path, the sender its name ------------
d = scratch()
p = hb(r'''
pu "add *" || { echo noadd; exit 1; }
i := dbus item 1
echo "by ${i["name"]} ${i["path"]} ${i["title"]}"
''', d, wait=True)
ip, log = item(d, "--path", "--title", "Indicator")
out, err = done(p)
reg = said(log, "registered")
check("an item registered by its object path alone is the sender's, at that path",
      reg and ("by %s /org/ayatana/NotificationItem/standin Indicator" % reg[0]["unique"]) in out,
      (out, err, reg))
open(log + ".quit", "w").close()

# --- an item that answers late never holds the pump ------------------------
d = scratch()
p = hb(r'''
worst=0
for ((j = 0; j < 200; j++)); do
	t0=$EPOCHREALTIME
	e := dbus pump
	t1=$EPOCHREALTIME
	ms=$(( (${t1/./} - ${t0/./}) / 1000 ))
	[ "$ms" -gt "$worst" ] && worst=$ms
	case " ${e[*]} " in *" add 1 "*) echo "added after $j"; break ;; esac
	sleep 0.02
done
echo "worst $worst"
''', d, wait=True)
ip, log = item(d, "--late", "1500")
out, err = done(p)
w = [int(l.split()[1]) for l in out.split("\n") if l.startswith("worst ")]
a = [int(l.split()[2]) for l in out.split("\n") if l.startswith("added after ")]
check("an item that answers a second and a half late is added when it answers, "
      "and no pump meanwhile waits for it",
      a and a[0] >= 30 and w and w[0] < 50 * SLOW, (out, err))
open(log + ".quit", "w").close()

# --- a picture and no icon name ---------------------------------------------
d = scratch()
p = hb(r'''
pu "add *" || exit 1
i := dbus item 1
echo "px ${i["pixmap"]} [${i["icon"]}]"
dbus pixmap 1 "$PWD/px.ppm" '#00ff00' && echo written
''', d, wait=True)
ip, log = item(d, "--pixmap-only")
out, err = done(p)
ppm = open(os.path.join(d, "px.ppm"), "rb").read() if os.path.exists(os.path.join(d, "px.ppm")) else b""
check("an item that offers only a picture has it, written over a background for img",
      "px 4x2 []" in out and "written" in out and
      ppm == b"P6\n4 2\n255\n" + bytes([255, 0, 0] * 4) + bytes([0, 255, 0] * 4), (out, err, ppm[:40]))
open(log + ".quit", "w").close()

# --- a second desktop is a host, and sees what the first knows -------------
d = scratch()
first = hb(r'''
for ((j = 0; j < 300; j++)); do e := dbus pump; [ -e "$PWD/stop" ] && break; sleep 0.05; done
''', d, wait=True)
ip, log = item(d, "--title", "Shared")
d2 = scratch()
second = hb(r'''
pu "add *" || { echo noadd; exit 1; }
i := dbus item 1
echo "seen ${i["title"]}"
''', d2)
out, err = done(second)
open(os.path.join(d, "stop"), "w").close()
done(first)
check("a desktop that finds a watcher already there is a host, and is given its items",
      "role host" in out and "seen Shared" in out, (out, err))
open(log + ".quit", "w").close()

# --- what it answers on the bus ---------------------------------------------
d = scratch()
first = hb(r'''
for ((j = 0; j < 300; j++)); do e := dbus pump; [ -e "$PWD/stop" ] && break; sleep 0.02; done
''', d, wait=True)
intro = subprocess.run(["dbus-send", "--session", "--print-reply", "--reply-timeout=3000",
                        "--dest=org.kde.StatusNotifierWatcher", "/StatusNotifierWatcher",
                        "org.freedesktop.DBus.Introspectable.Introspect"],
                       env=ENV, capture_output=True, text=True, timeout=10 * SLOW)
prop = subprocess.run(["dbus-send", "--session", "--print-reply", "--reply-timeout=3000",
                       "--dest=org.kde.StatusNotifierWatcher", "/StatusNotifierWatcher",
                       "org.freedesktop.DBus.Properties.Get",
                       "string:org.kde.StatusNotifierWatcher",
                       "string:IsStatusNotifierHostRegistered"],
                      env=ENV, capture_output=True, text=True, timeout=10 * SLOW)
odd = subprocess.run(["dbus-send", "--session", "--print-reply", "--reply-timeout=3000",
                      "--dest=org.kde.StatusNotifierWatcher", "/Nothing", "x.y.Nonsense"],
                     env=ENV, capture_output=True, text=True, timeout=10 * SLOW)
open(os.path.join(d, "stop"), "w").close()
done(first)
check("the watcher says what it is, and that a host is there",
      "RegisterStatusNotifierItem" in intro.stdout and "boolean true" in prop.stdout,
      (intro.stdout[-200:], intro.stderr, prop.stdout, prop.stderr))
check("a call it does not know is answered with an error at once, never left waiting",
      "UnknownMethod" in odd.stderr, (odd.stdout, odd.stderr))

# --- the desktop: the item on the menu bar ----------------------------------
# A real desktop through the pty harness, with this suite's bus: the item's
# letter on the bar, a left click activating it, a right click its own menu
# in its own words, a row chosen sent back as its Event, and the letter
# gone with the program.
from screen import Term, load, press, release

d = scratch()
sess = os.path.join(d, "tray.hibr")
open(sess, "w").write('%s. %s\nDT_BARDIRS+=("$DT_HERE/menubar")\ndt_menubar\ndt_open\ndt_run\ndt_close\n'
                      % (load("console"), tree("programs/desktop/desktop.hibr")))
t = Term(sess, rows=24, cols=100, settle=1.0,
         env={"DBUS_SESSION_BUS_ADDRESS": "unix:path=" + BUS})


def watcher():
    r = subprocess.run(["dbus-send", "--session", "--print-reply", "--reply-timeout=1000",
                        "--dest=org.freedesktop.DBus", "/org/freedesktop/DBus",
                        "org.freedesktop.DBus.GetNameOwner", "string:org.kde.StatusNotifierWatcher"],
                       env=ENV, capture_output=True, text=True)
    return r.returncode == 0


until(watcher, 15)
ip, log = item(d, "--title", "Zephyr")


def bar():
    t.collect(0.1)
    return t.screen()


sc = until(lambda: "Z" in bar().row(0) and bar(), 10)
zc = sc.find("Z")[1] if sc and sc.find("Z") else None
check("a program's tray item is its letter on the menu bar", zc is not None,
      sc.row(0) if sc else None)
if zc is not None:
    t.keys([press(0, zc), release(0, zc)])
    act = until(lambda: said(log, "Activate"), 5)
    check("a left click on it activates the program", bool(act), act)
    t.keys([press(0, zc, 2), release(0, zc, 2)])
    sc = until(lambda: bar().find("Show Hidden") and bar(), 5)
    check("a right click opens its own menu, in the program's words",
          sc and sc.find("Open") and sc.find("More") and sc.find("Never seen") is None
          and said(log, "AboutToShow"), sc)
    if sc and sc.find("Open"):
        r, c = sc.find("Open")
        t.keys([press(r, c), release(r, c)])
    ev = until(lambda: said(log, "Event"), 5)
    check("choosing a row sends the program that row's click",
          ev and ev[-1]["id"] == 1 and ev[-1]["event"] == "clicked", ev)
    open(log + ".quit", "w").close()
    sc = until(lambda: "Z" not in bar().row(0) and bar(), 10)
    check("and the letter goes when the program does", bool(sc), bar().row(0))
t.quit()
t.close()

for p in procs:
    if p.poll() is None:
        p.kill()
daemon.kill()
daemon.wait()
shutil.rmtree(D, True)
report(20)
