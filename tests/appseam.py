#!/usr/bin/env python3
"""The seam every call into an app goes through (ADR 0050, dt_appfn): a
callback an app hands the desktop -- a menu command, a note's action, a
confirmation's Yes, a dialog's answer -- runs as that app's when it is
called back, never as the desktop's, because it was recorded with its owner
(DT_CALLER) when it was handed in. When apps run in processes of their own,
that owner is the process the call is sent to.

    python3 tests/appseam.py
"""
import os, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import screen
from screen import Term, check, report, load, tree, scratch

MOD = tree("build/mods/console.so")
WM = tree("programs/desktop/desktop.hibr")
OUT = scratch("appseam-out")
MARK = scratch("appseam-do")

SESSION = r'''%s. %s
dt_open
declare -gA OW
fn ow_draw(int id, int h, int w, int row, int col) {
	console put -p "w$id" 1 1 "owner app"
	dt_scrollbar "$id" 1 "$w" 4 40 4 0 bar
}
fn ow_key(int id, key) { [ "$key" = Z ] || return 1; ow_said key; return 0; }
fn ow_scroll(int id, key, int top) { ow_said scroll; return 0; }
fn ow_name(v) { ow_said namer; ret "$v"; }
fn ow_pick(int id, v) { return 0; }
fn ow_list() {
	DT_POPNAMENEXT=ow_name
	dt_droplist "$OWID" 1 1 ow_pick one two
	return 0
}
ow_menus() {
	dt_menu "File"
	dt_item "Close" w dt_close_focused
	dt_editmenu
	dt_menu "Thing"
	dt_item "Say" s ow_said menu
}
fn ow_said(how, ...rest) { echo "$how ${DT_CALLER:-desktop}" >> %s; return 0; }
fn ow_confirmed() { ow_said confirm; }
fn ow_mine() { ow_said mine; }
fn ow_hand() {
	dt_confirm "Really?" ow_confirmed
	return 0
}
fn ow_post() {
	dt_notify "ow note" ow_said note
	return 0
}
ow_tick() { [ -n "${OW_TICKED-}" ] && return 0; OW_TICKED=1; ow_said rtick; }
fn ow_regtick() { dt_ticker ow_tick; return 0; }
fn ow_ask() { dt_ask "Name" "Name:" "" ow_answer; return 0; }
fn ow_answer(text, int id) { ow_said ask; return 0; }
dt_app ow "Owner" 6 30
OWID := dt_new "Owner" 6 30 3 3 ow
. "$DT_HERE/system/panel.hibr"
. "$DT_HERE/savers/saver.hibr"
fn aafirst_rows(int w) { ret 0; }
cp_pane aafirst "A First Pane" "x"
DT_LOADING=%s
. "$DT_LOADING"
DT_LOADING=
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
tst_go() {
	[ -f %s ] || { dt_want 100; return 0; }
	local c
	read -r c < %s
	command rm -f %s
	eval "$c"
	dt_want 30
}
dt_ticker tst_go
dt_run
dt_close
'''

# An installable of its own, loaded from its file as the desktop loads one
# (DT_LOADING): its hooks run as that file's.
INST = scratch("appseam-inst.hibr")
open(INST, "w").write(r'''fn in_said(how, ...rest) { echo "$how ${DT_CALLER:-desktop}" >> %s; return 0; }
in_barhook() { [ -n "${IN_ONCE-}" ] && return 0; IN_ONCE=1; in_said barhook; }
fn in_mode(m) { in_said mode; }
fn in_item() { in_said item; }
fn inst_draw(int row, int col) { [ -n "${IN_DREW-}" ] && return 0; IN_DREW=1; in_said strip; }
fn inloc_can(op, p) { in_said location; }
fn inloc_ls(...a) { return 0; }
fn insv_start(int r, int c) { in_said saver; return 0; }
fn insv_frame(int r, int c) { return 0; }
cp_pane inpane "Inst" "x"
fn inpane_rows(int w) {
	CP[$w][0]["kind"]=set
	CP[$w][0]["text"]="Seam Row"
	CP[$w][0]["key"]=seamrow
	CP[$w][0]["val"]=plain
	CP[$w][0]["vname"]=in_namer
	ret 1
}
fn in_namer(v) { in_said namer; ret "$v"; }
sv_saver insv "Inst saver" 100
in_tick() { [ -n "${IN_TICKED-}" ] && return 0; IN_TICKED=1; in_said ticker; }
dt_ticker in_tick
dt_barhook in_barhook
dt_modehook in_mode
dt_baritem initem "x" in_item
cs_module inst "Inst" 4
dt_location inl inloc
''' % OUT)

p = scratch("appseam.hibr")
open(p, "w").write(SESSION % (load(MOD), WM, OUT, INST, OUT, MARK, MARK, MARK))
if os.path.exists(OUT):
    os.unlink(OUT)
t = Term(p, rows=24, cols=90, settle=0.6)
t.until("owner app", tries=60, step=0.1)


def do(cmd):
    """Run cmd in the desktop, from its ticker, and wait until it is taken."""
    open(MARK, "w").write(cmd + "\n")
    deadline = time.time() + 5
    while os.path.exists(MARK) and time.time() < deadline:
        t.send(b"\x1b[I", settle=0.1, collect=0.1)


def said():
    deadline = time.time() + 5
    while time.time() < deadline:
        if os.path.exists(OUT):
            return open(OUT).read().split("\n")
        time.sleep(0.05)
    return []


# A menu command declared in the app's own _menus, chosen from the bar.
do('dt_menus; tst_menu Say')
time.sleep(0.3)
check("a menu command the app declared runs as the app's", "menu ow" in said(), said())

# A note's action and a confirmation's Yes, handed in from the app's own
# code (through the seam, as every call into an app is).
# (A confirmation's command is one word: the desktop's files are strict,
# so a command kept in a variable is never split.)
do('dt_appfn ow ow_hand')
time.sleep(0.3)
do('dt_confirm_key y')
time.sleep(0.3)
check("a confirmation's Yes, asked from the app's code, runs as the app's",
      "confirm ow" in said(), said())
do('dt_appfn ow ow_post')
time.sleep(0.3)
do('n=${DT_NOTEQ_ORDER[-1]-}; [ -n "$n" ] && dt_note_act "$n"')
time.sleep(0.3)
check("a note's action, posted from the app's code, runs as the app's",
      "note ow" in said(), said())

# A dialog's answer, the dialog opened from the app's own code.
do('dt_appfn ow ow_ask')
time.sleep(0.3)
do('for id in "${!DTA[@]}"; do dt_askok "$id"; done')
time.sleep(0.3)
check("a dialog's answer, asked from the app's code, runs as the app's",
      "ask ow" in said(), said())

# And what the desktop asks for itself stays the desktop's.
do('dt_confirm "Desktop?" ow_mine desktop; dt_confirm_key y')
time.sleep(0.3)
check("what the desktop asks for itself runs as the desktop's",
      "mine desktop" in said(), said())

# An installable's hooks, each registered while its file loaded, run as
# that file's: a bar hook, a mode hook (called once at registration), a bar
# item's command, a strip module's drawing, a location's question.
do('dt_bar; dt_barido initem left; dt_stripdraw; dt_loccan inl:///x read; true')
time.sleep(0.3)
mine = [l for l in said() if l.split(" ")[-1:] == [INST]]
do('dt_appfn ow ow_regtick')
time.sleep(0.5)
check("a ticker an app registers from its own code runs as the app's", "rtick ow" in said(), said())
mine = [l for l in said() if l.split(" ")[-1:] == [INST]]
do('echo "${DT_HOOKOWNER[inpane_]-none}" > %s.pane; sv_begin insv; dt_saverstop 2> /dev/null; true' % OUT)
time.sleep(0.5)
pane = open(OUT + ".pane").read().strip() if os.path.exists(OUT + ".pane") else ""
check("a Control Panel pane's owner is the file that registered it", pane == INST, pane)
mine = [l for l in said() if l.split(" ")[-1:] == [INST]]
for how in ("barhook", "mode", "item", "location", "ticker", "saver"):
    check("an installable's %s runs as the file that registered it" % how,
          any(l.startswith(how + " ") for l in mine), said())

# Shared code the desktop runs calls into an app through the seam too: a
# text field typing into its window's _key, a scrollbar dragged telling its
# window where, and a list's namer while the desktop builds the list --
# each runs as the app's, never as the desktop's.
do('dt_fdtype "$OWID" Z 1; true')
time.sleep(0.3)
check("a field typing into a window's own _key runs it as the app's", "key ow" in said(), said())
do('dt_sbto "$OWID" bar 3 0; true')
time.sleep(0.3)
check("a scrollbar the desktop drags tells the window as the app's", "scroll ow" in said(), said())
do('dt_appfn ow ow_list')
time.sleep(0.5)
check("a list the app opened is named, while the desktop builds it, as the app's",
      "namer ow" in said(), said())
do('dt_mclose 2> /dev/null; true')
time.sleep(0.3)

# A pane's row is named through its namer as the pane's own, whichever id
# the rows were built under: here the search's, typed into the Control
# Panel, which builds every pane's rows under a scratch id of its own.
do('PNL := dt_new "Control Panel" 22 70 2 2 panel; dt_raise "$PNL"')
time.sleep(0.5)
for ch in b"/zzqx":
    t.send(bytes([ch]), settle=0.3, collect=0.3)
time.sleep(0.5)
check("a pane's row reached by a typed search is named as the pane's",
      ("namer " + INST) in said(), said())

t.quit(b"q", 1.0)
for f in (p, OUT, MARK, INST, OUT + ".pane"):
    if os.path.exists(f):
        os.unlink(f)
report(18)
