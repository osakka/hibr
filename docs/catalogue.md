# The catalogue

`catalogue` fetches, verifies and places what the desktop runs. It is
installed as a command of its own, the way `vw` is, so it works for somebody who never
starts the desktop.

Since 0.99.194 it is also the **only** route (Gitea #220): a hibr install
is the window manager and what manages it, and no applications at all --
not Terminal and not Files. One command puts them back:

<!-- not run: fetches bundles over HTTPS -->
```sh
catalogue install apps/
```

A kind with a trailing slash is every entry of that kind, so
`catalogue install themes/` is every theme and `catalogue install apps/` is a
desktop. The nine kinds are the scan roots the desktop reads: `apps`,
`system`, `control-panel`, `control-strip`, `savers`, `themes`,
`colours`, `lang` and `mods`.

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
catalogue list
catalogue index https://your.host/hibr-apps/index.json
```

`catalogue index` with no argument says which address is in force. An `http://`
address is accepted with a warning, because nothing then says which server
answered; anything that is not an address is refused.

## The commands

| | |
|---|---|
| `catalogue list [TEXT]` | what the index offers, or what matches TEXT |
| `catalogue installed` | what is installed here, from where, and what each brought with it |
| `catalogue owns PATH` | what a file is and who put it there |
| `catalogue check [--repair]` | the record against the files, both ways |
| `catalogue info NAME` | everything the index says about one |
| `catalogue show NAME` | fetch it, verify it, and print it — without placing it |
| `catalogue install mods/NAME` | a module, into `$XDG_DATA_HOME/hibr/mods` |
| `catalogue install NAME...` | fetch, verify and place |
| `catalogue install KIND/` | every one of a kind -- `catalogue install apps/` is a whole desktop |
| `catalogue remove [-f] NAME...` | take one away; `-f` even if a window is open |
| `catalogue purge [-y]` | take away modules nothing needs any more; `--dry-run` lists them |
| `catalogue update [NAME...]` | everything with a newer version, or just these |
| `catalogue index [URL]` | the index this fetches from, or set it |
| `catalogue refresh` | fetch the index again now |
| `catalogue help` | the list above |

`-j` on `list`, `installed` or `info` answers JSON instead of columns, for
anything that is not a person — the Catalogue window below reads that,
so the window and the command read one answer instead of one parsing the
other's layout. `list -j` carries the **kinds** beside the entries, in the
order this program declares them, so a window grouping by kind keeps no
list of its own: the Catalogue kept a second copy until 0.99.194 and it
was three releases behind, which is why a module could be installed from
here and not seen there. `catalogue show` is the thing a catalogue of **scripts** can
offer and a package manager cannot: read an app before you run it, which
is what an app being a file you can open is for. It verifies the bundle's
`sha256` exactly as an install does, and places nothing.

A module built for another machine is **not listed**: the shell knows
what it is running on, so offering a choice it could never load is
asking a person to do arithmetic the program has already done. `apps
info NAME` still answers for one, and says which machine it is for —
somebody asking by name is asking a different question.

`catalogue list` marks what you already have, and says when the index has
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
of the same name, and `catalogue update` cannot overwrite your copy. That is the
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

`catalogue info` says which of these hold for the catalogue in force, including
when the third does not:

<!-- not run: fetches an index over HTTPS -->
```sh
catalogue info snake
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

`catalogue install clock` still works where a name means one thing. Where it
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

### Where an entry lands

An application in a folder of hibr's own tree is published into the same
folder and installed into it again, so a fetched desktop has the
submenus a shipped one has — `Accessories`, `Office`, `Internet`,
`Games` — instead of twenty-one applications in one list:

```text
bundles/apps/Internet/mail.hibr   ->  dir: "Internet"
                                  ->  ~/.local/share/hibr/apps/Internet/mail.hibr
```

`addapp` takes the folder from where the file lives, `mkindex` reads it
back out of the bundle's path exactly as it already reads the `kind`, so
the two cannot disagree, and `dt_apps` turns it into a submenu because
it already recursed. Only `apps` has folders; every other scan root is
read flat, and a **part** never carries one — a Control Panel pane is
found by one flat scan, so a folder under `apps` is the app's alone.

A `dir` decides a destination, so it is checked rather than trusted: a
segment is letters, digits, dashes, underscores and spaces, which is
what makes `Desk Accessories` a folder and `../../etc` not one. An index
that says otherwise has the folder **dropped**, not the install refused —
the entry is still a perfectly good app, and the worst that happens is
the flat placement everything had before this existed.

Installing over a version that placed a file somewhere else takes the
old one away (*"took away apps/snake.hibr, which this version no longer
places"*), so an app that moves into a folder leaves no copy behind on
the scan path. A path another installed entry also brings is kept, by
the same count `catalogue remove` asks.

### One copy for everybody

`--system` installs into the machine's own scope instead of yours — one
copy every user of the machine reads, which is what an image build or a
shared workstation wants:

<!-- not run: writes outside the home directory and needs root -->
```sh
sudo catalogue --system install apps/term apps/files system/panel
```

The `sudo` is not optional and the refusal says so before anything is
fetched, rather than letting `mkdir` complain from the middle of the work:

```text
catalogue: catalogue install --system writes /var/lib/hibr, which is root's.
That is deliberate: it is root-owned so every user of this machine reads the
same copy and nobody else can change what they all run (ADR 0045). Run it as
root instead:

  sudo catalogue install --system ...
```

**Reading that scope needs nothing.** `catalogue --system list` and
`catalogue --system installed` answer for any account, which is what you
run before reaching for root; only the verbs that write ask.

There are three scopes and they are read in this order
([ADR 0045](adr/0045-a-thing-has-a-scope.md)):

| read | | | |
|---|---|---|---|
| 1st | **yours, by hand** | `~/.config/hibr` | you write the file; no catalogue touches it |
| 2nd | **the machine's** | `$HIBR_SYSDIR`, `/var/lib/hibr` by default | `catalogue --system install`, root only |
| 3rd | **yours** | `$XDG_DATA_HOME/hibr` | `catalogue install` |
| 4th | **shipped** | `$PREFIX/share/hibr/desktop` | the package |

**An administrator's install is authoritative**: it is read before your
own, so a copy of yours cannot override it. That is the point of the
scope — on a managed machine the administrator decides what runs, and
an order that let any account quietly opt out would not deliver that.

Two things follow. Installing something into your own scope that the
machine already has is **refused**, whatever the versions say: a newer
copy in a folder nothing reads is worth less than no copy at all. And
a copy that was already yours when the administrator installed theirs
is reported as **defunct**, with the command that frees the space:

```text
apps/snake       1.0        apps           user    defunct: the machine has this
1 of these is defunct: the machine has its own, which is read first --
`catalogue remove NAME` frees the space
```

What you keep is `~/.config/hibr`, which stays above everything — a
file you **wrote**, not a copy of somebody else's, which is where
patching an installed app has always happened and which no update
overwrites.

`/var/lib` is the FHS place for state a program maintains, and it is
deliberately not under the install prefix: `make install` does
`rm -rf $(DESTDIR)$(DESKDIR)`, so a system scope there would be deleted
by the next source install, and `$(MODDIR)` is the package's, so a
catalogue writing into it would put two package managers in one
directory. The price is that it is not where anybody would think to
look, which is why `catalogue installed` names the scope rather than
leaving it to be read off a path.

Two things differ at system scope, both because the people who will run
what you install are not the person who read it:

- **An unsigned catalogue is refused**, where for your own scope it is a
  warning. A script you can read before you run it is ADR 0041's whole
  argument, and it does not survive installing into everybody's account.
- **A module's directory must be root-owned and private**, or the shell
  does not treat it as a system scope at all. The rule is *the default
  is checked and a path you chose is yours*: `$HIBR_SYSDIR` set by you
  affects only your own shell, the way `$HIBR_MODPATH` already does, and
  root never reads it.

`catalogue --system purge` reads the system scope only, and says so — it
cannot see what any individual has installed in their own home.

### What an entry needs

A bundle's own header says it, beside the `# part:` lines that say what
it brings:

```text
app contacts "The people in your address books, kept offline from CardDAV" 1.1
# part: control-panel/pimset.hibr
# modules: dav db pim
```

A **part** and a **module** are different relations, which is why they
are different words. A part is a file the entry *owns and brings* — a
Control Panel pane, a Control Strip module, a theme's picture —
published as a file of its own and removed with its owner, refcounted so
a pane two apps both bring survives the removal of one. A module is
owned by nobody: `pim` serves Calendar, Contacts and anybody's script,
and one that came with hibr is not an app's to delete.

`mkindex` reads that line into the index's `modules` field, and `apps
install` resolves it three ways, in order:

| | |
|---|---|
| already loadable | satisfied — this covers everything hibr ships, and `need` resolves an interface as well as a module name |
| in this catalogue as `mods/NAME` | fetched and placed **before** the thing that needs it, so an app is never on disk in a state where its first `need` fails |
| neither | the app is refused by name and **nothing is placed** — the check runs before a byte is fetched |

The state records which app a module was fetched for, so nothing has to
guess later whether anybody asked for it by name.

### Taking a module away again

Removing an app says **nothing** about its modules. `pim` serves
Calendar, Contacts and anybody's script, and the count this program
keeps can only see what it installed itself — a script of your own that
calls `need pim` is invisible to it. Deleting native code behind that
script's back is a worse failure than leaving a megabyte on disk, so it
is a verb you ask for rather than a step inside `remove`:

<!-- not run: reads an index over HTTPS and removes files -->
```sh
catalogue purge --dry-run
```

```text
fetched for something that is gone, and nothing installed needs now:
  mods/mermaid           was needed by apps/diagram
```

`-y` removes them without asking. A module you installed **by name** is
never listed, however little needs it: the state marks only what was
pulled in for something else. And run from a desktop window there is no
terminal to answer on, so it refuses and names the flag rather than
hanging on a read that can never return.

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
folder on disk cannot say what it came with; `catalogue remove` takes those away
and keeps any that another installed app brings too. One pane serves
Calendar and Contacts both, so removing one of the two leaves the other's
pane where it is, and says so.

### Where a file came from

Three things can put a file in a scope, and they are undone differently:

- **you asked for it** — an entry, removed by name;
- **an app brought it** — a `part`, removed with its owner, and refcounted
  so one two apps both bring outlives the removal of either;
- **something needed it** — a module fetched for a `needs`, which only
  `purge` takes away.

So `catalogue installed` says what each entry brought, and ends with the
two numbers that let you check a folder against it — twenty-one entries
put thirty files in a scope, and the nine that are parts are the
difference:

<!-- not run: reads the state of whatever machine it runs on -->
```sh
catalogue installed
```

```text
apps/files       1.1        apps           user    +1 part
apps/mail        1.1        apps           user    +1 part
apps/prayer      1.0        apps           user    +2 parts
apps/snake       1.0        apps           user

21 entries, 30 files
```

A module fetched for something else says so on the same line, which is the
only place that reason is ever shown:

```text
mods/pim         1.0        mods           user    automatic: needed by apps/contacts
```

And `catalogue owns` answers the other direction — the question a folder
you did not fill actually raises:

<!-- not run: reads the state of whatever machine it runs on -->
```sh
catalogue owns control-panel/mailset.hibr
```

```text
control-panel/mailset.hibr
  a part of apps/mail 1.1, in your own scope
  removed when apps/mail is
```

A shared one names every owner, since removing any single one of them
would leave it where it is:

```text
control-panel/pimset.hibr
  a part of apps/calendar 1.0, in your own scope
  a part of apps/contacts 1.1, in your own scope
  kept until every one of those is removed
```

It answers for a path in any scope, which matters because the same name
can be in four of them and only one of them runs: a file of your own in
`~/.config/hibr`, the machine's copy, yours, and the one hibr ships.

### The record, and the two ways it used to be lost

What is installed is kept in one file per scope — `apps.json` beside the
files it describes. Two things protect it, and both were added after an
audit found they were missing.

**One manager writes at a time.** A verb that changes anything takes a
lock before it reads, because a lock taken after the read would serialise
the writes and lose the update anyway: each run read the whole record at
its start and wrote the whole record back at its end, so two that
overlapped left both sets of files on disk and only one of them in the
record. Measured at **10 rounds out of 10** before the lock.

The mutex is a directory, since `mkdir` either creates or fails in one
syscall with no gap to race in. A lock whose owner has died is taken over
and says so; one whose owner is alive is waited for and then named:

```text
catalogue: another catalogue is writing /var/lib/hibr, as pid 4213.
Only one at a time may change the state: two at once and one of them is
lost. Try again when it has finished.
```

`AP_LOCKWAIT` is how many tenths of a second to wait first — ninety
seconds by default, because an install holds the lock while it fetches.
**Reading takes no lock and never waits**: `list`, `installed`, `info`
and `owns` answer while an install is running.

**The record is replaced, never rewritten in place.** It is written to a
temporary file beside the real one and renamed over it, so what is on disk
is always the whole of the old record or the whole of the new. It used to
be truncated in place, and a crash, a full disk or a Ctrl-C in that moment
left a partial file — which the next run read as *nothing installed*, with
every file still on disk and no longer claimed by anything.

That reading is also gone. A record that will not parse is **damaged**,
which is a different answer from an empty scope, and saying the second
when the first is true is how a half-written file became a silent total
loss. It now refuses, changes nothing, and says what to do:

```text
catalogue: …/apps.json will not read as JSON, so what is installed here
cannot be known. Nothing has been changed. The files are still where they
were; it is the record of them that is damaged.
```

An **empty** file is still an empty scope rather than damage, which is
what a fresh account has.

### Asking the disk, not the record

Everything above reads the record and believes it. `catalogue check` is
the one thing that asks the files, and it answers in both directions:

<!-- not run: reads the state of whatever machine it runs on -->
```sh
catalogue check
```

```text
/home/you/.local/share/hibr
  missing  apps/beta.hibr                     claimed by apps/beta
  orphan   apps/alpha.hibr                    is apps/alpha, which the record has lost
  orphan   apps/mine.hibr                     nothing in the index matches it

1 claimed and missing, 2 present and unclaimed, of 4 files
```

It exits non-zero when anything is wrong. It needs **no network**: the
comparison is the record against the files, and the index is read only to
recognise an orphan, only when it is already cached — a check that could
not run without the catalogue would be useless on the machine something
had gone wrong on.

An orphan is recognised by its `sha256`. A catalogue of plain files, which
is what `addapp` publishes, stores the installed file itself, so its hash
names the entry it came from; a **tarball**'s hash is the tarball's, so an
orphan from one is reported as unknown rather than guessed at.

`--repair` makes the **record** true and never touches a file:

```text
  dropped apps/beta, whose own file is gone
  took back apps/alpha 1.0, which the record had lost
```

Taking one back claims its parts too, so a whole install the record lost
comes back whole. An entry's **own** file is what says it is installed, so
one whose own file has gone is dropped even if a part of it is still
there — a Control Panel pane on its own is not an app, and the next check
names that pane as an orphan rather than keeping a dead entry alive on the
strength of it.

What it will not do is delete. A file nothing recognises may be one you
wrote, and `check` has already told you its path.

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

**Catalogue**, on the hibr menu, is this command with a window around
it — `examples/desktop/system/appstore.hibr`, the way the Vault accessory
is a window around `vw`. Anything one can do the other can, because there
is one implementation: the window runs `catalogue` as a child and never
has a catalogue of its own. It is not called *Applications*: that is
already the submenu of every app you can open, and two rows of one name on
one menu is a menu nobody can learn.

It shows what the command's own columns show, which until 0.99.202 it did
not: an entry wears **✓** when it is installed, **↻** when there is a
newer version, and **⚠** when the machine has its own copy — read before
yours, so yours can never run, which is worth more than a tick. The detail
pane says when an entry **shadows the one hibr ships**, and how many files
an entry brought with it, so what removing it would take away is readable
before you press it rather than afterwards.

The window acts in **your own scope only**. An install for the whole
machine needs root, which the window does not have and should not ask
for; that is `sudo catalogue --system install` at a terminal, and it is
what an image build does anyway.

```text
┌─┤ Catalogue ├─────────────────────────────────────────────────────┤_ □ x├┐
│ Search                                               5 in the catalogue  │
│ ITEM                            VERSION│ Snake                           │
│[-] Applications                      3 │ apps/snake                      │
│├─ ✓ snake                      1.0     │ installed 1.0                   │
│├─   retro_car                  1.2     │                                 │
│└─ ↻ mines                      1.1     │ A snake that grows as it eats,  │
│[+] Control Strip modules             1 │ on a board of your window's own │
│[+] Themes                            1 │ size.                           │
│                                        │                                 │
│                                        │                                 │
│                                        │                                 │
│                                        │                                 │
│                                        │                                 │
│                                        │                                 │
│  Uninstall ▄  Read It ▄  Refresh ▄                                       │
│  ▀▀▀▀▀▀▀▀▀▀▀  ▀▀▀▀▀▀▀▀▀  ▀▀▀▀▀▀▀▀▀                                       │
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

The marks it is drawn with are the desktop's, not this window's
(`widgets/tree.hibr`): Control Panel ▸ Appearance ▸ **Tree Style** picks
between `[+]`, arrows, triangles and dots, and **Tree Rules** turns the
lines joining a child to its branch off. Every mark is three columns wide
whichever style is chosen, so changing it never moves the columns beside
it.

`tab` or `/` moves to the search box and `tab`, `enter` or `escape` comes
back. Typing narrows the list as you type, against an entry's id, its title
and its prose, and the count beside the box says how many **entries**
matched — not how many rows are drawn, which would count the headings too.
A branch with a match in it opens itself.

| key | what it does |
|---|---|
| `i` | install the selected entry, or uninstall it if you have it |
| `u` | update it |
| `x` | uninstall it |
| `r` | read its page in a window |
| `f5` | fetch the index again |

**One button, and it flips**: Install when you have not got it, Uninstall
when you have — the same place and the same key, saying what pressing it
will do rather than offering two of which one is always wrong. Update is a
second button only while there is one to take. The App menu's own row
flips with it, for the same reason.

Every fetch is a **job**. `catalogue install` is seconds of network, and a
desktop that stops drawing while it waits is a desktop that looks broken,
so the command runs in the background with its output in a file of its own
and the next frame picks up the status — the shape `wm/remote.hibr` uses
for transfers. It is polled from the window's `_draw` *and* its `_idle`,
because a window nobody is looking at still has a job running in it. The
status line says what is happening while one runs, and what the command
said when it failed.

What the window reads is `catalogue list -j`, never the columns `apps` prints
for a person. The command answers JSON for exactly this, so the two read
one answer instead of one parsing the other's layout — which is how a
window comes to disagree with its own command. An `apps` too old to know
`-j` is named as the likely cause rather than drawn as an empty list with
no reason.

---

[← documentation](README.md) ·
[the decision record](adr/0041-an-app-is-a-folder-of-files-a-bundle-is-how-it-travels.md)
