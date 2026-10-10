# Installing desktop applications

`apps` fetches, verifies and places desktop applications. It is installed as
a command of its own, the way `vw` is, so it works for somebody who never
starts the desktop; the desktop's own **Applications** window runs this as a
child.

The whole of it rests on one fact, which is why there is so little machinery
here: **an app is a folder of plain files, and installing one is placing a
file where the desktop already looks.** A hibr script in
`~/.config/hibr/apps` is an application; nothing registers, nothing compiles,
nothing restarts. A bundle is only how it travels. The reasoning is
[ADR 0041](adr/0041-an-app-is-a-folder-of-files-a-bundle-is-how-it-travels.md),
which is this program's specification.

## Before anything: there is no public index yet

`apps` fetches a catalogue — an **index** — over HTTPS. The address it
defaults to is not published at the time of writing: it answers 404, so a
fresh install has nothing to list. Point it at one of your own and everything
below works:

<!-- not run: needs an index over HTTPS, and the default one is not published -->
```sh
apps index https://example.invalid/hibr-apps/index.json
apps list
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

The desktop scans five places, and each begins with a folder of **yours**
followed by a data folder the manager writes to:

| what it is | your folder |
|---|---|
| applications | `~/.config/hibr/apps` |
| the desktop's own apps | `~/.config/hibr/system` |
| Control Panel panes | `~/.config/hibr/control-panel` |
| Control Strip modules | `~/.config/hibr/control-strip` |
| screen savers | `~/.config/hibr/savers` |

Your folder is scanned **first**, so a file you wrote shadows a fetched app
of the same name, and `apps update` cannot overwrite your copy. That is the
whole protection: there is no lock and no database, only the order of two
directories.

An app's `kind` in the index decides which of the five it goes to. A kind
that is not one of them is refused rather than guessed at.

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
- **removing an app with a window open**, which it learns by asking the
  running desktop over its control socket. `-f` overrides that.

The archive is inspected before anything is unpacked: `archive` presents a
tarball as a folder, so the check happens on a path built by walking it
rather than on a line of listing output.

## Publishing an app

An index is one JSON document. Each entry is a name and what is needed to
fetch and place it:

```json
{"apps": {
  "clock": {
    "title":   "Clock",
    "version": "1.2",
    "about":   "a clock for the desk",
    "author":  "someone",
    "licence": "MIT",
    "kind":    "apps",
    "bundle":  "bundles/clock.hibr",
    "sha256":  "f1e2…"
  }
}}
```

`bundle` may be a full address or, as here, a path **relative to the index's
own** — which is what lets a mirror be a copy of one folder rather than a
rewrite of every entry.

A bundle is either the app's single `.hibr` file, which is most of them, or a
`.tar.gz` carrying `app.json` beside the files:

```json
{"app": "multi.hibr", "kind": "apps"}
```

What an app may then do is a contract the desktop already had — menus, an
About box, a Control Panel pane, a Control Strip module, notifications, a
screen saver, a window of any shape — and is described in
[the desktop's README](../examples/desktop/README.md).

## From the desktop

The **Applications** window is this command with a window around it, the way
the Vault accessory runs `vw`. Anything it can do, the command can do, and
the reverse — there is one implementation.

---

[← documentation](README.md) ·
[the decision record](adr/0041-an-app-is-a-folder-of-files-a-bundle-is-how-it-travels.md)
