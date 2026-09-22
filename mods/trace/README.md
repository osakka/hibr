# mods/trace

A traceroute that needs no privileges, and puts its hops in a map rather than
only on the screen.

    trace 1.1.1.1                  # once, like traceroute
    trace -l 1.1.1.1               # keep going, like mtr
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
| `-l` | keep probing and watch it live; needs a display |
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

## Live

`-l` keeps probing and draws what a single traceroute cannot show: how much is
being *lost*, and how much the timing *moves*.

```
 1.1.1.1 (1.1.1.1)   11 rounds   q quit  p pause  r reset
 Hop  Address           Loss   Snt   Last    Avg   Best   Wrst   Jttr  History
   1  san-cr-2.uk.home.   0.0%    11    0.4    0.6    0.2    3.6    0.7 ▁▁▁▁▁▁▁▁▁▁▁
   3  gw.uk.home.arpa     0.0%    11    1.6    1.4    0.9    2.9    0.6 ▁▁▁▁▁▁▁▁▁▁▁
   5  172.20.37.1        18.2%    11   23.3   29.4   21.5   42.8   12.1 ▃▃▅▃▄▃▃▅▃
  11  141.101.71.101     54.5%    11   28.2   44.4   26.2   71.9   32.2 █▄▃█▄
  14  141.101.71.121      0.0%    11   33.7   34.2   27.3   49.0    8.6 ▄▅▄▄▅▄▆▄▄▄▄
```

A hop that never answers is a row of `???` at 100% loss, which is normal —
plenty of routers decline to send ICMP — and is not the same as a hop that
answers sometimes. The history is the last forty round-trips as block heights,
coloured by the same scale as the latency columns. `p` pauses, `r` starts the
counting again, `q` leaves.

**Two things about the probing are not obvious and are worth keeping.** A round
sends every hop limit rather than doing one hop at a time, because the timeout
multiplied by the number of hops is far too slow to watch — twenty seconds a
round instead of one. But sending them as one burst makes routers rate limit
their ICMP, and that queueing lands in the timings: the same hop read 35 ms
probed singly and 520 ms in a burst. So the sends are spaced about twelve
milliseconds apart, *and replies are collected in between* — because timing a
reply when the round ends rather than when it arrives charges the wait for the
later hops to the earlier ones, and a LAN gateway then reads two hundred
milliseconds. Both halves are needed; either alone is wrong.

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
which need no network. `tests/mtr.py` drives the live mode through a pseudo
terminal against the loopback address — one hop, always reachable, always
fast. Neither tests a real trace across the internet, because a test that
depends on the network being up and on which routers answer is a test that
fails for reasons that are not about this code.
