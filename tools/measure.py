#!/usr/bin/env python3
"""The README's Measurements table, measured: hibr against dash and bash.

    tools/measure.py [path-to-hibr]        # default build/hibr

Prints the table as Markdown, with the version, the host's CPU and the load
average it ran under, so a reading carries what it needs to be judged.
Every row is taken the same way each time, which is the point of having a
script: the table was re-measured by hand three times, a different subset
of rows each time, and by 0.99.416 its times were 350 releases old.

  binary            the stripped size of a copy (the file itself is left alone)
  resident memory   the shell's own VmHWM, read from /proc/self/status with
                    the shell's own `read`, so no fork is counted; the median
                    of 25 runs, since one reading spreads by about 180 kB
  times             the best of 11, each process pinned to one core
  instructions      the loop under callgrind, the honest throughput number:
                    wall time between two builds moves several percent on
                    code layout alone (CLAUDE.md, Open items)
"""
import os, re, shutil, statistics, subprocess, sys, tempfile, time

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HIBR = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "build/hibr"))
SHELLS = [("hibr", HIBR), ("dash", shutil.which("dash")), ("bash", shutil.which("bash"))]
CORE = ["taskset", "-c", "2"] if shutil.which("taskset") else []

LOOP = "i=0; while [ $i -lt 50000 ]; do i=$((i+1)); done"
HWM = ("while read -r k v u; do case $k in VmHWM:) echo $v;; esac; done "
       "< /proc/self/status")
BIND = "f() { ret x; }; i=0; while [ $i -lt 5000 ]; do v := f; i=$((i+1)); done"
SUBST = "f() { echo x; }; i=0; while [ $i -lt 5000 ]; do v=$(f); i=$((i+1)); done"


def run(sh, code):
    return subprocess.run(CORE + [sh, "-c", code], capture_output=True, text=True)


def best(sh, code, n=11):
    t = []
    for _ in range(n):
        a = time.perf_counter()
        run(sh, code)
        t.append(time.perf_counter() - a)
    return min(t) * 1000


def startup(sh):
    # One run of `-c true` is under two milliseconds, below what one timing
    # resolves well, so each of the eleven is the mean of twenty.
    t = []
    for _ in range(11):
        a = time.perf_counter()
        for _ in range(20):
            run(sh, "true")
        t.append((time.perf_counter() - a) / 20)
    return min(t) * 1000


def hwm(sh, pre=""):
    v = []
    for _ in range(25):
        out = run(sh, pre + HWM).stdout.split()
        if out:
            v.append(int(out[-1]))
    return int(statistics.median(v)) if v else None


def stripped(path):
    with tempfile.TemporaryDirectory() as d:
        c = os.path.join(d, "s")
        shutil.copy(path, c)
        subprocess.run(["strip", c], check=True)
        return os.path.getsize(c) // 1024


def instructions(sh, code):
    if not shutil.which("valgrind"):
        return None
    with tempfile.TemporaryDirectory() as d:
        r = subprocess.run(["valgrind", "--tool=callgrind",
                            "--callgrind-out-file=" + os.path.join(d, "cg"),
                            sh, "-c", code], capture_output=True, text=True)
        m = re.search(r"Collected : (\d+)", r.stderr)
        return int(m.group(1)) if m else None


def main():
    ver = subprocess.run([HIBR, "-c", 'echo "$HIBR_VERSION"'], capture_output=True,
                         text=True).stdout.strip() or "?"
    cpu = "?"
    try:
        cpu = re.search(r"model name\s*:\s*(.*)", open("/proc/cpuinfo").read()).group(1)
    except (OSError, AttributeError):
        pass
    load = open("/proc/loadavg").read().split()[0]
    rows = {}
    for nm, sh in SHELLS:
        if not sh:
            continue
        r = rows[nm] = {}
        r["size"] = "%d KB" % stripped(os.path.realpath(sh))
        r["hwm0"] = "%d kB" % hwm(sh)
        r["hwm1"] = "%d kB" % hwm(sh, LOOP + "; ")
        r["loop"] = "%.0f ms" % best(sh, LOOP)
        ins = instructions(sh, LOOP)
        r["ins"] = "%.1fM" % (ins / 1e6) if ins else "--"
        r["bind"] = "**%.0f ms**" % best(sh, BIND) if nm == "hibr" else "--"
        r["subst"] = "%.0f ms" % best(sh, SUBST)
        r["start"] = "%.2f ms" % startup(sh)
    names = [n for n, s in SHELLS if s]
    print("hibr %s; %s; load average %s when it started; %s" %
          (ver, cpu, load, time.strftime("%Y-%m-%d")))
    print()
    print("| | " + " | ".join(names) + " |")
    print("|---|" + "---|" * len(names))
    for key, label in (("size", "binary, stripped"),
                       ("hwm0", "resident memory at startup"),
                       ("hwm1", "resident memory after the loop"),
                       ("loop", "the loop"),
                       ("ins", "the loop, instructions"),
                       ("bind", "5000 function calls, results via `:=`"),
                       ("subst", "the same through `$( )`"),
                       ("start", "startup, `-c true`")):
        print("| %s | %s |" % (label, " | ".join(rows[n][key] for n in names)))


main()
