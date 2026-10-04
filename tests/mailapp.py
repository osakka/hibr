#!/usr/bin/env python3
"""Drive the Mail app through a pseudo terminal, against tests/mailserve.py.

The server is in this process, so a check can look at what the app did to
the mailbox -- a star, an archive, a sent message -- as well as at the
screen. Nothing here reaches the network or the owner's own accounts: the
account file and the store are in a folder of the run's own.
Run it directly:  python3 tests/mailapp.py [path-to-hibr]
"""
import os, sys, subprocess, tempfile, time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import screen as sx
from screen import Term, check, report, load, tree, press, release
import mailserve as ms

if len(sys.argv) > 1:
    sx.HIBR = os.path.abspath(sys.argv[1])
SLOW = 4 if os.environ.get("ASAN_OPTIONS") else 1
D = tempfile.mkdtemp(prefix="hibr-mailapp-")
if os.environ.get("V"):
    print("in", D)
A = "[Gmail]/All Mail"


def msg(frm, subj, body, date, ctype="text/plain", mid=None, extra=""):
    return ("From: %s\r\nTo: Pat <pat@example.com>\r\nSubject: %s\r\nDate: %s\r\n"
            "Message-ID: <%s@x>\r\n%sContent-Type: %s; charset=utf-8\r\n\r\n%s"
            % (frm, subj, date, mid or abs(hash(subj)), extra, ctype, body))


def until(cond, secs=6.0):
    end = time.time() + secs * SLOW
    while time.time() < end:
        if cond():
            return True
        time.sleep(0.1)
    return cond()


def account(conf, name, port, smtp, extra=""):
    env = dict(os.environ, HIBR_MAIL_CONF=conf, HIBR_MODPATH=tree("build/mods"))
    subprocess.run([sx.HIBR, "-c", "need email; email account set %s -h 127.0.0.1 -p %d -S plain "
                    "-u pat -w 'right horse' -e pat@example.com -n 'Pat Doe' "
                    "-o 127.0.0.1 -q %d -T plain %s" % (name, port, smtp, extra)],
                   env=env, check=True)


def session(conf, tag):
    data = os.path.join(D, tag)
    sess = os.path.join(D, tag + ".hibr")
    open(sess, "w").write("%s. %s\n. %s\ndt_open\ndt_new Mail 26 100 1 2 mail\ndt_run\ndt_close\n" % (
        load("console", "email", "db", "html", "img"), tree("examples/desktop/desktop.hibr"),
        tree("examples/desktop/apps/Internet/mail.hibr")))
    return Term(sess, rows=30, cols=104, settle=1.5,
                env={"HIBR_MAIL_CONF": conf, "XDG_DATA_HOME": data})


def look(t, secs=0.6):
    t.collect(secs * SLOW)
    return t.screen()


def waitfor(t, text, secs=8.0):
    """The screen once it shows text, or as it is when the time is up."""
    end = time.time() + secs * SLOW
    while True:
        sc = look(t, 0.3)
        if sc.find(text) or time.time() > end:
            return sc


def click(t, sc, text, minrow=0):
    pos = sc.find_from(text, minrow)
    if not pos:
        return False
    r, c = pos
    t.send(press(r, c + 1), 0.1)
    t.send(release(r, c + 1), 0.4)
    return True


# --- a Gmail account ---------------------------------------------------------

box = ms.Box(gmail=True)
zoe = box.add(A, msg("Zoë Ünal <zoe@example.org>", "Lunch on Friday?",
                     "Hi Pat,\r\n\r\nAre you free for lunch on Friday? There is a new place on the corner.\r\n\r\nZoë",
                     "Fri, 2 Oct 2026 12:15:00 +0000"), labels=["\\Inbox", "\\Important"], thrid=7)
box.add(A, msg("Pat <pat@example.com>", "Re: Lunch on Friday?", "Yes! 12:30?\r\n\r\n> Are you free for lunch on Friday?",
               "Fri, 2 Oct 2026 13:00:00 +0000"), flags=["\\Seen"], labels=["\\Sent"], thrid=7)
gh = box.add(A, msg("GitHub <noreply@github.com>", "[hibr] Build passed",
                    "All checks have passed.\r\nhttps://github.com/osakka/hibr/actions",
                    "Sat, 3 Oct 2026 08:00:00 +0000"), labels=["\\Inbox", "Work"], thrid=8)
box.add(A, "From: Shop <deals@shop.example>\r\nTo: pat@example.com\r\nSubject: =?utf-8?q?50=25_off_everything?=\r\n"
        "Date: Wed, 30 Sep 2026 10:00:00 +0000\r\nContent-Type: text/html; charset=utf-8\r\n\r\n"
        "<html><body><h1>Big sale</h1><p>Everything is <b>50% off</b> until Sunday. "
        "<a href=\"https://shop.example/sale\">Shop now</a></p></body></html>\r\n",
        flags=["\\Seen"], labels=["Promotions"], thrid=9)
port = ms.serve("imap", box)
smtp = ms.serve("smtp", box)
conf = os.path.join(D, "gmail.conf")
account(conf, "'Home Mail'", port, smtp)
t = session(conf, "gmail")
sc = waitfor(t, "Zoë Ünal, me (2)")
check("the first sync lists the inbox's conversations, newest first",
      sc.find("[hibr] Build passed") and sc.find("Zoë Ünal, me (2)")
      and sc.find("[hibr] Build passed")[0] < sc.find("Zoë Ünal, me (2)")[0], sc)
check("a conversation is named by its first message, not by a reply's Re:",
      sc.find("Lunch on Friday? - Yes! 12:30?"), sc)
check("the sidebar has the standard views and the account's labels",
      all(sc.find(x) for x in ("Inbox", "Starred", "Important", "All Mail", "Work", "Promotions")), sc)
check("the inbox counts its two unread conversations", sc.find("Inbox           2"), sc)

t.keys([b"o"])
sc = waitfor(t, "Back")
check("o opens a conversation: its subject, its sender, its text, a link",
      sc.find("[hibr] Build passed") and sc.find("GitHub <noreply@github.com>")
      and sc.find("All checks have passed.") and sc.find("https://github.com/osakka/hibr/actions"), sc)
check("reading a conversation marks it read on the server",
      until(lambda: "\\Seen" in gh.flags), gh.flags)
check("and the inbox's count goes down", look(t).find("Inbox           1"))

t.keys([b"u", b"j", b"o"])
sc = waitfor(t, "Pat <pat@example.com>")
check("a conversation holds the reply that lives only in Sent",
      sc.find("Zoë Ünal <zoe@example.org>") and sc.find("Pat <pat@example.com>"), sc)
check("blank lines in a plain message keep its paragraphs apart",
      sc.find("Hi Pat,") and sc.row(sc.find("Hi Pat,")[0] + 1)[sc.find("Hi Pat,")[1]:].strip("·│ ") == ""
      and sc.find("Are you free for lunch on Friday? There is")[0] == sc.find("Hi Pat,")[0] + 2, sc)
check("a reply's quoted line is drawn as a quote", sc.find("│ Are you free for lunch"), sc)

t.keys([b"u", b"k", b"s"])
check("s stars a conversation on the server", until(lambda: "\\Flagged" in gh.flags), gh.flags)
t.keys([b"e"])
check("e archives: the inbox label comes off on the server",
      until(lambda: "\\Inbox" not in gh.labels), gh.labels)
sc = look(t)
check("and the conversation leaves the inbox", not sc.find("[hibr] Build passed"), sc)
t.keys([b"g", b"s"])
sc = look(t)
check("g s shows the starred conversation, archived or not", sc.find("[hibr] Build passed"), sc)

click(t, sc, "Promotions", 4)
sc = look(t)
check("a label in the sidebar shows its conversations, subject decoded",
      sc.find("50% off everything"), sc)
t.keys([b"o"])
sc = waitfor(t, "Big sale")
check("an HTML message is laid out: its heading and its text, the markup gone",
      sc.find("Big sale") and sc.find("Everything is 50% off until Sunday. Shop now")
      and not sc.find("<b>"), sc)

t.keys([b"u", b"g", b"i", b"l"])
sc = look(t)
check("l offers the account's labels", sc.find("Work") and sc.find("Promotions"), sc)
t.keys([b"\r"])
check("and the one chosen is put on the conversation on the server",
      until(lambda: "Work" in zoe.labels), zoe.labels)
t.keys([b"r"])
sc = waitfor(t, "Re: Lunch on Friday?")
check("r answers the newest message someone else sent, not the account's own reply",
      sc.find("To:      Zoë Ünal <zoe@example.org>") and sc.find("Subject: Re: Lunch on Friday?"), sc)
check("its text quoted below, blank lines and all",
      sc.find("On Oct 2, Zoë Ünal wrote:") and sc.find("> Hi Pat,")
      and sc.row(sc.find("> Hi Pat,")[0] + 1)[sc.find("> Hi Pat,")[1]:].strip("│ ·") == ">"
      and sc.find("> Are you free for lunch")[0] == sc.find("> Hi Pat,")[0] + 2, sc)
t.keys([b"Lovely.", b"\x1b", b"\r"])
check("a reply is sent in its thread: In-Reply-To names the message answered",
      until(lambda: len(box.sent) == 1, 10)
      and "In-Reply-To: <" in box.sent[0]["raw"] and "References: <" in box.sent[0]["raw"],
      box.sent and box.sent[0]["raw"])
look(t, 1.0)
t.keys([b"c"])
sc = waitfor(t, "New Message")
check("c opens a new message", sc.find("To:") and sc.find("Subject:"), sc)
t.keys([b"zoe@example.org", b"\t", b"\t", b"\t", b"See you there", b"\r", b"Friday it is.", b"\t", b"\t", b"\r"])
check("Send hands the message to the SMTP server", until(lambda: len(box.sent) == 2, 10), box.sent)
raw = box.sent[1]["raw"] if len(box.sent) > 1 else ""
check("to the recipient, with its subject, its text and the account's name",
      len(box.sent) > 1 and box.sent[1]["to"] == ["zoe@example.org"] and "Subject: See you there" in raw
      and "Friday it is." in raw and "Pat Doe <pat@example.com>" in raw, raw)
sc = look(t, 1.0)
check("the compose window closes once it is sent", not sc.find("┤ See you there ├"), sc)
t.keys([b"g", b"a", b"/", b"lunch friday", b"\r"])
sc = look(t)
check("/ searches every word, in the senders, subjects and text",
      sc.find("Lunch on Friday?") and not sc.find("[hibr] Build passed") and not sc.find("50% off"), sc)
t.keys([b"/", b"\x1b"])
sc = look(t)
check("escape puts every conversation back", sc.find("Lunch on Friday?") and sc.find("50% off"), sc)
t.keys([b"g", b"i"])
box.add(A, msg("Ana <ana@example.net>", "Pushed to you", "Arrived while the app was watching.",
               "Sun, 4 Oct 2026 07:00:00 +0000"), labels=["\\Inbox"], thrid=10)
sc = waitfor(t, "Pushed to you", 15)
check("new mail is pushed: IDLE wakes a sync, and the list shows it unasked", sc.find("Pushed to you"), sc)
t.quit(b"qy", 1.5)

# --- a plain IMAP server ------------------------------------------------------

box2 = ms.Box()
box2.add("INBOX", msg("Ana <ana@example.net>", "Minutes", "The minutes are attached.",
                      "Thu, 1 Oct 2026 09:30:00 +0000", mid="minutes"))
box2.add("Sent", msg("Pat <pat@example.com>", "Re: Minutes", "Thanks!",
                     "Thu, 1 Oct 2026 10:00:00 +0000", extra="In-Reply-To: <minutes@x>\r\nReferences: <minutes@x>\r\n"),
         flags=["\\Seen"])
box2.add("Archive", msg("Bo <bo@example.net>", "Old thing", "From the archive.",
                        "Tue, 29 Sep 2026 09:30:00 +0000"), flags=["\\Seen"])
conf2 = os.path.join(D, "plain.conf")
account(conf2, "work", ms.serve("imap", box2), ms.serve("smtp", box2))
t = session(conf2, "plain")
sc = waitfor(t, "Minutes")
check("an ordinary IMAP server lists its inbox", sc.find("Ana") and sc.find("Minutes"), sc)
check("and its folders stand in for labels", sc.find("Archive"), sc)
check("a reply in Sent joins its conversation", sc.find("Ana, me (2)"), sc)
box2.drop_stores = 1
t.keys([b"s"])
sc = waitfor(t, "connection was lost")
check("a connection lost while pushing a change says so", sc.find("connection was lost"), sc)
check("and the change is kept: still starred here, still queued for the server",
      sc.find("✱ Ana, me (2)") and not [m for m in box2.folders["INBOX"] if "\\Flagged" in m.flags], sc)
t.keys([b"R"])
check("the next sync delivers it", until(lambda: [m for m in box2.folders["INBOX"] if "\\Flagged" in m.flags]),
      [m.flags for m in box2.folders["INBOX"]])
look(t, 1.0)
t.keys([b"#"])
check("# moves a conversation to the server's Trash",
      until(lambda: len(box2.folders["Trash"]) == 1 and not [m for m in box2.folders["INBOX"] if "\\Deleted" not in m.flags]),
      {f: len(v) for f, v in box2.folders.items()})
check("but leaves the reply in Sent where it was", len(box2.folders["Sent"]) == 1,
      {f: len(v) for f, v in box2.folders.items()})
t.quit(b"qy", 1.5)

# --- a POP mailbox --------------------------------------------------------------

box3 = ms.Box()
box3.pop = [{"uid": "p-1", "raw": msg("Cy <cy@example.net>", "Popped", "Down the pipe.",
                                      "Thu, 1 Oct 2026 08:00:00 +0000").encode()},
            {"uid": "p-2", "raw": msg("Di <di@example.net>", "Also popped", "Second.",
                                      "Fri, 2 Oct 2026 08:00:00 +0000").encode()}]
conf3 = os.path.join(D, "pop.conf")
account(conf3, "home", ms.serve("pop", box3), ms.serve("smtp", box3), "-k pop")
t = session(conf3, "pop")
sc = waitfor(t, "Also popped")
check("a POP mailbox is downloaded into the inbox", sc.find("Popped") and sc.find("Also popped"), sc)
t.keys([b"o"])
sc = waitfor(t, "Second.")
check("and read offline, whole", sc.find("Di <di@example.net>") and sc.find("Second."), sc)
t.keys([b"u", b"#"])
check("# deletes it from the POP server", until(lambda: [m["uid"] for m in box3.pop] == ["p-1"]),
      [m["uid"] for m in box3.pop])
t.keys([b"g", b"i"])
sc = look(t, 1.0)
check("and the inbox keeps the other", sc.find("Popped") and not sc.find("Also popped"), sc)
click(t, sc, "Trash", 4)
sc = look(t)
check("while the copy here waits in Trash", sc.find("Also popped"), sc)
t.keys([b"c"])
waitfor(t, "New Message")
t.keys([b"cy@example.net", b"\t", b"\t", b"\t", b"From home", b"\r", b"Hi.", b"\t", b"\t", b"\r"])
check("a POP account sends through its SMTP server", until(lambda: len(box3.sent) == 1, 10), box3.sent)
sc = look(t, 1.0)
click(t, sc, "Sent", 4)
sc = waitfor(t, "From home")
check("and keeps what it sent in a Sent folder of its own", sc.find("From home - Hi."), sc)
t.quit(b"qy", 1.5)

# --- the account dialog -----------------------------------------------------------

conf4 = os.path.join(D, "dialog.conf")
sess = os.path.join(D, "dialog.hibr")
open(sess, "w").write("%s. %s\n. %s\nCP_PANEDIRS+=(\"%s\")\ncp_panes\ndt_open\nmlad_show\ndt_run\ndt_close\n" % (
    load("console", "email", "db"), tree("examples/desktop/desktop.hibr"),
    tree("examples/desktop/apps/panel.hibr"), tree("examples/desktop/control-panel")))
t = Term(sess, rows=30, cols=100, settle=1.5, env={"HIBR_MAIL_CONF": conf4, "XDG_DATA_HOME": D})
sc = waitfor(t, "Add a Mail Account")
check("Add Account opens its dialog, and the desktop lives", sc.find("Account name:") and sc.find("Password:"), sc)
t.keys([b"Home Email", b"\t", b"pat@example.com", b"\t", b"Pat Doe", b"\t", b"secret"] + [b"\t"] * 8 + [b"\r"])
sc = look(t)
check("a server it cannot work out from the address is asked for, not guessed",
      sc.find("no server given") and not os.path.exists(conf4), sc)
em = sc.find("Email address:")
if em:
    t.send(press(em[0], em[1] + 40), 0.1); t.send(release(em[0], em[1] + 40), 0.3)
t.keys([b"\x1b[F"] + [b"\x7f"] * len("example.com") + [b"gmail.com"])
click(t, look(t), "Save ", 4)
check("an address at Gmail saves, Gmail's servers filled in",
      until(lambda: os.path.exists(conf4) and "imap.gmail.com" in open(conf4).read()),
      open(conf4).read() if os.path.exists(conf4) else "no file")
t.quit(b"qy", 1.5)
out = subprocess.run([sx.HIBR, "-c", "%s. %s\n. %s\nCP_PANEDIRS+=(\"%s\")\ncp_panes\n"
                      "n := mailset_rows 1; i=0; while [ $i -lt $n ]; do echo \"${CP[1][$i][\"text\"]}|${CP[1][$i][\"val\"]}\"; i=$((i + 1)); done"
                      % (load("email"), tree("examples/desktop/desktop.hibr"), tree("examples/desktop/apps/panel.hibr"),
                         tree("examples/desktop/control-panel"))],
                     capture_output=True, text=True,
                     env=dict(os.environ, HIBR_MAIL_CONF=conf4, XDG_CONFIG_HOME=os.path.join(D, "cfg"),
                              HIBR_MODPATH=tree("build/mods")))
check("the pane lists an account whose name has a space in it",
      "Home Email|pat@gmail.com" in out.stdout and "arithmetic" not in out.stderr, out.stdout + out.stderr)

report(48)
