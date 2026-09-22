# mods/sysinfo

What this machine is, with a picture.

    sysinfo
    sysinfo -p       # no colour, whatever the output is

The operating system, kernel, architecture, uptime, shell, terminal, processor,
memory, disk and load, beside a picture chosen from `/etc/os-release`.

## The same rule as the cat

Colour and the picture's tint appear when standard output is a terminal, and
nothing at all when it is a pipe — so `sysinfo | mail` sends text, not escapes.
`-p` turns it off on a terminal too. `tests/700-sysinfo.t` counts the escapes
in a pipe and expects zero.

## Where the numbers come from

`/etc/os-release` for the name and the picture, `uname` for the kernel and the
architecture, `/proc/uptime`, `/proc/cpuinfo`, `/proc/meminfo`, `statvfs` on
`/`, and `/proc/loadavg`. Nothing is shelled out to, so it costs no forks.

The test checks the kernel release and the architecture against `uname` itself
rather than against a recorded string, because those are the two that would
quietly go stale.

## Pictures

Four: Debian, Alpine, CIX, and hibr's own, which is the fallback for anything
else. `-l name` picks one by hand, which is also how they are tested on a
machine that is only ever one distribution.

A picture is an array of lines, padded to the widest at draw time, so it need
not be a rectangle. Two tones are available: a line may contain `\001` and
`\002` to switch between them, which is the same idea as neofetch's `$1` and
`$2` and lets a logo have an inner shape in a second colour. The marks take no
columns and never reach the output — a picture with none is drawn entirely in
the first tone, which is what the older ones expect.

Width is counted in **columns, not bytes**: these are block-drawing glyphs at
three bytes each, so measuring with `strlen` would pad every line to a third of
where it should be and leave the text column ragged.
