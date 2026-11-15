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
import atexit, json, os, re, shutil, signal, subprocess, sys, tempfile, time

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
# Ended however the suite ends: a check that raises partway left this bus
# running for hours, under nobody.
atexit.register(daemon.kill)
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
dbus tray on
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

# --- the tray's names only while asked for ----------------------------------
# A desktop without the tray -- notifications alone, or the tray switched off
# -- must be no tray a program could register with and hide in: the names
# are taken by `tray on` and let go at once by `tray off`, its items with them.
def owned(name):
    r = subprocess.run(["dbus-send", "--session", "--print-reply", "--reply-timeout=1000",
                        "--dest=org.freedesktop.DBus", "/org/freedesktop/DBus",
                        "org.freedesktop.DBus.GetNameOwner", "string:" + name],
                       env=ENV, capture_output=True, text=True)
    return r.returncode == 0


d = scratch()
e = dict(ENV, EV=os.path.join(d, "ev"))
p = subprocess.Popen([sx.HIBR, "-c", PRE.split("dbus tray on")[0] + r"""
for ((j = 0; j < 20; j++)); do e := dbus pump; sleep 0.05; done
: > "$PWD/opened"
until [ -e "$PWD/on" ]; do e := dbus pump; sleep 0.05; done
dbus tray on
until [ -e "$PWD/off" ]; do e := dbus pump; sleep 0.05; done
dbus tray off
for ((j = 0; j < 20; j++)); do e := dbus pump; sleep 0.05; done
: > "$PWD/done"
until [ -e "$PWD/stop" ]; do e := dbus pump; sleep 0.05; done
"""], env=e, cwd=d, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
procs.append(p)
until(lambda: os.path.exists(os.path.join(d, "opened")), 10)
before = owned("org.kde.StatusNotifierWatcher")
open(os.path.join(d, "on"), "w").close()
during = until(lambda: owned("org.kde.StatusNotifierWatcher"), 10)
ip, log = item(d, "--title", "Brief")
open(os.path.join(d, "off"), "w").close()
until(lambda: os.path.exists(os.path.join(d, "done")), 10)
after = owned("org.kde.StatusNotifierWatcher")
open(os.path.join(d, "stop"), "w").close()
done(p)
check("the bus opened is no tray until asked; asked, it is; switched off, it lets the name go",
      not before and during and not after, (before, during, after))
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

# --- programs' notifications ----------------------------------------------
def notifier(d, summary, body, name="note"):
    """A program sending a notification, once it has its id or was refused."""
    log = os.path.join(d, name)
    p = subprocess.Popen([sys.executable, ITEM, log, "--notify", summary, body], env=ENV,
                         stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
    procs.append(p)
    until(lambda: said(log, "notified") or said(log, "nonotify"), 10)
    return p, log


d = scratch()
p = hb(r"""
dbus notes on
pu "notes *" || { echo nonotes; exit 1; }
n := dbus notes
echo "notes $n"
: > "$READY.2"
pu "note *" || { echo nonote; exit 1; }
k := dbus pump
i := dbus note 1
echo "note ${i["app"]}|${i["summary"]}|${i["body"]}|${i["urgency"]}|${i["actions"]}|${i[0]["key"]}:${i[0]["label"]}|${i[1]["key"]}"
: > "$READY.3"
pu "note 1" || echo noreplace
i := dbus note 1
echo "replaced ${i["summary"]}"
dbus noteact 1 default
quiet 10
: > "$READY.4"
pu "noteclose *" || echo noclose
echo "closed"
""", d, wait=True)
until(lambda: os.path.exists(os.path.join(d, "ready.2")), 10)
np, nlog = notifier(d, "Hello", "<b>Bold</b> &amp; plain")
until(lambda: os.path.exists(os.path.join(d, "ready.3")), 10)
open(nlog + ".renotify", "w").write("Hello again")
until(lambda: os.path.exists(os.path.join(d, "ready.4")), 10)
np2, nlog2 = notifier(d, "Second", "body", name="note2")
open(nlog2 + ".close", "w").close()
out, err = done(p)
lines = out.split("\n")
nt = said(nlog, "notified")
check("asked to, the desktop is the session's notification server",
      "notes on" in lines and nt and nt[0]["id"] == 1, (out, err, nt))
check("a notification is read: its program, summary, body as plain text, urgency and actions",
      "note Standin App|Hello|Bold & plain|2|2|default:Open|later" in lines, (out, err))
check("one its program replaces keeps its number and has the new text",
      "replaced Hello again" in lines and [x["id"] for x in nt] == [1, 1], (out, nt))
ai, cl = said(nlog, "ActionInvoked"), said(nlog, "NotificationClosed")
check("its action is sent to the program, and the note said closed by the person",
      ai and ai[-1]["key"] == "default" and cl and cl[-1]["why"] == 2, (ai, cl))
cl2 = said(nlog2, "NotificationClosed")
check("one its program closes goes, and the program is told so",
      "closed" in lines and cl2 and cl2[-1]["why"] == 3, (out, cl2))
for x in (np, np2):
    x.kill()

# --- the desktop: the item on the menu bar ----------------------------------
# A real desktop through the pty harness, with this suite's bus: the item's
# letter on the bar, a left click activating it, a right click its own menu
# in its own words, a row chosen sent back as its Event, and the letter
# gone with the program.
from screen import Term, load, press, release, IME_STANDIN

d = scratch()
sess = os.path.join(d, "tray.hibr")
SESS = ('%s. %s\n%%s\nDT_BARDIRS+=("$DT_HERE/menubar")\ndt_menubar\n'
        '. "$DT_HERE/control-panel/pnotes.hibr"\ndt_open\ndt_run\ndt_close\n'
        % (load("console", "img"), tree("programs/desktop/desktop.hibr")))
open(sess, "w").write(SESS % "")
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


check("the desktop shows programs' notifications by default",
      until(lambda: owned("org.freedesktop.Notifications"), 10), None)
np, nlog = notifier(d, "Ping", "from the stand-in")
sc = until(lambda: bar().find("Ping: from the stand-in") and bar(), 10)
check("a program's notification is one of the desktop's notes, in its own words",
      bool(sc), bar())
if sc:
    r, c = sc.find("Ping: from the stand-in")
    t.keys([press(r, c), release(r, c)])
ai = until(lambda: said(nlog, "ActionInvoked"), 5)
check("a click on it sends the program its default action",
      ai and ai[-1]["key"] == "default", (ai, said(nlog, "NotificationClosed")))
np2, nlog2 = notifier(d, "Pong", "left alone", name="note2")
cl = until(lambda: said(nlog2, "NotificationClosed"), 15)
check("one left alone goes when its time is up, and the program is told it expired",
      cl and cl[-1]["why"] == 1, cl)
for x in (np, np2):
    x.kill()
t.quit()
t.close()

# Where the terminal shows pixels, an icon is the program's own picture:
# its PNG, from the folder the program names, placed over the bar's cells.
import struct, zlib
d = scratch()
row = b"\0" + bytes([255, 255, 255, 255] * 16)
png = (b"\x89PNG\r\n\x1a\n" + b"".join(
    struct.pack(">I", len(c)) + t + c + struct.pack(">I", zlib.crc32(t + c) & 0xffffffff)
    for t, c in ((b"IHDR", struct.pack(">IIBBBBB", 16, 16, 8, 6, 0, 0, 0)),
                 (b"IDAT", zlib.compress(row * 16)), (b"IEND", b""))))
open(os.path.join(d, "standin-icon.png"), "wb").write(png)
sess2 = os.path.join(d, "tray.hibr")
open(sess2, "w").write(SESS % "")
t = Term(sess2, rows=24, cols=100, settle=1.0, cellw=8, cellh=16,
         env={"DBUS_SESSION_BUS_ADDRESS": "unix:path=" + BUS, "HIBR_GFX": "kitty"})
until(watcher, 15)
c0 = len([x for x in t.screen().apc if "a=T" in x])
ip, log = item(d, "--title", "Quill")


def placed():
    t.collect(0.1)
    sc = t.screen()
    return [x for x in sc.apc if "a=T" in x][c0:] and "Q" not in sc.row(0) and sc


sc = until(placed, 10)
check("where the terminal shows pixels, an icon is the program's own picture, not its letter",
      bool(sc), t.screen().row(0))
# With its own transparency (0.99.535): RGBA laid over the bar's cells,
# which keep the bar's own colour, so a see-through bar shows through the
# icon's clear pixels rather than one colour boxing it in.
kp = [x for x in (sc.kplace if sc else []) if "a=T" in x[2]]
last = kp[-1] if kp else None
under = [sc.style(last[0], last[1] + i)["bg"] for i in (0, 1)] if last else []
barbg = sc.style(0, 0)["bg"] if sc else None
check("the icon goes with its transparency, over the bar's cells, which keep the bar's colour",
      last is not None and "f=32" in last[2] and "z=1" in last[2]
      and barbg is not None and under == [barbg, barbg], (last, under, barbg))
# A program that sends its picture as pixels over the bus, with no file,
# is drawn the same way: the pixels written as a PNG with their alpha.
def alphas():
    t.collect(0.1)
    return len([x for x in t.screen().kplace if "f=32" in x[2]])


n0 = alphas()
pp, plog = item(d, "--pixmap-only", "--title", "Pix", "--id", "pix", name="pix")
sc = until(lambda: alphas() > n0 and t.screen(), 10)
check("an icon sent as pixels goes with its transparency too",
      bool(sc), t.screen().row(0))
open(plog + ".quit", "w").close()
open(log + ".quit", "w").close()
t.quit()
t.close()

# Switched off, the desktop is no tray -- a program finds nobody to
# register with -- and programs' notifications are still the desktop's:
# each is its own.
d = scratch()
sess3 = os.path.join(d, "tray.hibr")
open(sess3, "w").write(SESS % "DT_TRAY=0")
t = Term(sess3, rows=24, cols=100, settle=1.0,
         env={"DBUS_SESSION_BUS_ADDRESS": "unix:path=" + BUS})
notes = until(lambda: owned("org.freedesktop.Notifications"), 15)
time.sleep(1.5 * SLOW)
check("with Tray Icons switched off the desktop holds no tray name, and notifications still work",
      notes and not owned("org.kde.StatusNotifierWatcher"), notes)
t.quit()
t.close()

# --- every lever, pulled in its pane (Gitea #558) --------------------------
# The three panes these entries bring, each lever pulled through the Control
# Panel the way a person pulls it -- a value chosen from its list, a
# checkbox switched, a program moved -- and what it does read back from the
# bar and the bus. The first lever the owner pulled in 0.99.531, Icons
# Before the Arrow, had no list at all, and no check had pulled one.
d = scratch()
open(os.path.join(d, "standin-icon.png"), "wb").write(png)
sess4 = os.path.join(d, "levers.hibr")
open(sess4, "w").write(
    '%s. %s\n. "$DT_HERE/system/panel.hibr"\nDT_BARDIRS+=("$DT_HERE/menubar")\ndt_menubar\n'
    'CP_PANEDIRS+=("$DT_HERE/control-panel")\ncp_panes\n%s'
    'DT_KBLOC=1; DT_KBCUR=0; DT_KB[0]["label"]=EN; DT_KB[1]["label"]=SH\n'
    'dt_open\ndt_kbshow\nCPW := dt_new "Control Panel" 30 70 2 2 panel\npanel_goto "$CPW" trayset\n'
    'dt_run\ndt_close\n' % (load("console", "img"), tree("programs/desktop/desktop.hibr"),
                            IME_STANDIN))
t = Term(sess4, rows=34, cols=100, settle=1.0, cellw=8, cellh=16,
         env={"DBUS_SESSION_BUS_ADDRESS": "unix:path=" + BUS, "HIBR_GFX": "kitty"})
until(watcher, 15)
def scr():
    t.collect(0.1)
    return t.screen()


logs = {}
for title, ident in (("Quill", "quill"), ("Xenon", "xenon"), ("Juno", "juno"), ("Kilo", "kilo")):
    logs[title[0]] = item(d, "--title", title, "--id", ident, name=ident)[1]
# The pane is drawn, every program listed in it and the window focused,
# before the first lever is pulled: a click sent sooner reaches a desktop
# still drawing its first frames.
until(lambda: scr().find("Kilo"), 15)
t.keys([press(2, 30), release(2, 30)])
until(lambda: "Control Pan" in scr().row(0), 10)




def on_bar(letters):
    """The letters among these the bar shows, in the bar's order."""
    r0 = scr().row(0)
    return sorted([x for x in letters if x in r0], key=r0.index)


def rowat(label):
    """Where a row's label is, matched whole: Icons is not Tray Icons."""
    sc = scr()
    for r in range(sc.rows):
        m = re.search(r"(?<= )" + re.escape(label) + r"(?=  )", sc.row(r))
        if m:
            return r, m.start()
    return None


def pick(label, value):
    """Choose value from the list under label's row, as a click does."""
    sc = scr()
    at = rowat(label)
    if not at:
        return False
    r = at[0]
    c = sc.row(r).rfind("▾")
    t.keys([press(r, c), release(r, c)])
    sc = until(lambda: scr().find_from(value, r + 1) and scr(), 5)
    if not sc:
        return False
    vr, vc = sc.find_from(value, r + 1)
    t.keys([press(vr, vc), release(vr, vc)])
    return True


def twice(label):
    """Two clicks on a row's label: select it, then do it."""
    at = rowat(label)
    if at:
        t.keys([press(*at), release(*at), 0.2, press(*at), release(*at)])
    return bool(at)


ALL = ["Q", "X", "J", "K"]
pictured = until(lambda: on_bar(ALL) == [] and [x for x in scr().apc if "a=T" in x], 10)
pick("Icons", "letter")
got = until(lambda: len(on_bar(ALL)) == 4 and on_bar(ALL), 10)
check("Icons: choosing letter from its list puts each program's letter where its picture was",
      bool(pictured) and got, (bool(pictured), scr().row(0)))
pick("Icons Before the Arrow", "3")
got = until(lambda: len(on_bar(ALL)) == 3 and "▸" in scr().row(0) and on_bar(ALL), 10)
check("Icons Before the Arrow: choosing 3 keeps three on the bar and the rest under the arrow",
      bool(got), scr().row(0))
pick("Quill", "hidden")
got = until(lambda: "Q" not in on_bar(ALL) and on_bar(ALL), 10)
check("a program's row: choosing hidden takes it off the bar", bool(got), scr().row(0))
before = on_bar(ALL)
at = rowat("Xenon")
if at:
    t.keys([press(*at), release(*at), 0.2, b"\x1b[1;2B"])
got = until(lambda: on_bar(ALL) != before and on_bar(ALL), 10)
check("shift and an arrow on a program's row moves it along the bar",
      bool(got) and got.index("X") > got.index("J"), (before, got))
pick("Left Click", "menu")
first = (on_bar(ALL) or ["?"])[0]
k = scr().row(0).find(first)
if k >= 0:
    t.keys([press(0, k), release(0, k)])
sc = until(lambda: scr().find("Show Hidden") and scr(), 5)
check("Left Click: choosing menu makes a left click open the program's menu, not activate it",
      bool(sc) and first in logs and not said(logs[first], "Activate"),
      (first, scr().row(0), said(logs.get(first, os.devnull), "Activate")))
t.keys([b"\x1b", 0.3])
twice("Tray Icons on the Menu Bar")
off = until(lambda: not owned("org.kde.StatusNotifierWatcher") and on_bar(ALL) == [], 10)
# The row is selected now, so one more click on it switches it back.
at = rowat("Tray Icons on the Menu Bar")
if at:
    t.keys([press(*at), release(*at)])
back = until(lambda: owned("org.kde.StatusNotifierWatcher"), 10)
check("Tray Icons on the Menu Bar: off lets the tray's name and icons go, on takes them again",
      off and back, (off, back, scr().row(0)))
named = " EN " in scr().row(0)
at = scr().find("Keyboard on")
if at:
    t.keys([press(*at), release(*at), 0.3, b"\t", 0.2, b"\r"])
bare = until(lambda: " EN " not in scr().row(0) and scr(), 5)
check("Keyboard on Menu Bar: switching the name off leaves the keyboard's glyph without it",
      named and bool(bare), scr().row(0))
# Programs' notifications have no pane of their own: a second place beside
# Notifications for the same notes (the owner, 2026-11-15). Their switch is
# a row of Notifications, there while the listener is installed.
nopane = scr().find("Program Noti") is None
was = owned("org.freedesktop.Notifications")
at = scr().find("Notifications")
if at:
    t.keys([press(*at), release(*at), 0.3, b"\t", 0.2])
for _ in range(30):
    if rowat("Program Notifications"):
        break
    t.keys([b"\x1b[B", 0.1])
check("Program Notifications is a row of Notifications, not a pane beside it",
      nopane and bool(rowat("Program Notifications")), scr().text())
twice("Program Notifications")
gone = until(lambda: not owned("org.freedesktop.Notifications"), 10)
at = rowat("Program Notifications")
if at:
    t.keys([press(*at), release(*at)])
again = until(lambda: owned("org.freedesktop.Notifications"), 10)
check("Program Notifications: off lets the notification name go, on takes it again",
      was and gone and again, (was, gone, again))
for x in logs:
    open(logs[x] + ".quit", "w").close()
t.quit()
t.close()

for p in procs:
    if p.poll() is None:
        p.kill()
daemon.kill()
daemon.wait()
shutil.rmtree(D, True)
# --- the keyboard and the tray left the package (the owner, 2026-11-15) ----
# A desktop that had them is told once, after the upgrade, with a click to
# install them; the next start says nothing; a new desktop is told nothing.
plain = ('%s. %s\ndt_open\ndt_run\ndt_close\n'
         % (load("console"), tree("programs/desktop/desktop.hibr")))


def startup(home, secs=4):
    f = os.path.join(home, "s.hibr")
    open(f, "w").write(plain)
    t = Term(f, rows=24, cols=100, settle=1.0,
             env={"XDG_CONFIG_HOME": os.path.join(home, "config"),
                  "XDG_STATE_HOME": os.path.join(home, "state")})
    seen = until(lambda: (t.collect(0.1), t.screen().find("click to install"))[1], secs)
    t.quit()
    t.close()
    return bool(seen)


old_home = scratch()
os.makedirs(os.path.join(old_home, "config", "hibr"))
open(os.path.join(old_home, "config", "hibr", "desktop.json"), "w").write(
    '{"settings": {"DT_NOTEMS": "8000"}}')
first, again = startup(old_home), startup(old_home, 3)
new_home = scratch()
fresh = startup(new_home, 3)
check("a desktop that had the keyboard and the tray from the package is told once where they went",
      first and not again, (first, again))
check("and a new desktop is told nothing", not fresh, fresh)
report(45)
