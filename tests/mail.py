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
    # Several addresses on one account (Gitea #157): -a keeps them as given,
    # `email accounts` hands back the lot as an array with the one it sends
    # from first, and the file grows a fourteenth field rather than changing
    # any of the thirteen -- checked in both directions below.
    out, err = hb("email account set many -h h.example.com -e pat@example.com -w x"
                  " -a 'p.doe@example.com, pat@alias.org';"
                  " r := email accounts;"
                  " echo \"first=${r[many][addresses][0]}\";"
                  " echo \"second=${r[many][addresses][1]}\";"
                  " echo \"third=${r[many][addresses][2]}\";"
                  " echo \"fourth=[${r[many][addresses][3]-}]\"")
    check("every address of an account comes back as a list, the sending one first",
          "first=pat@example.com" in out and "second=p.doe@example.com" in out
          and "third=pat@alias.org" in out and "fourth=[]" in out, out + err)
    # The fourteenth field is appended, so the thirteen an older build knows
    # are where they were: this reads back a line written without it, and
    # tests/mailapp.py's own account file is one such.
    old = ("old\timap\th\t993\ttls\tu\tp\told@example.com\tOld\t\t465\ttls\t0\n")
    open(CONF, "a").write(old)
    out, err = hb("r := email accounts; echo \"old=${r[old][email]}\";"
                  " echo \"none=[${r[old][addresses][1]-}]\"")
    check("a line written before there were aliases still reads, with none of them",
          "old=old@example.com" in out and "none=[]" in out, out + err)
    out, err = hb("email account set many -a ''; r := email accounts;"
                  " echo \"left=[${r[many][addresses][1]-}]\";"
                  " email account rm many; email account rm old")
    check("and clearing them leaves the sending address alone", "left=[]" in out, out + err)
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

# A port and a security setting that cannot work together, refused where
# they are written rather than four errors later when nothing connects
# (Gitea #187). 993, 995 and 465 are TLS from the first byte; 143, 110,
# 587 and 25 begin in plain and are upgraded. The account this came from
# said `-p 993 -S starttls`, which is the one pairing where each end waits
# for the other for ever.
out, err = hb("email account set bad -h h.example.com -e p@example.com -w x"
              " -p 993 -S starttls; echo st $?")
check("TLS-from-the-first-byte port with starttls is refused, and says what to use",
      "st 1" in out and "993 is TLS from the first byte" in err and "-S tls" in err,
      out + err)
out, err = hb("email account set bad2 -h h.example.com -e p@example.com -w x"
              " -p 143 -S tls; echo st $?")
check("and a plain port with tls, the same way round",
      "st 1" in out and "143 begins in plain" in err and "-S starttls" in err, out + err)
out, err = hb("email account set smtpbad -h h.example.com -e p@example.com -w x"
              " -o s.example.com -q 465 -T starttls; echo st $?")
check("the outgoing server is judged too, and named as such",
      "st 1" in out and "SMTP port 465" in err, out + err)
# A port nobody standardised is nobody's business: a server doing something
# unusual there is not a mistake this can know about.
out, err = hb("email account set odd -h h.example.com -e p@example.com -w x"
              " -p 9930 -S starttls; echo st $?; email accounts")
check("an unusual port is left alone", "st 0" in out and "9930" in out, out + err)
# And a refused pairing must not leave half an account behind.
out, err = hb("email accounts")
check("an account refused for its pairing was not created",
      "bad" not in out and "smtpbad" not in out, out)

# One failed connection, one message. Everything after the first failure on
# a broken connection fails too -- the read, the greeting that has nothing
# to read, the write that follows -- and each used to say so, burying the
# one line that named the cause under three that were its consequences
# (Gitea #187). A listener that accepts and closes is a broken connection
# with no TLS and no server needed to arrange it.
import socket as _sk
_rude = _sk.socket()
_rude.setsockopt(_sk.SOL_SOCKET, _sk.SO_REUSEADDR, 1)
_rude.bind(("127.0.0.1", 0))
_rude.listen(4)


def _rudeloop():
    while True:
        try:
            c, _ = _rude.accept()
        except OSError:
            return
        c.close()


threading.Thread(target=_rudeloop, daemon=True).start()
out, err = hb("email account set rude -h 127.0.0.1 -p %d -S plain -e p@example.com"
              " -w x; email open rude; echo st $?" % _rude.getsockname()[1])
check("a connection that breaks says so once, not once per consequence",
      "st 1" in out and len([l for l in err.splitlines() if "email:" in l]) == 1,
      [l for l in err.splitlines() if "email:" in l])

# "Can it be reached, and will it take this password" -- for each direction,
# which is what a person wants to know when mail does not work (Gitea #188).
# The real login both times: nothing is sent and no mailbox changes, which
# is why the count of what the stand-in received is taken first.
SENTN = len(box.sent)
out, err = hb("email account set chk -h 127.0.0.1 -p %d -S plain -u pat -w 'right horse'"
              " -e pat@example.com -o 127.0.0.1 -q %d -T plain;"
              " email check chk; echo in $?; email check chk -o; echo out $?" % (p, smp))
check("check logs in to both servers and sends nothing",
      "in 0" in out and "out 0" in out, out + err)
out, err = hb("email account set chkbad -h 127.0.0.1 -p %d -S plain -u pat -w WRONG"
              " -e pat@example.com -o 127.0.0.1 -q %d -T plain;"
              " email check chkbad; echo in $?; email check chkbad -o; echo out $?" % (p, smp))
check("and a wrong password fails each in the server's own words",
      "in 1" in out and "out 1" in out and "AUTHENTICATIONFAILED" in err
      and "535" in err, out + err)
# The outgoing check stops at being logged in: a mailbox that gained a
# message would mean it had gone further than it says.
check("the outgoing check delivered nothing", len(box.sent) == SENTN, len(box.sent))

# lib/mailtest.hibr, the ladder the Mail Control Panel's Test draws: a line
# per step for each direction, so "it does not work" becomes which rung it
# stopped on (Gitea #188). Run here rather than in a desktop suite because
# it needs a server and no desktop at all -- it is a script somebody can
# run in a terminal too.
def ladder(acct):
    r = subprocess.run([sx.HIBR, tree("examples/desktop/lib/mailtest.hibr"), acct],
                       env=dict(os.environ, HIBR_MAIL_CONF=CONF, HIBR_MODPATH=MODS,
                                HIBR_MAIL_TIMEOUT="5"),
                       capture_output=True, text=True, timeout=90, cwd=D)
    return dict((" ".join(l.split()[:2]), l.split()[2]) for l in r.stdout.splitlines()
                if len(l.split()) >= 3), r.stdout


hb("email account set lad -h 127.0.0.1 -p %d -S plain -u pat -w 'right horse'"
   " -e pat@example.com -o 127.0.0.1 -q %d -T plain" % (p, smp))
st, raw = ladder("lad")
check("the ladder walks name, port, tls, greeting and login, each direction",
      [st.get("in " + k) for k in ("name", "port", "tls", "greet", "auth")] ==
      ["ok", "ok", "skip", "ok", "ok"] and st.get("out auth") == "ok", raw)
hb("email account set ladbad -h 127.0.0.1 -p %d -S plain -u pat -w WRONG"
   " -e pat@example.com -o 127.0.0.1 -q %d -T plain" % (p, smp))
st, raw = ladder("ladbad")
check("a wrong password stops at the login, with everything under it passed",
      st.get("in port") == "ok" and st.get("in greet") == "ok"
      and st.get("in auth") == "fail", raw)

# A server with virtual mailboxes wants the whole address as the login and
# refuses the name in front of the @ -- which arrives as "Authentication
# failed" and reads as a wrong password (Gitea #192). A stand-in whose own
# user *is* the address is the only way to drive it: the one above accepts
# `pat`, which is the wrong way round.
abox = ms.Box(user="pat@example.com")
abox.add("INBOX", msg("Bob <bob@example.net>", "Hi"), flags=[])
ap = ms.serve("imap", abox)
out, err = hb("email account set addr -h 127.0.0.1 -p %d -S plain -u pat"
              " -w 'right horse' -e pat@example.com;"
              " email check addr; echo bare $?;"
              " email check addr -u pat@example.com; echo given $?" % ap)
check("check -u tries the login it is given where the account's own is refused",
      "bare 1" in out and "given 0" in out, out + err)
# Reported, never written: which login a server wants is the account's own
# to change, so the file still says what it said.
out, err = hb("r := email accounts; echo \"login=${r[addr][user]}\"")
check("and the account it was given is left alone",
      "login=pat" in out and "login=pat@example.com" not in out, out + err)
hb("email account set ladaddr -h 127.0.0.1 -p %d -S plain -u pat -w 'right horse'"
   " -e pat@example.com -o 127.0.0.1 -q %d -T plain" % (ap, smp))
st, raw = ladder("ladaddr")
check("the ladder names the address when that is the login the server wants",
      st.get("in auth") == "fail" and st.get("in login") == "ok"
      and "pat@example.com as the login" in raw, raw)
# Nothing listening: the rungs above must say why they were skipped rather
# than reporting failures of their own, which is the cascade #187 is about
# wearing a different hat.
hb("email account set laddead -h 127.0.0.1 -p 9 -S plain -u pat -w x -e pat@example.com")
st, raw = ladder("laddead")
check("and a port with nothing on it fails once, the rest saying they were skipped",
      st.get("in name") == "ok" and st.get("in port") == "fail"
      and st.get("in tls") == "skip" and st.get("in auth") == "skip", raw)

# Detect: probe the candidates and take the pairing that answers, the
# encrypted one first (Gitea #189). The usual ports are privileged, so the
# candidates are named here -- which is the same option a server on an
# unusual port needs.
def detect(host, *args):
    r = subprocess.run([sx.HIBR, tree("examples/desktop/lib/mailtest.hibr"),
                        "--detect", host] + list(args),
                       env=dict(os.environ, HIBR_MAIL_CONF=CONF, HIBR_MODPATH=MODS,
                                HIBR_MAIL_TIMEOUT="5"),
                       capture_output=True, text=True, timeout=120, cwd=D)
    # "none" carries its reason on the same line, so a pick is its first
    # word when that word is none and the port/security pair otherwise --
    # reading two words either way turned "none" into "none nothing".
    picks = {}
    for l in r.stdout.splitlines():
        f = l.split()
        if f[1:2] == ["pick"]:
            picks[f[0]] = f[2] if f[2:3] == ["none"] else " ".join(f[2:4])
    return picks, r.stdout


pk, raw = detect("127.0.0.1", "imap", "127.0.0.1",
                 "-i", "%d/starttls" % sp, "-i", "%d/plain" % p,
                 "-o", "%d/plain" % smp)
check("detect takes the port that answers, and says so for each candidate",
      pk.get("in") == "%d starttls" % sp and pk.get("out") == "%d plain" % smp, raw)
# The order is the policy: an encrypted candidate that answers wins over a
# plain one that also answers, rather than whichever replied first.
pk, raw = detect("127.0.0.1", "imap", "127.0.0.1",
                 "-i", "%d/plain" % p, "-i", "%d/starttls" % sp,
                 "-o", "%d/plain" % smp)
check("and prefers the encrypted one when both answer, whatever the order asked",
      pk.get("in") == "%d starttls" % sp, raw)
# A greeting is not an offer: a port that greets and never advertises
# STARTTLS must not be written down as starttls, which is the very pairing
# #187 refuses.
pk, raw = detect("127.0.0.1", "imap", "127.0.0.1",
                 "-i", "%d/starttls" % p, "-o", "%d/plain" % smp)
check("a port that greets but never offers STARTTLS is not picked as starttls",
      pk.get("in") == "none", raw)
# And a plain pick says what it costs, where the password is concerned.
pk, raw = detect("127.0.0.1", "imap", "127.0.0.1",
                 "-i", "%d/plain" % p, "-o", "%d/plain" % smp)
check("a plain pick warns that nothing on it is encrypted",
      pk.get("in") == "%d plain" % p and "can be read on the way" in raw, raw)

report(46)
