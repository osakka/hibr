#!/usr/bin/env python3
"""The budgets an app in a process of its own must keep (ADR 0050, Decision
8), measured the same way in any tree, so the candidate is read against
0.99.549 -- the last release with no isolation code on main.

  python3 tools/budgets.py [--tree DIR] [--hosted] [--json FILE] [CASE...]
  python3 tools/budgets.py --compare A B [--runs N] [--hosted-b] [CASE...]

The cases, each a desktop of its own on a pty, 40 rows by 120 columns, the
apps written here so every tree runs the same code:

  idle     two windows of an app that draws only when told, nothing
           happening: processor time over ten seconds, percent of a core
  echo     a key's echo: the app shows the last key it was given; a key is
           written to the terminal and the screen read until the window
           shows it -- from outside, so a tree with no latency instruments
           of its own is measured the same as one with them (median, worst)
  redraw   a full window redrawn ten times a second: every cell of a 22x76
           window changes each draw, asked for with dt_want 100
  writers  the Terminal's three writers: three terminal windows, each a
           program printing a line every 10 ms (perl, so no fork per line)

Processor time is read from /proc for the desktop, the app processes (hibr
processes below it, hosted) and the programs below them, each reported
apart; the load average is said beside every reading, since the box is
shared. --hosted (on a tree that has the spine, dt_rthost) starts each app
the case uses in a process of its own first -- a test session only, as the
spine allows; a case whose app cannot be hosted there says so rather than
running in-process under the hosted name.

--compare runs each case alternately in tree A and tree B, N times, and
prints the medians with the spread. Every run is a pty session: run it
under the suites' lock.
"""
import json, os, re, statistics, subprocess, sys, tempfile, time

HERE = os.path.dirname(os.path.abspath(__file__))
TICK = os.sysconf("SC_CLK_TCK")
ROWS, COLS = 40, 120
CASES = ("idle", "echo", "redraw", "writers")

EC = r'''declare -gA EC
fn ec_draw(int id, int h, int w, int row, int col) {
	console put -p "w$id" 1 1 "key: ${EC[$id]["k"]:-none}   "
}
fn ec_key(int id, key) { EC[$id]["k"]=$key; return 0; }
command -v dt_app > /dev/null && dt_app ec "Echo" 6 30
'''
FW = r'''declare -gA FW
fn fw_draw(int id, int h, int w, int row, int col) {
	local n=${FW[$id]-0} r=1 line c
	FW[$id]=$(( (n + 1) % 10 ))
	printf -v line '%*s' "$w" ''
	line=${line// /$n}
	while [ "$r" -le "$h" ]; do
		console put -p "w$id" "$r" 1 "$line"
		r=$((r + 1))
	done
	dt_want 100
}
command -v dt_app > /dev/null && dt_app fw "Fill" 24 80
'''
WRITER = "perl -e '$|=1; for ($i=0;;$i++) { print \"line $i\\n\"; select(undef,undef,undef,0.01) }'"


def kids(pid):
    out = []
    try:
        for c in open("/proc/%d/task/%d/children" % (pid, pid)).read().split():
            out.append(int(c))
            out += kids(int(c))
    except OSError:
        pass
    return out


def ticks(pid):
    try:
        f = open("/proc/%d/stat" % pid).read().rsplit(")", 1)[1].split()
        return int(f[11]) + int(f[12])
    except OSError:
        return 0


def exe(pid):
    try:
        return os.path.realpath("/proc/%d/exe" % pid)
    except OSError:
        return ""


def cpu(root, secs, t):
    """Percent of a core over secs: the desktop, its hibr processes (app
    processes when hosted) and every other program below it -- read while
    the terminal is read, since a desktop whose frames nobody reads blocks
    writing them and would measure as idle."""
    me = exe(root)

    def sample():
        d = ticks(root)
        a = p = 0
        for k in kids(root):
            if exe(k) == me:
                a += ticks(k)
            else:
                p += ticks(k)
        return d, a, p
    s0 = sample()
    t.collect(secs)
    s1 = sample()
    return [round(100.0 * (b - a) / TICK / secs, 2) for a, b in zip(s0, s1)]


def hosted_ok(tree):
    return "dt_rthost" in open(os.path.join(tree, "programs/desktop/wm/rtview.hibr")).read() \
        if os.path.exists(os.path.join(tree, "programs/desktop/wm/rtview.hibr")) else False


def session(tree, scratch, case, hosted):
    """The session file for one case: the apps sourced (in-process) or only
    registered and hosted (a test session's dt_rthost), the windows opened
    from a ticker once the apps are ready, and the desktop's pid written."""
    apps = {"idle": ["ec"], "echo": ["ec"], "redraw": ["fw"], "writers": ["term"]}[case]
    files = {"ec": os.path.join(scratch, "ec.hibr"), "fw": os.path.join(scratch, "fw.hibr"),
             "term": os.path.join(tree, "programs/desktop/apps/term.hibr")}
    open(files["ec"], "w").write(EC)
    open(files["fw"], "w").write(FW)
    reg = {"ec": 'dt_app ec "Echo" 6 30', "fw": 'dt_app fw "Fill" 24 80',
           "term": 'dt_app term "Terminal" 14 52 many ">_"'}
    pre = []
    for a in apps:
        if hosted:
            pre.append("DT_LOADING=%s %s" % (files[a], reg[a]))
        else:
            pre.append("DT_LOADING=%s . %s" % (files[a], files[a]))
    pre.append("DT_LOADING=")
    opens = {"idle": ['dt_new Echo 6 30 3 2 ec', 'dt_new Echo 6 30 3 40 ec'],
             "echo": ['BG_W := dt_new Echo 6 30 3 2 ec', 'dt_raise "$BG_W"'],
             "redraw": ['dt_new Fill 24 80 3 2 fw'],
             "writers": ["DT_OPENCMD=(%s); dt_new Terminal 12 50 %d %d term" % (WRITER, r, c)
                         for r, c in ((2, 2), (2, 60), (18, 2))]}[case]
    host = " ".join("dt_rthost %s > /dev/null;" % a for a in apps) if hosted else ""
    ready = " && ".join('[ -n "${DT_RTHOST[%s]-}" ]' % a for a in apps) if hosted else "true"
    pidf = os.path.join(scratch, "pid")
    body = r'''
BG_STEP=0
bg_tick() {
	case $BG_STEP in
	0) %s BG_STEP=1; dt_want 50 ;;
	1) if %s; then
		%s
		echo $$ > %s
		BG_STEP=2
	   else dt_want 50; fi ;;
	esac
	return 0
}
dt_ticker bg_tick
''' % (host, ready, "\n\t\t".join(opens), pidf)
    sys.path.insert(0, os.path.join(os.path.dirname(HERE), "tests"))
    from screen import load
    p = os.path.join(scratch, "s-%s.hibr" % case)
    open(p, "w").write("%s. %s\ndt_open\n%s\n%s\ndt_run\ndt_close\n" % (
        load(os.path.join(tree, "build/mods/console.so")),
        os.path.join(tree, "programs/desktop/desktop.hibr"), "\n".join(pre), body))
    return p, pidf


def run(tree, case, hosted=False):
    tree = os.path.abspath(tree)
    if hosted and not hosted_ok(tree):
        return {"case": case, "skipped": "this tree has no spine (dt_rthost)"}
    if hosted and case == "writers":
        # What a terminal window runs is handed to its _open in DT_OPENCMD,
        # the desktop's own variable (appstate.py: an argument to come); a
        # hosted Terminal's _open runs in its own process, which never sees
        # it, and would start its default program -- a different case under
        # the same name.
        return {"case": case, "skipped": "DT_OPENCMD does not reach a hosted Terminal's _open yet"}
    scratch = tempfile.mkdtemp(prefix="budgets-")
    # One harness for every tree: this tree's tests/screen.py drives the
    # measured tree's binary and modules, so two trees differ only in what
    # they run, never in how they are driven.
    os.environ["HIBR"] = os.path.join(tree, "build/hibr")
    os.environ["HIBR_TESTMODS"] = os.path.join(tree, "build/mods")
    sys.path.insert(0, os.path.join(os.path.dirname(HERE), "tests"))
    os.chdir(tree)
    import screen
    from screen import Term
    p, pidf = session(tree, scratch, case, hosted)
    if os.path.exists(pidf):
        os.unlink(pidf)
    t = Term(p, rows=ROWS, cols=COLS, settle=0.5, env={"DT_BARTIME": "12:00"})
    out = {"case": case, "tree": tree, "hosted": hosted, "load": open("/proc/loadavg").read().split()[0]}
    deadline = time.time() + 20
    while time.time() < deadline and not (os.path.exists(pidf) and open(pidf).read().strip()):
        t.collect(0.1)
    if not (os.path.exists(pidf) and open(pidf).read().strip()):
        out["error"] = "the windows never opened: " + (open(t.log, errors="replace").read()[-400:]
                                                       if os.path.exists(t.log) else "no log")
        t.quit(b"q", 1.0)
        return out
    root = int(open(pidf).read())
    if case == "echo":
        sc = t.until("key: none", tries=200, step=0.02)
        lat, miss = [], 0
        for ch in "abcdefghijklmnoprstuvwxyz":
            t.collect(0.08)
            s = time.time()
            os.write(t.fd, ch.encode())
            t.sent += 1
            seen = False
            while time.time() - s < 3:
                t.collect(0.001)
                if t.screen().find("key: " + ch):
                    seen = True
                    break
            if seen:
                lat.append((time.time() - s) * 1000)
            else:
                miss += 1
        out["echo_ms"] = [round(statistics.median(lat), 1), round(max(lat), 1)] if lat else None
        out["echo_missed"] = miss
    else:
        t.collect(3.0 if case != "idle" else 2.0)
        f0 = t.frames()
        d, a, pr = cpu(root, 10.0, t)
        out["cpu"] = {"desktop": d, "apps": a, "programs": pr}
        # The frames drawn while it was measured, from the harness's idle
        # marker: what says the load was the one named.
        out["frames_per_s"] = round(((t.frames() or 0) - (f0 or 0)) / 10.0, 1)
    me = exe(root)
    out["hibr_children"] = sum(1 for k in kids(root) if exe(k) == me)
    out["log_errors"] = sum(1 for l in (open(t.log, errors="replace").read().splitlines()
                                        if os.path.exists(t.log) else []) if "not carried" in l or "refused" in l)
    t.quit(b"q", 1.0)
    return out


def single(argv):
    tree = argv[argv.index("--tree") + 1] if "--tree" in argv else os.path.dirname(HERE)
    hosted = "--hosted" in argv
    cases = [c for c in argv if c in CASES] or list(CASES)
    res = [run(tree, c, hosted) for c in cases]
    if "--json" in argv:
        json.dump(res, open(argv[argv.index("--json") + 1], "w"))
    for r in res:
        print(json.dumps(r))


def compare(argv):
    i = argv.index("--compare")
    a, b = argv[i + 1], argv[i + 2]
    n = int(argv[argv.index("--runs") + 1]) if "--runs" in argv else 3
    cases = [c for c in argv if c in CASES] or list(CASES)
    hb = "--hosted-b" in argv
    got = {(t, c): [] for t in (a, b) for c in cases}
    for k in range(n):
        for c in cases:
            for t in ((a, b) if k % 2 == 0 else (b, a)):
                cmd = [sys.executable, os.path.abspath(__file__), "--tree", t, c]
                if t == b and hb:
                    cmd.append("--hosted")
                r = subprocess.run(cmd, capture_output=True, text=True, timeout=300)
                line = [l for l in r.stdout.splitlines() if l.startswith("{")]
                got[(t, c)].append(json.loads(line[-1]) if line else {"error": r.stderr[-300:]})
                print("run %d %s %s: %s" % (k + 1, os.path.basename(t), c, line[-1] if line else r.stderr[-200:]),
                      flush=True)
    print()
    print("| case | %s | %s%s |" % (os.path.basename(a), os.path.basename(b), " (hosted)" if hb else ""))
    print("|---|---|---|")
    for c in cases:
        cells = []
        for t in (a, b):
            rs = [r for r in got[(t, c)] if "error" not in r and "skipped" not in r]
            if not rs:
                cells.append("--")
            elif c == "echo":
                ok = [r for r in rs if r.get("echo_ms")]
                med = [r["echo_ms"][0] for r in ok]
                worst = [r["echo_ms"][1] for r in ok]
                missed = sum(r.get("echo_missed", 0) for r in rs)
                cells.append(("%.1f ms median (%.1f-%.1f), worst %.1f" % (
                    statistics.median(med), min(med), max(med), max(worst)) if ok else "no echo")
                             + ("; %d keys never echoed" % missed if missed else ""))
            else:
                parts = []
                for who in ("desktop", "apps", "programs"):
                    v = [r["cpu"][who] for r in rs]
                    if who != "desktop" and max(v) == 0:
                        continue
                    parts.append("%s %.2f%% (%.2f-%.2f)" % (who, statistics.median(v), min(v), max(v)))
                fr = [r.get("frames_per_s") for r in rs if r.get("frames_per_s") is not None]
                if fr:
                    parts.append("%.1f frames/s" % statistics.median(fr))
                cells.append(", ".join(parts))
        print("| %s | %s | %s |" % (c, cells[0], cells[1]))
    loads = [r.get("load") for v in got.values() for r in v if r.get("load")]
    print("\nload average during the runs: %s to %s; %d runs of each" % (min(loads), max(loads), n))


if __name__ == "__main__":
    if "--compare" in sys.argv:
        compare(sys.argv[1:])
    else:
        single(sys.argv[1:])
