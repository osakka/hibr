#!/usr/bin/env python3
"""The catalogue: examples/catalogue.hibr, against a stand-in index.

    python3 tests/appmgr.py [path-to-hibr]

ADR 0041 is the manager's specification, and this suite is the half of it
that can be checked without a desktop: fetching an index, verifying a
bundle, placing an app where a scan already looks, and refusing what must
be refused.

The server is `tests/davserve.py --auth none`, which already serves a
folder of files over plain HTTP with its port on stdout -- a fifth stand-in
server was not needed. Everything the index describes is built here with
Python's own `tarfile` and `hashlib`, so our writer is never on both sides
of a check.

What is refused, each with a bundle made to do it:

  - a `sha256` that does not match what arrived;
  - a member whose path is absolute, or climbs out with `..`;
  - a bundle carrying a `.so`, refused with that word;
  - an index entry naming a kind that is not one of the desktop's five.

And what must keep working: a single `.hibr` file is its own bundle, which
is most apps; a multi-file bundle puts the app at `<kind>/NAME.hibr` and its
resources under `<kind>/NAME/`, because `dt_apps` would otherwise give it a
submenu of its own; and a file of the person's own in the *config* folder
still shadows a fetched app of the same name.
"""
import hashlib
import json
import re
import os
import shutil
import subprocess
import sys
import tarfile
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import screen as sx
from screen import check, report, tree

if len(sys.argv) > 1:
    sx.HIBR = os.path.abspath(sys.argv[1])

D = tempfile.mkdtemp(prefix="hibr-appmgr-")
WWW = os.path.join(D, "www")
HOME = os.path.join(D, "home")
os.makedirs(WWW)
os.makedirs(HOME)
APPS = tree("examples/catalogue.hibr")

# This machine as the catalogue spells it, and the shell's own module ABI
# -- both read from the shell under test rather than written down, since
# a number here would go stale the first time either moved.
ARCH = subprocess.run([sx.HIBR, "-c",
                       'u := uname -sm; u=${u,,}; echo "${u% *}-${u#* }"'],
                      capture_output=True, text=True).stdout.strip()
ABI = subprocess.run([sx.HIBR, "-c", 'echo "$HIBR_ABI"'],
                     capture_output=True, text=True).stdout.strip()


def sha(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        h.update(f.read())
    return h.hexdigest()


def put(name, data):
    """A file in the served folder; its sha256 back."""
    p = os.path.join(WWW, name)
    os.makedirs(os.path.dirname(p), exist_ok=True)
    with open(p, "wb") as f:
        f.write(data if isinstance(data, bytes) else data.encode())
    return sha(p)


def tar(name, members):
    """A .tar.gz in the served folder from {path: bytes}; its sha256 back.

    Paths are written exactly as given, so a hostile one -- absolute, or
    climbing with `..` -- is really in the tarball rather than described."""
    p = os.path.join(WWW, name)
    os.makedirs(os.path.dirname(p), exist_ok=True)
    with tarfile.open(p, "w:gz") as t:
        for path, body in members.items():
            info = tarfile.TarInfo(path)
            info.size = len(body)
            t.addfile(info, __import__("io").BytesIO(body))
    return sha(p)


APP = (b"# demo -- a one-file app, for the manager's own suite.\n"
       b"command -v dt_app > /dev/null && dt_app demo \"Demo\" 8 30 once \"D\"\n"
       b"fn demo_draw(id, h, w, row, col) { console put -p \"w$id\" 1 2 hello; }\n")
APP2 = APP.replace(b"hello", b"hello again")

h_one = put("bundles/demo.hibr", APP)
h_one2 = put("bundles/demo-2.hibr", APP2)
h_multi = tar("bundles/multi.tar.gz", {
    "app.json": json.dumps({"app": "multi.hibr", "kind": "apps"}).encode(),
    "multi.hibr": APP.replace(b"demo", b"multi"),
    "art/logo.txt": b"a resource\n",
})
# An app that brings a Control Panel pane of its own, which is what eight of
# the twenty-one real ones do (Gitea #217): the pane is a file in another of
# the desktop's scan roots, so it cannot be a resource under the app's own.
PANE = (b"#!/usr/bin/env hibr\n#\n# paneof -- the Control Panel pane for it.\n"
        b"command -v cp_pane > /dev/null && cp_pane paneof \"Pane\" \"P\" app\n")
h_parts = tar("bundles/parts.tar.gz", {
    "app.json": json.dumps({"app": "parts.hibr", "kind": "apps", "parts": [
        {"file": "paneof.hibr", "kind": "control-panel", "name": "paneof"}]}).encode(),
    "parts.hibr": APP.replace(b"demo", b"parts"),
    "paneof.hibr": PANE,
})
# A second app bringing the *same* pane -- one pane serves Calendar and
# Contacts both, so removing one of the two must leave the other's alone.
h_parts2 = tar("bundles/parts2.tar.gz", {
    "app.json": json.dumps({"app": "parts2.hibr", "kind": "apps", "parts": [
        {"file": "paneof.hibr", "kind": "control-panel", "name": "paneof"}]}).encode(),
    "parts2.hibr": APP.replace(b"demo", b"parts2"),
    "paneof.hibr": PANE,
})
h_badpart = tar("bundles/badpart.tar.gz", {
    "app.json": json.dumps({"app": "badpart.hibr", "kind": "apps", "parts": [
        {"file": "paneof.hibr", "kind": "etc", "name": "paneof"}]}).encode(),
    "badpart.hibr": APP.replace(b"demo", b"badpart"),
    "paneof.hibr": PANE,
})
h_climbpart = tar("bundles/climbpart.tar.gz", {
    "app.json": json.dumps({"app": "climbpart.hibr", "kind": "apps", "parts": [
        {"file": "paneof.hibr", "kind": "control-panel", "name": "../../escaped"}]}).encode(),
    "climbpart.hibr": APP.replace(b"demo", b"climbpart"),
    "paneof.hibr": PANE,
})
h_nopart = tar("bundles/nopart.tar.gz", {
    "app.json": json.dumps({"app": "nopart.hibr", "kind": "apps", "parts": [
        {"file": "missing.hibr", "kind": "control-panel", "name": "paneof"}]}).encode(),
    "nopart.hibr": APP.replace(b"demo", b"nopart"),
})
# The three kinds that are data rather than scripts (Gitea #224). A theme
# is JSON, and one with a picture is **two files that land beside each
# other** -- `cp_themeread`'s own `image` reads it from the themes folder --
# which is why a part names a filename now rather than a name with .hibr
# appended.
THEME = json.dumps({"title": "Under Test", "about": "a theme the suite made",
                    "colours": "midnight"}).encode()
SCHEME = json.dumps({"title": "Under Test", "about": "a scheme the suite made",
                     "wall": "#101010", "dot": "#202020", "bar": "#151515",
                     "active": "#63b3ed", "idle": "#4a5568", "face": "#101820",
                     "ink": "#cbd5e0"}).encode()
h_theme = put("bundles/plain.json", THEME)
h_scheme = put("bundles/scheme.json", SCHEME)
h_picture = tar("bundles/picture.tar.gz", {
    "app.json": json.dumps({"app": "picture.json", "kind": "themes", "parts": [
        {"file": "shot.png", "kind": "themes", "as": "picture.png"}]}).encode(),
    "picture.json": json.dumps({"title": "With A Picture", "about": "and its own image",
                                "colours": "midnight", "image": "picture.png"}).encode(),
    "shot.png": b"\x89PNG\r\n\x1a\nnot really a png, and nothing here decodes it\n",
})
h_climb = tar("bundles/climb.tar.gz", {"../escaped.hibr": APP})
h_abs = tar("bundles/abs.tar.gz", {"/etc/hibr-escaped.hibr": APP})
h_so = tar("bundles/mod.tar.gz", {"mod.hibr": APP, "evil.so": b"\x7fELF fake\n"})

INDEX = {"apps": {
    "demo": {"title": "Demo", "version": "1.0", "about": "a one-file app",
             "author": "the suite", "licence": "MIT", "kind": "apps",
             "bundle": "bundles/demo.hibr", "sha256": h_one},
    "multi": {"title": "Multi", "version": "1.0", "about": "an app with a resource",
              "kind": "apps", "bundle": "bundles/multi.tar.gz", "sha256": h_multi},
    "bad": {"title": "Bad Hash", "version": "1.0", "kind": "apps",
            "bundle": "bundles/demo.hibr", "sha256": "0" * 64},
    "climb": {"title": "Climber", "version": "1.0", "kind": "apps",
              "bundle": "bundles/climb.tar.gz", "sha256": h_climb},
    "abs": {"title": "Absolute", "version": "1.0", "kind": "apps",
            "bundle": "bundles/abs.tar.gz", "sha256": h_abs},
    "mod": {"title": "Module", "version": "1.0", "kind": "apps",
            "bundle": "bundles/mod.tar.gz", "sha256": h_so},
    "wrongkind": {"title": "Wrong Kind", "version": "1.0", "kind": "etc",
                  "bundle": "bundles/demo.hibr", "sha256": h_one},
    "parts": {"title": "Parts", "version": "1.0", "kind": "apps",
              "about": "an app with a pane of its own",
              "bundle": "bundles/parts.tar.gz", "sha256": h_parts},
    "parts2": {"title": "Parts Two", "version": "1.0", "kind": "apps",
               "bundle": "bundles/parts2.tar.gz", "sha256": h_parts2},
    "badpart": {"title": "Bad Part", "version": "1.0", "kind": "apps",
                "bundle": "bundles/badpart.tar.gz", "sha256": h_badpart},
    "climbpart": {"title": "Climbing Part", "version": "1.0", "kind": "apps",
                  "bundle": "bundles/climbpart.tar.gz", "sha256": h_climbpart},
    "nopart": {"title": "Missing Part", "version": "1.0", "kind": "apps",
               "bundle": "bundles/nopart.tar.gz", "sha256": h_nopart},
    # What an entry says it needs, which nothing read until 0.99.183.
    "needsmod": {"title": "Needs A Module", "version": "1.0", "kind": "apps",
                 "modules": "nosuchmodule", "bundle": "bundles/demo.hibr",
                 "sha256": h_one},
    "needsabi": {"title": "Needs An ABI", "version": "1.0", "kind": "apps",
                 "abi": "999", "bundle": "bundles/demo.hibr", "sha256": h_one},
    "needshibr": {"title": "Needs A Newer hibr", "version": "1.0", "kind": "apps",
                  "hibr": "99.0", "bundle": "bundles/demo.hibr", "sha256": h_one},
    "plain": {"title": "Plain Theme", "version": "1.0", "kind": "themes",
              "about": "a theme, which is JSON and not a script",
              "bundle": "bundles/plain.json", "sha256": h_theme},
    "scheme": {"title": "A Scheme", "version": "1.0", "kind": "colours",
               "bundle": "bundles/scheme.json", "sha256": h_scheme},
    "picture": {"title": "With A Picture", "version": "1.0", "kind": "themes",
                "bundle": "bundles/picture.tar.gz", "sha256": h_picture},
    # A .json offered as an application: the extension is the kind's, so
    # this would land where nothing looks.
    "crossed": {"title": "Crossed", "version": "1.0", "kind": "apps",
                "bundle": "bundles/plain.json", "sha256": h_theme},
    "needsok": {"title": "Needs What Is Here", "version": "1.0", "kind": "apps",
                "modules": "db, archive", "abi": None, "bundle": "bundles/demo.hibr",
                "sha256": h_one},
}}
del INDEX["apps"]["needsok"]["abi"]
put("index.json", json.dumps(INDEX, indent=1))

srv = subprocess.Popen(
    [sys.executable, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                  "davserve.py"), "--root", WWW, "--auth", "none"],
    stdout=subprocess.PIPE, text=True)
PORT = int(srv.stdout.readline().split()[1])
URL = "http://127.0.0.1:%d/index.json" % PORT

CONF = os.path.join(HOME, "config")
DATA = os.path.join(HOME, "data")
CACHE = os.path.join(HOME, "cache")
TMP = os.path.join(HOME, "tmp")


def apps(*args, env=None):
    """Run the manager with a config, data and cache folder of its own."""
    # And a TMPDIR of its own, which is not tidiness: `apps remove` asks a
    # running desktop whether any of its windows belong to the app
    # (ap_windows -> `desktop ctl windows`), and that client finds its
    # socket at $TMPDIR/hibr-ctl-$UID/<session>.ctl -- defaulting to the
    # session named in HIBR_HOLD, or to one called `desktop`. So a suite run
    # from a shell inside a held desktop, which is how this machine is used,
    # reached *that* desktop; and when its control socket was listening but
    # no longer accepting, the connect blocked with nothing to time it out
    # and the suite died on its own 120-second limit, in a check about
    # removing an app. Same shape as screen.py stripping HIBR_HOLD, for the
    # same reason: a suite must not depend on, or wait for, whatever desktop
    # the machine happens to be running. Gitea #180 is the other half -- a
    # control client that can block for ever.
    os.makedirs(TMP, exist_ok=True)
    e = dict(os.environ, XDG_CONFIG_HOME=CONF, XDG_DATA_HOME=DATA,
             XDG_CACHE_HOME=CACHE, HIBR_APPS_URL=URL,
             HIBR_MODPATH=tree("build/mods"), TMPDIR=TMP)
    e.pop("HIBR_HOLD", None)
    e.update(env or {})
    r = subprocess.run([sx.HIBR, APPS] + list(args), capture_output=True,
                       text=True, env=e, timeout=120)
    return r.returncode, r.stdout, r.stderr


def appsx(*runs, env=None):
    """Several managers at once, into one scope. Two that overlap used to
    lose one of them: each read the whole state at its start and wrote the
    whole state back at its end, so whichever finished last erased the
    other's entry while leaving its files on disk (Gitea #242)."""
    os.makedirs(TMP, exist_ok=True)
    e = dict(os.environ, XDG_CONFIG_HOME=CONF, XDG_DATA_HOME=DATA,
             XDG_CACHE_HOME=CACHE, HIBR_APPS_URL=URL,
             HIBR_MODPATH=tree("build/mods"), TMPDIR=TMP)
    e.pop("HIBR_HOLD", None)
    e.update(env or {})
    ps = [subprocess.Popen([sx.HIBR, APPS] + list(a), stdout=subprocess.PIPE,
                           stderr=subprocess.PIPE, text=True, env=e)
          for a in runs]
    return [(p.wait(timeout=120), ) + p.communicate() for p in ps]


try:
    rc, out, err = apps("list")
    check("list says what the index offers, with each one's version",
          rc == 0 and "demo" in out and "Demo" in out and "1.0" in out
          and "multi" in out, out + err)
    rc, out, err = apps("list", "resource")
    check("and a word narrows it to what matches",
          rc == 0 and "multi" in out and "demo " not in out, out + err)
    rc, out, err = apps("info", "demo")
    check("info says the version, the licence and where the index came from",
          rc == 0 and "1.0" in out and "MIT" in out and URL in out, out + err)
    # Since 0.99.191 that paragraph says which of the two it is, and the
    # `signed` line above it says why: this catalogue has no signature
    # beside it, so nothing says who wrote the app (ADR 0044).
    check("and says outright that nothing identifies the author",
          "signed    no" in out and "not signed" in out
          and "reading the file, which you can" in out, out)
    rc, out, err = apps("info", "nosuch")
    check("info on something the index has never heard of says so",
          rc != 0 and "no such app" in err, (rc, err))

    rc, out, err = apps("installed")
    check("nothing is installed to begin with",
          rc == 0 and "nothing installed" in out, out + err)

    # A single .hibr file is its own bundle, which is most apps.
    rc, out, err = apps("install", "demo")
    one = os.path.join(DATA, "hibr", "apps", "demo.hibr")
    check("installing a one-file app puts it where dt_apps will find it",
          rc == 0 and os.path.exists(one)
          and open(one, "rb").read() == APP, (rc, out, err))
    rc, out, err = apps("installed")
    check("and installed says so, with its version and kind",
          rc == 0 and "demo" in out and "1.0" in out and "apps" in out,
          out + err)
    rc, out, err = apps("list")
    check("and list marks it installed rather than offering it again",
          rc == 0 and "installed" in out, out)

    # A bundle of several files: the app at <kind>/NAME.hibr and the rest
    # under <kind>/NAME/, so dt_apps does not give it a submenu of its own.
    rc, out, err = apps("install", "multi")
    mapp = os.path.join(DATA, "hibr", "apps", "multi.hibr")
    mres = os.path.join(DATA, "hibr", "apps", "multi", "art", "logo.txt")
    check("a bundle of several files puts the app beside the others and its "
          "resources in a folder of its own",
          rc == 0 and os.path.exists(mapp) and os.path.exists(mres)
          and open(mres).read() == "a resource\n", (rc, out, err))
    check("and app.json is not left behind as a file of the app's",
          not os.path.exists(os.path.join(DATA, "hibr", "apps", "multi", "app.json")),
          os.listdir(os.path.join(DATA, "hibr", "apps", "multi")))

    # What must be refused. Each of these is a bundle built to do it.
    rc, out, err = apps("install", "bad")
    check("a bundle whose sha256 is not what the index says is refused, both "
          "hashes named",
          rc != 0 and "not what the index describes" in err
          and h_one in err and "0" * 64 in err, (rc, err))
    check("and nothing of it is left installed",
          not os.path.exists(os.path.join(DATA, "hibr", "apps", "bad.hibr")),
          os.listdir(os.path.join(DATA, "hibr", "apps")))
    rc, out, err = apps("install", "climb")
    check("a member climbing out with .. is refused, naming the path",
          rc != 0 and "outside its own folder" in err and ".." in err,
          (rc, err))
    check("and it wrote nothing outside the apps folder",
          not os.path.exists(os.path.join(DATA, "hibr", "escaped.hibr")),
          os.listdir(os.path.join(DATA, "hibr")))
    # An absolute member is *neutralised* rather than refused, and it is the
    # archive module that does it: `/etc/hibr-escaped.hibr` is presented as
    # a folder `etc` holding the file, the leading slash gone, so the path
    # the manager ever sees is an ordinary relative one. This bundle holds
    # that one file and nothing else, so it is also the only `.hibr` in it
    # and becomes the app -- landing as `apps/abs.hibr`, which is where an
    # app goes. Checked as what actually happens rather than as what was
    # expected: the property that matters is that nothing was written
    # outside the apps folder, and that is the check below.
    rc, out, err = apps("install", "abs")
    check("an absolute member path lands inside the apps folder, not at the "
          "path it asked for",
          rc == 0 and os.path.exists(
              os.path.join(DATA, "hibr", "apps", "abs.hibr")), (rc, out, err))
    check("and /etc is untouched, which is the property that matters",
          not os.path.exists("/etc/hibr-escaped.hibr"), "/etc/hibr-escaped.hibr")
    rc, out, err = apps("install", "mod")
    check("a bundle carrying a .so is refused with that word",
          rc != 0 and "module" in err and "evil.so" in err
          and "sandbox" in err, (rc, err))
    rc, out, err = apps("install", "wrongkind")
    check("an index entry naming a kind the desktop does not scan is refused",
          rc != 0 and "not one an app installs into" in err, (rc, err))

    # An update is the index saying a newer version, and nothing else.
    rc, out, err = apps("update")
    check("update with nothing newer says so rather than fetching again",
          rc == 0 and "up to date" in out, out + err)
    INDEX["apps"]["demo"]["version"] = "1.1"
    INDEX["apps"]["demo"]["bundle"] = "bundles/demo-2.hibr"
    INDEX["apps"]["demo"]["sha256"] = h_one2
    put("index.json", json.dumps(INDEX, indent=1))
    rc, out, err = apps("update")
    check("and with a newer one it says which version it moved to, and does",
          rc == 0 and "1.0 -> 1.1" in out
          and open(one, "rb").read() == APP2, (rc, out, err))

    # A person's own file wins, which is how patching an installed app works
    # and why an update cannot overwrite it (ADR 0041, decision 2).
    mine = os.path.join(CONF, "hibr", "apps", "demo.hibr")
    os.makedirs(os.path.dirname(mine), exist_ok=True)
    open(mine, "wb").write(APP.replace(b"hello", b"mine"))
    out = subprocess.run(
        [sx.HIBR, "-c",
         ". %s\nDT_APPDIRS=(\"%s/hibr/apps\" \"%s/hibr/apps\")\ndt_apps\n"
         'printf "%%s\\n" "${DT_SRC["demo"]}"'
         % (tree("examples/desktop/desktop.hibr"), CONF, DATA)],
        capture_output=True, text=True,
        env=dict(os.environ, HIBR_MODPATH=tree("build/mods"))).stdout.strip()
    check("a file of your own in the config folder shadows the fetched app",
          out == mine, (out, mine))

    # Removing. The window check needs a desktop, so what is checked here is
    # the half that does not: it goes, and the state forgets it.
    rc, out, err = apps("remove", "demo")
    # The id, not the bare name: a catalogue with five namespaces calls this
    # apps/demo, and `apps list` shows the same thing (Gitea #222).
    check("remove takes the app away and says so",
          rc == 0 and "removed apps/demo" in out and not os.path.exists(one),
          (rc, out, err))
    rc, out, err = apps("remove", "demo")
    check("and removing it twice says it is not installed rather than lying",
          rc != 0 and "not installed" in err, (rc, err))
    rc, out, err = apps("remove", "multi")
    check("removing a multi-file app takes its resource folder with it",
          rc == 0 and not os.path.exists(mapp)
          and not os.path.exists(os.path.join(DATA, "hibr", "apps", "multi")),
          (rc, out, err))

    # The index's own address, and what it refuses to be.
    rc, out, err = apps("index")
    check("index with no argument says which one is in force", rc == 0
          and URL in out, out + err)
    rc, out, err = apps("index", "ftp://example.com/i.json", env={"HIBR_APPS_URL": ""})
    check("and refuses an address that is not http or https",
          rc != 0 and "address" in err, (rc, err))
    rc, out, err = apps("index", "http://example.com/i.json", env={"HIBR_APPS_URL": ""})
    check("an http index is taken but says nothing identifies the server",
          rc == 0 and "not https" in err, (rc, out, err))

    # An index that cannot be fetched says that, rather than a stack of
    # whatever the http client thought.
    rc, out, err = apps("list", env={"HIBR_APPS_URL":
                                     "http://127.0.0.1:%d/nope.json" % PORT,
                                     "XDG_CACHE_HOME": os.path.join(HOME, "c2")})
    check("an index that is not there says so in one line, naming it",
          rc != 0 and "could not be fetched" in err and "nope.json" in err,
          (rc, err))

    # An app is more than one file: eight of the real twenty-one bring a
    # Control Panel pane, which lives in another of the desktop's five scan
    # roots and so cannot travel as a resource under the app's own
    # (Gitea #217). app.json names the parts; the state records every path
    # placed, because a folder on disk cannot say what it came with.
    pane = os.path.join(DATA, "hibr", "control-panel", "paneof.hibr")
    rc, out, err = apps("install", "parts")
    check("a bundle's part is placed in the scan root it names",
          rc == 0 and os.path.exists(os.path.join(DATA, "hibr", "apps", "parts.hibr"))
          and os.path.exists(pane), (rc, out, err))
    check("and the install says so, rather than placing it silently",
          "control-panel/paneof.hibr" in out, out)
    rc, out, err = apps("remove", "parts")
    check("removing the app takes its part away too",
          rc == 0 and not os.path.exists(pane)
          and not os.path.exists(os.path.join(DATA, "hibr", "apps", "parts.hibr")),
          (rc, out, err, os.path.exists(pane)))

    # One pane serves two apps -- pimset is Calendar's and Contacts' both --
    # so removing one of the two must leave the other's where it is. The
    # state is what makes that answerable rather than a guess.
    apps("install", "parts")
    apps("install", "parts2")
    rc, out, err = apps("remove", "parts")
    check("a part another installed app brings too is kept, and says so",
          rc == 0 and os.path.exists(pane) and "kept" in out, (rc, out, err))
    rc, out, err = apps("remove", "parts2")
    check("and goes once the last app that brings it is gone",
          rc == 0 and not os.path.exists(pane), (rc, out, err))

    # Twenty-one entries put thirty files in the owner's own scope and
    # nothing said where the other nine came from, which read as "themes,
    # control panels and apps are all installed" (Gitea #237). The state
    # always knew; only the listing did not say. Three reasons a file is
    # in a scope -- you asked, an app brought it, something needed it --
    # and until this release the listing showed the first alone.
    apps("install", "parts")
    apps("install", "demo")
    rc, out, err = apps("installed")
    partsrow = [l for l in out.splitlines() if l.startswith("apps/parts ")]
    demorow = [l for l in out.splitlines() if l.startswith("apps/demo ")]
    check("installed says what an entry brought with it",
          len(partsrow) == 1 and "+1 part" in partsrow[0], (out,))
    check("and says nothing of the kind for one that brought nothing",
          len(demorow) == 1 and "part" not in demorow[0], (out,))
    # The two numbers are the whole point: apart they do not reconcile,
    # and a person counting files in the folder is owed the second one.
    # Checked against the JSON the desktop window reads rather than
    # against literals -- the suite has other entries installed by now,
    # so a hard-coded total is a check of what ran before it.
    m = re.search(r"^(\d+) entries, (\d+) files$", out, re.M)
    rc2, js, err2 = apps("-j", "installed")
    st = json.loads(js)["installed"]
    files = set()
    for e in st.values():
        ps = e["paths"]
        ps = ps if isinstance(ps, list) else [ps[k] for k in sorted(ps)]
        res = ps[0].rsplit(".", 1)[0]
        files.add(ps[0])
        files.update(x for x in ps[1:] if x != res)
    check("and the totals reconcile entries against files",
          m is not None and int(m.group(1)) == len(st)
          and int(m.group(2)) == len(files), (out, js))
    # A pane two apps both bring is one file, so installing the second
    # adds an entry and only its own file -- which is the property, where
    # a literal total would only have been this run's arithmetic.
    e1, f1 = int(m.group(1)), int(m.group(2))
    apps("install", "parts2")
    rc, out, err = apps("installed")
    m = re.search(r"^(\d+) entries, (\d+) files$", out, re.M)
    check("a part two entries share is one file in the total, not two",
          m is not None and int(m.group(1)) == e1 + 1
          and int(m.group(2)) == f1 + 1, (e1, f1, out))

    # The reverse question, which is the one actually asked of a folder
    # somebody did not fill, and which nothing could answer before.
    rc, out, err = apps("owns", "control-panel/paneof.hibr")
    check("owns names every entry a shared part belongs to",
          rc == 0 and "apps/parts" in out and "apps/parts2" in out,
          (rc, out, err))
    check("and says a shared one outlives the removal of any one of them",
          "kept until" in out, out)
    rc, out, err = apps("remove", "parts2")
    rc, out, err = apps("owns", "control-panel/paneof.hibr")
    check("with one owner left it says what removing that one would do",
          rc == 0 and "a part of apps/parts" in out
          and "removed when apps/parts is" in out, (rc, out, err))
    rc, out, err = apps("owns", "apps/parts.hibr")
    check("owns tells an entry's own file from a part it brought",
          rc == 0 and "itself" in out and "a part of" not in out,
          (rc, out, err))
    rc, out, err = apps("owns", "control-panel/nosuch.hibr")
    check("and a path nothing claims fails rather than inventing an owner",
          rc != 0 and "nothing here knows about it" in out + err,
          (rc, out, err))
    rc, out, err = apps("info", "parts")
    check("info says what an entry brings, read from the index",
          rc == 0 and "brings" in out and "control-panel/paneof.hibr" in out,
          (rc, out, err))
    apps("remove", "parts")
    apps("remove", "demo")

    # The state is one file two managers can want at once. Each used to
    # read the whole of it at its start and write the whole of it back at
    # its end, so two that overlapped lost one: both placed their files and
    # whichever finished last erased the other's entry, leaving a file on
    # disk that `installed` could not see and `remove` could not take away.
    # Measured at 10 of 10 rounds against the release before this one
    # (Gitea #242).
    STATE = os.path.join(DATA, "hibr", "apps.json")
    lost = 0
    for _ in range(4):
        for n in ("demo", "parts"):
            apps("remove", n)
        appsx(("install", "demo"), ("install", "parts"))
        rc, out, err = apps("installed")
        if not ("apps/demo" in out and "apps/parts" in out):
            lost += 1
    check("two managers installing at once keep both, rather than one "
          "erasing the other's entry and orphaning its files",
          lost == 0, (lost, out))
    for n in ("demo", "parts"):
        apps("remove", n)

    # A state that will not parse is damaged, which is a different answer
    # from "nothing is installed" and was given as one: `printf > file`
    # truncates in place, so a crash mid-write left a partial file that the
    # next run read as an empty scope -- with every installed file still on
    # disk and no longer claimed (Gitea #241).
    apps("install", "demo")
    good = open(STATE).read()
    open(STATE, "w").write('{"installed":{"apps/demo":{"ver')
    rc, out, err = apps("installed")
    check("a damaged state is refused rather than read as nothing "
          "installed",
          rc != 0 and "will not read as JSON" in err, (rc, out, err))
    rc, out, err = apps("install", "parts")
    check("and a write refuses too, rather than starting a fresh record "
          "over the top of files that are still there",
          rc != 0 and not os.path.exists(os.path.join(DATA, "hibr", "apps",
                                                      "parts.hibr")),
          (rc, out, err))
    open(STATE, "w").write(good)
    # An empty file is a scope nothing has been installed into, which is
    # not damage -- the check has to tell those apart or a fresh account
    # cannot install anything.
    open(STATE, "w").write("")
    rc, out, err = apps("installed")
    check("an empty state is an empty scope, not a damaged one",
          rc == 0 and "nothing installed" in out + err, (rc, out, err))
    open(STATE, "w").write(good)

    # A lock nothing can break is a catalogue nothing can write again, and
    # a lock broken too eagerly is the race coming back.
    LOCK = os.path.join(DATA, "hibr", ".lock")
    os.makedirs(LOCK, exist_ok=True)
    open(os.path.join(LOCK, "pid"), "w").write("999999\n")
    rc, out, err = apps("install", "parts")
    check("a lock left behind by a process that is gone is taken over, "
          "and says so",
          rc == 0 and "taking over a lock" in out + err, (rc, out, err))
    os.makedirs(LOCK, exist_ok=True)
    open(os.path.join(LOCK, "pid"), "w").write("%d\n" % os.getpid())
    rc, out, err = apps("install", "demo", env={"AP_LOCKWAIT": "3"})
    check("and one held by a process that is alive is waited for and then "
          "named, rather than written over",
          rc != 0 and "another catalogue is writing" in err, (rc, err))
    shutil.rmtree(LOCK, True)
    apps("remove", "parts")

    # A part decides a destination, so the rules the member paths already
    # follow have to hold for where it lands as well.
    for name, why in (("badpart", "a kind that is not one"),
                      ("climbpart", "a name that climbs")):
        rc, out, err = apps("install", name)
        check("a part with %s is refused, and nothing is placed" % why,
              rc != 0 and "Refused" in err
              and not os.path.exists(os.path.join(DATA, "hibr", "apps",
                                                  name + ".hibr")),
              (rc, err))
    rc, out, err = apps("install", "nopart")
    check("a part naming a member the bundle does not have is refused",
          rc != 0 and "nopart" in err, (rc, err))

    # And what an entry says it needs, which `apps info` printed from the
    # beginning and nothing ever read: an app needing a module installed
    # happily without it and failed at its first call, in a window.
    for name, want in (("needsmod", "nosuchmodule"), ("needsabi", "ABI"),
                       ("needshibr", "newer")):
        rc, out, err = apps("install", name)
        check("an app needing %s this hibr lacks is refused before fetching"
              % want.lower(),
              rc != 0 and want in err
              and not os.path.exists(os.path.join(DATA, "hibr", "apps",
                                                  name + ".hibr")),
              (rc, err))
    rc, out, err = apps("install", "needsok")
    check("and one whose needs are met installs",
          rc == 0 and os.path.exists(os.path.join(DATA, "hibr", "apps",
                                                  "needsok.hibr")),
          (rc, out, err))

    # An app installed before any of this was recorded still removes: the
    # state has no paths for it, and the kind and the name are all there is.
    st = os.path.join(DATA, "hibr", "apps.json")
    d = json.load(open(st))
    # The state is keyed by the id since 0.99.184 (Gitea #222), and by the
    # bare name before it. Written to survive either, so this suite can be
    # run against an older apps as the control for those checks -- the first
    # version died here and took the eight checks below it with it, which is
    # a control that proves nothing.
    k = "apps/needsok" if "apps/needsok" in d["installed"] else "needsok"
    del d["installed"][k]["paths"]
    json.dump(d, open(st, "w"))
    rc, out, err = apps("remove", "needsok")
    check("an app installed before the paths were recorded still removes",
          rc == 0 and not os.path.exists(os.path.join(DATA, "hibr", "apps",
                                                      "needsok.hibr")),
          (rc, out, err))
    # A catalogue with five namespaces (Gitea #222). The desktop's scan
    # roots are five, a name is only unique inside one of them -- `clock` is
    # an application and a screen saver -- and an index keyed by name alone
    # silently kept one of any pair: 54 bundles went in and 53 entries came
    # out. So an index carries `entries`, keyed by kind/name, and keeps
    # `apps` for an older client. Everything above this point runs against
    # the name-keyed index, which is that compatibility proved rather than
    # asserted.
    E = {"apps": dict(INDEX["apps"]),
         "entries": {
             "apps/demo": dict(INDEX["apps"]["demo"], name="demo"),
             "apps/clock": {"title": "Clock", "version": "1.0", "kind": "apps",
                            "name": "clock", "about": "the desk clock",
                            "bundle": "bundles/demo.hibr", "sha256": h_one},
             "savers/clock": {"title": "Clock", "version": "1.0",
                              "kind": "savers", "name": "clock",
                              "about": "the clock saver",
                              "bundle": "bundles/demo.hibr", "sha256": h_one},
             # The kinds that are data rather than scripts (Gitea #224).
             "themes/plain": dict(INDEX["apps"]["plain"], name="plain"),
             "colours/scheme": dict(INDEX["apps"]["scheme"], name="scheme"),
             "themes/picture": dict(INDEX["apps"]["picture"], name="picture"),
             "apps/crossed": dict(INDEX["apps"]["crossed"], name="crossed"),
         }}
    put("index2.json", json.dumps(E, indent=1))
    E2 = {"HIBR_APPS_URL": "http://127.0.0.1:%d/index2.json" % PORT,
          "XDG_CACHE_HOME": os.path.join(HOME, "c3"),
          "XDG_DATA_HOME": os.path.join(HOME, "d3")}
    rc, out, err = apps("list", env=E2)
    check("an index keyed by kind/name is read, and the list shows the id",
          rc == 0 and "apps/clock" in out and "savers/clock" in out, out + err)
    rc, out, err = apps("install", "demo", env=E2)
    check("a bare name that means one thing still installs it",
          rc == 0 and os.path.exists(os.path.join(HOME, "d3", "hibr", "apps",
                                                  "demo.hibr")),
          (rc, out, err))
    rc, out, err = apps("install", "clock", env=E2)
    check("a bare name that means two is refused, naming both",
          rc != 0 and "apps/clock" in err and "savers/clock" in err,
          (rc, err))
    rc, out, err = apps("install", "savers/clock", env=E2)
    check("and the id installs the one it names, in its own scan root",
          rc == 0 and os.path.exists(os.path.join(HOME, "d3", "hibr", "savers",
                                                  "clock.hibr"))
          and not os.path.exists(os.path.join(HOME, "d3", "hibr", "apps",
                                              "clock.hibr")),
          (rc, out, err))
    rc, out, err = apps("install", "apps/clock", env=E2)
    check("both can be installed at once, which is the whole point",
          rc == 0 and os.path.exists(os.path.join(HOME, "d3", "hibr", "apps",
                                                  "clock.hibr"))
          and os.path.exists(os.path.join(HOME, "d3", "hibr", "savers",
                                          "clock.hibr")),
          (rc, out, err))
    rc, out, err = apps("installed", env=E2)
    check("and the state tells them apart",
          rc == 0 and "apps/clock" in out and "savers/clock" in out,
          (rc, out, err))
    rc, out, err = apps("remove", "savers/clock", env=E2)
    check("removing one leaves the other",
          rc == 0 and not os.path.exists(os.path.join(HOME, "d3", "hibr",
                                                      "savers", "clock.hibr"))
          and os.path.exists(os.path.join(HOME, "d3", "hibr", "apps",
                                          "clock.hibr")),
          (rc, out, err))

    # The three kinds that are data (Gitea #224): a theme, a colour scheme
    # and a translation catalogue are JSON, read from the same
    # $XDG_CONFIG_HOME/hibr/<kind> a script is, so installing one is placing
    # a file where the desktop already looks. `ap_dest` appended `.hibr` to
    # everything, so a theme had nowhere to land at all.
    rc, out, err = apps("install", "themes/plain", env=E2)
    th = os.path.join(HOME, "d3", "hibr", "themes", "plain.json")
    check("a theme lands as JSON in the themes folder",
          rc == 0 and os.path.exists(th)
          and json.load(open(th))["colours"] == "midnight", (rc, out, err))
    rc, out, err = apps("install", "colours/scheme", env=E2)
    check("and a colour scheme in its own",
          rc == 0 and os.path.exists(os.path.join(HOME, "d3", "hibr",
                                                  "colours", "scheme.json")),
          (rc, out, err))
    # A theme with a picture is two files beside each other, which is what a
    # part naming a filename buys: `name` would have appended .hibr to it.
    rc, out, err = apps("install", "themes/picture", env=E2)
    tdir = os.path.join(HOME, "d3", "hibr", "themes")
    check("a theme brings its own picture, beside it rather than under it",
          rc == 0 and os.path.exists(os.path.join(tdir, "picture.json"))
          and os.path.exists(os.path.join(tdir, "picture.png")),
          (rc, out, err, sorted(os.listdir(tdir))
           if os.path.isdir(tdir) else "no themes folder"))
    rc, out, err = apps("remove", "themes/picture", env=E2)
    check("and removing it takes the picture too",
          rc == 0 and not os.path.exists(os.path.join(tdir, "picture.png")),
          (rc, out, err, sorted(os.listdir(tdir))
           if os.path.isdir(tdir) else "no themes folder"))
    # The extension belongs to the kind, so it cannot be crossed: a .json
    # offered as an application is a file nothing would ever read. This one
    # guards the invariant rather than the change -- it passes against
    # 0.99.185 too, where a .json bundle was opened as a tarball and failed
    # that way instead. The four above it fail there.
    rc, out, err = apps("install", "apps/crossed", env=E2)
    check("a bundle whose extension is not the kind's is not placed as itself",
          not os.path.exists(os.path.join(HOME, "d3", "hibr", "apps",
                                          "crossed.hibr")),
          (rc, out, err))

    # `-j` is the answer anything that is not a person reads: the
    # desktop's Applications window reads this rather than parsing the
    # columns, so the two read one answer instead of one parsing the
    # other's layout (Gitea #219). Not the result slot the ticket asked
    # for -- `apps` is a separate program the desktop runs as a child, so
    # `:=` captures its standard output and a child cannot fill its
    # parent's slot. A builtin can; a command can only print.
    rc, out, err = apps("list", "-j")
    j = json.loads(out)
    check("`apps list -j` answers JSON, every fact in a field of its own",
          rc == 0 and j["entries"]["apps/demo"]["id"] == "apps/demo"
          and j["entries"]["apps/demo"]["title"]
          and "update" in j["entries"]["apps/demo"], out[:200])
    rc, out, err = apps("install", "demo")
    rc, out, err = apps("list", "-j")
    j = json.loads(out)["entries"]["apps/demo"]
    # Strings, as every other field in the index is: a reader in hibr
    # compares strings whatever the type, and one document with two
    # conventions is worse than either.
    check("and it says what is installed and whether that is behind, "
          "rather than a sentence to read twice",
          j["installed"] == j["version"] and j["update"] == "0", j)
    rc, out, err = apps("installed", "-j")
    j = json.loads(out)["installed"]
    check("`apps installed -j` answers every path an install placed, which "
          "is what says the cost of removing it",
          rc == 0 and "apps/demo" in j
          and "apps/demo.hibr" in j["apps/demo"]["paths"], out[:200])
    rc, out, err = apps("info", "demo", "-j")
    j = json.loads(out)
    check("`apps info NAME -j` answers one entry, with where the index "
          "itself came from",
          rc == 0 and j["id"] == "apps/demo" and j["index"].startswith("http"),
          out[:200])
    rc, out, err = apps("list", "-j", "nosuchthingatall")
    j = json.loads(out)
    check("and an empty answer is still JSON, so a reader never has to ask "
          "whether it got any",
          rc == 0 and j["entries"] == {}, out[:120])
    # The kinds travel with the answer and are a fact about this program
    # rather than about the index, so they are there when nothing matched
    # too -- and the two paths emit them separately (the empty one writes
    # its JSON out by hand, since an element cannot be assigned an empty
    # map), which is exactly the pair that drifts. The Catalogue window
    # groups by this list instead of keeping one of its own, after three
    # releases in which `mods` was in one copy and not the other
    # (Gitea #220).
    rc2, out2, err2 = apps("list", "-j")
    k2 = list(json.loads(out2)["kinds"])
    check("`list -j` carries the kinds it installs into, in its declared "
          "order, whether or not anything matched",
          list(j["kinds"]) == k2 and k2[0] == "apps" and "mods" in k2,
          (list(j["kinds"]), k2))
    rc, out, err = apps("remove", "demo")

    # Parts named by the **index** rather than carried in a tarball
    # (Gitea #231). A .tar.gz held an app.json saying where each member
    # went, which is a manifest of files -- what the index already is --
    # and a binary blob in a git repository that nothing can diff. Every
    # file is plain now and the index says what belongs with what.
    PANEBYTES = PANE
    hp_app = put("bundles/ixparts.hibr", APP.replace(b"demo", b"ixparts"))
    hp_pane = put("bundles/ixpane.hibr", PANEBYTES)
    E5 = {"entries": {
        "apps/ixparts": {
            "title": "Index Parts", "version": "2.1", "kind": "apps",
            "name": "ixparts", "about": "an app whose parts the index names",
            "bundle": "bundles/ixparts.hibr", "sha256": hp_app,
            "parts": [{"kind": "control-panel", "as": "ixpane.hibr",
                       "path": "bundles/ixpane.hibr", "sha256": hp_pane}]},
        # The same pane, with a sha256 that is not what is served: the
        # whole entry must be refused, and the *app* file -- already
        # fetched and verified by then -- must not be placed either.
        "apps/ixbad": {
            "title": "Index Parts Bad", "version": "1.0", "kind": "apps",
            "name": "ixbad", "about": "a part the index describes wrongly",
            "bundle": "bundles/ixparts.hibr", "sha256": hp_app,
            "parts": [{"kind": "control-panel", "as": "ixpane.hibr",
                       "path": "bundles/ixpane.hibr", "sha256": "0" * 64}]},
    }}
    put("index5.json", json.dumps(E5, indent=1))
    E5ENV = {"HIBR_APPS_URL": "http://127.0.0.1:%d/index5.json" % PORT,
             "XDG_CACHE_HOME": os.path.join(HOME, "c5"),
             "XDG_DATA_HOME": os.path.join(HOME, "d5")}
    d5 = os.path.join(HOME, "d5", "hibr")
    rc, out, err = apps("install", "apps/ixparts", env=E5ENV)
    check("an entry whose parts the index names installs them, each in its "
          "own scan root",
          rc == 0 and os.path.exists(os.path.join(d5, "apps", "ixparts.hibr"))
          and os.path.exists(os.path.join(d5, "control-panel", "ixpane.hibr")),
          (rc, out, err))
    rc, out, err = apps("install", "apps/ixbad", env=E5ENV)
    check("a part whose sha256 is not what arrived refuses the whole entry, "
          "and the app's own file is not placed either",
          rc != 0 and "control-panel/ixpane.hibr" in err
          and not os.path.exists(os.path.join(d5, "apps", "ixbad.hibr")),
          (rc, err, sorted(os.listdir(os.path.join(d5, "apps")))
           if os.path.isdir(os.path.join(d5, "apps")) else "no apps folder"))
    # This one passes against 0.99.188 as well, and says so: there the
    # install placed no part, so removing it had nothing to leave behind.
    # It is here because the claim counting is what must keep working now
    # that the manifest moved, not to prove the change.
    rc, out, err = apps("remove", "apps/ixparts", env=E5ENV)
    check("and removing it takes the part the index named with it",
          rc == 0 and not os.path.exists(os.path.join(d5, "apps", "ixparts.hibr"))
          and not os.path.exists(os.path.join(d5, "control-panel", "ixpane.hibr")),
          (rc, out, err))

    # The entry's own **name** decides a filename, and nothing checked it
    # until 0.99.186: `"name": "../../../climbed"` with kind `apps` wrote
    # `$AP_DATA/apps/../../../climbed.hibr` -- three folders above the data
    # directory, with the sha256 matching, since that says the bytes were
    # not changed on the way and nothing about where they land. Measured
    # before the fix as a real file; `../../.hibrc` is the same hole with
    # the shell's own startup file on the end of it. A part's path has been
    # checked since bundles could carry one, and the entry's name is the
    # other half of the same question (Gitea #225).
    E3 = {"entries": {
        "apps/demo": dict(INDEX["apps"]["demo"], name="demo"),
        "apps/climber": dict(INDEX["apps"]["demo"], name="../../../climbed"),
    }}
    put("index3.json", json.dumps(E3, indent=1))
    E4 = {"HIBR_APPS_URL": "http://127.0.0.1:%d/index3.json" % PORT,
          "XDG_CACHE_HOME": os.path.join(HOME, "c4"),
          "XDG_DATA_HOME": os.path.join(HOME, "d4")}
    climbed = os.path.join(HOME, "climbed.hibr")
    rc, out, err = apps("install", "apps/climber", env=E4)
    check("an entry whose name climbs out of the data folder is refused",
          rc != 0 and not os.path.exists(climbed),
          (rc, out, err, sorted(os.listdir(HOME))))
    # Refused rather than fatal: one bad entry is missing from the list, the
    # way a theme with a bad value is missing from Appearance's, instead of
    # taking the whole catalogue with it.
    rc, out, err = apps("list", env=E4)
    check("and the list leaves it out rather than ending there",
          rc == 0 and "apps/demo" in out and "climbed" not in out,
          (rc, out, err))

    # A state written before this was keyed by name, and everything in it was
    # an application, since the catalogue published nothing else: a bare key
    # means apps/<key> exactly.
    st3 = os.path.join(HOME, "d3", "hibr", "apps.json")
    d = json.load(open(st3))
    d["installed"]["demo"] = d["installed"].pop("apps/demo")
    json.dump(d, open(st3, "w"))
    rc, out, err = apps("list", env=E2)
    check("a state keyed by name is read as the apps it can only have been",
          rc == 0 and "installed" in out.split("apps/demo")[1].split("\n")[0],
          (rc, out))

    # --- a module from the catalogue (Gitea #221, ADR 0044) -------------
    #
    # The one kind that is native code, and the three things that differ
    # for it: it lands in the module directory a non-root shell searches
    # by name, it is refused unless the index is signed, and its `arch`
    # and `abi` are checked before the fetch rather than discovered after
    # it. The key here is the suite's own -- HIBR_APPS_KEYFP is how a
    # mirror pins its own, which is what makes this testable at all
    # without the project's secret key.
    GPG = shutil.which("gpg")
    GH = os.path.join(D, "gnupg")
    SOBYTES = open(tree("build/mods/csv.so"), "rb").read()
    h_mod = put("bundles/greet.so", SOBYTES)
    MIX = {"entries": {
        "mods/greet": {"title": "greet", "version": "1.3", "kind": "mods",
                       "name": "greet", "about": "a module, for the suite",
                       "arch": ARCH, "abi": ABI,
                       "bundle": "bundles/greet.so", "sha256": h_mod},
        "mods/elsewhere": {"title": "elsewhere", "version": "1.0",
                           "kind": "mods", "name": "elsewhere",
                           "arch": "darwin-arm64", "abi": ABI,
                           "bundle": "bundles/greet.so", "sha256": h_mod},
        "mods/oldabi": {"title": "oldabi", "version": "1.0", "kind": "mods",
                        "name": "oldabi", "arch": ARCH, "abi": "1",
                        "bundle": "bundles/greet.so", "sha256": h_mod},
        "apps/demo": {"title": "Demo", "version": "1.0", "kind": "apps",
                      "name": "demo", "bundle": "bundles/demo.hibr",
                      "sha256": h_one},
        # An app that needs a module the catalogue has. `greet` is not
        # loadable here -- it is `csv.so` under another name, and `need
        # greet` finds nothing -- so this exercises the fetch rather
        # than the already-satisfied path (Gitea #234).
        "apps/wantsmod": {"title": "Wants A Module", "version": "1.0",
                          "kind": "apps", "name": "wantsmod",
                          "modules": "greet", "bundle": "bundles/demo.hibr",
                          "sha256": h_one},
        # Where inside its kind it lands, so a fetched desktop has the
        # submenus a shipped one has (Gitea #233).
        "apps/inner": {"title": "Inner", "version": "1.0", "kind": "apps",
                       "name": "inner", "dir": "Games",
                       "bundle": "bundles/demo.hibr", "sha256": h_one},
        # A dir decides a destination, so it is checked and not trusted.
        # The entry is still a perfectly good app, so the folder is
        # dropped rather than the install refused -- the worst that
        # happens is the flat placement everybody had before it existed.
        "apps/climber": {"title": "Climber", "version": "1.0", "kind": "apps",
                         "name": "climber", "dir": "../../../etc",
                         "bundle": "bundles/demo.hibr", "sha256": h_one},
    }}
    put("index221.json", json.dumps(MIX, indent=1))
    D221 = os.path.join(HOME, "d221")
    E221 = {"HIBR_APPS_URL": "http://127.0.0.1:%d/index221.json" % PORT,
            "XDG_DATA_HOME": D221,
            "XDG_CACHE_HOME": os.path.join(HOME, "c221")}

    # Unsigned first, which is the state every catalogue was in until now.
    rc, out, err = apps("install", "mods/greet", env=E221)
    check("a module is refused from a catalogue with no signature on it",
          rc != 0 and "cannot verify" in err and "no signature" in err
          and not os.path.exists(os.path.join(D221, "hibr", "mods")),
          (rc, err))
    rc, out, err = apps("install", "apps/demo", env=E221)
    check("and a script from the same catalogue installs, saying so once",
          rc == 0 and "not verified" in out
          and os.path.exists(os.path.join(D221, "hibr", "apps", "demo.hibr")),
          (rc, out, err))

    if GPG:
        os.makedirs(GH, mode=0o700, exist_ok=True)
        subprocess.run([GPG, "--batch", "--pinentry-mode", "loopback",
                        "--passphrase", "", "--quick-generate-key",
                        "hibr suite <suite@example.invalid>", "ed25519",
                        "sign", "never"],
                       env=dict(os.environ, GNUPGHOME=GH),
                       capture_output=True, timeout=120)
        fp = subprocess.run([GPG, "--batch", "--with-colons", "--list-keys"],
                            env=dict(os.environ, GNUPGHOME=GH),
                            capture_output=True, text=True).stdout
        FP = [l.split(":")[9] for l in fp.split("\n") if l.startswith("fpr:")][0]
        KR = os.path.join(D, "suite.gpg")
        with open(KR, "wb") as f:
            f.write(subprocess.run([GPG, "--batch", "--export", FP],
                                   env=dict(os.environ, GNUPGHOME=GH),
                                   capture_output=True).stdout)
        subprocess.run([GPG, "--batch", "--yes", "--pinentry-mode", "loopback",
                        "--passphrase", "", "--local-user", FP, "--armor",
                        "--detach-sign", "-o",
                        os.path.join(WWW, "index221.json.asc"),
                        os.path.join(WWW, "index221.json")],
                       env=dict(os.environ, GNUPGHOME=GH), capture_output=True)
        E221S = dict(E221, HIBR_APPS_KEYRING=KR, HIBR_APPS_KEYFP=FP)
        rc, out, err = apps("install", "mods/greet", env=E221S)
        check("and installs from a signed one, into the module directory",
              rc == 0 and os.path.exists(
                  os.path.join(D221, "hibr", "mods", "greet.so")),
              (rc, out, err))
        # The two the index declares and nothing read before: a module is
        # built for one machine and one ABI, and neither is a thing to
        # find out after fetching it.
        rc, out, err = apps("install", "mods/elsewhere", env=E221S)
        check("a module built for another machine is named and declined",
              rc != 0 and "darwin-arm64" in err and ARCH in err, (rc, err))
        rc, out, err = apps("install", "mods/oldabi", env=E221S)
        check("and one built for another module ABI as well",
              rc != 0 and "ABI 1 " in err.replace("\n", " "), (rc, err))
        # A signature made by somebody else's key is not this key's.
        rc, out, err = apps("install", "mods/greet",
                            env=dict(E221S, HIBR_APPS_KEYFP="0" * 40))
        check("a signature by a key this does not pin is not a signature",
              rc != 0 and "cannot verify" in err, (rc, err))

        # What an app declares it needs is fetched before the app, so the
        # app is never on disk in a state where its first `need` fails.
        # The index field has been read since `apps` was written and
        # nothing had ever written it; mkindex fills it from a bundle's
        # own `# modules:` line now (Gitea #234).
        DW = os.path.join(HOME, "dwant")
        EW = dict(E221S, XDG_DATA_HOME=DW,
                  XDG_CACHE_HOME=os.path.join(HOME, "cwant"))
        rc, out, err = apps("install", "apps/wantsmod", env=EW)
        check("a module an app needs is fetched first, and the app says "
              "which app wanted it",
              rc == 0
              and os.path.exists(os.path.join(DW, "hibr", "mods", "greet.so"))
              and os.path.exists(os.path.join(DW, "hibr", "apps",
                                              "wantsmod.hibr"))
              and "which apps/wantsmod needs" in out,
              (rc, out, err))
        # Why it is here, which is the whole of what a later purge reads:
        # nobody asked for greet by name.
        st = json.load(open(os.path.join(DW, "hibr", "apps.json")))["installed"]
        check("and the state remembers which app it was fetched for",
              st.get("mods/greet", {}).get("auto") == "apps/wantsmod"
              and not st.get("apps/wantsmod", {}).get("auto"),
              {k: v.get("auto", "") for k, v in st.items()})
        # A module for another machine is not a choice to offer: the shell
        # knows what it is running on. Hidden from the list and from -j,
        # which is what the Catalogue window reads -- but `info` still
        # answers, because asking by name is a different question.
        rc, out, err = apps("list", env=EW)
        rc2, out2, err2 = apps("info", "mods/elsewhere", env=EW)
        check("a module built for another machine is not listed, and "
              "`info` still says why",
              rc == 0 and "mods/greet" in out and "elsewhere" not in out
              and rc2 == 0 and "darwin-arm64" in out2,
              (out, out2))

        # The folder an entry declares, and the folder it may not.
        rc, out, err = apps("install", "apps/inner", env=EW)
        rc2, out2, err2 = apps("install", "apps/climber", env=EW)
        esc = os.path.realpath(os.path.join(DW, "..", "..", "..", "etc",
                                            "climber.hibr"))
        check("an entry lands in the folder the index names, and a folder "
              "that climbs is dropped rather than followed",
              rc == 0 and os.path.exists(
                  os.path.join(DW, "hibr", "apps", "Games", "inner.hibr"))
              and rc2 == 0 and os.path.exists(
                  os.path.join(DW, "hibr", "apps", "climber.hibr"))
              and not os.path.exists(esc),
              (rc, out, rc2, out2))
        # Reinstalling has always written the new paths over the old
        # record and left the old files; `dir` made that reachable, since
        # an app that used to land flat now lands in a folder and both
        # would be on the scan path at once.
        st = json.load(open(os.path.join(DW, "hibr", "apps.json")))
        st["installed"]["apps/inner"]["paths"] = {"0": "apps/inner.hibr"}
        st["installed"]["apps/inner"]["version"] = "0.9"
        json.dump(st, open(os.path.join(DW, "hibr", "apps.json"), "w"))
        os.rename(os.path.join(DW, "hibr", "apps", "Games", "inner.hibr"),
                  os.path.join(DW, "hibr", "apps", "inner.hibr"))
        # purge: a module fetched *for* something, once nothing needs it.
        # Four cases and they must each behave differently, which is the
        # whole of the design -- native code is never removed as a side
        # effect of removing something else (Gitea #234).
        gso = os.path.join(DW, "hibr", "mods", "greet.so")
        rc, out, err = apps("purge", "--dry-run", env=EW)
        check("purge lists nothing while the app that needed the module "
              "is still installed",
              rc == 0 and "greet" not in out and os.path.exists(gso),
              (rc, out))
        apps("remove", "-f", "apps/wantsmod", env=EW)
        rc, out, err = apps("purge", "--dry-run", env=EW)
        check("and lists it once that app is gone, saying which app it "
              "was fetched for",
              rc == 0 and "mods/greet" in out and "apps/wantsmod" in out
              and os.path.exists(gso), (rc, out))
        rc, out, err = apps("purge", env=EW)
        check("asked with no terminal to ask on it refuses and names the "
              "flag, rather than hanging on a read that cannot return",
              rc != 0 and "no terminal" in err and os.path.exists(gso),
              (rc, out, err))
        rc, out, err = apps("purge", "-y", env=EW)
        check("and -y takes it away", rc == 0 and not os.path.exists(gso),
              (rc, out, err))
        # Asked for by name, it is yours: `auto` is set only when
        # something else pulled it in, so purge leaves this one for ever.
        apps("install", "mods/greet", env=EW)
        rc, out, err = apps("purge", "--dry-run", env=EW)
        check("a module installed by name is never purged, however little "
              "needs it",
              rc == 0 and "greet" not in out and os.path.exists(gso),
              (rc, out))

        rc, out, err = apps("update", "apps/inner", env=EW)
        check("and an app that moves into a folder leaves no copy behind "
              "at the path it used to have",
              rc == 0
              and os.path.exists(os.path.join(DW, "hibr", "apps", "Games",
                                              "inner.hibr"))
              and not os.path.exists(os.path.join(DW, "hibr", "apps",
                                                  "inner.hibr"))
              and "took away apps/inner.hibr" in out,
              (rc, out, err))

        # The system scope: an administrator's install, for every user of
        # the machine (ADR 0045). $HIBR_SYSDIR is a path chosen by the
        # person running this, so the root-owned test that guards the
        # compiled-in default does not apply to it -- which is what makes
        # it reachable from a suite at all.
        SYS = os.path.join(HOME, "sysroot")
        ES = dict(EW, HIBR_SYSDIR=SYS)
        rc, out, err = apps("--system", "install", "apps/demo", env=ES)
        check("--system installs into the machine's own scope and leaves "
              "the person's alone",
              rc == 0
              and os.path.exists(os.path.join(SYS, "apps", "demo.hibr"))
              and not os.path.exists(os.path.join(DW, "hibr", "apps",
                                                  "demo.hibr")),
              (rc, out, err))
        rc, out, err = apps("--system", "installed", env=ES)
        rc2, out2, err2 = apps("installed", env=ES)
        check("and each scope lists its own and says which it is, since "
              "/var/lib is not where anybody would look",
              rc == 0 and "apps/demo" in out and "system" in out
              and "apps/demo" not in out2,
              (out, out2))
        # A script is installed from an unverified catalogue with a
        # warning because you can read it first (ADR 0041). That holds
        # for your own home and fails for everybody's: the people who
        # run it are not the person who read it.
        rc, out, err = apps("--system", "install", "apps/demo",
                            env=dict(ES, HIBR_APPS_KEYFP="0" * 40))
        check("an unsigned catalogue is refused at system scope, where "
              "the same script is only warned about at user scope",
              rc != 0 and "every user of this machine" in err, (rc, err))

        # An ordinary account asking for --system is the expected way to
        # find out it needs root, since ADR 0045 makes that root
        # root-owned on purpose. Until 0.99.199 it met mkdir's own
        # `Permission denied` from the middle of the work, after the
        # index had been fetched and a bundle verified (Gitea #239).
        RO = os.path.join(HOME, "roroot")
        os.makedirs(RO, exist_ok=True)
        os.chmod(RO, 0o555)
        ERO = dict(EW, HIBR_SYSDIR=RO)
        try:
            rc, out, err = apps("--system", "install", "apps/demo", env=ERO)
            check("a --system install this account cannot write refuses "
                  "with the command to use, not mkdir's own complaint",
                  rc != 0 and "sudo catalogue install --system" in err
                  and "Permission denied" not in err
                  and not os.path.exists(os.path.join(RO, "apps")),
                  (rc, out, err))
            # Reading is what somebody does before reaching for sudo, and
            # the directory is world-readable by design, so gating it in
            # ap_scope rather than on the write verbs would have been
            # wrong in the direction nobody would have noticed.
            rc, out, err = apps("--system", "installed", env=ERO)
            check("and reading that same scope is not gated at all",
                  rc == 0, (rc, out, err))
        finally:
            os.chmod(RO, 0o755)

        # An administrator's install is authoritative: it is read before
        # a person's own, so a copy there could never run. Refused
        # whatever the versions say, and a copy that was already there
        # when the administrator installed is reported as defunct with
        # the command that frees the space (ADR 0045).
        rc, out, err = apps("install", "apps/demo", env=ES)
        check("a copy of your own is refused once the machine has one, "
              "since the machine's is read first and yours could not run",
              rc != 0 and "the machine already has this" in err
              and not os.path.exists(os.path.join(DW, "hibr", "apps",
                                                  "demo.hibr")),
              (rc, err))
        # The one way into a defunct copy: yours first, the
        # administrator's after. Asked of the paths the install
        # recorded, not of a path rebuilt from kind and name -- an app
        # lives in a folder of its own now, and the first version of
        # this looked for apps/inner.hibr while the file sat at
        # apps/Games/inner.hibr, so the column was blank for exactly
        # the entries it exists to mark.
        apps("--system", "remove", "-f", "apps/demo", env=ES)
        apps("install", "apps/demo", env=ES)
        apps("--system", "install", "apps/demo", env=ES)
        rc, out, err = apps("installed", env=ES)
        rc2, out2, err2 = apps("installed", "-j", env=ES)
        j2 = json.loads(out2)["installed"]
        check("and one that was yours before the administrator installed "
              "is named defunct, with what to do about it",
              rc == 0 and "defunct" in out and "catalogue remove" in out
              and j2["apps/demo"]["defunct"] == "1",
              (out, j2.get("apps/demo")))

        # And `update` has to *notice* a move on its own. An entry can
        # change folder without changing version -- `dir` arrived in
        # 0.99.195 and put every application in one, same bytes, new
        # path -- so an update that compares versions alone leaves it
        # where it was for ever. It shipped that way: twenty-one
        # applications stayed flat on the owner's own machine while the
        # index said they belonged in four folders. Here the state's
        # version is left matching the index, so only the path can
        # trigger it.
        inner_g = os.path.join(DW, "hibr", "apps", "Games", "inner.hibr")
        inner_f = os.path.join(DW, "hibr", "apps", "inner.hibr")
        st = json.load(open(os.path.join(DW, "hibr", "apps.json")))
        st["installed"]["apps/inner"]["paths"] = {"0": "apps/inner.hibr"}
        json.dump(st, open(os.path.join(DW, "hibr", "apps.json"), "w"))
        os.rename(inner_g, inner_f)
        rc, out, err = apps("update", env=EW)
        check("`update` moves an entry whose folder changed even though "
              "its version did not",
              rc == 0 and "moves to" in out
              and os.path.exists(inner_g) and not os.path.exists(inner_f),
              (rc, out, err))
    else:
        for name in ("and installs from a signed one, into the module directory",
                     "a module built for another machine is named and declined",
                     "and one built for another module ABI as well",
                     "a signature by a key this does not pin is not a signature",
                     "a module an app needs is fetched first, and the app says "
                     "which app wanted it",
                     "and the state remembers which app it was fetched for",
                     "a module built for another machine is not listed, and "
                     "`info` still says why",
                     "an entry lands in the folder the index names, and a "
                     "folder that climbs is dropped rather than followed",
                     "and an app that moves into a folder leaves no copy "
                     "behind at the path it used to have",
                     "purge lists nothing while the app that needed the "
                     "module is still installed",
                     "and lists it once that app is gone, saying which app "
                     "it was fetched for",
                     "asked with no terminal to ask on it refuses and names "
                     "the flag, rather than hanging on a read that cannot "
                     "return",
                     "and -y takes it away",
                     "a module installed by name is never purged, however "
                     "little needs it",
                     "`update` moves an entry whose folder changed even "
                     "though its version did not",
                     "--system installs into the machine's own scope and "
                     "leaves the person's alone",
                     "and each scope lists its own and says which it is, "
                     "since /var/lib is not where anybody would look",
                     "an unsigned catalogue is refused at system scope, "
                     "where the same script is only warned about at user "
                     "scope",
                     "a copy of your own is refused once the machine has "
                     "one, since the machine's is read first and yours "
                     "could not run",
                     "and one that was yours before the administrator "
                     "installed is named defunct, with what to do about "
                     "it"):
            rc, out, err = apps("install", "mods/greet", env=E221)
            check(name + " [no gpg here: the refusal says so]",
                  rc != 0 and "no gpg" in err, (rc, err))

finally:
    srv.terminate()
    srv.wait()
    shutil.rmtree(D, True)

report(106)
