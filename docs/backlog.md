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

### A desktop on the console — five steps of six built

Draggable, closable, minimisable windows on a text terminal, with apps
written as hibr functions. [Decision 0020](adr/0020-windows-are-drawn-not-composited.md)
records the design and the build order; [the guide](desktop.md) is how to use
it.

Built: stacking and hit testing in the console (`console pane raise|lower|drop|list`
and `console hit row col`), the window manager itself
(`examples/desktop.hibr`, a script), a session that opens three windows on it
(`examples/desktop-session.hibr`), and 34 tests driving both through a pty.
Dragging, focus, minimise, zoom, close, tab cycling, and keys and clicks
reaching the focused app all work.

Step 4 is built too: `examples/apps/calc.hibr` and `examples/apps/files.hibr`,
each also a program on its own. The wheel now goes to the window under the
pointer, and a click is reported in the coordinates the app draws in.

Step 5 is built: `examples/apps/panel.hibr`, which changes the theme, the
wallpaper and the refresh rate and manages the other windows. The window
manager grew a small surface for it — `dt_ids`, `dt_title`, `dt_hidden`,
`dt_raise` — so that an app managing other windows never reads `DT`.

And a menu bar, System 7's: the hibr menu on the left, the active
application's own menus beside it, the clock and the application menu on the
right. An app declares menus with `<app>_menus`, the same prefix contract it
declares `_draw` through. F10 or escape opens the bar; there are no modifier
shortcuts, because ctrl collides with everything a terminal window will need.

**Left, on the roadmap:** the terminal emulator module, so a hibr can run
inside a hibr window. Half of that is built — `mods/pty/` opens the terminal
and runs the program — and what remains is turning the escape sequences that
come back into cells. It is also what would let the test harness be hibr
rather than Python, see below.

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

---

[← documentation index](README.md)
