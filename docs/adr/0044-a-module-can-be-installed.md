# 0044 — A module can be installed, and the index is where the line is drawn

Status: accepted

## Context

[ADR 0041](0041-an-app-is-a-folder-of-files-a-bundle-is-how-it-travels.md)
rests on one sentence: *an app is a script you can read before you run it.*
Everything the application manager does is built on it — TLS for who
answered, a `sha256` for whether the payload survived the trip, and nothing
at all for who wrote it, because a person can open the file. That sentence
does not survive a `.so`, and `apps` has refused one by name since it was
written.

The owner asked for modules in the catalogue anyway, and was right to: a
module is how hibr grows, and *"tools are modules"* is the project's own
rule. Three things stood in the way, and the third is the interesting one.

- `ap_pathok` refuses any member path ending in `.so`.
- The index has an `abi` field and no `arch` field, and **neither is
  enforced**: a module must match `HIBR_ABI` exactly and be built for the
  machine, and nothing checked either.
- **There was no user module directory on the search path at all.**
  `m_open`, `mod list`, `mod avail`, `mod find` and `hibr_require` each
  walked the current directory, `$HIBR_MODPATH` and the compiled-in
  `HIBR_MODDIR`, and a file in `$XDG_DATA_HOME/hibr/mods` would never have
  been found — so "install a module" could not work whatever the catalogue
  said.

## The measurement that decided it

The obvious change is to put that folder on the path and be done. The
argument for it being safe was *"a writable directory on a non-root module
search path is no stronger than `~/.hibrc`, which hibr already runs without
ceremony"*, and that argument does not hold.

`m_ldauto` lets a **command** load the module that declares it
([ADR 0040](0040-a-module-may-answer-a-command.md)), and it reads an index
built by `m_bixbuild`, which walks the whole module path and **`dlopen`s
every candidate** to read its descriptor. hibr's own init is not called;
the object's ELF constructors are. Measured on this machine:

```text
$ strace -e trace=openat hibr -c 'typoooo' 2>&1 | grep -c 'hibr/.*\.so'
35
$ cat /tmp/t.hibr
typoooo 2>/dev/null; true
$ strace -f -e trace=openat hibr /tmp/t.hibr 2>&1 | grep -c 'hibr/.*\.so'
35
```

Thirty-five objects opened for one unknown command — in a **non-interactive
script**, on a box with **no `~/.hibrc` at all**. `.hibrc` is read by
interactive shells; this happens in a cron job. The two are not comparable,
and the index is built by a *typo*, which is not an act of trust.

## Decision

**The user's module folder is searched when something asks for a module by
name, and is left out of the index a missing command builds.**

`$XDG_DATA_HOME/hibr/mods`, or `~/.local/share/hibr/mods` when that is
unset, goes on the search path **after** `$HIBR_MODPATH` and **before**
`HIBR_MODDIR` — after, so a module installed here cannot shadow a build
tree under test; before, so a fetched module beats the shipped one, which
is the order every other kind already follows. Root's rule does not move an
inch: `HIBR_MODDIR` and nothing else
([ADR 0016](0016-privileges-are-dropped-never-gained.md)).

| what asks | the folder is searched |
|---|---|
| `mod load <name>`, `need <name>`, `hibr_require` | yes |
| `mod list`, `mod avail`, `mod find <builtin>` | yes |
| the index behind `HIBR_MODULES=after`/`before` | **no** |

So: **a module you installed is one you can ask for by name; it is never
loaded because you mistyped something.** `csv` as a bare command says
`command not found`; `need csv` then `csv` runs it. The same object on
`$HIBR_MODPATH` does autoload, which is what says this is the folder's
rule and not the module's.

One walk serves all five readers (`m_wstart`/`m_wnext` in `src/mod.c`),
with a flag for the index's. There were five copies of the colon-splitting
before, and a rule that lives in five places is a rule kept in one of them.

**A module install is refused unless the index is signed.** The catalogue
carries a detached signature over `index.json`, made with the same key the
apt repository uses, and the index already holds every bundle's `sha256` —
so one signature covers every kind. `apps` verifies it against a pinned
fingerprint. A **script** install warns once and goes on, because `sha256`
and TLS are its stated trust model and a reader is the backstop; a
**module** install stops, because there is no reader and the object runs as
you. A host with no `gpg` is the same case: scripts unaffected, modules
refused. Without this the rest is decoration, and `catalogue info` would be
claiming a check nothing makes.

The key is pinned by **fingerprint**, and the keyring is only where to
look: a swapped keyring verifies nothing unless it holds a key with those
forty characters. `HIBR_APPS_KEYFP` names a different one, because a
catalogue is a folder anybody may copy and serve and somebody running
their own signs it with their own key. That is not a hole in the model —
what this defends against is **a catalogue or a mirror that has been
changed**, not somebody who can already set your environment, who could
as easily put their own `gpg` on your `PATH`.

**`arch`, `abi` and `hibr` are checked before the fetch**, as an app's
requirements already are, so a module for another machine or another ABI is
named and declined rather than downloaded and found wanting.

## Consequences

A module from the catalogue is a real install: `catalogue install
mods/<name>` places it and `need <name>` finds it. Nothing new runs on a
typo. Root is where it was.

The cost is the asymmetry — a module in that folder cannot be reached by
running a builtin it declares, which is a thing a module in `HIBR_MODPATH`
can do, and somebody will be surprised by it once. The answer is that the
surprise is a `need` away and the alternative is code running because of a
spelling mistake.

The other cost is that the catalogue now has a key to keep. That was always
true of the apt repository; this uses the same one rather than making a
second thing to lose.

## Alternatives

**Stage it, and let the person add the folder to `HIBR_MODPATH`
themselves.** This was the design written up before the ticket, and it has
the same exposure the moment they do it — with the difference that they
asked. It was refused because "install" that does not install is a verb
that lies, and because the person adding the folder to `HIBR_MODPATH` gets
*more* than this decision gives them: the index as well.

**Search it everywhere, autoload included.** What the ticket first
proposed. Refused on the measurement above.

**Modules as debs instead.** The owner considered it and chose the
catalogue, for the reason that a module ought to be installable by somebody
who did not get hibr from apt. A deb is still the right answer for a
first-party module, and `HIBR_MODDIR` is still where root looks.

---

[← decision records](README.md) ·
[0041](0041-an-app-is-a-folder-of-files-a-bundle-is-how-it-travels.md) ·
[0016](0016-privileges-are-dropped-never-gained.md) ·
[0040](0040-a-module-may-answer-a-command.md)
