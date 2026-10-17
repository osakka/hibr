# 0047 — What the package ships is one list, and a part goes where its owner goes

Status: accepted (0.99.230, Gitea #271)

## Context

0.99.194 (Gitea #220) stopped the package shipping applications. It did
that by copying `programs/desktop/` whole and taking out `apps/`, and
nothing else. So everything that was not an application still shipped,
whatever it belonged to:

- **eight Control Panel panes of applications the package did not ship**
  -- Files, Terminal, Mail, Calendar & Contacts, Vault, Stickies, YouTube
  and Prayer Times -- each a row in a fresh desktop's Control Panel that
  said *"YouTube is not loaded"*, and Prayer Times' Control Strip module
  with them;
- **every theme and every colour scheme**: four themes, 2.1 MB, almost all
  of it three wallpaper pictures, and twelve schemes, all offered again by
  the catalogue.

The catalogue had the same confusion one step further on. An application
declares the files it brings -- `youtube.hibr` says
`# part: control-panel/tube.hibr` -- and the catalogue installs and removes
them together. But `mkindex` made every `.hibr` in `bundles/control-panel/`
an entry, and a part lands in that folder, so each app's pane was also an
entry of its own, installable with nothing behind it. `catalogue list`
marked all eight `shipped` on a machine with none of the apps.

The owner asked *"why would the YouTube control panel be installed without
the YouTube app?"*, then *"why do we also ship all the themes and all the
colours?"*, and then chose what the package should hold.

## Decision

**A file has two facts, and there is no third kind.** Its **kind** is
where it lands and which loader reads it: one of the nine. Its **owner** is
the entry that places it and takes it away. A **part** is a file an entry
owns that lands in another kind's folder, declared once, by the owner's
`# part:` line, or for a theme by its `image`. YouTube's pane is kind
`control-panel` and owned by `apps/youtube`; nothing about it is virtual.

**A part goes where its owner goes, and is never an entry of its own.** It
ships when its owner ships, and only then. `mkindex` drops any bundle some
entry claims as a part, so the catalogue offers YouTube's pane only with
YouTube. Two owners may bring one file (Calendar and Contacts both bring
`pimset`), and it ships if either does, which `cat_claimed` already
answers for removal.

**What the package ships is one list, `packaging/ships`**, of installables
by path. Shipping is the package's fact, not the installable's: a mark in
each file would have changed the bytes of some fifty catalogue entries,
every one of which would then need a new version and a republish for no
effect on anyone who installs it, and the list answers "what does the
package hold, and why that" in one place. The platform -- `wm/`,
`widgets/`, `lib/`, `login/` and every `# platform:` file -- always ships
and is not listed. The owner's choice, 2026-10-17:

- the desktop's own apps: About This Computer, Control Panel, Catalogue,
  Clipboard, Modules, Notifications, Screenshot and Task Manager;
- **Files, Terminal and Stickies; Calculator, Clock, Puzzle and Image
  Viewer** -- which reverses #220's "nothing you open";
- the desktop's own panes and the Control Strip's modules that belong to
  no app;
- **one screen saver, Mystify**, and **no language beyond English** --
  the other savers, Arabic and any language added later are in the
  catalogue (0.99.232: these two lines were the owner's second pass,
  after 0.99.230 had shipped every saver and Arabic);
- **the Classic theme and two colour schemes, Midnight and Paper**, the one
  light scheme;
- and every part of any of those.

**One tool decides, and everything asks it.** `tools/ships.hibr` reads the
list and the parts and prints what ships (or, `-n`, what does not). The
Makefile runs it with the shell it has just built, so a source install,
the Debian package, Homebrew and the AUR hold the same set without one of
them naming it; `tests/scopes.py` and `tools/docshots.py` build their "as
shipped" desktop from it instead of rules of their own (`k != "apps"`, and
a session that left `apps/` out). `make install SHIP=all` places
everything, for a machine that will never reach a catalogue, and
`make install-extras` places exactly what the default leaves out.

**Why shipping some applications again is not two copies of one thing.**
#220's reason was that shipping an app and publishing it is two copies
with nothing saying which is real. ADR 0045 answered that a week later
without anybody noticing it had: every copy has a scope, the person's
outranks the shipped one, `catalogue installed` says *shadows the one hibr
ships*, and `update` decides by the bytes. A shipped app that is also in
the catalogue is the same relationship the desktop's own `system/` apps
have had all along.

## Enforcement

`tools/ships.hibr -c` refuses: a listed path that does not exist or is not
an installable; a part listed on its own (it goes where its owner goes); a
part some file claims that does not exist; a shipped theme whose colour
scheme does not ship; and a pane whose `cp_pane` says `app` that no file
under `apps/` brings, or the other way round. That last one keeps the
Control Panel's Applications heading in step with the `# part:` lines,
which is two records of one fact held together by a check -- the way
callback arities and `dt_keep` already are. Deriving the heading at run
time from the loaded apps' `# part:` lines was weighed and not taken: the
panel suites load panes without their apps, so every app pane would
regroup there, and a live install would have to keep a map of claims
current through `dt_rescan`.

`tests/541-ships.t` holds the Makefile to the tool: the default install
against the list, file for file; `install-extras` against the rest; and
the two together against `SHIP=all`. It fails against the Makefile before
this change. `tests/appmgr.py` drives a ledger holding a pane installed on
its own through an index where it is only a part.

## Consequences

- A fresh desktop's Control Panel lists what exists: the panes of the
  apps that ship, and no row that says something is not loaded. `cp_absent`
  is still there for the case it was first written for, an app that is
  present but not loaded in this session.
- Thirteen entries leave the catalogue: `abouthibr`, `cliphist`,
  `filesview`, `mailset`, `notes`, `notifications`, `pimset`, `prayerset`,
  `taskmgr`, `terminal`, `tube` and `vaultset` from the panes, and
  `nextprayer` from the strip. Each still arrives with its app. Somebody
  who installed one on its own keeps it: from 0.99.230 `catalogue update`
  says which app it comes with now and goes on. **An older `catalogue`
  stops at it instead** ("no such app in the index"), which upgrading hibr
  or `catalogue remove` of that pane answers.
- The tour (`docs/desktop.md`) shows a desktop with an Applications menu,
  and installs Mail from the catalogue rather than Calculator and Files.
- A theme or colour scheme beyond the three is a catalogue install now,
  which is one click in the Catalogue window or `catalogue install
  themes/`.
