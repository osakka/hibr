# mods/trace

A traceroute that needs no privileges, and puts its hops in a map rather than
only on the screen.

    trace 1.1.1.1
    trace -q -m 20 example.com     # quiet: fill the map, print nothing
    echo "${TRACE[3]["ip"]} ${TRACE[3]["rtt"]}ms ${TRACE[3]["host"]}"

## How it gets the hops without root

The obvious way needs a raw socket to read ICMP, which needs root or
`CAP_NET_RAW`. This does not do that.

Instead it sends a UDP datagram to an unlikely port with the hop limit set to
1, 2, 3 and so on, and asks the kernel for the ICMP complaints that come back
by setting `IP_RECVERR` on the socket. The router that drops the packet says
so, the kernel files that on the socket's error queue, and `recvmsg` with
`MSG_ERRQUEUE` collects it along with the address of whoever complained. No
privilege of any kind is involved. `tracepath` works the same way.

The cost is that `IP_RECVERR` and `MSG_ERRQUEUE` are Linux's. On anything else
the builtin loads and refuses, saying why, rather than pretending.

| | |
|---|---|
| `-m hops` | how far to go, default 24 |
| `-w ms` | how long to wait for each hop, default 1000 |
| `-v name` | fill this map instead of `TRACE` |
| `-q` | fill the map and print nothing |
| `-n` | skip reverse DNS |

`TRACE[0]` holds `target`, `ip` and `hops`; `TRACE[1]` upward hold `ip`, `rtt`
and `host` per hop. A hop that did not answer has an empty `ip`, which is not
the same as one that does not exist.

Use quoted subscripts — `${TRACE[0]["hops"]}` — since an unquoted bare name is
looked up as a variable first, and a `hops` variable of your own would silently
redirect the subscript. See
[0006](../../docs/adr/0006-arrays-are-sparse-maps.md).

## The map

`examples/traceroute.hibr` draws the route on a world map. The drawing is a
shell script rather than part of this module on purpose: the module's job is
sockets, and everything above it is text handling that hibr does in-process
anyway.

Places come from `/usr/share/zoneinfo/zone1970.tab` — public domain, already on
every Unix, 312 coordinates — so there is no geolocation database to ship, pay
for, or watch go stale. A hop is placed when its reverse DNS carries a city
name or an airport code, which is how backbone routers are usually named.

**What that does not do is locate an address.** A router with no reverse DNS,
or one named after equipment rather than a city, is not placed and is listed as
unplaced. Naming is a convention, not a fact: a router called `lon` is *said*
to be in London by whoever named it. This is a picture of how the network
describes itself, not a measurement of where anything is, and the script says
which hops it placed and which it did not for exactly that reason.

The land mask is checked rather than drawn by eye: every coordinate in
`zone1970.tab` is projected onto it, and 86% land on land. The rest are islands
smaller than the five degrees of longitude one column covers.

## Testing

`tests/660-trace.t` covers the argument handling and the unresolvable case,
which need no network. There is no test of a real trace, because a test that
depends on the internet being up and on which routers answer is a test that
fails for reasons that are not about this code.
