#!/usr/bin/env python3
"""Every suite again, against the shell and its modules built with sanitizers.

    python3 tests/asan.py [suite...]     # default: all of them

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

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
ADIR = os.path.join(ROOT, "build", "asan")
LOGS = os.path.join(ROOT, "build", "asan-logs")
BIN = os.path.join(ADIR, "bin")
WRAP = os.path.join(BIN, "hibr")


def wrapper():
    """Write the setarch wrapper, as build/asan/bin/hibr, first on PATH."""
    os.makedirs(BIN, exist_ok=True)
    if os.path.lexists(WRAP):
        os.unlink(WRAP)
    open(WRAP, "w").write('#!/bin/sh\nexec setarch "$(uname -m)" -R %s "$@"\n'
                          % os.path.join(ADIR, "hibr"))
    os.chmod(WRAP, 0o755)


def main():
    subprocess.run(["make", "-s", "asan"], cwd=ROOT, check=True)
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
    st = subprocess.run([sys.executable, os.path.join(HERE, "all.py")]
                        + sys.argv[1:], cwd=ROOT, env=env).returncode
    reports = sorted(glob.glob(san + ".*"))
    for r in reports:
        print("\n== %s" % os.path.relpath(r, ROOT))
        print("".join(open(r, errors="replace").readlines()[:30]).rstrip())
    print("\nasan: %d sanitizer report%s" % (len(reports),
                                              "" if len(reports) == 1 else "s"))
    sys.exit(1 if st or reports else 0)


if __name__ == "__main__":
    main()
