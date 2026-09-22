# Backlog

Wanted, not yet built. Open items in [CLAUDE.md](../CLAUDE.md) are things known
about the code as it stands; this is what should exist next. Each entry says
what it needs, because "easy?" is usually answered by the part nobody thought
about.

## Modules

### List what is installed, not only what is loaded

`mod list` shows the modules that are **loaded**. Nothing shows what is
*available* — there are four `.so` files in `/usr/local/lib/hibr` and no way to
ask the shell about them.

Wanted: `mod avail` (or `mod list -a`), listing the module directory with each
module's name, version, ABI and builtins, and marking the ones already loaded.
Reading the ABI and description means `dlopen`ing each candidate to reach its
`hibr_module` descriptor, so it should refuse one whose ABI does not match
rather than loading it.

Small. The search-path logic in `m_open` already knows where to look, including
the rule that root consults only `HIBR_MODDIR`.

### Loading by path — already works

`mod load ./build/mods/ls.so` and `mod load /usr/local/lib/hibr/sys.so` both
work today; a name with a `/` in it is opened directly and never searched for.
Only a bare name goes through the search path. Nothing to do, recorded so the
question is not asked twice.

## Ports

### macOS

Tracked as a ticket; the state of play lives here so it is not rediscovered.

Dealt with: `sys/prctl.h` does not exist on Darwin, so `title` sets the comm
name behind an `#ifdef` and keeps the portable argv rewrite that is what `ps`
actually reads; the link flags, since Darwin keeps `dlopen` in libc, spells
`-rdynamic` as `-Wl,-export_dynamic` and wants `-dynamiclib` rather than
`-shared`; and `struct stat`'s nanosecond fields, `st_mtim` against
`st_mtimespec`, now behind `HIBR_MTIM` and `HIBR_ATIM` in `hibr.h`. That last
one is not cosmetic — the racy-index rule compares nanoseconds, and losing it
silently would make the prompt rehash the whole tree on every draw.

Also dealt with: `setresuid`/`setresgid` do not exist on Darwin. `drop` now
uses `setgid` then `setuid`, which move the real, effective *and* saved ids
together while the effective id is still root. What makes that trustworthy
rather than merely compiling is the check already there — `drop` verifies all
four ids afterwards and then tries `setuid(0)`, refusing to continue if root
can be taken back. That check is the security property, not the call used to
get there, and it holds on both platforms.

Still open: `tcc` almost certainly does not work on arm64 Darwin, so macOS
probably means `make CC=gcc` and that needs deciding rather than assuming;
`/proc/meminfo` in the prompt module has no Darwin equivalent and should report
nothing rather than a wrong number; whether modules should follow the `.dylib`
convention when `m_open` only appends `.so`; and the sonames `libssl` is
`dlopen`ed by, which differ and are not on the default search path under
Homebrew. Modules are built as `-dynamiclib`; if `dlopen` refuses them, `-bundle` is the
other spelling and the one macOS conventionally uses for plugins. Job control,
the pty line editor and the test suite are untested rather than known broken.

## Wanted

### A vi — built

`mods/vi`, on the display interface.

The two hard calls the entry asked for are made and written down. **The buffer
is a gap buffer**, not a piece table: a piece table earns its complexity by
making undo a snapshot of the piece list, and undo here records edits, so that
advantage never arrives. The line index rebuilds from the edit point forward,
which is the property the 2 GB argument actually needed — on 50 MB, 200 edits
at the far end with a re-index after each cost 0 ms. **Undo is linear with a
redo stack**, grouped so that `u` after typing a sentence removes the sentence.

`:w` writes through a temporary file and renames, and refuses when the file
changed on disk since it was read, with `:w!` to override — vim's behaviour,
and the owner's call.

Not there: counts, `.`, registers, marks, `:s`, and `!` to filter through a
command. When that last one arrives it must call `hibr_run` rather than
`popen`, which is the rule about not becoming a second shell.

### A most — built

`mods/most`, and the first module to use another one.

Two windows, horizontal scrolling, every match highlighted rather than jumped
between, `F` to follow a growing file, and colour in the input parsed into
screen-layer pens — so it survives paging *and* sideways scrolling, because the
escapes are no longer in the text being cut. Lines appear as they arrive rather
than after the end, so `slow-thing | most` is readable immediately.

The trap this entry recorded was the right one: a pager's input is the data,
not the keyboard, and the screen module already took the terminal from standard
output. `tests/most.py` covers both shapes and the piped one is the one that
matters.

Not done: a binary mode beyond noticing and saying so, more than two windows,
and a mark-and-return.

### The module registry — built

`hibr_provide` and `hibr_require`, modelled exactly on `hibr_scheme`. This was
the third of the three options costed under the cat, and the one recommended:
a module offers a named, versioned table of functions in its init and withdraws
it in its finaliser, and another asks for it by name and version. Modules stay
`RTLD_LOCAL`, so nothing collides.

It unblocks the vi and the monitor as well. **It does not unblock the cat's git
gutter** — the git reader inside `prompt.so` is not arranged as a table and
would have to be given one, which is a separate piece of work on that module
rather than on the mechanism.

### A cat — built

`mods/cat` exists. In a pipe it is `cat`, byte for byte — 37 comparisons
against `/bin/cat` itself, including every flag, the error cases and a binary
file, and 100 MB copies in 15 ms against `/bin/cat`'s 14. On a terminal it adds
a dim gutter, control bytes shown rather than sent, lexical colour for C,
shell, Python, JSON, Markdown and Makefiles, and a refusal to spew a binary.
`-p` turns all of it off.

Two things from this entry were **not** built, and the reasons are worth
keeping.

**Paging.** A `cat` that pages is half a `most`. When the `most` below exists
this can hand off to it; building a second pager inside a `cat` is the
duplication the display layer was created to avoid.

**The git gutter, which this entry promised and the architecture cannot yet
deliver.** hibr does read git's object store natively — but that code lives in
`prompt.so`, and `m_open` uses `RTLD_LOCAL`, so `cat.so` cannot reach a symbol
of it. There is no way to share code between two modules today. The three ways
out, none free:

- Move the git reader into the shell. Several thousand lines against the
  resident-memory priority, paid by every shell that never looks at a repo.
- Open modules `RTLD_GLOBAL`. Every module's symbols then collide with every
  other's, which is the `m_drop` trap generalised to the whole module system.
- **Give the ABI a way for one module to export to another** — a named registry
  a module publishes a function table into and another looks up by name and
  version. Real design work, and the only one that scales past two modules.

**The third was chosen and built** — see the module registry above. What that
leaves for the git gutter is not the mechanism but the shape of `prompt.so`:
its git reader is a set of functions, not a table it offers, so giving it one
is a piece of work on that module. Until then, anything wanting git data has to
be part of `prompt.so` or do without.

### A shared pty test harness

`tests/editor.py`, `tests/console.py`, `tests/cat.py` and `tests/most.py` each carry their own
twenty-five lines of `pty.fork` boilerplate, because everything interesting
about a terminal is invisible to `run.sh`. Four copies is three too many. A
shared `tests/ptyrun.py` would fix it — and must not be called `pty.py` or
`tty.py`, for the reason already recorded in `CLAUDE.md`.

---

[← documentation index](README.md)
