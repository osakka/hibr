# 0036 — A vault's keys stay in the module; the client is a script

Status: accepted

## Context

A Bitwarden client handles the most sensitive thing a person keeps: every
password they have. Bitwarden's own CLI is a Node.js program, and
Vaultwarden, self-hosted, is the server the owner uses (Gitea #71). The
client has to work offline -- the vault is needed most when the network
is not there -- and stay unlocked across commands without leaving
anything decrypted lying about.

## Decision

The split is between what touches a key and what does not. A module,
`vw`, holds the keys and does every piece of crypto: the KDF (PBKDF2 or
Argon2id), opening the account key, EncStrings, the session and PIN
wrappings, TOTP. Its one builtin, `vwk`, never prints a key; it prints
or binds only what a key opened. Everything else -- the server's
endpoints, the login and sync, storage, listing, matching names -- is a
hibr script, `vw`, installed as a command, with the requests going
through the dav module's HTTP client (`dav request`), so no second HTTP
implementation is written.

libcrypto is loaded at run time, as TLS loads libssl, and libargon2 only
for an Argon2id account: neither is a build dependency. Between commands
the key is kept wrapped under a random session key that only the shell's
`VW_SESSION` holds -- Bitwarden's own `BW_SESSION` model -- with an
expiry in the file beside it.

## Consequences

- The vault is kept encrypted, as the server sends it, so unlocking,
  listing and reading need no network; sync uses a stored refresh token,
  itself encrypted under the vault's key.
- Nothing decrypted is written to disk, and the suites check it: after a
  run, no plaintext and no refresh token is anywhere in the test's
  folders.
- A desktop app and a Control Panel pane can be built on the same two
  halves later; the lock timeout and PIN are the command's settings until
  then.
- The crypto is checked against an account built independently, with
  Python's cryptography package, not against itself.
