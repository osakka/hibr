# ADR 0061: an entry says what it needs, and one resolver answers

Status: proposed, with [ADR 0058](0058-the-catalogue-has-sources.md) to
[ADR 0062](0062-the-catalogue-has-a-native-half.md); waits for the owner's
approval. Epic: Gitea #561.

## Context

The owner (2026-11-16): dependencies "robust and across repos ... in a
nutshell, apt done right for hibr in hibr" -- and, beyond this epic, a real
operating system built around it.

An entry says what it needs **four ways** today, each its own word and its
own code path (docs/catalogue.md, *What an entry needs*):

- `# needs: menubar/sound` -- another entry of the same index, present or
  not, installed first by `cat_needin`;
- `# modules: dav db pim` -- a module, resolved by `need` (which also
  answers an interface: `terminal` finds `term.so`) or fetched as
  `mods/NAME` by `cat_getmod`;
- the `hibr` field -- the oldest hibr it runs on, compared in `cat_needs`;
- `# part:` -- a file it owns and brings, which is a different relation
  (ownership, refcounted) and stays one.

And `# replaces:` says what it takes over from. Reading the code for this
found what "robust" has to fix:

- **No resolver.** `cat_needin` installs what an entry needs by calling
  `cat_install` recursively, and catches a loop only by reaching
  `CAT_NESTMAX`.
- **Not one change.** Each thing pulled in is recorded (`cat_statesave`) as
  it lands, before the entry that needed it.
- **`remove` asks nobody.** It takes away an entry another installed entry
  needs, and nothing says so.
- **Presence only.** No version is ever asked for, nothing can conflict
  with anything, and nothing can stand in for a name except a module's
  interface, which only `need` knows about.

apt has all of these, and its worst moments are two of them: a resolver
that fails with "unmet dependencies" and no reason, and maintainer scripts
that run arbitrary code as root during an install.

## Decision

1. **One relation for "needs".** `needs` names what an entry cannot work
   without, and every one of today's three is a form of it:
   - an entry: `menubar/sound`, or another source's in full,
     `intelligence/mods/x`;
   - an interface: `iface/terminal` -- anything that provides it;
   - hibr itself: `hibr >= 0.99.540`.

   `# modules:` and the `hibr` field are read as `needs` and written by the
   publishing tool in their old places too, for an older client
   ([ADR 0058](0058-the-catalogue-has-sources.md), 11). A part stays a part.

   **An older client meets the new forms safely, but says nonsense unless
   given a floor.** Run against the installed 0.99.542 over a served index:
   `iface/terminal`, `hibr >= 0.99.540`, `menubar/clock >= 1.2, < 2` and
   `intelligence/mods/x` each refuse the install and place nothing -- with
   "needs >=, which is not in this catalogue" for the range, since
   `cat_needin` splits on blanks and commas. With a `hibr` floor on the
   entry, each is refused instead with "needs hibr 0.99.600 or newer, and
   this is 0.99.542". So **the publishing tool gives every entry that uses a
   new form the floor of the release that reads it**, and refuses to publish
   one without; `needs` keeps its name and no second field is needed.

   **`iface` and `hibr` are reserved**: neither may be a kind or a source's
   name, so `iface/terminal` and `hibr >= X` can never mean an entry.

2. **A need may name versions.** `needs: intelligence/mods/x >= 1.2, < 2`
   -- the operators `=`, `>=`, `>`, `<=`, `<`, compared as versions
   ([ADR 0062](0062-the-catalogue-has-a-native-half.md)). No range means any
   version, which is what every `needs` written today means.

3. **An entry provides interfaces, and the catalogue and `need` agree on
   them.** A module's are read from its own descriptor (`HIBR_MODULE_P`,
   what `hibr_require` already finds unloaded) -- never typed. A script
   entry declares `# provides:`. So "what satisfies `terminal`" has one
   answer whether a script asks with `need` or the catalogue asks before an
   install.

4. **An entry may conflict.** `# conflicts: apps/oldplayer < 3` refuses
   both installed at once. Two entries landing on one path conflict
   without being told to, as they are refused today.

5. **One resolver, before anything is fetched.** An install, update,
   rollback or remove is resolved **whole** against every verified source
   (and the shipped and installed sets) before a byte is downloaded:
   - it prefers what is installed (the smallest change), then the newest
     version, and keeps every hold
     ([ADR 0059](0059-an-entry-has-versions.md));
   - it is conflict-driven and **explains a failure** -- the method of
     PubGrub, the resolver of Dart's `pub`, which derives a chain of
     reasons as it learns: *"mail 3.0 needs pim >= 2, and pim 2
     is offered only for ABI 18; this hibr is ABI 17, so mail 3.0 cannot be
     installed here; mail 2.5.4 can."*
   - it is deterministic: the same sources and request give the same
     answer, so a test can state it.

6. **What it resolves is one transaction.** The whole solution is staged,
   verified, placed and recorded together, or not at all
   ([ADR 0059](0059-an-entry-has-versions.md), 6). Going back is resolved
   the same way, so a rollback that would break a dependent is refused
   with the reason, never half done.

7. **Removal protects what depends, and takes nothing away unasked.**
   `remove` resolves too: an entry another installed entry needs is
   refused, naming the dependents, unless asked to take them with it
   (`remove --with-dependents`), which lists them first. What was pulled in
   only because something needed it is **named** once nothing does -- by
   every `remove` and `update`, as apt names what `autoremove` would take --
   and taken away only by `purge`, which stays the verb you ask for. The
   reason is docs/catalogue.md's (*Taking a module away again*): the
   catalogue cannot see a script's own `need pim`, and deleting native code
   behind it is worse than a megabyte left on disk.

8. **Across sources, and never adding one.** A need may name another
   source's entry in full. A need whose source is not present fails to
   resolve, naming the source to add -- `needs` never adds one
   ([ADR 0058](0058-the-catalogue-has-sources.md), 4).

9. **No install scripts, ever.** An entry is files and declarations. What
   installing it does is what the catalogue does -- place, record, take
   away -- and nothing it brings runs during an install, update or removal.
   When something must happen beyond placing files, it is a **declared
   action** from a fixed list the catalogue carries out (there is none
   today; the first is added by an ADR of its own), never a script.

10. **Built for an operating system, without being one yet.** The owner
    means to build a real OS on this. Nothing here needs it today, and
    nothing here may stand in its way:
    - every path the catalogue writes is relative to a **root**, so a later
      `--root DIR` installs into an image as well as onto this machine;
    - a transaction covers everything a change writes, so a later
      whole-system *generation* is many transactions recorded as one;
    - the resolver is native ([ADR 0062](0062-the-catalogue-has-a-native-half.md))
      and sized for thousands of entries, not this catalogue's 124;
    - no relation, file or record here is specific to the desktop.

    The OS itself -- files anywhere under the root, the privileged path
    through `catpriv.hibr`, boot generations, offline mirrors and lock
    files -- is its own epic, opened after this one works.

## Phases

1. **One relation:** `needs` takes entries, interfaces and hibr; `provides`
   read from module descriptors and script headers.
2. **Versions and conflicts** in a need, written by the publisher, after
   versions exist ([ADR 0059](0059-an-entry-has-versions.md)).
3. **The resolver**, with its explanations, resolving every verb inside
   one transaction.
4. **Removal** protects dependents, and names what nothing needs.
5. **Across sources**, after sources exist
   ([ADR 0058](0058-the-catalogue-has-sources.md)).

## Consequences

- Every verb resolves before it acts, so a refusal costs nothing and says
  why.
- A publisher can now break an install by writing a range nobody can
  satisfy; the resolver says so, and the publishing tool resolves its own
  index before signing it, so a source cannot publish a need nothing it
  offers can meet.
- `# modules:` and the `hibr` field stay in the index for older clients,
  written by the tool from `needs`; nobody writes them by hand again.

## Alternatives

**Removing what nothing needs automatically.** apt's `autoremove` done by
every verb. Refused: the catalogue cannot see what a person's own scripts
`need`, so "nothing needs it" is only "nothing the catalogue placed needs
it".

**A SAT solver (libsolv, as zypper and dnf use).** Complete and fast, and
its failures are a list of clauses, not a reason a person can read. The
explanation is the point of 5.

**Keep `cat_needin`'s recursion and add versions to it.** Each verb would
grow its own walk -- install, update, remove and rollback each answering
"what does this need" separately, which is the parallel this ADR removes.

**apt's maintainer scripts, restricted.** Any script that can run during an
install is a script that can do anything as whoever installs; restricting
it is a list that has to be right for ever. Declarations cannot be wrong
that way.
