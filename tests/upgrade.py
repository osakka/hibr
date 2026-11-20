#!/usr/bin/env python3
"""After hibr is upgraded, the catalogue brings what it installed up to date.

    python3 tests/upgrade.py [path-to-hibr]
    UPGRADE_CAT=path python3 tests/upgrade.py   # the catalogue half only,
                                                # against another catalogue

The owner's decision (2026-11-20): the first time a desktop starts on a
newer hibr than the last such update ran against, every installed entry
with a newer version is updated without asking, and one note names each.
Two halves, each checked here:

  - the catalogue's: `catalogue -j update` answers a JSON line for each
    thing it did, asks the network nothing when nothing is installed, and
    takes away a copy of its own that hides a newer one hibr ships -- but
    names, and never touches, a copy that was edited or a file it never
    placed;
  - the desktop's (one pty session): the update runs as a job on the
    first start, one note says what it did, the stamp is written, and a
    second start on the same hibr asks nothing.

The index is served by tests/davserve.py, as tests/appmgr.py serves its
own, and `catalogue` on the PATH is this tree's, through a shim that
counts its runs: /usr/bin/catalogue is the installed one.
"""
import hashlib
import json
import os
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import screen as sx
from screen import Term, check, report, load, tree

if len(sys.argv) > 1:
    sx.HIBR = os.path.abspath(sys.argv[1])

CAT = os.environ.get("UPGRADE_CAT") or tree("programs/catalogue.hibr")
ONLYCAT = bool(os.environ.get("UPGRADE_CAT"))
D = tempfile.mkdtemp(prefix="hibr-upgrade-")
WWW = os.path.join(D, "www")
os.makedirs(WWW)
VER = subprocess.run([sx.HIBR, "-c", 'echo "$HIBR_VERSION"'],
                     capture_output=True, text=True).stdout.strip()


def put(name, data):
    """A file in the served folder; its sha256 back."""
    p = os.path.join(WWW, name)
    os.makedirs(os.path.dirname(p), exist_ok=True)
    with open(p, "wb") as f:
        f.write(data.encode())
    return hashlib.sha256(data.encode()).hexdigest()


def app(name, title, ver):
    return ('app %s "%s, for the upgrade suite" %s\n'
            'command -v dt_app > /dev/null && dt_app %s "%s" 8 30 once "%s"\n'
            % (name, title, ver, name, title, title[0]))


def entry(name, title, ver):
    return {"title": title, "version": ver, "kind": "apps",
            "bundle": "bundles/%s-%s.hibr" % (name, ver),
            "sha256": put("bundles/%s-%s.hibr" % (name, ver), app(name, title, ver))}


# What was installed, and what the catalogue offers after the upgrade:
# Audio has moved on; Image Viewer, Clock and Calculator are no longer in
# it, because hibr ships them now.
# And two the person changed by hand after they were placed: Sheet, which the
# catalogue has moved on from, and Notes, which it published again at the
# same version with other bytes (a fix without a new number).
NOTES2 = app("notes", "Notes", "1.0") + "# fixed in the catalogue\n"
OLD = {"apps": {"audio": entry("audio", "Audio", "0.7.2"),
                "sheet": entry("sheet", "Sheet", "1.0"),
                "notes": entry("notes", "Notes", "1.0"),
                "imgview": entry("imgview", "Image Viewer", "1.0"),
                "clock": entry("clock", "Clock", "1.0"),
                "calc": entry("calc", "Calculator", "2.0")}}
NEW = {"apps": {"audio": entry("audio", "Audio", "0.7.3"),
                "sheet": entry("sheet", "Sheet", "1.1"),
                "notes": {"title": "Notes", "version": "1.0", "kind": "apps",
                          "bundle": "bundles/notes-1.0b.hibr",
                          "sha256": put("bundles/notes-1.0b.hibr", NOTES2)}}}
put("old.json", json.dumps(OLD))
put("new.json", json.dumps(NEW))

# What hibr "ships", for the catalogue's one question of the desktop: a
# tree of its own, so the installed one is never what is compared with.
SHIP = os.path.join(D, "ship")
for n, v in (("imgview", "1.2.1"), ("clock", "1.1"), ("puzzle", "1.0"),
             ("calc", "1.0")):
    os.makedirs(os.path.join(SHIP, "apps", "Accessories"), exist_ok=True)
    open(os.path.join(SHIP, "apps", "Accessories", n + ".hibr"), "w").write(
        app(n, n.title(), v))

srv = subprocess.Popen(
    [sys.executable, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                  "davserve.py"), "--root", WWW, "--auth", "none"],
    stdout=subprocess.PIPE, text=True)
PORT = int(srv.stdout.readline().split()[1])


def url(ix):
    return "http://127.0.0.1:%d/%s" % (PORT, ix)


def home(nm):
    """Config, data, cache, state and a TMPDIR of their own."""
    h = os.path.join(D, nm)
    e = {"XDG_CONFIG_HOME": os.path.join(h, "config"),
         "XDG_DATA_HOME": os.path.join(h, "data"),
         "XDG_CACHE_HOME": os.path.join(h, "cache"),
         "XDG_STATE_HOME": os.path.join(h, "state"),
         "TMPDIR": os.path.join(h, "tmp"), "HIBR_DESKTOP": SHIP}
    for v in e.values():
        os.makedirs(v, exist_ok=True)
    return e


def cat(env, *args, ix="new.json"):
    e = dict(os.environ, HIBR_APPS_URL=url(ix),
             HIBR_MODPATH=tree("build/mods"), **env)
    e.pop("HIBR_HOLD", None)
    r = subprocess.run([sx.HIBR, CAT] + list(args), capture_output=True,
                       text=True, env=e, timeout=120)
    return r.returncode, r.stdout, r.stderr


def recs(out):
    """Each line of a -j update, parsed; None for a line that is not JSON."""
    rs = []
    for l in out.splitlines():
        try:
            rs.append(json.loads(l))
        except ValueError:
            rs.append(None)
    return rs


try:
    # Nothing installed: nothing to ask, so a catalogue nobody can reach
    # is not an error and is not even tried.
    E0 = home("empty")
    rc, out, err = cat(E0, "-j", "update", ix="../nothing-here.json")
    E0D = os.path.join(E0["XDG_DATA_HOME"], "hibr")
    check("with nothing installed an update asks the network nothing",
          rc == 0 and not os.path.exists(
              os.path.join(E0["XDG_CACHE_HOME"], "hibr", "index.json"))
          and out == "", "rc=%s out=%r err=%r" % (rc, out, err))

    E = home("one")
    rc, out, err = cat(E, "install", "audio", "imgview", "clock", "calc",
                       "sheet", "notes", ix="old.json")
    DATA = os.path.join(E["XDG_DATA_HOME"], "hibr")
    # The person edits their Clock, and has put a Puzzle of their own in
    # the catalogue's folder by hand.
    for n in ("clock", "sheet", "notes"):
        with open(os.path.join(DATA, "apps", n + ".hibr"), "a") as f:
            f.write("# mine\n")
    open(os.path.join(DATA, "apps", "puzzle.hibr"), "w").write(
        app("puzzle", "Puzzle", "0.5"))
    rc, out, err = cat(E, "-j", "update")
    rs = recs(out)
    by = {r["id"]: r for r in rs if r}
    check("-j update answers JSON and nothing else on standard output",
          rc == 0 and rs and None not in rs, "rc=%s out=%r err=%r" % (rc, out, err))
    a = by.get("apps/audio", {})
    check("an entry with a newer version is updated and says from what to what",
          a.get("op") == "updated" and a.get("title") == "Audio"
          and a.get("from") == "0.7.2" and a.get("to") == "0.7.3"
          and "0.7.3" in open(os.path.join(DATA, "apps", "audio.hibr")).read(),
          out + err)
    i = by.get("apps/imgview", {})
    check("a copy of the catalogue's that hides a newer one hibr ships is taken away, saying so",
          i.get("op") == "shipped" and i.get("to") == "1.2.1"
          and not os.path.exists(os.path.join(DATA, "apps", "imgview.hibr")),
          out + err)
    c = by.get("apps/clock", {})
    check("one the person edited is named and left as it is",
          c.get("op") == "edited"
          and "# mine" in open(os.path.join(DATA, "apps", "clock.hibr")).read(),
          out + err)
    p = by.get("apps/puzzle", {})
    check("a file the catalogue never placed is named and left as it is",
          p.get("op") == "yours" and p.get("from") == "0.5"
          and os.path.exists(os.path.join(DATA, "apps", "puzzle.hibr")),
          out + err)
    for n, why in (("sheet", "the catalogue's is newer"),
                   ("notes", "the catalogue published the same version again")):
        r = by.get("apps/" + n, {})
        check("a copy changed by hand is kept and named when %s" % why,
              r.get("op") == "changed"
              and "# mine" in open(os.path.join(DATA, "apps", n + ".hibr")).read(),
              out + err)
    check("one newer than what hibr ships is not touched",
          "apps/calc" not in by
          and os.path.exists(os.path.join(DATA, "apps", "calc.hibr")), out + err)

    if not ONLYCAT:
        # The desktop's half, on a home with Audio 0.7.2 installed and no
        # stamp: the first start updates it and says so once.
        H = home("desk")
        cat(H, "install", "audio", ix="old.json")
        BIN = os.path.join(D, "bin")
        os.makedirs(BIN)
        RUNS = os.path.join(D, "runs")
        open(os.path.join(BIN, "catalogue"), "w").write(
            '#!/bin/sh\necho run >> %s\nexec %s %s "$@"\n' % (RUNS, sx.HIBR, CAT))
        os.chmod(os.path.join(BIN, "catalogue"), 0o755)
        S = sx.scratch("upgrade.hibr")
        open(S, "w").write(
            "%s. %s\n"
            'DT_APPDIRS=("${XDG_DATA_HOME}/hibr/apps")\n'
            "dt_open\ndt_apps\ndt_run\ndt_close\n"
            % (load("console"), tree("programs/desktop/desktop.hibr")))
        TE = dict(H, PATH=BIN + ":" + os.environ["PATH"],
                  HIBR_APPS_URL=url("new.json"), DT_NOTEMS="60000")
        t = Term(S, env=TE, rows=24, cols=100)
        sc = t.until("Updated for hibr", tries=150)
        t.quit(b"q")
        check("the first start on a newer hibr updates what is installed and one note names it",
              sc.find("Updated for hibr %s: Audio 0.7.3" % VER) is not None, sc)
        check("and only that one note: the rescan it causes says nothing of its own",
              sc.find("was updated: Restart") is None, sc)
        st = os.path.join(H["XDG_STATE_HOME"], "hibr", "catalogue-hibr")
        check("the stamp says which hibr the update ran against",
              os.path.exists(st) and open(st).read().strip() == VER)
        t = Term(S, env=TE, rows=24, cols=100)
        t.keys([b"x"])
        t.quit(b"q")
        n = len(open(RUNS).read().split()) if os.path.exists(RUNS) else 0
        check("a second start on the same hibr asks the catalogue nothing", n == 1,
              "catalogue ran %d times" % n)
        os.unlink(S)
finally:
    srv.terminate()
    srv.wait()

report(9 if ONLYCAT else 14)
