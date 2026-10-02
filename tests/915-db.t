# The db module: a small column store. Recorded, because every answer is
# the module's own.
mod load ./build/mods/db.so
H=$PWD/build/hibr
D=$(mktemp -d)
trap 'rm -rf "$D"' EXIT

echo "--- create, insert, describe"
h := db create "$D/s.db" ts:int host:str:8 load:float
db insert $h 1 web1 0.5
db insert $h 2 web2 2.75
db insert $h 3 web1 3.1
db insert $h -4 db1 -1e3
db cols $h
db size $h

echo "--- query: every row, then filters, and a limit"
db query $h
db query $h where load gt 1
db query $h where host eq web1 and load lt 1
db query $h where host ge web limit 2
db query $h where host eq nobody; echo "none: $?"

echo "--- aggregates"
db count $h
db count $h where host eq web1
db sum $h ts
db sum $h load
db avg $h load where host eq web1
db min $h ts
db max $h host
db min $h load where host eq nobody; echo "min of nothing: $?"
db sum $h load where host eq nobody

echo "--- into a map with :=, types kept"
r := db query $h where ts eq 2
echo "${r[0][host]} ${r[0][load]} ${#r[@]}"
json emit r
n := db count $h
echo "count by := is $n"

echo "--- refused, each with a reason"
db insert $h x web1 1 2>&1
db insert $h 5 averyveryverylongname 1 2>&1
db insert $h 5 web1 2>&1 | sed "s|$D|D|"
db query $h where nope eq 1 2>&1
db query $h where ts like 1 2>&1
db sum $h host 2>&1
db create "$D/s.db" a:int 2>&1 | sed "s|$D|D|"
db create "$D/t.db" a:blob 2>&1
printf 'not a database at all, but long enough to read a header\n' > "$D/x.db"
db open "$D/x.db" 2>&1 | sed "s|$D|D|"
db query 99 2>&1

echo "--- closed and opened again, it is all still there"
db close $h
h := db open "$D/s.db"
db size $h
db query $h where ts lt 0

echo "--- a thousand rows to a group: growing past one, and import"
g := db create "$D/g.db" n:int sq:int tag:str
i=0
while [ $i -lt 2500 ]; do
	printf '%d\t%d\t%s\n' $i $((i * i)) t$((i % 3))
	i=$((i + 1))
done > "$D/rows.tsv"
printf 'oops\tnot\n' >> "$D/rows.tsv"
db import $g "$D/rows.tsv" 2>&1
db size $g
db query $g where n ge 2498
db count $g where tag eq t1
db sum $g n
db close $g

echo "--- record numbers, counted from 1, and starting from one"
g := db open "$D/g.db"
db query $g -n from 1024 limit 2
r := db query $g -n where n gt 2497
echo "record ${r[0]["#"]} holds n=${r[0]["n"]}, ${#r[@]} rows"
db count $g from 2001
db close $g

echo "--- a filter skips the row groups its zone maps rule out"
$H -d 3 -c "mod load ./build/mods/db.so; g := db open '$D/g.db'; db count \$g where n gt 2100" 2>&1 |
	grep -o 'groups skipped by their zones' | head -1
$H -d 3 -c "mod load ./build/mods/db.so; g := db open '$D/g.db'; db count \$g where n gt 2100" 2>&1 |
	grep -o '[0-9]* of [0-9]* groups skipped'
