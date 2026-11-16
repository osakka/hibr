# ADR 0058: the catalogue has sources

Status: proposed (the owner chose the direction, 2026-11-16; this design
waits for the owner's approval before anything is built).

## Context

The catalogue is one index, `https://osakka.github.io/hibr-apps/index.json`,
signed by one key (the apt repository's, `A1E3…4431`) and refused under any
other (ADR 0045, Gitea #221). Every entry in it was put there by a session
holding that key: an app, a pane, a theme, a language -- and, since ADR 0044,
a native module.

Two things now press on that.

**Other projects publish.** hibr-ime, Blit's own pane, Longwire, hibr-browser
and hibr-intelligence are each maintained outside hibr's tree, by their own
sessions, and each reaches people through the one catalogue -- whose key it
does not hold, so every publish is a hand-off -- and four of them through
apt, rpm or Arch as well, beside it. That is two routes for one thing.

**The first native module.** hibr-intelligence (#43) asked how to publish a
`.so`. No module has ever been published: `bundles/mods` does not exist,
though the install side refuses a wrong `arch` or `abi` and is tested
(`tests/appmgr.py`). Answering it showed what one key cannot do well:

- the owner's key would sign a binary nobody on the owner's side built;
- `arch` (`uname -sm`, `linux-x86_64`) says nothing about the C library: a
  `.so` loaded into hibr cannot carry its own, its glibc floor is set by
  where it was built, and a build from a newer machine installs on Debian 12
  and fails at `need`;
- an inference engine built for one CPU level (AVX2, `-march=native`) dies
  with SIGILL on an older one;
- `mkindex` reads a module's ABI by `dlopen`ing it, so it can index only its
  own architecture, and the key lives on one x86 machine.

The owner, choosing between "one catalogue plus debs" and this: each project
builds its own, in its own repository, and the catalogue fetches from them.

## Decision

**The catalogue has sources.** A source is a project's own signed index of
its own entries, hosted where the project hosts it.

1. **hibr's index lists the sources and pins their keys.** A source entry
   names its address and its key's fingerprint; hibr's own signature covers
   the list, so the owner's key stays the root of trust and signs no binary
   it did not build. A source is trusted for its own entries and nothing
   else. A person may add a source of their own in their config, pinned the
   same way, and the catalogue says so beside everything from it.

2. **An entry belongs to its source.** Its id is `SOURCE/KIND/NAME`
   (`intelligence/mods/hibr-intelligence`); hibr's own source is `hibr`, and
   a bare `KIND/NAME` keeps meaning hibr's, so nothing written today breaks.
   The installed ledger records each entry's source. **`replaces` and
   `needs` act within a source** -- or name another source's entry
   explicitly -- so no source can remove, replace or shadow what another
   placed. Two sources offering the same name are two entries; installing
   both is refused, with the reason, rather than one silently winning.

3. **One source down stops nothing else.** Each index is fetched, checked
   and cached on its own; a source that does not answer, or whose signature
   fails, is named once and left out, and `update` goes on with the rest.

4. **A native build says what it needs to load, and the client checks it
   before fetching.** Beside `arch` and `abi`: the highest `GLIBC_` version
   it uses, the libraries it `NEEDED`s, and its CPU level
   (`x86-64`, `x86-64-v2`, `-v3`, or the ARM equivalent). The publishing
   tool reads all of them from the object's own headers, never from what a
   publisher types, and refuses a build that needs anything beyond the C
   library family, or exports more than its descriptor. Native builds are
   made on the oldest supported baseline (Debian 12 today, as hibr's own
   packages are), and an engine that wants a higher CPU level ships a
   baseline build that dispatches at run time.

5. **The publishing tools ship with hibr.** `addapp`, `mkindex` and `stale`
   become `catalogue publish` (and its checks), so any project can make and
   sign its own index exactly as hibr-apps does today; hibr-apps becomes the
   first source, `hibr`, using the same tool.

6. **Debs stay only for what the catalogue cannot do:** something run as
   root or installed system-wide, and system libraries -- Blit's root parts,
   hibr-browser's WPE WebKit engine. Everything else a project makes goes
   through its source, and the deb-plus-catalogue parallel ends.

## Phases

Each a release with its own checks, nothing ahead of the one before:

1. **Ids and the ledger:** `SOURCE/` in ids and the installed record, bare
   ids meaning `hibr`, `replaces` and `needs` scoped. No second source yet;
   everything installed today is read as `hibr`'s.
2. **Several indexes:** hibr's index lists sources with pinned keys; each is
   fetched, verified and cached on its own; failure of one is isolated.
3. **`catalogue publish`:** the hibr-apps tools shipped, with the native
   load checks of 4; hibr-apps publishes with it.
4. **The first other source:** whichever project asks first moves its
   entries out of hibr-apps into its own source, and its deb route ends
   where 6 says it should.

Per-ABI builds (a module for an older hibr still installed) and indexing a
second architecture are each a ticket of their own, taken when a second
architecture or an ABI change first needs them.

## Consequences

- **Adding a source is trusting its publisher to run native code as you**
  -- the same trust as adding an apt repository, and it should read as such
  where a person adds one.
- More keys, and more sites that must be up; the isolation of 3 is what
  keeps that from becoming more outages.
- hibr-apps stops being the only place an entry can be published, and the
  owner stops being the only person who can publish one.

## Alternatives

**One catalogue, the owner's key over everything, each project's own debs
for the rest.** Simpler to run, and it is today. It leaves the owner's key
signing others' binaries, every native module routed through one machine,
and the two-route parallel in place. Not chosen (the owner, 2026-11-16).

**Bundles hosted by each project, indexed centrally.** Already possible: an
entry's `bundle` may be any address, checked against the central `sha256`
(docs/catalogue.md, *Index format*). It moves the hosting, not the building
or the signing, so it answers none of the above.
