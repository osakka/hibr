#!/usr/bin/env python3
"""Screens' Wayland Screen (Gitea #493, from the Steam window of #486):
Steam's Big Picture under Blit's gamescope bridge, in a desktop window, as
the program Screens puts on its own list wherever Steam is installed.

A stand-in bridge (bridgeserve.py) is started as the window would start the
real one, and a stand-in Blit (frameserve.py) is the display's: frames go
from the one to the other through the frames module and the console, the
bridge's EIS socket becomes an input route at Blit, and the route follows
the window's focus. Every check reads what crossed a socket or the terminal,
never the app's own state.
"""
import http.server, json, os, re, shutil, subprocess, sys, tempfile, threading, time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import screen as sx
from screen import Term, check, report, load, tree, press, release

D = tempfile.mkdtemp(prefix="hibr-screens-wl-")
HERE = os.path.dirname(os.path.abspath(__file__))
SLOW = 4 if "ASAN_OPTIONS" in os.environ else 1


def lines(lg):
    try:
        return [json.loads(l) for l in open(lg) if l.strip()]
    except OSError:
        return []


def wait(cond, t=4.0):
    end = time.time() + t * SLOW
    while time.time() < end:
        v = cond()
        if v:
            return v
        time.sleep(0.05)
    return cond()


nm = "hibr-stw-%d" % os.getpid()
blitlog = os.path.join(D, "blit.log")
open(blitlog, "w").close()
blit = subprocess.Popen([sys.executable, os.path.join(HERE, "frameserve.py"),
                         nm, "tok", blitlog, "v3"])
wait(lambda: "listening" in open(blitlog).read())
brlog = os.path.join(D, "bridge.log")
wrap = os.path.join(D, "blit-gamescope")
open(wrap, "w").write("#!/bin/sh\nexec %s %s %s \"$@\"\n"
                      % (sys.executable, os.path.join(HERE, "bridgeserve.py"), brlog))
os.chmod(wrap, 0o755)

# A stand-in for Blit's own API, which the window asks for its display's
# refresh rate with what Blit gives the desktop's session: two displays, the
# window's pane on the slower, and a token it refuses to answer without.
API = {"/v1/topology": {"panes": [{"id": "pane-display-1-1", "display_id": "display-1"},
                                  {"id": "pane-display-2-1", "display_id": "display-2"}]},
       "/v1/displays": {"revision": 1, "displays": [
           {"id": "display-1", "active_mode": {"refresh_hz": 143.9}},
           {"id": "display-2", "active_mode": {"refresh_hz": 74.6}}]}}
ASKED = []


class Api(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        ok = self.headers.get("Authorization") == "Bearer api-sekrit"
        ASKED.append((self.path, ok))
        b = json.dumps(API.get(self.path, {})).encode()
        self.send_response(200 if ok else 401)
        self.send_header("Content-Length", str(len(b) if ok else 0))
        self.end_headers()
        if ok:
            self.wfile.write(b)

    def log_message(self, *a):
        pass


api = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Api)
threading.Thread(target=api.serve_forever, daemon=True).start()

# Steam is installed here as far as Screens can tell: a steam on the path,
# which nothing runs -- the stand-in bridge never starts its command.
BIN = os.path.join(D, "bin")
os.mkdir(BIN)
for b in ("steam", "blit-gamescope"):
    open(os.path.join(BIN, b), "w").write("#!/bin/sh\nexit 0\n")
    os.chmod(os.path.join(BIN, b), 0o755)
PATH = BIN + ":" + os.environ.get("PATH", "")
LIST = os.path.join(D, "xscreen.tsv")
# A program of the list's own before Steam's row: Wayland Screen, the
# pointer held on a click, and a command a shell has to read.
open(LIST, "w").write('Game\tgame "a b"\tfit\tfollow\tdefault\tengine=wayland;rel=1\t-\n')
MARK = os.path.join(D, "open-game")

p = sx.scratch("screens-wl.hibr")
open(p, "w").write(
    "%s. %s\n. %s\nSCN_BRIDGE=%s\n"
    "tst_game() { [ -f %s ] || return 0; rm -f %s; dt_launch \"screens:0\"; dt_want 30; }\n"
    "dt_ticker tst_game\n"
    "dt_open\ndt_launch \"screens:1\"\ndt_run\ndt_close\n"
    % (load("console"), tree("programs/desktop/desktop.hibr"),
       tree("programs/desktop/apps/screens.hibr"), wrap, MARK, MARK))
t = Term(p, rows=26, cols=82, settle=1.0, cellw=8, cellh=16, caps=["kitty", "lock"],
         env={"HIBR_GFX": "kitty", "BLIT_FRAME_SOCKET": "@" + nm,
              "PATH": PATH, "SCN_LIST": LIST,
              "BLIT_FRAME_TOKEN": "tok", "XDG_RUNTIME_DIR": D,
              "BLIT_SERVER_PORT": str(api.server_address[1]),
              "BLIT_API_TOKEN": "api-sekrit", "BLIT_PANE_ID": "pane-display-2-1"})


def blitkind(k, **kw):
    return [x for x in lines(blitlog) if x["kind"] == k and
            all(x.get(a) == b for a, b in kw.items())]


st = wait(lambda: [x for x in lines(brlog) if x["kind"] == "start"])
argv = st[0]["argv"] if st else []
check("Steam, on the list by itself, starts the bridge with no placement of "
      "its own, Steam's integration and Big Picture, at the window's picture "
      "size in pixels",
      argv[:2] == ["--no-place", "--steam"] and "-W" in argv and "-H" in argv and
      int(argv[argv.index("-W") + 1]) % 8 == 0 and
      int(argv[argv.index("-H") + 1]) % 16 == 0 and
      argv[-3:] == ["--", "steam", "-gamepadui"], argv)
check("at the refresh rate of its own pane's display, rounded, not the "
      "fastest display's", "-r" in argv and argv[argv.index("-r") + 1] == "75" and
      argv.index("-r") < argv.index("--"), argv)
check("which it asked Blit for with the session's token, and the token goes "
      "nowhere near the bridge", ("/v1/topology", True) in ASKED and
      ("/v1/displays", True) in ASKED and not any("api-sekrit" in a for a in argv),
      (ASKED, argv))
lg = argv[argv.index("--log") + 1] if "--log" in argv else ""
check("and has it write its own log to a file in the state folder, never to "
      "anything the desktop watches", lg.endswith("/hibr/screens.log") and
      argv.index("--log") < argv.index("--"), argv)
check("and gives it the frames module's socket, not Blit's own",
      st and st[0]["sock"].startswith("@hibr-frames-") and st[0]["tok"], st)
f3 = wait(lambda: blitkind("frame3"))
cid = f3[0]["id"] if f3 else None
check("the bridge's frames reach the display's Blit under the console's id",
      cid and cid != 7, lines(blitlog)[-3:])
ok = wait(lambda: blitkind("input", verb="eis", id=cid, ok=True) and
          blitkind("input", verb="on", id=cid, ok=True))
check("its EIS socket becomes an input route there, on while the window "
      "has the keys", ok, [x for x in lines(blitlog) if x["kind"] == "input"])
ans = wait(lambda: [x["line"] for x in lines(brlog)
                    if x["kind"] == "answer" and "input" in x["line"].lower()])
check("and the bridge hears Blit's answers in its own id", ans and "OK input 7" in ans, ans)


def released():
    return sorted(int(x["line"].split()[2]) for x in lines(brlog)
                  if x["kind"] == "answer" and x["line"].startswith("RELEASE 7 "))


rel = wait(lambda: len(released()) > 20 and released()) or []
check("every frame the bridge sent comes back released, the ones before the "
      "window placed its picture too, so its buffers never run out",
      rel and rel[:len(rel)] == list(range(1, len(rel) + 1)), rel[:30])
t.collect(0.5)
n0 = len(blitkind("frame3"))
fr0 = t.frames()
t.collect(3.0)
n1 = len(blitkind("frame3"))
fr1 = t.frames()
check("frames go on flowing while the window is drawn, the bridge's own "
      "log drained so it never blocks writing it", n1 - n0 > 20, (n0, n1))
check("and the desktop draws nothing for them: frames pass in the console",
      fr1 - fr0 <= 2, (fr0, fr1))
sc = t.screen()
check("the window draws the picture rather than a waiting line",
      sc.find("Starting Steam") is None and sc.find("Waiting for") is None, sc)

# Every key to the program, as Steam's row has it: the menu key is the
# game's, and nothing opens.
ons = len(blitkind("input", verb="on"))
t.keys([b"\x1b[21~"])
t.collect(0.5)
sc = t.screen()
check("with every key to the program a desktop shortcut is the game's: the menu "
      "key opens nothing and the route stays on",
      sc.find("Restart Desktop") is None and not blitkind("input", verb="off"), sc)

# A menu opened with the mouse takes the keys, and the route goes off.
m = sc.find("Program")
t.keys([press(*m), release(*m)] if m and m[0] == 0 else [])
t.collect(0.5)
offs = wait(lambda: blitkind("input", verb="off"))
check("a menu opened over the game turns its route off", offs,
      [x for x in lines(blitlog) if x["kind"] == "input"])
t.keys([b"\x1b"])
back = wait(lambda: len(blitkind("input", verb="on")) > ons)
check("and closing it turns the route back on", back,
      [x for x in lines(blitlog) if x["kind"] == "input"])

# A click holds the pointer for the game, as Boxer does, and says how to
# get it back.
t.keys([press(10, 40), release(10, 40)])
t.collect(0.4)
check("a click in Steam's window holds the pointer for the game, and says "
      "which key lets go", b"\x1b[?7701h" in t.out and
      t.screen().find("has the mouse") is not None, (t.out[-200:], t.screen()))
t.keys([b"\x1b[21;5~"])
t.collect(0.3)

# Full Screen is still the desktop's, and so is leaving it.
c_fs = len(t.out)
t.keys([b"\x1b\r"])
t.collect(0.6)
fs = t.screen()
fsp = [m.group(1) for m in re.finditer(r"\x1b_G([^;\x1b]*)",
                                       t.out[c_fs:].decode("latin-1")) if "a=p" in m.group(1)]
t.keys([b"\x1b[23;5~"])
t.collect(0.6)
back = t.screen()
check("with every key to the program the desktop's Full Screen key still is the "
      "desktop's: the game takes the whole display, picture and all, and "
      "Leave Full Screen brings the menu bar back",
      fs.find("File") is None and fs.find("Waiting for") is None and
      fs.find("Starting Steam") is None and back.find("File") is not None,
      fs.text()[:600] + "\n-----\n" + back.text()[:600])
check("and in full screen the picture keeps gamescope's own shape, as wide "
      "as the display and centred, rather than stretched to the display's",
      fsp and ",c=82," in fsp[-1] + "," and ",r=23," in fsp[-1] + ",", fsp)

# Keys to the Program off, from the Program menu: the desktop's keys work
# again.
m = t.screen().find("Program")
t.keys([press(*m), release(*m)] if m else [])
t.collect(0.4)
k = t.screen().find("Keys to the Program")
t.keys([press(*k), release(*k)] if k else [])
t.collect(0.4)
t.keys([b"\x1b[21~"])
t.collect(0.5)
sc = t.screen()
check("with Keys to the Program off the menu key opens the menu bar again",
      sc.find("Restart Desktop") is not None, sc)
t.keys([b"\x1b"])

# Closing the window ends the bridge.
bpid = st[0].get("pid") if st else None
t.collect(0.3)
m = t.screen().find("File")
t.keys([press(*m), release(*m)] if m else [])
t.collect(0.4)
k = t.screen().find("Close Window")
t.keys([press(*k), release(*k)] if k else [])
gone = wait(lambda: bpid and not os.path.exists("/proc/%d" % bpid))
check("closing the window ends the bridge", gone, (bpid, lines(brlog)[-3:]))

# The list's own program: no Steam integration, its command through a
# shell, and a click that holds the pointer.
open(MARK, "w").close()
t.keys([b"x"])
st2 = wait(lambda: [x for x in lines(brlog) if x["kind"] == "start"][1:])
argv2 = st2[0]["argv"] if st2 else []
check("a program of the list's own starts the bridge without Steam's "
      "integration, and a command with quotes in it runs through a shell",
      argv2[:1] == ["--no-place"] and "--steam" not in argv2 and
      argv2[-4:] == ["--", "/bin/sh", "-c", 'game "a b"'], argv2)
wait(lambda: len(blitkind("frame3")) > n1 + 5)
t.collect(1.0)
c0 = len(t.out)
t.keys([press(10, 40), release(10, 40)])
t.collect(0.6)
check("and a click in its window holds the pointer, as Capture on Click says",
      b"\x1b[?7701h" in t.out[c0:], (t.out[c0:][-300:], t.screen()))
t.quit(b"q", 0.5)
blit.terminate()
blit.wait()


# The list and the pane, in a plain shell: Steam's row is the app's own
# until it is changed, a program's engine is one more of its options, and
# the dialog shows each engine's own rows.
def hb(script, path=PATH):
    r = subprocess.run(
        [sx.HIBR, "-c", ". %s\n. %s\n. %s\n. %s\n%s"
         % (tree("programs/desktop/desktop.hibr"),
            tree("programs/desktop/apps/screens.hibr"),
            tree("programs/desktop/system/panel.hibr"),
            tree("programs/desktop/control-panel/screensset.hibr"), script)],
        env=dict(os.environ, HIBR_MODPATH=tree("build/mods"), PATH=path,
                 SCN_LIST=LIST, XDG_RUNTIME_DIR=D),
        capture_output=True, text=True, timeout=60)
    return r.stdout, r.stderr


open(LIST, "w").write("Old\ttrue\tfit\tfollow\tdefault\t-\t-\n")
out, err = hb(
    'echo "names=${SCNP_NAME[*]}|${SCNP_VIRT[*]}|${SCNP_OPT[1]}"\n'
    'declare -gA CP; n := screensset_rows 40\n'
    'for ((i = 0; i < n; i++)); do t+="${CP[40][$i][text]}=${CP[40][$i][btns]-}|"; done\n'
    'echo "rows=$t"\n'
    'scn_save; echo "file=$(wc -l < %s)"\n'
    'SCNP_VIRT[1]=0; SCNP_OPT[1]="${SCNP_OPT[1]};rel=1"; scn_save; scn_load\n'
    'echo "kept=${SCNP_NAME[*]}|${SCNP_VIRT[*]}|$(cut -f1 %s | tr \\\\n ,)"\n'
    'SCNP_NAME[1]="Big Picture"; scn_save; scn_load; echo "renamed=${SCNP_NAME[*]}"\n'
    % (LIST, LIST))
check("where Steam is installed it is on the list after X Screen's own "
      "programs, on Wayland Screen with Steam's integration, every key and "
      "the pointer held on a click, and the pane gives it no Delete",
      "names=Old Steam|0 1|engine=wayland;steam=1;keys=1;rel=1" in out and
      "|Old=del0:Delete|Steam=|" in out, out + err)
check("the pane says where the bridge is", "|Bridge=|" in out and
      "Wayland Screen=|Bridge=|X Screen=|" in out, out + err)
check("Steam's row is not written to the file until it is changed, and "
      "changed it is kept there once, as any other program is",
      "file=1" in out and "kept=Old Steam|0 0|Old,Steam," in out, out + err)
check("and renamed it is still the one Steam, not a second beside it",
      "renamed=Old Big Picture" in out, out + err)

out, err = hb(
    'declare -gA SCNF\n'
    'SCNF[9][k]=-1; SCNF[9][virt]=0; SCNF[9][eng]="Wayland Screen"\n'
    'o := scnf_order 9; echo "word=$o"\n'
    'SCNF[9][eng]="X Screen"; o := scnf_order 9; echo "xord=$o"\n'
    'SCNF[8][k]=1; SCNF[8][virt]=1; SCNF[8][eng]="Wayland Screen"\n'
    'o := scnf_order 8; echo "virt=$o"\n'
    'for o in "${SCNS_OKEYS[@]}"; do SCNF[7][o_$o]=default; done\n'
    'SCNF[7][eng]="Wayland Screen"; SCNF[7][o_steam]=off; SCNF[7][o_rel]=on\n'
    'o := scnf_opts 7; echo "opts=$o"\n'
    'SCNF[7][eng]="X Screen"; o := scnf_opts 7; echo "xopts=$o"\n'
    'scnf_bridge && echo bridge=yes || echo bridge=no\n')
check("a Wayland Screen program's dialog has only its own rows: no window, "
      "picture, Xvfb or size, and a built-in row no Delete",
      "word=name cmd o_steam end eng o_keys o_rel ok cancel" in out and
      "xord=name cmd win pic end eng o_fps" in out and
      "virt=name cmd o_steam end eng o_keys o_rel ok cancel" in out, out + err)
check("its engine is kept as one more option, and an X Screen program's "
      "line says nothing of an engine, as every line X Screen wrote",
      "opts=engine=wayland;rel=1" in out and "xopts=rel=1" in out, out + err)
o2, e2 = hb("scnf_bridge && echo bridge=yes || echo bridge=no\n",
            path="/usr/bin:/bin")
check("a new program is on Wayland Screen where the bridge is installed, "
      "and on X Screen where it is not",
      "bridge=yes" in out and "bridge=no" in o2, (out, o2, err, e2))
shutil.rmtree(D, True)
api.shutdown()
report(30)
