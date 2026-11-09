#!/usr/bin/env python3
"""The pointer lock goes to the display showing the window that asks for it
(0.99.479).

The owner's main screen is two Blit sessions, one above the other, and the
lock went to whichever display hold had heard from last -- the bottom one,
the primary, which joined last -- so a game on the top half asked for a
confined pointer and the session that took it showed no picture: Blit fell
back to a hidden pointer. The desktop now asks from the window's middle, the
emulator keeps that cell, and hold gives the lock to the display whose part
of the screen holds it. Here: two stacked displays that both can hold the
pointer, the bottom joining last, and a program asking from a cell on each.
"""
import os, re, shutil, subprocess, sys, tempfile, time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import screen
from screen import Term, check, report, load

D = tempfile.mkdtemp(prefix="hibr-holdlock-")
SLOW = 4 if "ASAN_OPTIONS" in os.environ else 1
out = os.path.join(D, "s")
prog = os.path.join(D, "lock-%d.hibr" % os.getpid())
open(prog, "w").write(
    "need console\n"
    "console open\n"
    "while [ ! -f %(o)s.stop ]; do\n"
    "  for w in top bottom off plain; do\n"
    "    if [ -f %(o)s.$w ]; then\n"
    "      rm -f %(o)s.$w\n"
    "      case $w in\n"
    "      top) console pointer lock on 3 10 ;;\n"
    "      bottom) console pointer lock on 15 10 ;;\n"
    "      plain) console pointer lock on ;;\n"
    "      off) console pointer lock off ;;\n"
    "      esac\n"
    "    fi\n"
    "  done\n"
    "  console put 0 0 x\n"
    "  console flush\n"
    "  console key 150 > /dev/null\n"
    "done\n" % {"o": out})

nm = "hlk-%d" % os.getpid()
env = {"TMPDIR": D}
a0 = os.path.join(D, "a0.hibr")
open(a0, "w").write(load("hold") + 'hold new -d %s "$HIBR" %s\nsleep 0.6\n'
                    'hold attach -m -n top %s 0 0\n' % (nm, prog, nm))
t0 = Term(a0, rows=12, cols=60, settle=1.0, env=env, caps=["lock"])
t0.collect(1.0 * SLOW)
a1 = os.path.join(D, "a1.hibr")
open(a1, "w").write(load("hold") + 'hold attach -m -n bottom %s 12 0\n' % nm)
t1 = Term(a1, rows=12, cols=60, settle=1.0, env=env, caps=["lock"])
t1.collect(1.5 * SLOW)

ON, OFF = b"\x1b[?7701h", b"\x1b[?7701l"


def ask(w):
    """Ask as w says, and what each display was sent meanwhile."""
    c0, c1 = len(t0.out), len(t1.out)
    open(out + "." + w, "w").close()
    for _ in range(int(20 * SLOW)):
        t0.collect(0.1)
        t1.collect(0.1)
        if not os.path.exists(out + "." + w):
            break
    t0.collect(0.4)
    t1.collect(0.4)
    return t0.out[c0:], t1.out[c1:]


r = subprocess.run([screen.HIBR, "-c", load("hold") + "hold locks %s" % nm],
                   env=dict(os.environ, **env), capture_output=True, text=True,
                   timeout=10)
check("both displays can hold the pointer, the bottom one joined last",
      re.search(r"top yes", r.stdout) and re.search(r"bottom yes", r.stdout),
      r.stdout + r.stderr)
s0, s1 = ask("top")
check("asked from a cell on the top display, the top display holds it and "
      "the bottom one, which joined last, is not asked",
      ON in s0 and ON not in s1, (s0[-80:], s1[-80:]))
s0, s1 = ask("off")
check("given back, the top display is told to let go", OFF in s0, s0[-80:])
s0, s1 = ask("bottom")
check("asked from a cell on the bottom display, the bottom one holds it",
      ON in s1 and ON not in s0, (s0[-80:], s1[-80:]))
ask("off")
s0, s1 = ask("plain")
check("asked from nowhere in particular, it goes to the display last used, "
      "as before", (ON in s0) != (ON in s1), (s0[-80:], s1[-80:]))
ask("off")

open(out + ".stop", "w").close()
time.sleep(0.4)
t1.quit(None, 0.3)
t0.quit(None, 0.3)
subprocess.run([screen.HIBR, "-c", load("hold") + "hold kill %s" % nm],
               env=dict(os.environ, **env), capture_output=True)
shutil.rmtree(D, True)
report(6)
