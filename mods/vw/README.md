# vw — Bitwarden and Vaultwarden, from the shell

A client for a Bitwarden-compatible server -- Vaultwarden, self-hosted,
first -- in two halves (Gitea #71, ADR 0036): the `vw` module keeps the
vault's keys and does its crypto, and `vw`, a hibr script
(`examples/vw.hibr`, installed as the `vw` command), logs in, syncs and
reads the vault, with the requests going through the dav module's HTTP
client (`dav request`).

```text
vw server URL [-k]          the server: https://vault.example.com (-k: its
                            certificate is not checked)
vw login EMAIL              log in, sync, and unlock
vw unlock [--pin]           unlock -- offline, from the vault kept here
vw lock                     forget the key
vw sync                     fetch the vault again
vw list [TEXT]              id, name and user of every item, or those with TEXT
vw get password|username|totp|notes|uri|item NAME-OR-ID
vw pin set [--keep]|clear   a PIN to unlock with; --keep survives a restart
vw timeout MINUTES          lock after this long unused (0: never; 15 by default)
vw status
```

`vw unlock` and `vw login` print `export VW_SESSION=...`, the way
Bitwarden's own CLI hands out `BW_SESSION`: `eval "$(vw unlock)"` keeps
the shell unlocked. A command that finds the vault locked exits 3, a
usage error 2, anything else that fails 1.

## Online and offline

The vault is kept here as the server sends it -- encrypted -- in
`~/.local/share/hibr/vw/vault.json`, so `vw unlock`, `list` and `get`
need no network. `vw sync` fetches it again with the stored login (the
refresh token, itself encrypted under the vault's key); `vw login` is
needed only once, or when the server has forgotten the login. The account
-- server, email, the KDF and its settings, the encrypted account key --
is `~/.config/hibr/vw/account.json`. Both are 0600, in 0700 folders.

## What is kept, and where

Nothing decrypted is written to disk. The key opened by the master
password lives in the module's memory for one command; between commands
it is kept only wrapped, under a random session key, in
`$XDG_RUNTIME_DIR/hibr-vw-UID/session`, with the time it stops being
accepted. The session key itself is the `VW_SESSION` the shell holds, so
the file alone opens nothing, and `vw lock` removes it. Every use moves
the expiry on by the timeout.

A PIN wraps the same key under a key derived from the PIN with the
account's own KDF. Without `--keep` the wrapped copy is forgotten on
`vw lock`, as Bitwarden's "lock with master password on restart" does;
with it, the PIN alone unlocks until it is cleared.

## The module

`vwk` is the one builtin, and it never prints a key:

| command | does |
|---|---|
| `vwk hash -e EMAIL -k pbkdf2\|argon2 -i N [-m MiB -p P] [-s]` | read the master password (from the terminal, or one line of standard input with `-s`), derive the master key and keep it; the result is the master password hash the server is sent |
| `vwk open ENCKEY` | open the account's key with the master key just derived, and forget the master key |
| `vwk dec [-k ITEMKEY] TEXT`, `vwk enc [-k ITEMKEY] TEXT` | decrypt or encrypt one EncString, with the vault's key or an item's own |
| `vwk mdec TEXT`, `vwk menc TEXT` | the same under the key the **master password** makes, which exists only between `vwk hash` and `vwk open` -- for the one thing that has to be read before the vault's own key has arrived from the server: a remembered two-factor device |
| `vwk session new`, `vwk session open KEY WRAPPED` | wrap the open key under a new random key (`KEY TAB WRAPPED`), or open it again |
| `vwk pin wrap\|open ... [WRAPPED]` | the same, under a key derived from a PIN |
| `vwk totp SECRET [TIME]` | the code now, or at TIME: a base32 secret or an `otpauth://` address with its `digits` (6 to 8) and `period`; SHA-1 only, which is what authenticator apps use -- SHA-256 and SHA-512 are refused, saying so |
| `vwk form TEXT` | TEXT percent-encoded byte by byte for a form body |
| `vwk lock`, `vwk state`, `vwk idle SECONDS` | forget every key; `locked` or `unlocked`; lock by itself after this long unused |

The crypto is Bitwarden's: the master key by PBKDF2-SHA256 or Argon2id
over the lowercased email, stretched by HKDF-Expand into an encryption
key and a MAC key, and EncStrings of type 2 -- AES-256-CBC with an
HMAC-SHA256 checked before anything is decrypted. libcrypto is opened on
first use, never linked (`libcrypto.so.3`, then `.so`, then the macOS
names), and libargon2 only for an Argon2id account. Keys are wiped from
memory when they are done with, and on `vwk lock` and when the module is
unloaded.

`tests/vw_crypto.py` builds an account with Python's cryptography package
-- independently of the module -- and checks every command against it,
TOTP against RFC 6238's vectors. `tests/vw.py` drives the `vw` command
against `tests/bwserve.py`, a stand-in server.

## On the desktop

The **Vault** desk accessory and **Control Panel > Passwords** are built on
this command, not on the module: `examples/desktop/wm/vault.hibr` runs `vw`
as a child, so the keys stay where they are. The desktop is unlocked once
for all of it -- the session key is exported, so a Terminal window opened
afterwards can use `vw` too -- and what is copied from the vault leaves the
clipboard after a while and never reaches the clipboard history. See the
desktop's own README under Passwords.

## Two-step login

An account with two-factor on answers the password grant with a **400**
naming its providers, and `vw login` reads that rather than dying on it: it
picks one it can answer with a typed code -- the authenticator (0), an
emailed code (1), or a YubiKey OTP (3) -- asks for the code, and posts the
same grant again with `twoFactorToken`, `twoFactorProvider` and
`twoFactorRemember` beside it. `--recovery` sends a recovery code (8),
which no server ever lists but every server takes. **Duo (2 and 6), U2f (4)
and WebAuthn (7) each need a browser, and are named and refused rather than
half-tried.**

The code comes from the second line of standard input when the caller said
the password would come that way (`VW_STDIN=1`), and from the terminal
otherwise -- the desktop runs `vw` as a child with no terminal to ask on,
which is why there is a second line at all. Echo is left on: a code is not a
secret worth hiding, and a YubiKey types forty-four characters a person
needs to see go in.

A server told to remember the device hands back a `TwoFactorToken`, kept
with `vwk menc` beside the refresh token in the account file, so the next
login sends it as provider 5 and asks nothing. Three things follow, each one
a way of locking somebody out that this avoids:

- it is kept under the **master password's** key rather than the vault's,
  because the vault's key arrives in the very reply the token is trying to
  obtain -- and in the clear it would hand the second factor to anyone who
  can read the file;
- a token the server has stopped honouring (it drops them on a password
  change) comes back as the providers again, not as a wrong code, so the
  client asks and forgets what it had;
- a **wrong password** carries no provider list, which is how it is told
  from a dead token -- otherwise a mistyped password throws away a good
  remembered device.

`vw login` leaves with status **4** when a code is wanted and none was
given, which is what the desktop's own login box grows a Code field on: a
number rather than a sentence to match.

## What it does not do yet

- Change anything in the vault: it reads.
- Organisations and their collections, Sends, attachments, Steam's TOTP,
  or SSO.
- Two-step login through Duo, U2f or WebAuthn, each of which needs a
  browser; the three typed kinds and a recovery code are done.

