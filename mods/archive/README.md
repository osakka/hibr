# archive — a tarball as a folder

Open a `.tar` or `.tar.gz` and walk it as though it were a folder (Gitea
#102). What `archivemount` does with FUSE, without a kernel mount, because
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
```

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
tar, so it does not take the name. The name also leaves room for a zip
reader behind the same commands later.

## Reading only

A tar is append-only in practice, and rewriting one to change a member is
the thing archivemount does badly. Writing waits until it can be honest
about rewriting the whole file.

## Folders that are not there

Plenty of archives hold no entry for a folder at all -- anything Python's
`tarfile` writes, and many real ones -- only the files inside them. A path
is a folder here when anything is under it, so `archive ls a sub` lists
`sub`'s own whether or not the archive ever mentioned `sub`. Implied folders
come back as `d` with size 0 and the time of the member that revealed them.

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

- Writing anything.
- `.tar.bz2`, `.tar.xz`, `.tar.zst`: each is another decompressor, and only
  gzip came free with the prompt module's inflate.
- zip, which is a different container with its own index at the end.
- Members larger than memory: `cat` streams, but the scheme and the gzip
  expansion both hold the whole thing.
