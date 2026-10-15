# 0045 — A thing has a scope, and the scope says where it lives

Status: accepted; the system scope is built as of 0.99.197

## Context

Three releases drew pieces of one line without naming it.
[ADR 0041](0041-an-app-is-a-folder-of-files-a-bundle-is-how-it-travels.md)
made an app a folder of plain files and a bundle only how it travels.
[ADR 0044](0044-a-module-can-be-installed.md) let a `.so` travel the same
way and decided what has to be true before one may be trusted. 0.99.194
(Gitea #220) stopped the package shipping applications at all, because
shipping them *and* publishing them is two copies of one thing with
nothing saying which is real.

Then the owner asked two questions a week apart, and they turn out to be
the same question.

> If the catalogue is the one route, why do thirty-five modules still
> come in the package?

> Could we use the catalogue to install system-wide components, so we
> don't need to install them per user? What if the hibr desktop is the
> default system for a computer?

The first was answered badly at first — as a seam between *what hibr
builds* and what it does not. That rule, written out, says all
thirty-five modules ship, which is the opposite of the split it was
written to justify. The second question is what shows why: **the division
was never about who built a thing. It is about who it is for.**

## What a module costs where it sits

Two measurements, because the intuition is wrong in one direction and
right in the other.

**An unloaded module costs a shell nothing at startup.** Three module
directories — empty, the fifteen below, and all thirty-five — with
`hibr -c true`, median of 21 runs each:

| modules on the path | startup |
|---|---|
| none | 1161 µs |
| 15 | 873 µs |
| 35 | 836 µs |

The spread is noise and the *empty* directory is the slowest of the
three, which is the point: nothing walks the module path to start a
shell. `dlopen` on demand means what it says.

**The autoload index is the exception, and it is not small.**
`m_bixbuild` walks the whole module path and `dlopen`s every candidate to
read its descriptor — ADR 0044 measured that at thirty-five and built the
user folder's exclusion around it. Same three directories, the first
unknown command, median of 7:

| modules on the path | first unknown command |
|---|---|
| none | 905 µs |
| 15 | 2450 µs |
| 35 | 4202 µs |

So the twenty modules this record moves out are worth about **1.75 ms**
off that, paid once per shell on its first unknown command — `m_bixok` is
a flag on the process, not a file on disk. How often that happens depends
entirely on who is typing; the table is the cost when it does, not a
claim about how often.

**And the disk:**

| | |
|---|---|
| all 35 modules | 2055 kB |
| shipped (15, below) | 807 kB |
| published (20) | 1247 kB, **37%** of a 3353 kB package |

## Decision

**Everything hibr places has a scope, and the scope decides where it
lives, who may write it, and who it serves.** There are three, and the
desktop's scan order already had two of them.

| scope | written by | lives in | serves |
|---|---|---|---|
| **shipped** | the package, or `make install` | `$PREFIX/share/hibr/desktop`, `$PREFIX/lib/hibr` | everyone, and cannot be changed |
| **system** | an administrator, through the catalogue | `/var/lib/hibr` (see below) | every user of this machine, and it wins over theirs |
| **user** | you, through the catalogue | `$XDG_DATA_HOME/hibr` | you, where the machine has no copy |

Above all three sits `~/.config/hibr`, which is **yours, by hand** — the
folder you drop a script into, which has always come first and is not a
scope the catalogue writes.

**Precedence is `~/.config`, then system, then user, then shipped**, and
the middle pair is the decision worth stating: **an administrator's
install is authoritative and a person's own catalogue install does not
override it.**

The other order was written here first -- user above system, by analogy
with the existing config-then-data chain -- and it is wrong for the
machine this scope exists for. On a managed desktop it means somebody
who installed Mail last year goes on running last year's copy after the
administrator installs a current one, silently, for ever: their folder
is read first, so the update never reaches them and nothing says why.
The scope exists so an administrator can decide what the machine runs,
and an order that lets any account quietly opt out of that decision
does not deliver it.

What a person keeps is `~/.config/hibr/apps`, which stays above
everything. That is deliberate and is not the same thing: it holds a
file somebody **wrote**, not one a catalogue placed, it is where
patching an installed app has always happened (ADR 0041), and no update
overwrites it. So the escape hatch is a file of your own, not a second
copy of the administrator's.

Two things follow and both are built. A catalogue install into a
person's own scope of something the machine already has is **refused**,
whatever the versions say -- a newer copy in a folder nothing reads is
worth less than no copy at all. And a copy that was already there when
the administrator installed is reported as **defunct** rather than
merely shadowing, with the command that frees the space, since it is
the one way into that state and nothing else would say so.

**The shipped scope is as small as it can be, and that is what the
measurements are for.** It holds the shell, the window manager, the
desktop's own `system/` apps, the Control Panel panes that are nobody's
`part`, the Control Strip modules, the savers, the login screen, and
fifteen modules:

    archive auth console dav hcal hold img lang media
    prompt pty salat sys term uni

Read out of the tree rather than remembered, and four of them are there
for reasons worth writing down because each was missed on a first count:

- **`term`**, which nothing in the window manager names. `hold`'s own
  init calls `hibr_require` for `pty` and then `terminal`, and
  `dt_autohold` holds every desktop — so a session with no `term.so` is a
  desktop that cannot start, failing in `m_load`, several steps from
  anything that mentions a terminal.
- **`dav` and `archive`**, which are what the catalogue client itself
  needs. A fetcher that must be fetched is why "move everything" cannot
  be built.
- **`sys`**, which only `login/login.hibr` names — for `drop`, giving up
  root irreversibly ([ADR 0016](0016-privileges-are-dropped-never-gained.md)). A
  login screen that cannot drop privilege is not one to ship without.
- **`prompt`**, which has nothing to do with a desktop and is the whole
  point of the scope rule: hibr is a shell first, and the prompt is what
  hibr-as-a-shell *is*. Deciding it by "what the desktop needs" would
  have published it and made somebody with no desktop at all fetch their
  own prompt.

Everything else hibr builds — twenty modules, every application, theme,
colour scheme and language — is **published**, and travels under exactly
the rules anybody else's does: an index, a `sha256`, a signature, and
ADR 0044's refusal of an unverified module.

**A published module is published per architecture.** Nothing is ever
compiled on the machine it runs on: hibr's dependencies are libc and
libdl, and a C compiler is not one. So an entry of kind `mods` is not
one file but a matrix — `bundles/mods/<arch>/NAME.so`, with the index
naming which `(arch, abi)` pairs exist and the client choosing its own:

    "mods/mermaid": { "builds": {
        "linux-x86_64": { "bundle": …, "sha256": …, "abi": "16" },
        "darwin-arm64": { "bundle": …, "sha256": …, "abi": "16" } } }

Until 0.99.195 an entry carried a single `bundle` with one `arch` and
one `abi` beside it, so the catalogue could hold exactly one build of a
module and `ap_needs` refused every other machine by name. Honest, and
useless to anybody not on that machine — which is tolerable while a
module is somebody else's and fatal once twenty of hibr's own are
published this way.

The cost is the publisher's, and it is **per ABI bump, not per
release** — which is worth stating carefully, because the first draft of
this record got it wrong in the expensive direction and the owner caught
it.

A module is republished when its own source changed, or when
`HIBR_ABI` moved. It is **not** republished because the release number
moved: measured by bumping `HIBR_VER` from 0.99.194 to 0.99.199,
rebuilding, and comparing — `mermaid`, `csv` and `db` came out
byte-identical (`1285e640…`, `92340fdc…`, `aaf4868d…` before and after),
because nothing in any module references it. An **ABI** bump does change
every one of them, since `HIBR_MODULE_P` puts `HIBR_ABI` in the
descriptor struct itself: the same three went to `6f161616…`,
`d3c18781…`, `8dd1f3d8…`.

So the arithmetic is twenty modules times the architectures published,
**once every twenty releases or so** (13 ABI bumps in 275), plus
whichever individual modules a release actually changed. A typical
release republishes **none**, and `./stale` compares bytes, so it names
exactly which — nothing has to be remembered or assumed.

What this decision does still need is a build for each architecture — a
machine, a cross toolchain, or an emulator — and this project has none
of the three today (`aarch64-linux-gnu-gcc`, `zig` and the mingw
toolchain are all absent; only native `gcc`, `tcc` and `clang`). That,
rather than the republishing, is why publishing the modules is the last
step of the work.

An entry says what it **needs** and the catalogue resolves it: already
loadable → satisfied; in the catalogue as `mods/NAME` → fetched; neither
→ the app is refused by name and nothing is placed. A need that is
genuinely optional is already expressible and already used — About draws
its logo behind `if need sysinfo`, and degrades to no logo rather than to
no window.

## Where the system scope lives: `/var/lib/hibr`

The obvious home is `$PREFIX/share/hibr/desktop/apps` — the third scan
level, already root's, already where a root shell loads modules from. It
is wrong twice:

- **`make install` does `rm -rf $(DESTDIR)$(DESKDIR)`.** A
  system-installed application there is deleted by the next source
  install, silently, and the person who did it has no reason to connect
  the two.
- **`HIBR_MODDIR` is the package's.** dpkg owns every file in
  `/usr/lib/hibr`; a catalogue writing there puts two package managers in
  one directory, which is the thing this whole line exists to avoid.

So the system scope gets **a root the package never touches**, and it is
**`/var/lib/hibr`** — a compile-time default in the Makefile beside
`MODDIR`, so a distributor can move it, with the same `$(B)/.moddir`
stamp forcing a rebuild when it changes:

    /var/lib/hibr/apps/mail.hibr
    /var/lib/hibr/control-panel/mailset.hibr
    /var/lib/hibr/mods/pim.so
    /var/lib/hibr/catalogue.json

The FHS calls `/var/lib` the place for state a program maintains, which
is what a catalogue's store is. Two alternatives were weighed and
refused, each for a concrete reason rather than a taste:

- **`/usr/local/share/hibr` and `/usr/local/lib/hibr`** is the FHS place
  for software an administrator installed outside the distribution's
  package manager, and it is the first place a person would look — but
  `make install PREFIX=/usr/local` is an ordinary thing to do, and it
  does `rm -rf $(DESTDIR)$(DESKDIR)`. Choosing it would reintroduce
  exactly the trap this section exists to avoid, one directory along.
- **`/opt/hibr`** collides with nothing and nothing in hibr's build ever
  writes there, but `/opt` is conventionally one self-contained vendor
  tree rather than an overlay on an installation somewhere else: a shell
  in `/usr/bin` whose applications live in `/opt` reads as two
  installations.

The price of `/var/lib` is that it is not where somebody would think to
look, so `catalogue installed` has to say which scope each entry is in
rather than leaving it to be inferred from a path.

**Built in 0.99.197, and one thing in it is worth recording because it
was nearly two knobs.** A script needs to know where the system scope
is, and the module search needs to look there; writing `/var/lib/hibr`
into both is the parallel this record exists to refuse. So there is one
variable, `$HIBR_SYSDIR`: the shell sets it at startup from the
compiled value unless the environment already did, `catalogue --system`
reads it to know where to write, and `m_sysdir` reads it to know where
to look. What makes one variable safe for both is a single rule --
**the default is checked and a path you chose is yours**. The compiled
path is used only if it is root-owned and writable by nobody else; an
overridden one is trusted exactly as `$HIBR_MODPATH` already is, since
it affects only the shell whose environment holds it; and root never
reads the environment's at all, which is what keeps ADR 0016's
widening from being a way in.

The desktop's eight scan lists ask `dt_sysdir` for the folder rather
than writing the path out eight times, for the same reason.

## Consequences

**A release that moves `HIBR_ABI` must republish the catalogue's modules
in the same pass.** This is the price of publishing twenty of them and it
is not optional. `HIBR_ABI` has moved **13 times in 275 releases** — the
fourteenth `+#define` in the file's history is its own birth at ABI 3,
not a bump — and a module built against the old one is refused by
`m_load`. The index records the ABI and the architecture and declines a
mismatch by name (ADR 0044), so the failure says what it is rather than
crashing; to a person it still reads as *"Mail stopped opening after I
upgraded the shell"*. The rule belongs in the release ritual beside the
catalogue's own staleness check, and `./stale` is what answers "are they
current".

**A release republishes no module unless it changed one.** An earlier
draft of this record claimed the opposite — that every module relinks
every release because `HIBR_VER` is in a header they all include — and
it is false: `make` does relink them, and the bytes come out identical,
because no module references that macro. The measurement is above. The
rule that survives is narrow and cheap to keep: republish a module when
its source changed or when `HIBR_ABI` moved, and let `./stale` say which
by comparing bytes.

**ADR 0016 is widened, deliberately and visibly.** It says root loads
modules only from `HIBR_MODDIR`, because a writable directory on root's
module path is a way in. A system scope means root also loads from the
system module directory — which is defensible, since only root can write
it and ADR 0044 already refuses a module whose signature does not check
against the pinned key — but it is a widening and not a detail. The
system directory must be root-owned and refused if it is not, the way
`mods/dav` already refuses a servers file that is not `0600`.

**A system install must refuse an unsigned script, not warn.** ADR 0044
refuses an unverified *module* and warns on an unverified *script*,
because a script is something you can read before you run it. That
reasoning holds for a person installing into their own home and fails for
an administrator installing into everyone's: the people who will run it
are not the person who read it.

**State and `purge` are per scope.** `AP_ST` lives in the user's data
folder; the system scope needs its own, `catalogue installed` must say
which scope each entry is in, and a user's `purge` must never reach a
system one.

**The next cut on this same line is already broken, and this record
names it rather than leaving it to be rediscovered.** The package ships
31 Control Panel panes, and **nine of them are `part`s of applications it
no longer ships** — `filesview mailset notes pimset prayerset terminal
tube vaultset` and the strip's `nextprayer`. So after `catalogue install
mail` the Mail pane exists twice, identical bytes, in two scopes:
`/usr/share/hibr/desktop/control-panel/mailset.hibr` and
`~/.local/share/hibr/control-panel/mailset.hibr`. That is #220's "two
copies of one thing" one level down, and it is why the shipped `need` set
first came out as sixteen with `email` in it — a module the package only
appeared to need because it was carrying somebody else's pane. A pane
that is a published `part` is not shipped; `addapp` already knows which
those are, so the check is mechanical.

**The desktop itself is a catalogue entry, and this record said
otherwise.** The Decision above lists "the window manager, the desktop's
own `system/` apps, the Control Panel panes ... and fifteen modules" as
shipped, and the reason given was the bootstrap argument now refuted
under Alternatives. Read correctly, the rule puts almost none of that in
the package: `catalogue` needs `dav` and `archive`, the **shell** needs
`prompt` and `sys`, and every other module in that list of fifteen —
`console hold pty term img lang uni hcal salat media auth` — is wanted
by the *desktop* and by nothing else. They become what the desktop
entry `needs`, which is the mechanism this release already built.

What that buys is the thing the owner asked for and this record talked
itself out of: **one story.** hibr is a shell; everything above it comes
from the catalogue, by one route, with one vocabulary. A package of
about 1.6 MB instead of 3.4, and `catalogue install desktop` as the
first thing somebody types.

Four things have to be true and none is hard: the `desktop` launcher
must look where a fetched desktop lands rather than only at
`@DESKDIR@`; the desktop entry carries its modules as `needs`; the login
screen (`hibr-login@.service`, which runs as root) installs at **system**
scope, which is what that scope is for; and the per-architecture matrix
grows from twenty modules to about thirty, which is the real cost and
belongs in the same breath as the saving.

It is a release of its own, after the one this record was written for.
Nothing already shipped is reversed by it: a desktop that is in the
package today simply stops being there, the way the applications did in
0.99.194.

**What does not change:** ADR 0041's sentence, that an app is a script you
can read before you run it; ADR 0044's rule that a module is refused
outright from a catalogue that cannot be verified; and the fact that a
module in `$XDG_DATA_HOME/hibr/mods` is kept off the autoload index on
purpose, so a mistyped command can never load one. The system directory
is kept off it for the same reason.

## Alternatives refused

**Everything stays in the package.** What the project did, and what this
record's author recommended, on the argument that an unloaded module
costs nothing. The first table says that is true of startup; the second
says it is false of the autoload index, which is the measurement that
changed the recommendation. 1.75 ms per shell and 1247 kB on every
machine that will only ever open a terminal is a real price for keeping
twenty modules where they are not needed.

**Everything moves to the catalogue.** Refused here at first, on a
bootstrap argument that does not survive reading: *"the shipped content
needs fifteen modules, so a fresh install would have to reach the
catalogue to get what it needs to reach the catalogue."* That conflates
two different things. **The catalogue client is not the desktop.**
`catalogue` is a command, it needs `dav` and `archive` and nothing else,
and it runs in a terminal on a machine with no desktop at all. Nothing
about fetching requires the thing being fetched.

So the honest reading of this record's own rule — *what must be present
before anything can be fetched* — puts the **shell and its own modules**
in the shipped scope and the **desktop in the catalogue**, which is
where the owner put it when the argument above was shown to them. See
the Consequences: it is a later release's work, not a reversal of
anything already shipped.

**One scope, and let every user install their own.** What the owner's
second question refuses, rightly. On a machine where the desktop *is* the
system, every new account would fetch twenty-one applications over the
network before it had a menu — and an administrator who wants a known
desktop for everybody would have no way to say so.

**A second package per scope** — `hibr-desktop-apps` and friends.
Refused in 0.99.194 for a reason that applies here too: **apt installs a
new `Recommends:` on a fresh install and never on an upgrade**, so
anything expressed that way reaches new machines and silently skips every
existing one. A scope is a destination, not a package.

**A module as a `part` of the app that uses it.** A `part` is a file an
entry *owns and brings* — a pane, a strip module, a theme's picture — and
it is removed with its owner, refcounted so a pane two apps both bring
survives the removal of one. A module is owned by nobody: `pim` serves
Calendar, Contacts and anybody's script, and a shipped
`/usr/lib/hibr/pim.so` is not Contacts' to delete. Modelling it as a part
would mean an app removing native code out from under something it cannot
see. It is a different relation and it gets a different word: `needs`.

---

[← decision records](README.md) · [← documentation index](../README.md)
