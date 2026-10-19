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
from screen import Term, check, report, tree, load, press, release

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


def srun(feed, until=None, panel=False, env=None, rows=30, cols=100, settle=2.0):
    """A desktop with this suite's sound server, and with the Control Panel
    open at Sound when asked; then keys, and the screen once `until` (text
    or a function of the screen) holds."""
    s = tempfile.mkdtemp(prefix="hibr-snd-s-", dir=D)
    p = os.path.join(s, "session.hibr")
    open(p, "w").write(
        "%s. %s\n. %s/panel.hibr\nCP_PANEDIRS+=(\"%s\")\ncp_panes\ndt_open\n%s"
        "dt_run\ndt_close\n"
        % (load("console"), tree("programs/desktop/desktop.hibr"), SYS, CPD,
           "dt_panelpane sound\n" if panel else ""))
    e = {"HIBR_SOUND": "auto", "PULSE_SERVER": None, "XDG_RUNTIME_DIR": RT,
         "DT_BARTIME": "12:00"}
    e.update(env or {})
    t = Term(p, env=e, rows=rows, cols=cols, settle=settle)
    for _ in range(40):
        if "%" in t.screen().row(0) or "✕" in t.screen().row(0):
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
    t1.quit(None, 0.5)
    check("a volume changed by another program reaches the bar with nothing pressed",
          "♪ 30%" in bar(sc1), bar(sc1))

    # The pactl the desktop watches goes when the desktop does.
    time.sleep(0.5)
    left = []
    for d in os.listdir("/proc"):
        if not d.isdigit():
            continue
        try:
            cmd = open("/proc/%s/cmdline" % d, "rb").read().split(b"\0")
            env = open("/proc/%s/environ" % d, "rb").read()
        except OSError:
            continue
        if cmd[:2] == [b"pactl", b"subscribe"] and ("XDG_RUNTIME_DIR=" + RT).encode() in env:
            left.append(d)
    check("the pactl it watches is ended when the desktop ends", not left, left)

    # The wheel over the speaker.
    reset()
    sc = srun([], until=lambda sc: "50%" in bar(sc))
    col = bar(sc).find("♪")
    sc = srun([b"\x1b[<64;%d;1M" % (col + 1)], until=lambda sc: "55%" in bar(sc))
    check("the wheel over the speaker turns the volume", volume("spk") == 55,
          (volume("spk"), bar(sc)))

    # The sound menu: a click on the speaker opens it, with the outputs and
    # the one in use ticked; choosing another makes it the server's default.
    reset()
    sc = srun([press(0, col), release(0, col)], until="Headphones")
    check("a click on the speaker opens the sound menu: the level, the outputs, the inputs",
          sc.find("Volume 50%") is not None and sc.find("Speakers") is not None and
          sc.find("Headphones") is not None and sc.find("Microphone") is not None, sc)
    hp = sc.find("Headphones")
    tick = sc.find("✓ Speakers") or sc.find("✓Speakers")
    check("and the output in use is ticked", tick is not None, sc)
    if hp:
        sc = srun([press(0, col), release(0, col), 0.6, press(hp[0], hp[1] + 2),
                   release(hp[0], hp[1] + 2)],
                  until=lambda sc: pa("get-default-sink").strip() == "hp")
    check("choosing Headphones there makes it the output the server uses",
          pa("get-default-sink").strip() == "hp", pa("get-default-sink"))

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
    shutil.rmtree(D, True)
report(17)
