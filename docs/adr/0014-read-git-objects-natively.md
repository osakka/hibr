# 0014 — Read git's object store natively rather than forking git

Status: accepted

## Context

A prompt that shows branch, working-tree status and distance from upstream
needs data that only git has. The easy route is to fork `git status
--porcelain=v2 --branch` and parse it. That is correct, complete, and costs a
fork plus git's own work on every prompt.

Reading it directly means implementing a DEFLATE decoder, loose object reading,
packfile indexes with delta chains, SHA-1, the index format in three versions,
and `.gitignore` semantics — because there is no zlib to link against
([0001](0001-build-with-tcc-and-no-dependencies.md)).

## Decision

Read the repository directly. No forks.

`inflate.c` implements RFC 1950 and 1951. `obj.c` reads loose objects, pack
indexes v1 and v2, `OFS_DELTA` and `REF_DELTA` chains, and follows
`info/alternates` and linked worktrees. `sha1.c` provides the hashing the index
comparison needs. `idx.c` parses `.git/index` versions 2, 3 and 4. `status.c`
compares the index against the working tree and against HEAD's tree. `ign.c`
implements `.gitignore` matching. `walk.c` finds the divergence from upstream
by walking both histories by commit date.

## Consequences

A full prompt in a 2,600-file repository takes about 35 ms, against 32 ms for
`git status --porcelain=v2 --branch` on its own — and hibr pays no fork on top
of that. In a small repository it is about 3 ms, and outside a repository 1.4 ms.
Object reads are bounded by `PROMPT[git][max_object]` so a crafted object
cannot make a prompt allocate without limit.

The cost is roughly 2,500 lines of git implementation living inside a prompt
module, which has to be correct or the prompt lies. That is why it is verified
against git rather than against itself: 32,000 objects compared for type and
size, hundreds compared byte-for-byte including delta chains 50 deep, the index
parser compared against `git ls-files -s`, SHA-1 compared against `git
hash-object`, and 36 dirty-repository states compared against `git status
--porcelain=v2`.

Three divergences are accepted rather than fixed. Renames are detected only
when content is byte-identical, so an edited-and-moved file counts as two
changes. A submodule is clean unless its directory is missing; its working tree
is not inspected. In a `.gitignore` pattern, `**` in the middle of a path
behaves as `*`.

---

[← decisions](README.md)
