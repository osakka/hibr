# email — IMAP, POP3 and SMTP, and the MIME between them

`email` is a mail client a script can drive: accounts kept in a private
file, sessions to IMAP and POP3 servers, sending over SMTP, and the MIME
work every message needs — headers decoded from RFC 2047, bodies from
quoted-printable and base64, text into UTF-8 from whatever charset it came
in, parts taken out to files, and new messages built. Mail, the desktop's
mail app, is a script on top of it: `examples/desktop/lib/mailsync.hibr`
keeps an account offline with it, and the app reads what that keeps.

The module is called `email` because `mail` is a command (`mailx`), and a
module's builtin would hide it.

```text
mod load email                       # or need email
email account set work -e pat@example.com -w 'app password' -n 'Pat Doe'
email accounts                       # name, kind, server, user, address -- never the password
h := email open work                 # IMAP or POP3, as the account says
email folders "$h"; r := email select "$h" INBOX
r := email headers "$h" 1:200        # r[uid]["from"], ["subject"], ["date"]...
email fetch "$h" 42 msg.eml          # the whole message, to a file
email store "$h" 42 +'\Seen'; email move "$h" 42 Archive
email idle "$h" 600                  # wait for the folder to change
email close "$h"
email send work msg.eml              # SMTP, recipients from the headers
email build out.eml -f 'Pat <pat@example.com>' -t zoe@example.org -s Hi -T body.txt
r := email parse out.eml             # headers, text, html and parts
```

## Accounts

`email account set NAME` adds an account or changes one; what is not given
is kept. The fields are `-k imap|pop`, `-h host`, `-p port`,
`-S tls|starttls|plain`, `-u user`, `-w password`, `-e address`,
`-n "real name"`, the SMTP server `-o host -q port -T security`, and `-K`
to accept a certificate that does not verify. An address at Gmail, Outlook
or Hotmail fills in that provider's servers and ports when no host is
given, so a Gmail account is its address and an app password:

```text
email account set g -e someone@gmail.com -w 'abcd efgh ijkl mnop'
```

`email account mv OLD NEW` renames one, keeping everything else, and
`email account rm NAME` forgets it.

They live in `HIBR_MAIL_CONF`, else `$XDG_CONFIG_HOME/hibr/mail`, else
`~/.config/hibr/mail`: tab-separated lines, written mode 0600 through a
temporary file, and refused outright if anyone else can read the file —
`chmod 600` it, the error says. `email accounts` never prints a password,
and nothing else in the module does either.

There is no OAuth. Google accepts an app password for IMAP and SMTP when
the account has two-step verification; that is what the Mail app asks for.

## IMAP

LOGIN or SASL-IR PLAIN, after STARTTLS when asked; `LIST` with RFC 6154's
special-use roles (`folders` gives each name, its flags and its role — inbox,
sent, drafts, trash, spam, archive, all); `SELECT` or `EXAMINE` (`-r`);
`UID FETCH` of headers, flags, whole messages; `UID STORE`, `UID MOVE`
(with `COPY` and `EXPUNGE` where MOVE is missing), `UID SEARCH`, `APPEND`;
and `IDLE`, falling back to a check every half minute where the server does
not have it. Folder names are modified UTF-7 on the wire and UTF-8 here.

On Gmail (`email gmail h` says 1) headers carry `labels`, `thrid` and
`gmid` from `X-GM-LABELS`, `X-GM-THRID` and `X-GM-MSGID`, and
`email label h uids +Work -\Inbox` changes labels — which is how archiving
works there.

## POP3

USER and PASS, after STLS when asked. `email list h` answers each message
by its UIDL with its number and size (`r[uidl]["n"]`, `["size"]`);
`email fetch h n file` and `email delete h n` act on the number; the
deletions happen when the session is closed, as POP3 has them. A server
without UIDL is refused, since without it nothing can be kept offline.

## SMTP

`email send ACCOUNT file [recipient...]`: EHLO, STARTTLS or TLS from the
start, AUTH PLAIN or LOGIN, then the message with its lines dot-stuffed.
Without recipients they are read from To, Cc and Bcc, and the Bcc header
is taken out of what is sent.

## MIME

`email parse file` puts the message in `$RET`: `from`, `fromname`,
`fromaddr`, `to`, `cc`, `replyto`, `subject`, `date` (seconds since the
epoch), `msgid`, `inreplyto`, `refs`, `text` and `html` (each decoded and
in UTF-8), and `parts[i]` with `type`, `name`, `cid`, `disp` and `size`.
`email part file i out` writes one part out, decoded. Headers are decoded
from RFC 2047 words and RFC 2231 parameters; text from quoted-printable and
base64; charsets from UTF-8, ISO 8859-1 and -15 and Windows-1252 directly,
and anything else through iconv, found at run time (glibc's own, or
libiconv). Text that claims to be ASCII but is not valid UTF-8 is read as
Windows-1252, which is what it nearly always is.

```text
$ email hdec "=?utf-8?q?Caf=C3=A9?= at =?utf-8?b?bm9vbg==?="
Café at noon
$ printf 'Lunch on Friday?\n' > body.txt
$ email build out.eml -f "Pat Doe <pat@example.com>" -t "Zoë <zoe@example.org>" -s "Café" -T body.txt
$ grep -v '^Date:\|^Message-ID:' out.eml
From: Pat Doe <pat@example.com>
To: =?UTF-8?B?Wm/Dqw==?= <zoe@example.org>
Subject: =?UTF-8?B?Q2Fmw6k=?=
MIME-Version: 1.0
User-Agent: hibr mail
Content-Type: text/plain; charset=UTF-8
Content-Transfer-Encoding: 7bit

Lunch on Friday?
$ r := email parse out.eml
$ echo "${r["fromname"]} / ${r["subject"]}"
Pat Doe / Café
$ email clip 7 "Zoë Ünal"
Zoë Ü
```

`email build` takes `-f from`, `-t to`, `-c cc`, `-b bcc`, `-s subject`,
`-T text-file`, `-H html-file`, `-a attachment` (as many as wanted),
`-r in-reply-to`, `-R references`, and `-C calendar-file [-M METHOD]`: an
iCalendar object added beside the text, inside the same
multipart/alternative, as `text/calendar; method=METHOD` -- how an
invitation (REQUEST), an answer (REPLY) or a cancellation (CANCEL) is
mailed so any mail program offers its buttons. `email clip bytes text` shortens text
to at most that many bytes without cutting a character in half, for a
fixed-width column.

## TLS

Through the shell's own `tls_relay`, so libssl is opened only when a
connection needs it and is never linked. Certificates are verified unless
the account says `-K`.

## How it is tested

`tests/mailserve.py` is a stand-in IMAP (with Gmail's extensions), POP3
and SMTP server written for the suites, in the test's own process, on
127.0.0.1. `tests/mail.py` drives the module against it — logins right and
wrong, TLS and STARTTLS with a certificate of its own, folders in UTF-7,
headers, flags, labels, moves, IDLE woken by a new message, POP3, SMTP with
Bcc and dot-stuffing, and the MIME cases — and `tests/mailapp.py` drives
the Mail app through a pty against the same server. Both run under ASan.

## Files

| file | role |
|---|---|
| `mail.c` | the builtin, sessions, and the subcommands |
| `conf.c` | the accounts file |
| `conn.c` | connections: dialling, timeouts, TLS through the relay, buffered reads |
| `imap.c` | IMAP: responses, literals, the commands, modified UTF-7 |
| `pop.c` | POP3 |
| `smtp.c` | SMTP |
| `mime.c` | RFC 2047 and 2231, quoted-printable, base64, charsets, parsing and building |
| `ml.h` | what the files share |
