# db — a small column store

`db` keeps rows of typed columns in one file, appends to it, changes and
deletes rows in place, and answers filters and aggregates over it quickly,
without SQL. It is deliberately small: no joins, no indexes beyond its zone
maps, one writer. For a few million rows of measurements, events or logs a script
collects and asks about, it is the right size; for anything more, use a
database.

```text
mod load db                     # or need db
h := db create stats.db ts:int host:str:16 load:float
db insert $h 1727800000 web1 0.42
db import $h rows.tsv           # one row a line, tab-separated
db query $h where load gt 2.5 and host eq web1 limit 10
db count $h where host eq web1
db avg $h load where ts ge 1727800000
r := db query $h where host eq web2     # a map: ${r[0][load]}, types kept
db set $h 2 load 1.5                    # record 2, by the number -n shows
db update $h where host eq web1 set load 0
db delete $h where load gt 100          # or db delete $h 3 7, by number
db compact $h                           # drop deleted rows, renumbering
db close $h
```

## Columns

| type | holds | width |
|---|---|---|
| `int` | a 64-bit integer | 8 bytes |
| `float` | a double | 8 bytes |
| `str` or `str:N` | text up to N bytes (32 unless given, at most 4096) | N bytes |

A value that is not of its column's type, or a string longer than its
column, is refused with the reason, and nothing of that row is written.

## Commands

| command | does |
|---|---|
| `db create file col:type...` | make a new database (never over an existing file); gives a handle |
| `db open file` | open one; gives a handle |
| `db insert h val...` | append a row, values in column order; with `:=` gives its record number |
| `db import h file` | append every line of a tab-separated file (`-` for standard input); gives how many, and fails if any line was skipped |
| `db query h [-n] [where col op val [and ...]] [limit n] [from n]` | the matching rows: printed tab-separated, or with `:=` a map, `r[i][col]`, numbers kept as numbers. `-n` adds each row's record number, counted from 1, first (in a map, `r[i][#]`); `from n` starts at record n, skipping the groups before it unread |
| `db count h [where ...] [from n]` | how many rows match |
| `db sum\|avg h col [where ...]` | over an `int` or `float` column |
| `db min\|max h col [where ...]` | any column; a `str` is ordered byte by byte |
| `db set h N col val...` | change record N -- the number `query -n` gives it -- in the columns named; every value is checked before any is written |
| `db update h [where ...] set col val...` | change every row that matches; gives how many |
| `db delete h N...` | delete records by number; gives how many |
| `db delete h where ...` | delete every row that matches; a `where` is required, so nothing deletes every row by accident |
| `db compact h` | write the file again without its deleted rows, renumbering the rest; gives how many it kept |
| `db size h` | how many rows, not counting deleted ones |
| `db cols h` | the columns, as `name:type` |
| `db flush h` | write it to disk now |
| `db close h` | write it back and close it |

The operators are `eq ne lt le gt ge`; predicates join with `and`. A count
or a sum over no rows is 0; a minimum, maximum or average of none is
nothing, with status 1, as is a query that matches nothing.

## How it is stored

One file: a header, the schema, then row groups of 1024 rows. A group keeps
a mark for each of its rows that is deleted (128 bytes), then each column's
values together, after a zone map of that column's smallest and largest
value in the group. A query first asks each group's zones
whether any row in it could match, and skips the group without reading a
row when none can -- so a filter on something that grows with the data, a
timestamp or an id, reads only the groups where it can be true. The file
grows a group at a time as rows are added, and is mapped with `mmap`.

The 1024 is how many rows a group holds before the next begins, the way a
string grows in fixed chunks; a group past the last row is simply unused.

Every value has a fixed width at a fixed place, so `set` and `update` write
it where it is, and widen the group's zone if the new value lies outside
it. A zone only has to bound what the group holds, so a wider one is still
right -- a filter may read a group it could have skipped, never miss a row.
`delete` sets a row's mark and leaves the zones alone for the same reason;
every scan, count and aggregate passes over marked rows. A record keeps its
number until `compact`, which writes the live rows into a new file beside
the old, renames it over it, and so renumbers them -- the only thing that
does. A file made before 0.99.6 (`HIBRDB1`) has no marks: it reads and
updates as it is, and its first `delete` writes it again beside itself
with them (`HIBRDB2`), keeping every record's number. Run that while no
other handle has the same file open -- see locking below.

Measured on this machine, one million rows of an int, an int and a 4-byte
string, with the default tcc build: importing a 16 MB tab-separated file,
374 ms; a count whose filter must read every group, 31 ms; one whose zones
rule out all but ten groups, under a millisecond; 20 MB on disk.

## What it does not do

- **No crash recovery and no locking.** Rows reach the file through
  `MAP_SHARED`; `flush` and `close` `msync` them. A crash can lose rows
  added since the last of those, and two writers on one file will corrupt
  it -- nothing stops a second.
- **No undo.** An update or a delete is written at once; `compact` makes
  deletion permanent and renumbers.
- **Not portable between machines of different byte order**: numbers are
  stored as the machine holds them.
- **No query language.** A predicate is a column, an operator and a value;
  anything more is the script's job, with the rows `db query` gives it.
