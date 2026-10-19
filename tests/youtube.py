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
import os, re, shutil, subprocess, sys, tempfile

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
# A clip long enough to be well past the end of its own frame queue: the
# twelve-second one ends its input before a decoder racing ahead could
# get ahead of the clock (Gitea #288).
LONG = os.path.join(D, "long")
if have:
    os.makedirs(LONG)
    have = all(subprocess.run(["ffmpeg", "-loglevel", "error", "-y"] + a, cwd=LONG,
                              capture_output=True).returncode == 0 for a in (
        ["-f", "lavfi", "-i", "testsrc=size=256x144:rate=24:duration=40", "-c:v",
         "libvpx-vp9", "-b:v", "200k", "-g", "48", "-keyint_min", "48", "-an", "v.webm"],
        ["-f", "lavfi", "-i", "sine=frequency=440:duration=40", "-c:a", "libopus", "a.webm"],
        ["-i", "v.webm", "-c", "copy", "-f", "dash", "-dash_segment_type", "webm",
         "-seg_duration", "2", "-init_seg_name", "vinit.webm",
         "-media_seg_name", "vseg-$Number$.webm", "v.mpd"],
        ["-i", "a.webm", "-c", "copy", "-f", "dash", "-dash_segment_type", "webm",
         "-seg_duration", "2", "-init_seg_name", "ainit.webm",
         "-media_seg_name", "aseg-$Number$.webm", "a.mpd"]))
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
lsrv = subprocess.Popen([sys.executable, tree("tests/ytserve.py"), LONG, "40"],
                        stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True)
LBASE = "http://127.0.0.1:%s" % lsrv.stdout.readline().split()[1]
PROF = os.path.join(D, "profile")
DATA = os.path.join(D, "data")


SLOW = 4 if os.environ.get("ASAN_OPTIONS") else 1


def yrun(phases, arg="pattern", rows=28, cols=90, extra="", autonext=False,
         cell=None, env=None, caps=None):
    """A desktop with the YouTube app open on a search; then each phase:
    keys to send (a callable gets the screen and gives them) and what to
    wait for on the screen. The next video does not play by itself unless
    asked: the clip is twelve seconds, and a slow run would otherwise reach
    its end in the middle of a check about something else."""
    sess = os.path.join(D, "session.hibr")
    open(sess, "w").write(
        "%s. %s\n. %s\n%s\ndt_open\ndt_new \"YouTube\" 24 80 1 2 youtube \"%s\"\n"
        "dt_run\ndt_close\n"
        % (load("console", "web", "media"), tree("programs/desktop/desktop.hibr"),
           tree("programs/desktop/apps/Internet/youtube.hibr"), extra, arg))
    t = Term(sess, rows=rows, cols=cols, settle=2.0, caps=caps,
             cellw=cell[0] if cell else 0, cellh=cell[1] if cell else 0,
             env=dict({"YT_BASE": BASE, "HIBR_WEB_PROFILE": PROF, "HIBR_MEDIA_AUDIO": "none",
                  "YT_DATA": DATA, "YT_RESUME": "3", "YT_ENDED": "1",
                  "YT_AUTONEXT": "1" if autonext else "0"}, **(env or {})))
    shots = []
    for phase in phases:
        feed, until = phase[0], phase[1]
        secs = phase[2] if len(phase) > 2 else 15
        if callable(feed):
            feed = feed(t.screen())
        if feed:
            t.keys(list(feed), settle=0.4)
        for _ in range(int(secs * SLOW / 0.3)):
            sc = t.screen()
            if until is None or (until(sc) if callable(until) else
                                 sc.find(until) is not None):
                break
            t.collect(0.3)
        shots.append(t.screen())
    t.quit(b"q", 1.0)
    return shots


def playing(sc):
    """Playing, with something of the frame on screen. A half block is what
    the cell modes draw and a bitmap has no glyph at all -- the cells under
    a picture are deliberately blank -- so asking only for the half block
    asked about the mechanism rather than the outcome, and no suite could
    then drive the pixel path at all (Gitea #199)."""
    return "playing" in sc.text() and (sc.find("▀") is not None or sc.images)


def at(sc):
    """The time the player's bar shows, in seconds."""
    for r in range(sc.rows):
        m = re.search(r"(\d+):(\d\d) / \d+:\d\d", sc.row(r))
        if m:
            return int(m.group(1)) * 60 + int(m.group(2))
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

    # A playing video keeps the screen saver away, as Control Panel >
    # YouTube says by default, and a paused one does not (Gitea #277): two
    # seconds of idle time, four of playing with no key, then a pause.
    #
    # The home key before enter is not decoration. Two seconds is from the
    # session's start, and under a full gate the results took longer than
    # that to arrive, so the saver came on before anything was played --
    # rightly, with nothing playing -- and the enter that was meant to play
    # the video only dismissed it: the video never started, and the check
    # read as a playing video letting the saver in. Home does nothing on the
    # first result, or dismisses a saver already showing, so enter always
    # reaches the list. Not up, which was tried first: on the first result
    # up opens the search, and enter then searched instead of playing --
    # the opposite case, when the saver had not come on, and the one the
    # loaded runs could not show. Reproduced 2 runs in 3 under a spinner per core and
    # the desktop suite beside it, by logging what the desktop asked the
    # window: an empty state at every deadline in the failing runs.
    #
    # And a history of its own. The checks above leave this clip in the
    # shared one, so it was taken up seconds in -- 5, 8, 9.5 in a logged
    # run -- and under load the twelve-second clip reached its end inside
    # the four seconds, where the saver rightly comes (Gitea #299).
    s = yrun([([], "A test pattern"), ([b"\x1b[H"], None), ([b"\r"], playing),
              ([4.0], None),
              ([b" "], "paused"), ([3.5], None)],
             env={"DT_SAVERSECS": "2", "DT_SAVER": "clock",
                  "YT_DATA": os.path.join(D, "data-saver")})
    check("a playing video keeps the screen saver away past its idle time",
          "playing" in s[3].text() and "█" not in s[3].text(), s[3])
    check("and a paused one lets it come",
          "█" in s[5].text() and "paused" not in s[5].text(), s[5])

    # A video that has finished lets it come too: the picture stopped
    # moving, whatever the app's own state says. Four seconds rather than
    # two, so that the saver's own start -- one to two idle times after the
    # last frame -- cannot fall before a loaded run's next look at the bar.
    s = yrun([([], "A test pattern"), ([b"\x1b[H"], None), ([b"\r"], playing),
              ([], lambda sc: at(sc) >= 12 or sc.find("█") is not None, 40),
              ([], lambda sc: sc.find("█") is not None, 20)],
             env={"DT_SAVERSECS": "4", "DT_SAVER": "clock",
                  "YT_DATA": os.path.join(D, "data-saver2")})
    check("and so does one that has finished playing, though not before",
          at(s[3]) >= 12 and "█" not in s[3].text() and "█" in s[4].text(),
          (at(s[3]), s[3].text()[-200:], s[4].text()[-200:]))

    s = yrun([([], "A test pattern"), ([b"\r"], playing), ([1.0], None),
              ([b"\x1b[C"], lambda sc: at(sc) >= 6), ([1.5], None)])
    check("right goes five seconds on, and the picture goes on from there",
          at(s[3]) >= 6 and at(s[4]) >= at(s[3]) and s[4].find("▀") is not None,
          (at(s[2]), at(s[3]), at(s[4])))

    # The picture setting starts at `follow` -- Control Panel > Pictures --
    # and m walks the whole list the Picture menu offers, which on a terminal
    # that cannot place pixels resolves to half blocks either way: the bar
    # says which it is following, or which it was told.
    s = yrun([([], "A test pattern"), ([b"\r"], playing), ([b"m"], "  half"),
              ([b"m"], "ascii"), ([1.0], None), ([b"m"], "mono"),
              ([b"m"], "pixels"), ([b"m"], "follow:")])
    check("m walks the picture list: half, ASCII, plain, pixels, and back",
          any(c in s[4].text() for c in "#%@*") and s[4].find("▀") is None and
          "mono" in s[5].text() and "pixels" in s[6].text(), s[4])

    # A window that is not drawn takes no frames, and the picture must
    # still be where the clock is when it is drawn again (Gitea #288).
    # Away long enough for the queue of frames to fill: then the decoder
    # went on dropping the oldest -- the present -- until all it held was
    # frames from seconds ahead, and the picture stood still on coming
    # back while the clock and the sound went on.
    def picture(sc):
        return tuple((sc.style(r, c)["fg"], sc.style(r, c)["bg"])
                     for r in range(3, 20) for c in range(2, 80))
    s = yrun([([], "A test pattern"), ([b"\r"], playing), ([], lambda sc: at(sc) >= 1),
              ([b"\x1b2"], None, 0.1), ([14.0], None), ([b"\x1b1"], "YouTube ["),
              ([0.6], None), ([0.6], None)],
             env={"YT_DATA": os.path.join(D, "data-away"), "YT_BASE": LBASE})
    check("a player on another workspace comes back moving, not on a frame from before",
          len({picture(s[5]), picture(s[6]), picture(s[7])}) > 1 and
          at(s[7]) >= 14, (at(s[5]), at(s[7]), s[7]))

    # The mini player: the picture alone, no border, bottom right, on every
    # workspace; escape goes back to the full player where it was.
    def mini(sc):
        return sc.find("YouTube [") is None and any("\u2580" in sc.row(r)[50:] for r in range(17, 27))
    s = yrun([([], "A test pattern"), ([b"\r"], playing), ([b"i"], mini), ([1.0], None),
              ([b"\x1b2"], None), ([b"\x1b"], "YouTube [")])
    check("i makes it a mini player: the picture alone, no border or title, at the bottom right",
          mini(s[2]) and s[2].find("space pause") is None, s[2])
    check("it is on every workspace", mini(s[4]), s[4])
    check("and escape goes back to the full player, where it was",
          s[5].find("YouTube [") is not None and s[5].find("YouTube [")[0] == 1, s[5])

    # Where the mini player was last left is where it opens (Gitea #304),
    # and its right-click menu carries Sticky and Shadow.
    def minithere(sc):
        return any("▀" in sc.row(r)[4:34] for r in range(2, 8))
    s = yrun([([], "A test pattern"), ([b"\r"], playing), ([b"i"], minithere),
              ([lambda sc: press(4, 12, 2)], "Shadow")],
             env={"YT_MINIGEO": "2 4 6 30"})
    check("the mini player opens where it was last left",
          minithere(s[2]) and not mini(s[2]), s[2])
    check("and its right-click menu has Sticky and Shadow",
          s[3].find("Sticky") is not None and s[3].find("Shadow") is not None,
          s[3])

    s = yrun([([], "A test pattern"), ([b"\r"], playing), ([b"\x1b"], "Third result")])
    check("escape stops it and goes back to the results",
          s[2].find("Third result (pattern)") is not None and
          "playing" not in s[2].text(), s[2])

    s = yrun([([], "A test pattern"), ([b"\x1b[B", b"\x1b[B", b"\r"], playing)])
    check("down and enter play another result",
          s[1].find("Third result -- Pattern Channel") is not None, s[1])

    def selstyle(sc, text):
        hit = sc.find(text)
        return sc.style(hit[0], hit[1]) if hit else {}

    s = yrun([([], "Another clip"), ([b"\x1b[B"], None)])
    st = selstyle(s[1], "Another clip")
    check("the chosen result is lit in the theme's own selection colours, "
          "text on the accent -- never dark on dark",
          st.get("bg") == "#63b3ed" and st.get("fg") == "#1a202c", (st, s[1]))

    # YouTube's own player can give up part way -- "Something went wrong",
    # back to unstarted -- and then nothing more arrives. The app notices
    # and loads the video again, taking it up where it was, rather than
    # freezing on the last picture it had.
    import json, urllib.request
    urllib.request.urlopen(BASE + "/control?fail=vid2:4").read()
    s = yrun([([], "Another clip"), ([b"\x1b[B", b"\r"], playing),
              ([], lambda sc: at(sc) >= 9, 40)])
    loads = json.loads(urllib.request.urlopen(BASE + "/loads").read())
    check("when YouTube's own player stops part way, the video is loaded "
          "again and plays on past where it stopped",
          at(s[-1]) >= 9 and loads.get("vid2", 0) >= 2, (loads, s[-1]))

    # The same, with the new page held back from appending anything for a
    # moment: its media source and buffers are made at once, so the tap's
    # reset reaches the app before any of the new stream does. That is the
    # window in which the resume used to be sent into buffers nobody had
    # filled -- the tap had no init segment of its own to queue back, the
    # page's player was sent to a time in an empty buffer, and what came
    # of it was one stream with no init and the other not at all: nothing
    # decoded ever again and the picture stayed on the last frame it had.
    # Half the runs of the check above were that, Gitea #135.
    def counts():
        return (json.loads(urllib.request.urlopen(BASE + "/loads").read()).get("vid2", 0),
                json.loads(urllib.request.urlopen(BASE + "/early").read()).get("vid2", 0))

    urllib.request.urlopen(BASE + "/control?fail=vid2:4&slow=vid2:1200").read()
    wasl, wase = counts()
    s = yrun([([], "Another clip"), ([b"\x1b[B", b"\r"], playing),
              ([], lambda sc: at(sc) >= 9, 40)])
    nowl, nowe = counts()
    urllib.request.urlopen(BASE + "/control?slow=vid2:0").read()
    check("and it waits for the new stream's own first bytes before taking "
          "it up, rather than seeking into buffers nobody has filled",
          at(s[-1]) >= 9 and nowl - wasl >= 2 and nowe == wase,
          (nowl - wasl, nowe - wase, s[-1]))

    s = yrun([([], "A test pattern"), ([b"/"] + [b"\x15"] + [c.encode() for c in "zebra"] +
                                        [b"\r"], "A test pattern (zebra)")])
    check("/ goes back to the search field, and a new search lists afresh",
          s[1].find("A test pattern (zebra)") is not None and
          s[1].find("(pattern)") is None, s[1])

    s = yrun([([], "A playlist of patterns")])
    check("a search lists playlists and channels too, each saying what it opens",
          s[0].find("playlist, 3 videos") is not None and
          any(re.search(r"Pattern Channel +channel\u2502", s[0].row(r))
              for r in range(s[0].rows)), s[0])

    s = yrun([([], "A playlist of patterns"), ([b"\x1b[B"] * 3 + [b"\r"], "Listed Third"),
              ([b"\x1b"], "A playlist of patterns  "), ([b"\x1b[B"] * 4 + [b"\r"], "Upload A test")])
    check("enter on a playlist shows its videos under its own title",
          s[1].find("Playlist: A playlist of patterns") is not None and
          s[1].find("Listed A test pattern") is not None, s[1])
    check("escape goes back to the results it came from",
          s[2].find("Results for pattern") is not None and
          s[2].find("A playlist of patterns") is not None, s[2])
    check("and a channel shows its videos, the channel's name above them",
          s[3].find("Channel: Pattern Channel") is not None and
          s[3].find("Upload Third result") is not None, s[3])

    s = yrun([([], "Listed A test"), ([b"\r"], playing),
              ([b"n"], " Another clip -- Second Channel"),
              ([b"p"], " A test pattern -- Pattern Channel"),
              ([b"\x1b[1;2C"], " Another clip -- Second Channel")],
             arg="https://www.youtube.com/playlist?list=PL1", autonext=True)
    check("a playlist's address typed in opens the playlist",
          s[0].find("Playlist: Test playlist") is not None, s[0])
    check("n plays the next video in the list and p the one before",
          s[2].find("Another clip -- Second Channel") is not None and
          s[3].find("A test pattern -- Pattern Channel") is not None,
          s[2].dump() + s[3].dump())
    check("at a video's end the next in the list plays by itself",
          s[4].find("Another clip -- Second Channel") is not None, s[4])

    s = yrun([([], "A test pattern"), ([b"\r"], playing),
              ([b"\x1b[C"], lambda sc: at(sc) >= 6), ([b"\x1b"], "Results for"),
              ([b"h"], "History"), ([b"\r"], playing), ([], lambda sc: at(sc) >= 5)])
    hist = open(os.path.join(DATA, "history.tsv")).read() if os.path.exists(
        os.path.join(DATA, "history.tsv")) else ""
    check("what was watched is in the history, with where it was left",
          s[4].find("A test pattern") is not None and "\tvid1\tA test pattern" in hist, hist)
    check("and choosing it again takes it up there",
          at(s[6]) >= 5 and s[6].find("Taken up at") is not None, s[6])

    s = yrun([([], "Another clip"), ([b"\x1b[B", b"f"], "Kept as a favourite"),
              ([b"v"], "Favourites"), ([b"f"], "No favourites yet")])
    check("f keeps a favourite, starred in the list",
          s[1].find("\u2731Another clip") is not None, s[1])
    check("v lists the favourites, and f there takes one out",
          s[2].find("Another clip (pattern)") is not None and
          s[3].find("No favourites yet") is not None, s[2].dump() + s[3].dump())

    sess = os.path.join(D, "s2.hibr")
    open(sess, "w").write(
        "%s. %s\n. %s\n. %s\nCP_PANEDIRS+=(\"%s\")\ncp_panes\ndt_open\n"
        "dt_new \"Control Panel\" 24 76 1 1 panel tube\ndt_run\ndt_close\n"
        % (load("console"), tree("programs/desktop/desktop.hibr"),
           tree("programs/desktop/apps/Internet/youtube.hibr"),
           tree("programs/desktop/system/panel.hibr"), tree("programs/desktop/control-panel")))
    t = Term(sess, rows=28, cols=90, settle=1.5)
    sc = t.screen()
    # Every check above runs on a terminal that reports no cell size, so the
    # player can only ever have drawn cells: until this one, nothing had
    # driven its pixel path at all, which is how "blocks even in pixel mode"
    # could be reported against code no suite had asked (Gitea #199). Here
    # the terminal says what a cell measures, so `follow` resolves to pixels
    # and the frame goes out as a bitmap -- one placement, and no half block
    # anywhere, since the cells under a picture are left blank on purpose.
    s = yrun([([], "A test pattern"), ([b"\r"], playing),
              ([], lambda sc: at(sc) >= 1)], cell=(8, 16),
             env={"HIBR_GFX": "kitty"})
    check("on a terminal that can place pixels the frame is a picture, not "
          "cells", s[1].images and s[1].find("▀") is None, (s[1].images, s[1]))
    check("and the player says which it is drawing", "pixels" in s[2].text(),
          s[2])

    # The same, on the path every real session takes: nothing forced, and a
    # terminal that *answers* the kitty capability query. HIBR_GFX skips
    # that question entirely, so every pixel check here had been driving
    # the one path no desktop is on -- and the owner's report was from the
    # other one (Gitea #199).
    s = yrun([([], "A test pattern"), ([b"\r"], playing),
              ([], lambda sc: at(sc) >= 1)], cell=(8, 16), caps=["kitty"])
    check("and the same where the terminal answers for itself rather than "
          "being told, which is the path a real session is on",
          s[1].images and s[1].find("▀") is None, (s[1].images, s[1]))

    # The same session on a terminal that says nothing about its cells: the
    # player wanted pixels, cannot have them, and until 0.99.161 drew blocks
    # without saying why -- which reads as the setting being ignored rather
    # than the terminal not offering one (Gitea #199).
    s = yrun([([], "A test pattern"), ([b"\r"], playing)])
    check("where the terminal can place none, it says so beside the mode",
          "no pixels here" in s[1].text() and s[1].find("▀") is not None, s[1])

    # And the case that reads as the player ignoring the setting: a cell
    # size is known, so `dt_imgpix`'s own gate passes and the mode resolves
    # to **pixels**, and the display then declines because nothing has
    # answered about pictures -- so the pixel path falls through to cells
    # and the bar said `pixels` over a screen of blocks. `mode` is what was
    # asked for; `media info`'s `drawn` is what happened, and the bar shows
    # that now. This is the reading that tells the four layers apart, and
    # not having it is why this ticket outlived a hundred releases.
    s = yrun([([], "A test pattern"), ([b"\r"], playing)], cell=(8, 16),
             env={"DT_IMGMODE": "pixels"})
    check("and where pixels were resolved but the display declined, the bar "
          "says what was drawn rather than what was asked for",
          "drawn as blocks" in s[1].text() and s[1].find("▀") is not None
          and not s[1].images, s[1])

    t.quit(b"q", 1.0)
    check("Control Panel > YouTube holds the picture, the quality, the frames "
          "and the detail", sc.find("Quality Fetched") is not None and
          sc.find("Frames a Second") is not None and sc.find("Colour Detail") is not None
          and sc.find("tiny") is not None, sc)
finally:
    srv.kill()
    lsrv.kill()
    srv.wait()
    shutil.rmtree(D, True)

report(39)
