#!/usr/bin/env python3
"""The media module: FFmpeg's libraries driven from hibr, sound to a WAV
file standing in for a device, and the picture drawn through a pty.

The clips are made here with the ffmpeg program -- a test pattern and a
tone -- so the suite knows exactly what it should see and hear. Without
ffmpeg, or without the libraries, every check is skipped and the suite
says so.
"""
import os, shutil, struct, subprocess, sys, tempfile, wave

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import screen as sx
from screen import Term, check, report, tree

if len(sys.argv) > 1:
    sx.HIBR = os.path.abspath(sys.argv[1])

D = tempfile.mkdtemp(prefix="hibr-media-")
# Under the sanitizers decoding is several times slower: timing windows
# widen by this much, and nothing else changes.
SLOW = 4.0 if os.environ.get("ASAN_OPTIONS") else 1.0


def ff(*args):
    return subprocess.run(["ffmpeg", "-loglevel", "error", "-y"] + list(args),
                          cwd=D, capture_output=True).returncode == 0


def hb(script, wav=None, env=None):
    e = dict(os.environ, HIBR_MEDIA_AUDIO="wav:" + wav if wav else "none")
    e.update(env or {})
    r = subprocess.run([sx.HIBR, "-c", "mod load %s\n%s" %
                        (tree("build/mods/media.so"), script)],
                       env=e, capture_output=True, text=True, timeout=120, cwd=D)
    return r.stdout, r.stderr


def tone(path, skip=0.1, span=0.5):
    """The frequency and loudness of a WAV file's first channel."""
    w = wave.open(path)
    n, r, c = w.getnframes(), w.getframerate(), w.getnchannels()
    d = struct.unpack("<%dh" % (n * c), w.readframes(n))[::c]
    seg = d[int(r * skip):int(r * (skip + span))]
    z = sum(1 for a, b in zip(seg, seg[1:]) if (a < 0) != (b < 0))
    return z / 2 / span, max(abs(x) for x in seg), n / r


have = shutil.which("ffmpeg") is not None
if have:
    have = (ff("-f", "lavfi", "-i", "testsrc=size=320x180:rate=25:duration=3",
               "-f", "lavfi", "-i", "sine=frequency=440:duration=3:sample_rate=48000",
               "-c:v", "libx264", "-pix_fmt", "yuv420p", "-c:a", "aac", "-shortest", "t.mp4")
            and ff("-f", "lavfi", "-i", "testsrc=size=320x180:rate=25:duration=3",
                   "-f", "lavfi", "-i", "sine=frequency=440:duration=3",
                   "-c:v", "libvpx-vp9", "-c:a", "libopus", "-shortest", "t.webm")
            and ff("-f", "lavfi", "-i", "sine=frequency=880:duration=2", "a.mp3")
            and ff("-f", "lavfi", "-i", "testsrc=size=160x90:rate=10:duration=2",
                   "-c:v", "libx264", "-pix_fmt", "yuv420p", "v.mp4"))
if have:
    out, err = hb("media list")
    have = "no FFmpeg" not in err
if not have:
    print("no ffmpeg, or no FFmpeg libraries: every check skipped")
    shutil.rmtree(D, True)
    report(0)
    sys.exit(0)

out, err = hb("p := media open t.mp4 -p; i := media info $p; "
              'echo "${i["video"]} ${i["audio"]} ${i["width"]}x${i["height"]} '
              '${i["dur"]} ${i["paused"]} ${i["output"]} ${i["mode"]}"; media list')
check("open reads the streams: codecs, size, length; opened paused it says so",
      out.split("\n")[:2] == ["h264 aac 320x180 3.000 1 none half", "1"], out + err)
W = os.path.join(D, "out.wav")
out, err = hb("p := media open t.mp4; sleep 1.5; media close $p", wav=W)
f, peak, secs = tone(W)
check("the sound decoded is the tone that was encoded: 440 Hz, its own level",
      abs(f - 440) < 3 and 3500 < peak < 4700 and 1.0 < secs < 2.2,
      (f, peak, secs, err))
out, err = hb("p := media open t.mp4; media volume $p 25 > /dev/null; sleep 1.2; "
              "media volume $p 500; media volume $p -3; media close $p", wav=W)
f, peak2, secs = tone(W)
check("volume scales the sound, and is kept between 0 and 100",
      abs(f - 440) < 3 and abs(peak2 - peak / 4) < 250 and out == "100\n0\n",
      (peak, peak2, out, err))
out, err = hb("p := media open t.webm; i := media info $p; "
              'echo "${i["video"]} ${i["audio"]}"; sleep 0.6; media frame $p; echo $?')
check("WebM with VP9 and Opus plays too", out == "vp9 opus\n0\n", out + err)
out, err = hb("p := media open a.mp3; i := media info $p; "
              'echo "[${i["video"]}] ${i["audio"]}"; sleep 0.8; media frame $p; '
              "echo frame $?; media draw $p 0 0; echo draw $?", wav=W)
f, peak, secs = tone(W)
check("sound alone plays, and has no frame to draw",
      out.startswith("[] mp3\nframe 1\ndraw 1") and abs(f - 880) < 4, (out, err, f))
out, err = hb("p := media open v.mp4; i := media info $p; "
              'echo "${i["video"]} [${i["audio"]}]"; sleep 0.5; media frame $p; echo $?')
check("a picture with no sound plays on the wall clock", out == "h264 []\n0\n",
      out + err)
out, err = hb("p := media open t.mp4; sleep 0.5; media seek $p 2.2; sleep 0.3; "
              'i := media info $p; echo "${i["pos"]}"; sleep 1.2; media frame $p; '
              'i := media info $p; echo "${i["pos"]} ${i["ended"]}"')
l = out.split()
check("seek goes to a time; at the end the player says ended and stops at "
      "the length", len(l) == 3 and 2.3 < float(l[0]) < 2.9 and
      l[1:] == ["3.000", "1"], out + err)
out, err = hb("p := media open t.mp4; sleep 0.6; media pause $p; "
              'i := media info $p; a=${i["pos"]}; sleep 0.6; i := media info $p; '
              'echo "$a ${i["pos"]} ${i["paused"]}"; media play $p; sleep 0.5; '
              'i := media info $p; echo "${i["pos"]}"; media seek $p -10; sleep 0.2; '
              'i := media info $p; echo "${i["pos"]}"')
l = out.split()
check("pause stops the clock, play goes on from there, seek is kept inside",
      len(l) == 5 and l[0] == l[1] and l[2] == "1" and
      float(l[0]) < float(l[3]) < float(l[0]) + 0.8 and float(l[4]) < 0.3 * SLOW,
      out + err)
out, err = hb("p := media open t.mp4; media fps $p 0; "
              "for i in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20; do "
              'media frame $p; i := media info $p; echo "${i["pos"]} ${i["shown"]}"; '
              "sleep 0.05; done")
lag = [float(a) - float(b) for a, b in (x.split() for x in out.split("\n") if x)
       if float(b) > 0]
check("the frame shown keeps up with the clock, never more than a frame "
      "and a little behind", lag and max(lag) < 0.07 * SLOW and min(lag) > -0.01,
      (lag, err))
out, err = hb("media open /nonexistent.mp4; echo $?; media open /etc/passwd; echo $?; "
              "media play 99; echo $?; media size 1 2>/dev/null; echo $?")
check("a missing file, something that is not media and a player that is not "
      "there each say so", out == "1\n1\n1\n1\n" and "No such file" in err and
      "Invalid data" in err and "no player 99" in err, out + err)
out, err = hb("p := media open t.mp4 -p; sleep 0.3; media frame $p; media draw $p 0 0; "
              "echo $?; q := media open t.webm; media list; media close all; media list")
check("drawing needs a display, and close all closes every player",
      out == "1\n1 2\n\n" and "no display" in err, out + err)


def view(args, feed=(), rows=14, cols=48, cell=None, env=None):
    t = Term(tree("examples/play.hibr"), *args, rows=rows, cols=cols, settle=1.2,
             cellw=cell[0] if cell else 0, cellh=cell[1] if cell else 0,
             env=dict({"HIBR_MEDIA_AUDIO": "none",
                       "HIBR_MODPATH": tree("build/mods")}, **(env or {})))
    shots = []
    for k in feed:
        if k:
            t.send(k)
        t.collect(0.7)
        shots.append(t.screen())
    t.quit(b"q", 1.0)
    return shots


os.chdir(D)
s = view(["-p", "t.mp4"], [None, b"m", b"m"])
st = [s[0].style(r, c) for r, c in ((4, 9), (4, 23), (4, 37))]
check("the picture is drawn as half blocks in the clip's own colours, kept "
      "to its shape", s[0].find("▀") is not None and
      all(x["fg"] and x["bg"] for x in st) and len({x["bg"] for x in st}) == 3 and
      s[0].row(13).startswith(" 0:00 / 0:03  paused"), s[0])
check("ascii draws it as characters by brightness, in colour",
      any(ch in s[1].text() for ch in "#%@*") and s[1].find("▀") is None and
      s[1].style(4, 23)["fg"] and "ascii" in s[1].row(13), s[1])
check("mono draws the same characters without colour",
      any(ch in s[2].text() for ch in "#%@*") and s[2].style(4, 23)["fg"] is None and
      "mono" in s[2].row(13), s[2])
s = view(["t.mp4"], [None, b" ", b"\x1b[C"])
check("space pauses, and right seeks five seconds on -- to the end, still paused",
      "paused" in s[1].row(13) and "paused" in s[2].row(13) and
      s[2].row(13).startswith(" 0:03 / 0:03"), s[2])
# A film's frame is a different picture every frame, so the console sends it
# raw where it compresses a still (Gitea #129): compressing one would cost
# more than its bytes save, and this is the only suite that can reach that
# branch -- `img draw` always says a palette was chosen from its picture,
# and the branch is "no palette chosen and not under the text", which is a
# film and nothing else. Mode `sixel` is the picture path whichever protocol
# is in force; the cell size is what makes pictures possible at all.
s = view(["-m", "sixel", "-p", "t.mp4"], [None], cell=(8, 16),
         env={"HIBR_GFX": "kitty"})
p = [a for a in s[0].apc if "a=T" in a]
check("a film's frame goes out as a picture, uncompressed, where a still "
      "would be deflated", p and "f=24" in p[0] and "o=z" not in p[0], p)

out, err = subprocess.run([sx.HIBR, tree("examples/play.hibr"), "--help"],
                          capture_output=True, text=True).stdout, ""
check("play.hibr's --help comes from its declarations",
      "--mode" in out and "--start" in out and "--paused" in out, out)

shutil.rmtree(D, True)
report(18)
