#!/usr/bin/env python3
"""The application manager: examples/apps.hibr, against a stand-in index.

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
APPS = tree("examples/apps.hibr")


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
    check("and says outright that nothing identifies the author",
          "no" in out and "signatures" in out, out)
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
    check("and an empty answer is still JSON, so a reader never has to ask "
          "whether it got any",
          rc == 0 and json.loads(out) == {"entries": {}}, out[:120])
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
finally:
    srv.terminate()
    srv.wait()
    shutil.rmtree(D, True)

report(65)
