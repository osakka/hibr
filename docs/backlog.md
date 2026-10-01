# Backlog

Wanted, not yet built. Open items in [CLAUDE.md](../CLAUDE.md) are things known
about the code as it stands; this is what should exist next. Each entry says
what it needs, because "easy?" is usually answered by the part nobody thought
about.

## Modules

### List what is installed, not only what is loaded — built

`mod avail` (and `mod list -a`) lists the module directory itself, each
module's name, version, ABI and builtins, marking the ones already loaded —
`dlopen`ing each candidate to reach its `hibr_module` descriptor without
calling its init, refusing one whose ABI does not match.

### A bare command name autoloads its module — built

A second, different sense of "autoloading" from the interface-based one
below: this one starts from a command someone actually typed, not an
interface a tool asked for. `command_not_found` in `.hibrc` (interactive
only — a script still needs its own `need`) calls the new
`mod find <builtin>`, which walks the module path the same way
`hibr_require`/`need` already do but matches a candidate's `bi[]` table by
name instead of its declared interface, loading the first one that
registers it. `deploy.sh` writes the function into a new install's starter
`.hibrc` by default, so `console key`/`img draw`/anything a module
registers works without an explicit `mod load` first — the same shape
bash's own `command_not_found_handle` is, and, deliberately, only reachable
*after* PATH and every builtin/function has already refused the name, so it
can never shadow a real program the way checking modules before PATH would.
See [`mod find`](builtins.md#modules) and
[the interactive guide](interactive.md#starting-up).

### Move json (and other language-adjacent core) to a module?

Raised once the autoloader above existed: if a command autoloads its module
for free now, why keep `json.c` (parse/serialize, ~460 lines, the `json`
builtin) linked into every `hibr` binary rather than `mods/json.so`, loaded
on first use like `console` or `img`?

The autoloader does not actually make this free, and that is the answer for
now, not just a caveat. `command_not_found` lives in `.hibrc`, which is
never read by a script (`rc_load` is gated on `isatty(0)`) — only by a human
typing at a prompt. `json` is used from scripts, not typed interactively: a
grep across `examples/` and `docs/` found 9 files calling `json
parse`/`get`/`set`/`type` directly, none of them with a `need json` guard,
because it is core today. Moving it to a module on the strength of an
interactive-only convenience would break all nine silently.

Deeper than that: `json.c` does not own its data. It reads and writes
`ent->ty` (`J_STR`/`J_NUM`/`J_ARR`/…), the same field ordinary nested-map
assignment already threads through `var.c` and `expand.c` regardless of
whether `json.c` is ever linked. Moving the file would only relocate the
parse/serialize *command*; "nested maps carry JSON type fidelity" is core
infrastructure independent of it, and has to stay core.

If this is worth doing anyway, it needs a fallback that is not
interactive-only — something the core shell itself tries on any
command-not-found, script or interactive, before giving up, not a
`.hibrc`-defined function. That is a materially bigger, separate design
(effectively "checks modules" is `command_not_found`'s *own* fallback
built into the shell rather than opt-in) and has not been scoped. Same
question applies to `net.c`, `text.c`, `args.c` — see CLAUDE.md's own
"Open items" — this ticket is about `json.c` specifically because it is the
one raised so far.

### Loading by path — already works

`mod load ./build/mods/ls.so` and `mod load /usr/local/lib/hibr/sys.so` both
work today; a name with a `/` in it is opened directly and never searched for.
Only a bare name goes through the search path. Nothing to do, recorded so the
question is not asked twice.

### A database module — ultra small, not exhaustive

Raised with a concrete starting point: a pasted ~300-line C sketch of a
column-store analytical engine -- mmap'd row groups (fixed `id`/`val`
columns, 1024 rows each), a min/max zone map per group so a query can skip
a whole group without scanning it, and branch-light filter loops
(`vector_filter_gt_int32`, `vector_filter_lt_double_sel`) meant to let the
compiler auto-vectorize. The shape is sound and genuinely small -- no SQL,
no indexes beyond the zone maps, no transactions, no update/delete, single
writer assumed throughout. That is the right size for hibr; nothing here
should grow it.

What the sketch does not answer, and a real `mods/db.c` needs to before
it is more than a demo:

- **The fixed `VECTOR_SIZE=1024` array inside every row group** is a
  data-sized array, not a chunk size for allocation -- exactly what "No
  static buffer sizes" (CLAUDE.md) rules out elsewhere in this codebase.
  `HIBR_IOCH`-style reasoning (a constant *chunk* size that dynamic
  allocation is grown in) might justify it as "how many rows an mmap'd
  group holds before a new one starts," the same way a `str`'s own growth
  chunk is fixed -- but that is a design call to make on purpose, not
  inherit from the sketch because it was already there.
- **There is no real write path.** `db_bulk_populate` fabricates
  `rand()` data for a demo; an actual module needs `db insert <handle>
  <id> <val>` (or similar) appending a real row, growing the mmap
  (`ftruncate` + re-`mmap`, or a fixed max reserved up front -- another
  decision, not a default) when the current group fills.
- **The query surface is two hardcoded comparisons** (`id > x`, `val <
  y`). A builtin needs *some* general shape -- even if it is just
  `db query <handle> gt id <n> lt val <n>`, composeable predicates, not
  a single hardcoded pipeline -- without turning into a query language.
- **Schema is exactly two columns, int32 and double, and nothing else.**
  Fine for v1 if said out loud; hibr's own nested maps already carry JSON
  type fidelity (`ty` on `ent`), which is the obvious model to grow into
  if a second column type is ever wanted, rather than inventing a
  different one.
- **mmap with `MAP_SHARED` and `msync` is the entire durability story.**
  No crash recovery, no locking against a second writer -- worth stating
  plainly in the module's own README rather than discovering it, matching
  "be honest about limitations... state costs of design choices plainly."

Shape it as a normal reference module (`mods/db.c`, `hibr_bi db_bi[]`,
`HIBR_MODULE`), builtins along the lines of `db create|open|close|insert|
query|size`, values in and out through `$RET`/`ret` like every other
builtin here. Whoever picks this up should read `mods/README.md`'s
file-by-file breakdown first and follow an existing module's own shape
(`mods/mon/` is the closest existing thing -- small, single-purpose,
reads a fixed structure fast) rather than growing this one feature by
feature into something bigger than "ultra small" meant.

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

Also dealt with: `tcc` does not build on Darwin at all, and `CC = tcc` in the
Makefile is a plain assignment, which overrides make's own built-in `CC=cc`
regardless of platform -- so a bare `make` on Darwin tried tcc anyway rather
than falling back. The default is now conditional on `$(origin CC)` being
`default`: only make's own fallback gets replaced, by platform, so `make
CC=gcc` or `CC=gcc` in the environment still wins over either default, on
either platform, exactly as before.

**The rest of this section was rewritten once real hardware was actually
available (v0.23/0.24) — everything above this line was reasoned through
only; everything below was found, fixed and mostly verified live on a real
Mac (Apple Silicon, confirmed via `brew list` showing `/opt/homebrew/...`
paths).**

Dealt with, verified live: a terminal answering a fresh window's own
`TIOCGWINSZ` with 0x0 for a moment read as no terminal at all and fell back
to a fixed size that nothing then corrected -- `cn_size`'s retry budget
widened from 200ms to 1s, *and*, since a real Mac still stayed black
indefinitely (confirmed: waiting alone never revealed it, only an actual
resize did, meaning the terminal was never going to self-correct no matter
how long the wait), `dt_run` now forces one `console reassert` on its own
first idle tick regardless of whether the reported size even changed --
covers both a stale-size theory and a terminal-render-lag theory at once,
since only one of them is fixed by comparing sizes.

Dealt with, verified live: `libpng`/`libssl` were `dlopen`ed by bare Linux
`.so` names only, so wallpaper decoding and TLS silently never worked on
macOS at all. Both gain macOS paths by full Homebrew keg-only path (checked
against a real `brew list libpng openssl@3`: `libpng16.dylib` is genuinely
a symlink to the real `libpng16.16.dylib`, so the guessed name resolves).
`libssl` deliberately gets no bare `.dylib` fallback -- confirmed against
Apple's own developer forums that the system's unversioned copy hard-aborts
third-party code that loads it, "invalid dylib load", not a graceful
failure. Both also gained a negative cache: previously a missing library
was rediscovered-and-failed on every call, which for `libpng` meant three
failed `dlopen`s and a log line on every single desktop frame once a
wallpaper was set. The Homebrew formula (this repo's copy and the live
`osakka/homebrew-hibr` one) now `depends_on` both on macOS.

Dealt with, verified live (`mod load` succeeds, its Mach symbols resolve):
`mods/darwin.c`, a Darwin-only module (`ifeq ($(UNAME),Darwin)` in the
Makefile, not `#ifdef`, so nothing on Linux ever tries to compile it) --
`cpu` and `mem` from one `host_statistics(64)` call each, no fork. Task
Manager's system-wide meters were never implemented on macOS at all
(a placeholder returning 0); About hibr's own `top -l 2 -n 0` blocked the
whole single-threaded draw loop for about a second every three, a second,
separate, real source of the reported choppiness, quite apart from
`libpng`'s own per-frame retries. `cpu` deliberately returns raw counters,
not a percentage -- the previous reading belongs to whichever caller is
asking, and About and Task Manager can both be open at once, each on its
own throttle. Confirmed live: the reported CPU and memory percentages
themselves are correct, not just that the calls succeed.

Dealt with, verified live: `/usr/share/zoneinfo/zone1970.tab` (the Date &
Time Control Panel pane's map, and `examples/traceroute.hibr`) reported
missing on a real Mac -- `/usr/share/zoneinfo` there is usually a symlink
chain down through a version-stamped `/var/db/timezone/...` and apparently
does not always resolve. Both now check `$TZDIR`, then the standard path,
then the direct macOS path, and fail silently (no map mark, rather than a
shell error printed at every desktop startup) if none exist.

Dealt with, not yet verified live: `sysinfo` had no Darwin picture at all
(no `/etc/os-release` there, so `id`/`like` stayed empty and it always fell
back to hibr's own logo) -- a bitten-apple ASCII picture and an explicit
`uname`-based fallback (`id = "darwin"` when `/etc/os-release` is absent and
`uname`'s own `sysname` says `Darwin`) are added, checked only by eye on
this Linux machine with `sysinfo -l darwin`.

Still open: `mods/prompt`'s own `/proc/meminfo`/`/proc/loadavg` reads have
no Darwin equivalent and should report nothing rather than a wrong number.
Job control and the pty line editor are untested rather than known broken.
The window title-bar centring bug (buttons pushed a centred title
off-centre, reported from real use) and the redraw-skip slider's own
0-vs-1 base (also reported from real use) were both real bugs, now fixed,
not Darwin-specific -- listed in [CLAUDE.md](../CLAUDE.md)'s own traps, not
here, since neither is actually a port issue.

## Wanted

### A desktop on the console — five steps of six built

Draggable, closable, minimisable windows on a text terminal, with apps
written as hibr functions. [Decision 0020](adr/0020-windows-are-drawn-not-composited.md)
records the design and the build order; [the guide](../examples/desktop/README.md) is how to use
it.

Built: stacking and hit testing in the console (`console pane raise|lower|drop|list`
and `console hit row col`), the window manager itself
(`examples/desktop/desktop.hibr`, a script), a session that opens three windows on it
(`examples/desktop/session.hibr`), and 34 tests driving both through a pty.
Dragging, focus, minimise, zoom, close, tab cycling, and keys and clicks
reaching the focused app all work.

Step 4 is built too: `examples/desktop/desk-accessories/calc.hibr` and `examples/desktop/apps/files.hibr`,
each also a program on its own. The wheel now goes to the window under the
pointer, and a click is reported in the coordinates the app draws in.

Step 5 is built: `examples/desktop/apps/panel.hibr`, which changes the theme, the
wallpaper and the refresh rate. The window manager grew a small surface for
apps that manage other windows — `dt_ids`, `dt_title`, `dt_hidden`,
`dt_raise` — so that one never reads `DT` directly.

And a menu bar, System 7's: the hibr menu on the left, the active
application's own menus beside it, the clock and the application menu on the
right. An app declares menus with `<app>_menus`, the same prefix contract it
declares `_draw` through. F10 or escape opens the bar; there are no modifier
shortcuts, because ctrl collides with everything a terminal window will need.

**The terminal is built.** `mods/term/` parses what a program writes into
cells and paints them into a window, and `examples/desktop/apps/term.hibr` is a shell
in a window. Every window is its own pty and its own session, so two
terminals are two shells; `tests/apps.py` checks that typing in one does not
reach the other. It is also what would let the test harness be hibr rather
than Python, see below.

**And three games**, because they were asked for and because each one tests
something the other apps do not: `snake`, `mines` and `bricks` in
`examples/desktop/apps/`. Two of them animate, which is what `$EPOCHREALTIME` (bash
5's, now in the core) and `dt_want` (a window asking for its next frame
sooner, for one frame only) are for. A frame with two windows costs 2.4 ms,
so a game stepping every 60 ms costs about 4% of a core while it is played
and nothing when it is paused, hidden or not focused.

**Those four smaller gaps are closed.** A grow box in the bottom-right corner
resizes a window and clamps at a size the title bar still fits and at the edge
of the screen. Menus have ticks and items that cannot be chosen, and one level
of submenu — the control panel's Theme, Wallpaper and Refresh are now
submenus with a tick against whichever is current, which is a better thing
than the "Next Theme" they replaced. And a Window menu that is always there,
after the application's own, holds Move and Resize: both take the arrows
until enter or escape gives them back, so the desktop can be arranged without
a mouse.

Not planned: transparency, sub-cell placement, or a widget toolkit before
three apps have wanted the same widget.

### Arithmetic and quoted subscripts

`$(( m["key"] ))` cannot work: the argument to `$(( ))` and `(( ))` is
expanded with quote removal before the evaluator sees it, so the quotes are
gone and the subscript is evaluated. `let 'm["key"] = 1'` does work, because
its argument is a string the shell never unquotes.

Fixing it means carrying a quote mask alongside the arithmetic text the way
words already carry one — `xone_q` exists, and `struct ax` would need the mask
plus each name's offset into the original. It is tractable and it is not
small. Until then the workaround is one line: read the field into a variable
and use that.

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

**The map was not the useful part, and the owner said so.** On a network whose
carriers publish no reverse DNS there is nothing to place, and what is left is
a traceroute with an empty map. `trace -l` is the answer: loss, last, average,
best, worst and jitter per hop with a round-trip history, updating while you
watch, and needing no geolocation to be worth looking at. It is what `mtr` does
that `traceroute` does not.

Two things about its probing are not obvious and each looks right alone. A
round sends every hop limit rather than doing one at a time, because the
timeout multiplied by the hops is twenty seconds a round; but sent as a burst,
routers rate limit their ICMP and the queueing lands in the timings — the same
hop read 35 ms singly and 520 ms in a burst. So the sends are spaced, *and*
replies are collected between them, because timing a reply when the round ends
charges the later hops' wait to the earlier ones and a LAN gateway reads
200 ms.

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
[0019](adr/0019-the-console-display-assumes-xterm.md): no terminfo, no
ncurses. The guide is [full-screen programs](display.md), the demo is
`examples/console-demo.hibr`.

The mouse is decoded too — presses, releases, drags, the wheel, and modifiers,
in the same zero-based coordinates as `put`, so a click is a position. It is
off until a program asks, because reporting takes the terminal's own text
selection away from the person watching. The pager uses it for the wheel.

Not there yet, and worth adding when something needs it: a scrolling region,
so a pager can move a screenful without repainting it; and z-ordering for
panes, which nothing has asked for.

### A system monitor — built

`mods/mon`. Processors with a bar each, memory and swap, network and disk
rates, and the process table sorted by processor or by size. `p` pauses, `m`
sorts by memory, `c` by processor.

Everything in `/proc` is a counter since boot, so a rate is the difference
between two readings over the time between them — which is why the first
reading shows nothing. The trap worth keeping is in `/proc/[pid]/stat`: field
three is the process state and it is a *letter*, so a loop that skips fields by
reading numbers never gets past it and every later field reads zero. That looks
exactly like an idle machine using no memory, rather than like a parsing bug.

Not there: per-process network or disk, a tree view, sending signals, and
temperatures.

### A neofetch — built

`mods/sysinfo`. Operating system, kernel, architecture, uptime, shell,
terminal, processor, memory, disk and load, beside a picture chosen from
`/etc/os-release`.

It follows the cat's rule rather than the monitor's: colour on a terminal and
nothing at all in a pipe, so `sysinfo | mail` sends text. Everything comes from
`/proc`, `uname` and `statvfs`, so it forks for nothing.

Fourteen pictures, and a host shows **its own**: `ID` from `/etc/os-release`,
then each word of `ID_LIKE`, which is how Mint gets Ubuntu's and Rocky gets
RHEL's without needing their own. `tests/sysinfo-id.c` checks that chain
directly over 21 identifications, because it is the part that decides whether
the thing on screen is about the machine it is running on.

### Module autoloading — built

A tool asks for an interface and the shell finds something that offers it.
`hibr_require(s, "display", 1)` with nothing loaded walks the module path,
reads each module's descriptor *without* initialising it, and loads the first
that declares it offers `display`. So `mod load most; something | most` works
with no mention of the console anywhere.

That was the owner's point: being told to `mod load console` when you asked for
a display is two names for what feels like one thing. `mod avail` now shows
what each module offers, and the message when nothing does says that rather
than naming a module that might not be the right one.

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

It is called `hvi`, not `vi`. A module's builtins become commands, and an
editor that is missing counts, `.`, registers and marks should not be the one
that answers when somebody types `vi` out of habit. The rule that came out of
it: **shadow only when the replacement is complete, or when being wrong is
harmless** — which is why the cat still shadows `cat`.

Syntax colouring is the cat's, asked for through the registry as the
**highlight** interface rather than copied, so there is one set of language
tables. hibr has its own entry in them now instead of being treated as `sh`.

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

### A pseudo terminal, so hibr can test itself

**Why the full-screen suites are in Python, and the honest answer to it.**

Most of the suite is already shell: 75 `.t` files compared against bash, and
91 assertions in `tests/self.hibr` written in hibr. What is in Python is the
nine suites that drive a *terminal* — the console, the line editor, the pager,
the editor, the monitor, the traceroute, the cat, the window manager and its
apps — and they are in Python for one reason, which is not a preference:

**hibr cannot open a pseudo terminal.** There is no `posix_openpt`, no
`forkpty`, no `/dev/ptmx` and no `TIOCSWINSZ` anywhere in the source. A
harness has to be the controlling process of a pty: fork a child onto the
slave, set its window size, write keystrokes to the master and read back what
was drawn. Nothing in hibr can do any of that, so the harness cannot be hibr.

**Half of it now exists.** `mods/pty/` opens a pseudo terminal, starts a
program on it with a session and a controlling terminal of its own, and lets
a script write keys in, read output out, resize, signal and collect the exit
status — and `tests/750-pty.t` is a *shell* test of terminal handling, run by
`tests/run.sh` like anything else. What is left before the suites can move is
the harness itself: a `Screen` equivalent in hibr, and the nine suites
rewritten against it.

That capability is **already required** by the last step of
[decision 0020](adr/0020-windows-are-drawn-not-composited.md): a hibr running
inside a hibr window needs exactly a pty and a child on it. So `mods/term/`
gets built anyway, and the half of it that opens the pty — `pty spawn`,
`pty write`, `pty read`, `pty resize` — is the half the test harness needs.
When it lands, `tests/screen.py` can be `tests/screen.hibr`, and the shell
will test its own terminal programs.

Two things should stay independent of hibr even then, and this is a reason
rather than an excuse: `tests/diff.py` and `tests/fuzz.py` exist to *find*
hibr bugs by generating input and comparing against bash. A generator written
in the shell under test cannot be trusted to report that shell's failure — a
broken `$RANDOM` or a broken comparison would make a silent generator look
like a clean shell, which is a mistake this project has already made once and
recorded in `CLAUDE.md`. They could be hibr; they should not be.


## Testing

`tests/affected.py`, `tests/census.py`, `tests/uifuzz.py` and `tests/asan.py`
shipped in 0.50; see `tests/README.md`. What they left:

### Fuzz the apps that act on the machine

`uifuzz` leaves out Task Manager, Files, Terminal and the Date & Time pane,
because random input into them signals real processes, moves real files,
types into a real shell and runs sudo. They are where fuzzing would find the
most. Doing it needs a sandbox that makes all four harmless -- its own pid
namespace, a filesystem it cannot damage, no sudo -- and that is a decision
about what the test machine allows, to be made before it is built.

## The language

## For language models

Raised directly: what would make hibr the shell a language model reaches
for? Models write what their training data is full of, which is bash and
Python, and nothing in this repository changes that on its own. What hibr
can do is run the bash models already write *better* than bash does, and be
quick to learn from a short text when a model is handed one. These are
ordered by leverage. Each is measurable the same way: give a model the
reference and a set of tasks, and count the scripts that run correctly on
the first try, before and after.

### Measure the reference, and check the other pages the same way — built

In 0.63. `tests/531-doc-examples.t` runs every example in `docs/llm.md`,
`docs/cookbook.md` and `docs/data.md` on every build. `tools/llm-measure/`
holds thirteen tasks and a scorer, and its first run
(`runs/2026-10-01/RESULTS.md`) took Haiku 4.5 from 7/12 trap and 10/18
feature tasks without the page to all of them with it, and Sonnet 5.5 from
9/12 and 5/18 to all of them, three runs a cell, with ordinary bash never
once broken. Three of the four gaps it found were in the page and are
fixed. What is left: a fresh set of tasks, since the fixes were made from
these; and the one failure no page can prevent -- every script written
without the page read `BASH_REMATCH`, 6 of 6, and got nothing. Filling
`BASH_REMATCH` as well as `M` would make that work; ADR 0004 chose `M`
alone, so it is a decision, not a fix.

### A linter for the mistakes models make — built

`hibr --explain script`, in 0.59, with its ten rules in `mods/lint/`. The
`set -e`-in-a-condition rule was left out: hibr's errexit already reaches
inside those functions (ADR 0002), so there is nothing to warn about. What
could come next: a rule for a quoted `"$@"` missing in a wrapper, and
knowing that `opt ... int=` declares a number, which `test-unquoted` would
then pass.

### A safety net for commands an agent runs — dry run built

`hibr --plan` shipped in 0.66 (ADR 0027): the script's own logic runs, and
every write outside a scratch `$TMPDIR`, connection and program not known to
only read is refused and listed. Known gaps, by design: `awk` and every
program that runs another are refused rather than looked through, and a
refused program fails, so a plan stops where the script needed its result.
What remains of the item below is the policy that runs for real; a shell-only
version is a guard against mistakes, and a real boundary needs Landlock
(Linux) or a sandbox profile (macOS) on a machine that has them -- this one's
kernel has Landlock off.

The original item: a dry run, `hibr --plan script`, that runs nothing destructive and lists
what would have been touched -- files written, removed or moved, commands
spawned, hosts reached -- by intercepting the builtins and redirections
that do it and refusing `exec` of anything not known to be read-only. And a
policy file (`HIBR_POLICY`) the shell enforces while running for real: paths
outside a root are refused, named commands are refused, network is off.
Agent harnesses build this today out of wrappers around bash; in the shell
itself it cannot be walked around with a subshell. Hard parts, stated
plainly: a refused `exec` breaks most real scripts' plans, and a policy is
only as good as the list of what can write -- `/dev/tcp`, `>`, `mv`, a
module's builtins -- so every writing path has to be named and tested.

### An MCP server

A module, `mods/mcp/`, that serves hibr over the Model Context Protocol on
stdio: a persistent session (variables and functions survive between
calls), a `run` tool returning status, stdout, stderr and -- when a script
ends with `ret` or `json` -- a structured value, since nested maps and JSON
with type fidelity are exactly what a tool result wants. Agent mode is on
inside it. Claude Code and other clients could then call hibr directly
rather than through a generic shell tool. Needs: the JSON-RPC framing, the
tool schemas, and a decision about how long a session lives.

### Stay a drop-in for bash

Every place hibr differs from bash is a place a model's bash breaks, and a
model that has been burned once stops reaching for it. `tests/corpus.py` is
the instrument: 7 scripts of 465 invocations still differ (see `CLAUDE.md`'s
open items). Keep driving it to zero for anything not deliberately
different. The largest known one -- bash runs a script a command at a time
as it reads it, where hibr parsed the whole script first -- is gone in
0.60 (ADR 0026). And make each deliberate difference loud: a one-line
warning in agent mode the first time a script depends on bash behaviour hibr does
not have, rather than a silently different result.

### Be where models look

Packages in apt, nix and the other package managers beside the existing
Homebrew tap; many small, real, runnable examples in public repositories
and in the places answers are written; the reference above published at a
stable URL. The slowest of these and the only one that changes what a
model has seen, so it is worth starting early and doing steadily rather
than all at once.

---

[← documentation index](README.md)
