"""Write the PDFs the suites read (Gitea #348) -- by hand, to the letter of
the format, so nothing outside the tree is needed to make them: two US
Letter pages (612 by 792 points), known text at known places, a link on
page 1 to page 2 and one to an address, and a two-entry outline.

Every position is in PDF's own points with the origin at the bottom left;
the pdf module answers in points from the top left, so a check converts
with 792 - y. The cross-reference table is computed from the bytes, so the
file is valid as written, not merely accepted by a forgiving reader: qpdf
11's `qpdf --check` finds no syntax or stream error in it."""


def write(path, extra_text=""):
    objs = []

    def add(body):
        objs.append(body)
        return len(objs)

    font = add(b"<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>")
    s1 = (b"BT /F1 24 Tf 72 700 Td (Hello hibr PDF) Tj ET\n"
          b"BT /F1 14 Tf 72 650 Td (Go to page two) Tj ET\n"
          b"BT /F1 14 Tf 72 600 Td (Visit example) Tj ET\n" +
          ("BT /F1 12 Tf 72 560 Td (%s) Tj ET\n" % extra_text).encode())
    s2 = b"BT /F1 24 Tf 72 700 Td (Second page here) Tj ET\n"
    c1 = add(b"<< /Length %d >>\nstream\n" % len(s1) + s1 + b"endstream")
    c2 = add(b"<< /Length %d >>\nstream\n" % len(s2) + s2 + b"endstream")
    # Placeholders filled once the page numbers are known.
    pages = add(b"")
    p1 = add(b"")
    p2 = add(b"")
    goto = add(b"<< /Type /Annot /Subtype /Link /Rect [70 645 220 665] "
               b"/Border [0 0 0] /Dest [%d 0 R /XYZ 0 792 0] >>" % p2)
    uri = add(b"<< /Type /Annot /Subtype /Link /Rect [70 595 200 615] "
              b"/Border [0 0 0] /A << /S /URI /URI (https://example.com/) >> >>")
    res = b"/Resources << /Font << /F1 %d 0 R >> >>" % font
    objs[pages - 1] = (b"<< /Type /Pages /Kids [%d 0 R %d 0 R] /Count 2 >>"
                       % (p1, p2))
    objs[p1 - 1] = (b"<< /Type /Page /Parent %d 0 R /MediaBox [0 0 612 792] "
                    b"%s /Contents %d 0 R /Annots [%d 0 R %d 0 R] >>"
                    % (pages, res, c1, goto, uri))
    objs[p2 - 1] = (b"<< /Type /Page /Parent %d 0 R /MediaBox [0 0 612 792] "
                    b"%s /Contents %d 0 R >>" % (pages, res, c2))
    out = add(b"")
    o1 = add(b"")
    o2 = add(b"")
    objs[out - 1] = b"<< /Type /Outlines /First %d 0 R /Last %d 0 R /Count 2 >>" % (o1, o2)
    objs[o1 - 1] = (b"<< /Title (Introduction) /Parent %d 0 R /Next %d 0 R "
                    b"/Dest [%d 0 R /XYZ 0 792 0] >>" % (out, o2, p1))
    objs[o2 - 1] = (b"<< /Title (Second part) /Parent %d 0 R /Prev %d 0 R "
                    b"/Dest [%d 0 R /XYZ 0 792 0] >>" % (out, o1, p2))
    info = add(b"<< /Title (hibr test document) >>")
    cat = add(b"<< /Type /Catalog /Pages %d 0 R /Outlines %d 0 R >>" % (pages, out))
    data = bytearray(b"%PDF-1.4\n%\xe2\xe3\xcf\xd3\n")
    offs = []
    for i, body in enumerate(objs, 1):
        offs.append(len(data))
        data += b"%d 0 obj\n" % i + body + b"\nendobj\n"
    xref = len(data)
    data += b"xref\n0 %d\n0000000000 65535 f \n" % (len(objs) + 1)
    for o in offs:
        data += b"%010d 00000 n \n" % o
    data += (b"trailer\n<< /Size %d /Root %d 0 R /Info %d 0 R >>\nstartxref\n%d\n%%%%EOF\n"
             % (len(objs) + 1, cat, info, xref))
    open(path, "wb").write(bytes(data))
    return path


if __name__ == "__main__":
    import sys
    write(sys.argv[1] if len(sys.argv) > 1 else "test.pdf")
