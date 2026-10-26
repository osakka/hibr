# 0048 — A menu bar item is a file, and a bundle is a list of entries

Status: accepted (0.99.351, Gitea #392)

## Context

0.99.349 (Gitea #388) took Network, Sound, Bluetooth and Power out of the
package. Each became a catalogue entry: the pane, with its bar item and
the watcher behind the item carried as a `# part:` in `control-panel/`.
That worked, and it put the wrong thing in charge. The speaker on the menu
bar is useful with no pane at all, and the part relation said it could
not exist without one. Nothing on the bar was a thing of its own either:
the clock, the notification icon, the workspace numbers and the
application menu were drawn by `dt_bar` itself, at positions it worked
out in one function, with their clicks decided in another
(`wm/input.hibr`) and a third (`wm/notify.hibr`) reading where the icon
had landed.

The owner asked for two things together: a catalogue kind for menu bar
items, with each item on the bar today a script of its own, and a kind
that installs several entries at once.

## Decision

**A menu bar item is a file of `menubar/`,** scanned in every scope like
every other kind (ADR 0045) and placed by one layout. That includes the
desktop's own four, which say `# platform:` and are loaded by
`desktop.hibr`, so a desktop with no session still has its bar. Each pins
itself to the place it has always had (`dt_baripin`), which is why the bar
is the same cell for cell as before. Only an unpinned item is offered by
Control Panel > Appearance > Item Order. An item whose text follows the
desktop's state keeps it in a hook run before the bar's gate
(`dt_barhook`), calling `dt_baritem` only when it changed. That is a
per-frame call, which the bar refuses to an app's text, and it is
allowed here because each of the four reads a value that is already
cached.

**An entry may need another entry,** `# needs: menubar/sound`, which the
catalogue installs before it. This is different from a part. A part is
owned by its entry and removed with it; what an entry needs is an entry
of its own, installable alone.

**A bundle is a list of entries and nothing else** -- no file to place,
no hash, and, by the owner's choice, **no version**. It is current when
its members are. The ledger remembers which members a bundle came with,
so an update installs a member added since and does not put back one
taken away by hand. A bare name a bundle shares with its members means
the bundle. Removing one asks first on a terminal and refuses without
one, naming `-y` and `-k`, as `purge` does. The Catalogue window shows
bundles in a section of their own (the owner's choice), above the rest.

Sound, Network, Bluetooth and Power are each a bundle of `menubar/<x>`
and the pane, the pane needing the item.

## Consequences

- An install from 0.99.349 updates itself. The pane at 3.0 needs the
  menu bar item, which `update` installs, and the old watcher part is
  taken away because the new version no longer places it. The result
  reads as the bundle without the ledger ever recording one. The
  catalogue suite runs this migration rather than reasoning about it.
- An older catalogue meets the new index without failing. It lists the
  new kinds, refuses to install them by kind, and refuses a pane at 3.0
  by the hibr version it needs. A bare `sound` is several things to it,
  and it says so.
- In the source tree every test desktop would otherwise start the
  hardware watchers. Loading only the `# platform:` items from the window
  manager, and the rest from the session, keeps them where #388 had them:
  in a desktop that loads its session.
- A bundle with no version cannot be told apart from itself by `update`
  except by its member list. A change in what a member *is* reaches
  people through the member's own version, as it always did.

## Alternatives

- **Keep the watchers as parts of the panes.** Rejected: it makes the
  pane the owner of something that stands on its own.
- **Give a bundle a version** -- recommended, and declined by the owner.
  A version would let an older client see that a bundle changed. The
  member list does that job here.
- **Leave the clock, the notification icon, the workspaces and the
  application menu in `dt_bar`,** splitting them out later. That was
  recommended as the smaller step, and declined. Doing all eight at once
  meant one layout instead of two, and four readers of fixed positions
  became one reader of the items' own.
