#!/usr/bin/env python3
"""The YouTube app, against tests/ytserve.py -- a stand-in for YouTube
whose results page carries ytInitialData as YouTube's does and whose watch
page plays a clip through a media source the way YouTube's player does:
init segments, then two-second fragments, and after a seek only the
fragments from there. So the web module's tap, the pipes, the media
player and the app all run here, and nothing reaches the network.

Needs Chromium or Chrome, ffmpeg to make the clip, and FFmpeg's libraries;
without any of them every check is skipped, and the suite says so.
"""
import os, shutil, subprocess, sys, tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import screen as sx
from screen import Term, check, report, tree, load, press, release

if len(sys.argv) > 1:
    sx.HIBR = os.path.abspath(sys.argv[1])

D = tempfile.mkdtemp(prefix="hibr-yt-")
MEDIA = os.path.join(D, "media")
os.makedirs(MEDIA)


def ff(*args):
    return subprocess.run(["ffmpeg", "-loglevel", "error", "-y"] + list(args),
                          cwd=MEDIA, capture_output=True).returncode == 0


def have_browser():
    return any(shutil.which(n) for n in ("chromium", "chromium-browser", "google-chrome",
                                          "google-chrome-stable", "chrome")) or \
        bool(os.environ.get("HIBR_WEB_BROWSER"))


have = have_browser() and shutil.which("ffmpeg") is not None
if have:
    have = (ff("-f", "lavfi", "-i", "testsrc=size=256x144:rate=24:duration=12",
               "-c:v", "libvpx-vp9", "-b:v", "200k", "-g", "48", "-keyint_min", "48",
               "-an", "v.webm")
            and ff("-f", "lavfi", "-i", "sine=frequency=440:duration=12", "-c:a",
                   "libopus", "a.webm")
            and ff("-i", "v.webm", "-c", "copy", "-f", "dash", "-dash_segment_type", "webm",
                   "-seg_duration", "2", "-init_seg_name", "vinit.webm",
                   "-media_seg_name", "vseg-$Number$.webm", "v.mpd")
            and ff("-i", "a.webm", "-c", "copy", "-f", "dash", "-dash_segment_type", "webm",
                   "-seg_duration", "2", "-init_seg_name", "ainit.webm",
                   "-media_seg_name", "aseg-$Number$.webm", "a.mpd"))
if have:
    r = subprocess.run([sx.HIBR, "-c", "mod load %s; media list" %
                        tree("build/mods/media.so")], capture_output=True, text=True)
    have = "no FFmpeg" not in r.stderr
if not have:
    print("no Chromium, no ffmpeg or no FFmpeg libraries: every check skipped")
    shutil.rmtree(D, True)
    report(0)
    sys.exit(0)

srv = subprocess.Popen([sys.executable, tree("tests/ytserve.py"), MEDIA],
                       stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True)
BASE = "http://127.0.0.1:%s" % srv.stdout.readline().split()[1]
PROF = os.path.join(D, "profile")


def yrun(phases, arg="pattern", rows=28, cols=90, extra=""):
    """A desktop with the YouTube app open on a search; then each phase:
    keys to send (a callable gets the screen and gives them) and what to
    wait for on the screen."""
    sess = os.path.join(D, "session.hibr")
    open(sess, "w").write(
        "%s. %s\n. %s\n%s\ndt_open\ndt_new \"YouTube\" 24 80 1 2 youtube \"%s\"\n"
        "dt_run\ndt_close\n"
        % (load("console", "web", "media"), tree("examples/desktop/desktop.hibr"),
           tree("examples/desktop/apps/Internet/youtube.hibr"), extra, arg))
    t = Term(sess, rows=rows, cols=cols, settle=2.0,
             env={"YT_BASE": BASE, "HIBR_WEB_PROFILE": PROF, "HIBR_MEDIA_AUDIO": "none"})
    shots = []
    for feed, until in phases:
        if callable(feed):
            feed = feed(t.screen())
        if feed:
            t.keys(list(feed), settle=0.4)
        for _ in range(50):
            sc = t.screen()
            if until is None or (until(sc) if callable(until) else
                                 sc.find(until) is not None):
                break
            t.collect(0.3)
        shots.append(t.screen())
    t.quit(b"q", 1.0)
    return shots


def playing(sc):
    return "playing" in sc.text() and sc.find("▀") is not None


def at(sc):
    """The time the player's bar shows, in seconds."""
    for r in range(sc.rows):
        line = sc.row(r)
        i = line.find(" / 0:12")
        if i > 0:
            m, s = line[:i].split()[-1].split(":")
            return int(m) * 60 + int(s)
    return -1


try:
    s = yrun([([], "A test pattern (pattern)")])
    check("a search lists what the results page's own data holds: title, "
          "channel, length", s[0].find("A test pattern (pattern)") is not None and
          s[0].find("Pattern Channel") is not None and s[0].find("0:12") is not None and
          s[0].find("Third result (pattern)") is not None, s[0])

    s = yrun([([], "A test pattern"), ([b"\r"], playing), ([], lambda sc: at(sc) >= 1),
              ([b" "], "paused"), ([1.5], None), ([b" "], "playing")])
    check("enter plays the chosen result: its title from the page's player, "
          "the picture in the clip's colours, the clock going",
          s[1].find("A test pattern -- Pattern Channel") is not None and
          s[2].find("▀") is not None and at(s[2]) >= 1 and
          len({s[2].style(8, c)["bg"] for c in range(6, 78, 4)} - {None}) >= 3,
          (at(s[2]), s[2].dump()))
    check("space pauses it, and the clock stops", "paused" in s[3].text() and
          at(s[3]) == at(s[4]), (s[3].text()[-300:], s[4].text()[-300:]))
    check("and space plays it again", "playing" in s[5].text(), s[5])

    s = yrun([([], "A test pattern"), ([b"\r"], playing), ([1.0], None),
              ([b"\x1b[C"], lambda sc: at(sc) >= 6), ([1.5], None)])
    check("right goes five seconds on, and the picture goes on from there",
          at(s[3]) >= 6 and at(s[4]) >= at(s[3]) and s[4].find("▀") is not None,
          (at(s[2]), at(s[3]), at(s[4])))

    s = yrun([([], "A test pattern"), ([b"\r"], playing), ([b"m"], "ascii"),
              ([1.0], None), ([b"m"], "mono"), ([b"m"], "half")])
    check("m draws the picture as ASCII, then plain, then half blocks again",
          any(c in s[3].text() for c in "#%@*") and s[3].find("▀") is None and
          "mono" in s[4].text() and "half" in s[5].text(), s[3])

    s = yrun([([], "A test pattern"), ([b"\r"], playing), ([b"\x1b"], "Third result")])
    check("escape stops it and goes back to the results",
          s[2].find("Third result (pattern)") is not None and
          "playing" not in s[2].text(), s[2])

    s = yrun([([], "A test pattern"), ([b"\x1b[B", b"\x1b[B", b"\r"], playing)])
    check("down and enter play another result",
          s[1].find("Third result -- Pattern Channel") is not None, s[1])

    s = yrun([([], "A test pattern"), ([b"/"] + [b"\x15"] + [c.encode() for c in "zebra"] +
                                        [b"\r"], "A test pattern (zebra)")])
    check("/ goes back to the search field, and a new search lists afresh",
          s[1].find("A test pattern (zebra)") is not None and
          s[1].find("(pattern)") is None, s[1])

    sess = os.path.join(D, "s2.hibr")
    open(sess, "w").write(
        "%s. %s\n. %s\n. %s\nCP_PANEDIRS+=(\"%s\")\ncp_panes\ndt_open\n"
        "dt_new \"Control Panel\" 24 76 1 1 panel tube\ndt_run\ndt_close\n"
        % (load("console"), tree("examples/desktop/desktop.hibr"),
           tree("examples/desktop/apps/Internet/youtube.hibr"),
           tree("examples/desktop/apps/panel.hibr"), tree("examples/desktop/control-panel")))
    t = Term(sess, rows=28, cols=90, settle=1.5)
    sc = t.screen()
    t.quit(b"q", 1.0)
    check("Control Panel > YouTube holds the picture, the quality, the frames "
          "and the detail", sc.find("Quality Fetched") is not None and
          sc.find("Frames a Second") is not None and sc.find("Colour Detail") is not None
          and sc.find("tiny") is not None, sc)
finally:
    srv.kill()
    srv.wait()
    shutil.rmtree(D, True)

report(11)
