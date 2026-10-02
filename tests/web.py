#!/usr/bin/env python3
"""The web module and the Browser app, against local pages: a headless
Chromium driven over its pipe, its pages drawn as cells.

Needs Chromium or Chrome; without one every check is skipped, and the
suite says so rather than failing. Pages are files in a folder of their
own, so nothing here touches the network.
"""
import os, shutil, subprocess, sys, tempfile

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
A = "file://" + os.path.join(D, "a.html")
B = "file://" + os.path.join(D, "b.html")
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


def brun(feed, arg=A, env=None, wait=1.5, until=None):
    s = tempfile.mkdtemp(prefix="hibr-web-s-")
    p = os.path.join(s, "session.hibr")
    open(p, "w").write("%s\n. %s\n. %s\ndt_open\n"
                       'dt_new Browser 22 76 1 2 browser %s\ndt_run\ndt_close\n'
                       % (load("console"), tree("examples/desktop/desktop.hibr"),
                          tree("examples/desktop/apps/Internet/browser.hibr"), arg))
    e = {"HIBR_WEB_PROFILE": PROF, "XDG_DATA_HOME": os.path.join(D, "data")}
    e.update(env or {})
    t = Term(p, env=e, settle=3.0, rows=26, cols=80)
    t.keys(list(feed) + [wait], settle=0.5)
    for _ in range(27):
        if until is None or until in t.screen().text():
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
sc = brun([press(lk[0], lk[1] + 2), release(lk[0], lk[1] + 2), 2.5,
           b"\x1b[1;3D", 1.5], until="Hello hibr")
check("alt-left goes back", sc.find("Hello hibr") is not None and
      sc.find("This is page B.") is None, sc)
plus = sc.find(" + ")
sc = brun([press(2, 18), release(2, 18), 1.0] + [c.encode() for c in B] +
          [b"\r", 2.0], until="This is page B.")
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
sc = brun([press(2, 14), release(2, 14), 1.0])
check("closing the last tab closes the window",
      sc.find("┤ Browser") is None, sc)
sc = brun([], env={"HIBR_WEB_BROWSER": "/nonexistent/chromium"})
check("without a browser to drive, the window says what is missing",
      sc.find("No Chromium or Chrome") is not None, sc)

shutil.rmtree(D, True)
report(16)
