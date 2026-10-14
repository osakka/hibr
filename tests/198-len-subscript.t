# `${#name}` counts characters, which ADR 0021 says every length and slice
# in this shell does, and `${#name[sub]}` counted **bytes** -- so a length
# was right for a plain variable and wrong for the same text held in a map,
# which is where the desktop keeps all of its own (an app's state lives
# under its window id by rule, so 81 places measure a subscripted value).
# bash counts characters for both, which is what this compares against; the
# nested-map form is hibr's own and is asserted in tests/self.hibr.
#
# The tell that it was one missing call rather than a design question: the
# plain form, `str len` and **slicing a subscripted value** were all already
# right -- only the length of a subscripted value fell through to `strlen`
# (Gitea #226).
declare -A m
m["k"]="日本語"
m["mixed"]="ab漢cd"
m["dash-key"]="日本"
m["empty"]=""
a=("日本語" "ab")
x="日本語"

echo "plain ${#x}"
echo "map ${#m["k"]}"
echo "mixed ${#m["mixed"]}"
echo "dashed ${#m["dash-key"]}"
echo "empty ${#m["empty"]}"
echo "missing ${#m["nope"]}"
echo "index ${#a[0]} ${#a[1]}"
# A slice of the same value has always counted characters: the two agreeing
# is the point, since a length that disagrees with its own slice is what
# makes a cursor land off the end.
echo "slice ${m["k"]:0:2} ${x:0:2}"
# `[@]` and `[*]` are counts of entries, not lengths, and must not move.
echo "count ${#m[@]} ${#a[@]}"
