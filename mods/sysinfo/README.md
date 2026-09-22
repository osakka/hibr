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

There are three: Debian, Alpine, and hibr's own, which is the fallback for
everything else. Adding one is a table entry and an array of lines; they are
padded to the widest line at draw time, so they need not be a rectangle.
