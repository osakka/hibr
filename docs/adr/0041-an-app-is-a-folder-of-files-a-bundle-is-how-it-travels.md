# 0041 — An app is a folder of files; a bundle is how it travels

Status: accepted (the scan list below was `DA_DIRS`/`desk-accessories` when
this was written; 0.99.126 replaced it with `DT_SYSDIRS`/`system` and put
every application under one Applications submenu. The decisions here are
unaffected -- what travels, what it becomes on disk, and the trust model --
and the manager installs into the same five roots, one of them renamed.)

## Context

A desktop app here is a hibr script. `apps/write.hibr` is 2,000 lines of
shell, `apps/Accessories/calc.hibr` is 300, and both are read by the same
`dt_app` registration. Nothing about that needed designing — it fell out of
"tools are modules, applications are scripts" — and it means the hard part
of a package manager is already done: **installing an app is placing a file
where a scan already looks.**

Five scans exist, and every one of them begins with a folder of the
person's own:

| list | default |
|---|---|
| `DT_APPDIRS` (`wm/apps.hibr`) | `$XDG_CONFIG_HOME/hibr/apps`, scanned recursively, a subfolder becoming a submenu |
| `DT_SYSDIRS` | `$XDG_CONFIG_HOME/hibr/system`, flat (`DA_DIRS`/`desk-accessories` when this was written) |
| `CP_PANEDIRS` (`apps/panel.hibr`) | `$XDG_CONFIG_HOME/hibr/control-panel` |
| `CS_MODDIRS` (`wm/strip.hibr`) | `$XDG_CONFIG_HOME/hibr/control-strip` |
| `SV_DIRS` (`savers/saver.hibr`) | `$XDG_CONFIG_HOME/hibr/savers` |

And what an app may do is already a contract, not an ambition: menus
(`<app>_menus` with `dt_menu`/`dt_item`), an About box (`DT_ABOUT`, which
`dt_aboutinfo` derives from the file's own header comment when the app sets
nothing), a Control Panel pane (`cp_pane name title icon [group]`), a
Control Strip module (`cs_module`), notifications (`dt_notify`), a screen
saver (`sv_saver`) and a window of any shape (`dt_app`).

So the question is not how an app plugs in. It is **what travels, what it
turns into on disk, and what a person is agreeing to when they install
code someone else wrote.**

Two measurements decided most of it.

**`source` cannot read a scheme.** The archive module makes a tarball a
folder — `/dev/archive/NAME/path` is a filename anywhere a filename goes
(Gitea #102, `mods/archive/README.md`) — and that works for *resources*:

```text
read -r l < /dev/archive/b/pic.txt      →  a picture
. /dev/archive/b/app.hibr               →  source: No such file or directory
```

Schemes are opened by `rd_do`, the one place a redirection opens anything;
`source` opens the file itself. So an app's **code** cannot come out of a
bundle without either a shell change (`source` honouring a scheme, which is
its own decision) or `eval "$(archive cat …)"` — and that second loses
`BASH_SOURCE` and `DT_SRC`, which is what the About box reads the app's own
header through. The owner's instinct was "text on download, binary once
installed"; it is the other way round, and this is why.

**`--plan` on an app reports nothing.** `hibr --plan` runs a script's own
logic and refuses and records every write, connection and program (ADR
0027), so it looked like the answer to "what will this app do". On a real
app it prints nothing at all and exits 0, because an app file *defines*
functions and guards its one statement with `command -v dt_app`. Sourcing
an app does almost nothing; the app happens later, inside the desktop,
when the window manager calls its callbacks.

## Decision

**An app is a folder of plain files. A bundle is how it travels, and
nothing reads an app out of one.**

1. **Transport is a `.tar.gz`; the installation is a folder.** The manager
   fetches a bundle, verifies it, and unpacks it into a folder of plain
   files you can read, diff and patch. That is not a preference about
   taste: `source` cannot read a scheme, so it is the only form the
   desktop can load — and it keeps the property that every app on this
   desktop is a script you can open.

2. **Installed apps live under `$XDG_DATA_HOME`, and a person's own files
   win.** Each scan list gains
   `${XDG_DATA_HOME:-$HOME/.local/share}/hibr/<kind>` **after** its
   existing config entry. Config is what a person wrote and would back up;
   data is what a program placed and can fetch again. The order is the
   whole point: `~/.config/hibr/apps/calc.hibr` shadows a fetched `calc`,
   because `dt_apps` skips a name it has already seen — so patching an
   installed app means copying it to your own folder and editing it there,
   and an update cannot overwrite your copy.

3. **The index describes the app; a bundle of more than one file describes
   its own parts.** One `index.json` in the repository carries, per app:
   name, title, version, a one-line description, the author, the licence,
   the `hibr` and ABI it needs, the modules it needs, the bundle's path and
   its `sha256`. A **single `.hibr` file is its own bundle** and needs no
   manifest — most apps are one file. A bundle with several files carries
   `app.json` naming which file is the app, which the pane, which the strip
   module, which the saver, and where its resources are, because the index
   has no business knowing a bundle's internal layout.

4. **Everything is data, never a script.** The index and `app.json` are
   JSON, for the reason ADR 0028 gives for themes: they are what people
   hand each other, and a script would run as whoever read it. The
   manager parses them with `json`, which keeps types.

5. **Trust is disclosed, not claimed.** Three separate things, named
   separately because they protect different things:
   - **TLS** gives the identity of the *server* the index came from.
     Certificates are verified by default (ADR 0009).
   - **`sha256` from the index** gives the integrity of the *payload*, so a
     bundle that was changed in transit or on a mirror is refused. v1 forks
     `sha256sum`, which `--plan` already knows as a program that only
     reads; the shell has no hash of its own. That is the seam where
     signing would later go, and `vwk` already dlopens libcrypto for its
     own crypto.
   - **Nothing gives the identity of the *author*.** There are no
     signatures in v1, and the manager says so rather than implying it.
   - **`--plan` is not part of this.** It covers loading, and loading an
     app does almost nothing; a disclosure that reads as "this app does
     nothing" is worse than none, because it says *safe* about code it
     never ran. The trust in running an app is the trust in where it came
     from. An install-time dry run of an app's actual behaviour means
     driving its callbacks through a pty harness, which is real work and a
     separate decision.

6. **No binary modules.** A bundle may not carry a `.so`, and one that does
   is refused with that word. A module is native code with no sandbox of
   any kind, root loads modules only from the folder compiled into the
   binary (ADR 0016), and a downloaded `.so` is a different class of risk
   from a downloaded script — which is at least something a person can
   read. Managing the modules that are *already* installed stays the
   Modules app's job (Gitea #133).

7. **The manager is a command and a window, not a module.** The fetching
   and placing is `examples/apps.hibr`, installed as a command, the way
   `vw` is: it needs `dav request` for HTTPS, `json` for the index,
   `archive` to look inside a bundle before unpacking it, and the `mkdir`,
   `rm` and `mv` builtins to place it. No C, so no module — "the core
   grows only for things that make it a better shell". The desktop app is a
   window that runs that command as a child, exactly as the Vault
   accessory runs `vw` (`wm/vault.hibr`), so the manager works for someone
   who never starts the desktop.

8. **It belongs to the desktop, not to Applications.** By the rule this
   desktop now draws its menus on, a thing that manages the desktop is the
   desktop's; the Applications menu is for things that open something of
   yours. The desktop *icon* called Applications opens Files on the apps
   folder, so it is navigable for free.

9. **An app with a window open cannot be removed.** Uninstalling frees the
   folder, and the desktop's own `DT_SRC` and the app's functions would
   still be there with nothing behind them. The manager refuses while
   `dt_ids` shows a window of it, and says which; a forced uninstall closes
   them through `dt_closereq` first, so an app that asks before closing
   still asks. Replacing an app that is running is hot reload, which is
   Gitea #42 and not this.

## Consequences

**What this buys.** An app is one file or one folder, and installing it is
a copy — so a person can install by hand with `cp`, uninstall with `rm`,
and read every line of what they installed. The repository is a git repo
with a JSON index, so it can be browsed in a browser, mirrored by anyone,
and served by any static host.

**What it costs.** A bundle's resources are unpacked rather than read in
place, so an app with pictures costs its own disk twice while the bundle is
being installed and once after. A single-file app has no manifest, so its
metadata lives only in the index and a file copied by hand carries none —
which is why `dt_aboutinfo` reading an app's own header comment matters
more than it looks.

**What is deliberately missing, and will be asked for.** Signing. An app
store with ratings. Dependencies between apps. Installing a module.
Updating an app while its window is open. Each is a decision of its own,
and none of them is blocked by anything here.

**The seam to watch.** If `source` ever learns to open a scheme, reading an
app straight out of a bundle becomes possible, and this ADR's first
decision is the one to revisit — not because a folder is wrong, but
because the reason it is the only option would have gone.

---

[← decision records](README.md) · [← documentation index](../README.md)
