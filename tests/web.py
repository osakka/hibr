#!/usr/bin/env python3
"""The web module and the Browser app, against local pages: a headless
Chromium driven over its pipe, its pages drawn as cells.

Needs Chromium or Chrome; without one every check is skipped, and the
suite says so rather than failing. Pages are files in a folder of their
own, so nothing here touches the network.
"""
import base64, functools, http.server, os, shutil, struct, subprocess
import re, sys, tempfile, threading, zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import screen as sx
from screen import Term, check, report, press, release, load, tree, expect, bundle


if len(sys.argv) > 1:
    sx.HIBR = os.path.abspath(sys.argv[1])

D = tempfile.mkdtemp(prefix="hibr-web-")
PROF = os.path.join(D, "profile")
open(os.path.join(D, "a.html"), "w").write(
    "<html><head><title>Test page</title></head>"
    "<body style='background:#fff;font-family:sans-serif'>"
    "<h1>Hello hibr</h1>"
    "<p style='color:#c00'>A red paragraph with a <a href='b.html'>link to b</a>.</p>"
    "<form action='b.html'><input id=q name=q placeholder='search here'></form>"
    "<div style='background:#36c;color:#fff;width:200px;height:40px'>blue box</div>"
    "</body></html>")
open(os.path.join(D, "b.html"), "w").write(
    "<html><head><title>Page B</title></head><body><p>This is page B.</p>"
    "<a href='a.html'>back to a</a></body></html>")
# A page that spins in its own head: the parser reaches the script at the
# moment the navigation commits, so an eval issued then cannot run until the
# script ends -- and `load` fires just after it, while that eval is still
# waiting. That is what makes the window an event has to land in certain
# rather than lucky (Gitea #182). In the head rather than at the end of the
# body so that "the navigation committed" and "the page is busy" are the same
# instant, and the check needs no pause of its own between them.
open(os.path.join(D, "s.html"), "w").write(
    "<html><head><title>Page S</title>"
    "<script>const e = Date.now() + 800; while (Date.now() < e);</script>"
    "</head><body><p>The slow page.</p></body></html>")
# Black words on white and nothing else: a page whose picture can only have
# more than one colour in it if the words are *in* the picture. Everything
# else in a.html -- a blue box, a red paragraph -- gives a second colour
# whether the text is there or not, so it cannot answer this.
open(os.path.join(D, "t.html"), "w").write(
    "<html><head><title>Words page</title></head>"
    "<body style='background:#ffffff;margin:0'>"
    "<div style='color:#000000;font-size:38px;font-family:sans-serif'>"
    "WORDS WORDS WORDS<br>WORDS WORDS WORDS</div></body></html>")
# Pages for Control Panel > Browser (Gitea #276), one a setting: a line a
# script rewrites, a black picture on white, a dark scheme the page only
# shows when asked, a word placed a known distance from the left, and a page
# served over HTTP so a cookie can be set and forgotten.
open(os.path.join(D, "j.html"), "w").write(
    "<html><head><title>JS page</title></head><body><p id=x>STATICLINE</p>"
    "<script>document.getElementById('x').textContent='SCRIPTEDLINE'</script>"
    "</body></html>")


def blackpng(w, h):
    raw = b"".join(b"\x00" + b"\x00\x00\x00" * w for _ in range(h))

    def ch(t, d):
        return (struct.pack(">I", len(d)) + t + d +
                struct.pack(">I", zlib.crc32(t + d) & 0xffffffff))
    return (b"\x89PNG\r\n\x1a\n" +
            ch(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)) +
            ch(b"IDAT", zlib.compress(raw)) + ch(b"IEND", b""))


open(os.path.join(D, "black"), "wb").write(blackpng(320, 160))
open(os.path.join(D, "i.html"), "w").write(
    "<html><head><title>Image page</title></head>"
    "<body style='background:#fff;margin:0'><p>IMGPAGE</p>"
    "<img src='black'></body></html>")
open(os.path.join(D, "k.html"), "w").write(
    "<html><head><title>Dark page</title><style>"
    "body{background:#ffffff;color:#000}"
    "@media (prefers-color-scheme: dark){body{background:#000000;color:#fff}}"
    "</style></head><body><p>DARKTEST</p></body></html>")
open(os.path.join(D, "z.html"), "w").write(
    "<html><head><title>Zoom page</title></head><body style='margin:0'>"
    "<div style='margin-left:160px;font-family:monospace'>ZMARK</div>"
    "</body></html>")
class Quiet(http.server.SimpleHTTPRequestHandler):
    def log_message(self, *a):
        pass

    # Three answers of its own for Gitea #285: what the request carried
    # (/echo), a file to save rather than show (/dl), and a script whose
    # only work is to say it ran (/mark.js) -- so each setting is checked
    # by what reached the server or the page, not by what was asked for.
    def do_GET(self):
        if self.path.startswith("/echo"):
            body = ("<html><head><title>Echo</title></head><body>"
                    "<p id=h>LANG=%s DNT=%s GPC=%s</p></body></html>"
                    % (self.headers.get("Accept-Language", "-"),
                       self.headers.get("DNT", "-"),
                       self.headers.get("Sec-GPC", "-"))).encode()
            self.send_response(200)
            self.send_header("Content-Type", "text/html")
        elif self.path.startswith("/dl"):
            body = b"hello download\n"
            self.send_response(200)
            self.send_header("Content-Type", "application/octet-stream")
            self.send_header("Content-Disposition",
                             "attachment; filename=got.txt")
        elif self.path.startswith("/mark.js"):
            body = b"window.MARK = 1;"
            self.send_response(200)
            self.send_header("Content-Type", "text/javascript")
        else:
            return super().do_GET()
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)


SRV = http.server.ThreadingHTTPServer(
    ("127.0.0.1", 0), functools.partial(Quiet, directory=D))
threading.Thread(target=SRV.serve_forever, daemon=True).start()
HTTPB = "http://127.0.0.1:%d/b.html" % SRV.server_address[1]
HTTP = "http://127.0.0.1:%d" % SRV.server_address[1]
open(os.path.join(D, "mk.html"), "w").write(
    "<html><head><title>Mark</title><script src='/mark.js'></script>"
    "</head><body>MARKPAGE</body></html>")
open(os.path.join(D, "nt.html"), "w").write(
    "<html><head><title>Note</title></head><body><script>"
    "setTimeout(()=>{new Notification('Hi',{body:'there'})},200)"
    "</script></body></html>")
open(os.path.join(D, "geo.html"), "w").write(
    "<html><head><title>Geo</title></head><body><script>"
    "window.GEO='wait';navigator.geolocation.getCurrentPosition("
    "p=>{window.GEO=p.coords.latitude.toFixed(1)+' '+"
    "p.coords.longitude.toFixed(1)},e=>{window.GEO='refused '+e.code})"
    "</script></body></html>")
DLD = os.path.join(D, "downloads")
os.mkdir(DLD)
A = "file://" + os.path.join(D, "a.html")
B = "file://" + os.path.join(D, "b.html")
S = "file://" + os.path.join(D, "s.html")
W = "file://" + os.path.join(D, "t.html")
# A page that says, in its title, how wide it was laid out: what a cell of
# the terminal stands for in CSS pixels (Gitea #342).
open(os.path.join(D, "iw.html"), "w").write(
    "<html><head><title>IW</title><script>function s(){document.title="
    "'IW'+innerWidth}addEventListener('resize',s);s();</script></head>"
    "<body>x</body></html>")
IW = "file://" + os.path.join(D, "iw.html")
ENV = dict(os.environ, HIBR_WEB_PROFILE=PROF,
           HIBR_MODPATH=tree("build/mods"))


def have_browser():
    for n in ("chromium", "chromium-browser", "google-chrome",
              "google-chrome-stable", "chrome"):
        if shutil.which(n):
            return True
    return bool(os.environ.get("HIBR_WEB_BROWSER"))


def hb(script):
    r = subprocess.run([sx.HIBR, "-c", "mod load %s\n%s\nweb quit" %
                        (tree("build/mods/web.so"), script)],
                       env=ENV, capture_output=True, text=True, timeout=90)
    return r.stdout, r.stderr


if not have_browser():
    print("no Chromium or Chrome here: every check skipped")
    shutil.rmtree(D, True)
    report(0)
    sys.exit(0)

out, err = hb("t := web open %s; web wait $t 10000; web title $t; web url $t;"
              " web text $t; web links $t" % A)
check("web open loads a page; title, address, text and links come back",
      "Test page" in out and A in out and "Hello hibr" in out and
      "blue box" in out and (B + "\tlink to b") in out, out + err)
out, err = hb("t := web open %s; web wait $t 10000; web eval $t '1+2';"
              " web eval $t 'document.title'; web eval $t '({a:[1,true]})';"
              " r := web links $t; echo \"${r[0][href]} ${r[0][text]}\"" % A)
check("eval gives a value as text, an object as JSON; links fill a map",
      out.split("\n")[:3] == ["3", "Test page", '{"a":[1,true]}'] and
      (B + " link to b") in out, out + err)
out, err = hb("t := web open %s; web wait $t 10000; web size $t 20 70;"
              " web render $t; web click $t 4 25; web wait $t 5000; sleep 0.5;"
              " web title $t; web back $t; web wait $t 5000; sleep 0.3;"
              " web title $t; web forward $t; web wait $t 5000; sleep 0.3;"
              " web title $t" % A)
check("a click on a link follows it; back and forward go through history",
      out.split("\n")[:3] == ["Page B", "Test page", "Page B"], out + err)
# A render waits for its own reply by pumping the pipe, and the pump hands
# every event queued behind it to wb_event -- Page.loadEventFired among them,
# which sets dirty. Clearing dirty at the *end* of a render therefore wiped
# the one signal saying the page it had just read was already out of date,
# with nothing to set it again: a Browser window kept a blank new document
# for ever (Gitea #182). `idle` is asserted beside `dirty` on purpose -- it
# says the load event really was consumed inside that render, without which
# the check would pass for no reason.
out, err = hb("t := web open %s; web wait $t 10000; web size $t 20 70;"
              " web render $t > /dev/null; web go $t %s;"
              " n=0;"
              " while [ $n -lt 300 ]; do"
              "   web poll > /dev/null; u := web url $t;"
              "   case $u in *s.html) break ;; esac;"
              "   n=$((n + 1)); sleep 0.01;"
              " done;"
              " web render $t > /dev/null;"
              " if web dirty $t; then echo dirty; else echo clean; fi;"
              " if web loading $t; then echo loading; else echo idle; fi;"
              " echo \"waited $n\"" % (A, S))
check("an event that lands inside a render leaves the tab dirty",
      out.split("\n")[:2] == ["dirty", "idle"], out + err)
# `web set T render async` (ADR 0049, hibr-browser's NONBLOCKING step 1): a
# render sends and returns, and a later one builds what came back, so a
# page whose main thread is busy holds no frame. The page blocks itself for
# 1.5 s; each render is timed while it waits, until one has built the page
# (its height is known). The same loop synchronous is the control: unless
# it waits, the page was never blocked and the async figure says nothing.
def blocked(mode):
    out, err = hb("t := web open %s; web wait $t 10000; web size $t 20 70;"
                  " web set $t render %s;"
                  " web eval $t 'setTimeout(()=>{const e=Date.now()+1500;"
                  "while(Date.now()<e);},0);1' > /dev/null; sleep 0.1;"
                  " max=0; n=0; got=;"
                  " while [ $n -lt 80 ]; do"
                  "   s=${EPOCHREALTIME/./}; web render $t > /dev/null;"
                  "   e=${EPOCHREALTIME/./}; d=$(( (e - s) / 1000 ));"
                  "   [ $d -gt $max ] && max=$d;"
                  "   sc := web scroll $t; read -r a h b <<< \"$sc\";"
                  "   [ \"${h:-0}\" -gt 0 ] && { got=$n; break; };"
                  "   sleep 0.05; n=$((n + 1));"
                  " done; echo \"max $max got $got\"" % (A, mode))
    w = out.split()
    try:
        return int(w[1]), (int(w[3]) if len(w) > 3 else None), out + err
    except (IndexError, ValueError):
        return None, None, out + err
# `web evask` / `web evget` (cap evask, the shape hibr-browser's webkit.so
# offers): a script asked now and read on a later frame. While the page is
# busy the answer is "not yet" (1); then the value (0), read once -- a
# second read is "not yet" again -- and a script the page refused is 2, so
# a caller can tell waiting from failing.
out, err = hb("t := web open %s; web wait $t 10000;"
              " web eval $t 'setTimeout(()=>{const e=Date.now()+800;"
              "while(Date.now()<e);},0);1' > /dev/null; sleep 0.1;"
              " i := web evask $t '6*7'; web evget $t $i > /dev/null; echo \"first $?\";"
              " n=0; while [ $n -lt 100 ]; do v := web evget $t $i && break;"
              " sleep 0.05; n=$((n + 1)); done; echo \"val $v\";"
              " web evget $t $i > /dev/null; echo \"again $?\";"
              " j := web evask $t 'throw new Error(\"no\")'; rc=1; n=0;"
              " while [ $rc = 1 ] && [ $n -lt 100 ]; do web evget $t $j > /dev/null; rc=$?;"
              " sleep 0.05; n=$((n + 1)); done; echo \"refused $rc\"; web caps" % A)
check("evask asks without waiting; evget says not yet, then the value once, and a refusal apart",
      "first 1" in out and "val 42" in out and "again 1" in out and
      "refused 2" in out and "evask" in out, out + err)
smax, sgot, sout = blocked("sync")
amax, agot, aout = blocked("async")
check("an async render never waits on a busy page, and builds it after",
      smax is not None and smax >= 800 and amax is not None and
      amax < (400 if os.environ.get("ASAN_OPTIONS") else 100) and agot is not None and agot >= 5,
      (sout, aout))
out, err = hb("t := web open %s; web wait $t 10000; web size $t 20 70;"
              " web render $t; web click $t 6 3; web type $t 'hello there';"
              " web key $t backspace; web eval $t 'document.getElementById(\"q\").value';"
              " web key $t enter; web wait $t 5000; sleep 0.5; web url $t" % A)
check("typing goes into the field clicked, keys edit it, enter submits",
      out.split("\n")[0] == "hello ther" and "b.html?q=hello+ther" in out,
      out + err)
out, err = hb("t := web open %s; u := web open %s; web tabs; web close $u;"
              " web tabs; web text 99; echo \"st $?\"" % (A, B))
check("tabs open and close; a tab that is not there is refused",
      out.split("\n")[:2] == ["1 2", "1"] and "st 1" in out and
      "no tab 99" in err, out + err)
out, err = hb("t := web open %s; web wait $t 10000; web render $t;"
              " web draw $t 1 1; echo \"st $?\"" % A)
check("drawing needs a display, and says so", "st 1" in out and
      "no display" in err, out + err)


# The Browser app's own checks live with it, in hibr-browser's suites
# (0.99.524): what is here is the web module's.

# A browser that will not start says why in the log, and a page the browser
# refuses to load says so too (Gitea #327): Chromium's own output went to
# /dev/null, and a refused navigation was a debug line, so a Browser window
# that showed nothing left nothing behind to read. The stand-in browser here
# says what Chromium says when it is run as root.
FAKE = os.path.join(D, "fakechromium")
open(FAKE, "w").write("#!/bin/sh\necho 'Running as root without --no-sandbox"
                      " is not supported. See https://crbug.com/638180.' >&2\n"
                      "exit 1\n")
os.chmod(FAKE, 0o755)
r = subprocess.run([sx.HIBR, "-c", "mod load %s\nweb open about:blank"
                    % tree("build/mods/web.so")],
                   env=dict(ENV, HIBR_WEB_BROWSER=FAKE), capture_output=True,
                   text=True, timeout=60)
check("a browser that will not start says why, in its own words",
      "web: the browser did not start: Running as root without --no-sandbox"
      in r.stderr, r.stderr)
out, err = hb("t := web open http://nosuch.invalid/; web wait $t 8000")
check("a page the browser refuses to load is named in the log with the "
      "browser's reason", "web: http://nosuch.invalid/: net::ERR_" in err, err)

# --- the module's settings, each asked of a tab directly ---------------------

out, err = hb("t := web open; web set $t ua hibr-test/1; web go $t %s;"
              " web wait $t 10000; web eval $t navigator.userAgent" % A)
check("a tab's user agent is the one it is told",
      out.strip() == "hibr-test/1", out + err)
out, err = hb("t := web open %s; web wait $t 10000;"
              " web eval $t \"document.cookie='k=v; max-age=3600'\" > /dev/null;"
              " a := web eval $t document.cookie; web clear cookies;"
              " web reload $t; web wait $t 10000;"
              " b := web eval $t document.cookie; echo \"[$a] [$b]\"" % HTTPB)
check("Clear Cookies forgets what a site set",
      "[k=v] []" in out, out + err)
out, err = hb("t := web open %s; web wait $t 10000; web go $t %s;"
              " web wait $t 10000; web clear cache; echo \"cache $?\";"
              " web clear history $t; web back $t; echo \"back $?\";"
              " web title $t" % (A, B))
check("Clear Cache asks the browser to forget; Clear History empties back",
      "cache 0" in out and "back 1" in out and "Page B" in out, out + err)
out, err = hb("t := web open; web set $t js maybe; echo \"st $?\";"
              " web set $t zoom 5; echo \"st $?\"; web clear nothing;"
              " echo \"st $?\"")
check("web set and web clear refuse what they do not know",
      out.split() == ["st", "2", "st", "2", "st", "2"], out + err)

# Gitea #285, each by what reached the server or the page.
out, err = hb("t := web open; web go $t %s/echo; web wait $t 10000;"
              " a := web text $t; web set $t lang 'fr-FR,fr;q=0.9';"
              " web go $t %s/echo; web wait $t 10000; b := web text $t;"
              " c := web eval $t navigator.language;"
              " echo \"[$a] [$b] [$c]\"" % (HTTP, HTTP))
check("Language: the pages are asked for the languages given, and a "
      "page's scripts see the first",
      "LANG=fr-FR,fr;q=0.9" in out and "] [fr-FR]" in out and
      "[LANG=fr-FR,fr;q=0.9" not in out.split("] [")[0], out + err)
out, err = hb("t := web open; web set $t dnt on; web go $t %s/echo;"
              " web wait $t 10000; a := web text $t;"
              " g := web eval $t navigator.globalPrivacyControl;"
              " web set $t dnt off; web go $t %s/echo; web wait $t 10000;"
              " b := web text $t; echo \"[$a] [$g] [$b]\"" % (HTTP, HTTP))
check("Do Not Track: every request says so, and so does the page; off "
      "says nothing",
      "[LANG=" in out and "DNT=1 GPC=1]" in out and "[true]" in out and
      "DNT=- GPC=-]" in out, out + err)
out, err = hb("t := web open; web go $t %s/mk.html; web wait $t 10000;"
              " a := web eval $t 'String(window.MARK)';"
              " web set $t block '*mark.js*'; web go $t %s/mk.html;"
              " web wait $t 10000; b := web eval $t 'String(window.MARK)';"
              " echo \"[$a] [$b]\"" % (HTTP, HTTP))
check("Block: an address that matches a pattern is never fetched",
      "[1] [undefined]" in out, out + err)
out, err = hb("web permit geolocation allow; t := web open;"
              " web set $t geo '51.5 -0.1'; web go $t %s/geo.html;"
              " web wait $t 10000; sleep 0.5; a := web eval $t window.GEO;"
              " web permit geolocation refuse; web reload $t;"
              " web wait $t 10000; sleep 0.5; b := web eval $t window.GEO;"
              " echo \"[$a] [$b]\"" % HTTP)
check("Location: allowed, a page is told the position given; refused, "
      "it is refused",
      "[51.5 -0.1] [refused 1]" in out, out + err)
out, err = hb("web permit notifications allow; t := web open;"
              " web set $t notes on; web go $t %s/nt.html; web wait $t 10000;"
              " n=0; while [ $n -lt 50 ] && ! web notes > /dev/null 2>&1;"
              " do sleep 0.1; n=$((n + 1)); done;"
              " web go $t %s/nt.html; web wait $t 10000; sleep 0.6;"
              " x := web notes; echo \"[$x]\"" % (HTTP, HTTP))
check("Notifications: a page's note is handed over, title and body",
      "[Hi\tthere]" in out, out + err)
out, err = hb("t := web open; web go $t %s/dl; sleep 1.5;"
              " ls %s | wc -l; web downloads %s; web go $t %s/dl;"
              " n=0; while [ $n -lt 50 ]; do"
              "   x := web downloaded && break; sleep 0.1; n=$((n + 1));"
              " done; echo \"[$x]\"; cat %s/got.txt"
              % (HTTP, DLD, DLD, HTTP, DLD))
check("Downloads: none until a folder is given; then the file lands there "
      "and is told",
      out.split("\n")[0].strip() == "0" and
      ("[done\t%s/got.txt]" % DLD) in out and "hello download" in out,
      out + err)
pdf = os.path.join(D, "page.pdf")
out, err = hb("t := web open %s; web wait $t 10000; web pdf $t %s;"
              " echo \"st $?\"" % (A, pdf))
check("Print to PDF writes the page as a PDF",
      "st 0" in out and os.path.exists(pdf) and
      open(pdf, "rb").read(5) == b"%PDF-", out + err)

# A page's sound plays through hibr's own media module (Gitea #305): the
# browser records the tab -- the only way that reaches sound from another
# origin, which is where every site's media lives -- and the Browser feeds
# it to a player of its own, here writing to a file. The tone is served
# from a second server, so it is cross-origin, as a CDN's would be.
import math, wave
TONE = os.path.join(D, "tone.wav")
with wave.open(TONE, "wb") as wv:
    wv.setnchannels(1)
    wv.setsampwidth(2)
    wv.setframerate(48000)
    wv.writeframes(b"".join(struct.pack("<h", int(8000 * math.sin(2 * math.pi * 440 * i / 48000)))
                            for i in range(48000 * 20)))
SRV2 = http.server.ThreadingHTTPServer(
    ("127.0.0.1", 0), functools.partial(Quiet, directory=D))
threading.Thread(target=SRV2.serve_forever, daemon=True).start()
TONEURL = "http://127.0.0.1:%d/tone.wav" % SRV2.server_address[1]
open(os.path.join(D, "snd.html"), "w").write(
    "<html><head><title>Sound</title></head><body>"
    "<audio id=a src='%s' loop></audio>"
    "<button style='width:100%%;height:200px' onclick='a.play()'>PLAYBUTTON</button>"
    "</body></html>" % TONEURL)
# The browser takes a tab's capture of itself without asking, so a page's
# own call is refused -- in a frame it makes as well -- or any page could
# read the pixels of whatever it frames.
out, err = hb("t := web open %s/snd.html; web wait $t 10000; web size $t 24 80;"
              " web render $t > /dev/null; web click $t 4 10;"
              " web eval $t 'navigator.mediaDevices.getDisplayMedia({video:true,"
              "preferCurrentTab:true}).then(()=>\"GRANTED\",e=>e.name)';"
              " web eval $t 'var f=document.createElement(\"iframe\");"
              "document.body.appendChild(f);f.contentWindow.navigator.mediaDevices"
              ".getDisplayMedia({video:true,preferCurrentTab:true})"
              ".then(()=>\"GRANTED\",e=>e.name)'" % HTTP)
check("a page cannot capture its own tab, nor from a frame it makes",
      out.split() == ["NotAllowedError", "NotAllowedError"], out + err)
# The capture starts a tick after the press that asks for it, never inside
# it: started in the page's pointerdown it swallowed the press, and a click
# on a link failed to follow it about one time in three -- 4 of 12, measured
# -- at random. Twelve tries: the old way passes all of them by luck less
# than one run in a hundred.
out, err = hb("n=0; for i in 1 2 3 4 5 6 7 8 9 10 11 12; do t := web open %s; web wait $t 10000;"
              " web size $t 22 74; web render $t > /dev/null; web sound $t click;"
              " rc=$(web eval $t 'var r=document.querySelector(\"a\").getBoundingClientRect();"
              "Math.floor((r.top+r.height/2)/16)+\" \"+Math.floor((r.left+4)/8)');"
              " web click $t $rc; sleep 1.2; case $(web url $t) in *b.html) n=$((n+1)) ;; esac;"
              " web close $t; done; echo \"followed $n\"" % A)
check("with a tab's sound on, a click on a link still follows it, every time",
      "followed 12" in out, out + err)
# And a click that plays nothing records nothing: the recording starts when
# the page plays, so a page of text -- Gitea, say -- never has its tab
# captured, where every click on every page used to start one.
out, err = hb("t := web open %s; web wait $t 10000; web size $t 22 74;"
              " web render $t > /dev/null; web sound $t click; web click $t 0 2;"
              " sleep 1.5; s=$(web eval $t '(window.__hibrTap||{}).snd||\"-\"');"
              " echo \"snd $s\"" % A)
check("and a click on a page that plays nothing starts no recording",
      "snd -" in out, out + err)

# A page between documents has no head and no root element yet, and the
# styles the module adds -- the text hidden for a render, a zoom -- were
# appended to whichever of the two existed: a TypeError on stderr, which a
# gate's twelve quick navigations met once under load. Removing the root
# makes that moment certain.
out, err = hb("t := web open %s; web wait $t 10000;"
              " web eval $t 'document.documentElement.remove(); 1' > /dev/null;"
              " web size $t 24 80; web render $t > /dev/null;"
              " web set $t zoom 150; echo rendered" % A)
check("a page with no root element renders and zooms without a script error",
      "rendered" in out and "TypeError" not in err, out + err)

SRV2.shutdown()
SRV.shutdown()
shutil.rmtree(D, True)
report(26)
