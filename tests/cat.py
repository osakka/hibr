#!/usr/bin/env python3
"""Check what the cat module does when its output really is a terminal.

The .t files cannot reach this: everything cat adds is conditional on
isatty(1), and under tests/run.sh standard output is a pipe — which is the
point, and what tests/650-cat.t checks. This is the other half.
Run it directly:  python3 tests/cat.py [path-to-hibr]
"""
import os, sys, tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import screen as sx
from screen import Term, check, report, load

if len(sys.argv) > 1:
    sx.HIBR = os.path.abspath(sys.argv[1])
D = tempfile.mkdtemp(prefix="hibr-cat-")
LOAD = load("cat")


def tty(cmd, wait=1.5):
    """Run a command with standard output attached to a pseudo terminal."""
    t = Term("-c", LOAD + cmd, settle=0, size=False)
    t.collect(wait)
    t.close()
    return t.text


def write(nm, data):
    p = os.path.join(D, nm)
    open(p, "wb").write(data if isinstance(data, bytes) else data.encode())
    return p


c = write("s.c", '#include "h.h"\nint n = 42;\nreturn 0;\n')
sh = write("s.sh", '# a comment\nif [ -n "$x" ]; then\n  echo "hi 42"\nfi\n')
md = write("s.md", "# Heading\n\n- a bullet\n\nplain\n")
js = write("s.json", '{"a": 1, "b": true}\n')
u8 = write("u.txt", "héllo wörld — dash\nctrl\x01end\n")
bi = write("b.dat", open("/bin/sh", "rb").read(300))
pl = write("plain.txt", "one\ntwo\n")

t = tty("cat %s" % pl)
check("a gutter appears when output is a terminal", "│ one" in t)
check("the line number is dimmed", "\x1b[38;5;240m     1\x1b[0m" in t)
check("numbering counts on", "\x1b[38;5;240m     2\x1b[0m" in t)

t = tty("cat %s | cat -p" % pl)
check("a pipe turns all of it off", "│" not in t and "\x1b[" not in t)

t = tty("cat -p %s" % pl)
check("-p turns it off on a terminal too", "│" not in t and "\x1b[" not in t)

t = tty("cat %s" % c)
check("a C string is coloured", '\x1b[38;5;71m"h.h"\x1b[0m' in t)
check("a C number is coloured", "\x1b[38;5;173m42\x1b[0m" in t)
check("a C keyword is coloured", "\x1b[38;5;110mint\x1b[0m" in t)

t = tty("cat %s" % sh)
check("a shell comment is coloured", "\x1b[38;5;245;3m# a comment\x1b[0m" in t)
check("a shell keyword is coloured", "\x1b[38;5;110mif\x1b[0m" in t)
check("a number inside a string is not recoloured",
      '\x1b[38;5;71m"hi 42"\x1b[0m' in t)

t = tty("cat %s" % md)
check("a markdown heading is bold", "\x1b[1m# Heading\x1b[0m" in t)
check("a markdown bullet is marked", "\x1b[38;5;110m- a bullet\x1b[0m" in t)

t = tty("cat %s" % js)
check("json keys and values are told apart",
      '\x1b[38;5;71m"a"\x1b[0m' in t and "\x1b[38;5;110mtrue\x1b[0m" in t)

t = tty("cat %s" % u8)
check("utf-8 reaches the terminal unharmed", "héllo wörld — dash" in t)
check("a control byte is shown rather than sent", "\x1b[38;5;244m^A\x1b[0m" in t)

t = tty("cat %s; echo rc=$?" % bi)
check("a binary file is refused, not spewed", "binary file, 300 bytes" in t)
check("and that is a failure", "rc=1" in t)
check("nothing of it reaches the terminal", "ELF" not in t)

t = tty("cat -f %s" % bi)
check("-f shows it anyway", "ELF" in t)

t = tty("cat -n %s" % pl)
check("-n on a terminal still gives one gutter", t.count("│") == 2)

t = tty("cat %s %s" % (pl, pl))
check("numbering runs across files, as cat has always done",
      "\x1b[38;5;240m     4\x1b[0m" in t)

t = tty("cat %s/nosuchfile; echo rc=$?" % D)
check("a missing file is an error and a status", "rc=1" in t)

blk = write("b.c", "/* opens here\n   keeps going\n   ends */\nint x = 1;\n")
t = tty("cat %s" % blk)
check("a block comment carries to the next line",
      t.count("\x1b[38;5;245;3m") == 3)
check("and stops where it closes", "\x1b[38;5;110mint\x1b[0m" in t)

tb = write("t.txt", "col1\tcol2\nlonger1\tb\n")
t = tty("cat %s" % tb)
check("tabs reach the stop the file means, not the terminal's",
      "col1    col2" in t and "longer1 b" in t)

bad = write("bad.txt", b"ok \xff\xfe end\n")
t = tty("cat %s" % bad)
check("a byte that is not text is shown as itself", "<ff>" in t and "<fe>" in t)
check("and valid utf-8 beside it is untouched", "ok " in t and " end" in t)

print()
for f in os.listdir(D):
    os.unlink(os.path.join(D, f))
os.rmdir(D)
report(27)
