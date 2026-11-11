#!/usr/bin/env python3
"""The Applications menu's programs (Gitea #502): wm/programs.hibr and
Control Panel > Applications Menu (control-panel/menuedit.hibr).

The programs installed on the machine come from desktop entries in a
scratch XDG_DATA_DIRS and XDG_DATA_HOME, each with a rule to keep or
refuse it; launching one runs its command detached with the desktop's
WAYLAND_DISPLAY and DISPLAY, or in a Terminal window; with neither, a note
says what is missing. The editor's dialog is typed into, and what it keeps
is the person's menu.json, which wins over the machine's entries and can
hide one. The programs Screens ran come across once.
"""
import json, os, shutil, sys, tempfile, time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import screen as sx
from screen import Term, check, report, load, tree

D = tempfile.mkdtemp(prefix="hibr-menuedit-")
SLOW = 4 if "ASAN_OPTIONS" in os.environ else 1
SYS = os.path.join(D, "sys")
DATA = os.path.join(D, "data")
OUT = os.path.join(D, "out")
os.makedirs(os.path.join(SYS, "applications", "sub"))
os.makedirs(os.path.join(DATA, "applications"))
os.makedirs(os.path.join(DATA, "hibr"))

# A program that says what it was started with: its arguments and its
# displays, one line.
PROG = os.path.join(D, "fox")
open(PROG, "w").write("#!/bin/sh\nprintf '%%s|%%s|%%s\\n' \"$*\" \"$WAYLAND_DISPLAY\" "
                      "\"$DISPLAY\" >> %s\n" % OUT)
os.chmod(PROG, 0o755)
TOP = os.path.join(D, "top")
open(TOP, "w").write("#!/bin/sh\necho TOP-RAN; sleep 30\n")
os.chmod(TOP, 0o755)


def entry(path, **kv):
    body = "[Desktop Entry]\nType=Application\n"
    for k, v in kv.items():
        body += "%s=%s\n" % (k, v)
    open(path, "w").write(body)


A = os.path.join(SYS, "applications")
entry(os.path.join(A, "fox.desktop"), Name="Fox", Exec=PROG + " %U --x %%",
      Categories="Network;WebBrowser;")
open(os.path.join(A, "fox.desktop"), "a").write(
    "Name[de]=Fuchs\n[Desktop Action new]\nName=New Window\nExec=other\n")
entry(os.path.join(A, "top.desktop"), Name="Top", Exec=TOP, Terminal="true",
      Categories="System;")
entry(os.path.join(A, "ghost.desktop"), Name="Ghost", Exec="ghost", NoDisplay="true")
entry(os.path.join(A, "konly.desktop"), Name="KOnly", Exec="k", OnlyShowIn="KDE;")
entry(os.path.join(A, "miss.desktop"), Name="Missing", Exec="m",
      TryExec="no-such-program-here")
entry(os.path.join(A, "sub", "nest.desktop"), Name="Nested", Exec="nested",
      Categories="Game;")
entry(os.path.join(A, "dup.desktop"), Name="Machine's", Exec="dup")
entry(os.path.join(DATA, "applications", "dup.desktop"), Name="Mine", Exec="dup")
open(os.path.join(DATA, "hibr", "xscreen.tsv"), "w").write(
    "Old X\txclock -update 1\tfit\tfollow\tdefault\t-\t-\n")

ENV = {"XDG_DATA_HOME": DATA, "XDG_DATA_DIRS": SYS, "DT_PROGAUTO": "1"}
MENU = os.path.join(DATA, "hibr", "menu.json")
DUMP = os.path.join(D, "dump")
# What the menu holds: each program's entry, its title and folder.
PANE = ". %s\n" % tree("programs/desktop/control-panel/menuedit.hibr")
SHOW = PANE + ('dt_dump() { local k; for k in "${!DT_APPS[@]}"; do case $k in prog:*) '
        'printf "%%s|%%s|%%s\\n" "$k" "${DT_APPS[$k]["title"]}" "${DT_APPS[$k]["dir"]}";; '
        'esac; done > %s; }\n' % DUMP)


def hibr(body, env=None):
    """The window manager loaded, its programs read, and body run."""
    p = sx.scratch("menuedit-c.hibr")
    open(p, "w").write("%s. %s\nDT_APPDIRS=()\ndt_apps\n%s%s\n"
                       % (load(), tree("programs/desktop/desktop.hibr"), SHOW, body))
    import subprocess
    e = dict(os.environ, **ENV)
    e.update(env or {})
    r = subprocess.run([sx.HIBR, p], env=e, capture_output=True, text=True, timeout=60)
    os.unlink(p)
    return r


def dump():
    try:
        return {l.split("|")[0]: l.rstrip("\n").split("|")[1:] for l in open(DUMP)}
    except OSError:
        return {}


r = hibr("dt_dump")
got = dump()
check("installed programs are on the Applications menu, each in the folder "
      "its categories say, a nested file's id with a dash",
      got.get("prog:fox.desktop") == ["Fox", "Applications/Internet"] and
      got.get("prog:top.desktop") == ["Top", "Applications/System"] and
      got.get("prog:sub-nest.desktop") == ["Nested", "Applications/Games"], (got, r.stderr))
check("and none marked not for display, only for another desktop, or whose "
      "TryExec is missing", not any(k in got for k in
                                    ("prog:ghost.desktop", "prog:konly.desktop",
                                     "prog:miss.desktop")), got)
check("the person's own copy of an entry wins over the machine's",
      got.get("prog:dup.desktop", [""])[0] == "Mine", got)
check("and the programs Screens ran came across once, as the person's own",
      got.get("prog:own-0") == ["Old X", "Applications/Other"] and
      json.load(open(MENU)).get("own-0", {}).get("cmd") == "xclock -update 1",
      (got, open(MENU).read() if os.path.exists(MENU) else None))

# Launched: the command without its field codes, detached, with the
# desktop's displays.
r = hibr('dt_launch prog:fox.desktop\n', {"WAYLAND_DISPLAY": "wl-test", "DISPLAY": ":9"})
end = time.time() + 5 * SLOW
while time.time() < end and not (os.path.exists(OUT) and open(OUT).read()):
    time.sleep(0.05)
ran = open(OUT).read() if os.path.exists(OUT) else ""
check("a program starts with its command less its field codes, and the "
      "desktop's Wayland and X displays", ran.strip() == "--x %|wl-test|:9", (ran, r.stderr))

# The editor: Hide, an entry of the person's own typed into the dialog, and
# a program installed later found by a rescan.
r = hibr('pme_hide fox.desktop 1\ndt_dump\n')
got = dump()
check("Hide takes one of the machine's off the menu and keeps only that",
      "prog:fox.desktop" not in got and
      json.load(open(MENU)).get("fox.desktop") == {"hidden": "1"},
      (got, open(MENU).read()))
r = hibr('pme_hide fox.desktop ""\ndt_dump\n')
check("and Show puts it back, forgetting it", "prog:fox.desktop" in dump() and
      "fox.desktop" not in json.load(open(MENU)), open(MENU).read())

entry(os.path.join(A, "later.desktop"), Name="Later", Exec="later",
      Categories="Office;")
r = hibr('dt_rescan > /dev/null 2>&1\ndt_dump\n')
check("a program installed after the desktop started is found by a rescan",
      dump().get("prog:later.desktop") == ["Later", "Applications/Office"], dump())
r = hibr('dt_dump\n', {"DT_PROGAUTO": "0"})
got = dump()
check("with Show Installed Programs off only the person's own are listed",
      list(got) == ["prog:own-0"], got)

# The dialog, typed into in a desktop.
p = sx.scratch("menuedit-d.hibr")
open(p, "w").write("%s. %s\nDT_APPDIRS=()\ndt_apps\n%sdt_open\npme_dialog \"\"\n"
                   "dt_run\ndt_dump\ndt_close\n"
                   % (load("console"), tree("programs/desktop/desktop.hibr"), SHOW))
t = Term(p, env=ENV, settle=1.0)
t.keys([b"Clock", b"\t", b"myclock --face", b"\t", b"\x7f" * 5 + b"Tools/Time/", b"\t",
        b"\x1b[C", b"\t", b"\r"])
t.quit(b"q", 1.0)
os.unlink(p)
hibr("dt_dump")
own = json.load(open(MENU)).get("own-1", {})
check("the dialog keeps a name, a command, a folder inside a folder and a "
      "terminal program, and puts it on the menu",
      own.get("name") == "Clock" and own.get("cmd") == "myclock --face" and
      own.get("dir") == "Tools/Time" and own.get("term") == "1" and
      dump().get("prog:own-1") == ["Clock", "Applications/Tools/Time"],
      (open(MENU).read(), dump(), t.screen().text()[:1200]))

# A terminal program opens in a Terminal window; with no display at all a
# program that needs one says so.
p = sx.scratch("menuedit-t.hibr")
open(p, "w").write("%s. %s\nDT_APPDIRS=(%s)\ndt_apps\ndt_open\n"
                   "dt_launch prog:top.desktop\ndt_launch prog:fox.desktop\ndt_run\ndt_close\n"
                   % (load("console", "pty", "term"), tree("programs/desktop/desktop.hibr"),
                      tree("programs/desktop/apps")))
env = dict(ENV, WAYLAND_DISPLAY=None, DISPLAY=None)
t = Term(p, env=env, settle=1.0)
note = t.until("needs Programs in Windows", 40 * SLOW)
sc = t.until("TOP-RAN", 60 * SLOW)
t.quit(b"q", 1.0)
os.unlink(p)
check("a terminal program opens in a Terminal window",
      sc and t.screen().find("TOP-RAN"), t.screen().text()[:1500])
check("and a program with no display to open on says what it needs",
      note, t.screen().text()[:1500])

shutil.rmtree(D, True)
report(13)
