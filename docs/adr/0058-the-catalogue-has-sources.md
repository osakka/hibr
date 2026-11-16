# ADR 0058: the catalogue has sources

Status: proposed (the owner chose the direction, 2026-11-16; this design,
with [ADR 0059](0059-an-entry-has-versions.md) and
[ADR 0060](0060-a-native-build-says-what-it-needs.md), waits for the
owner's approval before anything is built). Epic: Gitea #561.

## Context

The catalogue is one index, `https://osakka.github.io/hibr-apps/index.json`,
signed by one key (the apt repository's, `A1E3…4431`, pinned in
`keyring.gpg` beside the shell and compared by fingerprint in
`cat_verify`) and refused for a module under any other (ADR 0044, Gitea
#221). Every entry in it was put there by a session holding that key: an
app, a pane, a theme, a language -- and, since ADR 0044, a native module.

Two things now press on that.

**Other projects publish.** hibr-ime, Blit's own pane, Longwire, hibr-browser
and hibr-intelligence are each maintained outside hibr's tree, by their own
sessions, and each reaches people through the one catalogue -- whose key it
does not hold, so every publish is a hand-off -- and four of them through
apt, rpm or Arch as well, beside it. That is two routes for one thing.

**The first native module.** hibr-intelligence (#43) asked how to publish a
`.so`. No module has ever been published, and reading the code to answer it
found that **none could have been installed**: `mkindex` writes a module's
bundle, hash, machine and ABI under `builds.<machine>`, and `cat_entry`
copies only top-level fields, so a module entry reaches `cat_getbundle`
with no `sha256` and is refused. `tests/appmgr.py` passes because it serves
a hand-written index with the fields at the top. Answering #43 also showed
what one key cannot do well:

- the owner's key would sign a binary nobody on the owner's side built;
- `mkindex` reads a module's ABI by `dlopen`ing it, so it can index only its
  own machine, and the key lives on one x86 machine;
- what a native build needs to load (the C library, the CPU) is not in the
  index at all -- [ADR 0060](0060-a-native-build-says-what-it-needs.md).

The owner, choosing between "one catalogue plus debs" and this: each project
builds its own, in its own repository, and the catalogue fetches from them.

## Decision

**The catalogue has sources.** A source is a project's own signed index of
its own entries, hosted where the project hosts it.

1. **hibr's index is the root, and lists the sources.** A source entry names
   its short name (`intelligence`), its index address, and one or more key
   fingerprints. hibr's own signature covers that list, so the owner's key
   stays the root of trust and signs no binary it did not build. A source is
   trusted for its own entries and nothing else. A person may add a source
   of their own (Control Panel, and `catalogue source add`), pinned the same
   way; adding one says plainly that it is trusting that publisher to run
   code as you -- the same trust as adding an apt repository -- and
   everything from it is labelled with its source wherever it is listed.

2. **An entry belongs to its source.** Its id is `SOURCE/KIND/NAME`
   (`intelligence/mods/hibr-intelligence`). hibr's own source is `hibr`, and
   a bare `KIND/NAME` keeps meaning hibr's, on the command line and in the
   installed record, so nothing written today breaks. **`replaces` and
   `needs` act within their own source** unless they name another source's
   entry in full -- a source can depend on another's entry, never remove,
   replace or shadow it. Two sources offering the same `KIND/NAME` are two
   entries; a typed bare name that is ambiguous is refused with both ids, as
   `cat_resolve` refuses an ambiguous name today, and installing the second
   is refused because both would land on the same path.

3. **One source down stops nothing else.** Each index is fetched, verified
   and cached on its own; a source that does not answer, or whose signature
   or serial fails, is named once, left out, and its cached copy is used for
   listing only. `update` goes on with the rest.

4. **An index cannot be replayed.** Every index carries a `serial` (an
   integer that only grows, written by the publishing tool) and an
   `expires` date. The client keeps the highest serial it has accepted from
   each source and refuses a lower one -- the attack that becomes possible
   the moment going back a version is a feature
   ([ADR 0059](0059-an-entry-has-versions.md)) is serving an old, validly
   signed index whose versions were withdrawn for a reason. Past `expires`
   a source's scripts are installed with a warning and its native builds
   are refused, which is the line ADR 0044 already draws between the two.

5. **Keys and sources have a life.**
   - *Rotation:* a source lists several fingerprints; a publisher adds the
     new key to hibr's list, signs with it, and the old one is dropped a
     release later. hibr's own root key rotates the same way, through
     `keyring.gpg` holding both for a release.
   - *Revocation:* hibr's list marks a source `revoked` with a reason. Its
     index is no longer fetched, nothing more is installed from it, and its
     installed entries are named on every `update` and in the Catalogue
     window with `catalogue remove` offered -- **never removed for the
     person**.
   - *A source dropped* from the list without revocation: its installed
     entries stay, said once per `update` as no longer offered, exactly as
     an entry the index dropped is said today (Gitea #271).
   - *A version withdrawn:* [ADR 0059](0059-an-entry-has-versions.md).

6. **An entry may move between sources, and only the root says so.**
   hibr's index carries `moves` (`"apps/longwire": "longwire/apps/longwire"`).
   `update` follows a move when the destination source is listed and
   verified and offers the entry: the installed copy is re-recorded under
   its new id, updated from there, and the move is said. A source cannot
   declare a move into itself; only the root can, since only the root
   decides which source is trusted for what.

7. **An older client keeps working.** The index at today's address stays
   readable by every 0.99.x `catalogue`: hibr's own entries are still under
   `entries` and `apps` in today's shape, and `sources`, `moves`, `serial`
   and `expires` are new fields an older client ignores. hibr's entries stay
   **bare** in the installed record, so an older client reads the record
   as it does today; an entry from another source is keyed
   `SOURCE/KIND/NAME`, which an older client finds no index entry for and
   leaves alone with its existing "not in the catalogue any more" line.

8. **The publishing tools ship with hibr.** `addapp`, `mkindex` and `stale`
   become `catalogue publish` and its checks, so any project makes and signs
   its own index exactly as hibr-apps does; hibr-apps is the first source,
   `hibr`, published with the same tool.

9. **Debs stay only for what the catalogue cannot do:** something run as
   root or installed system-wide, and system libraries -- Blit's root parts,
   hibr-browser's WPE WebKit engine. Everything else a project makes goes
   through its source, and the deb-plus-catalogue parallel ends.

## Phases

Each a release with checks that fail against the one before; the tickets
under #561 are the plan, and nothing starts ahead of the one before it.

1. **Ids and the record:** `SOURCE/` in ids, bare meaning `hibr`, `replaces`
   and `needs` scoped; the client reads `builds` (the bug above).
2. **Trust:** the root lists sources; each is fetched, verified, cached and
   isolated; serial, expiry, rotation, revocation, moves.
3. **Versions and rollback:** [ADR 0059](0059-an-entry-has-versions.md).
4. **Native builds:** [ADR 0060](0060-a-native-build-says-what-it-needs.md).
5. **`catalogue publish`:** hibr-apps publishes with it.
6. **Surfaces:** sources, versions, holds and rollback in the Catalogue
   window and the Control Panel.
7. **The first other source:** hibr-intelligence (#43) publishes its own,
   and the deb route ends where 9 says.

## Consequences

- **Adding a source is trusting its publisher to run native code as you**,
  and it reads as such where a person adds one.
- More keys, and more sites that must be up; the isolation of 3 is what
  keeps that from becoming more outages.
- hibr-apps stops being the only place an entry can be published, and the
  owner stops being the only person who can publish one.
- The root index grows a small list, and the client one more cache per
  source: `~/.cache/hibr/catalogue/<source>/`.

## Alternatives

**One catalogue, the owner's key over everything, each project's own debs
for the rest.** Simpler to run, and it is today. It leaves the owner's key
signing others' binaries, every native module routed through one machine,
and the two-route parallel in place. Not chosen (the owner, 2026-11-16).

**Bundles hosted by each project, indexed centrally.** Already possible: a
`bundle` may be an absolute address, checked against the `sha256` in the
signed index (`cat_bundleurl`, `cat_hostwarn`; docs/catalogue.md,
*Publishing an app*). It moves the hosting, not the building or the
signing, so it answers none of the above.

**A full update framework (TUF) imported whole.** Its roles, thresholds and
delegations answer more than this needs; the four things taken from it --
a root that delegates to per-source keys, serials against replay, expiry
against a frozen mirror, and rotation -- are the ones a single-owner
catalogue can run without a key ceremony.
