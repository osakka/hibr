# ADR 0062: the catalogue has a native half, the `pkg` module

**Parked by the owner (2026-11-16): nothing here is built until the owner
says so.** hibr-intelligence ships as its own apt package instead
(hibr-intelligence#43).

Status: proposed, with [ADR 0058](0058-the-catalogue-has-sources.md) to
[ADR 0061](0061-an-entry-says-what-it-needs.md); waits for the owner's
approval. Epic: Gitea #561.

## Context

The catalogue is a script (`programs/catalogue.hibr`), installed as a
command, and that is where its decisions belong: what to install, where,
and what to say. It reaches for four outside programs to do what a script
cannot do well:

| | for | used by |
|---|---|---|
| `gpg` | the signature on an index | `cat_verify` |
| `sha256sum` | a bundle's hash | `cat_sha` |
| `sort -V` | comparing versions | `cat_needs` |
| `readelf`, `objdump` | what a native build needs to load | [ADR 0060](0060-a-native-build-says-what-it-needs.md), planned |

Each is a dependency hibr does not ship, and each has already shaped
behaviour: with no `gpg` a script install from a signed catalogue falls to
a warning ("there is no gpg on this machine to check it with"), which on a
Mac without Homebrew's gnupg is every install. And the epic adds a fifth
job no program does: resolving dependencies
([ADR 0061](0061-an-entry-says-what-it-needs.md)), whose cost grows with
the size of everything offered -- this catalogue's 124 entries now, an
operating system's thousands later.

## Decision

1. **One module, `pkg`, is the catalogue's native half.** It decides
   nothing; the script asks it questions and acts on the answers. The name
   is free (`command -v pkg` finds nothing here) and says what it is for.
   It offers:
   - `pkg sha256 FILE` -- the hash, streamed, for a file of any size;
   - `pkg vercmp A B` -- versions compared by **a grammar of our own**, not
     an emulation of `sort -V`: a version is dotted non-negative integers
     (`1`, `2.5.4`, `0.99.542`), compared field by field with a missing
     field as 0, and anything else is refused by the publishing tool.
     Measured: every one of the 117 versions in today's index is already
     that shape (the six bundles carry none, by design, Gitea #392), so the
     grammar breaks nothing and leaves nothing for `sort -V`'s many edge
     cases to decide;
   - `pkg verify SIG DATA KEY` -- an OpenPGP detached signature checked
     against an armored public key, answering the **primary** key's
     fingerprint whether the primary or one of its signing subkeys made it,
     as `cat_verify` accepts today;
   - `pkg host` -- this machine's own answers to what a build needs: its
     machine, glibc version (`gnu_get_libc_version`), macOS version and CPU
     level (from `cpuid`, or `sysctl` on macOS), so no program is asked;
   - `pkg obj FILE` -- what a native build needs to load, from its own
     headers: machine, glibc floor, `NEEDED`, the ISA note when present,
     exported symbols; for Mach-O, the minimum macOS and `LC_LOAD_DYLIB`s;
   - `pkg solve` -- the resolver of ADR 0061, given the candidates as a map
     and answering a solution or the chain of reasons it failed.

   Each fills the result slot and binds with `:=`, as every builtin that
   answers does (CLAUDE.md: a bare `printf` is the bug).

2. **OpenPGP stays the format; `gpg` stops being needed to read it.**
   hibr's key is RSA-4096 and its signatures are OpenPGP version 4 over
   SHA-512, and the same `.asc` must go on serving an older client's
   `gpg` ([ADR 0058](0058-the-catalogue-has-sources.md), 11). So `pkg
   verify` reads exactly that much of OpenPGP's version 4 format -- signature
   and public-key packets and subkeys, RSA and EdDSA (Ed25519), SHA-256 and
   SHA-512, the hashed subpackets and trailer -- and refuses anything else
   by name. Three rules make it safe rather than merely working: an RSA
   signature is checked by **building the expected PKCS#1 v1.5 encoding and
   comparing it whole**, never by parsing the padding it decrypts to (the
   parser is where RSA signature forgeries have lived); only signature
   type 0x00, a signature over a binary document, is accepted; and an
   unknown subpacket marked **critical** refuses the signature, as the
   format requires. **Measured before it was proposed:** a Python script of about
   sixty lines, using nothing but `hashlib` and integer arithmetic, verifies
   hibr-apps' real `index.json.asc` against `/usr/share/hibr/keyring.gpg`
   (GOOD, by `A1E3…4431`) and refuses the same index with one byte added. A
   key format of our own was weighed and refused: it would mean a second
   signature beside the `.asc` for every older client, which is two
   signatures for one index.

3. **The arithmetic is libcrypto's, `dlopen`ed.** RSA, Ed25519 and the
   hashes come from libcrypto -- which the libssl `net.c` loads for TLS is
   built on, and which the vault module already `dlopen`s for its keys --
   so nothing new is linked,
   and a machine that can fetch over HTTPS can verify. What `pkg` writes
   itself is parsing: packets, ELF and Mach-O headers, versions, and the
   resolver.

4. **Signing stays with the publisher's `gpg`.** A private key belongs in
   the publisher's own agent, not in a module; `catalogue publish` signs
   through `gpg` and verifies its own signature through `pkg` before it
   writes anything. So the client needs no `gpg`, and a publisher needs the
   one it already has.

5. **The CPU scan stays a publishing step.** Reading instructions to find
   the CPU level ([ADR 0060](0060-a-native-build-says-what-it-needs.md),
   2) means decoding x86, which `objdump` does and `pkg` will not repeat.
   It runs only where a build is published, never where one is installed.

6. **Everything else stays in the script.** Which source wins, where a
   file lands, what to say and what to refuse: the reasons a person reads
   are written in hibr, where they are now, beside the rest of the
   catalogue. `pkg` holds no state and keeps nothing between calls.

## Consequences

- The client needs no `gpg`, `sha256sum`, `sort` or `getconf`. With
  libcrypto absent
  -- no TLS either -- `pkg` says so, and the client fails as it does today
  without `dav`.
- **The cost, named:** an OpenPGP reader of our own, which is
  security-critical code; and a resolver in C, justified by the operating
  system the owner means to build rather than by today's 124 entries.
- One more module, built and shipped with hibr like every other
  ([ADR 0045](0045-a-thing-has-a-scope.md)). Its OpenPGP reader is
  security-critical code of our own, so it is fuzzed (`tests/fuzz.py`'s
  approach), checked against `gpg`'s own answers on real signatures,
  refused-by-name on every unsupported packet, and run under the
  sanitizers in the quick gate.
- `cat_verify`'s "no gpg on this machine" outcome goes, and with it the
  only way a signed catalogue's script install fell to a warning by
  accident.

## Alternatives

**Keep the outside programs.** Four dependencies hibr does not ship, a
silent downgrade on a Mac, and no home for a resolver.

**Our own signature format (Ed25519 keys, minisign-style).** Smaller to
parse, and a second signature beside the `.asc` for every older client
for as long as one exists -- a parallel by construction.

**The resolver in the script.** Easiest to change, and a SAT-shaped
search in an interpreted loop is the one piece of the catalogue whose cost
grows with everything offered, which an operating system makes large.
