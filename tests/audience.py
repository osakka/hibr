#!/usr/bin/env python3
"""Who is sent what, against running apps (ADR 0050): an app in a process of
its own reads the desktop's shared state from what the desktop sent it, and
mirrors.hibr's DT_RTAUD -- derived by the parser, never chosen -- says who is
sent each name. A name an app reads and is not sent is a stale read in that
app's process, silently wrong. This holds the derivation to the apps
themselves.

Each app runs in its own process through the runtime (runtime.hibr, the path
DT_RTAUD governs: dt_rtwants is its one reader), launched for the app's own
file so it is sent what that file is sent, and driven the same way in three
desktops at once: one with DT_RTAUD as generated, one with every name sent
to every process, and one with a single name taken from the app's entry. A
runtime process puts no cells on the desktop's screen yet (ADR 0050: the
carriage of cells is still to be measured), so it draws its window into a
console of its own on a terminal of its own, after every step, and the
screens are compared cell by cell, glyphs and pens. The generated audience
must draw exactly what every name draws; the broken one must not, or the
comparison could never have failed.

    python3 tests/audience.py
"""
import os, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import screen
from screen import Term, check, report, load, tree, scratch

MOD = tree("build/mods/console.so")
WM = tree("programs/desktop/desktop.hibr")
HERE = tree("programs/desktop")
LAUNCH = scratch("audience-app.hibr")

# The app's own process: the runtime, the hello -- so what the desktop
# shares is here before the app's own top level reads any of it -- then the
# app's own file. Its standard error is a log of its own, so a stale read's error is
# kept for the report rather than drawn. aud_draw is the one thing it is told
# that is not the app's: the window as the app draws it, into the pane a
# window would be, and the step it drew for below it. What it was sent is
# counted to a file, never drawn: it differs by design.
open(LAUNCH, "w").write(r'''DT_HERE=%s
. "$DT_HERE/runtime.hibr"
exec 2>> "$AUD_LOG"
dt_rtopen "$AUD_PREFIX" || { echo "refused: $DT_RTSAID" >&2; exit 1; }
. "$AUD_APP"
console open || exit 1
fn aud_draw(int id, n) {
	local h=${DT[$id]["h"]} w=${DT[$id]["w"]}

	console clear
	console pane "w$id" 1 1 "$h" "$w"
	console pen "$DT_INK" "$DT_FACE"
	console fill -p "w$id" 0 0 "$h" "$w" " "
	"${AUD_PREFIX}_draw" "$id" $((h - 2)) $((w - 2)) 1 1
	console pen default default
	console put $((h + 2)) 0 "step $n"
	console flush
	printf '%%s\n' "$DT_RTRECV" > "$AUD_LOG.recv"
}
dt_rtrun
''' % HERE)

# The apps driven, each with what a person does to it: the desktop's side
# of a step (a resize, a colour scheme), then what the app is told -- its
# own functions, as the desktop calls a window's. A step's draw comes after.
# Each names the shared name the broken desktop takes from its entry, and
# why the app needs it.
APPS = [
    {"prefix": "calc", "file": "apps/Accessories/calc.hibr", "title": "Calculator",
     "h": 16, "w": 24, "shows": "integers only",
     # calc_draw paints its result in DT_GOOD, an error in DT_BAD and the c
     # key on DT_BAD on every frame: a calculator not sent it draws them
     # with no colour at all.
     "break": "DT_BAD",
     "steps": [
         ("opened", "", [["calc_open", "$AUDID", ""]]),
         ("keys", "", [["calc_key", "$AUDID", k] for k in "7*6="]),
         ("an error", "", [["calc_key", "$AUDID", k] for k in "1/0="]),
         ("a click", "", [["calc_click", "$AUDID", "4", "3", "left"]]),
         ("a resize", 'dt_resize "$AUDID" 18 30', []),
         ("a colour scheme", "cp_colours paper", []),
         ("a theme", "cp_theme chalk", []),
     ]},
    # Never a cell opened, so its clock never starts: two processes drawing a
    # second apart would otherwise differ by what the clock said.
    {"prefix": "mines", "file": "apps/Games/mines.hibr", "title": "Mines",
     "h": 15, "w": 31, "shows": "space opens, f flags",
     # The flags left are drawn in DT_BAD, and a flag on the board too.
     "break": "DT_BAD",
     "steps": [
         ("opened", "", [["mines_open", "$AUDID", ""]]),
         ("keys", "", [["mines_key", "$AUDID", k] for k in ("right", "right", "up", "f")]),
         ("a click", "", [["mines_click", "$AUDID", "4", "5", "right"]]),
         ("a resize", 'dt_resize "$AUDID" 17 35', []),
         ("a colour scheme", "cp_colours paper", []),
         ("a theme", "cp_theme chalk", []),
     ]},
    # Read through shared code rather than by the app's own file: the
    # diagram's lines are drawn in dia_pens' DT_INFO, and dia_pens is
    # lib/diagram.hibr's -- the transitive rule, which the two above never
    # reach.
    {"prefix": "diagram", "file": "apps/Office/diagram.hibr", "title": "Diagram",
     "h": 24, "w": 86, "shows": "Write Mermaid here",
     "break": "DT_INFO",
     "steps": [
         ("opened", "", [["diagram_open", "$AUDID", ""]]),
         ("keys", "", [["diagram_key", "$AUDID", k] for k in ("down", "down", "end")]),
         ("a click", "", [["diagram_mouse", "$AUDID", "press", "left", "2", "6"],
                          ["diagram_mouse", "$AUDID", "release", "left", "2", "6"]]),
         ("a resize", 'dt_resize "$AUDID" 26 92', []),
         ("a colour scheme", "cp_colours paper", []),
         ("a theme", "cp_theme chalk", []),
     ]},
]

MODES = ("generated", "every", "broken")


def q(w):
    """A word for the desktop's command line: a variable expanded, anything
    else as it is."""
    if w.startswith("$"):
        return '"%s"' % w
    return "'%s'" % w.replace("'", "'\\''")


class Desk:
    """One desktop, listening for app processes, with DT_RTAUD as its mode
    says, and a ticker that runs what a marker file holds."""

    def __init__(self, mode, app):
        self.mode = mode
        # Every name of its own is the app's as well as the mode's: the
        # last app's desktop of this mode may still be quitting, and one
        # that has not yet gone takes a command written to a marker file it
        # shares -- the next app's `AUDID := dt_new` with it, which left
        # every step of that app's window an empty id.
        nm = "%s-%s" % (app["prefix"], mode)
        self.mark = scratch("audience-%s-do" % nm)
        self.out = scratch("audience-%s-out" % nm)
        over = ""
        if mode == "every":
            over = 'for n in "${!DT_RTAUD[@]}"; do DT_RTAUD["$n"]="*"; done\n'
        elif mode == "broken":
            over = ('aud_w=(); read -ra aud_w <<< "${DT_RTAUD["%s"]}"; aud_k=\n'
                    'for aud_x in "${aud_w[@]}"; do [ "$aud_x" = %s ] || aud_k+="${aud_k:+ }$aud_x"; done\n'
                    'DT_RTAUD["%s"]=$aud_k\n' % (app["break"], app["file"], app["break"]))
        p = scratch("audience-%s.hibr" % nm)
        open(p, "w").write(r'''%s. %s
. "$DT_HERE/system/panel.hibr"
. "$DT_HERE/control-panel/appearance.hibr"
%sdt_open
DT_NOTEMS=60000
DT_LOADING=%s dt_app %s %s %d %d
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
''' % (load(MOD), WM, over, os.path.join(HERE, app["file"]), app["prefix"], q(app["title"]),
            app["h"], app["w"], self.mark, self.mark, self.mark))
        self.session = p
        # A folder of its own for its sockets: three desktops of one session
        # name would otherwise listen at one path.
        tmp = os.path.join(screen.HOME, nm)
        os.makedirs(tmp, exist_ok=True)
        self.t = Term(p, rows=30, cols=100, settle=0.6, env={"DT_BARTIME": "12:00", "TMPDIR": tmp})
        self.app = None
        self.missed = []
        self.log = scratch("audience-%s-app.log" % nm)

    def do(self, cmd):
        """Run cmd in the desktop, from its ticker; waits until it is taken.
        A desktop still starting under a loaded gate takes its first a while
        later, and the next command written over one not yet taken would
        lose it -- `AUDID := dt_new` lost made every step of the app's
        window an empty id -- so one not taken in time is named."""
        screen.putcmd(self.mark, cmd + "\n")
        deadline = time.time() + 30
        while os.path.exists(self.mark) and time.time() < deadline:
            self.t.send(b"\x1b[I", settle=0.1, collect=0.1)
        if os.path.exists(self.mark):
            self.missed.append("%s desktop never took: %s" % (self.mode, cmd))

    def ask(self, expr, secs=6):
        """What a shell expression prints in the desktop."""
        got = None
        deadline = time.time() + secs
        while time.time() < deadline:
            if os.path.exists(self.out):
                os.unlink(self.out)
            self.do("{ %s; } > %s 2>&1" % (expr, self.out))
            time.sleep(0.1)
            try:
                got = open(self.out).read().strip()
            except OSError:
                got = None
            if got:
                break
        return got

    def launch(self, app):
        """The app's window, and its process on a terminal of its own, with
        a launch issued for the app's own file: what it is sent is what
        that file is sent."""
        self.do('AUDID := dt_new %s %d %d 3 3 %s' % (q(app["title"]), app["h"], app["w"], app["prefix"]))
        tok = self.ask('x := dt_rtissue %s %s; echo "$x"' % (app["prefix"], q(os.path.join(HERE, app["file"]))))
        self.do("dt_rtlisten")
        path = self.ask('echo "$DT_RTPATH"')
        self.do("AUDL=%s" % tok)
        for f in (self.log, self.log + ".recv"):
            if os.path.exists(f):
                os.unlink(f)
        self.app = Term(LAUNCH, rows=32, cols=100, settle=0,
                        env={"DT_HERE": HERE, "DT_RTSOCK": path, "DT_RTTOKEN": tok,
                             "AUD_APP": os.path.join(HERE, app["file"]),
                             "AUD_PREFIX": app["prefix"], "AUD_LOG": self.log})
        self.tok = tok
        return self.ask('c=${DT_RTTOKC["$AUDL"]-}; [ -n "$c" ] && echo "${DT_RTCFILE["$c"]-none}"')

    def step(self, n, deskcmd, tells):
        cmds = [deskcmd] if deskcmd else []
        for tl in tells:
            cmds.append('dt_rttell "$AUDL" ' + " ".join(q(w) for w in tl))
        cmds.append('dt_rttell "$AUDL" aud_draw "$AUDID" %d' % n)
        t0 = time.time()
        self.do("; ".join(cmds))
        sc = self.app.until("step %d" % n, tries=100, step=0.1)
        if sc.find("step %d" % n) is None:
            # What a gate needs to name a step that never drew: how long it
            # was waited for, whether the desktop took it, what the app said.
            try:
                said = open(self.log, errors="replace").read()[-1500:]
            except OSError:
                said = ""
            self.missed.append("%s desktop, step %d not drawn after %.1f s (taken: %s):\n%s"
                               % (self.mode, n, time.time() - t0, not os.path.exists(self.mark), said))
        return sc

    def recv(self):
        try:
            return int(open(self.log + ".recv").read().strip())
        except (OSError, ValueError):
            return None

    def errors(self, every=False):
        try:
            return [l for l in open(self.log, errors="replace").read().split("\n") if every or "hibr:" in l]
        except OSError:
            return []

    def close(self):
        if self.app:
            self.app.close()
        self.t.quit(b"q", 1.0)
        for f in (self.session, self.mark, self.out, self.log, self.log + ".recv"):
            if os.path.exists(f):
                os.unlink(f)


def cells(a, b):
    """Every cell where two screens differ, glyph or pen."""
    out = []
    for r in range(min(a.rows, b.rows)):
        for c in range(min(a.cols, b.cols)):
            if a.g[r][c] != b.g[r][c] or a.p[r][c] != b.p[r][c]:
                out.append((r, c, a.g[r][c], b.g[r][c], a.p[r][c], b.p[r][c]))
    return out


def said(diff, n=6):
    return "; ".join("%d,%d %r/%r pen %s/%s" % d for d in diff[:n]) + (
        " (and %d more)" % (len(diff) - n) if len(diff) > n else "")


for app in APPS:
    desks = {m: Desk(m, app) for m in MODES}
    files = {m: d.launch(app) for m, d in desks.items()}
    check("%s: each process is sent what %s is sent, not every name as a file "
          "the inventory never read would be" % (app["prefix"], app["file"]),
          all(f == app["file"] for f in files.values()),
          (files, {m: (d.errors(True), d.app.text[-400:]) for m, d in desks.items()}))
    broke, idle, prev = [], [], None
    for i, (name, deskcmd, tells) in enumerate(app["steps"], 1):
        sc = {m: d.step(i, deskcmd, tells) for m, d in desks.items()}
        drew = all(s.find("step %d" % i) is not None for s in sc.values())
        if i == 1:
            check("%s: the app's own process draws its window, in every desktop "
                  "(else every comparison is of two empty screens)" % app["prefix"],
                  drew and all(s.find(app["shows"]) is not None for s in sc.values()),
                  sc["generated"])
        d = cells(sc["generated"], sc["every"])
        check("%s, %s: the generated audience draws what every name draws" % (app["prefix"], name),
              drew and not d, "\n".join(m for x in desks.values() for m in x.missed) + "\n" +
              said(d) + "\n" + sc["generated"].dump() + "\n" + sc["every"].dump())
        if os.environ.get("DUMP"):
            print(name, sc["generated"].dump())
        if prev is not None and not [x for x in cells(prev, sc["generated"])
                                     if not prev.row(x[0]).startswith("step ") and
                                     not sc["generated"].row(x[0]).startswith("step ")]:
            idle.append(name)
        prev = sc["generated"]
        b = cells(sc["broken"], sc["every"])
        if b:
            broke.append((name, b))
    check("%s: every step changed what the app draws (else its comparison "
          "was of the screen before)" % app["prefix"], not idle, idle)
    rg, re_ = desks["generated"].recv(), desks["every"].recv()
    check("%s: the generated audience sends fewer names than every name "
          "(else the two runs are one)" % app["prefix"],
          rg is not None and re_ is not None and rg < re_, (rg, re_))
    check("%s: the oracle fails an audience without %s for it, at the first step"
          % (app["prefix"], app["break"]),
          broke and broke[0][0] == app["steps"][0][0],
          broke and "%s: %s" % (broke[0][0], said(broke[0][1])))
    # A stale read that errors and draws nothing different is still a
    # stale read: the generated process may say nothing every name's did not.
    errs = {m: [l[l.index("hibr:"):] for l in desks[m].errors()] for m in MODES}
    extra = [l for l in errs["generated"] if l not in errs["every"]]
    check("%s: the generated audience's process logged no error every name's did not"
          % app["prefix"], not extra, extra)
    for d in desks.values():
        d.close()
for f in (LAUNCH,):
    if os.path.exists(f):
        os.unlink(f)
report(sum(6 + len(a["steps"]) for a in APPS) + 1)
