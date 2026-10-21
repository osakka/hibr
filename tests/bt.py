#!/usr/bin/env python3
"""The desktop's Bluetooth (Gitea #312): the item on the menu bar, the list
of devices beneath it, the Bluetooth menu, connecting and disconnecting,
looking for devices and pairing them -- just works, a passkey to confirm, a
PIN to type -- forgetting one, and Control Panel > Bluetooth.

This machine has no Bluetooth, so BlueZ is tests/net/bluetoothctl, a
stand-in whose show, devices and info answer byte for byte as the owner's
own bluetoothctl 5.82 did (tests/net/bt-*.txt); what was not captured --
the replies to actions, a battery line, the agent's questions -- is bluez's
own wording from its source, and the stand-in says which.

Every other suite starts its desktops with the Bluetooth layer off
(tests/screen.py): this one turns it on, for its stand-in only.
"""
import json, os, shutil, sys, tempfile, time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import screen as sx
from screen import Term, check, report, tree, load, press, release

if len(sys.argv) > 1:
    sx.HIBR = os.path.abspath(sys.argv[1])

HERE = os.path.dirname(os.path.abspath(__file__))
CAP = os.path.join(HERE, "net")
D = tempfile.mkdtemp(prefix="hibr-bt-")
BIN = os.path.join(D, "bin")
os.makedirs(BIN)
os.symlink(os.path.join(CAP, "bluetoothctl"), os.path.join(BIN, "bluetoothctl"))
CPD = tree("programs/desktop/control-panel")
SYS = tree("programs/desktop/system")
BT = "ᛒ"


def seed():
    """The owner's six paired devices as captured, the headphones connected
    with a battery, and three in reach to pair: one that just works, one
    that shows a passkey to confirm, and one that wants a PIN."""
    devs = []
    for l in open(os.path.join(CAP, "bt-devices.txt")).read().splitlines():
        _, mac, name = l.split(" ", 2)
        devs.append({"mac": mac, "name": name, "paired": True, "connected": False})
    devs[1].update(connected=True, battery=85, icon="audio-headset")
    devs += [
        {"mac": "AA:00:00:00:00:01", "name": "JBL Flip 6", "paired": False, "connected": False},
        {"mac": "AA:00:00:00:00:02", "name": "Pixel Phone", "paired": False, "connected": False,
         "pairmode": "confirm"},
        {"mac": "AA:00:00:00:00:03", "name": "Old Keyboard", "paired": False, "connected": False,
         "pairmode": "pin", "pin": "0000"},
    ]
    return {"power": "yes", "disc": "no", "scan": "no", "devices": devs,
            "inreach": [{"mac": "AA:00:00:00:00:04", "name": "Kitchen Speaker",
                         "paired": False, "connected": False}]}


class Bt:
    def __init__(self, **kw):
        self.d = tempfile.mkdtemp(prefix="bt-", dir=D)
        st = seed()
        st.update(kw)
        json.dump(st, open(os.path.join(self.d, "state.json"), "w"))

    def state(self):
        return json.load(open(os.path.join(self.d, "state.json")))

    def poke(self, f):
        s = self.state()
        f(s)
        json.dump(s, open(os.path.join(self.d, "state.json"), "w"))

    def dev(self, name):
        return next((d for d in self.state()["devices"] if d["name"] == name), None)


def srun(bt, feed, until=None, panel=False, env=None, during=None):
    s = tempfile.mkdtemp(prefix="s-", dir=D)
    p = os.path.join(s, "session.hibr")
    open(p, "w").write(
        "%s. %s\n. %s/panel.hibr\nCP_PANEDIRS+=(\"%s\")\ncp_panes\ndt_open\n%s"
        "dt_run\ndt_close\n"
        % (load("console"), tree("programs/desktop/desktop.hibr"), SYS, CPD,
           "dt_panelpane bluetooth\n" if panel else ""))
    e = {"HIBR_BT": "auto", "HIBR_BTSTATE": bt.d, "PATH": BIN + ":" + os.environ["PATH"],
         "DT_BARTIME": "12:00"}
    e.update(env or {})
    t = Term(p, env=e, rows=30, cols=100, settle=2.0)
    for _ in range(40):
        if BT in t.screen().row(0):
            break
        t.collect(0.25)
    if during:
        during(t)
    if feed:
        t.keys(list(feed), settle=0.5)
    for _ in range(60):
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


def at(sc, text, top=1):
    p = sc.find_from(text, top)
    return p


try:
    b = Bt()
    sc = srun(b, [])
    check("the Bluetooth item is on the menu bar, with how many devices are connected",
          BT + " 1" in bar(sc), bar(sc))
    col = bar(sc).find(BT)
    # Glyphs: Blit (Gitea #330) draws Bluetooth as Blit's own colour glyph,
    # not the runic letter that stands for it everywhere else.
    sc = srun(b, [], env={"DT_GLYPHSET": "blit"},
              until=lambda sc: chr(0x100006) + " 1" in bar(sc))
    check("with the Blit glyphs Bluetooth is Blit's own glyph",
          chr(0x100006) + " 1" in bar(sc) and BT not in bar(sc), bar(sc))

    sc = srun(b, [press(0, col), release(0, col)], until="8BitDo Retro Keyboard")
    hp = at(sc, "soundcore C30i")
    check("a left click lists the devices it knows, the connected one ticked with its battery",
          hp is not None and "✓" in sc.row(hp[0]) and "85%" in sc.row(hp[0]), sc)
    kb = at(sc, "8BitDo Retro Keyboard")
    jb = at(sc, "JBL Flip 6")
    check("and devices in reach under them, to pair", jb is not None and "pair" in sc.row(jb[0]), sc)

    b = Bt()
    sc = srun(b, [press(0, col), release(0, col), 0.4, press(kb[0], kb[1]), release(kb[0], kb[1])],
              until=lambda sc: (b.dev("8BitDo Retro Keyboard") or {}).get("connected"))
    check("a click on a known device connects it, and says so",
          b.dev("8BitDo Retro Keyboard")["connected"] and BT + " 2" in bar(sc), (bar(sc), sc))
    sc = srun(b, [press(0, col), release(0, col), 0.4, press(hp[0], hp[1]), release(hp[0], hp[1])],
              until=lambda sc: not b.dev("soundcore C30i")["connected"])
    check("and a click on a connected one disconnects it", not b.dev("soundcore C30i")["connected"], sc)

    b = Bt()
    sc = srun(b, [press(0, col), release(0, col), 0.4, press(jb[0], jb[1]), release(jb[0], jb[1])],
              until=lambda sc: (b.dev("JBL Flip 6") or {}).get("connected"))
    check("a click on a device in reach pairs it, trusts it and connects it",
          b.dev("JBL Flip 6")["paired"] and b.dev("JBL Flip 6").get("trusted") and
          b.dev("JBL Flip 6")["connected"], (b.dev("JBL Flip 6"), sc))

    b = Bt()
    px = at(sc, "Pixel Phone") or at(srun(b, [press(0, col), release(0, col)], until="Pixel Phone"),
                                     "Pixel Phone")
    sc = srun(b, [press(0, col), release(0, col), 0.4, press(px[0], px[1]), release(px[0], px[1]), 1.5],
              until="Does it show this number?")
    check("a device that shows a passkey asks whether it is the same, showing it",
          sc.find("Does it show this number?") is not None and sc.find("123456") is not None, sc)
    sc = srun(b, [press(0, col), release(0, col), 0.4, press(px[0], px[1]), release(px[0], px[1]), 1.5,
                  b"\r"],
              until=lambda sc: (b.dev("Pixel Phone") or {}).get("paired"))
    check("and yes pairs it", b.dev("Pixel Phone")["paired"], (b.dev("Pixel Phone"), sc))

    b = Bt()
    ok = at(srun(b, [press(0, col), release(0, col)], until="Old Keyboard"), "Old Keyboard")
    sc = srun(b, [press(0, col), release(0, col), 0.4, press(ok[0], ok[1]), release(ok[0], ok[1]), 1.5,
                  b"0000", b"\r"],
              until=lambda sc: (b.dev("Old Keyboard") or {}).get("paired"))
    check("a device that wants a PIN asks for it, and the right one pairs it",
          b.dev("Old Keyboard")["paired"], (b.dev("Old Keyboard"), sc))
    b = Bt()
    sc = srun(b, [press(0, col), release(0, col), 0.4, press(ok[0], ok[1]), release(ok[0], ok[1]), 1.5,
                  b"9999", b"\r"],
              until="Could not pair")
    check("and a wrong one says it could not pair, leaving it unpaired",
          sc.find("Could not pair Old Keyboard") is not None and
          not b.dev("Old Keyboard")["paired"], sc)

    # The menu, by a right click; Bluetooth off there.
    b = Bt()
    sc = srun(b, [press(0, col, 2), release(0, col, 2)], until="Bluetooth Settings")
    check("a right click opens the Bluetooth menu: how many are connected, the switch, the devices",
          sc.find("1 connected") is not None and sc.find("Look for Devices") is not None and
          sc.find("8BitDo Retro Keyboard") is not None, sc)
    m = at(sc, "Bluetooth", 2)
    if m:
        sc = srun(b, [press(0, col, 2), release(0, col, 2), 0.4, press(m[0], m[1]), release(m[0], m[1])],
                  until=lambda sc: "✕" in bar(sc))
    check("Bluetooth off there turns the adapter off, and the bar says so",
          b.state()["power"] == "no" and BT + " ✕" in bar(sc), (b.state()["power"], bar(sc)))

    b = Bt(refuse=["set power off"])
    sc = srun(b, [press(0, col, 2), release(0, col, 2), 0.4, press(m[0], m[1]), release(m[0], m[1])],
              until="not allowed")
    check("a session BlueZ refuses says so in words, and nothing changes",
          sc.find("not allowed from this session") is not None and b.state()["power"] == "yes", sc)

    # A change made elsewhere reaches the bar with nothing pressed.
    b = Bt()
    sc = srun(b, [], during=lambda t: (time.sleep(1.5),
                                       b.poke(lambda s: s["devices"][0].update(connected=True))),
              until=lambda sc: BT + " 2" in bar(sc))
    check("a device connecting elsewhere reaches the bar", BT + " 2" in bar(sc), bar(sc))

    # Control Panel > Bluetooth.
    b = Bt()
    sc = srun(b, [], panel=True, until="My Devices")
    check("Control Panel > Bluetooth names the adapter and lists the devices, connected or not",
          sc.find("elgibsson") is not None and sc.find("C8:26:E2:59:1D:E4") is not None and
          sc.find("connected, 85%") is not None and sc.find("not connected") is not None, sc)
    lk = at(sc, "Look for Devices")
    sc = srun(b, [press(lk[0], lk[1]), release(lk[0], lk[1]), 0.3, press(lk[0], lk[1]),
                  release(lk[0], lk[1])], panel=True,
              until=lambda sc: b.dev("Kitchen Speaker") is not None and sc.find("Kitchen Speaker"))
    check("Look for Devices there finds what is in reach", sc.find("Kitchen Speaker") is not None, sc)
    fg = at(sc, "Forget a Device")
    fc = sc.row(fg[0]).find("\u25be")
    sc = srun(b, [press(fg[0], fc), release(fg[0], fc), 0.5], panel=True,
              until=lambda sc: sc.find_from("ProtoArc EM01", fg[0] + 1) is not None)
    pe = at(sc, "ProtoArc EM01", fg[0] + 1)
    if pe:
        sc = srun(b, [press(fg[0], fc), release(fg[0], fc), 0.5,
                      press(pe[0], pe[1]), release(pe[0], pe[1]), 0.5, b"y"], panel=True,
                  until=lambda sc: b.dev("ProtoArc EM01") is None)
    check("Forget a Device asks first, then removes it, pairing and all",
          b.dev("ProtoArc EM01") is None, sc)
    # A desktop killed with -9 -- what the owner does to a spinning one --
    # takes its watcher with it: a guard in the watcher's own process group
    # ends the group once the desktop's pid has gone. Until 0.99.271 the
    # watcher lived on, and the sleep feeding it for ever.
    b = Bt()
    s9 = tempfile.mkdtemp(prefix="s-", dir=D)
    p9 = os.path.join(s9, "session.hibr")
    open(p9, "w").write("%s. %s\ndt_open\ndt_run\ndt_close\n"
                        % (load("console"), tree("programs/desktop/desktop.hibr")))
    t9 = Term(p9, env={"HIBR_BT": "auto", "HIBR_BTSTATE": b.d,
                       "PATH": BIN + ":" + os.environ["PATH"]}, rows=30, cols=100, settle=2.0)
    t9.until(lambda sc: BT in sc.row(0), 80)

    def watchers():
        """This suite's own stand-ins and the sleep that feeds one, by path."""
        return [l for l in os.popen("ps -eo pid=,args=").read().splitlines()
                if BIN in l and "bluetoothctl" in l]
    before = len(watchers())
    t9.signal(9)
    end = time.time() + 12
    while time.time() < end and watchers():
        time.sleep(0.5)
    check("a desktop killed with -9 leaves no watcher behind",
          before > 0 and watchers() == [], (before, watchers()))
    try:
        t9.quit(None, 0.2)
    except Exception:
        pass
    shutil.rmtree(s9, True)
    sc = srun(Bt(), [], panel=True, env={"DT_BTBAR": "0"}, until="My Devices")
    check("Bluetooth on Menu Bar off takes the item off the bar", BT not in bar(sc), bar(sc))
    sc = srun(Bt(adapter=False), [], panel=True, until="Adapter")
    check("with no adapter there is no item, and the pane says none was found",
          BT not in bar(sc) and sc.find("none found") is not None, sc)
finally:
    shutil.rmtree(D, True)

report(22)
