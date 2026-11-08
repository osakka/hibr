#!/usr/bin/env python3
"""The Steam window (Gitea #486, step 4): Steam's Big Picture under Blit's
gamescope bridge, in a desktop window.

A stand-in bridge (bridgeserve.py) is started as the window would start the
real one, and a stand-in Blit (frameserve.py) is the display's: frames go
from the one to the other through the frames module and the console, the
bridge's EIS socket becomes an input route at Blit, and the route follows
the window's focus. Every check reads what crossed a socket or the terminal,
never the app's own state.
"""
import http.server, json, os, shutil, subprocess, sys, tempfile, threading, time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import screen as sx
from screen import Term, check, report, load, tree, press, release

D = tempfile.mkdtemp(prefix="hibr-steamwin-")
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

p = sx.scratch("steamwin.hibr")
open(p, "w").write(
    "%s. %s\n. %s\nSTW_BRIDGE=%s\n"
    "dt_open\nstw_play\ndt_run\ndt_close\n"
    % (load("console"), tree("programs/desktop/desktop.hibr"),
       tree("programs/desktop/apps/Games/steamwin.hibr"), wrap))
t = Term(p, rows=26, cols=82, settle=1.0, cellw=8, cellh=16, caps=["kitty", "lock"],
         env={"HIBR_GFX": "kitty", "BLIT_FRAME_SOCKET": "@" + nm,
              "BLIT_FRAME_TOKEN": "tok", "XDG_RUNTIME_DIR": D,
              "BLIT_SERVER_PORT": str(api.server_address[1]),
              "BLIT_API_TOKEN": "api-sekrit", "BLIT_PANE_ID": "pane-display-2-1"})


def blitkind(k, **kw):
    return [x for x in lines(blitlog) if x["kind"] == k and
            all(x.get(a) == b for a, b in kw.items())]


st = wait(lambda: [x for x in lines(brlog) if x["kind"] == "start"])
argv = st[0]["argv"] if st else []
check("the window starts the bridge with no placement of its own, Steam's "
      "integration and Big Picture, at the window's picture size in pixels",
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
      "anything the desktop watches", lg.endswith("/hibr/steam.log") and
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

# Keys to the Game: the menu key is the game's, and nothing opens.
ons = len(blitkind("input", verb="on"))
t.keys([b"\x1b[21~"])
t.collect(0.5)
sc = t.screen()
check("with Keys to the Game a desktop shortcut is the game's: the menu "
      "key opens nothing and the route stays on",
      sc.find("Restart Desktop") is None and not blitkind("input", verb="off"), sc)

# A menu opened with the mouse takes the keys, and the route goes off.
m = sc.find("Steam")
t.keys([press(*m), release(*m)] if m and m[0] == 0 else [])
t.collect(0.5)
offs = wait(lambda: blitkind("input", verb="off"))
check("a menu opened over the game turns its route off", offs,
      [x for x in lines(blitlog) if x["kind"] == "input"])
t.keys([b"\x1b"])
back = wait(lambda: len(blitkind("input", verb="on")) > ons)
check("and closing it turns the route back on", back,
      [x for x in lines(blitlog) if x["kind"] == "input"])

# A click holds the pointer.
t.keys([press(10, 40), release(10, 40)])
t.collect(0.4)
check("a click in the window holds the pointer for the game",
      b"\x1b[?7701h" in t.out, t.out[-200:])
t.keys([b"\x1b[21;5~"])
t.collect(0.3)

# Full Screen is still the desktop's, and so is leaving it.
t.keys([b"\x1b\r"])
t.collect(0.6)
fs = t.screen()
t.keys([b"\x1b[23;5~"])
t.collect(0.6)
back = t.screen()
check("with Keys to the Game the desktop's Full Screen key still is the "
      "desktop's: the game takes the whole display, picture and all, and "
      "Leave Full Screen brings the menu bar back",
      fs.find("File") is None and fs.find("Waiting for") is None and
      fs.find("Starting Steam") is None and back.find("File") is not None,
      fs.text()[:600] + "\n-----\n" + back.text()[:600])

# Keys to the Game off, from the Steam menu: the desktop's keys work again.
m = t.screen().find("Steam")
t.keys([press(*m), release(*m)])
t.collect(0.4)
k = t.screen().find("Keys to the Game")
t.keys([press(*k), release(*k)] if k else [])
t.collect(0.4)
t.keys([b"\x1b[21~"])
t.collect(0.5)
sc = t.screen()
check("with Keys to the Game off the menu key opens the menu bar again",
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
t.quit(b"q", 0.5)
blit.terminate()
blit.wait()
shutil.rmtree(D, True)
api.shutdown()
report(20)
