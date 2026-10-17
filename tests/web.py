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


SRV = http.server.ThreadingHTTPServer(
    ("127.0.0.1", 0), functools.partial(Quiet, directory=D))
threading.Thread(target=SRV.serve_forever, daemon=True).start()
HTTPB = "http://127.0.0.1:%d/b.html" % SRV.server_address[1]
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
           b"\x1bb", 1.5], until="Hello hibr")
check("alt-b goes back", sc.find("Hello hibr") is not None and
      sc.find("This is page B.") is None, sc)
sc = brun([1.0] + [c.encode() for c in B] + [b"\r", 2.0], ready="Test page",
          click=(" + ", 1), until="This is page B.")
check("+ opens a tab with the keyboard in the address; enter goes there",
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
sc = brun([1.0], ready="Test page", click=("\u2715", 0),
          gone="\u2524 Browser")
check("closing the last tab closes the window",
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

SRV.shutdown()
shutil.rmtree(D, True)
report(31)
