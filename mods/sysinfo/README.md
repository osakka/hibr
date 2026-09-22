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

Fourteen: Arch, Ubuntu, Fedora, Mint, openSUSE, Gentoo, NixOS, Void, RHEL and
CentOS, FreeBSD, Debian and Raspbian, Alpine, CIX, and hibr's own.

**A host shows its own.** The picture follows `ID` in `/etc/os-release`, and
when that is a name with no picture, each word of `ID_LIKE` in turn — which is
how Mint gets Ubuntu's, Manjaro and EndeavourOS get Arch's, Rocky and AlmaLinux
get RHEL's, and Raspbian gets Debian's, without any of them needing one of
their own. hibr's own picture is the fallback for a system that says nothing
either way.

`-l name` forces one, which exists so the other thirteen can be tested on a
machine that is only ever one distribution. It is not a normal way to run it:
a CIX picture beside `OS: Debian GNU/Linux` is not a thing that should happen.

`tests/sysinfo-id.c` checks the identification directly — 21 cases including
every derivative above and three that should fall through to hibr — because
that logic is what makes a host show the right picture and it is easy to break
quietly.

A picture is an array of lines, padded to the widest at draw time, so it need
not be a rectangle. Two tones are available: a line may contain `\001` and
`\002` to switch between them, which is the same idea as neofetch's `$1` and
`$2` and lets a logo have an inner shape in a second colour. The marks take no
columns and never reach the output; a picture with none is drawn entirely in
the first tone.

Width is counted in **columns, not bytes**. Several of these are block-drawing
glyphs at three bytes each, so measuring with `strlen` would pad every line to
a third of where it belongs and leave the text column ragged.
