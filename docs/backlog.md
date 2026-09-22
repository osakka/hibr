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

## Wanted

### A visual traceroute with a world map

Trace a route and draw it on an ASCII map, each hop plotted, the line coloured
by latency.

What it needs, in order of how much it bites:

- **Getting the hops at all.** Classic traceroute sends UDP with a rising TTL
  and reads ICMP time-exceeded replies, and reading ICMP needs a raw socket,
  which needs root or `CAP_NET_RAW`. The shell can already drop privileges
  ([0016](adr/0016-privileges-are-dropped-never-gained.md)), so a module could
  open the socket while still root and drop immediately after — the same shape
  as binding a low port. The alternative is shelling out to `traceroute`, which
  is against the point of an in-process module.
- **Setting TTL per packet.** `setsockopt(IP_TTL)` is not reachable from the
  language today. A module can call it directly on a descriptor the shell
  owns, so this is a few lines, but it is a real gap worth its own builtin.
- **Turning an address into a place.** A map needs coordinates, which means a
  geo database on disk. That is data, not code, and it dates. Offline
  MaxMind-style lookup is a file format to parse; the alternative is a network
  service, which makes a traceroute tool depend on the network being up.
- **The map itself.** The easy part. A coarse ASCII projection is a lookup
  table, and the colour work is already done — the prompt module has a style
  language that turns names into ANSI.

So: the drawing is an afternoon, the privilege and geo parts are the project.

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

Start with the screen layer, and make it a module that other full-screen tools
can use — a monitor is then the first thing built on it, not the point of it.

---

[← documentation index](README.md)
