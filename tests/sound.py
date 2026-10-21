#!/usr/bin/env python3
"""The desktop's sound (Gitea #306): the speaker on the menu bar, the volume
keys, the sound menu, Control Panel > Sound, and the media module's own
sound reaching the output chosen -- against a real sound server, never a
stand-in: a private PipeWire (with WirePlumber and its PulseAudio side)
started in a runtime folder of this suite's own, with two virtual outputs
and a virtual input, so nothing reaches a speaker and nothing touches the
machine's own sound. Without PipeWire installed every check is skipped,
and the suite says so.

Every other suite starts its desktops with the sound layer off
(tests/screen.py): this one turns it on, for its own server only.
"""
import math, os, re, shutil, struct, subprocess, sys, tempfile, time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import screen as sx
from screen import Term, check, report, tree, load, press, release, drag, wheel

if len(sys.argv) > 1:
    sx.HIBR = os.path.abspath(sys.argv[1])

NEED = ("pipewire", "wireplumber", "pipewire-pulse", "pactl", "parecord",
        "dbus-daemon")
if not all(shutil.which(n) for n in NEED):
    print("no PipeWire, WirePlumber, pipewire-pulse or pactl here: every check skipped")
    report(0)
    sys.exit(0)

RT = tempfile.mkdtemp(prefix="hibr-snd-rt-")
os.chmod(RT, 0o700)
D = tempfile.mkdtemp(prefix="hibr-snd-")
SENV = dict(os.environ, XDG_RUNTIME_DIR=RT, LC_ALL="C")
SENV.pop("PULSE_SERVER", None)
procs = []


def pa(*a):
    """pactl against this suite's own server."""
    return subprocess.run(["pactl"] + list(a), env=SENV, capture_output=True,
                          text=True, timeout=10).stdout


def started():
    """Start the three on a session bus of their own, with no system bus to
    reach: a container's RTKit fails, and each realtime request waited out
    D-Bus's own 25 s timeout before the server's socket was made --
    measured, no socket for half a minute; without the system bus, 1.1 s.
    WirePlumber needs the session bus, and without it exits, leaving a
    server that cannot change its default output ("Not supported")."""
    bus = os.path.join(RT, "bus")
    procs.append(subprocess.Popen(["dbus-daemon", "--session", "--nofork",
                                   "--address=unix:path=" + bus],
                                  stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL))
    time.sleep(0.3)
    e = dict(SENV, DBUS_SYSTEM_BUS_ADDRESS="unix:path=/nonexistent/hibr-sys",
             DBUS_SESSION_BUS_ADDRESS="unix:path=" + bus)
    for n in ("pipewire", "wireplumber", "pipewire-pulse"):
        procs.append(subprocess.Popen([n], env=e, stdout=subprocess.DEVNULL,
                                      stderr=subprocess.DEVNULL))
        time.sleep(0.3)
    for _ in range(50):
        if "Server Name" in pa("info"):
            return True
        time.sleep(0.2)
    return False


# A second server of PipeWire alone, with no PulseAudio side: what a
# machine with PipeWire and no pipewire-pulse has, where pactl reaches
# nothing and the desktop talks to PipeWire through wpctl. Its devices are
# made with pw-cli, there being no PulseAudio modules to load.
RT2 = tempfile.mkdtemp(prefix="hibr-snd-rt-")
os.chmod(RT2, 0o700)
WENV = dict(os.environ, XDG_RUNTIME_DIR=RT2, LC_ALL="C")


def wp(*a):
    """wpctl against the PipeWire-only server."""
    return subprocess.run(["wpctl"] + list(a), env=WENV, capture_output=True,
                          text=True, timeout=10).stdout


def started2():
    bus = os.path.join(RT2, "bus")
    procs.append(subprocess.Popen(["dbus-daemon", "--session", "--nofork",
                                   "--address=unix:path=" + bus],
                                  stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL))
    time.sleep(0.3)
    e = dict(WENV, DBUS_SYSTEM_BUS_ADDRESS="unix:path=/nonexistent/hibr-sys",
             DBUS_SESSION_BUS_ADDRESS="unix:path=" + bus)
    for n in ("pipewire", "wireplumber"):
        procs.append(subprocess.Popen([n], env=e, stdout=subprocess.DEVNULL,
                                      stderr=subprocess.DEVNULL))
        time.sleep(0.5)
    for nm, ds, cl in (("spk", "Speakers", "Audio/Sink"), ("hp", "Headphones", "Audio/Sink"),
                       ("mic", "Microphone", "Audio/Source"), ("cam", "Webcam Mic", "Audio/Source")):
        subprocess.run(["pw-cli", "create-node", "adapter",
                        "{ factory.name=support.null-audio-sink node.name=%s "
                        "node.description=\"%s\" media.class=%s object.linger=true "
                        "audio.position=[FL FR] }" % (nm, ds, cl)],
                       env=WENV, capture_output=True, timeout=10)
    for _ in range(50):
        if "Headphones" in wp("status") and "Webcam Mic" in wp("status"):
            return True
        time.sleep(0.2)
    return False


def wid(desc):
    """A device's id in wpctl's own status, by what it is called."""
    m = re.search(r"(\d+)\. %s\s" % re.escape(desc), wp("status"))
    return m.group(1) if m else ""


def wdefault(section):
    """What wpctl's status stars in its Sinks or Sources."""
    out = wp("status")
    part = out.split(section + ":", 1)[1] if section + ":" in out else ""
    m = re.search(r"\*\s+\d+\. (.+?)\s+\[vol", part)
    return m.group(1) if m else ""


def stop():
    for p in procs:
        p.terminate()
    for p in procs:
        try:
            p.wait(5)
        except subprocess.TimeoutExpired:
            p.kill()


def volume(sink):
    m = re.search(r"(\d+)%", pa("get-sink-volume", sink))
    return int(m.group(1)) if m else -1


def muted(sink):
    return "yes" in pa("get-sink-mute", sink)


def reset():
    """Each session starts from the same state: Speakers the default at
    50%, unmuted, Microphone the input."""
    pa("set-default-sink", "spk")
    pa("set-default-source", "mic")
    for s in ("spk", "hp"):
        pa("set-sink-volume", s, "50%")
        pa("set-sink-mute", s, "0")


if not started():
    print("the private PipeWire did not answer: every check skipped")
    stop()
    shutil.rmtree(RT, True)
    report(0)
    sys.exit(0)
pa("load-module", "module-null-sink", "sink_name=spk",
   "sink_properties=device.description=Speakers")
pa("load-module", "module-null-sink", "sink_name=hp",
   "sink_properties=device.description=Headphones")
pa("load-module", "module-null-sink", "media.class=Audio/Source/Virtual",
   "sink_name=mic", "sink_properties=device.description=Microphone")
time.sleep(0.5)

CPD = tree("programs/desktop/control-panel")
SYS = tree("programs/desktop/system")


def srun(feed, until=None, panel=False, env=None, rows=30, cols=100, settle=2.0,
         rt=None, pre=""):
    """A desktop with this suite's sound server, and with the Control Panel
    open at Sound when asked; then keys, and the screen once `until` (text
    or a function of the screen) holds."""
    s = tempfile.mkdtemp(prefix="hibr-snd-s-", dir=D)
    p = os.path.join(s, "session.hibr")
    open(p, "w").write(
        "%s. %s\n. %s/panel.hibr\nCP_PANEDIRS+=(\"%s\")\ncp_panes\ndt_open\n%s%s"
        "dt_run\ndt_close\n"
        % (load("console"), tree("programs/desktop/desktop.hibr"), SYS, CPD, pre,
           "dt_panelpane sound\n" if panel else ""))
    e = {"HIBR_SOUND": "auto", "XDG_RUNTIME_DIR": rt or RT, "DT_BARTIME": "12:00"}
    if not rt:
        e["PULSE_SERVER"] = None
    e.update(env or {})
    t = Term(p, env=e, rows=rows, cols=cols, settle=settle)
    for _ in range(40):
        if "♪" in t.screen().text():
            break
        t.collect(0.25)
    if feed:
        t.keys(list(feed), settle=0.5)
    for _ in range(40):
        sc = t.screen()
        if until is None or (until(sc) if callable(until) else sc.find(until) is not None):
            break
        t.collect(0.25)
    sc = t.screen()
    t.quit(None, 0.5)
    shutil.rmtree(s, True)
    return sc


def bar(sc):
    return sc.row(0)


def slider(sc, top=1):
    """The slider's level row and its track's column: the first row under
    the bar with a percentage, and the column of the thumb below it."""
    lv = sc.find_from("%", top)
    if lv is None:
        return None
    for r in range(lv[0] + 1, lv[0] + 12):
        c = sc.row(r).find("\u2588")
        if c >= 0:
            return (lv[0], c)
    return None


try:
    reset()
    sc = srun([])
    check("the speaker is on the menu bar with the volume of the output in use",
          "♪ 50%" in bar(sc), bar(sc))

    # The suites' own desktops have no sound layer at all (tests/screen.py),
    # which is what keeps random input off a real machine's volume.
    s0 = tempfile.mkdtemp(prefix="hibr-snd-s-", dir=D)
    p0 = os.path.join(s0, "session.hibr")
    open(p0, "w").write("%s. %s\ndt_open\ndt_run\ndt_close\n"
                        % (load("console"), tree("programs/desktop/desktop.hibr")))
    t0 = Term(p0, env={"XDG_RUNTIME_DIR": RT, "DT_BARTIME": "12:00"},
              rows=30, cols=100, settle=2.0)
    t0.keys([b"\x1b="], settle=0.5)
    sc0 = t0.screen()
    t0.quit(None, 0.5)
    check("a desktop under every other suite has no speaker, and its volume key does nothing",
          "♪" not in bar(sc0) and volume("spk") == 50, (bar(sc0), volume("spk")))

    reset()
    sc = srun([b"\x1b=", b"\x1b="], until=lambda sc: "60%" in bar(sc))
    check("alt-= turns the volume up a step at a time, and the bar follows",
          volume("spk") == 60 and "♪ 60%" in bar(sc), (volume("spk"), bar(sc)))

    reset()
    sc = srun([b"\x1b-"], until=lambda sc: "45%" in bar(sc))
    check("alt-- turns it down", volume("spk") == 45 and "45%" in bar(sc),
          (volume("spk"), bar(sc)))

    reset()
    sc = srun([b"\x1b0"], until=lambda sc: "✕" in bar(sc))
    check("alt-0 mutes it, and the bar says so", muted("spk") and "♪ ✕" in bar(sc),
          (muted("spk"), bar(sc)))

    # A change made anywhere else reaches the bar without a key: the desktop
    # watches the server's own events rather than asking it again.
    reset()
    s1 = tempfile.mkdtemp(prefix="hibr-snd-s-", dir=D)
    p1 = os.path.join(s1, "session.hibr")
    open(p1, "w").write("%s. %s\ndt_open\ndt_run\ndt_close\n"
                        % (load("console"), tree("programs/desktop/desktop.hibr")))
    t1 = Term(p1, env={"HIBR_SOUND": "auto", "PULSE_SERVER": None,
                       "XDG_RUNTIME_DIR": RT, "DT_BARTIME": "12:00"},
              rows=30, cols=100, settle=2.0)
    for _ in range(40):
        if "50%" in t1.screen().row(0):
            break
        t1.collect(0.25)
    pa("set-sink-volume", "spk", "30%")
    seen = t1.until(lambda sc: "♪ 30%" in sc.row(0), 80)
    sc1 = t1.screen()
    pid = t1.pid

    # The watcher is the one program, a child of the desktop's own, and
    # nothing is run to help it (Gitea #339): no sh, grep, sleep or cut.
    def tree_of(p):
        out = []
        try:
            kids = open("/proc/%d/task/%d/children" % (p, p)).read().split()
        except OSError:
            return out
        for k in kids:
            try:
                out.append((int(k), p, open("/proc/%s/comm" % k).read().strip()))
            except OSError:
                continue
            out += tree_of(int(k))
        return out
    wtree = tree_of(pid)
    names = [n for _, _, n in wtree]
    check("the desktop watches with pactl alone, with no sh, grep, sleep or cut",
          "pactl" in names and not set(names) & {"sh", "dash", "bash", "grep", "sleep", "cut"}
          and all(pp == pid for _, pp, n in wtree if n == "pactl"), wtree)
    t1.quit(None, 0.5)
    check("a volume changed by another program reaches the bar with nothing pressed",
          "♪ 30%" in bar(sc1), bar(sc1))

    # The pactl the desktop watches goes when the desktop does: at once when
    # the desktop stops itself, and when it is killed outright the kernel
    # hangs up the pty only the desktop held.
    def subscribers():
        out = []
        for d in os.listdir("/proc"):
            if not d.isdigit():
                continue
            try:
                cmd = open("/proc/%s/cmdline" % d, "rb").read().split(b"\0")
                env = open("/proc/%s/environ" % d, "rb").read()
            except OSError:
                continue
            if cmd[:2] == [b"pactl", b"subscribe"] and ("XDG_RUNTIME_DIR=" + RT).encode() in env:
                out.append(d)
        return out
    end = time.time() + 7
    left = subscribers()
    while left and time.time() < end:
        time.sleep(0.5)
        left = subscribers()
    check("the pactl it watches is ended when the desktop ends", not left, left)

    # The wheel over the speaker.
    reset()
    sc = srun([], until=lambda sc: "50%" in bar(sc))
    col = bar(sc).find("♪")
    sc = srun([b"\x1b[<64;%d;1M" % (col + 1)], until=lambda sc: "55%" in bar(sc))
    check("the wheel over the speaker turns the volume", volume("spk") == 55,
          (volume("spk"), bar(sc)))

    # A left click on the speaker opens the volume as a slider beneath it
    # (Gitea #310): the level, a track with its thumb where the level is,
    # and the mute -- and not the menu, which is the right click's.
    reset()
    sc = srun([press(0, col), release(0, col)],
              until=lambda sc: slider(sc) is not None)
    sl = slider(sc)
    check("a left click on the speaker opens a volume slider beneath it, not the menu",
          sl is not None and sl[0] == 1 and sc.find_from("50%", 1) is not None and
          sc.find("Headphones") is None, sc)
    # The track is ten rows and the slider's last row is blank, as every
    # drop-down's is (the owner, 2026-10-21).
    check("the slider ends in a blank row, under ten rows of track",
          sl is not None and sc.row(sl[0] + 10)[sl[1]] in "│█" and
          sc.row(sl[0] + 11)[sl[1]] == " ", sc)
    if sl:
        tr, tc = sl[0] + 1, sl[1]
        sc = srun([press(0, col), release(0, col), 0.3, press(tr + 2, tc), release(tr + 2, tc)],
                  until=lambda sc: volume("spk") == 78)
        check("a click on its track puts the volume where it lands",
              volume("spk") == 78, sc)
        reset()
        thumb = tr + 4
        sc = srun([press(0, col), release(0, col), 0.3, press(thumb, tc), drag(thumb + 2, tc),
                   drag(tr + 9, tc), drag(tr + 12, tc), release(tr + 12, tc)],
                  until=lambda sc: volume("spk") == 0)
        check("and dragging its thumb carries the volume, past the end of the track too",
              volume("spk") == 0, sc)
        reset()
        sc = srun([press(0, col), release(0, col), 0.3, wheel(tr + 3, tc), wheel(tr + 3, tc)],
                  until=lambda sc: volume("spk") == 60)
        check("the wheel over the slider steps the volume", volume("spk") == 60,
              (volume("spk"), sc))
        reset()
        # No speaker at its foot any more, and a column narrower (the owner,
        # 2026-10-21): mute is `m` here and on the right click's menu.
        sc = srun([press(0, col), release(0, col), 0.3, b"m"],
                  until=lambda sc: muted("spk"))
        check("m mutes from the slider, which has no speaker at its foot",
              muted("spk") and "♪" not in "".join(sc.row(r)[tc - 3:tc + 4]
                                                   for r in range(tr, tr + 12)),
              (muted("spk"), sc))
        reset()
        sc = srun([press(0, col), release(0, col), 0.3, b"\x1b", 0.3])
        check("and escape closes it", slider(sc) is None, sc)
        sc = srun([press(0, col), release(0, col), 0.3, press(20, 60), release(20, 60), 0.3])
        check("as a click anywhere else does", slider(sc) is None and volume("spk") == 50, sc)
        sc = srun([press(0, col), release(0, col), 0.3, press(0, col, 2), release(0, col, 2)],
                  until="Headphones")
        check("and a right click on the speaker while it is open swaps it for the menu",
              slider(sc) is None and sc.find("Headphones") is not None, sc)

    # The sound menu: a right click on the speaker opens it, with the
    # outputs and the one in use ticked; choosing another makes it the
    # server's default.
    reset()
    sc = srun([press(0, col, 2), release(0, col, 2)], until="Headphones")
    check("a right click on the speaker opens the sound menu: the level, the outputs, the inputs",
          sc.find("Volume 50%") is not None and sc.find("Speakers") is not None and
          sc.find("Headphones") is not None and sc.find("Microphone") is not None, sc)
    hp = sc.find("Headphones")
    tick = sc.find("✓ Speakers") or sc.find("✓Speakers")
    check("and the output in use is ticked", tick is not None, sc)
    if hp:
        sc = srun([press(0, col, 2), release(0, col, 2), 0.6, press(hp[0], hp[1] + 2),
                   release(hp[0], hp[1] + 2)],
                  until=lambda sc: pa("get-default-sink").strip() == "hp")
    check("choosing Headphones there makes it the output the server uses",
          pa("get-default-sink").strip() == "hp", pa("get-default-sink"))
    check("and the input is left where it was",
          pa("get-default-source").strip() == "mic", pa("get-default-source"))

    # Control Panel > Sound > Sound System: Auto takes the first that
    # answers; a named one is used, here PipeWire on a machine with its
    # PulseAudio side too; and one that does not answer says so -- this box
    # has no ALSA mixer -- rather than another being taken in its place.
    reset()
    sc = srun([], panel=True, env={"DT_SNDKIND": "PipeWire"}, until="Sound Server")
    r = sc.find("Sound Server")
    check("Sound System set to PipeWire uses PipeWire itself",
          r is not None and "PipeWire" in sc.row(r[0]) and "PulseAudio" not in sc.row(r[0]) and
          "♪ 50%" in bar(sc), (bar(sc), sc))
    if shutil.which("amixer") is None:
        sc = srun([], panel=True, env={"DT_SNDKIND": "ALSA"}, until="not answering")
        check("and one that does not answer says so, with no speaker on the bar",
              sc.find("ALSA is not answering") is not None and "♪" not in bar(sc), sc)
    else:
        check("and one that does not answer says so (skipped: amixer here answers)", True)

    # Control Panel > Sound > Level on Menu Bar off: the speaker alone.
    reset()
    sc = srun([], env={"DT_SNDLEVEL": "0"}, until=lambda sc: "♪" in bar(sc))
    check("with Level on Menu Bar off the speaker shows no percentage",
          "♪" in bar(sc) and "%" not in bar(sc), bar(sc))

    # Two displays stacked, the primary -- and its bar -- below: the sound
    # menu opens under the bar, on the primary, never on the display above.
    reset()
    stack = ('DT_DISPLAYS=("0 0 10 100 self" "10 0 20 100 self")\n'
             'DT_DISPNAMES=(a b)\nDT_PRIMARY_COL=0\nDT_PRIMARY_W=100\n'
             'DT_PRIMARY_ROW=10\nDT_PRIMARY_H=20\n')
    sc = srun([], pre=stack, until=lambda sc: "♪" in sc.row(10))
    scol = sc.row(10).find("♪")
    sc = srun([press(10, scol, 2), release(10, scol, 2)], pre=stack, until="Headphones")
    hp2 = sc.find("Headphones")
    check("with the bar on a display below another, the sound menu opens under it",
          scol > 0 and hp2 is not None and hp2[0] > 10, (scol, hp2, sc))
    sc = srun([press(10, scol), release(10, scol)], pre=stack,
              until=lambda sc: slider(sc, 11) is not None)
    sl2 = slider(sc, 11)
    check("and so does the slider", sl2 is not None and sl2[0] == 11, (sl2, sc))

    # Control Panel > Sound.
    reset()
    sc = srun([], panel=True, until="Sound Server")
    check("Control Panel > Sound names the server and the devices in use",
          sc.find("PulseAudio") is not None and sc.find("Speakers") is not None and
          sc.find("Microphone") is not None and sc.find("Volume (50%)") is not None, sc)
    r = sc.find("Volume on Menu Bar")
    box = sc.row(r[0]).find("[x]") if r else -1
    if r and box > 0:
        # The first click on a row selects it, the second acts: the
        # Control Panel's own rule, for every row.
        sc = srun([press(r[0], box + 1), release(r[0], box + 1), 0.3,
                   press(r[0], box + 1), release(r[0], box + 1)], panel=True,
                  until=lambda sc: "♪" not in sc.row(0))
    check("and Volume on Menu Bar off takes the speaker off the bar",
          r is not None and "♪" not in bar(sc), (bar(sc), sc))

    # The media module plays to the server's chosen output, so the device
    # chosen in the pane is where hibr's own sound goes.
    reset()
    tone = os.path.join(D, "tone.wav")
    with open(tone, "wb") as f:
        n = 48000 * 3
        f.write(b"RIFF" + struct.pack("<I", 36 + n * 2) + b"WAVEfmt " +
                struct.pack("<IHHIIHH", 16, 1, 1, 48000, 96000, 2, 16) +
                b"data" + struct.pack("<I", n * 2) +
                b"".join(struct.pack("<h", int(8000 * math.sin(2 * math.pi * 440 * i / 48000)))
                         for i in range(n)))
    lv = {}
    for sink in ("spk", "hp"):
        pa("set-default-sink", sink)
        rec = os.path.join(D, "rec-%s.wav" % sink)
        other = os.path.join(D, "rec-other-%s.wav" % sink)
        o = "hp" if sink == "spk" else "spk"
        r1 = subprocess.Popen(["parecord", "-d", sink + ".monitor", "--file-format=wav", rec],
                              env=SENV)
        r2 = subprocess.Popen(["parecord", "-d", o + ".monitor", "--file-format=wav", other],
                              env=SENV)
        time.sleep(0.4)
        subprocess.run([sx.HIBR, "-c", "mod load %s; p := media open %s; sleep 2; media close $p"
                        % (tree("build/mods/media.so"), tone)],
                       env=dict(SENV, HIBR_MODPATH=tree("build/mods")),
                       capture_output=True, timeout=30)
        time.sleep(0.3)
        r1.terminate(); r2.terminate()
        r1.wait(5); r2.wait(5)

        def level(path):
            try:
                raw = open(path, "rb").read()[44:]
            except OSError:
                return -200
            if len(raw) < 4:
                return -200
            xs = struct.unpack("<%dh" % (len(raw) // 2), raw[:len(raw) // 2 * 2])
            rms = math.sqrt(sum(x * x for x in xs) / len(xs))
            return 20 * math.log10(rms / 32768) if rms > 0 else -200
        lv[sink] = (level(rec), level(other))
    check("hibr's own sound goes to the output in use, and only to it",
          all(a > -40 and b < -60 for a, b in lv.values()), lv)
    # PipeWire with no PulseAudio side, the owner's own machine: pactl
    # reaches nothing, so the desktop speaks to PipeWire through wpctl --
    # where it used to fall to ALSA's mixer, read 0% while sound played and
    # choose the input with the output.
    if started2():
        spk, hp3, mic = wid("Speakers"), wid("Headphones"), wid("Microphone")
        wp("set-default", spk)
        wp("set-default", mic)
        wp("set-volume", spk, "0.4")
        sc = srun([], rt=RT2, until=lambda sc: "40%" in bar(sc))
        check("with PipeWire and no PulseAudio side, the speaker shows the real volume",
              "♪ 40%" in bar(sc), (bar(sc), wp("status")))
        sc = srun([b"\x1b="], rt=RT2, until=lambda sc: "45%" in bar(sc))
        check("and alt-= turns PipeWire's own volume up",
              "Volume: 0.45" in wp("get-volume", spk), wp("get-volume", spk))
        col2 = bar(sc).find("♪")
        sc = srun([press(0, col2, 2), release(0, col2, 2), 0.6], rt=RT2, until="Headphones")
        h4 = sc.find("Headphones")
        if h4:
            sc = srun([press(0, col2, 2), release(0, col2, 2), 0.6, press(h4[0], h4[1] + 2),
                       release(h4[0], h4[1] + 2)], rt=RT2,
                      until=lambda sc: wdefault("Sinks") == "Headphones")
        check("choosing Headphones makes it PipeWire's output, and the input stays",
              wdefault("Sinks") == "Headphones" and wdefault("Sources") == "Microphone",
              (wdefault("Sinks"), wdefault("Sources"), sc))
        wp("set-default", spk)
        e2 = {"HIBR_SOUND": "auto", "XDG_RUNTIME_DIR": RT2, "DT_BARTIME": "12:00"}
        s2 = tempfile.mkdtemp(prefix="hibr-snd-s-", dir=D)
        p2 = os.path.join(s2, "session.hibr")
        open(p2, "w").write("%s. %s\ndt_open\ndt_run\ndt_close\n"
                            % (load("console"), tree("programs/desktop/desktop.hibr")))
        t2 = Term(p2, env=e2, rows=30, cols=100, settle=2.0)
        t2.until(lambda sc: "%" in sc.row(0), 80)
        time.sleep(1.2)
        wp("set-volume", spk, "0.3")
        t2.until(lambda sc: "♪ 30%" in sc.row(0), 100)
        sc2 = t2.screen()
        wtree = tree_of(t2.pid)
        names = [n for _, _, n in wtree]
        check("and watches PipeWire with pw-mon alone, filtering it itself, no grep",
              "pw-mon" in names and not set(names) & {"sh", "dash", "bash", "grep", "sleep", "cut"},
              wtree)
        t2.quit(None, 0.5)
        check("and a volume PipeWire changes elsewhere reaches the bar with nothing pressed",
              "♪ 30%" in bar(sc2), bar(sc2))
    else:
        for name in ("with PipeWire and no PulseAudio side, the speaker shows the real volume",
                     "and alt-= turns PipeWire's own volume up",
                     "choosing Headphones makes it PipeWire's output, and the input stays",
                     "and a volume PipeWire changes elsewhere reaches the bar with nothing pressed",
                     "and watches PipeWire with pw-mon alone, filtering it itself, no grep"):
            check(name + " (the PipeWire-only server did not start)", False)

    # With ALSA alone the output and the input are a card each, chosen
    # apart: one card for both was what chose the input with the output.
    rc = os.path.join(D, "asoundrc-alsa")
    out = subprocess.run([sx.HIBR, "-c",
        "fn dt_baritem(...a) { :; }\nfn dt_bariwheel(...a) { :; }\nfn dt_bariright(...a) { :; }\nfn dt_ticker(n) { :; }\n"
        "fn dt_notep(...a) { :; }\nfn dt_want(n) { :; }\ndeclare -gA GL DT_KEYS\n"
        ". %s\nDT_SND[\"kind\"]=alsa\nDT_SND[\"in\"]=2\nDT_SND[\"out\"]=1\n"
        "dt_sndrcwrite 1 2\nr := dt_sndacard; echo \"first $r\"\n"
        "DT_SND[\"in\"]=2\ndt_sndrcwrite 3 \"${DT_SND[\"in\"]}\"\n"
        "r := dt_sndacard; echo \"then $r\"\n" % tree("programs/desktop/wm/sound.hibr")],
        env=dict(os.environ, DT_SNDRC=rc), capture_output=True, text=True, timeout=30)
    body = open(rc).read() if os.path.exists(rc) else ""
    check("with ALSA alone, choosing an output writes it apart from the input",
          "first 1 2" in out.stdout and "then 3 2" in out.stdout and
          'playback.pcm "plughw:3"' in body and 'capture.pcm "plughw:2"' in body,
          (out.stdout, out.stderr, body))

    # Without a server, ALSA's mixer: a stand-in amixer that answers as
    # alsa-utils' own does ("Front Left: Playback 32768 [50%] [on]"), since
    # this box has no sound card for the real one to drive.
    bindir = os.path.join(D, "bin")
    os.makedirs(bindir)
    state = os.path.join(D, "amixer.state")
    open(state, "w").write("Master 50 on\nCapture 80 on\n")
    open(os.path.join(bindir, "amixer"), "w").write(
        "#!/usr/bin/env python3\n"
        "import sys\n"
        "S = %r\n"
        "st = dict((l.split()[0], l.split()[1:]) for l in open(S) if l.strip())\n"
        "a = [x for x in sys.argv[1:] if x != '-q']\n"
        "if a[:1] == ['-c']: a = a[2:]\n"
        "verb, ctl = a[0], a[1]\n"
        "if ctl not in st: sys.exit(1)\n"
        "v, on = st[ctl]\n"
        "if verb == 'sset':\n"
        "    w = a[2]\n"
        "    if w == 'toggle': on = 'off' if on == 'on' else 'on'\n"
        "    elif w.endswith('%%'): v = str(max(0, min(100, int(w[:-1]))))\n"
        "    st[ctl] = [v, on]\n"
        "    open(S, 'w').write(''.join('%%s %%s %%s\\n' %% (k, x[0], x[1]) for k, x in st.items()))\n"
        "kind = 'Playback' if ctl == 'Master' else 'Capture'\n"
        "raw = int(v) * 65536 // 100\n"
        "if verb == 'sget' or '-q' not in sys.argv:\n"
        "    print(\"Simple mixer control '%%s',0\" %% ctl)\n"
        "    print('  Limits: %%s 0 - 65536' %% kind)\n"
        "    for ch in ('Front Left', 'Front Right'):\n"
        "        print('  %%s: %%s %%d [%%s%%%%] [%%s]' %% (ch, kind, raw, v, on))\n"
        % state)
    os.chmod(os.path.join(bindir, "amixer"), 0o755)
    aenv = {"HIBR_SOUND": "alsa", "PATH": bindir + os.pathsep + os.environ["PATH"],
            "DT_SNDRC": os.path.join(D, "asoundrc")}
    sc = srun([b"\x1b="], env=aenv, until=lambda sc: "55%" in bar(sc))
    lvl = open(state).read().split()
    check("with ALSA's mixer and no server, the speaker shows Master and alt-= turns it up",
          "♪ 55%" in bar(sc) and lvl[:3] == ["Master", "55", "on"], (bar(sc), lvl))
    sc = srun([b"\x1b0"], env=aenv, until=lambda sc: "✕" in bar(sc))
    lvl = open(state).read().split()
    check("and alt-0 mutes Master", "♪ ✕" in bar(sc) and lvl[2] == "off",
          (bar(sc), lvl))
finally:
    stop()
    shutil.rmtree(RT, True)
    shutil.rmtree(RT2, True)
    shutil.rmtree(D, True)
report(39)
