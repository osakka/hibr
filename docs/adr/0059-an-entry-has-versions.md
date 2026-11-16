# ADR 0059: an entry has versions, and going back is a verb

Status: proposed, with ADRs [0058](0058-the-catalogue-has-sources.md) to
[0062](0062-the-catalogue-has-a-native-half.md); waits for the owner's
approval. Epic: Gitea #561.

## Context

An entry has one version: the one in the index today. `mkindex` says it in
its header -- "a bundle's path is stable and holds the current version
only: git is where a previous one lives ... `apps install` cannot ask for
an older version anyway" (Gitea #230). So:

- **There is no going back.** An update that breaks something leaves the
  person with it until the publisher publishes again. hibr itself has apt
  for this; nothing it installs does.
- **Nothing can stay put.** `update` with no names takes everything to the
  newest, and there is no way to say "not this one".
- **A path that changes content is a cache hazard.** `cat_getbundle`
  carries a paragraph explaining that a static host caches an index and a
  bundle independently, so for minutes after a publish the two can
  disagree and the sha256 refuses. A path whose content never changes
  cannot disagree with anything.
- **An install is atomic per file, not per change.** The record is written
  by rename (#241) and every part is verified before any is written, but
  an install that needs a module places the module, records it, and then
  places the app: an interruption between the two leaves half a change,
  recorded as done.

The owner asked for versioning with roll forward and roll back, as part of
a catalogue that is "100% working".

## Decision

1. **An index lists versions.** Each entry carries `versions`, newest
   first: version, bundle, sha256, its `hibr` floor, its parts (each with
   its own bundle and sha256), its native builds
   ([ADR 0060](0060-a-native-build-says-what-it-needs.md)), when it was
   published, and optionally `withdrawn` with a reason. The top-level
   fields today's clients read stay, filled from the newest version that
   is not withdrawn ([ADR 0058](0058-the-catalogue-has-sources.md), 11).

2. **A published version is immutable.** Its bundle lives at a path that
   names the version (`bundles/apps/Office/sheet/2.3.1/sheet.hibr`), and
   the publishing tool refuses to publish a version with different bytes
   under a number already used, and refuses a number not greater than the
   last (compared as `sort -V` compares, as `cat_needs` compares floors).
   The cache mismatch above stops being possible rather than explained.
   **And a path that works today ends:** `update` now fetches again an
   entry whose bytes changed under an unchanged version ("changed in the
   catalogue; fetched again"), and hibr-apps relies on it whenever a part
   changes and its owner's version does not. From this release on, every
   change to a part needs its owner's version raised, and the publishing
   tool refuses changed bytes without one. The client's memory of seen
   hashes ([ADR 0058](0058-the-catalogue-has-sources.md), 8) starts here.

3. **A source keeps a window of versions, and so does the client; both
   are settings, three by default** (the owner, 2026-11-16).
   - The publishing tool keeps the newest **three** versions of each entry
     in the index and on the host, and prunes older ones -- the same
     window the apt pool keeps, for the same 1 GB Pages limit. It is the
     publisher's setting, in the source's own config.
   - The client keeps the last **three** versions an install or update
     replaced, each with its record, under `$CAT_DATA/.prev/<id>/<version>/`,
     and drops the oldest past that. So going back works offline and after
     the source has pruned a version; going back further than the client
     kept fetches from the source's window. It is a Control Panel row
     (Catalogue > Versions Kept), and 0 keeps none.

4. **The verbs.**
   - `catalogue install ID@VERSION` installs a version the source offers.
   - `catalogue rollback ID [VERSION]` goes back to the kept previous
     version, or to the one named, and **holds** the entry.
   - `catalogue hold ID` / `catalogue unhold ID`. `update` of everything
     leaves a held entry where it is and says so once per run; `update ID`
     naming it is an explicit ask, releases the hold and goes to the
     newest -- rolling forward is releasing the hold.
   - `catalogue history ID`: what was installed, from which source, when,
     and how (install, update, rollback, move), from the record.
   - `catalogue info ID` lists the versions offered, marking the installed,
     the kept and the withdrawn ones.

5. **A withdrawn version is always left.** `update` moves an installed
   withdrawn version to the newest offered even without names, **and even
   when the entry is held** (the owner, 2026-11-16): a publisher withdraws a
   version because it is wrong, and a hold is a choice between good
   versions. The move keeps the hold on the version it lands on and says
   why, with the publisher's reason. `install` and `rollback` refuse a
   withdrawn version with its reason, and a kept copy of one is dropped.

6. **Every change is a transaction.** `install`, `update`, `rollback`,
   `remove` and a followed move each:
   1. fetch and verify everything the change needs -- every entry, every
      part, every module it pulls in -- into
      `$CAT_DATA/.stage/<pid>/`, and check every floor, ABI, machine and
      path, before anything in place is touched;
   2. write a journal, `$CAT_DATA/.txn`, naming each file to move;
   3. move what is in place to `.prev`, and the staged files into place;
   4. write the record (by rename, as today);
   5. remove the journal.

   A run that finds a journal at its start finishes the change if every
   staged file is present, and undoes it from `.prev` otherwise, then says
   which. One `install` that pulls in modules is one transaction: all of it
   lands or none of it does. The existing lock (`cat_lock`) covers it.

7. **Going back is checked like going forward.**
   - *A `hibr` floor:* a version needing a newer hibr than this one is
     refused with the floor, as an install is.
   - *A native build:* a kept copy is refused if the ABI it was built for
     is not this hibr's, since it would not load; a fetched one needs a
     build for this machine and ABI ([ADR 0060](0060-a-native-build-says-what-it-needs.md)).
   - *Parts:* belong to their version. Going back places that version's
     parts and takes away what only the newer one placed (`cat_stale`'s
     rule, unchanged).
   - *`needs`:* going back is resolved like any other change
     ([ADR 0061](0061-an-entry-says-what-it-needs.md), 6): a rollback that
     would leave a dependent's need unmet is refused, with the reason.
   - *A bundle:* has no version (Gitea #392); `rollback` on one is
     refused, naming its members, which roll back as the entries they are.
   - *hibr itself* is apt's, not the catalogue's. After hibr is
     downgraded, `catalogue check` names every installed entry whose floor
     is now above it and offers the newest version whose floor fits.

## Consequences

- A source's host carries up to three copies of each entry. Today's
  bundles are 4.6 MB in all; a native module is megabytes per machine per
  version, which is where the Pages limit will be felt first, and why the
  window is a setting of the publishing tool.
- The client carries up to three previous copies of everything it
  installed, by default; the Control Panel row is where that is traded
  against disk.
- The record grows a `history` per entry and a `hold` flag. Both are new
  fields an older client ignores.
- `cat_getbundle`'s cache paragraph can go, once every source is on
  versioned paths.


## Alternatives

**The client keeps everything it ever installed.** Rolls back anywhere,
offline, and grows without bound; a bounded number kept, plus the
source's window, covers the case that matters -- "the update I just took
broke it" -- and the bound is the person's to move.

**Snapshots of the whole data folder.** Rolls back everything at once,
including what the person did not want rolled back, and needs a copy of
every file for every change.

**Git as the version store.** What `mkindex` points at today. Nothing that
installs has git, and a person cannot install from a commit.
