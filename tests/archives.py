#!/usr/bin/env python3
"""The archives extension: an archive in Files is a folder, and Compress,
Extract Here and Extract To (Gitea #475, ADR 0053).

Each format goes through what really reads it here: the archive module for
tar and tar.gz, and for the rest whichever of bsdtar, unzip or 7z is on the
path, narrowed by a folder of links so each is the only one found. Hostile
archives are built by hand, since no tool will write one.
"""
import io, os, shutil, subprocess, sys, tarfile, tempfile, time, zipfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import screen as sx
from screen import Term, check, report, tree, load, press, release, expect

if len(sys.argv) > 1:
    sx.HIBR = os.path.abspath(sys.argv[1])

PLAN = 76
D = tempfile.mkdtemp(prefix="hibr-arc-")
W = os.path.join(D, "work")
CACHE = os.path.join(D, "cache")
os.makedirs(W)
SRC = os.path.join(D, "src")
os.makedirs(os.path.join(SRC, "proj", "sub"))
open(os.path.join(SRC, "proj", "README.md"), "w").write("hello\n")
open(os.path.join(SRC, "proj", "sub", "note one.txt"), "w").write("inside\n")


def mk(name, *what, cwd=SRC):
    subprocess.run(["bsdtar", "-a", "-cf", os.path.join(W, name)] + list(what),
                   cwd=cwd, check=True)


def fresh():
    """An empty working folder holding nothing left by the last check."""
    shutil.rmtree(W)
    os.makedirs(W)


def bindir(*names):
    """A folder of links to only the named programs, plus what any session
    needs, for a PATH on which those alone are found."""
    b = tempfile.mkdtemp(prefix="bin", dir=D)
    for n in list(names) + ["awk", "cat", "env", "find", "mkdir", "mv", "readlink", "rm", "sh"]:
        p = shutil.which(n)
        if p:
            os.symlink(p, os.path.join(b, n))
    return b


def session(body, env=None, rows=24, cols=80, pre="", extension=True):
    sess = os.path.join(D, "session.hibr")
    ext = ". %s\n. %s\n" % (tree("programs/desktop/apps/archives.hibr"),
                            tree("programs/desktop/control-panel/archiveset.hibr"))
    open(sess, "w").write(
        "%s. %s\n. %s\n. %s\n%s%s\ndt_open\n%s\ndt_run\ndt_close\n"
        % (load("console"), tree("programs/desktop/desktop.hibr"),
           tree("programs/desktop/apps/files.hibr"),
           tree("programs/desktop/system/panel.hibr"),
           ext if extension else "", pre, body))
    e = {"XDG_CACHE_HOME": CACHE}
    e.update(env or {})
    return Term(sess, env=e, settle=1.0, rows=rows, cols=cols)


def files(where=W):
    return 'dt_new "Files" 20 70 2 2 files "%s"' % where



OPENED = os.path.join(D, "opened")
STUB = ("dt_openfile() { { printf '%%s\\n' \"$1\"; cat -- \"$1\"; } > %s; }\n" % OPENED)


def opened():
    try:
        return open(OPENED).read()
    except OSError:
        return ""


def right(t, text):
    """Right-click an entry, and answer the menu that opens."""
    sc = t.until(text)
    at = sc.find(text)
    t.keys([press(at[0], at[1] + 1, 2), release(at[0], at[1] + 1, 2)], settle=0.3)
    return t.until("Open")


def choose(t, sc, text):
    """Click a row of an open menu."""
    at = sc.find(text)
    t.keys([press(at[0], at[1] + 1), release(at[0], at[1] + 1)], settle=0.3)


def ls(*p):
    return sorted(n for n in os.listdir(os.path.join(W, *p)))


def waitfor(cond, secs=10):
    end = time.time() + secs * sx.SLOW if hasattr(sx, "SLOW") else time.time() + secs
    while time.time() < end:
        if cond():
            return True
        time.sleep(0.05)
    return cond()


def into(t, name):
    """Move the cursor to an entry and press enter on it."""
    sc = t.until(name)
    at = sc.find(name)
    t.keys([b"\x1b[B"] * (at[0] - 3) + [b"\r"], settle=0.3)


def browse(name, env=None, why=""):
    """Go into an archive, into its folder, open a member, and back out."""
    if os.path.exists(OPENED):
        os.unlink(OPENED)
    t = session(files(), env=env, pre=STUB)
    into(t, name)
    sc = t.until("proj")
    check("%s opens as a folder%s" % (name, why),
          sc.find(name + ":/") is not None and sc.find("proj") is not None, sc)
    t.keys([b"\r"], settle=0.3)
    sc = t.until("README.md")
    check("%s: its folders go in" % name,
          sc.find("sub") is not None and sc.find("README.md") is not None, sc)
    down = [b"\x1b[B"] if sc.find("sub")[0] < sc.find("README.md")[0] else []
    t.keys(down + [b"\r"], settle=0.3)
    waitfor(lambda: "hello" in opened())
    check("%s: a member opens as a copy of itself" % name,
          opened().endswith("README.md\nhello\n"), opened())
    t.quit(None, 0.3)


# --- browsing, through each thing that can read -----------------------------
mk("demo.tar", "proj")
mk("demo.tar.gz", "proj")
mk("demo.tgz", "proj")
mk("demo.tar.xz", "proj")
mk("demo.tar.bz2", "proj")
mk("demo.tar.zst", "proj")
mk("demo.zip", "proj")
mk("demo.7z", "proj")
ONLYMOD = {"PATH": bindir()}
ONLYUNZIP = {"PATH": bindir("unzip")}
for n in ("demo.tar", "demo.tar.gz", "demo.tgz"):
    browse(n, ONLYMOD, ", with no program at all")
for n in ("demo.tar.xz", "demo.tar.bz2", "demo.tar.zst", "demo.zip", "demo.7z"):
    browse(n, None, " through bsdtar")
browse("demo.zip", ONLYUNZIP, " through unzip")

t = session(files(), env=ONLYMOD, pre=STUB)
into(t, "demo.zip")
sc = t.until("demo.tar", tries=5)
t.quit(None, 0.3)
check("a format with no program here is not shown as a folder",
      sc.find("demo.zip:/") is None, sc)
t = session(files(), env=ONLYMOD, pre=STUB)
sc = right(t, "demo.zip")
check("and it offers neither Extract nor Show Contents",
      sc.find("Extract Here") is None and sc.find("Compress") is not None, sc)
t.quit(None, 0.3)

t = session(files())
into(t, "demo.7z")
t.until("proj")
t.keys([b"\x7f"], settle=0.3)
sc = t.until(lambda s: s.find("demo.tar.gz") is not None)
check("backspace at an archive's top goes back to the folder it is in",
      sc.find("demo.7z:/") is None and sc.find("demo.tar.gz") is not None, sc)
t.quit(None, 0.3)

t = session(files('archive://%s/demo.tar.gz/proj' % W))
sc = t.until("README.md")
check("a window can open straight into one", sc.find("sub") is not None, sc)
sc = right(t, "README.md")
rename = sc.find("Rename")
check("an archive can only be read: Rename is dimmed where Copy is not",
      rename is not None and sc.style(*rename) != sc.style(*sc.find("Copy")), sc)
check("and offers nothing that needs the file on this machine",
      sc.find("Compress") is None and sc.find("Extract") is None, sc)
t.quit(None, 0.3)

# Copying out is a copy, keeps the name, and never overwrites.
fresh()
mk("demo.tar.gz", "proj")
mk("demo.zip", "proj")
body = ('dt_fileop copy "%s" "archive://%s/demo.tar.gz/proj/README.md" '
        '"archive://%s/demo.tar.gz/proj/sub"\n' % (W, W, W))
t = session(files() + "\n" + body)
sc = t.until("Copied to", tries=150)
check("copying out of an archive opens the copy window", sc.find("Copied to") is not None, sc)
waitfor(lambda: "README.md" in ls() and "sub" in ls())
sc = t.until("README.md")
check("a member and a folder copy out of an archive, and the window shows them",
      open(os.path.join(W, "README.md")).read() == "hello\n" and
      ls("sub") == ["note one.txt"] and sc.find("README.md") is not None, ls())
t.quit(None, 0.3)
t = session(files() + "\n" + body.replace("demo.tar.gz", "demo.zip"))
sc = t.until("already exists", tries=150)
check("and a name already there is asked about, in the copy window",
      sc.find("Replace") is not None and sc.find("goes to the trash") is not None, sc)
t.keys([b"\x1b"], settle=0.3)
t.quit(None, 0.3)
check("with nothing hidden left behind",
      not [n for n in ls() if n.startswith(".")], ls())

# --- extracting -------------------------------------------------------------
def extract(name, label="Extract Here", env=None, pre="", until=None, see=None):
    """Run a verb on an entry, wait for what it makes and for what the
    desktop says about it, and answer that screen."""
    t = session(files(), env=env, pre=pre)
    sc = right(t, name)
    choose(t, sc, label)
    ok = waitfor(until) if until else None
    sc = t.until(see, tries=150) if see else t.screen()
    t.quit(None, 0.3)
    return sc, ok


fresh()
mk("demo.tar.gz", "proj")
sc, ok = extract("demo.tar.gz", until=lambda: "proj" in ls())
check("Extract Here keeps a single top folder as it is",
      ok and ls("proj") == ["README.md", "sub"], ls())
sc, ok = extract("demo.tar.gz", until=lambda: "proj 2" in ls())
check("and a taken name gets a 2, with the first left alone",
      ok and ls("proj 2") == ["README.md", "sub"] and ls("proj") == ["README.md", "sub"], ls())
check("nothing hidden is left beside it", not [n for n in ls() if n.startswith(".")], ls())

fresh()
os.makedirs(os.path.join(D, "flat"), exist_ok=True)
open(os.path.join(D, "flat", "a.txt"), "w").write("a\n")
open(os.path.join(D, "flat", "b.txt"), "w").write("b\n")
mk("flat.zip", "a.txt", "b.txt", cwd=os.path.join(D, "flat"))
sc, ok = extract("flat.zip", until=lambda: "flat" in ls())
check("anything else goes into a new folder named after the archive",
      ok and ls("flat") == ["a.txt", "b.txt"] and "a.txt" not in ls(), ls())
fresh()
mk("one.tar.xz", "a.txt", cwd=os.path.join(D, "flat"))
sc, ok = extract("one.tar.xz", until=lambda: "one" in ls())
check("a single file is not a single folder", ok and ls("one") == ["a.txt"], ls())

fresh()
mk("flat.zip", "a.txt", "b.txt", cwd=os.path.join(D, "flat"))
sc, ok = extract("flat.zip", env=ONLYUNZIP, until=lambda: "flat" in ls())
check("unzip extracts it the same way", ok and ls("flat") == ["a.txt", "b.txt"], ls())

fresh()
mk("demo.tar.gz", "proj")
sc, ok = extract("demo.tar.gz", pre="DT_ARCFOLDER=always\n", until=lambda: "demo" in ls())
check("with New Folder chosen, a single folder goes inside one too",
      ok and ls("demo") == ["proj"], ls())

fresh()
mk("demo.tar.gz", "proj")
sc, ok = extract("demo.tar.gz", pre="DT_ARCOPEN=1\n", until=lambda: "proj" in ls(),
                 see="Files [proj]")
check("and Open Folder Afterwards opens it",
      ok and sc.find("Files [proj]") is not None, sc)

fresh()
mk("demo.tar.gz", "proj")
t = session(files())
sc = right(t, "demo.tar.gz")
choose(t, sc, "Extract To...")
sc = t.until("Extract To")
t.keys([b"\x7f" * 4, b"elsewhere", b"\r"], settle=0.3)
waitfor(lambda: "demoelsewhere" in ls() or "elsewhere" in ls())
t.until("Extracted", tries=150)
t.quit(None, 0.3)
got = [n for n in ls() if n.endswith("elsewhere")]
check("Extract To makes the folder the dialog names, with what the archive holds",
      len(got) == 1 and ls(got[0]) == ["proj"], ls())

# --- hostile archives -------------------------------------------------------
def climbing_zip(path):
    with zipfile.ZipFile(path, "w") as z:
        z.writestr("good.txt", "fine\n")
        z.writestr("../escaped.txt", "bad\n")


def climbing_tar(path):
    with tarfile.open(path, "w:gz") as a:
        i = tarfile.TarInfo("good.txt")
        i.size = 5
        a.addfile(i, io.BytesIO(b"fine\n"))
        i = tarfile.TarInfo("../escaped.txt")
        i.size = 4
        a.addfile(i, io.BytesIO(b"bad\n"))


def link_then_member(path, target):
    """A link that leads out, and a member written through it."""
    with tarfile.open(path, "w:xz") as a:
        i = tarfile.TarInfo("d")
        i.type = tarfile.SYMTYPE
        i.linkname = target
        a.addfile(i)
        i = tarfile.TarInfo("d/through.txt")
        i.size = 4
        a.addfile(i, io.BytesIO(b"bad\n"))


def link_out(path, target):
    with tarfile.open(path, "w:xz") as a:
        i = tarfile.TarInfo("ok.txt")
        i.size = 3
        a.addfile(i, io.BytesIO(b"ok\n"))
        i = tarfile.TarInfo("out")
        i.type = tarfile.SYMTYPE
        i.linkname = target
        a.addfile(i)


OUT = os.path.join(D, "outside")
os.makedirs(OUT)


def refused(name, env=None, what=""):
    sc, _ = extract(name, env=env, see=name + ":")
    left = [n for n in ls() if n != name]
    check("%s%s is refused whole, with nothing written" % (name, what),
          sc.find(name + ":") is not None and left == [] and not os.path.exists(os.path.join(D, "escaped.txt")) and
          os.listdir(OUT) == [], (left, sc))
    return sc


fresh()
climbing_zip(os.path.join(W, "climb.zip"))
refused("climb.zip", what=" through bsdtar")
refused("climb.zip", ONLYUNZIP, " through unzip")
fresh()
climbing_tar(os.path.join(W, "climb.tar.gz"))
sc = refused("climb.tar.gz", what=" through the module")
fresh()
link_then_member(os.path.join(W, "through.tar.xz"), OUT)
refused("through.tar.xz", what=", a link and a member through it,")
fresh()
with tarfile.open(os.path.join(W, "late.tar.gz"), "w:gz") as a:
    i = tarfile.TarInfo("sub/")
    i.type = tarfile.DIRTYPE
    a.addfile(i)
    i = tarfile.TarInfo("d")
    i.type = tarfile.SYMTYPE
    i.linkname = "sub"
    a.addfile(i)
    i = tarfile.TarInfo("d/x")
    i.size = 2
    a.addfile(i, io.BytesIO(b"x\n"))
refused("late.tar.gz", what=", whose link the module only finds it cannot make late,")
fresh()
link_out(os.path.join(W, "linkout.tar.xz"), OUT)
refused("linkout.tar.xz", what=", holding a link that leads out,")

fresh()
climbing_zip(os.path.join(W, "climb.zip"))
t = session(files('archive://%s/climb.zip' % W), env=ONLYUNZIP)
sc = t.until("good.txt")
check("and browsing one lists what is safe and hides the name that climbs",
      sc.find("good.txt") is not None and sc.find("escaped") is None, sc)
t.quit(None, 0.3)
t = session(files('archive://%s/climb.zip' % W))
sc = t.until("good.txt")
check("bsdtar's list has already dropped the climbing part of the name",
      sc.find("..") is None and sc.find("good.txt") is not None, sc)
t.quit(None, 0.3)

fresh()
open(os.path.join(W, "broken.zip"), "w").write("this is no archive\n")
t = session(files())
t.until("broken.zip")
t.keys([b"\r"], settle=0.3)
sc = t.until(lambda s: s.find("broken.zip:") is not None and s.find("broken.zip:/") is None,
             tries=150)
t.quit(None, 0.3)
check("what cannot be read says why and leaves the window where the file is",
      sc.find("broken.zip:/") is None and sc.find("broken.zip") is not None, sc)

# --- compressing ------------------------------------------------------------
fresh()
os.makedirs(os.path.join(W, "folder"))
open(os.path.join(W, "folder", "x.txt"), "w").write("x\n")
open(os.path.join(W, "-dash.txt"), "w").write("d\n")
sc, ok = extract("folder", "Compress", until=lambda: "folder.tar.gz" in ls())
names = subprocess.run(["bsdtar", "-tf", os.path.join(W, "folder.tar.gz")],
                       capture_output=True, text=True).stdout.split("\n")
check("Compress makes NAME.tar.gz beside it by default, with no program",
      ok and "folder/x.txt" in names, (ls(), names))
sc, ok = extract("folder", "Compress", until=lambda: "folder 2.tar.gz" in ls())
check("and a taken name gets a 2 before the extension", ok, ls())
sc, ok = extract("-dash.txt", "Compress", until=lambda: "-dash.txt.tar.gz" in ls())
check("a name starting with a dash is a name", ok, ls())
for fmt in ("zip", "7z", "tar.xz", "tar.bz2", "tar.zst", "tar"):
    sc, ok = extract("folder", "Compress", pre="DT_ARCFORMAT=%s\n" % fmt,
                     until=lambda: "folder." + fmt in ls())
    names = subprocess.run(["bsdtar", "-tf", os.path.join(W, "folder." + fmt)],
                           capture_output=True, text=True).stdout
    check("Compress writes %s" % fmt, ok and "folder/x.txt" in names, (ls(), names))
for n in list(ls()):
    if n.startswith("folder."):
        os.unlink(os.path.join(W, n))
sc, ok = extract("folder", "Compress", env={"PATH": bindir("zip")},
                 pre="DT_ARCFORMAT=zip\n", until=lambda: "folder.zip" in ls())
check("zip writes it when bsdtar is not here", ok, ls())
os.unlink(os.path.join(W, "folder.zip"))
sc, ok = extract("folder", "Compress", env=ONLYMOD, pre="DT_ARCFORMAT=7z\n",
                 until=lambda: "folder.tar.gz" in ls())
check("and a format whose program has gone falls back to tar.gz", ok, ls())
check("nothing hidden is left by any of them",
      not [n for n in ls() if n.startswith(".")], ls())

fresh()
open(os.path.join(W, "a.txt"), "w").write("a\n")
open(os.path.join(W, "b.txt"), "w").write("b\n")
t = session(files())
sc = t.until("b.txt")
t.keys([b" ", b" "], settle=0.3)
sc = right(t, "b.txt")
choose(t, sc, "Compress")
ok = waitfor(lambda: "Archive.tar.gz" in ls())
t.until("Made", tries=150)
t.quit(None, 0.3)
names = subprocess.run(["bsdtar", "-tf", os.path.join(W, "Archive.tar.gz")],
                       capture_output=True, text=True).stdout if ok else ""
check("several selected make one Archive holding each", ok and
      "a.txt" in names and "b.txt" in names, (ls(), names))

# --- no holes ---------------------------------------------------------------
fresh()
mk("demo.tar.gz", "proj")
open(os.path.join(W, "note.gz"), "w").write("x")
t = session(files(), pre=STUB)
sc = right(t, "note.gz")
check("a .gz that is not a tar is no archive",
      sc.find("Extract Here") is None and sc.find("Compress") is not None, sc)
t.quit(None, 0.3)

t = session(files(), pre=STUB, extension=False)
sc = right(t, "demo.tar.gz")
check("without the extension an archive is a plain file",
      sc.find("Extract") is None and sc.find("Compress") is None and
      sc.find("Show Contents") is None, sc)
t.quit(None, 0.3)

t = session('unset DT_APPS["archives"]\n' + files(), pre=STUB)
sc = right(t, "demo.tar.gz")
check("and taking it away takes every verb at once",
      sc.find("Extract") is None and sc.find("Compress") is None, sc)
t.keys([b"\x1b"], settle=0.2)
t.keys([b"\r"], settle=0.3)
t.collect(0.3)
sc = t.screen()
t.quit(None, 0.3)
check("and going in with it", sc.find("demo.tar.gz:/") is None, sc)

# --- the Control Panel ------------------------------------------------------
PANE = 'dt_new "Control Panel" 20 72 1 1 panel archiveset'
t = session(PANE, env={"PATH": bindir("bsdtar")})
sc = t.until("Compress To")
check("the pane has its three rows",
      sc.find("Extract Here Makes") is not None and
      sc.find("Open Folder Afterwards") is not None and
      sc.find("tar.gz") is not None and sc.find("Smart Folder") is not None, sc)
at = sc.find("Compress To")
t.keys([press(at[0], at[1] + 1), release(at[0], at[1] + 1)], settle=0.3)
t.keys([b"\x1b[C"], settle=0.3)
t.keys([b"\x1b[C"], settle=0.3)
sc = t.until("zip")
check("and Compress To steps through what can be written", sc.find("zip") is not None, sc)
t.quit(None, 0.3)
t = session(PANE, env=ONLYMOD)
sc = t.until("Compress To")
at = sc.find("Compress To")
t.keys([press(at[0], at[1] + 1), release(at[0], at[1] + 1)], settle=0.3)
t.keys([b"\x1b[C"], settle=0.3)
t.keys([b"\x1b[C"], settle=0.3)
t.collect(0.3)
sc = t.screen()
t.quit(None, 0.3)
check("with no program it offers only what the module writes",
      sc.find("zip") is None and sc.find("7z") is None, sc)

# --- a 7z program, where there is one ---------------------------------------
SEVEN = next((n for n in ("7z", "7zz", "7za") if shutil.which(n)), None)
CHECKS = 0
if SEVEN:
    ONLY7 = {"PATH": bindir(SEVEN)}
    fresh()
    mk("demo.7z", "proj")
    browse("demo.7z", ONLY7, " through " + SEVEN)
    sc, ok = extract("demo.7z", env=ONLY7, until=lambda: "proj" in ls())
    check("7z extracts", ok and ls("proj") == ["README.md", "sub"], ls())
    fresh()
    with tarfile.open(os.path.join(D, "l.tar"), "w") as a:
        i = tarfile.TarInfo("ln")
        i.type = tarfile.SYMTYPE
        i.linkname = "x"
        a.addfile(i)
    subprocess.run(["bsdtar", "-a", "-cf", os.path.join(W, "held.7z"),
                    "@" + os.path.join(D, "l.tar")], check=True)
    refused("held.7z", ONLY7, ", holding any link at all through 7z,")
    os.makedirs(os.path.join(W, "folder"))
    open(os.path.join(W, "folder", "x.txt"), "w").write("x\n")
    sc, ok = extract("folder", "Compress", env=ONLY7, pre="DT_ARCFORMAT=7z\n",
                     until=lambda: "folder.7z" in ls())
    names = subprocess.run(["bsdtar", "-tf", os.path.join(W, "folder.7z")],
                           capture_output=True, text=True).stdout
    check("and 7z writes it when bsdtar is not here", ok and "folder/x.txt" in names, (ls(), names))
    CHECKS = 6

shutil.rmtree(D, ignore_errors=True)
report(PLAN + CHECKS)
