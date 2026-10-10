# `json emit`, `keys`, `len` and `type` fill the result slot and stay quiet
# while one is bound, the way `json get` always has (Gitea #167). Recorded:
# json is hibr's own.
#
# Every check reads the slot *and* what was printed, because the bug was both
# halves at once -- the variable came back empty and the document went to
# standard output, which in a full-screen program is the middle of the
# screen. One verb passing says nothing about the other three, so each is
# asked separately.
d=$(mktemp -d)
declare -gA M
M["a"]["b"]=1
M["a"]["c"]=2

j := json emit M > "$d/out"
echo "emit: bound=[$j] printed=[$(cat "$d/out")]"

k := json keys M . > "$d/out"
echo "keys: bound=[$k] printed=[$(cat "$d/out")]"

x := json keys M .a named > "$d/out"
echo "keys with a name: bound=[$x] named=[$named] printed=[$(cat "$d/out")]"

n := json len M .a > "$d/out"
echo "len: bound=[$n] printed=[$(cat "$d/out")]"

t := json type M .a > "$d/out"
echo "type: bound=[$t] printed=[$(cat "$d/out")]"

# The bound value is the form that was asked for, so a script writing a file
# gets the indented one.
p := json emit M -p > "$d/out"
echo "emit -p bound a first line of: [$(printf '%s\n' "$p" | head -1)]"

echo "--- and with no slot bound, each prints exactly as it always did:"
json emit M
json keys M .
json len M .a
json type M .a
rm -rf "$d"
