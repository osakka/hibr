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
| `apps install NAME...` | fetch, verify and place |
| `apps remove [-f] NAME...` | take one away; `-f` even if a window is open |
| `apps update [NAME...]` | everything with a newer version, or just these |
| `apps index [URL]` | the index this fetches from, or set it |
| `apps refresh` | fetch the index again now |
| `apps help` | the list above |

`apps list` marks what you already have, and says when the index has
something newer:

```text
clock            Clock                      1.2
notes            Sticky Notes               installed
weather          Weather                    installed 0.9, 1.1 available
```

## Where a file lands, and why yours wins

The desktop scans eight places, and each begins with a folder of **yours**
followed by a data folder the manager writes to:

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

An entry's `kind` decides which of the eight it goes to, and **the kind
decides the extension**: a theme is JSON, not a script
([ADR 0028](adr/0028-a-theme-is-data.md)), and a translation catalogue is
too ([ADR 0032](adr/0032-the-desktop-is-translated-at-its-widgets.md)).
A kind that is not one of the eight is refused rather than guessed at, and
a bundle whose own extension is not its kind's is not placed as itself —
it would be a file nothing ever reads.

## What it trusts, and what it does not

Three separate things, named separately because they protect different
things and because claiming more would be a lie:

| | what it establishes |
|---|---|
| **TLS** | the identity of the **server** the index came from. Certificates are verified by default; nothing here turns that off |
| **sha256** | the integrity of the **payload**. A bundle changed in transit or on a mirror is refused |
| **nothing** | the identity of the **author**. There are no signatures |

`apps info` says the third one in as many words rather than implying
otherwise. If you need to know who wrote an app, this cannot tell you.

## What it refuses

Each refused outright rather than half-done, and each names what it found:

- **a bundle carrying a `.so`** — native code has no sandbox here, and root
  loads modules only from the directory compiled in
  ([ADR 0016](adr/0016-privileges-are-dropped-never-gained.md));
- **a member whose path is absolute or climbs out of its own folder** —
  `../escaped.hibr` and `/etc/x` are both refused, naming the path;
- **a symbolic link**, which a bundle has no business carrying;
- **a part whose kind is not one of the eight, or whose name is anything
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

A bundle is either the app's single `.hibr` file, which is most of them, or a
`.tar.gz` carrying `app.json` beside the files:

```json
{"app": "multi.hibr", "kind": "apps"}
```

### An app that is more than one file

A Control Panel pane and a Control Strip module are files in **other** scan
roots, so they cannot travel as resources under the app's own: eight of the
applications hibr ships have a pane, and installing one without it leaves
the app with nowhere to keep its settings. `app.json` names those parts, and
each is placed in the root it names:

```json
{"app": "mail.hibr", "kind": "apps",
 "parts": [{"file": "mailset.hibr", "kind": "control-panel", "name": "mailset"}]}
```

A part names the **filename** it lands as, with `as`:

```json
{"app": "construction.json", "kind": "themes",
 "parts": [{"file": "shot.png", "kind": "themes", "as": "construction.png"}]}
```

which is what a theme with a picture needs — `cp_themeread`'s `image` reads
it from beside the theme, in the same kind under another extension. The
older `name` still means "with this kind's own extension", so a bundle
published before this installs unchanged. A part's `kind` must be one of
the eight and its own name a plain filename: a part decides where a file
lands, so the rules the member paths follow hold for the destination too,
and a bundle with a bad part is refused whole.

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

There is no window for this yet, and this page said there was for several
releases — an **Applications** window described down to how it would run
the command as a child, which nothing in `examples/desktop/system/` ever
did. It is Gitea #219, and
documentation that describes what was never built is worse than silence,
because it is the evidence somebody would trust.

When it exists it will be this command with a window around it, the way the
Vault accessory runs `vw`: anything it can do the command can do, and the
reverse, because there is one implementation.

---

[← documentation](README.md) ·
[the decision record](adr/0041-an-app-is-a-folder-of-files-a-bundle-is-how-it-travels.md)
