#!/usr/bin/env python3
"""The desktop's network (Gitea #311): the item on the menu bar, the list of
networks beneath it, the network menu, joining a network -- saved, open, or
secured with a password asked for in a dialog -- and Control Panel >
Network.

This machine has no Wi-Fi and no NetworkManager, so NetworkManager is
tests/net/nmcli, a stand-in whose reads answer byte for byte as the owner's
own nmcli 1.52 did (tests/net/*.txt, captured on their machine) and whose
actions are judged by their exit status, as the desktop judges them. It
logs every call's argv, which is how the password is shown never to reach
a command line. The wired half needs no stand-in: iproute2 is real here,
and the last checks read this machine's own address through it.

Every other suite starts its desktops with the network layer off
(tests/screen.py): this one turns it on, for its stand-in only.
"""
import json, os, re, shutil, subprocess, sys, tempfile, time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import screen as sx
from screen import Term, check, report, tree, load, press, release, wheel

if len(sys.argv) > 1:
    sx.HIBR = os.path.abspath(sys.argv[1])

HERE = os.path.dirname(os.path.abspath(__file__))
CAP = os.path.join(HERE, "net")
D = tempfile.mkdtemp(prefix="hibr-net-")
BIN = os.path.join(D, "bin")
os.makedirs(BIN)
os.symlink(os.path.join(CAP, "nmcli"), os.path.join(BIN, "nmcli"))
os.symlink(os.path.join(CAP, "pkttyagent"), os.path.join(BIN, "pkttyagent"))
CPD = tree("programs/desktop/control-panel")
SYS = tree("programs/desktop/system")
BARS = "▂▄▆█"


def terse(line):
    """A terse line's fields, \\: and \\\\ undone -- how the captures read."""
    out, cur, i = [], "", 0
    while i < len(line):
        if line[i] == "\\" and i + 1 < len(line):
            cur += line[i + 1]
            i += 2
            continue
        if line[i] == ":":
            out.append(cur)
            cur = ""
        else:
            cur += line[i]
        i += 1
    out.append(cur)
    return out


def seed():
    """A state for the stand-in: the owner's devices, networks and saved
    connections as captured, and three more networks in reach to join."""
    def rows(f, keys):
        return [dict(zip(keys, terse(l))) for l in open(os.path.join(CAP, f)).read().splitlines()]
    devs = rows("dev-status.txt", ["DEVICE", "TYPE", "STATE", "CONNECTION"])
    aps = rows("wifi-list.txt", ["IN-USE", "SSID", "BSSID", "MODE", "CHAN", "SIGNAL", "BARS", "SECURITY"])
    cons = rows("con-show.txt", ["NAME", "UUID", "TYPE", "DEVICE"])
    ts = {r["UUID"]: r["TIMESTAMP"] for r in rows("con-show-ts.txt", ["NAME", "UUID", "TYPE", "TIMESTAMP", "AUTOCONNECT"])}
    for c in cons:
        c["TIMESTAMP"] = ts.get(c["UUID"], "0")
    for c in cons:
        if c["TYPE"] == "802-11-wireless":
            n = c["NAME"]
            c["ssid"] = re.sub(r" \d+$", "", n)
            c["psk"] = "saved"
    aps[0]["password"] = "saved"
    aps += [
        {"IN-USE": "", "SSID": "Cafe Free", "BSSID": "AA:BB:CC:00:00:01", "MODE": "Infra",
         "CHAN": "6", "SIGNAL": "60", "BARS": "", "SECURITY": ""},
        {"IN-USE": "", "SSID": "Neighbour 5G", "BSSID": "AA:BB:CC:00:00:02", "MODE": "Infra",
         "CHAN": "44", "SIGNAL": "40", "BARS": "", "SECURITY": "WPA2", "password": "letmein123"},
        {"IN-USE": "", "SSID": "gibnet", "BSSID": "AA:BB:CC:00:00:03", "MODE": "Infra",
         "CHAN": "11", "SIGNAL": "30", "BARS": "", "SECURITY": "WPA2", "password": "saved"},
    ]
    show = {"wlp3s0": {"GENERAL.STATE": "100 (connected)", "IP4.ADDRESS[1]": "192.168.10.54/24",
                       "IP4.GATEWAY": "192.168.10.254", "IP4.DNS[1]": "192.168.15.100",
                       "IP4.DNS[2]": "192.168.15.200"},
            "lo": {"GENERAL.STATE": "100 (connected (externally))", "IP4.ADDRESS[1]": "127.0.0.1/8",
                   "IP4.GATEWAY": ""},
            "p2p-dev-wlp3s0": {"GENERAL.STATE": "30 (disconnected)"},
            "eno1": {"GENERAL.STATE": "20 (unavailable)", "IP4.GATEWAY": ""}}
    return {"running": True, "wifi": "enabled", "devices": devs, "aps": aps, "cons": cons, "show": show}


OWNER = {l.split()[0].replace("org.freedesktop.NetworkManager.", ""): l.split()[1]
         for l in open(os.path.join(CAP, "permissions.txt")).read().splitlines()[1:]}


class Net:
    """One stand-in NetworkManager: its state folder, read back. perms is
    what polkit answers -- the owner's own session's answers, OWNER, are
    every one auth or no -- and pw the password its agent accepts."""
    def __init__(self, perms=None, pw=None):
        self.d = tempfile.mkdtemp(prefix="nm-", dir=D)
        st = seed()
        if perms:
            st["perms"] = perms
        if pw:
            st["agentpw"] = pw
        json.dump(st, open(os.path.join(self.d, "state.json"), "w"))

    def state(self):
        return json.load(open(os.path.join(self.d, "state.json")))

    def poke(self, f):
        s = self.state()
        f(s)
        json.dump(s, open(os.path.join(self.d, "state.json"), "w"))

    def argv(self):
        p = os.path.join(self.d, "argv.log")
        return open(p).read() if os.path.exists(p) else ""

    def inuse(self):
        return [a["SSID"] for a in self.state()["aps"] if a["IN-USE"] == "*"]

    def cons(self):
        return [c["NAME"] for c in self.state()["cons"]]


def srun(net, feed, until=None, panel=False, env=None, rows=30, cols=100, during=None):
    """A desktop with this stand-in, the Control Panel open at Network when
    asked; keys, then the screen once `until` (text or a function of the
    screen) holds."""
    s = tempfile.mkdtemp(prefix="s-", dir=D)
    p = os.path.join(s, "session.hibr")
    open(p, "w").write(
        "%s. %s\n. %s/panel.hibr\nCP_PANEDIRS+=(\"%s\")\ncp_panes\ndt_open\n%s"
        "dt_run\ndt_close\n"
        % (load("console"), tree("programs/desktop/desktop.hibr"), SYS, CPD,
           "dt_panelpane networks\n" if panel else ""))
    e = {"HIBR_NET": "auto", "HIBR_NMSTATE": net.d if net else "",
         "PATH": BIN + ":" + os.environ["PATH"], "DT_BARTIME": "12:00"}
    e.update(env or {})
    t = Term(p, env=e, rows=rows, cols=cols, settle=2.0)
    for _ in range(40):
        if net is None or BARS[0] in t.screen().row(0) or "✕" in t.screen().row(0):
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


def item(sc):
    """The column the network item starts at on the bar."""
    for g in (BARS[0], "⇌", "✕"):
        c = bar(sc).find(g)
        if c >= 0:
            return c
    return -1


def row(sc, text, top=1):
    p = sc.find_from(text, top)
    return p[0] if p else None


try:
    n = Net()
    sc = srun(n, [])
    check("the network item is on the menu bar, with the Wi-Fi in use's four bars",
          BARS in bar(sc), bar(sc))
    col = item(sc)

    # A left click lists the networks in reach: the one in use first and
    # ticked, the rest by signal, a key on each that asks a password.
    sc = srun(n, [press(0, col), release(0, col)], until="Cafe Free")
    g, cf, nb, gb = (row(sc, t) for t in (".gibnet/5ghz", "Cafe Free", "Neighbour 5G", "gibnet "))
    lc = sc.find_from("Cafe Free", 1)[1] if sc.find_from("Cafe Free", 1) else 20
    check("a left click lists the networks in reach beneath it, the one in use first and ticked",
          g is not None and cf is not None and nb is not None and g < cf < nb and
          "✓" in sc.row(g), sc)
    check("and a network that asks a password wears a key, an open one none",
          "⚿" in sc.row(nb) and "⚿" not in sc.row(cf), sc)

    # An open network: joined at once, a note when it is.
    n = Net()
    sc = srun(n, [press(0, col), release(0, col), 0.4, press(cf, lc), release(cf, lc)],
              until=lambda sc: n.inuse() == ["Cafe Free"] and sc.find("Joined Cafe Free"))
    check("a click on an open network joins it", n.inuse() == ["Cafe Free"], (n.inuse(), sc))
    check("and a note says so", sc.find("Joined Cafe Free") is not None, sc)

    # A saved network: its own connection is brought up, never a new one.
    n = Net()
    before = n.cons()
    sc = srun(n, [press(0, col), release(0, col), 0.4, press(gb, lc), release(gb, lc)],
              until=lambda sc: n.inuse() == ["gibnet"])
    check("a saved network is joined through the connection it already has, making none",
          n.inuse() == ["gibnet"] and n.cons() == before and
          "bce19d86-3053-4d0b-972f-a9a536294fae" in n.argv(), (n.inuse(), n.cons()[-2:]))

    # A secured network not yet saved: a dialog asks the password, starred.
    n = Net()
    sc = srun(n, [press(0, col), release(0, col), 0.4, press(nb, lc), release(nb, lc), 0.6,
                  b"letmein", 0.3], until="Password:")
    pr = row(sc, "Password:")
    check("a secured network not yet saved asks for its password, starred as it is typed",
          sc.find("Join Neighbour 5G") is not None and pr is not None and
          "*******" in sc.row(pr) and "letmein" not in sc.text(), sc)
    sc = srun(n, [press(0, col), release(0, col), 0.4, press(nb, lc), release(nb, lc), 0.6,
                  b"letmein123", b"\r"],
              until=lambda sc: n.inuse() == ["Neighbour 5G"])
    check("the right password joins it, and the network is kept for next time",
          n.inuse() == ["Neighbour 5G"] and "Neighbour 5G" in n.cons() and
          any(c.get("psk") == "letmein123" for c in n.state()["cons"]), (n.inuse(), n.cons()))
    check("and the password never reached a command line",
          "letmein123" not in n.argv() and n.argv() != "", n.argv()[-300:])

    n = Net()
    before = n.cons()
    sc = srun(n, [press(0, col), release(0, col), 0.4, press(nb, lc), release(nb, lc), 0.6,
                  b"wrongpass1", b"\r"],
              until="Could not join")
    check("a wrong password says it could not join",
          sc.find("Could not join Neighbour 5G") is not None, sc)
    check("and leaves no connection behind, nor the network it was on",
          n.cons() == before and n.inuse() == [".gibnet/5ghz"], (n.cons()[-2:], n.inuse()))

    # The menu, by a right click.
    n = Net()
    sc = srun(n, [press(0, col, 2), release(0, col, 2)], until="Network Settings")
    check("a right click opens the network menu: what it is on, Wi-Fi, Disconnect, the settings",
          sc.find("Connected to .gibnet/5ghz") is not None and sc.find("Wi-Fi") is not None and
          sc.find("Disconnect") is not None, sc)
    w = sc.find("Wi-Fi")
    if w:
        sc = srun(n, [press(0, col, 2), release(0, col, 2), 0.4, press(w[0], w[1]),
                      release(w[0], w[1])],
                  until=lambda sc: "✕" in bar(sc))
    check("Wi-Fi off there turns the radio off, and the bar says there is no network",
          n.state()["wifi"] == "disabled" and "✕" in bar(sc), (n.state()["wifi"], bar(sc)))

    # A change made elsewhere reaches the bar with nothing pressed.
    n = Net()
    sc = srun(n, [], during=lambda t: (time.sleep(1.5),
                                       n.poke(lambda s: s.update(wifi="disabled") or
                                              s["devices"][0].update(STATE="unavailable"))),
              until=lambda sc: "✕" in bar(sc))
    check("a change NetworkManager makes elsewhere reaches the bar", "✕" in bar(sc), bar(sc))

    # Control Panel > Network.
    n = Net()
    sc = srun(n, [], panel=True, until="Known Networks")
    check("Control Panel > Network says what manages it, what it is on, and its address",
          sc.find("NetworkManager") is not None and sc.find("Wi-Fi, .gibnet/5ghz") is not None and
          sc.find("192.168.10.54/24") is not None and
          sc.find("192.168.15.100, 192.1") is not None, sc)
    k = row(sc, ".gibnet/5ghz 2")
    check("and lists the networks it knows, each by its name and the network it joins",
          k is not None and ".gibnet/5ghz  Forget" in sc.row(k), sc)
    if k is not None:
        n2 = Net()
        f = sc.find_from("Forget", k)
        sc2 = srun(n2, [press(f[0], f[1]), release(f[0], f[1]), 0.3,
                        press(f[0], f[1]), release(f[0], f[1]), 0.5, b"y"], panel=True,
                   until=lambda sc: ".gibnet/5ghz 2" not in n2.cons())
        check("Forget there asks first, then takes the connection and its password away",
              ".gibnet/5ghz 2" not in n2.cons() and len(n2.cons()) == len(n.cons()) - 1,
              sc2)
    sc = srun(n, [], panel=True, env={"DT_NETBAR": "0"}, until="Known Networks")
    check("Network on Menu Bar off takes the item off the bar", BARS not in bar(sc), bar(sc))

    # A session polkit answers as it answered the owner's: every action
    # wants a password, and Wi-Fi's switch is refused. The desktop runs
    # polkit's agent and asks in a window of its own.
    n = Net(OWNER, "s3cret pw")
    sc = srun(n, [press(0, col), release(0, col), 0.4, press(gb, lc), release(gb, lc), 1.5],
              until="The system asks for a password")
    check("joining from a session polkit wants a password for asks for it in a window",
          sc.find("The system asks for a password") is not None and
          sc.find("network-control") is not None and
          sc.find("Authenticating as") is not None, sc)
    sc = srun(n, [press(0, col), release(0, col), 0.4, press(gb, lc), release(gb, lc), 1.5,
                  b"s3cret pw", b"\r"],
              until=lambda sc: n.inuse() == ["gibnet"])
    check("and the right password lets the join go on",
          n.inuse() == ["gibnet"], (n.inuse(), sc))
    check("the password polkit asked for reached no command line either",
          "s3cret" not in n.argv(), n.argv()[-300:])
    n = Net(OWNER, "s3cret pw")
    sc = srun(n, [press(0, col), release(0, col), 0.4, press(gb, lc), release(gb, lc), 1.5,
                  b"wrong one", b"\r"],
              until="Could not join gibnet")
    check("a wrong one says so, and changes nothing",
          sc.find("Could not join gibnet: the password was not") is not None and
          n.inuse() == [".gibnet/5ghz"], sc)
    sc = srun(Net(OWNER), [], panel=True, until="Known Networks")
    check("where polkit refuses Wi-Fi's switch the pane says so rather than offering one",
          sc.find("on -- locked here") is not None, sc)
    n = Net(dict(OWNER, **{"settings.modify.system": "no", "settings.modify.own": "yes",
                           "network-control": "yes"}))
    sc = srun(n, [press(0, col), release(0, col), 0.4, press(nb, lc), release(nb, lc), 0.6,
                  b"letmein123", b"\r"],
              until=lambda sc: n.inuse() == ["Neighbour 5G"])
    own = [c for c in n.state()["cons"] if c["NAME"] == "Neighbour 5G"]
    check("where a connection for everyone is refused, a new network is saved as this person's own",
          n.inuse() == ["Neighbour 5G"] and own and own[0].get("owner", "").startswith("user:"),
          (n.inuse(), own))

    # iproute2 alone, against this machine itself: the default route's
    # device, wired, and its own address.
    r = subprocess.run(["ip", "-j", "route", "show", "default"], capture_output=True, text=True).stdout
    try:
        dev = json.loads(r)[0]["dev"]
        a = json.loads(subprocess.run(["ip", "-j", "-4", "addr", "show", "dev", dev],
                                      capture_output=True, text=True).stdout)[0]["addr_info"][0]
        addr = "%s/%s" % (a["local"], a["prefixlen"])
        wifi = os.path.isdir("/sys/class/net/%s/wireless" % dev)
    except (ValueError, IndexError, KeyError):
        dev = addr = None
        wifi = False
    if dev and not wifi:
        sc = srun(None, [], panel=True, env={"HIBR_NET": "ip"}, until=addr)
        check("with iproute2 alone the bar shows a cable, and the pane this machine's own address",
              "⇌" in bar(sc) and sc.find(addr) is not None and sc.find(dev) is not None,
              (dev, addr, sc))
    else:
        check("with iproute2 alone the bar shows a cable (skipped: no wired default route here)", True)
finally:
    shutil.rmtree(D, True)

report(26)
