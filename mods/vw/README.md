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

## What it does not do yet

- Change anything in the vault: it reads.
- Organisations and their collections, Sends, attachments, two-step login
  (a server that asks for a second factor fails the login with its own
  message), Steam's TOTP, or SSO.
- A desktop app or a Control Panel pane: the lock timeout and the PIN are
  the command's settings until then.
