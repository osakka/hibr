# 0046 — An installable reaches only what every scope has

Status: accepted (0.99.224, Gitea #262)

## Context

ADR 0045 gave every desktop file a scope: `~/.config/hibr`, the machine's
`$HIBR_SYSDIR`, the person's `$XDG_DATA_HOME`, and the shipped copy, read in
that order. The catalogue installs into the third. Nothing then asked what
an installed file *reaches* — and a file written for the shipped tree
reaches things that are only there in the shipped tree.

That is how the owner's desktop came up on

```text
hibr: source: /home/osakka/.local/share/hibr/apps/../lib/pim.hibr: No such file or directory
hibr: pe_onsync: command not found
```

Calendar found its library at `${BASH_SOURCE%/*}/../lib/pim.hibr`, which is
`programs/desktop/lib/pim.hibr` in the shipped tree and nothing at all once
the catalogue had put Calendar in `~/.local/share/hibr/apps/`. Because the
user scope is read before the shipped copy, **installing Calendar replaced
a working app with one that could not load.** The same desktop logged
1.78 million `pe_servers: command not found` lines, one per frame, from
Calendar's own ticker.

A survey of the tree, before anything was changed, found the same root in
more than the one file:

- **eleven paths built from a file's own location**, in Calendar, Contacts,
  Diagram, Write, Sheet, Mail, and the PIM, Mail and Appearance panes —
  Mail's sync job and the mail account test among them, which break
  *silently*, with no error, only no mail;
- **220 calls from one installable into another, 156 with no guard** in
  their function: every pane into the Control Panel; Mail into Calendar and
  Contacts; the wallpaper picker into the Files app; the Desktop pane into
  the Screenshots helper.

So the question was not how to ship `lib/` into a fourth place. It was:
**what may an installable file reach, so that it works in every scope?**

## Decision

**An installable file reaches only what every scope has.** Three things,
and nothing else:

1. **The platform.** The core (`wm/`, `widgets/`, `desktop.hibr`), the
   shared libraries, and any file in an installable folder that says of
   itself, in its header, `# platform:` — today the Control Panel, which
   every pane is built on, and the saver framework, which every saver is.
   A platform file is part of the desktop: the catalogue refuses to publish
   one, so it is never installed apart from the panes or savers it runs.
2. **Shipped files, through `$DT_HERE`** — the shipped desktop's own
   directory, which the core already used for this. Never through
   `${BASH_SOURCE%/*}/..`, which is right in exactly one scope.
3. **Another installable, only behind a guard in the same function** —
   `dt_has`, `dt_needs` or `command -v`. `dt_needs APP FN` is the loud
   form: when the other side is absent it says *"APP is not installed. Find
   it in the Catalogue."*, the same words `dt_launch` uses.

Libraries are the desktop's to load. `dt_libs` sources every file in the
shipped `lib/` that marks itself with a `*_LIBDONE=1` line, before any app,
and only from there — a library is not a kind and has one home, so a stray
copy in a person's folder cannot shadow the package's. The files in `lib/`
without the mark are programs the apps run as jobs, which act the moment
they are read and are never sourced. Each app keeps its own guarded source
line, so one sourced on its own in a fixture still works; in a desktop
that line is never reached. That is also what repairs **installs already
out there without touching their files**: the old line in an old copy is
the one that is skipped.

### Why links between apps are optional rather than declared

The alternative was a dependency edge — Mail `needs:` Contacts — and the
catalogue already has `needs` for modules. It was not extended to apps,
because an edge between installables needs a resolver, an install order,
and a refusal to remove anything something else names; and every one of
those is a new way for an install to fail. A guard needs none of them.
Mail without Calendar shows an invitation's actions under their own names;
the Appearance pane without Files says the wallpaper picker needs Files.
The desktop stays dynamic by construction: a person can install or remove
either side, and the other does less rather than breaking.

### Why not a mounted archive

Asked directly: would a tarball mounted through `/dev/archive` have been
more robust? No. A mounted Calendar resolves
`${BASH_SOURCE%/*}/../lib/pim.hibr` to `/dev/archive/calendar/../lib/pim.hibr`,
which is not in the bundle either; putting `pim.hibr` into every bundle
that uses it puts three copies of one library on a machine, and whichever
loads first wins. Residence changes where files sit, and nothing about
what they reach. ADR 0041's measured blocker for mounting also still
stands — `img`, `media` and `web` open their paths with `fopen` and cannot
see a scheme. Mounting is a separate question, recorded as its own ticket.

## Enforcement

Each rule has a check, and each check is derived from the tree, so a new
application, pane, strip module or saver is covered with no edit to a test:

- **`tests/deps.py`** reads every file of every script kind the catalogue
  knows (its own `CAT_KEXT`), and fails on a path built from a file's own
  location, a sourced library without the mark, and a call into another
  installable with no guard in its function. Against the tree before this
  decision it reports 167 failures; against the tree after, none.
- **`tests/scopes.py`** starts the desktop in every arrangement that can
  exist — the platform alone; as the package ships it, which is no
  applications; every installable alone in an empty data folder over the
  platform, and *used* (an application launched, a pane opened, a saver
  started), since a missing dependency bites in a callback and not while a
  file is read; and every installable over the shipped desktop, shadowing
  its copies, which is the arrangement the owner's desktop was in. Any
  shell error any of them prints fails it, naming the installable. Against
  the tree before this decision it fails on Calendar, Contacts, Diagram and
  Write alone, and on everything-over-shipped; after, 73 of 73.
- **`catalogue update` decides by the bytes**: an entry is current only if
  every file it placed matches the `sha256` the index gives it. It used to
  ask only whether the version had moved, so a part that changed under an
  unchanged app — a pane its app brings — reached nobody who had installed
  the app.

The two checks see different things, which is why both exist. Mail's sync
path was wrong too, and `scopes.py` cannot see it, because a scratch
desktop has no mail account and the sync never runs; `deps.py` sees it by
reading.

## Consequences

- An app that wants something from another app asks first, and says what
  is missing when it is. Writing `fb_scan` from a pane without a guard
  fails the gate.
- A file that other installables build on is either platform, and says so,
  or it is an installable whose callers guard it. There is no third kind.
- A library is loaded on every desktop whether or not an app that uses it
  is installed. Both libraries are definitions and settings with no side
  effect on load; `pim.hibr` was already loaded on every desktop through
  the PIM pane, which sources it.
- The wallpaper picker is built on the Files app's folder model, so it is
  guarded and says it needs Files. Rebasing it on the core file dialog, so
  it needs nothing, is its own piece of work.
