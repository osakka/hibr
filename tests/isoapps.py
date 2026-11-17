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
	dt_sub "More"
	dt_item "Reset" r nt_reset "$id"
	dt_end
}
fn nt_bump(int id) { NT[$id]["n"]=$(( ${NT[$id]["n"]:-0} + 1 )); dt_want 30; return 0; }
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
do("tst_menu Close")
gone = False
deadline = time.time() + 6
while time.time() < deadline:
    do('dt_iswin "$ISOID" || echo gone > %s' % OUT)
    time.sleep(0.4)
    try:
        gone = open(OUT).read().strip() == "gone"
    except OSError:
        pass
    if gone:
        break
check("Close on its own File menu ends it, and its window goes with it", gone, t.screen())
if os.environ.get("DUMPLOG"):
    print(open(os.path.join(screen.HOME, str(t.pid), "state", "hibr", "desktop.log"), errors="replace").read())
t.quit(b"q", 1.0)
for f in (app, p, MARK, OUT, DROPPED):
    if os.path.exists(f):
        os.unlink(f)
report(10)
