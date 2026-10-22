#!/usr/bin/env python3
"""The PDF reader (Gitea #348): the pdf module's verbs, then the reader
through a pty -- as pixels, as half blocks and as text -- with paging, a
link, going to a page, search, the contents and Copy.

The documents are written here, by tests/pdfmake.py, to the letter of the
format: two US Letter pages, known text at known places, a link to page 2
and one to an address, a two-entry outline. Poppler reads them through the
module, loaded at run time; a box without libpoppler-glib skips every
check, the way tests/sound.py does without PipeWire.
"""
import base64, ctypes.util, os, re, shutil, subprocess, sys, tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import screen as sx
import pdfmake
from screen import Term, check, report, tree, load, press, release

if len(sys.argv) > 1:
    sx.HIBR = os.path.abspath(sys.argv[1])

if not ctypes.util.find_library("poppler-glib"):
    print("no libpoppler-glib here: every check skipped")
    report(0)

D = tempfile.mkdtemp(prefix="hibr-pdf-")
PDF = pdfmake.write(os.path.join(D, "t.pdf"))
MOD = tree("build/mods/pdf.so")


def hb(script):
    """The module through a shell of its own, answering stdout and stderr."""
    r = subprocess.run([sx.HIBR, "-c", "mod load %s\n%s" % (MOD, script)],
                       capture_output=True, text=True, timeout=60,
                       env=dict(os.environ, HIBR_RC="/dev/null"))
    return r.stdout, r.stderr


try:
    out, err = hb('d := pdf open "%s"; pdf pages $d; pdf title $d; pdf size $d 1' % PDF)
    check("pdf open reads the pages, the title and a page's size in points",
          out.split("\n")[:3] == ["2", "hibr test document", "612.0 792.0"], (out, err))
    out, _ = hb('d := pdf open "%s"; pdf outline $d' % PDF)
    check("pdf outline gives each entry's depth, page and title",
          out.strip() == "0\t1\tIntroduction\n0\t2\tSecond part", out)
    # "Go to page two" sits on a baseline at 650 from the bottom, 14 points
    # high: from the top that is 142, inside the box the search answers.
    out, _ = hb('d := pdf open "%s"; pdf find $d "Go to page"' % PDF)
    f = out.split()
    check("pdf find answers the page and the box, in points from the page's top left",
          len(f) == 5 and f[0] == "1" and float(f[2]) < 142 < float(f[4])
          and abs(float(f[1]) - 72) < 1, out)
    # The links' rectangles, from the bottom: 645-665 and 595-615; so from
    # the top, 127-147 and 177-197.
    out, _ = hb('d := pdf open "%s"; pdf link $d 1 100 140; pdf link $d 1 100 190;'
                ' pdf link $d 1 400 400 || echo none' % PDF)
    check("pdf link finds a link to a page, one to an address, and none where there is none",
          out.split("\n")[:3] == ["page 2 0.0", "uri https://example.com/", "none"], out)
    out, _ = hb('d := pdf open "%s"; y := pdf where $d 1.5 2 10; pdf at $d 1.5 918 50 $y' % PDF)
    f = out.split()
    check("pdf where and pdf at turn a page's point into the document's pixels and back",
          len(f) == 3 and f[0] == "2" and abs(float(f[1]) - 50 / 1.5) < 1
          and abs(float(f[2]) - 10) < 1, out)
    out, _ = hb('d := pdf open "%s"; pdf select $d 1 60 80 400 100' % PDF)
    check("pdf select gives the text inside a rectangle of a page",
          out.strip() == "Hello hibr PDF", out)
    out, _ = hb('d := pdf open "%s"; pdf zoom $d fitw 612 400; pdf zoom $d 100 612 400' % PDF)
    check("pdf zoom works out fit width and a percentage, 100 being 96 dots an inch",
          out.split("\n")[:2] == ["1.0000 75", "1.3333 100"], out)
    # A view of 320 by 420 at half size: two pages of 396 and the gap of 12
    # make the document 804 tall. The page is 306 wide, so it sits 7
    # pixels in, on the given background; a mark at 72-200 by 130-150
    # points lands at 43-107 by 65-75.
    out, _ = hb('d := pdf open "%s"; r := pdf view $d "%s" 320 420 0.5 0 0 -b 102030'
                ' -k 1 72 130 200 150; read -r f p t w <<< "$r"; cp "$f" "%s/v.ppm";'
                ' echo "$p $t $w"' % (PDF, D, D))
    img = open(os.path.join(D, "v.ppm"), "rb").read() if os.path.exists(os.path.join(D, "v.ppm")) else b""
    hdr = img.split(b"\n", 3)
    ok = len(hdr) == 4 and hdr[0] == b"P6" and hdr[1] == b"320 420"
    px = hdr[3] if ok else b""

    def at(x, y):
        i = (y * 320 + x) * 3
        return tuple(px[i:i + 3])
    check("pdf view writes the viewport as a picture of the size asked, saying the page and "
          "the document's height and width",
          ok and out.strip() == "1 804 320", (hdr[:2], out))
    check("with the page white on the given background, and a search mark where its points fall",
          ok and at(2, 200) == (0x10, 0x20, 0x30) and at(150, 300) == (255, 255, 255)
          and at(60, 70)[2] < 200 and at(30, 70) == (255, 255, 255), (at(2, 200), at(60, 70)))
    open(os.path.join(D, "not.pdf"), "w").write("plain text\n")
    out, err = hb('pdf open "%s/not.pdf"; echo "rc=$?"; pdf open "%s/none.pdf"; echo "rc=$?"' % (D, D))
    check("a file that is not a PDF, and one that is not there, are refused with a reason",
          out.strip() == "rc=1\nrc=1" and "not.pdf" in err and "no such file" in err, (out, err))

    def reader(env=None, cell=(0, 0), feed=(), wait=1.0):
        p = os.path.join(D, "s.hibr")
        open(p, "w").write("%s. %s\n. %s\ndt_open\ndt_new 'PDF Reader' 22 70 1 2 pdfr %s\n"
                           "dt_run\ndt_close\n"
                           % (load("console"), tree("programs/desktop/desktop.hibr"),
                              tree("programs/desktop/apps/Office/pdfr.hibr"), PDF))
        e = {"DT_IMGMODE": "half"}
        e.update(env or {})
        t = Term(p, rows=26, cols=80, settle=2.0, cellw=cell[0], cellh=cell[1], env=e)
        t.until(lambda sc: "Page 1 of 2" in sc.text(), 60)
        for f in feed:
            t.keys([f] if isinstance(f, bytes) else list(f), settle=0.5)
        t.collect(wait)
        sc = t.screen()
        t.quit(None, 0.5)
        return sc, t

    def status(sc):
        return sc.row(21)[3:70]

    sc, t = reader({"HIBR_GFX": "kitty", "DT_IMGMODE": "pixels"}, cell=(8, 16))
    check("where the terminal takes pixels the page is a picture, titled by the document",
          len(sc.images) == 1 and sc.find("PDF Reader [hibr test document]") is not None
          and "Page 1 of 2" in status(sc), (sc.images, sc))
    sc, t = reader()
    blocks = sum(sc.row(r)[3:70].count("▀") + sc.row(r)[3:70].count("▄")
                 for r in range(2, 21))
    check("where it does not, the page is half blocks, and no picture is sent",
          not sc.images and blocks > 200 and "Page 1 of 2" in status(sc), (blocks, sc))
    sc, t = reader(feed=[b"t"])
    check("t shows the page's own text, wrapped to the window",
          sc.find("Hello hibr PDF") is not None and sc.find("Go to page two") is not None, sc)
    # The link's box, from the top, is 70-220 by 127-147 points; at fit width
    # the viewport's 67 columns of 8 pixels make a zoom of 536/612.
    z = 67 * 8 / 612.0
    lr, lc = 2 + int(137 * z / 16), 3 + int(100 * z / 8)
    sc, t = reader(feed=[(press(lr, lc), release(lr, lc))])
    check("a click on a link to a page goes to that page", "Page 2 of 2" in status(sc), sc)
    sc, t = reader(feed=[b"g", b"2", b"\r"])
    check("g, a number and enter go to that page", "Page 2 of 2" in status(sc), sc)
    sc, t = reader(feed=[b"\x1b[F"])
    check("end goes to the last page", "Page 2 of 2" in status(sc), sc)
    sc, t = reader(feed=[b"/", b"S", b"e", b"c", b"o", b"n", b"d", b"\r"])
    check("/ searches the document and goes to the match, saying how many",
          "Page 2 of 2" in status(sc) and "/Second 1/1" in status(sc), sc)
    sc, t = reader(feed=[b"o"])
    intro, part = sc.find("Introduction"), sc.find("Second part")
    check("o shows the contents beside the pages", intro is not None and part is not None, sc)
    if part:
        sc, t = reader(feed=[b"o", (press(part[0], part[1] + 1), release(part[0], part[1] + 1))])
        check("and an entry clicked goes to its page", "Page 2 of 2" in status(sc), sc)
    else:
        check("and an entry clicked goes to its page", False, sc)
    sc, t = reader(feed=[b"\x1bc"])
    m = re.search(rb"\x1b\]52;c;([A-Za-z0-9+/=]+)\x07", t.raw)
    txt = base64.b64decode(m.group(1)).decode() if m else ""
    check("Copy with nothing selected copies the page's text", "Hello hibr PDF" in txt, txt)
    r = subprocess.run([sx.HIBR, "-c",
                        "fn dt_app(...a) { :; }\nfn dt_opener(...a) { echo \"opener ${a[*]}\"; }\n"
                        "declare -gA PR\nfn dt_keepstate(...a) { :; }\n. %s"
                        % tree("programs/desktop/apps/Office/pdfr.hibr")],
                       capture_output=True, text=True, timeout=30)
    check("and .pdf files open with it, from Files and File Types",
          "opener pdfr PDF Reader pdf app pdfr" in r.stdout, (r.stdout, r.stderr))
    # The way a person opens one: Files hands the path to dt_openfile, which
    # finds the reader by its File Types registration and launches it.
    # XDG_RUNTIME_DIR is this suite's own, with a folder of views left by a
    # desktop that has gone, which the first view takes away; and while the
    # reader is open its own folder holds the one view it shows.
    rt = os.path.join(D, "rt")
    gone = os.path.join(rt, "hibr-pdf-%d" % os.getuid(), "999999")
    os.makedirs(gone)
    open(os.path.join(gone, "pdf-1-1.ppm"), "w").write("old")
    p = os.path.join(D, "o.hibr")
    open(p, "w").write("%s. %s\n. %s\ndt_open\ndt_openfile %s\ndt_run\ndt_close\n"
                       % (load("console"), tree("programs/desktop/desktop.hibr"),
                          tree("programs/desktop/apps/Office/pdfr.hibr"), PDF))
    t = Term(p, rows=26, cols=80, settle=2.0, env={"DT_IMGMODE": "half", "XDG_RUNTIME_DIR": rt})
    sc = t.until(lambda sc: "Page 1 of 2" in sc.text(), 60)
    mine = os.path.join(rt, "hibr-pdf-%d" % os.getuid(), str(t.pid))
    views = sorted(os.listdir(mine)) if os.path.isdir(mine) else []
    t.quit(None, 0.5)
    check("a .pdf opened the way Files opens one comes up in the reader",
          sc.find("PDF Reader [hibr test document]") is not None, sc)
    check("its views live in a folder of this desktop's own, one at a time, and the "
          "folder of a desktop that has gone is taken away",
          len(views) == 1 and views[0].startswith("pdf-") and not os.path.exists(gone),
          (views, os.path.exists(gone)))
finally:
    shutil.rmtree(D, True)
report(24)
