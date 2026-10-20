#!/usr/bin/env python3
"""The web module and the Browser app, against local pages: a headless
Chromium driven over its pipe, its pages drawn as cells.

Needs Chromium or Chrome; without one every check is skipped, and the
suite says so rather than failing. Pages are files in a folder of their
own, so nothing here touches the network.
"""
import base64, functools, http.server, os, shutil, struct, subprocess
import sys, tempfile, threading, zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import screen as sx
from screen import Term, check, report, press, release, load, tree

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


def brun(feed, arg=A, env=None, wait=1.5, until=None, gone=None, ready=None,
         click=None, cell=(0, 0)):
    s = tempfile.mkdtemp(prefix="hibr-web-s-")
    p = os.path.join(s, "session.hibr")
    open(p, "w").write("%s\n. %s\n. %s\ndt_open\n"
                       'dt_new Browser 22 76 1 2 browser %s\ndt_run\ndt_close\n'
                       % (load("console"), tree("programs/desktop/desktop.hibr"),
                          tree("programs/desktop/apps/Internet/browser.hibr"), arg))
    e = {"HIBR_WEB_PROFILE": PROF, "XDG_DATA_HOME": os.path.join(D, "data"),
         "BW_HOME": ""}
    e.update(env or {})
    t = Term(p, env=e, settle=3.0, rows=26, cols=80, cellw=cell[0], cellh=cell[1])
    # Chromium's own start-up is what a fixed settle cannot bound: under a
    # full parallel run it has taken longer than three seconds, and a click
    # at a tab's own coordinates then lands on a bar that is not drawn yet.
    # ready is the text the feed needs on screen before a key is sent; until
    # and gone are what the feed is waited on for afterwards.
    if ready is not None:
        for _ in range(140):
            if ready in t.screen().text():
                break
            t.collect(0.3)
        if ready not in t.screen().text():
            print("   (brun: %r never appeared -- Chromium is still loading)" % ready)
    # A tab's own close box moves with the title's width: "New Tab" until
    # the page's title arrives, "Test page" after, two columns apart. So a
    # click is aimed at the glyph where it actually is, never at a column
    # worked out from the title the test expects to be there by then.
    if click is not None:
        txt, off = click
        pos = t.screen().find(txt)
        if pos is None:
            print("   (brun: %r is not on screen to click)" % txt)
        else:
            t.keys([press(pos[0], pos[1] + off), release(pos[0], pos[1] + off)],
                   settle=0.5)
    t.keys(list(feed) + [wait], settle=0.5)
    for _ in range(60):
        seen = t.screen().text()
        if (until is None or until in seen) and (gone is None or gone not in seen):
            break
        t.collect(0.3)
    sc = t.screen()
    t.quit(None, 0.5)
    shutil.rmtree(s, True)
    return sc


sc = brun([], until="Hello hibr")
check("Browser draws the page: its text, colours, the title on the tab",
      sc.find("Hello hibr") is not None and
      sc.find("A red paragraph with a link to b.") is not None and
      sc.find("Test page") is not None and
      sc.find("┤ Browser [Test page] ├") is not None, sc)
r = sc.find("A red paragraph")
check("the paragraph is drawn in the page's red",
      r is not None and sc.style(r[0], r[1] + 2)["fg"] == "#cc0000",
      sc.style(r[0], r[1] + 2) if r else sc)
lk = sc.find("link to b")
sc = brun([press(lk[0], lk[1] + 2), release(lk[0], lk[1] + 2), 1.5],
          until="This is page B.") if lk else sc
check("a click on a link in the window follows it",
      lk is not None and sc.find("This is page B.") is not None, sc)
# alt-b, not alt-left: a window's own key is looked at before the desktop's,
# so the Browser taking the arrows made them mean something different here
# from everywhere else. alt-[ and alt-] cannot be bound at all -- ESC [ and
# ESC ] are the CSI and OSC introducers, and the decoder consumes them.
sc = brun([press(lk[0], lk[1] + 2), release(lk[0], lk[1] + 2), 2.5,
           b"\x1bb", 1.5], until="Hello hibr", ready="Hello hibr")
check("alt-b goes back", sc.find("Hello hibr") is not None and
      sc.find("This is page B.") is None, sc)
# A lone tab has no tab strip, so the address is the window's first row
# and the page starts on its second; a second tab brings the strip back
# (Gitea #292). ctrl-t is New Tab, with the keyboard in the address.
sc = brun([1.0], ready="Test page")
check("one tab: no tab strip, the address on the first row",
      sc.find("Test page ✕") is None and "file://" in sc.row(2), sc)
sc = brun([1.0, b"\x14"] + [c.encode() for c in B] + [b"\r", 2.0],
          ready="Test page", until="This is page B.")
check("ctrl-t opens a tab with the keyboard in the address; enter goes "
      "there, and the strip is back for two",
      sc.find("Test page ✕") is not None and sc.find("Page B ✕") is not None
      and sc.find("This is page B.") is not None, sc)
sc = brun([b"\x1b[21~", b"\x1b[C", b"\x1b[C", b"\x1b[C", b"\x1b[C", b"\r", 0.8])
marks = os.path.join(D, "data", "hibr", "bookmarks.tsv")
text = open(marks).read() if os.path.exists(marks) else ""
check("Bookmarks > Add Bookmark keeps the page in a file of its own",
      text == "Test page\t%s\n" % A, repr(text))
sc = brun([b"\x1b[21~", b"\x1b[C", b"\x1b[C", b"\x1b[C", b"\x1b[C", 0.5])
check("and the Bookmarks menu lists it, and offers to remove it",
      sc.find("Remove Bookmark") is not None and sc.find("Test page") is not None,
      sc)
sc = brun([1.0, b"\x17"], ready="Test page", gone="\u2524 Browser")
check("ctrl-w on the last tab closes the window",
      sc.find("┤ Browser") is None, sc)
# A terminal that can paint pixels gets the page as a picture and no text
# layer at all: a text cell paints its own background, so anything drawn over
# the bitmap would box itself out of it (ADR 0037). The window's own chrome
# -- the tabs, the address -- is text as it always was, above the picture.
sc = brun([2.0], ready="Test page", cell=(8, 16),
          env={"HIBR_GFX": "sixel", "DT_IMGMODE": "sixel"})
check("where the terminal can paint pixels the page is a picture, the chrome text",
      len(sc.images) == 1 and sc.images[0][0] >= 3 and
      sc.find("Test page") is not None and sc.find("Hello hibr") is None,
      (sc.images, sc))
sc = brun([2.0], ready="Hello hibr", cell=(8, 16), env={"DT_IMGMODE": "half"})
check("and asked for blocks it is text over colours, with no picture sent",
      not sc.images and sc.find("Hello hibr") is not None, (sc.images, sc))
# And the words are *in* that picture. The screenshot is deliberately taken
# with the page's text made transparent, because the cells path draws it
# again over the top -- and until 0.99.173 that happened for the pixels
# path too, which draws nothing over it: a page of backgrounds and images
# with no words anywhere in it (Gitea #210). The check above asserts the
# text is not in the *cells*, which is right, stays right, and is satisfied
# just as well by a picture with no text in it at all -- so this one reads
# the picture itself. Black words on white: 7602 of its 161024 pixels are
# dark with the fix and **0** without, which is the whole bug as a number.
#
# kitty rather than sixel, because the harness keeps the payload only for
# an APC -- a DCS's bytes are counted and dropped. The page is sent raw
# (`f=24`), since cn_image compresses the wallpaper alone, but the inflate
# is attempted anyway so a change of that gate cannot quietly blind this.
sc = brun([2.0], arg=W, ready="Words page", cell=(8, 16),
          env={"HIBR_GFX": "kitty", "DT_IMGMODE": "pixels"})
dark = 0
for d in sc.imgdata:
    raw = base64.b64decode(d + "=" * (-len(d) % 4))
    try:
        raw = zlib.decompress(raw)
    except zlib.error:
        pass
    for i in range(0, len(raw) - 2, 3):
        if raw[i] < 110 and raw[i + 1] < 110 and raw[i + 2] < 110:
            dark += 1
check("and the page's own words are in the picture, not made transparent",
      len(sc.images) == 1 and dark > 500, (dark, len(sc.imgdata), sc.apc[:1]))

sc = brun([], env={"HIBR_WEB_BROWSER": "/nonexistent/chromium"})
check("without a browser to drive, the window says what is missing",
      sc.find("No Chromium or Chrome") is not None, sc)

# --- Control Panel > Browser (Gitea #276) --------------------------------------

J = "file://" + os.path.join(D, "j.html")
IM = "file://" + os.path.join(D, "i.html")
open(os.path.join(D, "tick.html"), "w").write(
    "<html><head><title>Tick</title></head><body><p>TICK <span id=n>0</span>"
    "</p><script>let i=0;setInterval(()=>{document.getElementById('n')"
    ".textContent=++i},200)</script></body></html>")
TK = "file://" + os.path.join(D, "tick.html")
open(os.path.join(D, "long.html"), "w").write(
    "<html><head><title>Long</title></head><body style='margin:0'>"
    + "".join("<div style='height:16px'>line %d</div>" % i for i in range(300))
    + "</body></html>")
LG = "file://" + os.path.join(D, "long.html")
K = "file://" + os.path.join(D, "k.html")
Z = "file://" + os.path.join(D, "z.html")

sc = brun([], arg="", env={"BW_HOME": B}, until="This is page B.")
check("a new window opens at the home page",
      sc.find("This is page B.") is not None, sc)
on = brun([], arg=J, until="SCRIPTEDLINE")
off = brun([], arg=J, env={"BW_JS": "0"}, until="STATICLINE")
check("JavaScript off: the page's own script does not run",
      on.find("SCRIPTEDLINE") is not None and
      off.find("STATICLINE") is not None and off.find("SCRIPTEDLINE") is None,
      (on, off))


def darkcells(sc):
    n = 0
    for r in range(3, 24):
        for c in range(1, 79):
            bg = sc.style(r, c).get("bg") or ""
            if bg.startswith("#") and len(bg) == 7 and \
               max(int(bg[1:3], 16), int(bg[3:5], 16), int(bg[5:7], 16)) < 0x10:
                n += 1
    return n


on = brun([2.0], arg=IM, until="IMGPAGE", env={"DT_IMGMODE": "half"})
off = brun([2.0], arg=IM, until="IMGPAGE",
           env={"DT_IMGMODE": "half", "BW_IMAGES": "0"})
check("Images off: the page lays out without its pictures",
      darkcells(on) > 50 and darkcells(off) == 0,
      (darkcells(on), darkcells(off)))
light = brun([1.0], arg=K, until="DARKTEST", env={"DT_IMGMODE": "half"})
dark = brun([1.0], arg=K, until="DARKTEST",
            env={"DT_IMGMODE": "half", "BW_DARK": "1"})
pl, pd = light.find("DARKTEST"), dark.find("DARKTEST")
check("Ask Sites for Dark Pages: a page's dark scheme is what is drawn",
      pl is not None and pd is not None and
      light.style(pl[0], pl[1])["bg"] == "#ffffff" and
      dark.style(pd[0], pd[1])["bg"] == "#000000",
      (light.style(*pl) if pl else light, dark.style(*pd) if pd else dark))
z1 = brun([1.0], arg=Z, until="ZMARK", env={"DT_IMGMODE": "half"})
z2 = brun([1.0], arg=Z, until="ZMARK",
          env={"DT_IMGMODE": "half", "BW_ZOOM": "200"})
p1, p2 = z1.find("ZMARK"), z2.find("ZMARK")
check("Page Zoom: the page is laid out larger, its words further in",
      p1 is not None and p2 is not None and p2[1] - p1[1] >= 15,
      (p1, p2))
sc = brun([], arg=A, env={"BW_MODE": "text"}, until="link to b[1]")
check("Text only: the page's words, each link numbered, and no picture",
      sc.find("Hello hibr") is not None and sc.find("link to b[1]") is not None
      and not sc.images, sc)
sc = brun([1.0], arg=A, env={"BW_MODE": "text"}, ready="link to b[1]",
          click=("[1]", 1), until="This is page B.")
check("and a click on a link's number follows it",
      sc.find("This is page B.") is not None, sc)
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
# And through the Browser, from its settings: what a tab is told, and a
# download told in a note as it lands.
sc = brun([], arg=HTTP + "/echo", until="DNT=1",
          env={"DT_IMGMODE": "half", "BW_LANG": "de-DE", "BW_DNT": "1"})
check("the Browser sends a tab the languages and Do Not Track it is set to",
      sc.find("LANG=de-DE DNT=1 GPC=1") is not None, sc)
DLD2 = os.path.join(D, "dl2")
sc = brun([], arg=HTTP + "/dl", until="Downloaded got.txt",
          env={"DT_IMGMODE": "half", "BW_DL": "1", "BW_DLDIR": DLD2})
check("and with downloads on, a file lands in the folder and a note says so",
      sc.find("Downloaded got.txt") is not None and
      os.path.exists(os.path.join(DLD2, "got.txt")), sc)
# A page that changes on its own is drawn as it changes, with nothing
# typed (#289): the browser says when the page repaints, and the window
# reads it again, no more often than DT_TERMMS.
import re as _re


def ticked(sc):
    m = _re.search(r"TICK (\d+)", sc.text())
    return int(m.group(1)) if m else -1


sc1 = brun([1.0], arg=TK, until="TICK", env={"DT_IMGMODE": "half"})
sc2 = brun([3.0], arg=TK, until="TICK", env={"DT_IMGMODE": "half"})
check("a page that changes on its own is drawn again with nothing typed",
      ticked(sc2) >= 8, (ticked(sc1), ticked(sc2), sc2))
# A scroll bar in the page's last column (#292), and a click low on it
# scrolls the page down there.
sc = brun([1.0], arg=LG, until="line 3", env={"DT_IMGMODE": "half"})
bar = [r for r in range(3, 24) if sc.at(r, 76) in ("\u2588", "\u2502")]
check("a long page has a scroll bar beside it", len(bar) >= 10,
      ([sc.at(r, 76) for r in range(3, 24)], sc))
sc = brun([1.0, press(20, 76), release(20, 76), 1.5], arg=LG,
          ready="line 3", until="line 2", env={"DT_IMGMODE": "half"})
check("and a click low on it scrolls the page down there",
      sc.find("line 0") is None and
      _re.search(r"line 2[0-9][0-9]", sc.text()) is not None, sc)
pdf = os.path.join(D, "page.pdf")
out, err = hb("t := web open %s; web wait $t 10000; web pdf $t %s;"
              " echo \"st $?\"" % (A, pdf))
check("Print to PDF writes the page as a PDF",
      "st 0" in out and os.path.exists(pdf) and
      open(pdf, "rb").read(5) == b"%PDF-", out + err)

# The line of key hints under the page went to the menus, each key beside
# its item, and the page has that row now.
sc = brun([], until="Hello hibr", ready="Hello hibr")
check("no line of key hints under the page", "alt-b back" not in sc.text() and
      "f6 address" not in sc.text(), sc)
sc = brun([], until="Reload", ready="Hello hibr", click=("Go", 0))
gb = sc.find("Back")
check("the Go menu shows each key beside its item",
      gb is not None and "alt-b" in sc.row(gb[0]) and
      sc.find("Reload") is not None and "f5" in sc.row(sc.find("Reload")[0]), sc)
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
open(os.path.join(D, "auto.html"), "w").write(
    "<html><head><title>Auto</title></head><body>"
    "<audio id=a src='%s' loop autoplay></audio><p>AUTOPAGE</p>"
    "</body></html>" % TONEURL)


def heard(path):
    """The level of what reached the player's file, in dB below full scale;
    -200 for no file or silence. The samples after the header, whatever it
    says: a desktop the suite ends never closes its player, so the header
    still holds the length it had when the file was opened."""
    try:
        raw = open(path, "rb").read()[44:]
    except OSError:
        return -200
    if len(raw) < 2:
        return -200
    xs = struct.unpack("<%dh" % (len(raw) // 2), raw[:len(raw) // 2 * 2])
    rms = math.sqrt(sum(x * x for x in xs) / len(xs))
    return 20 * math.log10(rms / 32768) if rms > 0 else -200


out = os.path.join(D, "snd1.wav")
sc = brun([3.0], arg=HTTP + "/snd.html", ready="PLAYBUTTON", click=("PLAYBUTTON", 2),
          env={"DT_IMGMODE": "half", "HIBR_MEDIA_AUDIO": "wav:" + out})
check("a click on a page plays its sound, from another origin, through the media module",
      heard(out) > -30, (heard(out), sc))
out = os.path.join(D, "snd2.wav")
sc = brun([3.0], arg=HTTP + "/auto.html", until="AUTOPAGE",
          env={"DT_IMGMODE": "half", "HIBR_MEDIA_AUDIO": "wav:" + out})
check("a page that plays on its own is not heard until it is pressed, by default",
      heard(out) < -60, (heard(out), sc))
out = os.path.join(D, "snd3.wav")
sc = brun([4.0], arg=HTTP + "/auto.html", until="AUTOPAGE",
          env={"DT_IMGMODE": "half", "HIBR_MEDIA_AUDIO": "wav:" + out,
               "BW_AUTOPLAY": "1"})
check("and with Pages May Play Sound On Their Own, it is heard as it loads",
      heard(out) > -30, (heard(out), sc))
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

SRV2.shutdown()
SRV.shutdown()
shutil.rmtree(D, True)
report(52)
