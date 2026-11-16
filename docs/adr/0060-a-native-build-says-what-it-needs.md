# ADR 0060: a native build says what it needs to load

Status: proposed, with [ADR 0058](0058-the-catalogue-has-sources.md);
waits for the owner's approval. Epic: Gitea #561.

## Context

[ADR 0044](0044-a-module-can-be-installed.md) let a `.so` be installed and
decided what must be true before one is trusted: a signed index, an exact
`abi`, and a machine (`arch`, `uname -sm` lowercased: `linux-x86_64`). Both
are checked before the fetch (`cat_needs`). That is what makes a module
*load* on the hibr it was built for. It says nothing about whether it loads
on the **machine**:

- **The C library.** A module cannot carry its own; it uses the one hibr
  was started with, and its floor is whatever the build machine had. This
  box is glibc 2.41 (Debian 13); hibr's packages are built on Debian 12
  (2.36, `packaging/deb/build-bookworm.sh`) so that they install there. A
  module built here installs on Debian 12 and fails at `need`, with a
  dynamic linker message about a symbol version.
- **The CPU.** An engine built with `-march=native` or for x86-64-v3 dies
  with SIGILL on an older processor -- not at `need`, but in the middle of
  the first call that uses the instruction.
- **Other libraries.** A module linked against a library the machine lacks
  fails at `need`; hibr's own modules `dlopen` everything beyond libc
  (libssl, libpng, libavcodec) so that they load and then say what is
  missing.
- **The ABI moves.** The core audit's last release narrows `struct ent`
  and `struct var` (#427), an ABI bump. A catalogue module built for ABI 17
  will not load on 18, and today the index can describe one build per
  machine, not one per ABI.
- **macOS** builds hibr too (`darwin-arm64`), where the same three questions
  have different answers: a minimum macOS version, `LC_LOAD_DYLIB`s, and
  arm64 throughout.

## Decision

1. **A native entry has builds, keyed by machine and ABI.** Each version's
   `builds` is a list; each build names `machine`, `abi`, `bundle`,
   `sha256`, `size`, and what it needs to load:
   - Linux: `glibc` -- the highest `GLIBC_` version the object references
     (its `.gnu.version_r`), `needed` -- its `DT_NEEDED` list, and `cpu` --
     its ISA level (`x86-64`, `x86-64-v2`, `-v3`, `-v4`; `armv8-a`).
   - macOS: `macos` -- the object's minimum OS version
     (`LC_BUILD_VERSION`), `needed` -- its `LC_LOAD_DYLIB` list, and `cpu`.

2. **Every fact is read from the object, never typed.** The publishing tool
   (`catalogue publish`, [ADR 0058](0058-the-catalogue-has-sources.md) 8)
   records a build **on the machine that built it, by loading it there** --
   which is how today's `mkindex` reads the ABI, and is a real check that
   the build loads at all -- and reads the rest from the object's headers.
   The index is assembled and signed wherever the source's key is. The ISA
   level is read from the object's GNU property note; a build without one
   is refused with the flag that adds it, since a level that cannot be read
   is a level that was guessed.

3. **The publishing tool refuses a build that cannot travel:**
   - `needed` beyond the C library family (`libc`, `libm`, `libdl`,
     `libpthread`, `librt`, the dynamic linker; `libSystem` on macOS) --
     anything else is `dlopen`ed at run time, as hibr's own modules do;
   - a global symbol that collides with the shell's or a shipped module's
     (`tests/999-symbols.t`'s check, run against the build) -- the
     preemption trap CLAUDE.md records twice;
   - a `glibc` floor above the baseline the source declares (Debian 12's
     2.36 for hibr's own), or a `cpu` above `x86-64` with no baseline build
     beside it.

4. **The client checks before it fetches.** Machine, ABI, `glibc` (from
   `getconf GNU_LIBC_VERSION`), `macos`, and `cpu` (from the processor's
   own flags) are compared before a byte is downloaded, and the size is
   said. No build for this machine is a refusal that names what is offered.
   An engine that wants v3 ships a baseline build that dispatches at run
   time; the index carries one build per machine and ABI, not one per CPU
   level, so the client never has to choose between CPU builds.

5. **An ABI change finds every module it strands.** A source keeps builds
   for the current ABI and the one before. When hibr's ABI moves,
   `catalogue update` fetches each installed module's build for the new
   one; a module with none is named -- "will not load until SOURCE builds
   it for ABI N" -- and stays installed, since removing it is the
   person's. The desktop says the same once at start while any installed
   module's ABI is not this hibr's. hibr's shipped modules are unaffected:
   they are built with the shell. Today no catalogue module exists, so the
   core audit's ABI release does not wait on this; the first source to
   publish a module does.

6. **The baseline is the oldest supported system.** hibr's own source
   builds native entries on Debian 12, x86-64 baseline, the way its
   packages are built; a source states its own and the tool holds it to
   it. **macOS builds are in the format and out of the first release**:
   no source publishes one until a Mac build machine records them, which
   is the owner's choice to make.

## Consequences

- A native entry costs the publisher a build machine per machine it
  supports, and a rebuild per hibr ABI -- the price of not shipping a
  libc.
- The index grows a list of builds per version; a script entry has none.
- The first ABI bump after a source publishes a module is a coordinated
  release for that source.

## Alternatives

**Static linking, or bundling a libc.** A `.so` loaded into hibr shares
hibr's C library: a second libc in one process is two heaps and two sets
of thread state, which crashes rather than isolates. Static linking of
everything *except* libc is what 3 already asks for.

**A build per CPU level, chosen by the client.** Multiplies every build by
four and moves a decision the engine makes better at run time into the
index.

**Publishers declare their own facts.** Simpler, and wrong in exactly the
way #43 would have been: the facts that break a load are the ones a
publisher does not know they have.
