#!/usr/bin/env python3
"""An app in a process of its own works as one in the desktop does (ADR 0050's
prototype, wm/isolate.hibr): its menus are on the desktop's bar and run there,
its settings are saved in the desktop's own file, Copy, Paste and a drop
reach it, and closing it from its own File menu closes its window.

The app is hosted (dt_isonew); each check drives it from the desktop's side,
as a person would -- a menu item chosen on the bar, the Edit shortcuts, a
file let go on the window -- through a ticker that runs what a marker file
says, and reads the result off the screen or the desktop's own state.
"""
import json, os, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import screen
from screen import Term, check, report, load, tree, scratch

MOD = tree("build/mods/console.so")
WM = tree("programs/desktop/desktop.hibr")
MARK = scratch("isoapps-do")
OUT = scratch("isoapps-out")
DROPPED = scratch("isoapps a dropped file.txt")

APP = r'''declare -gA NT
NT_LEVEL="${NT_LEVEL-1}"
command -v dt_keep > /dev/null && dt_keep NT_LEVEL
fn nt_draw(int id, int h, int w, int row, int col) {
	console put -p "w$id" 1 1 "n=${NT[$id]["n"]:-0} lv=$NT_LEVEL p=${NT[$id]["p"]-} d=${NT[$id]["d"]-} end"
	console put -p "w$id" 0 1 "size ${h}x${w} "
	[ -n "${NT[$id]["pic"]-}" ] && img draw PICPATH 2 1 2 4 -p "w$id" -m blocks 2> /dev/null
	return 0
}
nt_menus() {
	local id=$DT_FOCUS
	dt_menu "File"
	dt_item "Close" w dt_close_focused
	dt_editmenu
	dt_menu "Thing"
	dt_item "Bump" b nt_bump "$id"
	dt_item "Level Up" l nt_level
	dt_item "Show Picture" s nt_pic "$id"
	dt_item "Make Changes" c nt_dirty "$id"
	dt_item "Grab Desktop" g nt_evil
	dt_sub "More"
	dt_item "Reset" r nt_reset "$id"
	dt_end
}
fn nt_bump(int id) { NT[$id]["n"]=$(( ${NT[$id]["n"]:-0} + 1 )); dt_want 30; return 0; }
fn nt_dirty(int id) { NT[$id]["dirty"]=1; return 0; }
fn nt_canclose(int id) { [ -z "${NT[$id]["dirty"]-}" ]; }
nt_evil() { dt_hostsay "keep|DT_FACE|#ff0000"; dt_hostsay "keep|TW|x"; return 0; }
fn nt_pic(int id) { need img; NT[$id]["pic"]=1; dt_want 30; return 0; }
fn nt_reset(int id) { NT[$id]["n"]=0; dt_want 30; return 0; }
nt_level() { NT_LEVEL=$((NT_LEVEL + 1)); dt_save; dt_want 30; return 0; }
fn nt_copy(int id) { ret "copied-${NT[$id]["n"]:-0}"; }
fn nt_paste(int id, text, op = "") { NT[$id]["p"]=$text; dt_want 30; return 0; }
fn nt_drop(int id, int r, int c, op, ...paths) { NT[$id]["d"]="${paths[0]##*/}"; dt_want 30; return 0; }
dt_app nt "Notes" 6 60
'''

SESSION = r'''%s. %s
dt_open
tst_menu() {
	local m i
	for ((m = 0; m < MB_N; m++)); do
		for ((i = 0; i < ${MB[$m]["n"]:-0}; i++)); do
			[ "${MB[$m]["own"]}" = "$DT_FOCUS" ] && [ "${MI[$m][$i]["label"]}" = "$1" ] || continue
			MB_OPEN=$m
			MB_SEL=$i
			dt_mdo
			return 0
		done
	done
	echo "no menu row $1" >> %s
}
tst_do() {
	[ -f %s ] || { dt_want 100; return 0; }
	local c
	read -r c < %s
	rm -f %s
	eval "$c"
	dt_want 30
	return 0
}
dt_ticker tst_do
ISOID := dt_isonew nt %s Notes 6 60 3 3
dt_run
dt_close
'''

app = scratch("isoapps-nt.hibr")
open(app, "w").write(APP.replace("PICPATH", tree("tests/img-2x2.png")))
p = scratch("isoapps.hibr")
open(p, "w").write(SESSION % (load(MOD), WM, OUT, MARK, MARK, MARK, app))
open(DROPPED, "w").write("x\n")
t = Term(p, rows=24, cols=90, settle=0.6)


def do(cmd):
    """Run cmd in the desktop, from its ticker; waits until it has been
    taken, so the next one cannot overwrite it unread."""
    open(MARK, "w").write(cmd + "\n")
    deadline = time.time() + 5
    while os.path.exists(MARK) and time.time() < deadline:
        t.send(b"\x1b[I", settle=0.1, collect=0.1)


def shown(text, tries=60):
    return t.until(text, tries=tries, step=0.1).find(text) is not None


ok = shown("n=0 lv=1", tries=80)
check("the hosted app runs, in a window of the desktop", ok, t.screen())
if os.environ.get("DUMPLOG"):
    print(t.screen())
check("its own menus are on the desktop's bar while its window has the focus",
      shown("Thing"), t.screen())
do("tst_menu Bump")
check("a row chosen on the desktop's bar runs in the app", shown("n=1 "), t.screen())
do("tst_menu Reset")
check("and a row in one of its submenus", shown("n=0 "), t.screen())
do("tst_menu 'Level Up'")
ok = shown("lv=2")
conf = os.path.join(screen.HOME, str(t.pid), "config", "hibr", "desktop.json")
saved = None
deadline = time.time() + 5
while time.time() < deadline:
    try:
        saved = json.load(open(conf)).get("settings", {}).get("NT_LEVEL")
    except (OSError, ValueError):
        pass
    if str(saved) == "2":
        break
    time.sleep(0.2)
if os.environ.get("DUMPLOG"):
    print("conf:", open(conf).read().replace("\n"," ")[:600] if os.path.exists(conf) else "none", conf)
check("a setting the app changes is saved in the desktop's own file",
      ok and str(saved) == "2", (saved, t.screen()))
def halves(sc):
    return sum(sc.text().count(ch) for ch in "\u2580\u2584")


before = halves(t.screen())
do("tst_menu 'Show Picture'")
pic = t.until(lambda sc: halves(sc) >= before + 2, tries=60, step=0.1)
check("a picture the app draws comes through as its cells (half blocks: pixels do not cross yet)",
      halves(pic) >= before + 2, (before, halves(pic), pic))
do("dt_edit copy")
got = None
deadline = time.time() + 5
while time.time() < deadline:
    do('printf "%%s\\n" "$DT_CLIP" > %s' % OUT)
    time.sleep(0.3)
    try:
        got = open(OUT).read().strip()
    except OSError:
        got = None
    if got == "copied-0":
        break
check("Copy on its window copies what the app says, onto the desktop's clipboard",
      got == "copied-0", got)
do('dt_clipset "pasted-here" copy text; dt_edit paste')
check("Paste on its window reaches the app's own _paste", shown("p=pasted-here"), t.screen())
do('DT_DNDS=("%s"); dt_ondrop $((DT[$ISOID]["row"] + 2)) $((DT[$ISOID]["col"] + 4)) ""' % DROPPED)
check("a file let go on its window reaches the app's own _drop",
      shown("d=" + os.path.basename(DROPPED)[:12]), t.screen())
def ask(expr, want=None, secs=6):
    """What a shell expression says in the desktop, waited for until it is
    want (when given) or simply answered."""
    got = None
    deadline = time.time() + secs
    while time.time() < deadline:
        if os.path.exists(OUT):
            os.unlink(OUT)
        do('{ %s; } > %s 2>&1' % (expr, OUT))
        time.sleep(0.2)
        try:
            got = open(OUT).read().strip()
        except OSError:
            got = None
        if want is None and got is not None or got == want:
            break
    return got


def kids(pid):
    out = []
    for d in os.listdir("/proc"):
        if d.isdigit():
            try:
                st = open("/proc/%s/stat" % d).read().rsplit(")", 1)[1].split()
                if int(st[1]) == pid:
                    out.append(int(d))
            except (OSError, IndexError):
                pass
    return out


def zygote():
    for d in os.listdir("/proc"):
        if d.isdigit():
            try:
                if b"--zygote" in open("/proc/%s/cmdline" % d, "rb").read() and \
                        str(t.pid).encode() in open("/proc/%s/cmdline" % d, "rb").read():
                    return int(d)
            except OSError:
                pass
    return None


# What it may keep is what its own file keeps: a hosted app that sends the
# desktop's own names is refused, and the desktop's colours stay as they are.
face = ask('printf "%s\\n" "$DT_FACE"')
do("tst_menu 'Grab Desktop'")
time.sleep(1.0)
face2 = ask('printf "%s\\n" "$DT_FACE"')
logl = open(os.path.join(screen.HOME, str(t.pid), "state", "hibr", "desktop.log"), errors="replace").read()
check("a hosted app cannot keep the desktop's own settings: refused, and named in the log",
      face == face2 and face and logl.count("asked to keep a setting it may not") >= 2, (face, face2))

# Its window resized: the app is told its new size (the fork route's resize
# reaches it only through the stub, which passes the signal on).
s0 = t.until("size ", tries=20, step=0.1)
was = [l for l in s0.text().split("\n") if "size " in l]
do('dt_resize "$ISOID" 14 76')
grown = t.until(lambda sc: [l for l in sc.text().split("\n") if "size " in l] != was, tries=60, step=0.1)
now = [l for l in grown.text().split("\n") if "size " in l]
check("a hosted app's window resized: the app draws at its new size", now != was and now, (was, now))

# Closed from the desktop's side: the app is asked, and one with changes
# says no; asked again, it is ended, process and all.
z = zygote()
app0 = kids(z) if z else []
do("tst_menu 'Make Changes'")
do('dt_closereq "$ISOID"')
time.sleep(1.5)
still = ask('dt_iswin "$ISOID" && echo yes || echo no')
do('dt_closereq "$ISOID"')
gone = ask('dt_iswin "$ISOID" && echo yes || echo no', "no")
deadline = time.time() + 5
while time.time() < deadline and [p for p in app0 if p in kids(z)]:
    time.sleep(0.2)
check("closed from the desktop, an app with changes is asked first and stays; closed again, "
      "it ends, its process too", still == "yes" and gone == "no" and z and app0 and
      not [p for p in app0 if p in kids(z)], (still, gone, z, app0, kids(z) if z else None))

# Its stub killed outright -- what a desktop that dies takes with it: the
# app notices the stub has gone and ends, rather than running on with no
# terminal.
do('ISOID := dt_isonew nt %s Notes 6 60 3 3' % app)
shown("n=0 lv=", tries=80)
app1 = [p for p in kids(z)] if z else []
stub = ask('term pid "${TW[$ISOID]["t"]}"')
if stub and stub.isdigit():
    os.kill(int(stub), 9)
deadline = time.time() + 5
while time.time() < deadline and [p for p in app1 if p in kids(z)]:
    time.sleep(0.2)
check("its stub killed outright, the hosted app ends within seconds",
      stub and stub.isdigit() and app1 and not [p for p in app1 if p in kids(z)],
      (stub, app1, kids(z) if z else None))

# And Close on its own File menu ends it, and its window goes with it.
do('ISOID := dt_isonew nt %s Notes 6 60 3 3' % app)
shown("n=0 lv=", tries=80)
do("tst_menu Close")
gone = ask('dt_iswin "$ISOID" && echo yes || echo no', "no")
check("Close on its own File menu ends it, and its window goes with it", gone == "no", t.screen())
if os.environ.get("DUMPLOG"):
    print(open(os.path.join(screen.HOME, str(t.pid), "state", "hibr", "desktop.log"), errors="replace").read())
t.quit(b"q", 1.0)
for f in (app, p, MARK, OUT, DROPPED):
    if os.path.exists(f):
        os.unlink(f)
report(14)
