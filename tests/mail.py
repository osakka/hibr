#!/usr/bin/env python3
"""The email module against tests/mailserve.py: a stand-in IMAP, POP3 and
SMTP server run in this process, plain, over TLS and with STARTTLS, and
once as Gmail with its labels and thread ids. Checks look at the server's
own state -- flags really stored, labels really changed, the bytes the
SMTP server really received -- not only at what the module says.
Nothing here reaches the network.
"""
import os, stat, subprocess, sys, tempfile, threading, time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import screen as sx
from screen import check, report, tree
import mailserve as ms

if len(sys.argv) > 1:
    sx.HIBR = os.path.abspath(sys.argv[1])

D = tempfile.mkdtemp(prefix="hibr-email-")
CONF = os.path.join(D, "cfg", "mail")
MODS = os.environ.get("HIBR_TESTMODS") or tree("build/mods")


def hb(script, env=None, timeout=60):
    e = dict(os.environ, HIBR_MAIL_CONF=CONF, HIBR_MAIL_TIMEOUT="5")
    e.update(env or {})
    r = subprocess.run([sx.HIBR, "-c", "mod load %s/email.so\n%s" % (MODS, script)],
                       env=e, capture_output=True, text=True, timeout=timeout, cwd=D)
    return r.stdout, r.stderr


def msg(frm, subj, body="Hello there.\r\n", extra=""):
    return ("From: %s\r\nTo: pat@example.com\r\nSubject: %s\r\n"
            "Date: Thu, 1 Oct 2026 09:30:00 +0200\r\nMessage-ID: <%s@x>\r\n%s"
            "MIME-Version: 1.0\r\nContent-Type: text/plain; charset=utf-8\r\n\r\n%s"
            % (frm, subj, abs(hash(subj)), extra, body))


try:
    box = ms.Box()
    box.add("INBOX", msg("=?utf-8?q?Zo=C3=AB?= <zoe@example.org>", "=?utf-8?b?Q2Fmw6k=?= tonight?"),
            flags=["\\Seen"])
    box.add("INBOX", msg("Bob <bob@example.net>", "Report",
                         extra="Content-Type: multipart/mixed; boundary=b\r\n"), flags=[])
    p = ms.serve("imap", box)

    out, err = hb("email account set work -h 127.0.0.1 -p %d -S plain -u pat -w 'right horse' "
                  "-e pat@example.com -n 'Pat Doe' -o 127.0.0.1 -q 1 -T plain; email accounts" % p)
    mode = stat.S_IMODE(os.stat(CONF).st_mode) if os.path.exists(CONF) else 0
    check("account set writes a file only its owner can read; accounts never shows the password",
          mode == 0o600 and "work\timap\t127.0.0.1:%d\tpat\tpat@example.com" % p in out and
          "right horse" not in out and "right horse" in open(CONF).read(), out + err)
    out, err = hb("email account set g -e someone@gmail.com -w x; r := email accounts;"
                  " echo \"${r[g][host]}:${r[g][port]} ${r[g][smtphost]}:${r[g][smtpport]}\";"
                  " email account rm g; email accounts | cut -f1")
    check("a Gmail address fills in Gmail's servers and ports; rm forgets an account",
          "imap.gmail.com:993 smtp.gmail.com:465" in out and out.split("\n")[1:] == ["work", ""],
          out + err)
    out, err = hb("email account set m -h h1 -u u1 -w secret1; email account mv m n; echo st $?;"
                  " email account mv n work; echo st $?; email accounts | cut -f1,4")
    check("mv renames an account and keeps its password; never onto another's name",
          out.split("\n")[:2] == ["st 0", "st 1"] and "n\tu1" in out and "already an account" in err
          and "\tsecret1" in open(CONF).read().split("\nn\t", 1)[-1].split("\n")[0], out + err)
    hb("email account rm n")
    os.chmod(CONF, 0o644)
    out, err = hb("email accounts; echo st $?")
    check("an accounts file others can read is refused", "st 1" in out and "chmod 600" in err,
          out + err)
    os.chmod(CONF, 0o600)

    out, err = hb("email account set bad -h 127.0.0.1 -p %d -S plain -u pat -w wrong;"
                  " email open bad; echo st $?; email account rm bad" % p)
    check("a wrong password is refused with the server's words", "st 1" in out and
          "refused the login" in err and "Invalid credentials" in err, out + err)

    out, err = hb('h := email open work; echo "h=$h gmail=$(email gmail $h)";'
                  ' email folders $h; r := email select $h INBOX;'
                  ' echo "exists ${r[exists]} uidvalidity ${r[uidvalidity]} uidnext ${r[uidnext]}"')
    check("open logs in; folders lists each with its role; select gives the counts",
          "h=1 gmail=0" in out and "INBOX\tinbox\t" in out and "Sent\tsent\t" in out and
          "Archive\tarchive\t" in out and "exists 2 uidvalidity 7 uidnext 3" in out, out + err)

    out, err = hb('h := email open work; email select $h INBOX > /dev/null; r := email headers $h 1:*;'
                  ' for u in 1 2; do echo "$u|${r[$u][fromname]}|${r[$u][fromaddr]}|${r[$u][subject]}'
                  '|${r[$u][date]}|${r[$u][flags]}|${r[$u][attach]}|${r[$u][size]}"; done;'
                  ' email headers $h 2')
    check("headers come decoded: the sender's name and address, the subject, the date as epoch",
          "1|Zoë|zoe@example.org|Café tonight?|1790839800|\\Seen|0|" in out and
          "2|Bob|bob@example.net|Report|1790839800||1|" in out and
          "2\t1790839800\tBob\tReport" in out, out + err)

    out, err = hb('h := email open work; email select $h INBOX > /dev/null;'
                  ' email fetch $h 1 one.eml; r := email parse one.eml; echo "[${r[subject]}] [${r[text]}]"')
    check("fetch writes the whole message, and parse reads it back",
          "[Café tonight?] [Hello there." in out and
          open(os.path.join(D, "one.eml"), "rb").read() == box.folders["INBOX"][0].raw, out + err)

    out, err = hb('h := email open work; email select $h INBOX > /dev/null;'
                  ' email store $h 2 +\\\\Seen +\\\\Flagged; email store $h 1 -\\\\Seen;'
                  ' r := email flags $h 1:*; echo "1=[${r[1][flags]}] 2=[${r[2][flags]}]"')
    check("store sets and clears flags, and the server holds them",
          box.folders["INBOX"][1].flags == {"\\Seen", "\\Flagged"} and
          box.folders["INBOX"][0].flags == set() and "2=[\\Flagged \\Seen]" in out, out + err)

    out, err = hb('h := email open work; email select $h INBOX > /dev/null;'
                  ' email move $h 1 Archive; email copy $h 2 Archive; email uids $h;'
                  ' echo ---; email select $h Archive > /dev/null; email uids $h')
    check("move takes a message to another folder, copy leaves the original",
          len(box.folders["INBOX"]) == 1 and len(box.folders["Archive"]) == 2 and
          out.split("---")[0].split() == ["2"], out + err)

    open(os.path.join(D, "body.txt"), "w").write("Sent copy\n")
    out, err = hb("email build s.eml -f 'Pat <pat@example.com>' -t bob@example.net -s Hi -T body.txt"
                  " > /dev/null; h := email open work; email append $h Sent s.eml '\\Seen'; echo st $?")
    check("append puts a message in a folder with its flags",
          "st 0" in out and len(box.folders["Sent"]) == 1 and
          box.folders["Sent"][0].flags == {"\\Seen"} and b"Sent copy" in box.folders["Sent"][0].raw,
          out + err)

    open(os.path.join(D, "inv.ics"), "w", newline="").write(
        "BEGIN:VCALENDAR\r\nVERSION:2.0\r\nMETHOD:REQUEST\r\nBEGIN:VEVENT\r\nUID:i1\r\n"
        "DTSTART:20261005T090000Z\r\nSUMMARY:Planning\r\nEND:VEVENT\r\nEND:VCALENDAR\r\n")
    out, err = hb("email build c.eml -f 'Pat <pat@example.com>' -t bob@example.net -s Invitation -T body.txt"
                  " -C inv.ics -M REQUEST > /dev/null; p := email parse c.eml;"
                  " echo \"${p[\"parts\"][0][\"type\"]} ${p[\"parts\"][1][\"type\"]}\";"
                  " email part c.eml 1 got.ics; grep -c 'SUMMARY:Planning' got.ics;"
                  " grep -ci 'content-type: text/calendar.*method=REQUEST' c.eml")
    check("-C adds the event beside the text as text/calendar with its METHOD, readable back as a part",
          out.split() == ["text/plain", "text/calendar", "1", "1"], out + err)

    def later():
        time.sleep(1.5)
        box.add("INBOX", msg("New <n@example.org>", "Arrived during IDLE"))
    threading.Thread(target=later, daemon=True).start()
    t0 = time.time()
    out, err = hb("h := email open work; email select $h INBOX > /dev/null;"
                  " e := email idle $h 20; echo \"st $? [$e]\"")
    took = time.time() - t0
    check("idle returns as soon as the folder changes, saying how (%.1fs)" % took,
          "st 0" in out and "EXISTS" in out and took < 6, out + err)
    t0 = time.time()
    out, err = hb("h := email open work; email select $h INBOX > /dev/null; email idle $h 2; echo st $?")
    check("and with nothing happening it gives up when its time is up",
          "st 1" in out and 1.5 < time.time() - t0 < 8, out + err)

    g = ms.Box(gmail=True)
    g.add("INBOX", msg("Ann <ann@example.com>", "Labels"), labels=["\\Inbox", "\\Important",
                                                                  "Work/Projets dété"],
          thrid=1749, gmid=1750)
    gp = ms.serve("imap", g)
    hb("email account set gm -h 127.0.0.1 -p %d -S plain -u pat -w 'right horse'" % gp)
    out, err = hb('h := email open gm; echo "gmail=$(email gmail $h)"; email folders $h;'
                  ' email select $h INBOX > /dev/null; r := email headers $h 1:*;'
                  ' echo "labels=[${r[1][labels]}] thrid=${r[1][thrid]} gmid=${r[1][gmid]}"')
    check("on Gmail: its folders with their roles, a label in modified UTF-7 made readable",
          "gmail=1" in out and "[Gmail]/All Mail\tall\t" in out and
          "Work/Projets dété\t" in out, out + err)
    lab = out.split("labels=[", 1)[-1].split("]", 1)[0].split("\t")
    check("and each message's labels, thread id and message id",
          sorted(lab) == sorted(["\\Important", "\\Inbox", "Work/Projets dété"]) and
          "thrid=1749" in out and "gmid=1750" in out, out + err)
    out, err = hb("h := email open gm; email select $h INBOX > /dev/null;"
                  " email label $h 1 +Receipts '+Café' -\\\\Important; echo st $?;"
                  " email account set w2 -h 127.0.0.1"
                  " -p %d -S plain -u pat -w 'right horse'; h2 := email open w2;"
                  " email select $h2 INBOX > /dev/null; email label $h2 2 +x; echo st2 $?" % p)
    check("label adds and removes Gmail labels, non-ASCII ones too; other servers refuse",
          "st 0" in out and g.folders["INBOX"][0].labels ==
          {"\\Inbox", "Work/Projets dété", "Receipts", "Café"} and
          "st2 1" in out and "labels are Gmail's" in err, out + err)

    ms_run = subprocess.run(["openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes",
                             "-keyout", os.path.join(D, "k.pem"), "-out", os.path.join(D, "c.pem"),
                             "-days", "2", "-subj", "/CN=localhost"], capture_output=True)
    pair = (os.path.join(D, "c.pem"), os.path.join(D, "k.pem"))
    tp = ms.serve("imap", box, tls=pair)
    out, err = hb("email account set t -h localhost -p %d -S tls -u pat -w 'right horse';"
                  " email open t; echo st $?" % tp)
    check("over TLS an untrusted certificate is refused", "st 1" in out and
          "certificate" in err.lower(), out + err)
    out, err = hb("email account set t -K; h := email open t; email select $h INBOX | grep exists")
    check("-K trusts that server, and IMAP works over TLS", "exists\t2" in out, out + err)
    sp = ms.serve("imap", box, starttls=pair)
    out, err = hb("email account set st -h localhost -p %d -S starttls -u pat -w 'right horse' -K;"
                  " h := email open st; email select $h INBOX | grep exists" % sp)
    check("and with STARTTLS, upgraded before the login", "exists\t2" in out, out + err)

    box.pop = [{"uid": "u-1", "raw": msg("P <p@x>", "Pop one").encode()},
               {"uid": "u-2", "raw": msg("P <p@x>", "Pop two", body=".dotted line\r\nend\r\n").encode()}]
    sizes = [len(m["raw"]) for m in box.pop]
    pp = ms.serve("pop", box, starttls=pair)
    out, err = hb("email account set po -k pop -h localhost -p %d -S starttls -u pat -w 'right horse' -K;"
                  " h := email open po; email list $h; r := email list $h;"
                  " email fetch $h ${r[\"u-2\"][\"n\"]} two.eml; email delete $h 1; email close $h" % pp)
    check("POP3 over STLS lists messages by unique id and size, fetches one with its dots, deletes one",
          "1\tu-1\t%d\n" % sizes[0] in out and "2\tu-2\t%d\n" % sizes[1] in out and
          b".dotted line\r\nend" in open(os.path.join(D, "two.eml"), "rb").read() and
          [m["uid"] for m in box.pop] == ["u-2"], out + err)

    smp = ms.serve("smtp", box, starttls=pair)
    open(os.path.join(D, "msg.txt"), "w").write("Line one\n.starts with a dot\nZoë\n")
    out, err = hb("email account set work -o localhost -q %d -T starttls -K;"
                  " email build o.eml -f 'Pat Doe <pat@example.com>' -t 'Zoë <zoe@example.org>'"
                  " -c bob@example.net -b hidden@example.com -s 'Café' -T msg.txt > /dev/null;"
                  " email send work o.eml; echo st $?" % smp)
    sent = box.sent[-1] if box.sent else {"to": [], "raw": "", "from": ""}
    check("send goes over STARTTLS with AUTH PLAIN, to everyone in To, Cc and Bcc",
          "st 0" in out and sent["from"] == "pat@example.com" and
          sorted(sent["to"]) == ["bob@example.net", "hidden@example.com", "zoe@example.org"],
          out + err)
    check("and the Bcc header never leaves; a dot-led line arrives whole",
          "Bcc" not in sent["raw"] and "hidden@" not in sent["raw"] and
          ".starts with a dot" in sent["raw"].replace("=\r\n", ""), sent["raw"][:600])
    out, err = hb("email account set wb -h 127.0.0.1 -p 1 -u pat -w nope -o localhost -q %d"
                  " -T starttls -K; email send wb o.eml; echo st $?" % smp)
    check("a refused SMTP login says so", "st 1" in out and "refused the login" in err, out + err)
finally:
    pass

report(24)
