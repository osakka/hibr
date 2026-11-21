#!/usr/bin/env python3
"""Program Scale (Blit #170): the scale programs draw at on each display,
set by hand in Control Panel > Displays and told to Blit's compositor as the
seventh word of that display's layout line -- `WINDOW layout NAME X Y W H
MHZ [PCT]`, absent meaning 100 -- and only to a compositor at 0.3.130 or
later, since an older one reads a seven-word line as a display it does not
know. Pixels on the wire stay the desktop's.

Held, as every desktop of the owner's is, since the Displays pane and the
displays' names both come from hold: a stand-in Blit (frameserve.py) and a
stand-in compositor (natserve.py) of the version each run says. The first
run chooses 150% for the display in the pane and back to 100%, and reads
each layout the compositor was told and the settings saved; the second
starts a compositor at 0.3.129 with 150% already chosen, which must be told
the old six words.
"""
import json, os, re, shutil, subprocess, sys, tempfile, time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import screen as sx
from screen import Term, check, report, load, tree, press, release

HERE = os.path.dirname(os.path.abspath(__file__))
SLOW = 4 if "ASAN_OPTIONS" in os.environ else 1
D = tempfile.mkdtemp(prefix="hibr-natscale-")


def lines(lg):
    try:
        return [json.loads(l) for l in open(lg) if l.strip()]
    except (OSError, ValueError):
        return []


def wait(cond, t=4.0):
    end = time.time() + t * SLOW
    while time.time() < end:
        v = cond()
        if v:
            return v
        time.sleep(0.05)
    return cond()


def held(tag, version, preset=None):
    """A held desktop with a compositor at version, its display attached as
    `top`: (term, compositor log, mark prefix, config folder, session name)."""
    nm = "hibr-nsc%s-%d" % (tag, os.getpid())
    blitlog = os.path.join(D, "blit%s.log" % tag)
    open(blitlog, "w").close()
    blit = subprocess.Popen([sys.executable, os.path.join(HERE, "frameserve.py"),
                             nm, "tok", blitlog, "v3", "hold"])
    wait(lambda: "listening" in open(blitlog).read())
    complog = os.path.join(D, "comp%s.log" % tag)
    open(complog + ".version", "w").write(version + "\n")
    wrap = os.path.join(D, "blit-compositor%s" % tag)
    open(wrap, "w").write("#!/bin/sh\nexec %s %s %s \"$@\"\n"
                          % (sys.executable, os.path.join(HERE, "natserve.py"), complog))
    os.chmod(wrap, 0o755)
    mark = os.path.join(D, "mark%s" % tag)
    conf = os.path.join(D, "conf%s" % tag)
    os.makedirs(conf)
    p = sx.scratch("natscale%s.hibr" % tag)
    open(p, "w").write(
        "%s. %s\nDT_NATBIN=%s\nDT_SUPERVISE=off\n"
        "DT_SYSDIRS+=(\"$DT_HERE/system\")\ndt_sysapps\n"
        "CP_PANEDIRS+=(\"$DT_HERE/control-panel\")\ncp_panes\n"
        "tst_do() { [ -f %s.cp ] && { rm -f %s.cp; dt_panelpane displays; }; "
        "[ -f %s.quit ] && dt_quit; dt_want 200; return 0; }\n"
        "dt_ticker tst_do\ndt_open\ndt_run\ndt_close\n"
        % (load("console"), tree("programs/desktop/desktop.hibr"), wrap, mark, mark, mark))
    hn = "nsc%s-%d" % (tag, os.getpid())
    henv = {"HIBR_GFX": "kitty", "TMPDIR": D, "XDG_RUNTIME_DIR": D, "XDG_CONFIG_HOME": conf}
    if preset is not None:
        henv["DT_PROGSCALE"] = preset
    subprocess.run([sx.HIBR, "-c", load("hold") + 'hold new -d %s "$HIBR" %s' % (hn, p)],
                   env=dict({k: v for k, v in os.environ.items()
                             if k not in ("WAYLAND_DISPLAY", "HIBR_HOLD")}, **henv),
                   capture_output=True, timeout=20)
    a = os.path.join(D, "att%s.hibr" % tag)
    open(a, "w").write(load("hold") + "hold attach -m -n top %s 0 0\n" % hn)
    t = Term(a, rows=30, cols=96, settle=1.0, cellw=10, cellh=20, caps=["kitty"],
             env=dict(henv, BLIT_FRAME_SOCKET="@" + nm, BLIT_FRAME_TOKEN="tok",
                      BLIT_VERSION="0.3.127"))
    return t, complog, mark, conf, hn, blit


def layouts(complog):
    return [x["line"] for x in lines(complog) if x["kind"] == "line" and
            x["line"].split()[:2] == ["WINDOW", "layout"] and x["line"] != "WINDOW layout end"]


def stop(t, mark, hn, blit):
    open(mark + ".quit", "w").close()
    t.collect(1.0)
    t.quit(None, 0.5)
    t.close()
    subprocess.run([sx.HIBR, "-c", load("hold") + "hold kill %s" % hn],
                   env=dict(os.environ, TMPDIR=D), capture_output=True, timeout=20)
    blit.kill()


# --- a compositor that takes the seventh word ---------------------------------
t, complog, mark, conf, hn, blit = held("a", "0.3.130")


def scr():
    t.collect(0.1)
    return t.screen()


lay = wait(lambda: layouts(complog), 10)
check("the display's layout goes to the compositor in six words while its "
      "Program Scale is 100%",
      lay and re.fullmatch(r"WINDOW layout top 0 0 \d+ \d+ \d+", lay[-1]), lay)

open(mark + ".cp", "w").close()
sc = wait(lambda: scr().find("Program Scale:") and scr(), 8)
ps = sc.find("Program Scale:") if sc else None
row = sc.row(ps[0]) if ps else ""
check("Control Panel > Displays has a Program Scale row: the display, then "
      "its scale", ps and "top" in row and "100%" in row, sc or scr())

pct = sc.find_from("100%", ps[0]) if ps else None
if pct and pct[0] == ps[0]:
    t.keys([press(pct[0], pct[1]), release(pct[0], pct[1])])
sc = t.until(lambda s: ps and s.find_from("150%", ps[0] + 1) is not None)
item = sc.find_from("150%", ps[0] + 1) if ps else None
check("its list offers the steps from 100% to 400%",
      item is not None and sc.find_from("400%", ps[0] + 1) is not None and
      sc.find_from("125%", ps[0] + 1) is not None, sc)
if item:
    n0 = len(layouts(complog))
    t.keys([press(item[0], item[1]), release(item[0], item[1])])
    lay = wait(lambda: layouts(complog)[n0:], 6)
else:
    lay = []
check("chosen, 150% goes to the compositor at once as the layout's seventh word",
      lay and re.fullmatch(r"WINDOW layout top 0 0 \d+ \d+ \d+ 150", lay[-1]), lay)
sc = t.until(lambda s: ps and "150%" in s.row(ps[0]))
check("and the row shows it", ps and "150%" in sc.row(ps[0]), sc)
saved = wait(lambda: os.path.exists(os.path.join(conf, "hibr", "desktop.json")) and
             json.load(open(os.path.join(conf, "hibr", "desktop.json")))
             .get("settings", {}).get("DT_PROGSCALE"), 4)
check("kept by the display's name in the settings", saved == "top=150", saved)

# Back to 100%: the word goes, which the compositor reads as 100.
pct = sc.find_from("150%", ps[0]) if ps else None
if pct and pct[0] == ps[0]:
    t.keys([press(pct[0], pct[1]), release(pct[0], pct[1])])
sc = t.until(lambda s: ps and s.find_from("100%", ps[0] + 1) is not None)
item = sc.find_from("100%", ps[0] + 1) if ps else None
if item:
    n0 = len(layouts(complog))
    t.keys([press(item[0], item[1]), release(item[0], item[1])])
    lay = wait(lambda: layouts(complog)[n0:], 6)
else:
    lay = []
check("back at 100%, the layout is six words again",
      lay and re.fullmatch(r"WINDOW layout top 0 0 \d+ \d+ \d+", lay[-1]), lay)
stop(t, mark, hn, blit)

# --- an older compositor is never told it ----------------------------------------
t, complog, mark, conf, hn, blit = held("b", "0.3.129", preset="top=150")
lay = wait(lambda: layouts(complog), 10)
check("a compositor before 0.3.130 is told six words though the display's "
      "Program Scale is 150%",
      lay and all(re.fullmatch(r"WINDOW layout top 0 0 \d+ \d+ \d+", l) for l in lay), lay)
stop(t, mark, hn, blit)
shutil.rmtree(D, True)
report(9)
