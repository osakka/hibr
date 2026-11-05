# 0053 — Files reaches anywhere through locations

Status: accepted (0.99.439)

## Context

Files could show one kind of folder that is not on this machine: a WebDAV
server's. It did so by testing `dav://` in about twenty places -- the scan,
the title, the path bar, opening, renaming, deleting, new folders, pasting,
the menus -- and calling the dav module and `wm/remote.hibr` directly from
each. The owner then asked for archives to open in Files as folders, as a
catalogue extension that leaves no hole when it is removed (Gitea #475).
Done the same way, that is twenty more tests of a second scheme, and
Files naming code that may not be installed.

## Decision

**A location is a scheme and a prefix** (`wm/locations.hibr`, platform).
`dt_location dav rdav` says that a path beginning `dav://` is answered by
`rdav_` functions; `dt_locof PATH` gives the prefix of the location a path
is in, or fails for a path on this machine and for a location whose
functions are not loaded. Files asks that and nothing else: it names no
scheme.

**What a location answers is one list,** every part optional but `_ls`:
`_ls DIR` (a map of name, dir, size, mtime, and etag and type where it has
them -- the shape `dav ls` already had; `archive ls` gives `kind` where
this wants `dir`, so a location over it translates), `_ok`, `_norm`,
`_top`, `_title`, `_can OP`, `_open`, `_mkdir`, `_mv`, `_delete`,
`_copyplan` and `_copyrun` (`_fileop` until 0.99.442). A file is opened by the location fetching a copy and opening
that, because no program can read a scheme.

**What a location cannot do is asked, never inferred from its name.**
`_can` answers for write, mkdir, rename, delete and open; Files dims New
Folder, Rename, Cut, Paste and Delete where it says no, and a move out of a
location that cannot delete becomes a copy. So an archive, which can only
be read, needs no case of its own in Files.

**The copy window routes by location** (`wm/copying.hibr`): a move or copy
with either end in one is that location's, the folder's own first; two
different locations are refused, since neither can reach the other.

**WebDAV moved first, behaviour unchanged.** Its functions are the bodies
that were inline in Files; `tests/dav.py`, which drives Files against a
stand-in server, passes unedited. The Servers menu stays WebDAV's own.

## Consequences

- A new kind of place -- the archive extension, and anything after it -- is
  a file of functions and one `dt_location` line, installed and removed
  with whatever brings it, and Files gains nothing.
- The archive extension followed in 0.99.441 and needed three things this
  list did not have. `_can leave` says a location's top has a parent on
  this machine, so `..` shows there and `_norm` answers a local path; `_can
  browse PATH` says whether an opener may offer a file at all; and a third
  word on `dt_location` names the app that brings it, so unregistering the
  app takes the location at once, where functions already sourced would
  otherwise keep it alive until a restart. Listing went into a job, since
  a big compressed archive takes seconds to read: `_ls` answers empty until
  the index exists, and `fb_relist` then tells the windows showing it.
- Copies moved into the copy window in 0.99.442, after one from a NAS ran
  for minutes with nothing on screen. `_fileop` did the whole thing itself
  and could only leave a note, so it became a plan and a run: a job that says
  what is already there, since that may mean asking a server, and a job that
  reports as `copy run` does. What a location replaces has no trash to go
  to, so the window's question says it cannot be put back, and nothing a
  location does goes in Undo.
- A terminal still cannot start in a location, and a folder in one opens in
  the same window: Files keeps those two rules itself, since no location
  could honour them.
- The old names (`fb_remote`, `fb_rscan`, `fb_rrename`) are gone; a script
  of the user's own that called them has `fb_located`, `fb_lscan` and
  `fb_locmv`.

## Alternatives

**A FUSE mount per archive or server**, which every program could read.
Refused for the reason the archive module exists at all: no kernel mounts,
no privilege, nothing left mounted when the desktop dies.

**The `/dev/<scheme>/` protocol modules already register.** It answers
"read this file" anywhere a filename goes, which is what a location's
`_open` uses underneath for archives; it has no listing, no capabilities
and no writes, which are what Files needs, so it is the layer below this
one rather than a replacement for it.
