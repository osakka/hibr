#!/usr/bin/env python3
"""The `contacts` command (programs/contacts.hibr), against tests/davserve.py --pim.

`contacts find` and `contacts put` with no window and no desktop: the
server chosen through a real desktop.json, the card a script puts read
back by the Contacts app's own reader in a window, and what reached the
server read from the server's own folder. Every run has its own config,
data and temporary folders.
Run it directly:  python3 tests/contactscmd.py [path-to-hibr]
"""
import json, os, socket, subprocess, sys, tempfile, time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import screen as sx
from screen import Term, check, report, load, tree

if len(sys.argv) > 1:
    sx.HIBR = os.path.abspath(sys.argv[1])
SLOW = 4 if os.environ.get("ASAN_OPTIONS") else 1
CMD = tree("programs/contacts.hibr")
D = tempfile.mkdtemp(prefix="hibr-contactscmd-")
ROOT = os.path.join(D, "dav")
BOOK = os.path.join(ROOT, "addressbooks", "u", "people")
os.makedirs(os.path.join(ROOT, "principals", "u"))
os.makedirs(BOOK)
os.makedirs(os.path.join(ROOT, "calendars", "u"))
json.dump({"name": "People"}, open(os.path.join(BOOK, ".props"), "w"))
# A card as a phone or iCloud writes one: a grouped email with its label,
# an address, a birthday and a picture folded over two lines.
ZOE = ("BEGIN:VCARD\r\nVERSION:3.0\r\nUID:zoe\r\nFN:Zoë Ünal\r\nN:Ünal;Zoë;;;\r\n"
       "item1.EMAIL;TYPE=work:zoe@example.org\r\nitem1.X-ABLabel:office\r\n"
       "TEL;TYPE=cell:+44 7700 900123\r\nADR;TYPE=home:;;1 High St;Leeds;;LS1 1AA;UK\r\n"
       "BDAY:1990-04-01\r\nPHOTO;ENCODING=b;TYPE=JPEG:AAAA\r\n BBBB\r\n"
       "NOTE:first line\\nsecond line\r\nEND:VCARD\r\n")
open(os.path.join(BOOK, "zoe.vcf"), "w", newline="").write(ZOE)
open(os.path.join(BOOK, "nouid.vcf"), "w", newline="").write(
    "BEGIN:VCARD\r\nVERSION:3.0\r\nFN:Nora Nouid\r\nTEL:+33 1 23 45 67 89\r\nEND:VCARD\r\n")
LONG = "x" * 200
open(os.path.join(BOOK, "lena.vcf"), "w", newline="").write(
    "BEGIN:VCARD\r\nVERSION:3.0\r\nUID:lena\r\nFN:Lena Long\r\nEMAIL:lena@example.org\r\n"
    "NOTE:%s\\nlongwire lw1lena\r\nEND:VCARD\r\n" % LONG)
CALS = os.path.join(D, "cals")
os.makedirs(os.path.join(CALS, "principals", "u"))
os.makedirs(os.path.join(CALS, "calendars", "u", "work"))
os.makedirs(os.path.join(CALS, "addressbooks", "u"))
json.dump({"name": "Work", "comps": "VEVENT"}, open(os.path.join(CALS, "calendars", "u", "work", ".props"), "w"))

HERE = os.path.dirname(os.path.abspath(__file__))
srv = subprocess.Popen([sys.executable, os.path.join(HERE, "davserve.py"), "--root", ROOT,
                        "--pim", "--auth", "basic"], stdout=subprocess.PIPE, text=True)
PORT = int(srv.stdout.readline().split()[1])
srv2 = subprocess.Popen([sys.executable, os.path.join(HERE, "davserve.py"), "--root", CALS,
                         "--pim", "--auth", "basic"], stdout=subprocess.PIPE, text=True)
PORT2 = int(srv2.stdout.readline().split()[1])

CFG = os.path.join(D, "cfg")
DATA = os.path.join(D, "data")
TMP = os.path.join(D, "tmp")
for d in (CFG, DATA, TMP):
    os.makedirs(d)
ENV = dict(os.environ, XDG_CONFIG_HOME=CFG, XDG_DATA_HOME=DATA, TMPDIR=TMP,
           HIBR_MODPATH=tree("build/mods"))
for v in ("HIBR_HOLD", "DT_CONF", "DT_PIMSERVERS", "HIBR_CONNTIMEO"):
    ENV.pop(v, None)


def run(*args, script=None):
    """Run the command, or a script, as a program: status, stdout, stderr."""
    cmd = [sx.HIBR, "-c", script] if script is not None else [sx.HIBR, CMD] + list(args)
    p = subprocess.run(cmd, env=ENV, capture_output=True, text=True, timeout=120 * SLOW)
    return p.returncode, p.stdout, p.stderr


def server(port):
    run(script="need dav; dav server set home http://127.0.0.1:%d -u u -p p" % port)


def choose(names):
    os.makedirs(os.path.join(CFG, "hibr"), exist_ok=True)
    json.dump({"settings": {"DT_PIMSERVERS": names}},
              open(os.path.join(CFG, "hibr", "desktop.json"), "w"))


def card(name):
    with open(os.path.join(BOOK, name), newline="") as f:
        return f.read()


def cards():
    return sorted(n for n in os.listdir(BOOK) if n.endswith(".vcf"))


def lines(out):
    return [l.split("\t") for l in out.splitlines()]


def files(d):
    return [os.path.join(r, f) for r, _, fs in os.walk(d) for f in fs]


try:
    # No address book chosen: 2, one line saying so, and nothing kept.
    server(PORT)
    st, out, err = run("find", "zo")
    check("find with no address book chosen is status 2 with one plain line",
          st == 2 and out == "" and len(err.splitlines()) == 1 and "no address book" in err, (st, out, err))
    st, out, err = run("put", "--name", "Nobody", "--phone", "1")
    check("so is put, and nothing is kept anywhere", st == 2 and out == "" and files(DATA) == [],
          (st, out, err, files(DATA)))
    st, out, err = run("sync")
    check("and sync", st == 2 and out == "" and "no address book" in err and files(DATA) == [], (st, out, err))
    choose("")
    st, out, err = run("find", "zo")
    check("a settings file choosing nothing is the same", st == 2 and files(DATA) == [], (st, err))

    # The server chosen as the Control Panel chooses it, in desktop.json.
    choose("home")
    st, out, err = run("find", "zo")
    check("find never syncs: with no copy kept yet it is status 1, saying so",
          st == 1 and out == "" and "never been synced" in err and files(DATA) == [], (st, out, err))
    st, out, err = run("sync")
    check("sync makes the copy, status 0", st == 0 and out == "" and err == "", (st, out, err))
    st, out, err = run("find", "zo")
    check("find answers a line a card",
          st == 0 and out == "zoe\tZoë Ünal\tphone:+44 7700 900123\tnote:first line\tnote:second line\n"
          and err == "", (st, out, err))
    st, out, err = run("find", "ZOË")
    check("in any case", st == 0 and out.startswith("zoe\t"), (st, out, err))
    st, out, err = run("find", "nobody-at-all")
    check("no match is status 0 and nothing printed", st == 0 and out == "" and err == "", (st, out, err))
    st, out, err = run("find", "lw1lena")
    check("a note line past where the store cuts a note is still found",
          st == 0 and out.startswith("lena\tLena Long\tnote:%s\tnote:longwire lw1lena" % LONG), (st, out[:80], err))

    # A new card.
    st, out, err = run("put", "--name", "Cy New", "--phone", "+1 555 0100", "--note", "longwire lw1cy")
    uid = out.strip()
    check("put makes a card and answers its uid", st == 0 and uid.startswith("hibr-") and err == "",
          (st, out, err))
    body = card(uid + ".vcf") if uid + ".vcf" in cards() else ""
    check("the server has it, name, phone and note",
          "FN:Cy New" in body and "TEL:+1 555 0100" in body and "NOTE:longwire lw1cy" in body, (cards(), body))
    st, out, err = run("find", "cy new")
    check("found by name", st == 0 and out == "%s\tCy New\tphone:+1 555 0100\tnote:longwire lw1cy\n" % uid,
          (st, out, err))
    st, out, err = run("find", "555 0100")
    check("by a phone", st == 0 and out.startswith(uid + "\t"), (st, out, err))
    st, out, err = run("find", "5550100")
    check("by a phone's digits alone", st == 0 and out.startswith(uid + "\t"), (st, out, err))
    st, out, err = run("find", "lw1cy")
    check("and by a key kept in a note line", st == 0 and out.startswith(uid + "\t") and
          len(out.splitlines()) == 1, (st, out, err))

    # Changing a card someone else wrote: what was asked is added, the rest
    # kept as it was.
    st, out, err = run("put", "-u", "zoe", "--phone", "+447700900123", "--phone", "020 7946 0000",
                       "--note", "second line", "--note", "longwire lw1zoe")
    z = card("zoe.vcf")
    check("put -u answers the uid it changed", st == 0 and out == "zoe\n", (st, out, err))
    check("a phone already there by its digits is not added again, a new one is",
          z.count("TEL") == 2 and "TEL:020 7946 0000" in z and "TEL;TYPE=cell:+44 7700 900123" in z, z)
    check("a note line already there is not added again, a new one is",
          "NOTE:first line\\nsecond line\\nlongwire lw1zoe\r\n" in z, z)
    check("the name is kept when none is given, its N with it", "FN:Zoë Ünal\r\n" in z and
          "N:Ünal;Zoë;;;\r\n" in z, z)
    check("the email keeps its group, type and label; the address, birthday and picture are kept",
          "item1.EMAIL;TYPE=work:zoe@example.org\r\nitem1.X-ABLabel:office\r\n" in z and
          "ADR;TYPE=home:;;1 High St;Leeds;;LS1 1AA;UK\r\n" in z and "BDAY:1990-04-01\r\n" in z and
          "PHOTO;ENCODING=b;TYPE=JPEG:AAAA\r\n BBBB\r\n" in z, z)
    st, out, err = run("find", "lw1zoe")
    check("find reads the change back", st == 0 and out ==
          "zoe\tZoë Ünal\tphone:+44 7700 900123\tphone:020 7946 0000\tnote:first line\tnote:second line"
          "\tnote:longwire lw1zoe\n", (st, out, err))
    st, out, err = run("put", "-u", "zoe", "--name", "Zoë Ünal-Berg")
    z = card("zoe.vcf")
    check("--name replaces the name, and the old N goes with it",
          st == 0 and "FN:Zoë Ünal-Berg\r\n" in z and "Ünal;Zoë" not in z and "ADR;" in z, z)
    before = card("zoe.vcf")
    st, out, err = run("put", "-u", "zoe", "--phone", "0207 946 0000", "--note", "longwire lw1zoe")
    check("asking for nothing new answers the uid and sends nothing", st == 0 and out == "zoe\n" and
          card("zoe.vcf") == before, (st, out, err))
    st, out, err = run("find", "nora")
    check("a card with no uid is listed by its name on the server", st == 0 and
          out == "nouid.vcf\tNora Nouid\tphone:+33 1 23 45 67 89\n", (st, out, err))
    st, out, err = run("put", "-u", "nouid.vcf", "--phone", "+33123456789")
    check("which put -u takes", st == 0 and out == "nouid.vcf\n", (st, out, err))

    # Someone else changed the card since it was read here: the server
    # refuses the write, and put writes again over the copy it brings back.
    changed = card("zoe.vcf").replace("END:VCARD", "TEL:+1 999 0000\r\nEND:VCARD")
    other = os.path.join(D, "other.vcf")
    open(other, "w", newline="").write(changed)
    run(script="need dav; dav put %s http://127.0.0.1:%d/addressbooks/u/people/zoe.vcf > /dev/null" % (other, PORT))
    st, out, err = run("put", "-u", "zoe", "--note", "after the other change")
    z = card("zoe.vcf")
    check("a change refused because the server's copy moved on is made again over that copy",
          st == 0 and out == "zoe\n" and "TEL:+1 999 0000" in z and "after the other change" in z, (st, out, err, z))

    st, out, err = run("put", "-u", "nobody", "--note", "x")
    check("an unknown uid is status 1 with the reason", st == 1 and out == "" and "no card nobody" in err,
          (st, out, err))

    # Refusals.
    st, out, err = run("put", "--phone", "1")
    check("a new card without a name is refused", st == 1 and "needs --name" in err, (st, err))
    st, out, err = run("put", "--name", "A", "--note", "two\nlines")
    check("a note line with a newline in it is refused", st == 1 and "one line" in err, (st, err))
    st, out, err = run("find")
    check("find without TEXT is a usage error, status 1", st == 1 and "usage" in err, (st, err))
    st, out, err = run("lookup", "x")
    check("an unknown verb is status 1", st == 1 and "usage" in err, (st, err))

    # A card with no phone: no field is ever empty, so a script reading
    # with IFS set to a tab gets the notes where it expects them.
    st, out, err = run("put", "--name", "Tab\tName", "--note", "back\\slash")
    tuid = out.strip()
    check("a name with a tab in it is a card like any other", st == 0 and tuid.startswith("hibr-"), (st, out, err))
    st, out, err = run("put", "--name", " Sp  Aced ", "--note", " lead", "--note", "trail ")
    spaced = [n for n in cards() if "Sp  Aced" in card(n)]
    check("spaces kept as given make one card, answered once", st == 0 and out.strip().startswith("hibr-")
          and len(spaced) == 1, (st, out, err, spaced))
    st, out, err = run(script="IFS=$'\\t' read -ra f < <(\"$HIBR\" %s find back); "
                       "echo \"${#f[@]}|${f[1]}|${f[2]}\"" % CMD)
    check("with no phone the note is the third field, a tab and a backslash escaped",
          out == "3|Tab\\tName|note:back\\\\slash\n", (st, out, err))

    # Offline: kept and queued, found and changed before the server has it,
    # and one card once it does.
    server(9)
    st, out, err = run("put", "--name", "Dee Offline", "--phone", "123")
    duid = out.strip()
    check("with the server out of reach, put still answers the uid, saying it is queued",
          st == 0 and duid.startswith("hibr-") and "queued" in err, (st, out, err))
    st, out, err = run("find", "dee")
    check("find shows the card the queue holds", st == 0 and out == "%s\tDee Offline\tphone:123\n" % duid,
          (st, out, err))
    st, out, err = run("put", "-u", duid, "--note", "longwire lw1dee")
    check("put -u changes that card, still queued", st == 0 and out.strip() == duid, (st, out, err))
    server(PORT)
    st, out, err = run("put", "-u", duid, "--phone", "456")
    dee = [n for n in cards() if "Dee Offline" in card(n)]
    body = card(dee[0]) if dee else ""
    check("once the server is back it has one card with all three changes",
          len(dee) == 1 and "TEL:123" in body and "TEL:456" in body and "NOTE:longwire lw1dee" in body,
          (dee, body, err))

    server(9)
    run("put", "-u", "zoe", "--note", "offline one")
    st, out, err = run("put", "-u", "zoe", "--note", "offline two")
    server(PORT)
    run("put", "-u", "zoe", "--phone", "777")
    z = card("zoe.vcf").replace("\r\n ", "")
    check("two offline changes to a card the server has both reach it",
          st == 0 and "offline one\\noffline two" in z and "TEL:777" in z, (st, err, z))

    # A server that takes no connection: a listener whose queue is full
    # and which never accepts, so a connect neither fails nor completes.
    hole = socket.socket()
    hole.bind(("127.0.0.1", 0))
    hole.listen(0)
    fill = []
    for _ in range(4):
        c = socket.socket()
        c.setblocking(False)
        try:
            c.connect(hole.getsockname())
        except BlockingIOError:
            pass
        fill.append(c)
    server(hole.getsockname()[1])
    STORE = os.path.join(DATA, "hibr", "pim", "home")
    open(os.path.join(STORE, "synced"), "w").write("%d\n" % (time.time() - 3 * 3600))
    t0 = time.time()
    st, out, err = run("find", "lw1cy")
    took = time.time() - t0
    check("find with a stale copy and a dead server answers from the copy within 2 seconds",
          st == 0 and out.startswith(uid) and took < 2 * SLOW, (st, out, err, took))
    check("saying how old the copy is, once", err == "contacts: the copy of home is 3 hours old: contacts sync, "
          "or a desktop running, brings it up to date\n", err)
    t0 = time.time()
    st, out, err = run("put", "--name", "Hole Case", "--note", "lw1hole")
    took = time.time() - t0
    huid = out.strip()
    check("put against a dead server gives up within seconds, keeps the card queued, and answers its uid",
          st == 0 and huid.startswith("hibr-") and "queued" in err and "cannot reach" in err and took < 6 * SLOW,
          (st, out, err, took))
    t0 = time.time()
    st, out, err = run("sync")
    took = time.time() - t0
    check("sync against it is status 1 with the reason, within seconds",
          st == 1 and "cannot reach" in err and took < 6 * SLOW, (st, err, took))
    for c in fill:
        c.close()
    hole.close()
    server(PORT)
    st, out, err = run("sync")
    check("once the server is back, sync sends what was queued",
          st == 0 and any("lw1hole" in card(n) for n in cards()), (st, err, cards()))

    # A server that keeps only calendars is no address book.
    run(script="need dav; dav server set cals http://127.0.0.1:%d -u u -p p" % PORT2)
    choose("cals")
    st, out, err = run("put", "--name", "Nowhere")
    check("a chosen server that keeps no address book is status 2", st == 2 and
          "keeps an address book" in err, (st, out, err))
    choose("home")

    # The Contacts app reads what the command wrote.
    sess = os.path.join(D, "s.hibr")
    open(sess, "w").write(
        "%s. %s\n. %s\nneed dav\ndt_open\ndt_new Contacts 24 92 1 2 contacts\n"
        "dt_run\ndt_close\n" % (load("console", "db", "pim", "dav"), tree("programs/desktop/desktop.hibr"),
                                 tree("programs/desktop/apps/contacts.hibr")))
    t = Term(sess, rows=30, cols=104, settle=1.5,
             env={"XDG_DATA_HOME": DATA, "XDG_CONFIG_HOME": CFG})
    sc = t.until("Cy New", int(100 * SLOW))
    check("the Contacts app, choosing its server from the same settings, lists the cards put from a script", sc.find("Cy New") is not None and
          sc.find("Dee Offline") is not None, sc)
    t.quit(b"qy", 2)
finally:
    srv.terminate()
    srv2.terminate()
    srv.wait()
    srv2.wait()

report(48)
