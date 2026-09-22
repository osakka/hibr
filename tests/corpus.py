#!/usr/bin/env python3
"""Run real scripts under hibr and bash and compare what they do.

The parse-level sweep only asks whether a script reads. This asks whether it
behaves, which is where the remaining divergences are.

    python3 tests/corpus.py [--shell path] [--ref bash] [--list file]

Every run is contained: a fresh directory per invocation, with HOME, TMPDIR and
the working directory all inside it, stdin closed and a timeout. Containment is
not optional -- `tgz --help` writes an archive called `--help.tgz`, so a script
that looks like it only prints usage may not. Service scripts under /etc/init.d
are skipped outright; nothing good comes of running those twice.

Only stdout and exit status are compared. Error wording differs between shells
on purpose and is not a divergence.
"""
import os, shutil, subprocess, sys, tempfile

ARGS = [["--help"], ["--version"], []]
SKIP_DIRS = ("/etc/init.d",)


def sandbox_run(shell, script, args, keep):
    """Run one script with everything it might write pointed at a temp dir."""
    d = tempfile.mkdtemp(prefix="hibr-corpus-")
    env = dict(os.environ)
    env.update(HOME=d, TMPDIR=d, PWD=d, XDG_CACHE_HOME=d, XDG_CONFIG_HOME=d,
               XDG_DATA_HOME=d, LC_ALL="C", LANG="C")
    try:
        p = subprocess.run([shell, script] + args, cwd=d, env=env,
                           stdin=subprocess.DEVNULL, capture_output=True,
                           text=True, timeout=5)
        out, rc = p.stdout, p.returncode
    except subprocess.TimeoutExpired:
        out, rc = "", "timeout"
    except Exception as e:
        out, rc = "", "error:%s" % e
    wrote = sorted(os.listdir(d)) if os.path.isdir(d) else []
    if keep is not None:
        keep.extend(wrote)
    shutil.rmtree(d, ignore_errors=True)
    return rc, out


def main():
    shell, ref, listing = "./build/hibr", "bash", None
    a = sys.argv[1:]
    i = 0
    while i < len(a):
        if a[i] == "--shell":
            shell = a[i + 1]; i += 2
        elif a[i] == "--ref":
            ref = a[i + 1]; i += 2
        elif a[i] == "--list":
            listing = a[i + 1]; i += 2
        else:
            i += 1
    shell = os.path.abspath(shell) if "/" in shell else shell
    if listing:
        scripts = [l for l in open(listing).read().split() if l]
    else:
        print("give --list with a file of script paths", file=sys.stderr)
        return 2
    scripts = [s for s in scripts if not s.startswith(SKIP_DIRS)]

    runs = same = diff = 0
    reports = []
    for sc in scripts:
        for args in ARGS:
            wrote = []
            a1 = sandbox_run(ref, sc, args, wrote)
            a2 = sandbox_run(shell, sc, args, None)
            runs += 1
            if a1 == a2:
                same += 1
                continue
            diff += 1
            reports.append((sc, args, a1, a2, wrote))
    print("%d invocations over %d scripts: %d agreed, %d differed\n"
          % (runs, len(scripts), same, diff))
    for sc, args, a1, a2, wrote in reports:
        print("--- %s %s" % (sc, " ".join(args) or "(no arguments)"))
        print("    %-14s rc=%s out=%r" % (ref, a1[0], a1[1][:160]))
        print("    %-14s rc=%s out=%r" % (os.path.basename(shell), a2[0],
                                          a2[1][:160]))
        if wrote:
            print("    (it also wrote %s)" % ", ".join(wrote[:4]))
    return 1 if diff else 0


if __name__ == "__main__":
    sys.exit(main())
