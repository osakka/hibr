#!/usr/bin/env python3
"""What an app in a process of its own costs, against the same app in the
desktop's (ADR 0050's prototype, wm/isolate.hibr). Prints one table.

  python3 tools/isomeasure.py

Two apps: `ec` shows the last key it was given; `tk` redraws a counter every
100 ms, the changing case. Each runs once in-process and once isolated, in a
desktop of 24x80 with nothing else in it.
"""
import os, re, statistics, sys, time
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(os.path.dirname(HERE), "tests"))
from screen import Term, load, tree, scratch, press, release

MOD = tree("build/mods/console.so")
WM = tree("programs/desktop/desktop.hibr")
TICK = os.sysconf("SC_CLK_TCK")

EC = ('declare -gA EC\n'
      'fn ec_draw(int id, int h, int w, int row, int col) {\n'
      '    console put -p "w$id" 1 1 "key: ${EC[$id]["k"]:-none}   "\n'
      '}\n'
      'fn ec_key(int id, key) { EC[$id]["k"]=$key; return 0; }\n'
      'dt_app ec "Echo" 6 30\n')
TK = ('declare -gA TK\n'
      'fn tk_draw(int id, int h, int w, int row, int col) {\n'
      '    TK[$id]["n"]=$(( ${TK[$id]["n"]:-0} + 1 ))\n'
      '    console put -p "w$id" 1 1 "tick ${TK[$id]["n"]}   "\n'
      '    dt_want 100\n'
      '}\n'
      'dt_app tk "Ticker" 6 30\n')


def cpu(pid):
    try:
        f = open("/proc/%d/stat" % pid).read().rsplit(")", 1)[1].split()
        return (int(f[11]) + int(f[12])) / TICK
    except OSError:
        return 0.0


def children(pid):
    out = []
    try:
        for c in open("/proc/%d/task/%d/children" % (pid, pid)).read().split():
            out.append(int(c))
            out += children(int(c))
    except OSError:
        pass
    return out


def hostpid(root):
    """The hosted desktop: the one hibr below the session's own. Found by
    its executable, since a terminal's program is renamed (its argv is
    overwritten with a title) the moment it starts."""
    me = os.path.realpath("/proc/%d/exe" % root)
    for p in children(root):
        try:
            if os.path.realpath("/proc/%d/exe" % p) == me:
                return p
        except OSError:
            pass
    return None


def private_kb(pid):
    try:
        m = re.search(r"^Private_Dirty:\s+(\d+)", open("/proc/%d/smaps_rollup" % pid).read(), re.M)
        c = re.search(r"^Private_Clean:\s+(\d+)", open("/proc/%d/smaps_rollup" % pid).read(), re.M)
        return int(m.group(1)) + int(c.group(1))
    except (OSError, AttributeError):
        return 0


def session(app, prefix, iso, extra=""):
    f = scratch("isom-%s.hibr" % prefix)
    open(f, "w").write(app)
    if iso:
        opener = "dt_isonew %s %s %s 6 30 2 2 > /dev/null\n" % (prefix, f, prefix)
    else:
        opener = ". %s\ndt_new %s 6 30 2 2 %s\n" % (f, prefix, prefix)
    p = scratch("isom-s-%s-%d.hibr" % (prefix, iso))
    open(p, "w").write("%s. %s\ndt_open\n%s%sdt_run\ndt_close\n" % (load(MOD), WM, opener, extra))
    return p, f


def run_echo(iso):
    p, f = session(EC, "ec", iso)
    t0 = time.time()
    t = Term(p, rows=24, cols=80, settle=0.3)
    sc = t.until("key: none", tries=200, step=0.01)
    opened = time.time() - t0
    at = sc.find("key: none")
    t.send(press(at[0], at[1]) + release(at[0], at[1]), settle=0.3, collect=0.2)
    lat = []
    for ch in "abcdefghijklmnopqrst":
        t.collect(0.05)
        s = time.time()
        t.send(ch.encode(), settle=0, collect=0)
        t.until("key: " + ch, tries=400, step=0.005)
        lat.append((time.time() - s) * 1000)
    root = t.pid
    hp = hostpid(root) if iso else None
    time.sleep(1.0)
    c0p, c0h = cpu(root), cpu(hp) if hp else 0.0
    time.sleep(5.0)
    idlep = (cpu(root) - c0p) / 5 * 100
    idleh = ((cpu(hp) - c0h) / 5 * 100) if hp else 0.0
    mem = private_kb(hp) if hp else 0
    t.quit(b"q", 1.0)
    for x in (p, f):
        os.unlink(x)
    return opened, statistics.median(lat), max(lat), idlep, idleh, mem


def run_tick(iso):
    p, f = session(TK, "tk", iso)
    t = Term(p, rows=24, cols=80, settle=0.3)
    t.until("tick", tries=300, step=0.02)
    root = t.pid
    hp = hostpid(root) if iso else None
    time.sleep(1.0)
    c0p, c0h = cpu(root), cpu(hp) if hp else 0.0
    t.collect(5.0)
    busyp = (cpu(root) - c0p) / 5 * 100
    busyh = ((cpu(hp) - c0h) / 5 * 100) if hp else 0.0
    t.quit(b"q", 1.0)
    for x in (p, f):
        os.unlink(x)
    return busyp, busyh


rows = []
for iso in (0, 1):
    o, med, mx, ip, ih, mem = run_echo(iso)
    bp, bh = run_tick(iso)
    rows.append((("isolated" if iso else "in-process"), o, med, mx, ip, ih, mem, bp, bh))

print("%-11s %8s %9s %8s %10s %10s %9s %11s %11s" % (
    "", "open s", "key ms", "max ms", "idle cpu%", "host idle%", "host kB",
    "anim cpu%", "host anim%"))
for r in rows:
    print("%-11s %8.2f %9.1f %8.1f %10.2f %10.2f %9d %11.2f %11.2f" % r)
