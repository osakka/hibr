# mods/mon

What the machine is doing, while it does it.

    mon              # a reading a second
    mon -d 0.5       # twice a second
    mon -m           # sorted by memory

Processors with a bar each, memory and swap, network and disk rates, and every
process with how much of a processor it has used since the last reading —
sorted heaviest first, or by size with `m`. `p` pauses, `q` leaves.

| file | role |
|---|---|
| `mn.h` | one reading of the machine |
| `sample.c` | reading `/proc`, and turning counters into rates |
| `mon.c` | bars, histories and the loop |

## Everything in /proc is a counter

Nothing there reports a rate. `/proc/stat` says how many ticks each processor
has spent in each state since boot; `/proc/net/dev` says how many bytes an
interface has carried. A rate is the difference between two readings divided by
the time between them, which is why the first reading shows nothing and the
second shows everything.

**The field offsets in `/proc/[pid]/stat` are the trap.** Field three is the
process state and it is a *letter*, so a loop that skips fields by reading
numbers never gets past it and every later field is wrong — quietly, as zero.
Every process reporting 0.0% and 0 bytes is what that looks like. The order is:
skip the state, then ten numbers, then `utime` and `stime`, then eight more,
then `rss` in pages.

The other trap is the command name. It is in brackets and may itself contain
brackets and spaces, so it is found from the *last* `)` in the line, never the
first.

## Testing

`tests/mon.py`, through a pseudo terminal. The checks that matter compare
against `/proc` directly rather than against a remembered number: one bar per
processor the kernel reports, a memory total that matches `MemTotal`, and
process rows whose pids are real directories under `/proc` and which come out
in order. Values that move between one reading and the next are not asserted
on, because they move.
