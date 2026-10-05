#!/usr/bin/env python3
"""Drive Calendar through a pseudo terminal, against tests/davserve.py --pim.

The CalDAV server's folder is this suite's, so a check can look at what
reached the server -- an event added, changed, removed, answered -- as
well as at the screen. A mail server (tests/mailserve.py) in this process
takes the invitations and answers sent, and hands Mail an invitation to
answer. The zone is Europe/London throughout, so the times written are
known.
Run it directly:  python3 tests/calapp.py [path-to-hibr]
"""
import base64, datetime, json, os, subprocess, sys, tempfile, time, urllib.request
from zoneinfo import ZoneInfo

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import screen as sx
from screen import Term, check, report, load, tree, press, release
import mailserve as ms

if len(sys.argv) > 1:
    sx.HIBR = os.path.abspath(sys.argv[1])
SLOW = 4 if os.environ.get("ASAN_OPTIONS") else 1
TZ = "Europe/London"
Z = ZoneInfo(TZ)
D = tempfile.mkdtemp(prefix="hibr-calendar-")
if os.environ.get("V"):
    print("in", D)
ROOT = os.path.join(D, "dav")
CAL = os.path.join(ROOT, "calendars", "u", "work")
os.makedirs(os.path.join(ROOT, "principals", "u"))
os.makedirs(os.path.join(ROOT, "addressbooks", "u"))
os.makedirs(CAL)
json.dump({"name": "Work", "color": "#2e8b57"}, open(os.path.join(CAL, ".props"), "w"))

now = datetime.datetime.now(Z)
today = now.date()
tomorrow = today + datetime.timedelta(days=1)


def stamp(d, h, m):
    return "%04d%02d%02dT%02d%02d00" % (d.year, d.month, d.day, h, m)


def event(name, uid, body):
    with open(os.path.join(CAL, name), "w", newline="") as f:
        f.write("BEGIN:VCALENDAR\r\nVERSION:2.0\r\nPRODID:-//test//EN\r\nBEGIN:VEVENT\r\nUID:%s\r\n"
                "DTSTAMP:20261001T000000Z\r\n%sEND:VEVENT\r\nEND:VCALENDAR\r\n"
                % (uid, "".join(l + "\r\n" for l in body)))


event("standup.ics", "standup", ["SUMMARY:Standup", "DTSTART;TZID=%s:%s" % (TZ, stamp(today, 10, 0)),
                                 "DTEND;TZID=%s:%s" % (TZ, stamp(today, 10, 15)), "RRULE:FREQ=WEEKLY;COUNT=6"])
event("review.ics", "review", ["SUMMARY:Design review", "LOCATION:Room 4",
                               "DTSTART;TZID=%s:%s" % (TZ, stamp(today, 14, 0)),
                               "DTEND;TZID=%s:%s" % (TZ, stamp(today, 15, 0))])
# Due a day and a few seconds from now, with a reminder a day before: it
# goes off a few seconds into the run, which the ticker has to ask for.
soon = (now + datetime.timedelta(days=1, seconds=12 * SLOW)).astimezone(datetime.timezone.utc)
event("coffee.ics", "coffee", ["SUMMARY:Coffee break", "DTSTART:%sZ" % soon.strftime("%Y%m%dT%H%M%S"),
                               "DTEND:%sZ" % (soon + datetime.timedelta(minutes=15)).strftime("%Y%m%dT%H%M%S"),
                               "BEGIN:VALARM", "ACTION:DISPLAY", "DESCRIPTION:Coffee", "TRIGGER:-P1D",
                               "END:VALARM"])

srv = subprocess.Popen([sys.executable, os.path.join(os.path.dirname(os.path.abspath(__file__)), "davserve.py"),
                        "--root", ROOT, "--pim", "--auth", "basic"], stdout=subprocess.PIPE, text=True)
PORT = int(srv.stdout.readline().split()[1])

# An invitation from Ana, for tomorrow at eleven, to be answered in Mail.
INVITE = ("BEGIN:VCALENDAR\r\nVERSION:2.0\r\nPRODID:-//test//EN\r\nMETHOD:REQUEST\r\nBEGIN:VEVENT\r\n"
          "UID:planning-1@example.net\r\nDTSTAMP:20261001T000000Z\r\nSUMMARY:Planning\r\n"
          "DTSTART;TZID=%s:%s\r\nDTEND;TZID=%s:%s\r\nLOCATION:Room 9\r\n"
          "ORGANIZER;CN=Ana Smith:mailto:ana@example.net\r\n"
          "ATTENDEE;CN=Pat Doe;PARTSTAT=NEEDS-ACTION;RSVP=TRUE:mailto:pat@example.com\r\n"
          "ATTENDEE;CN=Bo Berg;PARTSTAT=NEEDS-ACTION:mailto:bo@example.com\r\n"
          "END:VEVENT\r\nEND:VCALENDAR\r\n" % (TZ, stamp(tomorrow, 11, 0), TZ, stamp(tomorrow, 12, 0)))
box = ms.Box()
box.add("INBOX", "From: Ana Smith <ana@example.net>\r\nTo: Pat <pat@example.com>\r\nSubject: Invitation: Planning\r\n"
        "Date: Sat, 3 Oct 2026 09:00:00 +0000\r\nMessage-ID: <inv1@example.net>\r\nMIME-Version: 1.0\r\n"
        "Content-Type: multipart/alternative; boundary=\"b1\"\r\n\r\n--b1\r\n"
        "Content-Type: text/plain; charset=utf-8\r\n\r\nYou are invited to Planning.\r\n--b1\r\n"
        "Content-Type: text/calendar; charset=utf-8; method=REQUEST\r\n\r\n" + INVITE + "--b1--\r\n")
IMAP = ms.serve("imap", box)
SMTP = ms.serve("smtp", box)
CONF = os.path.join(D, "mailconf")
subprocess.run([sx.HIBR, "-c", "need email; email account set 'Home Mail' -h 127.0.0.1 -p %d -S plain "
                "-u pat -w 'right horse' -e pat@example.com -n 'Pat Doe' -o 127.0.0.1 -q %d -T plain"
                % (IMAP, SMTP)], env=dict(os.environ, HIBR_MAIL_CONF=CONF, HIBR_MODPATH=tree("build/mods")),
               check=True)


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


def items():
    return sorted(n for n in os.listdir(CAL) if n.endswith(".ics"))


def item(name):
    try:
        with open(os.path.join(CAL, name), newline="") as f:
            return f.read().replace("\r\n ", "")
    except OSError:
        return ""


def having(text):
    return [n for n in items() if text in item(n)]


def session(tag, app, title, extra=None):
    sess = os.path.join(D, tag + ".hibr")
    open(sess, "w").write(
        "%s. %s\n. %s\n. %s\n. %s\nneed dav\ndav server set home http://127.0.0.1:%d -u u -p p\n"
        "DT_PIMSERVERS=home\ndt_open\ndt_new %s 36 100 1 2 %s\ndt_run\ndt_close\n"
        % (load("console", "email", "db", "html", "pim", "dav"), tree("examples/desktop/desktop.hibr"),
           tree("examples/desktop/desk-accessories/contacts.hibr"), tree("examples/desktop/desk-accessories/calendar.hibr"),
           tree("examples/desktop/apps/Internet/mail.hibr"), PORT, title, app))
    return Term(sess, rows=40, cols=110, settle=1.5,
                env=dict({"HIBR_MAIL_CONF": CONF, "XDG_DATA_HOME": os.path.join(D, "data"), "TZ": TZ},
                         **(extra or {})))


MONTH = today.strftime("%B %Y")
t = session("cal", "calendar", "Calendar")
try:
    sc = waitfor(t, "Design review")
    check("the month is drawn, named, with the days of the week", sc.find(MONTH) and sc.find("Mon") and sc.find("Sun"),
          sc)
    check("the first sync lists today's events under the month, with their times",
          sc.find("10:00-10:15  Standup") and sc.find("14:00-15:00  Design review -- Room 4"), sc)
    n = sum(sc.row(r).count("Standup") for r in range(40))
    check("a weekly event is in the month once a week", n >= 3, (n, sc))

    sc = waitfor(t, "Coffee break at", 25)
    check("a reminder goes off on its own time, said with the event's start", sc.find("Coffee break at"), sc)

    # An edit goes up conditional on the copy it was read from.
    t.keys([b"\t", b"j", b"\r"])
    sc = waitfor(t, "Edit Event")
    check("tab, j and enter edit the chosen event, its fields filled",
          sc.find("Design review") and sc.find("Room 4") and sc.find("14:00"), sc)
    t.keys([b" v2"] + [b"\t"] * 11 + [b"\r"])
    check("saving sends the change to the server, in the event's own zone",
          until(lambda: "SUMMARY:Design review v2" in item("review.ics"), 10), item("review.ics"))
    body = item("review.ics")
    check("its place and time kept, and no METHOD in a stored copy",
          "LOCATION:Room 4" in body and "DTSTART;TZID=Europe/London:%s" % stamp(today, 14, 0) in body
          and "METHOD" not in body, body)

    # A new event with someone invited: kept, and the invitation mailed.
    t.keys([b"\x1b", b"c"])
    sc = waitfor(t, "New Event")
    check("c makes a new event on the chosen day, at nine",
          sc.find(today.strftime("%Y-%m-%d")) and sc.find("09:00") and sc.find("10:00"), sc)
    t.keys([b"Dentist"] + [b"\t"] * 10 + [b"bo@example.com", b"\t", b"\r"])
    check("saving puts it on the server", until(lambda: having("SUMMARY:Dentist"), 10), items())
    def dentist():
        return item((having("SUMMARY:Dentist") or [""])[0])
    want = ("DTSTART;TZID=Europe/London:%s" % stamp(today, 9, 0), "ORGANIZER", "ORGANIZER;CN=Pat Doe:mailto:pat@example.com",
            "mailto:bo@example.com")
    check("at nine in this machine's zone, organised by the account, Bo invited",
          until(lambda: all(w in dentist() for w in want), 4), dentist())
    check("and an invitation goes to Bo by mail, the event in it as a request",
          until(lambda: [m for m in box.sent if "bo@example.com" in " ".join(m["to"])], 10)
          and "METHOD:REQUEST" in [m for m in box.sent if "bo@example.com" in " ".join(m["to"])][0]["raw"],
          box.sent)
    sc = waitfor(t, "09:00-10:00  Dentist")
    check("and it is in the day's list once the sync is back", sc.find("09:00-10:00  Dentist"), sc)

    # Removing it, after asking.
    t.keys([b"\t"])
    look(t)
    t.keys([b"#"])
    sc = look(t)
    check("# asks before removing anything", sc.find("Remove Dentist?"), sc)
    t.keys([b"y"])
    check("and removes it from the server", until(lambda: not having("SUMMARY:Dentist"), 10), items())

    # The agenda, and moving about.
    t.keys([b"\x1b", b"a"])
    sc = waitfor(t, "Agenda from")
    head = "%s %d %s %d" % (tomorrow.strftime("%a"), tomorrow.day, tomorrow.strftime("%B"), tomorrow.year)
    check("a lists what is coming under a heading for each day",
          sc.find("Design review v2") and sc.find(head) and sc.find("Coffee break")
          and sc.find(head)[0] < sc.find("Coffee break")[0], sc)
    t.keys([b"m", b"n"])
    nxt = (today.replace(day=1) + datetime.timedelta(days=32)).strftime("%B %Y")
    sc = look(t)
    check("m goes back to the month and n to the next", sc.find(nxt), sc)
    t.quit(b"qy", 2)
finally:
    pass

# Mirrored (#68): the week runs from the right, the title at the right.
t = session("calxy", "calendar", "Calendar", {"DT_LANG": "xy"})
try:
    sc = waitfor(t, "Standup")
    tue, title = sc.find("Tue"), sc.find(MONTH)
    wk = sc.row(tue[0]) if tue else ""
    check("mirrored, the month's week runs from the right and its title is at the right",
          tue and title and wk.find("Mon ") > wk.find("Wed ") > 0 and title[1] > 55, sc)
finally:
    t.quit(b"qy", 1.5)

# Answering an invitation in Mail -- with neither Calendar nor Contacts
# open, so the sync that brings a new event down is the desktop's own.
# Uploaded, as another client would, so the server's sync token moves.
req = urllib.request.Request("http://127.0.0.1:%d/calendars/u/work/late.ics" % PORT, method="PUT",
                             data=("BEGIN:VCALENDAR\r\nVERSION:2.0\r\nPRODID:-//test//EN\r\nBEGIN:VEVENT\r\n"
                                   "UID:late\r\nDTSTAMP:20261001T000000Z\r\nSUMMARY:Late meeting\r\n"
                                   "DTSTART;TZID=%s:%s\r\nDTEND;TZID=%s:%s\r\nEND:VEVENT\r\nEND:VCALENDAR\r\n"
                                   % (TZ, stamp(tomorrow, 18, 0), TZ, stamp(tomorrow, 19, 0))).encode(),
                             headers={"Content-Type": "text/calendar",
                                      "Authorization": "Basic " + base64.b64encode(b"u:p").decode()})
urllib.request.urlopen(req).read()
OBJDB = os.path.join(D, "data", "hibr", "pim", "home", "obj.db")


def stored(text):
    try:
        return text.encode() in open(OBJDB, "rb").read()
    except OSError:
        return False


t = session("mail", "mail", "Mail")
try:
    check("the desktop syncs on its own, with no Calendar or Contacts window open",
          until(lambda: stored("Late meeting"), 15), OBJDB)
    sc = waitfor(t, "Invitation: Planning")
    t.keys([b"o"])
    sc = waitfor(t, "From Ana Smith")
    check("an invitation in a message is a card: what, when, from whom",
          sc.find("Planning") and sc.find("11:00-12:00 -- Room 9") and sc.find("From Ana Smith; will you go?")
          and sc.find(" Yes ") and sc.find(" Maybe ") and sc.find(" No "), sc)
    pos = sc.find(" Yes ")
    t.send(press(pos[0], pos[1] + 2), 0.1)
    t.send(release(pos[0], pos[1] + 2), 0.4)
    check("Yes mails Ana an answer: a reply, Pat's answer accepted",
          until(lambda: [m for m in box.sent if "ana@example.net" in " ".join(m["to"])], 10)
          and "METHOD:REPLY" in [m for m in box.sent if "ana@example.net" in " ".join(m["to"])][0]["raw"]
          and "PARTSTAT=ACCEPTED" in [m for m in box.sent if "ana@example.net" in " ".join(m["to"])][0]["raw"],
          box.sent)
    check("and keeps the event in the calendar, accepted, with no METHOD",
          until(lambda: having("UID:planning-1@example.net"), 10)
          and "PARTSTAT=ACCEPTED" in item(having("UID:planning-1@example.net")[0])
          and "METHOD" not in item(having("UID:planning-1@example.net")[0]), items())
    sc = waitfor(t, "you said yes")
    check("the card then says what was answered", sc.find("you said yes"), sc)
    t.quit(b"qy", 2)
finally:
    srv.terminate()
    srv.wait()

report(23)
