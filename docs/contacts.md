# contacts

`contacts` finds and writes cards in your address book from a script, with
no window and no desktop running. The address book is the desktop's own:
the server chosen for contacts in Control Panel > Calendar & Contacts, kept
on this machine and synced the way the Contacts app syncs it. A card put
here is in the Contacts app and on the server; a card added anywhere else
is found here once it has been synced.

<!-- not run: needs a CardDAV server chosen for contacts -->
```sh
contacts sync
contacts find ana
contacts put --name "Bo Berg" --phone "+46 70 123 45 67" --note "longwire lw1q7x"
contacts put -u ana-1 --phone "07700 900123" --note "longwire lw1k2m"
```

- `contacts find TEXT` prints every card whose name, a phone, or a line of
  its note contains TEXT, in any case. It reads only the copy kept on this
  machine, and never waits on the network.
- `contacts put --name NAME [--phone NUMBER]... [--note LINE]...` makes a
  new card and prints its uid.
- `contacts put -u UID [--name NAME] [--phone NUMBER]... [--note LINE]...`
  changes that card and prints its uid.
- `contacts sync` brings the copy kept on this machine up to date now.

## The address book it uses

The servers are the ones Control Panel > Calendar & Contacts has switched on
for calendars and contacts, read from the desktop's settings file,
`~/.config/hibr/desktop.json` (`DT_CONF` names another, as it does for the
desktop). The file is read as data, and only that setting and the sync
interval are taken from it. The server's address and login are the Servers list's, kept by
`dav server set`. Each server's copy lives under
`~/.local/share/hibr/pim/SERVER`, written only by the desktop's sync,
`lib/pimsync.hibr`. `contacts` runs that same sync itself when it has to,
since no desktop may be running to run it:

- `find` never syncs, and never touches the network. It answers from the
  copy kept here, the cards still waiting to go up included, in a fraction
  of a second whatever the server is doing. When that copy is older than
  the desktop's own interval (Calendar & Contacts > Syncing > Every, 15
  minutes unless changed), it says how old on standard error, and still
  answers.
- `put` writes the card here, queues it, and then syncs, to send it.
- `contacts sync` syncs every chosen server, and is how a script with no
  desktop running keeps the copy fresh.

A running desktop keeps the copy fresh by itself, every interval, whether
or not Contacts is open.

<!-- not run: needs a CardDAV server chosen for contacts -->
```sh
contacts find ana
contacts sync
contacts find ana
```

```text
contacts: home has never been synced here: contacts sync brings it
ana-1	Ana Smith	phone:+44 20 7946 0000	note:Met at the conference
```

A copy three hours old:

```text
contacts: the copy of home is 3 hours old: contacts sync, or a desktop running, brings it up to date
hibr-1795112768-13964-2669142	Cy Offline	note:longwire lw1c3p
```

Every sync `contacts` runs is bounded, so a server that is off costs
seconds, not the dav module's half minute:

- The server must take a connection within 3 seconds, or the sync is not
  started. `HIBR_CONNTIMEO`, in milliseconds, sets another bound. Three
  seconds is long enough for a connection that loses its first packet, which
  is resent after one second, and short enough for a program waiting on it.
- A server that takes the connection and then stops answering is given 10
  seconds of silence a request rather than 30. `HIBR_DAV_TIMEOUT`, in
  seconds, sets another.
- A sync another program is already running, the desktop's for instance, is
  waited for up to 10 seconds. After that, `put` leaves its card queued.

Measured on this machine against a server that takes no connection: `find`
0.09 seconds, `put` and `sync` 3.1 seconds. Against one that takes the
connection and never answers, `put` took 10.1 seconds.

With no server chosen, `contacts` stops before anything is made, with status
2 and one line saying so:

<!-- setup
contacts() { "$HIBR" "${HIBR_MODPATH%/build/mods}/programs/contacts.hibr" "$@"; }
export XDG_CONFIG_HOME=$PWD/config XDG_DATA_HOME=$PWD/data
unset DT_CONF
-->
```sh
contacts find Ana
echo "status $?"
```

```output
contacts: no address book is chosen: Control Panel > Calendar & Contacts chooses a server for contacts
status 2
```

A server that is chosen but keeps only calendars is status 2 too, once it
has been synced: `none of the servers chosen keeps an address book`.

## What find prints

One line for each card, sorted by name. The fields are separated by tabs:

| field | what |
|---|---|
| first | the card's uid, which `put -u` takes |
| second | its name |
| then | `phone:NUMBER` for each phone, in the card's order |
| then | `note:LINE` for each line of its note, in order, empty lines left out |

A card has as many `phone:` and `note:` fields as it has phones and note
lines, none when it has none. No field is ever empty, so a tab in `IFS` never
moves one. The name is the card's full name. A card with none shows its
first email, or failing that its uid. A card with no uid shows the last part
of its address on the server, which `put -u` takes as well. Changing such a
card gives it a uid, and `put -u` prints the new one. A backslash, a
tab, a newline or a return inside a value is written as `\\`, `\t`, `\n` or
`\r`, so a line is always exactly one card.

<!-- not run: needs a CardDAV server chosen for contacts -->
```sh
contacts find lw1
```

```text
ana-1	Ana Smith	phone:+44 20 7946 0000	phone:07700 900123	note:Met at the conference	note:longwire lw1k2m
hibr-1795112766-8041-2669091	Bo Berg	phone:+46 70 123 45 67	note:longwire lw1q7x
```

A phone matches when it contains TEXT as written. When TEXT is a number, a
phone also matches on its digits alone, so `contacts find 701234567` finds
`+46 70 123 45 67`. TEXT may be empty, which lists every card. Nothing
matching prints nothing, with status 0.

A script reads it a line at a time. Here a program that keeps a key of its
own in a note line finds each person's numbers and key:

<!-- not run: needs a CardDAV server chosen for contacts -->
```sh
while IFS=$'\t' read -r -a f; do
	uid=${f[0]} name=${f[1]}
	for x in "${f[@]:2}"; do
		case $x in
		phone:*) echo "$name: call ${x#phone:}" ;;
		note:longwire\ *) echo "$name: key ${x#note:longwire }" ;;
		esac
	done
done < <(contacts find lw1)
```

```text
Ana Smith: call +44 20 7946 0000
Ana Smith: call 07700 900123
Ana Smith: key lw1k2m
Bo Berg: call +46 70 123 45 67
Bo Berg: key lw1q7x
```

`contacts` is a program, so `x := contacts find ...` binds what it prints,
the lines above, as `$( )` does. What a note line says is the caller's own business. `find`
matching note lines is what lets a program look a person up by a key it
keeps there.

## What put writes

`put` without `-u` always makes a new card, so `find` first if the person
may be there already. It needs `--name`. Each `--phone` and `--note` may be
given more than once. None of them may hold a newline, and a phone may not
hold a `;`. A new card goes to
the first chosen server that keeps an address book, in its first address
book.

`put -u UID` changes the card with that uid:

- `--name` replaces the name. With no `--name`, the name stays as it was.
- A `--phone` is added unless the card has that number already, compared by
  its digits alone. `07700 900123` and `0770-090-0123` are one number.
- A `--note` line is added at the end of the note unless the note has that
  line already, exactly.
- Everything else on the card stays as it was: its emails with their types
  and labels, its other phones, its address, birthday, picture,
  organisation, and anything a phone or another program wrote that hibr does
  not read. When the name changes, the structured name (N) is made again
  from the new one.

Asked for nothing the card does not already have, `put -u` changes nothing
and sends nothing, and still prints the uid. A uid the address book does not
have is status 1: `contacts: no card nobody in the address book`.

<!-- not run: needs a CardDAV server chosen for contacts -->
```sh
contacts put -u ana-1 --phone "+44 20 7946 0000" --phone "07700 900123" --note "longwire lw1k2m"
```

```text
ana-1
```

The card on the server afterwards. The first phone was there already; the
address and the email's type were kept:

```text
BEGIN:VCARD
VERSION:3.0
PRODID:-//hibr//pim//EN
UID:ana-1
FN:Ana Smith
N:Ana Smith;;;;
EMAIL;TYPE=work:ana@example.org
TEL;TYPE=cell:+44 20 7946 0000
ADR;TYPE=home:;;1 High St;Leeds;;LS1 1AA;UK
TEL:07700 900123
NOTE:Met at the conference\nlongwire lw1k2m
END:VCARD
```

A card is written and queued for the server exactly as an edit in the
Contacts app is. It is the same function, `pe_write` in
`programs/desktop/lib/pim.hibr`, and it goes up only if the server's copy is
still the one it was read from. `put` then reads the card back from the
server. When the server refused the change because its copy had changed in
the meantime, a phone edited somewhere else say, `put -u` plans the same
change again over that newer copy and sends it once more, or not at all
when the newer copy already holds it. A change still refused then, or one
the server will not take for any other reason, is status 1 with the reason:
`put` does not print a uid for a change it sent and the server refused. A
change only queued, because the server could not be reached, is sent by a
later sync, and if the server refuses it then, its copy wins as it does for
an edit in the Contacts app.

## When the server cannot be reached

The card is still kept, and queued for the next sync. `put` prints the uid
with status 0, and says on standard error that the server has not got it yet:

<!-- not run: needs a CardDAV server chosen for contacts -->
```sh
contacts put --name "Cy Offline" --note "longwire lw1c3p"
contacts sync
```

```text
contacts: kept, and queued for home, which has not got it yet: cannot reach 127.0.0.1:37873: Connection refused
hibr-1795112768-13964-2669142
contacts: home: cannot reach 127.0.0.1:37873: Connection refused
```

Until then, `find` and `put -u` see the queued card as it will be, and so
does the Contacts app. A change to it joins the queue, so two puts made
offline are still one card once the server has them. The next `put`,
`contacts sync` or the desktop's own sync sends the queue.

## Status

| status | meaning |
|---|---|
| 0 | done: the lines found, the uid put (sent or queued), or every server synced. Nothing found is 0 too |
| 1 | refused or failed, with the reason on standard error: a usage mistake, an unknown uid, a change the server refused, a server `sync` could not reach, a `find` with no copy kept here yet |
| 2 | no address book: no server is chosen for contacts, and nothing is kept anywhere; or none chosen keeps one |
