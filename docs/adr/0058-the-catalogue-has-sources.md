# ADR 0058: the catalogue has sources

Status: proposed (the owner chose a distributed catalogue, 2026-11-16, and
then chose that it has **no central index**: "one source of truth, no
parallels ... free to pick the best of what everyone else does"). This
design, with [ADR 0059](0059-an-entry-has-versions.md) and
[ADR 0060](0060-a-native-build-says-what-it-needs.md), waits for the
owner's approval before anything is built. Epic: Gitea #561.

## Context

The catalogue is one index, `https://osakka.github.io/hibr-apps/index.json`,
signed by one key (the apt repository's, `A1E3…4431`) and refused for a
module under any other (ADR 0044, Gitea #221). Every entry in it was put
there by a session holding that key.

Where that index is and who signs it is said **six ways** today, which is
itself the first thing this decision removes:

- the address: `CAT_URL_DEFAULT` in `catalogue.hibr`, the `url` that
  `catalogue index URL` writes to the config, and `HIBR_APPS_URL`;
- the key: `keyring.gpg`, which the Makefile installs beside the shell from
  `packaging/hibr-apps.gpg` and `HIBR_APPS_KEYRING` overrides, and the
  fingerprint `CAT_KEYFP` (overridable with `HIBR_APPS_KEYFP`) that
  `cat_verify` compares the signature with.

Two things now press on the one index.

**Other projects publish.** hibr-ime, Blit's own pane, Longwire, hibr-browser
and hibr-intelligence are each maintained outside hibr's tree, and each
reaches people through the one catalogue -- whose key it does not hold, so
every publish is a hand-off -- and four of them through apt, rpm or Arch as
well. Two routes for one thing.

**The first native module.** hibr-intelligence (its #43) asked how to
publish a `.so`. No module has ever been published, and reading the code
found that **none could have been installed**: `mkindex` writes a module's
bundle, hash, machine and ABI under `builds.<machine>`, and `cat_entry`
copies only top-level fields -- reproduced: the real `mkindex` over a copy
of hibr-apps with `csv.so` added, served locally, and `catalogue install
mods/csv` answers "the index gives no sha256". `tests/appmgr.py` passes
because it serves a hand-written index with the fields at the top. And one
key cannot sign other people's binaries honestly, `mkindex` can index only
its own machine, and what a native build needs to load is not in the index
at all ([ADR 0060](0060-a-native-build-says-what-it-needs.md)).

## What was taken from whom

Each borrowing removes a mechanism or closes a named hole; nothing is here
for resemblance.

- **apt:** a repository is a file naming an address and a key, added by
  whoever administers the machine -- so there is no central list. **Not
  taken:** apt lets any repository ship any package name, which is why
  pinning exists; here a name belongs to its source.
- **TUF:** serials against replay, expiry against a frozen mirror, and a
  key that announces its own successor -- which removes the need for
  anybody above a source to rotate it.
- **Debian's `extrepo`:** a trusted party may *offer* a third party's
  repository file without signing its packages -- which is how hibr vouches
  for a project without holding its key.
- **Go's `go.sum`:** a version, once seen, has one hash for ever, and a
  later answer that differs is refused. Here the client remembers it, so a
  source cannot quietly republish a version with different bytes.
- **Nix, Flatpak:** atomic changes and rollback ([ADR 0059](0059-an-entry-has-versions.md)),
  and refs named by their remote.

## Decision

1. **A source is a file, and that file is the only place a source is
   said.** `sources/<name>.json` holds the source's address and its
   **public key** -- the key itself, an armored OpenPGP key, not a
   fingerprint pointing at a keyring -- checked by the `pkg` module without
   `gpg` ([ADR 0062](0062-the-catalogue-has-a-native-half.md)). The format
   is the one every source and every older client already speaks: hibr's
   own key is RSA-4096, its signatures OpenPGP version 4 over SHA-512, and
   the same `.asc` serves an older client's `gpg` and a new client's `pkg`.
   hibr ships `sources/hibr.json`, which **replaces** all six of
   the mechanisms above: `CAT_URL_DEFAULT`, the config `url`,
   `HIBR_APPS_URL`, `keyring.gpg`, `HIBR_APPS_KEYRING` and
   `CAT_KEYFP`/`HIBR_APPS_KEYFP` go, and
   `catalogue index URL` becomes the way to *add a source*, not to replace
   the only one. There is no central index and no list of sources anywhere
   else; the files present are the sources.

2. **A source's identity is its key; its name is a label bound to it.**
   Without a central list nobody assigns names, so the name is bound to the
   key's fingerprint the first time a source file brings it, and a file
   that brings the same name with a different key is **refused** with both
   fingerprints. Two publishers who both call themselves `intelligence`
   cannot both be added under that name.

3. **Where a source file lives decides what it may do -- and the order is
   the reverse of every other kind's.** For an app, `~/.config/hibr` wins
   over the shipped copy (ADR 0045), which is right for a script a person
   patches and wrong for a key: `cat_keyring` already keeps the key out of
   anything writable for this reason. So for sources:
   - a name is **bound** by the most trusted scope that brings it --
     shipped, then system, then the person's -- and no less trusted scope
     can rebind it to another key;
   - the **address** may still be overridden the ordinary way, since an
     address is a location and not a trust: an administrator may point
     `hibr` at an internal mirror, and what arrives is still checked
     against hibr's key.

   So `hibr` cannot be replaced by a file in a person's folder, and an
   administrator's sources serve every user of the machine.

4. **A source is added only on purpose, three ways, all of which place the
   same file:**
   - by hand: `catalogue source add NAME URL KEYFILE` (and Control Panel >
     Catalogue), which says plainly that it trusts that publisher to run
     code as you -- the trust of adding an apt repository;
   - from another source: hibr's source **offers** source files for the
     projects it vouches for (`hibr/sources/intelligence`), installed like
     any entry and signed by hibr -- hibr vouches for *where and who*, and
     signs none of that project's code. Such an entry carries the `hibr`
     floor of the release that reads sources, so an older client refuses
     it with a reason rather than placing a file it cannot use;
   - by a package: Blit's deb may place its own source file in the shipped
     or system scope, as an apt repository ships its keyring.

   A source file is **never** pulled in by `needs`: adding a publisher
   allowed to run code as you is never a side effect.

5. **An entry belongs to its source.** Its id is `SOURCE/KIND/NAME`;
   `hibr`'s is `hibr`, and a bare `KIND/NAME` keeps meaning hibr's, on the
   command line and in the installed record, so nothing written today
   breaks. **`replaces` and `needs` act within their own source** unless
   they name another source's entry in full. Two sources offering the same
   `KIND/NAME` are two entries; a typed bare name matching both is refused
   with both ids, and installing the second is refused because both would
   land on the same path.

6. **An unsigned source is a setting, and says so everywhere** (the owner,
   2026-11-16: warn and install, as a setting). Control Panel > Catalogue >
   Unsigned Catalogues is *Warn and Install* by default, or *Refuse*. An
   unsigned source is a source file with an address and **no key**, and so:
   - its scripts install with the warning ADR 0044 gives today, once per
     run, and its name is labelled *unsigned* in every listing;
   - its native builds are refused whatever the setting -- a module is
     native code and runs as you (ADR 0044);
   - it is refused in the system scope whatever the setting, as an
     unverified install is today (ADR 0045);
   - it can never bind or shadow a name a keyed source holds, and it has no
     serial, expiry, seen hashes or moves, since each of those is only as
     good as the signature over it.

7. **One source down stops nothing else, and a bad fetch is not a bad
   source.** Each index is fetched, verified and cached on its own
   (`~/.cache/hibr/catalogue/<source>/`), each fetch bounded by
   `HIBR_CONNTIMEO`, and `update` goes on with the rest whatever one does.
   A static host caches an index and its `.asc` **separately** -- the
   paragraph in `cat_getbundle` measured ten minutes on GitHub Pages -- and
   a new index beside the old signature fails to verify (checked: one byte
   changed, `BAD signature`). So a fetched index that fails its signature,
   or carries a lower serial than one already accepted, is **set aside**
   and fetched again next time, and **the last verified index stays in
   force**, installs included. A source is named as failing only when it
   has no verified index at all, or the one it has has expired.

8. **An index cannot be replayed, and a version cannot be rewritten.**
   - Every index carries a `serial` that only grows; the client keeps the
     highest accepted per source and refuses a lower one.
   - An index may carry `expires`. The window is the source's own setting,
     **including never** (the owner, 2026-11-16). Past it a source's scripts
     install with a warning and its native builds are refused. A source
     with no expiry is still protected against replay by its serial, and is
     not protected against a mirror that stops updating and serves its last
     index for ever; the publishing tool says so where the window is set.
   - The client remembers the `sha256` of every version it has seen from a
     source, and refuses a later index giving the same version a different
     one -- naming it as the source having rewritten a published version.
     This starts with versions ([ADR 0059](0059-an-entry-has-versions.md)),
     not before: until then hibr-apps republishes changed bytes under an
     unchanged version, and `update` fetches them on purpose.
   - Serials, accepted keys and seen hashes are **trust state**, kept in
     `$XDG_STATE_HOME/hibr/catalogue/trust.json` (and `/var/lib/hibr` for
     the system scope) -- never the cache, since clearing a cache must not
     re-open replay.

9. **A key announces its successor; revocation is a version.**
   - *Rotation:* a source's index, signed by its current key, may name the
     next one. The client **never rewrites the bound file** -- it cannot
     write a shipped or system one, and writing the successor into a
     person's folder would be the rebind 3 refuses. It records the chain in
     its trust state instead: the key the file binds, then each successor
     that its predecessor signed, and it accepts the newest. A later source
     file bringing a key already in that chain -- the next hibr package's
     `hibr.json` -- is the same source, not a rebind. That is hibr's own
     key's rotation path too.
   - *What it costs:* a stolen key can announce the thief's key, and a lost
     key has no recovery -- the publisher starts a new source and asks
     people to add it, and hibr's source can offer the new file.
   - *Who can revoke what:* a publisher can revoke its own key, which a
     thief holding it never will. So the protection against theft is
     whoever placed the source file: hibr, for a source it vouches for (by a
     version, below); the administrator or the package, for one placed
     system-wide; the person, for one added by hand.
   - *Revocation:* a source file is an entry with versions (ADR 0059); its
     publisher -- or, for a vouched source, hibr -- publishes a version
     saying `revoked` with a reason. `update` installs it like any other
     version, stops fetching that source, and names its installed entries
     on every `update` and in the Catalogue window with `catalogue remove`
     offered. **They are never removed for the person.**
   - *A source file removed* by the person: its entries stay and are said
     once per `update` as no longer offered, exactly as an entry the index
     dropped is said today (Gitea #271).

10. **An entry moves when its old source says so.** A source's index may
    carry `moves` (`"apps/longwire": "longwire/apps/longwire"`), signed by
    the **old** key, so a publisher hands off only what is its own. `update`
    follows a move into a source that is present: the installed copy is
    re-recorded under its new id, updated from there, and the move said. A
    held entry ([ADR 0059](0059-an-entry-has-versions.md)) is not moved.
    **A move into a source that is not present stops and asks** (the owner,
    2026-11-16): the installed copy stays where it is, still updated from
    its old source for as long as that offers it, and `update` and the
    Catalogue window name the source to add -- with the file to add it from
    when a present source offers one. Adding a publisher allowed to run code
    as you is never something `update` does on its own.

11. **An older client keeps working.** hibr's index stays at today's
    address in today's shape: hibr's own entries under `entries` and `apps`,
    signed by today's key, with the new fields beside them, which an older
    client ignores. hibr's entries stay **bare** in the installed record, so
    an older client reads it as today; an entry from another source is keyed
    `SOURCE/KIND/NAME`, which an older client finds no index entry for and
    leaves alone. Run, not reasoned: the installed 0.99.542 `catalogue`,
    over a record holding `intelligence/mods/x`, lists it under `installed`,
    agrees with it under `check`, says under `update` that it "is not in the
    catalogue any more ... it stays as it is", and takes it away with
    `remove`.

12. **The publishing tools ship with hibr.** `addapp`, `mkindex` and
    `stale` become `catalogue publish`, so any project makes and signs its
    own index exactly as hibr-apps does; hibr-apps publishes as `hibr` with
    the same tool.

13. **Debs stay only for what the catalogue cannot do:** something run as
    root or installed system-wide, and system libraries -- Blit's root
    parts, hibr-browser's WPE WebKit engine. Everything else a project
    makes goes through its source, and the deb-plus-catalogue parallel
    ends.


## Phases

The tickets under #561 are the plan; each is a release with checks that
fail against the one before, and nothing starts ahead of the one before.

1. **The client reads `builds`**, and **`catalogue publish`** ships, equal
   to today's `mkindex`, so every later change to the publisher is written
   once, in the tool that ships.
2. **Ids and the record:** `SOURCE/` in ids, bare meaning `hibr`, scoped
   `replaces` and `needs`.
3. **Sources:** the file, its binding and scopes, the five mechanisms
   removed, isolation; then serials and expiry; then, after versions, seen
   hashes, rotation, revocation and moves.
4. **Versions and rollback:** [ADR 0059](0059-an-entry-has-versions.md).
5. **Native builds:** [ADR 0060](0060-a-native-build-says-what-it-needs.md).
6. **Surfaces:** sources, versions, holds and rollback in the Catalogue
   window and the Control Panel.
7. **The first other source:** hibr-intelligence publishes its own, and
   the deb route ends where 13 says; then the other projects.

## Consequences

- **Adding a source is trusting its publisher to run native code as you**,
  and it reads as such wherever a person adds one.
- More keys, and more sites that must be up; the isolation of 7 keeps that
  from becoming more outages.
- **No central off-switch.** Revoking a source reaches a person when they
  next update hibr's source -- the same reach apt has, and the price of no
  central index.
- **Expiry makes publishing periodic** for a source that sets one: it must
  re-sign before `expires` even when nothing changed.
- A move to a new source reaches a person only when they add it: until
  then their copy stays on the old source, named on every `update`. For
  hibr-ime, Longwire and Blit's pane (#581) that is every current user, so
  each move keeps the entry offered from hibr's source for a release.
- hibr-apps stops being the only place an entry can be published, and the
  owner stops being the only person who can publish one.

## Alternatives

**A root index listing every source** -- this ADR's first draft: hibr's
index lists the sources, pins their keys, declares moves and revokes.
Refused by the owner (2026-11-16) for a design with no centre. What it did
is done here by the source files themselves (1, 4), by vouching (4), by the
old key (10) and by a version (9).

**One catalogue, the owner's key over everything, each project's own debs
for the rest.** Simpler to run, and it is today. It leaves the owner's key
signing others' binaries, every native module routed through one machine,
and the two-route parallel in place. Not chosen (the owner, 2026-11-16).

**Bundles hosted by each project, indexed centrally.** Already possible: a
`bundle` may be an absolute address, checked against the `sha256` in the
signed index (`cat_bundleurl`, `cat_hostwarn`; docs/catalogue.md,
*Publishing an app*). It moves the hosting, not the building or the
signing.

**A full update framework (TUF) imported whole.** Its roles, thresholds and
delegations answer more than one-key-per-project needs; the three things
taken from it are the ones a single publisher can run without a key
ceremony.
