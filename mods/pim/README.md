# pim — calendars and contacts, as their files are written

`pim` reads and writes the two formats calendars and address books are
kept and exchanged in: iCalendar (RFC 5545), for events, tasks and
invitations, and vCard (3.0 and 4.0, and 2.1 when reading), for contacts.
It expands a recurring event into its occurrences over any window, the
part of iCalendar that is easy to get almost right. The dav module moves
these files to and from a CalDAV or CardDAV server; this module knows what
is in them.

```text
mod load pim                                  # or need pim
r := pim ics events cal.ics                   # every event and task, a map each
r := pim ics expand cal.ics FROM TO           # the occurrences in a window
pim ics build out.ics -u UID -s Summary -b START -e END [-z Europe/London] ...
pim ics reply invite.ics reply.ics me@example.org ACCEPTED
r := pim vcf cards people.vcf                 # every contact, a map each
pim vcf build out.vcf -u UID -f "Full Name" -e mail@example.org ...
```

Times are seconds since the epoch, both ways. Every subcommand reads a file,
`-t TEXT`, or standard input for `-`. Without `:=` each prints a line an
item; with it, the maps below.

## Events

`pim ics events` gives, for each VEVENT, VTODO and VJOURNAL: `kind`, `uid`,
`summary`, `location`, `description`, `status`, `start`, `end` (DTEND, or
DTSTART plus DURATION, or DUE for a task), `allday`, `tzid`, `rrule`,
`exdate` and `rdate` (instants, space-separated), `recurid` for an override,
`seq`, `org` and `orgname`, `att[j]` with `addr`, `name`, `partstat`,
`role` and `rsvp`, and `alarm[j]` with `trigger` (seconds from the start,
negative before it), `action` and `desc`. Text is unescaped and unfolded.
`PIM_METHOD` is the calendar's METHOD: REQUEST, REPLY or CANCEL for an
invitation, empty otherwise.

```text
$ pim ics events standup.ics
VEVENT	1791187200	1791188100	0	Stand-up
```

## Expanding

`pim ics expand FILE FROM TO` gives every occurrence that overlaps the
window, in order: the rule's (RRULE), the extra ones (RDATE), less the
removed ones (EXDATE), each replaced by the event whose RECURRENCE-ID names
it. Each occurrence has `start`, `end`, `allday`, `recurid` (the instant it
would have had), `item` (its event's number in `pim ics events`), `uid`,
`summary`, `location`, `status`, `override` and `recurring`.

A rule given `DTSTART;TZID=Europe/London:20261005T090000`,
`RRULE:FREQ=WEEKLY;BYDAY=MO,WE,FR;COUNT=6` and one EXDATE (the 7th):

```text
Mon 05 Oct 09:00  Stand-up
Fri 09 Oct 09:00  Stand-up
Mon 12 Oct 09:00  Stand-up
Wed 14 Oct 09:00  Stand-up
Fri 16 Oct 09:00  Stand-up
```

Five, not six: as RFC 5545 has it, an excluded occurrence still counts
towards COUNT, and DTSTART is always the first occurrence.

Every part of RRULE is read: FREQ from SECONDLY to YEARLY, INTERVAL, COUNT,
UNTIL, BYMONTH, BYWEEKNO, BYYEARDAY, BYMONTHDAY (negative from the end),
BYDAY with ordinals (`1FR`, `-1SU`, counted in the month for a monthly rule
and in the year for a yearly one), BYHOUR, BYMINUTE, BYSECOND, BYSETPOS and
WKST. A date that does not exist (the 30th of February) is passed over. The
walk is bounded: `PM_MAXSTEPS` periods and `PM_MAXINST` occurrences.

`tests/pim_rrule.py` expands every example in RFC 5545 section 3.8.5.3, and
some daylight saving edges, and compares each occurrence with what
python-dateutil makes of the same rule: 46 of 46. dateutil's answers are
kept in `tests/pim/rrule.txt`, so the suite runs without it; `--regen`
writes them again where it is installed.

## Time zones

A time with a TZID is converted with the system's zoneinfo, and TZ is put
back afterwards, since the shell and what it starts read it too. A zone the
system does not know (Outlook writes "W. Europe Standard Time") is read at
the standard offset the calendar's own VTIMEZONE gives it; failing that, as
local time. A floating time, and a whole day, are in the process's own zone.

`pim ics build -z ZONE` writes the event's times in that zone and a
VTIMEZONE for it, made from zoneinfo: every change of offset from the year
before the event to ten years after, found by a walk and pinned to the
second. `pim ics vtimezone ZONE [FROM [TO]]` prints one alone.

## Building and answering

`pim ics build OUT` takes `-u` uid, `-s` summary, `-b` start, `-e` end,
`-a` for whole days, `-z` zone, `-l` location, `-d` description, `-r`
rule, `-x` exdate (repeatable), `-o` organiser `addr[;name]`, `-A` attendee
`addr[;name[;partstat[;role]]]` (repeatable), `-q` sequence, `-S` status,
`-R` recurrence-id, `-L` minutes of a reminder before the start
(repeatable), `-m` METHOD, `-K VTODO` for a task (`-e` is then its DUE) and
`-c` colour. Lines end in CRLF and are folded at 75 octets, never inside a
UTF-8 character; text is escaped.

`pim ics reply IN OUT ADDRESS PARTSTAT [-r RECURID]` makes an iTIP REPLY
(RFC 5546) to an invitation: its UID, SEQUENCE, times, summary, organiser
and recurrence, the invitation's VTIMEZONEs, and only this attendee, with
ACCEPTED, TENTATIVE or DECLINED.

## Contacts

`pim vcf cards` gives, for each card: `uid`, `fn`, `given` and `family`
(from N), `nick`, `org`, `title`, `bday`, `note`, `url`, `version`,
`categories`, `photo` (1 when it has one; the picture itself is not
copied out), and lists `email[j]`, `tel[j]` and `adr[j]`, each with `v` and
`type` (every TYPE, lower case, comma-joined). It reads 3.0's groups
(`item1.EMAIL`), 2.1's bare types (`EMAIL;WORK;INTERNET:`) and
quoted-printable values with their soft line breaks, and 4.0's `tel:`
addresses.

`pim vcf build OUT` writes vCard 3.0 -- what iCloud and most servers want --
or 4.0 with `-V 4.0`, from `-u` uid, `-f` full name, `-n "family;given"`,
`-e email[;type]`, `-t tel[;type]`, `-a "street;city;region;code;country[;type]"`
(each repeatable), `-o`, `-T` title, `-b` birthday, `-N` note, `-U` url,
`-k` nickname and `-C` categories.

```text
$ pim vcf build ana.vcf -u ana-1 -f "Ana Smith" -n "Smith;Ana" -e "ana@example.org;work" -t "+44 20 7946 0000;cell"
$ cat ana.vcf
BEGIN:VCARD
VERSION:3.0
PRODID:-//hibr//pim//EN
UID:ana-1
FN:Ana Smith
N:Smith;Ana;;;
EMAIL;TYPE=work:ana@example.org
TEL;TYPE=cell:+44 20 7946 0000
END:VCARD
$ c := pim vcf cards ana.vcf
$ echo "${c[0]["fn"]} <${c[0]["email"][0]["v"]}> (${c[0]["email"][0]["type"]})"
Ana Smith <ana@example.org> (work)
```

## Limits

Components nest at most `PM_DEPTH` deep. What is read is never run: there
is no part of either format that is. A result with nothing in it is an
empty array, so `${#r[@]}` is 0.

## Files

| file | role |
|---|---|
| `pim.c` | the builtin |
| `line.c` | content lines: unfolding, parameters, escapes, folding on output |
| `time.c` | civil dates, zones through zoneinfo, durations, VTIMEZONE |
| `rrule.c` | RRULE parsing and expansion |
| `ics.c` | events, expansion, building, replies |
| `vcf.c` | contacts, read and built |
| `pm.h` | what the files share |
