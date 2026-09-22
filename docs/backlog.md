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

### A visual traceroute with a world map — built

`mods/trace` plus `examples/traceroute.hibr`. The three things that looked hard
turned out to have light answers, and one of them was not solved at all.

**Getting the hops needs no privilege after all.** The entry used to say this
needed a raw socket and therefore root. It does not: a UDP socket with
`IP_RECVERR` set collects the ICMP complaints on its error queue, which
`recvmsg(MSG_ERRQUEUE)` reads along with the address of the router that
complained. That is how `tracepath` does it. The cost is that it is Linux's;
elsewhere the builtin loads and refuses, saying why.

**Setting the hop limit** was said to need a new builtin. It needed
`setsockopt` inside the module, which is three lines.

**Places need no database.** `/usr/share/zoneinfo/zone1970.tab` is public
domain, already on every Unix, and holds 312 coordinates. Four capitals that
tzdata folds into a neighbour's zone — Amsterdam and the Nordic ones — are
given explicit coordinates rather than approximated by the neighbour, which
would have been several degrees out.

**The map was the easy part, as predicted, but not the way it was drawn.**
Hand-drawing it by eye scored 46% — that is, over half of real places fell in
the sea. The fix was to write the coastlines as longitude ranges per latitude
band, so they can be checked against an atlas instead of counted in characters,
and then to score the result against every coordinate in `zone1970.tab`. It is
now 86%, and every remaining miss is an island smaller than the five degrees of
longitude one column covers.

**What is honestly not solved: locating an address.** A hop is placed when its
reverse DNS carries a city name or an airport code. That is a convention, not a
measurement — a router called `lon` is *said* to be in London by whoever named
it, and one with no reverse DNS is not placed at all. The script says which
hops it placed and which it did not, rather than guessing. Real geolocation
still means a real database, and that remains a thing this does not do.

Two small extras that would be worth having: a `--demo` route exists so the map
can be seen working when a real path's routers happen to be anonymous, and the
six-letter backbone codes (`londen`, `frnkge`) are only partly covered.

### A display layer — built

`mods/console` now exists, so the vi, the most and the monitor are no longer
blocked on it. It was called `screen` until that turned out to shadow
`/usr/bin/screen` — a module's builtins become commands — and it is the
*console*, a text display. What it offers other modules is the **display**
interface in `mods/display.h`, which a framebuffer or SDL backend could offer
equally well without the tools noticing. It owns the terminal: alternate screen, a cell grid with two
buffers and a redraw that emits only the difference, panes, colour, and keys
decoded into names. 2095 bytes to paint an empty eighty by twenty-four screen,
8 bytes to change one character on it, and nothing at all for a flush with
nothing new.

What it deliberately does not do is in
[0019](adr/0019-the-console-display-assumes-xterm.md): no terminfo, no ncurses.
The guide is [full-screen programs](display.md), the demo is
`examples/console-demo.hibr`.

Not there yet, and worth adding when something needs it: a scrolling region,
so a pager can move a screenful without repainting it; and z-ordering for
panes, which nothing has asked for.

### A system monitor worth looking at

`btop`, but better looking and more useful.

What it needs:

- **A screen layer.** The line editor can move a cursor and knows how wide the
  terminal is, but there is no full-screen surface — no alternate screen, no
  region that redraws without flicker, no layout. That is the actual missing
  piece, and it is reusable: anything full-screen needs it.
- **Reading the numbers.** `/proc` is text, and hibr parses text in-process
  without forking — `str`, `match` and the map model are enough for `stat`,
  `meminfo`, `diskstats` and per-process `status`. This part suits the shell
  unusually well.
- **Drawing.** Braille or block-glyph plots, which are arithmetic and a lookup
  table.
- **Staying cheap.** A monitor that samples every second must not fork, or it
  is worse than the thing it replaces. This is the argument for doing it here
  rather than in a script.

The display layer above is built, so this is now reading `/proc` and drawing.

### A vi

Modal, and actually vi — but with the arrow keys working, and the rest of what
thirty years added: undo that goes back more than once, visual selection,
incremental search with highlight, unlimited line length, UTF-8 that is right.

- **The buffer.** A piece table or a gap buffer, not an array of lines. `str`
  and `vec` and the arena allocator are the right primitives, and the
  no-fixed-sizes rule means the answer to a 2 GB file is the same as to a
  20-byte one.
- **Undo.** The thing vi clones get wrong. Record edits, not snapshots, and
  decide early whether undo is linear or a tree — retrofitting a tree is a
  rewrite.
- **Modes and the key map.** A table, and it should be reachable from the shell
  so a `.hibrc` can rebind without a recompile.
- **What it must not become.** Not a second shell. It should call back into
  hibr for `!` and `:r !cmd` rather than growing its own way to run things.

Big, and the most interesting of these. Everything except the buffer and the
undo model is the screen layer.

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
