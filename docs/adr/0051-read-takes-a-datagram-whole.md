# 0051 — read takes a datagram whole

Status: accepted (0.99.404)

## Context

A read of a datagram socket -- `/dev/udp`, or a socket `listen -u` made --
empties one whole datagram, however few bytes it asks for; the rest is
dropped by the kernel. The `read` builtin read anything it could not seek
a byte at a time, so on such a socket it kept the first byte of a reply and
lost the rest, and `read -N 12` then waited for a datagram that never came.
Found by a DNS query written in hibr, whose reply came back as one byte.

bash reads `-N` in blocks, so `-N` works there; a line and `-n` it reads a
byte at a time, and on the same socket it keeps only `h` of
`hello world\n`.

## Decision

`read` reads a datagram socket a datagram at a time (`bi_rddgram`) and then
takes bytes from it as it always has: up to the delimiter, or up to the
count, across as many datagrams as that needs. What follows the delimiter or
the count inside one datagram is lost, as it is in bash -- a datagram cannot
be put back. Pipes, terminals and stream sockets still go a byte at a time,
since a byte the next command wants must not be taken.

`-N` therefore matches bash. A line and `-n` deliberately do not: they get
the line where bash gets its first byte, because copying bash here would be
copying the loss of data.

## Consequences

One `getsockopt` per `read` of a descriptor that cannot be seeked, to ask
whether it is a datagram socket (`net_isdgram`, which `recv` already used);
a pipe pays that call once per `read`, not per byte.
`tests/222-read-udp.t` records the behaviour, and fails against 0.99.403.
