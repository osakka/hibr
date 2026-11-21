# archive — a tarball, a zip or a disc image as a folder

Open a `.tar`, a `.tar.gz`, a zip or a disc image (ISO 9660, or a CUE
sheet and its BIN) and walk it as though it were a folder (Gitea #102,
#488). What `archivemount` does with FUSE, without a kernel mount, because
the shell already has the one mechanism it needs: a module can register a
*protocol*, so `/dev/archive/<name>/path/inside` is a filename anywhere a
filename goes.

```text
archive open NAME FILE     read the index and keep it under NAME
archive close NAME         forget it
archive list               what is open: name, file, members
archive ls NAME [PATH]     what a folder holds, a line each
archive stat NAME PATH     one member's own line
archive cat NAME PATH      its bytes
archive extract NAME DEST [PATH]
                           write it all, or PATH and what is under it,
                           into the folder DEST
archive create [-z] OUT [-C DIR] PATH...
                           write a new tar of PATH... (relative to DIR;
                           `.` is everything in it), gzipped with -z or
                           when OUT ends .gz or .tgz
```

What FILE is is decided from its bytes, never from its name, in this
order: gzip (a tar inside it), a zip (`PK` and a local header or an empty
archive's end record at the start), a tar (a first header whose checksum
adds up), a raw disc image (a CD sector's sync pattern at the start), an
ISO 9660 image (`CD001` at sector 16), a CUE sheet (text with a `FILE` and
a `TRACK` line), and anything else is read as a tar, which says when it is
not one. A zip and a disc image are read only; `create` writes tar.

A line is `kind TAB size TAB mtime TAB name`, the shape `dav ls` already
has: `f`, `d` or `l`. With `:=`, `ls` fills a map per entry (`kind`, `size`,
`mtime`, `name`, `path`) and `stat` and `cat` fill the slot instead of
printing.

<!-- setup
mkdir -p ex/notes && printf 'hibr\n' > ex/README.md
printf 'one\ntwo\n' > ex/notes/list.txt
(cd ex && tar czf ../demo.tar.gz README.md notes) 2> /dev/null
-->
```sh
need archive
archive open d demo.tar.gz
archive ls d | cut -f1,2,4
read -r first < /dev/archive/d/README.md
echo "the first line of README.md is: $first"
archive cat d notes/list.txt
archive close d
```

```output
f	5	README.md
d	0	notes
the first line of README.md is: hibr
one
two
```

## It is called archive, not tar

`tar` is a program everybody has, and a module's builtins become commands,
so a builtin called `tar` would make `/usr/bin/tar` unreachable the moment
the module loaded. The cat module shadows `cat` on purpose, because it is
byte-identical in a pipe and nothing can tell; this is no replacement for
tar, so it does not take the name. The name also left room for the zip
and disc image readers that are behind the same commands now.

## Writing a new one, and extracting one

A member is never changed in place: a tar is append-only in practice, and
rewriting one to change a member is the thing archivemount does badly. What
the module writes is a whole new archive, or a whole extraction (Gitea #475).

<!-- setup
mkdir -p ex/notes && printf 'hibr\n' > ex/README.md
printf 'one\ntwo\n' > ex/notes/list.txt
-->
```sh
need archive
archive create demo.tgz -C ex README.md notes
archive open d demo.tgz
mkdir out
n := archive extract d out
echo "extracted $n members"
ls out out/notes
archive close d
```

```output
extracted 3 members
out:
README.md
notes

out/notes:
list.txt
```

`create` writes ustar, with the prefix field for a long path and GNU's
long-name block where even that is too short, so GNU tar, bsdtar and
Python's tarfile all read it. It writes `OUT.hibr-part` and moves it into
place only when whole, never over a file that is there, and leaves out the
archive itself when it is written into a folder it is archiving. A `.gz`
is kept whole in memory before compressing, bounded by `ARCHIVE_MAX`; a
plain tar is written as it is read. Symbolic links are stored as links;
anything that is not a file, a folder or a link is left out, saying so.

`extract` is the one place a name from somebody else's archive becomes a
path on this machine, so it checks every member before it writes any:

- a name with an empty, `.` or `..` part is refused (a leading `/` was
  already taken off when the archive was read, as GNU tar does, so an
  absolute name lands inside DEST);
- a symbolic link whose target is absolute or climbs with `..` is refused;
- anything that would land on a file already in DEST is refused;

and on any of those **nothing is written**: the whole extraction is
refused, naming the member, rather than the bad one skipped. Links are
made last, after every file and folder, so nothing is ever written through
one. That leaves one refusal that comes late: a link whose own name an
earlier member has already made a folder of (`d`, then `d/x`) fails when
its turn comes, with what came before it already in DEST, all of it inside
DEST. So write into a folder of your own and move it when it is whole, as
the desktop's Archives extension does. A hard link is left out. Set-id and sticky bits are dropped, the rest
of the mode kept, and each file's time put back.

## A member can be sourced

Since 0.99.140 `source` honours a scheme whose module says it reads
something already on this machine, which this one does
(`hibr_schemef(..., HIBR_SCH_LOCAL)`), so a member holding shell code runs
in the current shell with `$BASH_SOURCE` naming the archive it came from --
which is what makes a tarball a library rather than only a folder of data.
See [0043](../../docs/adr/0043-source-honours-a-local-scheme.md).

## Folders that are not there

Plenty of archives hold no entry for a folder at all -- anything Python's
`tarfile` writes, and many real ones -- only the files inside them. A path
is a folder here when anything is under it, so `archive ls a sub` lists
`sub`'s own whether or not the archive ever mentioned `sub`. Implied folders
come back as `d` with size 0 and the time of the member that revealed them.

## Zip

The central directory at the end of the file is the index; each member's
own local header is read only for where its bytes start. A member is
stored or deflated -- inflated whole, through `mods/inflate.c`, when it is
first read, its length and CRC checked before a byte is handed out -- and
anything else is listed but not read: an encrypted member, or one
compressed another way (bzip2, LZMA, Deflate64, zstd), says so by name when
it is asked for, and an extraction holding one writes nothing. zip64's own
fields and end records are read, so a member or an archive past 4 GB is
fine; a zip spread over several files is refused.

A name marked UTF-8, or that reads as UTF-8, is kept as it is, and any
other is read as code page 437, which is what DOS wrote; Info-ZIP's
Unicode path field wins over both when its checksum matches. A name made on
DOS or Windows has its backslashes taken for folders. Times are the
extended timestamp field's when there is one, and otherwise the DOS time,
which has no zone and is read as local time. A member made on Unix keeps
its mode, and a symbolic link -- a member whose mode says so, its target
its bytes -- is a link, under the same rule as a tar's: one that points out
of the folder refuses the extraction.

<!-- setup
mkdir -p ex/notes && printf 'hibr\n' > ex/README.md
printf 'one\ntwo\n' > ex/notes/list.txt
(cd ex && bsdtar --format zip -cf ../demo.zip README.md notes)
-->
```sh
need archive
archive open z demo.zip
archive ls z notes | cut -f1,2,4
archive cat z notes/list.txt
archive close z
```

```output
f	8	list.txt
one
two
```

## Disc images

An ISO 9660 image is read from its volume descriptors at sector 16. The
names are Rock Ridge's when the image has them -- with each file's mode,
time and links, and folders relocated for depth followed back to where they
belong -- else Joliet's, else the plain ISO 9660 ones, their `;1` taken
off. A file recorded in several pieces reads whole when the pieces lie
together, and is refused by name when they do not, as is an interleaved
one. A folder the image names twice, or that is its own ancestor, is walked
once, and the folders together may not read more than the image holds.

A CUE sheet names a BIN beside it: the first track that is `MODE1/2048`,
`MODE1/2352` or `MODE2/2352` is opened from its `INDEX 01`, and the raw
sectors' sync and headers are skipped as it is read, so nothing is copied.
The BIN is found in the sheet's own folder, matching its case when no file
has it exactly, as a sheet written on Windows often needs; a sheet naming a
file anywhere else is refused, and so is one with no such track, naming
the mode it found. A raw `.bin` opens by itself too. Audio tracks are left
alone, and an image with only UDF on it is not read.

`ARCHIVE_MAX` bounds a zip and a disc image as well: what their members add
up to may pass the archive's own size by no more than it, which is what
stops a zip whose small members inflate enormously, or a disc image whose
files all claim the same bytes, from filling the disk on extraction.

## What it does with the file

A gzip archive is expanded once, at open, into a file of its own that
nothing else can see -- made and unlinked in the same breath -- so every
later read is a seek rather than another pass over the stream. The inflate
is `mods/inflate.c`, which the prompt module already had for git's objects
and packs; it is shared rather than written twice, and no zlib is linked.
`ARCHIVE_MAX` bounds how far an archive may expand (512 MB by default), so a
crafted one cannot ask for the machine's memory.

An uncompressed archive is read where it lies, and the index is 512-byte
header reads: 500 members take about 4 ms.

`/dev/archive/NAME/path` copies that member into a file of its own and hands
back a descriptor on it. A descriptor on the archive itself would read
straight past the member's end into the next header, and a pipe would need
something to fill it; a copy is the honest answer, and the member's own size
is what it costs.

## Headers it understands

ustar and GNU, with the prefix field, GNU's long name (`L`) and long link
(`K`) blocks, and pax extended headers (`x`, `g`) read for their `path=`.
Numbers are octal or GNU's base 256. An archive ends at two zero blocks; one
that is 1024 bytes of zeroes is an empty archive and opens, while a file that
is neither is refused rather than opening with no members -- which is what
the first version did with a 40-byte text file.

## What it does not do yet

- Changing a member in place: an archive is written whole, or not at all.
- `.tar.bz2`, `.tar.xz`, `.tar.zst`: each is another decompressor, and only
  gzip came free with the prompt module's inflate.
- Writing a zip or a disc image: `create` writes tar.
- A zip member compressed any way but deflate, an encrypted one, a
  self-extracting zip, and a disc image's second session or its UDF.
- Members larger than memory: `cat` streams, but the scheme and the gzip
  expansion both hold the whole thing.
- A member for a module that opens its own path. "Anywhere a filename goes"
  means a redirection or `source`; `img`, `media` and `web` call `fopen`
  themselves, so `img size /dev/archive/b/x.png` says it cannot open it.
  This is why the catalogue unpacks a bundle rather than mounting it (ADR
  0041).
