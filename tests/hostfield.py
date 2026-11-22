#!/usr/bin/env python3
"""A window drawn by its app's own process, where only that process knows
something the desktop acts on (ADR 0050, wm/rtcarry.hibr): whether its
field is live, how long a copy is, and whether a process kept with no
window is stuck.

    python3 tests/hostfield.py

Every app here is the test's own, in a file only its own process reads --
the desktop registers it (dt_app) and never sources it.

- A field: the desktop's Edit menu offers Copy, Cut, Paste and Select All
  for a live field in a hosted window whose app has none of its own, and
  Undo once there is typing to take back, as for a window of its own --
  that process says so with its menus. The Select All key selects the
  field's text even when it comes in the same read as the key that
  focused the field; with no field and no _selall it is the window's own
  key, as in-process.
- A copy longer than one line of the wire can carry is refused with a
  note, and the window's app goes on; a cut that long is not made, so its
  text stays where it was.
- A process kept with no window, stuck in a ticker that never returns --
  spinning, or blocked where nothing answers -- is a `held:` line in the
  log, judged from outside by its processor time or by where it waits,
  and left running; one kept for a quiet ticker and a job, or idle in its
  own wait with a ticker that asks for nothing, is never named. A ticker
  that stalls and returns is named by its own process, as that process.
"""
import os, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from screen import Term, check, report, load, tree, scratch, expect

MOD = tree("build/mods/console.so")
WM = tree("programs/desktop/desktop.hibr")
TERM = tree("programs/desktop/apps/term.hibr")
SAID = scratch("hostfield-said")
# A stall is judged against the session's DT_HOLDMS, which screen.py sets:
# 300 ms, and eight times that for the sanitizer build. A stall made here
# is counted on the processor, as a stall is judged, so a loaded box cannot
# make it a mere slow call: STALL clock ticks, a second at least and four
# thirds of the mark, spun for no longer than GUARD seconds by the clock.
# A process with no window is looked at every RES seconds, two marks' worth
# at least, since a spin is named only when one look to the next held the
# mark on the processor.
HOLD = int(os.environ["DT_HOLDMS"])
SLOW = 4 if os.environ.get("ASAN_OPTIONS") else 1
STALL = max(100, HOLD * 4 // 30)
GUARD = 4 * SLOW
RES = max(2, -(-2 * HOLD // 1000))
APPS = {n: scratch("hostfield-%s.hibr" % n) for n in ("fdapp", "mnapp", "keyapp", "bigapp", "cutapp", "spcapp", "spinapp", "calmapp", "blkapp", "idleapp")}

# A field and nothing else -- no _menus, no _copy, no _selall -- focused
# only once tab has been pressed. Its value is written out on each draw.
open(APPS["fdapp"], "w").write(r"""command -v dt_app > /dev/null && dt_app fdapp "Field" 6 40
declare -gA FD
fn fdapp_open(int id, arg = "") {
	FD["$id"]=hello
	FD["$id.c"]=5
	FD["$id.on"]=0
}
fn fdapp_draw(int id, int h, int w, int row, int col) {
	dt_textdraw "$id" 1 1 30 "${FD["$id"]}" "${FD["$id.c"]}" "${FD["$id.on"]}"
	printf '%%s\n' "${FD["$id"]}" > %(s)s.field
}
fn fdapp_key(int id, k) {
	local res
	if [ "${FD["$id.on"]}" = 0 ]; then
		[ "$k" = tab ] || return 1
		FD["$id.on"]=1
		return 0
	fi
	res := dt_textkey "${FD["$id"]}" "${FD["$id.c"]}" "$k" || return 1
	FD["$id"]=${res%% *}
	FD["$id.c"]=${res##* }
	return 0
}
""" % {"s": SAID})
# The same field in an app with menus of its own, for two windows of it.
open(APPS["mnapp"], "w").write(r"""command -v dt_app > /dev/null && dt_app mnapp "Menus" 6 40
declare -gA MN
fn mnapp_open(int id, arg = "") {
	MN["$id"]=hello
	MN["$id.c"]=5
	MN["$id.on"]=0
}
fn mnapp_draw(int id, int h, int w, int row, int col) {
	dt_textdraw "$id" 1 1 30 "${MN["$id"]}" "${MN["$id.c"]}" "${MN["$id.on"]}"
	echo "${MN["$id.on"]}" > %(s)s.mn$id
}
fn mnapp_key(int id, k) {
	[ "${MN["$id.on"]}" = 0 ] && [ "$k" = tab ] || return 1
	MN["$id.on"]=1
	return 0
}
mnapp_menus() {
	dt_menu "File"
	dt_item "Close" w dt_close_focused
	dt_editmenu
	dt_menu "Tools"
	dt_item "Ping" p true
}
""" % {"s": SAID})
# No field and no _selall: every key it is given is written out.
open(APPS["keyapp"], "w").write(r"""command -v dt_app > /dev/null && dt_app keyapp "Keys" 6 30
fn keyapp_draw(int id, int h, int w, int row, int col) {
	console put -p "w$id" 1 1 "keys"
}
fn keyapp_key(int id, k) {
	printf '%%s\n' "$k" >> %(s)s.keys
	return 0
}
""" % {"s": SAID})
# A window holding 2,097,152 characters, twice what the desktop reads a
# line to, with a Copy and a Cut of its own: what it holds is written out,
# by length, on each draw.
open(APPS["bigapp"], "w").write(r"""command -v dt_app > /dev/null && dt_app bigapp "Big" 6 30
declare -gA BIG
fn bigapp_open(int id, arg = "") {
	local s=a
	while [ "${#s}" -lt 2097152 ]; do s+=$s; done
	BIG["$id"]=$s
}
fn bigapp_draw(int id, int h, int w, int row, int col) {
	console put -p "w$id" 1 1 "big"
	echo "${#BIG["$id"]}" > %(s)s.big
}
fn bigapp_copy(int id) {
	ret "${BIG["$id"]}"
}
fn bigapp_cut(int id) {
	local t=${BIG["$id"]}
	BIG["$id"]=
	ret "$t"
}
""" % {"s": SAID})
# The same, with a Cut and a Paste and no Copy to measure a cut by.
open(APPS["cutapp"], "w").write(r"""command -v dt_app > /dev/null && dt_app cutapp "Cut" 6 30
declare -gA CUT
fn cutapp_open(int id, arg = "") {
	local s=a
	while [ "${#s}" -lt 2097152 ]; do s+=$s; done
	CUT["$id"]=$s
}
fn cutapp_draw(int id, int h, int w, int row, int col) {
	console put -p "w$id" 1 1 "cut"
	echo "${#CUT["$id"]}" > %(s)s.cut
}
fn cutapp_cut(int id) {
	local t=${CUT["$id"]}
	CUT["$id"]=
	ret "$t"
}
fn cutapp_paste(int id, text, op = "") {
	CUT["$id"]+=$text
}
""" % {"s": SAID})
# A window titled Spaces holding 400,000 spaces -- under the 1 MiB the
# desktop reads a line to as text, three times that on the wire, where a
# space is %20 -- and one titled Letters holding 300,000 letters, well
# under: each with a Copy of its own.
open(APPS["spcapp"], "w").write(r"""command -v dt_app > /dev/null && dt_app spcapp "Spaces" 6 30
declare -gA SPC
fn spcapp_open(int id, arg = "") {
	local s=" " n=400000 t
	[ "${DT[$id]["title"]-}" = Spaces ] || { s=a; n=300000; }
	t=$s
	while [ "${#t}" -lt "$n" ]; do t+=$t; done
	SPC["$id"]=${t:0:$n}
}
fn spcapp_draw(int id, int h, int w, int row, int col) {
	console put -p "w$id" 1 1 "spc"
	echo "${#SPC["$id"]}" > %(s)s.spc$id
}
fn spcapp_copy(int id) {
	ret "${SPC["$id"]}"
}
""" % {"s": SAID})
# A ticker that does nothing until a file says spin, then spins for four
# looks without returning; or says stall, then spins for STALL and
# returns.
open(APPS["spinapp"], "w").write(r"""command -v dt_app > /dev/null && dt_app spinapp "Spin" 6 30
spinapp_tick() {
	local e s t0
	local -a f
	echo "$$" > %(s)s.spid
	if [ -f %(s)s.stall ]; then
		command rm -f %(s)s.stall
		e=$((${EPOCHREALTIME/./} + %(guard)d))
		read -r s < /proc/$$/stat
		f=(${s##*) })
		t0=$((f[11] + f[12]))
		while [ "${EPOCHREALTIME/./}" -lt "$e" ]; do
			read -r s < /proc/$$/stat
			f=(${s##*) })
			[ $((f[11] + f[12] - t0)) -lt %(stall)d ] || break
		done
		echo done > %(s)s.stalled
	fi
	if [ -f %(s)s.spin ]; then
		command rm -f %(s)s.spin
		e=$((SECONDS + %(spin)d))
		while [ "$SECONDS" -lt "$e" ]; do :; done
		echo done > %(s)s.spun
	fi
	dt_want 200
}
command -v dt_ticker > /dev/null && dt_ticker spinapp_tick
fn spinapp_draw(int id, int h, int w, int row, int col) {
	console put -p "w$id" 1 1 "spin"
}
""" % {"s": SAID, "stall": STALL, "guard": GUARD * 1000000, "spin": 4 * RES})
# A ticker that, once a file says block, opens a pipe nothing writes to and
# waits there, using no processor, until the test writes to it.
open(APPS["blkapp"], "w").write(r"""command -v dt_app > /dev/null && dt_app blkapp "Block" 6 30
blkapp_tick() {
	local x
	echo "$$" > %(s)s.bpid
	if [ -f %(s)s.block ]; then
		command rm -f %(s)s.block
		read -r x < %(s)s.fifo
		echo done > %(s)s.unblocked
	fi
	dt_want 200
}
command -v dt_ticker > /dev/null && dt_ticker blkapp_tick
fn blkapp_draw(int id, int h, int w, int row, int col) {
	console put -p "w$id" 1 1 "block"
}
""" % {"s": SAID})
# A ticker that asks for nothing: kept with no window, its process sits in
# its own wait with no processor and no wakes, as a blocked one does.
open(APPS["idleapp"], "w").write(r"""command -v dt_app > /dev/null && dt_app idleapp "Idle" 6 30
idleapp_tick() {
	echo "$$" > %(s)s.ipid
}
command -v dt_ticker > /dev/null && dt_ticker idleapp_tick
fn idleapp_draw(int id, int h, int w, int row, int col) {
	console put -p "w$id" 1 1 "idle"
}
""" % {"s": SAID})
# A quiet ticker and a job that sleeps, kept with no window.
open(APPS["calmapp"], "w").write(r"""command -v dt_app > /dev/null && dt_app calmapp "Calm" 6 30
calmapp_tick() {
	echo "$$" > %(s)s.cpid
	dt_want 300
}
command -v dt_ticker > /dev/null && dt_ticker calmapp_tick
fn calmapp_draw(int id, int h, int w, int row, int col) {
	console put -p "w$id" 1 1 "calm"
}
fn calmapp_close(int id) {
	dt_job test close calmapp_done sleep 6
	return 0
}
fn calmapp_done(n, st = "") {
	echo "$st" > %(s)s.cjob
}
""" % {"s": SAID})

SESSION = r'''%s. %s
dt_open
DT_NOTEMS=60000
%s
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
REG = "\n".join('DT_LOADING=%s dt_app %s "%s" 6 40' % (APPS[n], n, n) for n in APPS) + "\nDT_LOADING="
# A window's view and its _open are two lines, which an app's process may
# read in two goes: every open told is held back 300 ms here, so the app
# always has the view first, and a window drawn before its _open has run
# is certain rather than a matter of timing.
REG += r'''
eval "$(declare -f dt_rtsay | sed '1s/dt_rtsay(/tst_rtsay(/')"
TST_HELD=()
TST_HELDAT=0
dt_rtsay() {
	case $2 in
	"call 0 "*" open"*) TST_HELD+=("$1" "$2"); TST_HELDAT=${EPOCHREALTIME/./}; dt_want 300; return 0 ;;
	esac
	tst_rtsay "$@"
}
tst_unhold() {
	local i
	[ "${#TST_HELD[@]}" -gt 0 ] || return 0
	[ $(( ${EPOCHREALTIME/./} - TST_HELDAT )) -ge 300000 ] || { dt_want 50; return 0; }
	for ((i = 0; i < ${#TST_HELD[@]}; i += 2)); do
		tst_rtsay "${TST_HELD[$i]}" "${TST_HELD[$((i + 1))]}"
	done
	TST_HELD=()
}
dt_ticker tst_unhold
'''


def session(name, body, env=None):
    p = scratch("hostfield-%s.hibr" % name)
    mark = scratch("hostfield-do-%s" % name)
    open(p, "w").write(SESSION % (load(MOD), WM, body, mark, mark, mark))
    e = {"DT_BARTIME": "12:00", "DT_RTENDSECS": "2"}
    e.update(env or {})
    term = Term(p, rows=30, cols=120, settle=0.6, env=e)
    term.mark = mark
    return term


def do(t, cmd):
    """Run cmd in t's desktop, from its ticker, and wait until it is taken."""
    open(t.mark, "w").write(cmd + "\n")
    deadline = time.time() + 5
    while os.path.exists(t.mark) and time.time() < deadline:
        t.send(b"\x1b[I", settle=0.05, collect=0.05)


def result(t, cmd):
    """Run cmd, which writes SAID.pend, and answer what it wrote."""
    if os.path.exists(SAID + ".pend"):
        os.unlink(SAID + ".pend")
    do(t, cmd + " > %s.pend" % SAID)
    deadline = time.time() + 20
    while time.time() < deadline and not os.path.exists(SAID + ".pend"):
        t.collect(0.05)
    t.collect(0.05)
    return open(SAID + ".pend").read().strip() if os.path.exists(SAID + ".pend") else ""


def waitfor(t, cmd, secs=10):
    deadline = time.time() + secs
    while time.time() < deadline:
        if result(t, "if %s; then echo yes; else echo no; fi" % cmd) == "yes":
            return True
    return False


def read(f):
    return open(SAID + f).read().strip() if os.path.exists(SAID + f) else ""


def until(t, f, want, secs=8):
    """Wait until SAID+f holds want (a function of its text)."""
    deadline = time.time() + secs
    while time.time() < deadline:
        if want(read(f)):
            return True
        t.collect(0.1)
    return want(read(f))


def alive(pid):
    try:
        return open("/proc/%s/stat" % pid).read().rsplit(")", 1)[1].split()[0] != "Z"
    except (OSError, IndexError):
        return False


def cpu(pid):
    """Processor time a process has spent, in clock ticks (fields 14 and 15)."""
    try:
        f = open("/proc/%s/stat" % pid).read().rsplit(")", 1)[1].split()
        return int(f[11]) + int(f[12])
    except (OSError, IndexError):
        return None


def log(t):
    return open(t.log, errors="replace").read() if os.path.exists(t.log) else ""


def host(t, app):
    do(t, "dt_rthost %s" % app)
    return waitfor(t, '[ -n "${DT_RTHOST[%s]-}" ]' % app)


def notes(t):
    return int(result(t, 'echo "$DT_NOTE_NEXT"') or -1)


def lastnote(t):
    return result(t, 'echo "${DT_NOTEQ[$((DT_NOTE_NEXT - 1))]["text"]-}"')



# The Edit menu as the desktop builds it now for the focused window: each
# row's label and whether it is dimmed.
REG += r"""
tst_edit() {
	local m r n
	DT_MENUIN=
	MB_N=0
	dt_menus
	for ((m = 0; m < MB_N; m++)); do
		[ "${MB[$m]["title"]-}" = Edit ] || continue
		n=${MB[$m]["n"]:-0}
		for ((r = 0; r < n; r++)); do
			[ -n "${MI[$m][$r]["label"]-}" ] && printf '%s=%s;' "${MI[$m][$r]["label"]}" "${MI[$m][$r]["dim"]-}"
		done
	done
	echo
}
"""


# Every menus line an app's process pushes for a window that does not hold
# the focus when it arrives, written out once TST_STRAY is set: the desktop
# keeps one menus for an app, so such a push takes the app's menus away
# from the window that has the focus.
REG += r"""
TST_STRAY=
eval "$(declare -f dt_rvpush_in | sed '1s/dt_rvpush_in(/tst_rvpush_in(/')"
dt_rvpush_in() {
	if [ -n "$TST_STRAY" ] && [ "$2" = menus ] && [[ ${4-} =~ ^\{\"id\":\"?([0-9]+) ]] &&
		[ "${BASH_REMATCH[1]}" != "$DT_FOCUS" ]; then
		echo "${BASH_REMATCH[1]} while $DT_FOCUS has the focus" >> %s.stray
	fi
	tst_rvpush_in "$@"
}
tst_bar() {
	local m
	DT_MENUIN=
	MB_N=0
	dt_menus
	for ((m = 0; m < MB_N; m++)); do
		[ -n "${MB[$m]["par"]-}" ] || printf '%%s;' "${MB[$m]["title"]-}"
	done
	echo
}
""" % SAID


def edit(t):
    return dict(p.split("=") for p in result(t, "tst_edit").split(";") if "=" in p)


for f in os.listdir(os.path.dirname(SAID)):
    if f.startswith(os.path.basename(SAID) + "."):
        os.unlink(os.path.join(os.path.dirname(SAID), f))

# Open issue 1 (dt_holdsess is not in an app's own process), reached here by
# a job started there (dt_jobname); and a 2 MiB cut held on purpose, which
# its process takes past the stall mark to measure, make and put back.
expect(r"dt_holdsess: command not found")
expect(r"held: input:(big|cut)app has not answered")
t = session("hosted", REG)

# --- A field in a hosted window, and the Edit menu ------------------------
host(t, "fdapp")
do(t, "F := dt_new Field 6 40 3 2 fdapp")
until(t, ".field", lambda s: s == "hello")
do(t, 'dt_clipset "pasted" copy text 0')
m = edit(t)
check("with no field live, Copy and Select All are dimmed for a window with none of its own (as before)",
      m.get("Copy") == "1" and m.get("Select All") == "1", m)
t.send(b"\t\x1ba", settle=0.4, collect=0.4)        # tab into the field, then Select All, in one read
t.send(b"\x1bc", settle=0.4, collect=0.4)          # Copy
check("the Select All key in the same read as the key that focused the field selects its text",
      waitfor(t, '[ "$DT_CLIP" = hello ]', 4), result(t, 'echo "[$DT_CLIP]"') + "\n" + log(t)[-600:])
waitfor(t, '[ "${DT_RVMENU["menus:fdapp"]["field"]-}" = 1 ]', 4)
m = edit(t)
check("once the field is live, Copy, Cut, Paste and Select All are offered though the app has none",
      all(m.get(k) == "0" for k in ("Copy", "Cut", "Paste", "Select All")), m)
check("and Undo is dimmed while there is no typing to take back",
      m.get("Undo") == "1", m)
t.send(b"\x1b[F", settle=0.3, collect=0.3)         # end
t.send(b"!", settle=0.4, collect=0.4)
until(t, ".field", lambda s: s == "hello!")
waitfor(t, '[ "${DT_RVMENU["menus:fdapp"]["fundo"]-}" = 1 ]', 4)
m = edit(t)
check("after typing in it, Undo is offered", m.get("Undo") == "0", (m, read(".field")))

# --- Two windows of one app with menus of its own -------------------------
host(t, "mnapp")
do(t, "MA := dt_new MenuA 6 40 14 2 mnapp")
do(t, "MW := dt_new MenuB 6 40 14 60 mnapp")
ma, mw = result(t, 'echo "$MA"'), result(t, 'echo "$MW"')
until(t, ".mn" + ma, lambda s: s == "0", 8)       # each opened in its process and drawn
until(t, ".mn" + mw, lambda s: s == "0", 8)
do(t, "dt_raise $MA")
t.send(b"\t", settle=0.4, collect=0.4)            # into the first window's field
until(t, ".mn" + ma, lambda s: s == "1", 6)
check("tab into the first window's field: its process says the field is live, with its menus",
      waitfor(t, '[ "${DT_RVMENU["menus:mnapp"]["id"]-}" = "$MA" ] && [ "${DT_RVMENU["menus:mnapp"]["field"]-}" = 1 ]', 6),
      (read(".mn" + ma), read(".mn" + mw), result(t, 'echo "focus=$DT_FOCUS MA=$MA MW=$MW id=${DT_RVMENU["menus:mnapp"]["id"]-} field=${DT_RVMENU["menus:mnapp"]["field"]-}"')))
do(t, "TST_STRAY=1; dt_raise $MW")
waitfor(t, '[ "${DT_RVMENU["menus:mnapp"]["id"]-}" = "$MW" ]', 6)
t.collect(1.5)
check("a window that has lost the focus never pushes its app's menus over the focused window's",
      read(".stray") == "", read(".stray"))
bar, m = result(t, "tst_bar"), edit(t)
check("the window with the focus keeps its app's menus, its own field not live: Copy dimmed",
      "Tools" in bar.split(";") and m.get("Copy") == "1", (bar, m))
do(t, "dt_raise $MA")
waitfor(t, '[ "${DT_RVMENU["menus:mnapp"]["id"]-}" = "$MA" ] && [ "${DT_RVMENU["menus:mnapp"]["field"]-}" = 1 ]', 6)
bar, m = result(t, "tst_bar"), edit(t)
check("and back in the first window, its field is live again: Copy offered, with the app's menus",
      "Tools" in bar.split(";") and m.get("Copy") == "0" and read(".stray") == "",
      (bar, m, read(".stray"), read(".mn" + ma),
       result(t, 'echo "focus=$DT_FOCUS id=${DT_RVMENU["menus:mnapp"]["id"]-} field=${DT_RVMENU["menus:mnapp"]["field"]-}"')))
do(t, "TST_STRAY=; dt_del $MA; dt_del $MW")

# --- Select All with no field and no _selall ------------------------------
host(t, "keyapp")
do(t, "K := dt_new Keys 6 30 12 2 keyapp")
until(t, ".keys", lambda s: True, 1)
t.send(b"\x1ba", settle=0.4, collect=0.4)
check("the Select All key in a window with no field and no _selall is the window's own key (as before)",
      until(t, ".keys", lambda s: "alt-a" in s.split("\n")), read(".keys"))

# --- A copy longer than one line can carry --------------------------------
host(t, "bigapp")
do(t, "B := dt_new Big 6 30 3 60 bigapp")
until(t, ".big", lambda s: s == "2097152", 15)
n0 = notes(t)
t.send(b"\x1bc", settle=0.5, collect=0.5)          # Copy
check("a copy of 2,097,152 characters is refused with a note",
      waitfor(t, '[ "${DT_NOTEQ[$((DT_NOTE_NEXT - 1))]["text"]-}" = "Too much to copy at once: 2097152 characters" ]', 10),
      (n0, notes(t), lastnote(t), log(t)[-600:]))
check("and the window's app goes on: the connection is kept",
      result(t, 'echo "${DT_RVC[$B]-none}"') not in ("-", "none", ""),
      result(t, 'echo "[${DT_RVC[$B]-none}]"') + "\n" + log(t)[-600:])
t.send(b"\x1bx", settle=0.5, collect=0.5)          # Cut
waitfor(t, '[ "${DT_NOTEQ[$((DT_NOTE_NEXT - 1))]["text"]-}" = "Too much to cut at once: 2097152 characters" ]', 10)
check("a cut that long is not made: the window still holds all of it",
      lastnote(t) == "Too much to cut at once: 2097152 characters" and read(".big") == "2097152"
      and result(t, 'echo "${DT_RVC[$B]-none}"') not in ("-", "none", ""),
      (lastnote(t), read(".big"), log(t)[-600:]))
do(t, "dt_del $B")
host(t, "cutapp")
do(t, "X := dt_new Cut 6 30 3 60 cutapp")
until(t, ".cut", lambda s: s == "2097152", 15)
t.send(b"\x1bx", settle=0.5, collect=0.5)          # Cut
waitfor(t, '[ "${DT_NOTEQ[$((DT_NOTE_NEXT - 1))]["text"]-}" = "Too much to cut at once: 2097152 characters" ]', 10)
t.collect(0.5)
check("a cut that long from a window with no Copy to measure it by is put back with its Paste: nothing lost",
      lastnote(t) == "Too much to cut at once: 2097152 characters" and read(".cut") == "2097152"
      and result(t, 'echo "${DT_RVC[$X]-none}"') not in ("-", "none", ""),
      (lastnote(t), read(".cut"), log(t)[-600:]))
do(t, "dt_del $X")

# --- Long as text, or long only on the wire -------------------------------
host(t, "spcapp")
# Both open before either closes: an app with no ticker ends its process
# with its last window, and a window opened as it ends is not this test's.
do(t, "P := dt_new Spaces 6 30 3 90 spcapp")
until(t, ".spc" + result(t, 'echo "$P"'), lambda s: s == "400000", 15)
do(t, "L := dt_new Letters 6 30 3 60 spcapp")
until(t, ".spc" + result(t, 'echo "$L"'), lambda s: s == "300000", 15)
t.send(b"\x1bc", settle=0.5, collect=0.5)          # Copy
check("a copy of 300,000 characters reaches the clipboard whole (control)",
      waitfor(t, '[ "${#DT_CLIP}" = 300000 ]', 10), result(t, 'echo "${#DT_CLIP}"'))
do(t, "dt_raise $P")
t.send(b"\x1bc", settle=0.5, collect=0.5)          # Copy
check("a copy of 400,000 spaces, three times that on the wire, is refused with a note, the connection kept",
      waitfor(t, '[ "${DT_NOTEQ[$((DT_NOTE_NEXT - 1))]["text"]-}" = "Too much to copy at once: 400000 characters" ]', 10)
      and result(t, 'echo "${DT_RVC[$P]-none}"') not in ("-", "none", ""),
      (lastnote(t), result(t, 'echo "[${DT_RVC[$P]-none}]"'), log(t)[-600:]))
do(t, "dt_del $L; dt_del $P")

# --- A process kept with no window ----------------------------------------
do(t, "DT_RTENDSECS=%d" % RES)
host(t, "spinapp")
do(t, "S := dt_new Spin 6 30 12 60 spinapp")
until(t, ".spid", lambda s: s != "")
spid = read(".spid")
# A ticker that stalls and returns is timed in its own process, as in the
# desktop, and named there -- and so is the eight-second spin below, once
# it ends. Before it: a smaller stall of the same name within ten seconds
# of a bigger one is not said again (dt_slow).
expect(r"held: spinapp_tick held")
open(SAID + ".stall", "w").close()
check("an app process's ticker that stalls a second and returns is a held: line, from its own timing, "
      "naming that process and not the desktop",
      until(t, ".stalled", lambda s: s == "done", 5 + GUARD) and
      until(t, "", lambda s: "held: spinapp_tick held spinapp's own process" in log(t), 6),
      log(t)[-800:])
do(t, "dt_del $S")
time.sleep(2.5)
open(SAID + ".spin", "w").close()
expect(r"held: resident:spinapp")
check("a process kept with no window and stuck in its ticker is a held: line in the log, judged from outside",
      until(t, "", lambda s: "held: resident:spinapp" in log(t), 3 * RES + 3), log(t)[-800:])
check("and it is left running", alive(spid), spid)
check("said once, though it went on spinning",
      log(t).count("held: resident:spinapp") == 1, log(t).count("held: resident:spinapp"))


host(t, "calmapp")
do(t, "C := dt_new Calm 6 30 12 2 calmapp")
until(t, ".cpid", lambda s: s != "")
do(t, "dt_del $C")
until(t, ".cjob", lambda s: s != "", 12)
check("one kept for a quiet ticker and a job that sleeps is never named (control)",
      read(".cjob") == "0" and "resident:calmapp" not in log(t), (read(".cjob"), log(t)[-800:]))


# --- A process kept with no window, blocked rather than busy ---------------
os.mkfifo(SAID + ".fifo")
host(t, "blkapp")
host(t, "idleapp")
do(t, "BK := dt_new Block 6 30 12 60 blkapp")
do(t, "IW := dt_new Idle 6 30 12 2 idleapp")
until(t, ".bpid", lambda s: s != "")
until(t, ".ipid", lambda s: s != "")
bpid, ipid = read(".bpid"), read(".ipid")
do(t, "dt_del $BK; dt_del $IW")
time.sleep(2.5)
expect(r"held: resident:blkapp")
open(SAID + ".block", "w").close()
check("a process kept with no window whose ticker is blocked, opening a pipe nothing writes to, is a held: "
      "line judged from outside, naming where it waits",
      until(t, "", lambda s: "held: resident:blkapp" in log(t) and "wait_for_partner" in log(t), 4 * RES + 6), log(t)[-800:])
t.collect(2 * RES + 0.5)                           # two looks more
blocked = alive(bpid) and log(t).count("held: resident:blkapp") == 1
try:
    fd = os.open(SAID + ".fifo", os.O_WRONLY | os.O_NONBLOCK)
    os.write(fd, b"go\n")
    os.close(fd)
except OSError:
    pass
check("and it is left running, said once while it waits, and its ticker goes on once the pipe is written",
      blocked and until(t, ".unblocked", lambda s: s == "done", 6),
      (alive(bpid), log(t).count("held: resident:blkapp"), read(".unblocked")))
check("one kept with no window idle in its own wait, its ticker asking for nothing, is never named (control)",
      alive(ipid) and "resident:idleapp" not in log(t), log(t)[-800:])

t.close()
for f in os.listdir(os.path.dirname(SAID)):
    if f.startswith(os.path.basename(SAID) + "."):
        os.unlink(os.path.join(os.path.dirname(SAID), f))
report(25)
