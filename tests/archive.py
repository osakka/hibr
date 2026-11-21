#!/usr/bin/env python3
"""An archive read as a folder: the archive module, against real tarballs,
zips and disc images.

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
import calendar
import io
import os
import random
import shutil
import struct
import subprocess
import sys
import tarfile
import tempfile
import time
import zipfile

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

hb256 = os.path.join(D, "b256.bin")
open(hb256, "wb").write(b"x" * 148 + b"\x80\x41\x41\x41\x41\x41\x41\x41" + b"x" * 356)
out, err, rc = hb("archive open a %s" % hb256)
check("a block whose checksum field reads as a huge base-256 number is refused as a "
      "header that does not add up, with no overflow on the way",
      rc == 1 and "does not add up" in err, (out, err, rc))

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

# --- writing and extracting (Gitea #475) ---------------------------------
# What create writes is read back by GNU tar itself, not only by this
# module, and what extract writes is compared with the tree it came from;
# the hostile archives are Python's, which writes any name it is given.
SRC = os.path.join(D, "src")
os.makedirs(os.path.join(SRC, "sub", "deep"))
open(os.path.join(SRC, "a.txt"), "w").write("hello\n")
open(os.path.join(SRC, "sub", "big.bin"), "wb").write(bytes(range(256)) * 900)
LONGD = "d" * 120
os.makedirs(os.path.join(SRC, LONGD))
open(os.path.join(SRC, LONGD, "f" * 110 + ".txt"), "w").write("long\n")
os.symlink("a.txt", os.path.join(SRC, "link"))
os.chmod(os.path.join(SRC, "a.txt"), 0o755)


def same(a, b):
    """Whether two trees hold the same names, bytes, links and modes."""
    r = subprocess.run(["diff", "-r", "--no-dereference", a, b], capture_output=True)
    return r.returncode == 0


def gnu(args, cwd):
    if not shutil.which("tar"):
        return None
    return subprocess.run(["tar"] + args, cwd=cwd, capture_output=True, text=True)


for ext, flag in (("tar", ""), ("tar.gz", "z")):
    out_p = os.path.join(D, "made." + ext)
    out, err, rc = hb("archive create %s -C %s a.txt sub link %s" % (out_p, SRC, LONGD))
    g = os.path.join(D, "gnu-" + ext)
    os.makedirs(g)
    r = gnu(["-x%sf" % flag, out_p, "-C", g], D)
    ok = r is None or (r.returncode == 0 and all(
        same(os.path.join(SRC, n), os.path.join(g, n)) for n in ("a.txt", "sub", "link", LONGD)))
    check("archive create writes a .%s that GNU tar extracts to the same tree, "
          "long names and links included" % ext, rc == 0 and ok, (out, err, r and r.stderr))

made = os.path.join(D, "made.tar.gz")
x = os.path.join(D, "x1")
os.makedirs(x)
out, err, rc = hb("archive open a %s\nn := archive extract a %s\necho \"$n\"" % (made, x))
check("archive extract writes every member back, the same tree, and says how many",
      rc == 0 and out.strip() == "7" and all(
          same(os.path.join(SRC, n), os.path.join(x, n)) for n in ("a.txt", "sub", "link", LONGD)),
      (out, err))
check("with the mode kept and the time put back",
      os.stat(os.path.join(x, "a.txt")).st_mode & 0o777 == 0o755 and
      int(os.stat(os.path.join(x, "a.txt")).st_mtime) ==
      int(os.stat(os.path.join(SRC, "a.txt")).st_mtime),
      oct(os.stat(os.path.join(x, "a.txt")).st_mode))
x = os.path.join(D, "x2")
os.makedirs(x)
out, err, rc = hb("archive open a %s\narchive extract a %s sub" % (made, x))
check("extract PATH writes that folder alone, under its own name",
      rc == 0 and sorted(os.listdir(x)) == ["sub"] and
      same(os.path.join(SRC, "sub"), os.path.join(x, "sub")), (out, err, os.listdir(x)))
out, err, rc = hb("archive open a %s\narchive extract a %s" % (made, x))
check("and a member that is there already refuses the whole extraction",
      rc != 0 and "there already" in err and not os.path.exists(os.path.join(x, "a.txt")),
      (out, err))


def hostile(name, entries):
    path = os.path.join(D, name)
    t = tarfile.open(path, "w", format=tarfile.GNU_FORMAT)
    for n, kind, data in entries:
        i = tarfile.TarInfo(n)
        if kind == "f":
            b = data.encode()
            i.size = len(b)
            t.addfile(i, io.BytesIO(b))
        else:
            i.type = tarfile.SYMTYPE
            i.linkname = data
            t.addfile(i)
    t.close()
    return path


for name, entries, why in (
        ("up.tar", [("ok.txt", "f", "x"), ("../escape.txt", "f", "bad")], "would land outside"),
        ("lnabs.tar", [("ok.txt", "f", "x"), ("pw", "l", "/etc/passwd")], "links out"),
        ("lnup.tar", [("ok.txt", "f", "x"), ("up", "l", "../../escape")], "links out")):
    box = os.path.join(D, "box-" + name)
    inner = os.path.join(box, "inner")
    os.makedirs(inner)
    out, err, rc = hb("archive open a %s\narchive extract a %s" % (hostile(name, entries), inner))
    check("an archive whose %s refuses the whole of it, nothing written anywhere"
          % entries[1][0], rc != 0 and why in err and os.listdir(inner) == [] and
          os.listdir(box) == ["inner"], (out, err, os.listdir(inner)))
dot = os.path.join(D, "dot.tar")
t = tarfile.open(dot, "w", format=tarfile.GNU_FORMAT)
i = tarfile.TarInfo("./")
i.type = tarfile.DIRTYPE
t.addfile(i)
b = b"dot"
i = tarfile.TarInfo("./in.txt")
i.size = len(b)
t.addfile(i, io.BytesIO(b))
t.close()
box = os.path.join(D, "box-dot")
os.makedirs(box)
out, err, rc = hb("archive open a %s\narchive extract a %s" % (dot, box))
check("a ./ root entry, as tar c . writes it, is the folder itself (Gitea #487)",
      rc == 0 and os.listdir(box) == ["in.txt"], (out, err, os.listdir(box)))
box = os.path.join(D, "box-abs")
os.makedirs(box)
out, err, rc = hb("archive open a %s\narchive extract a %s" % (
    hostile("abs.tar", [("/abs-escape-%d.txt" % os.getpid(), "f", "x")]), box))
check("an absolute name lands inside the folder, as GNU tar puts it",
      rc == 0 and os.listdir(box) == ["abs-escape-%d.txt" % os.getpid()] and
      not os.path.exists("/abs-escape-%d.txt" % os.getpid()), (out, err, os.listdir(box)))

out, err, rc = hb("archive create %s -C %s a.txt" % (made, SRC))
check("create never writes over an archive that is there", rc != 0 and "there already" in err,
      (out, err))
out, err, rc = hb("archive create %s -C %s ../a.txt" % (os.path.join(D, "n.tar"), SRC))
check("and refuses a path that climbs out of its folder",
      rc != 0 and "no .." in err and not os.path.exists(os.path.join(D, "n.tar")), (out, err))
inside = os.path.join(SRC, "self.tar")
out, err, rc = hb("archive create %s -C %s .\narchive open s %s\narchive ls s" % (inside, SRC, inside))
check("an archive written into the folder it holds leaves itself out",
      rc == 0 and "self.tar" not in out and not os.path.exists(inside + ".hibr-part"),
      (out, err))

# --- zip and disc images (Gitea #488) --------------------------------------
# The fixtures that say what is right come from programs that write these
# formats for a living -- Python's zipfile, zip, bsdtar and xorriso -- and the
# trees they hold are compared with the folder they were made from. Only the
# hostile and the damaged ones are patched by hand.
ZSRC = os.path.join(D, "zsrc")
os.makedirs(os.path.join(ZSRC, "sub", "deep"))
open(os.path.join(ZSRC, "a.txt"), "w").write("hello from the zip\n")
open(os.path.join(ZSRC, "sub", "b.txt"), "w").write("a line said again\n" * 400)
open(os.path.join(ZSRC, "sub", "deep", "r.bin"), "wb").write(bytes(range(256)) * 37)
os.symlink("a.txt", os.path.join(ZSRC, "link"))
os.chmod(os.path.join(ZSRC, "a.txt"), 0o750)
for root, dirs, files in os.walk(ZSRC):
    for n in dirs + files:
        os.utime(os.path.join(root, n), (1700000000, 1700000000), follow_symlinks=False)
WHEN = (2023, 11, 14, 22, 13, 20)


def pyzip(name, members):
    """A zip from Python's zipfile: (name, bytes or None for a folder, method)."""
    path = os.path.join(D, name)
    with zipfile.ZipFile(path, "w") as z:
        for n, data, meth in members:
            i = zipfile.ZipInfo(n, WHEN)
            i.compress_type = meth
            if data is None:
                i.external_attr = (0o40755 << 16) | 0x10
                z.writestr(i, b"")
            else:
                i.external_attr = 0o640 << 16
                z.writestr(i, data)
    return path


def zipents(data):
    """Where each central directory entry starts, and its name."""
    out, i = [], data.find(b"PK\x01\x02")
    while i >= 0 and data[i:i + 4] == b"PK\x01\x02":
        nl, xl, cl = struct.unpack("<HHH", data[i + 28:i + 34])
        out.append((i, data[i + 46:i + 46 + nl]))
        i += 46 + nl + xl + cl
    return out


def patch(path, name, fn):
    data = bytearray(open(path, "rb").read())
    fn(data)
    out = os.path.join(D, name)
    open(out, "wb").write(bytes(data))
    return out


B_TXT = ("a line said again\n" * 400).encode()
PZ = pyzip("py.zip", [("a.txt", b"hello from the zip\n", zipfile.ZIP_STORED),
                       ("sub/", None, zipfile.ZIP_STORED),
                       ("sub/b.txt", B_TXT, zipfile.ZIP_DEFLATED),
                       ("sub/deeper/c.txt", b"down here\n", zipfile.ZIP_DEFLATED),
                       ("%s/%s" % (LONGDIR, LONGFILE), b"a long way round\n", zipfile.ZIP_DEFLATED)])
out, err, rc = hb("archive open z %s\narchive list\narchive ls z\narchive ls z sub" % PZ)
rows = sorted(tuple(l.split("\t")[i] for i in (0, 3)) for l in out.splitlines()[1:])
check("a zip opens from its bytes and lists as a folder, implied folders among them",
      rc == 0 and out.startswith("z\t%s\t5\n" % PZ) and rows ==
      sorted([("d", "deeper"), ("d", LONGDIR), ("d", "sub"), ("f", "a.txt"), ("f", "b.txt")]), (out, err))
out, err, rc = hb("archive open z %s\narchive cat z sub/b.txt | cmp - %s && echo same\n"
                  "read -r l < /dev/archive/z/sub/deeper/c.txt\necho \"[$l]\"\n"
                  "archive cat z a.txt" % (PZ, os.path.join(ZSRC, "sub", "b.txt")))
check("a deflated member inflates to its own bytes, through cat and through the scheme, "
      "and a stored one reads where it lies",
      out == "same\n[down here]\nhello from the zip\n", (out, err))
out, err, rc = hb("archive open z %s\narchive stat z sub/b.txt" % PZ, env={"TZ": "UTC"})
check("stat gives a zip member's size and its DOS time, read as local time",
      out.strip() == "f\t%d\t%d\tsub/b.txt" % (len(B_TXT), calendar.timegm(WHEN + (0, 0, 0))), (out, err))

bz = os.path.join(D, "bsd.zip")
subprocess.run(["bsdtar", "--format", "zip", "-cf", bz, "a.txt", "sub", "link"], cwd=ZSRC, check=True)
x = os.path.join(D, "zx1")
os.makedirs(x)
out, err, rc = hb("archive open z %s\nn := archive extract z %s\necho \"$n\"" % (bz, x))
check("a zip bsdtar wrote extracts to the tree it was made from: modes, times and the link",
      rc == 0 and all(same(os.path.join(ZSRC, n), os.path.join(x, n)) for n in ("a.txt", "sub", "link")) and
      os.path.islink(os.path.join(x, "link")) and
      os.stat(os.path.join(x, "a.txt")).st_mode & 0o777 == 0o750 and
      int(os.stat(os.path.join(x, "sub", "deep", "r.bin")).st_mtime) == 1700000000, (out, err))
z64 = os.path.join(D, "z64.zip")
subprocess.run(["bsdtar", "--format", "zip", "--options", "zip:zip64", "-cf", z64, "sub"], cwd=ZSRC, check=True)
out, err, rc = hb("archive open z %s\narchive cat z sub/deep/r.bin | cmp - %s && echo same"
                  % (z64, os.path.join(ZSRC, "sub", "deep", "r.bin")))
check("so does one written with zip64's own end records",
      out == "same\n" and b"PK\x06\x06" in open(z64, "rb").read(), (out, err))

cp = patch(pyzip("cp0.zip", [("cafX.txt", b"dos\n", zipfile.ZIP_STORED)]), "cp437.zip",
           lambda d: d.__setitem__(slice(None), d.replace(b"cafX.txt", b"caf\x82.txt")))
out, err, rc = hb("archive open z %s\narchive ls z" % cp)
check("a name not marked UTF-8 is read as code page 437, as DOS wrote it",
      out.split("\t")[-1].strip() == "café.txt", (out, err))

bad = patch(PZ, "damaged.zip", lambda d: d.__setitem__(
    d.find(b"sub/b.txt") + 9 + 40, d[d.find(b"sub/b.txt") + 9 + 40] ^ 0x55))
out, err, rc = hb("archive open z %s\narchive cat z sub/b.txt" % bad)
check("a damaged deflate stream is refused, not handed out", rc != 0 and "damaged" in err, (out, err))


def zflag(field, val):
    def fn(d):
        for i, n in zipents(bytes(d)):
            if n == b"sub/b.txt":
                if field == 8:
                    d[i + 8] |= val
                else:
                    d[i + 10:i + 12] = struct.pack("<H", val)
    return fn


box = os.path.join(D, "zbox-enc")
os.makedirs(os.path.join(box, "in"))
enc = os.path.join(D, "enc.zip")
subprocess.run(["bsdtar", "--format", "zip", "--options", "zip:encryption=traditional,zip:compression=store",
                "--passphrase", "x", "-cf", enc, "a.txt", "sub"], cwd=ZSRC, check=True)
out, err, rc = hb("archive open z %s\narchive cat z sub/b.txt\necho \"cat $?\"\narchive extract z %s"
                  % (enc, os.path.join(box, "in")))
check("an encrypted member says so, and an extraction holding one writes nothing",
      rc != 0 and "cat 1" in out and "is encrypted" in err and "nothing was written" in err and
      os.listdir(os.path.join(box, "in")) == [], (out, err))
out, err, rc = hb("archive open z %s\narchive cat z sub/b.txt" % patch(PZ, "bz2.zip", zflag(10, 12)))
check("and so does a member compressed some way other than deflate",
      rc != 0 and "in a way this does not read" in err, (out, err))

huge = os.path.join(D, "huge64.zip")
i = zipfile.ZipInfo("big.txt", WHEN)
i.compress_type = zipfile.ZIP_DEFLATED
i.extra = struct.pack("<HHQ", 1, 8, 0x7fffffffffffffff)
with zipfile.ZipFile(huge, "w") as z:
    z.writestr(i, b"x" * 1000)
huge = patch(huge, "huge64b.zip", lambda d: d.__setitem__(
    slice(d.find(b"PK\x01\x02") + 20, d.find(b"PK\x01\x02") + 24), b"\xff\xff\xff\xff"))
out, err, rc = hb("archive open z %s\narchive cat z big.txt" % huge)
check("a zip64 compressed size reaching past the end of the file is refused, not allocated",
      rc != 0 and "ends early" in err, (out, err))
out, err, rc = hb("archive open z %s\narchive ls z" % pyzip("big.zip", [("zeros", bytes(1 << 20), zipfile.ZIP_DEFLATED)]),
                  env={"ARCHIVE_MAX": "4096"})
check("a zip whose members add up past ARCHIVE_MAX beyond its own size is refused at open",
      rc != 0 and "ARCHIVE_MAX" in err, (out, err))

for name, members, why in (
        ("zup.zip", [("ok.txt", b"x", 0), ("../escape.txt", b"bad", 0)], "would land outside"),
        ("zdeep.zip", [("ok.txt", b"x", 0), ("a/../../escape.txt", b"bad", 0)], "would land outside")):
    box = os.path.join(D, "zbox-" + name)
    inner = os.path.join(box, "inner")
    os.makedirs(inner)
    out, err, rc = hb("archive open a %s\narchive extract a %s" % (pyzip(name, members), inner))
    check("a zip whose %s refuses the whole of it, nothing written anywhere" % members[1][0],
          rc != 0 and why in err and os.listdir(inner) == [] and os.listdir(box) == ["inner"], (out, err))
lsrc = os.path.join(D, "lsrc")
os.makedirs(lsrc)
open(os.path.join(lsrc, "ok.txt"), "w").write("x")
os.symlink("/etc/passwd", os.path.join(lsrc, "pw"))
lz = os.path.join(D, "lnabs.zip")
subprocess.run(["bsdtar", "--format", "zip", "-cf", lz, "ok.txt", "pw"], cwd=lsrc, check=True)
box = os.path.join(D, "zbox-ln")
os.makedirs(os.path.join(box, "inner"))
out, err, rc = hb("archive open a %s\narchive extract a %s" % (lz, os.path.join(box, "inner")))
check("and a zip whose link points out of the folder refuses the whole of it",
      rc != 0 and "links out" in err and os.listdir(os.path.join(box, "inner")) == [], (out, err))

# Disc images: xorriso for Rock Ridge (it always writes it), bsdtar for
# Joliet alone and for plain ISO 9660, and CUE/BIN wrapped around them here,
# since nothing installed writes raw sectors.
LONGRR = "r" * 200 + ".txt"
open(os.path.join(ZSRC, "sub", LONGRR), "w").write("past one record\n")
os.utime(os.path.join(ZSRC, "sub", LONGRR), (1700000000, 1700000000))
os.utime(os.path.join(ZSRC, "sub"), (1700000000, 1700000000))


def mkiso(name, *opts):
    path = os.path.join(D, name)
    if opts and opts[0] == "xorriso":
        subprocess.run(["xorriso", "-as", "mkisofs", "-quiet", "-R", "-J", "-o", path, ZSRC],
                       check=True, capture_output=True)
    else:
        subprocess.run(["bsdtar", "--format", "iso9660"] + list(opts) + ["-cf", path, "a.txt", "sub"] +
                       ([] if opts else ["link"]), cwd=ZSRC, check=True)
    return path


RJ = mkiso("rj.iso", *(("xorriso",) if shutil.which("xorriso") else ()))
x = os.path.join(D, "ix1")
os.makedirs(x)
out, err, rc = hb("archive open i %s\narchive ls i\nn := archive extract i %s\necho \"$n\"" % (RJ, x))
check("an ISO 9660 image with Rock Ridge extracts to the tree it was made from, "
      "names, modes, times, a link and a name past one record's room",
      rc == 0 and all(same(os.path.join(ZSRC, n), os.path.join(x, n)) for n in ("a.txt", "sub", "link")) and
      os.stat(os.path.join(x, "a.txt")).st_mode & 0o777 == 0o750 and
      int(os.stat(os.path.join(x, "sub", LONGRR)).st_mtime) == 1700000000, (out, err))
JO = mkiso("joliet.iso", "--options", "!rockridge")
out, err, rc = hb("archive open i %s\narchive ls i sub" % JO)
check("one with Joliet names alone reads them, case and all",
      rc == 0 and sorted(l.split("\t")[3] for l in out.splitlines())[:2] == ["b.txt", "deep"], (out, err))
PL = mkiso("plain.iso", "--options", "!rockridge,!joliet")
out, err, rc = hb("archive open i %s\narchive ls i\narchive cat i SUB/B.TXT | cmp - %s && echo same"
                  % (PL, os.path.join(ZSRC, "sub", "b.txt")))
check("and a plain one its own names, the version taken off",
      rc == 0 and sorted(l.split("\t")[3] for l in out.splitlines()[:-1]) == ["A.TXT", "SUB"] and
      out.endswith("same\n"), (out, err))

ISO = open(RJ, "rb").read()


def rawbin(data, mode):
    out = bytearray()
    for i in range(0, len(data), 2048):
        sec = b"\x00" + b"\xff" * 10 + b"\x00" + bytes([0, 2, 0, mode]) + (bytes(8) if mode == 2 else b"")
        sec += data[i:i + 2048]
        out += sec + bytes(2352 - len(sec))
    return bytes(out)


CD = os.path.join(D, "cd")
os.makedirs(CD)
open(os.path.join(CD, "GAME.BIN"), "wb").write(rawbin(ISO, 1))
open(os.path.join(CD, "game.cue"), "w").write(
    'FILE "game.bin" BINARY\r\n  TRACK 01 MODE1/2352\r\n    INDEX 01 00:00:00\r\n'
    '  TRACK 02 AUDIO\r\n    INDEX 01 05:00:00\r\n')
open(os.path.join(CD, "m2.bin"), "wb").write(rawbin(ISO, 2))
open(os.path.join(CD, "late.img"), "wb").write(bytes(2048 * 150) + ISO)
open(os.path.join(CD, "late.cue"), "w").write(
    'FILE "late.img" BINARY\n TRACK 01 AUDIO\n  INDEX 01 00:00:00\n'
    ' TRACK 02 MODE1/2048\n  INDEX 00 00:00:00\n  INDEX 01 00:02:00\n')
want = os.path.join(ZSRC, "sub", "b.txt")
for f, what in (("game.cue", "a CUE sheet's raw MODE1/2352 track, its BIN named in another case"),
                ("m2.bin", "a raw MODE2/2352 image opened by itself"),
                ("late.cue", "a 2048-byte data track that starts at its INDEX 01, after an audio track")):
    out, err, rc = hb("archive open c %s\narchive cat c sub/b.txt | cmp - %s && echo same"
                      % (os.path.join(CD, f), want))
    check("%s reads the same files" % what, out == "same\n", (out, err))
open(os.path.join(CD, "up.cue"), "w").write('FILE "../plain.iso" BINARY\n TRACK 01 MODE1/2048\n')
open(os.path.join(CD, "odd.cue"), "w").write('FILE "game.bin" BINARY\n TRACK 01 MODE2/2336\n')
out, err, rc = hb("archive open c %s; echo $?; archive open d %s" %
                  (os.path.join(CD, "up.cue"), os.path.join(CD, "odd.cue")))
check("a CUE sheet naming a file outside its own folder is refused, and one with no "
      "track this reads names the mode it found",
      out == "1\n" and rc != 0 and "not beside it" in err and "MODE2/2336" in err, (out, err))
out, err, rc = hb("archive open c %s" % patch(os.path.join(CD, "m2.bin"), "nofs.bin",
                                               lambda d: d.__setitem__(slice(16 * 2352 + 25, 16 * 2352 + 30), b"XXXXX")))
check("a raw image whose data holds no ISO 9660 file system is refused, saying so",
      rc != 0 and "no ISO 9660" in err, (out, err))


def isodir(data, ext):
    recs, off, i = [], ext * 2048, 0
    while i < 2048 and data[off + i]:
        recs.append(bytearray(data[off + i:off + i + data[off + i]]))
        i += data[off + i]
    return recs


def isoput(data, ext, recs):
    b = b"".join(recs)
    data[ext * 2048:ext * 2048 + 2048] = b + bytes(2048 - len(b))


def root(data):
    return struct.unpack("<I", data[32768 + 156 + 2:32768 + 156 + 6])[0]


def cyc(d):
    r = root(d)
    recs = isodir(d, r)
    for rec in recs:
        if rec[33:33 + rec[32]] == b"SUB":
            rec[2:10] = struct.pack("<I", r) + struct.pack(">I", r)
    isoput(d, r, recs)


t0 = time.time()
out, err, rc = hb("archive open i %s\narchive ls i\narchive ls i SUB" % patch(PL, "cycle.iso", cyc))
check("a folder whose extent is its own parent's is walked once, not for ever",
      rc == 0 and "A.TXT" in out and time.time() - t0 < 20, (out, err))


def split(gap):
    def fn(d):
        r = root(d)
        sub = [rec for rec in isodir(d, r) if rec[33:33 + rec[32]] == b"SUB"][0]
        se = struct.unpack("<I", sub[2:6])[0]
        recs = isodir(d, se)
        for k, rec in enumerate(recs):
            if rec[33:33 + rec[32]] == b"B.TXT;1":
                e, n = struct.unpack("<I", rec[2:6])[0], struct.unpack("<I", rec[10:14])[0]
                one, two = bytearray(rec), bytearray(rec)
                one[25] |= 0x80
                one[10:18] = struct.pack("<I", 4096) + struct.pack(">I", 4096)
                two[2:10] = struct.pack("<I", e + 2 + gap) + struct.pack(">I", e + 2 + gap)
                two[10:18] = struct.pack("<I", n - 4096) + struct.pack(">I", n - 4096)
                recs[k:k + 1] = [one, two]
                break
        isoput(d, se, recs)
    return fn


out, err, rc = hb("archive open i %s\narchive cat i SUB/B.TXT | cmp - %s && echo same\n"
                  "archive open j %s\narchive cat j SUB/B.TXT"
                  % (patch(PL, "multi.iso", split(0)), want, patch(PL, "apart.iso", split(1))))
check("a file recorded in pieces reads whole when they lie together, and is refused by "
      "name when they do not", out == "same\n" and rc != 0 and "in pieces" in err, (out, err))

hsrc = os.path.join(D, "hsrc")
os.makedirs(hsrc)
open(os.path.join(hsrc, "XY"), "w").write("bad")
os.symlink("/etc/passwd", os.path.join(hsrc, "pw"))
hi = os.path.join(D, "hostile.iso")
subprocess.run(["bsdtar", "--format", "iso9660", "-cf", hi, "XY"], cwd=hsrc, check=True)
dd = patch(hi, "dotdot.iso", lambda d: d.__setitem__(slice(None), d.replace(b"NM\x07\x01\x00XY", b"NM\x07\x01\x00..")))
li = os.path.join(D, "link.iso")
subprocess.run(["bsdtar", "--format", "iso9660", "-cf", li, "pw"], cwd=hsrc, check=True)
for img, why, what in ((dd, "would land outside", "a Rock Ridge name of .."),
                       (li, "links out", "a Rock Ridge link to /etc/passwd")):
    box = os.path.join(D, "ibox-" + os.path.basename(img))
    os.makedirs(os.path.join(box, "inner"))
    out, err, rc = hb("archive open a %s\narchive extract a %s" % (img, os.path.join(box, "inner")))
    check("a disc image holding %s refuses the whole extraction, nothing written" % what,
          rc != 0 and why in err and os.listdir(os.path.join(box, "inner")) == [] and
          os.listdir(box) == ["inner"], (out, err))

# A bounded fuzz of the three new readers: valid images mutated where their
# headers, directories and lengths are, and cut short, each opened, listed,
# read and extracted into a folder of its own. What must never happen is a
# signal, a hang, a sanitizer report, or a byte written outside the folder.
SEED = int(os.environ.get("SEED", "488"))
rng = random.Random(SEED)
small = os.path.join(D, "fsrc")
os.makedirs(os.path.join(small, "d"))
open(os.path.join(small, "a.txt"), "w").write("tiny\n" * 50)
open(os.path.join(small, "d", "b.txt"), "w").write("b\n" * 3000)
os.symlink("a.txt", os.path.join(small, "l"))
fz = os.path.join(D, "fz.zip")
subprocess.run(["bsdtar", "--format", "zip", "-cf", fz, "a.txt", "d", "l"], cwd=small, check=True)
fi = os.path.join(D, "fz.iso")
subprocess.run(["bsdtar", "--format", "iso9660", "-cf", fi, "a.txt", "d", "l"], cwd=small, check=True)
fzd, fid = open(fz, "rb").read(), open(fi, "rb").read()
fbin = rawbin(fid[:40 * 2048], 1)
ISOREG = [(32768, 32768 + 2048 * 2), (32768 + 2048 * 2, 40 * 2048)]


def zipreg(d):
    return [(0, 80), (max(0, len(d) - 400), len(d))]


KINDS = [("zip", fzd, zipreg(fzd), None),
         ("iso", fid, ISOREG, None),
         ("bin", fbin, [(0, 16), (16 * 2352, 22 * 2352)], None),
         ("cue", fbin, [(0, 60)], 'FILE "in.bin" BINARY\n TRACK 01 MODE1/2352\n  INDEX 01 00:00:00\n')]
for kind, path in (("zip64", z64), ("cp437", cp), ("joliet", JO), ("plain", PL), ("longrr", RJ)):
    data = open(path, "rb").read()
    KINDS.append((kind, data, zipreg(data) if path.endswith(".zip") else ISOREG, None))


def mutate(data, regions):
    b = bytearray(data)
    for _ in range(rng.randint(1, 6)):
        lo, hi = rng.choice(regions)
        hi = min(hi, len(b))
        if hi <= lo:
            continue
        i = rng.randrange(lo, hi)
        k = rng.random()
        if k < 0.35:
            b[i] = rng.randrange(256)
        elif k < 0.55:
            b[i:i + 4] = b"\xff\xff\xff\xff"[:len(b[i:i + 4])]
        elif k < 0.7:
            b[i:i + 4] = bytes(len(b[i:i + 4]))
        elif k < 0.92:
            b[i] ^= 1 << rng.randrange(8)
        else:
            del b[i:]
    return bytes(b)


FZ = os.path.join(D, "fuzz")
cases, crashed, outside, opened = [], [], [], {}
for kind, data, regions, cue in KINDS:
    for n in range(40):
        c = os.path.join(FZ, "%s%02d" % (kind, n))
        os.makedirs(os.path.join(c, "box"))
        if cue:
            text = bytearray(cue.encode())
            for _ in range(rng.randint(0, 3)):
                text[rng.randrange(len(text))] = rng.choice(b' "0123:\nMODE/AUDIO')
            open(os.path.join(c, "in.bin"), "wb").write(mutate(data, regions) if rng.random() < 0.5 else data)
            open(os.path.join(c, "in.cue"), "wb").write(bytes(text))
            f = os.path.join(c, "in.cue")
        else:
            open(os.path.join(c, "in.bin"), "wb").write(mutate(data, regions))
            f = os.path.join(c, "in.bin")
        cases.append((kind, c, f))
for b0 in range(0, len(cases), 20):
    batch = cases[b0:b0 + 20]
    script = "".join(
        "if archive open f%d %s 2> /dev/null; then echo open%d; archive ls f%d > /dev/null 2>&1\n"
        "r := archive ls f%d d 2> /dev/null; archive cat f%d a.txt > /dev/null 2>&1\n"
        "archive cat f%d D/B.TXT > /dev/null 2>&1; archive extract f%d %s/box 2> /dev/null\n"
        "archive close f%d; fi; echo done%d\n" % (i, f, i, i, i, i, i, i, c, i, i)
        for i, (kind, c, f) in enumerate(batch))
    try:
        out, err, rc = hb(script)
    except subprocess.TimeoutExpired:
        out, err, rc = "", "timed out", -1
    if rc < 0 or "done%d" % (len(batch) - 1) not in out or "Sanitizer" in err or "runtime error" in err:
        crashed.append((batch[0][2], rc, err[-400:]))
    for i, (kind, c, f) in enumerate(batch):
        if "open%d\n" % i in out:
            opened[kind] = opened.get(kind, 0) + 1
        if sorted(os.listdir(c)) not in (["box", "in.bin"], ["box", "in.bin", "in.cue"]):
            outside.append((f, os.listdir(c)))
check("%d malformed zips, disc images and CUE sheets: none crashed, hung or tripped "
      "the sanitizers (seed %d)" % (len(cases), SEED), not crashed, crashed)
check("and none wrote anything outside the folder it was extracted into", not outside, outside)
check("and a share of each kind still opened, so the walkers themselves were reached",
      all(opened.get(k, 0) >= 6 for k, *_ in KINDS), opened)

shutil.rmtree(D, True)
report(65)
