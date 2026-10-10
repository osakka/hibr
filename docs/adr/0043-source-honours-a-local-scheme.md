# 0043 — `source` honours a scheme that says it is local

Status: accepted

## Context

[ADR 0041](0041-an-app-is-a-folder-of-files-a-bundle-is-how-it-travels.md)
decided that an app is a folder of plain files and a bundle is only how it
travels, and one measurement decided most of that: **`source` cannot read a
scheme.**

The archive module makes a tarball a folder — `/dev/archive/NAME/path` is a
filename anywhere a filename goes (Gitea #102) — and it worked for
everything except the one thing a folder of shell code is for:

```text
read -r l < /dev/archive/b/pic.txt      →  a picture
. /dev/archive/b/app.hibr               →  source: No such file or directory
```

Schemes are honoured by `rd_do`, the one place a redirection opens anything.
`source` opens the file itself, with `fopen`, so it never reaches the scheme
layer. The alternative — `eval "$(archive cat …)"` — runs the code and loses
`BASH_SOURCE`, which is how `DT_SRC` and the desktop's About box find an
app's own header.

That ADR named the other branch and did not take it: *"either a shell change
(`source` honouring a scheme, which is its own decision) or `eval`"*. This is
that decision.

Three things were checked before taking it, each one a command rather than a
reading:

- **`b_src` opens with `fopen` directly**, so there is exactly one place to
  change.
- **`BASH_SOURCE` flows from the argument** — `s->src = sr_name(s, path ?
  path : av[1])` — so a scheme path carries through rather than breaking.
- **The About box's header read already honours schemes**, because
  `dt_appline` reads with `done < "$f"`, which is a redirection.

## Decision

**A module says whether its scheme reads something already on this machine,
and `source` reads only those.**

`hibr_schemef(s, nm, fn, HIBR_SCH_LOCAL)` is how a module says it;
`hibr_scheme` keeps its signature and means the flag is off, so **the default
is refusal** and a scheme written before this release, or by somebody who has
not thought about it, is not one `source` will run.

The archive module says it. The http module does not, and that is the point.

## Why not every scheme

The first version of this gated on `sc_find` alone — any scheme a module had
registered — on the reasoning that a *module* registering something is the
opt-in, and that the danger was `/dev/tcp` and its siblings, which `net_is`
also answers for. That reasoning was wrong, and one command said so:

```text
$ printf 'echo "RAN from $BASH_SOURCE"\n' > f.hibr
$ python3 -m http.server 28883 --bind 127.0.0.1 &
$ hibr -c 'need http; . /dev/http/127.0.0.1/28883/f.hibr'
RAN from /dev/http/127.0.0.1/28883/f.hibr
```

The http module registers a scheme, so `source` had become **a one-word way
to fetch and run code off the network** — the precise thing the exclusion of
`/dev/tcp` was written to prevent, arriving through the door left open beside
it. The risk was also misattributed: `/dev/tcp/host/80` is a bare connection
and fetches nothing until a request is written to it, while `/dev/http/…`
fetches by opening.

So the boundary is not local-against-socket but **what the scheme does when
it is opened**: the archive module reads a file that is already here, and the
http module connects. That is a property only the module knows, so the module
is what declares it.

The alternative — ship the wider gate and document the hole, since
`eval "$(< /dev/http/…)"` has always been one line — was considered and
refused. It is not a restatement of an existing reach: `. "$lib"` with a path
somebody else chose goes from `No such file or directory` to *running their
code*, which is a widening, and a security boundary nobody can see in the
code is one that will be crossed by accident.

A scheme that is refused says so, rather than falling through to `fopen` and
reporting a file that plainly exists as missing:

```text
$ hibr -c 'need http; . /dev/http/127.0.0.1/9/x.hibr'
hibr: source: /dev/http/127.0.0.1/9/x.hibr: the http scheme may connect, and source runs what it reads
```

## What follows from it

- **`BASH_SOURCE` is the scheme path.** `. /dev/archive/b/app.hibr` reports
  `/dev/archive/b/app.hibr`, which names the bundle the code came from —
  more useful than a filesystem path, not less.
- **A scheme must answer EOF.** `source` reads to the end before it runs
  anything, as it always has. A scheme that never ends would hang it; the
  archive module is a file and does not.
- **A dry run refuses it.** A scheme's own open function is arbitrary C, so
  a plan that quietly read an archive member and ran it would be the hole
  [ADR 0027](0027-a-dry-run-refuses-what-it-cannot-show-is-harmless.md) is
  written against. The check lives in `b_src` rather than in `pl_redir`,
  which calls every scheme a *connection* — true of a socket and wrong
  about a tarball. It cannot be reached today, because `mod` and `need`
  both refuse under a plan so no scheme is ever registered there; it costs
  one test of a flag and is kept for the day that changes.
- **`HIBR_ABI` does not move.** `struct scheme` is private to `src/net.c`
  and `hibr_schemef` is an addition, so a module built for ABI 16 that
  never calls it still loads. The one cross-version effect is the other
  way about: the new `archive.so` needs a shell that exports
  `hibr_schemef`, which is not what `HIBR_ABI` guards — and the package,
  the tap and the AUR build ship the shell and its modules together.

## What this is not

It is **not** "an app is a mounted archive", and it changes nothing in the
application manager. An installed app is still a folder of plain files, for
the reason ADR 0041 gives in its second point and which has nothing to do
with `source`: **a person's own files win**, because the config folder is
scanned before the data one and `dt_apps` skips a name it has already seen.
A mounted bundle has no folder for that rule to act on, so keeping it would
need a second shadowing mechanism inside the scheme layer.

What this makes possible, each its own later decision: inspecting a bundle
without unpacking it, running `--plan` against an app's real code, and a
manager that could keep a bundle mounted rather than unpacked.

---

[← decision records](README.md)
