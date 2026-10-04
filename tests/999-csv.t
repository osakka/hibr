# The csv module: RFC 4180 records read whole into a map (by position, or
# by the header's names), streamed a record at a time, split from text and
# written with only the quoting a field needs. Recorded: bash has no csv.
mod load ./build/mods/csv.so
d=$(mktemp -d)
printf '%s\r\n' 'name,city,"note, with comma"' 'Ada,London,"said ""hi"""' > "$d/a.csv"
printf '%s\n' 'Omar,"Cairo' 'Egypt",plain' >> "$d/a.csv"
r := csv read "$d/a.csv"
echo "${#r[@]} records, ${#r[0][@]} fields"
echo "[${r[1][2]}]"
echo "[${r[2][1]}]"
r := csv read -H "$d/a.csv"
echo "${#r[@]} with a header: ${r[0]["name"]} of ${r[0]["city"]}, ${r[1]["note, with comma"]}"
h := csv open "$d/a.csv"
while x := csv row "$h"; do echo "row: ${#x[@]} fields, first ${x[0]}"; done
csv close "$h"
csv read "$d/a.csv"
csv line a "b,c" 'd"e' ''
csv line -s ';' 'x;y' z
csv line -s tab "a	b" c
x := csv split '1,"2,3",,4'
echo "${#x[@]}: ${x[1]} [${x[2]}] ${x[3]}"
printf '\357\273\277a;b\r\n1;2\r\n' > "$d/s.csv"
r := csv read -s ';' "$d/s.csv"
echo "bom skipped: [${r[0][0]}] ${r[1][1]}"
r := csv read -t ''
echo "empty: ${#r[@]}"
r := csv read -t 'one'
echo "no newline: ${r[0][0]}"
csv read 2> /dev/null; echo "no file: $?"
csv read "$d/missing.csv" 2> /dev/null; echo "missing: $?"
csv row 9 2> /dev/null; echo "bad handle: $?"
rm -rf "$d"
