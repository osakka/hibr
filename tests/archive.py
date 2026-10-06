#!/usr/bin/env python3
"""An archive read as a folder: the archive module, against real tarballs.

    python3 tests/archive.py [path-to-hibr]

The fixtures are built here with Python's own tarfile, so the suite needs
nothing installed and can write the awkward cases on purpose: an archive
with no folder entries at all (which is what tarfile writes, and what plenty
of real ones look like), a GNU long name, a pax header, and one big enough to
say whether the index is worth having.

What is checked: the members and their folders, implied folders among them,
one member's own line, its bytes through `archive cat` and through
`/dev/archive/NAME/path`, gzip read the same as plain, and every way of
asking for something that is not there.
"""
import io
import os
import shutil
import subprocess
import sys
import tarfile
import tempfile
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import screen as sx
from screen import check, report, tree

if len(sys.argv) > 1:
    sx.HIBR = os.path.abspath(sys.argv[1])
MODS = os.environ.get("HIBR_TESTMODS") or tree("build/mods")
D = tempfile.mkdtemp(prefix="hibr-arc-")
LONGDIR = "a-folder-whose-name-is-far-past-the-hundred-bytes-a-ustar-header-keeps-for-one-and-so-needs-a-block-of-its-own"
LONGFILE = "and-the-file-in-it-is-no-shorter-either-so-the-whole-path-has-to-come-from-that-block.txt"


def add(t, name, data, **kw):
    b = data.encode() if isinstance(data, str) else data
    i = tarfile.TarInfo(name)
    i.size = len(b)
    i.mtime = 1700000000
    for k, v in kw.items():
        setattr(i, k, v)
    t.addfile(i, io.BytesIO(b))


def build(name, mode="w", fmt=tarfile.GNU_FORMAT, dirs=True, many=0):
    path = os.path.join(D, name)
    t = tarfile.open(path, mode, format=fmt)
    if dirs:
        d = tarfile.TarInfo("sub")
        d.type, d.mode, d.mtime = tarfile.DIRTYPE, 0o755, 1700000000
        t.addfile(d)
    add(t, "a.txt", "hello from the archive\n")
    add(t, "sub/b.txt", "line one\nline two\n")
    add(t, "sub/deeper/c.txt", "down here\n")
    add(t, "%s/%s" % (LONGDIR, LONGFILE), "a long way round\n")
    for i in range(many):
        add(t, "many/f%04d.txt" % i, "row %d\n" % i)
    t.close()
    return path


PLAIN = build("plain.tar")
GZ = build("comp.tar.gz", "w:gz")
PAX = build("pax.tar", fmt=tarfile.PAX_FORMAT)
NODIRS = build("nodirs.tar", dirs=False)
MANY = build("many.tar.gz", "w:gz", many=500)


def hb(script, env=None):
    """Run a script with the archive module loaded; stdout, stderr, status."""
    r = subprocess.run(
        [sx.HIBR, "-c", "mod load %s/archive.so\n%s" % (MODS, script)],
        capture_output=True, text=True, timeout=60,
        env=dict(os.environ, **(env or {})))
    return r.stdout, r.stderr, r.returncode


out, err, rc = hb("archive open a %s\narchive list" % PLAIN)
check("an archive opens and says what it holds",
      rc == 0 and out.split("\t")[0] == "a" and out.rstrip().endswith("\t5"),
      (out, err))

out, err, rc = hb("archive open a %s\narchive ls a" % PLAIN)
rows = [l.split("\t") for l in out.splitlines()]
check("the root lists what is directly in it, folders and files",
      sorted((r[0], r[3]) for r in rows) ==
      [("d", LONGDIR), ("d", "sub"), ("f", "a.txt")], rows)

out, err, rc = hb("archive open a %s\narchive ls a sub" % PLAIN)
rows = [l.split("\t") for l in out.splitlines()]
check("a folder lists its own, with a folder nothing declared among them",
      sorted((r[0], r[3]) for r in rows) == [("d", "deeper"), ("f", "b.txt")], rows)

out, err, rc = hb("archive open a %s\narchive ls a sub/deeper" % NODIRS)
check("an archive with no folder entries at all has folders all the same",
      rc == 0 and out.split("\t")[3:] == ["c.txt\n".rstrip()] or "c.txt" in out,
      (out, err))

out, err, rc = hb("archive open a %s\narchive stat a sub/b.txt" % PLAIN)
check("stat gives the kind, the size, the time and the name",
      out.strip() == "f\t18\t1700000000\tsub/b.txt", (out, err))

out, err, rc = hb("archive open a %s\narchive cat a sub/b.txt" % PLAIN)
check("cat gives the member's own bytes", out == "line one\nline two\n", (out, err))

out, err, rc = hb("archive open a %s\nread -r l < /dev/archive/a/a.txt\necho \"[$l]\""
                  % PLAIN)
check("a member is a file anywhere a filename goes",
      out.strip() == "[hello from the archive]", (out, err))

out, err, rc = hb("archive open a %s\nwhile IFS= read -r l; do echo \"<$l>\"; done "
                  "< /dev/archive/a/sub/b.txt" % PLAIN)
check("and a loop reads it line by line", out == "<line one>\n<line two>\n", (out, err))

out, err, rc = hb("archive open g %s\narchive cat g sub/deeper/c.txt\n"
                  "read -r l < /dev/archive/g/a.txt\necho \"[$l]\"" % GZ)
check("a .tar.gz reads the same, expanded once at open",
      out == "down here\n[hello from the archive]\n", (out, err))

out, err, rc = hb("archive open p %s\narchive cat p %s/%s" % (PAX, LONGDIR, LONGFILE))
check("a pax header's own path reaches the member it names",
      out == "a long way round\n", (out, err))

out, err, rc = hb("archive open a %s\narchive cat a %s/%s" % (PLAIN, LONGDIR, LONGFILE))
check("so does a GNU long name", out == "a long way round\n", (out, err))

out, err, rc = hb("archive open a %s\narchive cat a nope.txt" % PLAIN)
check("a member that is not there is refused, saying so",
      rc != 0 and "not in a" in err, (out, err, rc))
out, err, rc = hb("archive open a %s\narchive cat a sub" % PLAIN)
check("and a folder is not a file to read",
      rc != 0 and "is a folder" in err, (out, err, rc))
out, err, rc = hb("archive ls nope")
check("an archive that was never opened is refused by name",
      rc != 0 and "not open" in err, (out, err, rc))
out, err, rc = hb("archive open a %s\narchive open a %s" % (PLAIN, PLAIN))
check("the same name twice is refused rather than losing the first",
      rc != 0 and "already" in err, (out, err, rc))
out, err, rc = hb("archive open a %s" % os.path.join(D, "nosuch.tar"))
check("a file that is not there says so", rc != 0 and "cannot be read" in err,
      (out, err, rc))
open(os.path.join(D, "junk.tar"), "w").write("this is not a tar at all, not even close")
out, err, rc = hb("archive open a %s" % os.path.join(D, "junk.tar"))
check("something that is not an archive is refused, not read as one",
      rc != 0 and ("not a tar archive" in err or "does not add up" in err),
      (out, err, rc))

out, err, rc = hb("archive open a %s\narchive close a\narchive ls a" % PLAIN)
check("close forgets it, and the name stops answering",
      rc != 0 and "not open" in err, (out, err, rc))
out, err, rc = hb("archive open a %s\narchive close a\n"
                  "read -r l < /dev/archive/a/a.txt" % PLAIN)
check("and the path stops being a file", rc != 0 and "no archive" in err,
      (out, err, rc))

t0 = time.time()
out, err, rc = hb("archive open m %s\nr := archive ls m many\necho \"${#r[@]}\"\n"
                  "archive cat m many/f0499.txt" % MANY)
took = time.time() - t0
check("five hundred members index and read: the count and the last one",
      out.splitlines()[:2] == ["500", "row 499"] and took < 20, (out, err, took))

out, err, rc = hb("archive open m %s\narchive ls m" % MANY, env={"ARCHIVE_MAX": "64"})
check("a bound on how far an archive may expand is kept",
      rc != 0 and ("does not expand" in err or "ARCHIVE_MAX" in err), (out, err, rc))

shutil.rmtree(D, True)
report(21)
