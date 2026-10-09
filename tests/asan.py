#!/usr/bin/env python3
"""Every suite again, against the shell and its modules built with sanitizers.

    python3 tests/asan.py [suite...]     # default: all of them
    python3 tests/asan.py --quick [--since REF]

--quick is the gate a release that changes C waits for, a few minutes
rather than twenty: tests/run.sh and self.hibr, which are the whole core;
the parser fuzzer; every suite of a C module that is not a desktop; and the
suites tests/affected.py says a changed module reaches, counting changes
since REF (the last tag by default) and in the working tree. A change to
the shell alone pulls in no pty suite, since what those add is the desktop's
scripts, which reach C only through the core run.sh already covers. The
full run stays, after every release and nightly, and a report from it is a
ticket at once: a memory bug only a pty suite reaches, in C a release did not
touch, surfaces a day late rather than never.

`make asan` builds build/asan/hibr and build/asan/mods with AddressSanitizer
and UBSan. This runs tests/all.py with HIBR pointing at that shell and
HIBR_TESTMODS at those modules, so the pty suites exercise console, term,
pty, hold and img end to end under the sanitizers -- which nothing else
does -- and tests/run.sh runs the ASan shell as the CLAUDE.md recipe does.

Reports go to files, not to stderr, since a desktop's stderr is its log and
a forked child's may be closed: each lands in build/asan-logs/san.<pid>, and
any at all fails the run, with its first lines printed. Leak checking is
off here; it is a per-test job (see CLAUDE.md), and a pty session that is
killed rather than quit would report everything it held.

Address randomisation is turned off for the shell with `setarch -R`, through
a wrapper script, build/asan/bin/hibr -- never written over a path that may
be a symlink to the real shell. That directory goes first on PATH, so a
program that runs `hibr` by name -- the terminal window's shell -- gets the
sanitizer build too, rather than the installed one handed sanitizer modules
it cannot load.
"""
import glob
import os
import shutil
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
ADIR = os.path.join(ROOT, "build", "asan")
LOGS = os.path.join(ROOT, "build", "asan-logs")
BIN = os.path.join(ADIR, "bin")
WRAP = os.path.join(BIN, "hibr")
QUICK = ["run.sh", "cat", "console", "term_diff", "md_spec", "html_tree",
         "mail", "pim", "pim_rrule", "dav", "uni_bidi", "uni_shape", "hcal_icu", "salat_adhan", "vw_crypto", "vw", "sixel", "kitgfx", "holdpix", "archive", "mermaid"]
FUZZ = 300
# Past this many suites a quick run is a full one in all but name; see quick().
QUICKMANY = 12


def wrapper():
    """Write the setarch wrapper, as build/asan/bin/hibr, first on PATH."""
    os.makedirs(BIN, exist_ok=True)
    if os.path.lexists(WRAP):
        os.unlink(WRAP)
    open(WRAP, "w").write('#!/bin/sh\nexec %s %s -R %s "$@"\n'
                          % (shutil.which("setarch"), os.uname().machine,
                             os.path.join(ADIR, "hibr")))
    os.chmod(WRAP, 0o755)


DISPLAY = ("console", "term", "pty", "hold")


def quick(since):
    """The quick gate's suites: QUICK and whatever a changed module reaches."""
    sys.path.insert(0, HERE)
    import affected
    from all import SUITES
    if not since:
        since = subprocess.run(["git", "describe", "--tags", "--abbrev=0"],
                               cwd=ROOT, capture_output=True, text=True,
                               check=True).stdout.strip()
    req, prov = affected.graph()
    where = affected.reach(req, prov)
    pick = set(QUICK)
    for p in affected.changed(since):
        if p.startswith("mods/"):
            got = affected.suites_for(os.path.normpath(p), req, prov, where)[0]
            # The desktop's pty suites run under the sanitizer only for a
            # module they drive the screen through: a calendar or a prayer
            # module loaded by the desktop is checked by its own suites,
            # and the nightly full run covers the rest -- 14 minutes saved.
            if p.split("/")[1].split(".")[0] not in DISPLAY:
                got = set(got) - set(affected.DESKTOP)
            pick |= got
    print("asan --quick since %s: %s" % (since, " ".join(
        s for s in SUITES if s in pick)), flush=True)
    got = [s for s in SUITES if s in pick]
    # A quick set is only quick while it is small, and it is large exactly
    # when the change is riskiest: a console, term, pty or hold change pulls
    # in every suite the desktop draws through. Run beside all.py that is
    # about eighty pty sessions at once, which is what made four of Write's
    # file-dialog checks fail two gates in a row while each passed alone
    # (Gitea #163). The operator is the only one who knows whether all.py
    # is running, so say it rather than decide it.
    if len(got) > QUICKMANY:
        print("asan: %d suites, most of them driving a pty -- run this "
              "alone, not beside all.py" % len(got), flush=True)
    return got


def extra(env):
    """self.hibr and the parser fuzzer, under the sanitizer shell."""
    cmds = [[WRAP, os.path.join(HERE, "self.hibr")],
            [sys.executable, os.path.join(HERE, "fuzz.py"), WRAP, str(FUZZ)]]
    with ThreadPoolExecutor(max_workers=2) as pool:
        res = list(pool.map(lambda c: subprocess.run(
            c, cwd=ROOT, env=dict(env, SEED="7"), capture_output=True,
            text=True), cmds))
    ok = True
    for name, r in zip(("self.hibr", "fuzz"), res):
        last = (r.stdout.strip() or r.stderr.strip()).splitlines()
        print("%-10s %-4s %s" % (name, "ok" if r.returncode == 0 else "FAIL",
                                 last[-1] if last else "no output"))
        ok = ok and r.returncode == 0
    return ok


def main():
    args = sys.argv[1:]
    fast = "--quick" in args
    since = None
    if "--since" in args:
        i = args.index("--since")
        since = args[i + 1]
        del args[i:i + 2]
    args = [a for a in args if a != "--quick"]
    subprocess.run(["make", "-s", "-j%d" % (os.cpu_count() or 1), "asan"],
                   cwd=ROOT, check=True)
    wrapper()
    shutil.rmtree(LOGS, ignore_errors=True)
    os.makedirs(LOGS)
    san = os.path.join(LOGS, "san")
    env = dict(os.environ, HIBR=WRAP,
               PATH=BIN + os.pathsep + os.environ.get("PATH", ""),
               HIBR_TESTMODS=os.path.join(ADIR, "mods"),
               HIBR_TESTLOGS=LOGS,
               ASAN_OPTIONS="detect_leaks=0:log_path=" + san,
               UBSAN_OPTIONS="print_stacktrace=1:log_path=" + san)
    if fast:
        with ThreadPoolExecutor(max_workers=2) as pool:
            more = pool.submit(extra, env)
            st = subprocess.run([sys.executable, os.path.join(HERE, "all.py")]
                                + (args or quick(since)), cwd=ROOT,
                                env=env).returncode
            st = st or (0 if more.result() else 1)
    else:
        st = subprocess.run([sys.executable, os.path.join(HERE, "all.py")]
                            + args, cwd=ROOT, env=env).returncode
    reports = sorted(glob.glob(san + ".*"))
    for r in reports:
        print("\n== %s" % os.path.relpath(r, ROOT))
        print("".join(open(r, errors="replace").readlines()[:30]).rstrip())
    print("\nasan: %d sanitizer report%s" % (len(reports),
                                              "" if len(reports) == 1 else "s"))
    sys.exit(1 if st or reports else 0)


if __name__ == "__main__":
    main()
