#!/usr/bin/env python3
"""The contain module: what a contained program can and cannot do.

Sheet's formulas run in it (Gitea #432, ADR 0052), so each check here is a
way out a formula would take: a write beside its folder, a connection, a
socket the user can reach, a mode or a time changed on somebody's file, a
signal to another process, a descriptor it was handed -- and, the other
half, that hibr itself and an ordinary pipeline still work inside.

A kernel without Landlock cannot contain anything, and the suite says so
on a line of its own and passes with nothing checked: that is the build
machine, whose kernel answers EOPNOTSUPP. It passed 12 of 12 on Linux 6.12
with Landlock ABI 6 for 0.99.427; run it on such a machine after any change
to mods/contain.c.
Run it directly:  python3 tests/contain.py [path-to-hibr]
"""
import os, shutil, socket, subprocess, sys, tempfile, time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from screen import check, report, tree

import screen as sx
HIBR = os.path.abspath(sys.argv[1]) if len(sys.argv) > 1 else sx.HIBR
MODS = os.environ.get("HIBR_TESTMODS", tree("build/mods"))
D = tempfile.mkdtemp(prefix="hibr-contain-")
W = os.path.join(D, "w")
os.mkdir(W)
ENV = dict(os.environ, HIBR_MODPATH=MODS)


def hb(script, *args):
    """Run hibr with the contain module loaded."""
    r = subprocess.run([HIBR, "-c", "need contain\n" + script, "x"] + list(args),
                       capture_output=True, text=True, cwd=D, env=ENV, timeout=60)
    return r.returncode, r.stdout, r.stderr


def inside(script, *args):
    """Run a script in a contained hibr whose folder is W."""
    p = os.path.join(D, "s.hibr")
    with open(p, "w") as f:
        f.write(script)
    return hb('contain exec "$1" -- "$2" "$3" "${@:4}"', W, HIBR, p, *args)


st, probe, _ = hb("contain probe")
if st != 0:
    print("skipped: nothing on this kernel can contain a program "
          "(contain probe found no Landlock); run this where it can")
    shutil.rmtree(D, ignore_errors=True)
    report()
print("contain probe:", probe.strip())

st, out, err = inside('echo in > "$1/a"; cat "$1/a"', W)
check("a contained program writes inside its folder", st == 0 and out == "in\n", err)

st, out, err = inside('echo out > "$1/out" && echo wrote', D)
check("and not beside it", "wrote" not in out and not os.path.exists(os.path.join(D, "out")), err)

st, out, err = inside('sh -c "echo out > \\"$1/sh\\"" && echo wrote', D)
check("nor can a program it starts", "wrote" not in out
      and not os.path.exists(os.path.join(D, "sh")), err)

st, out, err = inside('''need math
x=$(printf 'b\\na\\nc\\n' | sort | head -2 | tr -d '\\n')
echo "$x $(math 2+3) $(printf hi | cksum | cut -c1-3)"
''')
check("hibr, math and a pipeline work inside", out.startswith("ab 5 "), out + err)

srv = socket.socket()
srv.bind(("127.0.0.1", 0))
srv.listen(4)
port = srv.getsockname()[1]
st, out, err = hb('exec {f}<>/dev/tcp/127.0.0.1/%d && echo open' % port)
check("a connection opens uncontained", "open" in out, err)
st, out, err = inside('exec {f}<>/dev/tcp/127.0.0.1/%d && echo open' % port)
check("and is refused contained", "open" not in out, out + err)
srv.close()

up = os.path.join(D, "u.sock")
us = socket.socket(socket.AF_UNIX)
us.bind(up)
us.listen(4)
st, out, err = inside('exec {f}<>/dev/unix/%s && echo open' % up)
check("a Unix socket the user can reach is refused", "open" not in out, out + err)
us.close()

victim = os.path.join(D, "victim")
open(victim, "w").close()
os.chmod(victim, 0o644)
old = os.stat(victim)
time.sleep(1.1)
st, out, err = inside('chmod 600 "$1"; touch "$1"; touch -d 2001-01-01 "$1"; :', victim)
new = os.stat(victim)
check("no mode changed outside", (new.st_mode & 0o777) == 0o644, oct(new.st_mode))
check("no time changed outside", new.st_mtime == old.st_mtime,
      "%s %s" % (old.st_mtime, new.st_mtime))

sl = subprocess.Popen(["sleep", "30"])
st, out, err = inside('kill -TERM %d && echo sent' % sl.pid)
time.sleep(0.2)
alive = sl.poll() is None
check("no signal to another process", alive and "sent" not in out, out + err)
sl.kill()
sl.wait()

st, out, err = hb('exec 7> "$1/fd7"; contain exec "$2" -- "$3" -c "echo leak >&7; echo done"',
                  D, W, HIBR)
check("a descriptor it was handed is closed", "done" in out
      and os.path.getsize(os.path.join(D, "fd7")) == 0, out + err)

st, out, err = hb('contain exec "$1" -- false', W)
check("its status comes back", st == 1, err)

shutil.rmtree(D, ignore_errors=True)
report(12)
