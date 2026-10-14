# Installing desktop applications

`apps` fetches, verifies and places desktop applications. It is installed as
a command of its own, the way `vw` is, so it works for somebody who never
starts the desktop.

The whole of it rests on one fact, which is why there is so little machinery
here: **an app is a folder of plain files, and installing one is placing a
file where the desktop already looks.** A hibr script in
`~/.config/hibr/apps` is an application; nothing registers, nothing compiles,
nothing restarts. A bundle is only how it travels. The reasoning is
[ADR 0041](adr/0041-an-app-is-a-folder-of-files-a-bundle-is-how-it-travels.md),
which is this program's specification.

## The catalogue

`apps` fetches a catalogue — an **index** — over HTTPS, and defaults to the
one at <https://osakka.github.io/hibr-apps>: twenty-one applications, each a
hibr script, each with the description its own `app` line declares. It is a
git repository ([osakka/hibr-apps](https://github.com/osakka/hibr-apps))
served as a page, so it can be read in a browser before anything is fetched,
and a bundle's address in it is relative — which means any copy of that one
folder is a catalogue:

<!-- not run: fetches an index over HTTPS -->
```sh
apps list
apps index https://your.host/hibr-apps/index.json
```

`apps index` with no argument says which address is in force. An `http://`
address is accepted with a warning, because nothing then says which server
answered; anything that is not an address is refused.

## The commands

| | |
|---|---|
| `apps list [TEXT]` | what the index offers, or what matches TEXT |
| `apps installed` | what is installed here, and from where |
| `apps info NAME` | everything the index says about one |
| `apps show NAME` | fetch it, verify it, and print it — without placing it |
| `apps install mods/NAME` | a module, into `$XDG_DATA_HOME/hibr/mods` |
| `apps install NAME...` | fetch, verify and place |
| `apps remove [-f] NAME...` | take one away; `-f` even if a window is open |
| `apps update [NAME...]` | everything with a newer version, or just these |
| `apps index [URL]` | the index this fetches from, or set it |
| `apps refresh` | fetch the index again now |
| `apps help` | the list above |

`-j` on `list`, `installed` or `info` answers JSON instead of columns, for
anything that is not a person — the Applications window below reads that,
so the window and the command read one answer instead of one parsing the
other's layout. `apps show` is the thing a catalogue of **scripts** can
offer and a package manager cannot: read an app before you run it, which
is what an app being a file you can open is for. It verifies the bundle's
`sha256` exactly as an install does, and places nothing.

`apps list` marks what you already have, and says when the index has
something newer:

```text
clock            Clock                      1.2
notes            Sticky Notes               installed
weather          Weather                    installed 0.9, 1.1 available
```

## Where a file lands, and why yours wins

Eight of the nine kinds are places the **desktop** scans, and each begins
with a folder of **yours** followed by a data folder the manager writes to:

| what it is | your folder | a file is |
|---|---|---|
| applications | `~/.config/hibr/apps` | `.hibr` |
| the desktop's own apps | `~/.config/hibr/system` | `.hibr` |
| Control Panel panes | `~/.config/hibr/control-panel` | `.hibr` |
| Control Strip modules | `~/.config/hibr/control-strip` | `.hibr` |
| screen savers | `~/.config/hibr/savers` | `.hibr` |
| themes | `~/.config/hibr/themes` | `.json` |
| colour schemes | `~/.config/hibr/colours` | `.json` |
| languages | `~/.config/hibr/lang` | `.json` |

Your folder is scanned **first**, so a file you wrote shadows a fetched app
of the same name, and `apps update` cannot overwrite your copy. That is the
whole protection: there is no lock and no database, only the order of two
directories.

The ninth is `mods`, and it belongs to the **shell** rather than the
desktop: a module lands in `$XDG_DATA_HOME/hibr/mods`, which a non-root
shell searches when something asks for a module by name — `need NAME`,
`mod load NAME`, `mod avail`. It is the one kind that is native code, so
three things differ for it, each a refusal rather than a note: it is not
installed at all from a catalogue whose signature does not check out, and
its `arch` and `abi` are compared with this machine's before the bundle is
fetched. A module installed there is never loaded by a *mistyped command*,
which is deliberate and is the subject of
[ADR 0044](adr/0044-a-module-can-be-installed.md).

An entry's `kind` decides which of the nine it goes to, and **the kind
decides the extension**: a theme is JSON, not a script
([ADR 0028](adr/0028-a-theme-is-data.md)), a translation catalogue is too
([ADR 0032](adr/0032-the-desktop-is-translated-at-its-widgets.md)), and a
module is a `.so`. A kind that is not one of the nine is refused rather
than guessed at, and a bundle whose own extension is not its kind's is not
placed as itself — it would be a file nothing ever reads.

## What it trusts, and what it does not

Three separate things, named separately because they protect different
things and because claiming more would be a lie:

| | what it establishes |
|---|---|
| **TLS** | the identity of the **server** the index came from. Certificates are verified by default; nothing here turns that off |
| **sha256** | the integrity of the **payload**. A bundle changed in transit or on a mirror is refused |
| **a signature on the index** | the identity of the **publisher**. A detached OpenPGP signature over `index.json`, by the same key the apt repository uses — and since the index carries every bundle's `sha256`, one signature covers every bundle |

`apps info` says which of these hold for the catalogue in force, including
when the third does not:

<!-- not run: fetches an index over HTTPS -->
```sh
apps info snake
```

```text
signed    yes, by A1E3CA788A8FB02052DA22B413035BE1B1674431
```

What a verified signature says is that this is the catalogue that key
publishes. It does not say the app is good, and nothing here does.

A **script** from a catalogue that cannot be verified is installed with a
warning, because you can read the file and the `sha256` says you are
reading what the publisher hashed. A **module** is refused: there is
nothing to read and the object runs as you. A machine with no `gpg` on it
is the same case — scripts unaffected, modules refused.

`HIBR_APPS_KEYFP` pins a different fingerprint, for somebody serving a
catalogue of their own; the keyring `apps` reads is
`$PREFIX/share/hibr/keyring.gpg`, installed beside the shell, and
`HIBR_APPS_KEYRING` points elsewhere.

## What it refuses

Each refused outright rather than half-done, and each names what it found:

- **a bundle carrying a `.so`**, unless it is a bundle of kind `mods` —
  native code has no sandbox here, and root loads modules only from the
  directory compiled in
  ([ADR 0016](adr/0016-privileges-are-dropped-never-gained.md));
- **a module from a catalogue whose signature does not check out**, and
  **a module built for another machine or another module ABI**, both
  before the bundle is fetched
  ([ADR 0044](adr/0044-a-module-can-be-installed.md));
- **a member whose path is absolute or climbs out of its own folder** —
  `../escaped.hibr` and `/etc/x` are both refused, naming the path;
- **a symbolic link**, which a bundle has no business carrying;
- **a part whose kind is not one of the nine, or whose name is anything
  but a plain filename** — a part decides a destination;
- **an entry whose own name is not a plain filename** — the name decides
  where the file lands, so `../../../climbed` is refused, and that one
  entry is left out of the list rather than the catalogue being abandoned;
- **an app whose `abi`, `hibr` or `modules` this hibr cannot satisfy**,
  before the bundle is fetched rather than after it is placed;
- **removing an app with a window open**, which it learns by asking the
  running desktop over its control socket. `-f` overrides that.

The archive is inspected before anything is unpacked: `archive` presents a
tarball as a folder, so the check happens on a path built by walking it
rather than on a line of listing output.

## Publishing an app

An index is one JSON document. Each entry is an **id** — `kind/name` — and
what is needed to fetch and place it:

```json
{"entries": {
  "apps/clock": {
    "title":   "Clock",
    "version": "1.2",
    "about":   "a clock for the desk",
    "author":  "someone",
    "licence": "MIT",
    "kind":    "apps",
    "name":    "clock",
    "bundle":  "bundles/clock.hibr",
    "sha256":  "f1e2…"
  }
}}
```

`bundle` may be a full address or, as here, a path **relative to the index's
own** — which is what lets a mirror be a copy of one folder rather than a
rewrite of every entry.

### Why an id and not a name

The desktop has **eight** scan roots, and a name only has to be unique
inside one of them: `clock` is an application *and* a screen saver, and
both are installable. So an entry is identified by `kind/name`, and `name`
is what the file is called where it lands — a destination is never built
from an id.

`apps install clock` still works where a name means one thing. Where it
means two, it is **refused and both are named**, because whichever the
index happened to hold is not an answer:

```text
apps: clock: the catalogue has more than one -- apps/clock, savers/clock
Name the one you mean.
```

An index may also carry an `apps` section keyed by name, which is what an
`apps` older than 0.99.184 reads: it then gets every entry whose name is
unique and misses only the ones it could not have named. hibr's own
catalogue publishes both.

`hibr`, `abi` and `modules` say what an entry needs, and are **checked
before anything is fetched**: an exact module ABI, a minimum hibr version,
and a module that must already be there. A module never comes from here —
see [What it refuses](#what-it-refuses).

A bundle is a **plain file** — the app's own `.hibr`, a theme's `.json` —
at a path the index names. `version` is the entry's own, declared by the
thing itself and not the hibr release it last changed in.

### An app that is more than one file

A Control Panel pane and a Control Strip module are files in **other** scan
roots, so they cannot travel as resources under the app's own: eight of the
applications hibr ships have a pane, and installing one without it leaves
the app with nowhere to keep its settings. The index names those parts, each
with the path to fetch it from and its own `sha256`:

```json
"apps/mail": {
  "bundle": "bundles/apps/mail.hibr", "sha256": "…", "version": "1.0",
  "parts": [{"kind": "control-panel", "as": "mailset.hibr",
             "path": "bundles/control-panel/mailset.hibr", "sha256": "…"}]
}
```

`as` is the **filename it lands as**, which is what a theme with a picture
needs — `cp_themeread`'s `image` reads it from beside the theme, in the same
kind under another extension:

```json
"themes/construction": {
  "bundle": "bundles/themes/construction.json", "sha256": "…",
  "parts": [{"kind": "themes", "as": "construction.png",
             "path": "bundles/themes/construction.png", "sha256": "…"}]
}
```

A part's `kind` must be one of the eight and its own name a plain filename:
a part decides where a file lands, so the rules a path follows hold for the
destination too. **Every part is fetched and verified before any is
placed**, so an entry with one bad part installs nothing at all — not even
its own file, which by then has been fetched and checked.

There were tarballs here until 0.99.189: a `.tar.gz` carrying an `app.json`
that said which member was the app and where each part went. That is a
manifest of files, which is what an index is, and it put a binary in a
repository that nothing could diff. A catalogue that still serves one is
still read, so an older bundle installs unchanged.

The state records **every path placed**, not only the app's own, because a
folder on disk cannot say what it came with; `apps remove` takes those away
and keeps any that another installed app brings too. One pane serves
Calendar and Contacts both, so removing one of the two leaves the other's
pane where it is, and says so.

In hibr's own tree an app declares its parts in its own file, under the
`app` line that gives its description, which is what the publisher reads:

```text
app mail "Email, offline, labels and conversations as Gmail has them"
# part: control-panel/mailset.hibr
```

What an app may then do is a contract the desktop already had — menus, an
About box, a Control Panel pane, a Control Strip module, notifications, a
screen saver, a window of any shape — and is described in
[the desktop's README](../examples/desktop/README.md).

## From the desktop

**Applications Catalogue**, on the hibr menu, is this command with a
window around it — `examples/desktop/system/appstore.hibr`, the way the
Vault accessory is a window around `vw`. Anything one can do the other
can, because there is one implementation: the window runs `apps` as a
child and never has a catalogue of its own. It is not called
*Applications*: that is already the submenu of every app you can open, and
two rows of one name on one menu is a menu nobody can learn.

```text
┌─┤ Applications Catalogue ├────────────────────────────────────────┤_ □ x├┐
│ Search                                               5 in the catalogue  │
│ ITEM                            VERSION│ Snake                           │
│▼ Applications                        3 │ apps/snake                      │
│   ✓ snake                      1.0     │ installed 1.0                   │
│     retro_car                  1.2     │                                 │
│   ↻ mines                      1.1     │ A snake that grows as it eats,  │
│→ Control Strip modules               1 │ on a board of your window's own │
│→ Themes                              1 │ size.                           │
│                                        │                                 │
│                                        │                                 │
│                                        │                                 │
│                                        │                                 │
│                                        │                                 │
│                                        │                                 │
│  Remove ▄  Read It ▄  Refresh ▄                                          │
│  ▀▀▀▀▀▀▀▀  ▀▀▀▀▀▀▀▀▀  ▀▀▀▀▀▀▀▀▀                                          │
└──────────────────────────────────────────────────────────────────────────◢
```

The list is a **tree**, one branch per kind, each saying how many it holds
against the list's own right edge — a branch has no version, and a count
under that heading would read as one. `right` and `left` open and shut a
branch, `enter` and space toggle one, and a click on a heading does the
same. A branch with nothing in it is left out rather than drawn empty.
A tick marks an entry you already have and `↻` one with a newer version
waiting; the pane on the right is what the selected entry says about
itself, paragraphs kept.

`tab` or `/` moves to the search box and `tab`, `enter` or `escape` comes
back. Typing narrows the list as you type, against an entry's id, its title
and its prose, and the count beside the box says how many **entries**
matched — not how many rows are drawn, which would count the headings too.
A branch with a match in it opens itself.

| key | what it does |
|---|---|
| `i` | install the selected entry |
| `u` | update it |
| `x` | remove it |
| `r` | read its page in a window |
| `f5` | fetch the index again |

The same four are on the **App** menu, and the buttons along the bottom
change with what is selected: Install for an entry that is not here yet,
Update and Remove for one that is, Refresh always.

Every fetch is a **job**. `apps install` is seconds of network, and a
desktop that stops drawing while it waits is a desktop that looks broken,
so the command runs in the background with its output in a file of its own
and the next frame picks up the status — the shape `wm/remote.hibr` uses
for transfers. It is polled from the window's `_draw` *and* its `_idle`,
because a window nobody is looking at still has a job running in it. The
status line says what is happening while one runs, and what the command
said when it failed.

What the window reads is `apps list -j`, never the columns `apps` prints
for a person. The command answers JSON for exactly this, so the two read
one answer instead of one parsing the other's layout — which is how a
window comes to disagree with its own command. An `apps` too old to know
`-j` is named as the likely cause rather than drawn as an empty list with
no reason.

---

[← documentation](README.md) ·
[the decision record](adr/0041-an-app-is-a-folder-of-files-a-bundle-is-how-it-travels.md)
