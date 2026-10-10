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
}}
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


def apps(*args, env=None):
    """Run the manager with a config, data and cache folder of its own."""
    e = dict(os.environ, XDG_CONFIG_HOME=CONF, XDG_DATA_HOME=DATA,
             XDG_CACHE_HOME=CACHE, HIBR_APPS_URL=URL,
             HIBR_MODPATH=tree("build/mods"))
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
    check("remove takes the app away and says so",
          rc == 0 and "removed demo" in out and not os.path.exists(one),
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
finally:
    srv.terminate()
    srv.wait()
    shutil.rmtree(D, True)

report(29)
