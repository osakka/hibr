#!/usr/bin/env python3
"""Drive Contacts through a pseudo terminal, against tests/davserve.py --pim.

The CardDAV server's folder is this suite's, so a check can look at what
reached the server -- a card added, changed, removed -- as well as at the
screen. Mail is loaded beside it, for writing to a contact and for finishing
an address as it is typed.
Run it directly:  python3 tests/contacts.py [path-to-hibr]
"""
import json, os, subprocess, sys, tempfile, time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import screen as sx
from screen import Term, check, report, load, tree, press, release

if len(sys.argv) > 1:
    sx.HIBR = os.path.abspath(sys.argv[1])
SLOW = 4 if os.environ.get("ASAN_OPTIONS") else 1
D = tempfile.mkdtemp(prefix="hibr-contacts-")
ROOT = os.path.join(D, "dav")
BOOK = os.path.join(ROOT, "addressbooks", "u", "people")
os.makedirs(os.path.join(ROOT, "principals", "u"))
os.makedirs(BOOK)
os.makedirs(os.path.join(ROOT, "calendars", "u"))
json.dump({"name": "People"}, open(os.path.join(BOOK, ".props"), "w"))
PEOPLE = [("zoe", "Zoë Ünal", "zoe@example.org", "+44 7700 900123"),
          ("ana", "Ana Smith", "ana@example.net", ""),
          ("bo", "Bo Berg", "bo@example.com", "+46 70 123 45 67")]
for uid, fn, em, tel in PEOPLE:
    with open(os.path.join(BOOK, uid + ".vcf"), "w", newline="") as f:
        f.write("BEGIN:VCARD\r\nVERSION:3.0\r\nUID:%s\r\nFN:%s\r\nEMAIL;TYPE=work:%s\r\n%sEND:VCARD\r\n"
                % (uid, fn, em, "TEL;TYPE=cell:%s\r\n" % tel if tel else ""))

srv = subprocess.Popen([sys.executable, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                                     "davserve.py"), "--root", ROOT, "--pim", "--auth", "basic"],
                       stdout=subprocess.PIPE, text=True)
PORT = int(srv.stdout.readline().split()[1])
CONF = os.path.join(D, "mailconf")
subprocess.run([sx.HIBR, "-c", "need email; email account set 'Home Mail' -h 127.0.0.1 -p 1 -S plain "
                "-u pat -w x -e pat@example.com -n 'Pat Doe' -o 127.0.0.1 -q 1 -T plain"],
               env=dict(os.environ, HIBR_MAIL_CONF=CONF, HIBR_MODPATH=tree("build/mods")), check=True)


def until(cond, secs=6.0):
    end = time.time() + secs * SLOW
    while time.time() < end:
        if cond():
            return True
        time.sleep(0.1)
    return cond()


def look(t, secs=0.6):
    t.collect(secs * SLOW)
    return t.screen()


def waitfor(t, text, secs=8.0):
    end = time.time() + secs * SLOW
    while True:
        sc = look(t, 0.3)
        if sc.find(text) or time.time() > end:
            return sc


def cards():
    return sorted(n for n in os.listdir(BOOK) if n.endswith(".vcf"))


def card(name):
    with open(os.path.join(BOOK, name), newline="") as f:
        return f.read()


sess = os.path.join(D, "s.hibr")
open(sess, "w").write(
    "%s. %s\n. %s\n. %s\nneed dav\ndav server set home http://127.0.0.1:%d -u u -p p\n"
    "DT_PIMSERVERS=home\ndt_open\ndt_new Contacts 24 92 1 2 contacts\ndt_run\ndt_close\n"
    % (load("console", "email", "db", "html", "pim", "dav"), tree("examples/desktop/desktop.hibr"),
       tree("examples/desktop/desk-accessories/contacts.hibr"), tree("examples/desktop/apps/Internet/mail.hibr"), PORT))
t = Term(sess, rows=30, cols=104, settle=1.5,
         env={"HIBR_MAIL_CONF": CONF, "XDG_DATA_HOME": os.path.join(D, "data")})
try:
    sc = waitfor(t, "Zoë Ünal")
    listed = [sc.row(r)[3:13].strip() for r in range(3, 8)]
    check("the first sync lists the address book, sorted by name",
          listed[:3] == ["Ana Smith", "Bo Berg", "Zoë Ünal"], (listed, sc))
    check("the first is chosen and shown: its name, email and phone are absent when it has none",
          sc.find("ana@example.net") and sc.row(sc.find("ana@example.net")[0]).count("ana@example.net") == 1, sc)
    t.keys([b"j"])
    sc = look(t)
    check("j moves down; the person's phone is shown with their email",
          sc.find("bo@example.com") and sc.find("+46 70 123 45 67"), sc)
    t.keys([b"/", b"zo"])
    sc = look(t)
    check("/ searches: only who matches stays in the list", sc.find("Zoë Ünal") and not sc.find("Bo Berg")
          and sc.find("zoe@example.org"), sc)
    t.keys([b"\x1b"])
    sc = look(t)
    check("escape puts everyone back", sc.find("Bo Berg") and sc.find("Ana Smith"), sc)

    # A new contact, written to the server.
    t.keys([b"n"])
    sc = waitfor(t, "New Contact")
    check("n opens a new contact", sc.find("Name:") and sc.find("Emails:"), sc)
    t.keys([b"Cy New", b"\t", b"cy@example.org, cy@home.example", b"\t", b"+1 555 0100", b"\t",
            b"Acme", b"\t", b"\t", b"\r"])
    check("saving it puts a card on the server", until(lambda: len(cards()) == 4, 10), cards())
    new = [n for n in cards() if n not in ("ana.vcf", "bo.vcf", "zoe.vcf")]
    body = card(new[0]) if new else ""
    check("with every field, two emails among them", "FN:Cy New" in body and "EMAIL:cy@example.org" in body
          and "EMAIL:cy@home.example" in body and "TEL:+1 555 0100" in body and "ORG:Acme" in body, body)
    sc = waitfor(t, "Cy New")
    check("and it is in the list once the sync is back", sc.find("Cy New") is not None, sc)

    # An edit goes up conditional on the copy it was read from.
    t.keys([b"\x1b[H"])
    sc = look(t)
    t.keys([b"e"])
    sc = waitfor(t, "Edit Contact")
    check("e edits the chosen contact, its fields filled", sc.find("Ana Smith") and sc.find("ana@example.net"), sc)
    t.keys([b"\t", b"\t", b"+44 20 7946 0000", b"\t", b"\t", b"\t", b"\r"])
    check("saving sends the change to the server", until(lambda: "+44 20 7946 0000" in card("ana.vcf"), 10),
          card("ana.vcf"))

    # Removing someone, after asking.
    t.keys([b"\x1b[H", b"j"])
    look(t)
    t.keys([b"#"])
    sc = look(t)
    check("# asks before removing anyone", sc.find("Remove Bo Berg") is not None, sc)
    t.keys([b"y"])
    check("and removes them from the server", until(lambda: "bo.vcf" not in cards(), 10), cards())

    # Writing to someone, and finishing an address in Mail.
    t.keys([b"\x1b[H", 1.0, b"m"])
    sc = waitfor(t, "New Message")
    check("m writes to the chosen contact in Mail", sc.find("To:      Ana Smith <ana@example.net>"), sc)
    cc = sc.find("Cc:")
    if cc:
        t.send(press(cc[0], cc[1] + 12), 0.1)
        t.send(release(cc[0], cc[1] + 12), 0.3)
    t.keys([b"zo"])
    sc = look(t)
    check("a click on Cc puts the cursor there, and typing offers the people who match", sc.find("Zoë Ünal <zoe@example.org>") is not None, sc)
    t.keys([b"\t"])
    sc = look(t)
    check("tab takes the suggestion, a comma after it for the next",
          sc.find("Cc:      Zoë Ünal <zoe@example.org>,") is not None, sc)
    t.keys([b"\x1b", b"\x1b"])
    t.quit(b"qy", 2)

    # Mirrored (#68): the list at the right, the person chosen at the left,
    # each field's label at the right of its value.
    t = Term(sess, rows=30, cols=104, settle=1.5,
             env={"HIBR_MAIL_CONF": CONF, "XDG_DATA_HOME": os.path.join(D, "data"), "DT_LANG": "xy"})
    sc = waitfor(t, "Zo\u00eb")
    zo, em = sc.find("Zo\u00eb"), sc.find("ana@example.net")
    check("mirrored, the list is at the right and the person chosen at the left",
          zo and em and zo[1] > 60 and em[1] < 50 and
          sc.row(em[0]).find("email") > em[1], sc)
    t.quit(b"qy", 2)

    # Control Panel > PIM asks each Network Server what it keeps, and lists
    # a plain file server as files only rather than offering it.
    PLAIN = os.path.join(D, "plain")
    os.makedirs(PLAIN)
    srv2 = subprocess.Popen([sys.executable, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                                          "davserve.py"), "--root", PLAIN, "--auth", "basic"],
                            stdout=subprocess.PIPE, text=True)
    P2 = int(srv2.stdout.readline().split()[1])
    sess2 = os.path.join(D, "p.hibr")
    open(sess2, "w").write(
        "%s. %s\n. %s\n. %s/panel.hibr\nCP_PANEDIRS+=(\"%s\")\ncp_panes\nneed dav\n"
        "dav server set home http://127.0.0.1:%d -u u -p p\ndav server set files http://127.0.0.1:%d -u u -p p\n"
        "dt_open\ndt_new \"Control Panel\" 24 80 1 1 panel pimset\ndt_run\ndt_close\n"
        % (load("console", "db", "pim", "dav"), tree("examples/desktop/desktop.hibr"),
           tree("examples/desktop/desk-accessories/contacts.hibr"), tree("examples/desktop/apps"),
           tree("examples/desktop/control-panel"), PORT, P2))
    t = Term(sess2, rows=30, cols=104, settle=1.5, env={"XDG_DATA_HOME": os.path.join(D, "data2")})
    sc = waitfor(t, "files only")
    sc = waitfor(t, "(calendars, contacts)")
    check("PIM lists a server that keeps calendars and contacts with a switch, saying so",
          sc.find("home (calendars, contacts)") is not None, sc)
    check("and a plain file server as files only, with no switch",
          sc.find("files only") is not None and sc.find("files (") is None, sc)
    check("and offers to add a server", sc.find("Add Server") is not None, sc)
    t.quit(b"qy", 2)
    srv2.terminate()
    srv2.wait()
finally:
    srv.terminate()
    srv.wait()

report(21)
