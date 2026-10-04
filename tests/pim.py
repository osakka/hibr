#!/usr/bin/env python3
"""The pim module and dav's CalDAV and CardDAV side, against the stand-in.

pim reads, expands and writes iCalendar and vCard; recurrence is checked
rule by rule against RFC 5545's examples in tests/pim_rrule.py, and here
everything else: fields, escapes and folding, vCard 2.1 to 4.0, overrides,
EXDATE and RDATE, whole days, reminders, built events read back to the
same instants, invitations answered. Then dav's propfind, report and sync
are driven against tests/davserve.py --pim the way a sync job will.
Run it directly:  python3 tests/pim.py [path-to-hibr]
"""
import os, re, shutil, subprocess, sys, tempfile, time, json

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
from screen import check, report, tree

import screen as sx
HIBR = os.path.abspath(sys.argv[1]) if len(sys.argv) > 1 else sx.HIBR
MODS = os.environ.get("HIBR_TESTMODS", tree("build/mods"))
D = tempfile.mkdtemp(prefix="hibr-pim-")


def hb(script, tz="Europe/London", env=None):
    e = dict(os.environ, TZ=tz, HIBR_MODPATH=MODS, XDG_CONFIG_HOME=os.path.join(D, "cfg"))
    e.update(env or {})
    r = subprocess.run([HIBR, "-c", "need pim\n" + script], capture_output=True, text=True,
                       cwd=D, env=e)
    return r.stdout, r.stderr


def write(name, text):
    p = os.path.join(D, name)
    with open(p, "w", newline="") as f:
        f.write(text)
    return p


CAL = """BEGIN:VCALENDAR\r
VERSION:2.0\r
METHOD:REQUEST\r
BEGIN:VTIMEZONE\r
TZID:Custom Zone\r
BEGIN:STANDARD\r
DTSTART:19700101T000000\r
TZOFFSETFROM:+0200\r
TZOFFSETTO:+0200\r
END:STANDARD\r
END:VTIMEZONE\r
BEGIN:VEVENT\r
UID:weekly@x\r
DTSTART;TZID=Europe/London:20261005T090000\r
DTEND;TZID=Europe/London:20261005T093000\r
RRULE:FREQ=WEEKLY;COUNT=4\r
EXDATE;TZID=Europe/London:20261012T090000\r
RDATE;TZID=Europe/London:20261030T150000\r
SUMMARY:Stand\\, up\r
DESCRIPTION:Line one\\nLine two with a very long tail that runs on and on until it has\r
  to be folded\r
ORGANIZER;CN="Pat, Doe":mailto:pat@example.com\r
ATTENDEE;CN=Zoë;PARTSTAT=ACCEPTED:mailto:zoe@example.org\r
ATTENDEE;RSVP=TRUE:mailto:bo@example.net\r
BEGIN:VALARM\r
ACTION:DISPLAY\r
TRIGGER:-PT10M\r
END:VALARM\r
END:VEVENT\r
BEGIN:VEVENT\r
UID:weekly@x\r
RECURRENCE-ID;TZID=Europe/London:20261019T090000\r
DTSTART;TZID=Europe/London:20261019T110000\r
DTEND;TZID=Europe/London:20261019T113000\r
SUMMARY:Stand up, moved\r
END:VEVENT\r
BEGIN:VEVENT\r
UID:day@x\r
DTSTART;VALUE=DATE:20261024\r
SUMMARY:Away\r
END:VEVENT\r
BEGIN:VEVENT\r
UID:custom@x\r
DTSTART;TZID=Custom Zone:20261007T120000\r
DURATION:PT2H\r
SUMMARY:Elsewhere\r
END:VEVENT\r
BEGIN:VTODO\r
UID:todo@x\r
DUE:20261031T170000Z\r
SUMMARY:File it\r
PRIORITY:1\r
END:VTODO\r
END:VCALENDAR\r
"""
cal = write("cal.ics", CAL)
out, err = hb('r := pim ics events %s; i=0\nwhile [ -n "${r[$i]["kind"]+x}" ]; do '
              'echo "${r[$i]["kind"]}|${r[$i]["uid"]}|${r[$i]["summary"]}|${r[$i]["start"]}|'
              '${r[$i]["end"]}|${r[$i]["allday"]}|${r[$i]["recurid"]}"; i=$((i + 1)); done\n'
              'echo "method=$PIM_METHOD"\n'
              'echo "org=${r[0]["org"]} name=${r[0]["orgname"]}"\n'
              'echo "att=${r[0]["att"][0]["addr"]}/${r[0]["att"][0]["name"]}/${r[0]["att"][0]["partstat"]} '
              '${r[0]["att"][1]["addr"]}/${r[0]["att"][1]["partstat"]}/${r[0]["att"][1]["rsvp"]}"\n'
              'echo "alarm=${r[0]["alarm"][0]["trigger"]}"\n'
              'printf "desc=%%s\\n" "${r[0]["description"]}"\n'
              'echo "ex=${r[0]["exdate"]} rd=${r[0]["rdate"]} rule=${r[0]["rrule"]} prio=${r[4]["priority"]}"' % cal)
lines = out.splitlines()
check("events reads every event and task, its text unescaped",
      len([l for l in lines if l.startswith(("VEVENT|", "VTODO|"))]) == 5 and
      "VEVENT|weekly@x|Stand, up|1791187200|1791189000|0|" in lines, out + err)
check("a whole day is local midnight to midnight",
      any(l.startswith("VEVENT|day@x|Away|1792796400|1792882800|1|") for l in lines), out)
check("a zone only the calendar describes is read at its own offset",
      any(l.startswith("VEVENT|custom@x|Elsewhere|1791367200|1791374400|") for l in lines), out)
check("an override carries the instant it replaces",
      "VEVENT|weekly@x|Stand up, moved|1792404000|1792405800|0|1792396800" in lines, out)
check("the invitation's METHOD, organiser and attendees, quoted names unquoted",
      "method=REQUEST" in out and "org=pat@example.com name=Pat, Doe" in out and
      "att=zoe@example.org/Zoë/ACCEPTED bo@example.net/NEEDS-ACTION/1" in out, out)
check("a reminder is seconds before the start; a folded line is whole again",
      "alarm=-600" in out and "desc=Line one\nLine two with a very long tail that runs on and on "
      "until it has to be folded" in out, out)
check("EXDATE, RDATE and the rule are there for a script that wants them",
      "ex=1791792000 rd=1793372400 rule=FREQ=WEEKLY;COUNT=4 prio=1" in out, out)

out, err = hb("pim ics expand %s 1790000000 1795000000" % cal)
starts = [l.split("\t")[0] + " " + l.split("\t")[3] for l in out.splitlines()]
check("expand: the rule's instances less EXDATE, with RDATE, the override in its place, in order",
      starts == ["1791187200 Stand, up", "1791367200 Elsewhere", "1792404000 Stand up, moved",
                 "1792796400 Away", "1793005200 Stand, up", "1793372400 Stand, up",
                 "1793466000 File it"], starts)
out, err = hb('r := pim ics expand %s 1792390000 1792410000\necho "${#r[@]} ${r[0]["override"]} '
              '${r[0]["recurring"]} ${r[0]["recurid"]} ${r[0]["item"]}"' % cal)
check("an instance knows it is an override, of which instance, and which item it came from",
      out.strip() == "1 1 1 1792396800 1", out + err)

# Building: an event in a zone, read back to the same instants, with the
# zone described; a whole day; text that needs escaping and folding.
long = "A summary; with, commas and a long tail " + "x" * 120
out, err = hb("pim ics build %s/b.ics -u b1 -s '%s' -b 1791190800 -e 1791194400 -z America/New_York "
              "-r 'FREQ=DAILY;COUNT=3' -L 15 -o 'pat@example.com;Pat' -A 'zoe@example.org;Zoë' -m REQUEST\n"
              "r := pim ics events %s/b.ics\n"
              "echo \"${r[0][\"start\"]} ${r[0][\"end\"]} ${r[0][\"tzid\"]} ${#r[0][\"summary\"]} ${r[0][\"alarm\"][0][\"trigger\"]}\"\n"
              "pim ics expand %s/b.ics 0 2000000000 | wc -l" % (D, long, D, D))
built = open(os.path.join(D, "b.ics"), "rb").read().decode()
check("a built event reads back to the same instants in its zone, its text whole",
      out.split()[:5] == ["1791190800", "1791194400", "America/New_York", str(len(long)), "-900"] and
      out.split()[5] == "3", out + err)
check("and is written as RFC 5545 asks: CRLF, folded at 75 octets, a VTIMEZONE for the zone",
      all(len(l.encode()) <= 75 for l in built.split("\r\n")) and "\n" not in built.replace("\r\n", "") and
      "BEGIN:VTIMEZONE\r\nTZID:America/New_York" in built and "SUMMARY:A summary\\; with\\, commas" in built,
      built[:400])
out, err = hb("pim ics build - -u d1 -s Off -b 1792796400 -e 1792882800 -a | grep DT")
check("a whole day is written as dates", "DTSTART;VALUE=DATE:20261024" in out and
      "DTEND;VALUE=DATE:20261025" in out, out + err)
out, err = hb("pim ics build - -u x -b 1 -z Not/AZone; echo st=$?")
check("a zone the system does not know is refused", "st=2" in out and "not a zone" in err, out + err)

# Answering an invitation.
out, err = hb("pim ics reply %s/b.ics %s/rep.ics zoe@example.org ACCEPTED\n"
              "r := pim ics events %s/rep.ics\n"
              "echo \"$PIM_METHOD ${r[0][\"uid\"]} ${#r[0][\"att\"][@]} ${r[0][\"att\"][0][\"addr\"]} "
              "${r[0][\"att\"][0][\"partstat\"]} ${r[0][\"start\"]} ${r[0][\"org\"]}\"" % (D, D, D))
check("a reply carries the invitation's identity, times and organiser, and only this attendee's answer",
      out.strip() == "REPLY b1 1 zoe@example.org ACCEPTED 1791190800 pat@example.com", out + err)

# vCard: 3.0 with groups and types, 2.1's bare types and quoted-printable,
# 4.0's tel: URIs; and one built and read back.
VCF = """BEGIN:VCARD\r
VERSION:3.0\r
UID:c1\r
FN:Zoë Ünal\r
N:Ünal;Zoë;;;\r
EMAIL;TYPE=INTERNET;TYPE=WORK:zoe@example.org\r
item1.EMAIL;type=pref:zoe.home@example.org\r
TEL;TYPE=CELL:+44 7700 900123\r
ADR;TYPE=HOME:;;1 High St;London;;N1 1AA;UK\r
ORG:Example Ltd;Research\r
NOTE:Likes\\, tea\\nand cake\r
PHOTO;ENCODING=b;TYPE=JPEG:AAAA\r
END:VCARD\r
BEGIN:VCARD\r
VERSION:2.1\r
FN:Bob Old\r
EMAIL;WORK;INTERNET:bob@old.example\r
NOTE;ENCODING=QUOTED-PRINTABLE:Caf=C3=A9 au=\r
 lait\r
END:VCARD\r
BEGIN:VCARD\r
VERSION:4.0\r
FN:Cy New\r
TEL;VALUE=uri;TYPE=voice:tel:+1-555-0100\r
END:VCARD\r
"""
vcf = write("c.vcf", VCF)
out, err = hb('r := pim vcf cards %s\n'
              'echo "${#r[@]}|${r[0]["fn"]}|${r[0]["given"]}|${r[0]["family"]}|${r[0]["org"]}|${r[0]["photo"]}"\n'
              'echo "${r[0]["email"][0]["v"]}(${r[0]["email"][0]["type"]}) ${r[0]["email"][1]["v"]}(${r[0]["email"][1]["type"]})"\n'
              'echo "${r[0]["tel"][0]["v"]}(${r[0]["tel"][0]["type"]}) ${r[0]["adr"][0]["v"]}"\n'
              'printf "%%s|" "${r[0]["note"]}"; echo\n'
              'echo "${r[1]["email"][0]["v"]}(${r[1]["email"][0]["type"]}) ${r[1]["note"]}"\n'
              'echo "${r[2]["tel"][0]["v"]}"' % vcf)
l = out.splitlines()
check("vCard 3.0: names, organisation, emails with their types and groups, a photo noticed",
      l[:2] == ["3|Zoë Ünal|Zoë|Ünal|Example Ltd|1",
                "zoe@example.org(internet,work) zoe.home@example.org(pref)"], out + err)
check("phones and addresses, and a note's escapes",
      l[2] == "+44 7700 900123(cell) 1 High St, London, N1 1AA, UK" and
      "Likes, tea\nand cake|" in out, out)
check("vCard 2.1's bare types and a quoted-printable soft break; 4.0's tel: address",
      "bob@old.example(work,internet) Café au lait" in out and l[-1] == "+1-555-0100", out)
out, err = hb("pim vcf build %s/n.vcf -u n1 -f 'Ana Smith' -n 'Smith;Ana' -e 'ana@x.org;work' "
              "-e ana@home.example -t '+1 555;cell' -o 'Acme, Inc' -N 'two\nlines'\n"
              "r := pim vcf cards %s/n.vcf\n"
              "echo \"${r[0][\"fn\"]}|${r[0][\"family\"]}|${r[0][\"org\"]}|${r[0][\"email\"][1][\"v\"]}|"
              "${r[0][\"tel\"][0][\"type\"]}|${r[0][\"version\"]}\"" % (D, D))
check("a built card reads back the same", out.strip() == "Ana Smith|Smith|Acme, Inc|ana@home.example|cell|3.0",
      out + err)

# CalDAV and CardDAV against the stand-in, the way a sync will go:
# discovery, the collections, a change uploaded, a sync token honoured, a
# multiget, a conditional delete, and the redirect from .well-known.
root = os.path.join(D, "dav")
os.makedirs(os.path.join(root, "principals", "u"))
os.makedirs(os.path.join(root, "calendars", "u", "work"))
os.makedirs(os.path.join(root, "addressbooks", "u", "people"))
json.dump({"name": "Work", "color": "#3366cc", "components": ["VEVENT"]},
          open(os.path.join(root, "calendars", "u", "work", ".props"), "w"))
json.dump({"name": "People"}, open(os.path.join(root, "addressbooks", "u", "people", ".props"), "w"))
srv = subprocess.Popen([sys.executable, os.path.join(HERE, "davserve.py"), "--root", root, "--pim",
                        "--auth", "basic"], stdout=subprocess.PIPE, text=True)
port = int(srv.stdout.readline().split()[1])
DV = "need dav\ndav server set t http://127.0.0.1:%d -u u -p p\n" % port
try:
    out, err = hb(DV + "dav propfind dav://t/.well-known/caldav d:current-user-principal\n"
                  "dav propfind dav://t/principals/u/ c:calendar-home-set card:addressbook-home-set")
    check("discovery: .well-known leads to the principal, which names the homes",
          "current-user-principal\t/principals/u/" in out and "calendar-home-set\t/calendars/u/" in out
          and "addressbook-home-set\t/addressbooks/u/" in out, out + err)
    out, err = hb(DV + 'r := dav propfind -d 1 dav://t/calendars/u/ d:resourcetype d:displayname '
                  'ical:calendar-color c:supported-calendar-component-set cs:getctag d:sync-token\n'
                  'echo "${#r[@]}|${r[1]["href"]}|${r[1]["props"]["resourcetype"]}|${r[1]["props"]["displayname"]}|'
                  '${r[1]["props"]["calendar-color"]}|${r[1]["props"]["supported-calendar-component-set"]}"')
    check("the calendars in a home: kind, name, colour and components, a name attribute read",
          out.strip() == "2|/calendars/u/work/|collection calendar|Work|#3366cc|VEVENT", out + err)
    out, err = hb(DV + "r := dav sync dav://t/calendars/u/work/\necho \"n=${#r[@]} tok=$DAV_SYNC\"\n"
                  "e := dav put %s dav://t/calendars/u/work/b1.ics\necho \"etag=$e code=$DAV_CODE\"\n"
                  "dav propfind dav://t/calendars/u/work/b1.ics d:getcontenttype" % os.path.join(D, "b.ics"))
    tok0 = re.search(r"tok=(\S+)", out)
    check("a sync with no token, then an upload typed as a calendar from its name",
          "n=0" in out and tok0 and "code=201" in out and "getcontenttype\ttext/calendar" in out, out + err)
    out, err = hb(DV + "r := dav sync dav://t/calendars/u/work/ -t '%s' d:getetag c:calendar-data\n"
                  "echo \"n=${#r[@]} ${r[0][\"href\"]} ${r[0][\"status\"]} tok=$DAV_SYNC\"\n"
                  "printf '%%s\\n' \"${r[0][\"props\"][\"calendar-data\"]}\" | grep -c 'UID:b1'"
                  % (tok0.group(1) if tok0 else ""))
    tok1 = re.search(r"tok=(\S+)", out)
    check("the sync since that token brings the new event, its data and a new token",
          "n=1 /calendars/u/work/b1.ics 200" in out and out.strip().endswith("1") and tok1 and
          tok0 and tok1.group(1) != tok0.group(1), out + err)
    out, err = hb(DV + "dav rm -m '\"stale\"' dav://t/calendars/u/work/b1.ics; echo \"code=$DAV_CODE\"\n"
                  "r := dav report dav://t/calendars/u/work/ -d 1 -b '<c:calendar-multiget xmlns:d=\"DAV:\" "
                  "xmlns:c=\"urn:ietf:params:xml:ns:caldav\"><d:prop><d:getetag/></d:prop>"
                  "<d:href>/calendars/u/work/b1.ics</d:href></c:calendar-multiget>'\n"
                  "dav rm -m \"${r[0][\"props\"][\"getetag\"]}\" dav://t/calendars/u/work/b1.ics; echo \"code=$DAV_CODE\"\n"
                  "r := dav sync dav://t/calendars/u/work/ -t '%s'\necho \"n=${#r[@]} ${r[0][\"status\"]}\""
                  % (tok1.group(1) if tok1 else ""))
    check("a delete with a stale ETag is refused; with the one a multiget gave, it goes, and sync says so",
          "code=412" in out and "code=204" in out and "n=1 404" in out, out + err)
    out, err = hb(DV + "e := dav put %s dav://t/addressbooks/u/people/n1.vcf\n"
                  "r := dav report dav://t/addressbooks/u/people/ -d 1 -b '<card:addressbook-query xmlns:d=\"DAV:\" "
                  "xmlns:card=\"urn:ietf:params:xml:ns:carddav\"><d:prop><d:getetag/><card:address-data/></d:prop>"
                  "</card:addressbook-query>'\n"
                  "printf '%%s' \"${r[0][\"props\"][\"address-data\"]}\" > got.vcf\n"
                  "c := pim vcf cards got.vcf\necho \"${c[0][\"fn\"]}\"" % os.path.join(D, "n.vcf"))
    check("CardDAV: a card uploaded and an address book queried gives it back, readable",
          out.strip() == "Ana Smith", out + err)
finally:
    srv.terminate()
    srv.wait()

shutil.rmtree(D, True)
report(24)
