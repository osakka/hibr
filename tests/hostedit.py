#!/usr/bin/env python3
"""What a window drawn by its app's own process carries besides its view
and its input (ADR 0050, wm/rtcarry.hibr): the Edit menu's verbs, a
window's _idle, the app's work that goes on with no window left, and the
command a terminal window is opened to run.

    python3 tests/hostedit.py

Every app here is the test's own, in a file only its own process reads --
the desktop registers it (dt_app) and never sources it -- except the
Terminal, which is the shipped one, started in a process of its own.

- Edit: a field in a hosted window takes Cut and Paste itself, in its own
  process, and the desktop's clipboard gets what was cut; a window's own
  _copy hands back paths, said as files, and a secret, kept out of the
  history; Undo takes back the field's typing; Find reaches the window's
  own _find; a copy answered after the clipboard has changed is kept in
  the history and never made the clipboard over the newer one. One note
  each.
- _idle: a window whose app watches a descriptor reads it in its own
  process, every pass, though its _dirty says clean -- what it reads is
  drawn, and the process does not spin (its processor time, not its
  wakes).
- Work with no window: an app whose file registers a ticker keeps its
  process, and the ticker goes on, after its last window closes; one that
  started a job keeps its process until the job ends, its callback runs,
  and then ends by itself.
- A terminal window opened to run a command runs it hosted, as in-process.
"""
import os, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from screen import Term, check, report, load, tree, scratch, expect

MOD = tree("build/mods/console.so")
WM = tree("programs/desktop/desktop.hibr")
TERM = tree("programs/desktop/apps/term.hibr")
SAID = scratch("hostedit-said")
FIFO = scratch("hostedit-fifo")
APPS = {n: scratch("hostedit-%s.hibr" % n) for n in ("edapp", "edwin", "idler", "bgapp", "jobapp")}

# A field and nothing else: no _copy, _cut, _paste or _undo of its own, so
# every edit is the field's. Its value is written out on each draw.
open(APPS["edapp"], "w").write(r'''command -v dt_app > /dev/null && dt_app edapp "Edit" 6 40
declare -gA ED
fn edapp_open(int id, arg = "") {
	ED["$id"]=hello
	ED["$id.c"]=5
}
fn edapp_draw(int id, int h, int w, int row, int col) {
	dt_textdraw "$id" 1 1 30 "${ED["$id"]}" "${ED["$id.c"]}" 1
	printf '%%s\n' "${ED["$id"]}" > %(s)s.field
}
fn edapp_key(int id, k) {
	local res
	res := dt_textkey "${ED["$id"]}" "${ED["$id.c"]}" "$k" || return 1
	ED["$id"]=${res%% *}
	ED["$id.c"]=${res##* }
	return 0
}
''' % {"s": SAID})
# A window with a Copy and a Find of its own: what Copy hands back is chosen
# by a file the test writes.
open(APPS["edwin"], "w").write(r'''command -v dt_app > /dev/null && dt_app edwin "EdWin" 6 30
fn edwin_draw(int id, int h, int w, int row, int col) {
	console put -p "w$id" 1 1 "copy me"
}
fn edwin_copy(int id) {
	local m e
	m=$(< %(s)s.mode)
	case $m in
	paths) DT_CLIPKIND=paths; ret $'/tmp/one\n/tmp/two' ;;
	secret) DT_CLIPKIND=secret; ret "s3cret-word" ;;
	slow)
		e=$((SECONDS + 3))
		while [ "$SECONDS" -lt "$e" ]; do :; done
		ret "late text"
		;;
	*) ret "plain text" ;;
	esac
}
fn edwin_find(int id) {
	echo "$$" > %(s)s.find
}
''' % {"s": SAID})
# A window whose app watches a fifo: read by _idle, drawn by _draw only when
# _dirty says something new came.
open(APPS["idler"], "w").write(r'''command -v dt_app > /dev/null && dt_app idler "Idler" 6 30
declare -gA IDL
fn idler_open(int id, arg = "") {
	local fd
	exec {fd}<> %(f)s
	IDL["$id.fd"]=$fd
	IDL["$id.n"]=0
	IDL["$id.seen"]=-1
	console watch "$fd"
	echo "$$" > %(s)s.ipid
}
fn idler_dirty(int id) {
	[ "${IDL["$id.n"]}" != "${IDL["$id.seen"]}" ]
}
fn idler_draw(int id, int h, int w, int row, int col) {
	IDL["$id.seen"]=${IDL["$id.n"]}
	console put -p "w$id" 1 1 "lines ${IDL["$id.n"]} "
}
fn idler_idle(int id) {
	local l fd=${IDL["$id.fd"]}
	while read -t 0 -u "$fd"; do
		read -r -u "$fd" l || break
		IDL["$id.n"]=$((IDL["$id.n"] + 1))
	done
	return 0
}
''' % {"s": SAID, "f": FIFO})
# An app whose file registers a ticker, which writes a beat every 300 ms.
open(APPS["bgapp"], "w").write(r'''command -v dt_app > /dev/null && dt_app bgapp "Bg" 6 30
BG_N=0
bgapp_tick() {
	BG_N=$((BG_N + 1))
	echo "$$ $BG_N" > %(s)s.bg
	dt_want 300
}
command -v dt_ticker > /dev/null && dt_ticker bgapp_tick
fn bgapp_draw(int id, int h, int w, int row, int col) {
	console put -p "w$id" 1 1 "background"
}
''' % {"s": SAID})
# An app with no ticker whose close starts a job longer than the desktop
# waits for a windowless process to go.
open(APPS["jobapp"], "w").write(r'''command -v dt_app > /dev/null && dt_app jobapp "Job" 6 30
fn jobapp_draw(int id, int h, int w, int row, int col) {
	console put -p "w$id" 1 1 "job"
}
fn jobapp_close(int id) {
	echo "$$" > %(s)s.jpid
	dt_job test close jobapp_done sleep 4
	return 0
}
fn jobapp_done(n, st = "") {
	echo "$$ $st" > %(s)s.job
}
''' % {"s": SAID})

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
REG = "\n".join('DT_LOADING=%s dt_app %s "%s" 6 40' % (APPS[n], n, n) for n in APPS) + \
    '\nDT_LOADING=%s dt_app term "Terminal" 14 52 many ">_"\nDT_LOADING=' % TERM
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
    p = scratch("hostedit-%s.hibr" % name)
    mark = scratch("hostedit-do-%s" % name)
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


for f in os.listdir(os.path.dirname(SAID)):
    if f.startswith(os.path.basename(SAID) + "."):
        os.unlink(os.path.join(os.path.dirname(SAID), f))
if os.path.exists(FIFO):
    os.unlink(FIFO)
os.mkfifo(FIFO)

t = session("hosted", REG)

# --- Edit: a field in a hosted window ------------------------------------
host(t, "edapp")
do(t, "E := dt_new Edit 6 40 3 2 edapp")
until(t, ".field", lambda s: s == "hello")
check("a window is never drawn before its _open has run in its own process",
      read(".field") == "hello" and "cur expects int" not in log(t), (read(".field"), log(t)[-400:]))
n0 = notes(t)
t.send(b"\x1b[1;2H", settle=0.3, collect=0.3)      # shift-home: select it all
t.send(b"\x1bx", settle=0.3, collect=0.3)          # Cut
check("Cut takes the field's selection out, in its own process",
      until(t, ".field", lambda s: s == ""), read(".field") + "\n" + log(t)[-500:])
check("and the desktop's clipboard holds what was cut",
      waitfor(t, '[ "$DT_CLIP" = hello ] && [ "$DT_CLIPOP" = cut ]', 4),
      result(t, 'echo "[$DT_CLIP] [$DT_CLIPOP]"'))
check("with one note, saying so", notes(t) == n0 + 1 and lastnote(t) == "Cut 5 characters",
      (n0, notes(t), lastnote(t)))
t.send(b"x", settle=0.3, collect=0.3)
t.send(b"\x1bv", settle=0.3, collect=0.3)          # Paste
check("Paste types the clipboard into the field after what was typed",
      until(t, ".field", lambda s: s == "xhello"), read(".field"))
time.sleep(1.2)
t.send(b"!", settle=0.3, collect=0.3)
until(t, ".field", lambda s: s == "xhello!")
time.sleep(0.3)
t.send(b"\x1bz", settle=0.3, collect=0.3)          # Undo
check("Undo takes back the field's own typing, in its own process",
      until(t, ".field", lambda s: s == "xhello"), read(".field") + "\n" + log(t)[-400:])

# --- Edit: a window's own Copy and Find ------------------------------------
host(t, "edwin")
do(t, "W := dt_new EdWin 6 30 3 50 edwin")
waitfor(t, '[ -n "${DT_RVARGS[$W]-}" ]', 5)
open(SAID + ".mode", "w").write("paths")
n0 = notes(t)
t.send(b"\x1bc", settle=0.3, collect=0.3)          # Copy
check("a window's own _copy is asked in its process, and paths come back as paths",
      waitfor(t, '[ "$DT_CLIPKIND" = paths ] && [ "$DT_CLIP" = $\'/tmp/one\\n/tmp/two\' ]', 4),
      result(t, 'echo "[$DT_CLIPKIND] [$DT_CLIP]"'))
check("said as files, once", notes(t) == n0 + 1 and lastnote(t) == "Copied 2 files", (lastnote(t), notes(t) - n0))
open(SAID + ".mode", "w").write("secret")
h0 = result(t, 'echo "$DT_CHNEXT"')
t.send(b"\x1bc", settle=0.3, collect=0.3)
check("a secret is made the clipboard",
      waitfor(t, '[ "$DT_CLIP" = s3cret-word ] && [ "$DT_CLIPKIND" = secret ]', 4),
      result(t, 'echo "[$DT_CLIPKIND]"'))
check("and kept out of the history and the log",
      result(t, 'echo "$DT_CLIP"') == "s3cret-word" and result(t, 'echo "$DT_CHNEXT"') == h0
      and "s3cret" not in log(t), (h0, result(t, 'echo "$DT_CHNEXT"')))
do(t, "dt_editdo find")
check("Find reaches the window's own _find, in its own process",
      until(t, ".find", lambda s: s.isdigit() and s != str(t.pid)), read(".find"))
# The slow copy spins three seconds in its own process: a stall, named in
# the log as one, which is the point.
expect(r"held: input:edwin")
open(SAID + ".mode", "w").write("slow")
t.send(b"\x1bc", settle=0.2, collect=0.2)
time.sleep(0.5)
do(t, "dt_clipset newer copy text")
check("a copy answered after the clipboard changed leaves the newer one the clipboard",
      waitfor(t, '[ "${DT_CH[$((DT_CHNEXT - 1))]["text"]-}" = "late text" ]', 8) and
      result(t, 'echo "$DT_CLIP"') == "newer",
      (result(t, 'echo "$DT_CLIP"'), log(t)[-500:]))
check("and is kept in its history, said in the log", "came back after the clipboard changed" in log(t),
      log(t)[-400:])

# --- _idle ---------------------------------------------------------------
host(t, "idler")
do(t, "I := dt_new Idler 6 30 12 2 idler")
until(t, ".ipid", lambda s: s.isdigit())
ipid = read(".ipid")
fd = os.open(FIFO, os.O_WRONLY | os.O_NONBLOCK)
os.write(fd, b"one\ntwo\nthree\n")
ok = False
deadline = time.time() + 5
while time.time() < deadline and not ok:
    t.collect(0.1)
    ok = "lines 3" in t.screen().text()
check("a window's _idle reads what its app watches, in its own process, and it is drawn",
      ok, t.screen().text())
c0 = cpu(ipid)
time.sleep(2)
c1 = cpu(ipid)
hz = os.sysconf("SC_CLK_TCK")
check("and the process does not spin on it: the console never gives up waking for it, "
      "and under a tenth of a core over two seconds",
      "with nothing reading it" not in log(t) and c0 is not None and c1 is not None
      and (c1 - c0) < 0.2 * hz, (c0, c1, hz, log(t)[-300:]))
os.close(fd)

# --- work with no window -------------------------------------------------
host(t, "bgapp")
do(t, "B := dt_new Bg 6 30 20 2 bgapp")
until(t, ".bg", lambda s: len(s.split()) == 2)
bpid = read(".bg").split()[0]
do(t, "dt_del $B")
waitfor(t, '! dt_iswin "$B"', 4)
b0 = int((read(".bg").split() + ["0", "0"])[1])
time.sleep(5)
b1 = int((read(".bg").split() + ["0", "0"])[1])
check("an app whose file registers a ticker keeps its process with no window left, "
      "past the time a windowless one is given", alive(bpid), (bpid, log(t)[-500:]))
check("and its ticker goes on", b1 > b0 + 5, (b0, b1))
check("the desktop seeing its mark in its jobs folder, said once in the log",
      log(t).count("process %s has no window left and is still at work (its jobs folder)" % bpid) == 1,
      log(t)[-400:])
wk = result(t, r'DT_WANT=; dt_rvtick; m=; n=$((${EPOCHREALTIME/./} / 1000)); '
               r'for p in "${!DT_RTENDPID[@]}"; do x=$((DT_RTENDPID[$p] - n)); '
               r'[ -z "$m" ] || [ "$x" -lt "$m" ] && m=$x; done; echo "${DT_WANT:-none} ${m:-none}"').split()
check("and looking at it only when its time to end comes, not ten times a second",
      len(wk) == 2 and wk[1] != "none" and wk[0] != "none" and abs(int(wk[0]) - int(wk[1])) <= 50, wk)
try:
    sm = dict(l.split(":", 1) for l in open("/proc/%s/smaps_rollup" % bpid).read().splitlines()[1:])
    print("a windowless app process kept for its ticker: Rss %s, Pss %s"
          % (sm["Rss"].strip(), sm["Pss"].strip()))
except (OSError, KeyError, ValueError):
    pass
host(t, "jobapp")
do(t, "J := dt_new Job 6 30 20 40 jobapp")
waitfor(t, '[ -n "${DT_RVARGS[$J]-}" ]', 5)
do(t, "dt_del $J")
until(t, ".jpid", lambda s: s.isdigit())
jpid = read(".jpid")
time.sleep(3)
check("an app whose job runs on keeps its process past the time a windowless one is given, "
      "the desktop seeing the job in its jobs folder",
      jpid and alive(jpid) and not read(".job")
      and "process %s has no window left and is still at work (its jobs folder)" % jpid in log(t),
      (jpid, read(".job"), log(t)[-400:]))
done = until(t, ".job", lambda s: s == "%s 0" % jpid, 12)
deadline = time.time() + 8
while time.time() < deadline and jpid and alive(jpid):
    t.collect(0.2)
check("its job's callback runs in it when the job ends, and then it ends by itself",
      done and jpid and not alive(jpid)
      and "process %s had no window left and had not gone" % jpid not in log(t), log(t)[-600:])

# --- a terminal window's command ------------------------------------------
host(t, "term")
do(t, "DT_OPENCMD=(sh -c 'echo opened-$((6*7)); sleep 30'); DT_OPENSELF=1; "
      "T := dt_new Terminal 8 40 20 70 term")
ok = False
deadline = time.time() + 8
while time.time() < deadline and not ok:
    t.collect(0.1)
    ok = "opened-42" in t.screen().text()
check("a hosted Terminal opened to run a command runs it", ok, t.screen().text() + log(t)[-500:])
check("and the next window inherits none of it",
      result(t, 'echo "${#DT_OPENCMD[@]}[$DT_OPENSELF][${DT[$T]["cmdn"]}]"') == "0[][3]",
      result(t, 'echo "${#DT_OPENCMD[@]}[$DT_OPENSELF][${DT[$T]["cmdn"]}]"'))

t.close()
for f in list(APPS.values()) + [FIFO, t.mark, scratch("hostedit-hosted.hibr")]:
    if os.path.exists(f):
        os.unlink(f)
for f in os.listdir(os.path.dirname(SAID)):
    if f.startswith(os.path.basename(SAID) + "."):
        os.unlink(os.path.join(os.path.dirname(SAID), f))
report(24)
