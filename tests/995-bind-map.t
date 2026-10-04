# := with a subscripted target takes a result that is a map whole: the map
# filed under the key, at any depth, its numbers kept as numbers; a scalar
# result still binds as a scalar, and a quoted key stays literal. db query
# gives the map. Recorded: bash has no :=.
mod load ./build/mods/db.so
d=$(mktemp -d)
h := db create "$d/t.db" name:str:16 n:int
db insert "$h" alpha 1 > /dev/null
db insert "$h" beta 22 > /dev/null
declare -gA m
m["rows"] := db query "$h"
echo "${m["rows"][0]["name"]} ${m["rows"][1]["n"]}"
json emit m
i=3
m[$i]["deep"] := db query "$h" where n gt 5
echo "deeper: ${m[3]["deep"][0]["name"]}, ${#m[3]["deep"][@]} row"
m[3]["deep"] := db query "$h"
echo "a map bound over a map: ${#m[3]["deep"][@]} rows, ${m[3]["deep"][0]["name"]} first"
s() { ret plain; }
m["a-b"] := s
echo "scalar under a quoted key: ${m["a-b"]}"
m["rows"] := s
echo "a map entry bound again to a scalar: [${m["rows"]}] [${m["rows"][0]["name"]}]"
db close "$h"
rm -rf "$d"
